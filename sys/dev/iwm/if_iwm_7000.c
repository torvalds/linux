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
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2015 Intel Deutschland GmbH
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
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2015 Intel Deutschland GmbH
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"
#include "opt_iwm.h"

#include <sys/param.h>

#include "if_iwm_config.h"

#define IWM7260_FW	"iwm7260fw"
#define IWM3160_FW	"iwm3160fw"
#define IWM3168_FW	"iwm3168fw"
#define IWM7265_FW	"iwm7265fw"
#define IWM7265D_FW	"iwm7265Dfw"

#define IWM_NVM_HW_SECTION_NUM_FAMILY_7000	0

#define IWM_DEVICE_7000_COMMON						\
	.device_family = IWM_DEVICE_FAMILY_7000,			\
	.eeprom_size = IWM_OTP_LOW_IMAGE_SIZE_FAMILY_7000,		\
	.nvm_hw_section_num = IWM_NVM_HW_SECTION_NUM_FAMILY_7000,	\
	.apmg_wake_up_wa = 1

const struct iwm_cfg iwm7260_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 7260",
	.fw_name = IWM7260_FW,
	IWM_DEVICE_7000_COMMON,
	.host_interrupt_operation_mode = 1,
};

const struct iwm_cfg iwm3160_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 3160",
	.fw_name = IWM3160_FW,
	IWM_DEVICE_7000_COMMON,
	.host_interrupt_operation_mode = 1,
};

const struct iwm_cfg iwm3165_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 3165",
	.fw_name = IWM7265D_FW,
	IWM_DEVICE_7000_COMMON,
	.host_interrupt_operation_mode = 0,
};

const struct iwm_cfg iwm3168_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 3168",
	.fw_name = IWM3168_FW,
	IWM_DEVICE_7000_COMMON,
	.host_interrupt_operation_mode = 0,
	.nvm_type = IWM_NVM_SDP,
};

const struct iwm_cfg iwm7265_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 7265",
	.fw_name = IWM7265_FW,
	IWM_DEVICE_7000_COMMON,
	.host_interrupt_operation_mode = 0,
};

const struct iwm_cfg iwm7265d_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 7265",
	.fw_name = IWM7265D_FW,
	IWM_DEVICE_7000_COMMON,
	.host_interrupt_operation_mode = 0,
};

