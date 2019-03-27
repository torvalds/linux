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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/**
 * @file
 *
 * @brief This file contains the implementation for the public and protected
 *        methods for the port configuration agent.
 */

#include <dev/isci/scil/scic_controller.h>
#include <dev/isci/scil/scic_sds_logger.h>
#include <dev/isci/scil/scic_sds_controller.h>
#include <dev/isci/scil/scic_sds_port_configuration_agent.h>

#define SCIC_SDS_MPC_RECONFIGURATION_TIMEOUT    (10)
#define SCIC_SDS_APC_RECONFIGURATION_TIMEOUT    (10)
#define SCIC_SDS_APC_WAIT_LINK_UP_NOTIFICATION  (250)

enum SCIC_SDS_APC_ACTIVITY
{
   SCIC_SDS_APC_SKIP_PHY,
   SCIC_SDS_APC_ADD_PHY,
   SCIC_SDS_APC_START_TIMER,

   SCIC_SDS_APC_ACTIVITY_MAX
};

//******************************************************************************
// General port configuration agent routines
//******************************************************************************

/**
 * Compare the two SAS Address and
 * if SAS Address One is greater than SAS Address Two then return > 0
 * else if SAS Address One is less than SAS Address Two return < 0
 * Otherwise they are the same return 0
 *
 * @param[in] address_one A SAS Address to be compared.
 * @param[in] address_two A SAS Address to be compared.
 *
 * @return A signed value of x > 0 > y where
 *         x is returned for Address One > Address Two
 *         y is returned for Address One < Address Two
 *         0 is returned ofr Address One = Address Two
 */
static
S32 sci_sas_address_compare(
   SCI_SAS_ADDRESS_T address_one,
   SCI_SAS_ADDRESS_T address_two
)
{
   if (address_one.high > address_two.high)
   {
      return 1;
   }
   else if (address_one.high < address_two.high)
   {
      return -1;
   }
   else if (address_one.low > address_two.low)
   {
      return 1;
   }
   else if (address_one.low < address_two.low)
   {
      return -1;
   }

   // The two SAS Address must be identical
   return 0;
}

/**
 * This routine will find a matching port for the phy.  This means that the
 * port and phy both have the same broadcast sas address and same received
 * sas address.
 *
 * @param[in] controller The controller object used for the port search.
 * @param[in] phy The phy object to match.
 *
 * @return The port address or the SCI_INVALID_HANDLE if there is no matching
 *         port.
 *
 * @retvalue port address if the port can be found to match the phy.
 * @retvalue SCI_INVALID_HANDLE if there is no matching port for the phy.
 */
static
SCIC_SDS_PORT_T * scic_sds_port_configuration_agent_find_port(
   SCIC_SDS_CONTROLLER_T * controller,
   SCIC_SDS_PHY_T        * phy
)
{
   U8 port_index;
   SCI_PORT_HANDLE_T port_handle;
   SCI_SAS_ADDRESS_T port_sas_address;
   SCI_SAS_ADDRESS_T port_attached_device_address;
   SCI_SAS_ADDRESS_T phy_sas_address;
   SCI_SAS_ADDRESS_T phy_attached_device_address;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PORT | SCIC_LOG_OBJECT_PHY,
      "scic_sds_port_confgiruation_agent_find_port(0x%08x, 0x%08x) enter\n",
      controller, phy
   ));

   // Since this phy can be a member of a wide port check to see if one or
   // more phys match the sent and received SAS address as this phy in which
   // case it should participate in the same port.
   scic_sds_phy_get_sas_address(phy, &phy_sas_address);
   scic_sds_phy_get_attached_sas_address(phy, &phy_attached_device_address);

   for (port_index = 0; port_index < SCI_MAX_PORTS; port_index++)
   {
      if (scic_controller_get_port_handle(controller, port_index, &port_handle) == SCI_SUCCESS)
      {
         SCIC_SDS_PORT_T * port = (SCIC_SDS_PORT_T *)port_handle;

         scic_sds_port_get_sas_address(port, &port_sas_address);
         scic_sds_port_get_attached_sas_address(port, &port_attached_device_address);

         if (
               (sci_sas_address_compare(port_sas_address, phy_sas_address) == 0)
            && (sci_sas_address_compare(port_attached_device_address, phy_attached_device_address) == 0)
            )
         {
            return port;
         }
      }
   }

   return SCI_INVALID_HANDLE;
}

