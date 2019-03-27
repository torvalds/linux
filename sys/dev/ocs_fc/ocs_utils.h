/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @file
 *
 */

#ifndef __OCS_UTILS_H
#define __OCS_UTILS_H

#include "ocs_list.h"
typedef struct ocs_array_s ocs_array_t;

extern void ocs_array_set_slablen(uint32_t len);
extern ocs_array_t *ocs_array_alloc(ocs_os_handle_t os, uint32_t size, uint32_t count);
extern void ocs_array_free(ocs_array_t *array);
extern void *ocs_array_get(ocs_array_t *array, uint32_t idx);
extern uint32_t ocs_array_get_count(ocs_array_t *array);
extern uint32_t ocs_array_get_size(ocs_array_t *array);

/* Void pointer array and iterator */
typedef struct ocs_varray_s ocs_varray_t;

extern ocs_varray_t *ocs_varray_alloc(ocs_os_handle_t os, uint32_t entry_count);
extern void ocs_varray_free(ocs_varray_t *ai);
extern int32_t ocs_varray_add(ocs_varray_t *ai, void *entry);
extern void ocs_varray_iter_reset(ocs_varray_t *ai);
extern void *ocs_varray_iter_next(ocs_varray_t *ai);
extern void *_ocs_varray_iter_next(ocs_varray_t *ai);
extern void ocs_varray_lock(ocs_varray_t *ai);
extern void ocs_varray_unlock(ocs_varray_t *ai);
extern uint32_t ocs_varray_get_count(ocs_varray_t *ai);


/***************************************************************************
 * Circular buffer
 *
 */

typedef struct ocs_cbuf_s ocs_cbuf_t;

extern ocs_cbuf_t *ocs_cbuf_alloc(ocs_os_handle_t os, uint32_t entry_count);
extern void ocs_cbuf_free(ocs_cbuf_t *cbuf);
extern void *ocs_cbuf_get(ocs_cbuf_t *cbuf, int32_t timeout_usec);
extern int32_t ocs_cbuf_put(ocs_cbuf_t *cbuf, void *elem);
extern int32_t ocs_cbuf_prime(ocs_cbuf_t *cbuf, ocs_array_t *array);

typedef struct {
        void *vaddr;
        uint32_t length;
} ocs_scsi_vaddr_len_t;


#define OCS_TEXTBUF_MAX_ALLOC_LEN	(256*1024)

typedef struct {
	ocs_list_link_t link;
	uint8_t user_allocated:1;
	uint8_t *buffer;
	uint32_t buffer_length;
	uint32_t buffer_written;
} ocs_textbuf_segment_t;

typedef struct {
	ocs_t *ocs;
	ocs_list_t segment_list;
	uint8_t extendable:1;
	uint32_t allocation_length;
	uint32_t total_allocation_length;
	uint32_t max_allocation_length;
} ocs_textbuf_t;

extern int32_t ocs_textbuf_alloc(ocs_t *ocs, ocs_textbuf_t *textbuf, uint32_t length);
extern uint32_t ocs_textbuf_initialized(ocs_textbuf_t *textbuf);
extern int32_t ocs_textbuf_init(ocs_t *ocs, ocs_textbuf_t *textbuf, void *buffer, uint32_t length);
extern void ocs_textbuf_free(ocs_t *ocs, ocs_textbuf_t *textbuf);
extern void ocs_textbuf_putc(ocs_textbuf_t *textbuf, uint8_t c);
extern void ocs_textbuf_puts(ocs_textbuf_t *textbuf, char *s);
__attribute__((format(printf,2,3)))
extern void ocs_textbuf_printf(ocs_textbuf_t *textbuf, const char *fmt, ...);
__attribute__((format(printf,2,0)))
extern void ocs_textbuf_vprintf(ocs_textbuf_t *textbuf, const char *fmt, va_list ap);
extern void ocs_textbuf_buffer(ocs_textbuf_t *textbuf, uint8_t *buffer, uint32_t buffer_length);
extern void ocs_textbuf_copy(ocs_textbuf_t *textbuf, uint8_t *buffer, uint32_t buffer_length);
extern int32_t ocs_textbuf_remaining(ocs_textbuf_t *textbuf);
extern void ocs_textbuf_reset(ocs_textbuf_t *textbuf);
extern uint8_t *ocs_textbuf_get_buffer(ocs_textbuf_t *textbuf);
extern int32_t ocs_textbuf_get_length(ocs_textbuf_t *textbuf);
extern int32_t ocs_textbuf_get_written(ocs_textbuf_t *textbuf);
extern uint8_t *ocs_textbuf_ext_get_buffer(ocs_textbuf_t *textbuf, uint32_t idx);
extern int32_t ocs_textbuf_ext_get_length(ocs_textbuf_t *textbuf, uint32_t idx);
extern int32_t ocs_textbuf_ext_get_written(ocs_textbuf_t *textbuf, uint32_t idx);


typedef struct ocs_pool_s ocs_pool_t;

