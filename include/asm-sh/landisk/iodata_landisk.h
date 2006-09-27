#ifndef __ASM_SH_IODATA_LANDISK_H
#define __ASM_SH_IODATA_LANDISK_H

/*
 * linux/include/asm-sh/landisk/iodata_landisk.h
 *
 * Copyright (C) 2000  Atom Create Engineering Co., Ltd.
 *
 * IO-DATA LANDISK support
 */

/* Box specific addresses.  */

#define PA_USB		0xa4000000	/* USB Controller M66590 */

#define PA_ATARST	0xb0000000	/* ATA/FATA Access Control Register */
#define PA_LED		0xb0000001	/* LED Control Register */
#define PA_STATUS	0xb0000002	/* Switch Status Register */
#define PA_SHUTDOWN	0xb0000003	/* Shutdown Control Register */
#define PA_PCIPME	0xb0000004	/* PCI PME Status Register */
#define PA_IMASK	0xb0000005	/* Interrupt Mask Register */
/* 2003.10.31 I-O DATA NSD NWG	add.	for shutdown port clear */
#define PA_PWRINT_CLR	0xb0000006	/* Shutdown Interrupt clear Register */

#define PA_LCD_CLRDSP	0x00		/* LCD Clear Display Offset */
#define PA_LCD_RTNHOME	0x00		/* LCD Return Home Offset */
#define PA_LCD_ENTMODE	0x00		/* LCD Entry Mode Offset */
#define PA_LCD_DSPCTL	0x00		/* LCD Display ON/OFF Control Offset */
#define PA_LCD_FUNC	0x00		/* LCD Function Set Offset */
#define PA_LCD_CGRAM	0x00		/* LCD Set CGRAM Address Offset */
#define PA_LCD_DDRAM	0x00		/* LCD Set DDRAM Address Offset */
#define PA_LCD_RDFLAG	0x01		/* LCD Read Busy Flag Offset */
#define PA_LCD_WTDATA	0x02		/* LCD Write Datat to RAM Offset */
#define PA_LCD_RDDATA	0x03		/* LCD Read Data from RAM Offset */
#define PA_PIDE_OFFSET	0x40		/* CF IDE Offset */
#define PA_SIDE_OFFSET	0x40		/* HDD IDE Offset */

#define IRQ_PCIINTA	5		/* PCI INTA IRQ */
#define IRQ_PCIINTB	6		/* PCI INTB IRQ */
#define IRQ_PCIINDC	7		/* PCI INTC IRQ */
#define IRQ_PCIINTD	8		/* PCI INTD IRQ */
#define IRQ_ATA		9		/* ATA IRQ */
#define IRQ_FATA	10		/* FATA IRQ */
#define IRQ_POWER	11		/* Power Switch IRQ */
#define IRQ_BUTTON	12		/* USL-5P Button IRQ */
#define IRQ_FAULT	13		/* USL-5P Fault  IRQ */

#define SHUTDOWN_BTN_MAJOR	99	/* Shutdown button device major no. */

#define SHUTDOWN_LOOP_CNT	5	/* Shutdown button Detection loop */
#define SHUTDOWN_DELAY		200	/* Shutdown button delay value(ms) */


/* added by kogiidena */
/*
 *  landisk_ledparam
 *
 * led  ------10 -6543210 -6543210 -6543210
 *     |000000..|0.......|0.......|U.......|
 *     |  HARD  |fastblik| blink  |   on   |
 *
 *   led0: power       U:update flag
 *   led1: error
 *   led2: usb1
 *   led3: usb2
 *   led4: usb3
 *   led5: usb4
 *   led6: usb5
 *
 */
extern int landisk_ledparam;    /* from setup.c */
extern int landisk_buzzerparam; /* from setup.c */
extern int landisk_arch;        /* from setup.c */

#define __IO_PREFIX landisk
#include <asm/io_generic.h>

#endif  /* __ASM_SH_IODATA_LANDISK_H */

