/*
 * Copyright (C) 2014-2015 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __LINUX_IPU_V3_PRG_H_
#define __LINUX_IPU_V3_PRG_H_

#include <linux/ipu-v3.h>

#define PRG_SO_INTERLACE	1
#define PRG_SO_PROGRESSIVE	0
#define PRG_BLOCK_MODE		1
#define PRG_SCAN_MODE		0

struct ipu_prg_config {
	unsigned int id;
	unsigned int pre_num;
	ipu_channel_t ipu_ch;
	unsigned int stride;
	unsigned int height;
	unsigned int ipu_height;
	unsigned int crop_line;
	unsigned int so;
	unsigned int ilo;
	unsigned int block_mode;
	bool vflip;
	u32 baddr;
	u32 offset;
};

#ifdef CONFIG_MXC_IPU_V3_PRG
int ipu_prg_config(struct ipu_prg_config *config);
int ipu_prg_disable(unsigned int ipu_id, unsigned int pre_num);
int ipu_prg_wait_buf_ready(unsigned int ipu_id, unsigned int pre_num,
			   unsigned int hsk_line_num,
			   int pre_store_out_height);
#else
int ipu_prg_config(struct ipu_prg_config *config)
{
	return -ENODEV;
}

int ipu_prg_disable(unsigned int ipu_id, unsigned int pre_num)
{
	return -ENODEV;
}

int ipu_prg_wait_buf_ready(unsigned int ipu_id, unsigned int pre_num,
			   unsigned int hsk_line_num,
			   int pre_store_out_height)
{
	return -ENODEV;
}
#endif
#endif /* __LINUX_IPU_V3_PRG_H_ */
