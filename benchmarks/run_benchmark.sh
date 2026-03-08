#!/usr/bin/env bash
# benchmarks/run_benchmark.sh
#
# Runs the full HTTP benchmark suite against libasyik and/or GIN.
# Requires: wrk, bench_server (libasyik), bench_gin (GIN), bench_beast – run setup.sh first.
#
# Usage:
#   bash benchmarks/run_benchmark.sh [OPTIONS]
#
# Options:
#   --target=<libasyik|gin|beast|all>  Which server(s) to benchmark  (default: all)
#   --port=<N>                    Port for libasyik server      (default: 8080)
#   --gin-port=<N>                Port for GIN server           (default: 8082)
#   --beast-port=<N>              Port for Beast server         (default: 8084)
#   --duration=<N>                Seconds per wrk run           (default: 20)
#   --threads=<N>                 wrk worker threads            (default: 4)
#   --concurrency=<a,b,c,...>     Comma-separated concurrencies (default: 50,100,200,500)
#   --delay-ms=<N>                Delay for scenario D in ms    (default: 5)
#   --thread-multiplier=<N>       ASYIK_THREAD_MULTIPLIER       (default: 4)
#   --output-dir=<PATH>           Where to save raw results     (default: benchmarks/results/TIMESTAMP)
#
# Example:
#   bash benchmarks/run_benchmark.sh --target=all --concurrency=100,200,500 --duration=30

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# ── Colour helpers ─────────────────────────────────────────────────────────────
BOLD='\033[1m'; CYAN='\033[0;36m'; GREEN='\033[0;32m'
YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
info()    { echo -e "${CYAN}[bench]${NC} $*"; }
header()  { echo -e "\n${BOLD}${YELLOW}$*${NC}"; }
success() { echo -e "${GREEN}[bench]${NC} $*"; }
warn()    { echo -e "${YELLOW}[bench] WARN:${NC} $*"; }
die()     { echo -e "${RED}[bench] ERROR:${NC} $*" >&2; exit 1; }

# ── Default parameters ─────────────────────────────────────────────────────────
TARGET="all"
PORT=8080
GIN_PORT=8082
BEAST_PORT=8086
DURATION=20
THREADS=4
CONCURRENCY="50,100,200,500"
DELAY_CONCURRENCY="100,200,500,1000"
DELAY_MS=5
THREAD_MULTIPLIER=4
OUTPUT_DIR=""

# ── Parse arguments ────────────────────────────────────────────────────────────
for arg in "$@"; do
    case "${arg}" in
        --target=*)           TARGET="${arg#*=}" ;;
        --port=*)             PORT="${arg#*=}" ;;
        --gin-port=*)         GIN_PORT="${arg#*=}" ;;
        --beast-port=*)       BEAST_PORT="${arg#*=}" ;;
        --duration=*)         DURATION="${arg#*=}" ;;
        --threads=*)          THREADS="${arg#*=}" ;;
        --concurrency=*)      CONCURRENCY="${arg#*=}" ;;
        --delay-ms=*)         DELAY_MS="${arg#*=}" ;;
        --thread-multiplier=*) THREAD_MULTIPLIER="${arg#*=}" ;;
        --output-dir=*)       OUTPUT_DIR="${arg#*=}" ;;
        --help|-h)
            sed -n '/^# Usage:/,/^$/p' "$0"
            exit 0
            ;;
        *) die "Unknown argument: ${arg}" ;;
    esac
done

[[ "${OUTPUT_DIR}" == "" ]] && OUTPUT_DIR="${SCRIPT_DIR}/results/$(date +%Y%m%d_%H%M%S)"

LIBASYIK_BIN="${REPO_ROOT}/build_bench/benchmarks/bench_server"
GIN_BIN="${SCRIPT_DIR}/gin/bench_gin"
BEAST_BIN="${REPO_ROOT}/build_bench/benchmarks/bench_beast"
SCRIPTS_DIR="${SCRIPT_DIR}/scripts"

GO_BIN="/usr/local/go/bin"
export PATH="${GO_BIN}:${PATH}"

# ── Pre-flight checks ──────────────────────────────────────────────────────────
command -v wrk &>/dev/null || die "wrk not found. Run: bash benchmarks/setup.sh"

if [[ "${TARGET}" == "libasyik" || "${TARGET}" == "all" ]]; then
    [[ -x "${LIBASYIK_BIN}" ]] || \
        die "bench_server not found at ${LIBASYIK_BIN}. Run: bash benchmarks/setup.sh"
