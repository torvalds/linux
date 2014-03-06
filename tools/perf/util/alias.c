#include "cache.h"

static const char *alias_key;
static char *alias_val;

static int alias_lookup_cb(const char *k, const char *v,
			   void *cb __maybe_unused)
{
	if (!prefixcmp(k, "alias.") && !strcmp(k+6, alias_key)) {
		if (!v)
			return config_error_nonbool(k);
		alias_val = strdup(v);
		return 0;
	}
	return 0;
}

char *alias_lookup(const char *alias)
{
	alias_key = alias;
	alias_val = NULL;
	perf_config(alias_lookup_cb, NULL);
	return alias_val;
}

int split_cmdline(char *cmdline, const char ***argv)
{
	int src, dst, count = 0, size = 16;
	char quoted = 0;

	*argv = malloc(sizeof(char*) * size);

	/* split alias_string */
	(*argv)[count++] = cmdline;
	for (src = dst = 0; cmdline[src];) {
		char c = cmdline[src];
		if (!quoted && isspace(c)) {
			cmdline[dst++] = 0;
			while (cmdline[++src]
					&& isspace(cmdline[src]))
				; /* skip */
			if (count >= size) {
				size += 16;
				*argv = realloc(*argv, sizeof(char*) * size);
			}
			(*argv)[count++] = cmdline + dst;
		} else if (!quoted && (c == '\'' || c == '"')) {
			quoted = c;
			src++;
		} else if (c == quoted) {
			quoted = 0;
			src++;
		} else {
			if (c == '\\' && quoted != '\'') {
				src++;
				c = cmdline[src];
				if (!c) {
					zfree(argv);
					return error("cmdline ends with \\");
				}
			}
			cmdline[dst++] = c;
			src++;
		}
	}

	cmdline[dst] = 0;

	if (quoted) {
		zfree(argv);
		return error("unclosed quote");
	}

	return count;
}

