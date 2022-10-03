/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM libata

#if !defined(_TRACE_LIBATA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_LIBATA_H

#include <linux/ata.h>
#include <linux/libata.h>
#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#define ata_opcode_name(opcode)	{ opcode, #opcode }
#define show_opcode_name(val)					\
	__print_symbolic(val,					\
		 ata_opcode_name(ATA_CMD_DEV_RESET),		\
		 ata_opcode_name(ATA_CMD_CHK_POWER),		\
		 ata_opcode_name(ATA_CMD_STANDBY),		\
		 ata_opcode_name(ATA_CMD_IDLE),			\
		 ata_opcode_name(ATA_CMD_EDD),			\
		 ata_opcode_name(ATA_CMD_DOWNLOAD_MICRO),	\
		 ata_opcode_name(ATA_CMD_DOWNLOAD_MICRO_DMA),	\
		 ata_opcode_name(ATA_CMD_NOP),			\
		 ata_opcode_name(ATA_CMD_FLUSH),		\
		 ata_opcode_name(ATA_CMD_FLUSH_EXT),		\
		 ata_opcode_name(ATA_CMD_ID_ATA),		\
		 ata_opcode_name(ATA_CMD_ID_ATAPI),		\
		 ata_opcode_name(ATA_CMD_SERVICE),		\
		 ata_opcode_name(ATA_CMD_READ),			\
		 ata_opcode_name(ATA_CMD_READ_EXT),		\
		 ata_opcode_name(ATA_CMD_READ_QUEUED),		\
		 ata_opcode_name(ATA_CMD_READ_STREAM_EXT),	\
		 ata_opcode_name(ATA_CMD_READ_STREAM_DMA_EXT),	\
		 ata_opcode_name(ATA_CMD_WRITE),		\
		 ata_opcode_name(ATA_CMD_WRITE_EXT),		\
		 ata_opcode_name(ATA_CMD_WRITE_QUEUED),		\
		 ata_opcode_name(ATA_CMD_WRITE_STREAM_EXT),	\
		 ata_opcode_name(ATA_CMD_WRITE_STREAM_DMA_EXT), \
		 ata_opcode_name(ATA_CMD_WRITE_FUA_EXT),	\
		 ata_opcode_name(ATA_CMD_WRITE_QUEUED_FUA_EXT), \
		 ata_opcode_name(ATA_CMD_FPDMA_READ),		\
		 ata_opcode_name(ATA_CMD_FPDMA_WRITE),		\
		 ata_opcode_name(ATA_CMD_NCQ_NON_DATA),		\
		 ata_opcode_name(ATA_CMD_FPDMA_SEND),		\
		 ata_opcode_name(ATA_CMD_FPDMA_RECV),		\
		 ata_opcode_name(ATA_CMD_PIO_READ),		\
		 ata_opcode_name(ATA_CMD_PIO_READ_EXT),		\
		 ata_opcode_name(ATA_CMD_PIO_WRITE),		\
		 ata_opcode_name(ATA_CMD_PIO_WRITE_EXT),	\
		 ata_opcode_name(ATA_CMD_READ_MULTI),		\
		 ata_opcode_name(ATA_CMD_READ_MULTI_EXT),	\
		 ata_opcode_name(ATA_CMD_WRITE_MULTI),		\
		 ata_opcode_name(ATA_CMD_WRITE_MULTI_EXT),	\
		 ata_opcode_name(ATA_CMD_WRITE_MULTI_FUA_EXT),	\
		 ata_opcode_name(ATA_CMD_SET_FEATURES),		\
		 ata_opcode_name(ATA_CMD_SET_MULTI),		\
		 ata_opcode_name(ATA_CMD_PACKET),		\
		 ata_opcode_name(ATA_CMD_VERIFY),		\
		 ata_opcode_name(ATA_CMD_VERIFY_EXT),		\
		 ata_opcode_name(ATA_CMD_WRITE_UNCORR_EXT),	\
		 ata_opcode_name(ATA_CMD_STANDBYNOW1),		\
		 ata_opcode_name(ATA_CMD_IDLEIMMEDIATE),	\
		 ata_opcode_name(ATA_CMD_SLEEP),		\
		 ata_opcode_name(ATA_CMD_INIT_DEV_PARAMS),	\
		 ata_opcode_name(ATA_CMD_READ_NATIVE_MAX),	\
		 ata_opcode_name(ATA_CMD_READ_NATIVE_MAX_EXT),	\
		 ata_opcode_name(ATA_CMD_SET_MAX),		\
		 ata_opcode_name(ATA_CMD_SET_MAX_EXT),		\
		 ata_opcode_name(ATA_CMD_READ_LOG_EXT),		\
		 ata_opcode_name(ATA_CMD_WRITE_LOG_EXT),	\
		 ata_opcode_name(ATA_CMD_READ_LOG_DMA_EXT),	\
		 ata_opcode_name(ATA_CMD_WRITE_LOG_DMA_EXT),	\
		 ata_opcode_name(ATA_CMD_TRUSTED_NONDATA),	\
		 ata_opcode_name(ATA_CMD_TRUSTED_RCV),		\
		 ata_opcode_name(ATA_CMD_TRUSTED_RCV_DMA),	\
		 ata_opcode_name(ATA_CMD_TRUSTED_SND),		\
		 ata_opcode_name(ATA_CMD_TRUSTED_SND_DMA),	\
		 ata_opcode_name(ATA_CMD_PMP_READ),		\
		 ata_opcode_name(ATA_CMD_PMP_READ_DMA),		\
		 ata_opcode_name(ATA_CMD_PMP_WRITE),		\
		 ata_opcode_name(ATA_CMD_PMP_WRITE_DMA),	\
		 ata_opcode_name(ATA_CMD_CONF_OVERLAY),		\
		 ata_opcode_name(ATA_CMD_SEC_SET_PASS),		\
		 ata_opcode_name(ATA_CMD_SEC_UNLOCK),		\
		 ata_opcode_name(ATA_CMD_SEC_ERASE_PREP),	\
		 ata_opcode_name(ATA_CMD_SEC_ERASE_UNIT),	\
		 ata_opcode_name(ATA_CMD_SEC_FREEZE_LOCK),	\
		 ata_opcode_name(ATA_CMD_SEC_DISABLE_PASS),	\
		 ata_opcode_name(ATA_CMD_CONFIG_STREAM),	\
		 ata_opcode_name(ATA_CMD_SMART),		\
		 ata_opcode_name(ATA_CMD_MEDIA_LOCK),		\
		 ata_opcode_name(ATA_CMD_MEDIA_UNLOCK),		\
		 ata_opcode_name(ATA_CMD_DSM),			\
		 ata_opcode_name(ATA_CMD_CHK_MED_CRD_TYP),	\
		 ata_opcode_name(ATA_CMD_CFA_REQ_EXT_ERR),	\
		 ata_opcode_name(ATA_CMD_CFA_WRITE_NE),		\
		 ata_opcode_name(ATA_CMD_CFA_TRANS_SECT),	\
		 ata_opcode_name(ATA_CMD_CFA_ERASE),		\
		 ata_opcode_name(ATA_CMD_CFA_WRITE_MULT_NE),	\
		 ata_opcode_name(ATA_CMD_REQ_SENSE_DATA),	\
		 ata_opcode_name(ATA_CMD_SANITIZE_DEVICE),	\
		 ata_opcode_name(ATA_CMD_ZAC_MGMT_IN),		\
		 ata_opcode_name(ATA_CMD_ZAC_MGMT_OUT),		\
		 ata_opcode_name(ATA_CMD_RESTORE),		\
		 ata_opcode_name(ATA_CMD_READ_LONG),		\
		 ata_opcode_name(ATA_CMD_READ_LONG_ONCE),	\
		 ata_opcode_name(ATA_CMD_WRITE_LONG),		\
		 ata_opcode_name(ATA_CMD_WRITE_LONG_ONCE))

