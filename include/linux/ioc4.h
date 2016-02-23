/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2005 Silicon Graphics, Inc.  All Rights Reserved.
 */

#ifndef _LINUX_IOC4_H
#define _LINUX_IOC4_H

#include <linux/interrupt.h>

/***************
 * Definitions *
 ***************/

/* Miscellaneous values inherent to hardware */

#define IOC4_EXTINT_COUNT_DIVISOR 520	/* PCI clocks per COUNT tick */

/***********************************
 * Structures needed by subdrivers *
 ***********************************/

/* This structure fully describes the IOC4 miscellaneous registers which
 * appear at bar[0]+0x00000 through bar[0]+0x0005c.  The corresponding
 * PCI resource is managed by the main IOC4 driver because it contains
 * registers of interest to many different IOC4 subdrivers.
 */
struct ioc4_misc_regs {
	/* Miscellaneous IOC4 registers */
	union ioc4_pci_err_addr_l {
		uint32_t raw;
		struct {
			uint32_t valid:1;	/* Address captured */
			uint32_t master_id:4;	/* Unit causing error
						 * 0/1: Serial port 0 TX/RX
						 * 2/3: Serial port 1 TX/RX
						 * 4/5: Serial port 2 TX/RX
						 * 6/7: Serial port 3 TX/RX
						 * 8: ATA/ATAPI
						 * 9-15: Undefined
						 */
			uint32_t mul_err:1;	/* Multiple errors occurred */
			uint32_t addr:26;	/* Bits 31-6 of error addr */
		} fields;
	} pci_err_addr_l;
	uint32_t pci_err_addr_h;	/* Bits 63-32 of error addr */
	union ioc4_sio_int {
		uint32_t raw;
		struct {
			uint8_t tx_mt:1;	/* TX ring buffer empty */
			uint8_t rx_full:1;	/* RX ring buffer full */
			uint8_t rx_high:1;	/* RX high-water exceeded */
			uint8_t rx_timer:1;	/* RX timer has triggered */
			uint8_t delta_dcd:1;	/* DELTA_DCD seen */
			uint8_t delta_cts:1;	/* DELTA_CTS seen */
			uint8_t intr_pass:1;	/* Interrupt pass-through */
			uint8_t tx_explicit:1;	/* TX, MCW, or delay complete */
		} fields[4];
	} sio_ir;		/* Serial interrupt state */
	union ioc4_other_int {
		uint32_t raw;
		struct {
			uint32_t ata_int:1;	/* ATA port passthru */
			uint32_t ata_memerr:1;	/* ATA halted by mem error */
			uint32_t memerr:4;	/* Serial halted by mem err */
			uint32_t kbd_int:1;	/* kbd/mouse intr asserted */
			uint32_t reserved:16;	/* zero */
			uint32_t rt_int:1;	/* INT_OUT section latch */
			uint32_t gen_int:8;	/* Intr. from generic pins */
		} fields;
	} other_ir;		/* Other interrupt state */
	union ioc4_sio_int sio_ies;	/* Serial interrupt enable set */
	union ioc4_other_int other_ies;	/* Other interrupt enable set */
	union ioc4_sio_int sio_iec;	/* Serial interrupt enable clear */
	union ioc4_other_int other_iec;	/* Other interrupt enable clear */
	union ioc4_sio_cr {
		uint32_t raw;
		struct {
			uint32_t cmd_pulse:4;	/* Bytebus strobe width */
			uint32_t arb_diag:3;	/* PCI bus requester */
			uint32_t sio_diag_idle:1;	/* Active ser req? */
			uint32_t ata_diag_idle:1;	/* Active ATA req? */
			uint32_t ata_diag_active:1;	/* ATA req is winner */
			uint32_t reserved:22;	/* zero */
		} fields;
	} sio_cr;
	uint32_t unused1;
	union ioc4_int_out {
		uint32_t raw;
		struct {
			uint32_t count:16;	/* Period control */
			uint32_t mode:3;	/* Output signal shape */
			uint32_t reserved:11;	/* zero */
			uint32_t diag:1;	/* Timebase control */
			uint32_t int_out:1;	/* Current value */
		} fields;
	} int_out;		/* External interrupt output control */
	uint32_t unused2;
	union ioc4_gpcr {
		uint32_t raw;
		struct {
			uint32_t dir:8;	/* Pin direction */
			uint32_t edge:8;	/* Edge/level mode */
			uint32_t reserved1:4;	/* zero */
			uint32_t int_out_en:1;	/* INT_OUT enable */
			uint32_t reserved2:11;	/* zero */
		} fields;
	} gpcr_s;		/* Generic PIO control set */
	union ioc4_gpcr gpcr_c;	/* Generic PIO control clear */
	union ioc4_gpdr {
		uint32_t raw;
		struct {
			uint32_t gen_pin:8;	/* State of pins */
			uint32_t reserved:24;
		} fields;
	} gpdr;			/* Generic PIO data */
	uint32_t unused3;
	union ioc4_gppr {
		uint32_t raw;
		struct {
			uint32_t gen_pin:1;	/* Single pin state */
			uint32_t reserved:31;
		} fields;
	} gppr[8];		/* Generic PIO pins */
};

