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

#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/printk.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <asm/outercache.h>
#include <linux/list.h>
#include <linux/dma-mapping.h>

#include "ote_protocol.h"

#define SET_ANSWER(a, r, ro)	{ a.result = r; a.result_origin = ro; }

struct tlk_device tlk_dev;
DEFINE_MUTEX(smc_lock);

static int te_create_free_cmd_list(struct tlk_device *dev)
{
	int cmd_desc_count, ret = 0;
	struct te_cmd_req_desc *req_desc;
	struct te_cmd_req_desc_compat *req_desc_compat;
	int bitmap_size;
	bool use_reqbuf;

	/*
	 * Check if new shared req/param register SMC is supported.
	 *
	 * If it is, TLK can map in the shared req/param buffers and do_smc
	 * only needs to send the offsets within each (with cache coherency
	 * being maintained by HW through an NS mapping).
	 *
	 * If the SMC support is not yet present, then fallback to the old
	 * mode of writing to an uncached buffer to maintain coherency (and
	 * phys addresses are passed in do_smc).
	 */
	dev->req_param_buf = NULL;
	use_reqbuf = !tlk_generic_smc(TE_SMC_REGISTER_REQ_BUF, 0, 0);

	if (use_reqbuf) {
		dev->req_param_buf = kmalloc((2 * PAGE_SIZE), GFP_KERNEL);

		/* requests in the first page, params in the second */
		dev->req_addr   = (struct te_request *) dev->req_param_buf;
		dev->param_addr = (struct te_oper_param *)
					(dev->req_param_buf + PAGE_SIZE);

		tlk_generic_smc(TE_SMC_REGISTER_REQ_BUF,
				(uintptr_t)dev->req_addr, (2 * PAGE_SIZE));
	} else {
		dev->req_addr = dma_alloc_coherent(NULL, PAGE_SIZE,
					&dev->req_addr_phys, GFP_KERNEL);
		dev->param_addr = dma_alloc_coherent(NULL, PAGE_SIZE,
					&dev->param_addr_phys, GFP_KERNEL);
	}

	if (!dev->req_addr || !dev->param_addr || !dev->req_param_buf) {
		ret = -ENOMEM;
		goto error;
	}

	/* requests in the first page, params in the second */
	dev->req_addr_compat   = (struct te_request_compat *)
					dev->req_param_buf;
	dev->param_addr_compat = (struct te_oper_param_compat *)
					(dev->req_param_buf + PAGE_SIZE);

	/* alloc param bitmap allocator */
	bitmap_size = BITS_TO_LONGS(TE_PARAM_MAX) * sizeof(long);
	dev->param_bitmap = kzalloc(bitmap_size, GFP_KERNEL);

	for (cmd_desc_count = 0;
		cmd_desc_count < TE_CMD_DESC_MAX; cmd_desc_count++) {

		req_desc = kzalloc(sizeof(struct te_cmd_req_desc), GFP_KERNEL);
		if (req_desc == NULL) {
			pr_err("Failed to allocate cmd req descriptor\n");
			ret = -ENOMEM;
			goto error;
		}
		req_desc->req_addr = dev->req_addr + cmd_desc_count;
		INIT_LIST_HEAD(&(req_desc->list));

		/* Add the cmd param descriptor to free list */
		list_add_tail(&req_desc->list, &(dev->free_cmd_list));
	}

	for (cmd_desc_count = 0;
		cmd_desc_count < TE_CMD_DESC_MAX_COMPAT; cmd_desc_count++) {

		req_desc_compat = kzalloc(sizeof(struct te_cmd_req_desc_compat),
				GFP_KERNEL);
		if (req_desc_compat == NULL) {
			pr_err("Failed to allocate cmd req descriptor\n");
			ret = -ENOMEM;
			goto error;
		}
		req_desc_compat->req_addr =
			dev->req_addr_compat + cmd_desc_count;
		INIT_LIST_HEAD(&(req_desc_compat->list));

		/* Add the cmd param descriptor to free list */
		list_add_tail(&req_desc_compat->list, &(dev->free_cmd_list));
	}

error:
	return ret;
}

