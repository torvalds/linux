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
* @brief This file contains the method definitions to translate
*        SCSI Log Sense command based of the SATv2 spec.
*/

#if !defined(DISABLE_SATI_LOG_SENSE)

#include <dev/isci/scil/sati_log_sense.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_util.h>

//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************

/**
 * @brief This method constructs the SATI supported log page. This is a log
 *        containing the page codes of all the SATI supported log pages.
 *
 * @return n/a
 *
 */
static
void sati_supported_log_page_construct(
   SATI_TRANSLATOR_SEQUENCE_T  * sequence,
   void                        * scsi_io
)
{
   U32 next_byte;
   //set SPF = 0 and PAGE_CODE = 0
   sati_set_data_byte(sequence, scsi_io, 0, 0x00);

   //set SUBPAGE_CODE = 0
   sati_set_data_byte(sequence, scsi_io, 1, 0x00);

   //set the Page Length to (n-3) or 2 because only two log pages are supported
   sati_set_data_byte(sequence, scsi_io, 2, 0x00);
   sati_set_data_byte(sequence, scsi_io, 3, 0x02);

   //specify the next byte to be set
   next_byte = 4;

   if(sequence->device->capabilities & SATI_DEVICE_CAP_SMART_SUPPORT)
   {
      sati_set_data_byte(
         sequence,
         scsi_io,
         next_byte,
         SCSI_LOG_PAGE_INFORMATION_EXCEPTION
      );
      next_byte = 5;
   }

   if(sequence->device->capabilities & SATI_DEVICE_CAP_SMART_SELF_TEST_SUPPORT)
   {
      sati_set_data_byte(
         sequence,
         scsi_io,
         next_byte,
         SCSI_LOG_PAGE_SELF_TEST
      );
   }
}

/**
 * @brief This method sets bytes 4-19 of the self-test log parameter to zero.
 *
 * @return n/a
 *
 */
static
void sati_set_parameters_to_zero(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io
)
{
      sati_set_data_byte(sequence, scsi_io, 8, 0x00);   //log_parameter byte 4
      sati_set_data_byte(sequence, scsi_io, 9, 0x00);   //log_parameter byte 5
      sati_set_data_byte(sequence, scsi_io, 10, 0x00);  //log_parameter byte 6
      sati_set_data_byte(sequence, scsi_io, 11, 0x00);  //log_parameter byte 7
      sati_set_data_byte(sequence, scsi_io, 12, 0x00);  //log_parameter byte 8
      sati_set_data_byte(sequence, scsi_io, 13, 0x00);  //log_parameter byte 9
      sati_set_data_byte(sequence, scsi_io, 14, 0x00);  //log_parameter byte 10
      sati_set_data_byte(sequence, scsi_io, 15, 0x00);  //log_parameter byte 11
      sati_set_data_byte(sequence, scsi_io, 16, 0x00);  //log_parameter byte 12
      sati_set_data_byte(sequence, scsi_io, 17, 0x00);  //log_parameter byte 13
      sati_set_data_byte(sequence, scsi_io, 18, 0x00);  //log_parameter byte 14
      sati_set_data_byte(sequence, scsi_io, 19, 0x00);  //log_parameter byte 15
      sati_set_data_byte(sequence, scsi_io, 20, 0x00);  //log_parameter byte 16
      sati_set_data_byte(sequence, scsi_io, 21, 0x00);  //log_parameter byte 17
      sati_set_data_byte(sequence, scsi_io, 22, 0x00);  //log_parameter byte 18
      sati_set_data_byte(sequence, scsi_io, 23, 0x00);  //log_parameter byte 19
}

/**
 * @brief This method translates the ATA Extended SMART self-test log into
 *        SCSI Sense Key, Additional Sense Code, and Additional Sense code
 *        qualifiers based on the self test status byte in the appropriate
 *        descriptor entry.
 *
 * @return n/a
 *
 */
