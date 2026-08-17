#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Run dt-check-style against fixtures under good/ and bad/.
# good/ files must produce no output and exit 0 in both modes.
# bad/ files must produce the expected output (in expected/<name>.txt)
# and exit 1.
#
# The mode used for a bad fixture is whichever produces a violation:
# trailing-whitespace and tab fixtures use the default (relaxed),
# the rest use --mode=strict. The expected output files name the
# mode in their first line.

set -u

here=$(cd "$(dirname "$0")" && pwd)
tool="$here/../dt-check-style"
fail=0

run() {
    file=$1
    mode=$2
    "$tool" --mode="$mode" "$file" 2>&1
}

# good/ -- must exit 0 and produce no output in both modes
for f in "$here"/good/*; do
    [ -e "$f" ] || continue
    for mode in relaxed strict; do
        out=$(run "$f" "$mode")
        rc=$?
        if [ -n "$out" ] || [ "$rc" -ne 0 ]; then
            echo "FAIL good/$mode: $(basename "$f") (exit $rc, want 0):"
            echo "$out" | sed 's/^/  /'
            fail=$((fail + 1))
        fi
    done
done

# bad/ -- must match expected/<name>.txt
for f in "$here"/bad/*; do
    [ -e "$f" ] || continue
    name=$(basename "$f")
    expected="$here/expected/$name.txt"
    if [ ! -f "$expected" ]; then
        echo "FAIL bad: missing $expected"
        fail=$((fail + 1))
        continue
    fi
    mode=$(head -1 "$expected" | sed 's/^# mode=//')
    body=$(tail -n +2 "$expected")
    out=$(run "$f" "$mode")
    rc=$?
    # Strip the directory prefix so expected files are portable.
    out=$(printf '%s\n' "$out" | sed "s|$here/bad/|bad/|g")
    if [ "$out" != "$body" ] || [ "$rc" -ne 1 ]; then
        echo "FAIL bad/$mode: $name (exit $rc, want 1):"
        bf=$(mktemp)
        printf '%s\n' "$body" > "$bf"
        printf '%s\n' "$out" | diff -u "$bf" - | sed 's/^/  /'
        rm -f "$bf"
        fail=$((fail + 1))
    fi
done

if [ "$fail" -eq 0 ]; then
    echo "PASS"
    exit 0
fi
echo "FAILED ($fail)"
exit 1