/**
 * This routine will validate the port configuration is correct for the SCU
 * hardware.  The SCU hardware allows for port configurations as follows.
 *    LP0 -> (PE0), (PE0, PE1), (PE0, PE1, PE2, PE3)
 *    LP1 -> (PE1)
 *    LP2 -> (PE2), (PE2, PE3)
 *    LP3 -> (PE3)
 *
 * @param[in] controller This is the controller object that contains the
 *            port agent
 * @param[in] port_agent This is the port configruation agent for
 *            the controller.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS the port configuration is valid for this
 *         port configuration agent.
 * @retval SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION the port configuration
 *         is not valid for this port configuration agent.
 */
static
SCI_STATUS scic_sds_port_configuration_agent_validate_ports(
   SCIC_SDS_CONTROLLER_T               * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent
)
{
#if !defined(ARLINGTON_BUILD)
   SCI_SAS_ADDRESS_T first_address;
   SCI_SAS_ADDRESS_T second_address;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PORT,
      "scic_sds_port_configuration_agent_validate_ports(0x%08x, 0x%08x) enter\n",
      controller, port_agent
   ));

   // Sanity check the max ranges for all the phys the max index
   // is always equal to the port range index
   if (
         (port_agent->phy_valid_port_range[0].max_index != 0)
      || (port_agent->phy_valid_port_range[1].max_index != 1)
      || (port_agent->phy_valid_port_range[2].max_index != 2)
      || (port_agent->phy_valid_port_range[3].max_index != 3)
      )
   {
      return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
   }

   // This is a request to configure a single x4 port or at least attempt
   // to make all the phys into a single port
   if (
         (port_agent->phy_valid_port_range[0].min_index == 0)
      && (port_agent->phy_valid_port_range[1].min_index == 0)
      && (port_agent->phy_valid_port_range[2].min_index == 0)
      && (port_agent->phy_valid_port_range[3].min_index == 0)
      )
   {
      return SCI_SUCCESS;
   }

   // This is a degenerate case where phy 1 and phy 2 are assigned
   // to the same port this is explicitly disallowed by the hardware
   // unless they are part of the same x4 port and this condition was
   // already checked above.
   if (port_agent->phy_valid_port_range[2].min_index == 1)
   {
      return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
   }

   // PE0 and PE3 can never have the same SAS Address unless they
   // are part of the same x4 wide port and we have already checked
   // for this condition.
   scic_sds_phy_get_sas_address(&controller->phy_table[0], &first_address);
   scic_sds_phy_get_sas_address(&controller->phy_table[3], &second_address);

   if (sci_sas_address_compare(first_address, second_address) == 0)
   {
      return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
   }

   // PE0 and PE1 are configured into a 2x1 ports make sure that the
   // SAS Address for PE0 and PE2 are different since they can not be
   // part of the same port.
   if (
         (port_agent->phy_valid_port_range[0].min_index == 0)
      && (port_agent->phy_valid_port_range[1].min_index == 1)
      )
   {
      scic_sds_phy_get_sas_address(&controller->phy_table[0], &first_address);
      scic_sds_phy_get_sas_address(&controller->phy_table[2], &second_address);

      if (sci_sas_address_compare(first_address, second_address) == 0)
      {
         return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
      }
   }

   // PE2 and PE3 are configured into a 2x1 ports make sure that the
   // SAS Address for PE1 and PE3 are different since they can not be
   // part of the same port.
   if (
         (port_agent->phy_valid_port_range[2].min_index == 2)
      && (port_agent->phy_valid_port_range[3].min_index == 3)
      )
   {
      scic_sds_phy_get_sas_address(&controller->phy_table[1], &first_address);
      scic_sds_phy_get_sas_address(&controller->phy_table[3], &second_address);

      if (sci_sas_address_compare(first_address, second_address) == 0)
      {
         return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
      }
   }
#endif // !defined(ARLINGTON_BUILD)

   return SCI_SUCCESS;
}