static
void sati_translate_sense_values(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U8                           self_test_status_byte
)
{
   //byte 17
   sati_set_data_byte(
      sequence,
      scsi_io,
      21,
      SCSI_DIAGNOSTIC_FAILURE_ON_COMPONENT
   );

   switch(self_test_status_byte)
   {
      case 1:
         //byte 16
         sati_set_data_byte(sequence, scsi_io, 20, SCSI_SENSE_ABORTED_COMMAND);

         //byte 18
         sati_set_data_byte(sequence, scsi_io, 22, 0x81);
         break;

      case 2:
         //byte 16
         sati_set_data_byte(sequence, scsi_io, 20, SCSI_SENSE_ABORTED_COMMAND);

         //byte 18
         sati_set_data_byte(sequence, scsi_io, 22, 0x82);
         break;

      case 3:
         //byte 16
         sati_set_data_byte(sequence, scsi_io, 20, SCSI_SENSE_ABORTED_COMMAND);

         //byte 18
         sati_set_data_byte(sequence, scsi_io, 22, 0x83);
         break;

      case 4:
         //byte 16
         sati_set_data_byte(sequence, scsi_io, 20, SCSI_SENSE_HARDWARE_ERROR);

         //byte 18
         sati_set_data_byte(sequence, scsi_io, 22, 0x84);
         break;

      case 5:
         //byte 16
         sati_set_data_byte(sequence, scsi_io, 20, SCSI_SENSE_HARDWARE_ERROR);

         //byte 18
         sati_set_data_byte(sequence, scsi_io, 22, 0x85);
         break;

      case 6:
         //byte 16
         sati_set_data_byte(sequence, scsi_io, 20, SCSI_SENSE_HARDWARE_ERROR);

         //byte 18
         sati_set_data_byte(sequence, scsi_io, 22, 0x86);
         break;

      case 7:
         //byte 16
         sati_set_data_byte(sequence, scsi_io, 20, SCSI_SENSE_MEDIUM_ERROR);

         //byte 18
         sati_set_data_byte(sequence, scsi_io, 22, 0x87);
         break;

      case 8:
         //byte 16
         sati_set_data_byte(sequence, scsi_io, 20, SCSI_SENSE_HARDWARE_ERROR);

         //byte 18
         sati_set_data_byte(sequence, scsi_io, 22, 0x88);
         break;

      default:
         //byte 16
         sati_set_data_byte(sequence, scsi_io, 20, SCSI_SENSE_NO_SENSE);
         //byte 17
         sati_set_data_byte(sequence, scsi_io, 21, SCSI_ASC_NO_ADDITIONAL_SENSE);
         //byte 18
         sati_set_data_byte(sequence, scsi_io, 22, 0x00);
         break;
   }

}

/**
 * @brief This method retrieves the correct self-test results by checking the
 *        descriptor index in the extended SMART self-test log. The index is
 *        used to determine the appropriate descriptor entry.
 *
 * @return n/a
 *
 */
