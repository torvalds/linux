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
 * @brief This file contains the implementation of the SCIC_SDS_PHY public and
 *        protected methods.
 */

#include <dev/isci/scil/scic_user_callback.h>
#include <dev/isci/scil/scic_phy.h>
#include <dev/isci/scil/scic_sds_phy.h>
#include <dev/isci/scil/scic_sds_port.h>
#include <dev/isci/scil/scic_sds_controller_registers.h>
#include <dev/isci/scil/scic_sds_phy_registers.h>
#include <dev/isci/scil/scic_sds_logger.h>
#include <dev/isci/scil/scic_sds_remote_node_context.h>
#include <dev/isci/scil/sci_util.h>
#include <dev/isci/scil/scic_sds_controller.h>
#include <dev/isci/scil/scu_event_codes.h>
#include <dev/isci/scil/sci_base_state.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_sata.h>
#include <dev/isci/scil/sci_base_state_machine.h>
#include <dev/isci/scil/scic_sds_port_registers.h>

#define SCIC_SDS_PHY_MIN_TIMER_COUNT  (SCI_MAX_PHYS)
#define SCIC_SDS_PHY_MAX_TIMER_COUNT  (SCI_MAX_PHYS)

// Maximum arbitration wait time in micro-seconds
#define SCIC_SDS_PHY_MAX_ARBITRATION_WAIT_TIME  (700)

#define AFE_REGISTER_WRITE_DELAY 10

//*****************************************************************************
//* SCIC SDS PHY Internal Methods
//*****************************************************************************

/**
 * @brief This method will initialize the phy transport layer registers
 *
 * @param[in] this_phy
 * @param[in] transport_layer_registers
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_phy_transport_layer_initialization(
   SCIC_SDS_PHY_T                  *this_phy,
   SCU_TRANSPORT_LAYER_REGISTERS_T *transport_layer_registers
)
{
   U32 tl_control;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sds_phy_link_layer_initialization(this_phy:0x%x, link_layer_registers:0x%x)\n",
      this_phy, transport_layer_registers
   ));

   this_phy->transport_layer_registers = transport_layer_registers;

   SCU_STPTLDARNI_WRITE(this_phy, SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX);

   // Hardware team recommends that we enable the STP prefetch for all transports
   tl_control = SCU_TLCR_READ(this_phy);
   tl_control |= SCU_TLCR_GEN_BIT(STP_WRITE_DATA_PREFETCH);
   SCU_TLCR_WRITE(this_phy, tl_control);

   return SCI_SUCCESS;
}

/**
 * @brief This method will initialize the phy link layer registers
 *
 * @param[in] this_phy
 * @param[in] link_layer_registers
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_phy_link_layer_initialization(
   SCIC_SDS_PHY_T             *this_phy,
   SCU_LINK_LAYER_REGISTERS_T *link_layer_registers
)
{
   U32                phy_configuration;
   SAS_CAPABILITIES_T phy_capabilities;
   U32                parity_check = 0;
   U32                parity_count = 0;
   U32                link_layer_control;
   U32                phy_timer_timeout_values;
   U32                clksm_value = 0;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sds_phy_link_layer_initialization(this_phy:0x%x, link_layer_registers:0x%x)\n",
      this_phy, link_layer_registers
   ));

   this_phy->link_layer_registers = link_layer_registers;

   // Set our IDENTIFY frame data
   #define SCI_END_DEVICE 0x01

   SCU_SAS_TIID_WRITE(
      this_phy,
      (   SCU_SAS_TIID_GEN_BIT(SMP_INITIATOR)
        | SCU_SAS_TIID_GEN_BIT(SSP_INITIATOR)
        | SCU_SAS_TIID_GEN_BIT(STP_INITIATOR)
        | SCU_SAS_TIID_GEN_BIT(DA_SATA_HOST)
        | SCU_SAS_TIID_GEN_VAL(DEVICE_TYPE, SCI_END_DEVICE) )
      );

   // Write the device SAS Address
   SCU_SAS_TIDNH_WRITE(this_phy, 0xFEDCBA98);
   SCU_SAS_TIDNL_WRITE(this_phy, this_phy->phy_index);

   // Write the source SAS Address
   SCU_SAS_TISSAH_WRITE(
      this_phy,
      this_phy->owning_port->owning_controller->oem_parameters.sds1.phys[
          this_phy->phy_index].sas_address.sci_format.high
   );
   SCU_SAS_TISSAL_WRITE(
      this_phy,
      this_phy->owning_port->owning_controller->oem_parameters.sds1.phys[
          this_phy->phy_index].sas_address.sci_format.low
   );

   // Clear and Set the PHY Identifier
   SCU_SAS_TIPID_WRITE(this_phy, 0x00000000);
   SCU_SAS_TIPID_WRITE(this_phy, SCU_SAS_TIPID_GEN_VALUE(ID, this_phy->phy_index));

   // Change the initial state of the phy configuration register
   phy_configuration = SCU_SAS_PCFG_READ(this_phy);

   // Hold OOB state machine in reset
   phy_configuration |=  SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
   SCU_SAS_PCFG_WRITE(this_phy, phy_configuration);

   // Configure the SNW capabilities
   phy_capabilities.u.all = 0;
   phy_capabilities.u.bits.start                      = 1;
   phy_capabilities.u.bits.gen3_without_ssc_supported = 1;
   phy_capabilities.u.bits.gen2_without_ssc_supported = 1;
   phy_capabilities.u.bits.gen1_without_ssc_supported = 1;

   /*
    * Set up SSC settings according to version of OEM Parameters.
    */
   {
       U8 header_version, enable_sata, enable_sas,
          sata_spread, sas_type, sas_spread;
       OEM_SSC_PARAMETERS_T ssc;

       header_version = this_phy->owning_port->owning_controller->
                        oem_parameters_version;

       ssc.bf.ssc_sata_tx_spread_level =
          this_phy->owning_port->owning_controller->oem_parameters.sds1.controller.ssc_sata_tx_spread_level;
       ssc.bf.ssc_sas_tx_spread_level =
          this_phy->owning_port->owning_controller->oem_parameters.sds1.controller.ssc_sas_tx_spread_level;
       ssc.bf.ssc_sas_tx_type =
          this_phy->owning_port->owning_controller->oem_parameters.sds1.controller.ssc_sas_tx_type;
       enable_sata = enable_sas = sata_spread = sas_type = sas_spread = 0;

       if (header_version == SCI_OEM_PARAM_VER_1_0)
       {
           /*
            * Version 1.0 is merely turning SSC on to default values.;
            */
           if (ssc.do_enable_ssc != 0)
           {
               enable_sas = enable_sata = TRUE;
               sas_type = 0x0;      // Downspreading
               sata_spread = 0x2;   // +0 to -1419 PPM
               sas_spread = 0x2;    // +0 to -1419 PPM
           }
       }
       else // header_version >= SCI_OEM_PARAM_VER_1_1
       {
          /*
           * Version 1.1 can turn on SAS and SATA independently and
           * specify spread levels. Also can specify spread type for SAS.
           */
          if ((sata_spread = ssc.bf.ssc_sata_tx_spread_level) != 0)
             enable_sata = TRUE;  // Downspreading only
          if ((sas_spread = ssc.bf.ssc_sas_tx_spread_level) != 0)
          {
             enable_sas = TRUE;
             sas_type = ssc.bf.ssc_sas_tx_type;
          }
       }

       if (enable_sas == TRUE)
       {
           U32 reg_val = scu_afe_register_read(
                             this_phy->owning_port->owning_controller,
                             scu_afe_xcvr[this_phy->phy_index].
                             afe_xcvr_control0);
           reg_val |= (0x00100000 | (((U32)sas_type) << 19));
           scu_afe_register_write(
               this_phy->owning_port->owning_controller,
               scu_afe_xcvr[this_phy->phy_index].afe_xcvr_control0,
               reg_val);

           reg_val = scu_afe_register_read(
                             this_phy->owning_port->owning_controller,
                             scu_afe_xcvr[this_phy->phy_index].
                             afe_tx_ssc_control);
           reg_val |= (((U32)(sas_spread)) << 8);
           scu_afe_register_write(
               this_phy->owning_port->owning_controller,
               scu_afe_xcvr[this_phy->phy_index].afe_tx_ssc_control,
               reg_val);
      phy_capabilities.u.bits.gen3_with_ssc_supported = 1;
      phy_capabilities.u.bits.gen2_with_ssc_supported = 1;
      phy_capabilities.u.bits.gen1_with_ssc_supported = 1;
   }

       if (enable_sata == TRUE)
       {
           U32 reg_val = scu_afe_register_read(
                         this_phy->owning_port->owning_controller,
                         scu_afe_xcvr[this_phy->phy_index].
                         afe_tx_ssc_control);
           reg_val |= (U32)sata_spread;
           scu_afe_register_write(
               this_phy->owning_port->owning_controller,
               scu_afe_xcvr[this_phy->phy_index].afe_tx_ssc_control,
               reg_val);

           reg_val = scu_link_layer_register_read(
                         this_phy,
                         stp_control);
           reg_val |= (U32)(1 << 12);
           scu_link_layer_register_write(
               this_phy,
               stp_control,
               reg_val);
       }
   }

   // The SAS specification indicates that the phy_capabilities that
   // are transmitted shall have an even parity.  Calculate the parity.
   parity_check = phy_capabilities.u.all;
   while (parity_check != 0)
   {
      if (parity_check & 0x1)
         parity_count++;
      parity_check >>= 1;
   }

   // If parity indicates there are an odd number of bits set, then
   // set the parity bit to 1 in the phy capabilities.
   if ((parity_count % 2) != 0)
      phy_capabilities.u.bits.parity = 1;

   SCU_SAS_PHYCAP_WRITE(this_phy, phy_capabilities.u.all);

   // Set the enable spinup period but disable the ability to send notify enable spinup
   SCU_SAS_ENSPINUP_WRITE(
     this_phy,
     SCU_ENSPINUP_GEN_VAL(
        COUNT,
        this_phy->owning_port->owning_controller->user_parameters.sds1.
           phys[this_phy->phy_index].notify_enable_spin_up_insertion_frequency
     )
   );

   // Write the ALIGN Insertion Ferequency for connected phy and inpendent of connected state
   clksm_value = SCU_ALIGN_INSERTION_FREQUENCY_GEN_VAL (
                     CONNECTED,
                     this_phy->owning_port->owning_controller->user_parameters.sds1.
                        phys[this_phy->phy_index].in_connection_align_insertion_frequency
                 );

   clksm_value |= SCU_ALIGN_INSERTION_FREQUENCY_GEN_VAL (
                     GENERAL,
                     this_phy->owning_port->owning_controller->user_parameters.sds1.
                        phys[this_phy->phy_index].align_insertion_frequency
                  );

   SCU_SAS_CLKSM_WRITE ( this_phy, clksm_value);


#if defined(PBG_HBA_A0_BUILD) || defined(PBG_HBA_A2_BUILD) || defined(PBG_HBA_BETA_BUILD)
   /// @todo Provide a way to write this register correctly
   scu_link_layer_register_write(this_phy, afe_lookup_table_control, 0x02108421);
#elif defined(PBG_BUILD)
   if (
         (this_phy->owning_port->owning_controller->pci_revision == SCIC_SDS_PCI_REVISION_C0)
      || (this_phy->owning_port->owning_controller->pci_revision == SCIC_SDS_PCI_REVISION_C1)
      )
   {
      scu_link_layer_register_write(this_phy, afe_lookup_table_control, 0x04210400);
      scu_link_layer_register_write(this_phy, sas_primitive_timeout, 0x20A7C05);
   }
   else
   {
      scu_link_layer_register_write(this_phy, afe_lookup_table_control, 0x02108421);
   }
#else
   /// @todo Provide a way to write this register correctly
   scu_link_layer_register_write(this_phy, afe_lookup_table_control, 0x0e739ce7);
#endif

   link_layer_control = SCU_SAS_LLCTL_GEN_VAL(
                           NO_OUTBOUND_TASK_TIMEOUT,
                           (U8) this_phy->owning_port->owning_controller->
                           user_parameters.sds1.no_outbound_task_timeout
                        );

