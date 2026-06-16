#!/usr/bin/env bash
# setup.sh — Download, build NS-3, and install the spray-routing module.
#
# Usage:
#   bash scripts/setup.sh [NS3_VERSION]   (default: 3.40)
#
# After completion, run simulations with:
#   bash scripts/run-simulations.sh
#
# Prerequisites (Ubuntu/Debian):
#   sudo apt update
#   sudo apt install -y g++ python3 cmake ninja-build git \
#       libboost-all-dev libgsl-dev python3-pip
#   pip3 install matplotlib numpy seaborn pandas

set -euo pipefail

NS3_VERSION="${1:-3.40}"
NS3_DIR="ns-allinone-${NS3_VERSION}/ns-${NS3_VERSION}"
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"

echo "========================================================"
echo "  NS-3 Packet Spraying Setup"
echo "  NS-3 version : ${NS3_VERSION}"
echo "  Project root : ${REPO_DIR}"
echo "========================================================"

# ── Step 1: Download NS-3 ─────────────────────────────────────────────
if [ ! -d "${NS3_DIR}" ]; then
    echo "[1] Downloading NS-3 ${NS3_VERSION}..."
    wget -q --show-progress \
        "https://www.nsnam.org/releases/ns-allinone-${NS3_VERSION}.tar.bz2"
    tar -xjf "ns-allinone-${NS3_VERSION}.tar.bz2"
    echo "    Done."
else
    echo "[1] NS-3 ${NS3_VERSION} already present."
fi

# ── Step 2: Install spray-routing module ──────────────────────────────
echo "[2] Installing spray-routing module..."
SPRAY_SRC="${REPO_DIR}/src/spray-routing"
SPRAY_DST="${NS3_DIR}/src/spray-routing"

if [ -d "${SPRAY_DST}" ]; then
    rm -rf "${SPRAY_DST}"
fi
cp -r "${SPRAY_SRC}" "${SPRAY_DST}"
echo "    Installed to ${SPRAY_DST}"

# ── Step 3: Copy simulation scripts ───────────────────────────────────
echo "[3] Copying simulation scripts to NS-3 scratch/..."
cp "${REPO_DIR}/scratch/fat-tree-simulation.cc"  "${NS3_DIR}/scratch/"
cp "${REPO_DIR}/scratch/spine-leaf-simulation.cc" "${NS3_DIR}/scratch/"
echo "    Copied."

# Fix include paths for NS-3 scratch (scratch is one level above src/)
sed -i 's|../src/spray-routing/|../src/spray-routing/|g' \
    "${NS3_DIR}/scratch/fat-tree-simulation.cc"  || true
sed -i 's|../src/spray-routing/|../src/spray-routing/|g' \
    "${NS3_DIR}/scratch/spine-leaf-simulation.cc" || true

# Create results directory inside NS-3 root (simulations run from there)
mkdir -p "${NS3_DIR}/results"

# ── Step 4: Build NS-3 ────────────────────────────────────────────────
echo "[4] Building NS-3 (this takes a few minutes)..."
cd "${NS3_DIR}"

# Try cmake/ns3 script first (NS-3.36+), fall back to waf
if [ -f "./ns3" ]; then
    python3 ns3 configure --enable-examples --disable-python 2>&1 | tail -5
    python3 ns3 build 2>&1 | tail -20
elif [ -f "./waf" ]; then
    python3 waf configure --enable-examples 2>&1 | tail -5
    python3 waf build 2>&1 | tail -20
else
    echo "ERROR: neither ./ns3 nor ./waf found in ${NS3_DIR}"
    exit 1
fi

echo "[4] Build complete."
echo ""
echo "========================================================"
echo "  Setup complete!  Next steps:"
echo ""
echo "  cd ${NS3_DIR}"
echo "  bash ${REPO_DIR}/scripts/run-simulations.sh ${NS3_DIR}"
echo "========================================================"
