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
#include <linux/if_packet.h>
#include <linux/if_xdp.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "bpf.h"
#include "libbpf.h"
#include "libbpf_internal.h"
#include "xsk.h"

#ifndef SOL_XDP
 #define SOL_XDP 283
#endif

#ifndef AF_XDP
 #define AF_XDP 44
#endif

#ifndef PF_XDP
 #define PF_XDP AF_XDP
#endif

struct xsk_umem {
	struct xsk_ring_prod *fill;
	struct xsk_ring_cons *comp;
	char *umem_area;
	struct xsk_umem_config config;
	int fd;
	int refcount;
};

struct xsk_socket {
	struct xsk_ring_cons *rx;
	struct xsk_ring_prod *tx;
	__u64 outstanding_tx;
	struct xsk_umem *umem;
	struct xsk_socket_config config;
	int fd;
	int xsks_map;
	int ifindex;
	int prog_fd;
	int qidconf_map_fd;
	int xsks_map_fd;
	__u32 queue_id;
	char ifname[IFNAMSIZ];
};

struct xsk_nl_info {
	bool xdp_prog_attached;
	int ifindex;
	int fd;
};

/* For 32-bit systems, we need to use mmap2 as the offsets are 64-bit.
 * Unfortunately, it is not part of glibc.
 */
static inline void *xsk_mmap(void *addr, size_t length, int prot, int flags,
			     int fd, __u64 offset)
{
#ifdef __NR_mmap2
	unsigned int page_shift = __builtin_ffs(getpagesize()) - 1;
	long ret = syscall(__NR_mmap2, addr, length, prot, flags, fd,
			   (off_t)(offset >> page_shift));

	return (void *)ret;
#else
	return mmap(addr, length, prot, flags, fd, offset);
#endif
}

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
		return;
	}

	cfg->fill_size = usr_cfg->fill_size;
	cfg->comp_size = usr_cfg->comp_size;
	cfg->frame_size = usr_cfg->frame_size;
	cfg->frame_headroom = usr_cfg->frame_headroom;
}

static int xsk_set_xdp_socket_config(struct xsk_socket_config *cfg,
				     const struct xsk_socket_config *usr_cfg)
{
	if (!usr_cfg) {
		cfg->rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
		cfg->tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
		cfg->libbpf_flags = 0;
		cfg->xdp_flags = 0;
		cfg->bind_flags = 0;
		return 0;
	}

	if (usr_cfg->libbpf_flags & ~XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD)
		return -EINVAL;

	cfg->rx_size = usr_cfg->rx_size;
	cfg->tx_size = usr_cfg->tx_size;
	cfg->libbpf_flags = usr_cfg->libbpf_flags;
	cfg->xdp_flags = usr_cfg->xdp_flags;
	cfg->bind_flags = usr_cfg->bind_flags;

	return 0;
}

