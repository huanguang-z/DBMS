# DBMS — Storage Subsystem (C++17, Ubuntu 22.04)

This repository contains a teaching-oriented DBMS storage subsystem implemented in C++17. The current codebase focuses on pages, buffer management, free space tracking, segments, tuples/schemas, and a table heap, together with a standalone loader used to evaluate storage behavior under controlled conditions. Query processing, indexing, recovery, and concurrency control are planned as separate modules.

---

## 1. Repository Layout

```
DBMS/
├─ CMakeLists.txt
├─ Integration/
│  └─ main_storage_load.cpp     # Loader used in experiments
├─ Storage/
│  ├─ CMakeLists.txt
│  ├─ include/dbms/storage/...  # Stable public headers
│  ├─ internal/...              # Internal headers
│  └─ src/...                   # Implementations
└─ supplier.tbl                 # TPCH Supplier (10k rows), for demonstration
```

Public headers live under `Storage/include/dbms/storage/` to provide stable, minimal interfaces to other modules and external tools. Internal headers (non-ABI) are intentionally not exposed outside the storage target.

---

## 2. Build Instructions

Requirements (Ubuntu 22.04):

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build
```

Configure and build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

### Optional: enable LRU‑K

```bash
rm -rf build
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DDBMS_STORAGE_ENABLE_LRUK=ON
cmake --build build -j
```

---

## 3. Loader Usage

The integration binary `main_storage_load` reads the TPCH `supplier.tbl`, materializes tuples according to the configured schema, inserts them into a table heap, and prints storage telemetry (buffer hits/misses, evictions, flushes, and FSM distribution).

Typical run:

```bash
cd build
mkdir -p ../run/lab1
./main_storage_load ../supplier.tbl \
  --base_dir=../run/lab1 \
  --frames=256 \
  --page=8192 \
  --replacer=clock \
  --log_every=1000 | tee ../run/lab1/run.log
```

LRU‑K with K=2:

```bash
./main_storage_load ../supplier.tbl \
  --base_dir=../run/k2 \
  --frames=256 \
  --page=8192 \
  --replacer=lruk \
  --k=2 \
  --log_every=500 | tee ../run/k2/run.log
```

**Notes.**

- The loader accepts POSIX-style paths. Use an absolute path for `--base_dir` in non-standard environments.
- For Windows-origin data, consider normalizing line endings: `sudo apt install -y dos2unix && dos2unix supplier.tbl`.

---

## 4. Reproducible Experiments

The following scripts allow controlled experiments with page size, buffer frames, and replacement policy. They also produce a compact CSV summary for analysis.

Create scripts:

```bash
mkdir -p scripts
```

**Parameter sweep.**

```bash
cat > scripts/run_matrix.sh <<'SH'
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."/build
DATA=../supplier.tbl
STAMP=$(date +%m%d_%H%M)
ROOT=../run/exp_${STAMP}
mkdir -p "$ROOT"
echo "[INFO] Logs -> $ROOT"

PAGES=(8192 4096)
FRAMES=(64 128 256)
REPLACERS=(clock lruk)   # requires -DDBMS_STORAGE_ENABLE_LRUK=ON

for p in "${PAGES[@]}"; do
  for f in "${FRAMES[@]}"; do
    for r in "${REPLACERS[@]}"; do
      OUT="$ROOT/p${p}_f${f}_${r}"
      mkdir -p "$OUT"
      /usr/bin/time -f 'TIME real=%E user=%U sys=%S maxRSS=%MKB' \
      ./main_storage_load "$DATA" \
        --base_dir="$OUT" \
        --frames="$f" \
        --page="$p" \
        --replacer="$r" \
        --k=2 \
        --log_every=500 2>&1 | tee "$OUT/run.log"
    done
  done
done
echo "[DONE] $ROOT"
SH
chmod +x scripts/run_matrix.sh
```

**Varying K for LRU‑K (K ∈ {2,3,4}).**

```bash
cat > scripts/run_k_sweep.sh <<'SH'
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."/build
DATA=../supplier.tbl
STAMP=$(date +%m%d_%H%M)
ROOT=../run/exp_k_${STAMP}
mkdir -p "$ROOT"
for K in 2 3 4; do
  OUT="$ROOT/p8192_f256_lruk_k${K}"
  mkdir -p "$OUT"
  /usr/bin/time -f 'TIME real=%E user=%U sys=%S maxRSS=%MKB' \
  ./main_storage_load "$DATA" \
    --base_dir="$OUT" \
    --frames=256 \
    --page=8192 \
    --replacer=lruk \
    --k="$K" \
    --log_every=500 2>&1 | tee "$OUT/run.log"
