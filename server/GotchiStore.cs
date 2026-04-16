namespace AtomGotchi.Api;

public sealed class GotchiStore : IDisposable
{
    // ── Vitals ────────────────────────────────────────────────────────────────
    private double   _hunger          = 80;
    private double   _happiness       = 80;
    private double   _energy          = 80;
    private bool     _sleeping        = false;
    private bool     _sick            = false;
    private bool     _dead            = false;
    private bool     _needsClean      = false;

    private DateTime _bornAt          = DateTime.UtcNow;
    private DateTime _lastDecay       = DateTime.UtcNow;
    private DateTime _poopedAt        = DateTime.MinValue;
    private DateTime _veryHungryAt    = DateTime.MinValue;
    private DateTime _sickAt          = DateTime.MinValue;
    private double   _nextPoopMinutes;

    // ── Device state ──────────────────────────────────────────────────────────
    private int      _mood      = 0;
    private DateTime _updatedAt = DateTime.MinValue;

    // ── Command queue ─────────────────────────────────────────────────────────
    private string?  _pendingCommand;

    private readonly object _lock = new();
    private readonly System.Threading.Timer _decayTimer;

    // ── Decay constants ───────────────────────────────────────────────────────
    private const double HungerDecayPerMin    = 0.50;  // 100→0 en ~3.3h
    private const double HappinessDecayPerMin = 0.33;  // 100→0 en ~5h
    private const double EnergyDecayPerMin    = 0.25;  // 100→0 en ~6.7h despierto
    private const double EnergyRecoverPerMin  = 0.80;  // duerme: lleno en ~2h
    private const double PoopMinMin           = 30.0;
    private const double PoopMaxMin           = 90.0;
    private const double SickAfterPoopMin     = 15.0;
    private const double SickAfterHungryMin   = 10.0;
    private const double DeadAfterSickMin     = 30.0;

    // ─────────────────────────────────────────────────────────────────────────

    public GotchiStore()
    {
        _nextPoopMinutes = RandPoopInterval();
        // Decay autónomo cada 10 s aunque el Atom esté offline
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

    /// <summary>Llamado por el Atom cada 3 s. Ejecuta decay y devuelve vitals actuales.</summary>
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

    /// <summary>Ejecuta un comando de la web: aplica efecto en vitals y encola animación para el Atom.</summary>
    public void ExecuteCommand(string cmd)
    {
        lock (_lock)
        {
            if (_dead && cmd != "restart") return;

            switch (cmd)
            {
                case "feed":
                    _hunger       = Math.Min(100, _hunger + 40);
                    _veryHungryAt = DateTime.MinValue;
                    _pendingCommand = "feed";
                    break;

                case "play":
                    _happiness = Math.Min(100, _happiness + 30);
                    _energy    = Math.Max(0,   _energy    - 10);
                    _pendingCommand = "pet";
                    break;

                case "pet":
                    _happiness = Math.Min(100, _happiness + 15);
                    _pendingCommand = "pet";
                    break;

                case "sleep":
                    _sleeping = !_sleeping;
                    break;

                case "medicine":
                    _sick      = false;
                    _sickAt    = DateTime.MinValue;
                    _happiness = Math.Min(100, _happiness + 10);
                    _pendingCommand = "pet";
                    break;

                case "clean":
                    _needsClean      = false;
                    _poopedAt        = DateTime.MinValue;
                    _nextPoopMinutes = RandPoopInterval();
                    _happiness       = Math.Min(100, _happiness + 5);
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

    /// <summary>Devuelve el comando pendiente para el Atom y lo borra.</summary>
    public string? ConsumeCommand()
    {
        lock (_lock)
        {
            var cmd = _pendingCommand;
            _pendingCommand = null;
            return cmd;
        }
    }

    public void Dispose() => _decayTimer.Dispose();

    // ── Private ───────────────────────────────────────────────────────────────

    private void _Decay()
    {
        if (_dead) return;

        var now     = DateTime.UtcNow;
        var elapsed = (now - _lastDecay).TotalMinutes;
        if (elapsed < 1.0 / 60.0) return; // menos de 1 segundo, skip
        _lastDecay = now;

        // Stats básicos
        _hunger    = Math.Max(0, _hunger    - HungerDecayPerMin    * elapsed);
        _happiness = Math.Max(0, _happiness - HappinessDecayPerMin * elapsed);

        // Energía: recupera durmiendo, gasta despierto
        if (_sleeping)
            _energy = Math.Min(100, _energy + EnergyRecoverPerMin * elapsed);
        else
            _energy = Math.Max(0, _energy - EnergyDecayPerMin * elapsed);

        // Felicidad cae extra cuando tiene mucha hambre
        if (_hunger < 25)
            _happiness = Math.Max(0, _happiness - HappinessDecayPerMin * elapsed);

        // Despertarse solo cuando energía llena
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

        // ── Rastrear hambre extrema ───────────────────────────────────────────
        if (_hunger < 10)
        {
            if (_veryHungryAt == DateTime.MinValue) _veryHungryAt = now;
        }
        else
        {
            _veryHungryAt = DateTime.MinValue;
        }

        // ── Enfermar ──────────────────────────────────────────────────────────
        if (!_sick)
        {
            bool fromPoop   = _needsClean
                              && _poopedAt != DateTime.MinValue
                              && (now - _poopedAt).TotalMinutes > SickAfterPoopMin;
            bool fromHunger = _veryHungryAt != DateTime.MinValue
                              && (now - _veryHungryAt).TotalMinutes > SickAfterHungryMin;
            if (fromPoop || fromHunger)
            {
                _sick   = true;
                _sickAt = now;
            }
        }

        // ── Morir ─────────────────────────────────────────────────────────────
        if (_sick && _sickAt != DateTime.MinValue
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
    }

    private VitalsDto _BuildVitals() => new(
        (int)_hunger, (int)_happiness, (int)_energy,
        _sick, _dead, _needsClean, _sleeping);

    private GotchiStateModel _BuildModel() => new(
        _mood,
        (int)_hunger, (int)_happiness, (int)_energy,
        _sick, _dead, _needsClean, _sleeping,
        (DateTime.UtcNow - _bornAt).TotalHours,
        _updatedAt);

    private static double RandPoopInterval() =>
        PoopMinMin + Random.Shared.NextDouble() * (PoopMaxMin - PoopMinMin);
}