int xsk_umem__create(struct xsk_umem **umem_ptr, void *umem_area, __u64 size,
		     struct xsk_ring_prod *fill, struct xsk_ring_cons *comp,
		     const struct xsk_umem_config *usr_config)
{
	struct xdp_mmap_offsets off;
	struct xdp_umem_reg mr;
	struct xsk_umem *umem;
	socklen_t optlen;
	void *map;
	int err;

	if (!umem_area || !umem_ptr || !fill || !comp)
		return -EFAULT;
	if (!size && !xsk_page_aligned(umem_area))
		return -EINVAL;

	umem = calloc(1, sizeof(*umem));
	if (!umem)
		return -ENOMEM;

	umem->fd = socket(AF_XDP, SOCK_RAW, 0);
	if (umem->fd < 0) {
		err = -errno;
		goto out_umem_alloc;
	}

	umem->umem_area = umem_area;
	xsk_set_umem_config(&umem->config, usr_config);

	mr.addr = (uintptr_t)umem_area;
	mr.len = size;
	mr.chunk_size = umem->config.frame_size;
	mr.headroom = umem->config.frame_headroom;

	err = setsockopt(umem->fd, SOL_XDP, XDP_UMEM_REG, &mr, sizeof(mr));
	if (err) {
		err = -errno;
		goto out_socket;
	}
	err = setsockopt(umem->fd, SOL_XDP, XDP_UMEM_FILL_RING,
			 &umem->config.fill_size,
			 sizeof(umem->config.fill_size));
	if (err) {
		err = -errno;
		goto out_socket;
	}
	err = setsockopt(umem->fd, SOL_XDP, XDP_UMEM_COMPLETION_RING,
			 &umem->config.comp_size,
			 sizeof(umem->config.comp_size));
	if (err) {
		err = -errno;
		goto out_socket;
	}

	optlen = sizeof(off);
	err = getsockopt(umem->fd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
	if (err) {
		err = -errno;
		goto out_socket;
	}

	map = xsk_mmap(NULL, off.fr.desc +
		       umem->config.fill_size * sizeof(__u64),
		       PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
		       umem->fd, XDP_UMEM_PGOFF_FILL_RING);
	if (map == MAP_FAILED) {
		err = -errno;
		goto out_socket;
	}

	umem->fill = fill;
	fill->mask = umem->config.fill_size - 1;
	fill->size = umem->config.fill_size;
	fill->producer = map + off.fr.producer;
	fill->consumer = map + off.fr.consumer;
	fill->ring = map + off.fr.desc;
	fill->cached_cons = umem->config.fill_size;

	map = xsk_mmap(NULL,
		       off.cr.desc + umem->config.comp_size * sizeof(__u64),
		       PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
		       umem->fd, XDP_UMEM_PGOFF_COMPLETION_RING);
	if (map == MAP_FAILED) {
		err = -errno;
		goto out_mmap;
	}

	umem->comp = comp;
	comp->mask = umem->config.comp_size - 1;
	comp->size = umem->config.comp_size;
	comp->producer = map + off.cr.producer;
	comp->consumer = map + off.cr.consumer;
	comp->ring = map + off.cr.desc;

	*umem_ptr = umem;
	return 0;

out_mmap:
	munmap(map, off.fr.desc + umem->config.fill_size * sizeof(__u64));
out_socket:
	close(umem->fd);
out_umem_alloc:
	free(umem);
	return err;
}

static int xsk_load_xdp_prog(struct xsk_socket *xsk)
{
	static const int log_buf_size = 16 * 1024;
	char log_buf[log_buf_size];
	int err, prog_fd;

	/* This is the C-program:
	 * SEC("xdp_sock") int xdp_sock_prog(struct xdp_md *ctx)
	 * {
	 *     int *qidconf, index = ctx->rx_queue_index;
	 *
	 *     // A set entry here means that the correspnding queue_id
	 *     // has an active AF_XDP socket bound to it.
	 *     qidconf = bpf_map_lookup_elem(&qidconf_map, &index);
	 *     if (!qidconf)
	 *         return XDP_ABORTED;
	 *
	 *     if (*qidconf)
	 *         return bpf_redirect_map(&xsks_map, index, 0);
	 *
	 *     return XDP_PASS;
	 * }
	 */
	struct bpf_insn prog[] = {
		/* r1 = *(u32 *)(r1 + 16) */
		BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_1, 16),
		/* *(u32 *)(r10 - 4) = r1 */
		BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_1, -4),
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
		BPF_LD_MAP_FD(BPF_REG_1, xsk->qidconf_map_fd),
		BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
		BPF_MOV32_IMM(BPF_REG_0, 0),
		/* if r1 == 0 goto +8 */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 8),
		BPF_MOV32_IMM(BPF_REG_0, 2),
		/* r1 = *(u32 *)(r1 + 0) */
		BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_1, 0),
		/* if r1 == 0 goto +5 */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 5),
		/* r2 = *(u32 *)(r10 - 4) */
		BPF_LD_MAP_FD(BPF_REG_1, xsk->xsks_map_fd),
		BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_10, -4),
		BPF_MOV32_IMM(BPF_REG_3, 0),
		BPF_EMIT_CALL(BPF_FUNC_redirect_map),
		/* The jumps are to this instruction */
		BPF_EXIT_INSN(),
	};
	size_t insns_cnt = sizeof(prog) / sizeof(struct bpf_insn);

	prog_fd = bpf_load_program(BPF_PROG_TYPE_XDP, prog, insns_cnt,
				   "LGPL-2.1 or BSD-2-Clause", 0, log_buf,
				   log_buf_size);
	if (prog_fd < 0) {
		pr_warning("BPF log buffer:\n%s", log_buf);
		return prog_fd;
	}

	err = bpf_set_link_xdp_fd(xsk->ifindex, prog_fd, xsk->config.xdp_flags);
	if (err) {
		close(prog_fd);
		return err;
	}

	xsk->prog_fd = prog_fd;
	return 0;
}

