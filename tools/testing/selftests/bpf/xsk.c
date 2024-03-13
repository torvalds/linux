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
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/if_link.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
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

#define pr_warn(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)

enum xsk_prog {
	XSK_PROG_FALLBACK,
	XSK_PROG_REDIRECT_FLAGS,
};

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
	int prog_fd;
	int link_fd;
	int xsks_map_fd;
	char ifname[IFNAMSIZ];
	bool has_bpf_link;
};

struct xsk_socket {
	struct xsk_ring_cons *rx;
	struct xsk_ring_prod *tx;
	__u64 outstanding_tx;
	struct xsk_ctx *ctx;
	struct xsk_socket_config config;
	int fd;
};

struct xsk_nl_info {
	bool xdp_prog_attached;
	int ifindex;
	int fd;
};

/* Up until and including Linux 5.3 */
struct xdp_ring_offset_v1 {
	__u64 producer;
	__u64 consumer;
	__u64 desc;
};

/* Up until and including Linux 5.3 */
struct xdp_mmap_offsets_v1 {
	struct xdp_ring_offset_v1 rx;
	struct xdp_ring_offset_v1 tx;
	struct xdp_ring_offset_v1 fr;
	struct xdp_ring_offset_v1 cr;
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
		return;
	}

	cfg->fill_size = usr_cfg->fill_size;
	cfg->comp_size = usr_cfg->comp_size;
	cfg->frame_size = usr_cfg->frame_size;
	cfg->frame_headroom = usr_cfg->frame_headroom;
	cfg->flags = usr_cfg->flags;
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

static void xsk_mmap_offsets_v1(struct xdp_mmap_offsets *off)
{
	struct xdp_mmap_offsets_v1 off_v1;

	/* getsockopt on a kernel <= 5.3 has no flags fields.
	 * Copy over the offsets to the correct places in the >=5.4 format
	 * and put the flags where they would have been on that kernel.
	 */
	memcpy(&off_v1, off, sizeof(off_v1));

	off->rx.producer = off_v1.rx.producer;
	off->rx.consumer = off_v1.rx.consumer;
	off->rx.desc = off_v1.rx.desc;
	off->rx.flags = off_v1.rx.consumer + sizeof(__u32);

	off->tx.producer = off_v1.tx.producer;
	off->tx.consumer = off_v1.tx.consumer;
	off->tx.desc = off_v1.tx.desc;
	off->tx.flags = off_v1.tx.consumer + sizeof(__u32);

	off->fr.producer = off_v1.fr.producer;
	off->fr.consumer = off_v1.fr.consumer;
	off->fr.desc = off_v1.fr.desc;
	off->fr.flags = off_v1.fr.consumer + sizeof(__u32);

	off->cr.producer = off_v1.cr.producer;
	off->cr.consumer = off_v1.cr.consumer;
	off->cr.desc = off_v1.cr.desc;
	off->cr.flags = off_v1.cr.consumer + sizeof(__u32);
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

	if (optlen == sizeof(struct xdp_mmap_offsets_v1)) {
		xsk_mmap_offsets_v1(off);
		return 0;
	}

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

struct xsk_umem_config_v1 {
	__u32 fill_size;
	__u32 comp_size;
	__u32 frame_size;
	__u32 frame_headroom;
};

static enum xsk_prog get_xsk_prog(void)
{
	enum xsk_prog detected = XSK_PROG_FALLBACK;
	char data_in = 0, data_out;
	struct bpf_insn insns[] = {
		BPF_LD_MAP_FD(BPF_REG_1, 0),
		BPF_MOV64_IMM(BPF_REG_2, 0),
		BPF_MOV64_IMM(BPF_REG_3, XDP_PASS),
		BPF_EMIT_CALL(BPF_FUNC_redirect_map),
		BPF_EXIT_INSN(),
	};
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.data_in = &data_in,
		.data_size_in = 1,
		.data_out = &data_out,
	);

	int prog_fd, map_fd, ret, insn_cnt = ARRAY_SIZE(insns);

	map_fd = bpf_map_create(BPF_MAP_TYPE_XSKMAP, NULL, sizeof(int), sizeof(int), 1, NULL);
	if (map_fd < 0)
		return detected;

	insns[0].imm = map_fd;

	prog_fd = bpf_prog_load(BPF_PROG_TYPE_XDP, NULL, "GPL", insns, insn_cnt, NULL);
	if (prog_fd < 0) {
		close(map_fd);
		return detected;
	}

	ret = bpf_prog_test_run_opts(prog_fd, &opts);
	if (!ret && opts.retval == XDP_PASS)
		detected = XSK_PROG_REDIRECT_FLAGS;
	close(prog_fd);
	close(map_fd);
	return detected;
}

