/*
 * osd_sense.h - OSD Related sense handling definitions.
 *
 * Copyright (C) 2008 Panasas Inc.  All rights reserved.
 *
 * Authors:
 *   Boaz Harrosh <bharrosh@panasas.com>
 *   Benny Halevy <bhalevy@panasas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 *
 * This file contains types and constants that are defined by the protocol
 * Note: All names and symbols are taken from the OSD standard's text.
 */
#ifndef __OSD_SENSE_H__
#define __OSD_SENSE_H__

#include <scsi/osd_protocol.h>

/* SPC3r23 4.5.6 Sense key and sense code definitions table 27 */
enum scsi_sense_keys {
	scsi_sk_no_sense        = 0x0,
	scsi_sk_recovered_error = 0x1,
	scsi_sk_not_ready       = 0x2,
	scsi_sk_medium_error    = 0x3,
	scsi_sk_hardware_error  = 0x4,
	scsi_sk_illegal_request = 0x5,
	scsi_sk_unit_attention  = 0x6,
	scsi_sk_data_protect    = 0x7,
	scsi_sk_blank_check     = 0x8,
	scsi_sk_vendor_specific = 0x9,
	scsi_sk_copy_aborted    = 0xa,
	scsi_sk_aborted_command = 0xb,
	scsi_sk_volume_overflow = 0xd,
	scsi_sk_miscompare      = 0xe,
	scsi_sk_reserved        = 0xf,
};

/* SPC3r23 4.5.6 Sense key and sense code definitions table 28 */
/* Note: only those which can be returned by an OSD target. Most of
 *       these errors are taken care of by the generic scsi layer.
 */
