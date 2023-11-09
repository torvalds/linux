/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * spi-codec.h - Rockchip Spi-Codec Driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef __SPI_CODEC_H__
#define __SPI_CODEC_H__

#define PAYLOAD_MAX				(4096)

#define SS_CMD_BEGIN				(0xF4190BE6UL)

#define SS_CMD_PROGRAM				(0xFAAD0552UL)
#define SS_CMD_PARAMETER_SAFE			(0xA5015AFEUL)
#define SS_CMD_BLOCK_PARAMETER_SAFE		(0x4EA5B15AUL)
#define SS_CMD_BLOCK_PARAMETER_NO_SAFE		(0xFFA1F05EUL)

#define SS_CMD_END				(0xF1D20E2DUL)
#define SS_CMD_PARTIAL_END			(0xE1D21E2DUL)

#define SS_CMD_READ_REQUEST			(0xF3D20C2DUL)
#define SS_CMD_BK_MIPS_VALUE			(0x00CE6319UL)
#define SS_CMD_BK_ERROR_CODE			(0x00F7E808UL)
#define SS_CMD_BK_READ_VALUE			(0x00BC543AUL)
#define SS_CMD_BK_ACKNOWLEDGE			(0x00000000UL)
#define SS_CMD_BK_VERSION_INFO			(0x003EDC12UL)

struct spi_codec_protocol_packet {
	unsigned int cmd_begin;

	/* indicates the nature of payload */
	unsigned int cmd_type;

	/* payload data length */
	unsigned int payload_len;
	unsigned int payload[PAYLOAD_MAX];

	/* payload or entire packet */
	unsigned int crc;

	/* parameter id */
	unsigned int address;

	unsigned int cmd_end;
};

#endif
