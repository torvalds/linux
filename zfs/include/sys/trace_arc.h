/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

#include <sys/list.h>

#if defined(_KERNEL) && defined(HAVE_DECLARE_EVENT_CLASS)

#undef TRACE_SYSTEM
#define	TRACE_SYSTEM zfs

#undef TRACE_SYSTEM_VAR
#define	TRACE_SYSTEM_VAR zfs_arc

#if !defined(_TRACE_ARC_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_ARC_H

#include <linux/tracepoint.h>
#include <sys/types.h>
#include <sys/trace_common.h> /* For ZIO macros */

/*
 * Generic support for one argument tracepoints of the form:
 *
 * DTRACE_PROBE1(...,
 *     arc_buf_hdr_t *, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_arc_buf_hdr_class,
	TP_PROTO(arc_buf_hdr_t *ab),
	TP_ARGS(ab),
	TP_STRUCT__entry(
	    __array(uint64_t,		hdr_dva_word, 2)
	    __field(uint64_t,		hdr_birth)
	    __field(uint32_t,		hdr_flags)
	    __field(uint32_t,		hdr_bufcnt)
	    __field(arc_buf_contents_t,	hdr_type)
	    __field(uint16_t,		hdr_psize)
	    __field(uint16_t,		hdr_lsize)
	    __field(uint64_t,		hdr_spa)
	    __field(arc_state_type_t,	hdr_state_type)
	    __field(clock_t,		hdr_access)
	    __field(uint32_t,		hdr_mru_hits)
	    __field(uint32_t,		hdr_mru_ghost_hits)
	    __field(uint32_t,		hdr_mfu_hits)
	    __field(uint32_t,		hdr_mfu_ghost_hits)
	    __field(uint32_t,		hdr_l2_hits)
	    __field(int64_t,		hdr_refcount)
	),
	TP_fast_assign(
	    __entry->hdr_dva_word[0]	= ab->b_dva.dva_word[0];
	    __entry->hdr_dva_word[1]	= ab->b_dva.dva_word[1];
	    __entry->hdr_birth		= ab->b_birth;
	    __entry->hdr_flags		= ab->b_flags;
	    __entry->hdr_bufcnt	= ab->b_l1hdr.b_bufcnt;
	    __entry->hdr_psize		= ab->b_psize;
	    __entry->hdr_lsize		= ab->b_lsize;
	    __entry->hdr_spa		= ab->b_spa;
	    __entry->hdr_state_type	= ab->b_l1hdr.b_state->arcs_state;
	    __entry->hdr_access		= ab->b_l1hdr.b_arc_access;
	    __entry->hdr_mru_hits	= ab->b_l1hdr.b_mru_hits;
	    __entry->hdr_mru_ghost_hits	= ab->b_l1hdr.b_mru_ghost_hits;
	    __entry->hdr_mfu_hits	= ab->b_l1hdr.b_mfu_hits;
	    __entry->hdr_mfu_ghost_hits	= ab->b_l1hdr.b_mfu_ghost_hits;
	    __entry->hdr_l2_hits	= ab->b_l1hdr.b_l2_hits;
	    __entry->hdr_refcount	= ab->b_l1hdr.b_refcnt.rc_count;
	),
	TP_printk("hdr { dva 0x%llx:0x%llx birth %llu "
	    "flags 0x%x bufcnt %u type %u psize %u lsize %u spa %llu "
	    "state_type %u access %lu mru_hits %u mru_ghost_hits %u "
	    "mfu_hits %u mfu_ghost_hits %u l2_hits %u refcount %lli }",
	    __entry->hdr_dva_word[0], __entry->hdr_dva_word[1],
	    __entry->hdr_birth, __entry->hdr_flags,
	    __entry->hdr_bufcnt, __entry->hdr_type, __entry->hdr_psize,
	    __entry->hdr_lsize, __entry->hdr_spa, __entry->hdr_state_type,
	    __entry->hdr_access, __entry->hdr_mru_hits,
	    __entry->hdr_mru_ghost_hits, __entry->hdr_mfu_hits,
	    __entry->hdr_mfu_ghost_hits, __entry->hdr_l2_hits,
	    __entry->hdr_refcount)
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define	DEFINE_ARC_BUF_HDR_EVENT(name) \
DEFINE_EVENT(zfs_arc_buf_hdr_class, name, \
	TP_PROTO(arc_buf_hdr_t *ab), \
	TP_ARGS(ab))
/* END CSTYLED */
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc__hit);
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc__evict);
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc__delete);
DEFINE_ARC_BUF_HDR_EVENT(zfs_new_state__mru);
DEFINE_ARC_BUF_HDR_EVENT(zfs_new_state__mfu);
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc__sync__wait__for__async);
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc__demand__hit__predictive__prefetch);
DEFINE_ARC_BUF_HDR_EVENT(zfs_l2arc__hit);
DEFINE_ARC_BUF_HDR_EVENT(zfs_l2arc__miss);

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     vdev_t *, ...,
 *     zio_t *, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_l2arc_rw_class,
	TP_PROTO(vdev_t *vd, zio_t *zio),
	TP_ARGS(vd, zio),
	TP_STRUCT__entry(
	    __field(uint64_t,	vdev_id)
	    __field(uint64_t,	vdev_guid)
	    __field(uint64_t,	vdev_state)
	    ZIO_TP_STRUCT_ENTRY
	),
	TP_fast_assign(
	    __entry->vdev_id	= vd->vdev_id;
	    __entry->vdev_guid	= vd->vdev_guid;
	    __entry->vdev_state	= vd->vdev_state;
	    ZIO_TP_FAST_ASSIGN
	),
	TP_printk("vdev { id %llu guid %llu state %llu } "
	    ZIO_TP_PRINTK_FMT, __entry->vdev_id, __entry->vdev_guid,
	    __entry->vdev_state, ZIO_TP_PRINTK_ARGS)
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define	DEFINE_L2ARC_RW_EVENT(name) \
DEFINE_EVENT(zfs_l2arc_rw_class, name, \
	TP_PROTO(vdev_t *vd, zio_t *zio), \
	TP_ARGS(vd, zio))
