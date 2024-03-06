#!/bin/bash
# perf metrics value validation
# SPDX-License-Identifier: GPL-2.0

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

grep -q GenuineIntel /proc/cpuinfo || { echo Skipping non-Intel; exit 2; }

pythonvalidator=$(dirname $0)/lib/perf_metric_validation.py
rulefile=$(dirname $0)/lib/perf_metric_validation_rules.json
tmpdir=$(mktemp -d /tmp/__perf_test.program.XXXXX)
workload="perf bench futex hash -r 2 -s"

# Add -debug, save data file and full rule file
echo "Launch python validation script $pythonvalidator"
echo "Output will be stored in: $tmpdir"
$PYTHON $pythonvalidator -rule $rulefile -output_dir $tmpdir -wl "${workload}"
ret=$?
rm -rf $tmpdir

exit $ret

