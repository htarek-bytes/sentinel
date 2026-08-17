#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* mode = (argc > 1) ? argv[1] : "sleep";
    if (std::strcmp(mode, "ok") == 0)    { std::printf("worker: done\n"); return 0; }
    if (std::strcmp(mode, "fail") == 0)  { std::fprintf(stderr, "worker: failing\n"); return 3; }
    if (std::strcmp(mode, "crash") == 0) { std::abort(); }
    if (std::strcmp(mode, "sleep") == 0) {
        for (;;) { std::printf("worker: alive\n"); std::fflush(stdout); sleep(1); }
    }
    std::fprintf(stderr, "worker: unknown mode %s\n", mode);
    return 2;
}
