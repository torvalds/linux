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
#ifndef _SCIC_SDS_PORT_CONFIGURATION_AGENT_H_
#define _SCIC_SDS_PORT_CONFIGURATION_AGENT_H_

/**
 * @file
 *
 * @brief This file contains the structures, constants and prototypes used for
 *        the core controller automatic port configuration engine.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/scic_sds_port.h>
#include <dev/isci/scil/scic_sds_phy.h>

struct SCIC_SDS_CONTROLLER;
struct SCIC_SDS_PORT_CONFIGURATION_AGENT;
struct SCIC_SDS_PORT;
struct SCIC_SDS_PHY;

typedef void (*SCIC_SDS_PORT_CONFIGURATION_AGENT_PHY_HANDLER_T)(
   struct SCIC_SDS_CONTROLLER *,
   struct SCIC_SDS_PORT_CONFIGURATION_AGENT *,
   struct SCIC_SDS_PORT *,
   struct SCIC_SDS_PHY  *
);

struct SCIC_SDS_PORT_RANGE
{
   U8 min_index;
   U8 max_index;
};

typedef struct SCIC_SDS_PORT_CONFIGURATION_AGENT
{
   U16 phy_configured_mask;
   U16 phy_ready_mask;

   struct SCIC_SDS_PORT_RANGE phy_valid_port_range[SCI_MAX_PHYS];

   BOOL timer_pending;

   SCIC_SDS_PORT_CONFIGURATION_AGENT_PHY_HANDLER_T link_up_handler;
   SCIC_SDS_PORT_CONFIGURATION_AGENT_PHY_HANDLER_T link_down_handler;

   void *timer;

} SCIC_SDS_PORT_CONFIGURATION_AGENT_T;

void scic_sds_port_configuration_agent_construct(
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent
);

SCI_STATUS scic_sds_port_configuration_agent_initialize(
   struct SCIC_SDS_CONTROLLER          * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent
);

void scic_sds_port_configuration_agent_destroy(
   struct SCIC_SDS_CONTROLLER          * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent
);

void scic_sds_port_configuration_agent_release_resource(
   struct SCIC_SDS_CONTROLLER          * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_SDS_PORT_CONFIGURATION_AGENT_H_
