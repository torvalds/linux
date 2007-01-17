/*
 * SCSI target definitions
 */

#include <linux/dma-mapping.h>

struct Scsi_Host;
struct scsi_cmnd;
struct scsi_lun;

extern struct Scsi_Host *scsi_tgt_cmd_to_host(struct scsi_cmnd *);
extern int scsi_tgt_alloc_queue(struct Scsi_Host *);
extern void scsi_tgt_free_queue(struct Scsi_Host *);
extern int scsi_tgt_queue_command(struct scsi_cmnd *, struct scsi_lun *, u64);
extern int scsi_tgt_tsk_mgmt_request(struct Scsi_Host *, int, u64, struct scsi_lun *,
				     void *);
extern struct scsi_cmnd *scsi_host_get_command(struct Scsi_Host *,
					       enum dma_data_direction,	gfp_t);
extern void scsi_host_put_command(struct Scsi_Host *, struct scsi_cmnd *);
