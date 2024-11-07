// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#define __EXPORTED_HEADERS__

#include <linux/uio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#define __iovec_defined
#include <fcntl.h>
#include <malloc.h>
#include <error.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

#include <linux/memfd.h>
#include <linux/dma-buf.h>
#include <linux/udmabuf.h>
#include <libmnl/libmnl.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/netdev.h>
#include <time.h>
#include <net/if.h>

#include "netdev-user.h"
#include <ynl.h>

#define PAGE_SHIFT 12
#define TEST_PREFIX "ncdevmem"
#define NUM_PAGES 16000

#ifndef MSG_SOCK_DEVMEM
#define MSG_SOCK_DEVMEM 0x2000000
#endif

/*
 * tcpdevmem netcat. Works similarly to netcat but does device memory TCP
 * instead of regular TCP. Uses udmabuf to mock a dmabuf provider.
 *
 * Usage:
 *
 *	On server:
 *	ncdevmem -s <server IP> -c <client IP> -f eth1 -l -p 5201 -v 7
 *
 *	On client:
 *	yes $(echo -e \\x01\\x02\\x03\\x04\\x05\\x06) | \
 *		tr \\n \\0 | \
 *		head -c 5G | \
 *		nc <server IP> 5201 -p 5201
 *
 * Note this is compatible with regular netcat. i.e. the sender or receiver can
 * be replaced with regular netcat to test the RX or TX path in isolation.
 */

static char *server_ip = "192.168.1.4";
static char *client_ip;
static char *port = "5201";
static size_t do_validation;
static int start_queue = 8;
static int num_queues = 8;
static char *ifname = "eth1";
static unsigned int ifindex;
static unsigned int dmabuf_id;

struct memory_buffer {
	int fd;
	size_t size;

	int devfd;
	int memfd;
	char *buf_mem;
};

struct memory_provider {
	struct memory_buffer *(*alloc)(size_t size);
	void (*free)(struct memory_buffer *ctx);
	void (*memcpy_from_device)(void *dst, struct memory_buffer *src,
				   size_t off, int n);
};

static struct memory_buffer *udmabuf_alloc(size_t size)
{
	struct udmabuf_create create;
	struct memory_buffer *ctx;
	int ret;

	ctx = malloc(sizeof(*ctx));
	if (!ctx)
		error(1, ENOMEM, "malloc failed");

	ctx->size = size;

	ctx->devfd = open("/dev/udmabuf", O_RDWR);
	if (ctx->devfd < 0)
		error(1, errno,
		      "%s: [skip,no-udmabuf: Unable to access DMA buffer device file]\n",
		      TEST_PREFIX);

	ctx->memfd = memfd_create("udmabuf-test", MFD_ALLOW_SEALING);
	if (ctx->memfd < 0)
		error(1, errno, "%s: [skip,no-memfd]\n", TEST_PREFIX);

	ret = fcntl(ctx->memfd, F_ADD_SEALS, F_SEAL_SHRINK);
	if (ret < 0)
		error(1, errno, "%s: [skip,fcntl-add-seals]\n", TEST_PREFIX);

	ret = ftruncate(ctx->memfd, size);
	if (ret == -1)
		error(1, errno, "%s: [FAIL,memfd-truncate]\n", TEST_PREFIX);

	memset(&create, 0, sizeof(create));

	create.memfd = ctx->memfd;
	create.offset = 0;
	create.size = size;
	ctx->fd = ioctl(ctx->devfd, UDMABUF_CREATE, &create);
	if (ctx->fd < 0)
		error(1, errno, "%s: [FAIL, create udmabuf]\n", TEST_PREFIX);

	ctx->buf_mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
			    ctx->fd, 0);
	if (ctx->buf_mem == MAP_FAILED)
		error(1, errno, "%s: [FAIL, map udmabuf]\n", TEST_PREFIX);

	return ctx;
}

static void udmabuf_free(struct memory_buffer *ctx)
{
	munmap(ctx->buf_mem, ctx->size);
	close(ctx->fd);
	close(ctx->memfd);
	close(ctx->devfd);
	free(ctx);
}

static void udmabuf_memcpy_from_device(void *dst, struct memory_buffer *src,
				       size_t off, int n)
{
	struct dma_buf_sync sync = {};

	sync.flags = DMA_BUF_SYNC_START;
	ioctl(src->fd, DMA_BUF_IOCTL_SYNC, &sync);

	memcpy(dst, src->buf_mem + off, n);

	sync.flags = DMA_BUF_SYNC_END;
	ioctl(src->fd, DMA_BUF_IOCTL_SYNC, &sync);
}

static struct memory_provider udmabuf_memory_provider = {
	.alloc = udmabuf_alloc,
	.free = udmabuf_free,
	.memcpy_from_device = udmabuf_memcpy_from_device,
};