#if PHY_MAX_LINK_SPEED_GENERATION == SCIC_SDS_PARM_GEN1_SPEED
#define COMPILED_MAX_LINK_RATE SCU_SAS_LINK_LAYER_CONTROL_MAX_LINK_RATE_GEN1
#elif PHY_MAX_LINK_SPEED_GENERATION == SCIC_SDS_PARM_GEN2_SPEED
#define COMPILED_MAX_LINK_RATE SCU_SAS_LINK_LAYER_CONTROL_MAX_LINK_RATE_GEN2
#else
#define COMPILED_MAX_LINK_RATE SCU_SAS_LINK_LAYER_CONTROL_MAX_LINK_RATE_GEN3
#endif // PHY_MAX_LINK_SPEED_GENERATION

   if (this_phy->owning_port->owning_controller->user_parameters.sds1.
       phys[this_phy->phy_index].max_speed_generation == SCIC_SDS_PARM_GEN3_SPEED)
   {
      link_layer_control |= SCU_SAS_LLCTL_GEN_VAL(
                               MAX_LINK_RATE, COMPILED_MAX_LINK_RATE
                            );
   }
   else if (this_phy->owning_port->owning_controller->user_parameters.sds1.
       phys[this_phy->phy_index].max_speed_generation == SCIC_SDS_PARM_GEN2_SPEED)
   {
      link_layer_control |= SCU_SAS_LLCTL_GEN_VAL(
                               MAX_LINK_RATE,
                               MIN(
                                  SCU_SAS_LINK_LAYER_CONTROL_MAX_LINK_RATE_GEN2,
                                  COMPILED_MAX_LINK_RATE)
                            );
   }
   else
   {
      link_layer_control |= SCU_SAS_LLCTL_GEN_VAL(
                               MAX_LINK_RATE,
                               MIN(
                                  SCU_SAS_LINK_LAYER_CONTROL_MAX_LINK_RATE_GEN1,
                                  COMPILED_MAX_LINK_RATE)
                            );
   }

   scu_link_layer_register_write(
      this_phy, link_layer_control, link_layer_control
   );

   phy_timer_timeout_values = scu_link_layer_register_read(
                                 this_phy,
                                 phy_timer_timeout_values
                              );

   // Clear the default 0x36 (54us) RATE_CHANGE timeout value.
   phy_timer_timeout_values &= ~SCU_SAS_PHYTOV_GEN_VAL(RATE_CHANGE, 0xFF);

   // Set RATE_CHANGE timeout value to 0x3B (59us).  This ensures SCU can
   //  lock with 3Gb drive when SCU max rate is set to 1.5Gb.
   phy_timer_timeout_values |= SCU_SAS_PHYTOV_GEN_VAL(RATE_CHANGE, 0x3B);

   scu_link_layer_register_write(
      this_phy, phy_timer_timeout_values, phy_timer_timeout_values
   );

   // Program the max ARB time for the PHY to 700us so we inter-operate with
   // the PMC expander which shuts down PHYs if the expander PHY generates too
   // many breaks.  This time value will guarantee that the initiator PHY will
   // generate the break.
#if defined(PBG_HBA_A0_BUILD) || defined(PBG_HBA_A2_BUILD)
   scu_link_layer_register_write(
      this_phy,
      maximum_arbitration_wait_timer_timeout,
      SCIC_SDS_PHY_MAX_ARBITRATION_WAIT_TIME
   );
#endif // defined(PBG_HBA_A0_BUILD) || defined(PBG_HBA_A2_BUILD)

   // Disable the link layer hang detection timer
   scu_link_layer_register_write(
      this_phy, link_layer_hang_detection_timeout, 0x00000000
   );

   // We can exit the initial state to the stopped state
   sci_base_state_machine_change_state(
      scic_sds_phy_get_base_state_machine(this_phy),
      SCI_BASE_PHY_STATE_STOPPED
   );

   return SCI_SUCCESS;
}

/**
 * This function will handle the sata SIGNATURE FIS timeout condition.  It
 * will restart the starting substate machine since we dont know what has
 * actually happening.
 *
 * @param[in] cookie This object is cast to the SCIC_SDS_PHY_T object.
 *
 * @return none
 */
void scic_sds_phy_sata_timeout( SCI_OBJECT_HANDLE_T cookie)
{
   SCIC_SDS_PHY_T * this_phy = (SCIC_SDS_PHY_T *)cookie;

   SCIC_LOG_INFO((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "SCIC SDS Phy 0x%x did not receive signature fis before timeout.\n",
      this_phy
   ));

   sci_base_state_machine_stop(
      scic_sds_phy_get_starting_substate_machine(this_phy));

   sci_base_state_machine_change_state(
      scic_sds_phy_get_base_state_machine(this_phy),
      SCI_BASE_PHY_STATE_STARTING
   );
}

//*****************************************************************************
//* SCIC SDS PHY External Methods
//*****************************************************************************

/**
 * @brief This method returns the object size for a phy object.
 *
 * @return U32
 */
U32 scic_sds_phy_get_object_size(void)
{
   return sizeof(SCIC_SDS_PHY_T);
}

/**
 * @brief This method returns the minimum number of timers required for a
 *        phy object.
 *
 * @return U32
 */
U32 scic_sds_phy_get_min_timer_count(void)
{
   return SCIC_SDS_PHY_MIN_TIMER_COUNT;
}

/**
 * @brief This method returns the maximum number of timers required for a
 *        phy object.
 *
 * @return U32
 */
U32 scic_sds_phy_get_max_timer_count(void)
{
   return SCIC_SDS_PHY_MAX_TIMER_COUNT;
}

#ifdef SCI_LOGGING
static
void scic_sds_phy_initialize_state_logging(
   SCIC_SDS_PHY_T *this_phy
)
{
   sci_base_state_machine_logger_initialize(
      &this_phy->parent.state_machine_logger,
      &this_phy->parent.state_machine,
      &this_phy->parent.parent,
      scic_cb_logger_log_states,
      "SCIC_SDS_PHY_T", "base state machine",
      SCIC_LOG_OBJECT_PHY
   );

   sci_base_state_machine_logger_initialize(
      &this_phy->starting_substate_machine_logger,
      &this_phy->starting_substate_machine,
      &this_phy->parent.parent,
      scic_cb_logger_log_states,
      "SCIC_SDS_PHY_T", "starting substate machine",
      SCIC_LOG_OBJECT_PHY
   );
}
#endif // SCI_LOGGING

#ifdef SCIC_DEBUG_ENABLED
/**
 * Debug code to record the state transitions in the phy
 *
 * @param our_observer
 * @param the_state_machine
 */
void scic_sds_phy_observe_state_change(
   SCI_BASE_OBSERVER_T * our_observer,
   SCI_BASE_SUBJECT_T  * the_subject
)
{
   SCIC_SDS_PHY_T           *this_phy;
   SCI_BASE_STATE_MACHINE_T *the_state_machine;

   U8  transition_requestor;
   U32 base_state_id;
   U32 starting_substate_id;

   the_state_machine = (SCI_BASE_STATE_MACHINE_T *)the_subject;
   this_phy = (SCIC_SDS_PHY_T *)the_state_machine->state_machine_owner;

   if (the_state_machine == &this_phy->parent.state_machine)
   {
      transition_requestor = 0x01;
   }
   else if (the_state_machine == &this_phy->starting_substate_machine)
   {
      transition_requestor = 0x02;
   }
   else
   {
      transition_requestor = 0xFF;
   }

   base_state_id =
      sci_base_state_machine_get_state(&this_phy->parent.state_machine);
   starting_substate_id =
      sci_base_state_machine_get_state(&this_phy->starting_substate_machine);

   this_phy->state_record.state_transition_table[
      this_phy->state_record.index++] = ( (transition_requestor << 24)
                                        | ((U8)base_state_id << 8)
                                        | ((U8)starting_substate_id));

   this_phy->state_record.index =
      this_phy->state_record.index & (MAX_STATE_TRANSITION_RECORD - 1);

}
#endif // SCIC_DEBUG_ENABLED

#ifdef SCIC_DEBUG_ENABLED
/**
 * This method initializes the state record debug information for the phy
 * object.
 *
 * @pre The state machines for the phy object must be constructed before this
 *      function is called.
 *
 * @param this_phy The phy which is being initialized.
 */
void scic_sds_phy_initialize_state_recording(
   SCIC_SDS_PHY_T *this_phy
)
{
   this_phy->state_record.index = 0;

   sci_base_observer_initialize(
      &this_phy->state_record.base_state_observer,
      scic_sds_phy_observe_state_change,
      &this_phy->parent.state_machine.parent
   );

   sci_base_observer_initialize(
      &this_phy->state_record.starting_state_observer,
      scic_sds_phy_observe_state_change,
      &this_phy->starting_substate_machine.parent
   );
}
#endif // SCIC_DEBUG_ENABLED

/**
 * @brief This method will construct the SCIC_SDS_PHY object
 *
 * @param[in] this_phy
 * @param[in] owning_port
 * @param[in] phy_index
 *
 * @return none
 */
void scic_sds_phy_construct(
   SCIC_SDS_PHY_T  *this_phy,
   SCIC_SDS_PORT_T *owning_port,
   U8              phy_index
)
{
   // Call the base constructor first
   // Copy the logger from the port (this could be the dummy port)
   sci_base_phy_construct(
      &this_phy->parent,
      sci_base_object_get_logger(owning_port),
      scic_sds_phy_state_table
      );

   // Copy the rest of the input data to our locals
   this_phy->owning_port = owning_port;
   this_phy->phy_index = phy_index;
   this_phy->bcn_received_while_port_unassigned = FALSE;
   this_phy->protocol = SCIC_SDS_PHY_PROTOCOL_UNKNOWN;
   this_phy->link_layer_registers = NULL;
   this_phy->max_negotiated_speed = SCI_SAS_NO_LINK_RATE;
   this_phy->sata_timeout_timer = NULL;

   // Clear out the identification buffer data
   memset(&this_phy->phy_type, 0, sizeof(this_phy->phy_type));

   // Clear out the error counter data
   memset(this_phy->error_counter, 0, sizeof(this_phy->error_counter));

   // Initialize the substate machines
   sci_base_state_machine_construct(
      &this_phy->starting_substate_machine,
      &this_phy->parent.parent,
      scic_sds_phy_starting_substates,
      SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL
      );

   #ifdef SCI_LOGGING
   scic_sds_phy_initialize_state_logging(this_phy);
   #endif // SCI_LOGGING

   #ifdef SCIC_DEBUG_ENABLED
   scic_sds_phy_initialize_state_recording(this_phy);
   #endif // SCIC_DEBUG_ENABLED
}

/**
 * @brief This method returns the port currently containing this phy.
 *        If the phy is currently contained by the dummy port, then
 *        the phy is considered to not be part of a port.
 *
 * @param[in] this_phy This parameter specifies the phy for which to
 *            retrieve the containing port.
 *
 * @return This method returns a handle to a port that contains
 *         the supplied phy.
 * @retval SCI_INVALID_HANDLE This value is returned if the phy is not
 *         part of a real port (i.e. it's contained in the dummy port).
 * @retval !SCI_INVALID_HANDLE All other values indicate a handle/pointer
 *         to the port containing the phy.
 */
SCI_PORT_HANDLE_T scic_sds_phy_get_port(
   SCIC_SDS_PHY_T *this_phy
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_phy_get_port(0x%x) enter\n",
      this_phy
   ));

   if (scic_sds_port_get_index(this_phy->owning_port) == SCIC_SDS_DUMMY_PORT)
      return SCI_INVALID_HANDLE;

   return this_phy->owning_port;
}

/**
 * @brief This method will assign a port to the phy object.
 *
 * @param[in, out] this_phy This parameter specifies the phy for which
 *    to assign a port object.
 * @param[in] the_port This parameter is the port to assing to the phy.
 */
void scic_sds_phy_set_port(
   SCIC_SDS_PHY_T * this_phy,
   SCIC_SDS_PORT_T * the_port
)
{
   this_phy->owning_port = the_port;

   if (this_phy->bcn_received_while_port_unassigned)
   {
      this_phy->bcn_received_while_port_unassigned = FALSE;
      scic_sds_port_broadcast_change_received(this_phy->owning_port, this_phy);
   }
}

/**
 * @brief This method will initialize the constructed phy
 *
 * @param[in] this_phy
 * @param[in] link_layer_registers
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_phy_initialize(
   SCIC_SDS_PHY_T             *this_phy,
   void                       *transport_layer_registers,
   SCU_LINK_LAYER_REGISTERS_T *link_layer_registers
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sds_phy_initialize(this_phy:0x%x, link_layer_registers:0x%x)\n",
      this_phy, link_layer_registers
   ));

   // Perform the initialization of the TL hardware
   scic_sds_phy_transport_layer_initialization(this_phy, transport_layer_registers);

   // Perofrm the initialization of the PE hardware
   scic_sds_phy_link_layer_initialization(this_phy, link_layer_registers);

   // There is nothing that needs to be done in this state just
   // transition to the stopped state.
   sci_base_state_machine_change_state(
      scic_sds_phy_get_base_state_machine(this_phy),
      SCI_BASE_PHY_STATE_STOPPED
   );

   return SCI_SUCCESS;
}

/**
 * This method assigns the direct attached device ID for this phy.
 *
 * @param[in] this_phy The phy for which the direct attached device id is to
 *       be assigned.
 * @param[in] device_id The direct attached device ID to assign to the phy.
 *       This will either be the RNi for the device or an invalid RNi if there
 *       is no current device assigned to the phy.
 */
void scic_sds_phy_setup_transport(
   SCIC_SDS_PHY_T * this_phy,
   U32              device_id
)
{
   U32 tl_control;

   SCU_STPTLDARNI_WRITE(this_phy, device_id);

   // The read should guarntee that the first write gets posted
   // before the next write
   tl_control = SCU_TLCR_READ(this_phy);
   tl_control |= SCU_TLCR_GEN_BIT(CLEAR_TCI_NCQ_MAPPING_TABLE);
   SCU_TLCR_WRITE(this_phy, tl_control);
}

/**
 * This function will perform the register reads/writes to suspend the SCU
 * hardware protocol engine.
 *
 * @param[in,out] this_phy The phy object to be suspended.
 *
 * @return none
 */
void scic_sds_phy_suspend(
   SCIC_SDS_PHY_T * this_phy
)
{
   U32 scu_sas_pcfg_value;

   scu_sas_pcfg_value = SCU_SAS_PCFG_READ(this_phy);
   scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(SUSPEND_PROTOCOL_ENGINE);
   SCU_SAS_PCFG_WRITE(this_phy, scu_sas_pcfg_value);

   scic_sds_phy_setup_transport(
      this_phy, SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX
   );
}