#define ata_error_name(result)	{ result, #result }
#define show_error_name(val)				\
	__print_symbolic(val,				\
		ata_error_name(ATA_ICRC),		\
		ata_error_name(ATA_UNC),		\
		ata_error_name(ATA_MC),			\
		ata_error_name(ATA_IDNF),		\
		ata_error_name(ATA_MCR),		\
		ata_error_name(ATA_ABORTED),		\
		ata_error_name(ATA_TRK0NF),		\
		ata_error_name(ATA_AMNF))

#define ata_protocol_name(proto)	{ proto, #proto }
#define show_protocol_name(val)				\
	__print_symbolic(val,				\
		ata_protocol_name(ATA_PROT_UNKNOWN),	\
		ata_protocol_name(ATA_PROT_NODATA),	\
		ata_protocol_name(ATA_PROT_PIO),	\
		ata_protocol_name(ATA_PROT_DMA),	\
		ata_protocol_name(ATA_PROT_NCQ),	\
		ata_protocol_name(ATA_PROT_NCQ_NODATA),	\
		ata_protocol_name(ATAPI_PROT_NODATA),	\
		ata_protocol_name(ATAPI_PROT_PIO),	\
		ata_protocol_name(ATAPI_PROT_DMA))

#define ata_class_name(class)	{ class, #class }
#define show_class_name(val)				\
	__print_symbolic(val,				\
		ata_class_name(ATA_DEV_UNKNOWN),	\
		ata_class_name(ATA_DEV_ATA),		\
		ata_class_name(ATA_DEV_ATA_UNSUP),	\
		ata_class_name(ATA_DEV_ATAPI),		\
		ata_class_name(ATA_DEV_ATAPI_UNSUP),	\
		ata_class_name(ATA_DEV_PMP),		\
		ata_class_name(ATA_DEV_PMP_UNSUP),	\
		ata_class_name(ATA_DEV_SEMB),		\
		ata_class_name(ATA_DEV_SEMB_UNSUP),	\
		ata_class_name(ATA_DEV_ZAC),		\
		ata_class_name(ATA_DEV_ZAC_UNSUP),	\
		ata_class_name(ATA_DEV_NONE))

