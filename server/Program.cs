using AtomGotchi.Api;

var builder = WebApplication.CreateBuilder(args);
builder.Services.AddSingleton<ClaudeActivity>();

var app = builder.Build();

// Los hooks de Claude Code hacen POST con su JSON (hook_event_name, session_id, ...)
app.MapPost("/api/hook", (System.Text.Json.JsonElement body, ClaudeActivity activity) =>
{
    string? Get(string k) => body.ValueKind == System.Text.Json.JsonValueKind.Object
        && body.TryGetProperty(k, out var v) && v.ValueKind == System.Text.Json.JsonValueKind.String ? v.GetString() : null;
    var ev = Get("hook_event_name");
    if (ev is null) return Results.BadRequest(new { error = "falta hook_event_name" });
    activity.Apply(ev, Get("session_id"));
    return Results.Ok();
});

// El Atom hace GET cada ~1 s y recibe una sola palabra: idle|alert|work|wait|done|error|bored|sleep
app.MapGet("/api/claude", (ClaudeActivity activity) => Results.Text(activity.Snapshot().State));

// Versión JSON para depurar
app.MapGet("/api/claude/json", (ClaudeActivity activity) =>
{
    var (state, sessions) = activity.Snapshot();
    return Results.Ok(new { state, sessions });
});

app.Run();