done
echo "[DONE] $ROOT"
SH
chmod +x scripts/run_k_sweep.sh
```

**Summarize results to CSV.**

```bash
cat > scripts/summarize_results.sh <<'SH'
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."/build
ROOTS=("$@")
if [ "${#ROOTS[@]}" -eq 0 ]; then
  ROOTS=(../run/exp_* ../run/exp_k_*)
fi
echo "exp,case,page,frames,replacer,k,rows,bad,pages,hits,misses,evictions,flushes,real,user,sys,maxRSS"
for root in "${ROOTS[@]}"; do
  [ -d "$root" ] || continue
  for d in "$root"/*; do
    [ -d "$d" ] || continue
    base=$(basename "$d")     # p8192_f256_lruk or p8192_f256_lruk_k3
    log="$d/run.log"
    [ -f "$log" ] || continue
    page=""; frames=""; replacer=""; kval=""
    if [[ "$base" =~ ^p([0-9]+)_f([0-9]+)_([[:alnum:]]+)(_k([0-9]+))?$ ]]; then
      page="${BASH_REMATCH[1]}"
      frames="${BASH_REMATCH[2]}"
      replacer="${BASH_REMATCH[3]}"
      kval="${BASH_REMATCH[5]:-}"
    fi
    line=$(grep -F "[LOAD] done:" "$log" | tail -n1 || true)
    rows=""; bad=""; pagesc=""; hits=""; misses=""; evic=""; flush=""
    if [[ -n "$line" ]]; then
      rows=$(sed -E 's/.*rows=([0-9]+).*/\1/' <<<"$line")
      bad=$(sed -E 's/.*bad=([0-9]+).*/\1/' <<<"$line")
      pagesc=$(sed -E 's/.*pages=([0-9]+).*/\1/' <<<"$line")
      hits=$(sed -E 's/.*hits=([0-9]+).*/\1/' <<<"$line")
      misses=$(sed -E 's/.*misses=([0-9]+).*/\1/' <<<"$line")
      evic=$(sed -E 's/.*evictions=([0-9]+).*/\1/' <<<"$line")
      flush=$(sed -E 's/.*flushes=([0-9]+).*/\1/' <<<"$line")
    fi
    tline=$(grep -F "TIME real=" "$log" | tail -n1 || true)
    real=""; user=""; sys=""; rss=""
    if [[ -n "$tline" ]]; then
      real=$(sed -E 's/.*real=([^ ]+).*/\1/' <<<"$tline")
      user=$(sed -E 's/.*user=([^ ]+).*/\1/' <<<"$tline")
      sys=$(sed -E 's/.*sys=([^ ]+).*/\1/' <<<"$tline")
      rss=$(sed -E 's/.*maxRSS=([^ ]+).*/\1/' <<<"$tline")
    fi
    echo "$base,$base,$page,$frames,$replacer,$kval,$rows,$bad,$pagesc,$hits,$misses,$evic,$flush,$real,$user,$sys,$rss"
  done
done
SH
chmod +x scripts/summarize_results.sh
```

Run:

```bash
./scripts/run_matrix.sh
./scripts/run_k_sweep.sh
./scripts/summarize_results.sh > run/summary_all.csv
```

The CSV can be plotted directly or imported into a notebook. When comparing configurations, keep buffer sizing, page size, and replacement policy as controlled variables, and vary one factor at a time.

---

## 5. Reproducibility Notes

- **Repeatability.** The loader uses deterministic schema and insert order. Differences mostly stem from OS cache state and CPU scheduling; to reduce variance consider dropping page cache between runs (`sudo sh -c 'sync; echo 3 > /proc/sys/vm/drop_caches'`).
- **I/O path.** The disk manager uses standard POSIX I/O; no direct I/O or fsync batching is enabled unless specified in the underlying implementation.
- **Statistics.** Buffer stats report hits/misses/evictions; depending on the implementation, a final `FlushAll()` may or may not increment the `flushes` counter.


