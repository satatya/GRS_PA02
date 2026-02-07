#!/usr/bin/env bash
set -euo pipefail

# ----------------------------
# MT25084 Part C Experiment Runner
# ----------------------------
# Runs A1/A2/A3 across message sizes and thread counts
# Collects:
#  - perf stat counters into MT25084_Part_C_raw_*_perf.csv
#  - client logs into MT25084_Part_C_raw_*_clientX.log
# Produces:
#  - MT25084_Part_C_results.csv
# ----------------------------

if [[ "${EUID}" -ne 0 ]]; then
  echo "ERROR: Run with sudo"
  exit 1
fi

WORKDIR="$(cd "$(dirname "$0")" && pwd)"
OWNER="${SUDO_USER:-root}"

NS_SRV="ns_srv"
NS_CLI="ns_cli"
PORT=9090
SERVER_IP="10.200.1.1"
CLIENT_IP="10.200.1.2"
DUR=10

# >= 4 msg sizes (you already had 5; keeping as-is to not disturb flow)
MSG_SIZES=(64 256 1024 4096 16384)

# ✅ FIX: now >= 4 thread counts (only requested change)
THREAD_COUNTS=(1 2 4 8)

IMPLS=(A1 A2 A3)

# perf events (as per your perf list)
EVENTS="cycles,context-switches,cache-misses,L1-dcache-load-misses,LLC-load-misses"

RESULTS_CSV="MT25084_Part_C_results.csv"
HEADER="impl,msg_size,threads,duration_s,total_bytes,total_msgs,total_gbps,weighted_avg_oneway_us,cycles,context_switches,cache_misses,L1_dcache_load_misses,LLC_load_misses"

log() { echo "[C] $*"; }

setup_namespaces() {
  log "Setting up namespaces..."

  ip netns del "$NS_SRV" 2>/dev/null || true
  ip netns del "$NS_CLI" 2>/dev/null || true
  ip link del veth_srv 2>/dev/null || true

  ip netns add "$NS_SRV"
  ip netns add "$NS_CLI"

  ip link add veth_srv type veth peer name veth_cli
  ip link set veth_srv netns "$NS_SRV"
  ip link set veth_cli netns "$NS_CLI"

  ip -n "$NS_SRV" addr add "${SERVER_IP}/24" dev veth_srv
  ip -n "$NS_CLI" addr add "${CLIENT_IP}/24" dev veth_cli

  ip -n "$NS_SRV" link set lo up
  ip -n "$NS_CLI" link set lo up
  ip -n "$NS_SRV" link set veth_srv up
  ip -n "$NS_CLI" link set veth_cli up

  ip netns exec "$NS_CLI" ping -c 1 "$SERVER_IP" >/dev/null
}

compile_all() {
  log "Compiling all implementations..."
  cd "$WORKDIR"

  rm -f MT25084_Part_A1_Server MT25084_Part_A1_Client \
        MT25084_Part_A2_Server MT25084_Part_A2_Client \
        MT25084_Part_A3_Server MT25084_Part_A3_Client \
        *.o perf_*.txt MT25084_Part_C_raw_* MT25084_Part_C_results.csv 2>/dev/null || true

  gcc -O2 -Wall -Wextra -pthread -o MT25084_Part_A1_Server MT25084_Part_A1_Server.c -pthread
  gcc -O2 -Wall -Wextra -pthread -o MT25084_Part_A1_Client MT25084_Part_A1_Client.c -pthread
  gcc -O2 -Wall -Wextra -pthread -o MT25084_Part_A2_Server MT25084_Part_A2_Server.c -pthread
  gcc -O2 -Wall -Wextra -pthread -o MT25084_Part_A2_Client MT25084_Part_A2_Client.c -pthread
  gcc -O2 -Wall -Wextra -pthread -o MT25084_Part_A3_Server MT25084_Part_A3_Server.c -pthread
  gcc -O2 -Wall -Wextra -pthread -o MT25084_Part_A3_Client MT25084_Part_A3_Client.c -pthread
}

# ✅ FIXED: no gawk-only awk match() capture array
kill_port_if_any() {
  local pid=""
  pid="$(ip netns exec "$NS_SRV" ss -ltnp 2>/dev/null \
        | grep -m1 ":${PORT}" \
        | sed -n 's/.*pid=\([0-9]\+\).*/\1/p' || true)"
  if [[ -n "${pid}" ]]; then
    ip netns exec "$NS_SRV" kill -9 "$pid" 2>/dev/null || true
  fi
}