//******************************************************************************
// Manual port configuration agent routines
//******************************************************************************

/**
 * This routine will verify that all of the phys in the same port are using
 * the same SAS address.
 *
 * @param[in] controller This is the controller that contains the PHYs to
 *            be verified.
 */
static
SCI_STATUS scic_sds_mpc_agent_validate_phy_configuration(
   SCIC_SDS_CONTROLLER_T               * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent
)
{
   U32 phy_mask;
   U32 assigned_phy_mask;
   SCI_SAS_ADDRESS_T sas_address;
   SCI_SAS_ADDRESS_T phy_assigned_address;
   U8 port_index;
   U8 phy_index;

   assigned_phy_mask = 0;
   sas_address.high = 0;
   sas_address.low = 0;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PORT,
      "scic_sds_mpc_agent_validate_phy_configuration(0x%08x, 0x%08x) enter\n",
      controller, port_agent
   ));

   for (port_index = 0; port_index < SCI_MAX_PORTS; port_index++)
   {
      phy_mask = controller->oem_parameters.sds1.ports[port_index].phy_mask;

      if (phy_mask != 0)
      {
         // Make sure that one or more of the phys were not already assinged to
         // a different port.
         if ((phy_mask & ~assigned_phy_mask) == 0)
         {
            return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
         }

         // Find the starting phy index for this round through the loop
         for (phy_index = 0; phy_index < SCI_MAX_PHYS; phy_index++)
         {
            if ((1 << phy_index) & phy_mask)
            {
               scic_sds_phy_get_sas_address(
                  &controller->phy_table[phy_index], &sas_address
               );

               // The phy_index can be used as the starting point for the
               // port range since the hardware starts all logical ports
               // the same as the PE index.
               port_agent->phy_valid_port_range[phy_index].min_index = port_index;
               port_agent->phy_valid_port_range[phy_index].max_index = phy_index;

               if (phy_index != port_index)
               {
                  return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
               }

               break;
            }
         }

         // See how many additional phys are being added to this logical port.
         // Note: We have not moved the current phy_index so we will actually
         //       compare the startting phy with itself.
         //       This is expected and required to add the phy to the port.
         while (phy_index < SCI_MAX_PHYS)
         {
            if ((1 << phy_index) & phy_mask)
            {
               scic_sds_phy_get_sas_address(
                  &controller->phy_table[phy_index], &phy_assigned_address
               );

               if (sci_sas_address_compare(sas_address, phy_assigned_address) != 0)
               {
                  // The phy mask specified that this phy is part of the same port
                  // as the starting phy and it is not so fail this configuration
                  return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
               }

               port_agent->phy_valid_port_range[phy_index].min_index = port_index;
               port_agent->phy_valid_port_range[phy_index].max_index = phy_index;

               scic_sds_port_add_phy(
                  &controller->port_table[port_index],
                  &controller->phy_table[phy_index]
               );

               assigned_phy_mask |= (1 << phy_index);
            }

            phy_index++;
         }
      }
   }

   return scic_sds_port_configuration_agent_validate_ports(controller, port_agent);
}

/**
 * This timer routine is used to allow the SCI User to rediscover or change
 * device objects before a new series of link up notifications because a
 * link down has allowed a better port configuration.
 *
 * @param[in] controller This is the core controller object which is used
 *            to obtain the port configuration agent.
 */
static
void scic_sds_mpc_agent_timeout_handler(
   void * object
)
{
   U8 index;
   SCIC_SDS_CONTROLLER_T * controller = (SCIC_SDS_CONTROLLER_T *)object;
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent = &controller->port_agent;
   U16 configure_phy_mask;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PORT,
      "scic_sds_mpc_agent_timeout_handler(0x%08x) enter\n",
      controller
   ));

   port_agent->timer_pending = FALSE;

   // Find the mask of phys that are reported read but as yet unconfigured into a port
   configure_phy_mask = ~port_agent->phy_configured_mask & port_agent->phy_ready_mask;

   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      if (configure_phy_mask & (1 << index))
      {
         port_agent->link_up_handler(
                        controller,
                        port_agent,
                        scic_sds_phy_get_port(&controller->phy_table[index]),
                        &controller->phy_table[index]
                     );
      }
   }
}

