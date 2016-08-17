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

/*
 * Generic support for one argument tracepoints of the form:
 *
 * DTRACE_PROBE1(...,
 *     arc_buf_hdr_t *, ...);
 */

DECLARE_EVENT_CLASS(zfs_arc_buf_hdr_class,
	TP_PROTO(arc_buf_hdr_t *ab),
	TP_ARGS(ab),
	TP_STRUCT__entry(
	    __array(uint64_t,		hdr_dva_word, 2)
	    __field(uint64_t,		hdr_birth)
	    __field(uint32_t,		hdr_flags)
	    __field(uint32_t,		hdr_datacnt)
	    __field(arc_buf_contents_t,	hdr_type)
	    __field(uint64_t,		hdr_size)
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
	    __entry->hdr_datacnt	= ab->b_l1hdr.b_datacnt;
	    __entry->hdr_size		= ab->b_size;
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
	    "flags 0x%x datacnt %u type %u size %llu spa %llu "
	    "state_type %u access %lu mru_hits %u mru_ghost_hits %u "
	    "mfu_hits %u mfu_ghost_hits %u l2_hits %u refcount %lli }",
	    __entry->hdr_dva_word[0], __entry->hdr_dva_word[1],
	    __entry->hdr_birth, __entry->hdr_flags,
	    __entry->hdr_datacnt, __entry->hdr_type, __entry->hdr_size,
	    __entry->hdr_spa, __entry->hdr_state_type,
	    __entry->hdr_access, __entry->hdr_mru_hits,
	    __entry->hdr_mru_ghost_hits, __entry->hdr_mfu_hits,
	    __entry->hdr_mfu_ghost_hits, __entry->hdr_l2_hits,
	    __entry->hdr_refcount)
);

#define	DEFINE_ARC_BUF_HDR_EVENT(name) \
DEFINE_EVENT(zfs_arc_buf_hdr_class, name, \
	TP_PROTO(arc_buf_hdr_t *ab), \
	TP_ARGS(ab))
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc__hit);
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc__evict);
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc__delete);
DEFINE_ARC_BUF_HDR_EVENT(zfs_new_state__mru);
DEFINE_ARC_BUF_HDR_EVENT(zfs_new_state__mfu);
DEFINE_ARC_BUF_HDR_EVENT(zfs_l2arc__hit);
DEFINE_ARC_BUF_HDR_EVENT(zfs_l2arc__miss);

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     vdev_t *, ...,
 *     zio_t *, ...);
 */

#define	ZIO_TP_STRUCT_ENTRY						\
		__field(zio_type_t,		zio_type)		\
		__field(int,			zio_cmd)		\
		__field(zio_priority_t,		zio_priority)		\
		__field(uint64_t,		zio_size)		\
		__field(uint64_t,		zio_orig_size)		\
		__field(uint64_t,		zio_offset)		\
		__field(hrtime_t,		zio_timestamp)		\
		__field(hrtime_t,		zio_delta)		\
		__field(uint64_t,		zio_delay)		\
		__field(enum zio_flag,		zio_flags)		\
		__field(enum zio_stage,		zio_stage)		\
		__field(enum zio_stage,		zio_pipeline)		\
		__field(enum zio_flag,		zio_orig_flags)		\
		__field(enum zio_stage,		zio_orig_stage)		\
		__field(enum zio_stage,		zio_orig_pipeline)	\
		__field(uint8_t,		zio_reexecute)		\
		__field(uint64_t,		zio_txg)		\
		__field(int,			zio_error)		\
		__field(uint64_t,		zio_ena)		\
									\
		__field(enum zio_checksum,	zp_checksum)		\
		__field(enum zio_compress,	zp_compress)		\
		__field(dmu_object_type_t,	zp_type)		\
		__field(uint8_t,		zp_level)		\
		__field(uint8_t,		zp_copies)		\
		__field(boolean_t,		zp_dedup)		\
		__field(boolean_t,		zp_dedup_verify)	\
		__field(boolean_t,		zp_nopwrite)

#define	ZIO_TP_FAST_ASSIGN						    \
		__entry->zio_type		= zio->io_type;		    \
		__entry->zio_cmd		= zio->io_cmd;		    \
		__entry->zio_priority		= zio->io_priority;	    \
		__entry->zio_size		= zio->io_size;		    \
		__entry->zio_orig_size		= zio->io_orig_size;	    \
		__entry->zio_offset		= zio->io_offset;	    \
		__entry->zio_timestamp		= zio->io_timestamp;	    \
		__entry->zio_delta		= zio->io_delta;	    \
		__entry->zio_delay		= zio->io_delay;	    \
		__entry->zio_flags		= zio->io_flags;	    \
		__entry->zio_stage		= zio->io_stage;	    \
		__entry->zio_pipeline		= zio->io_pipeline;	    \
		__entry->zio_orig_flags		= zio->io_orig_flags;	    \
		__entry->zio_orig_stage		= zio->io_orig_stage;	    \
		__entry->zio_orig_pipeline	= zio->io_orig_pipeline;    \
		__entry->zio_reexecute		= zio->io_reexecute;	    \
		__entry->zio_txg		= zio->io_txg;		    \
		__entry->zio_error		= zio->io_error;	    \
		__entry->zio_ena		= zio->io_ena;		    \
									    \
		__entry->zp_checksum		= zio->io_prop.zp_checksum; \
		__entry->zp_compress		= zio->io_prop.zp_compress; \
		__entry->zp_type		= zio->io_prop.zp_type;	    \
		__entry->zp_level		= zio->io_prop.zp_level;    \
		__entry->zp_copies		= zio->io_prop.zp_copies;   \
		__entry->zp_dedup		= zio->io_prop.zp_dedup;    \
		__entry->zp_nopwrite		= zio->io_prop.zp_nopwrite; \
		__entry->zp_dedup_verify	= zio->io_prop.zp_dedup_verify;

