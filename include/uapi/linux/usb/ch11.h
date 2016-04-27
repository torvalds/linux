/*
 * This file holds Hub protocol constants and data structures that are
 * defined in chapter 11 (Hub Specification) of the USB 2.0 specification.
 *
 * It is used/shared between the USB core, the HCDs and couple of other USB
 * drivers.
 */

#ifndef __LINUX_CH11_H
#define __LINUX_CH11_H

#include <linux/types.h>	/* __u8 etc */

/* This is arbitrary.
 * From USB 2.0 spec Table 11-13, offset 7, a hub can
 * have up to 255 ports. The most yet reported is 10.
 *
 * Current Wireless USB host hardware (Intel i1480 for example) allows
 * up to 22 devices to connect. Upcoming hardware might raise that
 * limit. Because the arrays need to add a bit for hub status data, we
 * use 31, so plus one evens out to four bytes.
 */
#define USB_MAXCHILDREN		31

/*
 * Hub request types
 */

#define USB_RT_HUB	(USB_TYPE_CLASS | USB_RECIP_DEVICE)
#define USB_RT_PORT	(USB_TYPE_CLASS | USB_RECIP_OTHER)

/*
 * Port status type for GetPortStatus requests added in USB 3.1
 * See USB 3.1 spec Table 10-12
 */
#define HUB_PORT_STATUS		0
#define HUB_PORT_PD_STATUS	1
#define HUB_EXT_PORT_STATUS	2

/*
 * Hub class requests
 * See USB 2.0 spec Table 11-16
 */
#define HUB_CLEAR_TT_BUFFER	8
#define HUB_RESET_TT		9
#define HUB_GET_TT_STATE	10
#define HUB_STOP_TT		11

/*
 * Hub class additional requests defined by USB 3.0 spec
 * See USB 3.0 spec Table 10-6
 */
#define HUB_SET_DEPTH		12
#define HUB_GET_PORT_ERR_COUNT	13

/*
 * Hub Class feature numbers
 * See USB 2.0 spec Table 11-17
 */
#define C_HUB_LOCAL_POWER	0
#define C_HUB_OVER_CURRENT	1

/*
 * Port feature numbers
 * See USB 2.0 spec Table 11-17
 */
#define USB_PORT_FEAT_CONNECTION	0
#define USB_PORT_FEAT_ENABLE		1
#define USB_PORT_FEAT_SUSPEND		2	/* L2 suspend */
#define USB_PORT_FEAT_OVER_CURRENT	3
#define USB_PORT_FEAT_RESET		4
#define USB_PORT_FEAT_L1		5	/* L1 suspend */
#define USB_PORT_FEAT_POWER		8
#define USB_PORT_FEAT_LOWSPEED		9	/* Should never be used */
#define USB_PORT_FEAT_C_CONNECTION	16
#define USB_PORT_FEAT_C_ENABLE		17
#define USB_PORT_FEAT_C_SUSPEND		18
#define USB_PORT_FEAT_C_OVER_CURRENT	19
#define USB_PORT_FEAT_C_RESET		20
#define USB_PORT_FEAT_TEST              21
#define USB_PORT_FEAT_INDICATOR         22
#define USB_PORT_FEAT_C_PORT_L1         23

/*
 * Port feature selectors added by USB 3.0 spec.
 * See USB 3.0 spec Table 10-7
 */
#define USB_PORT_FEAT_LINK_STATE		5
#define USB_PORT_FEAT_U1_TIMEOUT		23
#define USB_PORT_FEAT_U2_TIMEOUT		24
#define USB_PORT_FEAT_C_PORT_LINK_STATE		25
#define USB_PORT_FEAT_C_PORT_CONFIG_ERROR	26
#define USB_PORT_FEAT_REMOTE_WAKE_MASK		27
#define USB_PORT_FEAT_BH_PORT_RESET		28
#define USB_PORT_FEAT_C_BH_PORT_RESET		29
#define USB_PORT_FEAT_FORCE_LINKPM_ACCEPT	30

#define USB_PORT_LPM_TIMEOUT(p)			(((p) & 0xff) << 8)

/* USB 3.0 hub remote wake mask bits, see table 10-14 */
#define USB_PORT_FEAT_REMOTE_WAKE_CONNECT	(1 << 8)
#define USB_PORT_FEAT_REMOTE_WAKE_DISCONNECT	(1 << 9)
#define USB_PORT_FEAT_REMOTE_WAKE_OVER_CURRENT	(1 << 10)