/**
 * This method handles the manual port configuration link up notifications.
 * Since all ports and phys are associate at initialization time we just turn
 * around and notifiy the port object that there is a link up.  If this PHY is
 * not associated with a port there is no action taken.
 *
 * @param[in] controller This is the controller object that receives the
 *            link up notification.
 * @param[in] port This is the port object associated with the phy.  If the
 *            is no associated port this is an SCI_INVALID_HANDLE.
 * @param[in] phy This is the phy object which has gone ready.
 *
 * @note Is it possible to get a link up notification from a phy that has
 *       no assocoated port?
 */
static
void scic_sds_mpc_agent_link_up(
   SCIC_SDS_CONTROLLER_T               * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent,
   SCIC_SDS_PORT_T                     * port,
   SCIC_SDS_PHY_T                      * phy
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PORT | SCIC_LOG_OBJECT_PHY,
      "scic_sds_mpc_agent_link_up(0x%08x, 0x%08x, 0x%08x, 0x%08x) enter\n",
      controller, port_agent, port, phy
   ));

   // If the port has an invalid handle then the phy was not assigned to
   // a port.  This is because the phy was not given the same SAS Address
   // as the other PHYs in the port.
   if (port != SCI_INVALID_HANDLE)
   {
      port_agent->phy_ready_mask |= (1 << scic_sds_phy_get_index(phy));

      scic_sds_port_link_up(port, phy);

      if ((port->active_phy_mask & (1 << scic_sds_phy_get_index(phy))) != 0)
      {
         port_agent->phy_configured_mask |= (1 << scic_sds_phy_get_index(phy));
      }
   }
}

/**
 * This method handles the manual port configuration link down notifications.
 * Since all ports and phys are associated at initialization time we just turn
 * around and notifiy the port object of the link down event.  If this PHY is
 * not associated with a port there is no action taken.
 *
 * @param[in] controller This is the controller object that receives the
 *            link down notification.
 * @param[in] port This is the port object associated with the phy.  If the
 *            is no associated port this is an SCI_INVALID_HANDLE.  The port
 *            is an invalid handle only if the phy was never port of this
 *            port.  This happens when the phy is not broadcasting the same
 *            SAS address as the other phys in the assigned port.
 * @param[in] phy This is the phy object which has gone link down.
 *
 * @note Is it possible to get a link down notification from a phy that has
 *       no assocoated port?
 */
static
void scic_sds_mpc_agent_link_down(
   SCIC_SDS_CONTROLLER_T               * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent,
   SCIC_SDS_PORT_T                     * port,
   SCIC_SDS_PHY_T                      * phy
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PORT | SCIC_LOG_OBJECT_PHY,
      "scic_sds_mpc_agent_link_down(0x%08x, 0x%08x, 0x%08x, 0x%08x) enter\n",
      controller, port_agent, port, phy
   ));

   if (port != SCI_INVALID_HANDLE)
   {
      // If we can form a new port from the remainder of the phys then we want
      // to start the timer to allow the SCI User to cleanup old devices and
      // rediscover the port before rebuilding the port with the phys that
      // remain in the ready state.
      port_agent->phy_ready_mask &= ~(1 << scic_sds_phy_get_index(phy));
      port_agent->phy_configured_mask &= ~(1 << scic_sds_phy_get_index(phy));

      // Check to see if there are more phys waiting to be configured into a port.
      // If there are allow the SCI User to tear down this port, if necessary, and
      // then reconstruc the port after the timeout.
      if (
            (port_agent->phy_configured_mask == 0x0000)
         && (port_agent->phy_ready_mask != 0x0000)
         && !port_agent->timer_pending
         )
      {
         port_agent->timer_pending = TRUE;

         scic_cb_timer_start(
            controller,
            port_agent->timer,
            SCIC_SDS_MPC_RECONFIGURATION_TIMEOUT
         );
      }

      scic_sds_port_link_down(port, phy);
   }
}

//******************************************************************************
// Automatic port configuration agent routines
//******************************************************************************

/**
 * This routine will verify that the phys are assigned a valid SAS address for
 * automatic port configuration mode.
 */