static int xsk_load_xdp_prog(struct xsk_socket *xsk)
{
	static const int log_buf_size = 16 * 1024;
	struct xsk_ctx *ctx = xsk->ctx;
	char log_buf[log_buf_size];
	int prog_fd;

	/* This is the fallback C-program:
	 * SEC("xdp_sock") int xdp_sock_prog(struct xdp_md *ctx)
	 * {
	 *     int ret, index = ctx->rx_queue_index;
	 *
	 *     // A set entry here means that the correspnding queue_id
	 *     // has an active AF_XDP socket bound to it.
	 *     ret = bpf_redirect_map(&xsks_map, index, XDP_PASS);
	 *     if (ret > 0)
	 *         return ret;
	 *
	 *     // Fallback for pre-5.3 kernels, not supporting default
	 *     // action in the flags parameter.
	 *     if (bpf_map_lookup_elem(&xsks_map, &index))
	 *         return bpf_redirect_map(&xsks_map, index, 0);
	 *     return XDP_PASS;
	 * }
	 */
	struct bpf_insn prog[] = {
		/* r2 = *(u32 *)(r1 + 16) */
		BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1, 16),
		/* *(u32 *)(r10 - 4) = r2 */
		BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_2, -4),
		/* r1 = xskmap[] */
		BPF_LD_MAP_FD(BPF_REG_1, ctx->xsks_map_fd),
		/* r3 = XDP_PASS */
		BPF_MOV64_IMM(BPF_REG_3, 2),
		/* call bpf_redirect_map */
		BPF_EMIT_CALL(BPF_FUNC_redirect_map),
		/* if w0 != 0 goto pc+13 */
		BPF_JMP32_IMM(BPF_JSGT, BPF_REG_0, 0, 13),
		/* r2 = r10 */
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		/* r2 += -4 */
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
		/* r1 = xskmap[] */
		BPF_LD_MAP_FD(BPF_REG_1, ctx->xsks_map_fd),
		/* call bpf_map_lookup_elem */
		BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
		/* r1 = r0 */
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
		/* r0 = XDP_PASS */
		BPF_MOV64_IMM(BPF_REG_0, 2),
		/* if r1 == 0 goto pc+5 */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 5),
		/* r2 = *(u32 *)(r10 - 4) */
		BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_10, -4),
		/* r1 = xskmap[] */
		BPF_LD_MAP_FD(BPF_REG_1, ctx->xsks_map_fd),
		/* r3 = 0 */
		BPF_MOV64_IMM(BPF_REG_3, 0),
		/* call bpf_redirect_map */
		BPF_EMIT_CALL(BPF_FUNC_redirect_map),
		/* The jumps are to this instruction */
		BPF_EXIT_INSN(),
	};

	/* This is the post-5.3 kernel C-program:
	 * SEC("xdp_sock") int xdp_sock_prog(struct xdp_md *ctx)
	 * {
	 *     return bpf_redirect_map(&xsks_map, ctx->rx_queue_index, XDP_PASS);
	 * }
	 */
	struct bpf_insn prog_redirect_flags[] = {
		/* r2 = *(u32 *)(r1 + 16) */
		BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1, 16),
		/* r1 = xskmap[] */
		BPF_LD_MAP_FD(BPF_REG_1, ctx->xsks_map_fd),
		/* r3 = XDP_PASS */
		BPF_MOV64_IMM(BPF_REG_3, 2),
		/* call bpf_redirect_map */
		BPF_EMIT_CALL(BPF_FUNC_redirect_map),
		BPF_EXIT_INSN(),
	};
	size_t insns_cnt[] = {ARRAY_SIZE(prog),
			      ARRAY_SIZE(prog_redirect_flags),
	};
	struct bpf_insn *progs[] = {prog, prog_redirect_flags};
	enum xsk_prog option = get_xsk_prog();
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		.log_buf = log_buf,
		.log_size = log_buf_size,
	);

	prog_fd = bpf_prog_load(BPF_PROG_TYPE_XDP, NULL, "LGPL-2.1 or BSD-2-Clause",
				progs[option], insns_cnt[option], &opts);
	if (prog_fd < 0) {
		pr_warn("BPF log buffer:\n%s", log_buf);
		return prog_fd;
	}

	ctx->prog_fd = prog_fd;
	return 0;
}

