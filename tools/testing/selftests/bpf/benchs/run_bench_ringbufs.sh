#!/bin/bash

set -eufo pipefail

RUN_BENCH="sudo ./bench -w3 -d10 -a"

function hits()
{
	echo "$*" | sed -E "s/.*hits\s+([0-9]+\.[0-9]+ ± [0-9]+\.[0-9]+M\/s).*/\1/"
}

function drops()
{
	echo "$*" | sed -E "s/.*drops\s+([0-9]+\.[0-9]+ ± [0-9]+\.[0-9]+M\/s).*/\1/"
}

function header()
{
	local len=${#1}

	printf "\n%s\n" "$1"
	for i in $(seq 1 $len); do printf '='; done
	printf '\n'
}

function summarize()
{
	bench="$1"
	summary=$(echo $2 | tail -n1)
	printf "%-20s %s (drops %s)\n" "$bench" "$(hits $summary)" "$(drops $summary)"
}

header "Single-producer, parallel producer"
for b in rb-libbpf rb-custom pb-libbpf pb-custom; do
	summarize $b "$($RUN_BENCH $b)"
done

header "Single-producer, parallel producer, sampled notification"
for b in rb-libbpf rb-custom pb-libbpf pb-custom; do
	summarize $b "$($RUN_BENCH --rb-sampled $b)"
done

header "Single-producer, back-to-back mode"
for b in rb-libbpf rb-custom pb-libbpf pb-custom; do
	summarize $b "$($RUN_BENCH --rb-b2b $b)"
	summarize $b-sampled "$($RUN_BENCH --rb-sampled --rb-b2b $b)"
done

header "Ringbuf back-to-back, effect of sample rate"
for b in 1 5 10 25 50 100 250 500 1000 2000 3000; do
	summarize "rb-sampled-$b" "$($RUN_BENCH --rb-b2b --rb-batch-cnt $b --rb-sampled --rb-sample-rate $b rb-custom)"
done
header "Perfbuf back-to-back, effect of sample rate"
for b in 1 5 10 25 50 100 250 500 1000 2000 3000; do
	summarize "pb-sampled-$b" "$($RUN_BENCH --rb-b2b --rb-batch-cnt $b --rb-sampled --rb-sample-rate $b pb-custom)"
done

header "Ringbuf back-to-back, reserve+commit vs output"
summarize "reserve" "$($RUN_BENCH --rb-b2b                 rb-custom)"
summarize "output"  "$($RUN_BENCH --rb-b2b --rb-use-output rb-custom)"

header "Ringbuf sampled, reserve+commit vs output"
summarize "reserve-sampled" "$($RUN_BENCH --rb-sampled                 rb-custom)"
summarize "output-sampled"  "$($RUN_BENCH --rb-sampled --rb-use-output rb-custom)"

header "Single-producer, consumer/producer competing on the same CPU, low batch count"
for b in rb-libbpf rb-custom pb-libbpf pb-custom; do
	summarize $b "$($RUN_BENCH --rb-batch-cnt 1 --rb-sample-rate 1 --prod-affinity 0 --cons-affinity 0 $b)"
done

header "Ringbuf, multi-producer contention"
for b in 1 2 3 4 8 12 16 20 24 28 32 36 40 44 48 52; do
	summarize "rb-libbpf nr_prod $b" "$($RUN_BENCH -p$b --rb-batch-cnt 50 rb-libbpf)"
done

