/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 ******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2014 Intel Corporation. All rights reserved.
 * Copyright (C) 2016 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright (C) 2016 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

/*
 * $FreeBSD$
 */

#ifndef __IWM_CONFIG_H__
#define __IWM_CONFIG_H__

enum iwm_device_family {
	IWM_DEVICE_FAMILY_UNDEFINED,
	IWM_DEVICE_FAMILY_7000,
	IWM_DEVICE_FAMILY_8000,
};

#define IWM_DEFAULT_MAX_TX_POWER	22

/* Antenna presence definitions */
#define	IWM_ANT_NONE	0x0
#define	IWM_ANT_A	(1 << 0)
#define	IWM_ANT_B	(1 << 1)
#define IWM_ANT_C	(1 << 2)
#define	IWM_ANT_AB	(IWM_ANT_A | IWM_ANT_B)
#define	IWM_ANT_AC	(IWM_ANT_A | IWM_ANT_C)
#define IWM_ANT_BC	(IWM_ANT_B | IWM_ANT_C)
#define IWM_ANT_ABC	(IWM_ANT_A | IWM_ANT_B | IWM_ANT_C)

static inline uint8_t num_of_ant(uint8_t mask)
{
	return  !!((mask) & IWM_ANT_A) +
		!!((mask) & IWM_ANT_B) +
		!!((mask) & IWM_ANT_C);
}

/* lower blocks contain EEPROM image and calibration data */
#define IWM_OTP_LOW_IMAGE_SIZE_FAMILY_7000	(16 * 512 * sizeof(uint16_t)) /* 16 KB */
#define IWM_OTP_LOW_IMAGE_SIZE_FAMILY_8000	(32 * 512 * sizeof(uint16_t)) /* 32 KB */
#define IWM_OTP_LOW_IMAGE_SIZE_FAMILY_9000	IWM_OTP_LOW_IMAGE_SIZE_FAMILY_8000


/**
 * enum iwl_nvm_type - nvm formats
 * @IWM_NVM: the regular format
 * @IWM_NVM_EXT: extended NVM format
 * @IWM_NVM_SDP: NVM format used by 3168 series
 */
enum iwm_nvm_type {
	IWM_NVM,
	IWM_NVM_EXT,
	IWM_NVM_SDP,
};

/**
 * struct iwm_cfg
 * @name: Official name of the device
 * @fw_name: Firmware filename.
 * @host_interrupt_operation_mode: device needs host interrupt operation
 *      mode set
 * @nvm_hw_section_num: the ID of the HW NVM section
 * @apmg_wake_up_wa: should the MAC access REQ be asserted when a command
 *      is in flight. This is due to a HW bug in 7260, 3160 and 7265.
 * @nvm_type: see &enum iwl_nvm_type
 */
struct iwm_cfg {
	const char *name;
        const char *fw_name;
        uint16_t eeprom_size;
        enum iwm_device_family device_family;
        int host_interrupt_operation_mode;
        uint8_t nvm_hw_section_num;
        int apmg_wake_up_wa;
        enum iwm_nvm_type nvm_type;
};

/*
 * This list declares the config structures for all devices.
 */
extern const struct iwm_cfg iwm7260_cfg;
extern const struct iwm_cfg iwm3160_cfg;
extern const struct iwm_cfg iwm3165_cfg;
extern const struct iwm_cfg iwm3168_cfg;
extern const struct iwm_cfg iwm7265_cfg;
extern const struct iwm_cfg iwm7265d_cfg;
extern const struct iwm_cfg iwm8260_cfg;
extern const struct iwm_cfg iwm8265_cfg;

#endif /* __IWM_CONFIG_H__ */
