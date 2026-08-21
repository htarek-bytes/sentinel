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
- Escalates to SIGKILL if the child ignores SIGTERM for five seconds
- Blocks signals around the fork so nothing slips through before the handlers
  are installed
- Adopts orphaned descendants and reaps them, so zombies do not pile up
- Ships as a container image where it runs as pid 1
- Restarts the child on request, backing off 1s, 2s, 4s and so on up to 16s
- Serves restart, crash and failure counters for Prometheus to scrape

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Two binaries land in `build/`: `sentinel` itself, and `worker`, a test child
used to exercise it.

## Usage

```sh
sentinel [--restart] [--metrics-port N] <program> [args...]
```

## Try it

`worker` takes a mode argument so you can trigger each kind of exit on demand:

```sh
cd build
./sentinel ./worker ok      # exits 0
./sentinel ./worker fail    # exits 3
./sentinel ./worker crash   # dies on SIGABRT
./sentinel ./worker sleep   # runs until you stop it
./sentinel ./worker stubborn # ignores SIGTERM, forces the SIGKILL path
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

A child that ignores SIGTERM gets five seconds of grace, then SIGKILL, which
cannot be caught or ignored. The handler arms `alarm(5)` after forwarding, and
a SIGALRM handler does the killing, so both halves stay async-signal-safe.

```sh
./sentinel ./worker stubborn &
kill -TERM %1       # ignored, dies ~5s later, sentinel reports 137
```

## Restarting

`--restart` keeps the child alive instead of exiting with it:

```sh
sentinel --restart ./worker fail
```

The pause doubles after each death, from one second up to sixteen, so a program
that dies the moment it starts cannot spin the machine. A child that stays up
for ten seconds counts as healthy and resets the pause back to one, otherwise
an occasional crash would eventually inherit a long delay from hours earlier.

On the way out it prints what it saw:

```
sentinel: 2 restarts, 0 crashes, 3 failures
```

A crash means the child was killed by a signal. A failure means it exited on
its own with a nonzero code. Keeping them apart matters because they usually
have different causes, and these are the numbers the metrics endpoint will
expose later.

SIGTERM stops the supervisor as well as the child. Without that, stopping
sentinel would just make it launch a replacement.

## Metrics

`--metrics-port` starts a small HTTP server that serves the counters in the
text format Prometheus scrapes:

```sh
sentinel --restart --metrics-port 9090 ./worker fail
curl localhost:9090/metrics
```

```
# HELP sentinel_restarts_total Times the child has been relaunched.
# TYPE sentinel_restarts_total counter
sentinel_restarts_total 2
# TYPE sentinel_crashes_total counter
sentinel_crashes_total 0
# TYPE sentinel_failures_total counter
sentinel_failures_total 3
# TYPE sentinel_child_up gauge
sentinel_child_up 0
```

It runs on its own thread, because the main one sits blocked in `waitpid` and
cannot also wait in `accept`. The thread blocks every signal for itself, so
SIGTERM keeps landing on the main thread where the handler lives. Delivering it
to the metrics thread instead would mean `waitpid` never gets its `EINTR`.

The counters are atomic since two threads touch them.

## Container

Sentinel is built as a two stage image. The compiler and cmake live in the
build stage and get thrown away, so the runtime image carries nothing but the
two binaries.

```sh
docker build -t sentinel .
docker run --rm sentinel /usr/local/bin/worker fail    # exits 3, same as before
```

Sentinel is the entrypoint, so it lands on pid 1 and whatever you pass becomes
the program it supervises. That is the case the whole project is aimed at.

```sh
docker run -d --name demo sentinel /usr/local/bin/worker sleep
time docker stop demo
```

That returns in well under a second. A program with no SIGTERM handler sitting
on pid 1 would ignore the signal, wait out docker's ten second grace period,
and get SIGKILLed instead. CI checks both halves on every push: that pid 1 is
really sentinel, and that the container stops promptly.

## Planned

- A compose stack wiring Prometheus and Grafana to the endpoint
