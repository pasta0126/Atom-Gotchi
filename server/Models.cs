namespace AtomGotchi.Api;

// Payload que manda el Atom: POST /api/state
public record DeviceStateDto(int Mood);

// Respuesta del servidor al Atom tras POST /api/state
public record VitalsDto(
    int Hunger, int Happiness, int Energy,
    bool Sick, bool Dead, bool NeedsClean, bool Sleeping);

// Estado completo para la web: GET /api/state/current
public record GotchiStateModel(
    int Mood,
    int Hunger, int Happiness, int Energy,
    bool Sick, bool Dead, bool NeedsClean, bool Sleeping,
    double AgeHours,
    DateTime UpdatedAt);

// Comando desde la web: POST /api/command
public record CommandDto(string Command);
