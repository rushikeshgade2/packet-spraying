# Packet Spraying of Elephant Flows
### NS-3 Simulation — Fat-Tree & Spine-Leaf Data-Centre Topologies

**Course project** comparing **ECMP** vs **Packet Spraying** for handling elephant
flows in data-centre networks.

---

## Quick Start

```bash
# Clone the repo
git clone https://github.com/rushikeshgade2/packet-spraying.git
cd packet-spraying

# Run everything (setup + simulate + plots)
bash run.sh
```

That single script:
1. Checks your system has `g++`, `python3`, `cmake`, `ninja`, `wget`, `tar`
2. Downloads NS-3.40 (~90 MB, one-time)
3. Installs the custom `spray-routing` module and builds NS-3 (~10 min first run)
4. Runs all 4 simulation scenarios
5. Generates comparison plots in `results/`

**After the first run, re-running takes ~1–2 minutes** (NS-3 is already built).

---

## Run in WSL + VS Code (Windows)

**1. Install WSL**
```powershell
wsl --install -d Ubuntu
```

**2. Install VS Code** on Windows, then add the WSL extension:
Extensions (`Ctrl+Shift+X`) → search **WSL** (by Microsoft) → Install.

**3. Open a WSL terminal**
Launch Ubuntu from the Start menu, or in VS Code press `Ctrl+Shift+P` →
`WSL: Connect to WSL`, then open a terminal with ``Ctrl+` ``.
You should see a Linux prompt such as `you@machine:~$`.

**4. Put the project on the Linux filesystem**

> **Important:** keep the project under your Linux home (`~`), **not** under `/mnt/c/`.
> Building on the Windows drive is far slower and can cause permission errors.

```bash
cd ~
mkdir -p projects && cd projects
cp "/mnt/c/Users/YOUR_WINDOWS_USERNAME/Downloads/packet-spraying-main.zip" .
unzip packet-spraying-main.zip
cd packet-spraying-main
bash run.sh
```

### System Requirements

| Requirement | Version |
|-------------|---------|
| OS | Ubuntu 20.04 / 22.04 / Debian (Linux) |
| C++ compiler | g++ 9+ |
| Python | 3.8+ |
| NS-3 | 3.40 (downloaded automatically) |
| RAM | ≥ 4 GB |
| Disk | ≥ 2 GB free |

Install dependencies (if needed):
```bash
sudo apt update
sudo apt install -y g++ python3 cmake ninja-build wget tar unzip
pip3 install matplotlib numpy
```

If `pip3` refuses with an *"externally-managed-environment"* error
(Ubuntu 23.04+ / Debian 12+), install through apt instead:
```bash
sudo apt install -y python3-matplotlib python3-numpy
```

---

## Simulation Parameters (as configured in the source)

These are the **actual values compiled into the simulation scripts** —
`scratch/fat-tree-simulation.cc` and `scratch/spine-leaf-simulation.cc`.

### Link Configuration

| Link | Bandwidth | Propagation Delay |
|------|-----------|-------------------|
| Core ↔ Aggregation (Fat-Tree) | **1 Gbps** | **5 µs** |
| Aggregation ↔ Edge (Fat-Tree) | **1 Gbps** | **5 µs** |
| Edge ↔ Host (Fat-Tree) | **1 Gbps** | **10 µs** |
| Spine ↔ Leaf (Spine-Leaf) | **1 Gbps** | **5 µs** |
| Leaf ↔ Host (Spine-Leaf) | **1 Gbps** | **10 µs** |

All links are `PointToPointHelper` links. Every link is assigned its own
**/30 subnet** (`255.255.255.252`) from the `10.x.y.0` address pool.

### Traffic Configuration

| Parameter | Value |
|-----------|-------|
| **Elephant flows** | **40** |
| Elephant application | `BulkSendApplication` over **TCP** |
| Elephant transfer size | **50 MB** (`50 × 1024 × 1024` = 52,428,800 bytes) |
| Elephant `SendSize` | **1448 bytes** |
| Elephant destination port | 5001 onwards (one port per flow) |
| Elephant marking | **IP TOS = `0x10`** |
| Elephant start time | Uniform random in **[0, 0.5] s** |
| **Mouse flows** | **40** |
| Mouse application | `OnOffApplication` over **UDP** |
| Mouse send rate | **10 Mbps** |
| Mouse packet size | **512 bytes** |
| Mouse On time | `ExponentialRandomVariable[Mean=0.1]` |
| Mouse Off time | `ExponentialRandomVariable[Mean=0.1]` |
| Mouse destination port | 6001 onwards (one port per flow) |
| Mouse start time | Uniform random in **[0, 1.0] s** |
| **Simulation duration** | **5.0 s** (applications stop at `simTime`) |
| Simulator stop | `simTime + 1.0` = **6.0 s** (allows flows to drain) |
| Default RNG seed | **1** |

Total: **80 application flows** (40 elephant + 40 mouse) per scenario, plus the
base sinks. FlowMonitor reports **120 monitored flows** (the extra 40 are the
reverse-direction TCP ACK flows of the elephants).

Base sinks also listen on **port 5000** (TCP) and **port 6000** (UDP) on every host.
Source/destination host pairs are chosen **randomly** per seed, so each `--seed`
produces a different traffic matrix.


> **Note on defaults:** the source code contains different compiled-in
> defaults for the two topology programs. The **final evaluation does not use
> those defaults**. All reported results were produced with
> **`--elephants=40 --mice=40`** passed explicitly, so both topologies are
> compared under the identical 40-elephant / 40-mouse workload. Pass those
> flags to reproduce the final experiments.

### Elephant Classification (post-processing)

`scripts/analyze.py` classifies a flow as an **elephant** if
`rxBytes ≥ 1,000,000` (**1 MB**); everything else is a **mouse** flow.

---

## Topologies Simulated

### 1. Fat-Tree (k = 4)

```
                    ┌──────────────────────────────┐
  Core Layer        │  [C0]  [C1]  [C2]  [C3]      │   4 core switches
                    └────┼────┼────┼────┼───────────┘
                     ╱╲  │   │   │  ╱╲
                    /  ╲ │   │   │ /  ╲
  Aggregation  [A00][A01][A10][A11][A20][A21][A30][A31]  8 agg switches
               ╲╱  ╲╱  ╲╱  ╲╱   ╲╱  ╲╱  ╲╱  ╲╱
  Edge Layer  [E00][E01][E10][E11][E20][E21][E30][E31]  8 edge switches
               ||   ||   ||   ||   ||   ||   ||   ||
  Hosts       H H  H H  H H  H H  H H  H H  H H  H H  16 hosts
