// SPDX-License-Identifier: GPL-2.0

/* Reference program for verifying XDP metadata on real HW. Functional test
 * only, doesn't test the performance.
 *
 * RX:
 * - UDP 9091 packets are diverted into AF_XDP
 * - Metadata verified:
 *   - rx_timestamp
 *   - rx_hash
 *
 * TX:
 * - TBD
 */

#include <test_progs.h>
#include <network_helpers.h>
#include "xdp_hw_metadata.skel.h"
#include "xsk.h"

#include <error.h>
#include <linux/errqueue.h>
#include <linux/if_link.h>
#include <linux/net_tstamp.h>
#include <linux/udp.h>
#include <linux/sockios.h>
#include <sys/mman.h>
#include <net/if.h>
#include <poll.h>
#include <time.h>

#include "xdp_metadata.h"

#define UMEM_NUM 16
#define UMEM_FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE
#define UMEM_SIZE (UMEM_FRAME_SIZE * UMEM_NUM)
#define XDP_FLAGS (XDP_FLAGS_DRV_MODE | XDP_FLAGS_REPLACE)

struct xsk {
	void *umem_area;
	struct xsk_umem *umem;
	struct xsk_ring_prod fill;
	struct xsk_ring_cons comp;
	struct xsk_ring_prod tx;
	struct xsk_ring_cons rx;
	struct xsk_socket *socket;
};

struct xdp_hw_metadata *bpf_obj;
struct xsk *rx_xsk;
const char *ifname;
int ifindex;
int rxq;

void test__fail(void) { /* for network_helpers.c */ }

