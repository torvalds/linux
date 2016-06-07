/*
 * Functions used by both the SCSI initiator code and the SCSI target code.
 */

#ifndef _SCSI_COMMON_H_
#define _SCSI_COMMON_H_

#include <linux/types.h>
#include <scsi/scsi_proto.h>

static inline unsigned
scsi_varlen_cdb_length(const void *hdr)
{
	return ((struct scsi_varlen_cdb_hdr *)hdr)->additional_cdb_length + 8;
}

extern const unsigned char scsi_command_size_tbl[8];
#define COMMAND_SIZE(opcode) scsi_command_size_tbl[((opcode) >> 5) & 7]

static inline unsigned
scsi_command_size(const unsigned char *cmnd)
{
	return (cmnd[0] == VARIABLE_LENGTH_CMD) ?
		scsi_varlen_cdb_length(cmnd) : COMMAND_SIZE(cmnd[0]);
}

/* Returns a human-readable name for the device */
extern const char *scsi_device_type(unsigned type);

extern void int_to_scsilun(u64, struct scsi_lun *);
extern u64 scsilun_to_int(struct scsi_lun *);

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

extern bool scsi_normalize_sense(const u8 *sense_buffer, int sb_len,
				 struct scsi_sense_hdr *sshdr);

extern void scsi_build_sense_buffer(int desc, u8 *buf, u8 key, u8 asc, u8 ascq);
int scsi_set_sense_information(u8 *buf, int buf_len, u64 info);
int scsi_set_sense_field_pointer(u8 *buf, int buf_len, u16 fp, u8 bp, bool cd);
extern const u8 * scsi_sense_desc_find(const u8 * sense_buffer, int sb_len,
				       int desc_type);

#endif /* _SCSI_COMMON_H_ */