/**
 * This function will perform the register reads/writes required to resume the
 * SCU hardware protocol engine.
 *
 * @param[in,out] this_phy The phy object to resume.
 *
 * @return none
 */
void scic_sds_phy_resume(
   SCIC_SDS_PHY_T * this_phy
)
{
   U32 scu_sas_pcfg_value;

   scu_sas_pcfg_value = SCU_SAS_PCFG_READ(this_phy);

   scu_sas_pcfg_value &= ~SCU_SAS_PCFG_GEN_BIT(SUSPEND_PROTOCOL_ENGINE);

   SCU_SAS_PCFG_WRITE(this_phy, scu_sas_pcfg_value);
}

/**
 * @brief This method returns the local sas address assigned to this phy.
 *
 * @param[in] this_phy This parameter specifies the phy for which
 *            to retrieve the local SAS address.
 * @param[out] sas_address This parameter specifies the location into
 *             which to copy the local SAS address.
 *
 * @return none
 */
void scic_sds_phy_get_sas_address(
   SCIC_SDS_PHY_T *this_phy,
   SCI_SAS_ADDRESS_T *sas_address
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sds_phy_get_sas_address(this_phy:0x%x, sas_address:0x%x)\n",
      this_phy, sas_address
   ));

   sas_address->high = SCU_SAS_TISSAH_READ(this_phy);
   sas_address->low  = SCU_SAS_TISSAL_READ(this_phy);
}

/**
 * @brief This method returns the remote end-point (i.e. attached)
 *        sas address assigned to this phy.
 *
 * @param[in] this_phy This parameter specifies the phy for which
 *            to retrieve the remote end-point SAS address.
 * @param[out] sas_address This parameter specifies the location into
 *             which to copy the remote end-point SAS address.
 *
 * @return none
 */
void scic_sds_phy_get_attached_sas_address(
   SCIC_SDS_PHY_T    *this_phy,
   SCI_SAS_ADDRESS_T *sas_address
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sds_phy_get_attached_sas_address(0x%x, 0x%x) enter\n",
      this_phy, sas_address
   ));

   sas_address->high
      = this_phy->phy_type.sas.identify_address_frame_buffer.sas_address.high;
   sas_address->low
      = this_phy->phy_type.sas.identify_address_frame_buffer.sas_address.low;
}

/**
 * @brief This method returns the supported protocols assigned to
 *        this phy
 *
 * @param[in] this_phy
 * @param[out] protocols
 */
void scic_sds_phy_get_protocols(
   SCIC_SDS_PHY_T *this_phy,
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T * protocols
)
{
   U32 tiid_value = SCU_SAS_TIID_READ(this_phy);

   //Check each bit of this register. please refer to
   //SAS Transmit Identification Register (SAS_TIID).
   if (tiid_value & 0x2)
      protocols->u.bits.smp_target = 1;

   if (tiid_value & 0x4)
      protocols->u.bits.stp_target = 1;

   if (tiid_value & 0x8)
      protocols->u.bits.ssp_target = 1;

   if (tiid_value & 0x200)
      protocols->u.bits.smp_initiator = 1;

   if ((tiid_value & 0x400))
      protocols->u.bits.stp_initiator = 1;

   if (tiid_value & 0x800)
      protocols->u.bits.ssp_initiator = 1;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sds_phy_get_protocols(this_phy:0x%x, protocols:0x%x)\n",
      this_phy, protocols->u.all
   ));
}

/**
 * This method returns the supported protocols for the attached phy.  If this
 * is a SAS phy the protocols are returned from the identify address frame.
 * If this is a SATA phy then protocols are made up and the target phy is an
 * STP target phy.
 *
 * @note The caller will get the entire set of bits for the protocol value.
 *
 * @param[in] this_phy The parameter is the phy object for which the attached
 *       phy protcols are to be returned.
 * @param[out] protocols The parameter is the returned protocols for the
 *       attached phy.
 */
void scic_sds_phy_get_attached_phy_protocols(
   SCIC_SDS_PHY_T *this_phy,
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T * protocols
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sds_phy_get_attached_phy_protocols(this_phy:0x%x, protocols:0x%x[0x%x])\n",
      this_phy, protocols, protocols->u.all
   ));

   protocols->u.all = 0;

   if (this_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SAS)
   {
      protocols->u.all =
         this_phy->phy_type.sas.identify_address_frame_buffer.protocols.u.all;
   }
   else if (this_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SATA)
   {
      protocols->u.bits.stp_target = 1;
   }
}


/**
 * @brief This method release resources in for a scic phy.
 *
 * @param[in] controller This parameter specifies the core controller, one of
 *            its phy's resources are to be released.
 * @param[in] this_phy This parameter specifies the phy whose resource is to
 *            be released.
 */
void scic_sds_phy_release_resource(
   SCIC_SDS_CONTROLLER_T * controller,
   SCIC_SDS_PHY_T        * this_phy
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sds_phy_release_resource(0x%x, 0x%x)\n",
      controller, this_phy
   ));

   //Currently, the only resource to be released is a timer.
   if (this_phy->sata_timeout_timer != NULL)
   {
      scic_cb_timer_destroy(controller, this_phy->sata_timeout_timer);
      this_phy->sata_timeout_timer = NULL;
   }
}


//*****************************************************************************
//* SCIC SDS PHY Handler Redirects
//*****************************************************************************

