#ifndef _SCSI_SCSI_DBG_H
#define _SCSI_SCSI_DBG_H

struct scsi_cmnd;
struct scsi_device;
struct scsi_sense_hdr;

#define SCSI_LOG_BUFSIZE 128

extern bool scsi_opcode_sa_name(int, int, const char **, const char **);
extern void scsi_print_command(struct scsi_cmnd *);
extern size_t __scsi_format_command(char *, size_t,
				   const unsigned char *, size_t);
extern void scsi_show_extd_sense(const struct scsi_device *, const char *,
				 unsigned char, unsigned char);
extern void scsi_show_sense_hdr(const struct scsi_device *, const char *,
				const struct scsi_sense_hdr *);
extern void scsi_print_sense_hdr(const struct scsi_device *, const char *,
				 const struct scsi_sense_hdr *);
extern void scsi_print_sense(const struct scsi_cmnd *);
extern void __scsi_print_sense(const struct scsi_device *, const char *name,
			       const unsigned char *sense_buffer,
			       int sense_len);
extern void scsi_print_result(const struct scsi_cmnd *, const char *, int);
extern const char *scsi_hostbyte_string(int);
extern const char *scsi_driverbyte_string(int);
extern const char *scsi_mlreturn_string(int);
extern const char *scsi_sense_key_string(unsigned char);
extern const char *scsi_extd_sense_format(unsigned char, unsigned char,
					  const char **);

#endif /* _SCSI_SCSI_DBG_H */
