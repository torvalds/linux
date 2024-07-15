/* SPDX-License-Identifier: MIT */
/*
 * ioreq.h: I/O request definitions for device models
 * Copyright (c) 2004, Intel Corporation.
 */

#ifndef __XEN_PUBLIC_HVM_IOREQ_H__
#define __XEN_PUBLIC_HVM_IOREQ_H__

#define IOREQ_READ      1
#define IOREQ_WRITE     0

#define STATE_IOREQ_NONE        0
#define STATE_IOREQ_READY       1
#define STATE_IOREQ_INPROCESS   2
#define STATE_IORESP_READY      3

#define IOREQ_TYPE_PIO          0 /* pio */
#define IOREQ_TYPE_COPY         1 /* mmio ops */
#define IOREQ_TYPE_PCI_CONFIG   2
#define IOREQ_TYPE_TIMEOFFSET   7
#define IOREQ_TYPE_INVALIDATE   8 /* mapcache */

/*
 * VMExit dispatcher should cooperate with instruction decoder to
 * prepare this structure and notify service OS and DM by sending
 * virq.
 *
 * For I/O type IOREQ_TYPE_PCI_CONFIG, the physical address is formatted
 * as follows:
 *
 * 63....48|47..40|39..35|34..32|31........0
 * SEGMENT |BUS   |DEV   |FN    |OFFSET
 */
struct ioreq {
	uint64_t addr;          /* physical address */
	uint64_t data;          /* data (or paddr of data) */
	uint32_t count;         /* for rep prefixes */
	uint32_t size;          /* size in bytes */
	uint32_t vp_eport;      /* evtchn for notifications to/from device model */
	uint16_t _pad0;
	uint8_t state:4;
	uint8_t data_is_ptr:1;  /* if 1, data above is the guest paddr
				 * of the real data to use. */
	uint8_t dir:1;          /* 1=read, 0=write */
	uint8_t df:1;
	uint8_t _pad1:1;
	uint8_t type;           /* I/O type */
};

#endif /* __XEN_PUBLIC_HVM_IOREQ_H__ */
