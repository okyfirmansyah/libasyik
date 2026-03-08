#!/usr/bin/env bash
# benchmarks/setup.sh
#
# Sets up the full benchmark environment:
#   1. Installs wrk (HTTP load generator)
#   2. Installs Go (for GIN server)
#   3. Fetches GIN dependencies (go mod tidy)
#   4. Builds libasyik bench_server (Release, -O3)
#   5. Builds GIN bench_gin binary
#   6. Builds Boost.Beast direct bench_beast binary
#
# Run once from the repo root:
#   bash benchmarks/setup.sh
#
# Tested on Ubuntu 20.04 x86_64 (matches the dev container).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# ── Privilege helper ──────────────────────────────────────────────────────────
if [[ $EUID -ne 0 ]] && command -v sudo &>/dev/null; then
    SUDO="sudo"
else
    SUDO=""
fi

# ── Colour helpers ────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()    { echo -e "${CYAN}[setup]${NC} $*"; }
success() { echo -e "${GREEN}[setup]${NC} $*"; }
warn()    { echo -e "${YELLOW}[setup]${NC} $*"; }
die()     { echo -e "${RED}[setup] ERROR:${NC} $*" >&2; exit 1; }

# ── Configuration ─────────────────────────────────────────────────────────────
GO_VERSION="${GO_VERSION:-1.22.0}"
GO_ARCH="linux-amd64"
GO_TARBALL="go${GO_VERSION}.${GO_ARCH}.tar.gz"
GO_URL="https://go.dev/dl/${GO_TARBALL}"
GO_INSTALL_DIR="/usr/local"
BUILD_BENCH_DIR="${REPO_ROOT}/build_bench"

# ── 1. Install wrk ─────────────────────────────────────────────────────────────
info "Step 1/5: Installing wrk (HTTP load generator) ..."
if command -v wrk &>/dev/null; then
    success "wrk already installed: $(wrk --version 2>&1 | head -1)"
else
    $SUDO apt-get update -qq
    # wrk is in Ubuntu repos from 20.04 onward
    if ! $SUDO apt-get install -y wrk 2>/dev/null; then
        info "apt wrk not found – building from source ..."
        $SUDO apt-get install -y build-essential libssl-dev git
        TMP_WRK=$(mktemp -d)
        git clone --depth=1 https://github.com/wg/wrk.git "${TMP_WRK}/wrk"
        make -C "${TMP_WRK}/wrk" -j"$(nproc)"
        $SUDO cp "${TMP_WRK}/wrk/wrk" /usr/local/bin/wrk
        rm -rf "${TMP_WRK}"
    fi
    success "wrk installed: $(wrk --version 2>&1 | head -1)"
fi

# ── 2. Install Go ──────────────────────────────────────────────────────────────
info "Step 2/5: Installing Go ${GO_VERSION} ..."
if [[ -x "${GO_INSTALL_DIR}/go/bin/go" ]]; then
    installed_ver="$("${GO_INSTALL_DIR}/go/bin/go" version | awk '{print $3}' | sed 's/go//')"
    if [[ "${installed_ver}" == "${GO_VERSION}" ]]; then
        success "Go ${GO_VERSION} already installed."
    else
        warn "Different Go version found (${installed_ver}). Upgrading to ${GO_VERSION} ..."
        ${SUDO:-} rm -rf "${GO_INSTALL_DIR}/go"
        wget -q --show-progress -O "/tmp/${GO_TARBALL}" "${GO_URL}"
        ${SUDO:-} tar -C "${GO_INSTALL_DIR}" -xzf "/tmp/${GO_TARBALL}"
        rm -f "/tmp/${GO_TARBALL}"
        success "Go ${GO_VERSION} installed."
    fi
else
    wget -q --show-progress -O "/tmp/${GO_TARBALL}" "${GO_URL}"
    ${SUDO} tar -C "${GO_INSTALL_DIR}" -xzf "/tmp/${GO_TARBALL}"
    rm -f "/tmp/${GO_TARBALL}"
    success "Go ${GO_VERSION} installed at ${GO_INSTALL_DIR}/go"
fi

# Add Go to PATH for this session
export PATH="${GO_INSTALL_DIR}/go/bin:${PATH}"
go version

# ── 3. Fetch GIN dependencies ─────────────────────────────────────────────────
info "Step 3/5: Fetching GIN dependencies ..."
cd "${SCRIPT_DIR}/gin"
# GONOSUMDB=* skips sum.golang.org verification (appropriate in dev containers)
GOPATH="${HOME}/go" GONOSUMDB="*" GOFLAGS="-mod=mod" go mod tidy
success "GIN dependencies ready."

# ── 4. Build libasyik bench_server ────────────────────────────────────────────
info "Step 4/6: Building libasyik bench_server (Release / -O3) ..."
mkdir -p "${BUILD_BENCH_DIR}"
cd "${BUILD_BENCH_DIR}"

cmake "${REPO_ROOT}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLIBASYIK_BUILD_BENCHMARKS=ON \
    -DLIBASYIK_ENABLE_SOCI=OFF \
    -DLIBASYIK_ENABLE_SSL_SERVER=OFF \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF \
    -G "Unix Makefiles" \
    -Wno-dev

cmake --build . --target bench_server --parallel "$(nproc)"
cmake --build . --target bench_beast  --parallel "$(nproc)"

BENCH_BIN="${BUILD_BENCH_DIR}/benchmarks/bench_server"
[[ -x "${BENCH_BIN}" ]] || die "bench_server binary not found at ${BENCH_BIN}"
success "Built: ${BENCH_BIN}"

BEAST_BENCH_BIN="${BUILD_BENCH_DIR}/benchmarks/bench_beast"
[[ -x "${BEAST_BENCH_BIN}" ]] || die "bench_beast binary not found at ${BEAST_BENCH_BIN}"
success "Built: ${BEAST_BENCH_BIN}"

# ── 5. Build GIN bench binary ──────────────────────────────────────────────────────
info "Step 5/6: Building GIN bench_gin binary ..."
cd "${SCRIPT_DIR}/gin"
GOPATH="${HOME}/go" GONOSUMDB="*" go build -ldflags="-s -w" -o bench_gin .
success "Built: ${SCRIPT_DIR}/gin/bench_gin"

# ── 6. bench_beast is already built in step 4 alongside bench_server ─────────────────────────────────────
info "Step 6/6: bench_beast already built in step 4 (same CMake build tree) — done."

# ── Done ───────────────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║          Benchmark environment ready!                ║${NC}"
echo -e "${GREEN}╠══════════════════════════════════════════════════════╣${NC}"
echo -e "${GREEN}║  libasyik server: ${BUILD_BENCH_DIR}/benchmarks/bench_server  ${NC}"
echo -e "${GREEN}║  GIN server:      ${SCRIPT_DIR}/gin/bench_gin        ${NC}"
echo -e "${GREEN}║  Beast server:    ${BUILD_BENCH_DIR}/benchmarks/bench_beast    ${NC}"
echo -e "${GREEN}║  wrk:             $(command -v wrk)                  ${NC}"
echo -e "${GREEN}╠══════════════════════════════════════════════════════╣${NC}"
echo -e "${GREEN}║  Run benchmarks:                                     ║${NC}"
echo -e "${GREEN}║    bash benchmarks/run_benchmark.sh --target=all     ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════╝${NC}"
