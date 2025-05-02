/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel Core SoC Power Management Controller Header File
 *
 * Copyright (c) 2025, Intel Corporation.
 * All Rights Reserved.
 *
 */
#ifndef INTEL_PMC_IPC_H
#define INTEL_PMC_IPC_H
#include <linux/acpi.h>

#define IPC_SOC_REGISTER_ACCESS			0xAA
#define IPC_SOC_SUB_CMD_READ			0x00
#define IPC_SOC_SUB_CMD_WRITE			0x01
#define PMC_IPCS_PARAM_COUNT			7
#define VALID_IPC_RESPONSE			5

struct pmc_ipc_cmd {
	u32 cmd;
	u32 sub_cmd;
	u32 size;
	u32 wbuf[4];
};

struct pmc_ipc_rbuf {
	u32 buf[4];
};

/**
 * intel_pmc_ipc() - PMC IPC Mailbox accessor
 * @ipc_cmd:  Prepared input command to send
 * @rbuf:     Allocated array for returned IPC data
 *
 * Return: 0 on success. Non-zero on mailbox error
 */
static inline int intel_pmc_ipc(struct pmc_ipc_cmd *ipc_cmd, struct pmc_ipc_rbuf *rbuf)
{
#ifdef CONFIG_ACPI
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object params[PMC_IPCS_PARAM_COUNT] = {
		{.type = ACPI_TYPE_INTEGER,},
		{.type = ACPI_TYPE_INTEGER,},
		{.type = ACPI_TYPE_INTEGER,},
		{.type = ACPI_TYPE_INTEGER,},
		{.type = ACPI_TYPE_INTEGER,},
		{.type = ACPI_TYPE_INTEGER,},
		{.type = ACPI_TYPE_INTEGER,},
	};
	struct acpi_object_list arg_list = { PMC_IPCS_PARAM_COUNT, params };
	union acpi_object *obj;
	int status;

	if (!ipc_cmd || !rbuf)
		return -EINVAL;

	/*
	 * 0: IPC Command
	 * 1: IPC Sub Command
	 * 2: Size
	 * 3-6: Write Buffer for offset
	 */
	params[0].integer.value = ipc_cmd->cmd;
	params[1].integer.value = ipc_cmd->sub_cmd;
	params[2].integer.value = ipc_cmd->size;
	params[3].integer.value = ipc_cmd->wbuf[0];
	params[4].integer.value = ipc_cmd->wbuf[1];
	params[5].integer.value = ipc_cmd->wbuf[2];
	params[6].integer.value = ipc_cmd->wbuf[3];

	status = acpi_evaluate_object(NULL, "\\IPCS", &arg_list, &buffer);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	obj = buffer.pointer;

	if (obj && obj->type == ACPI_TYPE_PACKAGE &&
	    obj->package.count == VALID_IPC_RESPONSE) {
		const union acpi_object *objs = obj->package.elements;

		if ((u8)objs[0].integer.value != 0)
			return -EINVAL;

		rbuf->buf[0] = objs[1].integer.value;
		rbuf->buf[1] = objs[2].integer.value;
		rbuf->buf[2] = objs[3].integer.value;
		rbuf->buf[3] = objs[4].integer.value;
	} else {
		return -EINVAL;
	}

	return 0;
#else
	return -ENODEV;
#endif /* CONFIG_ACPI */
}

#endif /* INTEL_PMC_IPC_H */
