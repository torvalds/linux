/*
 * USB IrDA Bridge Device Definition
 */

#ifndef __LINUX_USB_IRDA_H
#define __LINUX_USB_IRDA_H

/* This device should use Application-specific class */

#define USB_SUBCLASS_IRDA			0x02

/*-------------------------------------------------------------------------*/

/* Class-Specific requests (bRequest field) */

#define USB_REQ_CS_IRDA_RECEIVING		1
#define USB_REQ_CS_IRDA_CHECK_MEDIA_BUSY	3
#define USB_REQ_CS_IRDA_RATE_SNIFF		4
#define USB_REQ_CS_IRDA_UNICAST_LIST		5
#define USB_REQ_CS_IRDA_GET_CLASS_DESC		6

/*-------------------------------------------------------------------------*/

/* Class-Specific descriptor */

#define USB_DT_CS_IRDA				0x21

/*-------------------------------------------------------------------------*/

/* Data sizes */

#define USB_IRDA_DS_2048			(1 << 5)
#define USB_IRDA_DS_1024			(1 << 4)
#define USB_IRDA_DS_512				(1 << 3)
#define USB_IRDA_DS_256				(1 << 2)
#define USB_IRDA_DS_128				(1 << 1)
#define USB_IRDA_DS_64				(1 << 0)

/* Window sizes */

#define USB_IRDA_WS_7				(1 << 6)
#define USB_IRDA_WS_6				(1 << 5)
#define USB_IRDA_WS_5				(1 << 4)
#define USB_IRDA_WS_4				(1 << 3)
#define USB_IRDA_WS_3				(1 << 2)
#define USB_IRDA_WS_2				(1 << 1)
#define USB_IRDA_WS_1				(1 << 0)

/* Min turnaround times in usecs */

#define USB_IRDA_MTT_0				(1 << 7)
#define USB_IRDA_MTT_10				(1 << 6)
#define USB_IRDA_MTT_50				(1 << 5)
#define USB_IRDA_MTT_100			(1 << 4)
#define USB_IRDA_MTT_500			(1 << 3)
#define USB_IRDA_MTT_1000			(1 << 2)
#define USB_IRDA_MTT_5000			(1 << 1)
#define USB_IRDA_MTT_10000			(1 << 0)

/* Baud rates */

#define USB_IRDA_BR_4000000			(1 << 8)
#define USB_IRDA_BR_1152000			(1 << 7)
#define USB_IRDA_BR_576000			(1 << 6)
#define USB_IRDA_BR_115200			(1 << 5)
#define USB_IRDA_BR_57600			(1 << 4)
#define USB_IRDA_BR_38400			(1 << 3)
#define USB_IRDA_BR_19200			(1 << 2)
#define USB_IRDA_BR_9600			(1 << 1)
#define USB_IRDA_BR_2400			(1 << 0)

/* Additional BOFs */

#define USB_IRDA_AB_0				(1 << 7)
#define USB_IRDA_AB_1				(1 << 6)
#define USB_IRDA_AB_2				(1 << 5)
#define USB_IRDA_AB_3				(1 << 4)
#define USB_IRDA_AB_6				(1 << 3)
#define USB_IRDA_AB_12				(1 << 2)
#define USB_IRDA_AB_24				(1 << 1)
#define USB_IRDA_AB_48				(1 << 0)

/* IRDA Rate Sniff */

#define USB_IRDA_RATE_SNIFF			1

/*-------------------------------------------------------------------------*/

struct usb_irda_cs_descriptor {
	__u8	bLength;
	__u8	bDescriptorType;

	__le16	bcdSpecRevision;
	__u8	bmDataSize;
	__u8	bmWindowSize;
	__u8	bmMinTurnaroundTime;
	__le16	wBaudRate;
	__u8	bmAdditionalBOFs;
	__u8	bIrdaRateSniff;
	__u8	bMaxUnicastList;
} __attribute__ ((packed));

/*-------------------------------------------------------------------------*/

/* Data Format */

#define USB_IRDA_STATUS_MEDIA_BUSY	(1 << 7)

/* The following is a 4-bit value used for both
 * inbound and outbound headers:
 *
 * 0 - speed ignored
 * 1 - 2400 bps
 * 2 - 9600 bps
 * 3 - 19200 bps
 * 4 - 38400 bps
 * 5 - 57600 bps
 * 6 - 115200 bps
 * 7 - 576000 bps
 * 8 - 1.152 Mbps
 * 9 - 5 mbps
 * 10..15 - Reserved
 */
#define USB_IRDA_STATUS_LINK_SPEED	0x0f

/* The following is a 4-bit value used only for
 * outbound header:
 *
 * 0 - No change (BOF ignored)
 * 1 - 48 BOFs
 * 2 - 24 BOFs
 * 3 - 12 BOFs
 * 4 - 6 BOFs
 * 5 - 3 BOFs
 * 6 - 2 BOFs
 * 7 - 1 BOFs
 * 8 - 0 BOFs
 * 9..15 - Reserved
 */
#define USB_IRDA_EXTRA_BOFS		0xf0

struct usb_irda_inbound_header {
	__u8		bmStatus;
};

struct usb_irda_outbound_header {
	__u8		bmChange;
};

#endif /* __LINUX_USB_IRDA_H */