fi
if [[ "${TARGET}" == "gin" || "${TARGET}" == "all" ]]; then
    [[ -x "${GIN_BIN}" ]] || \
        die "bench_gin not found at ${GIN_BIN}. Run: bash benchmarks/setup.sh"
fi
if [[ "${TARGET}" == "beast" || "${TARGET}" == "all" ]]; then
    [[ -x "${BEAST_BIN}" ]] || \
        die "bench_beast not found at ${BEAST_BIN}. Run: bash benchmarks/setup.sh"
fi

mkdir -p "${OUTPUT_DIR}"

# ── Utility: wait for server to accept connections ─────────────────────────────
wait_for_server() {
    local port="$1"
    local max_wait=30
    info "Waiting for server on port ${port} ..."
    for i in $(seq 1 "${max_wait}"); do
        if wget -q -O /dev/null "http://127.0.0.1:${port}/plaintext" >/dev/null 2>&1; then
            success "Server ready after ${i}×0.5s"
            return 0
        fi
        sleep 0.5
    done
    die "Server on port ${port} did not start within ${max_wait}s"
}

# ── Utility: kill server by PID ────────────────────────────────────────────────
stop_server() {
    local pid="$1"
    if kill -0 "${pid}" 2>/dev/null; then
        kill "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
        info "Server (PID ${pid}) stopped."
    fi
}

# ── Utility: wait until nothing is listening on a port ────────────────────────
# Reads /proc/net/tcp and /proc/net/tcp6 (hex port in the local address column).
# Only checks for LISTEN state (0A) — ignores TIME_WAIT connections from wrk.
wait_for_port_free() {
    local port="$1"
    local hex_port
    hex_port=$(printf "%04X" "${port}")
    local max_wait=30
    info "Waiting for port ${port} listener to stop ..."
    for i in $(seq 1 "${max_wait}"); do
        # /proc/net/tcp format: "XXXXXXXX:PORT YYYYYYYY:PPPP STATE ..."
        # State 0A = TCP_LISTEN. We only wait for the listening socket to close;
        # TIME_WAIT (06) connections from wrk are harmless for re-binding.
        if ! grep -qiE "[0-9A-Fa-f]{8}:${hex_port}[[:space:]]+[0-9A-Fa-f]{8}:[0-9A-Fa-f]{4}[[:space:]]+0A" \
             /proc/net/tcp /proc/net/tcp6 2>/dev/null; then
            success "Port ${port} listener is gone after ${i}s"
            return 0
        fi
        sleep 1
    done
    warn "Port ${port} listener still present after ${max_wait}s — proceeding anyway"
}

# ── Utility: parse key metrics from a wrk output file ─────────────────────────
# Outputs: "RPS | p50 | p99"
parse_wrk() {
    local file="$1"
    local rps p50 p99
    rps=$(grep "Requests/sec:" "${file}"  | awk '{printf "%.0f", $2}')
    p50=$(grep -A5 "Latency Distribution" "${file}" | grep "50%" | awk '{print $2}')
    p99=$(grep -A5 "Latency Distribution" "${file}" | grep "99%" | awk '{print $2}')
    echo "${rps:-N/A} | ${p50:-N/A} | ${p99:-N/A}"
}

# ── Utility: run wrk for one (scenario, concurrency) pair ─────────────────────
run_wrk() {
    local label="$1"     # human label, also used as filename prefix
    local url="$2"
    local conc="$3"
    local extra="${4:-}"  # optional: -s script.lua
    local outfile="${OUTPUT_DIR}/${label}_c${conc}.txt"

    info "  wrk -t${THREADS} -c${conc} -d${DURATION}s --latency ${extra} ${url}"
    wrk -t"${THREADS}" -c"${conc}" -d"${DURATION}s" --latency ${extra} "${url}" \
        | tee "${outfile}"
}

