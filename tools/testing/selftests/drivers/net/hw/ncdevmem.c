// SPDX-License-Identifier: GPL-2.0
/*
 * tcpdevmem netcat. Works similarly to netcat but does device memory TCP
 * instead of regular TCP. Uses udmabuf to mock a dmabuf provider.
 *
 * Usage:
 *
 *     On server:
 *     ncdevmem -s <server IP> [-c <client IP>] -f eth1 -l -p 5201
 *
 *     On client:
 *     echo -n "hello\nworld" | \
 *		ncdevmem -s <server IP> [-c <client IP>] -p 5201 -f eth1
 *
 * Note this is compatible with regular netcat. i.e. the sender or receiver can
 * be replaced with regular netcat to test the RX or TX path in isolation.
 *
 * Test data validation (devmem TCP on RX only):
 *
 *     On server:
 *     ncdevmem -s <server IP> [-c <client IP>] -f eth1 -l -p 5201 -v 7
 *
 *     On client:
 *     yes $(echo -e \\x01\\x02\\x03\\x04\\x05\\x06) | \
 *             head -c 1G | \
 *             nc <server IP> 5201 -p 5201
 *
 * Test data validation (devmem TCP on RX and TX, validation happens on RX):
 *
 *	On server:
 *	ncdevmem -s <server IP> [-c <client IP>] -l -p 5201 -v 8 -f eth1
 *
 *	On client:
 *	yes $(echo -e \\x01\\x02\\x03\\x04\\x05\\x06\\x07) | \
 *		head -c 1M | \
 *		ncdevmem -s <server IP> [-c <client IP>] -p 5201 -f eth1
 */
#define _GNU_SOURCE
#define __EXPORTED_HEADERS__

#include <linux/uio.h>
#include <stdarg.h>
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
#include <poll.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/time.h>

#include <linux/memfd.h>
#include <linux/dma-buf.h>
#include <linux/errqueue.h>
#include <linux/udmabuf.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/netdev.h>
#include <linux/ethtool_netlink.h>
#include <time.h>
#include <net/if.h>

#include "netdev-user.h"
#include "ethtool-user.h"
#include <ynl.h>

#define PAGE_SHIFT 12
#define TEST_PREFIX "ncdevmem"
#define NUM_PAGES 16000

#ifndef MSG_SOCK_DEVMEM
#define MSG_SOCK_DEVMEM 0x2000000
#endif

#define MAX_IOV 1024

static size_t max_chunk;
static char *server_ip;
static char *client_ip;
static char *port;
static size_t do_validation;
static int start_queue = -1;
static int num_queues = -1;
static char *ifname;
static unsigned int ifindex;
static unsigned int dmabuf_id;
static uint32_t tx_dmabuf_id;
static int waittime_ms = 500;

/* System state loaded by current_config_load() */
#define MAX_FLOWS	8
static int ntuple_ids[MAX_FLOWS] = { -1, -1, -1, -1, -1, -1, -1, -1, };

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
	void (*memcpy_to_device)(struct memory_buffer *dst, size_t off,
				 void *src, int n);
	void (*memcpy_from_device)(void *dst, struct memory_buffer *src,
				   size_t off, int n);
};

static void pr_err(const char *fmt, ...)
{
	va_list args;

	fprintf(stderr, "%s: ", TEST_PREFIX);

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	if (errno != 0)
		fprintf(stderr, ": %s", strerror(errno));
	fprintf(stderr, "\n");
}

static struct memory_buffer *udmabuf_alloc(size_t size)
{
	struct udmabuf_create create;
	struct memory_buffer *ctx;
	int ret;

	ctx = malloc(sizeof(*ctx));
	if (!ctx)
		return NULL;

	ctx->size = size;

	ctx->devfd = open("/dev/udmabuf", O_RDWR);
	if (ctx->devfd < 0) {
		pr_err("[skip,no-udmabuf: Unable to access DMA buffer device file]");
		goto err_free_ctx;
	}

	ctx->memfd = memfd_create("udmabuf-test", MFD_ALLOW_SEALING);
	if (ctx->memfd < 0) {
		pr_err("[skip,no-memfd]");
		goto err_close_dev;
	}

	ret = fcntl(ctx->memfd, F_ADD_SEALS, F_SEAL_SHRINK);
	if (ret < 0) {
		pr_err("[skip,fcntl-add-seals]");
		goto err_close_memfd;
	}

	ret = ftruncate(ctx->memfd, size);
	if (ret == -1) {
		pr_err("[FAIL,memfd-truncate]");
		goto err_close_memfd;
	}

	memset(&create, 0, sizeof(create));

	create.memfd = ctx->memfd;
	create.offset = 0;
	create.size = size;
	ctx->fd = ioctl(ctx->devfd, UDMABUF_CREATE, &create);
	if (ctx->fd < 0) {
		pr_err("[FAIL, create udmabuf]");
		goto err_close_fd;
	}

