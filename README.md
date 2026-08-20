# sentinel

[![CI](https://github.com/htarek-bytes/sentinel/actions/workflows/ci.yml/badge.svg)](https://github.com/htarek-bytes/sentinel/actions/workflows/ci.yml)

A small process supervisor in C++17 for Linux.

## Why

When a process runs inside a container it becomes PID 1, and the kernel treats
PID 1 differently:

- Signals with no handler installed are not delivered to it. An app that never
  installs a SIGTERM handler will sit there ignoring `docker stop`, wait out
  the grace period, and get SIGKILLed instead of shutting down cleanly.
- It inherits every orphaned process in the container and is responsible for
  reaping them. If it doesn't, zombies pile up until the pid limit is hit.

That is what `tini` and `dumb-init` exist to solve. I am building sentinel to
understand the problem from the syscall level up: fork, exec, waitpid, signals,
process groups.

## What it does today

- Launches a child and waits for it
- Reports whether it exited normally or was killed, and by what
- Exits with the child's own status, so it composes with shell scripts and CI
- Forwards SIGINT and SIGTERM to the child instead of orphaning it
- Puts the child in its own process group, so grandchildren get signalled too

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Two binaries land in `build/`: `sentinel` itself, and `worker`, a test child
used to exercise it.

## Usage

```sh
sentinel <program> [args...]
```

## Try it

`worker` takes a mode argument so you can trigger each kind of exit on demand:

```sh
cd build
./sentinel ./worker ok      # exits 0
./sentinel ./worker fail    # exits 3
./sentinel ./worker crash   # dies on SIGABRT
./sentinel ./worker sleep   # runs until you stop it
```

A normal failure propagates the child's code:

```
$ ./sentinel ./worker fail
sentinel: started './worker' as pid 14658
worker: failing
sentinel: pid 14658 exited with code 3
$ echo $?
3
```

A signal death is reported separately, and reads as 128 + the signal number:

```
$ ./sentinel ./worker crash
sentinel: started './worker' as pid 14660
sentinel: pid 14660 killed by signal 6 (Aborted)
$ echo $?
134
```

## Exit status

| Situation | Exit status |
| --- | --- |
| Child exited normally | the child's own exit code |
| Child killed by signal N | `128 + N` |
| Program could not be exec'd | `127` |
| No program given | `1` |

This is the shell convention, which is why exit 137 out of a container means
SIGKILL (128 + 9), usually the OOM killer.

## Signals

Stopping sentinel stops what it supervises:

```sh
./sentinel ./worker sleep &
kill -TERM %1        # the worker dies too, sentinel reports signal 15
```

The handler only calls `kill()`, since that is one of the few functions safe to
call from signal context. Handlers are installed without `SA_RESTART` on
purpose, so `waitpid` returns `EINTR` and the reap loop stays under our control
rather than the kernel silently restarting the syscall.

Signals go to the child's whole process group, not just the direct child, so a
child that spawned its own children takes the tree down with it.

## Planned

- Escalate to SIGKILL when a child ignores SIGTERM
- Restart on unexpected exit, with backoff
- Expose restart and crash counters as Prometheus metrics