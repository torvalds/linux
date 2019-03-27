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
#ifndef _SCIC_PHY_H_
#define _SCIC_PHY_H_

/**
 * @file
 *
 * @brief This file contains all of the interface methods that can be called
 *        by an SCIC user on a phy (SAS or SATA) object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_status.h>

#include <dev/isci/scil/intel_sata.h>
#include <dev/isci/scil/intel_sas.h>


/**
 * @struct SCIC_PHY_PROPERTIES
 * @brief This structure defines the properties common to all phys
 *        that can be retrieved.
 */
typedef struct SCIC_PHY_PROPERTIES
{
   /**
    * This field specifies the port that currently contains the
    * supplied phy.  This field may be set to SCI_INVALID_HANDLE
    * if the phy is not currently contained in a port.
    */
   SCI_PORT_HANDLE_T  owning_port;

   /**
    * This field specifies the maximum link rate for which this phy
    * will negotiate.
    */
   SCI_SAS_LINK_RATE max_link_rate;

   /**
    * This field specifies the link rate at which the phy is
    * currently operating.
    */
   SCI_SAS_LINK_RATE  negotiated_link_rate;

   /**
    * This field indicates the identify address frame that will be
    * transmitted to the connected phy.
    */
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_T transmit_iaf;

   /**
    * This field specifies the index of the phy in relation to other
    * phys within the controller.  This index is zero relative.
    */
   U8 index;

} SCIC_PHY_PROPERTIES_T;

/**
 * @struct SCIC_SAS_PHY_PROPERTIES
 * @brief This structure defines the properties, specific to a
 *        SAS phy, that can be retrieved.
 */
typedef struct SCIC_SAS_PHY_PROPERTIES
{
   /**
    * This field delineates the Identify Address Frame received
    * from the remote end point.
    */
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_T received_iaf;

   /**
    * This field delineates the Phy capabilities structure received
    * from the remote end point.
    */
   SAS_CAPABILITIES_T received_capabilities;

} SCIC_SAS_PHY_PROPERTIES_T;

/**
 * @struct SCIC_SATA_PHY_PROPERTIES
 * @brief This structure defines the properties, specific to a
 *        SATA phy, that can be retrieved.
 */
typedef struct SCIC_SATA_PHY_PROPERTIES
{
   /**
    * This field delineates the signature FIS received from the
    * attached target.
    */
   SATA_FIS_REG_D2H_T signature_fis;

   /**
    * This field specifies to the user if a port selector is connected
    * on the specified phy.
    */
   BOOL is_port_selector_present;

} SCIC_SATA_PHY_PROPERTIES_T;

/**
 * @enum  SCIC_PHY_COUNTER_ID
 * @brief This enumeration depicts the various pieces of optional
 *        information that can be retrieved for a specific phy.
 */
