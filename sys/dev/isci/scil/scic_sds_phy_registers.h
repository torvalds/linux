/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
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
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
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
 * $FreeBSD$
 */
#ifndef _SCIC_SDS_PHY_REGISTERS_H_
#define _SCIC_SDS_PHY_REGISTERS_H_

/**
 * @file
 *
 * @brief This file contains the macros used by the phy object to read/write
 *        to the SCU link layer registers.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/scic_sds_controller.h>

//*****************************************************************************
//* SCU LINK LAYER REGISTER OPERATIONS
//*****************************************************************************

/**
 * Macro to read the transport layer register associated with this phy
 * object.
 */
#define scu_transport_layer_read(phy, reg) \
   scu_register_read( \
      scic_sds_phy_get_controller(phy), \
      (phy)->transport_layer_registers->reg \
   )

/**
 * Macro to write the transport layer register associated with this phy
 * object.
 */
#define scu_transport_layer_write(phy, reg, value) \
   scu_register_write( \
      scic_sds_phy_get_controller(phy), \
      (phy)->transport_layer_registers->reg, \
      (value) \
   )

//****************************************************************************
//* Transport Layer registers controlled by the phy object
//****************************************************************************

/**
 * This macro reads the Transport layer control register
 */
#define SCU_TLCR_READ(phy) \
   scu_transport_layer_read(phy, control)

/**
 * This macro writes the Transport layer control register
 */
#define SCU_TLCR_WRITE(phy, value) \
   scu_transport_layer_write(phy, control, value)

/**
 * This macro reads the Transport layer address translation register
 */
#define SCU_TLADTR_READ(phy) \
   scu_transport_layer_read(phy, address_translation)

/**
 * This macro writes the Transport layer address translation register
 */
#define SCU_TLADTR_WRITE(phy) \
   scu_transport_layer_write(phy, address_translation, value)

/**
 * This macro writes the STP Transport Layer Direct Attached RNi register.
 */
#define SCU_STPTLDARNI_WRITE(phy, index) \
   scu_transport_layer_write(phy, stp_rni, index)

/**
 * This macro reads the STP Transport Layer Direct Attached RNi register.
 */
#define SCU_STPTLDARNI_READ(phy) \
   scu_transport_layer_read(phy, stp_rni)

//*****************************************************************************
//* SCU LINK LAYER REGISTER OPERATIONS
//*****************************************************************************

/**
 * THis macro requests the SCU register write for the specified link layer
 * register.
 */
#define scu_link_layer_register_read(phy, reg) \
   scu_register_read( \
      scic_sds_phy_get_controller(phy), \
      (phy)->link_layer_registers->reg \
   )

/**
 * This macro requests the SCU register read for the specified link layer
 * register.
 */
#define scu_link_layer_register_write(phy, reg, value) \
   scu_register_write( \
      scic_sds_phy_get_controller(phy), \
      (phy)->link_layer_registers->reg, \
      (value) \
   )

//*****************************************************************************
//* SCU LINK LAYER REGISTERS
//*****************************************************************************

/// This macro reads from the SAS Identify Frame PHY Identifier register
#define SCU_SAS_TIPID_READ(phy) \
    scu_link_layer_register_read(phy, identify_frame_phy_id)

/// This macro writes to the SAS Identify Frame PHY Identifier register
#define SCU_SAS_TIPID_WRITE(phy, value) \
    scu_link_layer_register_write(phy, identify_frame_phy_id, value)

/// This macro reads from the SAS Identification register
#define SCU_SAS_TIID_READ(phy) \
    scu_link_layer_register_read(phy, transmit_identification)

/// This macro writes to the SAS Identification register
#define SCU_SAS_TIID_WRITE(phy, value) \
    scu_link_layer_register_write(phy, transmit_identification, value)

/// This macro reads the SAS Device Name High register
#define SCU_SAS_TIDNH_READ(phy) \
    scu_link_layer_register_read(phy, sas_device_name_high)

/// This macro writes the SAS Device Name High register
#define SCU_SAS_TIDNH_WRITE(phy, value) \
    scu_link_layer_register_write(phy, sas_device_name_high, value)

/// This macro reads the SAS Device Name Low register
#define SCU_SAS_TIDNL_READ(phy) \
    scu_link_layer_register_read(phy, sas_device_name_low)

/// This macro writes the SAS Device Name Low register
#define SCU_SAS_TIDNL_WRITE(phy, value) \
    scu_link_layer_register_write(phy, sas_device_name_low, value)

/// This macro reads the Source SAS Address High register
#define SCU_SAS_TISSAH_READ(phy) \
    scu_link_layer_register_read(phy, source_sas_address_high)

/// This macro writes the Source SAS Address High register
#define SCU_SAS_TISSAH_WRITE(phy, value) \
    scu_link_layer_register_write(phy, source_sas_address_high, value)

/// This macro reads the Source SAS Address Low register
#define SCU_SAS_TISSAL_READ(phy) \
    scu_link_layer_register_read(phy, source_sas_address_low)

/// This macro writes the Source SAS Address Low register
#define SCU_SAS_TISSAL_WRITE(phy, value) \
    scu_link_layer_register_write(phy, source_sas_address_low, value)

/// This macro reads the PHY Configuration register
#define SCU_SAS_PCFG_READ(phy) \
    scu_link_layer_register_read(phy, phy_configuration);

/// This macro writes the PHY Configuration register
#define SCU_SAS_PCFG_WRITE(phy, value) \
    scu_link_layer_register_write(phy, phy_configuration, value)

/// This macro reads the PHY Enable Spinup register
#define SCU_SAS_ENSPINUP_READ(phy) \
    scu_link_layer_register_read(phy, notify_enable_spinup_control)

/// This macro writes the PHY Enable Spinup register
#define SCU_SAS_ENSPINUP_WRITE(phy, value) \
    scu_link_layer_register_write(phy, notify_enable_spinup_control, value)

/// This macro reads the CLKSM register
#define SCU_SAS_CLKSM_READ(phy) \
    scu_link_layer_register_read(phy, clock_skew_management)

/// This macro writes the CLKSM register
#define SCU_SAS_CLKSM_WRITE(phy, value) \
    scu_link_layer_register_write(phy, clock_skew_management, value)

/// This macro reads the PHY Capacity register
#define SCU_SAS_PHYCAP_READ(phy) \
    scu_link_layer_register_read(phy, phy_capabilities)

/// This macro writes the PHY Capacity register
#define SCU_SAS_PHYCAP_WRITE(phy, value) \
    scu_link_layer_register_write(phy, phy_capabilities, value)

/// This macro reads the Received PHY Capacity register
#define SCU_SAS_RECPHYCAP_READ(phy) \
    scu_link_layer_register_read(phy, receive_phycap)

/// This macro reads the link layer control register
#define SCU_SAS_LLCTL_READ(phy) \
    scu_link_layer_register_read(phy, link_layer_control);

/// This macro writes the link layer control register
#define SCU_SAS_LLCTL_WRITE(phy, value) \
    scu_link_layer_register_write(phy, link_layer_control, value);

/// This macro reads the link layer status register
#define SCU_SAS_LLSTA_READ(phy) \
    scu_link_layer_register_read(phy, link_layer_status);

#define SCU_SAS_ECENCR_READ(phy) \
    scu_link_layer_register_read(phy, error_counter_event_notification_control)

#define SCU_SAS_ECENCR_WRITE(phy, value) \
    scu_link_layer_register_write(phy, error_counter_event_notification_control, value)

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_SDS_PHY_REGISTERS_H_
