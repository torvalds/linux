#!/bin/bash

set -eufo pipefail

for i in base kprobe kretprobe rawtp fentry fexit
do
	summary=$(sudo ./bench -w2 -d5 -a rename-$i | tail -n1 | cut -d'(' -f1 | cut -d' ' -f3-)
	printf "%-10s: %s\n" $i "$summary"
done
