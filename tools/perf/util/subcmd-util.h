#ifndef __PERF_SUBCMD_UTIL_H
#define __PERF_SUBCMD_UTIL_H

#include <stdio.h>

#define astrcatf(out, fmt, ...)						\
({									\
	char *tmp = *(out);						\
	if (asprintf((out), "%s" fmt, tmp ?: "", ## __VA_ARGS__) == -1)	\
		die("asprintf failed");					\
	free(tmp);							\
})

static inline void astrcat(char **out, const char *add)
{
	char *tmp = *out;

	if (asprintf(out, "%s%s", tmp ?: "", add) == -1)
		die("asprintf failed");

	free(tmp);
}

#endif /* __PERF_SUBCMD_UTIL_H */
