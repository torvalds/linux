#ifndef _PERF_PARSE_SUBLEVEL_OPTIONS_H
#define _PERF_PARSE_SUBLEVEL_OPTIONS_H

struct sublevel_option {
	const char *name;
	int *value_ptr;
};

int perf_parse_sublevel_options(const char *str, struct sublevel_option *opts);

#endif