/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (c) 2025, Stefan Metzmacher
 */

#ifndef __FS_SMB_COMMON_SMBDIRECT_INTERNAL_H__
#define __FS_SMB_COMMON_SMBDIRECT_INTERNAL_H__

#ifndef SMBDIRECT_USE_INLINE_C_FILES
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#endif /* ! SMBDIRECT_USE_INLINE_C_FILES */

#include "smbdirect.h"
#include "smbdirect_pdu.h"
#include "smbdirect_socket.h"

static void __smbdirect_socket_schedule_cleanup(struct smbdirect_socket *sc,
						const char *macro_name,
						unsigned int lvl,
						const char *func,
						unsigned int line,
						int error,
						enum smbdirect_socket_status *force_status);
#define smbdirect_socket_schedule_cleanup(__sc, __error) \
	__smbdirect_socket_schedule_cleanup(__sc, \
		"smbdirect_socket_schedule_cleanup", SMBDIRECT_LOG_ERR, \
		__func__, __LINE__, __error, NULL)
#define smbdirect_socket_schedule_cleanup_lvl(__sc, __lvl, __error) \
	__smbdirect_socket_schedule_cleanup(__sc, \
		"smbdirect_socket_schedule_cleanup_lvl", __lvl, \
		__func__, __LINE__, __error, NULL)
#define smbdirect_socket_schedule_cleanup_status(__sc, __lvl, __error, __status) do { \
	enum smbdirect_socket_status __force_status = __status; \
	__smbdirect_socket_schedule_cleanup(__sc, \
		"smbdirect_socket_schedule_cleanup_status", __lvl, \
		__func__, __LINE__, __error, &__force_status); \
} while (0)

static int smbdirect_socket_wait_for_credits(struct smbdirect_socket *sc,
					     enum smbdirect_socket_status expected_status,
					     int unexpected_errno,
					     wait_queue_head_t *waitq,
					     atomic_t *total_credits,
					     int needed);

static void smbdirect_connection_destroy_qp(struct smbdirect_socket *sc);

static void smbdirect_connection_destroy_mem_pools(struct smbdirect_socket *sc);

static void smbdirect_connection_put_recv_io(struct smbdirect_recv_io *msg);

static void smbdirect_connection_idle_timer_work(struct work_struct *work);

static void smbdirect_connection_destroy_mr_list(struct smbdirect_socket *sc);

#endif /* __FS_SMB_COMMON_SMBDIRECT_INTERNAL_H__ */
