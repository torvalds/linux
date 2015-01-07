/*
 * Copyright (c) 2013-2014 NVIDIA Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/list.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>

#include "ote_protocol.h"

static DECLARE_COMPLETION(req_ready);
static DECLARE_COMPLETION(req_complete);

static struct te_ss_op_legacy *ss_op_shmem_legacy;
static struct te_ss_op *ss_op_shmem;
static uint32_t ss_op_size;

static void indicate_ss_op_complete(void)
{
	tlk_generic_smc(TE_SMC_SS_REQ_COMPLETE, 0, 0);
}

int te_handle_ss_ioctl_legacy(struct file *file, unsigned int ioctl_num,
	unsigned long ioctl_param)
{
	switch (ioctl_num) {
	case TE_IOCTL_SS_NEW_REQ_LEGACY:
		/* wait for a new request */
		if (wait_for_completion_interruptible(&req_ready))
			return -ENODATA;

		/* transfer pending request to daemon's buffer */
		if (copy_to_user((void __user *)ioctl_param, ss_op_shmem_legacy,
					ss_op_size)) {
			pr_err("copy_to_user failed for new request\n");
			return -EFAULT;
		}
		break;

	case TE_IOCTL_SS_REQ_COMPLETE_LEGACY: /* request complete */
		if (copy_from_user(ss_op_shmem_legacy,
			(void __user *)ioctl_param, ss_op_size)) {
			pr_err("copy_from_user failed for request\n");
			return -EFAULT;
		}

		/* signal the producer */
		complete(&req_complete);
		break;
	}

	return 0;
}

void tlk_ss_op_legacy(uint32_t size)
{
	/* store size of request */
	ss_op_size = size;

	/* signal consumer */
	complete(&req_ready);

	/* wait for the consumer's signal */
	wait_for_completion(&req_complete);

	/* signal completion to the secure world */
	indicate_ss_op_complete();
}

static int __init tlk_ss_init_legacy(void)
{
	dma_addr_t ss_op_shmem_dma;

	/* allocate shared memory buffer */
	ss_op_shmem_legacy = dma_alloc_coherent(NULL,
		sizeof(struct te_ss_op_legacy), &ss_op_shmem_dma, GFP_KERNEL);
	if (!ss_op_shmem_legacy) {
		pr_err("%s: no memory available for fs operations\n", __func__);
		return -ENOMEM;
	}

	tlk_generic_smc(TE_SMC_SS_REGISTER_HANDLER_LEGACY,
		(uintptr_t)tlk_ss_op_legacy, (uintptr_t)ss_op_shmem_legacy);

	return 0;
}

arch_initcall(tlk_ss_init_legacy);

int te_handle_ss_ioctl(struct file *file, unsigned int ioctl_num,
	unsigned long ioctl_param)
{
	switch (ioctl_num) {
	case TE_IOCTL_SS_NEW_REQ:
		/* wait for a new request */
		if (wait_for_completion_interruptible(&req_ready))
			return -ENODATA;

		/* transfer pending request to daemon's buffer */
		if (copy_to_user((void __user *)ioctl_param, ss_op_shmem->data,
					ss_op_shmem->req_size)) {
			pr_err("copy_to_user failed for new request\n");
			return -EFAULT;
		}
		break;

	case TE_IOCTL_SS_REQ_COMPLETE: /* request complete */
		if (copy_from_user(ss_op_shmem->data,
			(void __user *)ioctl_param, ss_op_shmem->req_size)) {
			pr_err("copy_from_user failed for request\n");
			return -EFAULT;
		}

		/* signal the producer */
		complete(&req_complete);
		break;
	}

	return 0;
}

void tlk_ss_op(void)
{
	/* signal consumer */
	complete(&req_ready);

	/* wait for the consumer's signal */
	wait_for_completion(&req_complete);
}

static int __init tlk_ss_init(void)
{
	dma_addr_t ss_op_shmem_dma;
	int32_t ret;

	/* allocate shared memory buffer */
	ss_op_shmem = dma_alloc_coherent(NULL, sizeof(struct te_ss_op),
			&ss_op_shmem_dma, GFP_KERNEL);
	if (!ss_op_shmem) {
		pr_err("%s: no memory available for fs operations\n", __func__);
		return -ENOMEM;
	}

	ret = tlk_generic_smc(TE_SMC_SS_REGISTER_HANDLER,
			(uintptr_t)ss_op_shmem, 0);
	if (ret != 0) {
		dma_free_coherent(NULL, sizeof(struct te_ss_op),
			(void *)ss_op_shmem, ss_op_shmem_dma);
		ss_op_shmem = NULL;
		return -ENOTSUPP;
	}

	return 0;
}

arch_initcall(tlk_ss_init);
