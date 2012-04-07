/*
 * Analog Devices PPI header file
 *
 * Copyright (c) 2011 Analog Devices Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#ifndef _PPI_H_
#define _PPI_H_

#include <linux/interrupt.h>

#ifdef EPPI_EN
#define PORT_EN EPPI_EN
#define DMA32 0
#define PACK_EN PACKEN
#endif

struct ppi_if;

struct ppi_params {
	int width;
	int height;
	int bpp;
	unsigned long ppi_control;
	u32 int_mask;
	int blank_clocks;
};

struct ppi_ops {
	int (*attach_irq)(struct ppi_if *ppi, irq_handler_t handler);
	void (*detach_irq)(struct ppi_if *ppi);
	int (*start)(struct ppi_if *ppi);
	int (*stop)(struct ppi_if *ppi);
	int (*set_params)(struct ppi_if *ppi, struct ppi_params *params);
	void (*update_addr)(struct ppi_if *ppi, unsigned long addr);
};

enum ppi_type {
	PPI_TYPE_PPI,
	PPI_TYPE_EPPI,
};

struct ppi_info {
	enum ppi_type type;
	int dma_ch;
	int irq_err;
	void __iomem *base;
	const unsigned short *pin_req;
};

struct ppi_if {
	unsigned long ppi_control;
	const struct ppi_ops *ops;
	const struct ppi_info *info;
	bool err_int;
	void *priv;
};

struct ppi_if *ppi_create_instance(const struct ppi_info *info);
void ppi_delete_instance(struct ppi_if *ppi);
#endif
