namespace AtomGotchi.Api;

// Payload que manda el Atom: POST /api/state
// {"mood":N,"hunger":N,"thirst":N,"energy":N,"steps":N,"flags":N}
public record DeviceStateDto(int Mood, int Hunger, int Thirst, int Energy, int Steps, int Flags);

// Estado enriquecido que devuelve la web: GET /api/state/current
public record GotchiStateModel(
    int Mood,
    int Hunger,
    int Thirst,
    int Energy,
    int Steps,
    int Flags,
    DateTime UpdatedAt
);

// Comando que envía la web: POST /api/command
public record CommandDto(string Command);
