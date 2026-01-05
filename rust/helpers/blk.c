// SPDX-License-Identifier: GPL-2.0

#include <linux/blk-mq.h>
#include <linux/blkdev.h>

__rust_helper void *rust_helper_blk_mq_rq_to_pdu(struct request *rq)
{
	return blk_mq_rq_to_pdu(rq);
}

__rust_helper struct request *rust_helper_blk_mq_rq_from_pdu(void *pdu)
{
	return blk_mq_rq_from_pdu(pdu);
}