```

| Property | Formula | Value (k=4) |
|----------|---------|-------------|
| Pods | k | 4 |
| Aggregation switches per pod | k/2 | 2 |
| Edge switches per pod | k/2 | 2 |
| Core switches | (k/2)² | 4 |
| Total hosts | k³/4 | **16** |
| Equal-cost paths per host pair | (k/2)² | **4** |

Scaling: `--k=6` → 54 hosts, `--k=8` → 128 hosts. `k` must be **even and ≥ 2**.

### 2. Spine-Leaf

```
  Spine  [S0]  [S1]  [S2]  [S3]
          │╲    │╲    │╲    │╲
          │ ╲   │ ╲   │ ╲   │ ╲     (full mesh: every leaf ↔ every spine)
          │  ╲  │  ╲  │  ╲  │  ╲
  Leaf  [L0][L1][L2][L3][L4][L5][L6][L7]
          │    │    │
        H H  H H  H H  ...
```

| Property | Default |
|----------|---------|
| Spine switches (`--numSpine`) | **4** |
| Leaf switches (`--numLeaf`) | **8** |
| Hosts per leaf (`--hostsPerLeaf`) | **4** |
| Total hosts | numLeaf × hostsPerLeaf = **32** |
| Equal-cost paths (inter-leaf) | numSpine = **4** |

Topology is a **full mesh** between spine and leaf: 4 × 8 = **32 spine-leaf links**.

---

## Routing Strategies

### ECMP (Equal-Cost Multi-Path) — Baseline

```
Flow F1:   A ──[hash=path2]──────────────────────► B
Flow F2:   A ──[hash=path2]──(COLLISION!)────────► C
                               ↑
                         path2 is now congested
                         path0, path1, path3 are idle
