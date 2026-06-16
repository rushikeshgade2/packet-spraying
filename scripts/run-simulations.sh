#!/usr/bin/env bash
# run-simulations.sh — Run all four NS-3 scenarios and generate plots.
#
# Usage (from the project root, after setup.sh):
#   bash scripts/run-simulations.sh <ns3-root-dir>
#
# Example:
#   bash scripts/run-simulations.sh ns-allinone-3.40/ns-3.40
#
# Optional env vars:
#   K           Fat-tree pod count     (default 4)
#   SIM_TIME    Simulation time (s)    (default 5)
#   ELEPHANTS   Elephant flow count    (default 10)
#   MICE        Mouse flow count       (default 40)
#   SEED        RNG seed               (default 1)

set -euo pipefail

NS3_DIR="${1:-}"
if [ -z "${NS3_DIR}" ]; then
    echo "Usage: $0 <ns3-root-dir>"
    echo "  e.g. $0 ns-allinone-3.40/ns-3.40"
    exit 1
fi

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RESULTS="${REPO_DIR}/results"
mkdir -p "${RESULTS}"

K="${K:-4}"
SIM_TIME="${SIM_TIME:-5}"
ELEPHANTS="${ELEPHANTS:-10}"
MICE="${MICE:-40}"
SEED="${SEED:-1}"

run_ns3() {
    local script="$1"; shift
    local args="$*"
    if [ -f "${NS3_DIR}/ns3" ]; then
        cd "${NS3_DIR}" && python3 ns3 run "${script} ${args}" 2>&1
    else
        cd "${NS3_DIR}" && python3 waf --run "${script} ${args}" 2>&1
    fi
}

echo "========================================================"
echo "  Packet Spraying Simulations"
echo "  NS-3 : ${NS3_DIR}"
echo "  k=${K}  simTime=${SIM_TIME}s  elephants=${ELEPHANTS}  mice=${MICE}"
echo "========================================================"

# Symlink results dir into NS3 dir so simulations can write there
ln -sfn "${RESULTS}" "${NS3_DIR}/results" 2>/dev/null || true

# ── Fat-Tree ECMP ─────────────────────────────────────────────────────
echo ""
echo "[1/4] Fat-Tree / ECMP ..."
run_ns3 "fat-tree-simulation" \
    "--k=${K} --routing=ecmp --simTime=${SIM_TIME} \
     --elephants=${ELEPHANTS} --mice=${MICE} --seed=${SEED} \
     --flowmon=true --pcap=false" \
    | tee "${RESULTS}/fat-tree-ecmp.log"

# ── Fat-Tree Packet Spray ─────────────────────────────────────────────
echo ""
echo "[2/4] Fat-Tree / Packet Spraying ..."
run_ns3 "fat-tree-simulation" \
    "--k=${K} --routing=spray --simTime=${SIM_TIME} \
     --elephants=${ELEPHANTS} --mice=${MICE} --seed=${SEED} \
     --flowmon=true --pcap=false" \
    | tee "${RESULTS}/fat-tree-spray.log"

# ── Spine-Leaf ECMP ───────────────────────────────────────────────────
echo ""
echo "[3/4] Spine-Leaf / ECMP ..."
run_ns3 "spine-leaf-simulation" \
    "--numSpine=4 --numLeaf=8 --hostsPerLeaf=${K} \
     --routing=ecmp --simTime=${SIM_TIME} \
     --elephants=${ELEPHANTS} --mice=${MICE} --seed=${SEED} \
     --flowmon=true --pcap=false" \
    | tee "${RESULTS}/spine-leaf-ecmp.log"

# ── Spine-Leaf Packet Spray ───────────────────────────────────────────
echo ""
echo "[4/4] Spine-Leaf / Packet Spraying ..."
run_ns3 "spine-leaf-simulation" \
    "--numSpine=4 --numLeaf=8 --hostsPerLeaf=${K} \
     --routing=spray --simTime=${SIM_TIME} \
     --elephants=${ELEPHANTS} --mice=${MICE} --seed=${SEED} \
     --flowmon=true --pcap=false" \
    | tee "${RESULTS}/spine-leaf-spray.log"

# ── Analysis & plots ──────────────────────────────────────────────────
echo ""
echo "[5/5] Analysing results and generating plots..."
cd "${REPO_DIR}"
python3 scripts/analyze.py --results-dir "${RESULTS}"

echo ""
echo "========================================================"
echo "  All done!  Results and plots are in: ${RESULTS}/"
echo "========================================================"
ls -lh "${RESULTS}/"
