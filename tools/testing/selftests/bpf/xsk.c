// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * AF_XDP user-space access library.
 *
 * Copyright(c) 2018 - 2019 Intel Corporation.
 *
 * Author(s): Magnus Karlsson <magnus.karlsson@intel.com>
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <asm/barrier.h>
#include <linux/compiler.h>
#include <linux/ethtool.h>
#include <linux/filter.h>
#include <linux/if_ether.h>
#include <linux/if_link.h>
#include <linux/if_packet.h>
#include <linux/if_xdp.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "xsk.h"
#include "bpf_util.h"

#ifndef SOL_XDP
 #define SOL_XDP 283
#endif

#ifndef AF_XDP
 #define AF_XDP 44
#endif

#ifndef PF_XDP
 #define PF_XDP AF_XDP
#endif

#define pr_warn(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)

#define XSKMAP_SIZE 1

struct xsk_umem {
	struct xsk_ring_prod *fill_save;
	struct xsk_ring_cons *comp_save;
	char *umem_area;
	struct xsk_umem_config config;
	int fd;
	int refcount;
	struct list_head ctx_list;
	bool rx_ring_setup_done;
	bool tx_ring_setup_done;
};

struct xsk_ctx {
	struct xsk_ring_prod *fill;
	struct xsk_ring_cons *comp;
	__u32 queue_id;
	struct xsk_umem *umem;
	int refcount;
	int ifindex;
	struct list_head list;
};

struct xsk_socket {
	struct xsk_ring_cons *rx;
	struct xsk_ring_prod *tx;
	struct xsk_ctx *ctx;
	struct xsk_socket_config config;
	int fd;
};

struct nl_mtu_req {
	struct nlmsghdr nh;
	struct ifinfomsg msg;
	char             buf[512];
};

int xsk_umem__fd(const struct xsk_umem *umem)
{
	return umem ? umem->fd : -EINVAL;
}

int xsk_socket__fd(const struct xsk_socket *xsk)
{
	return xsk ? xsk->fd : -EINVAL;
}

static bool xsk_page_aligned(void *buffer)
{
	unsigned long addr = (unsigned long)buffer;

	return !(addr & (getpagesize() - 1));
}

static void xsk_set_umem_config(struct xsk_umem_config *cfg,
				const struct xsk_umem_config *usr_cfg)
{
	if (!usr_cfg) {
		cfg->fill_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
		cfg->comp_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
		cfg->frame_size = XSK_UMEM__DEFAULT_FRAME_SIZE;
		cfg->frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM;
		cfg->flags = XSK_UMEM__DEFAULT_FLAGS;
		cfg->tx_metadata_len = 0;
		return;
	}

	cfg->fill_size = usr_cfg->fill_size;
	cfg->comp_size = usr_cfg->comp_size;
	cfg->frame_size = usr_cfg->frame_size;
	cfg->frame_headroom = usr_cfg->frame_headroom;
	cfg->flags = usr_cfg->flags;
	cfg->tx_metadata_len = usr_cfg->tx_metadata_len;
}

static int xsk_set_xdp_socket_config(struct xsk_socket_config *cfg,
				     const struct xsk_socket_config *usr_cfg)
{
	if (!usr_cfg) {
		cfg->rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
		cfg->tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
		cfg->bind_flags = 0;
		return 0;
	}

	cfg->rx_size = usr_cfg->rx_size;
	cfg->tx_size = usr_cfg->tx_size;
	cfg->bind_flags = usr_cfg->bind_flags;

	return 0;
}

static int xsk_get_mmap_offsets(int fd, struct xdp_mmap_offsets *off)
{
	socklen_t optlen;
	int err;

	optlen = sizeof(*off);
	err = getsockopt(fd, SOL_XDP, XDP_MMAP_OFFSETS, off, &optlen);
	if (err)
		return err;

	if (optlen == sizeof(*off))
		return 0;

	return -EINVAL;
}

static int xsk_create_umem_rings(struct xsk_umem *umem, int fd,
				 struct xsk_ring_prod *fill,
				 struct xsk_ring_cons *comp)
{
	struct xdp_mmap_offsets off;
	void *map;
	int err;

	err = setsockopt(fd, SOL_XDP, XDP_UMEM_FILL_RING,
			 &umem->config.fill_size,
			 sizeof(umem->config.fill_size));
	if (err)
		return -errno;

	err = setsockopt(fd, SOL_XDP, XDP_UMEM_COMPLETION_RING,
			 &umem->config.comp_size,
			 sizeof(umem->config.comp_size));
	if (err)
		return -errno;

