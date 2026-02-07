# CSE638 / Graduate Systems — PA02 

**Roll No:** MT25084  
**Student Name:** Satatya De
**Submission folder name:** `GRS_PA02` 

This repository contains my PA02 implementation + experiment automation for comparing:

- **A1 (Two-copy baseline):** `send()` / `recv()` TCP client-server  
- **A2 (One-copy reduction):** `sendmsg()` (scatter/gather) using a stable pre-allocated payload buffer  
- **A3 (Zero-copy send path):** `sendmsg()` with `MSG_ZEROCOPY` (with safe fallback if unsupported)

The experiments are run in **separate Linux network namespaces** (no VM) using a `veth` pair, and performance counters are collected using `perf stat`.

---

## 1) What’s inside

### Part A — Implementations
- `MT25084_Part_A1_Server.c`, `MT25084_Part_A1_Client.c`
- `MT25084_Part_A2_Server.c`, `MT25084_Part_A2_Client.c`
- `MT25084_Part_A3_Server.c`, `MT25084_Part_A3_Client.c`

### Part C — Automation / Measurement
- `MT25084_Part_C_Run_Experiments.sh`  
  Creates namespaces, compiles A1/A2/A3, runs the full grid, parses `perf stat` outputs, and writes:
  - `MT25084_Part_C_results.csv`

### Part D — Derived metrics + plots
- `MT25084_Part_D_Plot.py` — reads Part C CSV, produces:
  - `MT25084_Part_D_derived.csv`
  - `MT25084_Part_D_plots/` (report has the plots)
- `MT25084_Part_D_Run.sh` — wrapper to run the plot script

### Build
- `Makefile`

---

## 2) Requirements / dependencies

Install (Ubuntu/Debian):
```bash
sudo apt-get update
sudo apt-get install -y build-essential iproute2 linux-tools-common linux-tools-generic python3 python3-pip
pip3 install --user pandas matplotlib
```

You also need:
- Root privileges for `ip netns` and `perf` (run with `sudo`)
- Kernel that supports the perf events used (script uses: cycles, context-switches, cache-misses, L1-dcache-load-misses, LLC-load-misses)

---

## 3) Build (compile everything)

From the project directory:
```bash
make
```

Clean compiled binaries:
```bash
make clean
```

---

## 4) Network namespace topology (how client/server are isolated)

The setup uses two namespaces:

- **Server namespace:** `ns_srv` with `10.200.1.1/24`
- **Client namespace:** `ns_cli` with `10.200.1.2/24`

Connected via a `veth` pair `veth_srv <-> veth_cli`.

### Manual setup (optional)
```bash
sudo ip netns add ns_srv
sudo ip netns add ns_cli

sudo ip link add veth_srv type veth peer name veth_cli
sudo ip link set veth_srv netns ns_srv
sudo ip link set veth_cli netns ns_cli

sudo ip -n ns_srv addr add 10.200.1.1/24 dev veth_srv
sudo ip -n ns_cli addr add 10.200.1.2/24 dev veth_cli

sudo ip -n ns_srv link set lo up
sudo ip -n ns_cli link set lo up
sudo ip -n ns_srv link set veth_srv up
sudo ip -n ns_cli link set veth_cli up

sudo ip netns exec ns_cli ping -c 1 10.200.1.1
```

### Cleanup namespaces (manual)
```bash
sudo ip netns del ns_srv 2>/dev/null || true
sudo ip netns del ns_cli 2>/dev/null || true
```

---

## 5) Run Part A manually (quick sanity runs)

**Important:** Start the server first, then the client(s).

### A1 (baseline) — example
**Terminal 1 (server):**
```bash
sudo ip netns exec ns_srv ./MT25084_Part_A1_Server 9090 1024 10 4
```

**Terminal 2 (4 clients):**
```bash
for i in 1 2 3 4; do
  sudo ip netns exec ns_cli ./MT25084_Part_A1_Client 10.200.1.1 9090 1024 10 &
done
wait
```

### A2 — example (same run parameters)
```bash
sudo ip netns exec ns_srv ./MT25084_Part_A2_Server 9090 1024 10 4
# then in another terminal:
for i in 1 2 3 4; do
  sudo ip netns exec ns_cli ./MT25084_Part_A2_Client 10.200.1.1 9090 1024 10 &
done
wait
```