/**
 * @brief This method will attempt to reset the phy.  This
 *        request is only valid when the phy is in an ready
 *        state
 *
 * @param[in] this_phy
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_phy_reset(
   SCIC_SDS_PHY_T * this_phy
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sds_phy_reset(this_phy:0x%08x)\n",
      this_phy
   ));

   return this_phy->state_handlers->parent.reset_handler(
                                             &this_phy->parent
                                           );
}

/**
 * @brief This method will process the event code received.
 *
 * @param[in] this_phy
 * @param[in] event_code
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_phy_event_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32 event_code
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sds_phy_event_handler(this_phy:0x%08x, event_code:%x)\n",
      this_phy, event_code
   ));

   return this_phy->state_handlers->event_handler(this_phy, event_code);
}

/**
 * @brief This method will process the frame index received.
 *
 * @param[in] this_phy
 * @param[in] frame_index
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_phy_frame_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32 frame_index
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sds_phy_frame_handler(this_phy:0x%08x, frame_index:%d)\n",
      this_phy, frame_index
   ));

   return this_phy->state_handlers->frame_handler(this_phy, frame_index);
}

/**
 * @brief This method will give the phy permission to consume power
 *
 * @param[in] this_phy
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_phy_consume_power_handler(
   SCIC_SDS_PHY_T *this_phy
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sds_phy_consume_power_handler(this_phy:0x%08x)\n",
      this_phy
   ));

   return this_phy->state_handlers->consume_power_handler(this_phy);
}

//*****************************************************************************
//* SCIC PHY Public Methods
//*****************************************************************************

SCI_STATUS scic_phy_get_properties(
   SCI_PHY_HANDLE_T        phy,
   SCIC_PHY_PROPERTIES_T * properties
)
{
   SCIC_SDS_PHY_T *this_phy;
   U8 max_speed_generation;

   this_phy = (SCIC_SDS_PHY_T *)phy;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_phy_get_properties(0x%x, 0x%x) enter\n",
      this_phy, properties
   ));

   if (phy == SCI_INVALID_HANDLE)
   {
      return SCI_FAILURE_INVALID_PHY;
   }

   memset(properties, 0, sizeof(SCIC_PHY_PROPERTIES_T));

   //get max link rate of this phy set by user.
   max_speed_generation =
      this_phy->owning_port->owning_controller->user_parameters.sds1.
         phys[this_phy->phy_index].max_speed_generation;

   properties->negotiated_link_rate     = this_phy->max_negotiated_speed;

   if (max_speed_generation == SCIC_SDS_PARM_GEN3_SPEED)
      properties->max_link_rate            = SCI_SAS_600_GB;
   else if (max_speed_generation == SCIC_SDS_PARM_GEN2_SPEED)
      properties->max_link_rate            = SCI_SAS_300_GB;
   else
      properties->max_link_rate            = SCI_SAS_150_GB;

   properties->index                    = this_phy->phy_index;
   properties->owning_port              = scic_sds_phy_get_port(this_phy);

   scic_sds_phy_get_protocols(this_phy, &properties->transmit_iaf.protocols);

   properties->transmit_iaf.sas_address.high =
      this_phy->owning_port->owning_controller->oem_parameters.sds1.
         phys[this_phy->phy_index].sas_address.sci_format.high;

   properties->transmit_iaf.sas_address.low =
      this_phy->owning_port->owning_controller->oem_parameters.sds1.
         phys[this_phy->phy_index].sas_address.sci_format.low;

   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_sas_phy_get_properties(
   SCI_PHY_HANDLE_T            phy,
   SCIC_SAS_PHY_PROPERTIES_T * properties
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sas_phy_get_properties(0x%x, 0x%x) enter\n",
      this_phy, properties
   ));

   if (this_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SAS)
   {
      memcpy(
         &properties->received_iaf,
         &this_phy->phy_type.sas.identify_address_frame_buffer,
         sizeof(SCI_SAS_IDENTIFY_ADDRESS_FRAME_T)
      );

      properties->received_capabilities.u.all
         = SCU_SAS_RECPHYCAP_READ(this_phy);

      return SCI_SUCCESS;
   }

   return SCI_FAILURE;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_sata_phy_get_properties(
   SCI_PHY_HANDLE_T             phy,
   SCIC_SATA_PHY_PROPERTIES_T * properties
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sata_phy_get_properties(0x%x, 0x%x) enter\n",
      this_phy, properties
   ));

   if (this_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SATA)
   {
      memcpy(
         &properties->signature_fis,
         &this_phy->phy_type.sata.signature_fis_buffer,
         sizeof(SATA_FIS_REG_D2H_T)
      );

      /// @todo add support for port selectors.
      properties->is_port_selector_present = FALSE;

      return SCI_SUCCESS;
   }

   return SCI_FAILURE;
}

// ---------------------------------------------------------------------------

#if !defined(DISABLE_PORT_SELECTORS)

SCI_STATUS scic_sata_phy_send_port_selection_signal(
   SCI_PHY_HANDLE_T  phy
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_sata_phy_send_port_selection_signals(0x%x) enter\n",
      this_phy
   ));

   /// @todo To be implemented
   ASSERT(FALSE);
   return SCI_FAILURE;
}

#endif // !defined(DISABLE_PORT_SELECTORS)

// ---------------------------------------------------------------------------

#if !defined(DISABLE_PHY_COUNTERS)

SCI_STATUS scic_phy_enable_counter(
   SCI_PHY_HANDLE_T       phy,
   SCIC_PHY_COUNTER_ID_T  counter_id
)
{
   SCIC_SDS_PHY_T *this_phy;
   SCI_STATUS status = SCI_SUCCESS;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_phy_enable_counter(0x%x, 0x%x) enter\n",
      this_phy, counter_id
   ));

   switch(counter_id)
   {
      case SCIC_PHY_COUNTER_RECEIVED_DONE_ACK_NAK_TIMEOUT:
         {
            U32 control = SCU_SAS_ECENCR_READ(this_phy);
            control |= (1 << SCU_ERR_CNT_RX_DONE_ACK_NAK_TIMEOUT_INDEX);
            SCU_SAS_ECENCR_WRITE(this_phy, control);
         }
         break;
      case SCIC_PHY_COUNTER_TRANSMITTED_DONE_ACK_NAK_TIMEOUT:
         {
            U32 control = SCU_SAS_ECENCR_READ(this_phy);
            control |= (1 << SCU_ERR_CNT_TX_DONE_ACK_NAK_TIMEOUT_INDEX);
            SCU_SAS_ECENCR_WRITE(this_phy, control);
         }
         break;
      case SCIC_PHY_COUNTER_INACTIVITY_TIMER_EXPIRED:
         {
            U32 control = SCU_SAS_ECENCR_READ(this_phy);
            control |= (1 << SCU_ERR_CNT_INACTIVITY_TIMER_EXPIRED_INDEX);
            SCU_SAS_ECENCR_WRITE(this_phy, control);
         }
         break;
      case SCIC_PHY_COUNTER_RECEIVED_DONE_CREDIT_TIMEOUT:
         {
            U32 control = SCU_SAS_ECENCR_READ(this_phy);
            control |= (1 << SCU_ERR_CNT_RX_DONE_CREDIT_TIMEOUT_INDEX);
            SCU_SAS_ECENCR_WRITE(this_phy, control);
         }
         break;
      case SCIC_PHY_COUNTER_TRANSMITTED_DONE_CREDIT_TIMEOUT:
         {
            U32 control = SCU_SAS_ECENCR_READ(this_phy);
            control |= (1 << SCU_ERR_CNT_TX_DONE_CREDIT_TIMEOUT_INDEX);
            SCU_SAS_ECENCR_WRITE(this_phy, control);
         }
         break;
      case SCIC_PHY_COUNTER_RECEIVED_CREDIT_BLOCKED:
         {
            U32 control = SCU_SAS_ECENCR_READ(this_phy);
            control |= (1 << SCU_ERR_CNT_RX_CREDIT_BLOCKED_RECEIVED_INDEX);
            SCU_SAS_ECENCR_WRITE(this_phy, control);
         }
         break;

         // These error counters are enabled by default, and cannot be
         //  disabled.  Return SCI_SUCCESS to denote that they are
         //  enabled, hiding the fact that enabling the counter is
         //  a no-op.
      case SCIC_PHY_COUNTER_RECEIVED_FRAME:
      case SCIC_PHY_COUNTER_TRANSMITTED_FRAME:
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_DWORD:
      case SCIC_PHY_COUNTER_TRANSMITTED_FRAME_DWORD:
      case SCIC_PHY_COUNTER_LOSS_OF_SYNC_ERROR:
      case SCIC_PHY_COUNTER_RECEIVED_DISPARITY_ERROR:
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_CRC_ERROR:
      case SCIC_PHY_COUNTER_RECEIVED_SHORT_FRAME:
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_WITHOUT_CREDIT:
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_AFTER_DONE:
      case SCIC_PHY_COUNTER_SN_DWORD_SYNC_ERROR:
         break;

      default:
         status = SCI_FAILURE;
         break;
   }
   return status;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_phy_disable_counter(
   SCI_PHY_HANDLE_T       phy,
   SCIC_PHY_COUNTER_ID_T  counter_id
)
{
   SCIC_SDS_PHY_T *this_phy;
   SCI_STATUS status = SCI_SUCCESS;

   this_phy = (SCIC_SDS_PHY_T *)phy;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_phy_disable_counter(0x%x, 0x%x) enter\n",
      this_phy, counter_id
   ));

   switch(counter_id)
   {
      case SCIC_PHY_COUNTER_RECEIVED_DONE_ACK_NAK_TIMEOUT:
         {
            U32 control = SCU_SAS_ECENCR_READ(this_phy);
            control &= ~(1 << SCU_ERR_CNT_RX_DONE_ACK_NAK_TIMEOUT_INDEX);
            SCU_SAS_ECENCR_WRITE(this_phy, control);
         }
         break;
      case SCIC_PHY_COUNTER_TRANSMITTED_DONE_ACK_NAK_TIMEOUT:
         {
            U32 control = SCU_SAS_ECENCR_READ(this_phy);
            control &= ~(1 << SCU_ERR_CNT_TX_DONE_ACK_NAK_TIMEOUT_INDEX);
            SCU_SAS_ECENCR_WRITE(this_phy, control);
         }
         break;
      case SCIC_PHY_COUNTER_INACTIVITY_TIMER_EXPIRED:
         {
            U32 control = SCU_SAS_ECENCR_READ(this_phy);
            control &= ~(1 << SCU_ERR_CNT_INACTIVITY_TIMER_EXPIRED_INDEX);
            SCU_SAS_ECENCR_WRITE(this_phy, control);
         }
         break;
      case SCIC_PHY_COUNTER_RECEIVED_DONE_CREDIT_TIMEOUT:
         {
            U32 control = SCU_SAS_ECENCR_READ(this_phy);
            control &= ~(1 << SCU_ERR_CNT_RX_DONE_CREDIT_TIMEOUT_INDEX);
            SCU_SAS_ECENCR_WRITE(this_phy, control);
         }
         break;
      case SCIC_PHY_COUNTER_TRANSMITTED_DONE_CREDIT_TIMEOUT:
         {
            U32 control = SCU_SAS_ECENCR_READ(this_phy);
            control &= ~(1 << SCU_ERR_CNT_TX_DONE_CREDIT_TIMEOUT_INDEX);
            SCU_SAS_ECENCR_WRITE(this_phy, control);
         }
         break;
      case SCIC_PHY_COUNTER_RECEIVED_CREDIT_BLOCKED:
         {
            U32 control = SCU_SAS_ECENCR_READ(this_phy);
            control &= ~(1 << SCU_ERR_CNT_RX_CREDIT_BLOCKED_RECEIVED_INDEX);
            SCU_SAS_ECENCR_WRITE(this_phy, control);
         }
         break;

         // These error counters cannot be disabled, so return SCI_FAILURE.
      case SCIC_PHY_COUNTER_RECEIVED_FRAME:
      case SCIC_PHY_COUNTER_TRANSMITTED_FRAME:
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_DWORD:
      case SCIC_PHY_COUNTER_TRANSMITTED_FRAME_DWORD:
      case SCIC_PHY_COUNTER_LOSS_OF_SYNC_ERROR:
      case SCIC_PHY_COUNTER_RECEIVED_DISPARITY_ERROR:
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_CRC_ERROR:
      case SCIC_PHY_COUNTER_RECEIVED_SHORT_FRAME:
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_WITHOUT_CREDIT:
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_AFTER_DONE:
      case SCIC_PHY_COUNTER_SN_DWORD_SYNC_ERROR:
      default:
         status = SCI_FAILURE;
         break;
   }
   return status;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_phy_get_counter(
   SCI_PHY_HANDLE_T        phy,
   SCIC_PHY_COUNTER_ID_T   counter_id,
   U32                   * data
)
{
   SCIC_SDS_PHY_T *this_phy;
   SCI_STATUS status = SCI_SUCCESS;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_phy_get_counter(0x%x, 0x%x) enter\n",
      this_phy, counter_id
   ));

   switch(counter_id)
   {
      case SCIC_PHY_COUNTER_RECEIVED_FRAME:
         *data = scu_link_layer_register_read(this_phy, received_frame_count);
         break;
      case SCIC_PHY_COUNTER_TRANSMITTED_FRAME:
         *data = scu_link_layer_register_read(this_phy, transmit_frame_count);
         break;
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_DWORD:
         *data = scu_link_layer_register_read(this_phy, received_dword_count);
         break;
      case SCIC_PHY_COUNTER_TRANSMITTED_FRAME_DWORD:
         *data = scu_link_layer_register_read(this_phy, transmit_dword_count);
         break;
      case SCIC_PHY_COUNTER_LOSS_OF_SYNC_ERROR:
         *data = scu_link_layer_register_read(this_phy, loss_of_sync_error_count);
         break;
      case SCIC_PHY_COUNTER_RECEIVED_DISPARITY_ERROR:
         *data = scu_link_layer_register_read(this_phy, running_disparity_error_count);
         break;
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_CRC_ERROR:
         *data = scu_link_layer_register_read(this_phy, received_frame_crc_error_count);
         break;
      case SCIC_PHY_COUNTER_RECEIVED_DONE_ACK_NAK_TIMEOUT:
         *data = this_phy->error_counter[SCU_ERR_CNT_RX_DONE_ACK_NAK_TIMEOUT_INDEX];
         break;
      case SCIC_PHY_COUNTER_TRANSMITTED_DONE_ACK_NAK_TIMEOUT:
         *data = this_phy->error_counter[SCU_ERR_CNT_TX_DONE_ACK_NAK_TIMEOUT_INDEX];
         break;
      case SCIC_PHY_COUNTER_INACTIVITY_TIMER_EXPIRED:
         *data = this_phy->error_counter[SCU_ERR_CNT_INACTIVITY_TIMER_EXPIRED_INDEX];
         break;
      case SCIC_PHY_COUNTER_RECEIVED_DONE_CREDIT_TIMEOUT:
         *data = this_phy->error_counter[SCU_ERR_CNT_RX_DONE_CREDIT_TIMEOUT_INDEX];
         break;
      case SCIC_PHY_COUNTER_TRANSMITTED_DONE_CREDIT_TIMEOUT:
         *data = this_phy->error_counter[SCU_ERR_CNT_TX_DONE_CREDIT_TIMEOUT_INDEX];
         break;
      case SCIC_PHY_COUNTER_RECEIVED_CREDIT_BLOCKED:
         *data = this_phy->error_counter[SCU_ERR_CNT_RX_CREDIT_BLOCKED_RECEIVED_INDEX];
         break;
      case SCIC_PHY_COUNTER_RECEIVED_SHORT_FRAME:
         *data = scu_link_layer_register_read(this_phy, received_short_frame_count);
         break;
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_WITHOUT_CREDIT:
         *data = scu_link_layer_register_read(this_phy, received_frame_without_credit_count);
         break;
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_AFTER_DONE:
         *data = scu_link_layer_register_read(this_phy, received_frame_after_done_count);
         break;
      case SCIC_PHY_COUNTER_SN_DWORD_SYNC_ERROR:
         *data = scu_link_layer_register_read(this_phy, phy_reset_problem_count);
         break;
      default:
         status = SCI_FAILURE;
         break;
   }

   return status;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_phy_clear_counter(
   SCI_PHY_HANDLE_T       phy,
   SCIC_PHY_COUNTER_ID_T  counter_id
)
{
   SCIC_SDS_PHY_T *this_phy;
   SCI_STATUS status = SCI_SUCCESS;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_phy_clear_counter(0x%x, 0x%x) enter\n",
      this_phy, counter_id
   ));

   switch(counter_id)
   {
      case SCIC_PHY_COUNTER_RECEIVED_FRAME:
         scu_link_layer_register_write(this_phy, received_frame_count, 0);
         break;
      case SCIC_PHY_COUNTER_TRANSMITTED_FRAME:
         scu_link_layer_register_write(this_phy, transmit_frame_count, 0);
         break;
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_DWORD:
         scu_link_layer_register_write(this_phy, received_dword_count, 0);
         break;
      case SCIC_PHY_COUNTER_TRANSMITTED_FRAME_DWORD:
         scu_link_layer_register_write(this_phy, transmit_dword_count, 0);
         break;
      case SCIC_PHY_COUNTER_LOSS_OF_SYNC_ERROR:
         scu_link_layer_register_write(this_phy, loss_of_sync_error_count, 0);
         break;
      case SCIC_PHY_COUNTER_RECEIVED_DISPARITY_ERROR:
         scu_link_layer_register_write(this_phy, running_disparity_error_count, 0);
         break;
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_CRC_ERROR:
         scu_link_layer_register_write(this_phy, received_frame_crc_error_count, 0);
         break;
      case SCIC_PHY_COUNTER_RECEIVED_DONE_ACK_NAK_TIMEOUT:
         this_phy->error_counter[SCU_ERR_CNT_RX_DONE_ACK_NAK_TIMEOUT_INDEX] = 0;
         break;
      case SCIC_PHY_COUNTER_TRANSMITTED_DONE_ACK_NAK_TIMEOUT:
         this_phy->error_counter[SCU_ERR_CNT_TX_DONE_ACK_NAK_TIMEOUT_INDEX] = 0;
         break;
      case SCIC_PHY_COUNTER_INACTIVITY_TIMER_EXPIRED:
         this_phy->error_counter[SCU_ERR_CNT_INACTIVITY_TIMER_EXPIRED_INDEX] = 0;
         break;
      case SCIC_PHY_COUNTER_RECEIVED_DONE_CREDIT_TIMEOUT:
         this_phy->error_counter[SCU_ERR_CNT_RX_DONE_CREDIT_TIMEOUT_INDEX] = 0;
         break;
      case SCIC_PHY_COUNTER_TRANSMITTED_DONE_CREDIT_TIMEOUT:
         this_phy->error_counter[SCU_ERR_CNT_TX_DONE_CREDIT_TIMEOUT_INDEX] = 0;
         break;
      case SCIC_PHY_COUNTER_RECEIVED_CREDIT_BLOCKED:
         this_phy->error_counter[SCU_ERR_CNT_RX_CREDIT_BLOCKED_RECEIVED_INDEX] = 0;
         break;
      case SCIC_PHY_COUNTER_RECEIVED_SHORT_FRAME:
         scu_link_layer_register_write(this_phy, received_short_frame_count, 0);
         break;
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_WITHOUT_CREDIT:
         scu_link_layer_register_write(this_phy, received_frame_without_credit_count, 0);
         break;
      case SCIC_PHY_COUNTER_RECEIVED_FRAME_AFTER_DONE:
         scu_link_layer_register_write(this_phy, received_frame_after_done_count, 0);
         break;
      case SCIC_PHY_COUNTER_SN_DWORD_SYNC_ERROR:
         scu_link_layer_register_write(this_phy, phy_reset_problem_count, 0);
         break;
      default:
         status = SCI_FAILURE;
   }

   return status;
}

#endif // !defined(DISABLE_PHY_COUNTERS)

SCI_STATUS scic_phy_stop(
   SCI_PHY_HANDLE_T       phy
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_phy_stop(this_phy:0x%x)\n",
      this_phy
   ));

   return this_phy->state_handlers->parent.stop_handler(&this_phy->parent);
}

SCI_STATUS scic_phy_start(
   SCI_PHY_HANDLE_T       phy
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "scic_phy_start(this_phy:0x%x)\n",
      this_phy
   ));

   return this_phy->state_handlers->parent.start_handler(&this_phy->parent);
}

//******************************************************************************
//* PHY STATE MACHINE
//******************************************************************************

//***************************************************************************
//*  DEFAULT HANDLERS
//***************************************************************************

/**
 * This is the default method for phy a start request.  It will report a
 * warning and exit.
 *
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_phy_default_start_handler(
   SCI_BASE_PHY_T *phy
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "SCIC Phy 0x%08x requested to start from invalid state %d\n",
      this_phy,
      sci_base_state_machine_get_state(&this_phy->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;

}

/**
 * This is the default method for phy a stop request.  It will report a
 * warning and exit.
 *
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_phy_default_stop_handler(
   SCI_BASE_PHY_T *phy
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "SCIC Phy 0x%08x requested to stop from invalid state %d\n",
      this_phy,
      sci_base_state_machine_get_state(&this_phy->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This is the default method for phy a reset request.  It will report a
 * warning and exit.
 *
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_phy_default_reset_handler(
   SCI_BASE_PHY_T * phy
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "SCIC Phy 0x%08x requested to reset from invalid state %d\n",
      this_phy,
      sci_base_state_machine_get_state(&this_phy->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This is the default method for phy a destruct request.  It will report a
 * warning and exit.
 *
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_phy_default_destroy_handler(
   SCI_BASE_PHY_T *phy
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   /// @todo Implement something for the default
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "SCIC Phy 0x%08x requested to destroy from invalid state %d\n",
      this_phy,
      sci_base_state_machine_get_state(&this_phy->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This is the default method for a phy frame handling request.  It will
 * report a warning, release the frame and exit.
 *
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 * @param[in] frame_index This is the frame index that was received from the
 *       SCU hardware.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_phy_default_frame_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32            frame_index
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "SCIC Phy 0x%08x received unexpected frame data %d while in state %d\n",
      this_phy, frame_index,
      sci_base_state_machine_get_state(&this_phy->parent.state_machine)
   ));

   scic_sds_controller_release_frame(
      scic_sds_phy_get_controller(this_phy), frame_index);

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This is the default method for a phy event handler.  It will report a
 * warning and exit.
 *
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 * @param[in] event_code This is the event code that was received from the SCU
 *       hardware.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_phy_default_event_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32            event_code
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "SCIC Phy 0x%08x received unexpected event status %x while in state %d\n",
      this_phy, event_code,
      sci_base_state_machine_get_state(&this_phy->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This is the default method for a phy consume power handler.  It will report
 * a warning and exit.
 *
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_phy_default_consume_power_handler(
   SCIC_SDS_PHY_T *this_phy
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_phy),
      SCIC_LOG_OBJECT_PHY,
      "SCIC Phy 0x%08x given unexpected permission to consume power while in state %d\n",
      this_phy,
      sci_base_state_machine_get_state(&this_phy->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

//******************************************************************************
//* PHY STOPPED STATE HANDLERS
//******************************************************************************

/**
 * This method takes the SCIC_SDS_PHY from a stopped state and attempts to
 * start it.
 *    - The phy state machine is transitioned to the
 *      SCI_BASE_PHY_STATE_STARTING.
 *
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_phy_stopped_state_start_handler(
   SCI_BASE_PHY_T *phy
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)phy;



   // Create the SIGNATURE FIS Timeout timer for this phy
   this_phy->sata_timeout_timer = scic_cb_timer_create(
      scic_sds_phy_get_controller(this_phy),
      scic_sds_phy_sata_timeout,
      this_phy
   );

   if (this_phy->sata_timeout_timer != NULL)
   {
      sci_base_state_machine_change_state(
         scic_sds_phy_get_base_state_machine(this_phy),
         SCI_BASE_PHY_STATE_STARTING
      );
   }

   return SCI_SUCCESS;
}

/**
 * This method takes the SCIC_SDS_PHY from a stopped state and destroys it.
 *    - This function takes no action.
 *
 * @todo Shouldn't this function transition the SCI_BASE_PHY::state_machine to
 *        the SCI_BASE_PHY_STATE_FINAL?
 *
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_phy_stopped_state_destroy_handler(
   SCI_BASE_PHY_T *phy
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   /// @todo what do we actually need to do here?
   return SCI_SUCCESS;
}

//******************************************************************************
//* PHY STARTING STATE HANDLERS
//******************************************************************************

// All of these state handlers are mapped to the starting sub-state machine

//******************************************************************************
//* PHY READY STATE HANDLERS
//******************************************************************************

/**
 * This method takes the SCIC_SDS_PHY from a ready state and attempts to stop
 * it.
 *    - The phy state machine is transitioned to the SCI_BASE_PHY_STATE_STOPPED.
 *
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_phy_ready_state_stop_handler(
   SCI_BASE_PHY_T *phy
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   sci_base_state_machine_change_state(
      scic_sds_phy_get_base_state_machine(this_phy),
      SCI_BASE_PHY_STATE_STOPPED
   );

   scic_sds_controller_link_down(
      scic_sds_phy_get_controller(this_phy),
      scic_sds_phy_get_port(this_phy),
      this_phy
   );

   return SCI_SUCCESS;
}

/**
 * This method takes the SCIC_SDS_PHY from a ready state and attempts to reset
 * it.
 *    - The phy state machine is transitioned to the SCI_BASE_PHY_STATE_STARTING.
 *
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_phy_ready_state_reset_handler(
   SCI_BASE_PHY_T * phy
)
{
   SCIC_SDS_PHY_T * this_phy;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   sci_base_state_machine_change_state(
      scic_sds_phy_get_base_state_machine(this_phy),
      SCI_BASE_PHY_STATE_RESETTING
   );

   return SCI_SUCCESS;
}

/**
 * This method request the SCIC_SDS_PHY handle the received event.  The only
 * event that we are interested in while in the ready state is the link
 * failure event.
 *    - decoded event is a link failure
 *       - transition the SCIC_SDS_PHY back to the SCI_BASE_PHY_STATE_STARTING
 *         state.
 *    - any other event received will report a warning message
 *
 * @param[in] phy This is the SCIC_SDS_PHY object which has received the
 *       event.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS if the event received is a link failure
 * @retval SCI_FAILURE_INVALID_STATE for any other event received.
 */
