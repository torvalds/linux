// SPDX-License-Identifier: GPL-2.0-only
/* net/core/xdp.c
 *
 * Copyright (c) 2017 Jesper Dangaard Brouer, Red Hat Inc.
 */
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/rhashtable.h>
#include <linux/bug.h>
#include <net/page_pool.h>

#include <net/xdp.h>
#include <net/xdp_priv.h> /* struct xdp_mem_allocator */
#include <trace/events/xdp.h>
#include <net/xdp_sock_drv.h>

#define REG_STATE_NEW		0x0
#define REG_STATE_REGISTERED	0x1
#define REG_STATE_UNREGISTERED	0x2
#define REG_STATE_UNUSED	0x3

static DEFINE_IDA(mem_id_pool);
static DEFINE_MUTEX(mem_id_lock);
#define MEM_ID_MAX 0xFFFE
#define MEM_ID_MIN 1
static int mem_id_next = MEM_ID_MIN;

static bool mem_id_init; /* false */
static struct rhashtable *mem_id_ht;

static u32 xdp_mem_id_hashfn(const void *data, u32 len, u32 seed)
{
	const u32 *k = data;
	const u32 key = *k;

	BUILD_BUG_ON(sizeof_field(struct xdp_mem_allocator, mem.id)
		     != sizeof(u32));

	/* Use cyclic increasing ID as direct hash key */
	return key;
}

static int xdp_mem_id_cmp(struct rhashtable_compare_arg *arg,
			  const void *ptr)
{
	const struct xdp_mem_allocator *xa = ptr;
	u32 mem_id = *(u32 *)arg->key;

	return xa->mem.id != mem_id;
}

static const struct rhashtable_params mem_id_rht_params = {
	.nelem_hint = 64,
	.head_offset = offsetof(struct xdp_mem_allocator, node),
	.key_offset  = offsetof(struct xdp_mem_allocator, mem.id),
	.key_len = sizeof_field(struct xdp_mem_allocator, mem.id),
	.max_size = MEM_ID_MAX,
	.min_size = 8,
	.automatic_shrinking = true,
	.hashfn    = xdp_mem_id_hashfn,
	.obj_cmpfn = xdp_mem_id_cmp,
};

static void __xdp_mem_allocator_rcu_free(struct rcu_head *rcu)
{
	struct xdp_mem_allocator *xa;

	xa = container_of(rcu, struct xdp_mem_allocator, rcu);

	/* Allow this ID to be reused */
	ida_simple_remove(&mem_id_pool, xa->mem.id);

	kfree(xa);
}

static void mem_xa_remove(struct xdp_mem_allocator *xa)
{
	trace_mem_disconnect(xa);

	if (!rhashtable_remove_fast(mem_id_ht, &xa->node, mem_id_rht_params))
		call_rcu(&xa->rcu, __xdp_mem_allocator_rcu_free);
}

static void mem_allocator_disconnect(void *allocator)
{
	struct xdp_mem_allocator *xa;
	struct rhashtable_iter iter;

	mutex_lock(&mem_id_lock);

	rhashtable_walk_enter(mem_id_ht, &iter);
	do {
		rhashtable_walk_start(&iter);

		while ((xa = rhashtable_walk_next(&iter)) && !IS_ERR(xa)) {
			if (xa->allocator == allocator)
				mem_xa_remove(xa);
		}

		rhashtable_walk_stop(&iter);

	} while (xa == ERR_PTR(-EAGAIN));
	rhashtable_walk_exit(&iter);

	mutex_unlock(&mem_id_lock);
}

