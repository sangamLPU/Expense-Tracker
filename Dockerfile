FROM debian:bookworm-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY CMakeLists.txt ./
COPY src ./src
RUN cmake -S . -B out -DCMAKE_BUILD_TYPE=Release \
    && cmake --build out --parallel

FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system tracker \
    && useradd --system --gid tracker --home-dir /app tracker \
    && mkdir -p /app/web /data \
    && chown -R tracker:tracker /app /data

WORKDIR /app
COPY --from=build /build/out/expense-tracker /usr/local/bin/expense-tracker
COPY --chown=tracker:tracker web/ /app/web/

USER tracker
EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/expense-tracker"]
CMD ["--host", "0.0.0.0", "--port", "8080", "--data", "/data/expense-tracker.json", "--web", "/app/web"]