	ctx->buf_mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
			    ctx->fd, 0);
	if (ctx->buf_mem == MAP_FAILED) {
		pr_err("[FAIL, map udmabuf]");
		goto err_close_fd;
	}

	return ctx;

err_close_fd:
	close(ctx->fd);
err_close_memfd:
	close(ctx->memfd);
err_close_dev:
	close(ctx->devfd);
err_free_ctx:
	free(ctx);
	return NULL;
}

static void udmabuf_free(struct memory_buffer *ctx)
{
	munmap(ctx->buf_mem, ctx->size);
	close(ctx->fd);
	close(ctx->memfd);
	close(ctx->devfd);
	free(ctx);
}

static void udmabuf_memcpy_to_device(struct memory_buffer *dst, size_t off,
				     void *src, int n)
{
	struct dma_buf_sync sync = {};

	sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE;
	ioctl(dst->fd, DMA_BUF_IOCTL_SYNC, &sync);

	memcpy(dst->buf_mem + off, src, n);

	sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
	ioctl(dst->fd, DMA_BUF_IOCTL_SYNC, &sync);
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
	.memcpy_to_device = udmabuf_memcpy_to_device,
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

int validate_buffer(void *line, size_t size)
{
	static unsigned char seed = 1;
	unsigned char *ptr = line;
	unsigned char expected;
	static int errors;
	size_t i;

	for (i = 0; i < size; i++) {
		expected = seed ? seed : '\n';
		if (ptr[i] != expected) {
			fprintf(stderr,
				"Failed validation: expected=%u, actual=%u, index=%lu\n",
				expected, ptr[i], i);
			errors++;
			if (errors > 20) {
				pr_err("validation failed");
				return -1;
			}
		}
		seed++;
		if (seed == do_validation)
			seed = 0;
	}

	fprintf(stdout, "Validated buffer\n");
	return 0;
}

static int
__run_command(char *out, size_t outlen, const char *cmd, va_list args)
{
	char command[256];
	FILE *fp;

	vsnprintf(command, sizeof(command), cmd, args);

	fprintf(stderr, "Running: %s\n", command);
	fp = popen(command, "r");
	if (!fp)
		return -1;
	if (out) {
		size_t len;

		if (!fgets(out, outlen, fp))
			return -1;

		/* Remove trailing newline if present */
		len = strlen(out);
		if (len && out[len - 1] == '\n')
			out[len - 1] = '\0';
	}
	return pclose(fp);
}

static int run_command(const char *cmd, ...)
{
	va_list args;
	int ret;

	va_start(args, cmd);
	ret = __run_command(NULL, 0, cmd, args);
	va_end(args);

	return ret;
}

static int ethtool_add_flow(const char *format, ...)
{
	char local_output[256], cmd[256];
	const char *id_start;
	int flow_idx, ret;
	char *endptr;
	long flow_id;
	va_list args;

	for (flow_idx = 0; flow_idx < MAX_FLOWS; flow_idx++)
		if (ntuple_ids[flow_idx] == -1)
			break;
	if (flow_idx == MAX_FLOWS) {
		fprintf(stderr, "Error: too many flows\n");
		return -1;
	}

	snprintf(cmd, sizeof(cmd), "ethtool -N %s %s", ifname, format);

	va_start(args, format);
	ret = __run_command(local_output, sizeof(local_output), cmd, args);
	va_end(args);

	if (ret != 0)
		return ret;

	/* Extract the ID from the output */
	id_start = strstr(local_output, "Added rule with ID ");
	if (!id_start)
		return -1;
	id_start += strlen("Added rule with ID ");

	flow_id = strtol(id_start, &endptr, 10);
	if (endptr == id_start || flow_id < 0 || flow_id > INT_MAX)
		return -1;

	fprintf(stderr, "Added flow rule with ID %ld\n", flow_id);
	ntuple_ids[flow_idx] = flow_id;
	return flow_id;
}

static int rxq_num(int ifindex)
{
	struct ethtool_channels_get_req *req;
	struct ethtool_channels_get_rsp *rsp;
	struct ynl_error yerr;
	struct ynl_sock *ys;
	int num = -1;

	ys = ynl_sock_create(&ynl_ethtool_family, &yerr);
	if (!ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return -1;
	}

	req = ethtool_channels_get_req_alloc();
	ethtool_channels_get_req_set_header_dev_index(req, ifindex);
	rsp = ethtool_channels_get(ys, req);
	if (rsp)
		num = rsp->rx_count + rsp->combined_count;
	ethtool_channels_get_req_free(req);
	ethtool_channels_get_rsp_free(rsp);

	ynl_sock_destroy(ys);

	return num;
}

static void reset_flow_steering(void)
{
	int i;

	for (i = 0; i < MAX_FLOWS; i++) {
		if (ntuple_ids[i] == -1)
			continue;
		run_command("ethtool -N %s delete %d",
			    ifname, ntuple_ids[i]);
		ntuple_ids[i] = -1;
	}
}

static const char *tcp_data_split_str(int val)
{
	switch (val) {
	case 0:
		return "off";
	case 1:
		return "auto";
	case 2:
		return "on";
	default:
		return "?";
	}
}

static struct ethtool_rings_get_rsp *get_ring_config(void)
{
	struct ethtool_rings_get_req *get_req;
	struct ethtool_rings_get_rsp *get_rsp;
	struct ynl_error yerr;
	struct ynl_sock *ys;

	ys = ynl_sock_create(&ynl_ethtool_family, &yerr);
	if (!ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return NULL;
	}

	get_req = ethtool_rings_get_req_alloc();
	ethtool_rings_get_req_set_header_dev_index(get_req, ifindex);
	get_rsp = ethtool_rings_get(ys, get_req);
	ethtool_rings_get_req_free(get_req);

	ynl_sock_destroy(ys);

	return get_rsp;
}

static void restore_ring_config(const struct ethtool_rings_get_rsp *config)
{
	struct ethtool_rings_get_req *get_req;
	struct ethtool_rings_get_rsp *get_rsp;
	struct ethtool_rings_set_req *req;
	struct ynl_error yerr;
	struct ynl_sock *ys;
	int ret;

	if (!config)
		return;

	ys = ynl_sock_create(&ynl_ethtool_family, &yerr);
	if (!ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return;
	}

	req = ethtool_rings_set_req_alloc();
	ethtool_rings_set_req_set_header_dev_index(req, ifindex);
	ethtool_rings_set_req_set_tcp_data_split(req,
						ETHTOOL_TCP_DATA_SPLIT_UNKNOWN);
	if (config->_present.hds_thresh)
		ethtool_rings_set_req_set_hds_thresh(req, config->hds_thresh);

	ret = ethtool_rings_set(ys, req);
	if (ret < 0)
		fprintf(stderr, "YNL restoring HDS cfg: %s\n", ys->err.msg);

	get_req = ethtool_rings_get_req_alloc();
	ethtool_rings_get_req_set_header_dev_index(get_req, ifindex);
	get_rsp = ethtool_rings_get(ys, get_req);
	ethtool_rings_get_req_free(get_req);

	/* use explicit value if UKNOWN didn't give us the previous */
	if (get_rsp->tcp_data_split != config->tcp_data_split) {
		ethtool_rings_set_req_set_tcp_data_split(req,
							config->tcp_data_split);
		ret = ethtool_rings_set(ys, req);
		if (ret < 0)
			fprintf(stderr, "YNL restoring expl HDS cfg: %s\n",
				ys->err.msg);
	}

	ethtool_rings_get_rsp_free(get_rsp);
	ethtool_rings_set_req_free(req);

	ynl_sock_destroy(ys);
}

static int
configure_headersplit(const struct ethtool_rings_get_rsp *old, bool on)
{
	struct ethtool_rings_get_req *get_req;
	struct ethtool_rings_get_rsp *get_rsp;
	struct ethtool_rings_set_req *req;
	struct ynl_error yerr;
	struct ynl_sock *ys;
	int ret;

	ys = ynl_sock_create(&ynl_ethtool_family, &yerr);
	if (!ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return -1;
	}

	req = ethtool_rings_set_req_alloc();
	ethtool_rings_set_req_set_header_dev_index(req, ifindex);
	if (on) {
		ethtool_rings_set_req_set_tcp_data_split(req,
						ETHTOOL_TCP_DATA_SPLIT_ENABLED);
		if (old->_present.hds_thresh)
			ethtool_rings_set_req_set_hds_thresh(req, 0);
	} else {
		ethtool_rings_set_req_set_tcp_data_split(req,
						ETHTOOL_TCP_DATA_SPLIT_UNKNOWN);
	}
	ret = ethtool_rings_set(ys, req);
	if (ret < 0)
		fprintf(stderr, "YNL failed: %s\n", ys->err.msg);
	ethtool_rings_set_req_free(req);

	if (ret == 0) {
		get_req = ethtool_rings_get_req_alloc();
		ethtool_rings_get_req_set_header_dev_index(get_req, ifindex);
		get_rsp = ethtool_rings_get(ys, get_req);
		ethtool_rings_get_req_free(get_req);
		if (get_rsp)
			fprintf(stderr, "TCP header split: %s\n",
				tcp_data_split_str(get_rsp->tcp_data_split));
		ethtool_rings_get_rsp_free(get_rsp);
	}

	ynl_sock_destroy(ys);

	return ret;
}

static int configure_rss(void)
{
	return run_command("ethtool -X %s equal %d >&2", ifname, start_queue);
}

static void reset_rss(void)
{
	run_command("ethtool -X %s default >&2", ifname, start_queue);
}

static int check_changing_channels(unsigned int rx, unsigned int tx)
{
	struct ethtool_channels_get_req *gchan;
	struct ethtool_channels_set_req *schan;
	struct ethtool_channels_get_rsp *chan;
	struct ynl_error yerr;
	struct ynl_sock *ys;
	int ret;

	fprintf(stderr, "setting channel count rx:%u tx:%u\n", rx, tx);

	ys = ynl_sock_create(&ynl_ethtool_family, &yerr);
	if (!ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return -1;
	}

	gchan = ethtool_channels_get_req_alloc();
	if (!gchan) {
		ret = -1;
		goto exit_close_sock;
	}

	ethtool_channels_get_req_set_header_dev_index(gchan, ifindex);
	chan = ethtool_channels_get(ys, gchan);
	ethtool_channels_get_req_free(gchan);
	if (!chan) {
		fprintf(stderr, "YNL get channels: %s\n", ys->err.msg);
		ret = -1;
		goto exit_close_sock;
	}

	schan =	ethtool_channels_set_req_alloc();
	if (!schan) {
		ret = -1;
		goto exit_free_chan;
	}

	ethtool_channels_set_req_set_header_dev_index(schan, ifindex);

	if (chan->_present.combined_count) {
		if (chan->_present.rx_count || chan->_present.tx_count) {
			ethtool_channels_set_req_set_rx_count(schan, 0);
			ethtool_channels_set_req_set_tx_count(schan, 0);
		}

		if (rx == tx) {
			ethtool_channels_set_req_set_combined_count(schan, rx);
		} else if (rx > tx) {
			ethtool_channels_set_req_set_combined_count(schan, tx);
			ethtool_channels_set_req_set_rx_count(schan, rx - tx);
		} else {
			ethtool_channels_set_req_set_combined_count(schan, rx);
			ethtool_channels_set_req_set_tx_count(schan, tx - rx);
		}

	} else if (chan->_present.rx_count) {
		ethtool_channels_set_req_set_rx_count(schan, rx);
		ethtool_channels_set_req_set_tx_count(schan, tx);
	} else {
		fprintf(stderr, "Error: device has neither combined nor rx channels\n");
		ret = -1;
		goto exit_free_schan;
	}

	ret = ethtool_channels_set(ys, schan);
	if (ret) {
		fprintf(stderr, "YNL set channels: %s\n", ys->err.msg);
	} else {
		/* We were expecting a failure, go back to previous settings */
		ethtool_channels_set_req_set_combined_count(schan,
							    chan->combined_count);
		ethtool_channels_set_req_set_rx_count(schan, chan->rx_count);
		ethtool_channels_set_req_set_tx_count(schan, chan->tx_count);

		ret = ethtool_channels_set(ys, schan);
		if (ret)
			fprintf(stderr, "YNL un-setting channels: %s\n",
				ys->err.msg);
	}

exit_free_schan:
	ethtool_channels_set_req_free(schan);
exit_free_chan:
	ethtool_channels_get_rsp_free(chan);
exit_close_sock:
	ynl_sock_destroy(ys);

	return ret;
}

static int configure_flow_steering(struct sockaddr_in6 *server_sin)
{
	const char *type = "tcp6";
	const char *server_addr;
	char buf[40];
	int flow_id;

	inet_ntop(AF_INET6, &server_sin->sin6_addr, buf, sizeof(buf));
	server_addr = buf;

	if (IN6_IS_ADDR_V4MAPPED(&server_sin->sin6_addr)) {
		type = "tcp4";
		server_addr = strrchr(server_addr, ':') + 1;
	}

	/* Try configure 5-tuple */
	flow_id = ethtool_add_flow("flow-type %s %s %s dst-ip %s %s %s dst-port %s queue %d",
				   type,
				   client_ip ? "src-ip" : "",
				   client_ip ?: "",
				   server_addr,
				   client_ip ? "src-port" : "",
				   client_ip ? port : "",
				   port, start_queue);
	if (flow_id < 0) {
		/* If that fails, try configure 3-tuple */
		flow_id = ethtool_add_flow("flow-type %s dst-ip %s dst-port %s queue %d",
					   type, server_addr, port, start_queue);
		if (flow_id < 0)
			/* If that fails, return error */
			return -1;
	}

	return 0;
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
		netdev_queue_id_free(queues);
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

static int bind_tx_queue(unsigned int ifindex, unsigned int dmabuf_fd,
			 struct ynl_sock **ys)
{
	struct netdev_bind_tx_req *req = NULL;
	struct netdev_bind_tx_rsp *rsp = NULL;
	struct ynl_error yerr;

	*ys = ynl_sock_create(&ynl_netdev_family, &yerr);
	if (!*ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return -1;
	}

	req = netdev_bind_tx_req_alloc();
	netdev_bind_tx_req_set_ifindex(req, ifindex);
	netdev_bind_tx_req_set_fd(req, dmabuf_fd);

	rsp = netdev_bind_tx(*ys, req);
	if (!rsp) {
		perror("netdev_bind_tx");
		goto err_close;
	}

	if (!rsp->_present.id) {
		perror("id not present");
		goto err_close;
	}

	fprintf(stderr, "got tx dmabuf id=%d\n", rsp->id);
	tx_dmabuf_id = rsp->id;

	netdev_bind_tx_req_free(req);
	netdev_bind_tx_rsp_free(rsp);

	return 0;

err_close:
	fprintf(stderr, "YNL failed: %s\n", (*ys)->err.msg);
	netdev_bind_tx_req_free(req);
	ynl_sock_destroy(*ys);
	return -1;
}

static int enable_reuseaddr(int fd)
{
	int opt = 1;
	int ret;

	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
	if (ret) {
		pr_err("SO_REUSEPORT failed");
		return -1;
	}

	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (ret) {
		pr_err("SO_REUSEADDR failed");
		return -1;
	}

	return 0;
}

static int parse_address(const char *str, int port, struct sockaddr_in6 *sin6)
{
	int ret;

	sin6->sin6_family = AF_INET6;
	sin6->sin6_port = htons(port);

	ret = inet_pton(sin6->sin6_family, str, &sin6->sin6_addr);
	if (ret != 1) {
		/* fallback to plain IPv4 */
		ret = inet_pton(AF_INET, str, &sin6->sin6_addr.s6_addr32[3]);
		if (ret != 1)
			return -1;

		/* add ::ffff prefix */
		sin6->sin6_addr.s6_addr32[0] = 0;
		sin6->sin6_addr.s6_addr32[1] = 0;
		sin6->sin6_addr.s6_addr16[4] = 0;
		sin6->sin6_addr.s6_addr16[5] = 0xffff;
	}

	return 0;
}

static struct netdev_queue_id *create_queues(void)
{
	struct netdev_queue_id *queues;
	size_t i = 0;

	queues = netdev_queue_id_alloc(num_queues);
	for (i = 0; i < num_queues; i++) {
		netdev_queue_id_set_type(&queues[i], NETDEV_QUEUE_TYPE_RX);
		netdev_queue_id_set_id(&queues[i], start_queue + i);
	}

	return queues;
}

static int do_server(struct memory_buffer *mem)
{
	struct ethtool_rings_get_rsp *ring_config;
	char ctrl_data[sizeof(int) * 20000];
	size_t non_page_aligned_frags = 0;
	struct sockaddr_in6 client_addr;
	struct sockaddr_in6 server_sin;
	size_t page_aligned_frags = 0;
	size_t total_received = 0;
	socklen_t client_addr_len;
	bool is_devmem = false;
	char *tmp_mem = NULL;
	struct ynl_sock *ys;
	char iobuf[819200];
	int ret, err = -1;
	char buffer[256];
	int socket_fd;
	int client_fd;

	ret = parse_address(server_ip, atoi(port), &server_sin);
	if (ret < 0) {
		pr_err("parse server address");
		return -1;
	}

	ring_config = get_ring_config();
	if (!ring_config) {
		pr_err("Failed to get current ring configuration");
		return -1;
	}

	if (configure_headersplit(ring_config, 1)) {
		pr_err("Failed to enable TCP header split");
		goto err_free_ring_config;
	}

	/* Configure RSS to divert all traffic from our devmem queues */
	if (configure_rss()) {
		pr_err("Failed to configure rss");
		goto err_reset_headersplit;
	}

	/* Flow steer our devmem flows to start_queue */
	if (configure_flow_steering(&server_sin)) {
		pr_err("Failed to configure flow steering");
		goto err_reset_rss;
	}

	if (bind_rx_queue(ifindex, mem->fd, create_queues(), num_queues, &ys)) {
		pr_err("Failed to bind");
		goto err_reset_flow_steering;
	}

	tmp_mem = malloc(mem->size);
	if (!tmp_mem)
		goto err_unbind;

	socket_fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (socket_fd < 0) {
		pr_err("Failed to create socket");
		goto err_free_tmp;
	}

	if (enable_reuseaddr(socket_fd))
		goto err_close_socket;

	fprintf(stderr, "binding to address %s:%d\n", server_ip,
		ntohs(server_sin.sin6_port));

	ret = bind(socket_fd, &server_sin, sizeof(server_sin));
	if (ret) {
		pr_err("Failed to bind");
		goto err_close_socket;
	}

	ret = listen(socket_fd, 1);
	if (ret) {
		pr_err("Failed to listen");
		goto err_close_socket;
	}

	client_addr_len = sizeof(client_addr);

	inet_ntop(AF_INET6, &server_sin.sin6_addr, buffer,
		  sizeof(buffer));
	fprintf(stderr, "Waiting or connection on %s:%d\n", buffer,
		ntohs(server_sin.sin6_port));
	client_fd = accept(socket_fd, &client_addr, &client_addr_len);
	if (client_fd < 0) {
		pr_err("Failed to accept");
		goto err_close_socket;
	}

	inet_ntop(AF_INET6, &client_addr.sin6_addr, buffer,
		  sizeof(buffer));
	fprintf(stderr, "Got connection from %s:%d\n", buffer,
		ntohs(client_addr.sin6_port));

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
			if (errno == EFAULT) {
				pr_err("received EFAULT, won't recover");
				goto err_close_client;
			}
			continue;
		}
		if (ret == 0) {
			errno = 0;
			pr_err("client exited");
			goto cleanup;
		}

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

			if (dmabuf_cmsg->dmabuf_id != dmabuf_id) {
				pr_err("received on wrong dmabuf_id: flow steering error");
				goto err_close_client;
			}

			if (dmabuf_cmsg->frag_size % getpagesize())
				non_page_aligned_frags++;
			else
				page_aligned_frags++;

			provider->memcpy_from_device(tmp_mem, mem,
						     dmabuf_cmsg->frag_offset,
						     dmabuf_cmsg->frag_size);

			if (do_validation) {
				if (validate_buffer(tmp_mem,
						    dmabuf_cmsg->frag_size))
					goto err_close_client;
			} else {
				print_nonzero_bytes(tmp_mem,
						    dmabuf_cmsg->frag_size);
			}

			ret = setsockopt(client_fd, SOL_SOCKET,
					 SO_DEVMEM_DONTNEED, &token,
					 sizeof(token));
			if (ret != 1) {
				pr_err("SO_DEVMEM_DONTNEED not enough tokens");
				goto err_close_client;
			}
		}
		if (!is_devmem) {
			pr_err("flow steering error");
			goto err_close_client;
		}

		fprintf(stderr, "total_received=%lu\n", total_received);
	}

	fprintf(stderr, "%s: ok\n", TEST_PREFIX);

	fprintf(stderr, "page_aligned_frags=%lu, non_page_aligned_frags=%lu\n",
		page_aligned_frags, non_page_aligned_frags);

