#!/usr/bin/env bash
# run-single.sh — Run one scenario interactively.
#
# Usage:
#   bash scripts/run-single.sh <ns3-root> <topology> <routing> [extra-args]
#
# Examples:
#   bash scripts/run-single.sh ns-allinone-3.40/ns-3.40 fat-tree spray
#   bash scripts/run-single.sh ns-allinone-3.40/ns-3.40 spine-leaf ecmp --simTime=10
#   bash scripts/run-single.sh ns-allinone-3.40/ns-3.40 fat-tree spray --k=8 --pcap=true

set -euo pipefail

NS3_DIR="${1:?Usage: $0 <ns3-root> <fat-tree|spine-leaf> <ecmp|spray> [args]}"
TOPOLOGY="${2:?Topology: fat-tree | spine-leaf}"
ROUTING="${3:?Routing: ecmp | spray}"
shift 3 || true
EXTRA="$*"

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RESULTS="${REPO_DIR}/results"
mkdir -p "${RESULTS}"
ln -sfn "${RESULTS}" "${NS3_DIR}/results" 2>/dev/null || true

SCRIPT="${TOPOLOGY}-simulation"

echo "Running: ${SCRIPT} --routing=${ROUTING} ${EXTRA}"

if [ -f "${NS3_DIR}/ns3" ]; then
    cd "${NS3_DIR}" && python3 ns3 run "${SCRIPT} --routing=${ROUTING} ${EXTRA}"
else
    cd "${NS3_DIR}" && python3 waf --run "${SCRIPT} --routing=${ROUTING} ${EXTRA}"
fi
