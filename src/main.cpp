// sentinel: launch a child process, wait for it, and report how it ended.
//
// Usage: sentinel <program> [args...]

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <sys/wait.h>
#include <unistd.h>

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
    std::printf("sentinel: started '%s' as pid %d\n", argv[1], pid);
    std::fflush(stdout);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
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