#define ata_sff_hsm_state_name(state)	{ state, #state }
#define show_sff_hsm_state_name(val)				\
    __print_symbolic(val,				\
		ata_sff_hsm_state_name(HSM_ST_IDLE),	\
		ata_sff_hsm_state_name(HSM_ST_FIRST),	\
		ata_sff_hsm_state_name(HSM_ST),		\
		ata_sff_hsm_state_name(HSM_ST_LAST),	\
		ata_sff_hsm_state_name(HSM_ST_ERR))

const char *libata_trace_parse_status(struct trace_seq*, unsigned char);
#define __parse_status(s) libata_trace_parse_status(p, s)

const char *libata_trace_parse_host_stat(struct trace_seq *, unsigned char);
#define __parse_host_stat(s) libata_trace_parse_host_stat(p, s)

const char *libata_trace_parse_eh_action(struct trace_seq *, unsigned int);
#define __parse_eh_action(a) libata_trace_parse_eh_action(p, a)

const char *libata_trace_parse_eh_err_mask(struct trace_seq *, unsigned int);
#define __parse_eh_err_mask(m) libata_trace_parse_eh_err_mask(p, m)

const char *libata_trace_parse_qc_flags(struct trace_seq *, unsigned int);
#define __parse_qc_flags(f) libata_trace_parse_qc_flags(p, f)

const char *libata_trace_parse_tf_flags(struct trace_seq *, unsigned int);
#define __parse_tf_flags(f) libata_trace_parse_tf_flags(p, f)

const char *libata_trace_parse_subcmd(struct trace_seq *, unsigned char,
				      unsigned char, unsigned char);
#define __parse_subcmd(c,f,h) libata_trace_parse_subcmd(p, c, f, h)

DECLARE_EVENT_CLASS(ata_qc_issue_template,

	TP_PROTO(struct ata_queued_cmd *qc),

	TP_ARGS(qc),

	TP_STRUCT__entry(
		__field( unsigned int,	ata_port )
		__field( unsigned int,	ata_dev	)
		__field( unsigned int,	tag	)
		__field( unsigned char,	cmd	)
		__field( unsigned char,	dev	)
		__field( unsigned char,	lbal	)
		__field( unsigned char,	lbam	)
		__field( unsigned char,	lbah	)
		__field( unsigned char,	nsect	)
		__field( unsigned char,	feature	)
		__field( unsigned char,	hob_lbal )
		__field( unsigned char,	hob_lbam )
		__field( unsigned char,	hob_lbah )
		__field( unsigned char,	hob_nsect )
		__field( unsigned char,	hob_feature )
		__field( unsigned char,	ctl )
		__field( unsigned char,	proto )
		__field( unsigned long,	flags )
	),

	TP_fast_assign(
		__entry->ata_port	= qc->ap->print_id;
		__entry->ata_dev	= qc->dev->link->pmp + qc->dev->devno;
		__entry->tag		= qc->tag;
		__entry->proto		= qc->tf.protocol;
		__entry->cmd		= qc->tf.command;
		__entry->dev		= qc->tf.device;
		__entry->lbal		= qc->tf.lbal;
		__entry->lbam		= qc->tf.lbam;
		__entry->lbah		= qc->tf.lbah;
		__entry->hob_lbal	= qc->tf.hob_lbal;
		__entry->hob_lbam	= qc->tf.hob_lbam;
		__entry->hob_lbah	= qc->tf.hob_lbah;
		__entry->feature	= qc->tf.feature;
		__entry->hob_feature	= qc->tf.hob_feature;
		__entry->nsect		= qc->tf.nsect;
		__entry->hob_nsect	= qc->tf.hob_nsect;
	),

	TP_printk("ata_port=%u ata_dev=%u tag=%d proto=%s cmd=%s%s " \
		  " tf=(%02x/%02x:%02x:%02x:%02x:%02x/%02x:%02x:%02x:%02x:%02x/%02x)",
		  __entry->ata_port, __entry->ata_dev, __entry->tag,
		  show_protocol_name(__entry->proto),
		  show_opcode_name(__entry->cmd),
		  __parse_subcmd(__entry->cmd, __entry->feature, __entry->hob_nsect),
		  __entry->cmd, __entry->feature, __entry->nsect,
		  __entry->lbal, __entry->lbam, __entry->lbah,
		  __entry->hob_feature, __entry->hob_nsect,
		  __entry->hob_lbal, __entry->hob_lbam, __entry->hob_lbah,
		  __entry->dev)
);

