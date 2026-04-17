namespace AtomGotchi.Api;

public sealed class GotchiStore : IDisposable
{
    // ── Vitals ────────────────────────────────────────────────────────────────
    private double   _hunger          = 80;
    private double   _happiness       = 80;
    private double   _energy          = 80;
    private bool     _sleeping        = false;
    private bool     _nightSleep      = false;  // forzado por horario nocturno
    private bool     _sick            = false;
    private bool     _dead            = false;
    private bool     _needsClean      = false;

    private DateTime _bornAt          = DateTime.UtcNow;
    private DateTime _lastDecay       = DateTime.UtcNow;
    private DateTime _poopedAt        = DateTime.MinValue;
    private DateTime _veryHungryAt    = DateTime.MinValue;
    private DateTime _sickAt          = DateTime.MinValue;
    private double   _nextPoopMinutes;

    // ── Personalidad ──────────────────────────────────────────────────────────
    private GotchiPersonality _personality = GotchiPersonality.Feliz;

    // ── Device state ──────────────────────────────────────────────────────────
    private int      _mood      = 0;
    private DateTime _updatedAt = DateTime.MinValue;

    // ── Command queue ─────────────────────────────────────────────────────────
    private string?  _pendingCommand;

    private readonly object _lock = new();
    private readonly System.Threading.Timer _decayTimer;

    // ── Timezone (Spain) ──────────────────────────────────────────────────────
    private static readonly TimeZoneInfo _tz = GetTz();

    private static TimeZoneInfo GetTz()
    {
        try  { return TimeZoneInfo.FindSystemTimeZoneById("Europe/Madrid"); }
        catch { return TimeZoneInfo.FindSystemTimeZoneById("Romance Standard Time"); }
    }

    // Horas de sueño nocturno (hora local)
    private const int NightStart = 22;
    private const int NightEnd   = 8;

    // ── Decay constants (base) ────────────────────────────────────────────────
    private const double HungerDecayPerMin    = 0.50;
    private const double HappinessDecayPerMin = 0.33;
    private const double EnergyDecayPerMin    = 0.25;
    private const double EnergyRecoverPerMin  = 0.80;
    private const double PoopMinMin           = 30.0;
    private const double PoopMaxMin           = 90.0;
    private const double SickAfterPoopMin     = 15.0;
    private const double SickAfterHungryMin   = 10.0;
    private const double DeadAfterSickMin     = 30.0;

    // ─────────────────────────────────────────────────────────────────────────

    public GotchiStore()
    {
        _nextPoopMinutes = RandPoopInterval();
        _decayTimer = new System.Threading.Timer(
            _ => { lock (_lock) _Decay(); }, null,
            TimeSpan.FromSeconds(10), TimeSpan.FromSeconds(10));
    }

    // ── Public ────────────────────────────────────────────────────────────────

    public GotchiStateModel? State
    {
        get
        {
            lock (_lock)
            {
                if (_updatedAt == DateTime.MinValue) return null;
                return _BuildModel();
            }
        }
    }

    public VitalsDto UpdateState(DeviceStateDto dto)
    {
        lock (_lock)
        {
            _mood      = dto.Mood;
            _updatedAt = DateTime.UtcNow;
            _Decay();
            return _BuildVitals();
        }
    }

    public void ExecuteCommand(string cmd)
    {
        lock (_lock)
        {
            if (_dead && cmd != "restart") return;

            double bonus = _personality == GotchiPersonality.Activo ? 1.20 : 1.0;

            switch (cmd)
            {
                case "feed":
                    double feedBonus = _personality == GotchiPersonality.Gloton ? 1.10 : 1.0;
                    _hunger       = Math.Min(100, _hunger + 40 * bonus * feedBonus);
                    _veryHungryAt = DateTime.MinValue;
                    _pendingCommand = "feed";
                    break;

                case "play":
                    _happiness = Math.Min(100, _happiness + 30 * bonus);
                    _energy    = Math.Max(0,   _energy    - 10);
                    _pendingCommand = "pet";
                    break;

                case "pet":
                    _happiness = Math.Min(100, _happiness + 15 * bonus);
                    _pendingCommand = "pet";
                    break;

                case "sleep":
                    if (!_nightSleep)  // el sueño nocturno no se puede desactivar manualmente
                        _sleeping = !_sleeping;
                    break;

                case "medicine":
                    _sick      = false;
                    _sickAt    = DateTime.MinValue;
                    _happiness = Math.Min(100, _happiness + 10 * bonus);
                    _pendingCommand = "pet";
                    break;

                case "clean":
                    _needsClean      = false;
                    _poopedAt        = DateTime.MinValue;
                    _nextPoopMinutes = RandPoopInterval();
                    _happiness       = Math.Min(100, _happiness + 5 * bonus);
                    _pendingCommand  = "pet";
                    break;

                case "shake":
                    _pendingCommand = "shake";
                    break;

                case "startle":
                    _pendingCommand = "startle";
                    break;

                case "noise":
                    _pendingCommand = "noise";
                    break;

                case "restart":
                    _Restart();
                    break;
            }
        }
    }

    public string? ConsumeCommand()
    {
        lock (_lock)
        {
            var cmd = _pendingCommand;
            _pendingCommand = null;
            return cmd;
        }
    }

    public void SetPersonality(string type)
    {
        lock (_lock)
        {
            _personality = type.ToLowerInvariant() switch
            {
                "feliz"    => GotchiPersonality.Feliz,
                "gloton"   => GotchiPersonality.Gloton,
                "dormilon" => GotchiPersonality.Dormilon,
                "activo"   => GotchiPersonality.Activo,
                _          => _personality
            };
        }
    }