static int xsk_create_bpf_link(struct xsk_socket *xsk)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, opts);
	struct xsk_ctx *ctx = xsk->ctx;
	__u32 prog_id = 0;
	int link_fd;
	int err;

	err = bpf_xdp_query_id(ctx->ifindex, xsk->config.xdp_flags, &prog_id);
	if (err) {
		pr_warn("getting XDP prog id failed\n");
		return err;
	}

	/* if there's a netlink-based XDP prog loaded on interface, bail out
	 * and ask user to do the removal by himself
	 */
	if (prog_id) {
		pr_warn("Netlink-based XDP prog detected, please unload it in order to launch AF_XDP prog\n");
		return -EINVAL;
	}

	opts.flags = xsk->config.xdp_flags & ~(XDP_FLAGS_UPDATE_IF_NOEXIST | XDP_FLAGS_REPLACE);

	link_fd = bpf_link_create(ctx->prog_fd, ctx->ifindex, BPF_XDP, &opts);
	if (link_fd < 0) {
		pr_warn("bpf_link_create failed: %s\n", strerror(errno));
		return link_fd;
	}

	ctx->link_fd = link_fd;
	return 0;
}

/* Copy up to sz - 1 bytes from zero-terminated src string and ensure that dst
 * is zero-terminated string no matter what (unless sz == 0, in which case
 * it's a no-op). It's conceptually close to FreeBSD's strlcpy(), but differs
 * in what is returned. Given this is internal helper, it's trivial to extend
 * this, when necessary. Use this instead of strncpy inside libbpf source code.
 */
static inline void libbpf_strlcpy(char *dst, const char *src, size_t sz)
{
        size_t i;

        if (sz == 0)
                return;

        sz--;
        for (i = 0; i < sz && src[i]; i++)
                dst[i] = src[i];
        dst[i] = '\0';
}

