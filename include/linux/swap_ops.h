/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MM_SWAP_OPS_H
#define _MM_SWAP_OPS_H

#include <linux/swap.h> /* for SWAP_CLUSTER_MAX */

struct swap_iocb {
	union {
		struct kiocb	iocb;
		struct bio	bio;
	};
	struct bio_vec		bvecs[SWAP_CLUSTER_MAX];
	int			nr_bvecs;
	int			len;
};

struct swap_io_ctx {
	struct swap_iocb	*sio;
	struct swap_info_struct	*sis;
};

/*
 * SWAP_OPS_F_REQUIRE_NOFS:
 *	When set, all reclaim operations must operated as GFS_NOFS and not
 *	just GFP_NOIO, as GFP_NOIO allocations could recourse into the
 *	file system backing this swap file.
 */
#define SWAP_OPS_F_REQUIRE_NOFS		(1U << 0)

struct swap_ops {
	unsigned int		flags;

	bool (*can_merge)(struct folio *folio, struct folio *prev_folio,
			size_t prev_folio_size, int rw);
	void (*submit_write)(struct swap_io_ctx *ctx);
	void (*submit_read)(struct swap_io_ctx *ctx);
};

#endif /* _MM_SWAP_OPS_H */
