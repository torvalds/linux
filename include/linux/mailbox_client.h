/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MAILBOX_CLIENT_H
#define __MAILBOX_CLIENT_H

#include <linux/mailbox.h>

/**
 * struct ipc_client - User of a mailbox
 * @chan_name: the "controller:channel" this client wants
 * @cl_id: The identity to associate any tx/rx data with
 * @rxcb: atomic callback to provide client the data received
 * @txcb: atomic callback to tell client of data transmission
 * @tx_block: if the ipc_send_message should block until data is transmitted
 * @tx_tout: Max block period in ms before TX is assumed failure
 * @knows_txdone: if the client could run the TX state machine. Usually if
 *    the client receives some ACK packet for transmission. Unused if the
 *    controller already has TX_Done/RTR IRQ.
 * @link_data: Optional controller specific parameters during channel request
 */
struct ipc_client {
	char *chan_name;
	void *cl_id;
	void (*rxcb)(void *cl_id, void *mssg);
	void (*txcb)(void *cl_id, void *mssg, enum xfer_result r);
	bool tx_block;
	unsigned long tx_tout;
	bool knows_txdone;
	void *link_data;
};

/**
 * The Client specifies its requirements and capabilities while asking for
 * a channel/mailbox by name. It can't be called from atomic context.
 * The channel is exclusively allocated and can't be used by another
 * client before the owner calls ipc_free_channel.
 */
void *ipc_request_channel(struct ipc_client *cl);

/**
 * For client to submit data to the controller destined for a remote
 * processor. If the client had set 'tx_block', the call will return
 * either when the remote receives the data or when 'tx_tout' millisecs
 * run out.
 *  In non-blocking mode, the requests are buffered by the API and a
 * non-zero token is returned for each queued request. If the queue
 * was full the returned token will be 0. Upon failure or successful
 * TX, the API calls 'txcb' from atomic context, from which the client
 * could submit yet another request.
 *  In blocking mode, 'txcb' is not called, effectively making the
 * queue length 1. The returned value is 0 if TX timed out, some
 * non-zero value upon success.
 */
request_token_t ipc_send_message(void *channel, void *mssg);

/**
 * The way for a client to run the TX state machine. This works
 * only if the client sets 'knows_txdone' and the IPC controller
 * doesn't get an IRQ for TX_Done/Remote_RTR.
 */
void ipc_client_txdone(void *channel, enum xfer_result r);

/**
 * The client relinquishes control of a mailbox by this call,
 * make it available to other clients.
 * The ipc_request/free_channel are light weight calls, so the
 * client should avoid holding it when it doesn't need to
 * transfer data.
 */
void ipc_free_channel(void *channel);

/**
 * The client make ask the API to be notified when a particular channel
 * becomes available to be acquired again.
 */
int ipc_notify_chan_register(const char *name, struct notifier_block *nb);

/**
 * The client is no more interested in acquiring the channel.
 */
void ipc_notify_chan_unregister(const char *name, struct notifier_block *nb);

#endif /* __MAILBOX_CLIENT_H */