static
SCI_STATUS scic_sds_apc_agent_validate_phy_configuration(
   SCIC_SDS_CONTROLLER_T               * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent
)
{
   U8 phy_index;
   U8 port_index;
   SCI_SAS_ADDRESS_T sas_address;
   SCI_SAS_ADDRESS_T phy_assigned_address;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PORT,
      "scic_sds_apc_agent_validate_phy_configuration(0x%08x, 0x%08x) enter\n",
      controller, port_agent
   ));

   phy_index = 0;

   while (phy_index < SCI_MAX_PHYS)
   {
      port_index = phy_index;

      // Get the assigned SAS Address for the first PHY on the controller.
      scic_sds_phy_get_sas_address(
         &controller->phy_table[phy_index], &sas_address
      );

      while (++phy_index < SCI_MAX_PHYS)
      {
         scic_sds_phy_get_sas_address(
            &controller->phy_table[phy_index], &phy_assigned_address
         );

         // Verify each of the SAS address are all the same for every PHY
         if (sci_sas_address_compare(sas_address, phy_assigned_address) == 0)
         {
            port_agent->phy_valid_port_range[phy_index].min_index = port_index;
            port_agent->phy_valid_port_range[phy_index].max_index = phy_index;
         }
         else
         {
            port_agent->phy_valid_port_range[phy_index].min_index = phy_index;
            port_agent->phy_valid_port_range[phy_index].max_index = phy_index;
            break;
         }
      }
   }

   return scic_sds_port_configuration_agent_validate_ports(controller, port_agent);
}

/**
 * This routine will restart the automatic port configuration timeout
 * timer for the next time period.  This could be caused by either a
 * link down event or a link up event where we can not yet tell to which
 * port a phy belongs.
 *
 * @param[in] controller This is the controller that to which the port
 *            agent is assigned.
 * @param[in] port_agent This is the port agent that is requesting the
 *            timer start operation.
 * @param[in] phy This is the phy that has caused the timer operation to
 *            be scheduled.
 * @param[in] timeout This is the timeout in ms.
 */
static
void scic_sds_apc_agent_start_timer(
   SCIC_SDS_CONTROLLER_T               * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent,
   SCIC_SDS_PHY_T                      * phy,
   U32                                   timeout
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PORT | SCIC_LOG_OBJECT_PHY,
      "scic_sds_apc_agent_start_timer(0x%08x, 0x%08x, 0x%08x, 0x%08x) enter\n",
      controller, port_agent, phy, timeout
   ));

   if (port_agent->timer_pending)
   {
      scic_cb_timer_stop(controller, port_agent->timer);
   }

   port_agent->timer_pending = TRUE;

   scic_cb_timer_start(controller, port_agent->timer, timeout);
}

/**
 * This method handles the automatic port configuration for link up notifications.
 *
 * @param[in] controller This is the controller object that receives the
 *            link up notification.
 * @param[in] phy This is the phy object which has gone link up.
 * @param[in] start_timer This tells the routine if it should start the timer for
 *            any phys that might be added to a port in the future.
 */
