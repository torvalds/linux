#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "util/debug.h"
#include "util/parse-sublevel-options.h"

static int parse_one_sublevel_option(const char *str,
				     struct sublevel_option *opts)
{
	struct sublevel_option *opt = opts;
	char *vstr, *s = strdup(str);
	int v = 1;

	if (!s) {
		pr_err("no memory\n");
		return -1;
	}

	vstr = strchr(s, '=');
	if (vstr)
		*vstr++ = 0;

	while (opt->name) {
		if (!strcmp(s, opt->name))
			break;
		opt++;
	}

	if (!opt->name) {
		pr_err("Unknown option name '%s'\n", s);
		free(s);
		return -1;
	}

	if (vstr)
		v = atoi(vstr);

	*opt->value_ptr = v;
	free(s);
	return 0;
}

/* parse options like --foo a=<n>,b,c... */
int perf_parse_sublevel_options(const char *str, struct sublevel_option *opts)
{
	char *s = strdup(str);
	char *p = NULL;
	int ret;

	if (!s) {
		pr_err("no memory\n");
		return -1;
	}

	p = strtok(s, ",");
	while (p) {
		ret = parse_one_sublevel_option(p, opts);
		if (ret) {
			free(s);
			return ret;
		}

		p = strtok(NULL, ",");
	}

	free(s);
	return 0;
}