# ── Core: run all scenarios for one framework ──────────────────────────────────
run_all_scenarios() {
    local framework="$1"
    local port="$2"
    local label_prefix="${3}"          # e.g. "libasyik" or "gin"
    local base_url="http://127.0.0.1:${port}"

    # Summary table rows accumulated here
    local summary_file="${OUTPUT_DIR}/${label_prefix}_summary.txt"
    printf "%-30s | %-12s | %-12s | %-8s | %-8s\n" \
        "Scenario" "Concurrency" "RPS" "p50 lat" "p99 lat" >> "${summary_file}"
    printf -- "%-30s-+-%-12s-+-%-12s-+-%-8s-+-%-8s\n" \
        "------------------------------" "------------" "------------" "--------" "--------" \
        >> "${summary_file}"

    append_summary() {
        local scenario="$1" conc="$2" outfile="$3"
        local parsed
        parsed=$(parse_wrk "${outfile}")
        local rps p50 p99
        IFS='|' read -r rps p50 p99 <<< "${parsed}"
        printf "%-30s | %-12s | %-12s | %-8s | %-8s\n" \
            "${scenario}" "${conc}" "$(echo "${rps}" | xargs)" \
            "$(echo "${p50}" | xargs)" "$(echo "${p99}" | xargs)" \
            >> "${summary_file}"
    }

    # ── A: Plaintext ─────────────────────────────────────────────────────────
    header "── [${framework}] Scenario A: GET /plaintext (baseline throughput) ──"
    IFS=',' read -ra CONCS <<< "${CONCURRENCY}"
    for c in "${CONCS[@]}"; do
        run_wrk "${label_prefix}_A_plaintext" "${base_url}/plaintext" "${c}"
        append_summary "A: plaintext" "${c}" "${OUTPUT_DIR}/${label_prefix}_A_plaintext_c${c}.txt"
        sleep 1
    done

    # ── B: JSON ───────────────────────────────────────────────────────────────
    header "── [${framework}] Scenario B: GET /json (JSON response) ──"
    for c in "${CONCS[@]}"; do
        run_wrk "${label_prefix}_B_json" "${base_url}/json" "${c}"
        append_summary "B: json" "${c}" "${OUTPUT_DIR}/${label_prefix}_B_json_c${c}.txt"
        sleep 1
    done

    # ── C: POST echo (small payload ~200B) ────────────────────────────────────
    header "── [${framework}] Scenario C: POST /echo (small JSON body) ──"
    for c in "${CONCS[@]}"; do
        run_wrk "${label_prefix}_C_echo_small" "${base_url}/echo" "${c}" \
            "-s ${SCRIPTS_DIR}/post_echo.lua"
        append_summary "C: echo-small" "${c}" \
            "${OUTPUT_DIR}/${label_prefix}_C_echo_small_c${c}.txt"
        sleep 1
    done

    # ── C2: POST echo (large payload ~4KB) ────────────────────────────────────
    header "── [${framework}] Scenario C2: POST /echo (large ~4KB body) ──"
    for c in "${CONCS[@]}"; do
        run_wrk "${label_prefix}_C_echo_large" "${base_url}/echo" "${c}" \
            "-s ${SCRIPTS_DIR}/post_large.lua"
        append_summary "C2: echo-large-4KB" "${c}" \
            "${OUTPUT_DIR}/${label_prefix}_C_echo_large_c${c}.txt"
        sleep 1
    done

    # ── D: Simulated I/O delay (fiber/goroutine scheduler stress) ─────────────
    header "── [${framework}] Scenario D: GET /delay/${DELAY_MS} (${DELAY_MS}ms I/O sim) ──"
    IFS=',' read -ra DCONCS <<< "${DELAY_CONCURRENCY}"
    for c in "${DCONCS[@]}"; do
        run_wrk "${label_prefix}_D_delay${DELAY_MS}ms" \
            "${base_url}/delay/${DELAY_MS}" "${c}"
        append_summary "D: delay-${DELAY_MS}ms" "${c}" \
            "${OUTPUT_DIR}/${label_prefix}_D_delay${DELAY_MS}ms_c${c}.txt"
        sleep 1
    done

    echo ""
    success "Summary for ${framework}:"
    cat "${summary_file}"
}

# ── Benchmark runner for libasyik ──────────────────────────────────────────────
bench_libasyik() {
    header "══════════════════════════════════════════════════════════"
    header "  TARGET: libasyik  (port ${PORT})"
    header "  Thread multiplier: ASYIK_THREAD_MULTIPLIER=${THREAD_MULTIPLIER}"
    header "══════════════════════════════════════════════════════════"

    # Kill any stale bench_server processes before starting fresh
    pkill -9 bench_server 2>/dev/null || true; sleep 1

    ASYIK_THREAD_MULTIPLIER="${THREAD_MULTIPLIER}" "${LIBASYIK_BIN}" "${PORT}" \
        >"${OUTPUT_DIR}/libasyik_server.log" 2>&1 &
    SERVER_PID=$!
    trap "stop_server ${SERVER_PID}" EXIT

    wait_for_server "${PORT}"
    run_all_scenarios "libasyik" "${PORT}" "libasyik"

    stop_server "${SERVER_PID}"
    trap - EXIT
}