typedef enum SCIC_PHY_COUNTER_ID
{
   /**
    * This PHY information field tracks the number of frames received.
    */
   SCIC_PHY_COUNTER_RECEIVED_FRAME,

   /**
    * This PHY information field tracks the number of frames transmitted.
    */
   SCIC_PHY_COUNTER_TRANSMITTED_FRAME,

   /**
    * This PHY information field tracks the number of DWORDs received.
    */
   SCIC_PHY_COUNTER_RECEIVED_FRAME_DWORD,

   /**
    * This PHY information field tracks the number of DWORDs transmitted.
    */
   SCIC_PHY_COUNTER_TRANSMITTED_FRAME_DWORD,

   /**
    * This PHY information field tracks the number of times DWORD
    * synchronization was lost.
    */
   SCIC_PHY_COUNTER_LOSS_OF_SYNC_ERROR,

   /**
    * This PHY information field tracks the number of received DWORDs with
    * running disparity errors.
    */
   SCIC_PHY_COUNTER_RECEIVED_DISPARITY_ERROR,

   /**
    * This PHY information field tracks the number of received frames with a
    * CRC error (not including short or truncated frames).
    */
   SCIC_PHY_COUNTER_RECEIVED_FRAME_CRC_ERROR,

   /**
    * This PHY information field tracks the number of DONE (ACK/NAK TIMEOUT)
    * primitives received.
    */
   SCIC_PHY_COUNTER_RECEIVED_DONE_ACK_NAK_TIMEOUT,

   /**
    * This PHY information field tracks the number of DONE (ACK/NAK TIMEOUT)
    * primitives transmitted.
    */
   SCIC_PHY_COUNTER_TRANSMITTED_DONE_ACK_NAK_TIMEOUT,

   /**
    * This PHY information field tracks the number of times the inactivity
    * timer for connections on the phy has been utilized.
    */
   SCIC_PHY_COUNTER_INACTIVITY_TIMER_EXPIRED,

   /**
    * This PHY information field tracks the number of DONE (CREDIT TIMEOUT)
    * primitives received.
    */
   SCIC_PHY_COUNTER_RECEIVED_DONE_CREDIT_TIMEOUT,

   /**
    * This PHY information field tracks the number of DONE (CREDIT TIMEOUT)
    * primitives transmitted.
    */
   SCIC_PHY_COUNTER_TRANSMITTED_DONE_CREDIT_TIMEOUT,

   /**
    * This PHY information field tracks the number of CREDIT BLOCKED
    * primitives received.
    * @note Depending on remote device implementation, credit blocks
    *       may occur regularly.
    */
   SCIC_PHY_COUNTER_RECEIVED_CREDIT_BLOCKED,

   /**
    * This PHY information field contains the number of short frames
    * received.  A short frame is simply a frame smaller then what is
    * allowed by either the SAS or SATA specification.
    */
   SCIC_PHY_COUNTER_RECEIVED_SHORT_FRAME,

   /**
    * This PHY information field contains the number of frames received after
    * credit has been exhausted.
    */
   SCIC_PHY_COUNTER_RECEIVED_FRAME_WITHOUT_CREDIT,

   /**
    * This PHY information field contains the number of frames received after
    * a DONE has been received.
    */
   SCIC_PHY_COUNTER_RECEIVED_FRAME_AFTER_DONE,

   /**
    * This PHY information field contains the number of times the phy
    * failed to achieve DWORD synchronization during speed negotiation.
    */
   SCIC_PHY_COUNTER_SN_DWORD_SYNC_ERROR
} SCIC_PHY_COUNTER_ID_T;

/**
 * @brief This method will enable the user to retrieve information
 *        common to all phys, such as: the negotiated link rate,
 *        the phy id, etc.
 *
 * @param[in]  phy This parameter specifies the phy for which to retrieve
 *             the properties.
 * @param[out] properties This parameter specifies the properties
 *             structure into which to copy the requested information.
 *
 * @return Indicate if the user specified a valid phy.
 * @retval SCI_SUCCESS This value is returned if the specified phy was valid.
 * @retval SCI_FAILURE_INVALID_PHY This value is returned if the specified phy
 *         is not valid.  When this value is returned, no data is copied to the
 *         properties output parameter.
 */
SCI_STATUS scic_phy_get_properties(
   SCI_PHY_HANDLE_T        phy,
   SCIC_PHY_PROPERTIES_T * properties
);

/**
 * @brief This method will enable the user to retrieve information
 *        specific to a SAS phy, such as: the received identify
 *        address frame, received phy capabilities, etc.
 *
 * @param[in]  phy this parameter specifies the phy for which to
 *             retrieve properties.
 * @param[out] properties This parameter specifies the properties
 *             structure into which to copy the requested information.
 *
 * @return This method returns an indication as to whether the SAS
 *         phy properties were successfully retrieved.
 * @retval SCI_SUCCESS This value is returned if the SAS properties
 *         are successfully retrieved.
 * @retval SCI_FAILURE This value is returned if the SAS properties
 *         are not successfully retrieved (e.g. It's not a SAS Phy).
 */
SCI_STATUS scic_sas_phy_get_properties(
   SCI_PHY_HANDLE_T            phy,
   SCIC_SAS_PHY_PROPERTIES_T * properties
);

/**
 * @brief This method will enable the user to retrieve information
 *        specific to a SATA phy, such as: the received signature
 *        FIS, if a port selector is present, etc.
 *
 * @param[in]  phy this parameter specifies the phy for which to
 *             retrieve properties.
 * @param[out] properties This parameter specifies the properties
 *             structure into which to copy the requested information.
 *
 * @return This method returns an indication as to whether the SATA
 *         phy properties were successfully retrieved.
 * @retval SCI_SUCCESS This value is returned if the SATA properties
 *         are successfully retrieved.
 * @retval SCI_FAILURE This value is returned if the SATA properties
 *         are not successfully retrieved (e.g. It's not a SATA Phy).
 */