static struct memory_provider *provider = &udmabuf_memory_provider;

static void print_nonzero_bytes(void *ptr, size_t size)
{
	unsigned char *p = ptr;
	unsigned int i;

	for (i = 0; i < size; i++)
		putchar(p[i]);
}

void validate_buffer(void *line, size_t size)
{
	static unsigned char seed = 1;
	unsigned char *ptr = line;
	int errors = 0;
	size_t i;

	for (i = 0; i < size; i++) {
		if (ptr[i] != seed) {
			fprintf(stderr,
				"Failed validation: expected=%u, actual=%u, index=%lu\n",
				seed, ptr[i], i);
			errors++;
			if (errors > 20)
				error(1, 0, "validation failed.");
		}
		seed++;
		if (seed == do_validation)
			seed = 0;
	}

	fprintf(stdout, "Validated buffer\n");
}

#define run_command(cmd, ...)                                           \
	({                                                              \
		char command[256];                                      \
		memset(command, 0, sizeof(command));                    \
		snprintf(command, sizeof(command), cmd, ##__VA_ARGS__); \
		fprintf(stderr, "Running: %s\n", command);                       \
		system(command);                                        \
	})

static int reset_flow_steering(void)
{
	int ret = 0;

	ret = run_command("sudo ethtool -K %s ntuple off >&2", ifname);
	if (ret)
		return ret;

	return run_command("sudo ethtool -K %s ntuple on >&2", ifname);
}

static int configure_headersplit(bool on)
{
	return run_command("sudo ethtool -G %s tcp-data-split %s >&2", ifname,
			   on ? "on" : "off");
}

static int configure_rss(void)
{
	return run_command("sudo ethtool -X %s equal %d >&2", ifname, start_queue);
}

static int configure_channels(unsigned int rx, unsigned int tx)
{
	return run_command("sudo ethtool -L %s rx %u tx %u", ifname, rx, tx);
}

static int configure_flow_steering(void)
{
	return run_command("sudo ethtool -N %s flow-type tcp4 %s %s dst-ip %s %s %s dst-port %s queue %d >&2",
			   ifname,
			   client_ip ? "src-ip" : "",
			   client_ip ?: "",
			   server_ip,
			   client_ip ? "src-port" : "",
			   client_ip ? port : "",
			   port, start_queue);
}

static int bind_rx_queue(unsigned int ifindex, unsigned int dmabuf_fd,
			 struct netdev_queue_id *queues,
			 unsigned int n_queue_index, struct ynl_sock **ys)
{
	struct netdev_bind_rx_req *req = NULL;
	struct netdev_bind_rx_rsp *rsp = NULL;
	struct ynl_error yerr;

	*ys = ynl_sock_create(&ynl_netdev_family, &yerr);
	if (!*ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return -1;
	}

	req = netdev_bind_rx_req_alloc();
	netdev_bind_rx_req_set_ifindex(req, ifindex);
	netdev_bind_rx_req_set_fd(req, dmabuf_fd);
	__netdev_bind_rx_req_set_queues(req, queues, n_queue_index);

	rsp = netdev_bind_rx(*ys, req);
	if (!rsp) {
		perror("netdev_bind_rx");
		goto err_close;
	}

	if (!rsp->_present.id) {
		perror("id not present");
		goto err_close;
	}

	fprintf(stderr, "got dmabuf id=%d\n", rsp->id);
	dmabuf_id = rsp->id;

	netdev_bind_rx_req_free(req);
	netdev_bind_rx_rsp_free(rsp);

	return 0;

err_close:
	fprintf(stderr, "YNL failed: %s\n", (*ys)->err.msg);
	netdev_bind_rx_req_free(req);
	ynl_sock_destroy(*ys);
	return -1;
}

int do_server(struct memory_buffer *mem)
{
	char ctrl_data[sizeof(int) * 20000];
	struct netdev_queue_id *queues;
	size_t non_page_aligned_frags = 0;
	struct sockaddr_in client_addr;
	struct sockaddr_in server_sin;
	size_t page_aligned_frags = 0;
	size_t total_received = 0;
	socklen_t client_addr_len;
	bool is_devmem = false;
	char *tmp_mem = NULL;
	struct ynl_sock *ys;
	char iobuf[819200];
	char buffer[256];
	int socket_fd;
	int client_fd;
	size_t i = 0;
	int opt = 1;
	int ret;

	if (reset_flow_steering())
		error(1, 0, "Failed to reset flow steering\n");

	/* Configure RSS to divert all traffic from our devmem queues */
	if (configure_rss())
		error(1, 0, "Failed to configure rss\n");

	/* Flow steer our devmem flows to start_queue */
	if (configure_flow_steering())
		error(1, 0, "Failed to configure flow steering\n");

	sleep(1);

	queues = malloc(sizeof(*queues) * num_queues);

	for (i = 0; i < num_queues; i++) {
		queues[i]._present.type = 1;
		queues[i]._present.id = 1;
		queues[i].type = NETDEV_QUEUE_TYPE_RX;
		queues[i].id = start_queue + i;
	}

	if (bind_rx_queue(ifindex, mem->fd, queues, num_queues, &ys))
		error(1, 0, "Failed to bind\n");

	tmp_mem = malloc(mem->size);
	if (!tmp_mem)
		error(1, ENOMEM, "malloc failed");

	server_sin.sin_family = AF_INET;
	server_sin.sin_port = htons(atoi(port));

	ret = inet_pton(server_sin.sin_family, server_ip, &server_sin.sin_addr);
	if (ret < 0)
		error(1, errno, "%s: [FAIL, create socket]\n", TEST_PREFIX);

	socket_fd = socket(server_sin.sin_family, SOCK_STREAM, 0);
	if (socket_fd < 0)
		error(1, errno, "%s: [FAIL, create socket]\n", TEST_PREFIX);

	ret = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &opt,
			 sizeof(opt));
	if (ret)
		error(1, errno, "%s: [FAIL, set sock opt]\n", TEST_PREFIX);

	ret = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
			 sizeof(opt));
	if (ret)
		error(1, errno, "%s: [FAIL, set sock opt]\n", TEST_PREFIX);

	fprintf(stderr, "binding to address %s:%d\n", server_ip,
		ntohs(server_sin.sin_port));

	ret = bind(socket_fd, &server_sin, sizeof(server_sin));
	if (ret)
		error(1, errno, "%s: [FAIL, bind]\n", TEST_PREFIX);

	ret = listen(socket_fd, 1);
	if (ret)
		error(1, errno, "%s: [FAIL, listen]\n", TEST_PREFIX);

	client_addr_len = sizeof(client_addr);

	inet_ntop(server_sin.sin_family, &server_sin.sin_addr, buffer,
		  sizeof(buffer));
	fprintf(stderr, "Waiting or connection on %s:%d\n", buffer,
		ntohs(server_sin.sin_port));
	client_fd = accept(socket_fd, &client_addr, &client_addr_len);

	inet_ntop(client_addr.sin_family, &client_addr.sin_addr, buffer,
		  sizeof(buffer));
	fprintf(stderr, "Got connection from %s:%d\n", buffer,
		ntohs(client_addr.sin_port));

	while (1) {
		struct iovec iov = { .iov_base = iobuf,
				     .iov_len = sizeof(iobuf) };
		struct dmabuf_cmsg *dmabuf_cmsg = NULL;
		struct cmsghdr *cm = NULL;
		struct msghdr msg = { 0 };
		struct dmabuf_token token;
		ssize_t ret;

		is_devmem = false;

		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = ctrl_data;
		msg.msg_controllen = sizeof(ctrl_data);
		ret = recvmsg(client_fd, &msg, MSG_SOCK_DEVMEM);
		fprintf(stderr, "recvmsg ret=%ld\n", ret);
		if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			continue;
		if (ret < 0) {
			perror("recvmsg");
			continue;
		}
		if (ret == 0) {
			fprintf(stderr, "client exited\n");
			goto cleanup;
		}

		i++;
		for (cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
			if (cm->cmsg_level != SOL_SOCKET ||
			    (cm->cmsg_type != SCM_DEVMEM_DMABUF &&
			     cm->cmsg_type != SCM_DEVMEM_LINEAR)) {
				fprintf(stderr, "skipping non-devmem cmsg\n");
				continue;
			}

			dmabuf_cmsg = (struct dmabuf_cmsg *)CMSG_DATA(cm);
			is_devmem = true;

			if (cm->cmsg_type == SCM_DEVMEM_LINEAR) {
				/* TODO: process data copied from skb's linear
				 * buffer.
				 */
				fprintf(stderr,
					"SCM_DEVMEM_LINEAR. dmabuf_cmsg->frag_size=%u\n",
					dmabuf_cmsg->frag_size);

				continue;
			}

			token.token_start = dmabuf_cmsg->frag_token;
			token.token_count = 1;

			total_received += dmabuf_cmsg->frag_size;
			fprintf(stderr,
				"received frag_page=%llu, in_page_offset=%llu, frag_offset=%llu, frag_size=%u, token=%u, total_received=%lu, dmabuf_id=%u\n",
				dmabuf_cmsg->frag_offset >> PAGE_SHIFT,
				dmabuf_cmsg->frag_offset % getpagesize(),
				dmabuf_cmsg->frag_offset,
				dmabuf_cmsg->frag_size, dmabuf_cmsg->frag_token,
				total_received, dmabuf_cmsg->dmabuf_id);

			if (dmabuf_cmsg->dmabuf_id != dmabuf_id)
				error(1, 0,
				      "received on wrong dmabuf_id: flow steering error\n");

			if (dmabuf_cmsg->frag_size % getpagesize())
				non_page_aligned_frags++;
			else
				page_aligned_frags++;

			provider->memcpy_from_device(tmp_mem, mem,
						     dmabuf_cmsg->frag_offset,
						     dmabuf_cmsg->frag_size);

			if (do_validation)
				validate_buffer(tmp_mem,
						dmabuf_cmsg->frag_size);
			else
				print_nonzero_bytes(tmp_mem,
						    dmabuf_cmsg->frag_size);

			ret = setsockopt(client_fd, SOL_SOCKET,
					 SO_DEVMEM_DONTNEED, &token,
					 sizeof(token));
			if (ret != 1)
				error(1, 0,
				      "SO_DEVMEM_DONTNEED not enough tokens");
		}
		if (!is_devmem)
			error(1, 0, "flow steering error\n");

		fprintf(stderr, "total_received=%lu\n", total_received);
	}

	fprintf(stderr, "%s: ok\n", TEST_PREFIX);

	fprintf(stderr, "page_aligned_frags=%lu, non_page_aligned_frags=%lu\n",
		page_aligned_frags, non_page_aligned_frags);

	fprintf(stderr, "page_aligned_frags=%lu, non_page_aligned_frags=%lu\n",
		page_aligned_frags, non_page_aligned_frags);