static
SCI_STATUS scic_sds_phy_ready_state_event_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32            event_code
)
{
   SCI_STATUS result = SCI_FAILURE;

   switch (scu_get_event_code(event_code))
   {
   case SCU_EVENT_LINK_FAILURE:
      // Link failure change state back to the starting state
      sci_base_state_machine_change_state(
         scic_sds_phy_get_base_state_machine(this_phy),
         SCI_BASE_PHY_STATE_STARTING
         );

      result = SCI_SUCCESS;
      break;

   case SCU_EVENT_BROADCAST_CHANGE:
      // Broadcast change received. Notify the port.
      if (scic_sds_phy_get_port(this_phy) != SCI_INVALID_HANDLE)
         scic_sds_port_broadcast_change_received(this_phy->owning_port, this_phy);
      else
         this_phy->bcn_received_while_port_unassigned = TRUE;
      break;

   case SCU_EVENT_ERR_CNT(RX_CREDIT_BLOCKED_RECEIVED):
   case SCU_EVENT_ERR_CNT(TX_DONE_CREDIT_TIMEOUT):
   case SCU_EVENT_ERR_CNT(RX_DONE_CREDIT_TIMEOUT):
   case SCU_EVENT_ERR_CNT(INACTIVITY_TIMER_EXPIRED):
   case SCU_EVENT_ERR_CNT(TX_DONE_ACK_NAK_TIMEOUT):
   case SCU_EVENT_ERR_CNT(RX_DONE_ACK_NAK_TIMEOUT):
      {
         U32 error_counter_index =
                scu_get_event_specifier(event_code) >> SCU_EVENT_SPECIFIC_CODE_SHIFT;

         this_phy->error_counter[error_counter_index]++;
         result = SCI_SUCCESS;
      }
      break;

   default:
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_phy),
         SCIC_LOG_OBJECT_PHY | SCIC_LOG_OBJECT_RECEIVED_EVENTS,
         "SCIC PHY 0x%x ready state machine received unexpected event_code %x\n",
         this_phy, event_code
      ));
      result = SCI_FAILURE_INVALID_STATE;
      break;
   }

   return result;
}

// ---------------------------------------------------------------------------

/**
 * This is the resetting state event handler.
 *
 * @param[in] this_phy This is the SCIC_SDS_PHY object which is receiving the
 *       event.
 * @param[in] event_code This is the event code to be processed.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
static
SCI_STATUS scic_sds_phy_resetting_state_event_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32            event_code
)
{
   SCI_STATUS result = SCI_FAILURE;

   switch (scu_get_event_code(event_code))
   {
   case SCU_EVENT_HARD_RESET_TRANSMITTED:
      // Link failure change state back to the starting state
      sci_base_state_machine_change_state(
         scic_sds_phy_get_base_state_machine(this_phy),
         SCI_BASE_PHY_STATE_STARTING
         );

      result = SCI_SUCCESS;
      break;

   default:
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_phy),
         SCIC_LOG_OBJECT_PHY | SCIC_LOG_OBJECT_RECEIVED_EVENTS,
         "SCIC PHY 0x%x resetting state machine received unexpected event_code %x\n",
         this_phy, event_code
      ));

      result = SCI_FAILURE_INVALID_STATE;
      break;
   }

   return result;
}

// ---------------------------------------------------------------------------

SCIC_SDS_PHY_STATE_HANDLER_T
   scic_sds_phy_state_handler_table[SCI_BASE_PHY_MAX_STATES] =
{
   // SCI_BASE_PHY_STATE_INITIAL
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_default_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_default_frame_handler,
      scic_sds_phy_default_event_handler,
      scic_sds_phy_default_consume_power_handler
   },
   // SCI_BASE_PHY_STATE_STOPPED
   {
      {
         scic_sds_phy_stopped_state_start_handler,
         scic_sds_phy_default_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_stopped_state_destroy_handler
      },
      scic_sds_phy_default_frame_handler,
      scic_sds_phy_default_event_handler,
      scic_sds_phy_default_consume_power_handler
   },
   // SCI_BASE_PHY_STATE_STARTING
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_default_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_default_frame_handler,
      scic_sds_phy_default_event_handler,
      scic_sds_phy_default_consume_power_handler
   },
   // SCI_BASE_PHY_STATE_READY
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_ready_state_stop_handler,
         scic_sds_phy_ready_state_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_default_frame_handler,
      scic_sds_phy_ready_state_event_handler,
      scic_sds_phy_default_consume_power_handler
   },
   // SCI_BASE_PHY_STATE_RESETTING
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_default_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_default_frame_handler,
      scic_sds_phy_resetting_state_event_handler,
      scic_sds_phy_default_consume_power_handler
   },
   // SCI_BASE_PHY_STATE_FINAL
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_default_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_default_frame_handler,
      scic_sds_phy_default_event_handler,
      scic_sds_phy_default_consume_power_handler
   }
};

//****************************************************************************
//*  PHY STATE PRIVATE METHODS
//****************************************************************************

/**
 * This method will stop the SCIC_SDS_PHY object. This does not reset the
 * protocol engine it just suspends it and places it in a state where it will
 * not cause the end device to power up.
 *
 * @param[in] this_phy This is the SCIC_SDS_PHY object to stop.
 *
 * @return none
 */
static
void scu_link_layer_stop_protocol_engine(
   SCIC_SDS_PHY_T *this_phy
)
{
   U32 scu_sas_pcfg_value;
   U32 enable_spinup_value;

   // Suspend the protocol engine and place it in a sata spinup hold state
   scu_sas_pcfg_value  = SCU_SAS_PCFG_READ(this_phy);
   scu_sas_pcfg_value |= (
                           SCU_SAS_PCFG_GEN_BIT(OOB_RESET)
                         | SCU_SAS_PCFG_GEN_BIT(SUSPEND_PROTOCOL_ENGINE)
                         | SCU_SAS_PCFG_GEN_BIT(SATA_SPINUP_HOLD)
                         );
   SCU_SAS_PCFG_WRITE(this_phy, scu_sas_pcfg_value);

   // Disable the notify enable spinup primitives
   enable_spinup_value = SCU_SAS_ENSPINUP_READ(this_phy);
   enable_spinup_value &= ~SCU_ENSPINUP_GEN_BIT(ENABLE);
   SCU_SAS_ENSPINUP_WRITE(this_phy, enable_spinup_value);
}

/**
 * This method will start the OOB/SN state machine for this SCIC_SDS_PHY
 * object.
 *
 * @param[in] this_phy This is the SCIC_SDS_PHY object on which to start the
 *       OOB/SN state machine.
 */
static
void scu_link_layer_start_oob(
   SCIC_SDS_PHY_T *this_phy
)
{
   U32 scu_sas_pcfg_value;

   /* Reset OOB sequence - start */
   scu_sas_pcfg_value = SCU_SAS_PCFG_READ(this_phy);
   scu_sas_pcfg_value &=
      ~(SCU_SAS_PCFG_GEN_BIT(OOB_RESET) | SCU_SAS_PCFG_GEN_BIT(HARD_RESET));
   SCU_SAS_PCFG_WRITE(this_phy, scu_sas_pcfg_value);
   SCU_SAS_PCFG_READ(this_phy);
   /* Reset OOB sequence - end */

   /* Start OOB sequence - start */
   scu_sas_pcfg_value = SCU_SAS_PCFG_READ(this_phy);
   scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE);
   SCU_SAS_PCFG_WRITE(this_phy, scu_sas_pcfg_value);
   SCU_SAS_PCFG_READ(this_phy);
   /* Start OOB sequence - end */
}

/**
 * This method will transmit a hard reset request on the specified phy. The
 * SCU hardware requires that we reset the OOB state machine and set the hard
 * reset bit in the phy configuration register.
 * We then must start OOB over with the hard reset bit set.
 *
 * @param[in] this_phy
 */
