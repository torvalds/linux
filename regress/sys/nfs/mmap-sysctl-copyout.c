/*	$OpenBSD: mmap-sysctl-copyout.c,v 1.3 2021/12/13 16:56:50 deraadt Exp $	*/

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

#define FILE	"sysctl-net.inet.tcp.stats"
#define CLIENT	"/mnt/regress-nfs-client"
#define SERVER	"/mnt/regress-nfs-server"

int
main(void)
{
	char *p, path[PATH_MAX];
	int mib[] = { CTL_NET, PF_INET, IPPROTO_TCP, TCPCTL_STATS };
	u_int miblen = sizeof(mib) / sizeof(mib[0]);
	struct tcpstat stats;
	int fd;
	size_t len;
	ssize_t n;

	/*
	 * Initialize file on NFS server.
	 */
	snprintf(path, sizeof(path), "%s/%s", SERVER, FILE);
	if ((fd = open(path, O_RDWR|O_CREAT|O_TRUNC, 0777)) == -1)
		err(1, "open write '%s'", path);
	len = sizeof(struct tcpstat);
	memset(&stats, 0, len);
	if ((n = write(fd, &stats, len)) == -1)
		err(1, "write");
	if ((size_t)n != len)
		errx(1, "write not %zu: %zd", len, n);
	if (close(fd) == -1)
		err(1, "close read");

	/*
	 * Map file on NFS client and write sysctl net.inet.tcp.stats into it.
	 */
	snprintf(path, sizeof(path), "%s/%s", CLIENT, FILE);
	if ((fd = open(path, O_RDWR)) == -1)
		err(1, "open mmap '%s'", path);
	p = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		err(1, "mmap");
	if (sysctl(mib, miblen, p, &len, NULL, 0) == -1)
		err(1, "sysctl get stat");
	if (len != sizeof(struct tcpstat))
		errx(1, "len not %zu: %zu", sizeof(struct tcpstat), len);
	if (close(fd) == -1)
		err(1, "close mmap");

	/*
	 * Read file from NFS server.
	 */
	snprintf(path, sizeof(path), "%s/%s", SERVER, FILE);
	if ((fd = open(path, O_RDONLY)) == -1)
		err(1, "open read '%s'", path);
	if ((n = read(fd, &stats, len)) == -1)
		err(1, "read");
	if ((size_t)n != len)
		errx(1, "read not %zu: %zd", len, n);
	if (close(fd) == -1)
		err(1, "close read");

	return (0);
}
