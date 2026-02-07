#!/usr/bin/env python3
import os
import sys
import pandas as pd
import matplotlib.pyplot as plt

DEFAULT_IN = "MT25084_Part_C_results.csv"
OUT_DIR = "MT25084_Part_D_plots"
DERIVED_OUT = "MT25084_Part_D_derived.csv"

REQUIRED_COLS = [
    "impl", "msg_size", "threads", "duration_s",
    "total_bytes", "total_msgs", "total_gbps",
    "weighted_avg_oneway_us", "cycles", "context_switches"
]

OPTIONAL_COLS = [
    "cache_references",
    "cache_misses",
    "L1_dcache_load_misses",
    "LLC_load_misses",
]

def ensure_numeric(df, cols):
    for c in cols:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce")
    return df

def set_log2_x(ax):
    try:
        ax.set_xscale("log", base=2)
    except TypeError:
        ax.set_xscale("log", basex=2)

def save_plot(fig, out_path_png):
    fig.tight_layout()
    fig.savefig(out_path_png, dpi=200)
    out_path_pdf = os.path.splitext(out_path_png)[0] + ".pdf"
    fig.savefig(out_path_pdf)
    plt.close(fig)

def plot_metric(df, metric_col, ylabel, title_prefix, out_basename):
    if metric_col not in df.columns:
        print(f"[skip] missing column: {metric_col}")
        return

    threads_list = sorted(df["threads"].dropna().unique())
    impls = sorted(df["impl"].dropna().unique())
    msg_sizes = sorted(df["msg_size"].dropna().unique())

    for t in threads_list:
        dft = df[df["threads"] == t].copy()

        fig = plt.figure()
        ax = fig.add_subplot(111)

        plotted_any = False
        for impl in impls:
            dfi = dft[dft["impl"] == impl].sort_values("msg_size")
            if len(dfi) == 0:
                continue
            ax.plot(dfi["msg_size"], dfi[metric_col], marker="o", label=str(impl))
            plotted_any = True

        set_log2_x(ax)
        ax.set_xticks(msg_sizes)
        ax.get_xaxis().set_major_formatter(plt.FuncFormatter(lambda v, _: f"{int(v)}"))
        ax.set_xlabel("Message size (bytes) [log2 scale]")
        ax.set_ylabel(ylabel)
        ax.set_title(f"{title_prefix} (threads={int(t)})")
        ax.grid(True, which="both", linestyle="--", linewidth=0.5, alpha=0.6)

        if plotted_any:
            ax.legend()

        out_png = os.path.join(OUT_DIR, f"{out_basename}_t{int(t)}.png")
        save_plot(fig, out_png)

def main():
    in_csv = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_IN
    if not os.path.exists(in_csv):
        print(f"ERROR: input CSV not found: {in_csv}")
        sys.exit(1)

    os.makedirs(OUT_DIR, exist_ok=True)
    df = pd.read_csv(in_csv)

    missing = [c for c in REQUIRED_COLS if c not in df.columns]
    if missing:
        print("ERROR: CSV missing required columns:", missing)
        print("Found columns:", list(df.columns))
        sys.exit(1)

    # ---- FIX: handle empty/NaN impl column ----
    df["impl"] = df["impl"].astype(str).str.strip()
    df.loc[df["impl"].isin(["nan", "None"]), "impl"] = ""

    if df["impl"].eq("").all():
        # C runs A1 -> A2 -> A3 for each (msg_size, threads, duration_s)
        grp = ["msg_size", "threads", "duration_s"]
        df["__k"] = df.groupby(grp).cumcount()
        mapping = {0: "A1", 1: "A2", 2: "A3"}
        df["impl"] = df["__k"].map(mapping).fillna("A?")
        df.drop(columns=["__k"], inplace=True)

    # IMPORTANT: DO NOT coerce impl to numeric
    numeric_cols = [c for c in (REQUIRED_COLS + OPTIONAL_COLS) if c != "impl"]
    df = ensure_numeric(df, numeric_cols)

    # Derived metrics
    df["cycles_per_byte"] = df["cycles"] / df["total_bytes"].replace(0, float("nan"))
    df["ctx_switches_per_sec"] = df["context_switches"] / df["duration_s"].replace(0, float("nan"))

    if "cache_misses" in df.columns:
        df["cache_misses_per_gb"] = df["cache_misses"] / (df["total_bytes"].replace(0, float("nan")) / (1024**3))
        df["cache_misses_per_mmsg"] = df["cache_misses"] / (df["total_msgs"].replace(0, float("nan")) / 1_000_000)

    if "L1_dcache_load_misses" in df.columns:
        df["L1_misses_per_gb"] = df["L1_dcache_load_misses"] / (df["total_bytes"].replace(0, float("nan")) / (1024**3))
        df["L1_misses_per_mmsg"] = df["L1_dcache_load_misses"] / (df["total_msgs"].replace(0, float("nan")) / 1_000_000)

    if "LLC_load_misses" in df.columns:
        df["LLC_misses_per_gb"] = df["LLC_load_misses"] / (df["total_bytes"].replace(0, float("nan")) / (1024**3))
        df["LLC_misses_per_mmsg"] = df["LLC_load_misses"] / (df["total_msgs"].replace(0, float("nan")) / 1_000_000)

    if "cache_references" in df.columns and "cache_misses" in df.columns:
        df["cache_miss_rate"] = df["cache_misses"] / df["cache_references"].replace(0, float("nan"))

    out_cols_candidate = [
        "impl","msg_size","threads","duration_s","total_bytes","total_msgs","total_gbps","weighted_avg_oneway_us",
        "cycles","context_switches",
        "cycles_per_byte","ctx_switches_per_sec",
        "cache_references","cache_misses","cache_miss_rate",
        "L1_dcache_load_misses","LLC_load_misses",
        "cache_misses_per_gb","cache_misses_per_mmsg",
        "L1_misses_per_gb","L1_misses_per_mmsg",
        "LLC_misses_per_gb","LLC_misses_per_mmsg"
    ]
    df_out_cols = [c for c in out_cols_candidate if c in df.columns]
    df[df_out_cols].to_csv(DERIVED_OUT, index=False)
    print(f"[ok] wrote: {DERIVED_OUT}")

    # Plots
    plot_metric(df, "total_gbps", "Throughput (Gbps)", "Throughput vs Message Size", "throughput_gbps")
    plot_metric(df, "weighted_avg_oneway_us", "Weighted avg one-way latency (us)", "Latency vs Message Size", "latency_us")
    plot_metric(df, "cycles_per_byte", "Cycles / byte", "CPU Cost vs Message Size", "cycles_per_byte")
    plot_metric(df, "ctx_switches_per_sec", "Context switches / sec", "Context Switches vs Message Size", "ctx_switches_per_sec")

    if "cache_misses_per_gb" in df.columns:
        plot_metric(df, "cache_misses_per_gb", "Cache misses per GiB transferred", "Cache Misses vs Message Size", "cache_misses_per_gb")
    if "L1_misses_per_gb" in df.columns:
        plot_metric(df, "L1_misses_per_gb", "L1D load misses per GiB transferred", "L1D Misses vs Message Size", "l1_misses_per_gb")
    if "LLC_misses_per_gb" in df.columns:
        plot_metric(df, "LLC_misses_per_gb", "LLC load misses per GiB transferred", "LLC Misses vs Message Size", "llc_misses_per_gb")

    print(f"[ok] plots in: {OUT_DIR}/ (png + pdf)")

if __name__ == "__main__":
    main()
