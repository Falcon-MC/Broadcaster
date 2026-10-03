FROM ubuntu:22.04 AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential cmake ninja-build git ca-certificates zlib1g-dev \
       libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY include include
COPY src src

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENSSL_USE_STATIC_LIBS=ON \
    && cmake --build build --parallel \
    && strip build/FalconBroadcaster

FROM debian:12-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates zlib1g \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/FalconBroadcaster /usr/local/bin/FalconBroadcaster

WORKDIR /data
VOLUME /data

ENTRYPOINT ["/usr/local/bin/FalconBroadcaster"]