static int xsk_get_max_queues(struct xsk_socket *xsk)
{
	struct ethtool_channels channels;
	struct ifreq ifr;
	int fd, err, ret;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -errno;

	channels.cmd = ETHTOOL_GCHANNELS;
	ifr.ifr_data = (void *)&channels;
	strncpy(ifr.ifr_name, xsk->ifname, IFNAMSIZ);
	err = ioctl(fd, SIOCETHTOOL, &ifr);
	if (err && errno != EOPNOTSUPP) {
		ret = -errno;
		goto out;
	}

	if (channels.max_combined == 0 || errno == EOPNOTSUPP)
		/* If the device says it has no channels, then all traffic
		 * is sent to a single stream, so max queues = 1.
		 */
		ret = 1;
	else
		ret = channels.max_combined;

out:
	close(fd);
	return ret;
}

static int xsk_create_bpf_maps(struct xsk_socket *xsk)
{
	int max_queues;
	int fd;

	max_queues = xsk_get_max_queues(xsk);
	if (max_queues < 0)
		return max_queues;

	fd = bpf_create_map_name(BPF_MAP_TYPE_ARRAY, "qidconf_map",
				 sizeof(int), sizeof(int), max_queues, 0);
	if (fd < 0)
		return fd;
	xsk->qidconf_map_fd = fd;

	fd = bpf_create_map_name(BPF_MAP_TYPE_XSKMAP, "xsks_map",
				 sizeof(int), sizeof(int), max_queues, 0);
	if (fd < 0) {
		close(xsk->qidconf_map_fd);
		return fd;
	}
	xsk->xsks_map_fd = fd;

	return 0;
}

static void xsk_delete_bpf_maps(struct xsk_socket *xsk)
{
	close(xsk->qidconf_map_fd);
	close(xsk->xsks_map_fd);
	xsk->qidconf_map_fd = -1;
	xsk->xsks_map_fd = -1;
}

static int xsk_lookup_bpf_maps(struct xsk_socket *xsk)
{
	__u32 i, *map_ids, num_maps, prog_len = sizeof(struct bpf_prog_info);
	__u32 map_len = sizeof(struct bpf_map_info);
	struct bpf_prog_info prog_info = {};
	struct bpf_map_info map_info;
	int fd, err;

	err = bpf_obj_get_info_by_fd(xsk->prog_fd, &prog_info, &prog_len);
	if (err)
		return err;

	num_maps = prog_info.nr_map_ids;

	map_ids = calloc(prog_info.nr_map_ids, sizeof(*map_ids));
	if (!map_ids)
		return -ENOMEM;

	memset(&prog_info, 0, prog_len);
	prog_info.nr_map_ids = num_maps;
	prog_info.map_ids = (__u64)(unsigned long)map_ids;

	err = bpf_obj_get_info_by_fd(xsk->prog_fd, &prog_info, &prog_len);
	if (err)
		goto out_map_ids;

	for (i = 0; i < prog_info.nr_map_ids; i++) {
		if (xsk->qidconf_map_fd != -1 && xsk->xsks_map_fd != -1)
			break;

		fd = bpf_map_get_fd_by_id(map_ids[i]);
		if (fd < 0)
			continue;

		err = bpf_obj_get_info_by_fd(fd, &map_info, &map_len);
		if (err) {
			close(fd);
			continue;
		}

		if (!strcmp(map_info.name, "qidconf_map")) {
			xsk->qidconf_map_fd = fd;
			continue;
		}

		if (!strcmp(map_info.name, "xsks_map")) {
			xsk->xsks_map_fd = fd;
			continue;
		}

		close(fd);
	}

	err = 0;
	if (xsk->qidconf_map_fd < 0 || xsk->xsks_map_fd < 0) {
		err = -ENOENT;
		xsk_delete_bpf_maps(xsk);
	}

out_map_ids:
	free(map_ids);
	return err;
}

