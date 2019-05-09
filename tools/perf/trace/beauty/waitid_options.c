// SPDX-License-Identifier: LGPL-2.1
#include <sys/types.h>
#include <sys/wait.h>

static size_t syscall_arg__scnprintf_waitid_options(char *bf, size_t size,
						    struct syscall_arg *arg)
{
	bool show_prefix = arg->show_string_prefix;
	const char *prefix = "W";
	int printed = 0, options = arg->val;

#define	P_OPTION(n) \
	if (options & W##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s%s", printed ? "|" : "", show_prefix ? prefix : "",  #n); \
		options &= ~W##n; \
	}

	P_OPTION(NOHANG);
	P_OPTION(UNTRACED);
	P_OPTION(CONTINUED);
#undef P_OPTION

	if (options)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", options);

	return printed;
}

#define SCA_WAITID_OPTIONS syscall_arg__scnprintf_waitid_options
