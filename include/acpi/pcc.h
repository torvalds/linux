/*
 * PCC (Platform Communications Channel) methods
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#ifndef _PCC_H
#define _PCC_H

#include <linux/mailbox_controller.h>
#include <linux/mailbox_client.h>

#define MAX_PCC_SUBSPACES	256
#ifdef CONFIG_PCC
extern struct mbox_chan *pcc_mbox_request_channel(struct mbox_client *cl,
						  int subspace_id);
extern void pcc_mbox_free_channel(struct mbox_chan *chan);
#else
static inline struct mbox_chan *pcc_mbox_request_channel(struct mbox_client *cl,
							 int subspace_id)
{
	return ERR_PTR(-ENODEV);
}
static inline void pcc_mbox_free_channel(struct mbox_chan *chan) { }
#endif

#endif /* _PCC_H */
