/*
 * Broadcom USB remote download definitions
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: usbrdl.h 275703 2011-08-04 20:20:27Z $
 */

#ifndef _USB_RDL_H
#define _USB_RDL_H

/* Control messages: bRequest values */
#define DL_GETSTATE		0	/* returns the rdl_state_t struct */
#define DL_CHECK_CRC		1	/* currently unused */
#define DL_GO			2	/* execute downloaded image */
#define DL_START		3	/* initialize dl state */
#define DL_REBOOT		4	/* reboot the device in 2 seconds */
#define DL_GETVER		5	/* returns the bootrom_id_t struct */
#define DL_GO_PROTECTED		6	/* execute the downloaded code and set reset event
					 * to occur in 2 seconds.  It is the responsibility
					 * of the downloaded code to clear this event
					 */
#define DL_EXEC			7	/* jump to a supplied address */
#define DL_RESETCFG		8	/* To support single enum on dongle
					 * - Not used by bootloader
					 */
#define DL_DEFER_RESP_OK	9	/* Potentially defer the response to setup
					 * if resp unavailable
					 */

#define	DL_HWCMD_MASK		0xfc	/* Mask for hardware read commands: */
#define	DL_RDHW			0x10	/* Read a hardware address (Ctl-in) */
#define	DL_RDHW32		0x10	/* Read a 32 bit word */
#define	DL_RDHW16		0x11	/* Read 16 bits */
#define	DL_RDHW8		0x12	/* Read an 8 bit byte */
#define	DL_WRHW			0x14	/* Write a hardware address (Ctl-out) */
#define DL_WRHW_BLK     0x13    /* Block write to hardware access */

#define DL_CMD_RDHW		1	/* read data from a backplane address */
#define DL_CMD_WRHW		2	/* write data to a backplane address */

#define	DL_JTCONF		0x15	/* Get JTAG configuration (Ctl_in)
					 *  Set JTAG configuration (Ctl-out)
					 */
#define	DL_JTON			0x16	/* Turn on jtag master (Ctl-in) */
#define	DL_JTOFF		0x17	/* Turn on jtag master (Ctl-in) */
#define	DL_RDRJT		0x18	/* Read a JTAG register (Ctl-in) */
#define	DL_WRJT			0x19	/* Write a hardware address over JTAG (Ctl/Bulk-out) */
#define	DL_WRRJT		0x1a	/* Write a JTAG register (Ctl/Bulk-out) */
#define	DL_JTRST		0x1b	/* Reset jtag fsm on jtag DUT (Ctl-in) */

#define	DL_RDJT			0x1c	/* Read a hardware address over JTAG (Ctl-in) */
#define	DL_RDJT32		0x1c	/* Read 32 bits */
#define	DL_RDJT16		0x1e	/* Read 16 bits (sz = 4 - low bits) */
#define	DL_RDJT8		0x1f	/* Read 8 bits */

#define	DL_DBGTRIG		0xFF	/* Trigger bRequest type to aid debug */

#define	DL_JTERROR		0x80000000


/* states */
#define DL_WAITING	0	/* waiting to rx first pkt that includes the hdr info */
#define DL_READY	1	/* hdr was good, waiting for more of the compressed image */
#define DL_BAD_HDR	2	/* hdr was corrupted */
#define DL_BAD_CRC	3	/* compressed image was corrupted */
#define DL_RUNNABLE	4	/* download was successful, waiting for go cmd */
#define DL_START_FAIL	5	/* failed to initialize correctly */
#define DL_NVRAM_TOOBIG	6	/* host specified nvram data exceeds DL_NVRAM value */
#define DL_IMAGE_TOOBIG	7	/* download image too big (exceeds DATA_START for rdl) */

#define TIMEOUT		5000	/* Timeout for usb commands */

struct bcm_device_id {
	char	*name;
	uint32	vend;
	uint32	prod;
};

typedef struct {
	uint32	state;
	uint32	bytes;
} rdl_state_t;

typedef struct {
	uint32	chip;		/* Chip id */
	uint32	chiprev;	/* Chip rev */
	uint32  ramsize;    /* Size of  RAM */
	uint32  remapbase;   /* Current remap base address */
	uint32  boardtype;   /* Type of board */
	uint32  boardrev;    /* Board revision */
} bootrom_id_t;

typedef struct {
	uint32	chip;		/* Chip id */
	uint32	chiprev;	/* Chip rev */
	uint32	ccrev;		/* Chipcommon core rev */
	uint32	siclock;	/* Backplane clock */
} jtagd_id_t;

/* struct for backplane & jtag accesses */
typedef struct {
	uint32	cmd;		/* tag to identify the cmd */
	uint32	addr;		/* backplane address for write */
	uint32	len;		/* length of data: 1, 2, 4 bytes */
	uint32	data;		/* data to write */
} hwacc_t;

/* struct for backplane & jtag accesses */
typedef struct {
	uint32	cmd;		/* tag to identify the cmd */
	uint32	addr;		/* backplane address for write */
	uint32	len;		/* length of data: 1, 2, 4 bytes */
	uint8	data[1];		/* data to write */
} hwacc_blk_t;


/* Jtag configuration structure */
typedef struct {
	uint32	cmd;		/* tag to identify the cmd */
	uint8	clkd;		/* Jtag clock divisor */
	uint8	disgpio;	/* Gpio to disable external driver */
	uint8	irsz;		/* IR size for readreg/writereg */
	uint8	drsz;		/* DR size for readreg/writereg */

	uint8	bigend;		/* Big endian */
	uint8	mode;		/* Current mode */
	uint16	delay;		/* Delay between jtagm "simple commands" */

	uint32	retries;	/* Number of retries for jtagm operations */
	uint32	ctrl;		/* Jtag control reg copy */
} jtagconf_t;

typedef void (*exec_fn_t)(void *sih);

#define USB_CTRL_IN (USB_TYPE_VENDOR | 0x80 | USB_RECIP_INTERFACE)
#define USB_CTRL_OUT (USB_TYPE_VENDOR | 0 | USB_RECIP_INTERFACE)

#define USB_CTRL_EP_TIMEOUT 500 /* Timeout used in USB control_msg transactions. */

#define RDL_CHUNK	1500  /* size of each dl transfer */

/* bootloader makes special use of trx header "offsets" array */
#define TRX_OFFSETS_DLFWLEN_IDX	0	/* Size of the fw; used in uncompressed case */
#define TRX_OFFSETS_JUMPTO_IDX	1	/* RAM address for jumpto after download */
#define TRX_OFFSETS_NVM_LEN_IDX	2	/* Length of appended NVRAM data */

#define TRX_OFFSETS_DLBASE_IDX  0       /* RAM start address for download */

#endif  /* _USB_RDL_H */