static int open_xsk(int ifindex, struct xsk *xsk, __u32 queue_id)
{
	int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
	const struct xsk_socket_config socket_config = {
		.rx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
		.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
		.bind_flags = XDP_COPY,
	};
	const struct xsk_umem_config umem_config = {
		.fill_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
		.comp_size = XSK_RING_CONS__DEFAULT_NUM_DESCS,
		.frame_size = XSK_UMEM__DEFAULT_FRAME_SIZE,
		.flags = XDP_UMEM_UNALIGNED_CHUNK_FLAG,
	};
	__u32 idx;
	u64 addr;
	int ret;
	int i;

	xsk->umem_area = mmap(NULL, UMEM_SIZE, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
	if (xsk->umem_area == MAP_FAILED)
		return -ENOMEM;

	ret = xsk_umem__create(&xsk->umem,
			       xsk->umem_area, UMEM_SIZE,
			       &xsk->fill,
			       &xsk->comp,
			       &umem_config);
	if (ret)
		return ret;

	ret = xsk_socket__create(&xsk->socket, ifindex, queue_id,
				 xsk->umem,
				 &xsk->rx,
				 &xsk->tx,
				 &socket_config);
	if (ret)
		return ret;

	/* First half of umem is for TX. This way address matches 1-to-1
	 * to the completion queue index.
	 */

	for (i = 0; i < UMEM_NUM / 2; i++) {
		addr = i * UMEM_FRAME_SIZE;
		printf("%p: tx_desc[%d] -> %lx\n", xsk, i, addr);
	}

	/* Second half of umem is for RX. */

	ret = xsk_ring_prod__reserve(&xsk->fill, UMEM_NUM / 2, &idx);
	for (i = 0; i < UMEM_NUM / 2; i++) {
		addr = (UMEM_NUM / 2 + i) * UMEM_FRAME_SIZE;
		printf("%p: rx_desc[%d] -> %lx\n", xsk, i, addr);
		*xsk_ring_prod__fill_addr(&xsk->fill, i) = addr;
	}
	xsk_ring_prod__submit(&xsk->fill, ret);

	return 0;
}

static void close_xsk(struct xsk *xsk)
{
	if (xsk->umem)
		xsk_umem__delete(xsk->umem);
	if (xsk->socket)
		xsk_socket__delete(xsk->socket);
	munmap(xsk->umem_area, UMEM_SIZE);
}

static void refill_rx(struct xsk *xsk, __u64 addr)
{
	__u32 idx;

	if (xsk_ring_prod__reserve(&xsk->fill, 1, &idx) == 1) {
		printf("%p: complete idx=%u addr=%llx\n", xsk, idx, addr);
		*xsk_ring_prod__fill_addr(&xsk->fill, idx) = addr;
		xsk_ring_prod__submit(&xsk->fill, 1);
	}
}

#define NANOSEC_PER_SEC 1000000000 /* 10^9 */
static __u64 gettime(clockid_t clock_id)
{
	struct timespec t;
	int res;

	/* See man clock_gettime(2) for type of clock_id's */
	res = clock_gettime(clock_id, &t);

	if (res < 0)
		error(res, errno, "Error with clock_gettime()");

	return (__u64) t.tv_sec * NANOSEC_PER_SEC + t.tv_nsec;
}

static void verify_xdp_metadata(void *data, clockid_t clock_id)
{
	struct xdp_meta *meta;

	meta = data - sizeof(*meta);

	if (meta->rx_hash_err < 0)
		printf("No rx_hash err=%d\n", meta->rx_hash_err);
	else
		printf("rx_hash: 0x%X with RSS type:0x%X\n",
		       meta->rx_hash, meta->rx_hash_type);

	printf("rx_timestamp:  %llu (sec:%0.4f)\n", meta->rx_timestamp,
	       (double)meta->rx_timestamp / NANOSEC_PER_SEC);
	if (meta->rx_timestamp) {
		__u64 usr_clock = gettime(clock_id);
		__u64 xdp_clock = meta->xdp_timestamp;
		__s64 delta_X = xdp_clock - meta->rx_timestamp;
		__s64 delta_X2U = usr_clock - xdp_clock;

		printf("XDP RX-time:   %llu (sec:%0.4f) delta sec:%0.4f (%0.3f usec)\n",
		       xdp_clock, (double)xdp_clock / NANOSEC_PER_SEC,
		       (double)delta_X / NANOSEC_PER_SEC,
		       (double)delta_X / 1000);

		printf("AF_XDP time:   %llu (sec:%0.4f) delta sec:%0.4f (%0.3f usec)\n",
		       usr_clock, (double)usr_clock / NANOSEC_PER_SEC,
		       (double)delta_X2U / NANOSEC_PER_SEC,
		       (double)delta_X2U / 1000);
	}

}

static void verify_skb_metadata(int fd)
{
	char cmsg_buf[1024];
	char packet_buf[128];

	struct scm_timestamping *ts;
	struct iovec packet_iov;
	struct cmsghdr *cmsg;
	struct msghdr hdr;

	memset(&hdr, 0, sizeof(hdr));
	hdr.msg_iov = &packet_iov;
	hdr.msg_iovlen = 1;
	packet_iov.iov_base = packet_buf;
	packet_iov.iov_len = sizeof(packet_buf);

	hdr.msg_control = cmsg_buf;
	hdr.msg_controllen = sizeof(cmsg_buf);

	if (recvmsg(fd, &hdr, 0) < 0)
		error(1, errno, "recvmsg");

	for (cmsg = CMSG_FIRSTHDR(&hdr); cmsg != NULL;
	     cmsg = CMSG_NXTHDR(&hdr, cmsg)) {

		if (cmsg->cmsg_level != SOL_SOCKET)
			continue;

		switch (cmsg->cmsg_type) {
		case SCM_TIMESTAMPING:
			ts = (struct scm_timestamping *)CMSG_DATA(cmsg);
			if (ts->ts[2].tv_sec || ts->ts[2].tv_nsec) {
				printf("found skb hwtstamp = %lu.%lu\n",
				       ts->ts[2].tv_sec, ts->ts[2].tv_nsec);
				return;
			}
			break;
		default:
			break;
		}
	}

	printf("skb hwtstamp is not found!\n");
}

static int verify_metadata(struct xsk *rx_xsk, int rxq, int server_fd, clockid_t clock_id)
{
	const struct xdp_desc *rx_desc;
	struct pollfd fds[rxq + 1];
	__u64 comp_addr;
	__u64 addr;
	__u32 idx;
	int ret;
	int i;

	for (i = 0; i < rxq; i++) {
		fds[i].fd = xsk_socket__fd(rx_xsk[i].socket);
		fds[i].events = POLLIN;
		fds[i].revents = 0;
	}

	fds[rxq].fd = server_fd;
	fds[rxq].events = POLLIN;
	fds[rxq].revents = 0;

	while (true) {
		errno = 0;
		ret = poll(fds, rxq + 1, 1000);
		printf("poll: %d (%d) skip=%llu fail=%llu redir=%llu\n",
		       ret, errno, bpf_obj->bss->pkts_skip,
		       bpf_obj->bss->pkts_fail, bpf_obj->bss->pkts_redir);
		if (ret < 0)
			break;
		if (ret == 0)
			continue;

		if (fds[rxq].revents)
			verify_skb_metadata(server_fd);

		for (i = 0; i < rxq; i++) {
			if (fds[i].revents == 0)
				continue;

			struct xsk *xsk = &rx_xsk[i];

			ret = xsk_ring_cons__peek(&xsk->rx, 1, &idx);
			printf("xsk_ring_cons__peek: %d\n", ret);
			if (ret != 1)
				continue;

			rx_desc = xsk_ring_cons__rx_desc(&xsk->rx, idx);
			comp_addr = xsk_umem__extract_addr(rx_desc->addr);
			addr = xsk_umem__add_offset_to_addr(rx_desc->addr);
			printf("%p: rx_desc[%u]->addr=%llx addr=%llx comp_addr=%llx\n",
			       xsk, idx, rx_desc->addr, addr, comp_addr);
			verify_xdp_metadata(xsk_umem__get_data(xsk->umem_area, addr),
					    clock_id);
			xsk_ring_cons__release(&xsk->rx, 1);
			refill_rx(xsk, comp_addr);
		}
	}

	return 0;
}

struct ethtool_channels {
	__u32	cmd;
	__u32	max_rx;
	__u32	max_tx;
	__u32	max_other;
	__u32	max_combined;
	__u32	rx_count;
	__u32	tx_count;
	__u32	other_count;
	__u32	combined_count;
};

#define ETHTOOL_GCHANNELS	0x0000003c /* Get no of channels */

static int rxq_num(const char *ifname)
{
	struct ethtool_channels ch = {
		.cmd = ETHTOOL_GCHANNELS,
	};

	struct ifreq ifr = {
		.ifr_data = (void *)&ch,
	};
	strncpy(ifr.ifr_name, ifname, IF_NAMESIZE - 1);
	int fd, ret;

	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0)
		error(1, errno, "socket");

	ret = ioctl(fd, SIOCETHTOOL, &ifr);
	if (ret < 0)
		error(1, errno, "ioctl(SIOCETHTOOL)");

	close(fd);

	return ch.rx_count + ch.combined_count;
}