cleanup:
	err = 0;

err_close_client:
	close(client_fd);
err_close_socket:
	close(socket_fd);
err_free_tmp:
	free(tmp_mem);
err_unbind:
	ynl_sock_destroy(ys);
err_reset_flow_steering:
	reset_flow_steering();
err_reset_rss:
	reset_rss();
err_reset_headersplit:
	restore_ring_config(ring_config);
err_free_ring_config:
	ethtool_rings_get_rsp_free(ring_config);
	return err;
}

int run_devmem_tests(void)
{
	struct ethtool_rings_get_rsp *ring_config;
	struct netdev_queue_id *queues;
	struct memory_buffer *mem;
	struct ynl_sock *ys;
	int err = -1;

	mem = provider->alloc(getpagesize() * NUM_PAGES);
	if (!mem) {
		pr_err("Failed to allocate memory buffer");
		return -1;
	}

	ring_config = get_ring_config();
	if (!ring_config) {
		pr_err("Failed to get current ring configuration");
		goto err_free_mem;
	}

	/* Configure RSS to divert all traffic from our devmem queues */
	if (configure_rss()) {
		pr_err("rss error");
		goto err_free_ring_config;
	}

	if (configure_headersplit(ring_config, 1)) {
		pr_err("Failed to configure header split");
		goto err_reset_rss;
	}

	queues = netdev_queue_id_alloc(num_queues);
	if (!queues) {
		pr_err("Failed to allocate empty queues array");
		goto err_reset_headersplit;
	}

	if (!bind_rx_queue(ifindex, mem->fd, queues, num_queues, &ys)) {
		pr_err("Binding empty queues array should have failed");
		goto err_unbind;
	}

	if (configure_headersplit(ring_config, 0)) {
		pr_err("Failed to configure header split");
		goto err_reset_headersplit;
	}

	queues = create_queues();
	if (!queues) {
		pr_err("Failed to create queues");
		goto err_reset_headersplit;
	}

	if (!bind_rx_queue(ifindex, mem->fd, queues, num_queues, &ys)) {
		pr_err("Configure dmabuf with header split off should have failed");
		goto err_unbind;
	}

	if (configure_headersplit(ring_config, 1)) {
		pr_err("Failed to configure header split");
		goto err_reset_headersplit;
	}

	queues = create_queues();
	if (!queues) {
		pr_err("Failed to create queues");
		goto err_reset_headersplit;
	}

	if (bind_rx_queue(ifindex, mem->fd, queues, num_queues, &ys)) {
		pr_err("Failed to bind");
		goto err_reset_headersplit;
	}

	/* Deactivating a bound queue should not be legal */
	if (!check_changing_channels(num_queues, num_queues)) {
		pr_err("Deactivating a bound queue should be illegal");
		goto err_unbind;
	}

	err = 0;
	goto err_unbind;

err_unbind:
	ynl_sock_destroy(ys);
err_reset_headersplit:
	restore_ring_config(ring_config);
err_reset_rss:
	reset_rss();
err_free_ring_config:
	ethtool_rings_get_rsp_free(ring_config);
err_free_mem:
	provider->free(mem);
	return err;
}

