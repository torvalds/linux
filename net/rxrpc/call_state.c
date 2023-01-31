// SPDX-License-Identifier: GPL-2.0-or-later
/* Call state changing functions.
 *
 * Copyright (C) 2022 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include "ar-internal.h"

/*
 * Transition a call to the complete state.
 */
bool rxrpc_set_call_completion(struct rxrpc_call *call,
				 enum rxrpc_call_completion compl,
				 u32 abort_code,
				 int error)
{
	if (__rxrpc_call_state(call) == RXRPC_CALL_COMPLETE)
		return false;

	call->abort_code = abort_code;
	call->error = error;
	call->completion = compl;
	/* Allow reader of completion state to operate locklessly */
	rxrpc_set_call_state(call, RXRPC_CALL_COMPLETE);
	trace_rxrpc_call_complete(call);
	wake_up(&call->waitq);
	rxrpc_notify_socket(call);
	return true;
}

/*
 * Record that a call successfully completed.
 */
bool rxrpc_call_completed(struct rxrpc_call *call)
{
	return rxrpc_set_call_completion(call, RXRPC_CALL_SUCCEEDED, 0, 0);
}

/*
 * Record that a call is locally aborted.
 */
bool rxrpc_abort_call(struct rxrpc_call *call, rxrpc_seq_t seq,
		      u32 abort_code, int error, enum rxrpc_abort_reason why)
{
	trace_rxrpc_abort(call->debug_id, why, call->cid, call->call_id, seq,
			  abort_code, error);
	if (!rxrpc_set_call_completion(call, RXRPC_CALL_LOCALLY_ABORTED,
				       abort_code, error))
		return false;
	if (test_bit(RXRPC_CALL_EXPOSED, &call->flags))
		rxrpc_send_abort_packet(call);
	return true;
}

/*
 * Record that a call errored out before even getting off the ground, thereby
 * setting the state to allow it to be destroyed.
 */
void rxrpc_prefail_call(struct rxrpc_call *call, enum rxrpc_call_completion compl,
			int error)
{
	call->abort_code	= RX_CALL_DEAD;
	call->error		= error;
	call->completion	= compl;
	call->_state		= RXRPC_CALL_COMPLETE;
	trace_rxrpc_call_complete(call);
	WARN_ON_ONCE(__test_and_set_bit(RXRPC_CALL_RELEASED, &call->flags));
}