/* END CSTYLED */
DEFINE_L2ARC_RW_EVENT(zfs_l2arc__read);
DEFINE_L2ARC_RW_EVENT(zfs_l2arc__write);


/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     zio_t *, ...,
 *     l2arc_write_callback_t *, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_l2arc_iodone_class,
	TP_PROTO(zio_t *zio, l2arc_write_callback_t *cb),
	TP_ARGS(zio, cb),
	TP_STRUCT__entry(ZIO_TP_STRUCT_ENTRY),
	TP_fast_assign(ZIO_TP_FAST_ASSIGN),
	TP_printk(ZIO_TP_PRINTK_FMT, ZIO_TP_PRINTK_ARGS)
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define	DEFINE_L2ARC_IODONE_EVENT(name) \
DEFINE_EVENT(zfs_l2arc_iodone_class, name, \
	TP_PROTO(zio_t *zio, l2arc_write_callback_t *cb), \
	TP_ARGS(zio, cb))
/* END CSTYLED */
DEFINE_L2ARC_IODONE_EVENT(zfs_l2arc__iodone);


/*
 * Generic support for four argument tracepoints of the form:
 *
 * DTRACE_PROBE4(...,
 *     arc_buf_hdr_t *, ...,
 *     const blkptr_t *,
 *     uint64_t,
 *     const zbookmark_phys_t *);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_arc_miss_class,
	TP_PROTO(arc_buf_hdr_t *hdr,
	    const blkptr_t *bp, uint64_t size, const zbookmark_phys_t *zb),
	TP_ARGS(hdr, bp, size, zb),
	TP_STRUCT__entry(
	    __array(uint64_t,		hdr_dva_word, 2)
	    __field(uint64_t,		hdr_birth)
	    __field(uint32_t,		hdr_flags)
	    __field(uint32_t,		hdr_bufcnt)
	    __field(arc_buf_contents_t,	hdr_type)
	    __field(uint16_t,		hdr_psize)
	    __field(uint16_t,		hdr_lsize)
	    __field(uint64_t,		hdr_spa)
	    __field(arc_state_type_t,	hdr_state_type)
	    __field(clock_t,		hdr_access)
	    __field(uint32_t,		hdr_mru_hits)
	    __field(uint32_t,		hdr_mru_ghost_hits)
	    __field(uint32_t,		hdr_mfu_hits)
	    __field(uint32_t,		hdr_mfu_ghost_hits)
	    __field(uint32_t,		hdr_l2_hits)
	    __field(int64_t,		hdr_refcount)

	    __array(uint64_t,		bp_dva0, 2)
	    __array(uint64_t,		bp_dva1, 2)
	    __array(uint64_t,		bp_dva2, 2)
	    __array(uint64_t,		bp_cksum, 4)

	    __field(uint64_t,		bp_lsize)

	    __field(uint64_t,		zb_objset)
	    __field(uint64_t,		zb_object)
	    __field(int64_t,		zb_level)
	    __field(uint64_t,		zb_blkid)
	),
	TP_fast_assign(
	    __entry->hdr_dva_word[0]	= hdr->b_dva.dva_word[0];
	    __entry->hdr_dva_word[1]	= hdr->b_dva.dva_word[1];
	    __entry->hdr_birth		= hdr->b_birth;
	    __entry->hdr_flags		= hdr->b_flags;
	    __entry->hdr_bufcnt		= hdr->b_l1hdr.b_bufcnt;
	    __entry->hdr_psize		= hdr->b_psize;
	    __entry->hdr_lsize		= hdr->b_lsize;
	    __entry->hdr_spa		= hdr->b_spa;
	    __entry->hdr_state_type	= hdr->b_l1hdr.b_state->arcs_state;
	    __entry->hdr_access		= hdr->b_l1hdr.b_arc_access;
	    __entry->hdr_mru_hits	= hdr->b_l1hdr.b_mru_hits;
	    __entry->hdr_mru_ghost_hits	= hdr->b_l1hdr.b_mru_ghost_hits;
	    __entry->hdr_mfu_hits	= hdr->b_l1hdr.b_mfu_hits;
	    __entry->hdr_mfu_ghost_hits	= hdr->b_l1hdr.b_mfu_ghost_hits;
	    __entry->hdr_l2_hits	= hdr->b_l1hdr.b_l2_hits;
	    __entry->hdr_refcount	= hdr->b_l1hdr.b_refcnt.rc_count;

	    __entry->bp_dva0[0]		= bp->blk_dva[0].dva_word[0];
	    __entry->bp_dva0[1]		= bp->blk_dva[0].dva_word[1];
	    __entry->bp_dva1[0]		= bp->blk_dva[1].dva_word[0];
	    __entry->bp_dva1[1]		= bp->blk_dva[1].dva_word[1];
	    __entry->bp_dva2[0]		= bp->blk_dva[2].dva_word[0];
	    __entry->bp_dva2[1]		= bp->blk_dva[2].dva_word[1];
	    __entry->bp_cksum[0]	= bp->blk_cksum.zc_word[0];
	    __entry->bp_cksum[1]	= bp->blk_cksum.zc_word[1];
	    __entry->bp_cksum[2]	= bp->blk_cksum.zc_word[2];
	    __entry->bp_cksum[3]	= bp->blk_cksum.zc_word[3];

	    __entry->bp_lsize		= size;

	    __entry->zb_objset		= zb->zb_objset;
	    __entry->zb_object		= zb->zb_object;
	    __entry->zb_level		= zb->zb_level;
	    __entry->zb_blkid		= zb->zb_blkid;
	),
	TP_printk("hdr { dva 0x%llx:0x%llx birth %llu "
	    "flags 0x%x bufcnt %u psize %u lsize %u spa %llu state_type %u "
	    "access %lu mru_hits %u mru_ghost_hits %u mfu_hits %u "
	    "mfu_ghost_hits %u l2_hits %u refcount %lli } "
	    "bp { dva0 0x%llx:0x%llx dva1 0x%llx:0x%llx dva2 "
	    "0x%llx:0x%llx cksum 0x%llx:0x%llx:0x%llx:0x%llx "
	    "lsize %llu } zb { objset %llu object %llu level %lli "
	    "blkid %llu }",
	    __entry->hdr_dva_word[0], __entry->hdr_dva_word[1],
	    __entry->hdr_birth, __entry->hdr_flags,
	    __entry->hdr_bufcnt, __entry->hdr_psize, __entry->hdr_lsize,
	    __entry->hdr_spa, __entry->hdr_state_type, __entry->hdr_access,
	    __entry->hdr_mru_hits, __entry->hdr_mru_ghost_hits,
	    __entry->hdr_mfu_hits, __entry->hdr_mfu_ghost_hits,
	    __entry->hdr_l2_hits, __entry->hdr_refcount,
	    __entry->bp_dva0[0], __entry->bp_dva0[1],
	    __entry->bp_dva1[0], __entry->bp_dva1[1],
	    __entry->bp_dva2[0], __entry->bp_dva2[1],
	    __entry->bp_cksum[0], __entry->bp_cksum[1],
	    __entry->bp_cksum[2], __entry->bp_cksum[3],
	    __entry->bp_lsize, __entry->zb_objset, __entry->zb_object,
	    __entry->zb_level, __entry->zb_blkid)
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define	DEFINE_ARC_MISS_EVENT(name) \
DEFINE_EVENT(zfs_arc_miss_class, name, \
	TP_PROTO(arc_buf_hdr_t *hdr, \
	    const blkptr_t *bp, uint64_t size, const zbookmark_phys_t *zb), \
	TP_ARGS(hdr, bp, size, zb))
/* END CSTYLED */
DEFINE_ARC_MISS_EVENT(zfs_arc__miss);

