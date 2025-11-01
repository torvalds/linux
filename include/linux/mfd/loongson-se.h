/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2025 Loongson Technology Corporation Limited */

#ifndef __MFD_LOONGSON_SE_H__
#define __MFD_LOONGSON_SE_H__

#define LOONGSON_ENGINE_CMD_TIMEOUT_US	10000
#define SE_SEND_CMD_REG			0x0
#define SE_SEND_CMD_REG_LEN		0x8
/* Controller command ID */
#define SE_CMD_START			0x0
#define SE_CMD_SET_DMA			0x3
#define SE_CMD_SET_ENGINE_CMDBUF	0x4

#define SE_S2LINT_STAT			0x88
#define SE_S2LINT_EN			0x8c
#define SE_S2LINT_CL			0x94
#define SE_L2SINT_STAT			0x98
#define SE_L2SINT_SET			0xa0

#define SE_INT_ALL			0xffffffff
#define SE_INT_CONTROLLER		BIT(0)

#define SE_ENGINE_MAX			16
#define SE_ENGINE_RNG			1
#define SE_CMD_RNG			0x100

#define SE_ENGINE_TPM			5
#define SE_CMD_TPM			0x500

#define SE_ENGINE_CMD_SIZE		32

struct loongson_se_engine {
	struct loongson_se *se;
	int id;

	/* Command buffer */
	void *command;
	void *command_ret;

	void *data_buffer;
	uint buffer_size;
	/* Data buffer offset to DMA base */
	uint buffer_off;

	struct completion completion;

};

struct loongson_se_engine *loongson_se_init_engine(struct device *dev, int id);
int loongson_se_send_engine_cmd(struct loongson_se_engine *engine);

#endif