static int xsk_get_max_queues(struct xsk_socket *xsk)
{
	struct ethtool_channels channels = { .cmd = ETHTOOL_GCHANNELS };
	struct xsk_ctx *ctx = xsk->ctx;
	struct ifreq ifr = {};
	int fd, err, ret;

	fd = socket(AF_LOCAL, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -errno;

	ifr.ifr_data = (void *)&channels;
	libbpf_strlcpy(ifr.ifr_name, ctx->ifname, IFNAMSIZ);
	err = ioctl(fd, SIOCETHTOOL, &ifr);
	if (err && errno != EOPNOTSUPP) {
		ret = -errno;
		goto out;
	}

	if (err) {
		/* If the device says it has no channels, then all traffic
		 * is sent to a single stream, so max queues = 1.
		 */
		ret = 1;
	} else {
		/* Take the max of rx, tx, combined. Drivers return
		 * the number of channels in different ways.
		 */
		ret = max(channels.max_rx, channels.max_tx);
		ret = max(ret, (int)channels.max_combined);
	}

out:
	close(fd);
	return ret;
}

static int xsk_create_bpf_maps(struct xsk_socket *xsk)
{
	struct xsk_ctx *ctx = xsk->ctx;
	int max_queues;
	int fd;

	max_queues = xsk_get_max_queues(xsk);
	if (max_queues < 0)
		return max_queues;

	fd = bpf_map_create(BPF_MAP_TYPE_XSKMAP, "xsks_map",
			    sizeof(int), sizeof(int), max_queues, NULL);
	if (fd < 0)
		return fd;

	ctx->xsks_map_fd = fd;

	return 0;
}

static void xsk_delete_bpf_maps(struct xsk_socket *xsk)
{
	struct xsk_ctx *ctx = xsk->ctx;

	bpf_map_delete_elem(ctx->xsks_map_fd, &ctx->queue_id);
	close(ctx->xsks_map_fd);
}

static int xsk_lookup_bpf_maps(struct xsk_socket *xsk)
{
	__u32 i, *map_ids, num_maps, prog_len = sizeof(struct bpf_prog_info);
	__u32 map_len = sizeof(struct bpf_map_info);
	struct bpf_prog_info prog_info = {};
	struct xsk_ctx *ctx = xsk->ctx;
	struct bpf_map_info map_info;
	int fd, err;

	err = bpf_obj_get_info_by_fd(ctx->prog_fd, &prog_info, &prog_len);
	if (err)
		return err;

	num_maps = prog_info.nr_map_ids;

	map_ids = calloc(prog_info.nr_map_ids, sizeof(*map_ids));
	if (!map_ids)
		return -ENOMEM;

	memset(&prog_info, 0, prog_len);
	prog_info.nr_map_ids = num_maps;
	prog_info.map_ids = (__u64)(unsigned long)map_ids;

	err = bpf_obj_get_info_by_fd(ctx->prog_fd, &prog_info, &prog_len);
	if (err)
		goto out_map_ids;

	ctx->xsks_map_fd = -1;

	for (i = 0; i < prog_info.nr_map_ids; i++) {
		fd = bpf_map_get_fd_by_id(map_ids[i]);
		if (fd < 0)
			continue;

		memset(&map_info, 0, map_len);
		err = bpf_obj_get_info_by_fd(fd, &map_info, &map_len);
		if (err) {
			close(fd);
			continue;
		}

		if (!strncmp(map_info.name, "xsks_map", sizeof(map_info.name))) {
			ctx->xsks_map_fd = fd;
			break;
		}

		close(fd);
	}

	if (ctx->xsks_map_fd == -1)
		err = -ENOENT;

out_map_ids:
	free(map_ids);
	return err;
}

static int xsk_set_bpf_maps(struct xsk_socket *xsk)
{
	struct xsk_ctx *ctx = xsk->ctx;

	return bpf_map_update_elem(ctx->xsks_map_fd, &ctx->queue_id,
				   &xsk->fd, 0);
}

static int xsk_link_lookup(int ifindex, __u32 *prog_id, int *link_fd)
{
	struct bpf_link_info link_info;
	__u32 link_len;
	__u32 id = 0;
	int err;
	int fd;

	while (true) {
		err = bpf_link_get_next_id(id, &id);
		if (err) {
			if (errno == ENOENT) {
				err = 0;
				break;
			}
			pr_warn("can't get next link: %s\n", strerror(errno));
			break;
		}

		fd = bpf_link_get_fd_by_id(id);
		if (fd < 0) {
			if (errno == ENOENT)
				continue;
			pr_warn("can't get link by id (%u): %s\n", id, strerror(errno));
			err = -errno;
			break;
		}

		link_len = sizeof(struct bpf_link_info);
		memset(&link_info, 0, link_len);
		err = bpf_obj_get_info_by_fd(fd, &link_info, &link_len);
		if (err) {
			pr_warn("can't get link info: %s\n", strerror(errno));
			close(fd);
			break;
		}
		if (link_info.type == BPF_LINK_TYPE_XDP) {
			if (link_info.xdp.ifindex == ifindex) {
				*link_fd = fd;
				if (prog_id)
					*prog_id = link_info.prog_id;
				break;
			}
		}
		close(fd);
	}

	return err;
}

static bool xsk_probe_bpf_link(void)
{
	LIBBPF_OPTS(bpf_link_create_opts, opts, .flags = XDP_FLAGS_SKB_MODE);
	struct bpf_insn insns[2] = {
		BPF_MOV64_IMM(BPF_REG_0, XDP_PASS),
		BPF_EXIT_INSN()
	};
	int prog_fd, link_fd = -1, insn_cnt = ARRAY_SIZE(insns);
	int ifindex_lo = 1;
	bool ret = false;
	int err;

	err = xsk_link_lookup(ifindex_lo, NULL, &link_fd);
	if (err)
		return ret;

	if (link_fd >= 0)
		return true;

	prog_fd = bpf_prog_load(BPF_PROG_TYPE_XDP, NULL, "GPL", insns, insn_cnt, NULL);
	if (prog_fd < 0)
		return ret;

	link_fd = bpf_link_create(prog_fd, ifindex_lo, BPF_XDP, &opts);
	close(prog_fd);

	if (link_fd >= 0) {
		ret = true;
		close(link_fd);
	}

	return ret;
}

static int xsk_create_xsk_struct(int ifindex, struct xsk_socket *xsk)
{
	char ifname[IFNAMSIZ];
	struct xsk_ctx *ctx;
	char *interface;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	interface = if_indextoname(ifindex, &ifname[0]);
	if (!interface) {
		free(ctx);
		return -errno;
	}

	ctx->ifindex = ifindex;
	libbpf_strlcpy(ctx->ifname, ifname, IFNAMSIZ);

	xsk->ctx = ctx;
	xsk->ctx->has_bpf_link = xsk_probe_bpf_link();

	return 0;
}

static int xsk_init_xdp_res(struct xsk_socket *xsk,
			    int *xsks_map_fd)
{
	struct xsk_ctx *ctx = xsk->ctx;
	int err;

	err = xsk_create_bpf_maps(xsk);
	if (err)
		return err;

	err = xsk_load_xdp_prog(xsk);
	if (err)
		goto err_load_xdp_prog;

	if (ctx->has_bpf_link)
		err = xsk_create_bpf_link(xsk);
	else
		err = bpf_xdp_attach(xsk->ctx->ifindex, ctx->prog_fd,
				     xsk->config.xdp_flags, NULL);

	if (err)
		goto err_attach_xdp_prog;

	if (!xsk->rx)
		return err;

	err = xsk_set_bpf_maps(xsk);
	if (err)
		goto err_set_bpf_maps;

	return err;

err_set_bpf_maps:
	if (ctx->has_bpf_link)
		close(ctx->link_fd);
	else
		bpf_xdp_detach(ctx->ifindex, 0, NULL);
err_attach_xdp_prog:
	close(ctx->prog_fd);
err_load_xdp_prog:
	xsk_delete_bpf_maps(xsk);
	return err;
}

static int xsk_lookup_xdp_res(struct xsk_socket *xsk, int *xsks_map_fd, int prog_id)
{
	struct xsk_ctx *ctx = xsk->ctx;
	int err;

	ctx->prog_fd = bpf_prog_get_fd_by_id(prog_id);
	if (ctx->prog_fd < 0) {
		err = -errno;
		goto err_prog_fd;
	}
	err = xsk_lookup_bpf_maps(xsk);
	if (err)
		goto err_lookup_maps;

	if (!xsk->rx)
		return err;

	err = xsk_set_bpf_maps(xsk);
	if (err)
		goto err_set_maps;

	return err;

err_set_maps:
	close(ctx->xsks_map_fd);
err_lookup_maps:
	close(ctx->prog_fd);
err_prog_fd:
	if (ctx->has_bpf_link)
		close(ctx->link_fd);
	return err;
}

static int __xsk_setup_xdp_prog(struct xsk_socket *_xdp, int *xsks_map_fd)
{
	struct xsk_socket *xsk = _xdp;
	struct xsk_ctx *ctx = xsk->ctx;
	__u32 prog_id = 0;
	int err;

	if (ctx->has_bpf_link)
		err = xsk_link_lookup(ctx->ifindex, &prog_id, &ctx->link_fd);
	else
		err = bpf_xdp_query_id(ctx->ifindex, xsk->config.xdp_flags, &prog_id);

	if (err)
		return err;

	err = !prog_id ? xsk_init_xdp_res(xsk, xsks_map_fd) :
			 xsk_lookup_xdp_res(xsk, xsks_map_fd, prog_id);

	if (!err && xsks_map_fd)
		*xsks_map_fd = ctx->xsks_map_fd;

	return err;
}

int xsk_setup_xdp_prog_xsk(struct xsk_socket *xsk, int *xsks_map_fd)
{
	return __xsk_setup_xdp_prog(xsk, xsks_map_fd);
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
				      const char *ifname, __u32 queue_id,
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
	libbpf_strlcpy(ctx->ifname, ifname, IFNAMSIZ);

	ctx->fill = fill;
	ctx->comp = comp;
	list_add(&ctx->list, &umem->ctx_list);
	ctx->has_bpf_link = xsk_probe_bpf_link();
	return ctx;
}

static void xsk_destroy_xsk_struct(struct xsk_socket *xsk)
{
	free(xsk->ctx);
	free(xsk);
}

int xsk_socket__update_xskmap(struct xsk_socket *xsk, int fd)
{
	xsk->ctx->xsks_map_fd = fd;
	return xsk_set_bpf_maps(xsk);
}

int xsk_setup_xdp_prog(int ifindex, int *xsks_map_fd)
{
	struct xsk_socket *xsk;
	int res;

	xsk = calloc(1, sizeof(*xsk));
	if (!xsk)
		return -ENOMEM;

	res = xsk_create_xsk_struct(ifindex, xsk);
	if (res) {
		free(xsk);
		return -EINVAL;
	}

	res = __xsk_setup_xdp_prog(xsk, xsks_map_fd);

	xsk_destroy_xsk_struct(xsk);

	return res;
}

int xsk_socket__create_shared(struct xsk_socket **xsk_ptr,
			      const char *ifname,
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
	int err, ifindex;

	if (!umem || !xsk_ptr || !(rx || tx))
		return -EFAULT;

	unmap = umem->fill_save != fill;

	xsk = calloc(1, sizeof(*xsk));
	if (!xsk)
		return -ENOMEM;

	err = xsk_set_xdp_socket_config(&xsk->config, usr_config);
	if (err)
		goto out_xsk_alloc;

	xsk->outstanding_tx = 0;
	ifindex = if_nametoindex(ifname);
	if (!ifindex) {
		err = -errno;
		goto out_xsk_alloc;
	}

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

		ctx = xsk_create_ctx(xsk, umem, ifindex, ifname, queue_id,
				     fill, comp);
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

	if (!(xsk->config.libbpf_flags & XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD)) {
		err = __xsk_setup_xdp_prog(xsk, NULL);
		if (err)
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

int xsk_socket__create(struct xsk_socket **xsk_ptr, const char *ifname,
		       __u32 queue_id, struct xsk_umem *umem,
		       struct xsk_ring_cons *rx, struct xsk_ring_prod *tx,
		       const struct xsk_socket_config *usr_config)
{
	if (!umem)
		return -EFAULT;

	return xsk_socket__create_shared(xsk_ptr, ifname, queue_id, umem,
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

	if (ctx->refcount == 1) {
		xsk_delete_bpf_maps(xsk);
		close(ctx->prog_fd);
		if (ctx->has_bpf_link)
			close(ctx->link_fd);
	}

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
