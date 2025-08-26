/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (c) 2025, Stefan Metzmacher
 */

#ifndef __FS_SMB_COMMON_SMBDIRECT_INTERNAL_H__
#define __FS_SMB_COMMON_SMBDIRECT_INTERNAL_H__

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

#endif /* __FS_SMB_COMMON_SMBDIRECT_INTERNAL_H__ */