/*
 * Hub Status and Hub Change results
 * See USB 2.0 spec Table 11-19 and Table 11-20
 * USB 3.1 extends the port status request and may return 4 additional bytes.
 * See USB 3.1 spec section 10.16.2.6 Table 10-12 and 10-15
 */
struct usb_port_status {
	__le16 wPortStatus;
	__le16 wPortChange;
	__le32 dwExtPortStatus;
} __attribute__ ((packed));

/*
 * wPortStatus bit field
 * See USB 2.0 spec Table 11-21
 */
#define USB_PORT_STAT_CONNECTION	0x0001
#define USB_PORT_STAT_ENABLE		0x0002
#define USB_PORT_STAT_SUSPEND		0x0004
#define USB_PORT_STAT_OVERCURRENT	0x0008
#define USB_PORT_STAT_RESET		0x0010
#define USB_PORT_STAT_L1		0x0020
/* bits 6 to 7 are reserved */
#define USB_PORT_STAT_POWER		0x0100
#define USB_PORT_STAT_LOW_SPEED		0x0200
#define USB_PORT_STAT_HIGH_SPEED        0x0400
#define USB_PORT_STAT_TEST              0x0800
#define USB_PORT_STAT_INDICATOR         0x1000
/* bits 13 to 15 are reserved */

/*
 * Additions to wPortStatus bit field from USB 3.0
 * See USB 3.0 spec Table 10-10
 */
#define USB_PORT_STAT_LINK_STATE	0x01e0
#define USB_SS_PORT_STAT_POWER		0x0200
#define USB_SS_PORT_STAT_SPEED		0x1c00
#define USB_PORT_STAT_SPEED_5GBPS	0x0000
/* Valid only if port is enabled */
/* Bits that are the same from USB 2.0 */
#define USB_SS_PORT_STAT_MASK (USB_PORT_STAT_CONNECTION |	    \
				USB_PORT_STAT_ENABLE |	    \
				USB_PORT_STAT_OVERCURRENT | \
				USB_PORT_STAT_RESET)

/*
 * Definitions for PORT_LINK_STATE values
 * (bits 5-8) in wPortStatus
 */
#define USB_SS_PORT_LS_U0		0x0000
#define USB_SS_PORT_LS_U1		0x0020
#define USB_SS_PORT_LS_U2		0x0040
#define USB_SS_PORT_LS_U3		0x0060
#define USB_SS_PORT_LS_SS_DISABLED	0x0080
#define USB_SS_PORT_LS_RX_DETECT	0x00a0
#define USB_SS_PORT_LS_SS_INACTIVE	0x00c0
#define USB_SS_PORT_LS_POLLING		0x00e0
#define USB_SS_PORT_LS_RECOVERY		0x0100
#define USB_SS_PORT_LS_HOT_RESET	0x0120
#define USB_SS_PORT_LS_COMP_MOD		0x0140
#define USB_SS_PORT_LS_LOOPBACK		0x0160

/*
 * wPortChange bit field
 * See USB 2.0 spec Table 11-22 and USB 2.0 LPM ECN Table-4.10
 * Bits 0 to 5 shown, bits 6 to 15 are reserved
 */
#define USB_PORT_STAT_C_CONNECTION	0x0001
#define USB_PORT_STAT_C_ENABLE		0x0002
#define USB_PORT_STAT_C_SUSPEND		0x0004
#define USB_PORT_STAT_C_OVERCURRENT	0x0008
#define USB_PORT_STAT_C_RESET		0x0010
#define USB_PORT_STAT_C_L1		0x0020
/*
 * USB 3.0 wPortChange bit fields
 * See USB 3.0 spec Table 10-11
 */
#define USB_PORT_STAT_C_BH_RESET	0x0020
#define USB_PORT_STAT_C_LINK_STATE	0x0040
#define USB_PORT_STAT_C_CONFIG_ERROR	0x0080

/*
 * USB 3.1 dwExtPortStatus field masks
 * See USB 3.1 spec 10.16.2.6.3 Table 10-15
 */

#define USB_EXT_PORT_STAT_RX_SPEED_ID	0x0000000f
#define USB_EXT_PORT_STAT_TX_SPEED_ID	0x000000f0
#define USB_EXT_PORT_STAT_RX_LANES	0x00000f00
#define USB_EXT_PORT_STAT_TX_LANES	0x0000f000

/*
 * wHubCharacteristics (masks)
 * See USB 2.0 spec Table 11-13, offset 3
 */
