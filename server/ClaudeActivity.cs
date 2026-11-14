namespace AtomGotchi.Api;

// Agrega la actividad de todas las sesiones de Claude Code (vía hooks) en un único
// estado que el Atom puede pintar: idle|alert|work|wait|done|error|bored|sleep.
public sealed class ClaudeActivity
{
    // Duración de los estados transitorios antes de volver a idle
    static readonly TimeSpan AlertFor = TimeSpan.FromSeconds(2.5);
    static readonly TimeSpan DoneFor  = TimeSpan.FromSeconds(6);
    static readonly TimeSpan ErrorFor = TimeSpan.FromSeconds(4);
    // Una sesión "trabajando" sin eventos tanto tiempo se da por colgada (p. ej. Esc)
    static readonly TimeSpan WorkStale = TimeSpan.FromMinutes(5);
    static readonly TimeSpan BoredAfter = TimeSpan.FromMinutes(10);
    static readonly TimeSpan SleepAfter = TimeSpan.FromMinutes(30);

    record Session(string State, DateTime Since);

    readonly object _lock = new();
    readonly Dictionary<string, Session> _sessions = new();
    DateTime _lastActivity = DateTime.UtcNow;
    readonly Func<DateTime> _now;

    public ClaudeActivity() : this(() => DateTime.UtcNow) { }
    public ClaudeActivity(Func<DateTime> now) { _now = now; _lastActivity = now(); }

    public void Apply(string eventName, string? sessionId)
    {
        var id = string.IsNullOrEmpty(sessionId) ? "default" : sessionId;
        var now = _now();
        lock (_lock)
        {
            _lastActivity = now;
            switch (eventName)
            {
                case "SessionStart":       _sessions[id] = new("alert", now); break;
                case "UserPromptSubmit":
                case "PreToolUse":
                case "PostToolUse":
                case "SubagentStop":
                case "PreCompact":         _sessions[id] = new("work", now); break;
                case "Notification":       _sessions[id] = new("wait", now); break;
                case "Stop":               _sessions[id] = new("done", now); break;
                case "PostToolUseFailure": _sessions[id] = new("error", now); break;
                case "SessionEnd":         _sessions.Remove(id); break;
            }
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
            _       => s.State,   // wait persiste hasta el siguiente evento
        };
    }

    static int Priority(string s) => s switch
    {
        "wait" => 5, "work" => 4, "error" => 3, "done" => 2, "alert" => 1, _ => 0,
    };

    public (string State, int Sessions) Snapshot()
    {
        var now = _now();
        lock (_lock)
        {
            var best = "idle";
            foreach (var s in _sessions.Values)
            {
                var e = Effective(s, now);
                if (Priority(e) > Priority(best)) best = e;
            }
            if (best == "idle")
            {
                var quiet = now - _lastActivity;
                best = quiet > SleepAfter ? "sleep" : quiet > BoredAfter ? "bored" : "idle";
            }
            return (best, _sessions.Count);
        }
    }
}
