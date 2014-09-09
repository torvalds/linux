/*
 * Copyright 2008-2014 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 *
 * @file mxc_asrc.h
 *
 * @brief i.MX Asynchronous Sample Rate Converter
 *
 * @ingroup Audio
 */

#ifndef __MXC_ASRC_UAPI_H__
#define __MXC_ASRC_UAPI_H__

#define ASRC_IOC_MAGIC		'C'

#define ASRC_REQ_PAIR		_IOWR(ASRC_IOC_MAGIC, 0, struct asrc_req)
#define ASRC_CONFIG_PAIR	_IOWR(ASRC_IOC_MAGIC, 1, struct asrc_config)
#define ASRC_RELEASE_PAIR	_IOW(ASRC_IOC_MAGIC, 2, enum asrc_pair_index)
#define ASRC_CONVERT		_IOW(ASRC_IOC_MAGIC, 3, struct asrc_convert_buffer)
#define ASRC_START_CONV		_IOW(ASRC_IOC_MAGIC, 4, enum asrc_pair_index)
#define ASRC_STOP_CONV		_IOW(ASRC_IOC_MAGIC, 5, enum asrc_pair_index)
#define ASRC_STATUS		_IOW(ASRC_IOC_MAGIC, 6, struct asrc_status_flags)
#define ASRC_FLUSH		_IOW(ASRC_IOC_MAGIC, 7, enum asrc_pair_index)

enum asrc_pair_index {
	ASRC_INVALID_PAIR = -1,
	ASRC_PAIR_A = 0,
	ASRC_PAIR_B = 1,
	ASRC_PAIR_C = 2,
};

#define ASRC_PAIR_MAX_NUM	(ASRC_PAIR_C + 1)

enum asrc_inclk {
	INCLK_NONE = 0x03,
	INCLK_ESAI_RX = 0x00,
	INCLK_SSI1_RX = 0x01,
	INCLK_SSI2_RX = 0x02,
	INCLK_SSI3_RX = 0x07,
	INCLK_SPDIF_RX = 0x04,
	INCLK_MLB_CLK = 0x05,
	INCLK_PAD = 0x06,
	INCLK_ESAI_TX = 0x08,
	INCLK_SSI1_TX = 0x09,
	INCLK_SSI2_TX = 0x0a,
	INCLK_SSI3_TX = 0x0b,
	INCLK_SPDIF_TX = 0x0c,
	INCLK_ASRCK1_CLK = 0x0f,
};

enum asrc_outclk {
	OUTCLK_NONE = 0x03,
	OUTCLK_ESAI_TX = 0x00,
	OUTCLK_SSI1_TX = 0x01,
	OUTCLK_SSI2_TX = 0x02,
	OUTCLK_SSI3_TX = 0x07,
	OUTCLK_SPDIF_TX = 0x04,
	OUTCLK_MLB_CLK = 0x05,
	OUTCLK_PAD = 0x06,
	OUTCLK_ESAI_RX = 0x08,
	OUTCLK_SSI1_RX = 0x09,
	OUTCLK_SSI2_RX = 0x0a,
	OUTCLK_SSI3_RX = 0x0b,
	OUTCLK_SPDIF_RX = 0x0c,
	OUTCLK_ASRCK1_CLK = 0x0f,
};

enum asrc_word_width {
	ASRC_WIDTH_24_BIT = 0,
	ASRC_WIDTH_16_BIT = 1,
	ASRC_WIDTH_8_BIT = 2,
};

struct asrc_config {
	enum asrc_pair_index pair;
	unsigned int channel_num;
	unsigned int buffer_num;
	unsigned int dma_buffer_size;
	unsigned int input_sample_rate;
	unsigned int output_sample_rate;
	enum asrc_word_width input_word_width;
	enum asrc_word_width output_word_width;
	enum asrc_inclk inclk;
	enum asrc_outclk outclk;
};

struct asrc_req {
	unsigned int chn_num;
	enum asrc_pair_index index;
};

struct asrc_querybuf {
	unsigned int buffer_index;
	unsigned int input_length;
	unsigned int output_length;
	unsigned long input_offset;
	unsigned long output_offset;
};

struct asrc_convert_buffer {
	void *input_buffer_vaddr;
	void *output_buffer_vaddr;
	unsigned int input_buffer_length;
	unsigned int output_buffer_length;
};

struct asrc_status_flags {
	enum asrc_pair_index index;
	unsigned int overload_error;
};

enum asrc_error_status {
	ASRC_TASK_Q_OVERLOAD		= 0x01,
	ASRC_OUTPUT_TASK_OVERLOAD	= 0x02,
	ASRC_INPUT_TASK_OVERLOAD	= 0x04,
	ASRC_OUTPUT_BUFFER_OVERFLOW	= 0x08,
	ASRC_INPUT_BUFFER_UNDERRUN	= 0x10,
};
#endif/* __MXC_ASRC_UAPI_H__ */
