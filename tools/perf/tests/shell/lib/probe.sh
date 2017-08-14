# Arnaldo Carvalho de Melo <acme@kernel.org>, 2017

skip_if_no_perf_probe() {
	perf probe |& grep -q 'is not a perf-command' && return 2
	return 0
}