#define	ZIO_TP_PRINTK_FMT						\
	"zio { type %u cmd %i prio %u size %llu orig_size %llu "	\
	"offset %llu timestamp %llu delta %llu delay %llu "		\
	"flags 0x%x stage 0x%x pipeline 0x%x orig_flags 0x%x "		\
	"orig_stage 0x%x orig_pipeline 0x%x reexecute %u "		\
	"txg %llu error %d ena %llu prop { checksum %u compress %u "	\
	"type %u level %u copies %u dedup %u dedup_verify %u nopwrite %u } }"

#define	ZIO_TP_PRINTK_ARGS						\
	__entry->zio_type, __entry->zio_cmd, __entry->zio_priority,	\
	__entry->zio_size, __entry->zio_orig_size, __entry->zio_offset,	\
	__entry->zio_timestamp, __entry->zio_delta, __entry->zio_delay,	\
	__entry->zio_flags, __entry->zio_stage, __entry->zio_pipeline,	\
	__entry->zio_orig_flags, __entry->zio_orig_stage,		\
	__entry->zio_orig_pipeline, __entry->zio_reexecute,		\
	__entry->zio_txg, __entry->zio_error, __entry->zio_ena,		\
	__entry->zp_checksum, __entry->zp_compress, __entry->zp_type,	\
	__entry->zp_level, __entry->zp_copies, __entry->zp_dedup,	\
	__entry->zp_dedup_verify, __entry->zp_nopwrite

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

#define	DEFINE_L2ARC_RW_EVENT(name) \
DEFINE_EVENT(zfs_l2arc_rw_class, name, \
	TP_PROTO(vdev_t *vd, zio_t *zio), \
	TP_ARGS(vd, zio))
DEFINE_L2ARC_RW_EVENT(zfs_l2arc__read);
DEFINE_L2ARC_RW_EVENT(zfs_l2arc__write);


/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     zio_t *, ...,
 *     l2arc_write_callback_t *, ...);
 */

DECLARE_EVENT_CLASS(zfs_l2arc_iodone_class,
	TP_PROTO(zio_t *zio, l2arc_write_callback_t *cb),
	TP_ARGS(zio, cb),
	TP_STRUCT__entry(ZIO_TP_STRUCT_ENTRY),
	TP_fast_assign(ZIO_TP_FAST_ASSIGN),
	TP_printk(ZIO_TP_PRINTK_FMT, ZIO_TP_PRINTK_ARGS)
);

#define	DEFINE_L2ARC_IODONE_EVENT(name) \
DEFINE_EVENT(zfs_l2arc_iodone_class, name, \
	TP_PROTO(zio_t *zio, l2arc_write_callback_t *cb), \
	TP_ARGS(zio, cb))
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

DECLARE_EVENT_CLASS(zfs_arc_miss_class,
	TP_PROTO(arc_buf_hdr_t *hdr,
	    const blkptr_t *bp, uint64_t size, const zbookmark_phys_t *zb),
	TP_ARGS(hdr, bp, size, zb),
	TP_STRUCT__entry(
	    __array(uint64_t,		hdr_dva_word, 2)
	    __field(uint64_t,		hdr_birth)
	    __field(uint32_t,		hdr_flags)
	    __field(uint32_t,		hdr_datacnt)
	    __field(arc_buf_contents_t,	hdr_type)
	    __field(uint64_t,		hdr_size)
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
	    __entry->hdr_datacnt	= hdr->b_l1hdr.b_datacnt;
	    __entry->hdr_size		= hdr->b_size;
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
	    "flags 0x%x datacnt %u size %llu spa %llu state_type %u "
	    "access %lu mru_hits %u mru_ghost_hits %u mfu_hits %u "
	    "mfu_ghost_hits %u l2_hits %u refcount %lli } "
	    "bp { dva0 0x%llx:0x%llx dva1 0x%llx:0x%llx dva2 "
	    "0x%llx:0x%llx cksum 0x%llx:0x%llx:0x%llx:0x%llx "
	    "lsize %llu } zb { objset %llu object %llu level %lli "
	    "blkid %llu }",
	    __entry->hdr_dva_word[0], __entry->hdr_dva_word[1],
	    __entry->hdr_birth, __entry->hdr_flags,
	    __entry->hdr_datacnt, __entry->hdr_size,
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

#define	DEFINE_ARC_MISS_EVENT(name) \
DEFINE_EVENT(zfs_arc_miss_class, name, \
	TP_PROTO(arc_buf_hdr_t *hdr, \
	    const blkptr_t *bp, uint64_t size, const zbookmark_phys_t *zb), \
	TP_ARGS(hdr, bp, size, zb))
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

#define	DEFINE_L2ARC_EVICT_EVENT(name) \
DEFINE_EVENT(zfs_l2arc_evict_class, name, \
	TP_PROTO(l2arc_dev_t *dev, \
	    list_t *buflist, uint64_t taddr, boolean_t all), \
	TP_ARGS(dev, buflist, taddr, all))
DEFINE_L2ARC_EVICT_EVENT(zfs_l2arc__evict);

#endif /* _TRACE_ARC_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_arc
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