static
void scic_sds_apc_agent_configure_ports(
   SCIC_SDS_CONTROLLER_T               * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent,
   SCIC_SDS_PHY_T                      * phy,
   BOOL                                  start_timer
)
{
   U8 port_index;
   SCI_STATUS status;
   SCIC_SDS_PORT_T * port;
   SCI_PORT_HANDLE_T port_handle;
   enum SCIC_SDS_APC_ACTIVITY apc_activity = SCIC_SDS_APC_SKIP_PHY;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PORT | SCIC_LOG_OBJECT_PHY,
      "scic_sds_apc_agent_configure_ports(0x%08x, 0x%08x, 0x%08x, %d) enter\n",
      controller, port_agent, phy, start_timer
   ));

   port = scic_sds_port_configuration_agent_find_port(controller, phy);

   if (port != SCI_INVALID_HANDLE)
   {
      if (scic_sds_port_is_valid_phy_assignment(port, phy->phy_index))
         apc_activity = SCIC_SDS_APC_ADD_PHY;
      else
         apc_activity = SCIC_SDS_APC_SKIP_PHY;
   }
   else
   {
      // There is no matching Port for this PHY so lets search through the
      // Ports and see if we can add the PHY to its own port or maybe start
      // the timer and wait to see if a wider port can be made.
      //
      // Note the break when we reach the condition of the port id == phy id
      for (
             port_index = port_agent->phy_valid_port_range[phy->phy_index].min_index;
             port_index <= port_agent->phy_valid_port_range[phy->phy_index].max_index;
             port_index++
          )
      {
         scic_controller_get_port_handle(controller, port_index, &port_handle);

         port = (SCIC_SDS_PORT_T *)port_handle;

         // First we must make sure that this PHY can be added to this Port.
         if (scic_sds_port_is_valid_phy_assignment(port, phy->phy_index))
         {
            // Port contains a PHY with a greater PHY ID than the current
            // PHY that has gone link up.  This phy can not be part of any
            // port so skip it and move on.
            if (port->active_phy_mask > (1 << phy->phy_index))
            {
               apc_activity = SCIC_SDS_APC_SKIP_PHY;
               break;
            }

            // We have reached the end of our Port list and have not found
            // any reason why we should not either add the PHY to the port
            // or wait for more phys to become active.
            if (port->physical_port_index == phy->phy_index)
            {
               // The Port either has no active PHYs.
               // Consider that if the port had any active PHYs we would have
               // or active PHYs with
               // a lower PHY Id than this PHY.
               if (apc_activity != SCIC_SDS_APC_START_TIMER)
               {
                  apc_activity = SCIC_SDS_APC_ADD_PHY;
               }

               break;
            }

            // The current Port has no active PHYs and this PHY could be part
            // of this Port.  Since we dont know as yet setup to start the
            // timer and see if there is a better configuration.
            if (port->active_phy_mask == 0)
            {
               apc_activity = SCIC_SDS_APC_START_TIMER;
            }
         }
         else if (port->active_phy_mask != 0)
         {
            // The Port has an active phy and the current Phy can not
            // participate in this port so skip the PHY and see if
            // there is a better configuration.
            apc_activity = SCIC_SDS_APC_SKIP_PHY;
         }
      }
   }

   // Check to see if the start timer operations should instead map to an
   // add phy operation.  This is caused because we have been waiting to
   // add a phy to a port but could not because the automatic port
   // configuration engine had a choice of possible ports for the phy.
   // Since we have gone through a timeout we are going to restrict the
   // choice to the smallest possible port.
   if (
         (start_timer == FALSE)
      && (apc_activity == SCIC_SDS_APC_START_TIMER)
      )
   {
      apc_activity = SCIC_SDS_APC_ADD_PHY;
   }

   switch (apc_activity)
   {
   case SCIC_SDS_APC_ADD_PHY:
      status = scic_sds_port_add_phy(port, phy);

      if (status == SCI_SUCCESS)
      {
         port_agent->phy_configured_mask |= (1 << phy->phy_index);
      }
      break;

   case SCIC_SDS_APC_START_TIMER:
      scic_sds_apc_agent_start_timer(
         controller, port_agent, phy, SCIC_SDS_APC_WAIT_LINK_UP_NOTIFICATION
      );
      break;

   case SCIC_SDS_APC_SKIP_PHY:
   default:
      // do nothing the PHY can not be made part of a port at this time.
      break;
   }
}

/**
 * This method handles the automatic port configuration for link up notifications.
 *
 * @param[in] controller This is the controller object that receives the
 *            link up notification.
 * @param[in] port This is the port object associated with the phy.  If the
 *            is no associated port this is an SCI_INVALID_HANDLE.
 * @param[in] phy This is the phy object which has gone link up.
 *
 * @note Is it possible to get a link down notification from a phy that has
 *       no assocoated port?
 */
