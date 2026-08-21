// sentinel - runs a child process and reports how it died
// usage: sentinel <program> [args...]

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

// the handler reads this, so it has to be sig_atomic_t
static volatile sig_atomic_t g_child_pid = 0;

// set once someone asks us to stop, so the restart loop knows to give up
static volatile sig_atomic_t g_stopping = 0;

// what we have seen so far. only main touches these for now, the metrics
// endpoint later on will need them to be atomic
static unsigned long g_restarts = 0;
static unsigned long g_crashes  = 0;   // child was killed by a signal
static unsigned long g_failures = 0;   // child exited with a nonzero code

// careful in here: only async-signal-safe calls. kill() is ok, printf is not.
static const unsigned GRACE_SECONDS = 5;
static const unsigned BACKOFF_MIN_SECONDS = 1;
static const unsigned BACKOFF_MAX_SECONDS = 16;

// a child that stayed up this long counts as healthy, so the next crash starts
// the backoff over instead of inheriting a long delay from hours ago
static const unsigned HEALTHY_RUN_SECONDS = 10;

static void forward_to_child(int sig) {
    g_stopping = 1;
    if (g_child_pid > 0) {
        kill(-g_child_pid, sig);   // negative pid = the whole group
        alarm(GRACE_SECONDS);      // start the clock, SIGALRM finishes the job
    }
}

// grace period expired and it is still around
static void escalate_to_kill(int) {
    if (g_child_pid > 0) {
        kill(-g_child_pid, SIGKILL);
    }
}

static void install_handlers() {
    struct sigaction sa{};
    sa.sa_handler = forward_to_child;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   // no SA_RESTART on purpose, we want EINTR from waitpid
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    sa.sa_handler = escalate_to_kill;
    sigaction(SIGALRM, &sa, nullptr);
}

// fork, exec, then wait on that child while reaping anything else we adopt.
// returns the child pid and fills status, or -1 if we could not start it.
static pid_t run_once(char** args, int* status) {
    // block these two before forking, otherwise a signal can land in the gap
    // before install_handlers() runs and kill us while the child lives on
    sigset_t mask, old;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, &old);

    pid_t pid = fork();
    if (pid < 0) {
        std::fprintf(stderr, "sentinel: fork failed: %s\n", std::strerror(errno));
        sigprocmask(SIG_SETMASK, &old, nullptr);
        return -1;
    }

    if (pid == 0) {
        // exec keeps the mask, so put it back. otherwise whatever we launch
        // starts out unable to catch SIGINT or SIGTERM
        sigprocmask(SIG_SETMASK, &old, nullptr);

        setpgid(0, 0);   // own group, so signals reach grandchildren too

        // args is NULL terminated already, it points into our own argv
        execvp(args[0], args);

        // we only get here if exec failed. _exit, not exit, otherwise we flush
        // the stdio buffers we inherited from the parent and print things twice
        std::fprintf(stderr, "sentinel: exec '%s' failed: %s\n", args[0], std::strerror(errno));
        _exit(127);
    }

    setpgid(pid, pid);   // do it on both sides, whichever wins is fine
    g_child_pid = pid;
    install_handlers();
    sigprocmask(SIG_SETMASK, &old, nullptr);   // handlers are up, safe now

    std::printf("sentinel: started '%s' as pid %d\n", args[0], pid);
    std::fflush(stdout);

    // reap whatever turns up, not only our own child. running as pid 1 we
    // inherit orphans and there is nobody else left to clean them up
    for (;;) {
        pid_t reaped = waitpid(-1, status, 0);

        if (reaped < 0) {
            if (errno == EINTR) {
                continue;   // a signal woke us, just ask again
            }
            std::fprintf(stderr, "sentinel: waitpid failed: %s\n", std::strerror(errno));
            return -1;
        }

        if (reaped == pid) {
            break;   // that was the child we launched, go report on it
        }
        // anything else was an orphan we adopted. it is reaped, keep waiting
    }

    g_child_pid = 0;
    return pid;
}

static void print_summary() {
    std::printf("sentinel: %lu restarts, %lu crashes, %lu failures\n",
                g_restarts, g_crashes, g_failures);
    std::fflush(stdout);
}

// turn a wait status into the exit code we hand back to our own caller
static int report(pid_t pid, int status) {
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code != 0) {
            g_failures++;
        }
        std::printf("sentinel: pid %d exited with code %d\n", pid, code);
        return code;
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        g_crashes++;
        std::printf("sentinel: pid %d killed by signal %d (%s)\n", pid, sig, strsignal(sig));
        return 128 + sig;   // shell convention
    }

    std::fprintf(stderr, "sentinel: pid %d ended in an unexpected way\n", pid);
    return 1;
}

int main(int argc, char** argv) {
    bool restart = false;
    int first = 1;

    if (argc > 1 && std::strcmp(argv[1], "--restart") == 0) {
        restart = true;
        first = 2;
    }

    if (argc <= first) {
        std::fprintf(stderr, "usage: %s [--restart] <program> [args...]\n", argv[0]);
        return 1;
    }

    // ask the kernel to reparent orphaned descendants to us instead of pid 1.
    // without this the loop in run_once never sees an orphan to reap
    prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0);

    unsigned backoff = BACKOFF_MIN_SECONDS;

    for (;;) {
        time_t started = time(nullptr);

        int status = 0;
        pid_t pid = run_once(&argv[first], &status);
        if (pid < 0) {
            return 1;
        }

        int code = report(pid, status);

        // g_stopping means the signal was aimed at us, not just the child, so
        // stop supervising rather than starting another one
        if (!restart || g_stopping) {
            if (restart) {
                print_summary();
            }
            return code;
        }

        if (time(nullptr) - started >= (time_t)HEALTHY_RUN_SECONDS) {
            backoff = BACKOFF_MIN_SECONDS;   // it was up a while, start over
        }

        std::printf("sentinel: restarting '%s' in %us\n", argv[first], backoff);
        std::fflush(stdout);

        sleep(backoff);
        if (g_stopping) {
            print_summary();   // signal landed while we were waiting
            return code;
        }

        if (backoff < BACKOFF_MAX_SECONDS) {
            backoff *= 2;
        }

        g_restarts++;
    }
}
