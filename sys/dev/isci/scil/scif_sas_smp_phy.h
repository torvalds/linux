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
#ifndef _SCIF_SAS_SMP_PHY_H_
#define _SCIF_SAS_SMP_PHY_H_

/**
 * @file
 *
 * @brief This file contains the protected interface structures, constants,
 *        and methods for the SCIF_SAS_SMP_PHY object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/scif_sas_remote_device.h>
#include <dev/isci/scil/sci_fast_list.h>

struct SCIF_SAS_CONTROLLER;
struct SCIF_SAS_SMP_PHY;
struct SCIF_SAS_REMOTE_DEVICE;

/**
 * @struct SCIF_SAS_SMP_PHY
 *
 * @brief This structure stores data for a smp phy of a smp device (expander).
 */
typedef struct SCIF_SAS_SMP_PHY
{
   /**
    * A smp phy can either connect to a end device or another smp phy,
    * This two conditions are mutual exclusive.
    */
   union{
      /**
       * The attached smp phy. This field has valid meaning when
       * attached_device_type is expander.
       */
      struct SCIF_SAS_SMP_PHY       * attached_phy;

      /**
       * The attached end device. This field has valid meaning when
       * attached_device_type is end_device.
       */
      struct SCIF_SAS_REMOTE_DEVICE * end_device;
   } u;

   /**
    * This field records the owning expander device this smp phy belongs to.
    */
   struct SCIF_SAS_REMOTE_DEVICE * owning_device;

   /**
    * The list element of this smp phy for the smp phy list of the ownig expander.
    */
   SCI_FAST_LIST_ELEMENT_T    list_element;

   /**
    * This field records the attached sas address, retrieved from a DISCOVER
    * response. Zero value is valid.
    */
   SCI_SAS_ADDRESS_T          attached_sas_address;

   /**
    * This field records the attached device type, retrieved from a DISCOVER
    * response.
    */
   U8                         attached_device_type;

   /**
    * This field records the routing attribute, retrieved from a DISCOVER
    * response.
    */
   U8                         routing_attribute;

   /**
    * This field records the phy identifier of this smp phy, retrieved from a
    * DISCOVER response.
    */
   U8                         phy_identifier;

   /**
    * this field stores the last route index for previous round of config
    * route table activity on a smp phy within one DISCOVER process.
    */
   U16                        config_route_table_index_anchor;

}SCIF_SAS_SMP_PHY_T;


void scif_sas_smp_phy_construct(
   SCIF_SAS_SMP_PHY_T            * this_smp_phy,
   struct SCIF_SAS_REMOTE_DEVICE * owning_device,
   U8                              expander_phy_id
);

void scif_sas_smp_phy_destruct(
   SCIF_SAS_SMP_PHY_T       * this_smp_phy
);

void scif_sas_smp_phy_save_information(
   SCIF_SAS_SMP_PHY_T            * this_smp_phy,
   struct SCIF_SAS_REMOTE_DEVICE * attached_device,
   SMP_RESPONSE_DISCOVER_T       * discover_response
);

SCI_STATUS scif_sas_smp_phy_set_attached_phy(
   SCIF_SAS_SMP_PHY_T            * this_smp_phy,
   U8                              attached_phy_identifier,
   struct SCIF_SAS_REMOTE_DEVICE * attached_remote_device
);

SCI_STATUS scif_sas_smp_phy_verify_routing_attribute(
   SCIF_SAS_SMP_PHY_T * this_smp_phy,
   SCIF_SAS_SMP_PHY_T * attached_smp_phy
);

SCIF_SAS_SMP_PHY_T * scif_sas_smp_phy_find_next_phy_in_wide_port(
   SCIF_SAS_SMP_PHY_T * this_smp_phy
);

SCIF_SAS_SMP_PHY_T * scif_sas_smp_phy_find_middle_phy_in_wide_port(
   SCIF_SAS_SMP_PHY_T * this_smp_phy
);

SCIF_SAS_SMP_PHY_T * scif_sas_smp_phy_find_highest_phy_in_wide_port(
   SCIF_SAS_SMP_PHY_T * this_smp_phy
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_SAS_SMP_PHY_H_
