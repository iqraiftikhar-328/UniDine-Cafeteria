FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    g++ \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY server.cpp .

RUN g++ -std=c++17 -O2 -o server server.cpp -lpthread

EXPOSE 8080

CMD ["./server"]
