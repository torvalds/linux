#ifndef _SCSI_SCSI_REQUEST_H
#define _SCSI_SCSI_REQUEST_H

#include <linux/blk-mq.h>

#define BLK_MAX_CDB	16

struct scsi_request {
	unsigned char	__cmd[BLK_MAX_CDB];
	unsigned char	*cmd;
	unsigned short	cmd_len;
	unsigned int	sense_len;
	unsigned int	resid_len;	/* residual count */
	void		*sense;
};

static inline struct scsi_request *scsi_req(struct request *rq)
{
	return blk_mq_rq_to_pdu(rq);
}

static inline void scsi_req_free_cmd(struct scsi_request *req)
{
	if (req->cmd != req->__cmd)
		kfree(req->cmd);
}

void scsi_req_init(struct request *);

#endif /* _SCSI_SCSI_REQUEST_H */