wait_for_listen() {
  local i
  for i in {1..200}; do
    if ip netns exec "$NS_SRV" ss -ltn 2>/dev/null | grep -q ":${PORT}"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

perf_get_val() {
  # args: perf_file event_name
  local file="$1"
  local ev="$2"
  awk -F',' -v e="$ev" '
    $0 ~ /^#/ { next }
    $3 == e { gsub(/[[:space:]]/, "", $1); print $1; exit }
  ' "$file"
}

parse_client_summary() {
  # args: client_log
  local f="$1"
  local line
  line="$(grep -m1 '^SUMMARY' "$f" 2>/dev/null || true)"
  if [[ -z "$line" ]]; then
    echo "0 0 0 0 0"
    return
  fi
  local bytes secs gbps msgs avg
  bytes="$(echo "$line" | sed -n 's/.*bytes=\([0-9]\+\).*/\1/p')"
  secs="$(echo "$line"  | sed -n 's/.*seconds=\([0-9.]\+\).*/\1/p')"
  gbps="$(echo "$line"  | sed -n 's/.*gbps=\([0-9.]\+\).*/\1/p')"
  msgs="$(echo "$line"  | sed -n 's/.*msgs=\([0-9]\+\).*/\1/p')"
  avg="$(echo "$line"   | sed -n 's/.*avg_oneway_us=\([0-9.]\+\).*/\1/p')"
  echo "${bytes:-0} ${secs:-0} ${gbps:-0} ${msgs:-0} ${avg:-0}"
}

run_one() {
  local impl="$1"
  local msg="$2"
  local t="$3"
  local dur="$4"

  local tag="${impl}_m${msg}_t${t}_d${dur}"
  local perf_raw="MT25084_Part_C_raw_${tag}_perf.csv"
  local server_log="MT25084_Part_C_raw_${tag}_server.log"

  rm -f "$perf_raw" "$server_log" "MT25084_Part_C_raw_${tag}_client"*.log 2>/dev/null || true

  kill_port_if_any

  local server_bin="./MT25084_Part_${impl}_Server"
  local client_bin="./MT25084_Part_${impl}_Client"

  log "==> Running ${impl} msg=${msg} threads=${t} dur=${dur}s"

  # IMPORTANT: server args = port msg_size duration num_clients
  ip netns exec "$NS_SRV" bash -lc "
    cd '$WORKDIR' &&
    timeout -k 1s $((dur+3))s perf stat -x, --no-big-num \
      -e '$EVENTS' -o '$perf_raw' \
      '$server_bin' '$PORT' '$msg' '$dur' '$t'
  " >"$server_log" 2>&1 &
  local srv_pid=$!

  if ! wait_for_listen; then
    log "ERROR: server did not start listening for $tag"
    kill -9 "$srv_pid" 2>/dev/null || true
    wait "$srv_pid" 2>/dev/null || true
    return 1
  fi

  # Run T clients in parallel
  local pids=()
  local i
  for i in $(seq 1 "$t"); do
    ip netns exec "$NS_CLI" bash -lc "
      cd '$WORKDIR' &&
      '$client_bin' '$SERVER_IP' '$PORT' '$msg' '$dur'
    " >"MT25084_Part_C_raw_${tag}_client${i}.log" 2>&1 &
    pids+=("$!")
  done

  for pid in "${pids[@]}"; do
    wait "$pid" || true
  done

  wait "$srv_pid" || true

  chown "$OWNER":"$OWNER" "$perf_raw" "$server_log" MT25084_Part_C_raw_"${tag}"_client*.log 2>/dev/null || true

  # Aggregate client summaries
  local total_bytes=0
  local total_msgs=0
  local total_gbps="0"
  local weighted_sum="0"

  for i in $(seq 1 "$t"); do
    local f="MT25084_Part_C_raw_${tag}_client${i}.log"
    read -r b s g m a < <(parse_client_summary "$f")

    total_bytes=$((total_bytes + b))
    total_msgs=$((total_msgs + m))
    total_gbps="$(awk -v x="$total_gbps" -v y="$g" 'BEGIN{printf "%.6f", x+y}')"
    weighted_sum="$(awk -v ws="$weighted_sum" -v avg="$a" -v msgs="$m" 'BEGIN{printf "%.6f", ws + (avg*msgs)}')"
  done

  local wavg="0"
  if [[ "$total_msgs" -gt 0 ]]; then
    wavg="$(awk -v ws="$weighted_sum" -v tm="$total_msgs" 'BEGIN{printf "%.6f", ws/tm}')"
  fi

  # Parse perf counters
  local cycles cs cachem l1 llc
  cycles="$(perf_get_val "$perf_raw" "cycles" || true)"; cycles="${cycles:-0}"
  cs="$(perf_get_val "$perf_raw" "context-switches" || true)"; cs="${cs:-0}"
  cachem="$(perf_get_val "$perf_raw" "cache-misses" || true)"; cachem="${cachem:-0}"
  l1="$(perf_get_val "$perf_raw" "L1-dcache-load-misses" || true)"; l1="${l1:-0}"
  llc="$(perf_get_val "$perf_raw" "LLC-load-misses" || true)"; llc="${llc:-0}"

  echo "${impl},${msg},${t},${dur},${total_bytes},${total_msgs},${total_gbps},${wavg},${cycles},${cs},${cachem},${l1},${llc}" >> "$RESULTS_CSV"
}

main() {
  setup_namespaces
  compile_all

  cd "$WORKDIR"
  echo "$HEADER" > "$RESULTS_CSV"
  chown "$OWNER":"$OWNER" "$RESULTS_CSV" 2>/dev/null || true

  log "Running experiment grid..."
  local msg t impl
  for msg in "${MSG_SIZES[@]}"; do
    for t in "${THREAD_COUNTS[@]}"; do
      for impl in "${IMPLS[@]}"; do
        run_one "$impl" "$msg" "$t" "$DUR"
      done
    done
  done

  log "Done. Results: $RESULTS_CSV"
}

main "$@"
rm -f MT25084_Part_C_raw_* MT25084_Part_C_raw_*_perf.csv MT25084_Part_C_raw_*_server.log MT25084_Part_C_raw_*_client*.log 2>/dev/null || true