static struct te_oper_param *te_get_free_params(struct tlk_device *dev,
	unsigned int nparams)
{
	struct te_oper_param *params = NULL;
	int idx, nbits;

	if (nparams) {
		nbits = get_count_order(nparams);
		idx = bitmap_find_free_region(dev->param_bitmap,
				TE_PARAM_MAX, nbits);
		if (idx >= 0)
			params = dev->param_addr + idx;
	}
	return params;
}

static void te_put_free_params(struct tlk_device *dev,
	struct te_oper_param *params, uint32_t nparams)
{
	int idx, nbits;

	idx = (params - dev->param_addr);
	nbits = get_count_order(nparams);
	bitmap_release_region(dev->param_bitmap, idx, nbits);
}

static struct te_oper_param_compat *
	te_get_free_params_compat(struct tlk_device *dev, unsigned int nparams)
{
	struct te_oper_param_compat *params = NULL;
	int idx, nbits;

	if (nparams) {
		nbits = get_count_order(nparams);
		idx = bitmap_find_free_region(dev->param_bitmap,
				TE_PARAM_MAX, nbits);
		if (idx >= 0)
			params = dev->param_addr_compat + idx;
	}
	return params;
}

static void te_put_free_params_compat(struct tlk_device *dev,
	struct te_oper_param_compat *params, uint32_t nparams)
{
	int idx, nbits;

	idx = (params - dev->param_addr_compat);
	nbits = get_count_order(nparams);
	bitmap_release_region(dev->param_bitmap, idx, nbits);
}

static struct te_cmd_req_desc *te_get_free_cmd_desc(struct tlk_device *dev)
{
	struct te_cmd_req_desc *cmd_desc = NULL;

	if (!(list_empty(&(dev->free_cmd_list)))) {
		cmd_desc = list_first_entry(&(dev->free_cmd_list),
				struct te_cmd_req_desc, list);
		list_del(&(cmd_desc->list));
		list_add_tail(&cmd_desc->list, &(dev->used_cmd_list));
	}
	return cmd_desc;
}

static void te_put_used_cmd_desc(struct tlk_device *dev,
	struct te_cmd_req_desc *cmd_desc)
{
	struct te_cmd_req_desc *param_desc, *tmp_param_desc;

	if (cmd_desc) {
		list_for_each_entry_safe(param_desc, tmp_param_desc,
				&(dev->used_cmd_list), list) {
			if (cmd_desc->req_addr == param_desc->req_addr) {
				list_del(&param_desc->list);
				list_add_tail(&param_desc->list,
					&(dev->free_cmd_list));
			}
		}
	}
}

static struct te_cmd_req_desc_compat *
te_get_free_cmd_desc_compat(struct tlk_device *dev)
{
	struct te_cmd_req_desc_compat *cmd_desc = NULL;

	if (!(list_empty(&(dev->free_cmd_list)))) {
		cmd_desc = list_first_entry(&(dev->free_cmd_list),
				struct te_cmd_req_desc_compat, list);
		list_del(&(cmd_desc->list));
		list_add_tail(&cmd_desc->list, &(dev->used_cmd_list));
	}
	return cmd_desc;
}

static void te_put_used_cmd_desc_compat(struct tlk_device *dev,
	struct te_cmd_req_desc_compat *cmd_desc)
{
	struct te_cmd_req_desc_compat *param_desc, *tmp_param_desc;

	if (cmd_desc) {
		list_for_each_entry_safe(param_desc, tmp_param_desc,
				&(dev->used_cmd_list), list) {
			if (cmd_desc->req_addr == param_desc->req_addr) {
				list_del(&param_desc->list);
				list_add_tail(&param_desc->list,
					&(dev->free_cmd_list));
			}
		}
	}
}

static void __attribute__((unused)) te_print_cmd_list(
	struct tlk_device *dev, int used_list)
{
	struct te_cmd_req_desc *param_desc;