DEFINE_EVENT(ata_qc_issue_template, ata_qc_prep,
	     TP_PROTO(struct ata_queued_cmd *qc),
	     TP_ARGS(qc));

DEFINE_EVENT(ata_qc_issue_template, ata_qc_issue,
	     TP_PROTO(struct ata_queued_cmd *qc),
	     TP_ARGS(qc));

DECLARE_EVENT_CLASS(ata_qc_complete_template,

	TP_PROTO(struct ata_queued_cmd *qc),

	TP_ARGS(qc),

	TP_STRUCT__entry(
		__field( unsigned int,	ata_port )
		__field( unsigned int,	ata_dev	)
		__field( unsigned int,	tag	)
		__field( unsigned char,	status	)
		__field( unsigned char,	dev	)
		__field( unsigned char,	lbal	)
		__field( unsigned char,	lbam	)
		__field( unsigned char,	lbah	)
		__field( unsigned char,	nsect	)
		__field( unsigned char,	error	)
		__field( unsigned char,	hob_lbal )
		__field( unsigned char,	hob_lbam )
		__field( unsigned char,	hob_lbah )
		__field( unsigned char,	hob_nsect )
		__field( unsigned char,	hob_feature )
		__field( unsigned char,	ctl )
		__field( unsigned long,	flags )
	),

	TP_fast_assign(
		__entry->ata_port	= qc->ap->print_id;
		__entry->ata_dev	= qc->dev->link->pmp + qc->dev->devno;
		__entry->tag		= qc->tag;
		__entry->status		= qc->result_tf.command;
		__entry->dev		= qc->result_tf.device;
		__entry->lbal		= qc->result_tf.lbal;
		__entry->lbam		= qc->result_tf.lbam;
		__entry->lbah		= qc->result_tf.lbah;
		__entry->hob_lbal	= qc->result_tf.hob_lbal;
		__entry->hob_lbam	= qc->result_tf.hob_lbam;
		__entry->hob_lbah	= qc->result_tf.hob_lbah;
		__entry->error		= qc->result_tf.feature;
		__entry->hob_feature	= qc->result_tf.hob_feature;
		__entry->nsect		= qc->result_tf.nsect;
		__entry->hob_nsect	= qc->result_tf.hob_nsect;
		__entry->flags		= qc->flags;
	),

	TP_printk("ata_port=%u ata_dev=%u tag=%d flags=%s status=%s " \
		  " res=(%02x/%02x:%02x:%02x:%02x:%02x/%02x:%02x:%02x:%02x:%02x/%02x)",
		  __entry->ata_port, __entry->ata_dev, __entry->tag,
		  __parse_qc_flags(__entry->flags),
		  __parse_status(__entry->status),
		  __entry->status, __entry->error, __entry->nsect,
		  __entry->lbal, __entry->lbam, __entry->lbah,
		  __entry->hob_feature, __entry->hob_nsect,
		  __entry->hob_lbal, __entry->hob_lbam, __entry->hob_lbah,
		  __entry->dev)
);

