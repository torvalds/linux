/*
 * SBEFIFO FSI Client device driver
 *
 * Copyright (C) IBM Corporation 2017
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERGCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef LINUX_FSI_SBEFIFO_H
#define LINUX_FSI_SBEFIFO_H

#define SBEFIFO_CMD_PUT_OCC_SRAM	0xa404
#define SBEFIFO_CMD_GET_OCC_SRAM	0xa403
#define SBEFIFO_CMD_GET_SBE_FFDC	0xa801

#define SBEFIFO_MAX_FFDC_SIZE		0x2000

struct device;

int sbefifo_submit(struct device *dev, const __be32 *command, size_t cmd_len,
		   __be32 *response, size_t *resp_len);

int sbefifo_parse_status(struct device *dev, u16 cmd, __be32 *response,
			 size_t resp_len, size_t *data_len);

#endif /* LINUX_FSI_SBEFIFO_H */