enum osd_additional_sense_codes {
	scsi_no_additional_sense_information			= 0x0000,
	scsi_operation_in_progress				= 0x0016,
	scsi_cleaning_requested					= 0x0017,
	scsi_lunr_cause_not_reportable				= 0x0400,
	scsi_logical_unit_is_in_process_of_becoming_ready	= 0x0401,
	scsi_lunr_initializing_command_required			= 0x0402,
	scsi_lunr_manual_intervention_required			= 0x0403,
	scsi_lunr_operation_in_progress				= 0x0407,
	scsi_lunr_selftest_in_progress				= 0x0409,
	scsi_luna_asymmetric_access_state_transition		= 0x040a,
	scsi_luna_target_port_in_standby_state			= 0x040b,
	scsi_luna_target_port_in_unavailable_state		= 0x040c,
	scsi_lunr_notify_enable_spinup_required			= 0x0411,
	scsi_logical_unit_does_not_respond_to_selection		= 0x0500,
	scsi_logical_unit_communication_failure			= 0x0800,
	scsi_logical_unit_communication_timeout			= 0x0801,
	scsi_logical_unit_communication_parity_error		= 0x0802,
	scsi_error_log_overflow					= 0x0a00,
	scsi_warning						= 0x0b00,
	scsi_warning_specified_temperature_exceeded		= 0x0b01,
	scsi_warning_enclosure_degraded				= 0x0b02,
	scsi_write_error_unexpected_unsolicited_data		= 0x0c0c,
	scsi_write_error_not_enough_unsolicited_data		= 0x0c0d,
	scsi_invalid_information_unit				= 0x0e00,
	scsi_invalid_field_in_command_information_unit		= 0x0e03,
	scsi_read_error_failed_retransmission_request		= 0x1113,
	scsi_parameter_list_length_error			= 0x1a00,
	scsi_invalid_command_operation_code			= 0x2000,
	scsi_invalid_field_in_cdb				= 0x2400,
	osd_security_audit_value_frozen				= 0x2404,
	osd_security_working_key_frozen				= 0x2405,
	osd_nonce_not_unique					= 0x2406,
	osd_nonce_timestamp_out_of_range			= 0x2407,
	scsi_logical_unit_not_supported				= 0x2500,
	scsi_invalid_field_in_parameter_list			= 0x2600,
	scsi_parameter_not_supported				= 0x2601,
	scsi_parameter_value_invalid				= 0x2602,
	scsi_invalid_release_of_persistent_reservation		= 0x2604,
	osd_invalid_dataout_buffer_integrity_check_value	= 0x260f,
	scsi_not_ready_to_ready_change_medium_may_have_changed	= 0x2800,
	scsi_power_on_reset_or_bus_device_reset_occurred	= 0x2900,
	scsi_power_on_occurred					= 0x2901,
	scsi_scsi_bus_reset_occurred				= 0x2902,
	scsi_bus_device_reset_function_occurred			= 0x2903,
	scsi_device_internal_reset				= 0x2904,
	scsi_transceiver_mode_changed_to_single_ended		= 0x2905,
	scsi_transceiver_mode_changed_to_lvd			= 0x2906,
	scsi_i_t_nexus_loss_occurred				= 0x2907,
	scsi_parameters_changed					= 0x2a00,
	scsi_mode_parameters_changed				= 0x2a01,
	scsi_asymmetric_access_state_changed			= 0x2a06,
	scsi_priority_changed					= 0x2a08,
	scsi_command_sequence_error				= 0x2c00,
	scsi_previous_busy_status				= 0x2c07,
	scsi_previous_task_set_full_status			= 0x2c08,
	scsi_previous_reservation_conflict_status		= 0x2c09,
	osd_partition_or_collection_contains_user_objects	= 0x2c0a,
	scsi_commands_cleared_by_another_initiator		= 0x2f00,
	scsi_cleaning_failure					= 0x3007,
	scsi_enclosure_failure					= 0x3400,
	scsi_enclosure_services_failure				= 0x3500,
	scsi_unsupported_enclosure_function			= 0x3501,
	scsi_enclosure_services_unavailable			= 0x3502,
	scsi_enclosure_services_transfer_failure		= 0x3503,
	scsi_enclosure_services_transfer_refused		= 0x3504,
	scsi_enclosure_services_checksum_error			= 0x3505,
	scsi_rounded_parameter					= 0x3700,
	osd_read_past_end_of_user_object			= 0x3b17,
	scsi_logical_unit_has_not_self_configured_yet		= 0x3e00,
	scsi_logical_unit_failure				= 0x3e01,
	scsi_timeout_on_logical_unit				= 0x3e02,
	scsi_logical_unit_failed_selftest			= 0x3e03,
	scsi_logical_unit_unable_to_update_selftest_log		= 0x3e04,
	scsi_target_operating_conditions_have_changed		= 0x3f00,
	scsi_microcode_has_been_changed				= 0x3f01,
	scsi_inquiry_data_has_changed				= 0x3f03,
	scsi_echo_buffer_overwritten				= 0x3f0f,
	scsi_diagnostic_failure_on_component_nn_first		= 0x4080,
	scsi_diagnostic_failure_on_component_nn_last		= 0x40ff,
	scsi_message_error					= 0x4300,
	scsi_internal_target_failure				= 0x4400,
	scsi_select_or_reselect_failure				= 0x4500,
	scsi_scsi_parity_error					= 0x4700,
	scsi_data_phase_crc_error_detected			= 0x4701,
	scsi_scsi_parity_error_detected_during_st_data_phase	= 0x4702,
	scsi_asynchronous_information_protection_error_detected	= 0x4704,
	scsi_protocol_service_crc_error				= 0x4705,
	scsi_phy_test_function_in_progress			= 0x4706,
	scsi_invalid_message_error				= 0x4900,
	scsi_command_phase_error				= 0x4a00,
	scsi_data_phase_error					= 0x4b00,
	scsi_logical_unit_failed_self_configuration		= 0x4c00,
	scsi_overlapped_commands_attempted			= 0x4e00,
	osd_quota_error						= 0x5507,
	scsi_failure_prediction_threshold_exceeded		= 0x5d00,
	scsi_failure_prediction_threshold_exceeded_false	= 0x5dff,
	scsi_voltage_fault					= 0x6500,
};

enum scsi_descriptor_types {
	scsi_sense_information			= 0x0,
	scsi_sense_command_specific_information	= 0x1,
	scsi_sense_key_specific			= 0x2,
	scsi_sense_field_replaceable_unit	= 0x3,
	scsi_sense_stream_commands		= 0x4,
	scsi_sense_block_commands		= 0x5,
	osd_sense_object_identification		= 0x6,
	osd_sense_response_integrity_check	= 0x7,
	osd_sense_attribute_identification	= 0x8,
	scsi_sense_ata_return			= 0x9,

	scsi_sense_Reserved_first		= 0x0A,
	scsi_sense_Reserved_last		= 0x7F,
	scsi_sense_Vendor_specific_first	= 0x80,
	scsi_sense_Vendor_specific_last		= 0xFF,
};

struct scsi_sense_descriptor { /* for picking into desc type */
	u8	descriptor_type; /* one of enum scsi_descriptor_types */
	u8	additional_length; /* n - 1 */
	u8	data[];
} __packed;

