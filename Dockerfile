# ---- Build stage ----
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ make libhiredis-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY Makefile ./
COPY include ./include
COPY src ./src
RUN make

# ---- Runtime stage ----
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        libhiredis1.1.0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/mitral ./mitral

EXPOSE 8080
CMD ["./mitral"]