### A3 — example (MSG_ZEROCOPY attempt + fallback)
```bash
sudo ip netns exec ns_srv ./MT25084_Part_A3_Server 9090 1024 10 4
# then:
for i in 1 2 3 4; do
  sudo ip netns exec ns_cli ./MT25084_Part_A3_Client 10.200.1.1 9090 1024 10 &
done
wait
```

---

## 6) Collect `perf stat` for one run (manual)

Run perf around the server process. Example (A1, 4 clients, 10 seconds):
```bash
sudo ip netns exec ns_srv perf stat   -e cycles,context-switches,cache-misses,L1-dcache-load-misses,LLC-load-misses   -o perf_A1_m1024_t4.txt   ./MT25084_Part_A1_Server 9090 1024 10 4
```

Then run clients in another terminal as shown above.

---

## 7) Part C — Run the full experiment grid (main workflow)

Run:
```bash
sudo ./MT25084_Part_C_Run_Experiments.sh
```

This script:
1. Sets up namespaces (`ns_srv`, `ns_cli`)
2. Compiles A1/A2/A3 (gcc `-O2 -pthread`)
3. Runs experiments over:

- **Message sizes**: `64, 256, 1024, 4096, 16384` bytes  
- **Thread counts**: `1, 2, 4, 8` (thread count = number of client processes)  
- **Implementations**: `A1, A2, A3`  
- **Duration**: `10s`

4. Captures:
- Total bytes/messages/GBps (from client logs)
- perf counters (from `perf stat`)

Outputs:
- `MT25084_Part_C_results.csv`

---

## 8) Part D — Generate derived CSV + plots

Run:
```bash
./MT25084_Part_D_Run.sh MT25084_Part_C_results.csv
```

Outputs:
- `MT25084_Part_D_derived.csv`
- `MT25084_Part_D_plots/` (png + pdf figures)

---

## 9) Helpful CLI utilities (debugging / cleanup)

### Find a listening server / process and kill it
```bash
sudo ss -ltnp | grep 9090 || true
sudo lsof -iTCP:9090 -sTCP:LISTEN -nP || true
```

Kill by PID:
```bash
sudo kill -TERM <PID>
# if still stuck:
sudo kill -KILL <PID>
```

Kill all servers/clients by name:
```bash
sudo pkill -f MT25084_Part_A1_Server || true
sudo pkill -f MT25084_Part_A2_Server || true
sudo pkill -f MT25084_Part_A3_Server || true
sudo pkill -f MT25084_Part_A1_Client || true
sudo pkill -f MT25084_Part_A2_Client || true
sudo pkill -f MT25084_Part_A3_Client || true
```

### Remove experiment log artifacts
```bash
rm -f MT25084_Part_C_raw_* perf_*.txt 2>/dev/null || true
```

---

## 10) Submission checklist (do this before zipping)

The handout requires a clean submission zip. Before creating the final zip:

1. **Remove binaries** (do not submit compiled executables):
   ```bash
   make clean
   ```

2. **Do not include plot images as separate PNG/JPG files**  
   Keep plots inside the report PDF/DOCX, but don’t ship the raw plot image directory if forbidden by your handout.

3. Ensure there are **no extra subfolders** inside the final zip (flatten if required).

4. Ensure files start with roll number comment (this README does: `<!-- MT25084 -->`).

---

## 11) System information (fill in from your machine)

```bash
uname -a
lscpu | head -n 30
free -h
perf --version
```

- CPU model:  
- RAM:  
- Kernel:  
- perf version:  

---

## 12) Notes on A1/A2/A3 “copies” (summary)

- **A1 (send/recv):** baseline socket path; user→kernel copy on send, kernel→user copy on recv.
- **A2 (sendmsg):** uses `sendmsg` with stable buffer / iovec to remove an *avoidable* user-space staging copy (still has kernel user→kernel copy).
- **A3 (MSG_ZEROCOPY):** attempts to remove the user→kernel payload copy on send by pinning user pages and letting NIC DMA read from them; completion is asynchronous (error queue). Falls back safely if unsupported.

---

If anything fails, the first thing to check is namespace connectivity:

```bash
sudo ip netns exec ns_cli ping -c 1 10.200.1.1
sudo ip netns exec ns_srv ip addr
sudo ip netns exec ns_cli ip addr
```
