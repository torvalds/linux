#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
pids="$(pgrep ^$1)" || exit 1
for pid in $pids
do
  chrt -p $pid | cut -d ':' -f 2 | head -n1 | grep "^ $2\$" >/dev/null
  chrt -p $pid | cut -d ':' -f 2 | tail -n1 | grep "^ $3\$" >/dev/null
done && echo "Priorities are set correctly"
