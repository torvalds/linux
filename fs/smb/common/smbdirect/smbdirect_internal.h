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
#include "smbdirect_public.h"
#include "smbdirect_socket.h"

#ifdef SMBDIRECT_USE_INLINE_C_FILES
/* this is temporary while this file is included in others */
#define __SMBDIRECT_PRIVATE__ __maybe_unused static
#else
#define __SMBDIRECT_PRIVATE__
#endif

__SMBDIRECT_PRIVATE__
int smbdirect_socket_init_new(struct net *net, struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
int smbdirect_socket_init_accepting(struct rdma_cm_id *id, struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
void __smbdirect_socket_schedule_cleanup(struct smbdirect_socket *sc,
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

__SMBDIRECT_PRIVATE__
void smbdirect_socket_destroy_sync(struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
int smbdirect_socket_wait_for_credits(struct smbdirect_socket *sc,
				      enum smbdirect_socket_status expected_status,
				      int unexpected_errno,
				      wait_queue_head_t *waitq,
				      atomic_t *total_credits,
				      int needed);

__SMBDIRECT_PRIVATE__
void smbdirect_connection_rdma_established(struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
void smbdirect_connection_negotiation_done(struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
int smbdirect_connection_create_qp(struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
void smbdirect_connection_destroy_qp(struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
int smbdirect_connection_create_mem_pools(struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
void smbdirect_connection_destroy_mem_pools(struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
struct smbdirect_send_io *smbdirect_connection_alloc_send_io(struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
void smbdirect_connection_free_send_io(struct smbdirect_send_io *msg);

__SMBDIRECT_PRIVATE__
struct smbdirect_recv_io *smbdirect_connection_get_recv_io(struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
void smbdirect_connection_put_recv_io(struct smbdirect_recv_io *msg);

__SMBDIRECT_PRIVATE__
void smbdirect_connection_reassembly_append_recv_io(struct smbdirect_socket *sc,
						    struct smbdirect_recv_io *msg,
						    u32 data_length);

__SMBDIRECT_PRIVATE__
struct smbdirect_recv_io *
smbdirect_connection_reassembly_first_recv_io(struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
void smbdirect_connection_negotiate_rdma_resources(struct smbdirect_socket *sc,
						   u8 peer_initiator_depth,
						   u8 peer_responder_resources,
						   const struct rdma_conn_param *param);

__SMBDIRECT_PRIVATE__
void smbdirect_connection_idle_timer_work(struct work_struct *work);

__SMBDIRECT_PRIVATE__
u16 smbdirect_connection_grant_recv_credits(struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
int smbdirect_connection_post_send_wr(struct smbdirect_socket *sc,
				      struct ib_send_wr *wr);

__SMBDIRECT_PRIVATE__
int smbdirect_connection_post_recv_io(struct smbdirect_recv_io *msg);

__SMBDIRECT_PRIVATE__
void smbdirect_connection_recv_io_done(struct ib_cq *cq, struct ib_wc *wc);

__SMBDIRECT_PRIVATE__
int smbdirect_connection_recv_io_refill(struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
int smbdirect_connection_create_mr_list(struct smbdirect_socket *sc);

__SMBDIRECT_PRIVATE__
void smbdirect_connection_destroy_mr_list(struct smbdirect_socket *sc);

void smbdirect_accept_negotiate_finish(struct smbdirect_socket *sc, u32 ntstatus);

#endif /* __FS_SMB_COMMON_SMBDIRECT_INTERNAL_H__ */