static uint64_t gettimeofday_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000ULL) + (tv.tv_usec / 1000ULL);
}

static int do_poll(int fd)
{
	struct pollfd pfd;
	int ret;

	pfd.revents = 0;
	pfd.fd = fd;

	ret = poll(&pfd, 1, waittime_ms);
	if (ret == -1) {
		pr_err("poll");
		return -1;
	}

	return ret && (pfd.revents & POLLERR);
}

static int wait_compl(int fd)
{
	int64_t tstop = gettimeofday_ms() + waittime_ms;
	char control[CMSG_SPACE(100)] = {};
	struct sock_extended_err *serr;
	struct msghdr msg = {};
	struct cmsghdr *cm;
	__u32 hi, lo;
	int ret;

	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);

	while (gettimeofday_ms() < tstop) {
		ret = do_poll(fd);
		if (ret < 0)
			return ret;
		if (!ret)
			continue;

		ret = recvmsg(fd, &msg, MSG_ERRQUEUE);
		if (ret < 0) {
			if (errno == EAGAIN)
				continue;
			pr_err("recvmsg(MSG_ERRQUEUE)");
			return -1;
		}
		if (msg.msg_flags & MSG_CTRUNC) {
			pr_err("MSG_CTRUNC");
			return -1;
		}

		for (cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
			if (cm->cmsg_level != SOL_IP &&
			    cm->cmsg_level != SOL_IPV6)
				continue;
			if (cm->cmsg_level == SOL_IP &&
			    cm->cmsg_type != IP_RECVERR)
				continue;
			if (cm->cmsg_level == SOL_IPV6 &&
			    cm->cmsg_type != IPV6_RECVERR)
				continue;

			serr = (void *)CMSG_DATA(cm);
			if (serr->ee_origin != SO_EE_ORIGIN_ZEROCOPY) {
				pr_err("wrong origin %u", serr->ee_origin);
				return -1;
			}
			if (serr->ee_errno != 0) {
				pr_err("wrong errno %d", serr->ee_errno);
				return -1;
			}

			hi = serr->ee_data;
			lo = serr->ee_info;

			fprintf(stderr, "tx complete [%d,%d]\n", lo, hi);
			return 0;
		}
	}

	pr_err("did not receive tx completion");
	return -1;
}