#define HUB_CHAR_LPSM		0x0003 /* Logical Power Switching Mode mask */
#define HUB_CHAR_COMMON_LPSM	0x0000 /* All ports power control at once */
#define HUB_CHAR_INDV_PORT_LPSM	0x0001 /* per-port power control */
#define HUB_CHAR_NO_LPSM	0x0002 /* no power switching */

#define HUB_CHAR_COMPOUND	0x0004 /* hub is part of a compound device */

#define HUB_CHAR_OCPM		0x0018 /* Over-Current Protection Mode mask */
#define HUB_CHAR_COMMON_OCPM	0x0000 /* All ports Over-Current reporting */
#define HUB_CHAR_INDV_PORT_OCPM	0x0008 /* per-port Over-current reporting */
#define HUB_CHAR_NO_OCPM	0x0010 /* No Over-current Protection support */

#define HUB_CHAR_TTTT		0x0060 /* TT Think Time mask */
#define HUB_CHAR_PORTIND	0x0080 /* per-port indicators (LEDs) */

struct usb_hub_status {
	__le16 wHubStatus;
	__le16 wHubChange;
} __attribute__ ((packed));

/*
 * Hub Status & Hub Change bit masks
 * See USB 2.0 spec Table 11-19 and Table 11-20
 * Bits 0 and 1 for wHubStatus and wHubChange
 * Bits 2 to 15 are reserved for both
 */
#define HUB_STATUS_LOCAL_POWER	0x0001
#define HUB_STATUS_OVERCURRENT	0x0002
#define HUB_CHANGE_LOCAL_POWER	0x0001
#define HUB_CHANGE_OVERCURRENT	0x0002


/*
 * Hub descriptor
 * See USB 2.0 spec Table 11-13
 */

#define USB_DT_HUB			(USB_TYPE_CLASS | 0x09)
#define USB_DT_SS_HUB			(USB_TYPE_CLASS | 0x0a)
#define USB_DT_HUB_NONVAR_SIZE		7
#define USB_DT_SS_HUB_SIZE              12

/*
 * Hub Device descriptor
 * USB Hub class device protocols
 */

#define USB_HUB_PR_FS		0 /* Full speed hub */
#define USB_HUB_PR_HS_NO_TT	0 /* Hi-speed hub without TT */
#define USB_HUB_PR_HS_SINGLE_TT	1 /* Hi-speed hub with single TT */
#define USB_HUB_PR_HS_MULTI_TT	2 /* Hi-speed hub with multiple TT */
#define USB_HUB_PR_SS		3 /* Super speed hub */

struct usb_hub_descriptor {
	__u8  bDescLength;
	__u8  bDescriptorType;
	__u8  bNbrPorts;
	__le16 wHubCharacteristics;
	__u8  bPwrOn2PwrGood;
	__u8  bHubContrCurrent;

	/* 2.0 and 3.0 hubs differ here */
	union {
		struct {
			/* add 1 bit for hub status change; round to bytes */
			__u8  DeviceRemovable[(USB_MAXCHILDREN + 1 + 7) / 8];
			__u8  PortPwrCtrlMask[(USB_MAXCHILDREN + 1 + 7) / 8];
		}  __attribute__ ((packed)) hs;

		struct {
			__u8 bHubHdrDecLat;
			__le16 wHubDelay;
			__le16 DeviceRemovable;
		}  __attribute__ ((packed)) ss;
	} u;
} __attribute__ ((packed));

/* port indicator status selectors, tables 11-7 and 11-25 */
#define HUB_LED_AUTO	0
#define HUB_LED_AMBER	1
#define HUB_LED_GREEN	2
#define HUB_LED_OFF	3

enum hub_led_mode {
	INDICATOR_AUTO = 0,
	INDICATOR_CYCLE,
	/* software blinks for attention:  software, hardware, reserved */
	INDICATOR_GREEN_BLINK, INDICATOR_GREEN_BLINK_OFF,
	INDICATOR_AMBER_BLINK, INDICATOR_AMBER_BLINK_OFF,
	INDICATOR_ALT_BLINK, INDICATOR_ALT_BLINK_OFF
} __attribute__ ((packed));

/* Transaction Translator Think Times, in bits */
#define HUB_TTTT_8_BITS		0x00
#define HUB_TTTT_16_BITS	0x20
#define HUB_TTTT_24_BITS	0x40
#define HUB_TTTT_32_BITS	0x60

#endif /* __LINUX_CH11_H */
