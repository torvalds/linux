// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <linux/if_xdp.h>
#include <linux/if_link.h>
#include <net/if.h>
#include <inttypes.h>

#include "ksft.h"

#define UMEM_SZ (1U << 16)
#define NUM_DESC (UMEM_SZ / 2048)


static void print_usage(const char *bin)
{
	fprintf(stderr, "Usage: %s ifindex queue_id [-z]\n\n"
		"where:\n\t-z: force zerocopy mode", bin);
}

/* this is a simple helper program that creates an XDP socket and does the
 * minimum necessary to get bind() to succeed.
 *
 * this test program is not intended to actually process packets, but could be
 * extended in the future if that is actually needed.
 *
 * it is used by queues.py to ensure the xsk netlinux attribute is set
 * correctly.
 */
int main(int argc, char **argv)
{
	struct xdp_umem_reg umem_reg = { 0 };
	struct sockaddr_xdp sxdp = { 0 };
	int num_desc = NUM_DESC;
	void *umem_area;
	int retry = 0;
	int ifindex;
	int sock_fd;
	int queue;

	if (argc != 3 && argc != 4) {
		print_usage(argv[0]);
		return 1;
	}

	sock_fd = socket(AF_XDP, SOCK_RAW, 0);
	if (sock_fd < 0) {
		perror("socket creation failed");
		/* if the kernel doesn't support AF_XDP, let the test program
		 * know with -1. All other error paths return 1.
		 */
		if (errno == EAFNOSUPPORT)
			return -1;
		return 1;
	}

	/* "Probing mode", just checking if AF_XDP sockets are supported */
	if (!strcmp(argv[1], "-") && !strcmp(argv[2], "-")) {
		printf("AF_XDP support detected\n");
		close(sock_fd);
		return 0;
	}

	ifindex = atoi(argv[1]);
	queue = atoi(argv[2]);

	umem_area = mmap(NULL, UMEM_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE |
			MAP_ANONYMOUS, -1, 0);
	if (umem_area == MAP_FAILED) {
		perror("mmap failed");
		return 1;
	}

	umem_reg.addr = (uintptr_t)umem_area;
	umem_reg.len = UMEM_SZ;
	umem_reg.chunk_size = 2048;
	umem_reg.headroom = 0;

	setsockopt(sock_fd, SOL_XDP, XDP_UMEM_REG, &umem_reg,
		   sizeof(umem_reg));
	setsockopt(sock_fd, SOL_XDP, XDP_UMEM_FILL_RING, &num_desc,
		   sizeof(num_desc));
	setsockopt(sock_fd, SOL_XDP, XDP_UMEM_COMPLETION_RING, &num_desc,
		   sizeof(num_desc));
	setsockopt(sock_fd, SOL_XDP, XDP_RX_RING, &num_desc, sizeof(num_desc));

	sxdp.sxdp_family = AF_XDP;
	sxdp.sxdp_ifindex = ifindex;
	sxdp.sxdp_queue_id = queue;
	sxdp.sxdp_flags = 0;

	if (argc > 3) {
		if (!strcmp(argv[3], "-z")) {
			sxdp.sxdp_flags = XDP_ZEROCOPY;
		} else {
			print_usage(argv[0]);
			return 1;
		}
	}

	while (1) {
		if (bind(sock_fd, (struct sockaddr *)&sxdp, sizeof(sxdp)) == 0)
			break;

		if (errno == EBUSY && retry < 3) {
			retry++;
			sleep(1);
			continue;
		} else {
			perror("bind failed");
			munmap(umem_area, UMEM_SZ);
			close(sock_fd);
			return 1;
		}
	}

	ksft_ready();
	ksft_wait();

	/* parent program will write a byte to stdin when its ready for this
	 * helper to exit
	 */

	close(sock_fd);
	return 0;
}
