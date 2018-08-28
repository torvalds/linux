// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>

#include "trace/beauty/beauty.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <arpa/inet.h>

static const char *socket_families[] = {
	"UNSPEC", "LOCAL", "INET", "AX25", "IPX", "APPLETALK", "NETROM",
	"BRIDGE", "ATMPVC", "X25", "INET6", "ROSE", "DECnet", "NETBEUI",
	"SECURITY", "KEY", "NETLINK", "PACKET", "ASH", "ECONET", "ATMSVC",
	"RDS", "SNA", "IRDA", "PPPOX", "WANPIPE", "LLC", "IB", "CAN", "TIPC",
	"BLUETOOTH", "IUCV", "RXRPC", "ISDN", "PHONET", "IEEE802154", "CAIF",
	"ALG", "NFC", "VSOCK",
};
DEFINE_STRARRAY(socket_families);

static size_t syscall_arg__scnprintf_augmented_sockaddr(struct syscall_arg *arg, char *bf, size_t size)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)arg->augmented.args;
	char family[32];
	size_t printed;

	strarray__scnprintf(&strarray__socket_families, family, sizeof(family), "%d", sin->sin_family);
	printed = scnprintf(bf, size, "{ .family: %s", family);

	if (sin->sin_family == AF_INET) {
		char tmp[512];
		printed += scnprintf(bf + printed, size - printed, ", port: %d, addr: %s", ntohs(sin->sin_port),
				     inet_ntop(sin->sin_family, &sin->sin_addr, tmp, sizeof(tmp)));
	} else if (sin->sin_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sin;
		u32 flowinfo = ntohl(sin6->sin6_flowinfo);
		char tmp[512];

		printed += scnprintf(bf + printed, size - printed, ", port: %d, addr: %s", ntohs(sin6->sin6_port),
				     inet_ntop(sin6->sin6_family, &sin6->sin6_addr, tmp, sizeof(tmp)));
		if (flowinfo != 0)
			printed += scnprintf(bf + printed, size - printed, ", flowinfo: %lu", flowinfo);
		if (sin6->sin6_scope_id != 0)
			printed += scnprintf(bf + printed, size - printed, ", scope_id: %lu", sin6->sin6_scope_id);
	} else if (sin->sin_family == AF_LOCAL) {
		struct sockaddr_un *sun = (struct sockaddr_un *)sin;
		printed += scnprintf(bf + printed, size - printed, ", path: %s", sun->sun_path);
	}

	return printed + scnprintf(bf + printed, size - printed, " }");
}

size_t syscall_arg__scnprintf_sockaddr(char *bf, size_t size, struct syscall_arg *arg)
{
	if (arg->augmented.args)
		return syscall_arg__scnprintf_augmented_sockaddr(arg, bf, size);

	return scnprintf(bf, size, "%#x", arg->val);
}
