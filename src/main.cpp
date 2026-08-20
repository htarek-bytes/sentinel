// sentinel - runs a child process and reports how it died
// usage: sentinel <program> [args...]

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

#include <sys/wait.h>
#include <unistd.h>

// the handler reads this, so it has to be sig_atomic_t
static volatile sig_atomic_t g_child_pid = 0;

// careful in here: only async-signal-safe calls. kill() is ok, printf is not.
static void forward_to_child(int sig) {
    if (g_child_pid > 0) {
        kill(-g_child_pid, sig);   // negative pid = the whole group
    }
}

static void install_handlers() {
    struct sigaction sa{};
    sa.sa_handler = forward_to_child;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   // no SA_RESTART on purpose, we want EINTR from waitpid
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <program> [args...]\n", argv[0]);
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::fprintf(stderr, "sentinel: fork failed: %s\n", std::strerror(errno));
        return 1;
    }

    if (pid == 0) {
        setpgid(0, 0);   // own group, so signals reach grandchildren too

        // argv is already NULL terminated so &argv[1] works as the arg vector
        execvp(argv[1], &argv[1]);

        // we only get here if exec failed. _exit, not exit, otherwise we flush
        // the stdio buffers we inherited from the parent and print things twice
        std::fprintf(stderr, "sentinel: exec '%s' failed: %s\n", argv[1], std::strerror(errno));
        _exit(127);
    }

    setpgid(pid, pid);   // do it on both sides, whichever wins is fine
    g_child_pid = pid;
    install_handlers();

    // TODO a signal landing between the fork and the line above still kills us
    // and leaves the child orphaned. sigprocmask around the fork would fix it

    std::printf("sentinel: started '%s' as pid %d\n", argv[1], pid);
    std::fflush(stdout);

    // EINTR here just means a signal woke us up, not a real failure
    int status = 0;
    pid_t reaped;
    do {
        reaped = waitpid(pid, &status, 0);
    } while (reaped < 0 && errno == EINTR);

    if (reaped < 0) {
        std::fprintf(stderr, "sentinel: waitpid failed: %s\n", std::strerror(errno));
        return 1;
    }

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        std::printf("sentinel: pid %d exited with code %d\n", pid, code);
        return code;
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        std::printf("sentinel: pid %d killed by signal %d (%s)\n", pid, sig, strsignal(sig));
        return 128 + sig;   // shell convention
    }

    std::fprintf(stderr, "sentinel: pid %d ended in an unexpected way\n", pid);
    return 1;
}