DEFINE_EVENT(ata_qc_complete_template, ata_qc_complete_internal,
	     TP_PROTO(struct ata_queued_cmd *qc),
	     TP_ARGS(qc));

DEFINE_EVENT(ata_qc_complete_template, ata_qc_complete_failed,
	     TP_PROTO(struct ata_queued_cmd *qc),
	     TP_ARGS(qc));

DEFINE_EVENT(ata_qc_complete_template, ata_qc_complete_done,
	     TP_PROTO(struct ata_queued_cmd *qc),
	     TP_ARGS(qc));

TRACE_EVENT(ata_tf_load,

	TP_PROTO(struct ata_port *ap, const struct ata_taskfile *tf),

	TP_ARGS(ap, tf),

	TP_STRUCT__entry(
		__field( unsigned int,	ata_port )
		__field( unsigned char,	cmd	)
		__field( unsigned char,	dev	)
		__field( unsigned char,	lbal	)
		__field( unsigned char,	lbam	)
		__field( unsigned char,	lbah	)
		__field( unsigned char,	nsect	)
		__field( unsigned char,	feature	)
		__field( unsigned char,	hob_lbal )
		__field( unsigned char,	hob_lbam )
		__field( unsigned char,	hob_lbah )
		__field( unsigned char,	hob_nsect )
		__field( unsigned char,	hob_feature )
		__field( unsigned char,	proto	)
	),

	TP_fast_assign(
		__entry->ata_port	= ap->print_id;
		__entry->proto		= tf->protocol;
		__entry->cmd		= tf->command;
		__entry->dev		= tf->device;
		__entry->lbal		= tf->lbal;
		__entry->lbam		= tf->lbam;
		__entry->lbah		= tf->lbah;
		__entry->hob_lbal	= tf->hob_lbal;
		__entry->hob_lbam	= tf->hob_lbam;
		__entry->hob_lbah	= tf->hob_lbah;
		__entry->feature	= tf->feature;
		__entry->hob_feature	= tf->hob_feature;
		__entry->nsect		= tf->nsect;
		__entry->hob_nsect	= tf->hob_nsect;
	),

	TP_printk("ata_port=%u proto=%s cmd=%s%s " \
		  " tf=(%02x/%02x:%02x:%02x:%02x:%02x/%02x:%02x:%02x:%02x:%02x/%02x)",
		  __entry->ata_port,
		  show_protocol_name(__entry->proto),
		  show_opcode_name(__entry->cmd),
		  __parse_subcmd(__entry->cmd, __entry->feature, __entry->hob_nsect),
		  __entry->cmd, __entry->feature, __entry->nsect,
		  __entry->lbal, __entry->lbam, __entry->lbah,
		  __entry->hob_feature, __entry->hob_nsect,
		  __entry->hob_lbal, __entry->hob_lbam, __entry->hob_lbah,
		  __entry->dev)
);

DECLARE_EVENT_CLASS(ata_exec_command_template,

	TP_PROTO(struct ata_port *ap, const struct ata_taskfile *tf, unsigned int tag),

	TP_ARGS(ap, tf, tag),

	TP_STRUCT__entry(
		__field( unsigned int,	ata_port )
		__field( unsigned int,	tag	)
		__field( unsigned char,	cmd	)
		__field( unsigned char,	feature	)
		__field( unsigned char,	hob_nsect )
		__field( unsigned char,	proto	)
	),

	TP_fast_assign(
		__entry->ata_port	= ap->print_id;
		__entry->tag		= tag;
		__entry->proto		= tf->protocol;
		__entry->cmd		= tf->command;
		__entry->feature	= tf->feature;
		__entry->hob_nsect	= tf->hob_nsect;
	),

	TP_printk("ata_port=%u tag=%d proto=%s cmd=%s%s",
		  __entry->ata_port, __entry->tag,
		  show_protocol_name(__entry->proto),
		  show_opcode_name(__entry->cmd),
		  __parse_subcmd(__entry->cmd, __entry->feature, __entry->hob_nsect))
);

DEFINE_EVENT(ata_exec_command_template, ata_exec_command,
	     TP_PROTO(struct ata_port *ap, const struct ata_taskfile *tf, unsigned int tag),
	     TP_ARGS(ap, tf, tag));

DEFINE_EVENT(ata_exec_command_template, ata_bmdma_setup,
	     TP_PROTO(struct ata_port *ap, const struct ata_taskfile *tf, unsigned int tag),
	     TP_ARGS(ap, tf, tag));