	if (!used_list) {
		pr_info("Printing free cmd list\n");
		if (!(list_empty(&(dev->free_cmd_list)))) {
			list_for_each_entry(param_desc, &(dev->free_cmd_list),
					list)
				pr_info("Phys addr for cmd req desc (%p)\n",
					param_desc->req_addr);
		}
	} else {
		pr_info("Printing used cmd list\n");
		if (!(list_empty(&(dev->used_cmd_list)))) {
			list_for_each_entry(param_desc, &(dev->used_cmd_list),
					list)
				pr_info("Phys addr for cmd req desc (%p)\n",
					param_desc->req_addr);
		}
	}
}

static int tlk_device_open(struct inode *inode, struct file *file)
{
	struct tlk_context *context;
	int ret = 0;

	context = kzalloc(sizeof(struct tlk_context), GFP_KERNEL);
	if (!context) {
		ret = -ENOMEM;
		goto error;
	}
	context->dev = &tlk_dev;
	INIT_LIST_HEAD(&(context->shmem_alloc_list));

	file->private_data = context;
	return 0;
error:
	return ret;
}

static int tlk_device_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	file->private_data = NULL;
	return 0;
}

static int copy_params_from_user(struct te_request *req,
	struct te_operation *operation)
{
	struct te_oper_param *param_array;
	struct te_oper_param *user_param;
	uint32_t i;

	if (operation->list_count == 0)
		return 0;

	param_array = req->params;
	if (param_array == NULL) {
		pr_err("param_array empty\n");
		return 1;
	}

	user_param = operation->list_head;
	for (i = 0; i < operation->list_count && user_param != NULL; i++) {
		if (copy_from_user(param_array + i, user_param,
					sizeof(struct te_oper_param))) {
			pr_err("Failed to copy operation parameter:%d, %p, " \
					"list_count: %d\n",
					i, user_param, operation->list_count);
			return 1;
		}
		user_param = param_array[i].next_ptr_user;
	}
	return 0;
}

static int copy_params_to_user(struct te_request *req,
	struct te_operation *operation)
{
	struct te_oper_param *param_array;
	struct te_oper_param *user_param;
	uint32_t i;

	if (operation->list_count == 0)
		return 0;

	param_array = req->params;
	if (param_array == NULL) {
		pr_err("param_array empty\n");
		return 1;
	}

	user_param = operation->list_head;
	for (i = 0; i < req->params_size; i++) {
		if (copy_to_user(user_param, param_array + i,
					sizeof(struct te_oper_param))) {
			pr_err("Failed to copy back parameter:%d %p\n", i,
					user_param);
			return 1;
		}
		user_param = param_array[i].next_ptr_user;
	}
	return 0;
}

