# ProcessRuntime

[![Unreal Engine 5.7](https://img.shields.io/badge/Unreal%20Engine-5.7-blue.svg)](https://www.unrealengine.com/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

Reusable Unreal Engine runtime process ownership and monitoring plugin.

## Installation

Add the plugin as a project submodule, then enable `ProcessRuntime` in the
project descriptor and build the Editor target:

```powershell
git submodule add https://github.com/MC-Oruc/ProcessRuntime.git Plugins/ProcessRuntime
git submodule update --init --recursive
```

## Responsibilities

- Starts only explicitly requested executable paths.
- Tracks multiple named, owned processes for the lifetime of the engine.
- Captures merged stdout/stderr through `FMonitoredProcess`.
- Marshals monitor-thread callbacks to the game thread.
- Stops owned process trees during normal shutdown.
- On Win64, uses a Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` to avoid orphan processes after a hard host shutdown.

The plugin deliberately has no knowledge of HTTP, ports, text generation, models, or llama.cpp.

`UProcessRuntimeSubsystem` is engine-scoped, so an owned process can survive PIE world/GameInstance recreation. It terminates only processes created under its own `FManagedProcessId`; a foreign process occupying the same port is never killed. Win64 Job Object assignment can be denied by a host job policy; that condition is logged and normal `FMonitoredProcess::Cancel(true)` tree termination remains available.
