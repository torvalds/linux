// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (c) 2025, Stefan Metzmacher
 */

#include "smbdirect_internal.h"

__maybe_unused /* this is temporary while this file is included in others */
static bool smbdirect_frwr_is_supported(const struct ib_device_attr *attrs)
{
	/*
	 * Test if FRWR (Fast Registration Work Requests) is supported on the
	 * device This implementation requires FRWR on RDMA read/write return
	 * value: true if it is supported
	 */

	if (!(attrs->device_cap_flags & IB_DEVICE_MEM_MGT_EXTENSIONS))
		return false;
	if (attrs->max_fast_reg_page_list_len == 0)
		return false;
	return true;
}

static void smbdirect_socket_cleanup_work(struct work_struct *work);

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_socket_prepare_create(struct smbdirect_socket *sc,
					    const struct smbdirect_socket_parameters *sp,
					    struct workqueue_struct *workqueue)
{
	smbdirect_socket_init(sc);

	/*
	 * Make a copy of the callers parameters
	 * from here we only work on the copy
	 */
	sc->parameters = *sp;

	/*
	 * Remember the callers workqueue
	 */
	sc->workqueue = workqueue;

	INIT_WORK(&sc->disconnect_work, smbdirect_socket_cleanup_work);

	INIT_DELAYED_WORK(&sc->idle.timer_work, smbdirect_connection_idle_timer_work);
}

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_socket_set_logging(struct smbdirect_socket *sc,
					 void *private_ptr,
					 bool (*needed)(struct smbdirect_socket *sc,
							void *private_ptr,
							unsigned int lvl,
							unsigned int cls),
					 void (*vaprintf)(struct smbdirect_socket *sc,
							  const char *func,
							  unsigned int line,
							  void *private_ptr,
							  unsigned int lvl,
							  unsigned int cls,
							  struct va_format *vaf))
{
	sc->logging.private_ptr = private_ptr;
	sc->logging.needed = needed;
	sc->logging.vaprintf = vaprintf;
}

static void smbdirect_socket_wake_up_all(struct smbdirect_socket *sc)
{
	/*
	 * Wake up all waiters in all wait queues
	 * in order to notice the broken connection.
	 */
	wake_up_all(&sc->status_wait);
	wake_up_all(&sc->send_io.bcredits.wait_queue);
	wake_up_all(&sc->send_io.lcredits.wait_queue);
	wake_up_all(&sc->send_io.credits.wait_queue);
	wake_up_all(&sc->send_io.pending.dec_wait_queue);
	wake_up_all(&sc->send_io.pending.zero_wait_queue);
	wake_up_all(&sc->recv_io.reassembly.wait_queue);
	wake_up_all(&sc->rw_io.credits.wait_queue);
	wake_up_all(&sc->mr_io.ready.wait_queue);
	wake_up_all(&sc->mr_io.cleanup.wait_queue);
}

static void __smbdirect_socket_schedule_cleanup(struct smbdirect_socket *sc,
						const char *macro_name,
						unsigned int lvl,
						const char *func,
						unsigned int line,
						int error,
						enum smbdirect_socket_status *force_status)
{
	bool was_first = false;

	if (!sc->first_error) {
		___smbdirect_log_generic(sc, func, line,
			lvl,
			SMBDIRECT_LOG_RDMA_EVENT,
			"%s(%1pe%s%s) called from %s in line=%u status=%s\n",
			macro_name,
			SMBDIRECT_DEBUG_ERR_PTR(error),
			force_status ? ", " : "",
			force_status ? smbdirect_socket_status_string(*force_status) : "",
			func, line,
			smbdirect_socket_status_string(sc->status));
		if (error)
			sc->first_error = error;
		else
			sc->first_error = -ECONNABORTED;
		was_first = true;
	}

	/*
	 * make sure other work (than disconnect_work)
	 * is not queued again but here we don't block and avoid
	 * disable[_delayed]_work_sync()
	 */
	disable_work(&sc->connect.work);
	disable_work(&sc->recv_io.posted.refill_work);
	disable_work(&sc->mr_io.recovery_work);
	disable_work(&sc->idle.immediate_work);
	sc->idle.keepalive = SMBDIRECT_KEEPALIVE_NONE;
	disable_delayed_work(&sc->idle.timer_work);

