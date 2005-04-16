/*
 * Various TURBOchannel related stuff
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Information obtained through the get_tcinfo prom call
 * created from:
 *
 * TURBOchannel Firmware Specification
 *
 * EK-TCAAD-FS-004
 * from Digital Equipment Corporation
 *
 * Copyright (c) 1998 Harald Koerfgen
 */

typedef struct {
	int revision;
	int clk_period;
	int slot_size;
	int io_timeout;
	int dma_range;
	int max_dma_burst;
	int parity;
	int reserved[4];
} tcinfo;

#define MAX_SLOT 7

typedef struct {
	unsigned long base_addr;
	unsigned char name[9];
	unsigned char vendor[9];
	unsigned char firmware[9];
	int interrupt;
	int flags;
} slot_info;

/*
 * Values for flags
 */
#define FREE 	1<<0
#define IN_USE	1<<1