cleanup:

	free(tmp_mem);
	close(client_fd);
	close(socket_fd);
	ynl_sock_destroy(ys);

	return 0;
}

void run_devmem_tests(void)
{
	struct netdev_queue_id *queues;
	struct memory_buffer *mem;
	struct ynl_sock *ys;
	size_t i = 0;

	mem = provider->alloc(getpagesize() * NUM_PAGES);

	/* Configure RSS to divert all traffic from our devmem queues */
	if (configure_rss())
		error(1, 0, "rss error\n");

	queues = calloc(num_queues, sizeof(*queues));

	if (configure_headersplit(1))
		error(1, 0, "Failed to configure header split\n");

	if (!bind_rx_queue(ifindex, mem->fd, queues, num_queues, &ys))
		error(1, 0, "Binding empty queues array should have failed\n");

	for (i = 0; i < num_queues; i++) {
		queues[i]._present.type = 1;
		queues[i]._present.id = 1;
		queues[i].type = NETDEV_QUEUE_TYPE_RX;
		queues[i].id = start_queue + i;
	}

	if (configure_headersplit(0))
		error(1, 0, "Failed to configure header split\n");

	if (!bind_rx_queue(ifindex, mem->fd, queues, num_queues, &ys))
		error(1, 0, "Configure dmabuf with header split off should have failed\n");

	if (configure_headersplit(1))
		error(1, 0, "Failed to configure header split\n");

	for (i = 0; i < num_queues; i++) {
		queues[i]._present.type = 1;
		queues[i]._present.id = 1;
		queues[i].type = NETDEV_QUEUE_TYPE_RX;
		queues[i].id = start_queue + i;
	}

	if (bind_rx_queue(ifindex, mem->fd, queues, num_queues, &ys))
		error(1, 0, "Failed to bind\n");

	/* Deactivating a bound queue should not be legal */
	if (!configure_channels(num_queues, num_queues - 1))
		error(1, 0, "Deactivating a bound queue should be illegal.\n");

	/* Closing the netlink socket does an implicit unbind */
	ynl_sock_destroy(ys);

	provider->free(mem);
}

int main(int argc, char *argv[])
{
	struct memory_buffer *mem;
	int is_server = 0, opt;
	int ret;

	while ((opt = getopt(argc, argv, "ls:c:p:v:q:t:f:")) != -1) {
		switch (opt) {
		case 'l':
			is_server = 1;
			break;
		case 's':
			server_ip = optarg;
			break;
		case 'c':
			client_ip = optarg;
			break;
		case 'p':
			port = optarg;
			break;
		case 'v':
			do_validation = atoll(optarg);
			break;
		case 'q':
			num_queues = atoi(optarg);
			break;
		case 't':
			start_queue = atoi(optarg);
			break;
		case 'f':
			ifname = optarg;
			break;
		case '?':
			fprintf(stderr, "unknown option: %c\n", optopt);
			break;
		}
	}

	ifindex = if_nametoindex(ifname);

	for (; optind < argc; optind++)
		fprintf(stderr, "extra arguments: %s\n", argv[optind]);

	run_devmem_tests();

	mem = provider->alloc(getpagesize() * NUM_PAGES);
	ret = is_server ? do_server(mem) : 1;
	provider->free(mem);

	return ret;
}
