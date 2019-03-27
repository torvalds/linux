/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

/* \file  cn23xx_device.h
 * \brief Host Driver: Routines that perform CN23XX specific operations.
 */

#ifndef __CN23XX_PF_DEVICE_H__
#define __CN23XX_PF_DEVICE_H__

#include "cn23xx_pf_regs.h"

/*
 * Register address and configuration for a CN23XX devices.
 * If device specific changes need to be made then add a struct to include
 * device specific fields as shown in the commented section
 */
struct lio_cn23xx_pf {
	/* PCI interrupt summary register */
	uint32_t	intr_sum_reg64;

	/* PCI interrupt enable register */
	uint32_t	intr_enb_reg64;

	/* The PCI interrupt mask used by interrupt handler */
	uint64_t	intr_mask64;

	struct lio_config *conf;
};

#define BUSY_READING_REG_PF_LOOP_COUNT	10000

int	lio_cn23xx_pf_setup_device(struct octeon_device *oct);

uint32_t	lio_cn23xx_pf_get_oq_ticks(struct octeon_device *oct,
					   uint32_t time_intr_in_us);

int	lio_cn23xx_pf_fw_loaded(struct octeon_device *oct);

#endif	/* __CN23XX_PF_DEVICE_H__ */
