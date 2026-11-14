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

// ── Atom ─────────────────────────────────────────────────────────────────────

// El Atom hace GET cada ~1 s. Formato clave=valor por líneas (fácil de parsear sin librerías JSON).
// El servidor ya decide qué sesión se sigue y si hay atención pendiente; el Atom solo pinta.
app.MapGet("/api/claude", (ClaudeActivity activity) =>
{
    var s = activity.Snapshot();
    return Results.Text(
        $"state={s.State}\ntool={s.Tool}\nmood={s.Mood}\nattn={(s.Attention ? 1 : 0)}\nattnseq={s.AttentionSeq}\nsessions={s.Sessions}\n");
});

// El Atom confirma (cualquier pulsación) y se apaga el parpadeo de "tarea terminada"
app.MapPost("/api/ack", (ClaudeActivity activity) => { activity.Ack(); return Results.Ok(); });

// ── Elegir qué sesión sigue el gotchi ────────────────────────────────────────

// Sesiones activas (id completo, nombre legible, estado, herramienta, tareas hechas, segundos sin actividad, si es la seguida)
app.MapGet("/api/sessions", (ClaudeActivity activity) =>
{
    var (active, sessions) = activity.ListSessions();
    return Results.Ok(new { active, sessions });
});

app.MapGet("/api/active", (ClaudeActivity activity) => Results.Ok(new { active = activity.ListSessions().Active }));

// {"id": "<id | prefijo del id | nombre>"}  ·  {"id": "auto"} o {"id": null} = seguir todas (automático)
app.MapPost("/api/active", (ActiveDto dto, ClaudeActivity activity) =>
{
    var r = activity.SetActive(dto.Id);
    return r.Ok ? Results.Ok(new { active = r.Active }) : Results.NotFound(new { error = r.Error, active = r.Active });
});

// Versión JSON del estado completo, para depurar
app.MapGet("/api/claude/json", (ClaudeActivity activity) => Results.Ok(activity.Snapshot()));

app.Run();

public record ActiveDto(string? Id);
