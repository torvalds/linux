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
bool __rxrpc_set_call_completion(struct rxrpc_call *call,
				 enum rxrpc_call_completion compl,
				 u32 abort_code,
				 int error)
{
	if (call->state < RXRPC_CALL_COMPLETE) {
		call->abort_code = abort_code;
		call->error = error;
		call->completion = compl;
		call->state = RXRPC_CALL_COMPLETE;
		trace_rxrpc_call_complete(call);
		wake_up(&call->waitq);
		rxrpc_notify_socket(call);
		return true;
	}
	return false;
}

bool rxrpc_set_call_completion(struct rxrpc_call *call,
			       enum rxrpc_call_completion compl,
			       u32 abort_code,
			       int error)
{
	bool ret = false;

	if (call->state < RXRPC_CALL_COMPLETE) {
		write_lock(&call->state_lock);
		ret = __rxrpc_set_call_completion(call, compl, abort_code, error);
		write_unlock(&call->state_lock);
	}
	return ret;
}

/*
 * Record that a call successfully completed.
 */
bool __rxrpc_call_completed(struct rxrpc_call *call)
{
	return __rxrpc_set_call_completion(call, RXRPC_CALL_SUCCEEDED, 0, 0);
}

bool rxrpc_call_completed(struct rxrpc_call *call)
{
	bool ret = false;

	if (call->state < RXRPC_CALL_COMPLETE) {
		write_lock(&call->state_lock);
		ret = __rxrpc_call_completed(call);
		write_unlock(&call->state_lock);
	}
	return ret;
}

/*
 * Record that a call is locally aborted.
 */
bool __rxrpc_abort_call(struct rxrpc_call *call, rxrpc_seq_t seq,
			u32 abort_code, int error, enum rxrpc_abort_reason why)
{
	trace_rxrpc_abort(call->debug_id, why, call->cid, call->call_id, seq,
			  abort_code, error);
	return __rxrpc_set_call_completion(call, RXRPC_CALL_LOCALLY_ABORTED,
					   abort_code, error);
}

bool rxrpc_abort_call(struct rxrpc_call *call, rxrpc_seq_t seq,
		      u32 abort_code, int error, enum rxrpc_abort_reason why)
{
	bool ret;

	write_lock(&call->state_lock);
	ret = __rxrpc_abort_call(call, seq, abort_code, error, why);
	write_unlock(&call->state_lock);
	if (ret && test_bit(RXRPC_CALL_EXPOSED, &call->flags))
		rxrpc_send_abort_packet(call);
	return ret;
}
