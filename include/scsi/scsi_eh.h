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

static inline int scsi_sense_valid(struct scsi_sense_hdr *sshdr)
{
	if (!sshdr)
		return 0;

	return (sshdr->response_code & 0x70) == 0x70;
}


extern void scsi_eh_finish_cmd(struct scsi_cmnd *scmd,
			       struct list_head *done_q);
extern void scsi_eh_flush_done_q(struct list_head *done_q);
extern void scsi_report_bus_reset(struct Scsi_Host *, int);
extern void scsi_report_device_reset(struct Scsi_Host *, int, int);
extern int scsi_block_when_processing_errors(struct scsi_device *);
extern int scsi_normalize_sense(const u8 *sense_buffer, int sb_len,
		struct scsi_sense_hdr *sshdr);
extern int scsi_command_normalize_sense(struct scsi_cmnd *cmd,
		struct scsi_sense_hdr *sshdr);

static inline int scsi_sense_is_deferred(struct scsi_sense_hdr *sshdr)
{
	return ((sshdr->response_code >= 0x70) && (sshdr->response_code & 1));
}

extern const u8 * scsi_sense_desc_find(const u8 * sense_buffer, int sb_len,
				       int desc_type);

extern int scsi_get_sense_info_fld(const u8 * sense_buffer, int sb_len,
				   u64 * info_out);
 
/*
 * Reset request from external source
 */
#define SCSI_TRY_RESET_DEVICE	1
#define SCSI_TRY_RESET_BUS	2
#define SCSI_TRY_RESET_HOST	3

extern int scsi_reset_provider(struct scsi_device *, int);

struct scsi_eh_save {
	/* saved state */
	int result;
	enum dma_data_direction data_direction;
	unsigned char cmd_len;
	unsigned char cmnd[MAX_COMMAND_SIZE];
	struct scsi_data_buffer sdb;
	struct request *next_rq;

	/* new command support */
	struct scatterlist sense_sgl;
};

extern void scsi_eh_prep_cmnd(struct scsi_cmnd *scmd,
		struct scsi_eh_save *ses, unsigned char *cmnd,
		int cmnd_size, unsigned sense_bytes);

extern void scsi_eh_restore_cmnd(struct scsi_cmnd* scmd,
		struct scsi_eh_save *ses);

#endif /* _SCSI_SCSI_EH_H */
