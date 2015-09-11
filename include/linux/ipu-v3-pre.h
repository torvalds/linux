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
#ifndef __LINUX_IPU_V3_PRE_H_
#define __LINUX_IPU_V3_PRE_H_

#define IPU_PRE_MAX_WIDTH	1920
#define IPU_PRE_MAX_BPP		4

struct ipu_rect {
	int left;
	int top;
	int width;
	int height;
};

struct ipu_pre_context {
	bool repeat;
	bool vflip;
	bool handshake_en;
	bool hsk_abort_en;
	unsigned int hsk_line_num;
	bool sdw_update;
	unsigned int block_size;
	unsigned int interlaced;
	unsigned int prefetch_mode;

	unsigned long cur_buf;
	unsigned long next_buf;

	unsigned int tile_fmt;

	unsigned int read_burst;
	unsigned int prefetch_input_bpp;
	unsigned int prefetch_input_pixel_fmt;
	unsigned int prefetch_shift_offset;
	unsigned int prefetch_shift_width;
	bool shift_bypass;
	bool field_inverse;
	bool tpr_coor_offset_en;
	/* the output of prefetch is
	 * also the input of store
	 */
	struct ipu_rect prefetch_output_size;
	unsigned int prefetch_input_active_width;
	unsigned int prefetch_input_width;
	unsigned int prefetch_input_height;
	unsigned int store_pitch;
	int interlace_offset;

	bool store_en;
	unsigned int write_burst;
	unsigned int store_output_bpp;

	unsigned int sec_buf_off;
	unsigned int trd_buf_off;

	/* return for IPU fb caller */
	unsigned long store_addr;
};

#ifdef CONFIG_MXC_IPU_V3_PRE
int ipu_pre_alloc(int ipu_id, ipu_channel_t ipu_ch);
void ipu_pre_free(unsigned int *id);
unsigned long ipu_pre_alloc_double_buffer(unsigned int id, unsigned int size);
void ipu_pre_free_double_buffer(unsigned int id);
int ipu_pre_config(int id, struct ipu_pre_context *config);
int ipu_pre_set_ctrl(unsigned int id, struct ipu_pre_context *config);
int ipu_pre_enable(int id);
void ipu_pre_disable(int id);
int ipu_pre_set_fb_buffer(int id, unsigned long fb_paddr,
			  unsigned int x_crop,
			  unsigned int y_crop,
			  unsigned int sec_buf_off,
			  unsigned int trd_buf_off);
int ipu_pre_sdw_update(int id);
#else
int ipu_pre_alloc(int ipu_id, ipu_channel_t channel)
{
	return -ENODEV;
}

void ipu_pre_free(unsigned int *id)
{
}

unsigned long ipu_pre_alloc_double_buffer(unsigned int id, unsigned int size)
{
	return -ENODEV;
}

void ipu_pre_free_double_buffer(unsigned int id)
{
}

int ipu_pre_config(int id, struct ipu_pre_context *config)
{
	return -ENODEV;
}

int ipu_pre_set_ctrl(unsigned int id, struct ipu_pre_context *config)
{
	return -ENODEV;
}

int ipu_pre_enable(int id)
{
	return -ENODEV;
}

void ipu_pre_disable(int id)
{
	return;
}

int ipu_pre_set_fb_buffer(int id, unsigned long fb_paddr,
			  unsigned int x_crop,
			  unsigned int y_crop,
			  unsigned int sec_buf_off,
			  unsigned int trd_buf_off)
{
	return -ENODEV;
}
int ipu_pre_sdw_update(int id)
{
	return -ENODEV;
}
#endif
#endif /* __LINUX_IPU_V3_PRE_H_ */
