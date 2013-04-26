/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MAILBOX_CONTROLLER_H
#define __MAILBOX_CONTROLLER_H

#include <linux/mailbox.h>

/**
 * struct ipc_link - s/w representation of a communication link
 * @link_name: Literal name assigned to the link. Physically
 *    identical channels may have the same name.
 * @api_priv: hook for the API to map its private data on the link
 *    Controller driver must not touch it.
 */
struct ipc_link {
	char link_name[16];
	void *api_priv;
};

/**
 * struct ipc_link - s/w representation of a communication link
 * @send_data: The API asks the IPC controller driver, in atomic
 *    context try to transmit a message on the bus. Returns 0 if
 *    data is accepted for transmission, -EBUSY while rejecting
 *    if the remote hasn't yet read the last data sent. Actual
 *    transmission of data is reported by the controller via
 *    ipc_link_txdone (if it has some TX ACK irq). It must not
 *    block.
 * @startup: Called when a client requests the link. The controller
 *    could ask clients for additional parameters of communication
 *    to be provided via client's link_data. This call may block.
 *    After this call the Controller must forward any data received
 *    on the link by calling ipc_link_received_data (which won't block)
 * @shutdown: Called when a client relinquishes control of a link.
 *    This call may block too. The controller must not forwared
 *    any received data anymore.
 * @is_ready: If the controller sets 'txdone_poll', the API calls
 *    this to poll status of last TX. The controller must give priority
 *    to IRQ method over polling and never set both txdone_poll and
 *    txdone_irq. Only in polling mode 'send_data' is expected to
 *    return -EBUSY. Used only if txdone_poll:=true && txdone_irq:=false
 */
struct ipc_link_ops {
	int (*send_data)(struct ipc_link *link, void *data);
	int (*startup)(struct ipc_link *link, void *params);
	void (*shutdown)(struct ipc_link *link);
	bool (*is_ready)(struct ipc_link *link);
};

/**
 * struct ipc_controller - Controller of a class of communication links
 * @controller_name: Literal name of the controller.
 * @ops: Operators that work on each communication link
 * @links: Null terminated array of links.
 * @txdone_irq: Indicates if the controller can report to API when the
 *    last transmitted data was read by the remote. Eg, if it has some
 *    TX ACK irq.
 * @txdone_poll: If the controller can read but not report the TX done.
 *    Eg, is some register shows the TX status but no interrupt rises.
 *    Ignored if 'txdone_irq' is set.
 * @txpoll_period: If 'txdone_poll' is in effect, the API polls for
 *    last TX's status after these many millisecs
 */
struct ipc_controller {
	char controller_name[16];
	struct ipc_link_ops *ops;
	struct ipc_link **links;
	bool txdone_irq;
	bool txdone_poll;
	unsigned txpoll_period;
};

/**
 * The controller driver registers its communication links to the
 * global pool managed by the API.
 */
int ipc_links_register(struct ipc_controller *ipc_con);

/**
 * After startup and before shutdown any data received on the link
 * is pused to the API via atomic ipc_link_received_data() API.
 * The controller should ACK the RX only after this call returns.
 */
void ipc_link_received_data(struct ipc_link *link, void *data);

/**
 * The controller the has IRQ for TX ACK calls this atomic API
 * to tick the TX state machine. It works only if txdone_irq
 * is set by the controller.
 */
void ipc_link_txdone(struct ipc_link *link, enum xfer_result r);

/**
 * Purge the links from the global pool maintained by the API.
 */
void ipc_links_unregister(struct ipc_controller *ipc_con);

#endif /* __MAILBOX_CONTROLLER_H */
