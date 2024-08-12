#!/bin/bash

set -eufo pipefail

def_tests=( \
	usermode-count kernel-count syscall-count \
	fentry fexit fmodret \
	rawtp tp \
	kprobe kprobe-multi \
	kretprobe kretprobe-multi \
)

tests=("$@")
if [ ${#tests[@]} -eq 0 ]; then
	tests=("${def_tests[@]}")
fi

p=${PROD_CNT:-1}

for t in "${tests[@]}"; do
	summary=$(sudo ./bench -w2 -d5 -a -p$p trig-$t | tail -n1 | cut -d'(' -f1 | cut -d' ' -f3-)
	printf "%-15s: %s\n" $t "$summary"
done
