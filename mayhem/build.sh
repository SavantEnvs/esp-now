#!/usr/bin/env bash
#
# mayhem/build.sh — host build of the esp-now ESP-IDF component for fuzzing.
#
# esp-now only builds inside ESP-IDF for an ESP32. Here its sources (src/**) are compiled
# unchanged with clang against mayhem/shim/: host stand-ins for the ESP-IDF/FreeRTOS APIs it
# calls (a cooperative, virtual-time FreeRTOS on pthreads; RAM NVS/flash; an ESP-NOW driver
# that hands frames to the registered receive callback). Real libraries: mbedTLS and
# protobuf-c from apt, and ESP-IDF's protocomm (security1 handshake) and console (esp_console,
# argtable3, linenoise) from the pinned /opt/toolchains/deps/esp-idf-{protocomm,console}-*
# sources the Dockerfile fetched.
#
# Outputs (mayhem/build/, gitignored, recreated every run):
#   fuzz_<target>            libFuzzer binaries (ASan+UBSan, DWARF 3)
#   fuzz_<target>-standalone run-once reproducers
#   espnow_selftest          functional self-test (normal flags) that mayhem/test.sh runs
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
PROTOCOMM="${PROTOCOMM:-$(ls -d /opt/toolchains/deps/esp-idf-protocomm-*/src | head -1)}"
CONSOLE="${CONSOLE:-$(ls -d /opt/toolchains/deps/esp-idf-console-*/src | head -1)}"

cd "$SRC"
B=mayhem/build
mkdir -p "$B/fuzz" "$B/test"

INC=(-Imayhem/shim/include -Isrc/espnow/include -Isrc/utils/include -Isrc/security/include
     -Isrc/security/include/protocomm/security -Isrc/ota/include -Isrc/control/include
     -Isrc/provisioning/include -Isrc/time/include
     -I"$PROTOCOMM/include/security" -I"$PROTOCOMM/include/common" -I"$PROTOCOMM/proto-c" -I"$PROTOCOMM/src/common"
     -Isrc/debug/include -I"$CONSOLE" -I"$CONSOLE/private_include")
# The component's own CMakeLists adds -Wno-pointer-sign -Wno-format.
WFLAGS=(-std=gnu11 -Wno-pointer-sign -Wno-format -Wno-unused-function)

# espnow.c is compiled through mayhem/harness/espnow_unit.c (which #includes it).
# Modules with file-scope state are compiled through mayhem/harness/*_unit.c, which #include
# the unchanged module source and add a reset for that state (see the unit files).
LIB_SRCS=(
  mayhem/harness/espnow_unit.c src/espnow/src/espnow_group.c
  src/security/src/espnow_security.c mayhem/harness/sec_initiator_unit.c
  mayhem/harness/sec_responder_unit.c src/security/src/protocomm/security/client_security1.c
  mayhem/harness/ota_initiator_unit.c mayhem/harness/ota_responder_unit.c
  mayhem/harness/ctrl_unit.c mayhem/harness/prov_unit.c mayhem/harness/time_unit.c
  src/utils/src/espnow_mem.c src/utils/src/espnow_storage.c src/utils/src/espnow_utils.c
  "$PROTOCOMM/src/security/security1.c" "$PROTOCOMM/src/common/protocomm.c"
  "$PROTOCOMM/proto-c/session.pb-c.c" "$PROTOCOMM/proto-c/sec0.pb-c.c" "$PROTOCOMM/proto-c/sec1.pb-c.c" "$PROTOCOMM/proto-c/sec2.pb-c.c" "$PROTOCOMM/proto-c/constants.pb-c.c"
  mayhem/harness/console_unit.c mayhem/harness/cmd_espnow_unit.c mayhem/harness/cmd_iperf_unit.c
  mayhem/harness/console_cmds_unit.c "$CONSOLE/split_argv.c" "$CONSOLE/linenoise/linenoise.c"
  "$CONSOLE"/argtable3/arg_*.c "$CONSOLE/argtable3/argtable3.c"
  mayhem/shim/esp_shim.c mayhem/shim/freertos_shim.c mayhem/shim/console_shim.c
)
LIBS=(-lmbedcrypto -lprotobuf-c -lpthread -lm)

# build_lib <outdir> <cflags...>: compile LIB_SRCS into <outdir>/libespnow.a
build_lib() {
  local out="$1"; shift
  local objs=() pids=() i=0
  rm -f "$out/libespnow.a"
  for s in "${LIB_SRCS[@]}"; do
    local o="$out/$(basename "${s%.c}").o"
    objs+=("$o")
    local extra=()
    # espnow_ctrl.c reads the responder_value union through its bool member
    # (ctrl_data->responder_value_b) for any value; UBSan's `bool` check fires on nearly every
    # bind frame, so only that one check is relaxed, only for that module.
    # argtable3 calls each option type's reset/scan/check/error function through a
    # `void (*)(void *)`-style cast of its own typed function (argtable3.c arg_reset() etc.);
    # UBSan's `function` check fires on every parse. arg_end() also lays an int[] and then a
    # void*[] out in one allocation: aligned on the 32-bit ESP32 (both 4 bytes), misaligned on
    # this 64-bit host whenever maxcount is odd, so `alignment` fires too. Only those two
    # checks are relaxed, only for the argtable3 sources.
    case "$s" in
      *ctrl_unit.c) extra=(-fno-sanitize=bool) ;;
      */argtable3/*) extra=(-fno-sanitize=function,alignment) ;;
    esac
    $CC "$@" "${extra[@]}" "${WFLAGS[@]}" "${INC[@]}" -c "$s" -o "$o" &
    pids+=($!)
    i=$((i + 1))
    if [ $((i % MAYHEM_JOBS)) -eq 0 ]; then wait "${pids[@]}"; pids=(); fi
  done
  for p in "${pids[@]}"; do wait "$p"; done
  ar rcs "$out/libespnow.a" "${objs[@]}"
}

# 1) the component + shim, instrumented, for the fuzzers
build_lib "$B/fuzz" -O1 $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.c -o "$B/fuzz/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/fuzz/standalone_main.o"

TARGETS=(fuzz_espnow_recv fuzz_ota_responder fuzz_ota_initiator fuzz_sec_responder fuzz_sec_initiator
         fuzz_ctrl fuzz_prov fuzz_time fuzz_console fuzz_iperf)
for t in "${TARGETS[@]}"; do
  $CC -O1 $SANITIZER_FLAGS $DEBUG_FLAGS "${WFLAGS[@]}" "${INC[@]}" -fsanitize=fuzzer-no-link \
      -c "mayhem/harness/$t.c" -o "$B/fuzz/$t.o"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$B/fuzz/$t.o" "$B/fuzz/lsan_off.o" \
      "$B/fuzz/libespnow.a" "${LIBS[@]}" -o "$B/$t"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS "$B/fuzz/standalone_main.o" "$B/fuzz/$t.o" "$B/fuzz/lsan_off.o" \
      "$B/fuzz/libespnow.a" "${LIBS[@]}" -o "$B/$t-standalone"
done

# 2) the functional self-test, with normal flags
build_lib "$B/test" -O2 -g $COVERAGE_FLAGS
$CC -O2 -g $COVERAGE_FLAGS "${WFLAGS[@]}" "${INC[@]}" -D_GNU_SOURCE mayhem/test/espnow_selftest.c "$B/test/libespnow.a" \
    "${LIBS[@]}" $COVERAGE_FLAGS -o "$B/espnow_selftest"

echo "build.sh: built ${TARGETS[*]} + espnow_selftest in $SRC/$B"
