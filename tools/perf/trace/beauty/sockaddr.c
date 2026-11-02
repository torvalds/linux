// SPDX-License-Identifier: LGPL-2.1
// Copyright (C) 2018, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>

#include "trace/beauty/beauty.h"
#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include "trace/beauty/generated/sockaddr.c"
DEFINE_STRARRAY(socket_families, "PF_");

static size_t af_inet__scnprintf(struct sockaddr *sa, size_t sa_size, char *bf, size_t size)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;
	char tmp[16];

	if (sa_size < sizeof(*sin))
		return 0;

	return scnprintf(bf, size, ", port: %d, addr: %s", ntohs(sin->sin_port),
			 inet_ntop(sin->sin_family, &sin->sin_addr, tmp, sizeof(tmp)));
}

static size_t af_inet6__scnprintf(struct sockaddr *sa, size_t sa_size, char *bf, size_t size)
{
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
	u32 flowinfo;
	char tmp[512];
	size_t printed;

	/* RFC 2133's version, which the kernel accepts, lacks sin6_scope_id. */
	if (sa_size < offsetof(struct sockaddr_in6, sin6_scope_id))
		return 0;

	flowinfo = ntohl(sin6->sin6_flowinfo);
	printed = scnprintf(bf, size, ", port: %d, addr: %s", ntohs(sin6->sin6_port),
			    inet_ntop(sin6->sin6_family, &sin6->sin6_addr, tmp, sizeof(tmp)));
	if (flowinfo != 0)
		printed += scnprintf(bf + printed, size - printed, ", flowinfo: %lu", flowinfo);
	if (sa_size >= sizeof(*sin6) && sin6->sin6_scope_id != 0)
		printed += scnprintf(bf + printed, size - printed, ", scope_id: %lu", sin6->sin6_scope_id);

	return printed;
}

static size_t af_local__scnprintf(struct sockaddr *sa, size_t sa_size, char *bf, size_t size)
{
	struct sockaddr_un *sun = (struct sockaddr_un *)sa;
	size_t path_size;

	if (sa_size <= offsetof(struct sockaddr_un, sun_path))
		return 0;

	/* The path needn't be NUL terminated. */
	path_size = sa_size - offsetof(struct sockaddr_un, sun_path);
	if (path_size > sizeof(sun->sun_path))
		path_size = sizeof(sun->sun_path);

	return scnprintf(bf, size, ", path: %.*s", (int)path_size, sun->sun_path);
}

static size_t (*af_scnprintfs[])(struct sockaddr *sa, size_t sa_size, char *bf, size_t size) = {
	[AF_LOCAL] = af_local__scnprintf,
	[AF_INET]  = af_inet__scnprintf,
	[AF_INET6] = af_inet6__scnprintf,
};

static size_t syscall_arg__scnprintf_augmented_sockaddr(struct syscall_arg *arg, char *bf, size_t size)
{
	const struct augmented_arg *augmented_arg = arg->augmented.args;
	struct sockaddr *sa = (struct sockaddr *)&augmented_arg->value;
	size_t sa_size = (size_t)augmented_arg->size;
	char family[32];
	size_t printed;

	strarray__scnprintf(&strarray__socket_families, family, sizeof(family), "%d", arg->show_string_prefix, sa->sa_family);
	printed = scnprintf(bf, size, "{ .family: %s", family);

	if (sa->sa_family < ARRAY_SIZE(af_scnprintfs) && af_scnprintfs[sa->sa_family])
		printed += af_scnprintfs[sa->sa_family](sa, sa_size, bf + printed, size - printed);

	return printed + scnprintf(bf + printed, size - printed, " }");
}

size_t syscall_arg__scnprintf_sockaddr(char *bf, size_t size, struct syscall_arg *arg)
{
	/* The family printers check the rest of the payload. */
	if (syscall_arg__augmented_args_valid(arg, sizeof(sa_family_t)))
		return syscall_arg__scnprintf_augmented_sockaddr(arg, bf, size);

	return scnprintf(bf, size, "%#lx", arg->val);
}
