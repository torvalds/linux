#ifndef _SCSI_SCSI_DBG_H
#define _SCSI_SCSI_DBG_H

struct scsi_cmnd;
struct scsi_device;
struct scsi_sense_hdr;

extern void scsi_print_command(struct scsi_cmnd *);
extern void __scsi_print_command(const unsigned char *, size_t);
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
extern void scsi_print_result(struct scsi_cmnd *, const char *, int);
extern const char *scsi_hostbyte_string(int);
extern const char *scsi_driverbyte_string(int);
extern const char *scsi_mlreturn_string(int);
extern const char *scsi_sense_key_string(unsigned char);
extern const char *scsi_extd_sense_format(unsigned char, unsigned char,
					  const char **);

#endif /* _SCSI_SCSI_DBG_H */
