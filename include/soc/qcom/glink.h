/* Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _SOC_QCOM_GLINK_H_
#define _SOC_QCOM_GLINK_H_

#include <linux/types.h>

/* Maximum size (including null) for channel, edge, or transport names */
#define GLINK_NAME_SIZE 32

/* Maximum packet size for TX and RX */
#define GLINK_MAX_PKT_SIZE SZ_1M

/**
 * G-Link Port State Notification Values
 */
enum {
	GLINK_CONNECTED,
	GLINK_LOCAL_DISCONNECTED,
	GLINK_REMOTE_DISCONNECTED,
};

/**
 * G-Link Open Options
 *
 * Used to define the glink_open_config::options field which is passed into
 * glink_open().
 */
enum {
	GLINK_OPT_INITIAL_XPORT = BIT(0),
	GLINK_OPT_RX_INTENT_NOTIF = BIT(1),
};

/**
 * Open configuration.
 *
 * priv:			Private data passed into user callbacks
 * options:			Open option flags
 * rx_intent_req_timeout_ms:	Timeout for requesting an RX intent, in
 *			milliseconds; if set to 0, timeout is infinite
 * notify_rx:			Receive notification function (required)
 * notify_tx_done:		Transmit-done notification function (required)
 * notify_state:		State-change notification (required)
 * notify_rx_intent_req:	Receive intent request (optional)
 * notify_rxv:			Receive notification function for vector buffers
 *			(required if notify_rx is not provided)
 * notify_sig:			Signal-change notification (optional)
 * notify_rx_tracer_pkt:	Receive notification for tracer packet
 * notify_remote_rx_intent:	Receive notification for remote-queued RX intent
 *
 * This structure is passed into the glink_open() call to setup
 * configuration handles.  All unused fields should be set to 0.
 *
 * The structure is copied internally before the call to glink_open() returns.
 */
struct glink_open_config {
	void *priv;
	uint32_t options;

	const char *transport;
	const char *edge;
	const char *name;
	unsigned int rx_intent_req_timeout_ms;

	void (*notify_rx)(void *handle, const void *priv, const void *pkt_priv,
			const void *ptr, size_t size);
	void (*notify_tx_done)(void *handle, const void *priv,
			const void *pkt_priv, const void *ptr);
	void (*notify_state)(void *handle, const void *priv, unsigned event);
	bool (*notify_rx_intent_req)(void *handle, const void *priv,
			size_t req_size);
	void (*notify_rxv)(void *handle, const void *priv, const void *pkt_priv,
			   void *iovec, size_t size,
			   void * (*vbuf_provider)(void *iovec, size_t offset,
						 size_t *size),
			   void * (*pbuf_provider)(void *iovec, size_t offset,
						 size_t *size));
	void (*notify_rx_sigs)(void *handle, const void *priv,
			uint32_t old_sigs, uint32_t new_sigs);
	void (*notify_rx_abort)(void *handle, const void *priv,
			const void *pkt_priv);
	void (*notify_tx_abort)(void *handle, const void *priv,
			const void *pkt_priv);
	void (*notify_rx_tracer_pkt)(void *handle, const void *priv,
			const void *pkt_priv, const void *ptr, size_t size);
	void (*notify_remote_rx_intent)(void *handle, const void *priv,
					size_t size);
};

enum glink_link_state {
	GLINK_LINK_STATE_UP,
	GLINK_LINK_STATE_DOWN,
};

/**
 * Data structure containing information during Link State callback
 * transport:	String identifying the transport.
 * edge:	String identifying the edge.
 * link_state:	Link state(UP?DOWN).
 */
struct glink_link_state_cb_info {
	const char *transport;
	const char *edge;
	enum glink_link_state link_state;
};

/**
 * Data structure containing information for link state registration
 * transport:	String identifying the transport.
 * edge:	String identifying the edge.
 * glink_link_state_notif_cb:	Callback function used to pass the event.
 */
struct glink_link_info {
	const char *transport;
	const char *edge;
	void (*glink_link_state_notif_cb)(
			struct glink_link_state_cb_info *cb_info,
			void *priv);
};

enum tx_flags {
	GLINK_TX_REQ_INTENT = 0x1,
	GLINK_TX_SINGLE_THREADED = 0x2,
	GLINK_TX_TRACER_PKT = 0x4,
	GLINK_TX_ATOMIC = 0x8,
};

#ifdef CONFIG_MSM_GLINK
/**
 * Open GLINK channel.
 *
 * @cfg_ptr:	Open configuration structure (the structure is copied before
 *		glink_open returns).  All unused fields should be zero-filled.
 *
 * This should not be called from link state callback context by clients.
 * It is recommended that client should invoke this function from their own
 * thread.
 *
 * Return:  Pointer to channel on success, PTR_ERR() with standard Linux
 * error code on failure.
 */
