#ifndef __ARM_SCPI_H
#define __ARM_SCPI_H

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#define SCPI_MHU_CHANNEL_MAX		2

#define SCPI_CMD_INVALID		0x00
#define SCPI_CMD_SCP_READY		0x01
#define SCPI_CMD_SCP_CAPABILITIES	0x02
#define SCPI_CMD_FAULT			0x03
#define SCPI_CMD_GET_CLOCKS		0x13
#define SCPI_CMD_SET_CLOCK_FREQ_INDEX	0x14
#define SCPI_CMD_SET_CLOCK_FREQ		0x15
#define SCPI_CMD_GET_CLOCK_FREQ		0x16

int scpi_exec_command(uint8_t cmd, void *payload, int size,
			void *reply_payload, int rsize);

#endif /* __ARM_SCPI_H */