/*
 * Generic support for four argument tracepoints of the form:
 *
 * DTRACE_PROBE4(...,
 *     l2arc_dev_t *, ...,
 *     list_t *, ...,
 *     uint64_t, ...,
 *     boolean_t, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_l2arc_evict_class,
	TP_PROTO(l2arc_dev_t *dev,
	    list_t *buflist, uint64_t taddr, boolean_t all),
	TP_ARGS(dev, buflist, taddr, all),
	TP_STRUCT__entry(
	    __field(uint64_t,		vdev_id)
	    __field(uint64_t,		vdev_guid)
	    __field(uint64_t,		vdev_state)

	    __field(uint64_t,		l2ad_hand)
	    __field(uint64_t,		l2ad_start)
	    __field(uint64_t,		l2ad_end)
	    __field(boolean_t,		l2ad_first)
	    __field(boolean_t,		l2ad_writing)

	    __field(uint64_t,		taddr)
	    __field(boolean_t,		all)
	),
	TP_fast_assign(
	    __entry->vdev_id		= dev->l2ad_vdev->vdev_id;
	    __entry->vdev_guid		= dev->l2ad_vdev->vdev_guid;
	    __entry->vdev_state		= dev->l2ad_vdev->vdev_state;

	    __entry->l2ad_hand		= dev->l2ad_hand;
	    __entry->l2ad_start		= dev->l2ad_start;
	    __entry->l2ad_end		= dev->l2ad_end;
	    __entry->l2ad_first		= dev->l2ad_first;
	    __entry->l2ad_writing	= dev->l2ad_writing;

	    __entry->taddr		= taddr;
	    __entry->all		= all;
	),
	TP_printk("l2ad { vdev { id %llu guid %llu state %llu } "
	    "hand %llu start %llu end %llu "
	    "first %d writing %d } taddr %llu all %d",
	    __entry->vdev_id, __entry->vdev_guid, __entry->vdev_state,
	    __entry->l2ad_hand, __entry->l2ad_start,
	    __entry->l2ad_end, __entry->l2ad_first, __entry->l2ad_writing,
	    __entry->taddr, __entry->all)
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define	DEFINE_L2ARC_EVICT_EVENT(name) \
DEFINE_EVENT(zfs_l2arc_evict_class, name, \
	TP_PROTO(l2arc_dev_t *dev, \
	    list_t *buflist, uint64_t taddr, boolean_t all), \
	TP_ARGS(dev, buflist, taddr, all))
/* END CSTYLED */
DEFINE_L2ARC_EVICT_EVENT(zfs_l2arc__evict);

#endif /* _TRACE_ARC_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_arc
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