SCI_STATUS scic_sata_phy_get_properties(
   SCI_PHY_HANDLE_T             phy,
   SCIC_SATA_PHY_PROPERTIES_T * properties
);

/**
 * @brief This method allows the SCIC user to instruct the SCIC
 *        implementation to send the SATA port selection signal.
 *
 * @param[in]  phy this parameter specifies the phy for which to send
 *             the port selection signal.
 *
 * @return An indication of whether the port selection signal was
 *         successfully executed.
 * @retval SCI_SUCCESS This value is returned if the port selection signal
 *         was successfully transmitted.
 */
SCI_STATUS scic_sata_phy_send_port_selection_signal(
   SCI_PHY_HANDLE_T  phy
);

/**
 * @brief This method requests the SCI implementation to begin tracking
 *        information specified by the supplied parameters.
 *
 * @param[in]  phy this parameter specifies the phy for which to enable
 *             the information type.
 * @param[in]  counter_id this parameter specifies the information
 *             type to be enabled.
 *
 * @return Indicate if enablement of the information type was successful.
 * @retval SCI_SUCCESS This value is returned if the information type was
 *         successfully enabled.
 * @retval SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD This value is returned
 *         if the supplied information type is not supported.
 */
SCI_STATUS scic_phy_enable_counter(
   SCI_PHY_HANDLE_T       phy,
   SCIC_PHY_COUNTER_ID_T  counter_id
);

/**
 * @brief This method requests the SCI implementation to stop tracking
 *        information specified by the supplied parameters.
 *
 * @param[in]  phy this parameter specifies the phy for which to disable
 *             the information type.
 * @param[in]  counter_id this parameter specifies the information
 *             type to be disabled.
 *
 * @return Indicate if disablement of the information type was successful.
 * @retval SCI_SUCCESS This value is returned if the information type was
 *         successfully disabled.
 * @retval SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD This value is returned
 *         if the supplied information type is not supported.
 */
SCI_STATUS scic_phy_disable_counter(
   SCI_PHY_HANDLE_T       phy,
   SCIC_PHY_COUNTER_ID_T  counter_id
);

/**
 * @brief This method requests the SCI implementation to retrieve
 *        tracking information specified by the supplied parameters.
 *
 * @param[in]  phy this parameter specifies the phy for which to retrieve
 *             the information type.
 * @param[in]  counter_id this parameter specifies the information
 *             type to be retrieved.
 * @param[out] data this parameter is a 32-bit pointer to a location
 *             where the data being retrieved is to be placed.
 *
 * @return Indicate if retrieval of the information type was successful.
 * @retval SCI_SUCCESS This value is returned if the information type was
 *         successfully retrieved.
 * @retval SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD This value is returned
 *         if the supplied information type is not supported.
 */
SCI_STATUS scic_phy_get_counter(
   SCI_PHY_HANDLE_T        phy,
   SCIC_PHY_COUNTER_ID_T   counter_id,
   U32                   * data
);

/**
 * @brief This method requests the SCI implementation to clear (reset)
 *        tracking information specified by the supplied parameters.
 *
 * @param[in]  phy this parameter specifies the phy for which to clear
 *             the information type.
 * @param[in]  counter_id this parameter specifies the information
 *             type to be cleared.
 *
 * @return Indicate if clearing of the information type was successful.
 * @retval SCI_SUCCESS This value is returned if the information type was
 *         successfully cleared.
 * @retval SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD This value is returned
 *         if the supplied information type is not supported.
 */
SCI_STATUS scic_phy_clear_counter(
   SCI_PHY_HANDLE_T       phy,
   SCIC_PHY_COUNTER_ID_T  counter_id
);

/**
 * @brief This method will attempt to stop the phy object.
 *
 * @param[in] this_phy
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS if the phy is going to stop
 *         SCI_INVALID_STATE if the phy is not in a valid state
 *         to stop
 */
SCI_STATUS scic_phy_stop(
   SCI_PHY_HANDLE_T       phy
);

/**
 * @brief This method will attempt to start the phy object. This
 *        request is only valid when the phy is in the stopped
 *        state
 *
 * @param[in] this_phy
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_phy_start(
   SCI_PHY_HANDLE_T       phy
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_PHY_H_