extern ocs_pool_t *ocs_pool_alloc(ocs_os_handle_t os, uint32_t size, uint32_t count, uint32_t use_lock);
extern void ocs_pool_reset(ocs_pool_t *pool);
extern void ocs_pool_free(ocs_pool_t *pool);
extern void *ocs_pool_get(ocs_pool_t *pool);
extern void ocs_pool_put(ocs_pool_t *pool, void *item);
extern uint32_t ocs_pool_get_count(ocs_pool_t *pool);
extern void *ocs_pool_get_instance(ocs_pool_t *pool, uint32_t idx);
extern uint32_t ocs_pool_get_freelist_count(ocs_pool_t *pool);


/* Uncomment this line to enable logging extended queue history
 */
//#define OCS_DEBUG_QUEUE_HISTORY


/* Allocate maximum allowed (4M) */
#if defined(OCS_DEBUG_QUEUE_HISTORY)
#define OCS_Q_HIST_SIZE (1000000UL)		/* Size in words */
#endif

#define OCS_LOG_ENABLE_SM_TRACE(ocs)		(((ocs) != NULL) ? (((ocs)->logmask & (1U << 0)) != 0) : 0)
#define OCS_LOG_ENABLE_ELS_TRACE(ocs)		(((ocs) != NULL) ? (((ocs)->logmask & (1U << 1)) != 0) : 0)
#define OCS_LOG_ENABLE_SCSI_TRACE(ocs)		(((ocs) != NULL) ? (((ocs)->logmask & (1U << 2)) != 0) : 0)
#define OCS_LOG_ENABLE_SCSI_TGT_TRACE(ocs)	(((ocs) != NULL) ? (((ocs)->logmask & (1U << 3)) != 0) : 0)
#define OCS_LOG_ENABLE_DOMAIN_SM_TRACE(ocs)	(((ocs) != NULL) ? (((ocs)->logmask & (1U << 4)) != 0) : 0)
#define OCS_LOG_ENABLE_Q_FULL_BUSY_MSG(ocs)	(((ocs) != NULL) ? (((ocs)->logmask & (1U << 5)) != 0) : 0)
#define OCS_LOG_ENABLE_IO_ERRORS(ocs)		(((ocs) != NULL) ? (((ocs)->logmask & (1U << 6)) != 0) : 0)


extern void ocs_dump32(uint32_t, ocs_os_handle_t, const char *, void *, uint32_t);
extern void ocs_debug_enable(uint32_t mask);
extern void ocs_debug_disable(uint32_t mask);
extern int ocs_debug_is_enabled(uint32_t mask);
extern void ocs_debug_attach(void *);
extern void ocs_debug_detach(void *);

#if defined(OCS_DEBUG_QUEUE_HISTORY)

/**
 * @brief Queue history footer
 */
typedef union ocs_q_hist_ftr_u {
	uint32_t word;
	struct {
#define Q_HIST_TYPE_LEN 		3
#define Q_HIST_MASK_LEN 		29
		uint32_t mask:Q_HIST_MASK_LEN,
			 type:Q_HIST_TYPE_LEN;
	} s;
} ocs_q_hist_ftr_t;


/**
 * @brief WQE command mask lookup
 */
typedef struct ocs_q_hist_wqe_mask_s {
	uint8_t command;
	uint32_t mask;
} ocs_q_hist_wqe_mask_t;

/**
 * @brief CQE mask lookup
 */
typedef struct ocs_q_hist_cqe_mask_s {
	uint8_t ctype;
	uint32_t :Q_HIST_MASK_LEN,
		 type:Q_HIST_TYPE_LEN;
	uint32_t mask;
	uint32_t mask_err;
} ocs_q_hist_cqe_mask_t;

/**
 * @brief Queue history type
 */
typedef enum {
	/* changes need to be made to ocs_queue_history_type_name() as well */
	OCS_Q_HIST_TYPE_WQE = 0,
	OCS_Q_HIST_TYPE_CWQE,
	OCS_Q_HIST_TYPE_CXABT,
	OCS_Q_HIST_TYPE_MISC,
} ocs_q_hist_type_t;

static __inline const char *
ocs_queue_history_type_name(ocs_q_hist_type_t type)
{
	switch (type) {
	case OCS_Q_HIST_TYPE_WQE: return "wqe"; break;
	case OCS_Q_HIST_TYPE_CWQE: return "wcqe"; break;
	case OCS_Q_HIST_TYPE_CXABT: return "xacqe"; break;
	case OCS_Q_HIST_TYPE_MISC: return "misc"; break;
	default: return "unknown"; break;
	}
}

typedef struct {
	ocs_t		*ocs;
	uint32_t	*q_hist;
	uint32_t	q_hist_index;
	ocs_lock_t	q_hist_lock;
} ocs_hw_q_hist_t;

