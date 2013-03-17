#ifndef __HDMI_CEC_DEV_H
#define __HDMI_CEC_DEV_H

#include <linux/ioctl.h>
#include <linux/hdmi-cec/hdmi-cec.h>

#define CEC_IOCTL_BASE	'C'

#define CEC_SET_LOGICAL_ADDRESS	_IOW(CEC_IOCTL_BASE, 0, int)
#define CEC_SEND_MESSAGE	_IOW(CEC_IOCTL_BASE, 1, int)
#define CEC_RECV_MESSAGE	_IOWR(CEC_IOCTL_BASE, 2, struct cec_msg)
#define CEC_RESET_DEVICE	_IOW(CEC_IOCTL_BASE, 3, int)
#define CEC_GET_COUNTERS	_IOR(CEC_IOCTL_BASE, 4, struct cec_counters)
#define CEC_SET_RX_MODE		_IOW(CEC_IOCTL_BASE, 5, enum cec_rx_mode)

#define CEC_MAX_DEVS	(10)

#ifdef __KERNEL__

struct cec_device;

int __init cec_dev_init(void);
void __exit cec_dev_exit(void);

int cec_create_dev_node(struct cec_device *dev);
void cec_remove_dev_node(struct cec_device *dev);

#endif /* __KERNEL__ */

#endif /* __HDMI_CEC_DEV_H */