void xdp_rxq_info_unreg_mem_model(struct xdp_rxq_info *xdp_rxq)
{
	struct xdp_mem_allocator *xa;
	int id = xdp_rxq->mem.id;

	if (xdp_rxq->reg_state != REG_STATE_REGISTERED) {
		WARN(1, "Missing register, driver bug");
		return;
	}

	if (id == 0)
		return;

	if (xdp_rxq->mem.type == MEM_TYPE_PAGE_POOL) {
		rcu_read_lock();
		xa = rhashtable_lookup(mem_id_ht, &id, mem_id_rht_params);
		page_pool_destroy(xa->page_pool);
		rcu_read_unlock();
	}
}
EXPORT_SYMBOL_GPL(xdp_rxq_info_unreg_mem_model);

void xdp_rxq_info_unreg(struct xdp_rxq_info *xdp_rxq)
{
	/* Simplify driver cleanup code paths, allow unreg "unused" */
	if (xdp_rxq->reg_state == REG_STATE_UNUSED)
		return;

	WARN(!(xdp_rxq->reg_state == REG_STATE_REGISTERED), "Driver BUG");

	xdp_rxq_info_unreg_mem_model(xdp_rxq);

	xdp_rxq->reg_state = REG_STATE_UNREGISTERED;
	xdp_rxq->dev = NULL;

	/* Reset mem info to defaults */
	xdp_rxq->mem.id = 0;
	xdp_rxq->mem.type = 0;
}
EXPORT_SYMBOL_GPL(xdp_rxq_info_unreg);

static void xdp_rxq_info_init(struct xdp_rxq_info *xdp_rxq)
{
	memset(xdp_rxq, 0, sizeof(*xdp_rxq));
}

/* Returns 0 on success, negative on failure */
int xdp_rxq_info_reg(struct xdp_rxq_info *xdp_rxq,
		     struct net_device *dev, u32 queue_index)
{
	if (xdp_rxq->reg_state == REG_STATE_UNUSED) {
		WARN(1, "Driver promised not to register this");
		return -EINVAL;
	}

	if (xdp_rxq->reg_state == REG_STATE_REGISTERED) {
		WARN(1, "Missing unregister, handled but fix driver");
		xdp_rxq_info_unreg(xdp_rxq);
	}

	if (!dev) {
		WARN(1, "Missing net_device from driver");
		return -ENODEV;
	}

	/* State either UNREGISTERED or NEW */
	xdp_rxq_info_init(xdp_rxq);
	xdp_rxq->dev = dev;
	xdp_rxq->queue_index = queue_index;

	xdp_rxq->reg_state = REG_STATE_REGISTERED;
	return 0;
}
EXPORT_SYMBOL_GPL(xdp_rxq_info_reg);

void xdp_rxq_info_unused(struct xdp_rxq_info *xdp_rxq)
{
	xdp_rxq->reg_state = REG_STATE_UNUSED;
}
EXPORT_SYMBOL_GPL(xdp_rxq_info_unused);

bool xdp_rxq_info_is_reg(struct xdp_rxq_info *xdp_rxq)
{
	return (xdp_rxq->reg_state == REG_STATE_REGISTERED);
}
EXPORT_SYMBOL_GPL(xdp_rxq_info_is_reg);

static int __mem_id_init_hash_table(void)
{
	struct rhashtable *rht;
	int ret;

	if (unlikely(mem_id_init))
		return 0;

	rht = kzalloc(sizeof(*rht), GFP_KERNEL);
	if (!rht)
		return -ENOMEM;

	ret = rhashtable_init(rht, &mem_id_rht_params);
	if (ret < 0) {
		kfree(rht);
		return ret;
	}
	mem_id_ht = rht;
	smp_mb(); /* mutex lock should provide enough pairing */
	mem_id_init = true;

	return 0;
}

/* Allocate a cyclic ID that maps to allocator pointer.
 * See: https://www.kernel.org/doc/html/latest/core-api/idr.html
 *
 * Caller must lock mem_id_lock.
 */
