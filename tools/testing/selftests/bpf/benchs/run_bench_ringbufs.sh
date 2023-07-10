#!/bin/bash

source ./benchs/run_common.sh

set -eufo pipefail

RUN_RB_BENCH="$RUN_BENCH -c1"

header "Single-producer, parallel producer"
for b in rb-libbpf rb-custom pb-libbpf pb-custom; do
	summarize $b "$($RUN_RB_BENCH $b)"
done

header "Single-producer, parallel producer, sampled notification"
for b in rb-libbpf rb-custom pb-libbpf pb-custom; do
	summarize $b "$($RUN_RB_BENCH --rb-sampled $b)"
done

header "Single-producer, back-to-back mode"
for b in rb-libbpf rb-custom pb-libbpf pb-custom; do
	summarize $b "$($RUN_RB_BENCH --rb-b2b $b)"
	summarize $b-sampled "$($RUN_RB_BENCH --rb-sampled --rb-b2b $b)"
done

header "Ringbuf back-to-back, effect of sample rate"
for b in 1 5 10 25 50 100 250 500 1000 2000 3000; do
	summarize "rb-sampled-$b" "$($RUN_RB_BENCH --rb-b2b --rb-batch-cnt $b --rb-sampled --rb-sample-rate $b rb-custom)"
done
header "Perfbuf back-to-back, effect of sample rate"
for b in 1 5 10 25 50 100 250 500 1000 2000 3000; do
	summarize "pb-sampled-$b" "$($RUN_RB_BENCH --rb-b2b --rb-batch-cnt $b --rb-sampled --rb-sample-rate $b pb-custom)"
done

header "Ringbuf back-to-back, reserve+commit vs output"
summarize "reserve" "$($RUN_RB_BENCH --rb-b2b                 rb-custom)"
summarize "output"  "$($RUN_RB_BENCH --rb-b2b --rb-use-output rb-custom)"

header "Ringbuf sampled, reserve+commit vs output"
summarize "reserve-sampled" "$($RUN_RB_BENCH --rb-sampled                 rb-custom)"
summarize "output-sampled"  "$($RUN_RB_BENCH --rb-sampled --rb-use-output rb-custom)"

header "Single-producer, consumer/producer competing on the same CPU, low batch count"
for b in rb-libbpf rb-custom pb-libbpf pb-custom; do
	summarize $b "$($RUN_RB_BENCH --rb-batch-cnt 1 --rb-sample-rate 1 --prod-affinity 0 --cons-affinity 0 $b)"
done

header "Ringbuf, multi-producer contention"
for b in 1 2 3 4 8 12 16 20 24 28 32 36 40 44 48 52; do
	summarize "rb-libbpf nr_prod $b" "$($RUN_RB_BENCH -p$b --rb-batch-cnt 50 rb-libbpf)"
done

