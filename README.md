# ProcessRuntime

Reusable Unreal Engine runtime process ownership and monitoring plugin.

## Responsibilities

- Starts only explicitly requested executable paths.
- Tracks multiple named, owned processes for the lifetime of the engine.
- Captures merged stdout/stderr through `FMonitoredProcess`.
- Marshals monitor-thread callbacks to the game thread.
- Stops owned process trees during normal shutdown.
- On Win64, uses a Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` to avoid orphan processes after a hard host shutdown.

The plugin deliberately has no knowledge of HTTP, ports, text generation, models, or llama.cpp.