	switch (sc->status) {
	case SMBDIRECT_SOCKET_RESOLVE_ADDR_FAILED:
	case SMBDIRECT_SOCKET_RESOLVE_ROUTE_FAILED:
	case SMBDIRECT_SOCKET_RDMA_CONNECT_FAILED:
	case SMBDIRECT_SOCKET_NEGOTIATE_FAILED:
	case SMBDIRECT_SOCKET_ERROR:
	case SMBDIRECT_SOCKET_DISCONNECTING:
	case SMBDIRECT_SOCKET_DISCONNECTED:
	case SMBDIRECT_SOCKET_DESTROYED:
		/*
		 * Keep the current error status
		 */
		break;

	case SMBDIRECT_SOCKET_RESOLVE_ADDR_NEEDED:
	case SMBDIRECT_SOCKET_RESOLVE_ADDR_RUNNING:
		sc->status = SMBDIRECT_SOCKET_RESOLVE_ADDR_FAILED;
		break;

	case SMBDIRECT_SOCKET_RESOLVE_ROUTE_NEEDED:
	case SMBDIRECT_SOCKET_RESOLVE_ROUTE_RUNNING:
		sc->status = SMBDIRECT_SOCKET_RESOLVE_ROUTE_FAILED;
		break;

	case SMBDIRECT_SOCKET_RDMA_CONNECT_NEEDED:
	case SMBDIRECT_SOCKET_RDMA_CONNECT_RUNNING:
		sc->status = SMBDIRECT_SOCKET_RDMA_CONNECT_FAILED;
		break;

	case SMBDIRECT_SOCKET_NEGOTIATE_NEEDED:
	case SMBDIRECT_SOCKET_NEGOTIATE_RUNNING:
		sc->status = SMBDIRECT_SOCKET_NEGOTIATE_FAILED;
		break;

	case SMBDIRECT_SOCKET_CREATED:
		sc->status = SMBDIRECT_SOCKET_DISCONNECTED;
		break;

	case SMBDIRECT_SOCKET_CONNECTED:
		sc->status = SMBDIRECT_SOCKET_ERROR;
		break;
	}

	if (force_status && (was_first || *force_status > sc->status))
		sc->status = *force_status;

	/*
	 * Wake up all waiters in all wait queues
	 * in order to notice the broken connection.
	 */
	smbdirect_socket_wake_up_all(sc);

	queue_work(sc->workqueue, &sc->disconnect_work);
}

static void smbdirect_socket_cleanup_work(struct work_struct *work)
{
	struct smbdirect_socket *sc =
		container_of(work, struct smbdirect_socket, disconnect_work);

	/*
	 * This should not never be called in an interrupt!
	 */
	WARN_ON_ONCE(in_interrupt());

	if (!sc->first_error) {
		smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_ERR,
			"%s called with first_error==0\n",
			smbdirect_socket_status_string(sc->status));

		sc->first_error = -ECONNABORTED;
	}

	/*
	 * make sure this and other work is not queued again
	 * but here we don't block and avoid
	 * disable[_delayed]_work_sync()
	 */
	disable_work(&sc->disconnect_work);
	disable_work(&sc->connect.work);
	disable_work(&sc->recv_io.posted.refill_work);
	disable_work(&sc->mr_io.recovery_work);
	disable_work(&sc->idle.immediate_work);
	sc->idle.keepalive = SMBDIRECT_KEEPALIVE_NONE;
	disable_delayed_work(&sc->idle.timer_work);

	switch (sc->status) {
	case SMBDIRECT_SOCKET_NEGOTIATE_NEEDED:
	case SMBDIRECT_SOCKET_NEGOTIATE_RUNNING:
	case SMBDIRECT_SOCKET_NEGOTIATE_FAILED:
	case SMBDIRECT_SOCKET_CONNECTED:
	case SMBDIRECT_SOCKET_ERROR:
		sc->status = SMBDIRECT_SOCKET_DISCONNECTING;
		rdma_disconnect(sc->rdma.cm_id);
		break;

	case SMBDIRECT_SOCKET_CREATED:
	case SMBDIRECT_SOCKET_RESOLVE_ADDR_NEEDED:
	case SMBDIRECT_SOCKET_RESOLVE_ADDR_RUNNING:
	case SMBDIRECT_SOCKET_RESOLVE_ADDR_FAILED:
	case SMBDIRECT_SOCKET_RESOLVE_ROUTE_NEEDED:
	case SMBDIRECT_SOCKET_RESOLVE_ROUTE_RUNNING:
	case SMBDIRECT_SOCKET_RESOLVE_ROUTE_FAILED:
	case SMBDIRECT_SOCKET_RDMA_CONNECT_NEEDED:
	case SMBDIRECT_SOCKET_RDMA_CONNECT_RUNNING:
	case SMBDIRECT_SOCKET_RDMA_CONNECT_FAILED:
		/*
		 * rdma_{accept,connect}() never reached
		 * RDMA_CM_EVENT_ESTABLISHED
		 */
		sc->status = SMBDIRECT_SOCKET_DISCONNECTED;
		break;

	case SMBDIRECT_SOCKET_DISCONNECTING:
	case SMBDIRECT_SOCKET_DISCONNECTED:
	case SMBDIRECT_SOCKET_DESTROYED:
		break;
	}

	/*
	 * Wake up all waiters in all wait queues
	 * in order to notice the broken connection.
	 */
	smbdirect_socket_wake_up_all(sc);
}

