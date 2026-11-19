# syntax=docker/dockerfile:1.7
#
# Commit image for esp-now (an ESP-IDF component), built for the host: the component's
# sources are compiled against mayhem/shim/ (ESP-IDF/FreeRTOS stand-ins) — see mayhem/build.sh.
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
ARG SOURCE_DATE_EPOCH
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

# mbedTLS (AEAD, Curve25519, AES-CTR, SHA-256) and protobuf-c: what ESP-IDF builds the component with.
USER root
RUN [ -n "$SOURCE_DATE_EPOCH" ] && repro-sources-list.sh || true ; \
    apt-get update && apt-get install -yq --no-install-recommends libmbedtls-dev libprotobuf-c-dev \
 && rm -rf /var/lib/apt/lists/*

# ESP-IDF's protocomm component (security1 handshake server, SessionData protobufs), which the
# component's security responder uses and apt does not ship. Fetched file by file from the
# pinned ESP-IDF commit, each checked against mayhem/deps/esp-idf-protocomm-v5.3.2.sha256.
# Source only: build.sh compiles it (it is part of the fuzzed handshake path).
ARG IDF_COMMIT=9d7f2d69f50d1288526d4f1027108e314e8c879f
COPY mayhem/deps/esp-idf-protocomm-v5.3.2.sha256 /tmp/protocomm.sha256
RUN d=/opt/toolchains/deps/esp-idf-protocomm-v5.3.2/src && mkdir -p "$d" && cd "$d" \
 && grep -v '^#' /tmp/protocomm.sha256 | while read -r sum f; do \
      mkdir -p "$(dirname "$f")" \
      && curl -fsSL --retry 3 -o "$f" "https://raw.githubusercontent.com/espressif/esp-idf/${IDF_COMMIT}/components/protocomm/$f" ; \
    done \
 && sha256sum -c /tmp/protocomm.sha256 \
 && rm /tmp/protocomm.sha256 && chmod -R a+rX,a-w /opt/toolchains/deps/esp-idf-protocomm-v5.3.2

# ESP-IDF's console component (esp_console command registry and line splitting, argtable3,
# linenoise) at the same commit: the component's remote debug console (src/debug) hands every
# DEBUG_COMMAND radio frame to esp_console_run(). Plain C; ESP-IDF itself builds it for its
# linux host target. Checked against mayhem/deps/esp-idf-console-v5.3.2.sha256; build.sh
# compiles it.
COPY mayhem/deps/esp-idf-console-v5.3.2.sha256 /tmp/console.sha256
RUN d=/opt/toolchains/deps/esp-idf-console-v5.3.2/src && mkdir -p "$d" && cd "$d" \
 && grep -v '^#' /tmp/console.sha256 | while read -r sum f; do \
      mkdir -p "$(dirname "$f")" \
      && curl -fsSL --retry 3 -o "$f" "https://raw.githubusercontent.com/espressif/esp-idf/${IDF_COMMIT}/components/console/$f" ; \
    done \
 && sha256sum -c /tmp/console.sha256 \
 && rm /tmp/console.sha256 && chmod -R a+rX,a-w /opt/toolchains/deps/esp-idf-console-v5.3.2

USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
