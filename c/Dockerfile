FROM debian:trixie
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends gcc libc6-dev libssl-dev libpq-dev >/dev/null && rm -rf /var/lib/apt/lists/*
COPY hmacbench.c pqbench.c /src/
RUN gcc -O2 -Wno-deprecated-declarations -o /usr/local/bin/hmacbench /src/hmacbench.c -lcrypto -lpthread && gcc -O2 -I/usr/include/postgresql -o /usr/local/bin/pqbench /src/pqbench.c -lpq -lpthread
