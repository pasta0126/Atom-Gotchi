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
// Las sesiones van como "s=<id>|<nombre>|<estado>|<tareas terminadas>|<herramienta>" (máx. 6, orden estable por aparición).
app.MapGet("/api/claude", (ClaudeActivity activity) =>
{
    var s = activity.Snapshot();
    // Los valores no pueden contener saltos de línea ni el separador '|'
    static string Clean(string v, int max = 64)
    {
        var c = v.Replace('\n', ' ').Replace('\r', ' ').Replace('|', '/');
        return c.Length > max ? c[..max] : c;
    }
    var sb = new System.Text.StringBuilder();
    sb.Append($"state={s.State}\ntool={s.Tool}\nproject={Clean(s.Project)}\nmood={s.Mood}\n");
    sb.Append($"done={s.DoneSeq}\ndoneproject={Clean(s.DoneProject)}\ntasks={s.TasksToday}\nsessions={s.Sessions}\n");
    foreach (var x in s.List.Take(6)) sb.Append($"s={Clean(x.Id, 8)}|{Clean(x.Name, 24)}|{x.State}|{x.DoneSeq}|{x.Tool}\n");
    return Results.Text(sb.ToString());
});

// Versión JSON para depurar
app.MapGet("/api/claude/json", (ClaudeActivity activity) => Results.Ok(activity.Snapshot()));

app.Run();
