#!/usr/bin/env python3
"""
analyze.py — Parse NS-3 FlowMonitor XML files and produce comparison plots.

Usage:
    python3 scripts/analyze.py [--results-dir results]

Expects these files in results/:
    fat-tree-ecmp-flowmon.xml
    fat-tree-spray-flowmon.xml
    spine-leaf-ecmp-flowmon.xml
    spine-leaf-spray-flowmon.xml
"""

import os
import sys
import argparse
import xml.etree.ElementTree as ET
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

# Elephant threshold: flows with rxBytes >= this are classified as elephant
ELEPHANT_BYTES = 1_000_000   # 1 MB

PALETTE = {
    "Fat-Tree ECMP":      "#E74C3C",
    "Fat-Tree Spray":     "#2ECC71",
    "Spine-Leaf ECMP":    "#3498DB",
    "Spine-Leaf Spray":   "#F39C12",
}


# ─────────────────────────────────────────────────────────────────────
def parse_flowmon(xml_path: str) -> list[dict]:
    """Return a list of per-flow metric dicts from a FlowMonitor XML file."""
    if not os.path.exists(xml_path):
        print(f"  [warn] {xml_path} not found — skipping")
        return []

    tree = ET.parse(xml_path)
    root = tree.getroot()

    # Build flowId -> (src, dst, proto) from Ipv4FlowClassifier
    flows_meta = {}
    for classifier in root.iter("Ipv4FlowClassifier"):
        for flow in classifier.findall("Flow"):
            fid = int(flow.get("flowId"))
            flows_meta[fid] = {
                "src":   flow.get("sourceAddress"),
                "dst":   flow.get("destinationAddress"),
                "sport": int(flow.get("sourcePort", 0)),
                "dport": int(flow.get("destinationPort", 0)),
                "proto": int(flow.get("protocol", 0)),
            }

    records = []
    for flow_stats in root.iter("FlowStats"):
        for flow in flow_stats.findall("Flow"):
            fid = int(flow.get("flowId"))

            tx_pkts = int(flow.get("txPackets",  0))
            rx_pkts = int(flow.get("rxPackets",  0))
            tx_bytes = int(flow.get("txBytes",   0))
            rx_bytes = int(flow.get("rxBytes",   0))
            lost    = int(flow.get("lostPackets",0))

            # Timestamps in nanoseconds
            t_first_tx = float(flow.get("timeFirstTxPacket", "0").replace("+","").replace("ns",""))
            t_last_rx  = float(flow.get("timeLastRxPacket",  "0").replace("+","").replace("ns",""))
            delay_sum_ns = float(flow.get("delaySum", "0ns").replace("+","").replace("ns",""))

            duration_s = (t_last_rx - t_first_tx) / 1e9
            tput_mbps  = (rx_bytes * 8.0 / 1e6 / duration_s) if duration_s > 0 else 0.0
            avg_delay_ms = (delay_sum_ns / rx_pkts / 1e6) if rx_pkts > 0 else 0.0
            fct_s = duration_s  # approximation: FCT ≈ last_rx - first_tx

            meta = flows_meta.get(fid, {})
            is_elephant = rx_bytes >= ELEPHANT_BYTES

            records.append({
                "flow_id":     fid,
                "src":         meta.get("src", "?"),
                "dst":         meta.get("dst", "?"),
                "proto":       "TCP" if meta.get("proto") == 6 else "UDP",
                "tx_pkts":     tx_pkts,
                "rx_pkts":     rx_pkts,
                "rx_bytes":    rx_bytes,
                "lost_pkts":   lost,
                "tput_mbps":   tput_mbps,
                "avg_delay_ms":avg_delay_ms,
                "fct_s":       fct_s,
                "is_elephant": is_elephant,
            })

    return records


