/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SCSI_SCSI_DBG_H
#define _SCSI_SCSI_DBG_H

struct scsi_cmnd;
struct scsi_device;
struct scsi_sense_hdr;

#define SCSI_LOG_BUFSIZE 128

extern void scsi_print_command(struct scsi_cmnd *);
extern size_t __scsi_format_command(char *, size_t,
				   const unsigned char *, size_t);
extern void scsi_show_extd_sense(const struct scsi_device *, const char *,
				 unsigned char, unsigned char);
extern void scsi_print_sense_hdr(const struct scsi_device *, const char *,
				 const struct scsi_sense_hdr *);
extern void scsi_print_sense(const struct scsi_cmnd *);
extern void __scsi_print_sense(const struct scsi_device *, const char *name,
			       const unsigned char *sense_buffer,
			       int sense_len);
extern void scsi_print_result(const struct scsi_cmnd *, const char *, int);

#ifdef CONFIG_SCSI_CONSTANTS
extern bool scsi_opcode_sa_name(int, int, const char **, const char **);
extern const char *scsi_sense_key_string(unsigned char);
extern const char *scsi_extd_sense_format(unsigned char, unsigned char,
					  const char **);
extern const char *scsi_mlreturn_string(int);
extern const char *scsi_hostbyte_string(int);
extern const char *scsi_driverbyte_string(int);
#else
static inline bool
scsi_opcode_sa_name(int cmd, int sa,
		    const char **cdb_name, const char **sa_name)
{
	*cdb_name = NULL;
	switch (cmd) {
	case VARIABLE_LENGTH_CMD:
	case MAINTENANCE_IN:
	case MAINTENANCE_OUT:
	case PERSISTENT_RESERVE_IN:
	case PERSISTENT_RESERVE_OUT:
	case SERVICE_ACTION_IN_12:
	case SERVICE_ACTION_OUT_12:
	case SERVICE_ACTION_BIDIRECTIONAL:
	case SERVICE_ACTION_IN_16:
	case SERVICE_ACTION_OUT_16:
	case EXTENDED_COPY:
	case RECEIVE_COPY_RESULTS:
		*sa_name = NULL;
		return true;
	default:
		return false;
	}
}

static inline const char *
scsi_sense_key_string(unsigned char key)
{
	return NULL;
}

static inline const char *
scsi_extd_sense_format(unsigned char asc, unsigned char ascq, const char **fmt)
{
	*fmt = NULL;
	return NULL;
}

static inline const char *
scsi_mlreturn_string(int result)
{
	return NULL;
}

static inline const char *
scsi_hostbyte_string(int result)
{
	return NULL;
}

static inline const char *
scsi_driverbyte_string(int result)
{
	return NULL;
}

#endif

#endif /* _SCSI_SCSI_DBG_H */
