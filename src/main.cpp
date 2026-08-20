// sentinel: launch a child process, wait for it, and report how it ended.
//
// Usage: sentinel <program> [args...]

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

#include <sys/wait.h>
#include <unistd.h>

namespace {

// Written by main, read inside the signal handler. sig_atomic_t is the only
// type guaranteed to be accessed atomically from signal context.
volatile sig_atomic_t g_child_pid = 0;

// Runs in signal context, so it may call only async-signal-safe functions.
// kill() is on that list. printf and malloc are not, which is why this
// handler does one thing and nothing else.
void forward_to_child(int sig) {
    if (g_child_pid > 0) {
        kill(g_child_pid, sig);
    }
}

void install_forwarding_handlers() {
    struct sigaction sa{};
    sa.sa_handler = forward_to_child;
    sigemptyset(&sa.sa_mask);
    // Deliberately no SA_RESTART: we want waitpid to return EINTR rather than
    // silently restarting, so the reap loop stays under our control.
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

}  // namespace

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
        // Child: replace this process image with the requested program.
        // argv is NULL-terminated, so &argv[1] is a valid argument vector.
        execvp(argv[1], &argv[1]);

        // Only reached if exec failed. Use _exit so we don't flush the
        // stdio buffers this process inherited from the parent.
        std::fprintf(stderr, "sentinel: exec '%s' failed: %s\n", argv[1], std::strerror(errno));
        _exit(127);
    }

    // Parent: the child is running concurrently from here on.
    //
    // Note the ordering. Publishing the pid before installing the handlers
    // means a signal arriving in between still leaves sentinel on the default
    // disposition, which kills it and orphans the child. That window is
    // narrow but real, and closing it needs sigprocmask.
    g_child_pid = pid;
    install_forwarding_handlers();

    std::printf("sentinel: started '%s' as pid %d\n", argv[1], pid);
    std::fflush(stdout);

    // waitpid returns EINTR if a signal arrives while we are blocked. That is
    // not an error, it means "interrupted, ask again". Retrying here keeps the
    // reap correct once signal handlers exist.
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
        return 128 + sig;
    }

    std::fprintf(stderr, "sentinel: pid %d ended in an unexpected way\n", pid);
    return 1;
}
