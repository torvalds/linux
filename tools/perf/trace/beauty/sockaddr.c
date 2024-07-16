// SPDX-License-Identifier: LGPL-2.1
// Copyright (C) 2018, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>

#include "trace/beauty/beauty.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include "trace/beauty/generated/sockaddr.c"
DEFINE_STRARRAY(socket_families, "PF_");

static size_t af_inet__scnprintf(struct sockaddr *sa, char *bf, size_t size)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;
	char tmp[16];
	return scnprintf(bf, size, ", port: %d, addr: %s", ntohs(sin->sin_port),
			 inet_ntop(sin->sin_family, &sin->sin_addr, tmp, sizeof(tmp)));
}

static size_t af_inet6__scnprintf(struct sockaddr *sa, char *bf, size_t size)
{
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
	u32 flowinfo = ntohl(sin6->sin6_flowinfo);
	char tmp[512];
	size_t printed = scnprintf(bf, size, ", port: %d, addr: %s", ntohs(sin6->sin6_port),
				   inet_ntop(sin6->sin6_family, &sin6->sin6_addr, tmp, sizeof(tmp)));
	if (flowinfo != 0)
		printed += scnprintf(bf + printed, size - printed, ", flowinfo: %lu", flowinfo);
	if (sin6->sin6_scope_id != 0)
		printed += scnprintf(bf + printed, size - printed, ", scope_id: %lu", sin6->sin6_scope_id);

	return printed;
}

static size_t af_local__scnprintf(struct sockaddr *sa, char *bf, size_t size)
{
	struct sockaddr_un *sun = (struct sockaddr_un *)sa;
	return scnprintf(bf, size, ", path: %s", sun->sun_path);
}

static size_t (*af_scnprintfs[])(struct sockaddr *sa, char *bf, size_t size) = {
	[AF_LOCAL] = af_local__scnprintf,
	[AF_INET]  = af_inet__scnprintf,
	[AF_INET6] = af_inet6__scnprintf,
};

static size_t syscall_arg__scnprintf_augmented_sockaddr(struct syscall_arg *arg, char *bf, size_t size)
{
	struct sockaddr *sa = (struct sockaddr *)arg->augmented.args;
	char family[32];
	size_t printed;

	strarray__scnprintf(&strarray__socket_families, family, sizeof(family), "%d", arg->show_string_prefix, sa->sa_family);
	printed = scnprintf(bf, size, "{ .family: %s", family);

	if (sa->sa_family < ARRAY_SIZE(af_scnprintfs) && af_scnprintfs[sa->sa_family])
		printed += af_scnprintfs[sa->sa_family](sa, bf + printed, size - printed);

	return printed + scnprintf(bf + printed, size - printed, " }");
}

size_t syscall_arg__scnprintf_sockaddr(char *bf, size_t size, struct syscall_arg *arg)
{
	if (arg->augmented.args)
		return syscall_arg__scnprintf_augmented_sockaddr(arg, bf, size);

	return scnprintf(bf, size, "%#lx", arg->val);
}