DEFINE_EVENT(ata_exec_command_template, ata_bmdma_start,
	     TP_PROTO(struct ata_port *ap, const struct ata_taskfile *tf, unsigned int tag),
	     TP_ARGS(ap, tf, tag));

DEFINE_EVENT(ata_exec_command_template, ata_bmdma_stop,
	     TP_PROTO(struct ata_port *ap, const struct ata_taskfile *tf, unsigned int tag),
	     TP_ARGS(ap, tf, tag));

TRACE_EVENT(ata_bmdma_status,

	TP_PROTO(struct ata_port *ap, unsigned int host_stat),

	TP_ARGS(ap, host_stat),

	TP_STRUCT__entry(
		__field( unsigned int,	ata_port )
		__field( unsigned int,	tag	)
		__field( unsigned char,	host_stat )
	),

	TP_fast_assign(
		__entry->ata_port	= ap->print_id;
		__entry->host_stat	= host_stat;
	),

	TP_printk("ata_port=%u host_stat=%s",
		  __entry->ata_port,
		  __parse_host_stat(__entry->host_stat))
);

TRACE_EVENT(ata_eh_link_autopsy,

	TP_PROTO(struct ata_device *dev, unsigned int eh_action, unsigned int eh_err_mask),

	TP_ARGS(dev, eh_action, eh_err_mask),

	TP_STRUCT__entry(
		__field( unsigned int,	ata_port )
		__field( unsigned int,	ata_dev	)
		__field( unsigned int,	eh_action )
		__field( unsigned int,	eh_err_mask)
	),

	TP_fast_assign(
		__entry->ata_port	= dev->link->ap->print_id;
		__entry->ata_dev	= dev->link->pmp + dev->devno;
		__entry->eh_action	= eh_action;
		__entry->eh_err_mask	= eh_err_mask;
	),

	TP_printk("ata_port=%u ata_dev=%u eh_action=%s err_mask=%s",
		  __entry->ata_port, __entry->ata_dev,
		  __parse_eh_action(__entry->eh_action),
		  __parse_eh_err_mask(__entry->eh_err_mask))
);

TRACE_EVENT(ata_eh_link_autopsy_qc,

	TP_PROTO(struct ata_queued_cmd *qc),

	TP_ARGS(qc),

	TP_STRUCT__entry(
		__field( unsigned int,	ata_port )
		__field( unsigned int,	ata_dev	)
		__field( unsigned int,	tag	)
		__field( unsigned int,	qc_flags )
		__field( unsigned int,	eh_err_mask)
	),

	TP_fast_assign(
		__entry->ata_port	= qc->ap->print_id;
		__entry->ata_dev	= qc->dev->link->pmp + qc->dev->devno;
		__entry->tag		= qc->tag;
		__entry->qc_flags	= qc->flags;
		__entry->eh_err_mask	= qc->err_mask;
	),

	TP_printk("ata_port=%u ata_dev=%u tag=%d flags=%s err_mask=%s",
		  __entry->ata_port, __entry->ata_dev, __entry->tag,
		  __parse_qc_flags(__entry->qc_flags),
		  __parse_eh_err_mask(__entry->eh_err_mask))
);

DECLARE_EVENT_CLASS(ata_eh_action_template,

	TP_PROTO(struct ata_link *link, unsigned int devno, unsigned int eh_action),

	TP_ARGS(link, devno, eh_action),

	TP_STRUCT__entry(
		__field( unsigned int,	ata_port )
		__field( unsigned int,	ata_dev	)
		__field( unsigned int,	eh_action )
	),

	TP_fast_assign(
		__entry->ata_port	= link->ap->print_id;
		__entry->ata_dev	= link->pmp + devno;
		__entry->eh_action	= eh_action;
	),

	TP_printk("ata_port=%u ata_dev=%u eh_action=%s",
		  __entry->ata_port, __entry->ata_dev,
		  __parse_eh_action(__entry->eh_action))
);

DEFINE_EVENT(ata_eh_action_template, ata_eh_about_to_do,
	     TP_PROTO(struct ata_link *link, unsigned int devno, unsigned int eh_action),
	     TP_ARGS(link, devno, eh_action));

DEFINE_EVENT(ata_eh_action_template, ata_eh_done,
	     TP_PROTO(struct ata_link *link, unsigned int devno, unsigned int eh_action),
	     TP_ARGS(link, devno, eh_action));

