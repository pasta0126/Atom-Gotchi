namespace AtomGotchi.Api;

/// <summary>
/// Estado en memoria del Gotchi. Un solo comando pendiente a la vez;
/// el Atom lo consume en el siguiente poll y queda vacío.
/// </summary>
public sealed class GotchiStore
{
    private GotchiStateModel? _state;
    private string? _pendingCommand;
    private readonly object _lock = new();

    public GotchiStateModel? State
    {
        get { lock (_lock) return _state; }
    }

    public void UpdateState(DeviceStateDto dto)
    {
        lock (_lock)
            _state = new GotchiStateModel(
                dto.Mood, dto.Hunger, dto.Thirst,
                dto.Energy, dto.Steps, dto.Flags,
                DateTime.UtcNow);
    }

    public void EnqueueCommand(string cmd)
    {
        lock (_lock) _pendingCommand = cmd;
    }

    /// <summary>Devuelve el comando pendiente y lo borra. Null si no hay.</summary>
    public string? ConsumeCommand()
    {
        lock (_lock)
        {
            var cmd = _pendingCommand;
            _pendingCommand = null;
            return cmd;
        }
    }
}
