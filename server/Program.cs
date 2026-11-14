using AtomGotchi.Api;

var builder = WebApplication.CreateBuilder(args);
builder.Services.AddSingleton<ClaudeActivity>();

var app = builder.Build();

// Los hooks de Claude Code hacen POST con su JSON (hook_event_name, session_id, cwd, tool_name, ...)
app.MapPost("/api/hook", (System.Text.Json.JsonElement body, ClaudeActivity activity) =>
{
    string? Get(string k) => body.ValueKind == System.Text.Json.JsonValueKind.Object
        && body.TryGetProperty(k, out var v) && v.ValueKind == System.Text.Json.JsonValueKind.String ? v.GetString() : null;
    var ev = Get("hook_event_name");
    if (ev is null) return Results.BadRequest(new { error = "falta hook_event_name" });
    activity.Apply(ev, Get("session_id"), Get("cwd"), Get("tool_name"));
    return Results.Ok();
});

// El Atom hace GET cada ~1 s. Formato clave=valor por líneas (fácil de parsear sin librerías JSON).
app.MapGet("/api/claude", (ClaudeActivity activity) =>
{
    var s = activity.Snapshot();
    // Los valores no pueden contener saltos de línea
    static string Clean(string v) => v.Replace('\n', ' ').Replace('\r', ' ');
    return Results.Text(
        $"state={s.State}\ntool={s.Tool}\nproject={Clean(s.Project)}\nmood={s.Mood}\n" +
        $"done={s.DoneSeq}\ndoneproject={Clean(s.DoneProject)}\ntasks={s.TasksToday}\nsessions={s.Sessions}\n");
});

// Versión JSON para depurar
app.MapGet("/api/claude/json", (ClaudeActivity activity) => Results.Ok(activity.Snapshot()));

app.Run();
