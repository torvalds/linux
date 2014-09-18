/*
 * Any part of this program may be used in documents licensed under
 * the GNU Free Documentation License, Version 1.1 or any later version
 * published by the Free Software Foundation.
 */

#ifndef _UAPI_PARPORT_H_
#define _UAPI_PARPORT_H_

/* Start off with user-visible constants */

/* Maximum of 16 ports per machine */
#define PARPORT_MAX  16

/* Magic numbers */
#define PARPORT_IRQ_NONE  -1
#define PARPORT_DMA_NONE  -1
#define PARPORT_IRQ_AUTO  -2
#define PARPORT_DMA_AUTO  -2
#define PARPORT_DMA_NOFIFO -3
#define PARPORT_DISABLE   -2
#define PARPORT_IRQ_PROBEONLY -3
#define PARPORT_IOHI_AUTO -1

#define PARPORT_CONTROL_STROBE    0x1
#define PARPORT_CONTROL_AUTOFD    0x2
#define PARPORT_CONTROL_INIT      0x4
#define PARPORT_CONTROL_SELECT    0x8

#define PARPORT_STATUS_ERROR      0x8
#define PARPORT_STATUS_SELECT     0x10
#define PARPORT_STATUS_PAPEROUT   0x20
#define PARPORT_STATUS_ACK        0x40
#define PARPORT_STATUS_BUSY       0x80

/* Type classes for Plug-and-Play probe.  */
typedef enum {
	PARPORT_CLASS_LEGACY = 0,       /* Non-IEEE1284 device */
	PARPORT_CLASS_PRINTER,
	PARPORT_CLASS_MODEM,
	PARPORT_CLASS_NET,
	PARPORT_CLASS_HDC,              /* Hard disk controller */
	PARPORT_CLASS_PCMCIA,
	PARPORT_CLASS_MEDIA,            /* Multimedia device */
	PARPORT_CLASS_FDC,              /* Floppy disk controller */
	PARPORT_CLASS_PORTS,
	PARPORT_CLASS_SCANNER,
	PARPORT_CLASS_DIGCAM,
	PARPORT_CLASS_OTHER,            /* Anything else */
	PARPORT_CLASS_UNSPEC,           /* No CLS field in ID */
	PARPORT_CLASS_SCSIADAPTER
} parport_device_class;

/* The "modes" entry in parport is a bit field representing the
   capabilities of the hardware. */
#define PARPORT_MODE_PCSPP	(1<<0) /* IBM PC registers available. */
#define PARPORT_MODE_TRISTATE	(1<<1) /* Can tristate. */
#define PARPORT_MODE_EPP	(1<<2) /* Hardware EPP. */
#define PARPORT_MODE_ECP	(1<<3) /* Hardware ECP. */
#define PARPORT_MODE_COMPAT	(1<<4) /* Hardware 'printer protocol'. */
#define PARPORT_MODE_DMA	(1<<5) /* Hardware can DMA. */
#define PARPORT_MODE_SAFEININT	(1<<6) /* SPP registers accessible in IRQ. */

/* IEEE1284 modes: 
   Nibble mode, byte mode, ECP, ECPRLE and EPP are their own
   'extensibility request' values.  Others are special.
   'Real' ECP modes must have the IEEE1284_MODE_ECP bit set.  */
#define IEEE1284_MODE_NIBBLE             0
#define IEEE1284_MODE_BYTE              (1<<0)
#define IEEE1284_MODE_COMPAT            (1<<8)
#define IEEE1284_MODE_BECP              (1<<9) /* Bounded ECP mode */
#define IEEE1284_MODE_ECP               (1<<4)
#define IEEE1284_MODE_ECPRLE            (IEEE1284_MODE_ECP | (1<<5))
#define IEEE1284_MODE_ECPSWE            (1<<10) /* Software-emulated */
#define IEEE1284_MODE_EPP               (1<<6)
#define IEEE1284_MODE_EPPSL             (1<<11) /* EPP 1.7 */
#define IEEE1284_MODE_EPPSWE            (1<<12) /* Software-emulated */
#define IEEE1284_DEVICEID               (1<<2)  /* This is a flag */
#define IEEE1284_EXT_LINK               (1<<14) /* This flag causes the
						 * extensibility link to
						 * be requested, using
						 * bits 0-6. */

/* For the benefit of parport_read/write, you can use these with
 * parport_negotiate to use address operations.  They have no effect
 * other than to make parport_read/write use address transfers. */
#define IEEE1284_ADDR			(1<<13)	/* This is a flag */
#define IEEE1284_DATA			 0	/* So is this */

/* Flags for block transfer operations. */
#define PARPORT_EPP_FAST		(1<<0) /* Unreliable counts. */
#define PARPORT_W91284PIC		(1<<1) /* have a Warp9 w91284pic in the device */

/* The rest is for the kernel only */
#endif /* _UAPI_PARPORT_H_ */
