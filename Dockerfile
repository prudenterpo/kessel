FROM debian:bookworm-slim AS build

RUN apt-get update \
    && apt-get install --yes --no-install-recommends build-essential cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src

RUN cmake -S . -B /build \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
    && cmake --build /build --target kessel --parallel

FROM debian:bookworm-slim AS runtime

RUN groupadd --system kessel \
    && useradd --system --gid kessel --no-create-home \
        --home-dir /nonexistent --shell /usr/sbin/nologin kessel

COPY --from=build /build/kessel /usr/local/bin/kessel

USER kessel
EXPOSE 7070

ENTRYPOINT ["/usr/local/bin/kessel"]