static
void sati_get_self_test_results(
   SATI_TRANSLATOR_SEQUENCE_T          * sequence,
   void                                * scsi_io,
   ATA_EXTENDED_SMART_SELF_TEST_LOG_T  * ata_log
)
{
   U16 descriptor_index = *((U16 *)(&ata_log->self_test_descriptor_index[0]));

   /*
    * SATv2 wants data from descriptor N where N is equal to
    * (descriptor_index - parameter_code) + 1. Since parameter
    * code is always 0x0001 just checking descriptor_index.
    */

   if(descriptor_index <= 0)
   {
      sati_set_parameters_to_zero(sequence, scsi_io);
   }
   else
   {
      sati_set_data_byte(
       sequence,
       scsi_io,
       8,
       ata_log->descriptor_entrys[descriptor_index].DESCRIPTOR_ENTRY.status_byte
      );

      //Sef-test number unspecified per satv2
      sati_set_data_byte(sequence, scsi_io, 9, 0x00);
      sati_set_data_byte(
       sequence,
       scsi_io,
       10,
       ata_log->descriptor_entrys[descriptor_index].DESCRIPTOR_ENTRY.time_stamp_high
      );

      sati_set_data_byte(
       sequence,
       scsi_io,
       11,
       ata_log->descriptor_entrys[descriptor_index].DESCRIPTOR_ENTRY.time_stamp_low
      );

      //set to zero because it's a 48bit address
      sati_set_data_byte(sequence, scsi_io, 12, 0x00);
      sati_set_data_byte(sequence, scsi_io, 13, 0x00);

      sati_set_data_byte(
       sequence,
       scsi_io,
       14,
       ata_log->descriptor_entrys[descriptor_index].DESCRIPTOR_ENTRY.failing_lba_high_ext
      );

      sati_set_data_byte(
       sequence,
       scsi_io,
       15,
       ata_log->descriptor_entrys[descriptor_index].DESCRIPTOR_ENTRY.failing_lba_mid_ext
      );

      sati_set_data_byte(
       sequence,
       scsi_io,
       16,
       ata_log->descriptor_entrys[descriptor_index].DESCRIPTOR_ENTRY.failing_lba_low_ext
      );

      sati_set_data_byte(
       sequence,
       scsi_io,
       17,
       ata_log->descriptor_entrys[descriptor_index].DESCRIPTOR_ENTRY.failing_lba_high
      );

      sati_set_data_byte(
       sequence,
       scsi_io,
       18,
       ata_log->descriptor_entrys[descriptor_index].DESCRIPTOR_ENTRY.failing_lba_mid
      );

      sati_set_data_byte(
       sequence,
       scsi_io,
       19,
       ata_log->descriptor_entrys[descriptor_index].DESCRIPTOR_ENTRY.failing_lba_low
      );

      sati_translate_sense_values(
       sequence,
       scsi_io,
       ata_log->descriptor_entrys[descriptor_index].DESCRIPTOR_ENTRY.status_byte
      );
   }
}

/**
* @brief This method will construct the first eight bytes of the SCSI self test
*        log page for both cases when SATI sends a ATA read log ext and a smart
*        read log command.
*
* @return n/a
*
*/
static
void sati_self_test_log_header_construct(
   SATI_TRANSLATOR_SEQUENCE_T  * sequence,
   void                        * scsi_io
)
{
   //PAGE CODE for Self-Test Log Page
   sati_set_data_byte(sequence, scsi_io, 0, 0x10);
   sati_set_data_byte(sequence, scsi_io, 1, 0x00);

   //PAGE LENGTH is 0x14 instead of 0x190, not returning 20/0x190 log perameters
   sati_set_data_byte(sequence, scsi_io, 2, 0x00);
   sati_set_data_byte(sequence, scsi_io, 3, 0x14);

   /*
    * Log PARAMETER 0x0001
    * Only sending one log parameter per self-test request.
    */
   sati_set_data_byte(sequence, scsi_io, 4, 0x00);       //log_parameter byte 0
   sati_set_data_byte(sequence, scsi_io, 5, 0x01);       //log_parameter byte 1

   //Set to 0x03 per SATv2 spec
   sati_set_data_byte(sequence, scsi_io, 6, 0x03);       //log_parameter byte 2

   //Parameter Length set to 0x10 per SATv2 spec
   sati_set_data_byte(sequence, scsi_io, 7, 0x10);       //log_parameter byte 3
}

/**
 * @brief This method will construct the SCSI self test log page from
 *        the Extended SMART self-test log response received from the
 *        ATA device. The response is from a ATA_Read_Log_EXT command
 *        issued by SATI.
 *
 * @return n/a
 *
 */
static
void sati_extended_self_test_log_page_construct(
   SATI_TRANSLATOR_SEQUENCE_T  * sequence,
   void                        * scsi_io,
   void                        * ata_data
)
{
   ATA_EXTENDED_SMART_SELF_TEST_LOG_T * ata_log =
                  (ATA_EXTENDED_SMART_SELF_TEST_LOG_T*) ata_data;

   sati_self_test_log_header_construct(sequence, scsi_io);

   //bytes 4-19
   if( (ata_log->self_test_descriptor_index[0] == 0) &&
       (ata_log->self_test_descriptor_index[1] == 0))
   {
      sati_set_parameters_to_zero(sequence, scsi_io);
   }
   else
   {
      sati_get_self_test_results(sequence, scsi_io, ata_log);
   }
}

