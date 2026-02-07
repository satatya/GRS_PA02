#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

IN="${1:-MT25084_Part_C_results.csv}"

if [[ ! -f "$IN" ]]; then
  echo "ERROR: cannot include input CSV: $IN"
  exit 1
fi

python3 ./MT25084_Part_D_Plot.py "$IN"

echo
echo "Done."
echo "1) Derived CSV: MT25084_Part_D_derived.csv"
echo "2) Plots:       MT25084_Part_D_plots/"
