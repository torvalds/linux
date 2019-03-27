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
 *        methods for the SCIC_SDS_PORT object.
 */

#include <dev/isci/scil/scic_phy.h>
#include <dev/isci/scil/scic_port.h>
#include <dev/isci/scil/scic_controller.h>
#include <dev/isci/scil/scic_user_callback.h>

#include <dev/isci/scil/scic_sds_controller.h>
#include <dev/isci/scil/scic_sds_port.h>
#include <dev/isci/scil/scic_sds_phy.h>
#include <dev/isci/scil/scic_sds_remote_device.h>
#include <dev/isci/scil/scic_sds_request.h>
#include <dev/isci/scil/scic_sds_port_registers.h>
#include <dev/isci/scil/scic_sds_logger.h>
#include <dev/isci/scil/scic_sds_phy_registers.h>

#include <dev/isci/scil/intel_sas.h>
#include <dev/isci/scil/scic_sds_remote_node_context.h>
#include <dev/isci/scil/sci_util.h>

#define SCIC_SDS_PORT_MIN_TIMER_COUNT  (SCI_MAX_PORTS)
#define SCIC_SDS_PORT_MAX_TIMER_COUNT  (SCI_MAX_PORTS)

#define SCIC_SDS_PORT_HARD_RESET_TIMEOUT  (1000)
#define SCU_DUMMY_INDEX    (0xFFFF)

/**
 * This method will return a TRUE value if the specified phy can be assigned
 * to this port
 *
 * The following is a list of phys for each port that are allowed:
 * - Port 0 - 3 2 1 0
 * - Port 1 -     1
 * - Port 2 - 3 2
 * - Port 3 - 3
 *
 * This method doesn't preclude all configurations.  It merely ensures
 * that a phy is part of the allowable set of phy identifiers for
 * that port.  For example, one could assign phy 3 to port 0 and no other
 * phys.  Please refer to scic_sds_port_is_phy_mask_valid() for
 * information regarding whether the phy_mask for a port can be supported.
 *
 * @param[in] this_port This is the port object to which the phy is being
 *       assigned.
 * @param[in] phy_index This is the phy index that is being assigned to the
 *       port.
 *
 * @return BOOL
 * @retval TRUE if this is a valid phy assignment for the port
 * @retval FALSE if this is not a valid phy assignment for the port
 */
BOOL scic_sds_port_is_valid_phy_assignment(
   SCIC_SDS_PORT_T *this_port,
   U32              phy_index
)
{
   // Initialize to invalid value.
   U32  existing_phy_index = SCI_MAX_PHYS;
   U32  index;

   if ((this_port->physical_port_index == 1) && (phy_index != 1))
   {
      return FALSE;
   }

   if (this_port->physical_port_index == 3 && phy_index != 3)
   {
      return FALSE;
   }

   if (
          (this_port->physical_port_index == 2)
       && ((phy_index == 0) || (phy_index == 1))
      )
   {
      return FALSE;
   }

   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      if (  (this_port->phy_table[index] != NULL)
         && (index != phy_index) )
      {
         existing_phy_index = index;
      }
   }

   // Ensure that all of the phys in the port are capable of
   // operating at the same maximum link rate.
   if (
         (existing_phy_index < SCI_MAX_PHYS)
      && (this_port->owning_controller->user_parameters.sds1.phys[
             phy_index].max_speed_generation !=
          this_port->owning_controller->user_parameters.sds1.phys[
             existing_phy_index].max_speed_generation)
      )
      return FALSE;

   return TRUE;
}

/**
 * @brief This method requests a list (mask) of the phys contained in the
 *        supplied SAS port.
 *
 * @param[in]  this_port a handle corresponding to the SAS port for which
 *             to return the phy mask.
 *
 * @return Return a bit mask indicating which phys are a part of this port.
 *         Each bit corresponds to a phy identifier (e.g. bit 0 = phy id 0).
 */
U32 scic_sds_port_get_phys(
   SCIC_SDS_PORT_T * this_port
)
{
   U32 index;
   U32 mask;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "scic_sds_port_get_phys(0x%x) enter\n",
      this_port
   ));

   mask = 0;

   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      if (this_port->phy_table[index] != NULL)
      {
         mask |= (1 << index);
      }
   }

   return mask;
}

/**
 * This method will return a TRUE value if the port's phy mask can be
 * supported by the SCU.
 *
 * The following is a list of valid PHY mask configurations for each
 * port:
 * - Port 0 - [[3  2] 1] 0
 * - Port 1 -        [1]
 * - Port 2 - [[3] 2]
 * - Port 3 -  [3]
 *
 * @param[in] this_port This is the port object for which to determine
 *       if the phy mask can be supported.
 *
 * @return This method returns a boolean indication specifying if the
 *         phy mask can be supported.
 * @retval TRUE if this is a valid phy assignment for the port
 * @retval FALSE if this is not a valid phy assignment for the port
 */
BOOL scic_sds_port_is_phy_mask_valid(
   SCIC_SDS_PORT_T *this_port,
   U32              phy_mask
)
{
   if (this_port->physical_port_index == 0)
   {
      if (  ((phy_mask & 0x0F) == 0x0F)
         || ((phy_mask & 0x03) == 0x03)
         || ((phy_mask & 0x01) == 0x01)
         || (phy_mask == 0) )
         return TRUE;
   }
   else if (this_port->physical_port_index == 1)
   {
      if (  ((phy_mask & 0x02) == 0x02)
         || (phy_mask == 0) )
         return TRUE;
   }
   else if (this_port->physical_port_index == 2)
   {
      if (  ((phy_mask & 0x0C) == 0x0C)
         || ((phy_mask & 0x04) == 0x04)
         || (phy_mask == 0) )
         return TRUE;
   }
   else if (this_port->physical_port_index == 3)
   {
      if (  ((phy_mask & 0x08) == 0x08)
         || (phy_mask == 0) )
         return TRUE;
   }

   return FALSE;
}

/**
 * This method retrieves a currently active (i.e. connected) phy
 * contained in the port.  Currently, the lowest order phy that is
 * connected is returned.
 *
 * @param[in] this_port This parameter specifies the port from which
 *            to return a connected phy.
 *
 * @return This method returns a pointer to a SCIS_SDS_PHY object.
 * @retval NULL This value is returned if there are no currently
 *         active (i.e. connected to a remote end point) phys
 *         contained in the port.
 * @retval All other values specify a SCIC_SDS_PHY object that is
 *         active in the port.
 */
SCIC_SDS_PHY_T * scic_sds_port_get_a_connected_phy(
   SCIC_SDS_PORT_T *this_port
)
{
   U32             index;
   SCIC_SDS_PHY_T *phy;

   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      // Ensure that the phy is both part of the port and currently
      // connected to the remote end-point.
      phy = this_port->phy_table[index];
      if (
            (phy != NULL)
         && scic_sds_port_active_phy(this_port, phy)
         )
      {
         return phy;
      }
   }

   return NULL;
}

/**
 * This method attempts to make the assignment of the phy to the port.
 * If successful the phy is assigned to the ports phy table.
 *
 * @param[in, out] port The port object to which the phy assignement
 *                 is being made.
 * @param[in, out] phy The phy which is being assigned to the port.
 *
 * @return BOOL
 * @retval TRUE if the phy assignment can be made.
 * @retval FALSE if the phy assignement can not be made.
 *
 * @note This is a functional test that only fails if the phy is currently
 *       assigned to a different port.
 */
SCI_STATUS scic_sds_port_set_phy(
   SCIC_SDS_PORT_T *port,
   SCIC_SDS_PHY_T  *phy
)
{
   // Check to see if we can add this phy to a port
   // that means that the phy is not part of a port and that the port does
   // not already have a phy assinged to the phy index.
   if (
         (port->phy_table[phy->phy_index] == SCI_INVALID_HANDLE)
      && (scic_sds_phy_get_port(phy) == SCI_INVALID_HANDLE)
      && scic_sds_port_is_valid_phy_assignment(port, phy->phy_index)
      )
   {
      // Phy is being added in the stopped state so we are in MPC mode
      // make logical port index = physical port index
      port->logical_port_index = port->physical_port_index;
      port->phy_table[phy->phy_index] = phy;
      scic_sds_phy_set_port(phy, port);

      return SCI_SUCCESS;
   }

   return SCI_FAILURE;
}

/**
 * This method will clear the phy assigned to this port.  This method fails
 * if this phy is not currently assinged to this port.
 *
 * @param[in, out] port The port from which the phy is being cleared.
 * @param[in, out] phy The phy being cleared from the port.
 *
 * @return BOOL
 * @retval TRUE if the phy is removed from the port.
 * @retval FALSE if this phy is not assined to this port.
 */
SCI_STATUS scic_sds_port_clear_phy(
   SCIC_SDS_PORT_T *port,
   SCIC_SDS_PHY_T  *phy
)
{
   // Make sure that this phy is part of this port
   if (
           (port->phy_table[phy->phy_index] == phy)
        && (scic_sds_phy_get_port(phy) == port)
      )
   {
      // Yep it is assigned to this port so remove it
      scic_sds_phy_set_port(
         phy,
         &scic_sds_port_get_controller(port)->port_table[SCI_MAX_PORTS]
      );

      port->phy_table[phy->phy_index] = SCI_INVALID_HANDLE;

      return SCI_SUCCESS;
   }

   return SCI_FAILURE;
}

/**
 * This method will add a PHY to the selected port.
 *
 * @param[in] this_port This parameter specifies the port in which the phy will
 *            be added.
 *
 * @param[in] the_phy This parameter is the phy which is to be added to the
 *            port.
 *
 * @return This method returns an SCI_STATUS.
 * @retval SCI_SUCCESS the phy has been added to the port.
 * @retval Any other status is failre to add the phy to the port.
 */
SCI_STATUS scic_sds_port_add_phy(
   SCIC_SDS_PORT_T * this_port,
   SCIC_SDS_PHY_T  * the_phy
)
{
   return this_port->state_handlers->parent.add_phy_handler(
                                          &this_port->parent, &the_phy->parent);
}


/**
 * This method will remove the PHY from the selected PORT.
 *
 * @param[in] this_port This parameter specifies the port in which the phy will
 *            be added.
 *
 * @param[in] the_phy This parameter is the phy which is to be added to the
 *            port.
 *
 * @return This method returns an SCI_STATUS.
 * @retval SCI_SUCCESS the phy has been removed from the port.
 * @retval Any other status is failre to add the phy to the port.
 */
SCI_STATUS scic_sds_port_remove_phy(
   SCIC_SDS_PORT_T * this_port,
   SCIC_SDS_PHY_T  * the_phy
)
{
   return this_port->state_handlers->parent.remove_phy_handler(
                                          &this_port->parent, &the_phy->parent);
}

/**
 * @brief This method requests the SAS address for the supplied SAS port
 *        from the SCI implementation.
 *
 * @param[in]  this_port a handle corresponding to the SAS port for which
 *             to return the SAS address.
 * @param[out] sas_address This parameter specifies a pointer to a SAS
 *             address structure into which the core will copy the SAS
 *             address for the port.
 *
 * @return none
 */
void scic_sds_port_get_sas_address(
   SCIC_SDS_PORT_T   * this_port,
   SCI_SAS_ADDRESS_T * sas_address
)
{
   U32 index;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "scic_sds_port_get_sas_address(0x%x, 0x%x) enter\n",
      this_port, sas_address
   ));

   sas_address->high = 0;
   sas_address->low  = 0;

   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      if (this_port->phy_table[index] != NULL)
      {
         scic_sds_phy_get_sas_address(this_port->phy_table[index], sas_address);
      }
   }
}

/**
 * @brief This method will indicate which protocols are supported by this
 *        port.
 *
 * @param[in]  this_port a handle corresponding to the SAS port for which
 *             to return the supported protocols.
 * @param[out] protocols This parameter specifies a pointer to an IAF
 *             protocol field structure into which the core will copy
 *             the protocol values for the port.  The values are
 *             returned as part of a bit mask in order to allow for
 *             multi-protocol support.
 *
 * @return none
 */
