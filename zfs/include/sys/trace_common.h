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

/*
 * This file contains commonly used trace macros.  Feel free to add and use
 * them in your tracepoint headers.
 */

#ifndef	_SYS_TRACE_COMMON_H
#define	_SYS_TRACE_COMMON_H
#include <linux/tracepoint.h>

/* ZIO macros */
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

#endif /* _SYS_TRACE_COMMON_H */