static
void scu_link_layer_tx_hard_reset(
   SCIC_SDS_PHY_T *this_phy
)
{
   U32 phy_configuration_value;

   // SAS Phys must wait for the HARD_RESET_TX event notification to transition
   // to the starting state.
   phy_configuration_value = SCU_SAS_PCFG_READ(this_phy);
   phy_configuration_value |=
      (SCU_SAS_PCFG_GEN_BIT(HARD_RESET) | SCU_SAS_PCFG_GEN_BIT(OOB_RESET));
   SCU_SAS_PCFG_WRITE(this_phy, phy_configuration_value);

   // Now take the OOB state machine out of reset
   phy_configuration_value |= SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE);
   phy_configuration_value &= ~SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
   SCU_SAS_PCFG_WRITE(this_phy, phy_configuration_value);
}

//****************************************************************************
//*  PHY BASE STATE METHODS
//****************************************************************************

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCI_BASE_PHY_STATE_INITIAL.
 *    - This function sets the state handlers for the phy object base state
 * machine initial state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_initial_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_base_state_handlers(this_phy, SCI_BASE_PHY_STATE_INITIAL);
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCI_BASE_PHY_STATE_INITIAL.
 *    - This function sets the state handlers for the phy object base state
 * machine initial state.
 *    - The SCU hardware is requested to stop the protocol engine.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_stopped_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   /// @todo We need to get to the controller to place this PE in a reset state
   scic_sds_phy_set_base_state_handlers(this_phy, SCI_BASE_PHY_STATE_STOPPED);

   if (this_phy->sata_timeout_timer != NULL)
   {
      scic_cb_timer_destroy(
         scic_sds_phy_get_controller(this_phy),
         this_phy->sata_timeout_timer
      );

      this_phy->sata_timeout_timer = NULL;
   }

   scu_link_layer_stop_protocol_engine(this_phy);
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCI_BASE_PHY_STATE_STARTING.
 *    - This function sets the state handlers for the phy object base state
 * machine starting state.
 *    - The SCU hardware is requested to start OOB/SN on this protocol engine.
 *    - The phy starting substate machine is started.
 *    - If the previous state was the ready state then the
 *      SCIC_SDS_CONTROLLER is informed that the phy has gone link down.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_base_state_handlers(this_phy, SCI_BASE_PHY_STATE_STARTING);

   scu_link_layer_stop_protocol_engine(this_phy);
   scu_link_layer_start_oob(this_phy);

   // We don't know what kind of phy we are going to be just yet
   this_phy->protocol = SCIC_SDS_PHY_PROTOCOL_UNKNOWN;
   this_phy->bcn_received_while_port_unassigned = FALSE;

   // Change over to the starting substate machine to continue
   sci_base_state_machine_start(&this_phy->starting_substate_machine);

   if (this_phy->parent.state_machine.previous_state_id
       == SCI_BASE_PHY_STATE_READY)
   {
      scic_sds_controller_link_down(
         scic_sds_phy_get_controller(this_phy),
         scic_sds_phy_get_port(this_phy),
         this_phy
      );
   }
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCI_BASE_PHY_STATE_READY.
 *    - This function sets the state handlers for the phy object base state
 * machine ready state.
 *    - The SCU hardware protocol engine is resumed.
 *    - The SCIC_SDS_CONTROLLER is informed that the phy object has gone link
 *      up.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_ready_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_base_state_handlers(this_phy, SCI_BASE_PHY_STATE_READY);

   scic_sds_controller_link_up(
      scic_sds_phy_get_controller(this_phy),
      scic_sds_phy_get_port(this_phy),
      this_phy
   );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * exiting the SCI_BASE_PHY_STATE_INITIAL. This function suspends the SCU
 * hardware protocol engine represented by this SCIC_SDS_PHY object.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_ready_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_suspend(this_phy);
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCI_BASE_PHY_STATE_RESETTING.
 *    - This function sets the state handlers for the phy object base state
 * machine resetting state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_resetting_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T * this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_base_state_handlers(this_phy, SCI_BASE_PHY_STATE_RESETTING);

   // The phy is being reset, therefore deactivate it from the port.
   // In the resetting state we don't notify the user regarding
   // link up and link down notifications.
   scic_sds_port_deactivate_phy(this_phy->owning_port, this_phy, FALSE);

   if (this_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SAS)
   {
      scu_link_layer_tx_hard_reset(this_phy);
   }
   else
   {
      // The SCU does not need to have a descrete reset state so just go back to
      // the starting state.
      sci_base_state_machine_change_state(
         &this_phy->parent.state_machine,
         SCI_BASE_PHY_STATE_STARTING
      );
   }
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCI_BASE_PHY_STATE_FINAL.
 *    - This function sets the state handlers for the phy object base state
 * machine final state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_final_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_base_state_handlers(this_phy, SCI_BASE_PHY_STATE_FINAL);

   // Nothing to do here
}

// ---------------------------------------------------------------------------

SCI_BASE_STATE_T scic_sds_phy_state_table[SCI_BASE_PHY_MAX_STATES] =
{
   {
      SCI_BASE_PHY_STATE_INITIAL,
      scic_sds_phy_initial_state_enter,
      NULL,
   },
   {
      SCI_BASE_PHY_STATE_STOPPED,
      scic_sds_phy_stopped_state_enter,
      NULL,
   },
   {
      SCI_BASE_PHY_STATE_STARTING,
      scic_sds_phy_starting_state_enter,
      NULL,
   },
   {
      SCI_BASE_PHY_STATE_READY,
      scic_sds_phy_ready_state_enter,
      scic_sds_phy_ready_state_exit,
   },
   {
      SCI_BASE_PHY_STATE_RESETTING,
      scic_sds_phy_resetting_state_enter,
      NULL,
   },
   {
      SCI_BASE_PHY_STATE_FINAL,
      scic_sds_phy_final_state_enter,
      NULL,
   }
};

//******************************************************************************
//* PHY STARTING SUB-STATE MACHINE
//******************************************************************************

//*****************************************************************************
//* SCIC SDS PHY HELPER FUNCTIONS
//*****************************************************************************


/**
 * This method continues the link training for the phy as if it were a SAS PHY
 * instead of a SATA PHY. This is done because the completion queue had a SAS
 * PHY DETECTED event when the state machine was expecting a SATA PHY event.
 *
 * @param[in] this_phy The phy object that received SAS PHY DETECTED.
 *
 * @return none
 */
static
void scic_sds_phy_start_sas_link_training(
   SCIC_SDS_PHY_T * this_phy
)
{
   U32 phy_control;

   phy_control = SCU_SAS_PCFG_READ(this_phy);
   phy_control |= SCU_SAS_PCFG_GEN_BIT(SATA_SPINUP_HOLD);
   SCU_SAS_PCFG_WRITE(this_phy, phy_control);

   sci_base_state_machine_change_state(
      &this_phy->starting_substate_machine,
      SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_SPEED_EN
   );

   this_phy->protocol = SCIC_SDS_PHY_PROTOCOL_SAS;
}

/**
 * This method continues the link training for the phy as if it were a SATA
 * PHY instead of a SAS PHY.  This is done because the completion queue had a
 * SATA SPINUP HOLD event when the state machine was expecting a SAS PHY
 * event.
 *
 * @param[in] this_phy The phy object that received a SATA SPINUP HOLD event
 *
 * @return none
 */
static
void scic_sds_phy_start_sata_link_training(
   SCIC_SDS_PHY_T * this_phy
)
{
   sci_base_state_machine_change_state(
      &this_phy->starting_substate_machine,
      SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER
   );

   this_phy->protocol = SCIC_SDS_PHY_PROTOCOL_SATA;
}

/**
 * @brief This method performs processing common to all protocols upon
 *        completion of link training.
 *
 * @param[in,out] this_phy This parameter specifies the phy object for which
 *                link training has completed.
 * @param[in]     max_link_rate This parameter specifies the maximum link
 *                rate to be associated with this phy.
 * @param[in]     next_state This parameter specifies the next state for the
 *                phy's starting sub-state machine.
 *
 * @return none
 */
static
void scic_sds_phy_complete_link_training(
   SCIC_SDS_PHY_T *   this_phy,
   SCI_SAS_LINK_RATE  max_link_rate,
   U32                next_state
)
{
   this_phy->max_negotiated_speed = max_link_rate;

   sci_base_state_machine_change_state(
      scic_sds_phy_get_starting_substate_machine(this_phy), next_state
   );
}

/**
 * This method restarts the SCIC_SDS_PHY objects base state machine in the
 * starting state from any starting substate.
 *
 * @param[in] this_phy The SCIC_SDS_PHY object to restart.
 *
 * @return none
 */
void scic_sds_phy_restart_starting_state(
   SCIC_SDS_PHY_T *this_phy
)
{
   // Stop the current substate machine
   sci_base_state_machine_stop(
      scic_sds_phy_get_starting_substate_machine(this_phy)
   );

   // Re-enter the base state machine starting state
   sci_base_state_machine_change_state(
      scic_sds_phy_get_base_state_machine(this_phy),
      SCI_BASE_PHY_STATE_STARTING
      );
}


//*****************************************************************************
//* SCIC SDS PHY general handlers
//*****************************************************************************

static
SCI_STATUS scic_sds_phy_starting_substate_general_stop_handler(
   SCI_BASE_PHY_T *phy
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)phy;

   sci_base_state_machine_stop(
      &this_phy->starting_substate_machine
   );

   sci_base_state_machine_change_state(
      &phy->state_machine,
      SCI_BASE_PHY_STATE_STOPPED
   );

   return SCI_SUCCESS;
}

//*****************************************************************************
//* SCIC SDS PHY EVENT_HANDLERS
//*****************************************************************************

/**
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SPEED_EN.
 *    - decode the event
 *       - sas phy detected causes a state transition to the wait for speed
 *         event notification.
 *       - any other events log a warning message and set a failure status
 *
 * @param[in] phy This SCIC_SDS_PHY object which has received an event.
 * @param[in] event_code This is the event code which the phy object is to
 *       decode.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS on any valid event notification
 * @retval SCI_FAILURE on any unexpected event notifation
 */
static
SCI_STATUS scic_sds_phy_starting_substate_await_ossp_event_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32 event_code
)
{
   U32 result = SCI_SUCCESS;

   switch (scu_get_event_code(event_code))
   {
   case SCU_EVENT_SAS_PHY_DETECTED:
      scic_sds_phy_start_sas_link_training(this_phy);
      this_phy->is_in_link_training = TRUE;
   break;

   case SCU_EVENT_SATA_SPINUP_HOLD:
      scic_sds_phy_start_sata_link_training(this_phy);
      this_phy->is_in_link_training = TRUE;
   break;

   default:
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_phy),
         SCIC_LOG_OBJECT_PHY | SCIC_LOG_OBJECT_RECEIVED_EVENTS,
         "PHY starting substate machine received unexpected event_code %x\n",
         event_code
      ));

      result = SCI_FAILURE;
   break;
   }

   return result;
}

/**
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SPEED_EN.
 *    - decode the event
 *       - sas phy detected returns us back to this state.
 *       - speed event detected causes a state transition to the wait for iaf.
 *       - identify timeout is an un-expected event and the state machine is
 *         restarted.
 *       - link failure events restart the starting state machine
 *       - any other events log a warning message and set a failure status
 *
 * @param[in] phy This SCIC_SDS_PHY object which has received an event.
 * @param[in] event_code This is the event code which the phy object is to
 *       decode.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS on any valid event notification
 * @retval SCI_FAILURE on any unexpected event notifation
 */
static
SCI_STATUS scic_sds_phy_starting_substate_await_sas_phy_speed_event_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32 event_code
)
{
   U32 result = SCI_SUCCESS;

   switch (scu_get_event_code(event_code))
   {
   case SCU_EVENT_SAS_PHY_DETECTED:
      // Why is this being reported again by the controller?
      // We would re-enter this state so just stay here
   break;

   case SCU_EVENT_SAS_15:
   case SCU_EVENT_SAS_15_SSC:
      scic_sds_phy_complete_link_training(
         this_phy, SCI_SAS_150_GB, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF
      );
   break;

   case SCU_EVENT_SAS_30:
   case SCU_EVENT_SAS_30_SSC:
      scic_sds_phy_complete_link_training(
         this_phy, SCI_SAS_300_GB, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF
      );
   break;

   case SCU_EVENT_SAS_60:
   case SCU_EVENT_SAS_60_SSC:
      scic_sds_phy_complete_link_training(
         this_phy, SCI_SAS_600_GB, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF
      );
   break;

   case SCU_EVENT_SATA_SPINUP_HOLD:
      // We were doing SAS PHY link training and received a SATA PHY event
      // continue OOB/SN as if this were a SATA PHY
      scic_sds_phy_start_sata_link_training(this_phy);
   break;

   case SCU_EVENT_LINK_FAILURE:
      // Link failure change state back to the starting state
      scic_sds_phy_restart_starting_state(this_phy);
   break;

   default:
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_phy),
         SCIC_LOG_OBJECT_PHY | SCIC_LOG_OBJECT_RECEIVED_EVENTS,
         "PHY starting substate machine received unexpected event_code %x\n",
         event_code
      ));

      result = SCI_FAILURE;
   break;
   }

   return result;
}

/**
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF.
 *    - decode the event
 *       - sas phy detected event backs up the state machine to the await
 *         speed notification.
 *       - identify timeout is an un-expected event and the state machine is
 *         restarted.
 *       - link failure events restart the starting state machine
 *       - any other events log a warning message and set a failure status
 *
 * @param[in] phy This SCIC_SDS_PHY object which has received an event.
 * @param[in] event_code This is the event code which the phy object is to
 *       decode.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS on any valid event notification
 * @retval SCI_FAILURE on any unexpected event notifation
 */