static
void scic_sds_port_get_protocols(
   SCIC_SDS_PORT_T                            * this_port,
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T * protocols
)
{
   U8 index;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "scic_sds_port_get_protocols(0x%x, 0x%x) enter\n",
      this_port, protocols
   ));

   protocols->u.all = 0;

   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      if (this_port->phy_table[index] != NULL)
      {
         scic_sds_phy_get_protocols(this_port->phy_table[index], protocols);
      }
   }
}

/**
 * @brief This method requests the SAS address for the device directly
 *        attached to this SAS port.
 *
 * @param[in]  this_port a handle corresponding to the SAS port for which
 *             to return the SAS address.
 * @param[out] sas_address This parameter specifies a pointer to a SAS
 *             address structure into which the core will copy the SAS
 *             address for the device directly attached to the port.
 *
 * @return none
 */
void scic_sds_port_get_attached_sas_address(
   SCIC_SDS_PORT_T   * this_port,
   SCI_SAS_ADDRESS_T * sas_address
)
{
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T protocols;
   SCIC_SDS_PHY_T  *phy;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "scic_sds_port_get_attached_sas_address(0x%x, 0x%x) enter\n",
      this_port, sas_address
   ));

   // Ensure that the phy is both part of the port and currently
   // connected to the remote end-point.
   phy = scic_sds_port_get_a_connected_phy(this_port);
   if (phy != NULL)
   {
      scic_sds_phy_get_attached_phy_protocols(phy, &protocols);

      if (!protocols.u.bits.stp_target)
      {
         scic_sds_phy_get_attached_sas_address(phy, sas_address);
      }
      else
      {
         scic_sds_phy_get_sas_address(phy, sas_address);
         sas_address->low += phy->phy_index;

		 //Need to make up attached STP device's SAS address in
		 //the same order as recorded IAF from SSP device.
		 sas_address->high = SCIC_SWAP_DWORD(sas_address->high);
		 sas_address->low = SCIC_SWAP_DWORD(sas_address->low);
      }
   }
   else
   {
      sas_address->high = 0;
      sas_address->low  = 0;
   }
}

/**
 * @brief This method will indicate which protocols are supported by this
 *        remote device.
 *
 * @param[in]  this_port a handle corresponding to the SAS port for which
 *             to return the supported protocols.
 * @param[out] protocols This parameter specifies a pointer to an IAF
 *             protocol field structure into which the core will copy
 *             the protocol values for the port.  The values are
 *             returned as part of a bit mask in order to allow for
 *             multi-protocol support.
 *
 * @return none
 */
void scic_sds_port_get_attached_protocols(
   SCIC_SDS_PORT_T                            * this_port,
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T * protocols
)
{
   SCIC_SDS_PHY_T  *phy;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "scic_sds_port_get_attached_protocols(0x%x, 0x%x) enter\n",
      this_port, protocols
   ));

   // Ensure that the phy is both part of the port and currently
   // connected to the remote end-point.
   phy = scic_sds_port_get_a_connected_phy(this_port);
   if (phy != NULL)
      scic_sds_phy_get_attached_phy_protocols(phy, protocols);
   else
      protocols->u.all = 0;
}

/**
 * @brief This method returns the amount of memory required for a port
 *        object.
 *
 * @return U32
 */
U32 scic_sds_port_get_object_size(void)
{
   return sizeof(SCIC_SDS_PORT_T);
}

/**
 * @brief This method returns the minimum number of timers required for all
 *        port objects.
 *
 * @return U32
 */
U32 scic_sds_port_get_min_timer_count(void)
{
   return SCIC_SDS_PORT_MIN_TIMER_COUNT;
}

/**
 * @brief This method returns the maximum number of timers required for all
 *        port objects.
 *
 * @return U32
 */
U32 scic_sds_port_get_max_timer_count(void)
{
   return SCIC_SDS_PORT_MAX_TIMER_COUNT;
}

#ifdef SCI_LOGGING
void scic_sds_port_initialize_state_logging(
   SCIC_SDS_PORT_T *this_port
)
{
   sci_base_state_machine_logger_initialize(
      &this_port->parent.state_machine_logger,
      &this_port->parent.state_machine,
      &this_port->parent.parent,
      scic_cb_logger_log_states,
      "SCIC_SDS_PORT_T", "base state machine",
      SCIC_LOG_OBJECT_PORT
   );

   sci_base_state_machine_logger_initialize(
      &this_port->ready_substate_machine_logger,
      &this_port->ready_substate_machine,
      &this_port->parent.parent,
      scic_cb_logger_log_states,
      "SCIC_SDS_PORT_T", "ready substate machine",
      SCIC_LOG_OBJECT_PORT
   );
}
#endif

/**
 * This routine will construct a dummy remote node context data structure
 * This structure will be posted to the hardware to work around a scheduler
 * error in the hardware.
 *
 * @param[in] this_port The logical port on which we need to create the
 *            remote node context.
 * @param[in] rni The remote node index for this remote node context.
 *
 * @return none
 */
static
void scic_sds_port_construct_dummy_rnc(
   SCIC_SDS_PORT_T *this_port,
   U16              rni
)
{
   SCU_REMOTE_NODE_CONTEXT_T * rnc;

   rnc = &(this_port->owning_controller->remote_node_context_table[rni]);

   memset(rnc, 0, sizeof(SCU_REMOTE_NODE_CONTEXT_T));

   rnc->ssp.remote_sas_address_hi = 0;
   rnc->ssp.remote_sas_address_lo = 0;

   rnc->ssp.remote_node_index = rni;
   rnc->ssp.remote_node_port_width = 1;
   rnc->ssp.logical_port_index = this_port->physical_port_index;

   rnc->ssp.nexus_loss_timer_enable = FALSE;
   rnc->ssp.check_bit = FALSE;
   rnc->ssp.is_valid = TRUE;
   rnc->ssp.is_remote_node_context = TRUE;
   rnc->ssp.function_number = 0;
   rnc->ssp.arbitration_wait_time = 0;
}

/**
 * This routine will construct a dummy task context data structure.  This
 * structure will be posted to the hardwre to work around a scheduler error
 * in the hardware.
 *
 * @param[in] this_port The logical port on which we need to create the
 *            remote node context.
 *            context.
 * @param[in] tci The remote node index for this remote node context.
 *
 */
static
void scic_sds_port_construct_dummy_task(
   SCIC_SDS_PORT_T *this_port,
   U16              tci
)
{
   SCU_TASK_CONTEXT_T * task_context;

   task_context = scic_sds_controller_get_task_context_buffer(this_port->owning_controller, tci);

   memset(task_context, 0, sizeof(SCU_TASK_CONTEXT_T));

   task_context->abort = 0;
   task_context->priority = 0;
   task_context->initiator_request = 1;
   task_context->connection_rate = 1;
   task_context->protocol_engine_index = 0;
   task_context->logical_port_index = this_port->physical_port_index;
   task_context->protocol_type = SCU_TASK_CONTEXT_PROTOCOL_SSP;
   task_context->task_index = scic_sds_io_tag_get_index(tci);
   task_context->valid = SCU_TASK_CONTEXT_VALID;
   task_context->context_type = SCU_TASK_CONTEXT_TYPE;

   task_context->remote_node_index = this_port->reserved_rni;
   task_context->command_code = 0;

   task_context->link_layer_control = 0;
   task_context->do_not_dma_ssp_good_response = 1;
   task_context->strict_ordering = 0;
   task_context->control_frame = 0;
   task_context->timeout_enable = 0;
   task_context->block_guard_enable = 0;

   task_context->address_modifier = 0;

   task_context->task_phase = 0x01;
}

/**
 * This routine will free any allocated dummy resources for this port.
 *
 * @param[in, out] this_port The port on which the resources are being destroyed.
 */
static
void scic_sds_port_destroy_dummy_resources(
   SCIC_SDS_PORT_T * this_port
)
{
   if (this_port->reserved_tci != SCU_DUMMY_INDEX)
   {
      scic_controller_free_io_tag(
         this_port->owning_controller, this_port->reserved_tci
      );
   }

   if (this_port->reserved_rni != SCU_DUMMY_INDEX)
   {
      scic_sds_remote_node_table_release_remote_node_index(
         &this_port->owning_controller->available_remote_nodes, 1, this_port->reserved_rni
      );
   }

   this_port->reserved_rni = SCU_DUMMY_INDEX;
   this_port->reserved_tci = SCU_DUMMY_INDEX;
}

/**
 * @brief
 *
 * @param[in] this_port
 * @param[in] port_index
 * @param[in] owning_controller
 */
void scic_sds_port_construct(
   SCIC_SDS_PORT_T         *this_port,
   U8                      port_index,
   SCIC_SDS_CONTROLLER_T   *owning_controller
)
{
   U32 index;

   sci_base_port_construct(
      &this_port->parent,
      sci_base_object_get_logger(owning_controller),
      scic_sds_port_state_table
   );

   sci_base_state_machine_construct(
      scic_sds_port_get_ready_substate_machine(this_port),
      &this_port->parent.parent,
      scic_sds_port_ready_substate_table,
      SCIC_SDS_PORT_READY_SUBSTATE_WAITING
   );

   scic_sds_port_initialize_state_logging(this_port);

   this_port->logical_port_index  = SCIC_SDS_DUMMY_PORT;
   this_port->physical_port_index = port_index;
   this_port->active_phy_mask     = 0;
   this_port->enabled_phy_mask    = 0;
   this_port->owning_controller = owning_controller;

   this_port->started_request_count = 0;
   this_port->assigned_device_count = 0;

   this_port->reserved_rni = SCU_DUMMY_INDEX;
   this_port->reserved_tci = SCU_DUMMY_INDEX;

   this_port->timer_handle = SCI_INVALID_HANDLE;

   this_port->port_task_scheduler_registers = NULL;

   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      this_port->phy_table[index] = NULL;
   }
}

/**
 * @brief This method performs initialization of the supplied port.
 *        Initialization includes:
 *        - state machine initialization
 *        - member variable initialization
 *        - configuring the phy_mask
 *
 * @param[in] this_port
 * @param[in] transport_layer_registers
 * @param[in] port_task_scheduler_registers
 * @param[in] port_configuration_regsiter
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION This value is
 *         returned if the phy being added to the port
 */
SCI_STATUS scic_sds_port_initialize(
   SCIC_SDS_PORT_T *this_port,
   void            *port_task_scheduler_registers,
   void            *port_configuration_regsiter,
   void            *viit_registers
)
{
   this_port->port_task_scheduler_registers  = port_task_scheduler_registers;
   this_port->port_pe_configuration_register = port_configuration_regsiter;
   this_port->viit_registers                 = viit_registers;

   return SCI_SUCCESS;
}

/**
 * This method is the a general link up handler for the SCIC_SDS_PORT object.
 * This function will determine if this SCIC_SDS_PHY can
 * be assigned to this SCIC_SDS_PORT object. If the SCIC_SDS_PHY object can
 * is not a valid PHY for this port then the function will notify the SCIC_USER.
 * A PHY can only be part of a port if it's attached SAS ADDRESS is the same as
 * all other PHYs in the same port.
 *
 * @param[in] this_port This is the SCIC_SDS_PORT object for which has a phy
 *       that has gone link up.
 * @param[in] the_phy This is the SCIC_SDS_PHY object that has gone link up.
 * @param[in] do_notify_user This parameter specifies whether to inform
 *            the user (via scic_cb_port_link_up()) as to the fact that
 *            a new phy as become ready.
 * @param[in] do_resume_phy This parameter specifies whether to resume the phy.
 *            If this function is called from MPC mode, it will be always true.
 *            for APC, this will be false, so that phys could be resumed later
 *
 * @return none
 */
