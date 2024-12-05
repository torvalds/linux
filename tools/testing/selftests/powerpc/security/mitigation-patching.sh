#!/usr/bin/env bash

set -euo pipefail

TIMEOUT=10

function do_one
{
    local mitigation="$1"
    local orig
    local start
    local now

    orig=$(cat "$mitigation")

    start=$(date +%s)
    now=$start

    while [[ $((now-start)) -lt "$TIMEOUT" ]]
    do
        echo 0 > "$mitigation"
        echo 1 > "$mitigation"

        now=$(date +%s)
    done

    echo "$orig" > "$mitigation"
}

rc=0
cd /sys/kernel/debug/powerpc || rc=1
if [[ "$rc" -ne 0 ]]; then
    echo "Error: couldn't cd to /sys/kernel/debug/powerpc" >&2
    exit 1
fi

tainted=$(cat /proc/sys/kernel/tainted)
if [[ "$tainted" -ne 0 ]]; then
    echo "Warning: kernel already tainted! ($tainted)" >&2
fi

mitigations="barrier_nospec stf_barrier count_cache_flush rfi_flush entry_flush uaccess_flush"

for m in $mitigations
do
    if [[ -f /sys/kernel/debug/powerpc/$m ]]
    then
        do_one "$m" &
    fi
done

echo "Spawned threads enabling/disabling mitigations ..."

if stress-ng > /dev/null 2>&1; then
    stress="stress-ng"
elif stress > /dev/null 2>&1; then
    stress="stress"
else
    stress=""
fi

if [[ -n "$stress" ]]; then
    "$stress" -m "$(nproc)" -t "$TIMEOUT" &
    echo "Spawned VM stressors ..."
fi

echo "Waiting for timeout ..."
wait

orig_tainted=$tainted
tainted=$(cat /proc/sys/kernel/tainted)
if [[ "$tainted" != "$orig_tainted" ]]; then
    echo "Error: kernel newly tainted, before ($orig_tainted) after ($tainted)" >&2
    exit 1
fi

echo "OK"
exit 0
