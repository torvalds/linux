/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SCSI_SCSI_REQUEST_H
#define _SCSI_SCSI_REQUEST_H

#include <linux/blk-mq.h>

struct scsi_request {
	int		result;
	unsigned int	resid_len;	/* residual count */
	int		retries;
};

static inline struct scsi_request *scsi_req(struct request *rq)
{
	return blk_mq_rq_to_pdu(rq);
}

#endif /* _SCSI_SCSI_REQUEST_H */