void scic_sds_port_general_link_up_handler(
   SCIC_SDS_PORT_T * this_port,
   SCIC_SDS_PHY_T  * the_phy,
   BOOL              do_notify_user,
   BOOL              do_resume_phy
)
{
   SCI_SAS_ADDRESS_T  port_sas_address;
   SCI_SAS_ADDRESS_T  phy_sas_address;

   scic_sds_port_get_attached_sas_address(this_port, &port_sas_address);
   scic_sds_phy_get_attached_sas_address(the_phy, &phy_sas_address);

   // If the SAS address of the new phy matches the SAS address of
   // other phys in the port OR this is the first phy in the port,
   // then activate the phy and allow it to be used for operations
   // in this port.
   if (
         (
            (phy_sas_address.high == port_sas_address.high)
         && (phy_sas_address.low  == port_sas_address.low )
         )
         || (this_port->active_phy_mask == 0)
      )
   {
      scic_sds_port_activate_phy(this_port, the_phy, do_notify_user, do_resume_phy);

      if (this_port->parent.state_machine.current_state_id
          == SCI_BASE_PORT_STATE_RESETTING)
      {
         sci_base_state_machine_change_state(
            &this_port->parent.state_machine, SCI_BASE_PORT_STATE_READY
         );
      }
   }
   else
   {
      scic_sds_port_invalid_link_up(this_port, the_phy);
   }
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_port_add_phy(
   SCI_PORT_HANDLE_T handle,
   SCI_PHY_HANDLE_T phy
)
{
   #if defined (SCI_LOGGING)
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)handle;
   #endif // defined (SCI_LOGGING)

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "scic_port_add_phy(0x%x, 0x%x) enter\n",
      handle, phy
   ));

   SCIC_LOG_ERROR((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "Interface function scic_port_add_phy() has been deprecated. "
      "PORT configuration is handled through the OEM parameters.\n"
   ));

   return SCI_FAILURE_ADDING_PHY_UNSUPPORTED;

}

// ---------------------------------------------------------------------------

SCI_STATUS scic_port_remove_phy(
   SCI_PORT_HANDLE_T handle,
   SCI_PHY_HANDLE_T phy
)
{
   #if defined (SCI_LOGGING)
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)handle;
   #endif // defined (SCI_LOGGING)

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "scic_port_remove_phy(0x%x, 0x%x) enter\n",
      handle, phy
   ));

   SCIC_LOG_ERROR((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "Interface function scic_port_remove_phy() has been deprecated. "
      "PORT configuration is handled through the OEM parameters.\n"
   ));

   return SCI_FAILURE_ADDING_PHY_UNSUPPORTED;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_port_get_properties(
   SCI_PORT_HANDLE_T        port,
   SCIC_PORT_PROPERTIES_T * properties
)
{
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)port;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "scic_port_get_properties(0x%x, 0x%x) enter\n",
      port, properties
   ));

   if (
         (port == SCI_INVALID_HANDLE)
      || (this_port->logical_port_index == SCIC_SDS_DUMMY_PORT)
      )
   {
      return SCI_FAILURE_INVALID_PORT;
   }

   properties->index    = this_port->logical_port_index;
   properties->phy_mask = scic_sds_port_get_phys(this_port);
   scic_sds_port_get_sas_address(this_port, &properties->local.sas_address);
   scic_sds_port_get_protocols(this_port, &properties->local.protocols);
   scic_sds_port_get_attached_sas_address(this_port, &properties->remote.sas_address);
   scic_sds_port_get_attached_protocols(this_port, &properties->remote.protocols);

   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_port_hard_reset(
   SCI_PORT_HANDLE_T handle,
   U32               reset_timeout
)
{
   SCIC_SDS_PORT_T * this_port = (SCIC_SDS_PORT_T *)handle;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "scic_port_hard_reset(0x%x, 0x%x) enter\n",
      handle, reset_timeout
   ));

   return this_port->state_handlers->parent.reset_handler(
                                       &this_port->parent,
                                       reset_timeout
                                     );
}

/**
 * This method assigns the direct attached device ID for this port.
 *
 * @param[in] this_port The port for which the direct attached device id is to
 *       be assigned.
 * @param[in] device_id The direct attached device ID to assign to the port.
 *       This will be the RNi for the device
 */
void scic_sds_port_setup_transports(
   SCIC_SDS_PORT_T * this_port,
   U32               device_id
)
{
   U8 index;

   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      if (this_port->active_phy_mask & (1 << index))
      {
         scic_sds_phy_setup_transport(this_port->phy_table[index], device_id);
      }
   }
}

/**
 * This method will resume the phy which is already added in the port.
 * Activation includes:
 * - enabling the Protocol Engine in the silicon.
 * - update the reay mask.
 *
 * @param[in] this_port This is the port on which the phy should be enabled.
 * @return none
 */
static
void scic_sds_port_resume_phy(
   SCIC_SDS_PORT_T * this_port,
   SCIC_SDS_PHY_T  * the_phy
)
{
   scic_sds_phy_resume (the_phy);
   this_port->enabled_phy_mask |= 1 << the_phy->phy_index;
}
/**
 * This method will activate the phy in the port.
 * Activation includes:
 * - adding the phy to the port
 * - enabling the Protocol Engine in the silicon.
 * - notifying the user that the link is up.
 *
 * @param[in] this_port This is the port on which the phy should be enabled.
 * @param[in] the_phy This is the specific phy which to enable.
 * @param[in] do_notify_user This parameter specifies whether to inform
 *            the user (via scic_cb_port_link_up()) as to the fact that
 *            a new phy as become ready.
 * @param[in] do_resume_phy This parameter specifies whether to resume the phy.
 *            If this function is called from MPC mode, it will be always true.
 *            for APC, this will be false, so that phys could be resumed later
 *

 * @return none
 */
void scic_sds_port_activate_phy(
   SCIC_SDS_PORT_T * this_port,
   SCIC_SDS_PHY_T  * the_phy,
   BOOL              do_notify_user,
   BOOL              do_resume_phy
)
{
   SCIC_SDS_CONTROLLER_T                      * controller;
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T   protocols;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "scic_sds_port_activate_phy(0x%x,0x%x,0x%x) enter\n",
      this_port, the_phy, do_notify_user
   ));

   controller = scic_sds_port_get_controller(this_port);
   scic_sds_phy_get_attached_phy_protocols(the_phy, &protocols);

   // If this is sata port then the phy has already been resumed
   if (!protocols.u.bits.stp_target)
   {
      if (do_resume_phy == TRUE)
      {
         scic_sds_port_resume_phy(this_port, the_phy);
      }
   }

   this_port->active_phy_mask |= 1 << the_phy->phy_index;

   scic_sds_controller_clear_invalid_phy(controller, the_phy);

   if (do_notify_user == TRUE)
      scic_cb_port_link_up(this_port->owning_controller, this_port, the_phy);
}

/**
 * This method will deactivate the supplied phy in the port.
 *
 * @param[in] this_port This is the port on which the phy should be
 *            deactivated.
 * @param[in] the_phy This is the specific phy that is no longer
 *            active in the port.
 * @param[in] do_notify_user This parameter specifies whether to inform
 *            the user (via scic_cb_port_link_down()) as to the fact that
 *            a new phy as become ready.
 *
 * @return none
 */
void scic_sds_port_deactivate_phy(
   SCIC_SDS_PORT_T * this_port,
   SCIC_SDS_PHY_T  * the_phy,
   BOOL              do_notify_user
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "scic_sds_port_deactivate_phy(0x%x,0x%x,0x%x) enter\n",
      this_port, the_phy, do_notify_user
   ));

   this_port->active_phy_mask &= ~(1 << the_phy->phy_index);
   this_port->enabled_phy_mask  &= ~(1 << the_phy->phy_index);

   the_phy->max_negotiated_speed = SCI_SAS_NO_LINK_RATE;

   // Re-assign the phy back to the LP as if it were a narrow port for APC mode.
   // For MPC mode, the phy will remain in the port
   if (this_port->owning_controller->oem_parameters.sds1.controller.mode_type
       == SCIC_PORT_AUTOMATIC_CONFIGURATION_MODE)
   {
   SCU_PCSPExCR_WRITE(this_port, the_phy->phy_index, the_phy->phy_index);
   }

   if (do_notify_user == TRUE)
      scic_cb_port_link_down(this_port->owning_controller, this_port, the_phy);
}

/**
 * This method will disable the phy and report that the phy is not valid for this
 * port object.
 *
 * @param[in] this_port This is the port on which the phy should be disabled.
 * @param[in] the_phy This is the specific phy which to disabled.
 *
 * @return None
 */
void scic_sds_port_invalid_link_up(
   SCIC_SDS_PORT_T * this_port,
   SCIC_SDS_PHY_T  * the_phy
)
{
   SCIC_SDS_CONTROLLER_T * controller = scic_sds_port_get_controller(this_port);

   // Check to see if we have alreay reported this link as bad and if not go
   // ahead and tell the SCI_USER that we have discovered an invalid link.
   if ((controller->invalid_phy_mask & (1 << the_phy->phy_index)) == 0)
   {
      scic_sds_controller_set_invalid_phy(controller, the_phy);

      scic_cb_port_invalid_link_up(controller, this_port, the_phy);
   }
}

/**
 * @brief This method returns FALSE if the port only has a single phy object
 *        assigned.  If there are no phys or more than one phy then the
 *        method will return TRUE.
 *
 * @param[in] this_port The port for which the wide port condition is to be
 *            checked.
 *
 * @return BOOL
 * @retval TRUE Is returned if this is a wide ported port.
 * @retval FALSE Is returned if this is a narrow port.
 */
static
BOOL scic_sds_port_is_wide(
   SCIC_SDS_PORT_T *this_port
)
{
   U32 index;
   U32 phy_count = 0;

   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      if (this_port->phy_table[index] != NULL)
      {
         phy_count++;
      }
   }

   return (phy_count != 1);
}

/**
 * @brief This method is called by the PHY object when the link is detected.
 *        if the port wants the PHY to continue on to the link up state then
 *        the port layer must return TRUE.  If the port object returns FALSE
 *        the phy object must halt its attempt to go link up.
 *
 * @param[in] this_port The port associated with the phy object.
 * @param[in] the_phy The phy object that is trying to go link up.
 *
 * @return TRUE if the phy object can continue to the link up condition.
 * @retval TRUE Is returned if this phy can continue to the ready state.
 * @retval FALSE Is returned if can not continue on to the ready state.
 *
 * @note This notification is in place for wide ports and direct attached
 *       phys.  Since there are no wide ported SATA devices this could
 *       become an invalid port configuration.
 */
BOOL scic_sds_port_link_detected(
   SCIC_SDS_PORT_T *this_port,
   SCIC_SDS_PHY_T  *the_phy
)
{
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T protocols;

   scic_sds_phy_get_attached_phy_protocols(the_phy, &protocols);

   if (
         (this_port->logical_port_index != SCIC_SDS_DUMMY_PORT)
      && (protocols.u.bits.stp_target)
      )
   {
      if (scic_sds_port_is_wide(this_port))
      {
         //direct attached Sata phy cannot be in wide port.
         scic_sds_port_invalid_link_up( this_port, the_phy);
      return FALSE;
   }
      else
      {
         SCIC_SDS_PORT_T *destination_port = &(this_port->owning_controller->port_table[the_phy->phy_index]);

         //add the phy to the its logical port for direct attached SATA. The phy will be added
         //to port whose port_index will be the phy_index.
         SCU_PCSPExCR_WRITE( destination_port, the_phy->phy_index, the_phy->phy_index);
      }
   }

   return TRUE;
}

/**
 * @brief This method is the entry point for the phy to inform
 *        the port that it is now in a ready state
 *
 * @param[in] this_port
 * @param[in] phy
 */
void scic_sds_port_link_up(
   SCIC_SDS_PORT_T *this_port,
   SCIC_SDS_PHY_T  *the_phy
)
{
   the_phy->is_in_link_training = FALSE;

   this_port->state_handlers->link_up_handler(this_port, the_phy);
}

/**
 * @brief This method is the entry point for the phy to inform
 *        the port that it is no longer in a ready state
 *
 * @param[in] this_port
 * @param[in] phy
 */
void scic_sds_port_link_down(
   SCIC_SDS_PORT_T *this_port,
   SCIC_SDS_PHY_T  *the_phy
)
{
   this_port->state_handlers->link_down_handler(this_port, the_phy);
}

