/* $Id: iflash.h,v 1.2 2000/11/13 18:01:54 dwmw2 Exp $ */

#ifndef __MTD_IFLASH_H__
#define __MTD_IFLASH_H__

/* Extended CIS registers for Series 2 and 2+ cards */
/* The registers are all offsets from 0x4000 */
#define CISREG_CSR		0x0100
#define CISREG_WP		0x0104
#define CISREG_RDYBSY		0x0140

/* Extended CIS registers for Series 2 cards */
#define CISREG_SLEEP		0x0118
#define CISREG_RDY_MASK		0x0120
#define CISREG_RDY_STATUS	0x0130

/* Extended CIS registers for Series 2+ cards */
#define CISREG_VCR		0x010c

/* Card Status Register */
#define CSR_SRESET		0x20	/* Soft reset */
#define CSR_CMWP		0x10	/* Common memory write protect */
#define CSR_PWRDOWN		0x08	/* Power down status */
#define CSR_CISWP		0x04	/* Common memory CIS WP */
#define CSR_WP			0x02	/* Mechanical write protect */
#define CSR_READY		0x01	/* Ready/busy status */

/* Write Protection Register */
#define WP_BLKEN		0x04	/* Enable block locking */
#define WP_CMWP			0x02	/* Common memory write protect */
#define WP_CISWP		0x01	/* Common memory CIS WP */

/* Voltage Control Register */
#define VCR_VCC_LEVEL		0x80	/* 0 = 5V, 1 = 3.3V */
#define VCR_VPP_VALID		0x02	/* Vpp Valid */
#define VCR_VPP_GEN		0x01	/* Integrated Vpp generator */

/* Ready/Busy Mode Register */
#define RDYBSY_RACK		0x02	/* Ready acknowledge */
#define RDYBSY_MODE		0x01	/* 1 = high performance */

#define LOW(x) ((x) & 0xff)

/* 28F008SA-Compatible Command Set */
#define IF_READ_ARRAY		0xffff
#define IF_INTEL_ID		0x9090
#define IF_READ_CSR		0x7070
#define IF_CLEAR_CSR		0x5050
#define IF_WRITE		0x4040
#define IF_BLOCK_ERASE		0x2020
#define IF_ERASE_SUSPEND	0xb0b0
#define IF_CONFIRM		0xd0d0

/* 28F016SA Performance Enhancement Commands */
#define IF_READ_PAGE		0x7575
#define IF_PAGE_SWAP		0x7272
#define IF_SINGLE_LOAD		0x7474
#define IF_SEQ_LOAD		0xe0e0
#define IF_PAGE_WRITE		0x0c0c
#define IF_RDY_MODE		0x9696
#define IF_RDY_LEVEL		0x0101
#define IF_RDY_PULSE_WRITE	0x0202
#define IF_RDY_PULSE_ERASE	0x0303
#define IF_RDY_DISABLE		0x0404
#define IF_LOCK_BLOCK		0x7777
#define IF_UPLOAD_STATUS	0x9797
#define IF_READ_ESR		0x7171
#define IF_ERASE_UNLOCKED	0xa7a7
#define IF_SLEEP		0xf0f0
#define IF_ABORT		0x8080
#define IF_UPLOAD_DEVINFO	0x9999

/* Definitions for Compatible Status Register */
#define CSR_WR_READY		0x8080	/* Write state machine status */
#define CSR_ERA_SUSPEND		0x4040	/* Erase suspend status */
#define CSR_ERA_ERR		0x2020	/* Erase status */
#define CSR_WR_ERR		0x1010	/* Data write status */
#define CSR_VPP_LOW		0x0808	/* Vpp status */

/* Definitions for Global Status Register */
#define GSR_WR_READY		0x8080	/* Write state machine status */
#define GSR_OP_SUSPEND		0x4040	/* Operation suspend status */
#define GSR_OP_ERR		0x2020	/* Device operation status */
#define GSR_SLEEP		0x1010	/* Device sleep status */
#define GSR_QUEUE_FULL		0x0808	/* Queue status */
#define GSR_PAGE_AVAIL		0x0404	/* Page buffer available status */
#define GSR_PAGE_READY		0x0202	/* Page buffer status */
#define GSR_PAGE_SELECT		0x0101	/* Page buffer select status */

/* Definitions for Block Status Register */
#define BSR_READY		0x8080	/* Block status */
#define BSR_UNLOCK		0x4040	/* Block lock status */
#define BSR_FAILED		0x2020	/* Block operation status */
#define BSR_ABORTED		0x1010	/* Operation abort status */
#define BSR_QUEUE_FULL		0x0808	/* Queue status */
#define BSR_VPP_LOW		0x0404	/* Vpp status */

#endif /* __MTD_IFLASH_H__ */
