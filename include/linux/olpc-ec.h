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

/* SCI source values */
#define EC_SCI_SRC_EMPTY        0x00
#define EC_SCI_SRC_GAME         0x01
#define EC_SCI_SRC_BATTERY      0x02
#define EC_SCI_SRC_BATSOC       0x04
#define EC_SCI_SRC_BATERR       0x08
#define EC_SCI_SRC_EBOOK        0x10    /* XO-1 only */
#define EC_SCI_SRC_WLAN         0x20    /* XO-1 only */
#define EC_SCI_SRC_ACPWR        0x40
#define EC_SCI_SRC_BATCRIT      0x80
#define EC_SCI_SRC_GPWAKE       0x100   /* XO-1.5 only */
#define EC_SCI_SRC_ALL          0x1FF

struct platform_device;

struct olpc_ec_driver {
	int (*suspend)(struct platform_device *);
	int (*resume)(struct platform_device *);

	int (*ec_cmd)(u8, u8 *, size_t, u8 *, size_t, void *);

	bool wakeup_available;
};

#ifdef CONFIG_OLPC

extern void olpc_ec_driver_register(struct olpc_ec_driver *drv, void *arg);

extern int olpc_ec_cmd(u8 cmd, u8 *inbuf, size_t inlen, u8 *outbuf,
		size_t outlen);

extern void olpc_ec_wakeup_set(u16 value);
extern void olpc_ec_wakeup_clear(u16 value);

extern int olpc_ec_mask_write(u16 bits);
extern int olpc_ec_sci_query(u16 *sci_value);

extern bool olpc_ec_wakeup_available(void);

#else

static inline int olpc_ec_cmd(u8 cmd, u8 *inbuf, size_t inlen, u8 *outbuf,
		size_t outlen) { return -ENODEV; }

static inline void olpc_ec_wakeup_set(u16 value) { }
static inline void olpc_ec_wakeup_clear(u16 value) { }

static inline bool olpc_ec_wakeup_available(void)
{
	return false;
}

#endif /* CONFIG_OLPC */

#endif /* _LINUX_OLPC_EC_H */