	err = xsk_get_mmap_offsets(fd, &off);
	if (err)
		return -errno;

	map = mmap(NULL, off.fr.desc + umem->config.fill_size * sizeof(__u64),
		   PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd,
		   XDP_UMEM_PGOFF_FILL_RING);
	if (map == MAP_FAILED)
		return -errno;

	fill->mask = umem->config.fill_size - 1;
	fill->size = umem->config.fill_size;
	fill->producer = map + off.fr.producer;
	fill->consumer = map + off.fr.consumer;
	fill->flags = map + off.fr.flags;
	fill->ring = map + off.fr.desc;
	fill->cached_cons = umem->config.fill_size;

	map = mmap(NULL, off.cr.desc + umem->config.comp_size * sizeof(__u64),
		   PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd,
		   XDP_UMEM_PGOFF_COMPLETION_RING);
	if (map == MAP_FAILED) {
		err = -errno;
		goto out_mmap;
	}

	comp->mask = umem->config.comp_size - 1;
	comp->size = umem->config.comp_size;
	comp->producer = map + off.cr.producer;
	comp->consumer = map + off.cr.consumer;
	comp->flags = map + off.cr.flags;
	comp->ring = map + off.cr.desc;

	return 0;

out_mmap:
	munmap(map, off.fr.desc + umem->config.fill_size * sizeof(__u64));
	return err;
}

int xsk_umem__create(struct xsk_umem **umem_ptr, void *umem_area,
		     __u64 size, struct xsk_ring_prod *fill,
		     struct xsk_ring_cons *comp,
		     const struct xsk_umem_config *usr_config)
{
	struct xdp_umem_reg mr;
	struct xsk_umem *umem;
	int err;

	if (!umem_area || !umem_ptr || !fill || !comp)
		return -EFAULT;
	if (!size && !xsk_page_aligned(umem_area))
		return -EINVAL;

	umem = calloc(1, sizeof(*umem));
	if (!umem)
		return -ENOMEM;

	umem->fd = socket(AF_XDP, SOCK_RAW | SOCK_CLOEXEC, 0);
	if (umem->fd < 0) {
		err = -errno;
		goto out_umem_alloc;
	}

	umem->umem_area = umem_area;
	INIT_LIST_HEAD(&umem->ctx_list);
	xsk_set_umem_config(&umem->config, usr_config);

	memset(&mr, 0, sizeof(mr));
	mr.addr = (uintptr_t)umem_area;
	mr.len = size;
	mr.chunk_size = umem->config.frame_size;
	mr.headroom = umem->config.frame_headroom;
	mr.flags = umem->config.flags;
	mr.tx_metadata_len = umem->config.tx_metadata_len;

	err = setsockopt(umem->fd, SOL_XDP, XDP_UMEM_REG, &mr, sizeof(mr));
	if (err) {
		err = -errno;
		goto out_socket;
	}

	err = xsk_create_umem_rings(umem, umem->fd, fill, comp);
	if (err)
		goto out_socket;

	umem->fill_save = fill;
	umem->comp_save = comp;
	*umem_ptr = umem;
	return 0;

out_socket:
	close(umem->fd);
out_umem_alloc:
	free(umem);
	return err;
}

bool xsk_is_in_mode(u32 ifindex, int mode)
{
	LIBBPF_OPTS(bpf_xdp_query_opts, opts);
	int ret;

	ret = bpf_xdp_query(ifindex, mode, &opts);
	if (ret) {
		printf("XDP mode query returned error %s\n", strerror(errno));
		return false;
	}

	if (mode == XDP_FLAGS_DRV_MODE)
		return opts.attach_mode == XDP_ATTACHED_DRV;
	else if (mode == XDP_FLAGS_SKB_MODE)
		return opts.attach_mode == XDP_ATTACHED_SKB;

	return false;
}

/* Lifted from netlink.c in tools/lib/bpf */
static int netlink_recvmsg(int sock, struct msghdr *mhdr, int flags)
{
	int len;

	do {
		len = recvmsg(sock, mhdr, flags);
	} while (len < 0 && (errno == EINTR || errno == EAGAIN));

	if (len < 0)
		return -errno;
	return len;
}

/* Lifted from netlink.c in tools/lib/bpf */
static int alloc_iov(struct iovec *iov, int len)
{
	void *nbuf;

	nbuf = realloc(iov->iov_base, len);
	if (!nbuf)
		return -ENOMEM;

	iov->iov_base = nbuf;
	iov->iov_len = len;
	return 0;
}