/**
 * @brief This method is called to start an IO request on this port.
 *
 * @param[in] this_port
 * @param[in] the_device
 * @param[in] the_io_request
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_port_start_io(
   SCIC_SDS_PORT_T          *this_port,
   SCIC_SDS_REMOTE_DEVICE_T *the_device,
   SCIC_SDS_REQUEST_T       *the_io_request
)
{
   return this_port->state_handlers->start_io_handler(
                                       this_port, the_device, the_io_request);
}

/**
 * @brief This method is called to complete an IO request to the port.
 *
 * @param[in] this_port
 * @param[in] the_device
 * @param[in] the_io_request
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_port_complete_io(
   SCIC_SDS_PORT_T          *this_port,
   SCIC_SDS_REMOTE_DEVICE_T *the_device,
   SCIC_SDS_REQUEST_T       *the_io_request
)
{
   return this_port->state_handlers->complete_io_handler(
                                       this_port, the_device, the_io_request);
}

/**
 * @brief This method is provided to timeout requests for port operations.
 *        Mostly its for the port reset operation.
 *
 * @param[in] port This is the parameter or cookie value that is provided
 *       to the timer construct operation.
 */
void scic_sds_port_timeout_handler(
   void *port
)
{
   U32 current_state;
   SCIC_SDS_PORT_T * this_port;

   this_port = (SCIC_SDS_PORT_T *)port;
   current_state = sci_base_state_machine_get_state(
                           &this_port->parent.state_machine);

   if (current_state == SCI_BASE_PORT_STATE_RESETTING)
   {
      // if the port is still in the resetting state then the timeout fired
      // before the reset completed.
      sci_base_state_machine_change_state(
         &this_port->parent.state_machine,
         SCI_BASE_PORT_STATE_FAILED
      );
   }
   else if (current_state == SCI_BASE_PORT_STATE_STOPPED)
   {
      // if the port is stopped then the start request failed
      // In this case stay in the stopped state.
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_port),
         SCIC_LOG_OBJECT_PORT,
         "SCIC Port 0x%x failed to stop before tiemout.\n",
         this_port
      ));
   }
   else if (current_state == SCI_BASE_PORT_STATE_STOPPING)
   {
      // if the port is still stopping then the stop has not completed
      scic_cb_port_stop_complete(
         scic_sds_port_get_controller(this_port),
         port,
         SCI_FAILURE_TIMEOUT
      );
   }
   else
   {
      // The port is in the ready state and we have a timer reporting a timeout
      // this should not happen.
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_port),
         SCIC_LOG_OBJECT_PORT,
         "SCIC Port 0x%x is processing a timeout operation in state %d.\n",
         this_port, current_state
      ));
   }
}

// ---------------------------------------------------------------------------

#ifdef SCIC_DEBUG_ENABLED
void scic_sds_port_decrement_request_count(
   SCIC_SDS_PORT_T *this_port
)
{
   if (this_port->started_request_count == 0)
   {
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_port),
         SCIC_LOG_OBJECT_PORT,
         "SCIC Port object requested to decrement started io count past zero.\n"
      ));
   }
   else
   {
      this_port->started_request_count--;
   }
}
#endif

/**
 * @brief This function updates the hardwares VIIT entry for this port.
 *
 * @param[in] this_port
 */
void scic_sds_port_update_viit_entry(
   SCIC_SDS_PORT_T *this_port
)
{
   SCI_SAS_ADDRESS_T sas_address;

   scic_sds_port_get_sas_address(this_port, &sas_address);

   scu_port_viit_register_write(
      this_port, initiator_sas_address_hi, sas_address.high);

   scu_port_viit_register_write(
      this_port, initiator_sas_address_lo, sas_address.low);

   // This value get cleared just in case its not already cleared
   scu_port_viit_register_write(
      this_port, reserved, 0);


   // We are required to update the status register last
   scu_port_viit_register_write(
      this_port, status, (
           SCU_VIIT_ENTRY_ID_VIIT
         | SCU_VIIT_IPPT_INITIATOR
         | ((1UL << this_port->physical_port_index) << SCU_VIIT_ENTRY_LPVIE_SHIFT)
         | SCU_VIIT_STATUS_ALL_VALID
         )
   );
}

/**
 * @brief This method returns the maximum allowed speed for data transfers
 *        on this port.  This maximum allowed speed evaluates to the maximum
 *        speed of the slowest phy in the port.
 *
 * @param[in] this_port This parameter specifies the port for which to
 *            retrieve the maximum allowed speed.
 *
 * @return This method returns the maximum negotiated speed of the slowest
 *         phy in the port.
 */
SCI_SAS_LINK_RATE scic_sds_port_get_max_allowed_speed(
   SCIC_SDS_PORT_T * this_port
)
{
   U16                index             = 0;
   SCI_SAS_LINK_RATE  max_allowed_speed = SCI_SAS_600_GB;
   SCIC_SDS_PHY_T   * phy               = NULL;

   // Loop through all of the phys in this port and find the phy with the
   // lowest maximum link rate.
   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      phy = this_port->phy_table[index];
      if (
            (phy != NULL)
         && (scic_sds_port_active_phy(this_port, phy) == TRUE)
         && (phy->max_negotiated_speed < max_allowed_speed)
         )
         max_allowed_speed = phy->max_negotiated_speed;
   }

   return max_allowed_speed;
}


/**
 * @brief This method passes the event to core user.
 * @param[in] this_port The port that a BCN happens.
 * @param[in] this_phy  The phy that receives BCN.
 *
 * @return none
 */
void scic_sds_port_broadcast_change_received(
   SCIC_SDS_PORT_T * this_port,
   SCIC_SDS_PHY_T * this_phy
)
{
   //notify the user.
   scic_cb_port_bc_change_primitive_recieved(
      this_port->owning_controller, this_port, this_phy
   );
}


/**
 * @brief This API methhod enables the broadcast change notification from
 *        underneath hardware.
 * @param[in] this_port The port that a BCN had been disabled from.
 *
 * @return none
 */
void scic_port_enable_broadcast_change_notification(
   SCI_PORT_HANDLE_T  port
)
{
   SCIC_SDS_PORT_T * this_port = (SCIC_SDS_PORT_T *)port;
   SCIC_SDS_PHY_T * phy;
   U32 register_value;
   U8 index;

   // Loop through all of the phys to enable BCN.
   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      phy = this_port->phy_table[index];
      if ( phy != NULL)
      {
         register_value = SCU_SAS_LLCTL_READ(phy);

         // clear the bit by writing 1.
         SCU_SAS_LLCTL_WRITE(phy, register_value);
      }
   }
}

/**
 * @brief This method release resources in for a scic port.
 *
 * @param[in] controller This parameter specifies the core controller, one of
 *            its phy's resources are to be released.
 * @param[in] this_port This parameter specifies the port whose resource is to
 *            be released.
 */
void scic_sds_port_release_resource(
   SCIC_SDS_CONTROLLER_T * controller,
   SCIC_SDS_PORT_T *this_port
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "scic_sds_port_release_resource(0x%x, 0x%x)\n",
      controller, this_port
   ));

   //Currently, the only resource to be released is a timer.
   if (this_port->timer_handle != NULL)
   {
      scic_cb_timer_destroy(controller, this_port->timer_handle);
      this_port->timer_handle = NULL;
   }
}


//******************************************************************************
//* PORT STATE MACHINE
//******************************************************************************

//***************************************************************************
//*  DEFAULT HANDLERS
//***************************************************************************

/**
 * This is the default method for port a start request.  It will report a
 * warning and exit.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_port_default_start_handler(
   SCI_BASE_PORT_T *port
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_PORT_T *)port),
      SCIC_LOG_OBJECT_PORT,
      "SCIC Port 0x%08x requested to start while in invalid state %d\n",
      port,
      sci_base_state_machine_get_state(
         scic_sds_port_get_base_state_machine((SCIC_SDS_PORT_T *)port))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This is the default method for a port stop request.  It will report a
 * warning and exit.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_port_default_stop_handler(
   SCI_BASE_PORT_T *port
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_PORT_T *)port),
      SCIC_LOG_OBJECT_PORT,
      "SCIC Port 0x%08x requested to stop while in invalid state %d\n",
      port,
      sci_base_state_machine_get_state(
         scic_sds_port_get_base_state_machine((SCIC_SDS_PORT_T *)port))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This is the default method for a port destruct request.  It will report a
 * warning and exit.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_port_default_destruct_handler(
   SCI_BASE_PORT_T *port
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_PORT_T *)port),
      SCIC_LOG_OBJECT_PORT,
      "SCIC Port 0x%08x requested to destruct while in invalid state %d\n",
      port,
      sci_base_state_machine_get_state(
         scic_sds_port_get_base_state_machine((SCIC_SDS_PORT_T *)port))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This is the default method for a port reset request.  It will report a
 * warning and exit.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 * @param[in] timeout This is the timeout for the reset request to complete.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_port_default_reset_handler(
   SCI_BASE_PORT_T * port,
   U32               timeout
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_PORT_T *)port),
      SCIC_LOG_OBJECT_PORT,
      "SCIC Port 0x%08x requested to reset while in invalid state %d\n",
      port,
      sci_base_state_machine_get_state(
         scic_sds_port_get_base_state_machine((SCIC_SDS_PORT_T *)port))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This is the default method for a port add phy request.  It will report a
 * warning and exit.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_port_default_add_phy_handler(
   SCI_BASE_PORT_T *port,
   SCI_BASE_PHY_T  *phy
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_PORT_T *)port),
      SCIC_LOG_OBJECT_PORT,
      "SCIC Port 0x%08x requested to add phy 0x%08x while in invalid state %d\n",
      port, phy,
      sci_base_state_machine_get_state(
         scic_sds_port_get_base_state_machine((SCIC_SDS_PORT_T *)port))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This is the default method for a port remove phy request.  It will report a
 * warning and exit.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_port_default_remove_phy_handler(
   SCI_BASE_PORT_T *port,
   SCI_BASE_PHY_T  *phy
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_PORT_T *)port),
      SCIC_LOG_OBJECT_PORT,
      "SCIC Port 0x%08x requested to remove phy 0x%08x while in invalid state %d\n",
      port, phy,
      sci_base_state_machine_get_state(
         scic_sds_port_get_base_state_machine((SCIC_SDS_PORT_T *)port))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This is the default method for a port unsolicited frame request.  It will
 * report a warning and exit.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 *
 * @todo Is it even possible to receive an unsolicited frame directed to a
 *       port object?  It seems possible if we implementing virtual functions
 *       but until then?
 */
SCI_STATUS scic_sds_port_default_frame_handler(
   SCIC_SDS_PORT_T * port,
   U32               frame_index
)
{
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)port;

   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "SCIC Port 0x%08x requested to process frame %d while in invalid state %d\n",
      port, frame_index,
      sci_base_state_machine_get_state(
         scic_sds_port_get_base_state_machine(this_port))
   ));

   scic_sds_controller_release_frame(
      scic_sds_port_get_controller(this_port), frame_index
   );

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This is the default method for a port event request.  It will report a
 * warning and exit.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_port_default_event_handler(
   SCIC_SDS_PORT_T * port,
   U32               event_code
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_PORT_T *)port),
      SCIC_LOG_OBJECT_PORT,
      "SCIC Port 0x%08x requested to process event 0x%08x while in invalid state %d\n",
      port, event_code,
      sci_base_state_machine_get_state(
         scic_sds_port_get_base_state_machine((SCIC_SDS_PORT_T *)port))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This is the default method for a port link up notification.  It will report
 * a warning and exit.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
void scic_sds_port_default_link_up_handler(
   SCIC_SDS_PORT_T *this_port,
   SCIC_SDS_PHY_T  *phy
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "SCIC Port 0x%08x received link_up notification from phy 0x%08x while in invalid state %d\n",
      this_port, phy,
      sci_base_state_machine_get_state(
         scic_sds_port_get_base_state_machine(this_port))
   ));
}