static int __mem_id_cyclic_get(gfp_t gfp)
{
	int retries = 1;
	int id;

again:
	id = ida_simple_get(&mem_id_pool, mem_id_next, MEM_ID_MAX, gfp);
	if (id < 0) {
		if (id == -ENOSPC) {
			/* Cyclic allocator, reset next id */
			if (retries--) {
				mem_id_next = MEM_ID_MIN;
				goto again;
			}
		}
		return id; /* errno */
	}
	mem_id_next = id + 1;

	return id;
}

static bool __is_supported_mem_type(enum xdp_mem_type type)
{
	if (type == MEM_TYPE_PAGE_POOL)
		return is_page_pool_compiled_in();

	if (type >= MEM_TYPE_MAX)
		return false;

	return true;
}

int xdp_rxq_info_reg_mem_model(struct xdp_rxq_info *xdp_rxq,
			       enum xdp_mem_type type, void *allocator)
{
	struct xdp_mem_allocator *xdp_alloc;
	gfp_t gfp = GFP_KERNEL;
	int id, errno, ret;
	void *ptr;

	if (xdp_rxq->reg_state != REG_STATE_REGISTERED) {
		WARN(1, "Missing register, driver bug");
		return -EFAULT;
	}

	if (!__is_supported_mem_type(type))
		return -EOPNOTSUPP;

	xdp_rxq->mem.type = type;

	if (!allocator) {
		if (type == MEM_TYPE_PAGE_POOL)
			return -EINVAL; /* Setup time check page_pool req */
		return 0;
	}

	/* Delay init of rhashtable to save memory if feature isn't used */
	if (!mem_id_init) {
		mutex_lock(&mem_id_lock);
		ret = __mem_id_init_hash_table();
		mutex_unlock(&mem_id_lock);
		if (ret < 0) {
			WARN_ON(1);
			return ret;
		}
	}

	xdp_alloc = kzalloc(sizeof(*xdp_alloc), gfp);
	if (!xdp_alloc)
		return -ENOMEM;

	mutex_lock(&mem_id_lock);
	id = __mem_id_cyclic_get(gfp);
	if (id < 0) {
		errno = id;
		goto err;
	}
	xdp_rxq->mem.id = id;
	xdp_alloc->mem  = xdp_rxq->mem;
	xdp_alloc->allocator = allocator;

	/* Insert allocator into ID lookup table */
	ptr = rhashtable_insert_slow(mem_id_ht, &id, &xdp_alloc->node);
	if (IS_ERR(ptr)) {
		ida_simple_remove(&mem_id_pool, xdp_rxq->mem.id);
		xdp_rxq->mem.id = 0;
		errno = PTR_ERR(ptr);
		goto err;
	}

	if (type == MEM_TYPE_PAGE_POOL)
		page_pool_use_xdp_mem(allocator, mem_allocator_disconnect);

	mutex_unlock(&mem_id_lock);

	trace_mem_connect(xdp_alloc, xdp_rxq);
	return 0;
err:
	mutex_unlock(&mem_id_lock);
	kfree(xdp_alloc);
	return errno;
}
EXPORT_SYMBOL_GPL(xdp_rxq_info_reg_mem_model);

/* XDP RX runs under NAPI protection, and in different delivery error
 * scenarios (e.g. queue full), it is possible to return the xdp_frame
 * while still leveraging this protection.  The @napi_direct boolean
 * is used for those calls sites.  Thus, allowing for faster recycling
 * of xdp_frames/pages in those cases.
 */
static void __xdp_return(void *data, struct xdp_mem_info *mem, bool napi_direct,
			 struct xdp_buff *xdp)
{
	struct xdp_mem_allocator *xa;
	struct page *page;