```

All packets of a flow follow the **same single path**, selected by hashing the
**5-tuple** `(src_ip, dst_ip, src_port, dst_port, protocol)` with
**FNV-1a** (offset basis `2166136261`, prime `16777619`) plus a final avalanche
mix, then taking `hash % |ECMP set|`. Two elephant flows can hash to the same
path and saturate it. This matches what real ECMP hardware does.

### Packet Spraying — Proposed Solution

```
Elephant flow F1 packets:
  pkt1 ──► path0 ──┐
  pkt2 ──► path1 ──┤──► Destination (reorders in TCP buffer)
  pkt3 ──► path2 ──┤
  pkt4 ──► path3 ──┘

Mouse flows: continue to use the 5-tuple hash for flow-level path consistency.
```

For every packet of an elephant flow, a next hop is selected **independently and
uniformly at random** from the equal-cost set using NS-3's
`UniformRandomVariable::GetInteger(0, |ECMP set| - 1)`. Because it uses NS-3's
RNG, runs are **reproducible** and controlled by `RngSeedManager` (the `--seed`
argument).

Spraying is only applied when **both** conditions hold:
`isElephant && m_sprayElephantOnly && ecmpSet.size() > 1`.

**Elephant detection** — a packet is treated as an elephant if **either**:
- the IP header **TOS field is non-zero** (the simulations set `TOS = 0x10`), or
- the packet carries an **`ns3::ElephantTag`**

---

## Results (k=4, seed=1, simTime=5 s, 40 elephants, 40 mice)

Measured from the FlowMonitor XML in `results/`:

| Scenario | Flows | Elephant Tput (Mbps) | Mouse Tput (Mbps) | Elephant FCT (s) | Mouse FCT (s) | Loss % |
|----------|-------|----------------------|-------------------|------------------|---------------|--------|
| Fat-Tree ECMP | 80 | 164.63 | 5.30 | 3.7384 | 4.3862 | 0.004 |
| **Fat-Tree Spray** | 80 | **249.47** | 5.30 | **2.0380** | 4.3859 | **0.000** |
| Spine-Leaf ECMP | 80 | 221.75 | 5.35 | 2.4278 | 5.3636 | 0.002 |
| **Spine-Leaf Spray** | 80 | **493.23** | 5.35 | **1.0142** | 5.3636 | **0.000** |

**Average end-to-end delay** (from the per-scenario `.log` files, 120 monitored flows):

| Scenario | Tx Packets | Rx Packets | Avg Delay |
|----------|------------|------------|-----------|
| Fat-Tree ECMP | 6,237,603 | 6,237,445 | 1.48 ms |
| Fat-Tree Spray | 6,400,919 | 6,400,918 | 0.03 ms |
| Spine-Leaf ECMP | 6,563,783 | 6,563,708 | 1.18 ms |
| Spine-Leaf Spray | 6,400,963 | 6,400,961 | 0.00 ms |

### Key Findings

- **Elephant throughput** improves by **+51.5%** on Fat-Tree
  (164.63 → 249.47 Mbps) and **+122.4%** on Spine-Leaf (221.75 → 493.23 Mbps).
- **Flow Completion Time** for elephants drops by **45.5%** on Fat-Tree
  (3.74 → 2.04 s) and **58.2%** on Spine-Leaf (2.43 → 1.01 s).
- **Packet loss falls to zero** under spraying in both topologies, versus
  0.004% (Fat-Tree) and 0.002% (Spine-Leaf) under ECMP.
- **Mouse flows are unaffected** — throughput is identical to 2 decimal places
  (5.30 / 5.35 Mbps), confirming that mice keep their deterministic hashed path
  and retain deterministic flow-level path selection.
- Spine-Leaf benefits more than Fat-Tree because every inter-leaf path traverses
  exactly one spine, allowing packet spraying to distribute elephant traffic across the available spine paths more evenly.

---

## Output Files

After running, check the `results/` folder:

| File | Description |
|------|-------------|
| `fat-tree-ecmp-flowmon.xml` | Raw NS-3 flow statistics (Fat-Tree, ECMP) |
| `fat-tree-spray-flowmon.xml` | Raw NS-3 flow statistics (Fat-Tree, Spray) |
| `spine-leaf-ecmp-flowmon.xml` | Raw NS-3 flow statistics (Spine-Leaf, ECMP) |
| `spine-leaf-spray-flowmon.xml` | Raw NS-3 flow statistics (Spine-Leaf, Spray) |
| `failure-demo-spray-flowmon.xml` | Raw statistics from the spine-failure demo |
| `Fat-Tree-ECMP.log` | Per-flow console output (Fat-Tree, ECMP) |
| `Fat-Tree-Packet-Spray.log` | Per-flow console output (Fat-Tree, Spray) |
| `Spine-Leaf-ECMP.log` | Per-flow console output (Spine-Leaf, ECMP) |
| `Spine-Leaf-Packet-Spray.log` | Per-flow console output (Spine-Leaf, Spray) |
| `throughput_comparison.png` | Average throughput: elephant vs mouse flows |
| `fct_comparison.png` | Flow Completion Time comparison |
| `delay_comparison.png` | Average packet delay |
| `throughput_cdf.png` | CDF of per-flow throughput |
| `packet_loss.png` | Packet loss rate per scenario |

---

## Project Structure

```
packet-spraying-main/
├── run.sh                              # One-command setup + run + plot
├── README.md
├── src/spray-routing/                  # Custom NS-3 routing module
│   ├── model/spray-routing.{h,cc}      # SprayRouting : Ipv4RoutingProtocol
│   ├── model/elephant-tag.{h,cc}       # 4-byte ElephantTag
│   ├── helper/spray-routing-helper.{h,cc}
│   ├── CMakeLists.txt
│   └── wscript
├── scratch/
│   ├── fat-tree-simulation.cc          # Fat-Tree topology + traffic
│   ├── spine-leaf-simulation.cc        # Spine-Leaf topology + traffic
│   └── failure-demo.cc                 # Spine-failure resilience demo
├── scripts/
│   ├── setup.sh                        # Download + patch + build NS-3.40
│   ├── run-simulations.sh              # Run all 4 scenarios
│   ├── run-single.sh                   # Run one scenario interactively
│   ├── sweep.sh                        # Multi-seed variance sweep (seeds 1–10)
│   └── analyze.py                      # Parse FlowMonitor XML → 5 plots
└── results/                            # All XML, logs, and PNG output
```

---

## Source Code Walkthrough

### `src/spray-routing/model/elephant-tag.h/.cc`
A **4-byte** `Tag` (a single `uint32_t` flow ID) attached to packets of an
elephant flow. The routing layer reads this tag to decide spray vs hash.

### `src/spray-routing/model/spray-routing.h/.cc`
The core contribution — a complete `Ipv4RoutingProtocol` implementation:

```
RouteOutput()  [called for every outgoing packet at the source]
  │
  ├── Build FlowTuple {src, dst, sport, dport, proto}
  │     (ports extracted from the transport header; 0 if unavailable)
  ├── Longest-prefix match over m_routes
  ├── Filter to lowest-metric routes → ECMP candidate set
  │     (interfaces in m_downIfaces are skipped — link-failure aware)
  │
  ├── Elephant?  (TOS != 0  OR  ElephantTag present)
  │     YES → idx = UniformRandomVariable.GetInteger(0, |set| - 1)   ← SPRAY
  │     NO  → idx = HashFlow(5-tuple) % |set|                        ← ECMP
  │
  └── Return Ipv4Route with selected nexthop + interface

