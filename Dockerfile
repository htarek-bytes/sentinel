# build stage: full toolchain, thrown away afterwards
FROM debian:bookworm-slim AS build

RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential cmake \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build

# runtime stage: no compiler, no cmake, just the two binaries
FROM debian:bookworm-slim

COPY --from=build /src/build/sentinel /usr/local/bin/sentinel
COPY --from=build /src/build/worker   /usr/local/bin/worker

# sentinel is pid 1, which is the whole point. whatever you pass to
# docker run becomes the program it supervises.
ENTRYPOINT ["/usr/local/bin/sentinel"]
CMD ["/usr/local/bin/worker", "sleep"]
