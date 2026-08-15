// SPDX-License-Identifier: GPL-2.0
#include "debug.h"
#include "symbol_conf.h"
#include "unwind.h"
#include <linux/string.h>
#include <string.h>
#include <stdlib.h>

int unwind__get_entries(unwind_entry_cb_t cb __maybe_unused, void *arg __maybe_unused,
			struct thread *thread __maybe_unused,
			struct perf_sample *data __maybe_unused,
			int max_stack __maybe_unused,
			bool best_effort __maybe_unused)
{
	int ret = 0;

#if defined(HAVE_LIBDW_SUPPORT) || defined(HAVE_LIBUNWIND_SUPPORT)
	if (symbol_conf.unwind_style[0] == UNWIND_STYLE_UNKNOWN) {
		int i = 0;
#ifdef HAVE_LIBDW_SUPPORT
		symbol_conf.unwind_style[i++] = UNWIND_STYLE_LIBDW;
#endif
#ifdef HAVE_LIBUNWIND_SUPPORT
		symbol_conf.unwind_style[i++] = UNWIND_STYLE_LIBUNWIND;
#endif
	}
#endif //defined(HAVE_LIBDW_SUPPORT) || defined(HAVE_LIBUNWIND_SUPPORT)

	for (size_t i = 0; i < ARRAY_SIZE(symbol_conf.unwind_style); i++) {
		switch (symbol_conf.unwind_style[i]) {
		case UNWIND_STYLE_LIBDW:
			ret = libdw__get_entries(cb, arg, thread, data, max_stack, best_effort);
			break;
		case UNWIND_STYLE_LIBUNWIND:
			ret = libunwind__get_entries(cb, arg, thread, data, max_stack, best_effort);
			break;
		case UNWIND_STYLE_UNKNOWN:
		default:
#if !defined(HAVE_LIBDW_SUPPORT) && !defined(HAVE_LIBUNWIND_SUPPORT)
			pr_warning_once(
				"Error: dwarf unwinding not supported, build perf with libdw or libunwind.\n");
#endif
			ret = 0;
			break;
		}
		if (ret > 0) {
			ret = 0;
			break;
		}
		if (ret < 0)
			break;
	}
	return ret;
}

int unwind__configure(const char *var, const char *value, void *cb __maybe_unused)
{
	static const char * const unwind_style_names[] = {
		[UNWIND_STYLE_LIBDW] = "libdw",
		[UNWIND_STYLE_LIBUNWIND] = "libunwind",
		NULL
	};
	char *s, *p, *saveptr;
	size_t i = 0;

	if (strcmp(var, "unwind.style"))
		return 0;

	if (!value)
		return -1;

	s = strdup(value);
	if (!s)
		return -1;

	memset(symbol_conf.unwind_style, 0, sizeof(symbol_conf.unwind_style));

	p = strtok_r(s, ",", &saveptr);
	while (p && i < ARRAY_SIZE(symbol_conf.unwind_style)) {
		bool found = false;
		char *q = strim(p);

		for (size_t j = UNWIND_STYLE_LIBDW; j < MAX_UNWIND_STYLE; j++) {
			if (!strcasecmp(q, unwind_style_names[j])) {
				symbol_conf.unwind_style[i++] = j;
				found = true;
				break;
			}
		}
		if (!found)
			pr_warning("Unknown unwind style: %s\n", q);
		p = strtok_r(NULL, ",", &saveptr);
	}

	free(s);
	return 0;
}

int unwind__option(const struct option *opt __maybe_unused,
		   const char *arg,
		   int unset __maybe_unused)
{
	return unwind__configure("unwind.style", arg, NULL);
}
