// SPDX-License-Identifier: GPL-2.0
/*
 * trace/beauty/socket.c
 *
 *  Copyright (C) 2018, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <sys/types.h>
#include <sys/socket.h>

static size_t socket__scnprintf_ipproto(int protocol, char *bf, size_t size)
{
#include "trace/beauty/generated/socket_ipproto_array.c"
	static DEFINE_STRARRAY(socket_ipproto);

	return strarray__scnprintf(&strarray__socket_ipproto, bf, size, "%d", protocol);
}

size_t syscall_arg__scnprintf_socket_protocol(char *bf, size_t size, struct syscall_arg *arg)
{
	int domain = syscall_arg__val(arg, 0);

	if (domain == AF_INET || domain == AF_INET6)
		return socket__scnprintf_ipproto(arg->val, bf, size);

	return syscall_arg__scnprintf_int(bf, size, arg);
}