static void hwtstamp_ioctl(int op, const char *ifname, struct hwtstamp_config *cfg)
{
	struct ifreq ifr = {
		.ifr_data = (void *)cfg,
	};
	strncpy(ifr.ifr_name, ifname, IF_NAMESIZE - 1);
	int fd, ret;

	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0)
		error(1, errno, "socket");

	ret = ioctl(fd, op, &ifr);
	if (ret < 0)
		error(1, errno, "ioctl(%d)", op);

	close(fd);
}

static struct hwtstamp_config saved_hwtstamp_cfg;
static const char *saved_hwtstamp_ifname;

static void hwtstamp_restore(void)
{
	hwtstamp_ioctl(SIOCSHWTSTAMP, saved_hwtstamp_ifname, &saved_hwtstamp_cfg);
}

static void hwtstamp_enable(const char *ifname)
{
	struct hwtstamp_config cfg = {
		.rx_filter = HWTSTAMP_FILTER_ALL,
	};

	hwtstamp_ioctl(SIOCGHWTSTAMP, ifname, &saved_hwtstamp_cfg);
	saved_hwtstamp_ifname = strdup(ifname);
	atexit(hwtstamp_restore);

	hwtstamp_ioctl(SIOCSHWTSTAMP, ifname, &cfg);
}

