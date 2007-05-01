#ifndef _POWERPC_PMI_H
#define _POWERPC_PMI_H

/*
 * Definitions for talking with PMI device on PowerPC
 *
 * PMI (Platform Management Interrupt) is a way to communicate
 * with the BMC (Baseboard Management Controller) via interrupts.
 * Unlike IPMI it is bidirectional and has a low latency.
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * Author: Christian Krafft <krafft@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef __KERNEL__

#include <asm/of_device.h>

#define PMI_TYPE_FREQ_CHANGE	0x01
#define PMI_READ_TYPE		0
#define PMI_READ_DATA0		1
#define PMI_READ_DATA1		2
#define PMI_READ_DATA2		3
#define PMI_WRITE_TYPE		4
#define PMI_WRITE_DATA0		5
#define PMI_WRITE_DATA1		6
#define PMI_WRITE_DATA2		7

#define PMI_ACK			0x80

#define PMI_TIMEOUT		100

typedef struct {
	u8	type;
	u8	data0;
	u8	data1;
	u8	data2;
} pmi_message_t;

struct pmi_handler {
	struct list_head node;
	u8 type;
	void (*handle_pmi_message) (struct of_device *, pmi_message_t);
};

void pmi_register_handler(struct of_device *, struct pmi_handler *);
void pmi_unregister_handler(struct of_device *, struct pmi_handler *);

void pmi_send_message(struct of_device *, pmi_message_t);

#endif /* __KERNEL__ */
#endif /* _POWERPC_PMI_H */
