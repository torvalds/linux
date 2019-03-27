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
#ifndef _SATI_UTIL_H_
#define _SATI_UTIL_H_

/**
 * @file
 * @brief This file contains all of the interface methods, macros, structures
 *        that provide general support for SATI.  Some methods can be utilized
 *        by a user to construct ATA/ATAPI commands, copy ATA device
 *        structure data, fill in sense data, etc.
 */

#include <sys/param.h>

#include <dev/isci/scil/sati_types.h>
#include <dev/isci/scil/sati_translator_sequence.h>

#include <dev/isci/scil/intel_sata.h>
#include <dev/isci/scil/intel_sas.h>

/**
 * This macro allows the translator to be able to handle environments where
 * the contents of the CDB are of a different endian nature of byte swapped
 * in some fashion.
 */
#define sati_get_cdb_byte(the_cdb, index)    (the_cdb)[(index)]

#define sati_get_ata_status(the_reg_fis) ((SATA_FIS_REG_D2H_T*)(the_reg_fis))->status
#define sati_get_ata_error(the_reg_fis) ((SATA_FIS_REG_D2H_T*)(the_reg_fis))->error

#define sati_get_ata_command(the_reg_fis)   \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->command

#define sati_get_ata_sector_count(the_reg_fis) \
   ((SATA_FIS_REG_D2H_T*)(the_reg_fis))->sector_count
#define sati_get_ata_sector_count_exp(the_reg_fis) \
   ((SATA_FIS_REG_D2H_T*)(the_reg_fis))->sector_count_exp
#define sati_get_ata_lba_low(the_reg_fis) \
   ((SATA_FIS_REG_D2H_T*)(the_reg_fis))->lba_low
#define sati_get_ata_lba_mid(the_reg_fis) \
   ((SATA_FIS_REG_D2H_T*)(the_reg_fis))->lba_mid
#define sati_get_ata_lba_high(the_reg_fis) \
   ((SATA_FIS_REG_D2H_T*)(the_reg_fis))->lba_high
#define sati_get_ata_sector_count_ext(the_reg_fis) \
   ((SATA_FIS_REG_D2H_T*)(the_reg_fis))->sector_count_exp
#define sati_get_ata_lba_low_ext(the_reg_fis) \
   ((SATA_FIS_REG_D2H_T*)(the_reg_fis))->lba_low_exp
#define sati_get_ata_lba_mid_ext(the_reg_fis) \
   ((SATA_FIS_REG_D2H_T*)(the_reg_fis))->lba_mid_exp
#define sati_get_ata_lba_high_ext(the_reg_fis) \
   ((SATA_FIS_REG_D2H_T*)(the_reg_fis))->lba_high_exp
#define sati_get_ata_device(the_reg_fis) \
   ((SATA_FIS_REG_D2H_T*)(the_reg_fis))->device

#define sati_set_ata_status(the_reg_fis, value) \
   ((SATA_FIS_REG_D2H_T*)(the_reg_fis))->status = (value)
#define sati_set_sata_fis_type(the_reg_fis, value) \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->fis_type = (value)
#define sati_set_sata_command_flag(the_reg_fis) \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->command_flag = 1
#define sati_clear_sata_command_flag(the_reg_fis) \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->command_flag = 0

#define sati_set_ata_command(the_reg_fis, value)          \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->command = (value)
#define sati_set_ata_features(the_reg_fis, value)         \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->features = (value)
#define sati_set_ata_features_exp(the_reg_fis, value)     \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->features_exp = (value)
#define sati_set_ata_control(the_reg_fis, value)          \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->control = (value)
#define sati_set_ata_sector_count(the_reg_fis, value)     \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->sector_count = (value)
#define sati_set_ata_sector_count_exp(the_reg_fis, value) \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->sector_count_exp = (value)
#define sati_set_ata_lba_low(the_reg_fis, value)          \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->lba_low = (value)
#define sati_set_ata_lba_mid(the_reg_fis, value)          \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->lba_mid = (value)
#define sati_set_ata_lba_high(the_reg_fis, value)         \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->lba_high = (value)
#define sati_set_ata_lba_low_exp(the_reg_fis, value)      \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->lba_low_exp = (value)
#define sati_set_ata_lba_mid_exp(the_reg_fis, value)      \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->lba_mid_exp = (value)
#define sati_set_ata_lba_high_exp(the_reg_fis, value)     \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->lba_high_exp = (value)
#define sati_set_ata_device_head(the_reg_fis, value)      \
   ((SATA_FIS_REG_H2D_T*)(the_reg_fis))->device = (value)