/**
 * This is the default method for a port link down notification.  It will
 * report a warning and exit.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
void scic_sds_port_default_link_down_handler(
   SCIC_SDS_PORT_T *this_port,
   SCIC_SDS_PHY_T  *phy
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "SCIC Port 0x%08x received link down notification from phy 0x%08x while in invalid state %d\n",
      this_port, phy,
      sci_base_state_machine_get_state(
         scic_sds_port_get_base_state_machine(this_port))
   ));
}

/**
 * This is the default method for a port start io request.  It will report a
 * warning and exit.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_port_default_start_io_handler(
   SCIC_SDS_PORT_T          *this_port,
   SCIC_SDS_REMOTE_DEVICE_T *device,
   SCIC_SDS_REQUEST_T       *io_request
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "SCIC Port 0x%08x requested to start io request 0x%08x while in invalid state %d\n",
      this_port, io_request,
      sci_base_state_machine_get_state(
         scic_sds_port_get_base_state_machine(this_port))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This is the default method for a port complete io request.  It will report
 * a warning and exit.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_port_default_complete_io_handler(
   SCIC_SDS_PORT_T          *this_port,
   SCIC_SDS_REMOTE_DEVICE_T *device,
   SCIC_SDS_REQUEST_T       *io_request
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_port),
      SCIC_LOG_OBJECT_PORT,
      "SCIC Port 0x%08x requested to complete io request 0x%08x while in invalid state %d\n",
      this_port, io_request,
      sci_base_state_machine_get_state(
         scic_sds_port_get_base_state_machine(this_port))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

//****************************************************************************
//* GENERAL STATE HANDLERS
//****************************************************************************

/**
 * This is a general complete io request handler for the SCIC_SDS_PORT object.
 *
 * @param[in] port This is the SCIC_SDS_PORT object on which the io request
 *       count will be decremented.
 * @param[in] device This is the SCIC_SDS_REMOTE_DEVICE object to which the io
 *       request is being directed.  This parameter is not required to
 *       complete this operation.
 * @param[in] io_request This is the request that is being completed on this
 *       port object.  This parameter is not required to complete this
 *       operation.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_port_general_complete_io_handler(
   SCIC_SDS_PORT_T          *port,
   SCIC_SDS_REMOTE_DEVICE_T *device,
   SCIC_SDS_REQUEST_T       *io_request
)
{
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)port;

   scic_sds_port_decrement_request_count(this_port);

   return SCI_SUCCESS;
}

//****************************************************************************
//* STOPPED STATE HANDLERS
//****************************************************************************
static
BOOL scic_sds_port_requires_scheduler_workaround(
   SCIC_SDS_PORT_T * this_port
)
{
   if (
         (
           this_port->owning_controller->logical_port_entries
         < this_port->owning_controller->task_context_entries
         )
      && (
           this_port->owning_controller->logical_port_entries
         < this_port->owning_controller->remote_node_entries
         )
      )
   {
      return TRUE;
   }

   return FALSE;
}


/**
 * This method takes the SCIC_SDS_PORT from a stopped state and attempts to
 * start it.  To start a port it must have no assiged devices and it must have
 * at least one phy assigned to it.  If those conditions are met then the port
 * can transition to the ready state.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION This SCIC_SDS_PORT
 *         object could not be started because the port configuration is not
 *         valid.
 * @retval SCI_SUCCESS the start request is successful and the SCIC_SDS_PORT
 *         object has transitioned to the SCI_BASE_PORT_STATE_READY.
 */
static
SCI_STATUS scic_sds_port_stopped_state_start_handler(
   SCI_BASE_PORT_T *port
)
{
   U32 phy_mask;
   SCI_STATUS status = SCI_SUCCESS;
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)port;

   if (this_port->assigned_device_count > 0)
   {
      /// @todo This is a start failure operation because there are still
      ///       devices assigned to this port.  There must be no devices
      ///       assigned to a port on a start operation.
      return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
   }

   this_port->timer_handle = scic_cb_timer_create(
      scic_sds_port_get_controller(this_port),
      scic_sds_port_timeout_handler,
      this_port
   );

   if (this_port->timer_handle == SCI_INVALID_HANDLE)
   {
      return SCI_FAILURE_INSUFFICIENT_RESOURCES;
   }

   if (scic_sds_port_requires_scheduler_workaround(this_port))
   {
   if (this_port->reserved_rni == SCU_DUMMY_INDEX)
   {
      this_port->reserved_rni =
         scic_sds_remote_node_table_allocate_remote_node(
            &this_port->owning_controller->available_remote_nodes, 1
         );

      if (this_port->reserved_rni != SCU_DUMMY_INDEX)
      {
         scic_sds_port_construct_dummy_rnc(
            this_port,
            this_port->reserved_rni
         );
      }
      else
      {
         status = SCI_FAILURE_INSUFFICIENT_RESOURCES;
      }
   }

   if (this_port->reserved_tci == SCU_DUMMY_INDEX)
   {
      // Allocate a TCI and remove the sequence nibble
      this_port->reserved_tci =
         scic_controller_allocate_io_tag(this_port->owning_controller);

      if (this_port->reserved_tci != SCU_DUMMY_INDEX)
      {
         scic_sds_port_construct_dummy_task(this_port, this_port->reserved_tci);
      }
      else
      {
         status = SCI_FAILURE_INSUFFICIENT_RESOURCES;
      }
   }
   }

   if (status == SCI_SUCCESS)
   {
      phy_mask = scic_sds_port_get_phys(this_port);

      // There are one or more phys assigned to this port.  Make sure
      // the port's phy mask is in fact legal and supported by the
      // silicon.
      if (scic_sds_port_is_phy_mask_valid(this_port, phy_mask) == TRUE)
      {
         sci_base_state_machine_change_state(
            scic_sds_port_get_base_state_machine(this_port),
            SCI_BASE_PORT_STATE_READY
         );
      }
      else
      {
         status = SCI_FAILURE;
      }
   }

   if (status != SCI_SUCCESS)
   {
      scic_sds_port_destroy_dummy_resources(this_port);
   }

   return status;
}

/**
 * This method takes the SCIC_SDS_PORT that is in a stopped state and handles
 * a stop request.  This function takes no action.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS the stop request is successful as the SCIC_SDS_PORT
 *         object is already stopped.
 */
static
SCI_STATUS scic_sds_port_stopped_state_stop_handler(
   SCI_BASE_PORT_T *port
)
{
   // We are already stopped so there is nothing to do here
   return SCI_SUCCESS;
}

/**
 * This method takes the SCIC_SDS_PORT that is in a stopped state and handles
 * the destruct request.  The stopped state is the only state in which the
 * SCIC_SDS_PORT can be destroyed.  This function causes the port object to
 * transition to the SCI_BASE_PORT_STATE_FINAL.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_port_stopped_state_destruct_handler(
   SCI_BASE_PORT_T *port
)
{
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)port;

   sci_base_state_machine_stop(&this_port->parent.state_machine);

   return SCI_SUCCESS;
}

/**
 * This method takes the SCIC_SDS_PORT that is in a stopped state and handles
 * the add phy request.  In MPC mode the only time a phy can be added to a
 * port is in the SCI_BASE_PORT_STATE_STOPPED.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION is returned when the phy
 *         can not be added to the port.
 * @retval SCI_SUCCESS if the phy is added to the port.
 */
static
SCI_STATUS scic_sds_port_stopped_state_add_phy_handler(
   SCI_BASE_PORT_T *port,
   SCI_BASE_PHY_T  *phy
)
{
   SCIC_SDS_PORT_T * this_port = (SCIC_SDS_PORT_T *)port;
   SCIC_SDS_PHY_T  * this_phy  = (SCIC_SDS_PHY_T  *)phy;
   SCI_SAS_ADDRESS_T port_sas_address;

   // Read the port assigned SAS Address if there is one
   scic_sds_port_get_sas_address(this_port, &port_sas_address);

   if (port_sas_address.high != 0 && port_sas_address.low != 0)
   {
      SCI_SAS_ADDRESS_T phy_sas_address;

      // Make sure that the PHY SAS Address matches the SAS Address
      // for this port.
      scic_sds_phy_get_sas_address(this_phy, &phy_sas_address);

      if (
            (port_sas_address.high != phy_sas_address.high)
         || (port_sas_address.low  != phy_sas_address.low)
         )
      {
         return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
      }
   }

   return scic_sds_port_set_phy(this_port, this_phy);
}


/**
 * This method takes the SCIC_SDS_PORT that is in a stopped state and handles
 * the remove phy request.  In MPC mode the only time a phy can be removed
 * from a port is in the SCI_BASE_PORT_STATE_STOPPED.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION is returned when the phy
 *         can not be added to the port.
 * @retval SCI_SUCCESS if the phy is added to the port.
 */
static
SCI_STATUS scic_sds_port_stopped_state_remove_phy_handler(
   SCI_BASE_PORT_T *port,
   SCI_BASE_PHY_T  *phy
)
{
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)port;
   SCIC_SDS_PHY_T  *this_phy  = (SCIC_SDS_PHY_T  *)phy;

   return scic_sds_port_clear_phy(this_port, this_phy);
}

//****************************************************************************
//*  READY STATE HANDLERS
//****************************************************************************

//****************************************************************************
//*  RESETTING STATE HANDLERS
//****************************************************************************

//****************************************************************************
//*  STOPPING STATE HANDLERS
//****************************************************************************

