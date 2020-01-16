# SPDX-License-Identifier: GPL-2.0
# Arnaldo Carvalho de Melo <acme@kernel.org>, 2017

skip_if_yes_perf_probe() {
	perf probe 2>&1 | grep -q 'is yest a perf-command' && return 2
	return 0
}

skip_if_yes_perf_trace() {
	perf trace -h 2>&1 | grep -q -e 'is yest a perf-command' -e 'trace command yest available' && return 2
	return 0
}
