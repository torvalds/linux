// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (C) 2018, LG Electronics.
 *   Copyright (c) 2025, Stefan Metzmacher
 */

#include "internal.h"

static int smbdirect_listen_rdma_event_handler(struct rdma_cm_id *id,
					       struct rdma_cm_event *event);

/*
 * This is called by a socket that failed while it
 * is still on the pending or ready list of its
 * listener, typically at the end of
 * smbdirect_socket_cleanup_work(), when the
 * disconnect was already started.
 *
 * It moves itself to the orphaned list and
 * lets smbdirect_listen_purge_orphaned_work()
 * release it. Otherwise it would stay on the
 * pending or ready list until the listener is
 * destroyed and fill up the backlog, so that
 * no new connections would be accepted.
 *
 * It's fine to call this more than once.
 */
void smbdirect_listen_orphan_socket(struct smbdirect_socket *sc)
{
	struct smbdirect_socket *lsc;
	unsigned long flags;

	/*
	 * The memory of the listener is freed via
	 * kfree_rcu(), so it's safe to dereference it
	 * under rcu_read_lock(), even if sc->accept.listener
	 * is cleared and the listener is released concurrently.
	 */
	rcu_read_lock();
	lsc = READ_ONCE(sc->accept.listener);
	if (!lsc) {
		rcu_read_unlock();
		return;
	}

	spin_lock_irqsave(&lsc->listen.lock, flags);
	if (sc->accept.listener == lsc) {
		list_move_tail(&sc->accept.list, &lsc->listen.orphaned);
		queue_work(lsc->workqueues.cleanup, &lsc->listen.purge_orphaned_work);
	}
	spin_unlock_irqrestore(&lsc->listen.lock, flags);
	rcu_read_unlock();
}

static void smbdirect_listen_purge_orphaned_work(struct work_struct *work)
{
	struct smbdirect_socket *lsc =
		container_of(work, struct smbdirect_socket, listen.purge_orphaned_work);
	struct smbdirect_socket *psc, *tsc;
	LIST_HEAD(orphaned_list);
	unsigned long flags;

	/*
	 * Clearing accept.listener under listen.lock
	 * makes us responsible for releasing them.
	 */
	spin_lock_irqsave(&lsc->listen.lock, flags);
	list_splice_tail_init(&lsc->listen.orphaned, &orphaned_list);
	list_for_each_entry(psc, &orphaned_list, accept.list)
		WRITE_ONCE(psc->accept.listener, NULL);
	spin_unlock_irqrestore(&lsc->listen.lock, flags);

	/*
	 * We don't hold the listener's rdma_lock_handler()
	 * lock here, see smbdirect_socket_destroy()
	 * for why that's important.
	 */
	list_for_each_entry_safe(psc, tsc, &orphaned_list, accept.list) {
		list_del_init(&psc->accept.list);
		smbdirect_socket_release(psc);
	}
}

