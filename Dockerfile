FROM alpine:3.22 AS builder

RUN apk add --no-cache build-base cmake git ca-certificates

WORKDIR /build

COPY CMakeLists.txt ./
COPY config.yaml ./
COPY src/ ./src/
COPY example.cpp benchmark_load.cpp ./

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_STATIC=ON  && cmake --build build

FROM alpine:3.22 AS binary

COPY --from=builder /build/build/fulltext-search-service /fulltext-search-service

RUN mkdir -p /output

CMD ["cp", "/fulltext-search-service", "/output/fulltext-search-service"]

FROM alpine:3.22

WORKDIR /app

COPY --from=builder /build/config.yaml ./config.yaml
COPY --from=builder /build/build/fulltext-search-service ./

RUN mkdir -p /var/lib/fulltext-search-service

EXPOSE 8000

CMD ["./fulltext-search-service", "--config=/app/config.yaml"]