static
SCI_STATUS scic_sds_phy_starting_substate_await_iaf_uf_event_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32 event_code
)
{
   U32 result = SCI_SUCCESS;

   switch (scu_get_event_code(event_code))
   {
   case SCU_EVENT_SAS_PHY_DETECTED:
      // Backup the state machine
      scic_sds_phy_start_sas_link_training(this_phy);
      break;

   case SCU_EVENT_SATA_SPINUP_HOLD:
      // We were doing SAS PHY link training and received a SATA PHY event
      // continue OOB/SN as if this were a SATA PHY
      scic_sds_phy_start_sata_link_training(this_phy);
   break;

   case SCU_EVENT_RECEIVED_IDENTIFY_TIMEOUT:
   case SCU_EVENT_LINK_FAILURE:
   case SCU_EVENT_HARD_RESET_RECEIVED:
      // Start the oob/sn state machine over again
      scic_sds_phy_restart_starting_state(this_phy);
      break;

   default:
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_phy),
         SCIC_LOG_OBJECT_PHY | SCIC_LOG_OBJECT_RECEIVED_EVENTS,
         "PHY starting substate machine received unexpected event_code %x\n",
         event_code
      ));

      result = SCI_FAILURE;
      break;
   }

   return result;
}

/**
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_POWER.
 *    - decode the event
 *       - link failure events restart the starting state machine
 *       - any other events log a warning message and set a failure status
 *
 * @param[in] phy This SCIC_SDS_PHY object which has received an event.
 * @param[in] event_code This is the event code which the phy object is to
 *       decode.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS on a link failure event
 * @retval SCI_FAILURE on any unexpected event notifation
 */
static
SCI_STATUS scic_sds_phy_starting_substate_await_sas_power_event_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32 event_code
)
{
   U32 result = SCI_SUCCESS;

   switch (scu_get_event_code(event_code))
   {
   case SCU_EVENT_LINK_FAILURE:
      // Link failure change state back to the starting state
      scic_sds_phy_restart_starting_state(this_phy);
      break;

   default:
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_phy),
         SCIC_LOG_OBJECT_PHY | SCIC_LOG_OBJECT_RECEIVED_EVENTS,
         "PHY starting substate machine received unexpected event_code %x\n",
         event_code
      ));

      result = SCI_FAILURE;
      break;
   }

   return result;
}

/**
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER.
 *    - decode the event
 *       - link failure events restart the starting state machine
 *       - sata spinup hold events are ignored since they are expected
 *       - any other events log a warning message and set a failure status
 *
 * @param[in] phy This SCIC_SDS_PHY object which has received an event.
 * @param[in] event_code This is the event code which the phy object is to
 *       decode.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS on a link failure event
 * @retval SCI_FAILURE on any unexpected event notifation
 */
static
SCI_STATUS scic_sds_phy_starting_substate_await_sata_power_event_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32 event_code
)
{
   U32 result = SCI_SUCCESS;

   switch (scu_get_event_code(event_code))
   {
   case SCU_EVENT_LINK_FAILURE:
      // Link failure change state back to the starting state
      scic_sds_phy_restart_starting_state(this_phy);
      break;

   case SCU_EVENT_SATA_SPINUP_HOLD:
      // These events are received every 10ms and are expected while in this state
      break;

   case SCU_EVENT_SAS_PHY_DETECTED:
      // There has been a change in the phy type before OOB/SN for the
      // SATA finished start down the SAS link traning path.
      scic_sds_phy_start_sas_link_training(this_phy);
   break;

   default:
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_phy),
         SCIC_LOG_OBJECT_PHY | SCIC_LOG_OBJECT_RECEIVED_EVENTS,
         "PHY starting substate machine received unexpected event_code %x\n",
         event_code
      ));

      result = SCI_FAILURE;
      break;
   }

   return result;
}

/**
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN.
 *    - decode the event
 *       - link failure events restart the starting state machine
 *       - sata spinup hold events are ignored since they are expected
 *       - sata phy detected event change to the wait speed event
 *       - any other events log a warning message and set a failure status
 *
 * @param[in] phy This SCIC_SDS_PHY object which has received an event.
 * @param[in] event_code This is the event code which the phy object is to
 *       decode.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS on a link failure event
 * @retval SCI_FAILURE on any unexpected event notifation
 */
static
SCI_STATUS scic_sds_phy_starting_substate_await_sata_phy_event_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32 event_code
)
{
   U32 result = SCI_SUCCESS;

   switch (scu_get_event_code(event_code))
   {
   case SCU_EVENT_LINK_FAILURE:
      // Link failure change state back to the starting state
      scic_sds_phy_restart_starting_state(this_phy);
      break;

   case SCU_EVENT_SATA_SPINUP_HOLD:
      // These events might be received since we dont know how many may be in
      // the completion queue while waiting for power
      break;

   case SCU_EVENT_SATA_PHY_DETECTED:
      this_phy->protocol = SCIC_SDS_PHY_PROTOCOL_SATA;

      // We have received the SATA PHY notification change state
      sci_base_state_machine_change_state(
         scic_sds_phy_get_starting_substate_machine(this_phy),
         SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN
         );
      break;

   case SCU_EVENT_SAS_PHY_DETECTED:
      // There has been a change in the phy type before OOB/SN for the
      // SATA finished start down the SAS link traning path.
      scic_sds_phy_start_sas_link_training(this_phy);
   break;

   default:
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_phy),
         SCIC_LOG_OBJECT_PHY | SCIC_LOG_OBJECT_RECEIVED_EVENTS,
         "PHY starting substate machine received unexpected event_code %x\n",
         event_code
      ));

      result = SCI_FAILURE;
      break;
   }

   return result;
}

/**
 * This method is called when an event notification is received for the phy
 * object when in the state
 * SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN.
 *    - decode the event
 *       - sata phy detected returns us back to this state.
 *       - speed event detected causes a state transition to the wait for
 *         signature.
 *       - link failure events restart the starting state machine
 *       - any other events log a warning message and set a failure status
 *
 * @param[in] phy This SCIC_SDS_PHY object which has received an event.
 * @param[in] event_code This is the event code which the phy object is to
 *       decode.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS on any valid event notification
 * @retval SCI_FAILURE on any unexpected event notifation
 */
static
SCI_STATUS scic_sds_phy_starting_substate_await_sata_speed_event_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32 event_code
)
{
   U32 result = SCI_SUCCESS;

   switch (scu_get_event_code(event_code))
   {
   case SCU_EVENT_SATA_PHY_DETECTED:
      // The hardware reports multiple SATA PHY detected events
      // ignore the extras
   break;

   case SCU_EVENT_SATA_15:
   case SCU_EVENT_SATA_15_SSC:
      scic_sds_phy_complete_link_training(
         this_phy,
         SCI_SAS_150_GB,
         SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF
      );
   break;

   case SCU_EVENT_SATA_30:
   case SCU_EVENT_SATA_30_SSC:
      scic_sds_phy_complete_link_training(
         this_phy,
         SCI_SAS_300_GB,
         SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF
      );
   break;

   case SCU_EVENT_SATA_60:
   case SCU_EVENT_SATA_60_SSC:
      scic_sds_phy_complete_link_training(
         this_phy,
         SCI_SAS_600_GB,
         SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF
      );
   break;

   case SCU_EVENT_LINK_FAILURE:
      // Link failure change state back to the starting state
      scic_sds_phy_restart_starting_state(this_phy);
   break;

   case SCU_EVENT_SAS_PHY_DETECTED:
      // There has been a change in the phy type before OOB/SN for the
      // SATA finished start down the SAS link traning path.
      scic_sds_phy_start_sas_link_training(this_phy);
   break;

   default:
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_phy),
         SCIC_LOG_OBJECT_PHY | SCIC_LOG_OBJECT_RECEIVED_EVENTS,
         "PHY starting substate machine received unexpected event_code %x\n",
         event_code
      ));

      result = SCI_FAILURE;
   break;
   }

   return result;
}

/**
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF.
 *    - decode the event
 *       - sas phy detected event backs up the state machine to the await
 *         speed notification.
 *       - identify timeout is an un-expected event and the state machine is
 *         restarted.
 *       - link failure events restart the starting state machine
 *       - any other events log a warning message and set a failure status
 *
 * @param[in] phy This SCIC_SDS_PHY object which has received an event.
 * @param[in] event_code This is the event code which the phy object is to
 *       decode.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS on any valid event notification
 * @retval SCI_FAILURE on any unexpected event notifation
 */
static
SCI_STATUS scic_sds_phy_starting_substate_await_sig_fis_event_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32 event_code
)
{
   U32 result = SCI_SUCCESS;

   switch (scu_get_event_code(event_code))
   {
   case SCU_EVENT_SATA_PHY_DETECTED:
      // Backup the state machine
      sci_base_state_machine_change_state(
         scic_sds_phy_get_starting_substate_machine(this_phy),
         SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN
         );
      break;

   case SCU_EVENT_LINK_FAILURE:
      // Link failure change state back to the starting state
      scic_sds_phy_restart_starting_state(this_phy);
      break;

   default:
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_phy),
         SCIC_LOG_OBJECT_PHY | SCIC_LOG_OBJECT_RECEIVED_EVENTS,
         "PHY starting substate machine received unexpected event_code %x\n",
         event_code
      ));

      result = SCI_FAILURE;
      break;
   }

   return result;
}


//*****************************************************************************
//*  SCIC SDS PHY FRAME_HANDLERS
//*****************************************************************************

/**
 * This method decodes the unsolicited frame when the SCIC_SDS_PHY is in the
 * SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF.
 *    - Get the UF Header
 *    - If the UF is an IAF
 *       - Copy IAF data to local phy object IAF data buffer.
 *       - Change starting substate to wait power.
 *    - else
 *       - log warning message of unexpected unsolicted frame
 *    - release frame buffer
 *
 * @param[in] phy This is SCIC_SDS_PHY object which is being requested to
 *       decode the frame data.
 * @param[in] frame_index This is the index of the unsolicited frame which was
 *       received for this phy.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_phy_starting_substate_await_iaf_uf_frame_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32            frame_index
)
{
   SCI_STATUS                        result;
   U32                              *frame_words;
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_T *identify_frame;

   result = scic_sds_unsolicited_frame_control_get_header(
               &(scic_sds_phy_get_controller(this_phy)->uf_control),
               frame_index,
               (void **)&frame_words);

   if (result != SCI_SUCCESS)
   {
      return result;
   }

   frame_words[0] = SCIC_SWAP_DWORD(frame_words[0]);
   identify_frame = (SCI_SAS_IDENTIFY_ADDRESS_FRAME_T *)frame_words;

   if (identify_frame->address_frame_type == 0)
   {
      // Byte swap the rest of the frame so we can make
      // a copy of the buffer
      frame_words[1] = SCIC_SWAP_DWORD(frame_words[1]);
      frame_words[2] = SCIC_SWAP_DWORD(frame_words[2]);
      frame_words[3] = SCIC_SWAP_DWORD(frame_words[3]);
      frame_words[4] = SCIC_SWAP_DWORD(frame_words[4]);
      frame_words[5] = SCIC_SWAP_DWORD(frame_words[5]);

      memcpy(
         &this_phy->phy_type.sas.identify_address_frame_buffer,
         identify_frame,
         sizeof(SCI_SAS_IDENTIFY_ADDRESS_FRAME_T)
      );

      if (identify_frame->protocols.u.bits.smp_target)
      {
         // We got the IAF for an expander PHY go to the final state since
         // there are no power requirements for expander phys.
         sci_base_state_machine_change_state(
            scic_sds_phy_get_starting_substate_machine(this_phy),
            SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL
         );
      }
      else
      {
         // We got the IAF we can now go to the await spinup semaphore state
         sci_base_state_machine_change_state(
            scic_sds_phy_get_starting_substate_machine(this_phy),
            SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER
         );
      }

      result = SCI_SUCCESS;
   }
   else
   {
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_phy),
         SCIC_LOG_OBJECT_PHY | SCIC_LOG_OBJECT_UNSOLICITED_FRAMES,
         "PHY starting substate machine received unexpected frame id %x\n",
         frame_index
      ));
   }

   // Regardless of the result release this frame since we are done with it
   scic_sds_controller_release_frame(
      scic_sds_phy_get_controller(this_phy), frame_index
      );

   return result;
}

/**
 * This method decodes the unsolicited frame when the SCIC_SDS_PHY is in the
 * SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF.
 *    - Get the UF Header
 *    - If the UF is an SIGNATURE FIS
 *       - Copy IAF data to local phy object SIGNATURE FIS data buffer.
 *    - else
 *       - log warning message of unexpected unsolicted frame
 *    - release frame buffer
 *
 * @param[in] phy This is SCIC_SDS_PHY object which is being requested to
 *       decode the frame data.
 * @param[in] frame_index This is the index of the unsolicited frame which was
 *       received for this phy.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 *
 * @todo Must decode the SIGNATURE FIS data
 */
