#!/bin/bash
# perf stat csv summary test
# SPDX-License-Identifier: GPL-2.0

set -e

#
#     1.001364330 9224197  cycles 8012885033 100.00
#         summary 9224197  cycles 8012885033 100.00
#
perf stat -e cycles  -x' ' -I1000 --interval-count 1 --summary 2>&1 | \
grep -e summary | \
while read summary _ _ _ _
do
	if [ $summary != "summary" ]; then
		exit 1
	fi
done

#
#     1.001360298 9148534  cycles 8012853854 100.00
#9148534  cycles 8012853854 100.00
#
perf stat -e cycles  -x' ' -I1000 --interval-count 1 --summary --no-csv-summary 2>&1 | \
grep -e summary | \
while read _ _ _ _
do
	exit 1
done

exit 0
