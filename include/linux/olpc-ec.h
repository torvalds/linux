/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_OLPC_EC_H
#define _LINUX_OLPC_EC_H

/* XO-1 EC commands */
#define EC_FIRMWARE_REV			0x08
#define EC_WRITE_SCI_MASK		0x1b
#define EC_WAKE_UP_WLAN			0x24
#define EC_WLAN_LEAVE_RESET		0x25
#define EC_DCON_POWER_MODE		0x26
#define EC_READ_EB_MODE			0x2a
#define EC_SET_SCI_INHIBIT		0x32
#define EC_SET_SCI_INHIBIT_RELEASE	0x34
#define EC_WLAN_ENTER_RESET		0x35
#define EC_WRITE_EXT_SCI_MASK		0x38
#define EC_SCI_QUERY			0x84
#define EC_EXT_SCI_QUERY		0x85

struct platform_device;

struct olpc_ec_driver {
	int (*probe)(struct platform_device *);
	int (*suspend)(struct platform_device *);
	int (*resume)(struct platform_device *);

	int (*ec_cmd)(u8, u8 *, size_t, u8 *, size_t, void *);
};

#ifdef CONFIG_OLPC

extern void olpc_ec_driver_register(struct olpc_ec_driver *drv, void *arg);

extern int olpc_ec_cmd(u8 cmd, u8 *inbuf, size_t inlen, u8 *outbuf,
		size_t outlen);

#else

static inline int olpc_ec_cmd(u8 cmd, u8 *inbuf, size_t inlen, u8 *outbuf,
		size_t outlen) { return -ENODEV; }

#endif /* CONFIG_OLPC */

#endif /* _LINUX_OLPC_EC_H */