static long te_handle_trustedapp_ioctl(struct file *file,
	unsigned int ioctl_num, unsigned long ioctl_param)
{
	long err = 0;
	union te_cmd cmd;
	void *ptr_user_answer = NULL;
	struct te_operation *operation = NULL;
	struct te_oper_param *params = NULL;
	struct te_answer answer;
	struct te_request *request;

	struct te_cmd_req_desc *cmd_desc = NULL;
	struct tlk_context *context = file->private_data;
	struct tlk_device *dev = context->dev;

	if (copy_from_user(&cmd, (void __user *)ioctl_param,
				sizeof(union te_cmd))) {
		pr_err("Failed to copy command request\n");
		err = -EFAULT;
		goto error;
	}

	memset(&answer, 0, sizeof(struct te_answer));

	switch (ioctl_num) {
	case TE_IOCTL_OPEN_CLIENT_SESSION:
		operation = &cmd.opensession.operation;
		ptr_user_answer = (void *)cmd.opensession.answer;

		cmd_desc = te_get_free_cmd_desc(dev);
		params = te_get_free_params(dev, operation->list_count);

		if (!cmd_desc || (operation->list_count && !params)) {
			SET_ANSWER(answer,
				   OTE_ERROR_OUT_OF_MEMORY,
				   OTE_RESULT_ORIGIN_COMMS);
			pr_err("failed to get cmd_desc/params\n");
			goto error;
		}

		request = cmd_desc->req_addr;
		memset(request, 0, sizeof(struct te_request));

		request->params = params;
		request->params_size = operation->list_count;

		if (copy_params_from_user(request, operation)) {
			err = -EFAULT;
			pr_info("failed to copy params from user\n");
			goto error;
		}

		te_open_session(&cmd.opensession, request, context);

		SET_ANSWER(answer, request->result, request->result_origin);
		answer.session_id = request->session_id;
		break;

	case TE_IOCTL_CLOSE_CLIENT_SESSION:
		ptr_user_answer = (void *)cmd.closesession.answer;
		cmd_desc = te_get_free_cmd_desc(dev);
		if (!cmd_desc) {
			SET_ANSWER(answer,
				   OTE_ERROR_OUT_OF_MEMORY,
				   OTE_RESULT_ORIGIN_COMMS);
			pr_err("failed to get cmd_desc\n");
			goto error;
		}

		request = cmd_desc->req_addr;
		memset(request, 0, sizeof(struct te_request));

		/* close session cannot fail */
		te_close_session(&cmd.closesession, request, context);
		break;

	case TE_IOCTL_LAUNCH_OPERATION:
		operation = &cmd.launchop.operation;
		ptr_user_answer = (void *)cmd.launchop.answer;

		cmd_desc = te_get_free_cmd_desc(dev);
		params = te_get_free_params(dev, operation->list_count);

		if (!cmd_desc || (operation->list_count && !params)) {
			SET_ANSWER(answer,
				   OTE_ERROR_OUT_OF_MEMORY,
				   OTE_RESULT_ORIGIN_COMMS);
			pr_err("failed to get cmd_desc/params\n");
			goto error;
		}

		request = cmd_desc->req_addr;
		memset(request, 0, sizeof(struct te_request));

		request->params = params;
		request->params_size = operation->list_count;

		if (copy_params_from_user(request, operation)) {
			err = -EFAULT;
			pr_info("failed to copy params from user\n");
			goto error;
		}

		te_launch_operation(&cmd.launchop, request, context);

		SET_ANSWER(answer, request->result, request->result_origin);
		break;

	default:
		pr_err("Invalid IOCTL Cmd\n");
		err = -EINVAL;
		goto error;
	}
	if (ptr_user_answer && !err) {
		if (copy_to_user(ptr_user_answer, &answer,
			sizeof(struct te_answer))) {
			pr_err("Failed to copy answer\n");
			err = -EFAULT;
		}
	}
	if (request->params && !err) {
		if (copy_params_to_user(request, operation)) {
			pr_err("Failed to copy return params\n");
			err = -EFAULT;
		}
	}

error:
	if (cmd_desc)
		te_put_used_cmd_desc(dev, cmd_desc);
	if (params)
		te_put_free_params(dev, params, operation->list_count);
	return err;
}

static int copy_params_from_user_compat(struct te_request_compat *req,
	struct te_operation_compat *operation)
{
	struct te_oper_param_compat *param_array;
	struct te_oper_param_compat *user_param;
	uint32_t i;

	if (operation->list_count == 0)
		return 0;

	param_array = (struct te_oper_param_compat *)(uintptr_t)req->params;
	if (param_array == NULL) {
		pr_err("param_array empty\n");
		return 1;
	}

	user_param = (struct te_oper_param_compat *)(uintptr_t)
		operation->list_head;
	for (i = 0; i < operation->list_count && user_param != NULL; i++) {
		if (copy_from_user(param_array + i, user_param,
					sizeof(struct te_oper_param_compat))) {
			pr_err("Failed to copy operation parameter:%d, %p, " \
					"list_count: %d\n",
					i, user_param, operation->list_count);
			return 1;
		}
		user_param = (struct te_oper_param_compat *)(uintptr_t)
			param_array[i].next_ptr_user;
	}
	return 0;
}

static int copy_params_to_user_compat(struct te_request_compat *req,
	struct te_operation_compat *operation)
{
	struct te_oper_param_compat *param_array;
	struct te_oper_param_compat *user_param;
	uint32_t i;

