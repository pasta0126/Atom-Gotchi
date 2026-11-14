namespace AtomGotchi.Api;

// Datos que el servidor envía al Atom en cada consulta.
public record SessionInfo(string Id, string Name, string State, long DoneSeq, string Tool);

public record ClaudeSnapshot(
    string State,       // idle|alert|work|wait|done|error|bored|sleep
    string Tool,        // read|search|edit|bash|web|agent|other|"" (solo en work)
    string Project,     // nombre de carpeta del proyecto de la sesión dominante
    int Mood,           // -2..2 (humor acumulado)
    long DoneSeq,       // contador de tareas terminadas (sube con cada Stop)
    string DoneProject, // proyecto de la última tarea terminada
    int TasksToday,
    int Sessions,
    IReadOnlyList<SessionInfo> List);     // sesiones, la más reciente primero

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

    long _doneSeq;
    string _doneProject = "";
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
                    _doneSeq++; _doneProject = project; _tasksToday++; _errorStreak = 0;
                    break;
                case "PostToolUseFailure":
                    _sessions[id] = new("error", now, project, "", prev?.Done ?? 0);
                    _errorStreak++; _lastError = now;
                    break;
                case "SessionEnd": _sessions.Remove(id); break;
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

    // Lista para el selector del Atom: orden estable (por aparición); nombres duplicados con sufijo " #n".
    List<SessionInfo> BuildList(DateTime now)
    {
        var ordered = _sessions.OrderBy(kv => kv.Value.Born).ThenBy(kv => kv.Key).ToList();
        var seen = new Dictionary<string, int>();
        var total = ordered.GroupBy(kv => kv.Value.Project).ToDictionary(g => g.Key, g => g.Count());
        var res = new List<SessionInfo>();
        foreach (var kv in ordered)
        {
            var name = kv.Value.Project == "" ? "sesion" : kv.Value.Project;
            if (total[kv.Value.Project] > 1)
            {
                seen[kv.Value.Project] = seen.GetValueOrDefault(kv.Value.Project) + 1;
                name += " #" + seen[kv.Value.Project];
            }
            var id = kv.Key.Length > 4 ? kv.Key[..4] : kv.Key;
            var eff = Effective(kv.Value, now);
            res.Add(new SessionInfo(id, name, eff, kv.Value.Done, eff == "work" ? kv.Value.Tool : ""));
        }
        return res;
    }

    public ClaudeSnapshot Snapshot()
    {
        var now = _now();
        lock (_lock)
        {
            foreach (var k in _sessions.Where(kv => now - kv.Value.Since > ForgetAfter).Select(kv => kv.Key).ToList())
                _sessions.Remove(k);
            var best = "idle"; var project = ""; var tool = "";
            foreach (var s in _sessions.Values)
            {
                var e = Effective(s, now);
                if (Priority(e) > Priority(best) || (Priority(e) == Priority(best) && project == ""))
                {
                    best = e; project = s.Project; tool = e == "work" ? s.Tool : "";
                }
            }
            var list = BuildList(now);
            if (best == "idle")
            {
                var quiet = now - _lastActivity;
                best = quiet > SleepAfter ? "sleep" : quiet > BoredAfter ? "bored" : "idle";
            }
            if (now.Date != _day) { _day = now.Date; _tasksToday = 0; }
            return new ClaudeSnapshot(best, tool, project, ComputeMood(now), _doneSeq, _doneProject, _tasksToday, _sessions.Count, list);
        }
    }
}
