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

#if defined(_KERNEL) && defined(HAVE_DECLARE_EVENT_CLASS)

#undef TRACE_SYSTEM
#define	TRACE_SYSTEM zfs

#undef TRACE_SYSTEM_VAR
#define	TRACE_SYSTEM_VAR zfs_dnode

#if !defined(_TRACE_DNODE_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_DNODE_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for three argument tracepoints of the form:
 *
 * DTRACE_PROBE3(...,
 *     dnode_t *, ...,
 *     int64_t, ...,
 *     uint32_t, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_dnode_move_class,
	TP_PROTO(dnode_t *dn, int64_t refcount, uint32_t dbufs),
	TP_ARGS(dn, refcount, dbufs),
	TP_STRUCT__entry(
	    __field(uint64_t,		dn_object)
	    __field(dmu_object_type_t,	dn_type)
	    __field(uint16_t,		dn_bonuslen)
	    __field(uint8_t,		dn_bonustype)
	    __field(uint8_t,		dn_nblkptr)
	    __field(uint8_t,		dn_checksum)
	    __field(uint8_t,		dn_compress)
	    __field(uint8_t,		dn_nlevels)
	    __field(uint8_t,		dn_indblkshift)
	    __field(uint8_t,		dn_datablkshift)
	    __field(uint8_t,		dn_moved)
	    __field(uint16_t,		dn_datablkszsec)
	    __field(uint32_t,		dn_datablksz)
	    __field(uint64_t,		dn_maxblkid)
	    __field(int64_t,		dn_tx_holds)
	    __field(int64_t,		dn_holds)
	    __field(boolean_t,		dn_have_spill)

	    __field(int64_t,		refcount)
	    __field(uint32_t,		dbufs)
	),
	TP_fast_assign(
	    __entry->dn_object		= dn->dn_object;
	    __entry->dn_type		= dn->dn_type;
	    __entry->dn_bonuslen	= dn->dn_bonuslen;
	    __entry->dn_bonustype	= dn->dn_bonustype;
	    __entry->dn_nblkptr		= dn->dn_nblkptr;
	    __entry->dn_checksum	= dn->dn_checksum;
	    __entry->dn_compress	= dn->dn_compress;
	    __entry->dn_nlevels		= dn->dn_nlevels;
	    __entry->dn_indblkshift	= dn->dn_indblkshift;
	    __entry->dn_datablkshift	= dn->dn_datablkshift;
	    __entry->dn_moved		= dn->dn_moved;
	    __entry->dn_datablkszsec	= dn->dn_datablkszsec;
	    __entry->dn_datablksz	= dn->dn_datablksz;
	    __entry->dn_maxblkid	= dn->dn_maxblkid;
	    __entry->dn_tx_holds	= dn->dn_tx_holds.rc_count;
	    __entry->dn_holds		= dn->dn_holds.rc_count;
	    __entry->dn_have_spill	= dn->dn_have_spill;

	    __entry->refcount		= refcount;
	    __entry->dbufs		= dbufs;
	),
	TP_printk("dn { object %llu type %d bonuslen %u bonustype %u "
	    "nblkptr %u checksum %u compress %u nlevels %u indblkshift %u "
	    "datablkshift %u moved %u datablkszsec %u datablksz %u "
	    "maxblkid %llu tx_holds %lli holds %lli have_spill %d } "
	    "refcount %lli dbufs %u",
	    __entry->dn_object, __entry->dn_type, __entry->dn_bonuslen,
	    __entry->dn_bonustype, __entry->dn_nblkptr, __entry->dn_checksum,
	    __entry->dn_compress, __entry->dn_nlevels, __entry->dn_indblkshift,
	    __entry->dn_datablkshift, __entry->dn_moved,
	    __entry->dn_datablkszsec, __entry->dn_datablksz,
	    __entry->dn_maxblkid, __entry->dn_tx_holds, __entry->dn_holds,
	    __entry->dn_have_spill, __entry->refcount, __entry->dbufs)
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define	DEFINE_DNODE_MOVE_EVENT(name) \
DEFINE_EVENT(zfs_dnode_move_class, name, \
	TP_PROTO(dnode_t *dn, int64_t refcount, uint32_t dbufs), \
	TP_ARGS(dn, refcount, dbufs))
/* END CSTYLED */
DEFINE_DNODE_MOVE_EVENT(zfs_dnode__move);

#endif /* _TRACE_DNODE_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_dnode
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
