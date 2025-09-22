/*	$OpenBSD: mmap-sysctl-copyin.c,v 1.3 2021/12/13 16:56:50 deraadt Exp $	*/

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FILE	"sysctl-net.inet.tcp.always_keepalive"
#define CLIENT	"/mnt/regress-nfs-client"
#define SERVER	"/mnt/regress-nfs-server"

int
main(void)
{
	char *p, path[PATH_MAX];
	int mib[] = { CTL_NET, PF_INET, IPPROTO_TCP, TCPCTL_ALWAYS_KEEPALIVE };
	u_int miblen = sizeof(mib) / sizeof(mib[0]);
	int fd, val;
	size_t len;

	/*
	 * Get current value of sysctl net.inet.tcp.always_keepalive
	 * and write it into a file on the NFS server.
	 */
	snprintf(path, sizeof(path), "%s/%s", SERVER, FILE);
	if ((fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0777)) == -1)
		err(1, "open write '%s'", path);
	len = sizeof(int);
	if (sysctl(mib, miblen, &val, &len, NULL, 0) == -1)
		err(1, "sysctl get keepalive");
	if (len != sizeof(int))
		errx(1, "len is not %zu: %zu", sizeof(int), len);
	if (write(fd, &val, len) == -1)
		err(1, "write");
	if (close(fd) == -1)
		err(1, "close write");

	/*
	 * Map file on NFS client and read value to
	 * sysctl net.inet.tcp.always_keepalive.
	 */
	snprintf(path, sizeof(path), "%s/%s", CLIENT, FILE);
	if ((fd = open(path, O_RDWR)) == -1)
		err(1, "open mmap '%s'", path);
	p = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		err(1, "mmap");
	if (sysctl(mib, miblen, NULL, 0, p, sizeof(int)) == -1)
		err(1, "sysctl set keepalive");
	if (close(fd) == -1)
		err(1, "close mmap");

	return (0);
}