static void smbdirect_socket_destroy(struct smbdirect_socket *sc)
{
	struct smbdirect_recv_io *recv_io;
	struct smbdirect_recv_io *recv_tmp;
	LIST_HEAD(all_list);
	unsigned long flags;

	smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
		"status=%s first_error=%1pe",
		smbdirect_socket_status_string(sc->status),
		SMBDIRECT_DEBUG_ERR_PTR(sc->first_error));

	/*
	 * This should not never be called in an interrupt!
	 */
	WARN_ON_ONCE(in_interrupt());

	if (sc->status == SMBDIRECT_SOCKET_DESTROYED)
		return;

	WARN_ONCE(sc->status != SMBDIRECT_SOCKET_DISCONNECTED,
		  "status=%s first_error=%1pe",
		  smbdirect_socket_status_string(sc->status),
		  SMBDIRECT_DEBUG_ERR_PTR(sc->first_error));

	/*
	 * Wake up all waiters in all wait queues
	 * in order to notice the broken connection.
	 *
	 * Most likely this was already called via
	 * smbdirect_socket_cleanup_work(), but call it again...
	 */
	smbdirect_socket_wake_up_all(sc);

	disable_work_sync(&sc->disconnect_work);
	disable_work_sync(&sc->connect.work);
	disable_work_sync(&sc->recv_io.posted.refill_work);
	disable_work_sync(&sc->mr_io.recovery_work);
	disable_work_sync(&sc->idle.immediate_work);
	disable_delayed_work_sync(&sc->idle.timer_work);

	if (sc->rdma.cm_id)
		rdma_lock_handler(sc->rdma.cm_id);

	if (sc->ib.qp) {
		smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
			"drain qp\n");
		ib_drain_qp(sc->ib.qp);
	}

	/* It's not possible for upper layer to get to reassembly */
	smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
		"drain the reassembly queue\n");
	spin_lock_irqsave(&sc->recv_io.reassembly.lock, flags);
	list_splice_tail_init(&sc->recv_io.reassembly.list, &all_list);
	spin_unlock_irqrestore(&sc->recv_io.reassembly.lock, flags);
	list_for_each_entry_safe(recv_io, recv_tmp, &all_list, list)
		smbdirect_connection_put_recv_io(recv_io);
	sc->recv_io.reassembly.data_length = 0;

	smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
		"freeing mr list\n");
	smbdirect_connection_destroy_mr_list(sc);

	smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
		"destroying qp\n");
	smbdirect_connection_destroy_qp(sc);
	if (sc->rdma.cm_id) {
		rdma_unlock_handler(sc->rdma.cm_id);
		smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
			"destroying cm_id\n");
		rdma_destroy_id(sc->rdma.cm_id);
		sc->rdma.cm_id = NULL;
	}

	smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
		"destroying mem pools\n");
	smbdirect_connection_destroy_mem_pools(sc);

	sc->status = SMBDIRECT_SOCKET_DESTROYED;

	smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
		"rdma session destroyed\n");
}

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_socket_destroy_sync(struct smbdirect_socket *sc)
{
	smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
		"status=%s first_error=%1pe",
		smbdirect_socket_status_string(sc->status),
		SMBDIRECT_DEBUG_ERR_PTR(sc->first_error));

	/*
	 * This should not never be called in an interrupt!
	 */
	WARN_ON_ONCE(in_interrupt());

	smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
		"cancelling and disable disconnect_work\n");
	disable_work_sync(&sc->disconnect_work);

	smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
		"destroying rdma session\n");
	if (sc->status < SMBDIRECT_SOCKET_DISCONNECTING) {
		/*
		 * SMBDIRECT_LOG_INFO is enough here
		 * as this is the typical case where
		 * we terminate the connection ourself.
		 */
		smbdirect_socket_schedule_cleanup_lvl(sc,
						      SMBDIRECT_LOG_INFO,
						      -ESHUTDOWN);
		smbdirect_socket_cleanup_work(&sc->disconnect_work);
	}
	if (sc->status < SMBDIRECT_SOCKET_DISCONNECTED) {
		smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
			"wait for transport being disconnected\n");
		wait_event(sc->status_wait, sc->status == SMBDIRECT_SOCKET_DISCONNECTED);
		smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
			"waited for transport being disconnected\n");
	}

	/*
	 * Once we reached SMBDIRECT_SOCKET_DISCONNECTED,
	 * we should call smbdirect_socket_destroy()
	 */
	smbdirect_socket_destroy(sc);
	smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
		"status=%s first_error=%1pe",
		smbdirect_socket_status_string(sc->status),
		SMBDIRECT_DEBUG_ERR_PTR(sc->first_error));
}

__maybe_unused /* this is temporary while this file is included in others */
static int smbdirect_socket_wait_for_credits(struct smbdirect_socket *sc,
					     enum smbdirect_socket_status expected_status,
					     int unexpected_errno,
					     wait_queue_head_t *waitq,
					     atomic_t *total_credits,
					     int needed)
{
	int ret;

	if (WARN_ON_ONCE(needed < 0))
		return -EINVAL;

	do {
		if (atomic_sub_return(needed, total_credits) >= 0)
			return 0;

		atomic_add(needed, total_credits);
		ret = wait_event_interruptible(*waitq,
					       atomic_read(total_credits) >= needed ||
					       sc->status != expected_status);

		if (sc->status != expected_status)
			return unexpected_errno;
		else if (ret < 0)
			return ret;
	} while (true);
}
