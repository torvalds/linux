/*
 * Definitions for working with the Flattened Device Tree data format
 *
 * Copyright 2009 Benjamin Herrenschmidt, IBM Corp
 * benh@kernel.crashing.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#ifndef _LINUX_OF_FDT_H
#define _LINUX_OF_FDT_H

/* Definitions used by the flattened device tree */
#define OF_DT_HEADER		0xd00dfeed	/* marker */
#define OF_DT_BEGIN_NODE	0x1		/* Start of node, full name */
#define OF_DT_END_NODE		0x2		/* End node */
#define OF_DT_PROP		0x3		/* Property: name off, size,
						 * content */
#define OF_DT_NOP		0x4		/* nop */
#define OF_DT_END		0x9

#define OF_DT_VERSION		0x10

#endif /* _LINUX_OF_FDT_H */
