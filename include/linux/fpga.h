/*
 * FPGA Framework
 *
 *  Copyright (C) 2013 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/completion.h>

#ifndef _LINUX_FPGA_H
#define _LINUX_FPGA_H

struct fpga_manager;
struct fpga_mgr_transport;

/*---------------------------------------------------------------------------*/

struct fpga_mgr_transport {
	struct fpga_manager *mgr;
	char type[16];

	u32 (*reg_readl)(struct fpga_mgr_transport *transp, u32 reg_offset);
	void (*reg_writel)(struct fpga_mgr_transport *transp, u32 reg_offset,
			   u32 value);
	u32 (*reg_raw_readl)(struct fpga_mgr_transport *transp, u32 reg_offset);
	void (*reg_raw_writel)(struct fpga_mgr_transport *transp, u32 reg_offset,
			   u32 value);
	u32 (*data_readl)(struct fpga_mgr_transport *transp);
	void (*data_writel)(struct fpga_mgr_transport *transp, u32 value);

	void __iomem *fpga_base_addr;
	void __iomem *fpga_data_addr;
};

extern int fpga_mgr_attach_transport(struct fpga_manager *mgr);
extern void fpga_mgr_detach_transport(struct fpga_manager *mgr);

extern int fpga_attach_mmio_transport(struct fpga_manager *mgr);
extern void fpga_detach_mmio_transport(struct fpga_manager *mgr);

/*---------------------------------------------------------------------------*/

/*
 * fpga_manager_ops are the low level functions implemented by a specific
 * fpga manager driver.  Leaving any of these out that aren't needed is fine
 * as they are all tested for NULL before being called.
 */
struct fpga_manager_ops {
	/* Returns a string of the FPGA's status */
	int (*status)(struct fpga_manager *mgr, char *buf);

	/* Prepare the FPGA for reading its confuration data */
	int (*read_init)(struct fpga_manager *mgr);

	/* Read count bytes configuration data from the FPGA */
	ssize_t (*read)(struct fpga_manager *mgr, char *buf, size_t count);

	/* Return FPGA to a default state after reading is done */
	int (*read_complete)(struct fpga_manager *mgr);

	/* Prepare the FPGA to receive confuration data */
	int (*write_init)(struct fpga_manager *mgr);

	/* Write count bytes of configuration data to the FPGA */
	ssize_t (*write)(struct fpga_manager *mgr, char *buf, size_t count);

	/* Return FPGA to default state after writing is done */
	int (*write_complete)(struct fpga_manager *mgr);

	/* Set FPGA into a specific state during driver remove */
	void (*fpga_remove)(struct fpga_manager *mgr);

	/* FPGA mangager isr */
	irqreturn_t (*isr)(int irq, void *dev_id);
};

/* flag bits */
#define FPGA_MGR_DEV_BUSY 0

struct fpga_manager {
	struct device_node *np;
	struct device *parent;
	struct device *dev;
	struct cdev cdev;
	int irq;
	struct completion status_complete;

	int nr;
	char name[48];
	unsigned long flags;
	struct fpga_manager_ops *mops;
	struct fpga_mgr_transport *transp;

	void *priv;
};

#if defined(CONFIG_FPGA) || defined(CONFIG_FPGA_MODULE)

extern int register_fpga_manager(struct platform_device *pdev,
				 struct fpga_manager_ops *mops,
				 char *name, void *priv);

extern void remove_fpga_manager(struct platform_device *pdev);

/*
 * Read/write FPGA manager registers
 */
static inline u32 fpga_mgr_reg_readl(struct fpga_manager *mgr, u32 offset)
{
	struct fpga_mgr_transport *transp = mgr->transp;
	return transp->reg_readl(transp, offset);
}

static inline void fpga_mgr_reg_writel(struct fpga_manager *mgr, u32 offset,
				       u32 value)
{
	struct fpga_mgr_transport *transp = mgr->transp;
	transp->reg_writel(transp, offset, value);
}

static inline u32 fpga_mgr_reg_raw_readl(struct fpga_manager *mgr, u32 offset)
{
	struct fpga_mgr_transport *transp = mgr->transp;
	return transp->reg_raw_readl(transp, offset);
}

static inline void fpga_mgr_reg_raw_writel(struct fpga_manager *mgr, u32 offset,
				       u32 value)
{
	struct fpga_mgr_transport *transp = mgr->transp;
	transp->reg_raw_writel(transp, offset, value);
}

static inline void fpga_mgr_reg_set_bitsl(struct fpga_manager *mgr,
					  u32 offset, u32 bits)
{
	u32 val;
	val = fpga_mgr_reg_readl(mgr, offset);
	val |= bits;
	fpga_mgr_reg_writel(mgr, offset, val);
}

static inline void fpga_mgr_reg_clr_bitsl(struct fpga_manager *mgr,
					  u32 offset, u32 bits)
{
	u32 val;
	val = fpga_mgr_reg_readl(mgr, offset);
	val &= ~bits;
	fpga_mgr_reg_writel(mgr, offset, val);
}

/*
 * Read/write FPGA configuration data
 */
static inline u32 fpga_mgr_data_readl(struct fpga_manager *mgr)
{
	struct fpga_mgr_transport *transp = mgr->transp;
	return transp->data_readl(transp);
}

static inline void fpga_mgr_data_writel(struct fpga_manager *mgr, u32 value)
{
	struct fpga_mgr_transport *transp = mgr->transp;
	transp->data_writel(transp, value);
}

#endif /* CONFIG_FPGA */
#endif /*_LINUX_FPGA_H */