	if (operation->list_count == 0)
		return 0;

	param_array =
		(struct te_oper_param_compat *)(uintptr_t)req->params;
	if (param_array == NULL) {
		pr_err("param_array empty\n");
		return 1;
	}

	user_param =
		(struct te_oper_param_compat *)(uintptr_t)operation->list_head;
	for (i = 0; i < req->params_size; i++) {
		if (copy_to_user(user_param, param_array + i,
					sizeof(struct te_oper_param_compat))) {
			pr_err("Failed to copy back parameter:%d %p\n", i,
					user_param);
			return 1;
		}
		user_param = (struct te_oper_param_compat *)(uintptr_t)
			param_array[i].next_ptr_user;
	}
	return 0;
}

static long te_handle_trustedapp_ioctl_compat(struct file *file,
	unsigned int ioctl_num, unsigned long ioctl_param)
{
	long err = 0;
	union te_cmd_compat cmd_compat;
	struct te_operation_compat *operation = NULL;
	struct te_oper_param_compat *params = NULL;
	struct te_request_compat *request;
	void __user *ptr_user_answer = NULL;
	struct te_answer answer;
	struct te_cmd_req_desc_compat *cmd_desc = NULL;
	struct tlk_context *context = file->private_data;
	struct tlk_device *dev = context->dev;

	if (copy_from_user(&cmd_compat, (void __user *)ioctl_param,
				sizeof(union te_cmd_compat))) {
		pr_err("Failed to copy command request\n");
		err = -EFAULT;
		goto error;
	}

	memset(&answer, 0, sizeof(struct te_answer));

	switch (ioctl_num) {
	case TE_IOCTL_OPEN_CLIENT_SESSION_COMPAT:
		operation = &cmd_compat.opensession.operation;
		ptr_user_answer = (void *)(uintptr_t)
					cmd_compat.opensession.answer;

		cmd_desc = te_get_free_cmd_desc_compat(dev);
		params = te_get_free_params_compat(dev, operation->list_count);

		if (!cmd_desc || (operation->list_count && !params)) {
			SET_ANSWER(answer,
				   OTE_ERROR_OUT_OF_MEMORY,
				   OTE_RESULT_ORIGIN_COMMS);
			pr_err("failed to get cmd_desc/params\n");
			goto error;
		}

		request = cmd_desc->req_addr;
		memset(request, 0, sizeof(struct te_request_compat));

		request->params = (uintptr_t)params;
		request->params_size = operation->list_count;

		if (copy_params_from_user_compat(request, operation)) {
			err = -EFAULT;
			pr_info("failed to copy params from user\n");
			goto error;
		}

		te_open_session_compat(&cmd_compat.opensession,
					request, context);

		SET_ANSWER(answer, request->result, request->result_origin);
		answer.session_id = request->session_id;
		break;

	case TE_IOCTL_CLOSE_CLIENT_SESSION_COMPAT:
		ptr_user_answer = (void *)(uintptr_t)
					cmd_compat.closesession.answer;
		cmd_desc = te_get_free_cmd_desc_compat(dev);
		if (!cmd_desc) {
			SET_ANSWER(answer,
				   OTE_ERROR_OUT_OF_MEMORY,
				   OTE_RESULT_ORIGIN_COMMS);
			pr_err("failed to get cmd_desc\n");
			goto error;
		}

		request = cmd_desc->req_addr;
		memset(request, 0, sizeof(struct te_request_compat));

		/* close session cannot fail */
		te_close_session_compat(&cmd_compat.closesession,
					request, context);
		break;

	case TE_IOCTL_LAUNCH_OPERATION_COMPAT:
		operation = &cmd_compat.launchop.operation;
		ptr_user_answer = (void *)(uintptr_t)cmd_compat.launchop.answer;

		cmd_desc = te_get_free_cmd_desc_compat(dev);
		params = te_get_free_params_compat(dev, operation->list_count);

		if (!cmd_desc || (operation->list_count && !params)) {
			SET_ANSWER(answer,
				   OTE_ERROR_OUT_OF_MEMORY,
				   OTE_RESULT_ORIGIN_COMMS);
			pr_err("failed to get cmd_desc/params\n");
			goto error;
		}

		request = cmd_desc->req_addr;
		memset(request, 0, sizeof(struct te_request_compat));

		request->params = (uintptr_t)params;
		request->params_size = operation->list_count;

		if (copy_params_from_user_compat(request, operation)) {
			err = -EFAULT;
			pr_info("failed to copy params from user\n");
			goto error;
		}

		te_launch_operation_compat(&cmd_compat.launchop,
						request, context);

		SET_ANSWER(answer, request->result, request->result_origin);
		break;

	default:
		pr_err("Invalid IOCTL Cmd\n");
		err = -EINVAL;
		goto error;
	}
	if (ptr_user_answer && !err) {
		if (copy_to_user(ptr_user_answer, &answer,
			sizeof(struct te_answer))) {
			pr_err("Failed to copy answer\n");
			err = -EFAULT;
		}
	}
	if (request->params && !err) {
		if (copy_params_to_user_compat(request, operation)) {
			pr_err("Failed to copy return params\n");
			err = -EFAULT;
		}
	}

error:
	if (cmd_desc)
		te_put_used_cmd_desc_compat(dev, cmd_desc);
	if (params)
		te_put_free_params_compat(dev, params, operation->list_count);
	return err;
}

