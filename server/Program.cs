using System.Security.Cryptography;
using System.Text;
using Microsoft.OpenApi.Models;
using AtomGotchi.Api;

var builder = WebApplication.CreateBuilder(args);
builder.Services.AddSingleton<ClaudeActivity>();
builder.Services.AddEndpointsApiExplorer();
builder.Services.AddSwaggerGen(o =>
{
    o.SwaggerDoc("v1", new OpenApiInfo
    {
        Title = "AtomGotchi API",
        Version = "v1",
        Description = "Muestra en un Atom S3R lo que hace Claude Code. Los hooks de Claude Code envían eventos; el Atom consulta el estado.\n\n" +
                      "Pulsa **Authorize** e introduce la clave de API (cabecera `X-Api-Key`) para poder ejecutar los endpoints.",
    });
    o.AddSecurityDefinition("ApiKey", new OpenApiSecurityScheme
    {
        Type = SecuritySchemeType.ApiKey, In = ParameterLocation.Header, Name = "X-Api-Key",
        Description = "Clave de API (variable API_KEY del servidor)",
    });
    o.AddSecurityRequirement(new OpenApiSecurityRequirement
    {
        [new OpenApiSecurityScheme { Reference = new OpenApiReference { Type = ReferenceType.SecurityScheme, Id = "ApiKey" } }] = Array.Empty<string>(),
    });
});

var app = builder.Build();

// Swagger UI en la raíz del dominio (el JSON y la UI son públicos; ejecutar endpoints exige la clave)
app.UseSwagger();
app.UseSwaggerUI(o => { o.SwaggerEndpoint("/swagger/v1/swagger.json", "AtomGotchi API v1"); o.RoutePrefix = ""; o.DocumentTitle = "AtomGotchi API"; });

// Auth: si el servidor tiene API_KEY, todos los /api/* exigen la cabecera X-Api-Key
var apiKey = app.Configuration["API_KEY"];
if (string.IsNullOrEmpty(apiKey)) app.Logger.LogWarning("API_KEY no configurada: los endpoints /api están SIN autenticación");
var keyBytes = Encoding.UTF8.GetBytes(apiKey ?? "");
app.Use(async (ctx, next) =>
{
    if (keyBytes.Length > 0 && ctx.Request.Path.StartsWithSegments("/api"))
    {
        var given = Encoding.UTF8.GetBytes(ctx.Request.Headers["X-Api-Key"].ToString());
        if (given.Length != keyBytes.Length || !CryptographicOperations.FixedTimeEquals(given, keyBytes))
        {
            ctx.Response.StatusCode = StatusCodes.Status401Unauthorized;
            await ctx.Response.WriteAsJsonAsync(new { error = "falta o es incorrecta la cabecera X-Api-Key" });
            return;
        }
    }
    await next();
});

// ── Claude Code → servidor ───────────────────────────────────────────────────

app.MapPost("/api/hook", (System.Text.Json.JsonElement body, ClaudeActivity activity) =>
{
    string? Get(string k) => body.ValueKind == System.Text.Json.JsonValueKind.Object
        && body.TryGetProperty(k, out var v) && v.ValueKind == System.Text.Json.JsonValueKind.String ? v.GetString() : null;
    var ev = Get("hook_event_name");
    if (ev is null) return Results.BadRequest(new { error = "falta hook_event_name" });
    activity.Apply(ev, Get("session_id"), Get("cwd"), Get("tool_name"));
    return Results.Ok();
}).WithTags("Claude Code").WithSummary("Evento de un hook de Claude Code")
  .WithDescription("Recibe el JSON tal cual lo manda un hook (hook_event_name, session_id, cwd, tool_name…). Lo envía hooks/gotchi-hook.sh.");

// ── Atom ─────────────────────────────────────────────────────────────────────

app.MapGet("/api/claude", (ClaudeActivity activity) =>
{
    var s = activity.Snapshot();
    return Results.Text(
        $"state={s.State}\ntool={s.Tool}\nmood={s.Mood}\nattn={(s.Attention ? 1 : 0)}\nattnseq={s.AttentionSeq}\nsessions={s.Sessions}\n");
}).WithTags("Atom").WithSummary("Estado para el Atom (texto clave=valor)")
  .WithDescription("Lo consulta el Atom cada segundo. Ya incluye qué sesión se sigue y si hay atención pendiente.");

app.MapPost("/api/ack", (ClaudeActivity activity) => { activity.Ack(); return Results.Ok(); })
   .WithTags("Atom").WithSummary("Confirmar la atención pendiente")
   .WithDescription("Lo llama el Atom al pulsar el botón: apaga el parpadeo de \"tarea terminada\".");

app.MapGet("/api/claude/json", (ClaudeActivity activity) => Results.Ok(activity.Snapshot()))
   .WithTags("Atom").WithSummary("Estado completo en JSON (depuración)");

// ── Elegir qué sesión sigue el gotchi ────────────────────────────────────────

app.MapGet("/api/sessions", (ClaudeActivity activity) =>
{
    var (active, sessions) = activity.ListSessions();
    return Results.Ok(new { active, sessions });
}).WithTags("Sesiones").WithSummary("Sesiones activas de Claude Code")
  .WithDescription("Id completo, nombre legible (con #n si el proyecto se repite), estado, herramienta, tareas hechas, segundos sin actividad y cuál es la seguida.");

app.MapGet("/api/active", (ClaudeActivity activity) => Results.Ok(new { active = activity.ListSessions().Active }))
   .WithTags("Sesiones").WithSummary("Sesión que sigue el gotchi (null = automático)");

app.MapPost("/api/active", (ActiveDto dto, ClaudeActivity activity) =>
{
    var r = activity.SetActive(dto.Id);
    return r.Ok ? Results.Ok(new { active = r.Active }) : Results.NotFound(new { error = r.Error, active = r.Active });
}).WithTags("Sesiones").WithSummary("Elegir la sesión a seguir")
  .WithDescription("id = id completo, prefijo del id o nombre (p. ej. \"web #2\"). Con \"auto\" o null sigue todas.");

app.Run();

public record ActiveDto(string? Id);