# ─────────────────────────────────────────────────────────────────────
def aggregate(records: list[dict]) -> dict:
    if not records:
        return {}

    el  = [r for r in records if r["is_elephant"]]
    mse = [r for r in records if not r["is_elephant"]]

    def avg(lst, key):
        vals = [r[key] for r in lst if r[key] is not None]
        return float(np.mean(vals)) if vals else 0.0

    total_rx  = sum(r["rx_pkts"]  for r in records)
    total_tx  = sum(r["tx_pkts"]  for r in records)
    lost      = sum(r["lost_pkts"] for r in records)
    loss_rate = lost / total_tx if total_tx > 0 else 0.0

    return {
        "n_flows":          len(records),
        "n_elephant":       len(el),
        "n_mouse":          len(mse),
        "avg_el_tput":      avg(el,  "tput_mbps"),
        "avg_ms_tput":      avg(mse, "tput_mbps"),
        "avg_el_fct":       avg(el,  "fct_s"),
        "avg_ms_fct":       avg(mse, "fct_s"),
        "avg_el_delay_ms":  avg(el,  "avg_delay_ms"),
        "avg_ms_delay_ms":  avg(mse, "avg_delay_ms"),
        "pkt_loss_rate":    loss_rate,
        "all_tputs":        [r["tput_mbps"] for r in records],
        "all_fcts":         [r["fct_s"]     for r in records],
        "el_tputs":         [r["tput_mbps"] for r in el],
        "ms_tputs":         [r["tput_mbps"] for r in mse],
    }


# ─────────────────────────────────────────────────────────────────────
def save(fig, path):
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  [saved] {path}")


# ─────────────────────────────────────────────────────────────────────
def plot_throughput_bar(agg_dict: dict, out_dir: str):
    labels = list(agg_dict.keys())
    el_tp  = [agg_dict[l]["avg_el_tput"] for l in labels]
    ms_tp  = [agg_dict[l]["avg_ms_tput"] for l in labels]

    x = np.arange(len(labels))
    w = 0.35
    fig, ax = plt.subplots(figsize=(10, 5))
    b1 = ax.bar(x - w/2, el_tp, w, label="Elephant", color="#E74C3C", alpha=0.85)
    b2 = ax.bar(x + w/2, ms_tp, w, label="Mouse",    color="#3498DB", alpha=0.85)

    for bar in list(b1) + list(b2):
        h = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2, h + 0.3,
                f"{h:.1f}", ha="center", va="bottom", fontsize=8)

    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=15, ha="right")
    ax.set_ylabel("Avg Throughput (Mbps)")
    ax.set_title("Average Flow Throughput: Elephant vs Mouse")
    ax.legend()
    ax.grid(axis="y", alpha=0.3)
    save(fig, os.path.join(out_dir, "throughput_comparison.png"))


def plot_fct_bar(agg_dict: dict, out_dir: str):
    labels = list(agg_dict.keys())
    el_fct = [agg_dict[l]["avg_el_fct"] for l in labels]
    ms_fct = [agg_dict[l]["avg_ms_fct"] for l in labels]

    x = np.arange(len(labels))
    w = 0.35
    fig, ax = plt.subplots(figsize=(10, 5))
    ax.bar(x - w/2, el_fct, w, label="Elephant", color="#E74C3C", alpha=0.85)
    ax.bar(x + w/2, ms_fct, w, label="Mouse",    color="#3498DB", alpha=0.85)

    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=15, ha="right")
    ax.set_ylabel("Avg Flow Completion Time (s)")
    ax.set_title("Average FCT: Elephant vs Mouse")
    ax.legend()
    ax.grid(axis="y", alpha=0.3)
    save(fig, os.path.join(out_dir, "fct_comparison.png"))


def plot_delay_bar(agg_dict: dict, out_dir: str):
    labels   = list(agg_dict.keys())
    el_delay = [agg_dict[l]["avg_el_delay_ms"] for l in labels]
    ms_delay = [agg_dict[l]["avg_ms_delay_ms"] for l in labels]

    x = np.arange(len(labels))
    w = 0.35
    fig, ax = plt.subplots(figsize=(10, 5))
    ax.bar(x - w/2, el_delay, w, label="Elephant", color="#E74C3C", alpha=0.85)
    ax.bar(x + w/2, ms_delay, w, label="Mouse",    color="#3498DB", alpha=0.85)

    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=15, ha="right")
    ax.set_ylabel("Avg Packet Delay (ms)")
    ax.set_title("Average Packet Delay per Flow Class")
    ax.legend()
    ax.grid(axis="y", alpha=0.3)
    save(fig, os.path.join(out_dir, "delay_comparison.png"))


