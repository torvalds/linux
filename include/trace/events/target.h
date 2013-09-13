#undef TRACE_SYSTEM
#define TRACE_SYSTEM target

#if !defined(_TRACE_TARGET_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TARGET_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <target/target_core_base.h>

/* cribbed verbatim from <trace/event/scsi.h> */
#define scsi_opcode_name(opcode)	{ opcode, #opcode }
#define show_opcode_name(val)					\
	__print_symbolic(val,					\
		scsi_opcode_name(TEST_UNIT_READY),		\
		scsi_opcode_name(REZERO_UNIT),			\
		scsi_opcode_name(REQUEST_SENSE),		\
		scsi_opcode_name(FORMAT_UNIT),			\
		scsi_opcode_name(READ_BLOCK_LIMITS),		\
		scsi_opcode_name(REASSIGN_BLOCKS),		\
		scsi_opcode_name(INITIALIZE_ELEMENT_STATUS),	\
		scsi_opcode_name(READ_6),			\
		scsi_opcode_name(WRITE_6),			\
		scsi_opcode_name(SEEK_6),			\
		scsi_opcode_name(READ_REVERSE),			\
		scsi_opcode_name(WRITE_FILEMARKS),		\
		scsi_opcode_name(SPACE),			\
		scsi_opcode_name(INQUIRY),			\
		scsi_opcode_name(RECOVER_BUFFERED_DATA),	\
		scsi_opcode_name(MODE_SELECT),			\
		scsi_opcode_name(RESERVE),			\
		scsi_opcode_name(RELEASE),			\
		scsi_opcode_name(COPY),				\
		scsi_opcode_name(ERASE),			\
		scsi_opcode_name(MODE_SENSE),			\
		scsi_opcode_name(START_STOP),			\
		scsi_opcode_name(RECEIVE_DIAGNOSTIC),		\
		scsi_opcode_name(SEND_DIAGNOSTIC),		\
		scsi_opcode_name(ALLOW_MEDIUM_REMOVAL),		\
		scsi_opcode_name(SET_WINDOW),			\
		scsi_opcode_name(READ_CAPACITY),		\
		scsi_opcode_name(READ_10),			\
		scsi_opcode_name(WRITE_10),			\
		scsi_opcode_name(SEEK_10),			\
		scsi_opcode_name(POSITION_TO_ELEMENT),		\
		scsi_opcode_name(WRITE_VERIFY),			\
		scsi_opcode_name(VERIFY),			\
		scsi_opcode_name(SEARCH_HIGH),			\
		scsi_opcode_name(SEARCH_EQUAL),			\
		scsi_opcode_name(SEARCH_LOW),			\
		scsi_opcode_name(SET_LIMITS),			\
		scsi_opcode_name(PRE_FETCH),			\
		scsi_opcode_name(READ_POSITION),		\
		scsi_opcode_name(SYNCHRONIZE_CACHE),		\
		scsi_opcode_name(LOCK_UNLOCK_CACHE),		\
		scsi_opcode_name(READ_DEFECT_DATA),		\
		scsi_opcode_name(MEDIUM_SCAN),			\
		scsi_opcode_name(COMPARE),			\
		scsi_opcode_name(COPY_VERIFY),			\
		scsi_opcode_name(WRITE_BUFFER),			\
		scsi_opcode_name(READ_BUFFER),			\
		scsi_opcode_name(UPDATE_BLOCK),			\
		scsi_opcode_name(READ_LONG),			\
		scsi_opcode_name(WRITE_LONG),			\
		scsi_opcode_name(CHANGE_DEFINITION),		\
		scsi_opcode_name(WRITE_SAME),			\
		scsi_opcode_name(UNMAP),			\
		scsi_opcode_name(READ_TOC),			\
		scsi_opcode_name(LOG_SELECT),			\
		scsi_opcode_name(LOG_SENSE),			\
		scsi_opcode_name(XDWRITEREAD_10),		\
		scsi_opcode_name(MODE_SELECT_10),		\
		scsi_opcode_name(RESERVE_10),			\
		scsi_opcode_name(RELEASE_10),			\
		scsi_opcode_name(MODE_SENSE_10),		\
		scsi_opcode_name(PERSISTENT_RESERVE_IN),	\
		scsi_opcode_name(PERSISTENT_RESERVE_OUT),	\
		scsi_opcode_name(VARIABLE_LENGTH_CMD),		\
		scsi_opcode_name(REPORT_LUNS),			\
		scsi_opcode_name(MAINTENANCE_IN),		\
		scsi_opcode_name(MAINTENANCE_OUT),		\
		scsi_opcode_name(MOVE_MEDIUM),			\
		scsi_opcode_name(EXCHANGE_MEDIUM),		\
		scsi_opcode_name(READ_12),			\
		scsi_opcode_name(WRITE_12),			\
		scsi_opcode_name(WRITE_VERIFY_12),		\
		scsi_opcode_name(SEARCH_HIGH_12),		\
		scsi_opcode_name(SEARCH_EQUAL_12),		\
		scsi_opcode_name(SEARCH_LOW_12),		\
		scsi_opcode_name(READ_ELEMENT_STATUS),		\
		scsi_opcode_name(SEND_VOLUME_TAG),		\
		scsi_opcode_name(WRITE_LONG_2),			\
		scsi_opcode_name(READ_16),			\
		scsi_opcode_name(WRITE_16),			\
		scsi_opcode_name(VERIFY_16),			\
		scsi_opcode_name(WRITE_SAME_16),		\
		scsi_opcode_name(SERVICE_ACTION_IN),		\
		scsi_opcode_name(SAI_READ_CAPACITY_16),		\
		scsi_opcode_name(SAI_GET_LBA_STATUS),		\
		scsi_opcode_name(MI_REPORT_TARGET_PGS),		\
		scsi_opcode_name(MO_SET_TARGET_PGS),		\
		scsi_opcode_name(READ_32),			\
		scsi_opcode_name(WRITE_32),			\
		scsi_opcode_name(WRITE_SAME_32),		\
		scsi_opcode_name(ATA_16),			\
		scsi_opcode_name(ATA_12))