#define ATA_MID_REGISTER_THRESHOLD_EXCEEDED    0xF4
#define ATA_HIGH_REGISTER_THRESHOLD_EXCEEDED   0x2C

#define ATA_MICROCODE_OFFSET_DOWNLOAD        0x03
#define ATA_MICROCODE_DOWNLOAD_SAVE          0x07

void sati_ata_non_data_command(
   void                        * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T  * sequence
);

void sati_ata_identify_device_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

void sati_ata_execute_device_diagnostic_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

void sati_ata_identify_device_copy_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * destination_scsi_io,
   U32                          destination_offset,
   U8                         * source_buffer,
   U32                          source_offset,
   U32                          length,
   BOOL                         use_printable_chars
);

void sati_copy_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * destination_scsi_io,
   U32                          destination_offset,
   U8                         * source_buffer,
   U32                          length
);

void sati_ata_identify_device_get_sector_info(
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                        * lba_high,
   U32                        * lba_low,
   U32                        * sector_size
);

void sati_ata_check_power_mode_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

U8 sati_scsi_get_sense_data_length(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void * scsi_io
);

void sati_scsi_common_response_iu_construct(
   SCI_SSP_RESPONSE_IU_T *      rsp_iu,
   U8                           scsi_status,
   U8                           sense_data_length,
   U8                           data_present
);

void sati_scsi_sense_data_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U8                           status,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
);

void sati_scsi_fixed_sense_data_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U8                           response_code,
   U8                           scsi_status,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
);

void sati_scsi_descriptor_sense_data_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U8                           response_code,
   U8                           scsi_status,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
);

void sati_scsi_read_ncq_error_sense_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_input_data,
   U8                           scsi_status,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
);

void sati_scsi_read_error_sense_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U8                           status,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
);

void sati_scsi_response_data_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U8                           response_data
);

void sati_get_data_byte(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U32                          byte_offset,
   U8                         * value
);

void sati_set_data_byte(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U32                          byte_offset,
   U8                           value
);

void sati_set_data_dword(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U32                          byte_offset,
   U32                          value
);

void sati_set_sense_data_byte(
   U8  * sense_data,
   U32   max_sense_data_len,
   U32   byte_offset,
   U8    value
);

void sati_ata_flush_cache_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

void sati_ata_standby_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   U16                          count
);

void sati_ata_standby_immediate_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

void sati_ata_idle_immediate_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

void sati_ata_idle_immediate_unload_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

void sati_ata_idle_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

void sati_ata_media_eject_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

void sati_ata_read_verify_sectors_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

void sati_ata_smart_return_status_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   U8                           feature_value
);

void sati_ata_smart_read_log_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   U8                           log_address,
   U32                          transfer_length
);

void sati_ata_write_uncorrectable_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   U8                           feature_value
);

void sati_ata_set_features_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   U8                           feature
);

void sati_ata_read_log_ext_construct(
   void                          * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T    * sequence,
   U8                              log_address,
   U32                             transfer_length
);

BOOL sati_device_state_stopped(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io
);

void sati_ata_read_buffer_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

void sati_ata_write_buffer_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

void sati_ata_download_microcode_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   U8                           mode,
   U32                          block_count,
   U32                          buffer_offset
);

#endif // _SATI_UTIL_H_

