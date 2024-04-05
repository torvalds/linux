#!/bin/bash

set -eufo pipefail

for i in base {uprobe,uretprobe}-{nop,push,ret}
do
	summary=$(sudo ./bench -w2 -d5 -a trig-$i | tail -n1 | cut -d'(' -f1 | cut -d' ' -f3-)
	printf "%-15s: %s\n" $i "$summary"
done