int smbdirect_socket_listen(struct smbdirect_socket *sc, int backlog)
{
	int ret;

	if (backlog < 0)
		return -EINVAL;
	if (!backlog)
		backlog = 1; /* use 1 as default for now */

	if (sc->first_error)
		return -EINVAL;

	if (sc->status != SMBDIRECT_SOCKET_CREATED)
		return -EINVAL;

	if (WARN_ON_ONCE(!sc->rdma.cm_id))
		return -EINVAL;

	if (sc->rdma.cm_id->device)
		smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
			"try to listen on addr: %pISpsfc dev: %.*s\n",
			&sc->rdma.cm_id->route.addr.src_addr,
			IB_DEVICE_NAME_MAX,
			sc->rdma.cm_id->device->name);
	else
		smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
			"try to listen on addr: %pISpsfc\n",
			&sc->rdma.cm_id->route.addr.src_addr);

	/* already checked above */
	WARN_ON_ONCE(sc->status != SMBDIRECT_SOCKET_CREATED);
	sc->status = SMBDIRECT_SOCKET_LISTENING;
	sc->rdma.expected_event = RDMA_CM_EVENT_CONNECT_REQUEST;
	rdma_lock_handler(sc->rdma.cm_id);
	sc->rdma.cm_id->event_handler = smbdirect_listen_rdma_event_handler;
	rdma_unlock_handler(sc->rdma.cm_id);

	INIT_WORK(&sc->listen.purge_orphaned_work,
		  smbdirect_listen_purge_orphaned_work);

	ret = rdma_listen(sc->rdma.cm_id, backlog);
	if (ret) {
		sc->first_error = ret;
		sc->status = SMBDIRECT_SOCKET_DISCONNECTED;
		if (sc->rdma.cm_id->device)
			smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
				"listening failed %1pe on addr: %pISpsfc dev: %.*s\n",
				SMBDIRECT_DEBUG_ERR_PTR(ret),
				&sc->rdma.cm_id->route.addr.src_addr,
				IB_DEVICE_NAME_MAX,
				sc->rdma.cm_id->device->name);
		else
			smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
				"listening failed %1pe on addr: %pISpsfc\n",
				SMBDIRECT_DEBUG_ERR_PTR(ret),
				&sc->rdma.cm_id->route.addr.src_addr);
		return ret;
	}

	/*
	 * This is a value > 0, checked above,
	 * so we are able to use sc->listen.backlog == -1,
	 * as indication that the socket was never
	 * a listener.
	 */
	sc->listen.backlog = backlog;

	if (sc->rdma.cm_id->device)
		smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
			"listening on addr: %pISpsfc dev: %.*s\n",
			&sc->rdma.cm_id->route.addr.src_addr,
			IB_DEVICE_NAME_MAX,
			sc->rdma.cm_id->device->name);
	else
		smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_INFO,
			"listening on addr: %pISpsfc\n",
			&sc->rdma.cm_id->route.addr.src_addr);

	/*
	 * The rest happens async via smbdirect_listen_rdma_event_handler()
	 */
	return 0;
}
EXPORT_SYMBOL_GPL(smbdirect_socket_listen);

static int smbdirect_new_rdma_event_handler(struct rdma_cm_id *new_id,
					    struct rdma_cm_event *event)
{
	int ret = -ESTALE;

	/*
	 * This should be replaced before any real work
	 * starts! So it should never be called!
	 */

	if (event->event == RDMA_CM_EVENT_DEVICE_REMOVAL)
		ret = -ENETDOWN;
	if (IS_ERR(SMBDIRECT_DEBUG_ERR_PTR(event->status)))
		ret = event->status;
	WARN_ONCE(1,
		  "%s should not be called! event=%s status=%d => ret=%1pe\n",
		  __func__,
		  rdma_event_msg(event->event),
		  event->status,
		  SMBDIRECT_DEBUG_ERR_PTR(ret));
	return -ESTALE;
}

static int smbdirect_listen_connect_request(struct smbdirect_socket *lsc,
					    struct rdma_cm_id *new_id,
					    const struct rdma_cm_event *event);

static int smbdirect_listen_rdma_event_handler(struct rdma_cm_id *new_id,
					       struct rdma_cm_event *event)
{
	struct smbdirect_socket *lsc = new_id->context;
	int ret;

