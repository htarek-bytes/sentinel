# sentinel

A small process supervisor written in C++17 on POSIX.

Right now it does one thing: launch a single child process, wait for it to
finish, and report exactly how it ended. It exits with the same status the
child did, so it behaves correctly inside shell scripts and CI pipelines.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

This produces two binaries in `build/`:

| Binary     | Purpose                                            |
| ---------- | -------------------------------------------------- |
| `sentinel` | The supervisor itself.                              |
| `worker`   | A test child used to exercise the supervisor.       |

## Usage

```sh
sentinel <program> [args...]
```

## Try it

The `worker` binary takes a mode argument so you can produce each kind of
exit on demand:

```sh
cd build

./sentinel ./worker ok      # exits 0
./sentinel ./worker fail    # exits 3
./sentinel ./worker crash   # dies on SIGABRT
./sentinel ./worker sleep   # runs until you stop it
```

Check the status that came back with `echo $?`:

```
$ ./sentinel ./worker fail
sentinel: started './worker' as pid 5206
worker: failing
sentinel: pid 5206 exited with code 3
$ echo $?
3
```

## Exit status

`sentinel` mirrors the shell's convention:

| Situation                  | Exit status  |
| -------------------------- | ------------ |
| Child exited normally      | The child's own exit code |
| Child killed by signal `N` | `128 + N`    |
| No program given           | `1`          |
| Program could not be exec'd| `127`        |

## Roadmap

- [x] Launch and reap a single child process
- [ ] Forward SIGINT and SIGTERM to the child
- [ ] Restart the child when it exits unexpectedly
- [ ] Backoff between restarts
- [ ] Supervise several children at once
- [ ] Read the process list from a config file
- [ ] Capture and prefix child stdout/stderr