	switch (mem->type) {
	case MEM_TYPE_PAGE_POOL:
		rcu_read_lock();
		/* mem->id is valid, checked in xdp_rxq_info_reg_mem_model() */
		xa = rhashtable_lookup(mem_id_ht, &mem->id, mem_id_rht_params);
		page = virt_to_head_page(data);
		napi_direct &= !xdp_return_frame_no_direct();
		page_pool_put_full_page(xa->page_pool, page, napi_direct);
		rcu_read_unlock();
		break;
	case MEM_TYPE_PAGE_SHARED:
		page_frag_free(data);
		break;
	case MEM_TYPE_PAGE_ORDER0:
		page = virt_to_page(data); /* Assumes order0 page*/
		put_page(page);
		break;
	case MEM_TYPE_XSK_BUFF_POOL:
		/* NB! Only valid from an xdp_buff! */
		xsk_buff_free(xdp);
		break;
	default:
		/* Not possible, checked in xdp_rxq_info_reg_mem_model() */
		WARN(1, "Incorrect XDP memory type (%d) usage", mem->type);
		break;
	}
}

void xdp_return_frame(struct xdp_frame *xdpf)
{
	__xdp_return(xdpf->data, &xdpf->mem, false, NULL);
}
EXPORT_SYMBOL_GPL(xdp_return_frame);

void xdp_return_frame_rx_napi(struct xdp_frame *xdpf)
{
	__xdp_return(xdpf->data, &xdpf->mem, true, NULL);
}
EXPORT_SYMBOL_GPL(xdp_return_frame_rx_napi);

void xdp_return_buff(struct xdp_buff *xdp)
{
	__xdp_return(xdp->data, &xdp->rxq->mem, true, xdp);
}

/* Only called for MEM_TYPE_PAGE_POOL see xdp.h */
void __xdp_release_frame(void *data, struct xdp_mem_info *mem)
{
	struct xdp_mem_allocator *xa;
	struct page *page;

	rcu_read_lock();
	xa = rhashtable_lookup(mem_id_ht, &mem->id, mem_id_rht_params);
	page = virt_to_head_page(data);
	if (xa)
		page_pool_release_page(xa->page_pool, page);
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(__xdp_release_frame);

void xdp_attachment_setup(struct xdp_attachment_info *info,
			  struct netdev_bpf *bpf)
{
	if (info->prog)
		bpf_prog_put(info->prog);
	info->prog = bpf->prog;
	info->flags = bpf->flags;
}
EXPORT_SYMBOL_GPL(xdp_attachment_setup);

struct xdp_frame *xdp_convert_zc_to_xdp_frame(struct xdp_buff *xdp)
{
	unsigned int metasize, totsize;
	void *addr, *data_to_copy;
	struct xdp_frame *xdpf;
	struct page *page;

	/* Clone into a MEM_TYPE_PAGE_ORDER0 xdp_frame. */
	metasize = xdp_data_meta_unsupported(xdp) ? 0 :
		   xdp->data - xdp->data_meta;
	totsize = xdp->data_end - xdp->data + metasize;

	if (sizeof(*xdpf) + totsize > PAGE_SIZE)
		return NULL;

	page = dev_alloc_page();
	if (!page)
		return NULL;

	addr = page_to_virt(page);
	xdpf = addr;
	memset(xdpf, 0, sizeof(*xdpf));

	addr += sizeof(*xdpf);
	data_to_copy = metasize ? xdp->data_meta : xdp->data;
	memcpy(addr, data_to_copy, totsize);

	xdpf->data = addr + metasize;
	xdpf->len = totsize - metasize;
	xdpf->headroom = 0;
	xdpf->metasize = metasize;
	xdpf->frame_sz = PAGE_SIZE;
	xdpf->mem.type = MEM_TYPE_PAGE_ORDER0;

	xsk_buff_free(xdp);
	return xdpf;
}
EXPORT_SYMBOL_GPL(xdp_convert_zc_to_xdp_frame);

/* Used by XDP_WARN macro, to avoid inlining WARN() in fast-path */
void xdp_warn(const char *msg, const char *func, const int line)
{
	WARN(1, "XDP_WARN: %s(line:%d): %s\n", func, line, msg);
};
EXPORT_SYMBOL_GPL(xdp_warn);
