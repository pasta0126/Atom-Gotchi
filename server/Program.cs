using AtomGotchi.Api;

var builder = WebApplication.CreateBuilder(args);
builder.Services.AddSingleton<GotchiStore>();
builder.Services.AddSingleton<ClaudeActivity>();

var app = builder.Build();
app.UseStaticFiles();

// ── Atom → servidor ───────────────────────────────────────────────────────────

// El Atom hace POST cada 3 s; el servidor aplica decay y devuelve vitals actuales
app.MapPost("/api/state", (DeviceStateDto dto, GotchiStore store) =>
    Results.Ok(store.UpdateState(dto)));

// El Atom hace GET cada 3 s; si hay un comando pendiente lo devuelve y lo borra
app.MapGet("/api/command/next", (GotchiStore store) =>
    Results.Text(store.ConsumeCommand() ?? ""));

// ── Web → servidor ────────────────────────────────────────────────────────────

// La web pide el estado completo para renderizar la UI
app.MapGet("/api/state/current", (GotchiStore store) =>
    store.State is { } s ? Results.Ok(s) : Results.NoContent());

// La web envía un comando; el efecto en vitals es inmediato, la animación se encola para el Atom
app.MapPost("/api/command", (CommandDto dto, GotchiStore store) =>
{
    var valid = new[]
    {
        "feed", "play", "pet", "sleep", "medicine", "clean",
        "shake", "startle", "noise", "restart"
    };
    if (!valid.Contains(dto.Command.ToLowerInvariant()))
        return Results.BadRequest(new { error = "Comando desconocido" });

    store.ExecuteCommand(dto.Command.ToLowerInvariant());
    return Results.Ok();
});

// Cambiar personalidad del Gotchi
app.MapPost("/api/personality", (PersonalityDto dto, GotchiStore store) =>
{
    store.SetPersonality(dto.Type);
    return Results.Ok();
});

// ── Claude Code → servidor → Atom ─────────────────────────────────────────────

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

// Versión JSON para depurar / web
app.MapGet("/api/claude/json", (ClaudeActivity activity) =>
{
    var (state, sessions) = activity.Snapshot();
    return Results.Ok(new { state, sessions });
});

app.MapFallbackToFile("index.html");
app.Run();
