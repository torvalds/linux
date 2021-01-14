/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Management Complex (MC) userspace public interface
 *
 * Copyright 2021 NXP
 *
 */
#ifndef _UAPI_FSL_MC_H_
#define _UAPI_FSL_MC_H_

#include <linux/types.h>

#define MC_CMD_NUM_OF_PARAMS	7

/**
 * struct fsl_mc_command - Management Complex (MC) command structure
 * @header: MC command header
 * @params: MC command parameters
 */
struct fsl_mc_command {
	__le64 header;
	__le64 params[MC_CMD_NUM_OF_PARAMS];
};

#endif /* _UAPI_FSL_MC_H_ */
