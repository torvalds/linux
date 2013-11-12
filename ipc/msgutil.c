/*
 * linux/ipc/msgutil.c
 * Copyright (C) 1999, 2004 Manfred Spraul
 *
 * This file is released under GNU General Public Licence version 2 or
 * (at your option) any later version.
 *
 * See the file COPYING for more details.
 */

#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <linux/ipc.h>
#include <linux/msg.h>
#include <linux/ipc_namespace.h>
#include <linux/utsname.h>
#include <asm/uaccess.h>

#include "util.h"

DEFINE_SPINLOCK(mq_lock);

/*
 * The next 2 defines are here bc this is the only file
 * compiled when either CONFIG_SYSVIPC and CONFIG_POSIX_MQUEUE
 * and not CONFIG_IPC_NS.
 */
struct ipc_namespace init_ipc_ns = {
	.count		= ATOMIC_INIT(1),
	.user_ns = &init_user_ns,
};

atomic_t nr_ipc_ns = ATOMIC_INIT(1);

struct msg_msgseg {
	struct msg_msgseg* next;
	/* the next part of the message follows immediately */
};

#define DATALEN_MSG	((size_t)PAGE_SIZE-sizeof(struct msg_msg))
#define DATALEN_SEG	((size_t)PAGE_SIZE-sizeof(struct msg_msgseg))

struct msg_msg *load_msg(const void __user *src, size_t len)
{
	struct msg_msg *msg;
	struct msg_msgseg **pseg;
	int err;
	size_t alen;

	alen = len;
	if (alen > DATALEN_MSG)
		alen = DATALEN_MSG;

	msg = kmalloc(sizeof(*msg) + alen, GFP_KERNEL);
	if (msg == NULL)
		return ERR_PTR(-ENOMEM);

	msg->next = NULL;
	msg->security = NULL;

	if (copy_from_user(msg + 1, src, alen)) {
		err = -EFAULT;
		goto out_err;
	}

	len -= alen;
	src = ((char __user *)src) + alen;
	pseg = &msg->next;
	while (len > 0) {
		struct msg_msgseg *seg;
		alen = len;
		if (alen > DATALEN_SEG)
			alen = DATALEN_SEG;
		seg = kmalloc(sizeof(*seg) + alen,
						 GFP_KERNEL);
		if (seg == NULL) {
			err = -ENOMEM;
			goto out_err;
		}
		*pseg = seg;
		seg->next = NULL;
		if (copy_from_user(seg + 1, src, alen)) {
			err = -EFAULT;
			goto out_err;
		}
		pseg = &seg->next;
		len -= alen;
		src = ((char __user *)src) + alen;
	}

	err = security_msg_msg_alloc(msg);
	if (err)
		goto out_err;

	return msg;

out_err:
	free_msg(msg);
	return ERR_PTR(err);
}

int store_msg(void __user *dest, struct msg_msg *msg, size_t len)
{
	size_t alen;
	struct msg_msgseg *seg;

	alen = len;
	if (alen > DATALEN_MSG)
		alen = DATALEN_MSG;
	if (copy_to_user(dest, msg + 1, alen))
		return -1;

	len -= alen;
	dest = ((char __user *)dest) + alen;
	seg = msg->next;
	while (len > 0) {
		alen = len;
		if (alen > DATALEN_SEG)
			alen = DATALEN_SEG;
		if (copy_to_user(dest, seg + 1, alen))
			return -1;
		len -= alen;
		dest = ((char __user *)dest) + alen;
		seg = seg->next;
	}
	return 0;
}

void free_msg(struct msg_msg *msg)
{
	struct msg_msgseg *seg;

	security_msg_msg_free(msg);

	seg = msg->next;
	kfree(msg);
	while (seg != NULL) {
		struct msg_msgseg *tmp = seg->next;
		kfree(seg);
		seg = tmp;
	}
}
