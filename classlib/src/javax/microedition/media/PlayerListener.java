package javax.microedition.media;
public interface PlayerListener {
    String STARTED = "started", STOPPED = "stopped", STOPPED_AT_TIME = "stoppedAtTime", END_OF_MEDIA = "endOfMedia",
        DURATION_UPDATED = "durationUpdated", DEVICE_UNAVAILABLE = "deviceUnavailable", DEVICE_AVAILABLE = "deviceAvailable",
        VOLUME_CHANGED = "volumeChanged", SIZE_CHANGED = "sizeChanged", ERROR = "error", CLOSED = "closed",
        RECORD_STARTED = "recordStarted", RECORD_STOPPED = "recordStopped", RECORD_ERROR = "recordError",
        BUFFERING_STARTED = "bufferingStarted", BUFFERING_STOPPED = "bufferingStopped";
    void playerUpdate(Player player, String event, Object eventData);
}