static
void scic_sds_apc_agent_link_up(
   SCIC_SDS_CONTROLLER_T               * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent,
   SCIC_SDS_PORT_T                     * port,
   SCIC_SDS_PHY_T                      * phy
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PORT | SCIC_LOG_OBJECT_PHY,
      "scic_sds_apc_agent_link_up(0x%08x, 0x%08x, 0x%08x, 0x%08x) enter\n",
      controller, port_agent, port, phy
   ));

   //the phy is not the part of this port, configure the port with this phy
   if (port == SCI_INVALID_HANDLE)
   {
      port_agent->phy_ready_mask |= (1 << scic_sds_phy_get_index(phy));

      scic_sds_apc_agent_start_timer(
         controller, port_agent, phy, SCIC_SDS_APC_WAIT_LINK_UP_NOTIFICATION
      );
   }
   else
   {
      //the phy is already the part of the port

      //if the PORT'S state is resetting then the link up is from port hard reset
      //in this case, we need to tell the port that link up is received
      if (  SCI_BASE_PORT_STATE_RESETTING
            == port->parent.state_machine.current_state_id
         )
      {
         //notify the port that port needs to be ready
         port_agent->phy_ready_mask |= (1 << scic_sds_phy_get_index(phy));
         scic_sds_port_link_up(port, phy);
      }
      else
      {
         ASSERT (0);
      }
   }
}

/**
 * This method handles the automatic port configuration link down notifications.
 * If this PHY is * not associated with a port there is no action taken.
 *
 * @param[in] controller This is the controller object that receives the
 *            link down notification.
 * @param[in] port This is the port object associated with the phy.  If the
 *            is no associated port this is an SCI_INVALID_HANDLE.
 * @param[in] phy This is the phy object which has gone link down.
 *
 * @note Is it possible to get a link down notification from a phy that has
 *       no assocoated port?
 */
static
void scic_sds_apc_agent_link_down(
   SCIC_SDS_CONTROLLER_T               * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent,
   SCIC_SDS_PORT_T                     * port,
   SCIC_SDS_PHY_T                      * phy
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PORT | SCIC_LOG_OBJECT_PHY,
      "scic_sds_apc_agent_link_down(0x%08x, 0x%08x, 0x%08x, 0x%08x) enter\n",
      controller, port_agent, port, phy
   ));

   port_agent->phy_ready_mask &= ~(1 << scic_sds_phy_get_index(phy));

   if (port != SCI_INVALID_HANDLE)
   {
      if (port_agent->phy_configured_mask & (1 << phy->phy_index))
      {
         SCI_STATUS status;

         status = scic_sds_port_remove_phy(port, phy);

         if (status == SCI_SUCCESS)
         {
            port_agent->phy_configured_mask &= ~(1 << phy->phy_index);
         }
      }
   }
}

/**
 * This routine will try to configure the phys into ports when the timer fires.
 *
 * @param[in] object This is actually the controller that needs to have the
 *            pending phys configured.
 */
static
void scic_sds_apc_agent_timeout_handler(
   void * object
)
{
   U32 index;
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent;
   SCIC_SDS_CONTROLLER_T * controller = (SCIC_SDS_CONTROLLER_T *)object;
   U16 configure_phy_mask;

   port_agent = scic_sds_controller_get_port_configuration_agent(controller);

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PORT,
      "scic_sds_apc_agent_timeout_handler(0x%08x) enter\n",
      controller
   ));

   port_agent->timer_pending = FALSE;

   configure_phy_mask = ~port_agent->phy_configured_mask & port_agent->phy_ready_mask;

   if (configure_phy_mask != 0x00)
   {
      for (index = 0; index < SCI_MAX_PHYS; index++)
      {
         if (configure_phy_mask & (1 << index))
         {
            scic_sds_apc_agent_configure_ports(
               controller, port_agent, &controller->phy_table[index], FALSE
            );
         }
      }

      //Notify the controller ports are configured.
      if (
            (port_agent->phy_ready_mask == port_agent->phy_configured_mask) &&
            (controller->next_phy_to_start == SCI_MAX_PHYS) &&
            (controller->phy_startup_timer_pending == FALSE)
         )
      {
         // The controller has successfully finished the start process.
         // Inform the SCI Core user and transition to the READY state.
         if (scic_sds_controller_is_start_complete(controller) == TRUE)
         {
            scic_sds_controller_port_agent_configured_ports(controller);
         }
      }
   }
}

//******************************************************************************
// Public port configuration agent routines
//******************************************************************************