extern void ocs_queue_history_cqe(ocs_hw_q_hist_t*, uint8_t, uint32_t *, uint8_t, uint32_t, uint32_t);
extern void ocs_queue_history_wq(ocs_hw_q_hist_t*, uint32_t *, uint32_t, uint32_t);
extern void ocs_queue_history_misc(ocs_hw_q_hist_t*, uint32_t *, uint32_t);
extern void ocs_queue_history_init(ocs_t *, ocs_hw_q_hist_t*);
extern void ocs_queue_history_free(ocs_hw_q_hist_t*);
extern uint32_t ocs_queue_history_prev_index(uint32_t);
extern uint8_t ocs_queue_history_q_info_enabled(void);
extern uint8_t ocs_queue_history_timestamp_enabled(void);
#else
#define ocs_queue_history_wq(...)
#define ocs_queue_history_cqe(...)
#define ocs_queue_history_misc(...)
#define ocs_queue_history_init(...)
#define ocs_queue_history_free(...)
#endif

#define OCS_DEBUG_ALWAYS		(1U << 0)
#define OCS_DEBUG_ENABLE_MQ_DUMP	(1U << 1)
#define OCS_DEBUG_ENABLE_CQ_DUMP	(1U << 2)
#define OCS_DEBUG_ENABLE_WQ_DUMP	(1U << 3)
#define OCS_DEBUG_ENABLE_EQ_DUMP	(1U << 4)
#define OCS_DEBUG_ENABLE_SPARAM_DUMP	(1U << 5)

extern void _ocs_assert(const char *cond, const char *filename, int linenum);

#define ocs_assert(cond, ...) \
	do { \
		if (!(cond)) { \
			_ocs_assert(#cond, __FILE__, __LINE__); \
			return __VA_ARGS__; \
		} \
	} while (0)

extern void ocs_dump_service_params(const char *label, void *sparms);
extern void ocs_display_sparams(const char *prelabel, const char *reqlabel, int dest, void *textbuf, void *sparams);


typedef struct {
	uint16_t crc;
	uint16_t app_tag;
	uint32_t ref_tag;
} ocs_dif_t;

/* DIF guard calculations */
extern uint16_t ocs_scsi_dif_calc_crc(const uint8_t *, uint32_t size, uint16_t crc);
extern uint16_t ocs_scsi_dif_calc_checksum(ocs_scsi_vaddr_len_t addrlen[], uint32_t addrlen_count);

/**
 * @brief Power State change message types
 *
 */
typedef enum {
	OCS_PM_PREPARE = 1,
	OCS_PM_SLEEP,
	OCS_PM_HIBERNATE,
	OCS_PM_RESUME,
} ocs_pm_msg_e;

/**
 * @brief Power State values
 *
 */
typedef enum {
	OCS_PM_STATE_S0 = 0,
	OCS_PM_STATE_S1,
	OCS_PM_STATE_S2,
	OCS_PM_STATE_S3,
	OCS_PM_STATE_S4,
} ocs_pm_state_e;

typedef struct {
	ocs_pm_state_e pm_state;		/*<< Current PM state */
} ocs_pm_context_t;

extern int32_t ocs_pm_request(ocs_t *ocs, ocs_pm_msg_e msg, int32_t (*callback)(ocs_t *ocs, int32_t status, void *arg),
	void *arg);
extern ocs_pm_state_e ocs_pm_get_state(ocs_t *ocs);
extern const char *ocs_pm_get_state_string(ocs_t *ocs);

#define SPV_ROWLEN	256
#define SPV_DIM		3


/*!
* @defgroup spv Sparse Vector
*/

/**
 * @brief Sparse vector structure.
 */
typedef struct sparse_vector_s {
	ocs_os_handle_t os;
	uint32_t max_idx;		/**< maximum index value */
	void **array;			/**< pointer to 3D array */
} *sparse_vector_t;

extern void spv_del(sparse_vector_t spv);
extern sparse_vector_t spv_new(ocs_os_handle_t os);
extern void spv_set(sparse_vector_t sv, uint32_t idx, void *value);
extern void *spv_get(sparse_vector_t sv, uint32_t idx);

extern unsigned short t10crc16(const unsigned char *blk_adr, unsigned long blk_len, unsigned short crc);

typedef struct ocs_ramlog_s ocs_ramlog_t;

#define OCS_RAMLOG_DEFAULT_BUFFERS              5

extern ocs_ramlog_t *ocs_ramlog_init(ocs_t *ocs, uint32_t buffer_len, uint32_t buffer_count);
extern void ocs_ramlog_free(ocs_t *ocs, ocs_ramlog_t *ramlog);
extern void ocs_ramlog_clear(ocs_t *ocs, ocs_ramlog_t *ramlog, int clear_start_of_day, int clear_recent);
__attribute__((format(printf,2,3)))
extern int32_t ocs_ramlog_printf(void *os, const char *fmt, ...);
__attribute__((format(printf,2,0)))
extern int32_t ocs_ramlog_vprintf(ocs_ramlog_t *ramlog, const char *fmt, va_list ap);
extern int32_t ocs_ddump_ramlog(ocs_textbuf_t *textbuf, ocs_ramlog_t *ramlog);

#endif 