DECLARE_EVENT_CLASS(ata_link_reset_begin_template,

	TP_PROTO(struct ata_link *link, unsigned int *class, unsigned long deadline),

	TP_ARGS(link, class, deadline),

	TP_STRUCT__entry(
		__field( unsigned int,	ata_port )
		__array( unsigned int,	class, 2 )
		__field( unsigned long,	deadline )
	),

	TP_fast_assign(
		__entry->ata_port	= link->ap->print_id;
		memcpy(__entry->class, class, 2);
		__entry->deadline	= deadline;
	),

	TP_printk("ata_port=%u deadline=%lu classes=[%s,%s]",
		  __entry->ata_port, __entry->deadline,
		  show_class_name(__entry->class[0]),
		  show_class_name(__entry->class[1]))
);

DEFINE_EVENT(ata_link_reset_begin_template, ata_link_hardreset_begin,
	     TP_PROTO(struct ata_link *link, unsigned int *class, unsigned long deadline),
	     TP_ARGS(link, class, deadline));

DEFINE_EVENT(ata_link_reset_begin_template, ata_slave_hardreset_begin,
	     TP_PROTO(struct ata_link *link, unsigned int *class, unsigned long deadline),
	     TP_ARGS(link, class, deadline));

DEFINE_EVENT(ata_link_reset_begin_template, ata_link_softreset_begin,
	     TP_PROTO(struct ata_link *link, unsigned int *class, unsigned long deadline),
	     TP_ARGS(link, class, deadline));

DECLARE_EVENT_CLASS(ata_link_reset_end_template,

	TP_PROTO(struct ata_link *link, unsigned int *class, int rc),

	TP_ARGS(link, class, rc),

	TP_STRUCT__entry(
		__field( unsigned int,	ata_port )
		__array( unsigned int,	class, 2 )
		__field( int,		rc	)
	),

	TP_fast_assign(
		__entry->ata_port	= link->ap->print_id;
		memcpy(__entry->class, class, 2);
		__entry->rc		= rc;
	),

	TP_printk("ata_port=%u rc=%d class=[%s,%s]",
		  __entry->ata_port, __entry->rc,
		  show_class_name(__entry->class[0]),
		  show_class_name(__entry->class[1]))
);

DEFINE_EVENT(ata_link_reset_end_template, ata_link_hardreset_end,
	     TP_PROTO(struct ata_link *link, unsigned int *class, int rc),
	     TP_ARGS(link, class, rc));

DEFINE_EVENT(ata_link_reset_end_template, ata_slave_hardreset_end,
	     TP_PROTO(struct ata_link *link, unsigned int *class, int rc),
	     TP_ARGS(link, class, rc));

DEFINE_EVENT(ata_link_reset_end_template, ata_link_softreset_end,
	     TP_PROTO(struct ata_link *link, unsigned int *class, int rc),
	     TP_ARGS(link, class, rc));

DEFINE_EVENT(ata_link_reset_end_template, ata_link_postreset,
	     TP_PROTO(struct ata_link *link, unsigned int *class, int rc),
	     TP_ARGS(link, class, rc));

DEFINE_EVENT(ata_link_reset_end_template, ata_slave_postreset,
	     TP_PROTO(struct ata_link *link, unsigned int *class, int rc),
	     TP_ARGS(link, class, rc));

DECLARE_EVENT_CLASS(ata_port_eh_begin_template,

	TP_PROTO(struct ata_port *ap),

	TP_ARGS(ap),

	TP_STRUCT__entry(
		__field( unsigned int,	ata_port )
	),

	TP_fast_assign(
		__entry->ata_port	= ap->print_id;
	),

	TP_printk("ata_port=%u", __entry->ata_port)
);

DEFINE_EVENT(ata_port_eh_begin_template, ata_std_sched_eh,
	     TP_PROTO(struct ata_port *ap),
	     TP_ARGS(ap));

DEFINE_EVENT(ata_port_eh_begin_template, ata_port_freeze,
	     TP_PROTO(struct ata_port *ap),
	     TP_ARGS(ap));

DEFINE_EVENT(ata_port_eh_begin_template, ata_port_thaw,
	     TP_PROTO(struct ata_port *ap),
	     TP_ARGS(ap));