/* Masks for GPCR DIR pins */
#define IOC4_GPCR_DIR_0 0x01	/* External interrupt output */
#define IOC4_GPCR_DIR_1 0x02	/* External interrupt input */
#define IOC4_GPCR_DIR_2 0x04
#define IOC4_GPCR_DIR_3 0x08	/* Keyboard/mouse presence */
#define IOC4_GPCR_DIR_4 0x10	/* Ser. port 0 xcvr select (0=232, 1=422) */
#define IOC4_GPCR_DIR_5 0x20	/* Ser. port 1 xcvr select (0=232, 1=422) */
#define IOC4_GPCR_DIR_6 0x40	/* Ser. port 2 xcvr select (0=232, 1=422) */
#define IOC4_GPCR_DIR_7 0x80	/* Ser. port 3 xcvr select (0=232, 1=422) */

/* Masks for GPCR EDGE pins */
#define IOC4_GPCR_EDGE_0 0x01
#define IOC4_GPCR_EDGE_1 0x02	/* External interrupt input */
#define IOC4_GPCR_EDGE_2 0x04
#define IOC4_GPCR_EDGE_3 0x08
#define IOC4_GPCR_EDGE_4 0x10
#define IOC4_GPCR_EDGE_5 0x20
#define IOC4_GPCR_EDGE_6 0x40
#define IOC4_GPCR_EDGE_7 0x80

#define IOC4_VARIANT_IO9	0x0900
#define IOC4_VARIANT_PCI_RT	0x0901
#define IOC4_VARIANT_IO10	0x1000

/* One of these per IOC4 */
struct ioc4_driver_data {
	struct list_head idd_list;
	unsigned long idd_bar0;
	struct pci_dev *idd_pdev;
	const struct pci_device_id *idd_pci_id;
	struct ioc4_misc_regs __iomem *idd_misc_regs;
	unsigned long count_period;
	void *idd_serial_data;
	unsigned int idd_variant;
};

/* One per submodule */
struct ioc4_submodule {
	struct list_head is_list;
	char *is_name;
	struct module *is_owner;
	int (*is_probe) (struct ioc4_driver_data *);
	int (*is_remove) (struct ioc4_driver_data *);
};

#define IOC4_NUM_CARDS		8	/* max cards per partition */

/**********************************
 * Functions needed by submodules *
 **********************************/

extern int ioc4_register_submodule(struct ioc4_submodule *);
extern void ioc4_unregister_submodule(struct ioc4_submodule *);

#endif				/* _LINUX_IOC4_H */