    public void Dispose() => _decayTimer.Dispose();

    // ── Private ───────────────────────────────────────────────────────────────

    private void _Decay()
    {
        if (_dead) return;

        var now     = DateTime.UtcNow;
        var elapsed = (now - _lastDecay).TotalMinutes;
        if (elapsed < 1.0 / 60.0) return;
        _lastDecay = now;

        // ── Sueño nocturno automático ─────────────────────────────────────────
        var localHour = TimeZoneInfo.ConvertTimeFromUtc(now, _tz).Hour;
        bool isNight  = localHour >= NightStart || localHour < NightEnd;

        if (isNight && !_nightSleep)
        {
            _nightSleep = true;
            _sleeping   = true;
        }
        else if (!isNight && _nightSleep)
        {
            _nightSleep = false;
            // Deja _sleeping activo hasta que la energía se recupere
        }

        // ── Multiplicadores de personalidad ───────────────────────────────────
        double hungerMult    = _personality == GotchiPersonality.Gloton   ? 1.60 : 1.0;
        double happinessMult = _personality == GotchiPersonality.Feliz     ? 0.60 : 1.0;
        double energyMult    = _personality == GotchiPersonality.Dormilon  ? 1.50 : 1.0;
        if (_personality == GotchiPersonality.Activo)
        {
            hungerMult    *= 1.25;
            happinessMult *= 1.25;
            energyMult    *= 1.25;
        }

        // ── Stats básicos ─────────────────────────────────────────────────────
        _hunger    = Math.Max(0, _hunger    - HungerDecayPerMin    * hungerMult    * elapsed);
        _happiness = Math.Max(0, _happiness - HappinessDecayPerMin * happinessMult * elapsed);

        double recoverMult = _personality == GotchiPersonality.Dormilon ? 1.30 : 1.0;
        if (_sleeping)
            _energy = Math.Min(100, _energy + EnergyRecoverPerMin * recoverMult * elapsed);
        else
            _energy = Math.Max(0, _energy - EnergyDecayPerMin * energyMult * elapsed);

        if (_hunger < 25)
            _happiness = Math.Max(0, _happiness - HappinessDecayPerMin * elapsed);

        if (_sleeping && _energy >= 100) _sleeping = false;

        // ── Caca ──────────────────────────────────────────────────────────────
        if (!_needsClean)
        {
            _nextPoopMinutes -= elapsed;
            if (_nextPoopMinutes <= 0)
            {
                _needsClean = true;
                _poopedAt   = now;
                _happiness  = Math.Max(0, _happiness - 10);
            }
        }

        // ── Hambre extrema ────────────────────────────────────────────────────
        if (_hunger < 10)
        {
            if (_veryHungryAt == DateTime.MinValue) _veryHungryAt = now;
        }
        else
        {
            _veryHungryAt = DateTime.MinValue;
        }

        // ── Enfermar ──────────────────────────────────────────────────────────
        double sickTimeMult = _personality == GotchiPersonality.Activo ? 0.7 : 1.0;
        if (!_sick)
        {
            bool fromPoop   = _needsClean
                              && _poopedAt != DateTime.MinValue
                              && (now - _poopedAt).TotalMinutes > SickAfterPoopMin * sickTimeMult;
            bool fromHunger = _veryHungryAt != DateTime.MinValue
                              && (now - _veryHungryAt).TotalMinutes > SickAfterHungryMin * sickTimeMult;
            if (fromPoop || fromHunger)
            {
                _sick   = true;
                _sickAt = now;
            }
        }

        // ── Pausar timer de muerte mientras duerme ───────────────────────────
        if (_sick && _sleeping && _sickAt != DateTime.MinValue)
        {
            _sickAt = _sickAt.Add(TimeSpan.FromMinutes(elapsed));
        }

        // ── Morir (solo despierto) ────────────────────────────────────────────
        if (_sick && !_sleeping && _sickAt != DateTime.MinValue
            && (now - _sickAt).TotalMinutes > DeadAfterSickMin)
        {
            _dead = true;
        }
    }

    private void _Restart()
    {
        _hunger          = 80;
        _happiness       = 80;
        _energy          = 80;
        _sleeping        = false;
        _nightSleep      = false;
        _sick            = false;
        _dead            = false;
        _needsClean      = false;
        _bornAt          = DateTime.UtcNow;
        _lastDecay       = DateTime.UtcNow;
        _poopedAt        = DateTime.MinValue;
        _veryHungryAt    = DateTime.MinValue;
        _sickAt          = DateTime.MinValue;
        _nextPoopMinutes = RandPoopInterval();
        _mood            = 0;
        _pendingCommand  = null;
        // _personality se mantiene al reiniciar
    }

    private VitalsDto _BuildVitals() => new(
        (int)_hunger, (int)_happiness, (int)_energy,
        _sick, _dead, _needsClean, _sleeping);

    private GotchiStateModel _BuildModel() => new(
        _mood,
        (int)_hunger, (int)_happiness, (int)_energy,
        _sick, _dead, _needsClean, _sleeping,
        (DateTime.UtcNow - _bornAt).TotalHours,
        _updatedAt,
        _personality);

    private static double RandPoopInterval() =>
        PoopMinMin + Random.Shared.NextDouble() * (PoopMaxMin - PoopMinMin);
}