static void xsk_clear_bpf_maps(struct xsk_socket *xsk)
{
	int qid = false;

	bpf_map_update_elem(xsk->qidconf_map_fd, &xsk->queue_id, &qid, 0);
	bpf_map_delete_elem(xsk->xsks_map_fd, &xsk->queue_id);
}

static int xsk_set_bpf_maps(struct xsk_socket *xsk)
{
	int qid = true, fd = xsk->fd, err;

	err = bpf_map_update_elem(xsk->qidconf_map_fd, &xsk->queue_id, &qid, 0);
	if (err)
		goto out;

	err = bpf_map_update_elem(xsk->xsks_map_fd, &xsk->queue_id, &fd, 0);
	if (err)
		goto out;

	return 0;
out:
	xsk_clear_bpf_maps(xsk);
	return err;
}

static int xsk_setup_xdp_prog(struct xsk_socket *xsk)
{
	__u32 prog_id = 0;
	int err;

	err = bpf_get_link_xdp_id(xsk->ifindex, &prog_id,
				  xsk->config.xdp_flags);
	if (err)
		return err;

	if (!prog_id) {
		err = xsk_create_bpf_maps(xsk);
		if (err)
			return err;

		err = xsk_load_xdp_prog(xsk);
		if (err)
			goto out_maps;
	} else {
		xsk->prog_fd = bpf_prog_get_fd_by_id(prog_id);
		err = xsk_lookup_bpf_maps(xsk);
		if (err)
			goto out_load;
	}

	err = xsk_set_bpf_maps(xsk);
	if (err)
		goto out_load;

	return 0;

out_load:
	close(xsk->prog_fd);
out_maps:
	xsk_delete_bpf_maps(xsk);
	return err;
}