/* Original version lifted from netlink.c in tools/lib/bpf */
static int netlink_recv(int sock)
{
	struct iovec iov = {};
	struct msghdr mhdr = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};
	bool multipart = true;
	struct nlmsgerr *err;
	struct nlmsghdr *nh;
	int len, ret;

	ret = alloc_iov(&iov, 4096);
	if (ret)
		goto done;

	while (multipart) {
		multipart = false;
		len = netlink_recvmsg(sock, &mhdr, MSG_PEEK | MSG_TRUNC);
		if (len < 0) {
			ret = len;
			goto done;
		}

		if (len > iov.iov_len) {
			ret = alloc_iov(&iov, len);
			if (ret)
				goto done;
		}

		len = netlink_recvmsg(sock, &mhdr, 0);
		if (len < 0) {
			ret = len;
			goto done;
		}

		if (len == 0)
			break;

		for (nh = (struct nlmsghdr *)iov.iov_base; NLMSG_OK(nh, len);
		     nh = NLMSG_NEXT(nh, len)) {
			if (nh->nlmsg_flags & NLM_F_MULTI)
				multipart = true;
			switch (nh->nlmsg_type) {
			case NLMSG_ERROR:
				err = (struct nlmsgerr *)NLMSG_DATA(nh);
				if (!err->error)
					continue;
				ret = err->error;
				goto done;
			case NLMSG_DONE:
				ret = 0;
				goto done;
			default:
				break;
			}
		}
	}
	ret = 0;
done:
	free(iov.iov_base);
	return ret;
}

int xsk_set_mtu(int ifindex, int mtu)
{
	struct nl_mtu_req req;
	struct rtattr *rta;
	int fd, ret;

	fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
	if (fd < 0)
		return fd;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_type = RTM_NEWLINK;
	req.msg.ifi_family = AF_UNSPEC;
	req.msg.ifi_index = ifindex;
	rta = (struct rtattr *)(((char *)&req) + NLMSG_ALIGN(req.nh.nlmsg_len));
	rta->rta_type = IFLA_MTU;
	rta->rta_len = RTA_LENGTH(sizeof(unsigned int));
	req.nh.nlmsg_len = NLMSG_ALIGN(req.nh.nlmsg_len) + RTA_LENGTH(sizeof(mtu));
	memcpy(RTA_DATA(rta), &mtu, sizeof(mtu));

	ret = send(fd, &req, req.nh.nlmsg_len, 0);
	if (ret < 0) {
		close(fd);
		return errno;
	}

	ret = netlink_recv(fd);
	close(fd);
	return ret;
}

int xsk_attach_xdp_program(struct bpf_program *prog, int ifindex, u32 xdp_flags)
{
	int prog_fd;

	prog_fd = bpf_program__fd(prog);
	return bpf_xdp_attach(ifindex, prog_fd, xdp_flags, NULL);
}

void xsk_detach_xdp_program(int ifindex, u32 xdp_flags)
{
	bpf_xdp_detach(ifindex, xdp_flags, NULL);
}

void xsk_clear_xskmap(struct bpf_map *map)
{
	u32 index = 0;
	int map_fd;

	map_fd = bpf_map__fd(map);
	bpf_map_delete_elem(map_fd, &index);
}

int xsk_update_xskmap(struct bpf_map *map, struct xsk_socket *xsk, u32 index)
{
	int map_fd, sock_fd;

	map_fd = bpf_map__fd(map);
	sock_fd = xsk_socket__fd(xsk);

	return bpf_map_update_elem(map_fd, &index, &sock_fd, 0);
}

static struct xsk_ctx *xsk_get_ctx(struct xsk_umem *umem, int ifindex,
				   __u32 queue_id)
{
	struct xsk_ctx *ctx;

	if (list_empty(&umem->ctx_list))
		return NULL;

	list_for_each_entry(ctx, &umem->ctx_list, list) {
		if (ctx->ifindex == ifindex && ctx->queue_id == queue_id) {
			ctx->refcount++;
			return ctx;
		}
	}

	return NULL;
}

static void xsk_put_ctx(struct xsk_ctx *ctx, bool unmap)
{
	struct xsk_umem *umem = ctx->umem;
	struct xdp_mmap_offsets off;
	int err;

	if (--ctx->refcount)
		return;

	if (!unmap)
		goto out_free;

	err = xsk_get_mmap_offsets(umem->fd, &off);
	if (err)
		goto out_free;

	munmap(ctx->fill->ring - off.fr.desc, off.fr.desc + umem->config.fill_size *
	       sizeof(__u64));
	munmap(ctx->comp->ring - off.cr.desc, off.cr.desc + umem->config.comp_size *
	       sizeof(__u64));

out_free:
	list_del(&ctx->list);
	free(ctx);
}