/**
* @brief This method will construct the SCSI self test log page from
*        the SMART self-test log response received from the ATA device.
*        The response is from a ATA_SMART_Read_Log command issued by SATI.
*
* @return n/a
*
*/
static
void sati_self_test_log_page_construct(
   SATI_TRANSLATOR_SEQUENCE_T  * sequence,
   void                        * scsi_io,
   void                        * ata_data
)
{
   ATA_SMART_SELF_TEST_LOG_T * ata_log =
                        (ATA_SMART_SELF_TEST_LOG_T*) ata_data;

   sati_self_test_log_header_construct(sequence, scsi_io);

   //first descriptor entry(index == 0) is always used because scsi_parameter_code == 1
   sati_set_data_byte(
      sequence,
      scsi_io,
      8,
      ata_log->descriptor_entrys[0].SMART_DESCRIPTOR_ENTRY.status_byte
   );

   //Sef-test number unspecified per satv2
   sati_set_data_byte(sequence, scsi_io, 9, 0x00);

   sati_set_data_byte(
      sequence,
      scsi_io,
      10,
      ata_log->descriptor_entrys[0].SMART_DESCRIPTOR_ENTRY.time_stamp_high
   );

   sati_set_data_byte(
      sequence,
      scsi_io,
      11,
      ata_log->descriptor_entrys[0].SMART_DESCRIPTOR_ENTRY.time_stamp_low
   );

   //set to zero because it's a 28bit address
   sati_set_data_byte(sequence, scsi_io, 12, 0x00);
   sati_set_data_byte(sequence, scsi_io, 13, 0x00);
   sati_set_data_byte(sequence, scsi_io, 14, 0x00);
   sati_set_data_byte(sequence, scsi_io, 15, 0x00);

   sati_set_data_byte(
      sequence,
      scsi_io,
      16,
      ata_log->descriptor_entrys[0].SMART_DESCRIPTOR_ENTRY.failing_lba_low_ext
   );

   sati_set_data_byte(
      sequence,
      scsi_io,
      17,
      ata_log->descriptor_entrys[0].SMART_DESCRIPTOR_ENTRY.failing_lba_high
   );

   sati_set_data_byte(
      sequence,
      scsi_io,
      18,
      ata_log->descriptor_entrys[0].SMART_DESCRIPTOR_ENTRY.failing_lba_mid
   );

   sati_set_data_byte(
      sequence,
      scsi_io,
      19,
      ata_log->descriptor_entrys[0].SMART_DESCRIPTOR_ENTRY.failing_lba_low
   );

   sati_translate_sense_values(
      sequence,
      scsi_io,
      ata_log->descriptor_entrys[0].SMART_DESCRIPTOR_ENTRY.status_byte
   );
}

/**
* @brief This method will construct the SCSI information exception log page from
*        the ATA SMART response received from the ATA device. The response is
*         from a ATA SMART return status command issued by SATI.
*
* @return n/a
*
*/
static
void sati_information_exception_log_page_contruct(
   SATI_TRANSLATOR_SEQUENCE_T  * sequence,
   void                        * scsi_io,
   void                        * ata_io
)
{
   U8 * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);
   U32 mid_register = sati_get_ata_lba_mid(register_fis);
   U32 high_register = sati_get_ata_lba_high(register_fis);

   //Information Exception Page code
   sati_set_data_byte(
      sequence,
      scsi_io,
      0,
      SCSI_LOG_PAGE_INFORMATION_EXCEPTION
   );

   //Sub-page code
   sati_set_data_byte(sequence, scsi_io, 1, 0x00);

   //Page length of log parameters
   sati_set_data_byte(sequence, scsi_io, 2, 0x00);
   sati_set_data_byte(sequence, scsi_io, 3, 0x08);

   //parameter code
   sati_set_data_byte(sequence, scsi_io, 4, 0x00);
   sati_set_data_byte(sequence, scsi_io, 5, 0x00);

   //Format and Linking
   sati_set_data_byte(sequence, scsi_io, 6, 0x03);
   //Parameter Length
   sati_set_data_byte(sequence, scsi_io, 7, 0x04);

   if(mid_register == ATA_MID_REGISTER_THRESHOLD_EXCEEDED
      && high_register == ATA_HIGH_REGISTER_THRESHOLD_EXCEEDED)
   {
      sati_set_data_byte(
         sequence,
         scsi_io,
         8,
         SCSI_ASC_HARDWARE_IMPENDING_FAILURE
      );

      sati_set_data_byte(
         sequence,
         scsi_io,
         9,
         SCSI_ASCQ_GENERAL_HARD_DRIVE_FAILURE
      );
   }
   else
   {
      sati_set_data_byte(sequence, scsi_io, 8, SCSI_ASC_NO_ADDITIONAL_SENSE);
      sati_set_data_byte(sequence, scsi_io, 9, SCSI_ASCQ_NO_ADDITIONAL_SENSE);
   }
   //setting most recent temperature reading to 0xFF(not supported) for now.
   sati_set_data_byte(sequence, scsi_io, 10, 0xFF);
}

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

