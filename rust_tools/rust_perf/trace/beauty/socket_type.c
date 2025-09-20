// SPDX-License-Identifier: LGPL-2.1
#include <sys/types.h>
#include <sys/socket.h>

#ifndef SOCK_DCCP
# define SOCK_DCCP		6
#endif

#ifndef SOCK_CLOEXEC
# define SOCK_CLOEXEC		02000000
#endif

#ifndef SOCK_NONBLOCK
# define SOCK_NONBLOCK		00004000
#endif

#ifndef SOCK_TYPE_MASK
#define SOCK_TYPE_MASK 0xf
#endif

static size_t syscall_arg__scnprintf_socket_type(char *bf, size_t size, struct syscall_arg *arg)
{
	bool show_prefix = arg->show_string_prefix;
	const char *prefix = "SOCK_";
	size_t printed;
	int type = arg->val,
	    flags = type & ~SOCK_TYPE_MASK;

	type &= SOCK_TYPE_MASK;
	/*
	 * Can't use a strarray, MIPS may override for ABI reasons.
	 */
	switch (type) {
#define	P_SK_TYPE(n) case SOCK_##n: printed = scnprintf(bf, size, "%s%s", show_prefix ? prefix : "", #n); break;
	P_SK_TYPE(STREAM);
	P_SK_TYPE(DGRAM);
	P_SK_TYPE(RAW);
	P_SK_TYPE(RDM);
	P_SK_TYPE(SEQPACKET);
	P_SK_TYPE(DCCP);
	P_SK_TYPE(PACKET);
#undef P_SK_TYPE
	default:
		printed = scnprintf(bf, size, "%#x", type);
	}

#define	P_SK_FLAG(n) \
	if (flags & SOCK_##n) { \
		printed += scnprintf(bf + printed, size - printed, "|%s", #n); \
		flags &= ~SOCK_##n; \
	}

	P_SK_FLAG(CLOEXEC);
	P_SK_FLAG(NONBLOCK);
#undef P_SK_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "|%#x", flags);

	return printed;
}

#define SCA_SK_TYPE syscall_arg__scnprintf_socket_type
