/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/anon_inodes.h>
#include <linux/semaphore.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/device.h>

#include "tee_shm.h"
#include "tee_core.h"
#include "tee_supp_com.h"

#define TEE_RPC_BUFFER	0x00000001
#define TEE_RPC_VALUE	0x00000002

enum teec_rpc_result tee_supp_cmd(struct tee *tee,
				  uint32_t id, void *data, size_t datalen)
{
	struct tee_rpc *rpc = tee->rpc;
	enum teec_rpc_result res = TEEC_RPC_FAIL;
	size_t size;
	struct task_struct *task = current;

	dev_dbg(tee->dev, "> tgid:[%d] id:[0x%08x]\n", task->tgid, id);

	if (atomic_read(&rpc->used) == 0) {
		dev_err(tee->dev, "%s: ERROR Supplicant application NOT ready\n"
				, __func__);
		goto out;
	}

	switch (id) {
	case TEE_RPC_ICMD_ALLOCATE:
		{
			struct tee_rpc_alloc *alloc;
			struct tee_shm *shmint;

			alloc = (struct tee_rpc_alloc *)data;
			size = alloc->size;
			memset(alloc, 0, sizeof(struct tee_rpc_alloc));
			shmint = tee_shm_alloc_from_rpc(tee, size);
			if (IS_ERR_OR_NULL(shmint))
				break;

			alloc->size = size;
			alloc->data = (void *)(unsigned long)shmint->paddr;
			alloc->shm = shmint;
			res = TEEC_RPC_OK;

			break;
		}
	case TEE_RPC_ICMD_FREE:
		{
			struct tee_rpc_free *free;

			free = (struct tee_rpc_free *)data;
			tee_shm_free_from_rpc(free->shm);
			res = TEEC_RPC_OK;
			break;
		}
	case TEE_RPC_ICMD_INVOKE:
		{
			if (sizeof(rpc->commToUser) < datalen)
				break;

			/*
			 * Other threads blocks here until we've copied our
			 * answer from the supplicant
			 */
			mutex_lock(&rpc->thrd_mutex);

			mutex_lock(&rpc->outsync);
			memcpy(&rpc->commToUser, data, datalen);
			mutex_unlock(&rpc->outsync);

			dev_dbg(tee->dev,
				"Supplicant Cmd: %x. Give hand to supplicant\n",
				rpc->commToUser.cmd);

			up(&rpc->datatouser);

			down(&rpc->datafromuser);

			dev_dbg(tee->dev,
				"Supplicant Cmd: %x. Give hand to fw\n",
				rpc->commToUser.cmd);

			mutex_lock(&rpc->insync);
			memcpy(data, &rpc->commFromUser, datalen);
			mutex_unlock(&rpc->insync);

			mutex_unlock(&rpc->thrd_mutex);

			res = TEEC_RPC_OK;

			break;
		}
	default:
		/* not supported */
		break;
	}

out:
	dev_dbg(tee->dev, "< res: [%d]\n", res);

	return res;
}
EXPORT_SYMBOL(tee_supp_cmd);

ssize_t tee_supp_read(struct file *filp, char __user *buffer,
		  size_t length, loff_t *offset)
{
	struct tee_context *ctx = (struct tee_context *)(filp->private_data);
	struct tee *tee;
	struct tee_rpc *rpc;
	struct task_struct *task = current;
	int ret;

	BUG_ON(!ctx);
	tee = ctx->tee;
	BUG_ON(!tee);
	BUG_ON(!tee->dev);
	BUG_ON(!tee->rpc);

	dev_dbg(tee->dev, "> ctx %p\n", ctx);

	rpc = tee->rpc;

	if (atomic_read(&rpc->used) == 0) {
		dev_err(tee->dev, "%s: ERROR Supplicant application NOT ready\n"
				, __func__);
		ret = -EPERM;
		goto out;
	}

	if (down_interruptible(&rpc->datatouser))
		return -ERESTARTSYS;

	dev_dbg(tee->dev, "> tgid:[%d]\n", task->tgid);

	mutex_lock(&rpc->outsync);

	ret =
	    sizeof(rpc->commToUser) - sizeof(rpc->commToUser.cmds) +
	    sizeof(rpc->commToUser.cmds[0]) * rpc->commToUser.nbr_bf;
	if (length < ret) {
		ret = -EINVAL;
	} else {
		if (copy_to_user(buffer, &rpc->commToUser, ret)) {
			dev_err(tee->dev,
				"[%s] error, copy_to_user failed!\n", __func__);
			ret = -EINVAL;
		}
	}

	mutex_unlock(&rpc->outsync);

out:
	dev_dbg(tee->dev, "< [%d]\n", ret);
	return ret;
}