/**
 * @brief This method will translate the SCSI Log Sense command into ATA commands
 *        specified by SATv2. ATA commands Read Log EXT and SMART Read Log will
 *        be issued by this translation.
 *
 * @return SATI_STATUS Indicates if the command translation succeeded.
 *
 */
SATI_STATUS sati_log_sense_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * cdb = sati_cb_get_cdb_address(scsi_io);
   SATI_STATUS status = SATI_FAILURE;

   if(SATI_LOG_SENSE_GET_PC_FIELD(cdb) == 1 &&
      (sati_get_cdb_byte(cdb, 3) == 0))
   {
      sequence->allocation_length = (sati_get_cdb_byte(cdb, 7) << 8) |
                                    (sati_get_cdb_byte(cdb, 8));

      switch(SATI_LOG_SENSE_GET_PAGE_CODE(cdb))
      {
         //Return Supported Log Pages log page
         case SCSI_LOG_PAGE_SUPPORTED_PAGES :
            sati_supported_log_page_construct(sequence, scsi_io);
            sequence->type = SATI_SEQUENCE_LOG_SENSE_SUPPORTED_LOG_PAGE;
            status = SATI_COMPLETE;
            break;

         //Return Self-Test Results log page
         case SCSI_LOG_PAGE_SELF_TEST :

            if((sequence->device->capabilities &
               SATI_DEVICE_CAP_SMART_SELF_TEST_SUPPORT) == 0)
            {
               sati_scsi_sense_data_construct(
                  sequence,
                  scsi_io,
                  SCSI_STATUS_CHECK_CONDITION,
                  SCSI_SENSE_ILLEGAL_REQUEST,
                  SCSI_ASC_INVALID_FIELD_IN_CDB,
                  SCSI_ASCQ_INVALID_FIELD_IN_CDB
               );
               status = SATI_FAILURE_CHECK_RESPONSE_DATA;
            }
            else
            {
               //check if 48-bit Address feature set is supported
               if((sequence->device->capabilities &
                  SATI_DEVICE_CAP_48BIT_ENABLE))
               {
                  //ATA Read Log Ext with log address set to 0x07
                  sati_ata_read_log_ext_construct(
                                     ata_io,
                                     sequence,
                                     ATA_LOG_PAGE_EXTENDED_SMART_SELF_TEST,
                                     sizeof(ATA_EXTENDED_SMART_SELF_TEST_LOG_T)
                  );
                  sequence->type =
                            SATI_SEQUENCE_LOG_SENSE_EXTENDED_SELF_TEST_LOG_PAGE;
                  status = SATI_SUCCESS;
               }
               else
               {
                  //ATA Smart Read Log with log address set to 0x06
                  sati_ata_smart_read_log_construct(
                                       ata_io,
                                       sequence,
                                       ATA_LOG_PAGE_SMART_SELF_TEST,
                                       sizeof(ATA_SMART_SELF_TEST_LOG_T)
                  );
                  sequence->type = SATI_SEQUENCE_LOG_SENSE_SELF_TEST_LOG_PAGE;
                  status = SATI_SUCCESS;
               }
            }
            break;

         //Return Informational Exceptions log page
         case SCSI_LOG_PAGE_INFORMATION_EXCEPTION :
            if(sequence->device->capabilities & SATI_DEVICE_CAP_SMART_SUPPORT)
            {
               if(sequence->device->capabilities & SATI_DEVICE_CAP_SMART_ENABLE)
               {
                  sati_ata_smart_return_status_construct(
                                       ata_io,
                                       sequence,
                                       ATA_SMART_SUB_CMD_RETURN_STATUS
                  );
                  sequence->type =
                                SATI_SEQUENCE_LOG_SENSE_INFO_EXCEPTION_LOG_PAGE;
                  status = SATI_SUCCESS;
               }
               else
               {
                  sati_scsi_sense_data_construct(
                     sequence,
                     scsi_io,
                     SCSI_STATUS_CHECK_CONDITION,
                     SCSI_SENSE_ABORTED_COMMAND,
                     SCSI_ASC_ATA_DEVICE_FEATURE_NOT_ENABLED,
                     SCSI_ASCQ_ATA_DEVICE_FEATURE_NOT_ENABLED
                  );

                  status = SATI_FAILURE_CHECK_RESPONSE_DATA;
               }
            }
            else
            {
               sati_scsi_sense_data_construct(
                  sequence,
                  scsi_io,
                  SCSI_STATUS_CHECK_CONDITION,
                  SCSI_SENSE_ILLEGAL_REQUEST,
                  SCSI_ASC_INVALID_FIELD_IN_CDB,
                  SCSI_ASCQ_INVALID_FIELD_IN_CDB
               );

               status = SATI_FAILURE_CHECK_RESPONSE_DATA;
            }
            break;
         default :
            //UNSPECIFIED SATv2r9
            sati_scsi_sense_data_construct(
               sequence,
               scsi_io,
               SCSI_STATUS_CHECK_CONDITION,
               SCSI_SENSE_ILLEGAL_REQUEST,
               SCSI_ASC_NO_ADDITIONAL_SENSE ,
               SCSI_ASCQ_NO_ADDITIONAL_SENSE
            );
            status = SATI_FAILURE_CHECK_RESPONSE_DATA;
            break;
      }
   }
   return status;
}

