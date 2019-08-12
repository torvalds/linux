/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_SOUND_SOF_XTENSA_H__
#define __INCLUDE_SOUND_SOF_XTENSA_H__

#include <sound/sof/header.h>

/*
 * Architecture specific debug
 */

/* Xtensa Firmware Oops data */
struct sof_ipc_dsp_oops_xtensa {
	struct sof_ipc_dsp_oops_arch_hdr arch_hdr;
	struct sof_ipc_dsp_oops_plat_hdr plat_hdr;
	uint32_t exccause;
	uint32_t excvaddr;
	uint32_t ps;
	uint32_t epc1;
	uint32_t epc2;
	uint32_t epc3;
	uint32_t epc4;
	uint32_t epc5;
	uint32_t epc6;
	uint32_t epc7;
	uint32_t eps2;
	uint32_t eps3;
	uint32_t eps4;
	uint32_t eps5;
	uint32_t eps6;
	uint32_t eps7;
	uint32_t depc;
	uint32_t intenable;
	uint32_t interrupt;
	uint32_t sar;
	uint32_t debugcause;
	uint32_t windowbase;
	uint32_t windowstart;
	uint32_t excsave1;
	uint32_t ar[];
}  __packed;

#endif
