#ifndef __LINUX_USB_DLN2_H
#define __LINUX_USB_DLN2_H

#define DLN2_CMD(cmd, id)		((cmd) | ((id) << 8))

struct dln2_platform_data {
	u16 handle;		/* sub-driver handle (internally used only) */
	u8 port;		/* I2C/SPI port */
};

/**
 * dln2_event_cb_t - event callback function signature
 *
 * @pdev - the sub-device that registered this callback
 * @echo - the echo header field received in the message
 * @data - the data payload
 * @len  - the data payload length
 *
 * The callback function is called in interrupt context and the data payload is
 * only valid during the call. If the user needs later access of the data, it
 * must copy it.
 */

typedef void (*dln2_event_cb_t)(struct platform_device *pdev, u16 echo,
				const void *data, int len);

/**
 * dl2n_register_event_cb - register a callback function for an event
 *
 * @pdev - the sub-device that registers the callback
 * @event - the event for which to register a callback
 * @event_cb - the callback function
 *
 * @return 0 in case of success, negative value in case of error
 */
int dln2_register_event_cb(struct platform_device *pdev, u16 event,
			   dln2_event_cb_t event_cb);

/**
 * dln2_unregister_event_cb - unregister the callback function for an event
 *
 * @pdev - the sub-device that registered the callback
 * @event - the event for which to register a callback
 */
void dln2_unregister_event_cb(struct platform_device *pdev, u16 event);

/**
 * dln2_transfer - issue a DLN2 command and wait for a response and the
 * associated data
 *
 * @pdev - the sub-device which is issuing this transfer
 * @cmd - the command to be sent to the device
 * @obuf - the buffer to be sent to the device; it can be NULL if the user
 *	doesn't need to transmit data with this command
 * @obuf_len - the size of the buffer to be sent to the device
 * @ibuf - any data associated with the response will be copied here; it can be
 *	NULL if the user doesn't need the response data
 * @ibuf_len - must be initialized to the input buffer size; it will be modified
 *	to indicate the actual data transferred;
 *
 * @return 0 for success, negative value for errors
 */
int dln2_transfer(struct platform_device *pdev, u16 cmd,
		  const void *obuf, unsigned obuf_len,
		  void *ibuf, unsigned *ibuf_len);

/**
 * dln2_transfer_rx - variant of @dln2_transfer() where TX buffer is not needed
 *
 * @pdev - the sub-device which is issuing this transfer
 * @cmd - the command to be sent to the device
 * @ibuf - any data associated with the response will be copied here; it can be
 *	NULL if the user doesn't need the response data
 * @ibuf_len - must be initialized to the input buffer size; it will be modified
 *	to indicate the actual data transferred;
 *
 * @return 0 for success, negative value for errors
 */

static inline int dln2_transfer_rx(struct platform_device *pdev, u16 cmd,
				   void *ibuf, unsigned *ibuf_len)
{
	return dln2_transfer(pdev, cmd, NULL, 0, ibuf, ibuf_len);
}

/**
 * dln2_transfer_tx - variant of @dln2_transfer() where RX buffer is not needed
 *
 * @pdev - the sub-device which is issuing this transfer
 * @cmd - the command to be sent to the device
 * @obuf - the buffer to be sent to the device; it can be NULL if the
 *	user doesn't need to transmit data with this command
 * @obuf_len - the size of the buffer to be sent to the device
 *
 * @return 0 for success, negative value for errors
 */
static inline int dln2_transfer_tx(struct platform_device *pdev, u16 cmd,
				   const void *obuf, unsigned obuf_len)
{
	return dln2_transfer(pdev, cmd, obuf, obuf_len, NULL, NULL);
}

#endif
