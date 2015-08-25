/*
 * Copyright 2004-2013, 2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU Lesser General
 * Public License.  You may obtain a copy of the GNU Lesser General
 * Public License Version 2.1 or later at the following locations:
 *
 * http://www.opensource.org/licenses/lgpl-license.html
 * http://www.gnu.org/copyleft/lgpl.html
 */

/*!
 * @defgroup VPU Video Processor Unit Driver
 */

/*!
 * @file linux/mxc_vpu.h
 *
 * @brief VPU system initialization and file operation definition
 *
 * @ingroup VPU
 */

#ifndef __LINUX_MXC_VPU_H__
#define __LINUX_MXC_VPU_H__

#include <linux/fs.h>

struct mxc_vpu_platform_data {
	bool iram_enable;
	int  iram_size;
	void (*reset) (void);
	void (*pg) (int);
};

struct vpu_mem_desc {
	u32 size;
	dma_addr_t phy_addr;
	u32 cpu_addr;		/* cpu address to free the dma mem */
	u32 virt_uaddr;		/* virtual user space address */
};

#define VPU_IOC_MAGIC  'V'

#define VPU_IOC_PHYMEM_ALLOC	_IO(VPU_IOC_MAGIC, 0)
#define VPU_IOC_PHYMEM_FREE	_IO(VPU_IOC_MAGIC, 1)
#define VPU_IOC_WAIT4INT	_IO(VPU_IOC_MAGIC, 2)
#define VPU_IOC_PHYMEM_DUMP	_IO(VPU_IOC_MAGIC, 3)
#define VPU_IOC_REG_DUMP	_IO(VPU_IOC_MAGIC, 4)
#define VPU_IOC_IRAM_SETTING	_IO(VPU_IOC_MAGIC, 6)
#define VPU_IOC_CLKGATE_SETTING	_IO(VPU_IOC_MAGIC, 7)
#define VPU_IOC_GET_WORK_ADDR   _IO(VPU_IOC_MAGIC, 8)
#define VPU_IOC_REQ_VSHARE_MEM	_IO(VPU_IOC_MAGIC, 9)
#define VPU_IOC_SYS_SW_RESET	_IO(VPU_IOC_MAGIC, 11)
#define VPU_IOC_GET_SHARE_MEM   _IO(VPU_IOC_MAGIC, 12)
#define VPU_IOC_QUERY_BITWORK_MEM  _IO(VPU_IOC_MAGIC, 13)
#define VPU_IOC_SET_BITWORK_MEM    _IO(VPU_IOC_MAGIC, 14)
#define VPU_IOC_PHYMEM_CHECK	_IO(VPU_IOC_MAGIC, 15)
#define VPU_IOC_LOCK_DEV	_IO(VPU_IOC_MAGIC, 16)

#define BIT_CODE_RUN			0x000
#define BIT_CODE_DOWN			0x004
#define BIT_INT_CLEAR			0x00C
#define BIT_INT_STATUS			0x010
#define BIT_CUR_PC			0x018
#define BIT_INT_REASON			0x174

#define MJPEG_PIC_STATUS_REG		0x3004
#define MBC_SET_SUBBLK_EN		0x4A0

#define BIT_WORK_CTRL_BUF_BASE		0x100
#define BIT_WORK_CTRL_BUF_REG(i)	(BIT_WORK_CTRL_BUF_BASE + i * 4)
#define BIT_CODE_BUF_ADDR		BIT_WORK_CTRL_BUF_REG(0)
#define BIT_WORK_BUF_ADDR		BIT_WORK_CTRL_BUF_REG(1)
#define BIT_PARA_BUF_ADDR		BIT_WORK_CTRL_BUF_REG(2)
#define BIT_BIT_STREAM_CTRL		BIT_WORK_CTRL_BUF_REG(3)
#define BIT_FRAME_MEM_CTRL		BIT_WORK_CTRL_BUF_REG(4)
#define BIT_BIT_STREAM_PARAM		BIT_WORK_CTRL_BUF_REG(5)

#ifndef CONFIG_SOC_IMX6Q
#define BIT_RESET_CTRL			0x11C
#else
#define BIT_RESET_CTRL			0x128
#endif

/* i could be 0, 1, 2, 3 */
#define	BIT_RD_PTR_BASE			0x120
#define BIT_RD_PTR_REG(i)		(BIT_RD_PTR_BASE + i * 8)
#define BIT_WR_PTR_REG(i)		(BIT_RD_PTR_BASE + i * 8 + 4)

/* i could be 0, 1, 2, 3 */
#define BIT_FRM_DIS_FLG_BASE		(cpu_is_mx51() ? 0x150 : 0x140)
#define	BIT_FRM_DIS_FLG_REG(i)		(BIT_FRM_DIS_FLG_BASE + i * 4)

#define BIT_BUSY_FLAG			0x160
#define BIT_RUN_COMMAND			0x164
#define BIT_INT_ENABLE			0x170

#define	BITVAL_PIC_RUN			8

#define	VPU_SLEEP_REG_VALUE		10
#define	VPU_WAKE_REG_VALUE		11

int vl2cc_init(u32 vl2cc_hw_base);
void vl2cc_enable(void);
void vl2cc_flush(void);
void vl2cc_disable(void);
void vl2cc_cleanup(void);

int vl2cc_init(u32 vl2cc_hw_base);
void vl2cc_enable(void);
void vl2cc_flush(void);
void vl2cc_disable(void);
void vl2cc_cleanup(void);

#endif
