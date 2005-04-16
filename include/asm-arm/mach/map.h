/*
 *  linux/include/asm-arm/map.h
 *
 *  Copyright (C) 1999-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Page table mapping constructs and function prototypes
 */
struct map_desc {
	unsigned long virtual;
	unsigned long physical;
	unsigned long length;
	unsigned int type;
};

struct meminfo;

#define MT_DEVICE		0
#define MT_CACHECLEAN		1
#define MT_MINICLEAN		2
#define MT_LOW_VECTORS		3
#define MT_HIGH_VECTORS		4
#define MT_MEMORY		5
#define MT_ROM			6
#define MT_IXP2000_DEVICE	7

extern void create_memmap_holes(struct meminfo *);
extern void memtable_init(struct meminfo *);
extern void iotable_init(struct map_desc *, int);
extern void setup_io_desc(void);