static struct xsk_ctx *xsk_create_ctx(struct xsk_socket *xsk,
				      struct xsk_umem *umem, int ifindex,
				      __u32 queue_id,
				      struct xsk_ring_prod *fill,
				      struct xsk_ring_cons *comp)
{
	struct xsk_ctx *ctx;
	int err;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;

	if (!umem->fill_save) {
		err = xsk_create_umem_rings(umem, xsk->fd, fill, comp);
		if (err) {
			free(ctx);
			return NULL;
		}
	} else if (umem->fill_save != fill || umem->comp_save != comp) {
		/* Copy over rings to new structs. */
		memcpy(fill, umem->fill_save, sizeof(*fill));
		memcpy(comp, umem->comp_save, sizeof(*comp));
	}

	ctx->ifindex = ifindex;
	ctx->refcount = 1;
	ctx->umem = umem;
	ctx->queue_id = queue_id;

	ctx->fill = fill;
	ctx->comp = comp;
	list_add(&ctx->list, &umem->ctx_list);
	return ctx;
}

int xsk_socket__create_shared(struct xsk_socket **xsk_ptr,
			      int ifindex,
			      __u32 queue_id, struct xsk_umem *umem,
			      struct xsk_ring_cons *rx,
			      struct xsk_ring_prod *tx,
			      struct xsk_ring_prod *fill,
			      struct xsk_ring_cons *comp,
			      const struct xsk_socket_config *usr_config)
{
	bool unmap, rx_setup_done = false, tx_setup_done = false;
	void *rx_map = NULL, *tx_map = NULL;
	struct sockaddr_xdp sxdp = {};
	struct xdp_mmap_offsets off;
	struct xsk_socket *xsk;
	struct xsk_ctx *ctx;
	int err;

	if (!umem || !xsk_ptr || !(rx || tx))
		return -EFAULT;

	unmap = umem->fill_save != fill;

	xsk = calloc(1, sizeof(*xsk));
	if (!xsk)
		return -ENOMEM;

	err = xsk_set_xdp_socket_config(&xsk->config, usr_config);
	if (err)
		goto out_xsk_alloc;

	if (umem->refcount++ > 0) {
		xsk->fd = socket(AF_XDP, SOCK_RAW | SOCK_CLOEXEC, 0);
		if (xsk->fd < 0) {
			err = -errno;
			goto out_xsk_alloc;
		}
	} else {
		xsk->fd = umem->fd;
		rx_setup_done = umem->rx_ring_setup_done;
		tx_setup_done = umem->tx_ring_setup_done;
	}

	ctx = xsk_get_ctx(umem, ifindex, queue_id);
	if (!ctx) {
		if (!fill || !comp) {
			err = -EFAULT;
			goto out_socket;
		}

		ctx = xsk_create_ctx(xsk, umem, ifindex, queue_id, fill, comp);
		if (!ctx) {
			err = -ENOMEM;
			goto out_socket;
		}
	}
	xsk->ctx = ctx;

	if (rx && !rx_setup_done) {
		err = setsockopt(xsk->fd, SOL_XDP, XDP_RX_RING,
				 &xsk->config.rx_size,
				 sizeof(xsk->config.rx_size));
		if (err) {
			err = -errno;
			goto out_put_ctx;
		}
		if (xsk->fd == umem->fd)
			umem->rx_ring_setup_done = true;
	}
	if (tx && !tx_setup_done) {
		err = setsockopt(xsk->fd, SOL_XDP, XDP_TX_RING,
				 &xsk->config.tx_size,
				 sizeof(xsk->config.tx_size));
		if (err) {
			err = -errno;
			goto out_put_ctx;
		}
		if (xsk->fd == umem->fd)
			umem->tx_ring_setup_done = true;
	}

	err = xsk_get_mmap_offsets(xsk->fd, &off);
	if (err) {
		err = -errno;
		goto out_put_ctx;
	}

	if (rx) {
		rx_map = mmap(NULL, off.rx.desc +
			      xsk->config.rx_size * sizeof(struct xdp_desc),
			      PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
			      xsk->fd, XDP_PGOFF_RX_RING);
		if (rx_map == MAP_FAILED) {
			err = -errno;
			goto out_put_ctx;
		}

		rx->mask = xsk->config.rx_size - 1;
		rx->size = xsk->config.rx_size;
		rx->producer = rx_map + off.rx.producer;
		rx->consumer = rx_map + off.rx.consumer;
		rx->flags = rx_map + off.rx.flags;
		rx->ring = rx_map + off.rx.desc;
		rx->cached_prod = *rx->producer;
		rx->cached_cons = *rx->consumer;
	}
	xsk->rx = rx;

	if (tx) {
		tx_map = mmap(NULL, off.tx.desc +
			      xsk->config.tx_size * sizeof(struct xdp_desc),
			      PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
			      xsk->fd, XDP_PGOFF_TX_RING);
		if (tx_map == MAP_FAILED) {
			err = -errno;
			goto out_mmap_rx;
		}

		tx->mask = xsk->config.tx_size - 1;
		tx->size = xsk->config.tx_size;
		tx->producer = tx_map + off.tx.producer;
		tx->consumer = tx_map + off.tx.consumer;
		tx->flags = tx_map + off.tx.flags;
		tx->ring = tx_map + off.tx.desc;
		tx->cached_prod = *tx->producer;
		/* cached_cons is r->size bigger than the real consumer pointer
		 * See xsk_prod_nb_free
		 */
		tx->cached_cons = *tx->consumer + xsk->config.tx_size;
	}
	xsk->tx = tx;

	sxdp.sxdp_family = PF_XDP;
	sxdp.sxdp_ifindex = ctx->ifindex;
	sxdp.sxdp_queue_id = ctx->queue_id;
	if (umem->refcount > 1) {
		sxdp.sxdp_flags |= XDP_SHARED_UMEM;
		sxdp.sxdp_shared_umem_fd = umem->fd;
	} else {
		sxdp.sxdp_flags = xsk->config.bind_flags;
	}

	err = bind(xsk->fd, (struct sockaddr *)&sxdp, sizeof(sxdp));
	if (err) {
		err = -errno;
		goto out_mmap_tx;
	}

	*xsk_ptr = xsk;
	umem->fill_save = NULL;
	umem->comp_save = NULL;
	return 0;

out_mmap_tx:
	if (tx)
		munmap(tx_map, off.tx.desc +
		       xsk->config.tx_size * sizeof(struct xdp_desc));
out_mmap_rx:
	if (rx)
		munmap(rx_map, off.rx.desc +
		       xsk->config.rx_size * sizeof(struct xdp_desc));
out_put_ctx:
	xsk_put_ctx(ctx, unmap);
out_socket:
	if (--umem->refcount)
		close(xsk->fd);
out_xsk_alloc:
	free(xsk);
	return err;
}