static int do_client(struct memory_buffer *mem)
{
	char ctrl_data[CMSG_SPACE(sizeof(__u32))];
	struct sockaddr_in6 server_sin;
	struct sockaddr_in6 client_sin;
	struct ynl_sock *ys = NULL;
	struct iovec iov[MAX_IOV];
	struct msghdr msg = {};
	ssize_t line_size = 0;
	struct cmsghdr *cmsg;
	char *line = NULL;
	int ret, err = -1;
	size_t len = 0;
	int socket_fd;
	__u32 ddmabuf;
	int opt = 1;

	ret = parse_address(server_ip, atoi(port), &server_sin);
	if (ret < 0) {
		pr_err("parse server address");
		return -1;
	}

	if (client_ip) {
		ret = parse_address(client_ip, atoi(port), &client_sin);
		if (ret < 0) {
			pr_err("parse client address");
			return ret;
		}
	}

	socket_fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (socket_fd < 0) {
		pr_err("create socket");
		return -1;
	}

	if (enable_reuseaddr(socket_fd))
		goto err_close_socket;

	ret = setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, ifname,
			 strlen(ifname) + 1);
	if (ret) {
		pr_err("bindtodevice");
		goto err_close_socket;
	}

	if (bind_tx_queue(ifindex, mem->fd, &ys)) {
		pr_err("Failed to bind");
		goto err_close_socket;
	}

	if (client_ip) {
		ret = bind(socket_fd, &client_sin, sizeof(client_sin));
		if (ret) {
			pr_err("bind");
			goto err_unbind;
		}
	}

	ret = setsockopt(socket_fd, SOL_SOCKET, SO_ZEROCOPY, &opt, sizeof(opt));
	if (ret) {
		pr_err("set sock opt");
		goto err_unbind;
	}

	fprintf(stderr, "Connect to %s %d (via %s)\n", server_ip,
		ntohs(server_sin.sin6_port), ifname);

	ret = connect(socket_fd, &server_sin, sizeof(server_sin));
	if (ret) {
		pr_err("connect");
		goto err_unbind;
	}

	while (1) {
		free(line);
		line = NULL;
		line_size = getline(&line, &len, stdin);

		if (line_size < 0)
			break;

		if (max_chunk) {
			msg.msg_iovlen =
				(line_size + max_chunk - 1) / max_chunk;
			if (msg.msg_iovlen > MAX_IOV) {
				pr_err("can't partition %zd bytes into maximum of %d chunks",
				       line_size, MAX_IOV);
				goto err_free_line;
			}

			for (int i = 0; i < msg.msg_iovlen; i++) {
				iov[i].iov_base = (void *)(i * max_chunk);
				iov[i].iov_len = max_chunk;
			}

			iov[msg.msg_iovlen - 1].iov_len =
				line_size - (msg.msg_iovlen - 1) * max_chunk;
		} else {
			iov[0].iov_base = 0;
			iov[0].iov_len = line_size;
			msg.msg_iovlen = 1;
		}

		msg.msg_iov = iov;
		provider->memcpy_to_device(mem, 0, line, line_size);

		msg.msg_control = ctrl_data;
		msg.msg_controllen = sizeof(ctrl_data);

		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_DEVMEM_DMABUF;
		cmsg->cmsg_len = CMSG_LEN(sizeof(__u32));

		ddmabuf = tx_dmabuf_id;

		*((__u32 *)CMSG_DATA(cmsg)) = ddmabuf;

		ret = sendmsg(socket_fd, &msg, MSG_ZEROCOPY);
		if (ret < 0) {
			pr_err("Failed sendmsg");
			goto err_free_line;
		}

		fprintf(stderr, "sendmsg_ret=%d\n", ret);

		if (ret != line_size) {
			pr_err("Did not send all bytes %d vs %zd", ret, line_size);
			goto err_free_line;
		}

		if (wait_compl(socket_fd))
			goto err_free_line;
	}

	fprintf(stderr, "%s: tx ok\n", TEST_PREFIX);

	err = 0;