/**
 * This method takes the SCIC_SDS_PORT that is in a stopping state and handles
 * the complete io request. Should the request count reach 0 then the port
 * object will transition to the stopped state.
 *
 * @param[in] port This is the SCIC_SDS_PORT object on which the io request
 *       count will be decremented.
 * @param[in] device This is the SCIC_SDS_REMOTE_DEVICE object to which the io
 *       request is being directed.  This parameter is not required to
 *       complete this operation.
 * @param[in] io_request This is the request that is being completed on this
 *       port object.  This parameter is not required to complete this
 *       operation.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_port_stopping_state_complete_io_handler(
   SCIC_SDS_PORT_T          *port,
   SCIC_SDS_REMOTE_DEVICE_T *device,
   SCIC_SDS_REQUEST_T       *io_request
)
{
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)port;

   scic_sds_port_decrement_request_count(this_port);

   if (this_port->started_request_count == 0)
   {
      sci_base_state_machine_change_state(
         scic_sds_port_get_base_state_machine(this_port),
         SCI_BASE_PORT_STATE_STOPPED
      );
   }

   return SCI_SUCCESS;
}

//****************************************************************************
//*  RESETTING STATE HANDLERS
//****************************************************************************

/**
 * This method will stop a failed port.  This causes a transition to the
 * stopping state.
 *
 * @param[in] port This is the port object which is being requested to stop.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_port_reset_state_stop_handler(
   SCI_BASE_PORT_T *port
)
{
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)port;

   sci_base_state_machine_change_state(
      &this_port->parent.state_machine,
      SCI_BASE_PORT_STATE_STOPPING
   );

   return SCI_SUCCESS;
}

/**
 * This method will transition a failed port to its ready state.  The port
 * failed because a hard reset request timed out but at some time later one or
 * more phys in the port became ready.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
void scic_sds_port_reset_state_link_up_handler(
   SCIC_SDS_PORT_T *this_port,
   SCIC_SDS_PHY_T  *phy
)
{
   /// @todo We should make sure that the phy that has gone link up is the same
   ///       one on which we sent the reset.  It is possible that the phy on
   ///       which we sent the reset is not the one that has gone link up and we
   ///       want to make sure that phy being reset comes back.  Consider the
   ///       case where a reset is sent but before the hardware processes the
   ///       reset it get a link up on the port because of a hot plug event.
   ///       because of the reset request this phy will go link down almost
   ///       immediately.

   // In the resetting state we don't notify the user regarding
   // link up and link down notifications.
   scic_sds_port_general_link_up_handler(this_port, phy, FALSE, TRUE);
}

/**
 * This method process link down notifications that occur during a
 * port reset operation. Link downs can occur during the reset operation.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
void scic_sds_port_reset_state_link_down_handler(
   SCIC_SDS_PORT_T *this_port,
   SCIC_SDS_PHY_T  *phy
)
{
   // In the resetting state we don't notify the user regarding
   // link up and link down notifications.
   scic_sds_port_deactivate_phy(this_port, phy, FALSE);
}

// ---------------------------------------------------------------------------

SCIC_SDS_PORT_STATE_HANDLER_T
   scic_sds_port_state_handler_table[SCI_BASE_PORT_MAX_STATES] =
{
   // SCI_BASE_PORT_STATE_STOPPED
   {
      {
         scic_sds_port_stopped_state_start_handler,
         scic_sds_port_stopped_state_stop_handler,
         scic_sds_port_stopped_state_destruct_handler,
         scic_sds_port_default_reset_handler,
         scic_sds_port_stopped_state_add_phy_handler,
         scic_sds_port_stopped_state_remove_phy_handler
      },
      scic_sds_port_default_frame_handler,
      scic_sds_port_default_event_handler,
      scic_sds_port_default_link_up_handler,
      scic_sds_port_default_link_down_handler,
      scic_sds_port_default_start_io_handler,
      scic_sds_port_default_complete_io_handler
   },
   // SCI_BASE_PORT_STATE_STOPPING
   {
      {
         scic_sds_port_default_start_handler,
         scic_sds_port_default_stop_handler,
         scic_sds_port_default_destruct_handler,
         scic_sds_port_default_reset_handler,
         scic_sds_port_default_add_phy_handler,
         scic_sds_port_default_remove_phy_handler
      },
      scic_sds_port_default_frame_handler,
      scic_sds_port_default_event_handler,
      scic_sds_port_default_link_up_handler,
      scic_sds_port_default_link_down_handler,
      scic_sds_port_default_start_io_handler,
      scic_sds_port_stopping_state_complete_io_handler
   },
   // SCI_BASE_PORT_STATE_READY
   {
      {
         scic_sds_port_default_start_handler,
         scic_sds_port_default_stop_handler,
         scic_sds_port_default_destruct_handler,
         scic_sds_port_default_reset_handler,
         scic_sds_port_default_add_phy_handler,
         scic_sds_port_default_remove_phy_handler
      },
      scic_sds_port_default_frame_handler,
      scic_sds_port_default_event_handler,
      scic_sds_port_default_link_up_handler,
      scic_sds_port_default_link_down_handler,
      scic_sds_port_default_start_io_handler,
      scic_sds_port_general_complete_io_handler
   },
   // SCI_BASE_PORT_STATE_RESETTING
   {
      {
         scic_sds_port_default_start_handler,
         scic_sds_port_reset_state_stop_handler,
         scic_sds_port_default_destruct_handler,
         scic_sds_port_default_reset_handler,
         scic_sds_port_default_add_phy_handler,
         scic_sds_port_default_remove_phy_handler
      },
      scic_sds_port_default_frame_handler,
      scic_sds_port_default_event_handler,
      scic_sds_port_reset_state_link_up_handler,
      scic_sds_port_reset_state_link_down_handler,
      scic_sds_port_default_start_io_handler,
      scic_sds_port_general_complete_io_handler
   },
   // SCI_BASE_PORT_STATE_FAILED
   {
      {
         scic_sds_port_default_start_handler,
         scic_sds_port_default_stop_handler,
         scic_sds_port_default_destruct_handler,
         scic_sds_port_default_reset_handler,
         scic_sds_port_default_add_phy_handler,
         scic_sds_port_default_remove_phy_handler
      },
      scic_sds_port_default_frame_handler,
      scic_sds_port_default_event_handler,
      scic_sds_port_default_link_up_handler,
      scic_sds_port_default_link_down_handler,
      scic_sds_port_default_start_io_handler,
      scic_sds_port_general_complete_io_handler
   }
};

//******************************************************************************
//*  PORT STATE PRIVATE METHODS
//******************************************************************************

/**
 * This method will enable the SCU Port Task Scheduler for this port object
 * but will leave the port task scheduler in a suspended state.
 *
 * @param[in] this_port This is the port object which to suspend.
 *
 * @return none
 */
static
void scic_sds_port_enable_port_task_scheduler(
   SCIC_SDS_PORT_T *this_port
)
{
   U32 pts_control_value;

   pts_control_value = scu_port_task_scheduler_read(this_port, control);

   pts_control_value |= SCU_PTSxCR_GEN_BIT(ENABLE) | SCU_PTSxCR_GEN_BIT(SUSPEND);

   scu_port_task_scheduler_write(this_port, control, pts_control_value);
}

/**
 * This method will disable the SCU port task scheduler for this port
 * object.
 *
 * @param[in] this_port This is the port object which to resume.
 *
 * @return none
 */
static
void scic_sds_port_disable_port_task_scheduler(
   SCIC_SDS_PORT_T *this_port
)
{
   U32 pts_control_value;

   pts_control_value = scu_port_task_scheduler_read(this_port, control);

   pts_control_value &= ~(   SCU_PTSxCR_GEN_BIT(ENABLE)
                           | SCU_PTSxCR_GEN_BIT(SUSPEND) );

   scu_port_task_scheduler_write(this_port, control, pts_control_value);
}

/**
 *
 */
static
void scic_sds_port_post_dummy_remote_node(
      SCIC_SDS_PORT_T *this_port
)
{
   U32 command;
   SCU_REMOTE_NODE_CONTEXT_T * rnc;

   if (this_port->reserved_rni != SCU_DUMMY_INDEX)
   {
   rnc = &(this_port->owning_controller->remote_node_context_table[this_port->reserved_rni]);

   rnc->ssp.is_valid = TRUE;

   command = (
       (SCU_CONTEXT_COMMAND_POST_RNC_32)
     | (this_port->physical_port_index << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT)
     | (this_port->reserved_rni)
   );

   scic_sds_controller_post_request(this_port->owning_controller, command);

   scic_cb_stall_execution(10);

   command = (
       (SCU_CONTEXT_COMMAND_POST_RNC_SUSPEND_TX_RX)
     | (this_port->physical_port_index << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT)
     | (this_port->reserved_rni)
   );

   scic_sds_controller_post_request(this_port->owning_controller, command);
}
}

/**
 *
 */
static
void scic_sds_port_invalidate_dummy_remote_node(
   SCIC_SDS_PORT_T *this_port
)
{
   U32 command;
   SCU_REMOTE_NODE_CONTEXT_T * rnc;

   if (this_port->reserved_rni != SCU_DUMMY_INDEX)
   {
   rnc = &(this_port->owning_controller->remote_node_context_table[this_port->reserved_rni]);

   rnc->ssp.is_valid = FALSE;

   scic_cb_stall_execution(10);

   command = (
       (SCU_CONTEXT_COMMAND_POST_RNC_INVALIDATE)
     | (this_port->physical_port_index << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT)
     | (this_port->reserved_rni)
   );

   scic_sds_controller_post_request(this_port->owning_controller, command);
}
}

//******************************************************************************
//*  PORT STATE METHODS
//******************************************************************************

/**
 * This method will perform the actions required by the SCIC_SDS_PORT on
 * entering the SCI_BASE_PORT_STATE_STOPPED. This function sets the stopped
 * state handlers for the SCIC_SDS_PORT object and disables the port task
 * scheduler in the hardware.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PORT object.
 *
 * @return none
 */
static
void scic_sds_port_stopped_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PORT_T *this_port;
   this_port = (SCIC_SDS_PORT_T *)object;

   scic_sds_port_set_base_state_handlers(
      this_port, SCI_BASE_PORT_STATE_STOPPED
   );

   if (
         SCI_BASE_PORT_STATE_STOPPING
      == this_port->parent.state_machine.previous_state_id
      )
   {
      // If we enter this state becasuse of a request to stop
      // the port then we want to disable the hardwares port
      // task scheduler.
      scic_sds_port_disable_port_task_scheduler(this_port);
   }
}

/**
 * This method will perform the actions required by the SCIC_SDS_PORT on
 * exiting the SCI_BASE_STATE_STOPPED. This function enables the SCU hardware
 * port task scheduler.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PORT object.
 *
 * @return none
 */
static
void scic_sds_port_stopped_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PORT_T *this_port;
   this_port = (SCIC_SDS_PORT_T *)object;

   // Enable and suspend the port task scheduler
   scic_sds_port_enable_port_task_scheduler(this_port);
}

/**
 * This method will perform the actions required by the SCIC_SDS_PORT on
 * entering the SCI_BASE_PORT_STATE_READY. This function sets the ready state
 * handlers for the SCIC_SDS_PORT object, reports the port object as not ready
 * and starts the ready substate machine.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PORT object.
 *
 * @return none
 */
static
void scic_sds_port_ready_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PORT_T *this_port;
   this_port = (SCIC_SDS_PORT_T *)object;

   // Put the ready state handlers in place though they will not be there long
   scic_sds_port_set_base_state_handlers(
      this_port, SCI_BASE_PORT_STATE_READY
   );

   if (
          SCI_BASE_PORT_STATE_RESETTING
      == this_port->parent.state_machine.previous_state_id
      )
   {
      scic_cb_port_hard_reset_complete(
         scic_sds_port_get_controller(this_port),
         this_port,
         SCI_SUCCESS
      );
   }
   else
   {
      // Notify the caller that the port is not yet ready
      scic_cb_port_not_ready(
         scic_sds_port_get_controller(this_port),
         this_port,
         SCIC_PORT_NOT_READY_NO_ACTIVE_PHYS
      );
   }

   // Post and suspend the dummy remote node context for this
   // port.
   scic_sds_port_post_dummy_remote_node(this_port);

   // Start the ready substate machine
   sci_base_state_machine_start(
      scic_sds_port_get_ready_substate_machine(this_port)
   );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PORT on
 * exiting the SCI_BASE_STATE_READY. This function does nothing.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PORT object.
 *
 * @return none
 */
static
void scic_sds_port_ready_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PORT_T *this_port;
   this_port = (SCIC_SDS_PORT_T *)object;

   sci_base_state_machine_stop(&this_port->ready_substate_machine);

   scic_cb_stall_execution(10);
   scic_sds_port_invalidate_dummy_remote_node(this_port);
}

/**
 * This method will perform the actions required by the SCIC_SDS_PORT on
 * entering the SCI_BASE_PORT_STATE_RESETTING. This function sets the
 * resetting state handlers for the SCIC_SDS_PORT object.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PORT object.
 *
 * @return none
 */
static
void scic_sds_port_resetting_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PORT_T *this_port;
   this_port = (SCIC_SDS_PORT_T *)object;

   scic_sds_port_set_base_state_handlers(
      this_port, SCI_BASE_PORT_STATE_RESETTING
   );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PORT on
 * exiting the SCI_BASE_STATE_RESETTING. This function does nothing.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PORT object.
 *
 * @return none
 */
static
void scic_sds_port_resetting_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PORT_T *this_port;
   this_port = (SCIC_SDS_PORT_T *)object;

   scic_cb_timer_stop(
      scic_sds_port_get_controller(this_port),
      this_port->timer_handle
   );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PORT on
 * entering the SCI_BASE_PORT_STATE_STOPPING. This function sets the stopping
 * state handlers for the SCIC_SDS_PORT object.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PORT object.
 *
 * @return none
 */
static
void scic_sds_port_stopping_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PORT_T *this_port;
   this_port = (SCIC_SDS_PORT_T *)object;

   scic_sds_port_set_base_state_handlers(
      this_port, SCI_BASE_PORT_STATE_STOPPING
   );

   if (this_port->started_request_count == 0)
   {
      sci_base_state_machine_change_state(
         &this_port->parent.state_machine,
         SCI_BASE_PORT_STATE_STOPPED
      );
   }
}

/**
 * This method will perform the actions required by the SCIC_SDS_PORT on
 * exiting the SCI_BASE_STATE_STOPPING. This function does nothing.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PORT object.
 *
 * @return none
 */
static
void scic_sds_port_stopping_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PORT_T *this_port;
   this_port = (SCIC_SDS_PORT_T *)object;

   scic_cb_timer_stop(
      scic_sds_port_get_controller(this_port),
      this_port->timer_handle
   );

   scic_cb_timer_destroy(
      scic_sds_port_get_controller(this_port),
      this_port->timer_handle
   );
   this_port->timer_handle = NULL;

   scic_sds_port_destroy_dummy_resources(this_port);
}

