// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/socket.c
 *
 *  Copyright (C) 2018, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <sys/types.h>
#include <sys/socket.h>

#include "trace/beauty/generated/socket.c"

static size_t socket__scnprintf_ipproto(int protocol, char *bf, size_t size, bool show_prefix)
{
	static DEFINE_STRARRAY(socket_ipproto, "IPPROTO_");

	return strarray__scnprintf(&strarray__socket_ipproto, bf, size, "%d", show_prefix, protocol);
}

size_t syscall_arg__scnprintf_socket_protocol(char *bf, size_t size, struct syscall_arg *arg)
{
	int domain = syscall_arg__val(arg, 0);

	if (domain == AF_INET || domain == AF_INET6)
		return socket__scnprintf_ipproto(arg->val, bf, size, arg->show_string_prefix);

	return syscall_arg__scnprintf_int(bf, size, arg);
}

static size_t socket__scnprintf_level(int level, char *bf, size_t size, bool show_prefix)
{
#if defined(__alpha__) || defined(__hppa__) || defined(__mips__) || defined(__sparc__)
	const int sol_socket = 0xffff;
#else
	const int sol_socket = 1;
#endif
	if (level == sol_socket)
		return scnprintf(bf, size, "%sSOCKET", show_prefix ? "SOL_" : "");

	return strarray__scnprintf(&strarray__socket_level, bf, size, "%d", show_prefix, level);
}

size_t syscall_arg__scnprintf_socket_level(char *bf, size_t size, struct syscall_arg *arg)
{
	return socket__scnprintf_level(arg->val, bf, size, arg->show_string_prefix);
}
