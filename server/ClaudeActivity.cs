namespace AtomGotchi.Api;

// Una sesión de Claude Code tal y como la ve quien elige la sesión activa (GET /api/sessions).
public record SessionInfo(string Id, string Name, string State, string Tool, long DoneCount, int IdleSeconds, bool Active);

// Lo que recibe el Atom en cada consulta (GET /api/claude): ya incluye qué sesión sigue y si hay atención pendiente.
public record ClaudeSnapshot(
    string State,       // idle|alert|work|wait|done|error|bored|sleep
    string Tool,        // read|search|edit|bash|web|agent|other|"" (solo en work)
    int Mood,           // -2..2 (humor acumulado)
    bool Attention,     // una tarea terminó y nadie la ha confirmado todavía
    long AttentionSeq,  // sube con cada tarea terminada relevante
    string? ActiveId,   // sesión seguida; null = automático (agregado de todas)
    int Sessions);

// Agrega la actividad de todas las sesiones de Claude Code (vía hooks) en un único
// estado que el Atom puede pintar, más "memoria" (rachas, errores) para el humor.
public sealed class ClaudeActivity
{
    // Duración de los estados transitorios antes de volver a idle
    static readonly TimeSpan AlertFor = TimeSpan.FromSeconds(2.5);
    static readonly TimeSpan DoneFor  = TimeSpan.FromSeconds(6);
    static readonly TimeSpan ErrorFor = TimeSpan.FromSeconds(4);
    // Una sesión "trabajando" sin eventos tanto tiempo se da por colgada (p. ej. Esc)
    static readonly TimeSpan WorkStale = TimeSpan.FromMinutes(5);
    // Una sesión esperando permiso que no da señales tanto tiempo se da por abandonada (terminal cerrado)
    static readonly TimeSpan WaitStale = TimeSpan.FromMinutes(20);
    // Sesiones sin eventos durante tanto tiempo se olvidan (si no llegó SessionEnd)
    static readonly TimeSpan ForgetAfter = TimeSpan.FromHours(2);
    static readonly TimeSpan BoredAfter = TimeSpan.FromMinutes(10);
    static readonly TimeSpan SleepAfter = TimeSpan.FromMinutes(30);
    // Memoria del humor
    static readonly TimeSpan ErrorMemory = TimeSpan.FromMinutes(10);

    record Session(string State, DateTime Since, string Project, string Tool, long Done = 0, DateTime Born = default);

    readonly object _lock = new();
    readonly Dictionary<string, Session> _sessions = new();
    readonly Func<DateTime> _now;
    DateTime _lastActivity;

    string? _activeId;           // sesión que sigue el gotchi (null = automático)
    long _attnSeq, _ackSeq;      // atención pendiente = _attnSeq > _ackSeq
    int _tasksToday;
    DateTime _day;
    int _errorStreak;
    DateTime _lastError = DateTime.MinValue;

    public ClaudeActivity() : this(() => DateTime.UtcNow) { }
    public ClaudeActivity(Func<DateTime> now) { _now = now; _lastActivity = now(); _day = now().Date; }

    public static string ProjectFromCwd(string? cwd)
    {
        if (string.IsNullOrWhiteSpace(cwd)) return "";
        var trimmed = cwd.TrimEnd('/', '\\');
        var i = Math.Max(trimmed.LastIndexOf('/'), trimmed.LastIndexOf('\\'));
        return i >= 0 ? trimmed[(i + 1)..] : trimmed;
    }

    public static string ToolCategory(string? tool) => tool switch
    {
        "Read" or "LS" => "read",
        "Grep" or "Glob" => "search",
        "Edit" or "MultiEdit" or "Write" or "NotebookEdit" => "edit",
        "Bash" or "BashOutput" => "bash",
        "WebFetch" or "WebSearch" => "web",
        "Task" or "Agent" => "agent",
        null or "" => "other",
        _ => "other",
    };

    public void Apply(string eventName, string? sessionId, string? cwd = null, string? toolName = null)
    {
        var id = string.IsNullOrEmpty(sessionId) ? "default" : sessionId;
        var now = _now();
        lock (_lock)
        {
            _lastActivity = now;
            if (now.Date != _day) { _day = now.Date; _tasksToday = 0; }

            _sessions.TryGetValue(id, out var prev);
            var project = ProjectFromCwd(cwd);
            if (project == "") project = prev?.Project ?? "";
            var tool = ToolCategory(toolName);

            switch (eventName)
            {
                case "SessionStart": _sessions[id] = new("alert", now, project, "", prev?.Done ?? 0); break;
                case "UserPromptSubmit":
                case "SubagentStop":
                case "PreCompact":   _sessions[id] = new("work", now, project, prev?.Tool ?? "other", prev?.Done ?? 0); break;
                case "PreToolUse":
                case "PostToolUse":  _sessions[id] = new("work", now, project, tool, prev?.Done ?? 0); break;
                case "Notification": _sessions[id] = new("wait", now, project, "", prev?.Done ?? 0); break;
                case "Stop":
                    _sessions[id] = new("done", now, project, "", (prev?.Done ?? 0) + 1);
                    _tasksToday++; _errorStreak = 0;
                    if (_activeId == null || _activeId == id) _attnSeq++;   // solo avisa la sesión que sigues (o cualquiera en automático)
                    break;
                case "PostToolUseFailure":
                    _sessions[id] = new("error", now, project, "", prev?.Done ?? 0);
                    _errorStreak++; _lastError = now;
                    break;
                case "SessionEnd": _sessions.Remove(id); if (_activeId == id) _activeId = null; break;
            }
            // El orden de la lista debe ser estable: por momento de aparición de la sesión
            if (_sessions.TryGetValue(id, out var cur)) _sessions[id] = cur with { Born = prev?.Born ?? now };
        }
    }