#define show_task_attribute_name(val)				\
	__print_symbolic(val,					\
		{ MSG_SIMPLE_TAG,	"SIMPLE"	},	\
		{ MSG_HEAD_TAG,		"HEAD"		},	\
		{ MSG_ORDERED_TAG,	"ORDERED"	},	\
		{ MSG_ACA_TAG,		"ACA"		} )

#define show_scsi_status_name(val)				\
	__print_symbolic(val,					\
		{ SAM_STAT_GOOD,	"GOOD" },		\
		{ SAM_STAT_CHECK_CONDITION, "CHECK CONDITION" }, \
		{ SAM_STAT_CONDITION_MET, "CONDITION MET" },	\
		{ SAM_STAT_BUSY,	"BUSY" },		\
		{ SAM_STAT_INTERMEDIATE, "INTERMEDIATE" },	\
		{ SAM_STAT_INTERMEDIATE_CONDITION_MET, "INTERMEDIATE CONDITION MET" }, \
		{ SAM_STAT_RESERVATION_CONFLICT, "RESERVATION CONFLICT" }, \
		{ SAM_STAT_COMMAND_TERMINATED, "COMMAND TERMINATED" }, \
		{ SAM_STAT_TASK_SET_FULL, "TASK SET FULL" },	\
		{ SAM_STAT_ACA_ACTIVE, "ACA ACTIVE" },		\
		{ SAM_STAT_TASK_ABORTED, "TASK ABORTED" } )

TRACE_EVENT(target_sequencer_start,

	TP_PROTO(struct se_cmd *cmd),

	TP_ARGS(cmd),

	TP_STRUCT__entry(
		__field( unsigned int,	unpacked_lun	)
		__field( unsigned int,	opcode		)
		__field( unsigned int,	data_length	)
		__field( unsigned int,	task_attribute  )
		__array( unsigned char,	cdb, TCM_MAX_COMMAND_SIZE	)
		__string( initiator,	cmd->se_sess->se_node_acl->initiatorname	)
	),

	TP_fast_assign(
		__entry->unpacked_lun	= cmd->se_lun->unpacked_lun;
		__entry->opcode		= cmd->t_task_cdb[0];
		__entry->data_length	= cmd->data_length;
		__entry->task_attribute	= cmd->sam_task_attr;
		memcpy(__entry->cdb, cmd->t_task_cdb, TCM_MAX_COMMAND_SIZE);
		__assign_str(initiator, cmd->se_sess->se_node_acl->initiatorname);
	),

	TP_printk("%s -> LUN %03u %s data_length %6u  CDB %s  (TA:%s C:%02x)",
		  __get_str(initiator), __entry->unpacked_lun,
		  show_opcode_name(__entry->opcode),
		  __entry->data_length, __print_hex(__entry->cdb, 16),
		  show_task_attribute_name(__entry->task_attribute),
		  scsi_command_size(__entry->cdb) <= 16 ?
			__entry->cdb[scsi_command_size(__entry->cdb) - 1] :
			__entry->cdb[1]
	)
);

TRACE_EVENT(target_cmd_complete,

	TP_PROTO(struct se_cmd *cmd),

	TP_ARGS(cmd),

	TP_STRUCT__entry(
		__field( unsigned int,	unpacked_lun	)
		__field( unsigned int,	opcode		)
		__field( unsigned int,	data_length	)
		__field( unsigned int,	task_attribute  )
		__field( unsigned char,	scsi_status	)
		__field( unsigned char,	sense_length	)
		__array( unsigned char,	cdb, TCM_MAX_COMMAND_SIZE	)
		__array( unsigned char,	sense_data, 18	)
		__string(initiator,	cmd->se_sess->se_node_acl->initiatorname)
	),

	TP_fast_assign(
		__entry->unpacked_lun	= cmd->se_lun->unpacked_lun;
		__entry->opcode		= cmd->t_task_cdb[0];
		__entry->data_length	= cmd->data_length;
		__entry->task_attribute	= cmd->sam_task_attr;
		__entry->scsi_status	= cmd->scsi_status;
		__entry->sense_length	= cmd->scsi_status == SAM_STAT_CHECK_CONDITION ?
			min(18, ((u8 *) cmd->sense_buffer)[SPC_ADD_SENSE_LEN_OFFSET] + 8) : 0;
		memcpy(__entry->cdb, cmd->t_task_cdb, TCM_MAX_COMMAND_SIZE);
		memcpy(__entry->sense_data, cmd->sense_buffer, __entry->sense_length);
		__assign_str(initiator, cmd->se_sess->se_node_acl->initiatorname);
	),

	TP_printk("%s <- LUN %03u status %s (sense len %d%s%s)  %s data_length %6u  CDB %s  (TA:%s C:%02x)",
		  __get_str(initiator), __entry->unpacked_lun,
		  show_scsi_status_name(__entry->scsi_status),
		  __entry->sense_length, __entry->sense_length ? " / " : "",
		  __print_hex(__entry->sense_data, __entry->sense_length),
		  show_opcode_name(__entry->opcode),
		  __entry->data_length, __print_hex(__entry->cdb, 16),
		  show_task_attribute_name(__entry->task_attribute),
		  scsi_command_size(__entry->cdb) <= 16 ?
			__entry->cdb[scsi_command_size(__entry->cdb) - 1] :
			__entry->cdb[1]
	)
);

#endif /*  _TRACE_TARGET_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