/* OSD deploys only scsi descriptor_based sense buffers */
struct scsi_sense_descriptor_based {
/*0*/	u8 	response_code; /* 0x72 or 0x73 */
/*1*/	u8 	sense_key; /* one of enum scsi_sense_keys (4 lower bits) */
/*2*/	__be16	additional_sense_code; /* enum osd_additional_sense_codes */
/*4*/	u8	Reserved[3];
/*7*/	u8	additional_sense_length; /* n - 7 */
/*8*/	struct	scsi_sense_descriptor ssd[0]; /* variable length, 1 or more */
} __packed;

/* some descriptors deployed by OSD */

/* SPC3r23 4.5.2.3 Command-specific information sense data descriptor */
/* Note: this is the same for descriptor_type=00 but with type=00 the
 *        Reserved[0] == 0x80 (ie. bit-7 set)
 */
struct scsi_sense_command_specific_data_descriptor {
/*0*/	u8	descriptor_type; /* (00h/01h) */
/*1*/	u8	additional_length; /* (0Ah) */
/*2*/	u8	Reserved[2];
/*4*/	__be64  information;
} __packed;
/*12*/

struct scsi_sense_key_specific_data_descriptor {
/*0*/	u8	descriptor_type; /* (02h) */
/*1*/	u8	additional_length; /* (06h) */
/*2*/	u8	Reserved[2];
/* SKSV, C/D, Reserved (2), BPV, BIT POINTER (3) */
/*4*/	u8	sksv_cd_bpv_bp;
/*5*/	__be16	value; /* field-pointer/progress-value/retry-count/... */
/*7*/	u8	Reserved2;
} __packed;
/*8*/

/* 4.16.2.1 OSD error identification sense data descriptor - table 52 */
/* Note: these bits are defined LE order for easy definition, this way the BIT()
 * number is the same as in the documentation. Below members at
 * osd_sense_identification_data_descriptor are therefore defined __le32.
 */
enum osd_command_functions_bits {
	OSD_CFB_COMMAND		 = BIT(4),
	OSD_CFB_CMD_CAP_VERIFIED = BIT(5),
	OSD_CFB_VALIDATION	 = BIT(7),
	OSD_CFB_IMP_ST_ATT	 = BIT(12),
	OSD_CFB_SET_ATT		 = BIT(20),
	OSD_CFB_SA_CAP_VERIFIED	 = BIT(21),
	OSD_CFB_GET_ATT		 = BIT(28),
	OSD_CFB_GA_CAP_VERIFIED	 = BIT(29),
};

struct osd_sense_identification_data_descriptor {
/*0*/	u8	descriptor_type; /* (06h) */
/*1*/	u8	additional_length; /* (1Eh) */
/*2*/	u8	Reserved[6];
/*8*/	__le32	not_initiated_functions; /*osd_command_functions_bits*/
/*12*/	__le32	completed_functions; /*osd_command_functions_bits*/
/*16*/ 	__be64	partition_id;
/*24*/	__be64	object_id;
} __packed;
/*32*/

struct osd_sense_response_integrity_check_descriptor {
/*0*/	u8	descriptor_type; /* (07h) */
/*1*/	u8	additional_length; /* (20h) */
/*2*/	u8	integrity_check_value[32]; /*FIXME: OSDv2_CRYPTO_KEYID_SIZE*/
} __packed;
/*34*/

struct osd_sense_attributes_data_descriptor {
/*0*/	u8	descriptor_type; /* (08h) */
/*1*/	u8	additional_length; /* (n-2) */
/*2*/	u8	Reserved[6];
	struct osd_sense_attr {
/*8*/		__be32	attr_page;
/*12*/		__be32	attr_id;
/*16*/	} sense_attrs[0]; /* 1 or more */
} __packed;
/*variable*/

/* Dig into scsi_sk_illegal_request/scsi_invalid_field_in_cdb errors */

/*FIXME: Support also field in CAPS*/
#define OSD_CDB_OFFSET(F) offsetof(struct osd_cdb_head, F)

enum osdv2_cdb_field_offset {
	OSDv1_CFO_STARTING_BYTE	= OSD_CDB_OFFSET(v1.start_address),
	OSD_CFO_STARTING_BYTE	= OSD_CDB_OFFSET(v2.start_address),
	OSD_CFO_PARTITION_ID	= OSD_CDB_OFFSET(partition),
	OSD_CFO_OBJECT_ID	= OSD_CDB_OFFSET(object),
};

#endif /* ndef __OSD_SENSE_H__ */