void *glink_open(const struct glink_open_config *cfg_ptr);

/**
 * glink_close() - Close a previously opened channel.
 *
 * @handle:	handle to close
 *
 * Once the closing process has been completed, the GLINK_LOCAL_DISCONNECTED
 * state event will be sent and the channel can be reopened.
 *
 * Return:  0 on success; -EINVAL for invalid handle, -EBUSY is close is
 * already in progress, standard Linux Error code otherwise.
 */
int glink_close(void *handle);

/**
 * glink_tx() - Transmit packet.
 *
 * @handle:	handle returned by glink_open()
 * @pkt_priv:	opaque data value that will be returned to client with
 *		notify_tx_done notification
 * @data:	pointer to the data
 * @size:	size of data
 * @tx_flags:	Flags to specify transmit specific options
 *
 * Return:	-EINVAL for invalid handle; -EBUSY if channel isn't ready for
 *		transmit operation (not fully opened); -EAGAIN if remote side
 *		has not provided a receive intent that is big enough.
 */
int glink_tx(void *handle, void *pkt_priv, void *data, size_t size,
							uint32_t tx_flags);

/**
 * glink_queue_rx_intent() - Register an intent to receive data.
 *
 * @handle:	handle returned by glink_open()
 * @pkt_priv:	opaque data type that is returned when a packet is received
 * size:	maximum size of data to receive
 *
 * Return: 0 for success; standard Linux error code for failure case
 */
int glink_queue_rx_intent(void *handle, const void *pkt_priv, size_t size);

/**
 * glink_rx_intent_exists() - Check if an intent of size exists.
 *
 * @handle:	handle returned by glink_open()
 * @size:	size of an intent to check or 0 for any intent
 *
 * Return:	TRUE if an intent exists with greater than or equal to the size
 *		else FALSE
 */
bool glink_rx_intent_exists(void *handle, size_t size);

/**
 * glink_rx_done() - Return receive buffer to remote side.
 *
 * @handle:	handle returned by glink_open()
 * @ptr:	data pointer provided in the notify_rx() call
 * @reuse:	if true, receive intent is re-used
 *
 * Return: 0 for success; standard Linux error code for failure case
 */
int glink_rx_done(void *handle, const void *ptr, bool reuse);

/**
 * glink_txv() - Transmit a packet in vector form.
 *
 * @handle:	handle returned by glink_open()
 * @pkt_priv:	opaque data value that will be returned to client with
 *		notify_tx_done notification
 * @iovec:	pointer to the vector (must remain valid until notify_tx_done
 *		notification)
 * @size:	size of data/vector
 * @vbuf_provider: Client provided helper function to iterate the vector
 *		in physical address space
 * @pbuf_provider: Client provided helper function to iterate the vector
 *		in virtual address space
 * @tx_flags:	Flags to specify transmit specific options
 *
 * Return: -EINVAL for invalid handle; -EBUSY if channel isn't ready for
 *           transmit operation (not fully opened); -EAGAIN if remote side has
 *           not provided a receive intent that is big enough.
 */
int glink_txv(void *handle, void *pkt_priv,
	      void *iovec, size_t size,
	      void * (*vbuf_provider)(void *iovec, size_t offset, size_t *size),
	      void * (*pbuf_provider)(void *iovec, size_t offset, size_t *size),
	      uint32_t tx_flags);

/**
 * glink_sigs_set() - Set the local signals for the GLINK channel
 *
 * @handle:	handle returned by glink_open()
 * @sigs:	modified signal value
 *
 * Return: 0 for success; standard Linux error code for failure case
 */
int glink_sigs_set(void *handle, uint32_t sigs);

/**
 * glink_sigs_local_get() - Get the local signals for the GLINK channel
 *
 * handle:	handle returned by glink_open()
 * sigs:	Pointer to hold the signals
 *
 * Return: 0 for success; standard Linux error code for failure case
 */
int glink_sigs_local_get(void *handle, uint32_t *sigs);

/**
 * glink_sigs_remote_get() - Get the Remote signals for the GLINK channel
 *
 * handle:	handle returned by glink_open()
 * sigs:	Pointer to hold the signals
 *
 * Return: 0 for success; standard Linux error code for failure case
 */
int glink_sigs_remote_get(void *handle, uint32_t *sigs);