/**
 * This method will construct the port configuration agent for operation.
 * This call is universal for both manual port configuration and automatic
 * port configuration modes.
 *
 * @param[in] port_agent This is the port configuration agent for this
 *            controller object.
 */
void scic_sds_port_configuration_agent_construct(
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent
)
{
   U32 index;

   port_agent->phy_configured_mask = 0x00;
   port_agent->phy_ready_mask = 0x00;

   port_agent->link_up_handler = NULL;
   port_agent->link_down_handler = NULL;

   port_agent->timer_pending = FALSE;
   port_agent->timer = NULL;

   for (index = 0; index < SCI_MAX_PORTS; index++)
   {
      port_agent->phy_valid_port_range[index].min_index = 0;
      port_agent->phy_valid_port_range[index].max_index = 0;
   }
}

/**
 * This method will construct the port configuration agent for this controller.
 *
 * @param[in] controller This is the controller object for which the port
 *            agent is being initialized.
 *
 * @param[in] port_agent This is the port configuration agent that is being
 *            initialized.  The initialization path is handled differntly
 *            for the automatic port configuration agent and the manual port
 *            configuration agent.
 *
 * @return
 */
SCI_STATUS scic_sds_port_configuration_agent_initialize(
   SCIC_SDS_CONTROLLER_T               * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent
)
{
   SCI_STATUS status = SCI_SUCCESS;
   enum SCIC_PORT_CONFIGURATION_MODE mode;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PORT,
      "scic_sds_port_configuration_agent_initialize(0x%08x, 0x%08x) enter\n",
      controller, port_agent
   ));

   mode = controller->oem_parameters.sds1.controller.mode_type;

   if (mode == SCIC_PORT_MANUAL_CONFIGURATION_MODE)
   {
      status = scic_sds_mpc_agent_validate_phy_configuration(controller, port_agent);

      port_agent->link_up_handler = scic_sds_mpc_agent_link_up;
      port_agent->link_down_handler = scic_sds_mpc_agent_link_down;

      port_agent->timer = scic_cb_timer_create(
                              controller,
                              scic_sds_mpc_agent_timeout_handler,
                              controller
                          );
   }
   else
   {
      status = scic_sds_apc_agent_validate_phy_configuration(controller, port_agent);

      port_agent->link_up_handler = scic_sds_apc_agent_link_up;
      port_agent->link_down_handler = scic_sds_apc_agent_link_down;

      port_agent->timer = scic_cb_timer_create(
                              controller,
                              scic_sds_apc_agent_timeout_handler,
                              controller
                          );
   }

   // Make sure we have actually gotten a timer
   if (status == SCI_SUCCESS && port_agent->timer == NULL)
   {
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "Controller 0x%x automatic port configuration agent could not get timer.\n",
         controller
     ));

     status = SCI_FAILURE;
   }

   return status;
}

/**
 * This method will destroy the port configuration agent for this controller.
 *
 * @param[in] controller This is the controller object for which the port
 *            agent is being destroyed.
 *
 * @param[in] port_agent This is the port configuration agent that is being
 *            destroyed.
 *
 * @return
 */
void scic_sds_port_configuration_agent_destroy(
   SCIC_SDS_CONTROLLER_T               * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent
)
{
   if (port_agent->timer_pending == TRUE)
   {
      scic_cb_timer_stop(controller, port_agent->timer);
   }

   scic_cb_timer_destroy(controller, port_agent->timer);

   port_agent->timer_pending = FALSE;
   port_agent->timer = NULL;
}


/**
 * @brief This method release resources in for a scic port configuration agent.
 *
 * @param[in] controller This parameter specifies the core controller, one of
 *            its phy's resources are to be released.
 * @param[in] this_phy This parameter specifies the phy whose resource is to
 *            be released.
 */
void scic_sds_port_configuration_agent_release_resource(
   SCIC_SDS_CONTROLLER_T               * controller,
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T * port_agent
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_PORT,
      "scic_sds_port_configuration_agent_release_resource(0x%x, 0x%x)\n",
      controller, port_agent
   ));

   //Currently, the only resource to be released is a timer.
   if (port_agent->timer != NULL)
   {
      scic_cb_timer_destroy(controller, port_agent->timer);
      port_agent->timer = NULL;
   }
}
