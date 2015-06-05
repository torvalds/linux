#ifndef _SCSI_SCSI_EH_H
#define _SCSI_SCSI_EH_H

#include <linux/scatterlist.h>

#include <scsi/scsi_cmnd.h>
struct scsi_device;
struct Scsi_Host;

/*
 * This is a slightly modified SCSI sense "descriptor" format header.
 * The addition is to allow the 0x70 and 0x71 response codes. The idea
 * is to place the salient data from either "fixed" or "descriptor" sense
 * format into one structure to ease application processing.
 *
 * The original sense buffer should be kept around for those cases
 * in which more information is required (e.g. the LBA of a MEDIUM ERROR).
 */
struct scsi_sense_hdr {		/* See SPC-3 section 4.5 */
	u8 response_code;	/* permit: 0x0, 0x70, 0x71, 0x72, 0x73 */
	u8 sense_key;
	u8 asc;
	u8 ascq;
	u8 byte4;
	u8 byte5;
	u8 byte6;
	u8 additional_length;	/* always 0 for fixed sense format */
};

static inline bool scsi_sense_valid(const struct scsi_sense_hdr *sshdr)
{
	if (!sshdr)
		return false;

	return (sshdr->response_code & 0x70) == 0x70;
}


extern void scsi_eh_finish_cmd(struct scsi_cmnd *scmd,
			       struct list_head *done_q);
extern void scsi_eh_flush_done_q(struct list_head *done_q);
extern void scsi_report_bus_reset(struct Scsi_Host *, int);
extern void scsi_report_device_reset(struct Scsi_Host *, int, int);
extern int scsi_block_when_processing_errors(struct scsi_device *);
extern bool scsi_normalize_sense(const u8 *sense_buffer, int sb_len,
				 struct scsi_sense_hdr *sshdr);
extern bool scsi_command_normalize_sense(const struct scsi_cmnd *cmd,
					 struct scsi_sense_hdr *sshdr);

static inline bool scsi_sense_is_deferred(const struct scsi_sense_hdr *sshdr)
{
	return ((sshdr->response_code >= 0x70) && (sshdr->response_code & 1));
}

extern const u8 * scsi_sense_desc_find(const u8 * sense_buffer, int sb_len,
				       int desc_type);

extern int scsi_get_sense_info_fld(const u8 * sense_buffer, int sb_len,
				   u64 * info_out);

extern void scsi_build_sense_buffer(int desc, u8 *buf, u8 key, u8 asc, u8 ascq);
extern void scsi_set_sense_information(u8 *buf, u64 info);

extern int scsi_ioctl_reset(struct scsi_device *, int __user *);

struct scsi_eh_save {
	/* saved state */
	int result;
	enum dma_data_direction data_direction;
	unsigned underflow;
	unsigned char cmd_len;
	unsigned char prot_op;
	unsigned char *cmnd;
	struct scsi_data_buffer sdb;
	struct request *next_rq;
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