    string Effective(Session s, DateTime now)
    {
        var age = now - s.Since;
        return s.State switch
        {
            "alert" => age > AlertFor ? "idle" : "alert",
            "done"  => age > DoneFor  ? "idle" : "done",
            "error" => age > ErrorFor ? "idle" : "error",
            "work"  => age > WorkStale ? "idle" : "work",
            "wait"  => age > WaitStale ? "idle" : "wait",
            _       => s.State,   // wait persiste hasta el siguiente evento
        };
    }

    static int Priority(string s) => s switch
    {
        "wait" => 5, "work" => 4, "error" => 3, "done" => 2, "alert" => 1, _ => 0,
    };

    int ComputeMood(DateTime now)
    {
        var recentError = now - _lastError < ErrorMemory;
        if (recentError && _errorStreak >= 3) return -2;
        if (recentError && _errorStreak >= 1) return -1;
        if (_tasksToday >= 6) return 2;
        if (_tasksToday >= 2) return 1;
        return 0;
    }

    // Nombres para humanos: proyecto, con sufijo " #n" si hay varias sesiones del mismo proyecto.
    Dictionary<string, string> Names()
    {
        var ordered = _sessions.OrderBy(kv => kv.Value.Born).ThenBy(kv => kv.Key).ToList();
        var total = ordered.GroupBy(kv => kv.Value.Project).ToDictionary(g => g.Key, g => g.Count());
        var seen = new Dictionary<string, int>();
        var names = new Dictionary<string, string>();
        foreach (var kv in ordered)
        {
            var name = kv.Value.Project == "" ? "sesion" : kv.Value.Project;
            if (total[kv.Value.Project] > 1)
            {
                seen[kv.Value.Project] = seen.GetValueOrDefault(kv.Value.Project) + 1;
                name += " #" + seen[kv.Value.Project];
            }
            names[kv.Key] = name;
        }
        return names;
    }

    // Sesiones actuales (orden estable por aparición) y la activa.
    public (string? Active, IReadOnlyList<SessionInfo> Sessions) ListSessions()
    {
        var now = _now();
        lock (_lock)
        {
            Prune(now);
            var names = Names();
            var list = _sessions.OrderBy(kv => kv.Value.Born).ThenBy(kv => kv.Key).Select(kv =>
            {
                var eff = Effective(kv.Value, now);
                return new SessionInfo(kv.Key, names[kv.Key], eff, eff == "work" ? kv.Value.Tool : "", kv.Value.Done,
                                       (int)(now - kv.Value.Since).TotalSeconds, kv.Key == _activeId);
            }).ToList();
            return (_activeId, list);
        }
    }

    // Elige la sesión a seguir: id completo, prefijo único del id, o nombre ("web #2"). null/""/"auto" = automático.
    public (bool Ok, string? Active, string? Error) SetActive(string? idOrName)
    {
        lock (_lock)
        {
            if (string.IsNullOrWhiteSpace(idOrName) || idOrName.Equals("auto", StringComparison.OrdinalIgnoreCase))
            {
                _activeId = null; _ackSeq = _attnSeq;   // al cambiar de sesión, no arrastrar avisos de la anterior
                return (true, null, null);
            }
            var names = Names();
            var matches = _sessions.Keys.Where(k => k == idOrName).ToList();
            if (matches.Count == 0) matches = _sessions.Keys.Where(k => k.StartsWith(idOrName, StringComparison.OrdinalIgnoreCase)).ToList();
            if (matches.Count == 0) matches = names.Where(kv => kv.Value.Equals(idOrName, StringComparison.OrdinalIgnoreCase)).Select(kv => kv.Key).ToList();
            if (matches.Count == 0) return (false, _activeId, "sesión no encontrada");
            if (matches.Count > 1) return (false, _activeId, "ambiguo: varias sesiones coinciden");
            _activeId = matches[0]; _ackSeq = _attnSeq;
            return (true, _activeId, null);
        }
    }

    public void Ack() { lock (_lock) _ackSeq = _attnSeq; }

    void Prune(DateTime now)
    {
        foreach (var k in _sessions.Where(kv => now - kv.Value.Since > ForgetAfter).Select(kv => kv.Key).ToList())
        {
            _sessions.Remove(k);
            if (_activeId == k) _activeId = null;
        }
    }

    public ClaudeSnapshot Snapshot()
    {
        var now = _now();
        lock (_lock)
        {
            Prune(now);
            var best = "idle"; var tool = "";
            if (_activeId != null && _sessions.TryGetValue(_activeId, out var act))
            {
                // Sigues una sesión: su estado manda, salvo que otra esté esperando algo de ti
                best = Effective(act, now); tool = best == "work" ? act.Tool : "";
                foreach (var kv in _sessions)
                    if (kv.Key != _activeId && Effective(kv.Value, now) == "wait") { best = "wait"; tool = ""; }
            }
            else
            {
                string? bestProject = null;
                foreach (var s in _sessions.Values)
                {
                    var e = Effective(s, now);
                    if (Priority(e) > Priority(best) || (Priority(e) == Priority(best) && bestProject == null))
                    {
                        best = e; bestProject = s.Project; tool = e == "work" ? s.Tool : "";
                    }
                }
            }
            if (best == "idle")
            {
                var quiet = now - _lastActivity;
                best = quiet > SleepAfter ? "sleep" : quiet > BoredAfter ? "bored" : "idle";
            }
            if (now.Date != _day) { _day = now.Date; _tasksToday = 0; }
            return new ClaudeSnapshot(best, tool, ComputeMood(now), _attnSeq > _ackSeq, _attnSeq, _activeId, _sessions.Count);
        }
    }
}