err_free_line:
	free(line);
err_unbind:
	ynl_sock_destroy(ys);
err_close_socket:
	close(socket_fd);
	return err;
}

int main(int argc, char *argv[])
{
	struct memory_buffer *mem;
	int is_server = 0, opt;
	int ret, err = 1;

	while ((opt = getopt(argc, argv, "ls:c:p:v:q:t:f:z:")) != -1) {
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
		case 'z':
			max_chunk = atoi(optarg);
			break;
		case '?':
			fprintf(stderr, "unknown option: %c\n", optopt);
			break;
		}
	}

	if (!ifname) {
		pr_err("Missing -f argument");
		return 1;
	}

	ifindex = if_nametoindex(ifname);

	fprintf(stderr, "using ifindex=%u\n", ifindex);

	if (!server_ip && !client_ip) {
		if (start_queue < 0 && num_queues < 0) {
			num_queues = rxq_num(ifindex);
			if (num_queues < 0) {
				pr_err("couldn't detect number of queues");
				return 1;
			}
			if (num_queues < 2) {
				pr_err("number of device queues is too low");
				return 1;
			}
			/* make sure can bind to multiple queues */
			start_queue = num_queues / 2;
			num_queues /= 2;
		}

		if (start_queue < 0 || num_queues < 0) {
			pr_err("Both -t and -q are required");
			return 1;
		}

		return run_devmem_tests();
	}

	if (start_queue < 0 && num_queues < 0) {
		num_queues = rxq_num(ifindex);
		if (num_queues < 2) {
			pr_err("number of device queues is too low");
			return 1;
		}

		num_queues = 1;
		start_queue = rxq_num(ifindex) - num_queues;

		if (start_queue < 0) {
			pr_err("couldn't detect number of queues");
			return 1;
		}

		fprintf(stderr, "using queues %d..%d\n", start_queue, start_queue + num_queues);
	}

	for (; optind < argc; optind++)
		fprintf(stderr, "extra arguments: %s\n", argv[optind]);

	if (start_queue < 0) {
		pr_err("Missing -t argument");
		return 1;
	}

	if (num_queues < 0) {
		pr_err("Missing -q argument");
		return 1;
	}

	if (!server_ip) {
		pr_err("Missing -s argument");
		return 1;
	}

	if (!port) {
		pr_err("Missing -p argument");
		return 1;
	}

	mem = provider->alloc(getpagesize() * NUM_PAGES);
	if (!mem) {
		pr_err("Failed to allocate memory buffer");
		return 1;
	}

	ret = is_server ? do_server(mem) : do_client(mem);
	if (ret)
		goto err_free_mem;

	err = 0;

err_free_mem:
	provider->free(mem);
	return err;
}
