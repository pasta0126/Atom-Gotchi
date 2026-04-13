using AtomGotchi.Api;

var builder = WebApplication.CreateBuilder(args);
builder.Services.AddSingleton<GotchiStore>();

var app = builder.Build();
app.UseStaticFiles();

// ── Atom → servidor ───────────────────────────────────────────────────────────

// El Atom hace POST cada 3 s con su estado actual
app.MapPost("/api/state", (DeviceStateDto dto, GotchiStore store) =>
{
    store.UpdateState(dto);
    return Results.Ok();
});

// El Atom hace GET cada 3 s; si hay un comando pendiente lo devuelve y lo borra
app.MapGet("/api/command/next", (GotchiStore store) =>
    Results.Text(store.ConsumeCommand() ?? ""));

// ── Web → servidor ────────────────────────────────────────────────────────────

// La web pide el estado actual para renderizar la UI
app.MapGet("/api/state/current", (GotchiStore store) =>
    store.State is { } s ? Results.Ok(s) : Results.NoContent());

// La web encola un comando (feed / drink / pet)
app.MapPost("/api/command", (CommandDto dto, GotchiStore store) =>
{
    var valid = new[] { "feed", "drink", "pet" };
    if (!valid.Contains(dto.Command.ToLowerInvariant()))
        return Results.BadRequest(new { error = "Comando desconocido" });

    store.EnqueueCommand(dto.Command.ToLowerInvariant());
    return Results.Ok();
});

// Cualquier otra ruta sirve la SPA
app.MapFallbackToFile("index.html");

app.Run();