int xsk_socket__create(struct xsk_socket **xsk_ptr, const char *ifname,
		       __u32 queue_id, struct xsk_umem *umem,
		       struct xsk_ring_cons *rx, struct xsk_ring_prod *tx,
		       const struct xsk_socket_config *usr_config)
{
	void *rx_map = NULL, *tx_map = NULL;
	struct sockaddr_xdp sxdp = {};
	struct xdp_mmap_offsets off;
	struct xsk_socket *xsk;
	socklen_t optlen;
	int err;

	if (!umem || !xsk_ptr || !rx || !tx)
		return -EFAULT;

	if (umem->refcount) {
		pr_warning("Error: shared umems not supported by libbpf.\n");
		return -EBUSY;
	}

	xsk = calloc(1, sizeof(*xsk));
	if (!xsk)
		return -ENOMEM;

	if (umem->refcount++ > 0) {
		xsk->fd = socket(AF_XDP, SOCK_RAW, 0);
		if (xsk->fd < 0) {
			err = -errno;
			goto out_xsk_alloc;
		}
	} else {
		xsk->fd = umem->fd;
	}

	xsk->outstanding_tx = 0;
	xsk->queue_id = queue_id;
	xsk->umem = umem;
	xsk->ifindex = if_nametoindex(ifname);
	if (!xsk->ifindex) {
		err = -errno;
		goto out_socket;
	}
	strncpy(xsk->ifname, ifname, IFNAMSIZ);

	err = xsk_set_xdp_socket_config(&xsk->config, usr_config);
	if (err)
		goto out_socket;

	if (rx) {
		err = setsockopt(xsk->fd, SOL_XDP, XDP_RX_RING,
				 &xsk->config.rx_size,
				 sizeof(xsk->config.rx_size));
		if (err) {
			err = -errno;
			goto out_socket;
		}
	}
	if (tx) {
		err = setsockopt(xsk->fd, SOL_XDP, XDP_TX_RING,
				 &xsk->config.tx_size,
				 sizeof(xsk->config.tx_size));
		if (err) {
			err = -errno;
			goto out_socket;
		}
	}

	optlen = sizeof(off);
	err = getsockopt(xsk->fd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
	if (err) {
		err = -errno;
		goto out_socket;
	}

	if (rx) {
		rx_map = xsk_mmap(NULL, off.rx.desc +
				  xsk->config.rx_size * sizeof(struct xdp_desc),
				  PROT_READ | PROT_WRITE,
				  MAP_SHARED | MAP_POPULATE,
				  xsk->fd, XDP_PGOFF_RX_RING);
		if (rx_map == MAP_FAILED) {
			err = -errno;
			goto out_socket;
		}

		rx->mask = xsk->config.rx_size - 1;
		rx->size = xsk->config.rx_size;
		rx->producer = rx_map + off.rx.producer;
		rx->consumer = rx_map + off.rx.consumer;
		rx->ring = rx_map + off.rx.desc;
	}
	xsk->rx = rx;

	if (tx) {
		tx_map = xsk_mmap(NULL, off.tx.desc +
				  xsk->config.tx_size * sizeof(struct xdp_desc),
				  PROT_READ | PROT_WRITE,
				  MAP_SHARED | MAP_POPULATE,
				  xsk->fd, XDP_PGOFF_TX_RING);
		if (tx_map == MAP_FAILED) {
			err = -errno;
			goto out_mmap_rx;
		}

		tx->mask = xsk->config.tx_size - 1;
		tx->size = xsk->config.tx_size;
		tx->producer = tx_map + off.tx.producer;
		tx->consumer = tx_map + off.tx.consumer;
		tx->ring = tx_map + off.tx.desc;
		tx->cached_cons = xsk->config.tx_size;
	}
	xsk->tx = tx;

	sxdp.sxdp_family = PF_XDP;
	sxdp.sxdp_ifindex = xsk->ifindex;
	sxdp.sxdp_queue_id = xsk->queue_id;
	sxdp.sxdp_flags = xsk->config.bind_flags;

	err = bind(xsk->fd, (struct sockaddr *)&sxdp, sizeof(sxdp));
	if (err) {
		err = -errno;
		goto out_mmap_tx;
	}

	xsk->qidconf_map_fd = -1;
	xsk->xsks_map_fd = -1;

	if (!(xsk->config.libbpf_flags & XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD)) {
		err = xsk_setup_xdp_prog(xsk);
		if (err)
			goto out_mmap_tx;
	}

	*xsk_ptr = xsk;
	return 0;

out_mmap_tx:
	if (tx)
		munmap(tx_map, off.tx.desc +
		       xsk->config.tx_size * sizeof(struct xdp_desc));
out_mmap_rx:
	if (rx)
		munmap(rx_map, off.rx.desc +
		       xsk->config.rx_size * sizeof(struct xdp_desc));
out_socket:
	if (--umem->refcount)
		close(xsk->fd);
out_xsk_alloc:
	free(xsk);
	return err;
}

int xsk_umem__delete(struct xsk_umem *umem)
{
	struct xdp_mmap_offsets off;
	socklen_t optlen;
	int err;

	if (!umem)
		return 0;

	if (umem->refcount)
		return -EBUSY;

	optlen = sizeof(off);
	err = getsockopt(umem->fd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
	if (!err) {
		munmap(umem->fill->ring - off.fr.desc,
		       off.fr.desc + umem->config.fill_size * sizeof(__u64));
		munmap(umem->comp->ring - off.cr.desc,
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
	socklen_t optlen;
	int err;

	if (!xsk)
		return;

	xsk_clear_bpf_maps(xsk);
	xsk_delete_bpf_maps(xsk);

	optlen = sizeof(off);
	err = getsockopt(xsk->fd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
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

	xsk->umem->refcount--;
	/* Do not close an fd that also has an associated umem connected
	 * to it.
	 */
	if (xsk->fd != xsk->umem->fd)
		close(xsk->fd);
	free(xsk);
}