/**
 * glink_register_link_state_cb() - Register for link state notification
 * @link_info:	Data structure containing the link identification and callback.
 * @priv:	Private information to be passed with the callback.
 *
 * This function is used to register a notifier to receive the updates about a
 * link's/transport's state. This notifier needs to be registered first before
 * an attempt to open a channel.
 *
 * Return: a reference to the notifier handle.
 */
void *glink_register_link_state_cb(struct glink_link_info *link_info,
				   void *priv);

/**
 * glink_unregister_link_state_cb() - Unregister the link state notification
 * notif_handle:	Handle to be unregistered.
 *
 * This function is used to unregister a notifier to stop receiving the updates
 * about a link's/transport's state.
 */
void glink_unregister_link_state_cb(void *notif_handle);

/**
 * glink_qos_latency() - Register the latency QoS requirement
 * @handle:	Channel handle in which the latency is required.
 * @latency_us:	Latency requirement in units of micro-seconds.
 * @pkt_size:	Worst case packet size for which the latency is required.
 *
 * This function is used to register the latency requirement for a channel
 * and ensures that the latency requirement for this channel is met without
 * impacting the existing latency requirements of other channels.
 *
 * Return: 0 if QoS request is achievable, standard Linux error codes on error
 */
int glink_qos_latency(void *handle, unsigned long latency_us, size_t pkt_size);

/**
 * glink_qos_cancel() - Cancel or unregister the QoS request
 * @handle:	Channel handle for which the QoS request is cancelled.
 *
 * This function is used to cancel/unregister the QoS requests for a channel.
 *
 * Return: 0 on success, standard Linux error codes on failure
 */
int glink_qos_cancel(void *handle);

/**
 * glink_qos_start() - Start of the transmission requiring QoS
 * @handle:	Channel handle in which the transmit activity is performed.
 *
 * This function is called by the clients to indicate G-Link regarding the
 * start of the transmission which requires a certain QoS. The clients
 * must account for the QoS ramp time to ensure meeting the QoS.
 *
 * Return: 0 on success, standard Linux error codes on failure
 */
int glink_qos_start(void *handle);

/**
 * glink_qos_get_ramp_time() - Get the QoS ramp time
 * @handle:	Channel handle for which the QoS ramp time is required.
 * @pkt_size:	Worst case packet size.
 *
 * This function is called by the clients to obtain the ramp time required
 * to meet the QoS requirements.
 *
 * Return: QoS ramp time is returned in units of micro-seconds
 */
unsigned long glink_qos_get_ramp_time(void *handle, size_t pkt_size);

#else /* CONFIG_MSM_GLINK */
static inline void *glink_open(const struct glink_open_config *cfg_ptr)
{
	return NULL;
}

static inline int glink_close(void *handle)
{
	return -ENODEV;
}

static inline int glink_tx(void *handle, void *pkt_priv, void *data,
					size_t size, uint32_t tx_flags)
{
	return -ENODEV;
}

static inline int glink_queue_rx_intent(void *handle, const void *pkt_priv,
								size_t size)
{
	return -ENODEV;
}

static inline bool glink_rx_intent_exists(void *handle, size_t size)
{
	return -ENODEV;
}

static inline int glink_rx_done(void *handle, const void *ptr, bool reuse)
{
	return -ENODEV;
}

static inline int glink_txv(void *handle, void *pkt_priv,
	      void *iovec, size_t size,
	      void * (*vbuf_provider)(void *iovec, size_t offset, size_t *size),
	      void * (*pbuf_provider)(void *iovec, size_t offset, size_t *size),
	      uint32_t tx_flags)
{
	return -ENODEV;
}

static inline int glink_sigs_set(void *handle, uint32_t sigs)
{
	return -ENODEV;
}

static inline int glink_sigs_local_get(void *handle, uint32_t *sigs)
{
	return -ENODEV;
}

static inline int glink_sigs_remote_get(void *handle, uint32_t *sigs)
{
	return -ENODEV;
}

static inline void *glink_register_link_state_cb(
				struct glink_link_info *link_info, void *priv)
{
	return NULL;
}

static inline void glink_unregister_link_state_cb(void *notif_handle)
{
}

static inline int glink_qos_latency(void *handle, unsigned long latency_us,
				    size_t pkt_size)
{
	return -ENODEV;
}

static inline int glink_qos_cancel(void *handle)
{
	return -ENODEV;
}

static inline int glink_qos_start(void *handle)
{
	return -ENODEV;
}

static inline unsigned long glink_qos_get_ramp_time(void *handle,
						    size_t pkt_size)
{
	return 0;
}
#endif /* CONFIG_MSM_GLINK */
#endif /* _SOC_QCOM_GLINK_H_ */