static
SCI_STATUS scic_sds_phy_starting_substate_await_sig_fis_frame_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32            frame_index
)
{
   SCI_STATUS          result;
   U32               * frame_words;
   SATA_FIS_HEADER_T * fis_frame_header;
   U32               * fis_frame_data;

   result = scic_sds_unsolicited_frame_control_get_header(
               &(scic_sds_phy_get_controller(this_phy)->uf_control),
               frame_index,
               (void **)&frame_words);

   if (result != SCI_SUCCESS)
   {
      return result;
   }

   fis_frame_header = (SATA_FIS_HEADER_T *)frame_words;

   if (
         (fis_frame_header->fis_type == SATA_FIS_TYPE_REGD2H)
      && !(fis_frame_header->status & ATA_STATUS_REG_BSY_BIT)
      )
   {
      scic_sds_unsolicited_frame_control_get_buffer(
         &(scic_sds_phy_get_controller(this_phy)->uf_control),
         frame_index,
         (void **)&fis_frame_data
      );

      scic_sds_controller_copy_sata_response(
         &this_phy->phy_type.sata.signature_fis_buffer,
         frame_words,
         fis_frame_data
      );

      // We got the IAF we can now go to the await spinup semaphore state
      sci_base_state_machine_change_state(
         scic_sds_phy_get_starting_substate_machine(this_phy),
         SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL
         );

      result = SCI_SUCCESS;
   }
   else
   {
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_phy),
         SCIC_LOG_OBJECT_PHY | SCIC_LOG_OBJECT_UNSOLICITED_FRAMES,
         "PHY starting substate machine received unexpected frame id %x\n",
         frame_index
      ));
   }

   // Regardless of the result release this frame since we are done with it
   scic_sds_controller_release_frame(
      scic_sds_phy_get_controller(this_phy), frame_index
      );

   return result;
}

//*****************************************************************************
//* SCIC SDS PHY POWER_HANDLERS
//*****************************************************************************

/**
 * This method is called by the SCIC_SDS_CONTROLLER when the phy object is
 * granted power.
 *    - The notify enable spinups are turned on for this phy object
 *    - The phy state machine is transitioned to the
 *    SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL.
 *
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_phy_starting_substate_await_sas_power_consume_power_handler(
   SCIC_SDS_PHY_T *this_phy
)
{
   U32 enable_spinup;

   enable_spinup = SCU_SAS_ENSPINUP_READ(this_phy);
   enable_spinup |= SCU_ENSPINUP_GEN_BIT(ENABLE);
   SCU_SAS_ENSPINUP_WRITE(this_phy, enable_spinup);

   // Change state to the final state this substate machine has run to completion
   sci_base_state_machine_change_state(
      scic_sds_phy_get_starting_substate_machine(this_phy),
      SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL
      );

   return SCI_SUCCESS;
}

/**
 * This method is called by the SCIC_SDS_CONTROLLER when the phy object is
 * granted power.
 *    - The phy state machine is transitioned to the
 *    SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN.
 *
 * @param[in] phy This is the SCI_BASE_PHY object which is cast into a
 *       SCIC_SDS_PHY object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_phy_starting_substate_await_sata_power_consume_power_handler(
   SCIC_SDS_PHY_T *this_phy
)
{
   U32 scu_sas_pcfg_value;

   // Release the spinup hold state and reset the OOB state machine
   scu_sas_pcfg_value = SCU_SAS_PCFG_READ(this_phy);
   scu_sas_pcfg_value &=
      ~(SCU_SAS_PCFG_GEN_BIT(SATA_SPINUP_HOLD) | SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE));
   scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
   SCU_SAS_PCFG_WRITE(this_phy, scu_sas_pcfg_value);

   // Now restart the OOB operation
   scu_sas_pcfg_value &= ~SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
   scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE);
   SCU_SAS_PCFG_WRITE(this_phy, scu_sas_pcfg_value);

   // Change state to the final state this substate machine has run to completion
   sci_base_state_machine_change_state(
      scic_sds_phy_get_starting_substate_machine(this_phy),
      SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN
   );

   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

SCIC_SDS_PHY_STATE_HANDLER_T
   scic_sds_phy_starting_substate_handler_table[SCIC_SDS_PHY_STARTING_MAX_SUBSTATES] =
{
   // SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_starting_substate_general_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_default_frame_handler,
      scic_sds_phy_default_event_handler,
      scic_sds_phy_default_consume_power_handler
   },
   // SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_OSSP_EN
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_starting_substate_general_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_default_frame_handler,
      scic_sds_phy_starting_substate_await_ossp_event_handler,
      scic_sds_phy_default_consume_power_handler
   },
   // SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_SPEED_EN
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_starting_substate_general_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_default_frame_handler,
      scic_sds_phy_starting_substate_await_sas_phy_speed_event_handler,
      scic_sds_phy_default_consume_power_handler
   },
   // SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_default_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_starting_substate_await_iaf_uf_frame_handler,
      scic_sds_phy_starting_substate_await_iaf_uf_event_handler,
      scic_sds_phy_default_consume_power_handler
   },
   // SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_starting_substate_general_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_default_frame_handler,
      scic_sds_phy_starting_substate_await_sas_power_event_handler,
      scic_sds_phy_starting_substate_await_sas_power_consume_power_handler
   },
   // SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER,
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_starting_substate_general_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_default_frame_handler,
      scic_sds_phy_starting_substate_await_sata_power_event_handler,
      scic_sds_phy_starting_substate_await_sata_power_consume_power_handler
   },
   // SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN,
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_starting_substate_general_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_default_frame_handler,
      scic_sds_phy_starting_substate_await_sata_phy_event_handler,
      scic_sds_phy_default_consume_power_handler
   },
   // SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN,
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_starting_substate_general_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_default_frame_handler,
      scic_sds_phy_starting_substate_await_sata_speed_event_handler,
      scic_sds_phy_default_consume_power_handler
   },
   // SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF,
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_starting_substate_general_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_starting_substate_await_sig_fis_frame_handler,
      scic_sds_phy_starting_substate_await_sig_fis_event_handler,
      scic_sds_phy_default_consume_power_handler
   },
   // SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL
   {
      {
         scic_sds_phy_default_start_handler,
         scic_sds_phy_starting_substate_general_stop_handler,
         scic_sds_phy_default_reset_handler,
         scic_sds_phy_default_destroy_handler
      },
      scic_sds_phy_default_frame_handler,
      scic_sds_phy_default_event_handler,
      scic_sds_phy_default_consume_power_handler
   }
};

/**
 * This macro sets the starting substate handlers by state_id
 */
#define scic_sds_phy_set_starting_substate_handlers(phy, state_id) \
   scic_sds_phy_set_state_handlers( \
      (phy), \
      &scic_sds_phy_starting_substate_handler_table[(state_id)] \
   )

//****************************************************************************
//*  PHY STARTING SUBSTATE METHODS
//****************************************************************************

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL.
 *    - The initial state handlers are put in place for the SCIC_SDS_PHY
 *      object.
 *    - The state is changed to the wait phy type event notification.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_initial_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_starting_substate_handlers(
      this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL);

   // This is just an temporary state go off to the starting state
   sci_base_state_machine_change_state(
      scic_sds_phy_get_starting_substate_machine(this_phy),
      SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_OSSP_EN
   );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_PHY_TYPE_EN.
 *    - Set the SCIC_SDS_PHY object state handlers for this state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_await_ossp_en_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_starting_substate_handlers(
      this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_OSSP_EN
      );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SPEED_EN.
 *    - Set the SCIC_SDS_PHY object state handlers for this state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_await_sas_speed_en_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_starting_substate_handlers(
      this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_SPEED_EN
      );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF.
 *    - Set the SCIC_SDS_PHY object state handlers for this state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_await_iaf_uf_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_starting_substate_handlers(
      this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF
      );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER.
 *    - Set the SCIC_SDS_PHY object state handlers for this state.
 *    - Add this phy object to the power control queue
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_await_sas_power_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_starting_substate_handlers(
      this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER
      );

   scic_sds_controller_power_control_queue_insert(
      scic_sds_phy_get_controller(this_phy),
      this_phy
      );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * exiting the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER.
 *    - Remove the SCIC_SDS_PHY object from the power control queue.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_await_sas_power_substate_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_controller_power_control_queue_remove(
      scic_sds_phy_get_controller(this_phy), this_phy
   );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER.
 *    - Set the SCIC_SDS_PHY object state handlers for this state.
 *    - Add this phy object to the power control queue
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_await_sata_power_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_starting_substate_handlers(
      this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER
      );

   scic_sds_controller_power_control_queue_insert(
      scic_sds_phy_get_controller(this_phy),
      this_phy
      );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * exiting the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER.
 *    - Remove the SCIC_SDS_PHY object from the power control queue.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_await_sata_power_substate_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_controller_power_control_queue_remove(
      scic_sds_phy_get_controller(this_phy),
      this_phy
      );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN.
 *    - Set the SCIC_SDS_PHY object state handlers for this state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_await_sata_phy_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_starting_substate_handlers(
      this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN
      );

   scic_cb_timer_start(
      scic_sds_phy_get_controller(this_phy),
      this_phy->sata_timeout_timer,
      SCIC_SDS_SATA_LINK_TRAINING_TIMEOUT
   );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * exiting the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN.
 *    - stop the timer that was started on entry to await sata phy
 *      event notification
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_await_sata_phy_substate_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_cb_timer_stop(
      scic_sds_phy_get_controller(this_phy),
      this_phy->sata_timeout_timer
   );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN.
 *    - Set the SCIC_SDS_PHY object state handlers for this state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_await_sata_speed_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_starting_substate_handlers(
      this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN
      );

   scic_cb_timer_start(
      scic_sds_phy_get_controller(this_phy),
      this_phy->sata_timeout_timer,
      SCIC_SDS_SATA_LINK_TRAINING_TIMEOUT
   );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * exiting the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN.
 *    - stop the timer that was started on entry to await sata phy
 *      event notification
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_await_sata_speed_substate_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_cb_timer_stop(
      scic_sds_phy_get_controller(this_phy),
      this_phy->sata_timeout_timer
   );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF.
 *    - Set the SCIC_SDS_PHY object state handlers for this state.
 *    - Start the SIGNATURE FIS timeout timer
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_await_sig_fis_uf_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   BOOL             continue_to_ready_state;
   SCIC_SDS_PHY_T * this_phy;

   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_starting_substate_handlers(
      this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF
   );

   continue_to_ready_state = scic_sds_port_link_detected(
                                 this_phy->owning_port,
                                 this_phy
                             );

   if (continue_to_ready_state)
   {
      // Clear the PE suspend condition so we can actually receive SIG FIS
      // The hardware will not respond to the XRDY until the PE suspend
      // condition is cleared.
      scic_sds_phy_resume(this_phy);

      scic_cb_timer_start(
         scic_sds_phy_get_controller(this_phy),
         this_phy->sata_timeout_timer,
         SCIC_SDS_SIGNATURE_FIS_TIMEOUT
      );
   }
   else
   {
      this_phy->is_in_link_training = FALSE;
   }
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * exiting the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF.
 *    - Stop the SIGNATURE FIS timeout timer.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_await_sig_fis_uf_substate_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_cb_timer_stop(
      scic_sds_phy_get_controller(this_phy),
      this_phy->sata_timeout_timer
   );
}

/**
 * This method will perform the actions required by the SCIC_SDS_PHY on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL.
 *    - Set the SCIC_SDS_PHY object state handlers for this state.
 *    - Change base state machine to the ready state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_PHY object.
 *
 * @return none
 */
static
void scic_sds_phy_starting_final_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_PHY_T *this_phy;
   this_phy = (SCIC_SDS_PHY_T *)object;

   scic_sds_phy_set_starting_substate_handlers(
      this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL
      );

   // State machine has run to completion so exit out and change
   // the base state machine to the ready state
   sci_base_state_machine_change_state(
      scic_sds_phy_get_base_state_machine(this_phy),
      SCI_BASE_PHY_STATE_READY);
}

// ---------------------------------------------------------------------------

SCI_BASE_STATE_T
   scic_sds_phy_starting_substates[SCIC_SDS_PHY_STARTING_MAX_SUBSTATES] =
{
   {
      SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL,
      scic_sds_phy_starting_initial_substate_enter,
      NULL,
   },
   {
      SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_OSSP_EN,
      scic_sds_phy_starting_await_ossp_en_substate_enter,
      NULL,
   },
   {
      SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_SPEED_EN,
      scic_sds_phy_starting_await_sas_speed_en_substate_enter,
      NULL,
   },
   {
      SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF,
      scic_sds_phy_starting_await_iaf_uf_substate_enter,
      NULL,
   },
   {
      SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER,
      scic_sds_phy_starting_await_sas_power_substate_enter,
      scic_sds_phy_starting_await_sas_power_substate_exit,
   },
   {
      SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER,
      scic_sds_phy_starting_await_sata_power_substate_enter,
      scic_sds_phy_starting_await_sata_power_substate_exit
   },
   {
      SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN,
      scic_sds_phy_starting_await_sata_phy_substate_enter,
      scic_sds_phy_starting_await_sata_phy_substate_exit
   },
   {
      SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN,
      scic_sds_phy_starting_await_sata_speed_substate_enter,
      scic_sds_phy_starting_await_sata_speed_substate_exit
   },
   {
      SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF,
      scic_sds_phy_starting_await_sig_fis_uf_substate_enter,
      scic_sds_phy_starting_await_sig_fis_uf_substate_exit
   },
   {
      SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL,
      scic_sds_phy_starting_final_substate_enter,
      NULL,
   }
};

