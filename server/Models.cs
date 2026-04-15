namespace AtomGotchi.Api;

// Payload que manda el Atom: POST /api/state
// {"mood":N,"steps":N}
public record DeviceStateDto(int Mood, int Steps);

// Estado enriquecido que devuelve la web: GET /api/state/current
public record GotchiStateModel(int Mood, int Steps, DateTime UpdatedAt);

// Comando que envía la web: POST /api/command
public record CommandDto(string Command);
