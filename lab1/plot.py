#!/usr/bin/env python3
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

LAB1 = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(LAB1, "perf_plot.png")

THREADS = [1, 2, 4, 8, 16]

PHASE_LABELS = {
    "empty": "Empty lock (no writes)",
    "1": "Наивный подход",
    "2": "Шардирование локов",
    "3": "Thread-Local",
    "4": "Двойная буферизация",
}

PHASE_ORDER = ["empty", "1", "2", "3", "4"]

COLORS = {
    "empty": "#888888",
    "1": "#e74c3c",
    "2": "#e67e22",
    "3": "#2ecc71",
    "4": "#3498db",
}

BASELINE_0 = 68.1

INPUT = {
    "empty": [43.8, 8.7, 6.9, 7.0, 6.0],
    "1": [37.3, 8.5, 3.4, 1.9, 1.7],
    "2": [12.5, 4.4, 3.1, 2.0, 2.1],
    "3": [26.6, 41.2, 63.7, 92.2, 100.1],
    "4": [30.4, 54.4, 84.0, 122.1, 129.4],
}


def parse_log(path):
    data = {}
    current = None
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if line.startswith("PHASE "):
                current = line.split(maxsplit=1)[1]
                data.setdefault(current, [])
                continue
            parts = line.split()
            if len(parts) == 2 and current is not None:
                data[current].append(float(parts[1]))
    for key in list(data):
        if key not in PHASE_LABELS or len(data[key]) != len(THREADS):
            data.pop(key, None)
    return data


def main():
    data = dict(INPUT)

    fig, ax = plt.subplots(figsize=(9, 6))

    baseline_values = [BASELINE_0] * len(THREADS)
    ax.plot(
        THREADS, baseline_values,
        linestyle="--", color="#16a085", alpha=0.8,
        label="Baseline",
    )

    for key in PHASE_ORDER:
        if key not in data:
            continue
        ax.plot(
            THREADS, data[key], marker="o",
            color=COLORS[key], label=PHASE_LABELS[key],
        )

    ax.set_xlabel("number of threads")
    ax.set_ylabel("throughput, (op * 10^6)/sec")
    ax.set_xscale("log", base=2)
    ax.set_xticks(THREADS)
    ax.grid(True, which="both", linestyle=":", alpha=0.5)
    ax.legend(loc="upper left", fontsize=8)
    fig.tight_layout()
    fig.savefig(OUT, dpi=150)


if __name__ == "__main__":
    main()