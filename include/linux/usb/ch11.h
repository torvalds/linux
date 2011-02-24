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

/*
 * Hub request types
 */

#define USB_RT_HUB	(USB_TYPE_CLASS | USB_RECIP_DEVICE)
#define USB_RT_PORT	(USB_TYPE_CLASS | USB_RECIP_OTHER)

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
#define USB_PORT_FEAT_C_LINK_STATE		25
#define USB_PORT_FEAT_C_CONFIG_ERR		26
#define USB_PORT_FEAT_REMOTE_WAKE_MASK		27
#define USB_PORT_FEAT_BH_PORT_RESET		28
#define USB_PORT_FEAT_C_BH_PORT_RESET		29
#define USB_PORT_FEAT_FORCE_LINKPM_ACCEPT	30

/*
 * Hub Status and Hub Change results
 * See USB 2.0 spec Table 11-19 and Table 11-20
 */
struct usb_port_status {
	__le16 wPortStatus;
	__le16 wPortChange;
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
#define USB_PORT_STAT_SUPER_SPEED	0x8000	/* Linux-internal */

/*
 * Additions to wPortStatus bit field from USB 3.0
 * See USB 3.0 spec Table 10-10
 */
#define USB_PORT_STAT_LINK_STATE	0x01e0
#define USB_SS_PORT_STAT_POWER		0x0200
#define USB_PORT_STAT_SPEED_5GBPS	0x0000
/* Valid only if port is enabled */

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
 * See USB 2.0 spec Table 11-22
 * Bits 0 to 4 shown, bits 5 to 15 are reserved
 */
#define USB_PORT_STAT_C_CONNECTION	0x0001
#define USB_PORT_STAT_C_ENABLE		0x0002
#define USB_PORT_STAT_C_SUSPEND		0x0004
#define USB_PORT_STAT_C_OVERCURRENT	0x0008
#define USB_PORT_STAT_C_RESET		0x0010
#define USB_PORT_STAT_C_L1		0x0020

/*
 * wHubCharacteristics (masks)
 * See USB 2.0 spec Table 11-13, offset 3
 */
#define HUB_CHAR_LPSM		0x0003 /* D1 .. D0 */
#define HUB_CHAR_COMPOUND	0x0004 /* D2       */
#define HUB_CHAR_OCPM		0x0018 /* D4 .. D3 */
#define HUB_CHAR_TTTT           0x0060 /* D6 .. D5 */
#define HUB_CHAR_PORTIND        0x0080 /* D7       */

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
#define USB_DT_HUB_NONVAR_SIZE		7

struct usb_hub_descriptor {
	__u8  bDescLength;
	__u8  bDescriptorType;
	__u8  bNbrPorts;
	__le16 wHubCharacteristics;
	__u8  bPwrOn2PwrGood;
	__u8  bHubContrCurrent;
		/* add 1 bit for hub status change; round to bytes */
	__u8  DeviceRemovable[(USB_MAXCHILDREN + 1 + 7) / 8];
	__u8  PortPwrCtrlMask[(USB_MAXCHILDREN + 1 + 7) / 8];
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
