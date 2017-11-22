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
#define	TRACE_SYSTEM_VAR zfs_dbuf

#if !defined(_TRACE_DBUF_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_DBUF_H

#include <linux/tracepoint.h>
#include <sys/types.h>

#ifndef	TRACE_DBUF_MSG_MAX
#define	TRACE_DBUF_MSG_MAX	512
#endif

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     dmu_buf_impl_t *, ...,
 *     zio_t *, ...);
 */

#define	DBUF_TP_STRUCT_ENTRY					\
	__dynamic_array(char,	os_spa,	TRACE_DBUF_MSG_MAX)	\
	__field(uint64_t,	ds_object)			\
	__field(uint64_t,	db_object)			\
	__field(uint64_t,	db_level)			\
	__field(uint64_t,	db_blkid)			\
	__field(uint64_t,	db_offset)			\
	__field(uint64_t,	db_size)			\
	__field(uint64_t,	db_state)			\
	__field(int64_t,	db_holds)			\
	__dynamic_array(char,	msg,	TRACE_DBUF_MSG_MAX)

#define	DBUF_TP_FAST_ASSIGN						\
	if (db != NULL) {						\
		__assign_str(os_spa,					\
		spa_name(DB_DNODE(db)->dn_objset->os_spa));		\
									\
		__entry->ds_object = db->db_objset->os_dsl_dataset ?	\
		db->db_objset->os_dsl_dataset->ds_object : 0;		\
									\
		__entry->db_object = db->db.db_object;			\
		__entry->db_level  = db->db_level;			\
		__entry->db_blkid  = db->db_blkid;			\
		__entry->db_offset = db->db.db_offset;			\
		__entry->db_size   = db->db.db_size;			\
		__entry->db_state  = db->db_state;			\
		__entry->db_holds  = refcount_count(&db->db_holds);	\
		snprintf(__get_str(msg), TRACE_DBUF_MSG_MAX,		\
		    DBUF_TP_PRINTK_FMT, DBUF_TP_PRINTK_ARGS);		\
	} else {							\
		__assign_str(os_spa, "NULL")				\
		__entry->ds_object = 0;					\
		__entry->db_object = 0;					\
		__entry->db_level  = 0;					\
		__entry->db_blkid  = 0;					\
		__entry->db_offset = 0;					\
		__entry->db_size   = 0;					\
		__entry->db_state  = 0;					\
		__entry->db_holds  = 0;					\
		snprintf(__get_str(msg), TRACE_DBUF_MSG_MAX,		\
		    "dbuf { NULL }");					\
	}

#define	DBUF_TP_PRINTK_FMT						\
	"dbuf { spa \"%s\" objset %llu object %llu level %llu "		\
	"blkid %llu offset %llu size %llu state %llu holds %lld }"

#define	DBUF_TP_PRINTK_ARGS					\
	__get_str(os_spa), __entry->ds_object,			\
	__entry->db_object, __entry->db_level,			\
	__entry->db_blkid, __entry->db_offset,			\
	__entry->db_size, __entry->db_state, __entry->db_holds

/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_dbuf_class,
	TP_PROTO(dmu_buf_impl_t *db, zio_t *zio),
	TP_ARGS(db, zio),
	TP_STRUCT__entry(DBUF_TP_STRUCT_ENTRY),
	TP_fast_assign(DBUF_TP_FAST_ASSIGN),
	TP_printk("%s", __get_str(msg))
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define	DEFINE_DBUF_EVENT(name) \
DEFINE_EVENT(zfs_dbuf_class, name, \
	TP_PROTO(dmu_buf_impl_t *db, zio_t *zio), \
	TP_ARGS(db, zio))
/* END CSTYLED */
DEFINE_DBUF_EVENT(zfs_blocked__read);

/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_dbuf_evict_one_class,
	TP_PROTO(dmu_buf_impl_t *db, multilist_sublist_t *mls),
	TP_ARGS(db, mls),
	TP_STRUCT__entry(DBUF_TP_STRUCT_ENTRY),
	TP_fast_assign(DBUF_TP_FAST_ASSIGN),
	TP_printk("%s", __get_str(msg))
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define	DEFINE_DBUF_EVICT_ONE_EVENT(name) \
DEFINE_EVENT(zfs_dbuf_evict_one_class, name, \
	TP_PROTO(dmu_buf_impl_t *db, multilist_sublist_t *mls), \
	TP_ARGS(db, mls))
/* END CSTYLED */
DEFINE_DBUF_EVICT_ONE_EVENT(zfs_dbuf__evict__one);

#endif /* _TRACE_DBUF_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_dbuf
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
