namespace AtomGotchi.Api;

public enum GotchiPersonality
{
    Feliz    = 0,  // happiness decays 40% slower
    Gloton   = 1,  // hunger decays 60% faster
    Dormilon = 2,  // energy decays 50% faster but recovers faster too
    Activo   = 3,  // all stats decay 25% faster, actions give 20% more
}

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
    DateTime UpdatedAt,
    GotchiPersonality Personality);

// Comando desde la web: POST /api/command
public record CommandDto(string Command);

// Cambiar personalidad: POST /api/personality
public record PersonalityDto(string Type);