static void cleanup(void)
{
	LIBBPF_OPTS(bpf_xdp_attach_opts, opts);
	int ret;
	int i;

	if (bpf_obj) {
		opts.old_prog_fd = bpf_program__fd(bpf_obj->progs.rx);
		if (opts.old_prog_fd >= 0) {
			printf("detaching bpf program....\n");
			ret = bpf_xdp_detach(ifindex, XDP_FLAGS, &opts);
			if (ret)
				printf("failed to detach XDP program: %d\n", ret);
		}
	}

	for (i = 0; i < rxq; i++)
		close_xsk(&rx_xsk[i]);

	if (bpf_obj)
		xdp_hw_metadata__destroy(bpf_obj);
}

static void handle_signal(int sig)
{
	/* interrupting poll() is all we need */
}

static void timestamping_enable(int fd, int val)
{
	int ret;

	ret = setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &val, sizeof(val));
	if (ret < 0)
		error(1, errno, "setsockopt(SO_TIMESTAMPING)");
}

int main(int argc, char *argv[])
{
	clockid_t clock_id = CLOCK_TAI;
	int server_fd = -1;
	int ret;
	int i;

	struct bpf_program *prog;

	if (argc != 2) {
		fprintf(stderr, "pass device name\n");
		return -1;
	}

	ifname = argv[1];
	ifindex = if_nametoindex(ifname);
	rxq = rxq_num(ifname);

	printf("rxq: %d\n", rxq);

	hwtstamp_enable(ifname);

	rx_xsk = malloc(sizeof(struct xsk) * rxq);
	if (!rx_xsk)
		error(1, ENOMEM, "malloc");

	for (i = 0; i < rxq; i++) {
		printf("open_xsk(%s, %p, %d)\n", ifname, &rx_xsk[i], i);
		ret = open_xsk(ifindex, &rx_xsk[i], i);
		if (ret)
			error(1, -ret, "open_xsk");

		printf("xsk_socket__fd() -> %d\n", xsk_socket__fd(rx_xsk[i].socket));
	}

	printf("open bpf program...\n");
	bpf_obj = xdp_hw_metadata__open();
	if (libbpf_get_error(bpf_obj))
		error(1, libbpf_get_error(bpf_obj), "xdp_hw_metadata__open");

	prog = bpf_object__find_program_by_name(bpf_obj->obj, "rx");
	bpf_program__set_ifindex(prog, ifindex);
	bpf_program__set_flags(prog, BPF_F_XDP_DEV_BOUND_ONLY);

	printf("load bpf program...\n");
	ret = xdp_hw_metadata__load(bpf_obj);
	if (ret)
		error(1, -ret, "xdp_hw_metadata__load");

	printf("prepare skb endpoint...\n");
	server_fd = start_server(AF_INET6, SOCK_DGRAM, NULL, 9092, 1000);
	if (server_fd < 0)
		error(1, errno, "start_server");
	timestamping_enable(server_fd,
			    SOF_TIMESTAMPING_SOFTWARE |
			    SOF_TIMESTAMPING_RAW_HARDWARE);

	printf("prepare xsk map...\n");
	for (i = 0; i < rxq; i++) {
		int sock_fd = xsk_socket__fd(rx_xsk[i].socket);
		__u32 queue_id = i;

		printf("map[%d] = %d\n", queue_id, sock_fd);
		ret = bpf_map_update_elem(bpf_map__fd(bpf_obj->maps.xsk), &queue_id, &sock_fd, 0);
		if (ret)
			error(1, -ret, "bpf_map_update_elem");
	}

	printf("attach bpf program...\n");
	ret = bpf_xdp_attach(ifindex,
			     bpf_program__fd(bpf_obj->progs.rx),
			     XDP_FLAGS, NULL);
	if (ret)
		error(1, -ret, "bpf_xdp_attach");

	signal(SIGINT, handle_signal);
	ret = verify_metadata(rx_xsk, rxq, server_fd, clock_id);
	close(server_fd);
	cleanup();
	if (ret)
		error(1, -ret, "verify_metadata");
}