/**
 * @brief This method will translate the response to the SATI Log Sense
 *        translation. ATA command responses will be translated into the
 *        correct SCSI log pages to be returned by SATI.
 *
 * @return SATI_STATUS Indicates if the response translation succeeded.
 *
 */
SATI_STATUS sati_log_sense_translate_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);
   SATI_STATUS status = SATI_FAILURE;

   if(sati_get_ata_status(register_fis) & ATA_STATUS_REG_ERROR_BIT)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ABORTED_COMMAND,
         SCSI_ASC_NO_ADDITIONAL_SENSE ,
         SCSI_ASCQ_NO_ADDITIONAL_SENSE
      );
      status = SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else
   {

      void * ata_data = sati_cb_get_ata_data_address(ata_io);

      if(ata_data == NULL)
      {
         return SATI_FAILURE;
      }

      switch(sequence->type)
      {
         case SATI_SEQUENCE_LOG_SENSE_EXTENDED_SELF_TEST_LOG_PAGE:
            sati_extended_self_test_log_page_construct(
                                 sequence, scsi_io, ata_data
            );

            status = SATI_COMPLETE;
            break;

         case SATI_SEQUENCE_LOG_SENSE_SELF_TEST_LOG_PAGE:
            sati_self_test_log_page_construct(sequence, scsi_io, ata_data);
            status = SATI_COMPLETE;
            break;

         case SATI_SEQUENCE_LOG_SENSE_INFO_EXCEPTION_LOG_PAGE:
            //This function needs a d->h register fis, not ata data
            sati_information_exception_log_page_contruct(
                                 sequence, scsi_io, ata_io
            );

            status = SATI_COMPLETE;
            break;

         default:
            sati_scsi_sense_data_construct(
               sequence,
               scsi_io,
               SCSI_STATUS_CHECK_CONDITION,
               SCSI_SENSE_ABORTED_COMMAND,
               SCSI_ASC_NO_ADDITIONAL_SENSE ,
               SCSI_ASCQ_NO_ADDITIONAL_SENSE
            );
            status = SATI_FAILURE_CHECK_RESPONSE_DATA;
            break;
      }
   }
   return status;
}

#endif // !defined(DISABLE_SATI_LOG_SENSE)