/**
 * This method will perform the actions required by the SCIC_SDS_PORT on
 * entering the SCI_BASE_PORT_STATE_STOPPING. This function sets the stopping
 * state handlers for the SCIC_SDS_PORT object.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PORT object.
 *
 * @return none
 */
static
void scic_sds_port_failed_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PORT_T *this_port;
   this_port = (SCIC_SDS_PORT_T *)object;

   scic_sds_port_set_base_state_handlers(
      this_port,
      SCI_BASE_PORT_STATE_FAILED
   );

   scic_cb_port_hard_reset_complete(
      scic_sds_port_get_controller(this_port),
      this_port,
      SCI_FAILURE_TIMEOUT
   );
}

// ---------------------------------------------------------------------------

SCI_BASE_STATE_T scic_sds_port_state_table[SCI_BASE_PORT_MAX_STATES] =
{
   {
      SCI_BASE_PORT_STATE_STOPPED,
      scic_sds_port_stopped_state_enter,
      scic_sds_port_stopped_state_exit
   },
   {
      SCI_BASE_PORT_STATE_STOPPING,
      scic_sds_port_stopping_state_enter,
      scic_sds_port_stopping_state_exit
   },
   {
      SCI_BASE_PORT_STATE_READY,
      scic_sds_port_ready_state_enter,
      scic_sds_port_ready_state_exit
   },
   {
      SCI_BASE_PORT_STATE_RESETTING,
      scic_sds_port_resetting_state_enter,
      scic_sds_port_resetting_state_exit
   },
   {
      SCI_BASE_PORT_STATE_FAILED,
      scic_sds_port_failed_state_enter,
      NULL
   }
};

//******************************************************************************
//* PORT READY SUB-STATE MACHINE
//******************************************************************************

//****************************************************************************
//*  READY SUBSTATE HANDLERS
//****************************************************************************

/**
 * This method is the general ready state stop handler for the SCIC_SDS_PORT
 * object.  This function will transition the ready substate machine to its
 * final state.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_port_ready_substate_stop_handler(
   SCI_BASE_PORT_T *port
)
{
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)port;

   sci_base_state_machine_change_state(
      &this_port->parent.state_machine,
      SCI_BASE_PORT_STATE_STOPPING
   );

   return SCI_SUCCESS;
}

/**
 * This method is the general ready substate complete io handler for the
 * SCIC_SDS_PORT object.  This function decrments the outstanding request
 * count for this port object.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 * @param[in] device This is the SCI_BASE_REMOTE_DEVICE object which is not
 *       used in this function.
 * @param[in] io_request This is the SCI_BASE_REQUEST object which is not used
 *       in this function.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_port_ready_substate_complete_io_handler(
   SCIC_SDS_PORT_T               *port,
   struct SCIC_SDS_REMOTE_DEVICE *device,
   struct SCIC_SDS_REQUEST       *io_request
)
{
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)port;

   scic_sds_port_decrement_request_count(this_port);

   return SCI_SUCCESS;
}

static
SCI_STATUS scic_sds_port_ready_substate_add_phy_handler(
   SCI_BASE_PORT_T *port,
   SCI_BASE_PHY_T  *phy
)
{
   SCIC_SDS_PORT_T * this_port = (SCIC_SDS_PORT_T *)port;
   SCIC_SDS_PHY_T  * this_phy  = (SCIC_SDS_PHY_T  *)phy;
   SCI_STATUS        status;

   status = scic_sds_port_set_phy(this_port, this_phy);

   if (status == SCI_SUCCESS)
   {
      scic_sds_port_general_link_up_handler(this_port, this_phy, TRUE, FALSE);

      this_port->not_ready_reason = SCIC_PORT_NOT_READY_RECONFIGURING;

      sci_base_state_machine_change_state(
         &this_port->ready_substate_machine,
         SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING
      );
   }

   return status;
}

static
SCI_STATUS scic_sds_port_ready_substate_remove_phy_handler(
   SCI_BASE_PORT_T *port,
   SCI_BASE_PHY_T  *phy
)
{
   SCIC_SDS_PORT_T * this_port = (SCIC_SDS_PORT_T *)port;
   SCIC_SDS_PHY_T  * this_phy  = (SCIC_SDS_PHY_T  *)phy;
   SCI_STATUS        status;

   status = scic_sds_port_clear_phy(this_port, this_phy);

   if (status == SCI_SUCCESS)
   {
      scic_sds_port_deactivate_phy(this_port, this_phy, TRUE);

      this_port->not_ready_reason = SCIC_PORT_NOT_READY_RECONFIGURING;

      sci_base_state_machine_change_state(
         &this_port->ready_substate_machine,
         SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING
      );
   }

   return status;
}

//****************************************************************************
//*  READY SUBSTATE WAITING HANDLERS
//****************************************************************************

/**
 * This method is the ready waiting substate link up handler for the
 * SCIC_SDS_PORT object.  This methos will report the link up condition for
 * this port and will transition to the ready operational substate.
 *
 * @param[in] this_port This is the SCIC_SDS_PORT object that which has a phy
 *       that has gone link up.
 * @param[in] the_phy This is the SCIC_SDS_PHY object that has gone link up.
 *
 * @return none
 */
static
void scic_sds_port_ready_waiting_substate_link_up_handler(
   SCIC_SDS_PORT_T *this_port,
   SCIC_SDS_PHY_T  *the_phy
)
{
   // Since this is the first phy going link up for the port we can just enable
   // it and continue.
   scic_sds_port_activate_phy(this_port, the_phy, TRUE, TRUE);

   sci_base_state_machine_change_state(
      &this_port->ready_substate_machine,
      SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL
   );
}

/**
 * This method is the ready waiting substate start io handler for the
 * SCIC_SDS_PORT object. The port object can not accept new requests so the
 * request is failed.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 * @param[in] device This is the SCI_BASE_REMOTE_DEVICE object which is not
 *       used in this request.
 * @param[in] io_request This is the SCI_BASE_REQUEST object which is not used
 *       in this function.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
static
SCI_STATUS scic_sds_port_ready_waiting_substate_start_io_handler(
   SCIC_SDS_PORT_T          *port,
   SCIC_SDS_REMOTE_DEVICE_T *device,
   SCIC_SDS_REQUEST_T       *io_request
)
{
   return SCI_FAILURE_INVALID_STATE;
}

//****************************************************************************
//*  READY SUBSTATE OPERATIONAL HANDLERS
//****************************************************************************

/**
 * This method will cause the port to reset.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 * @param[in] timeout This is the timeout for the reset request to complete.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_port_ready_operational_substate_reset_handler(
   SCI_BASE_PORT_T * port,
   U32               timeout
)
{
   SCI_STATUS        status = SCI_FAILURE_INVALID_PHY;
   U32               phy_index;
   SCIC_SDS_PORT_T * this_port = (SCIC_SDS_PORT_T *)port;
   SCIC_SDS_PHY_T  * selected_phy = SCI_INVALID_HANDLE;


   // Select a phy on which we can send the hard reset request.
   for (
         phy_index = 0;
            (phy_index < SCI_MAX_PHYS)
         && (selected_phy == SCI_INVALID_HANDLE);
         phy_index++
       )
   {
      selected_phy = this_port->phy_table[phy_index];

      if (
            (selected_phy != SCI_INVALID_HANDLE)
         && !scic_sds_port_active_phy(this_port, selected_phy)
         )
      {
         // We found a phy but it is not ready select different phy
         selected_phy = SCI_INVALID_HANDLE;
      }
   }

   // If we have a phy then go ahead and start the reset procedure
   if (selected_phy != SCI_INVALID_HANDLE)
   {
      status = scic_sds_phy_reset(selected_phy);

      if (status == SCI_SUCCESS)
      {
         scic_cb_timer_start(
            scic_sds_port_get_controller(this_port),
            this_port->timer_handle,
            timeout
         );

         this_port->not_ready_reason = SCIC_PORT_NOT_READY_HARD_RESET_REQUESTED;

         sci_base_state_machine_change_state(
            &this_port->parent.state_machine,
            SCI_BASE_PORT_STATE_RESETTING
         );
      }
   }

   return status;
}

/**
 * This method is the ready operational substate link up handler for the
 * SCIC_SDS_PORT object. This function notifies the SCI User that the phy has
 * gone link up.
 *
 * @param[in] this_port This is the SCIC_SDS_PORT object that which has a phy
 *       that has gone link up.
 * @param[in] the_phy This is the SCIC_SDS_PHY object that has gone link up.
 *
 * @return none
 */
static
void scic_sds_port_ready_operational_substate_link_up_handler(
   SCIC_SDS_PORT_T *this_port,
   SCIC_SDS_PHY_T  *the_phy
)
{
   scic_sds_port_general_link_up_handler(this_port, the_phy, TRUE, TRUE);
}

/**
 * This method is the ready operational substate link down handler for the
 * SCIC_SDS_PORT object. This function notifies the SCI User that the phy has
 * gone link down and if this is the last phy in the port the port will change
 * state to the ready waiting substate.
 *
 * @param[in] this_port This is the SCIC_SDS_PORT object that which has a phy
 *       that has gone link down.
 * @param[in] the_phy This is the SCIC_SDS_PHY object that has gone link down.
 *
 * @return none
 */
static
void scic_sds_port_ready_operational_substate_link_down_handler(
   SCIC_SDS_PORT_T *this_port,
   SCIC_SDS_PHY_T  *the_phy
)
{
   scic_sds_port_deactivate_phy(this_port, the_phy, TRUE);

   // If there are no active phys left in the port, then transition
   // the port to the WAITING state until such time as a phy goes
   // link up.
   if (this_port->active_phy_mask == 0)
   {
      sci_base_state_machine_change_state(
         scic_sds_port_get_ready_substate_machine(this_port),
         SCIC_SDS_PORT_READY_SUBSTATE_WAITING
      );
   }
}

/**
 * This method is the ready operational substate start io handler for the
 * SCIC_SDS_PORT object.  This function incremetns the outstanding request
 * count for this port object.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 * @param[in] device This is the SCI_BASE_REMOTE_DEVICE object which is not
 *       used in this function.
 * @param[in] io_request This is the SCI_BASE_REQUEST object which is not used
 *       in this function.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_port_ready_operational_substate_start_io_handler(
   SCIC_SDS_PORT_T          *port,
   SCIC_SDS_REMOTE_DEVICE_T *device,
   SCIC_SDS_REQUEST_T       *io_request
)
{
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)port;

   scic_sds_port_increment_request_count(this_port);

   return SCI_SUCCESS;
}

//****************************************************************************
//*  READY SUBSTATE OPERATIONAL HANDLERS
//****************************************************************************

/**
 * This is the default method for a port add phy request.  It will report a
 * warning and exit.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
static
SCI_STATUS scic_sds_port_ready_configuring_substate_add_phy_handler(
   SCI_BASE_PORT_T *port,
   SCI_BASE_PHY_T  *phy
)
{
   SCIC_SDS_PORT_T * this_port = (SCIC_SDS_PORT_T *)port;
   SCIC_SDS_PHY_T  * this_phy  = (SCIC_SDS_PHY_T  *)phy;
   SCI_STATUS        status;

   status = scic_sds_port_set_phy(this_port, this_phy);

   if (status == SCI_SUCCESS)
   {
      scic_sds_port_general_link_up_handler(this_port, this_phy, TRUE, FALSE);

      // Re-enter the configuring state since this may be the last phy in
      // the port.
      sci_base_state_machine_change_state(
         &this_port->ready_substate_machine,
         SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING
      );
   }

   return status;
}

/**
 * This is the default method for a port remove phy request.  It will report a
 * warning and exit.
 *
 * @param[in] port This is the SCI_BASE_PORT object which is cast into a
 *       SCIC_SDS_PORT object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
static
SCI_STATUS scic_sds_port_ready_configuring_substate_remove_phy_handler(
   SCI_BASE_PORT_T *port,
   SCI_BASE_PHY_T  *phy
)
{
   SCIC_SDS_PORT_T * this_port = (SCIC_SDS_PORT_T *)port;
   SCIC_SDS_PHY_T  * this_phy  = (SCIC_SDS_PHY_T  *)phy;
   SCI_STATUS        status;

   status = scic_sds_port_clear_phy(this_port, this_phy);

   if (status == SCI_SUCCESS)
   {
      scic_sds_port_deactivate_phy(this_port, this_phy, TRUE);

      // Re-enter the configuring state since this may be the last phy in
      // the port.
      sci_base_state_machine_change_state(
         &this_port->ready_substate_machine,
         SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING
      );
   }

   return status;
}

/**
 * This method will decrement the outstanding request count for this port.
 * If the request count goes to 0 then the port can be reprogrammed with
 * its new phy data.
 *
 * @param[in] port This is the port that is being requested to complete
 *            the io request.
 * @param[in] device This is the device on which the io is completing.
 * @param[in] io_request This is the io request that is completing.
 */