	if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
		new_id->context = NULL;
		new_id->event_handler = smbdirect_new_rdma_event_handler;
	} else
		new_id = NULL;

	/*
	 * cma_cm_event_handler() has
	 * lockdep_assert_held(&id_priv->handler_mutex);
	 *
	 * Mutexes are not allowed in interrupts,
	 * and we rely on not being in an interrupt here,
	 * as we might sleep.
	 */
	WARN_ON_ONCE(in_interrupt());

	if (event->status || event->event != lsc->rdma.expected_event) {
		ret = -ECONNABORTED;

		if (event->event == RDMA_CM_EVENT_DEVICE_REMOVAL)
			ret = -ENETDOWN;
		if (IS_ERR(SMBDIRECT_DEBUG_ERR_PTR(event->status)))
			ret = event->status;

		smbdirect_log_rdma_event(lsc, SMBDIRECT_LOG_ERR,
			"%s (first_error=%1pe, expected=%s) => event=%s status=%d => ret=%1pe\n",
			smbdirect_socket_status_string(lsc->status),
			SMBDIRECT_DEBUG_ERR_PTR(lsc->first_error),
			rdma_event_msg(lsc->rdma.expected_event),
			rdma_event_msg(event->event),
			event->status,
			SMBDIRECT_DEBUG_ERR_PTR(ret));

		/*
		 * In case of error return it and let the caller
		 * destroy new_id
		 */
		smbdirect_socket_schedule_cleanup(lsc, ret);
		return new_id ? ret : 0;
	}

	smbdirect_log_rdma_event(lsc, SMBDIRECT_LOG_INFO,
		"%s (first_error=%1pe) event=%s\n",
		smbdirect_socket_status_string(lsc->status),
		SMBDIRECT_DEBUG_ERR_PTR(lsc->first_error),
		rdma_event_msg(event->event));

	/*
	 * In case of error return it and let the caller
	 * destroy new_id
	 */
	if (lsc->first_error)
		return new_id ? lsc->first_error : 0;

	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		WARN_ON_ONCE(lsc->status != SMBDIRECT_SOCKET_LISTENING);

		/*
		 * In case of error return it and let the caller
		 * destroy new_id
		 */
		ret = smbdirect_listen_connect_request(lsc, new_id, event);
		if (ret)
			return ret;
		return 0;

	default:
		break;
	}

	/*
	 * This is an internal error
	 */
	WARN_ON_ONCE(lsc->rdma.expected_event != RDMA_CM_EVENT_CONNECT_REQUEST);
	smbdirect_socket_schedule_cleanup(lsc, -EINVAL);
	return 0;
}