ssize_t tee_supp_write(struct file *filp, const char __user *buffer,
		   size_t length, loff_t *offset)
{
	struct tee_context *ctx = (struct tee_context *)(filp->private_data);
	struct tee *tee;
	struct tee_rpc *rpc;
	struct task_struct *task = current;
	int ret = 0;

	BUG_ON(!ctx);
	BUG_ON(!ctx->tee);
	BUG_ON(!ctx->tee->rpc);
	tee = ctx->tee;
	rpc = tee->rpc;
	dev_dbg(tee->dev, "> tgid:[%d]\n", task->tgid);

	if (atomic_read(&rpc->used) == 0) {
		dev_err(tee->dev, "%s: ERROR Supplicant application NOT ready\n"
				, __func__);
		goto out;
	}

	if (length > 0 && length < sizeof(rpc->commFromUser)) {
		uint32_t i;

		mutex_lock(&rpc->insync);

		if (copy_from_user(&rpc->commFromUser, buffer, length)) {
			dev_err(tee->dev,
				"%s: ERROR, tee_session copy_from_user failed\n",
				__func__);
			mutex_unlock(&rpc->insync);
			ret = -EINVAL;
			goto out;
		}

		/* Translate virtual address of caller into physical address */
		for (i = 0; i < rpc->commFromUser.nbr_bf; i++) {
			if (rpc->commFromUser.cmds[i].type == TEE_RPC_BUFFER
			    && rpc->commFromUser.cmds[i].buffer) {
				struct vm_area_struct *vma =
				    find_vma(current->mm,
					     (unsigned long)rpc->
					     commFromUser.cmds[i].buffer);
				if (vma != NULL) {
					struct tee_shm *shm =
					    vma->vm_private_data;
					BUG_ON(!shm);
					dev_dbg(tee->dev,
						"%d gid2pa(0x%p => %x)\n", i,
						rpc->commFromUser.cmds[i].
						buffer,
						(unsigned int)shm->paddr);
					rpc->commFromUser.cmds[i].buffer =
					    (void *)(unsigned long)shm->paddr;
				} else
					dev_dbg(tee->dev,
						" gid2pa(0x%p => NULL\n)",
						rpc->commFromUser.cmds[i].
						buffer);
			}
		}

		mutex_unlock(&rpc->insync);
		up(&rpc->datafromuser);
		ret = length;
	}

out:
	dev_dbg(tee->dev, "< [%d]\n", ret);
	return ret;
}

int tee_supp_init(struct tee *tee)
{
	struct tee_rpc *rpc =
	    devm_kzalloc(tee->dev, sizeof(struct tee_rpc), GFP_KERNEL);
	if (!rpc) {
		dev_err(tee->dev, "%s: can't allocate tee_rpc structure\n",
				__func__);
		return -ENOMEM;
	}

	sema_init(&rpc->datafromuser, 0);
	sema_init(&rpc->datatouser, 0);
	mutex_init(&rpc->thrd_mutex);
	mutex_init(&rpc->outsync);
	mutex_init(&rpc->insync);
	atomic_set(&rpc->used, 0);
	tee->rpc = rpc;
	return 0;
}

void tee_supp_deinit(struct tee *tee)
{
	devm_kfree(tee->dev, tee->rpc);
	tee->rpc = NULL;
}
