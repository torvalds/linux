#!/bin/bash
# perf annotate weight regression test
# SPDX-License-Identifier: GPL-2.0

set -e

shelldir=$(dirname "$0")
# shellcheck source=tools/perf/tests/shell/lib/perf_has_symbol.sh
. "${shelldir}"/lib/perf_has_symbol.sh

testsym="test_loop"
skip_test_missing_symbol "${testsym}"

perfdata=$(mktemp /tmp/__perf_test.annotate_weight.XXXXX)
record_log=$(mktemp /tmp/__perf_test.annotate_weight.XXXXX.log)
report_out=$(mktemp /tmp/__perf_test.annotate_weight.XXXXX.report)
annotate_out=$(mktemp /tmp/__perf_test.annotate_weight.XXXXX.annotate)

cleanup() {
	rm -f "${perfdata}" "${record_log}" "${report_out}" "${annotate_out}"
	trap - EXIT TERM INT
}

trap 'cleanup; exit 1' TERM INT
trap cleanup EXIT

# mem-loads:pu requests a precise user PEBS event whose sample weight should
# be populated by -W. Unsupported PEBS/weight PMUs are skipped below.
if ! perf record -W -e mem-loads:pu -o "${perfdata}" -- perf test -w thloop \
	> /dev/null 2> "${record_log}"; then
	echo "[SKIP] precise PEBS weight sampling is unavailable"
	exit 2
fi

# Confirm the PMU actually produced nonzero weights. A successful record alone
# is insufficient: some PMUs accept the event but provide no weight payload.
if ! perf report --stdio -i "${perfdata}" --fields=weight1,weight2,weight3,symbol --percent-limit 0 \
	> "${report_out}" 2> "${record_log}"; then
	echo "[SKIP] weighted samples cannot be decoded"
	exit 2
fi

if ! awk '$1 ~ /^[0-9]/ && ($1 + 0) > 0 { found = 1 } END { exit !found }' \
	"${report_out}"; then
	echo "[SKIP] PEBS weight sampling produced no nonzero weights"
	exit 2
fi

perf annotate --stdio -i "${perfdata}" --symbol "${testsym}" \
	> "${annotate_out}" 2> "${record_log}"

grep -q 'Percent Weight' "${annotate_out}"

# The second numeric column is the rendered average weight. This assertion
# fails on the original regression because calc_percent() left it at zero.
if ! awk '$1 ~ /^[0-9]/ && $2 ~ /^[0-9]/ && ($2 + 0) > 0 { found = 1 } END { exit !found }' \
	"${annotate_out}"; then
	echo "Annotation output contained no nonzero weight"
	cat "${annotate_out}"
	exit 1
fi

echo "PEBS annotation weights: PASS"