# ── Benchmark runner for Boost.Beast (direct, no libasyik) ───────────────────
bench_beast() {
    header "══════════════════════════════════════════════════════════"
    header "  TARGET: Boost.Beast direct  (port ${BEAST_PORT})"
    header "  (raw Beast/Asio — theoretical performance ceiling)"
    header "══════════════════════════════════════════════════════════"

    pkill -9 bench_beast 2>/dev/null || true; sleep 1

    BEAST_THREAD_MULTIPLIER="${THREAD_MULTIPLIER}" "${BEAST_BIN}" "${BEAST_PORT}" \
        >"${OUTPUT_DIR}/beast_server.log" 2>&1 &
    SERVER_PID=$!
    trap "stop_server ${SERVER_PID}" EXIT

    wait_for_server "${BEAST_PORT}"
    run_all_scenarios "Beast" "${BEAST_PORT}" "beast"

    stop_server "${SERVER_PID}"
    trap - EXIT
}

# ── Benchmark runner for GIN ───────────────────────────────────────────────────
bench_gin() {
    header "══════════════════════════════════════════════════════════"
    header "  TARGET: GIN  (port ${GIN_PORT})"
    header "══════════════════════════════════════════════════════════"

    # Kill any stale bench_gin processes before starting fresh
    pkill -9 bench_gin 2>/dev/null || true; sleep 1

    PORT="${GIN_PORT}" "${GIN_BIN}" \
        >"${OUTPUT_DIR}/gin_server.log" 2>&1 &
    SERVER_PID=$!
    trap "stop_server ${SERVER_PID}" EXIT

    wait_for_server "${GIN_PORT}"
    run_all_scenarios "GIN" "${GIN_PORT}" "gin"

    stop_server "${SERVER_PID}"
    trap - EXIT
}

# ── Print benchmark config ─────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}Benchmark Configuration${NC}"
echo "  Target:             ${TARGET}"
echo "  Port (libasyik):    ${PORT}"
echo "  Port (GIN):         ${GIN_PORT}"
echo "  Port (Beast):       ${BEAST_PORT}"
echo "  Duration per run:   ${DURATION}s"
echo "  wrk threads:        ${THREADS}"
echo "  Concurrency levels: ${CONCURRENCY}  (delay scenario: ${DELAY_CONCURRENCY})"
echo "  Delay (scenario D): ${DELAY_MS}ms"
echo "  Results dir:        ${OUTPUT_DIR}"
echo ""

# ── Run selected targets ───────────────────────────────────────────────────────
case "${TARGET}" in
    libasyik)
        bench_libasyik
        ;;
    gin)
        bench_gin
        ;;
    beast)
        bench_beast
        ;;
    all)
        bench_libasyik
        echo ""
        bench_gin
        echo ""
        bench_beast
        # ── Side-by-side comparison ───────────────────────────────────────
        header "══ COMPARISON SUMMARY ══"
        echo ""
        echo -e "${BOLD}libasyik${NC}"
        cat "${OUTPUT_DIR}/libasyik_summary.txt" 2>/dev/null || echo "(no data)"
        echo ""
        echo -e "${BOLD}GIN${NC}"
        cat "${OUTPUT_DIR}/gin_summary.txt" 2>/dev/null || echo "(no data)"
        echo ""
        echo -e "${BOLD}Boost.Beast (direct — theoretical ceiling)${NC}"
        cat "${OUTPUT_DIR}/beast_summary.txt" 2>/dev/null || echo "(no data)"
        ;;
    *)
        die "Unknown target '${TARGET}'. Use: libasyik | gin | beast | all"
        ;;
esac

echo ""
success "All results saved to: ${OUTPUT_DIR}"
echo "  Raw wrk files: ${OUTPUT_DIR}/*.txt"
echo "  Server logs:   ${OUTPUT_DIR}/*_server.log"
echo ""
echo "Tips for better accuracy:"
echo "  • Run on dedicated hardware (no other heavy processes)"
echo "  • For cross-machine testing: run server here, wrk on a separate machine"
echo "  • Re-run 2-3 times and average; discard first warm-up run"
echo "  • Use --duration=60 for production-quality numbers"
