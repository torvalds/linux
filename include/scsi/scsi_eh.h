/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SCSI_SCSI_EH_H
#define _SCSI_SCSI_EH_H

#include <linux/scatterlist.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_common.h>
struct scsi_device;
struct Scsi_Host;

extern void scsi_eh_finish_cmd(struct scsi_cmnd *scmd,
			       struct list_head *done_q);
extern void scsi_eh_flush_done_q(struct list_head *done_q);
extern void scsi_report_bus_reset(struct Scsi_Host *, int);
extern void scsi_report_device_reset(struct Scsi_Host *, int, int);
extern int scsi_block_when_processing_errors(struct scsi_device *);
extern bool scsi_command_normalize_sense(const struct scsi_cmnd *cmd,
					 struct scsi_sense_hdr *sshdr);
extern enum scsi_disposition scsi_check_sense(struct scsi_cmnd *);

static inline bool scsi_sense_is_deferred(const struct scsi_sense_hdr *sshdr)
{
	return ((sshdr->response_code >= 0x70) && (sshdr->response_code & 1));
}

extern bool scsi_get_sense_info_fld(const u8 *sense_buffer, int sb_len,
				    u64 *info_out);

extern int scsi_ioctl_reset(struct scsi_device *, int __user *);

struct scsi_eh_save {
	/* saved state */
	int result;
	unsigned int resid_len;
	int eh_eflags;
	enum dma_data_direction data_direction;
	unsigned underflow;
	unsigned char cmd_len;
	unsigned char prot_op;
	unsigned char *cmnd;
	struct scsi_data_buffer sdb;
	/* new command support */
	unsigned char eh_cmnd[BLK_MAX_CDB];
	struct scatterlist sense_sgl;
};

extern void scsi_eh_prep_cmnd(struct scsi_cmnd *scmd,
		struct scsi_eh_save *ses, unsigned char *cmnd,
		int cmnd_size, unsigned sense_bytes);

extern void scsi_eh_restore_cmnd(struct scsi_cmnd* scmd,
		struct scsi_eh_save *ses);

#endif /* _SCSI_SCSI_EH_H */