static int smbdirect_listen_connect_request(struct smbdirect_socket *lsc,
					    struct rdma_cm_id *new_id,
					    const struct rdma_cm_event *event)
{
	const struct smbdirect_socket_parameters *lsp = &lsc->parameters;
	struct smbdirect_socket *nsc;
	unsigned long flags;
	size_t backlog = max_t(size_t, 1, lsc->listen.backlog);
	size_t psockets;
	size_t rsockets;
	size_t osockets;
	int ret;

	if (!smbdirect_frwr_is_supported(&new_id->device->attrs)) {
		smbdirect_log_rdma_event(lsc, SMBDIRECT_LOG_ERR,
			"Fast Registration Work Requests (FRWR) is not supported device %.*s\n",
			IB_DEVICE_NAME_MAX,
			new_id->device->name);
		smbdirect_log_rdma_event(lsc, SMBDIRECT_LOG_ERR,
			"Device capability flags = %llx max_fast_reg_page_list_len = %u\n",
			new_id->device->attrs.device_cap_flags,
			new_id->device->attrs.max_fast_reg_page_list_len);
		return -EPROTONOSUPPORT;
	}

	if (lsp->flags & SMBDIRECT_FLAG_PORT_RANGE_ONLY_IB &&
	    !rdma_ib_or_roce(new_id->device, new_id->port_num)) {
		smbdirect_log_rdma_event(lsc, SMBDIRECT_LOG_ERR,
			"Not IB: device: %.*s IW:%u local: %pISpsfc remote: %pISpsfc\n",
			IB_DEVICE_NAME_MAX,
			new_id->device->name,
			rdma_protocol_iwarp(new_id->device, new_id->port_num),
			&new_id->route.addr.src_addr,
			&new_id->route.addr.dst_addr);
		return -EPROTONOSUPPORT;
	}
	if (lsp->flags & SMBDIRECT_FLAG_PORT_RANGE_ONLY_IW &&
	    !rdma_protocol_iwarp(new_id->device, new_id->port_num)) {
		smbdirect_log_rdma_event(lsc, SMBDIRECT_LOG_ERR,
			"Not IW: device: %.*s IB:%u local: %pISpsfc remote: %pISpsfc\n",
			IB_DEVICE_NAME_MAX,
			new_id->device->name,
			rdma_ib_or_roce(new_id->device, new_id->port_num),
			&new_id->route.addr.src_addr,
			&new_id->route.addr.dst_addr);
		return -EPROTONOSUPPORT;
	}

	spin_lock_irqsave(&lsc->listen.lock, flags);
	psockets = list_count_nodes(&lsc->listen.pending);
	rsockets = list_count_nodes(&lsc->listen.ready);
	/*
	 * Orphaned sockets still count against
	 * the backlog until they are released by
	 * smbdirect_listen_purge_orphaned_work().
	 */
	osockets = list_count_nodes(&lsc->listen.orphaned);
	spin_unlock_irqrestore(&lsc->listen.lock, flags);

	if (psockets > backlog ||
	    rsockets > backlog ||
	    osockets > backlog ||
	    (psockets + rsockets + osockets) > backlog) {
		smbdirect_log_rdma_event(lsc, SMBDIRECT_LOG_ERR,
			"Backlog[%d][%zu] full pending[%zu] ready[%zu] orphaned[%zu]\n",
			lsc->listen.backlog, backlog, psockets, rsockets, osockets);
		return -EBUSY;
	}

	ret = smbdirect_socket_create_accepting(new_id, &nsc);
	if (ret)
		goto socket_init_failed;

	nsc->logging = lsc->logging;
	ret = smbdirect_socket_set_initial_parameters(nsc, &lsc->parameters);
	if (ret)
		goto set_params_failed;
	ret = smbdirect_socket_set_kernel_settings(nsc,
						   lsc->ib.poll_ctx,
						   lsc->send_io.mem.gfp_mask);
	if (ret)
		goto set_settings_failed;

	/*
	 * Publish nsc on the pending list with accept.listener set before
	 * smbdirect_accept_connect_request() calls rdma_accept(). Once the
	 * connection can establish, smbdirect_accept_negotiate_recv_work()
	 * may run and it must observe accept.listener, otherwise nsc would
	 * be left stranded on the pending list forever.
	 *
	 * The listener's handler_mutex is held while we're called, so
	 * smbdirect_socket_destroy() of the listener can't reach nsc on the
	 * pending list before we're done.
	 *
	 * From here nsc is published on the pending list: on failure of
	 * smbdirect_accept_connect_request() below we schedule nsc's teardown,
	 * which orphans nsc off the listener so
	 * smbdirect_listen_purge_orphaned_work() -> smbdirect_socket_release()
	 * releases it.
	 */
	spin_lock_irqsave(&lsc->listen.lock, flags);
	list_add_tail(&nsc->accept.list, &lsc->listen.pending);
	WRITE_ONCE(nsc->accept.listener, lsc);
	spin_unlock_irqrestore(&lsc->listen.lock, flags);

	ret = smbdirect_accept_connect_request(nsc, &event->param.conn);
	if (ret) {
		/*
		 * The rdma_cm core holds both the listener's and nsc's
		 * id_priv->handler_mutex across this CONNECT_REQUEST handler,
		 * so neither can go away under us here and nsc cannot receive
		 * any rdma event: it is safe to hand nsc to its teardown.
		 *
		 * That teardown must be deferred though: nsc->rdma.cm_id
		 * (= new_id) has its handler_mutex held by us, so it cannot be
		 * destroyed synchronously from here.
		 * smbdirect_socket_schedule_cleanup() only queues work;
		 * smbdirect_socket_cleanup_work() then orphans nsc off the
		 * listener and smbdirect_listen_purge_orphaned_work() releases
		 * it, destroying nsc's cm_id with rdma_destroy_id().
		 */
		smbdirect_socket_schedule_cleanup(nsc, ret);
	}

	/*
	 * Always return 0 so the rdma_cm core keeps new_id: nsc owns it now.
	 * On success it stays connected; on failure its deferred teardown
	 * above destroys it.
	 */
	return 0;

set_settings_failed:
set_params_failed:
	/*
	 * The caller will destroy new_id
	 */
	nsc->ib.dev = NULL;
	nsc->rdma.cm_id = NULL;
	smbdirect_socket_release(nsc);
socket_init_failed:
	return ret;
}