int xsk_socket__create(struct xsk_socket **xsk_ptr, int ifindex,
		       __u32 queue_id, struct xsk_umem *umem,
		       struct xsk_ring_cons *rx, struct xsk_ring_prod *tx,
		       const struct xsk_socket_config *usr_config)
{
	if (!umem)
		return -EFAULT;

	return xsk_socket__create_shared(xsk_ptr, ifindex, queue_id, umem,
					 rx, tx, umem->fill_save,
					 umem->comp_save, usr_config);
}

int xsk_umem__delete(struct xsk_umem *umem)
{
	struct xdp_mmap_offsets off;
	int err;

	if (!umem)
		return 0;

	if (umem->refcount)
		return -EBUSY;

	err = xsk_get_mmap_offsets(umem->fd, &off);
	if (!err && umem->fill_save && umem->comp_save) {
		munmap(umem->fill_save->ring - off.fr.desc,
		       off.fr.desc + umem->config.fill_size * sizeof(__u64));
		munmap(umem->comp_save->ring - off.cr.desc,
		       off.cr.desc + umem->config.comp_size * sizeof(__u64));
	}

	close(umem->fd);
	free(umem);

	return 0;
}

void xsk_socket__delete(struct xsk_socket *xsk)
{
	size_t desc_sz = sizeof(struct xdp_desc);
	struct xdp_mmap_offsets off;
	struct xsk_umem *umem;
	struct xsk_ctx *ctx;
	int err;

	if (!xsk)
		return;

	ctx = xsk->ctx;
	umem = ctx->umem;

	xsk_put_ctx(ctx, true);

	err = xsk_get_mmap_offsets(xsk->fd, &off);
	if (!err) {
		if (xsk->rx) {
			munmap(xsk->rx->ring - off.rx.desc,
			       off.rx.desc + xsk->config.rx_size * desc_sz);
		}
		if (xsk->tx) {
			munmap(xsk->tx->ring - off.tx.desc,
			       off.tx.desc + xsk->config.tx_size * desc_sz);
		}
	}

	umem->refcount--;
	/* Do not close an fd that also has an associated umem connected
	 * to it.
	 */
	if (xsk->fd != umem->fd)
		close(xsk->fd);
	free(xsk);
}
