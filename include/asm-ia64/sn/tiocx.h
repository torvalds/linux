/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2005 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef _ASM_IA64_SN_TIO_TIOCX_H
#define _ASM_IA64_SN_TIO_TIOCX_H

#ifdef __KERNEL__

struct cx_id_s {
	unsigned int part_num;
	unsigned int mfg_num;
	int nasid;
};

struct cx_dev {
	struct cx_id_s cx_id;
	void *soft;			/* driver specific */
	struct hubdev_info *hubdev;
	struct device dev;
	struct cx_drv *driver;
};

struct cx_device_id {
	unsigned int part_num;
	unsigned int mfg_num;
};

struct cx_drv {
	char *name;
	const struct cx_device_id *id_table;
	struct device_driver driver;
	int (*probe) (struct cx_dev * dev, const struct cx_device_id * id);
	int (*remove) (struct cx_dev * dev);
};

/* create DMA address by stripping AS bits */
#define TIOCX_DMA_ADDR(a) (uint64_t)((uint64_t)(a) & 0xffffcfffffffffUL)

#define TIOCX_TO_TIOCX_DMA_ADDR(a) (uint64_t)(((uint64_t)(a) & 0xfffffffff) |  \
                                  ((((uint64_t)(a)) & 0xffffc000000000UL) <<2))

#define TIO_CE_ASIC_PARTNUM 0xce00
#define TIOCX_CORELET 3

/* These are taken from tio_mmr_as.h */
#define TIO_ICE_FRZ_CFG               TIO_MMR_ADDR_MOD(0x00000000b0008100UL)
#define TIO_ICE_PMI_TX_CFG            TIO_MMR_ADDR_MOD(0x00000000b000b100UL)
#define TIO_ICE_PMI_TX_DYN_CREDIT_STAT_CB3 TIO_MMR_ADDR_MOD(0x00000000b000be18UL)
#define TIO_ICE_PMI_TX_DYN_CREDIT_STAT_CB3_CREDIT_CNT_MASK 0x000000000000000fUL

#define to_cx_dev(n) container_of(n, struct cx_dev, dev)
#define to_cx_driver(drv) container_of(drv, struct cx_drv, driver)

extern struct sn_irq_info *tiocx_irq_alloc(nasid_t, int, int, nasid_t, int);
extern void tiocx_irq_free(struct sn_irq_info *);
extern int cx_device_unregister(struct cx_dev *);
extern int cx_device_register(nasid_t, int, int, struct hubdev_info *);
extern int cx_driver_unregister(struct cx_drv *);
extern int cx_driver_register(struct cx_drv *);
extern uint64_t tiocx_dma_addr(uint64_t addr);
extern uint64_t tiocx_swin_base(int nasid);
extern void tiocx_mmr_store(int nasid, uint64_t offset, uint64_t value);
extern uint64_t tiocx_mmr_load(int nasid, uint64_t offset);

#endif				//  __KERNEL__
#endif				// _ASM_IA64_SN_TIO_TIOCX__