RouteInput()   [called when forwarding at intermediate switches]
  └── Same logic, then calls ucb(route, packet, header)
```

Public API:
- `AddRoute(dest, mask, gateway, interface, metric = 1)` — repeated calls with
  the same `(dest, mask)` and metric form the ECMP set for that prefix
- `SetMode(bool sprayElephantOnly)` — default `true`
- `AssignStreams(int64_t)` — fixes the RNG stream for reproducibility

### `src/spray-routing/helper/spray-routing-helper.h/.cc`
Integrates `SprayRouting` with NS-3's `InternetStackHelper`:
```cpp
SprayRoutingHelper sh;
internet.SetRoutingHelper(sh);
internet.Install(allNodes);
```

### `scratch/fat-tree-simulation.cc`
1. **Creates nodes** — core, aggregation, edge, host
2. **Creates links** — `PointToPointHelper` at 1 Gbps / 5 µs (switch-to-switch)
   and 1 Gbps / 10 µs (edge-to-host)
3. **Assigns IPs** — one **/30** per link from the `10.x.y.0` pool
4. **Sets up routes** — manually populates routing tables on every switch so
   that all `(k/2)²` equal-cost paths appear as same-metric entries
5. **Installs flows** — 40 × `BulkSendApplication` (TCP, 50 MB, TOS `0x10`)
   and 40 × `OnOffApplication` (UDP, 10 Mbps, 512 B)
6. **FlowMonitor** — collects per-flow statistics → serialises to XML

### `scratch/spine-leaf-simulation.cc`
Same structure, adapted for the spine-leaf full mesh. On each leaf switch,
**spine uplinks occupy interfaces `1 … numSpine`** and **host links occupy
`numSpine+1 … numSpine+hostsPerLeaf`**.

### `scratch/failure-demo.cc`
Demonstrates resilience: `--failSpine=N` brings down spine switch *N* mid-run so
the routing layer must re-balance across the remaining spines.
`--failSpine=-1` runs the healthy baseline.

### `scripts/analyze.py`
- Parses FlowMonitor XML with `xml.etree.ElementTree`
- Classifies flows as elephant (`rxBytes ≥ 1,000,000`) or mouse
- Computes throughput (Mbps), FCT (s), average delay (ms), loss rate (%)
- Generates 5 matplotlib comparison plots at **150 dpi**

Run standalone:
```bash
python3 scripts/analyze.py --results-dir results
```

---

## Configurable Parameters

### Fat-Tree

```bash
./ns3 run "fat-tree-simulation \
  --k=4            # Pod count, even and >= 2: 4→16 hosts, 6→54, 8→128   [4]
  --routing=spray  # ecmp | spray                                        [spray]
  --simTime=5      # Simulation duration in seconds                      [5]
  --elephants=40   # Number of elephant (BulkSend TCP, 50 MB) flows      [40]
  --mice=40        # Number of mouse (OnOff UDP, 10 Mbps) flows          [40]
  --seed=1         # RNG seed / run number for reproducibility           [1]
  --flowmon=true   # Save FlowMonitor XML                                [true]
  --pcap=false     # Capture PCAP traces (creates .pcap files)           [false]"
