// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (C) 2018, LG Electronics.
 *   Copyright (c) 2025, Stefan Metzmacher
 */

#include "smbdirect_internal.h"

static int smbdirect_listen_rdma_event_handler(struct rdma_cm_id *id,
					       struct rdma_cm_event *event);

__SMBDIRECT_PUBLIC__
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
__SMBDIRECT_EXPORT_SYMBOL__(smbdirect_socket_listen);

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
	spin_unlock_irqrestore(&lsc->listen.lock, flags);

	if (psockets > backlog ||
	    rsockets > backlog ||
	    (psockets + rsockets) > backlog) {
		smbdirect_log_rdma_event(lsc, SMBDIRECT_LOG_ERR,
			"Backlog[%d][%zu] full pending[%zu] ready[%zu]\n",
			lsc->listen.backlog, backlog, psockets, rsockets);
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

	spin_lock_irqsave(&lsc->listen.lock, flags);
	list_add_tail(&nsc->accept.list, &lsc->listen.pending);
	nsc->accept.listener = lsc;
	spin_unlock_irqrestore(&lsc->listen.lock, flags);

	ret = smbdirect_accept_connect_request(nsc, &event->param.conn);
	if (ret)
		goto accept_connect_failed;

	return 0;

accept_connect_failed:
	spin_lock_irqsave(&lsc->listen.lock, flags);
	list_del_init(&nsc->accept.list);
	nsc->accept.listener = NULL;
	spin_unlock_irqrestore(&lsc->listen.lock, flags);
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