DECLARE_EVENT_CLASS(ata_sff_hsm_template,

	TP_PROTO(struct ata_queued_cmd *qc, unsigned char status),

	TP_ARGS(qc, status),

	TP_STRUCT__entry(
		__field( unsigned int,	ata_port )
		__field( unsigned int,	ata_dev	)
		__field( unsigned int,	tag	)
		__field( unsigned int,	qc_flags )
		__field( unsigned int,	protocol )
		__field( unsigned int,	hsm_state )
		__field( unsigned char,	dev_state )
	),

	TP_fast_assign(
		__entry->ata_port	= qc->ap->print_id;
		__entry->ata_dev	= qc->dev->link->pmp + qc->dev->devno;
		__entry->tag		= qc->tag;
		__entry->qc_flags	= qc->flags;
		__entry->protocol	= qc->tf.protocol;
		__entry->hsm_state	= qc->ap->hsm_task_state;
		__entry->dev_state	= status;
	),

	TP_printk("ata_port=%u ata_dev=%u tag=%d proto=%s flags=%s task_state=%s dev_stat=0x%X",
		  __entry->ata_port, __entry->ata_dev, __entry->tag,
		  show_protocol_name(__entry->protocol),
		  __parse_qc_flags(__entry->qc_flags),
		  show_sff_hsm_state_name(__entry->hsm_state),
		  __entry->dev_state)
);

DEFINE_EVENT(ata_sff_hsm_template, ata_sff_hsm_state,
	TP_PROTO(struct ata_queued_cmd *qc, unsigned char state),
	TP_ARGS(qc, state));

DEFINE_EVENT(ata_sff_hsm_template, ata_sff_hsm_command_complete,
	TP_PROTO(struct ata_queued_cmd *qc, unsigned char state),
	TP_ARGS(qc, state));

DEFINE_EVENT(ata_sff_hsm_template, ata_sff_port_intr,
	TP_PROTO(struct ata_queued_cmd *qc, unsigned char state),
	TP_ARGS(qc, state));

DECLARE_EVENT_CLASS(ata_transfer_data_template,

	TP_PROTO(struct ata_queued_cmd *qc, unsigned int offset, unsigned int count),

	TP_ARGS(qc, offset, count),

	TP_STRUCT__entry(
		__field( unsigned int,	ata_port )
		__field( unsigned int,	ata_dev	)
		__field( unsigned int,	tag	)
		__field( unsigned int,	flags	)
		__field( unsigned int,	offset	)
		__field( unsigned int,	bytes	)
	),

	TP_fast_assign(
		__entry->ata_port	= qc->ap->print_id;
		__entry->ata_dev	= qc->dev->link->pmp + qc->dev->devno;
		__entry->tag		= qc->tag;
		__entry->flags		= qc->tf.flags;
		__entry->offset		= offset;
		__entry->bytes		= count;
	),

	TP_printk("ata_port=%u ata_dev=%u tag=%d flags=%s offset=%u bytes=%u",
		  __entry->ata_port, __entry->ata_dev, __entry->tag,
		  __parse_tf_flags(__entry->flags),
		  __entry->offset, __entry->bytes)
);

DEFINE_EVENT(ata_transfer_data_template, ata_sff_pio_transfer_data,
	     TP_PROTO(struct ata_queued_cmd *qc, unsigned int offset, unsigned int count),
	     TP_ARGS(qc, offset, count));

DEFINE_EVENT(ata_transfer_data_template, atapi_pio_transfer_data,
	     TP_PROTO(struct ata_queued_cmd *qc, unsigned int offset, unsigned int count),
	     TP_ARGS(qc, offset, count));

DEFINE_EVENT(ata_transfer_data_template, atapi_send_cdb,
	     TP_PROTO(struct ata_queued_cmd *qc, unsigned int offset, unsigned int count),
	     TP_ARGS(qc, offset, count));

DECLARE_EVENT_CLASS(ata_sff_template,

	TP_PROTO(struct ata_port *ap),

	TP_ARGS(ap),

	TP_STRUCT__entry(
		__field( unsigned int,	ata_port )
		__field( unsigned char,	hsm_state )
	),

	TP_fast_assign(
		__entry->ata_port	= ap->print_id;
		__entry->hsm_state	= ap->hsm_task_state;
	),

	TP_printk("ata_port=%u task_state=%s",
		  __entry->ata_port,
		  show_sff_hsm_state_name(__entry->hsm_state))
);

DEFINE_EVENT(ata_sff_template, ata_sff_flush_pio_task,
	     TP_PROTO(struct ata_port *ap),
	     TP_ARGS(ap));

#endif /*  _TRACE_LIBATA_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
