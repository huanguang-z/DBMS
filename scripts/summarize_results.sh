#!/usr/bin/env bash
set -euo pipefail
shopt -s nullglob

# ROOT 列表：传参则用参数；否则默认找 ../run/exp_* 与 ../run/exp_k_*
if [ "$#" -gt 0 ]; then
  ROOTS=("$@")
else
  ROOTS=(../run/exp_* ../run/exp_k_*)
fi

echo "exp_dir,case,page,frames,replacer,k,rows,bad,pages,hits,misses,evictions,flushes,real,user,sys,maxRSS"

for root in "${ROOTS[@]}"; do
  [ -d "$root" ] || continue
  for d in "$root"/*; do
    [ -d "$d" ] || continue
    base="$(basename "$d")"        # 例：p8192_f256_lruk_k3
    log="$d/run.log"
    [ -f "$log" ] || continue

    page="" ; frames="" ; replacer="" ; kval=""
    # 先从目录名解析：p{page}_f{frames}_{replacer}[_k{K}]
    if [[ "$base" =~ ^p([0-9]+)_f([0-9]+)_([[:alnum:]]+)(_k([0-9]+))?$ ]]; then
      page="${BASH_REMATCH[1]}"
      frames="${BASH_REMATCH[2]}"
      replacer="${BASH_REMATCH[3]}"
      kval="${BASH_REMATCH[5]:-}"
    fi
    # 若目录名不规范，则从日志 [LOAD] begin: 兜底解析
    if [[ -z "$page" || -z "$frames" || -z "$replacer" ]]; then
      if line=$(grep -m1 "^\[LOAD\] begin:" "$log"); then
        [[ -z "$page"     ]] && page=$(sed -E 's/.*page_size=([0-9]+).*/\1/' <<<"$line")
        [[ -z "$frames"   ]] && frames=$(sed -E 's/.*frames=([0-9]+).*/\1/' <<<"$line")
        [[ -z "$replacer" ]] && replacer=$(sed -E 's/.*replacer=([[:alnum:]]+).*/\1/' <<<"$line")
        [[ "$replacer" == "lruk" && -z "$kval" ]] && kval=$(sed -nE 's/.*replacer=lruk.*\(k=([0-9]+)\).*/\1/p' "$log" | head -n1)
      fi
    fi

    # [LOAD] done 行
    done_line=$(grep -F "[LOAD] done:" "$log" | tail -n1 || true)
    rows="" bad="" pagesc="" hits="" misses="" evic="" flush=""
    if [[ -n "$done_line" ]]; then
      rows=$(sed -E 's/.*rows=([0-9]+).*/\1/' <<<"$done_line")
      bad=$(sed -E 's/.*bad=([0-9]+).*/\1/' <<<"$done_line")
      pagesc=$(sed -E 's/.*pages=([0-9]+).*/\1/' <<<"$done_line")
      hits=$(sed -E 's/.*hits=([0-9]+).*/\1/' <<<"$done_line")
      misses=$(sed -E 's/.*misses=([0-9]+).*/\1/' <<<"$done_line")
      evic=$(sed -E 's/.*evictions=([0-9]+).*/\1/' <<<"$done_line")
      flush=$(sed -E 's/.*flushes=([0-9]+).*/\1/' <<<"$done_line")
    fi

    # /usr/bin/time 的 TIME 行
    tline=$(grep -F "TIME real=" "$log" | tail -n1 || true)
    real="" user="" sys="" rss=""
    if [[ -n "$tline" ]]; then
      real=$(sed -E 's/.*real=([^ ]+).*/\1/' <<<"$tline")
      user=$(sed -E 's/.*user=([^ ]+).*/\1/' <<<"$tline")
      sys=$(sed -E 's/.*sys=([^ ]+).*/\1/' <<<"$tline")
      rss=$(sed -E 's/.*maxRSS=([^ ]+).*/\1/' <<<"$tline")
    fi

    echo "$base,$base,$page,$frames,$replacer,$kval,$rows,$bad,$pagesc,$hits,$misses,$evic,$flush,$real,$user,$sys,$rss"
  done
done