static
SCI_STATUS scic_sds_port_ready_configuring_substate_complete_io_handler(
   SCIC_SDS_PORT_T          *port,
   SCIC_SDS_REMOTE_DEVICE_T *device,
   SCIC_SDS_REQUEST_T       *io_request
)
{
   scic_sds_port_decrement_request_count(port);

   if (port->started_request_count == 0)
   {
      sci_base_state_machine_change_state(
         &port->ready_substate_machine,
         SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL
      );
   }

   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

SCIC_SDS_PORT_STATE_HANDLER_T
   scic_sds_port_ready_substate_handler_table[SCIC_SDS_PORT_READY_MAX_SUBSTATES] =
{
   // SCIC_SDS_PORT_READY_SUBSTATE_WAITING
   {
      {
         scic_sds_port_default_start_handler,
         scic_sds_port_ready_substate_stop_handler,
         scic_sds_port_default_destruct_handler,
         scic_sds_port_default_reset_handler,
         scic_sds_port_ready_substate_add_phy_handler,
         scic_sds_port_default_remove_phy_handler
      },
      scic_sds_port_default_frame_handler,
      scic_sds_port_default_event_handler,
      scic_sds_port_ready_waiting_substate_link_up_handler,
      scic_sds_port_default_link_down_handler,
      scic_sds_port_ready_waiting_substate_start_io_handler,
      scic_sds_port_ready_substate_complete_io_handler,
   },
   // SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL
   {
      {
         scic_sds_port_default_start_handler,
         scic_sds_port_ready_substate_stop_handler,
         scic_sds_port_default_destruct_handler,
         scic_sds_port_ready_operational_substate_reset_handler,
         scic_sds_port_ready_substate_add_phy_handler,
         scic_sds_port_ready_substate_remove_phy_handler
      },
      scic_sds_port_default_frame_handler,
      scic_sds_port_default_event_handler,
      scic_sds_port_ready_operational_substate_link_up_handler,
      scic_sds_port_ready_operational_substate_link_down_handler,
      scic_sds_port_ready_operational_substate_start_io_handler,
      scic_sds_port_ready_substate_complete_io_handler
   },
   // SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING
   {
      {
         scic_sds_port_default_start_handler,
         scic_sds_port_ready_substate_stop_handler,
         scic_sds_port_default_destruct_handler,
         scic_sds_port_default_reset_handler,
         scic_sds_port_ready_configuring_substate_add_phy_handler,
         scic_sds_port_ready_configuring_substate_remove_phy_handler
      },
      scic_sds_port_default_frame_handler,
      scic_sds_port_default_event_handler,
      scic_sds_port_default_link_up_handler,
      scic_sds_port_default_link_down_handler,
      scic_sds_port_default_start_io_handler,
      scic_sds_port_ready_configuring_substate_complete_io_handler
   }
};

/**
 * This macro sets the port ready substate handlers.
 */
#define scic_sds_port_set_ready_state_handlers(port, state_id) \
   scic_sds_port_set_state_handlers( \
      port, &scic_sds_port_ready_substate_handler_table[(state_id)] \
   )

//******************************************************************************
//*  PORT STATE PRIVATE METHODS
//******************************************************************************

/**
 * This method will susped the port task scheduler for this port object.
 *
 * @param[in] this_port This is the SCIC_SDS_PORT object to suspend.
 *
 * @return none
 */
void scic_sds_port_suspend_port_task_scheduler(
   SCIC_SDS_PORT_T *this_port
)
{
   U32 pts_control_value;

   pts_control_value = scu_port_task_scheduler_read(this_port, control);
   pts_control_value |= SCU_PTSxCR_GEN_BIT(SUSPEND);
   scu_port_task_scheduler_write(this_port, control, pts_control_value);
}

/**
 * This method will resume the port task scheduler for this port object.
 *
 * @param[in] this_port This is the SCIC_SDS_PORT object to resume.
 *
 * @return none
 */
void scic_sds_port_resume_port_task_scheduler(
   SCIC_SDS_PORT_T *this_port
)
{
   U32 pts_control_value;

   pts_control_value = scu_port_task_scheduler_read(this_port, control);

   pts_control_value &= ~SCU_PTSxCR_GEN_BIT(SUSPEND);

   scu_port_task_scheduler_write(this_port, control, pts_control_value);
}

/**
 * This routine will post the dummy request.  This will prevent the hardware
 * scheduler from posting new requests to the front of the scheduler queue
 * causing a starvation problem for currently ongoing requests.
 *
 * @parm[in] this_port The port on which the task must be posted.
 *
 * @return none
 */
static
void scic_sds_port_post_dummy_request(
   SCIC_SDS_PORT_T *this_port
)
{
   U32 command;
   SCU_TASK_CONTEXT_T * task_context;

   if (this_port->reserved_tci != SCU_DUMMY_INDEX)
   {
   task_context = scic_sds_controller_get_task_context_buffer(
                     this_port->owning_controller,
                     this_port->reserved_tci
                  );

   task_context->abort = 0;

   command = (
         (SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC)
      | (this_port->physical_port_index << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT)
      | (this_port->reserved_tci)
   );

   scic_sds_controller_post_request(this_port->owning_controller, command);
}
}

/**
 * This routine will abort the dummy request.  This will alow the hardware to
 * power down parts of the silicon to save power.
 *
 * @parm[in] this_port The port on which the task must be aborted.
 *
 * @return none
 */
static
void scic_sds_port_abort_dummy_request(
   SCIC_SDS_PORT_T *this_port
)
{
   U32 command;
   SCU_TASK_CONTEXT_T * task_context;

   if (this_port->reserved_tci != SCU_DUMMY_INDEX)
   {
   task_context = scic_sds_controller_get_task_context_buffer(
                     this_port->owning_controller,
                     this_port->reserved_tci
                  );

   task_context->abort = 1;

   command = (
        (SCU_CONTEXT_COMMAND_REQUEST_POST_TC_ABORT)
      | (this_port->physical_port_index << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT)
      | (this_port->reserved_tci)
   );

   scic_sds_controller_post_request(this_port->owning_controller, command);
}
}

//******************************************************************************
//*  PORT READY SUBSTATE METHODS
//******************************************************************************

/**
 * This method will perform the actions required by the SCIC_SDS_PORT on
 * entering the SCIC_SDS_PORT_READY_SUBSTATE_WAITING. This function checks the
 * port for any ready phys.  If there is at least one phy in a ready state
 * then the port transitions to the ready operational substate.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PORT object.
 *
 * @return none
 */
static
void scic_sds_port_ready_substate_waiting_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)object;

   scic_sds_port_set_ready_state_handlers(
      this_port, SCIC_SDS_PORT_READY_SUBSTATE_WAITING
   );

   scic_sds_port_suspend_port_task_scheduler(this_port);


   this_port->not_ready_reason = SCIC_PORT_NOT_READY_NO_ACTIVE_PHYS;

   if (this_port->active_phy_mask != 0)
   {
      // At least one of the phys on the port is ready
      sci_base_state_machine_change_state(
         &this_port->ready_substate_machine,
         SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL
      );
   }
}

/**
 * This method will perform the actions required by the SCIC_SDS_PORT on
 * exiting the SCIC_SDS_PORT_READY_SUBSTATE_WAITING. This function resume the
 * PTSG that was suspended at the entry of this state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PORT object.
 *
 * @return none
 */
static
void scic_sds_port_ready_substate_waiting_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)object;
   scic_sds_port_resume_port_task_scheduler(this_port);
}

/**
 * This method will perform the actions required by the SCIC_SDS_PORT on
 * entering the SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL. This function sets
 * the state handlers for the port object, notifies the SCI User that the port
 * is ready, and resumes port operations.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PORT object.
 *
 * @return none
 */
static
void scic_sds_port_ready_substate_operational_enter(
   SCI_BASE_OBJECT_T *object
)
{
   U32 index;
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)object;

   scic_sds_port_set_ready_state_handlers(
      this_port, SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL
   );

   scic_cb_port_ready(
      scic_sds_port_get_controller(this_port), this_port
   );

   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      if (this_port->phy_table[index] != NULL)
      {
         scic_sds_port_write_phy_assignment(
            this_port, this_port->phy_table[index]
         );

         //if the bit at the index location for active phy mask is set and
         //enabled_phy_mask is not set then resume the phy
         if ( ( (this_port->active_phy_mask ^ this_port->enabled_phy_mask) & (1 << index) ) != 0)
         {
            scic_sds_port_resume_phy (
               this_port,
               this_port->phy_table[index]
            );
         }
      }
   }

   scic_sds_port_update_viit_entry(this_port);

   // Post the dummy task for the port so the hardware can schedule
   // io correctly
   scic_sds_port_post_dummy_request(this_port);
}

/**
 * This method will perform the actions required by the SCIC_SDS_PORT on
 * exiting the SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL. This function reports
 * the port not ready and suspends the port task scheduler.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PORT object.
 *
 * @return none
 */
static
void scic_sds_port_ready_substate_operational_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)object;

   // Kill the dummy task for this port if it has not yet posted
   // the hardware will treat this as a NOP and just return abort
   // complete.
   scic_sds_port_abort_dummy_request(this_port);

   scic_cb_port_not_ready(
      scic_sds_port_get_controller(this_port),
      this_port,
      this_port->not_ready_reason
   );
}

//******************************************************************************
//*  PORT READY CONFIGURING METHODS
//******************************************************************************

/**
 * This method will perform the actions required by the SCIC_SDS_PORT on
 * exiting the SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL. This function reports
 * the port not ready and suspends the port task scheduler.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PORT object.
 *
 * @return none
 */
static
void scic_sds_port_ready_substate_configuring_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PORT_T *this_port = (SCIC_SDS_PORT_T *)object;

   scic_sds_port_set_ready_state_handlers(
      this_port, SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING
   );

   if (this_port->active_phy_mask == 0)
   {
      scic_cb_port_not_ready(
         scic_sds_port_get_controller(this_port),
         this_port,
         SCIC_PORT_NOT_READY_NO_ACTIVE_PHYS
      );

      sci_base_state_machine_change_state(
         &this_port->ready_substate_machine,
         SCIC_SDS_PORT_READY_SUBSTATE_WAITING
      );
   }
   //do not wait for IO to go to 0 in this state.
   else
   {
      sci_base_state_machine_change_state(
         &this_port->ready_substate_machine,
         SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL
      );
   }
}

// ---------------------------------------------------------------------------

SCI_BASE_STATE_T
   scic_sds_port_ready_substate_table[SCIC_SDS_PORT_READY_MAX_SUBSTATES] =
{
   {
      SCIC_SDS_PORT_READY_SUBSTATE_WAITING,
      scic_sds_port_ready_substate_waiting_enter,
      scic_sds_port_ready_substate_waiting_exit
   },
   {
      SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL,
      scic_sds_port_ready_substate_operational_enter,
      scic_sds_port_ready_substate_operational_exit
   },
   {
      SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING,
      scic_sds_port_ready_substate_configuring_enter,
      NULL
   }
};