def plot_tput_cdf(agg_dict: dict, out_dir: str):
    fig, axes = plt.subplots(1, 2, figsize=(13, 5))

    for ax, flow_type, key, title in [
        (axes[0], "Elephant", "el_tputs", "Elephant Flow Throughput CDF"),
        (axes[1], "Mouse",    "ms_tputs", "Mouse Flow Throughput CDF"),
    ]:
        for label, agg in agg_dict.items():
            vals = sorted(agg[key])
            if not vals:
                continue
            cdf  = np.arange(1, len(vals)+1) / len(vals)
            color = PALETTE.get(label)
            ax.plot(vals, cdf, label=label, linewidth=2, color=color)
        ax.set_xlabel("Throughput (Mbps)")
        ax.set_ylabel("CDF")
        ax.set_title(title)
        ax.legend(fontsize=8)
        ax.grid(True, alpha=0.3)
        ax.set_xlim(left=0)

    fig.tight_layout()
    save(fig, os.path.join(out_dir, "throughput_cdf.png"))


def plot_loss_rate(agg_dict: dict, out_dir: str):
    labels = list(agg_dict.keys())
    losses = [agg_dict[l]["pkt_loss_rate"] * 100 for l in labels]
    colors = [PALETTE.get(l, "#95A5A6") for l in labels]

    fig, ax = plt.subplots(figsize=(8, 5))
    ax.bar(labels, losses, color=colors, alpha=0.85)
    ax.set_ylabel("Packet Loss Rate (%)")
    ax.set_title("Packet Loss Rate by Scenario")
    ax.tick_params(axis="x", rotation=15)
    ax.grid(axis="y", alpha=0.3)
    save(fig, os.path.join(out_dir, "packet_loss.png"))


def print_summary_table(agg_dict: dict):
    print(f"\n{'Scenario':<22} {'Flows':>6} {'ElephTput':>10} {'MouseTput':>10}"
          f" {'ElephFCT(s)':>12} {'MouseFCT(s)':>12} {'Loss%':>7}")
    print("-" * 83)
    for label, a in agg_dict.items():
        if not a:
            continue
        print(f"{label:<22} {a['n_flows']:>6} {a['avg_el_tput']:>10.2f}"
              f" {a['avg_ms_tput']:>10.2f} {a['avg_el_fct']:>12.4f}"
              f" {a['avg_ms_fct']:>12.4f} {a['pkt_loss_rate']*100:>7.3f}")
    print()


# ─────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="Analyze FlowMonitor XML output")
    parser.add_argument("--results-dir", default="results", help="Directory with XML files")
    args = parser.parse_args()

    rd = args.results_dir
    os.makedirs(rd, exist_ok=True)

    files = {
        "Fat-Tree ECMP":    os.path.join(rd, "fat-tree-ecmp-flowmon.xml"),
        "Fat-Tree Spray":   os.path.join(rd, "fat-tree-spray-flowmon.xml"),
        "Spine-Leaf ECMP":  os.path.join(rd, "spine-leaf-ecmp-flowmon.xml"),
        "Spine-Leaf Spray": os.path.join(rd, "spine-leaf-spray-flowmon.xml"),
    }

    agg_dict = {}
    for label, path in files.items():
        print(f"Parsing {path} ...")
        records = parse_flowmon(path)
        print(f"  -> {len(records)} flows")
        agg_dict[label] = aggregate(records)

    # Remove labels with no data
    agg_dict = {k: v for k, v in agg_dict.items() if v}

    if not agg_dict:
        print("No data found. Run the simulations first.")
        sys.exit(1)

    print_summary_table(agg_dict)

    print("Generating plots...")
    plot_throughput_bar(agg_dict, rd)
    plot_fct_bar       (agg_dict, rd)
    plot_delay_bar     (agg_dict, rd)
    plot_tput_cdf      (agg_dict, rd)
    plot_loss_rate     (agg_dict, rd)
    print("Done.")


if __name__ == "__main__":
    main()
