/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SCSI_SCSI_REQUEST_H
#define _SCSI_SCSI_REQUEST_H

#include <linux/blk-mq.h>

#define BLK_MAX_CDB	16

struct scsi_request {
	unsigned char	__cmd[BLK_MAX_CDB];
	unsigned char	*cmd;
	unsigned short	cmd_len;
	int		result;
	unsigned int	sense_len;
	unsigned int	resid_len;	/* residual count */
	int		retries;
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

#endif /* _SCSI_SCSI_REQUEST_H */