```

### Spine-Leaf

```bash
./ns3 run "spine-leaf-simulation \
  --numSpine=4      # Spine switch count (= number of equal-cost paths)  [4]
  --numLeaf=8       # Leaf switch count                                  [8]
  --hostsPerLeaf=4  # Hosts per leaf switch                              [4]
  --routing=spray   # ecmp | spray                                       [spray]
  --simTime=5       # Simulation duration in seconds                     [5]
  --elephants=40    # Number of elephant flows                           [40]
  --mice=40         # Number of mouse flows                              [40]
  --seed=1          # RNG seed                                           [1]"
```

### Failure Demo

```bash
./ns3 run "failure-demo --routing=spray --failSpine=-1"   # healthy baseline
./ns3 run "failure-demo --routing=spray --failSpine=0"    # spine 0 dies
```

### Helper Scripts

```bash
# Run one scenario
bash scripts/run-single.sh ns-allinone-3.40/ns-3.40 fat-tree spray
bash scripts/run-single.sh ns-allinone-3.40/ns-3.40 spine-leaf ecmp --simTime=10
bash scripts/run-single.sh ns-allinone-3.40/ns-3.40 fat-tree spray --k=8 --pcap=true

# Run all 4 scenarios (env vars: K, SIM_TIME, ELEPHANTS, MICE, SEED)
bash scripts/run-simulations.sh ns-allinone-3.40/ns-3.40
K=8 SIM_TIME=10 bash scripts/run-simulations.sh ns-allinone-3.40/ns-3.40

# Multi-seed sweep — runs seeds 1..10 and reports mean + std of elephant throughput
bash scripts/sweep.sh
```

---

## Problem Statement

In data-centre networks, **elephant flows** (large TCP transfers ≥ 1 MB) cause
severe congestion on a single network path when standard **ECMP routing** is
used. ECMP selects one path per flow by hashing the 5-tuple — if two elephant
flows hash to the same path, traffic can become concentrated on that path while other available paths are underutilised.

**Packet Spraying** sends each packet of an elephant flow across the available
equal-cost paths using uniform random path selection. For every packet, one of
the equal-cost paths is chosen independently using NS-3's
`UniformRandomVariable`, helping distribute traffic across the available paths. The key idea is to distribute
*packets* rather than *flows*, reducing the likelihood of concentrating elephant
traffic on a single path. Mouse flows continue to use the deterministic 5-tuple
hash, maintaining flow-level path consistency.