static long tlk_device_ioctl(struct file *file, unsigned int ioctl_num,
	unsigned long ioctl_param)
{
	int err;

	switch (ioctl_num) {
	case TE_IOCTL_OPEN_CLIENT_SESSION:
	case TE_IOCTL_CLOSE_CLIENT_SESSION:
	case TE_IOCTL_LAUNCH_OPERATION:
		mutex_lock(&smc_lock);
		err = te_handle_trustedapp_ioctl(file, ioctl_num, ioctl_param);
		mutex_unlock(&smc_lock);
		break;

	case TE_IOCTL_OPEN_CLIENT_SESSION_COMPAT:
	case TE_IOCTL_CLOSE_CLIENT_SESSION_COMPAT:
	case TE_IOCTL_LAUNCH_OPERATION_COMPAT:
		mutex_lock(&smc_lock);
		err = te_handle_trustedapp_ioctl_compat(file, ioctl_num,
							ioctl_param);
		mutex_unlock(&smc_lock);
		break;

	case TE_IOCTL_SS_NEW_REQ_LEGACY:
	case TE_IOCTL_SS_REQ_COMPLETE_LEGACY:
		err = te_handle_ss_ioctl_legacy(file, ioctl_num, ioctl_param);
		break;

	case TE_IOCTL_SS_NEW_REQ:
	case TE_IOCTL_SS_REQ_COMPLETE:
		err = te_handle_ss_ioctl(file, ioctl_num, ioctl_param);
		break;

	default:
		pr_err("%s: Invalid IOCTL (0x%x) id 0x%x max 0x%lx\n",
			__func__, ioctl_num, _IOC_NR(ioctl_num),
			(unsigned long)TE_IOCTL_MAX_NR);
		err = -EINVAL;
		break;
	}

	return err;
}

/*
 * tlk_driver function definitions.
 */
static const struct file_operations tlk_device_fops = {
	.owner = THIS_MODULE,
	.open = tlk_device_open,
	.release = tlk_device_release,
	.unlocked_ioctl = tlk_device_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tlk_device_ioctl,
#endif
};

struct miscdevice tlk_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "tlk_device",
	.fops = &tlk_device_fops,
};

static int __init tlk_init(void)
{
	int ret;

	INIT_LIST_HEAD(&(tlk_dev.used_cmd_list));
	INIT_LIST_HEAD(&(tlk_dev.free_cmd_list));

	ret = te_create_free_cmd_list(&tlk_dev);
	if (ret != 0)
		return ret;

	return misc_register(&tlk_misc_device);
}

module_init(tlk_init);
