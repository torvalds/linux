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
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/file.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/anon_inodes.h>

#include "tee_shm.h"
#include "tee_core_priv.h"

static int _init_tee_cmd(struct tee_session *sess, struct tee_cmd_io *cmd_io,
			 struct tee_cmd *cmd);
static void _update_client_tee_cmd(struct tee_session *sess,
				   struct tee_cmd_io *cmd_io,
				   struct tee_cmd *cmd);
static void _release_tee_cmd(struct tee_session *sess, struct tee_cmd *cmd);

#define _DEV_TEE _DEV(sess->ctx->tee)

#define INMSG dev_dbg(_DEV_TEE, "%s: >\n", __func__)
#define OUTMSG(val) dev_dbg(_DEV_TEE, "%s: < %d\n", __func__, (int)val)

/******************************************************************************/

static inline bool flag_set(int val, int flags)
{
	return (val & flags) == flags;
}

static inline bool is_mapped_temp(int flags)
{
	return flag_set(flags, TEE_SHM_MAPPED | TEE_SHM_TEMP);
}


/******************************************************************************/

#define _UUID_STR_SIZE 35
static char *_uuid_to_str(const TEEC_UUID *uuid)
{
	static char uuid_str[_UUID_STR_SIZE];

	if (uuid) {
		sprintf(uuid_str,
			"%08x-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x",
			uuid->timeLow, uuid->timeMid, uuid->timeHiAndVersion,
			uuid->clockSeqAndNode[0], uuid->clockSeqAndNode[1],
			uuid->clockSeqAndNode[2], uuid->clockSeqAndNode[3],
			uuid->clockSeqAndNode[4], uuid->clockSeqAndNode[5],
			uuid->clockSeqAndNode[6], uuid->clockSeqAndNode[7]);
	} else {
		sprintf(uuid_str, "NULL");
	}

	return uuid_str;
}

static int tee_copy_from_user(struct tee_context *ctx, void *to, void *from,
			      size_t size)
{
	if ((!to) || (!from) || (!size))
		return 0;
	if (ctx->usr_client)
		return copy_from_user(to, from, size);
	else {
		memcpy(to, from, size);
		return 0;
	}
}

static int tee_copy_to_user(struct tee_context *ctx, void *to, void *from,
			    size_t size)
{
	if ((!to) || (!from) || (!size))
		return 0;
	if (ctx->usr_client)
		return copy_to_user(to, from, size);
	else {
		memcpy(to, from, size);
		return 0;
	}
}

/* Defined as macro to let the put_user macro see the types */
#define tee_put_user(ctx, from, to)				\
	do {							\
		if ((ctx)->usr_client)				\
			put_user(from, to);			\
		else						\
			*to = from;				\
	} while (0)

static inline int tee_session_is_opened(struct tee_session *sess)
{
	if (sess && sess->sessid)
		return (sess->sessid != 0);
	return 0;
}

static int tee_session_open_be(struct tee_session *sess,
			       struct tee_cmd_io *cmd_io)
{
	int ret = -EINVAL;
	struct tee *tee;
	struct tee_cmd cmd;

	BUG_ON(!sess || !sess->ctx || !sess->ctx->tee);

	tee = sess->ctx->tee;

	dev_dbg(_DEV(tee), "%s: > open a new session", __func__);

	sess->sessid = 0;
	ret = _init_tee_cmd(sess, cmd_io, &cmd);
	if (ret)
		goto out;

	if (cmd.uuid) {
		dev_dbg(_DEV(tee), "%s: UUID=%s\n", __func__,
			_uuid_to_str((TEEC_UUID *) cmd.uuid->kaddr));
	}

	ret = tee->ops->open(sess, &cmd);
	if (ret == 0)
		_update_client_tee_cmd(sess, cmd_io, &cmd);
	else {
		/* propagate the reason of the error */
		cmd_io->origin = cmd.origin;
		cmd_io->err = cmd.err;
	}

out:
	_release_tee_cmd(sess, &cmd);
	dev_dbg(_DEV(tee), "%s: < ret=%d, sessid=%08x", __func__, ret,
		sess->sessid);
	return ret;
}

int tee_session_invoke_be(struct tee_session *sess, struct tee_cmd_io *cmd_io)
{
	int ret = -EINVAL;
	struct tee *tee;
	struct tee_cmd cmd;

	BUG_ON(!sess || !sess->ctx || !sess->ctx->tee);

	tee = sess->ctx->tee;

	dev_dbg(_DEV(tee), "%s: > sessid=%08x, cmd=0x%08x\n", __func__,
		sess->sessid, cmd_io->cmd);

	ret = _init_tee_cmd(sess, cmd_io, &cmd);
	if (ret)
		goto out;

	ret = tee->ops->invoke(sess, &cmd);
	if (!ret)
		_update_client_tee_cmd(sess, cmd_io, &cmd);
	else {
		/* propagate the reason of the error */
		cmd_io->origin = cmd.origin;
		cmd_io->err = cmd.err;
	}

out:
	_release_tee_cmd(sess, &cmd);
	dev_dbg(_DEV(tee), "%s: < ret=%d", __func__, ret);
	return ret;
}

static int tee_session_close_be(struct tee_session *sess)
{
	int ret = -EINVAL;
	struct tee *tee;

	BUG_ON(!sess || !sess->ctx || !sess->ctx->tee);

	tee = sess->ctx->tee;

	dev_dbg(_DEV(tee), "%s: > sessid=%08x", __func__, sess->sessid);

	ret = tee->ops->close(sess);
	sess->sessid = 0;

	dev_dbg(_DEV(tee), "%s: < ret=%d", __func__, ret);
	return ret;
}

static int tee_session_cancel_be(struct tee_session *sess,
				 struct tee_cmd_io *cmd_io)
{
	int ret = -EINVAL;
	struct tee *tee;
	struct tee_cmd cmd;

	BUG_ON(!sess || !sess->ctx || !sess->ctx->tee);

	tee = sess->ctx->tee;

	dev_dbg(_DEV(tee), "%s: > sessid=%08x, cmd=0x%08x\n", __func__,
		sess->sessid, cmd_io->cmd);

	ret = _init_tee_cmd(sess, cmd_io, &cmd);
	if (ret)
		goto out;

	ret = tee->ops->cancel(sess, &cmd);

out:
	_release_tee_cmd(sess, &cmd);
	dev_dbg(_DEV(tee), "%s: < ret=%d", __func__, ret);
	return ret;
}

static int tee_do_invoke_command(struct tee_session *sess,
				 struct tee_cmd_io __user *u_cmd)
{
	int ret = -EINVAL;
	struct tee *tee;
	struct tee_cmd_io k_cmd;
	struct tee_context *ctx;

	BUG_ON(!sess->ctx);
	BUG_ON(!sess->ctx->tee);
	ctx = sess->ctx;
	tee = sess->ctx->tee;

	dev_dbg(_DEV(tee), "%s: > sessid=%08x\n", __func__, sess->sessid);

	BUG_ON(!sess->sessid);

	if (tee_copy_from_user
	    (ctx, &k_cmd, (void *)u_cmd, sizeof(struct tee_cmd_io))) {
		dev_err(_DEV(tee), "%s: tee_copy_from_user failed\n", __func__);
		goto exit;
	}

	if ((k_cmd.op == NULL) || (k_cmd.uuid != NULL) ||
	    (k_cmd.data != NULL) || (k_cmd.data_size != 0)) {
		dev_err(_DEV(tee),
			"%s: op or/and data parameters are not valid\n",
			__func__);
		goto exit;
	}

	ret = tee_session_invoke_be(sess, &k_cmd);
	if (ret)
		dev_err(_DEV(tee), "%s: tee_invoke_command failed\n", __func__);

	tee_put_user(ctx, k_cmd.err, &u_cmd->err);
	tee_put_user(ctx, k_cmd.origin, &u_cmd->origin);

exit:
	dev_dbg(_DEV(tee), "%s: < ret=%d\n", __func__, ret);
	return ret;
}

static int tee_do_cancel_cmd(struct tee_session *sess,
			     struct tee_cmd_io __user *u_cmd)
{
	int ret = -EINVAL;
	struct tee *tee;
	struct tee_cmd_io k_cmd;
	struct tee_context *ctx;

	BUG_ON(!sess->ctx);
	BUG_ON(!sess->ctx->tee);
	ctx = sess->ctx;
	tee = sess->ctx->tee;

	dev_dbg(sess->ctx->tee->dev, "%s: > sessid=%08x\n", __func__,
		sess->sessid);

	BUG_ON(!sess->sessid);

	if (tee_copy_from_user
	    (ctx, &k_cmd, (void *)u_cmd, sizeof(struct tee_cmd_io))) {
		dev_err(_DEV(tee), "%s: tee_copy_from_user failed\n", __func__);
		goto exit;
	}

	if ((k_cmd.op == NULL) || (k_cmd.uuid != NULL) ||
	    (k_cmd.data != NULL) || (k_cmd.data_size != 0)) {
		dev_err(_DEV(tee),
			"%s: op or/and data parameters are not valid\n",
			__func__);
		goto exit;
	}

	ret = tee_session_cancel_be(sess, &k_cmd);
	if (ret)
		dev_err(_DEV(tee), "%s: tee_invoke_command failed\n", __func__);

	tee_put_user(ctx, k_cmd.err, &u_cmd->err);
	tee_put_user(ctx, k_cmd.origin, &u_cmd->origin);

exit:
	dev_dbg(_DEV(tee), "%s: < ret=%d", __func__, ret);
	return ret;
}

static long tee_session_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg)
{
	struct tee *tee;
	struct tee_session *sess = filp->private_data;
	int ret;

	BUG_ON(!sess || !sess->ctx || !sess->ctx->tee);

	tee = sess->ctx->tee;

	dev_dbg(_DEV(tee), "%s: > cmd nr=%d\n", __func__, _IOC_NR(cmd));

	switch (cmd) {
	case TEE_INVOKE_COMMAND_IOC:
		ret =
		    tee_do_invoke_command(sess,
					  (struct tee_cmd_io __user *)arg);
		break;
	case TEE_REQUEST_CANCELLATION_IOC:
		ret = tee_do_cancel_cmd(sess, (struct tee_cmd_io __user *)arg);
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	dev_dbg(_DEV(tee), "%s: < ret=%d\n", __func__, ret);

	return ret;
}

static int tee_session_release(struct inode *inode, struct file *filp)
{
	struct tee_session *sess = filp->private_data;
	int ret = 0;
	struct tee *tee;

	BUG_ON(!sess || !sess->ctx || !sess->ctx->tee);
	tee = sess->ctx->tee;

	ret = tee_session_close_and_destroy(sess);
	return ret;
}

const struct file_operations tee_session_fops = {
	.owner = THIS_MODULE,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tee_session_ioctl,
#endif
	.unlocked_ioctl = tee_session_ioctl,
	.compat_ioctl = tee_session_ioctl,
	.release = tee_session_release,
};

int tee_session_close_and_destroy(struct tee_session *sess)
{
	int ret;
	struct tee *tee;
	struct tee_context *ctx;

	if (!sess || !sess->ctx || !sess->ctx->tee)
		return -EINVAL;

	ctx = sess->ctx;
	tee = ctx->tee;

	dev_dbg(_DEV(tee), "%s: > sess=%p\n", __func__, sess);

	if (!tee_session_is_opened(sess))
		return -EINVAL;

	ret = tee_session_close_be(sess);

	mutex_lock(&tee->lock);
	tee_dec_stats(&tee->stats[TEE_STATS_SESSION_IDX]);
	list_del(&sess->entry);

	devm_kfree(_DEV(tee), sess);
	tee_context_put(ctx);
	tee_put(tee);
	mutex_unlock(&tee->lock);

	dev_dbg(_DEV(tee), "%s: <\n", __func__);
	return ret;
}

struct tee_session *tee_session_create_and_open(struct tee_context *ctx,
						struct tee_cmd_io *cmd_io)
{
	int ret = 0;
	struct tee_session *sess;
	struct tee *tee;

	BUG_ON(!ctx->tee);

	tee = ctx->tee;

	dev_dbg(_DEV(tee), "%s: >\n", __func__);
	ret = tee_get(tee);
	if (ret)
		return ERR_PTR(-EBUSY);

	sess = devm_kzalloc(_DEV(tee), sizeof(struct tee_session), GFP_KERNEL);
	if (!sess) {
		dev_err(_DEV(tee), "%s: tee_session allocation() failed\n",
			__func__);
		tee_put(tee);
		return ERR_PTR(-ENOMEM);
	}

	tee_context_get(ctx);
	sess->ctx = ctx;

	ret = tee_session_open_be(sess, cmd_io);
	mutex_lock(&tee->lock);
	if (ret || !sess->sessid || cmd_io->err) {
		dev_err(_DEV(tee), "%s: ERROR ret=%d (err=0x%08x, org=%d,  sessid=0x%08x)\n",
				__func__, ret, cmd_io->err,
				cmd_io->origin, sess->sessid);
		tee_put(tee);
		tee_context_put(ctx);
		devm_kfree(_DEV(tee), sess);
		mutex_unlock(&tee->lock);
		if (ret)
			return ERR_PTR(ret);
		else
			return NULL;
	}

	tee_inc_stats(&tee->stats[TEE_STATS_SESSION_IDX]);
	list_add_tail(&sess->entry, &ctx->list_sess);
	mutex_unlock(&tee->lock);

	dev_dbg(_DEV(tee), "%s: < sess=%p\n", __func__, sess);
	return sess;
}

int tee_session_create_fd(struct tee_context *ctx, struct tee_cmd_io *cmd_io)
{
	int ret;
	struct tee_session *sess;
	struct tee *tee = ctx->tee;

	BUG_ON(cmd_io->fd_sess > 0);

	dev_dbg(_DEV(tee), "%s: >\n", __func__);

	sess = tee_session_create_and_open(ctx, cmd_io);
	if (IS_ERR_OR_NULL(sess)) {
		ret = PTR_ERR(sess);
		dev_dbg(_DEV(tee), "%s: ERROR can't create the session (ret=%d, err=0x%08x, org=%d)\n",
			__func__, ret, cmd_io->err, cmd_io->origin);
		cmd_io->fd_sess = -1;
		goto out;
	}

	/* Retrieve a fd */
	cmd_io->fd_sess = -1;
	ret =
	    anon_inode_getfd("tee_session", &tee_session_fops, sess, O_CLOEXEC);
	if (ret < 0) {
		dev_err(_DEV(tee), "%s: ERROR can't get a fd (ret=%d)\n",
			__func__, ret);
		tee_session_close_and_destroy(sess);
		goto out;
	}
	cmd_io->fd_sess = ret;
	ret = 0;

out:
	dev_dbg(_DEV(tee), "%s: < ret=%d, sess=%p, fd=%d\n", __func__,
		ret, sess, cmd_io->fd_sess);
	return ret;
}

static bool tee_session_is_supported_type(struct tee_session *sess, int type)
{
	switch (type) {
	case TEEC_NONE:
	case TEEC_VALUE_INPUT:
	case TEEC_VALUE_OUTPUT:
	case TEEC_VALUE_INOUT:
	case TEEC_MEMREF_TEMP_INPUT:
	case TEEC_MEMREF_TEMP_OUTPUT:
	case TEEC_MEMREF_TEMP_INOUT:
	case TEEC_MEMREF_WHOLE:
	case TEEC_MEMREF_PARTIAL_INPUT:
	case TEEC_MEMREF_PARTIAL_OUTPUT:
	case TEEC_MEMREF_PARTIAL_INOUT:
		return true;
	default:
		dev_err(_DEV_TEE, "type is invalid (type %02x)\n", type);
		return false;
	}
}

static int to_memref_type(int flags)
{
	if (flag_set(flags, TEEC_MEM_INPUT | TEEC_MEM_OUTPUT))
		return TEEC_MEMREF_TEMP_INOUT;

	if (flag_set(flags, TEEC_MEM_INPUT))
		return TEEC_MEMREF_TEMP_INPUT;

	if (flag_set(flags, TEEC_MEM_OUTPUT))
		return TEEC_MEMREF_TEMP_OUTPUT;

	pr_err("%s: bad flags=%x\n", __func__, flags);
	return 0;
}

static int _init_tee_cmd(struct tee_session *sess, struct tee_cmd_io *cmd_io,
			 struct tee_cmd *cmd)
{
	int ret = -EINVAL;
	int idx;
	TEEC_Operation op;
	struct tee_data *param = &cmd->param;
	struct tee *tee;
	struct tee_context *ctx;

	BUG_ON(!sess->ctx);
	BUG_ON(!sess->ctx->tee);
	ctx = sess->ctx;
	tee = sess->ctx->tee;

	dev_dbg(_DEV(tee), "%s: > sessid=%08x\n", __func__, sess->sessid);

	memset(cmd, 0, sizeof(struct tee_cmd));

	cmd->cmd = cmd_io->cmd;
	cmd->origin = TEEC_ORIGIN_TEE;
	cmd->err = TEEC_ERROR_BAD_PARAMETERS;
	cmd_io->origin = cmd->origin;
	cmd_io->err = cmd->err;

	if (tee_context_copy_from_client(ctx, &op, cmd_io->op, sizeof(op)))
		goto out;

	cmd->param.type_original = op.paramTypes;

	for (idx = 0; idx < TEEC_CONFIG_PAYLOAD_REF_COUNT; ++idx) {
		uint32_t offset = 0;
		uint32_t size = 0;
		int type = TEEC_PARAM_TYPE_GET(op.paramTypes, idx);

		switch (type) {
		case TEEC_NONE:
			break;

		case TEEC_VALUE_INPUT:
		case TEEC_VALUE_OUTPUT:
		case TEEC_VALUE_INOUT:
			param->params[idx].value = op.params[idx].value;
			dev_dbg(_DEV_TEE,
				"%s: param[%d]:type=%d,a=%08x,b=%08x (VALUE)\n",
				__func__, idx, type, param->params[idx].value.a,
				param->params[idx].value.b);
			break;

		case TEEC_MEMREF_TEMP_INPUT:
		case TEEC_MEMREF_TEMP_OUTPUT:
		case TEEC_MEMREF_TEMP_INOUT:
			dev_dbg(_DEV_TEE,
				"> param[%d]:type=%d,buffer=%p,s=%zu (TMPREF)\n",
				idx, type, op.params[idx].tmpref.buffer,
				op.params[idx].tmpref.size);

			param->params[idx].shm =
			    tee_context_create_tmpref_buffer(ctx,
					     op.params[idx].tmpref.size,
					     op.params[idx].tmpref.buffer,
					     type);
			if (IS_ERR_OR_NULL(param->params[idx].shm))
				goto out;

			dev_dbg(_DEV_TEE, "< %d %pad:%zd\n", idx,
					&param->params[idx].shm->paddr,
					param->params[idx].shm->size_alloc);
			break;

		case TEEC_MEMREF_PARTIAL_INPUT:
		case TEEC_MEMREF_PARTIAL_OUTPUT:
		case TEEC_MEMREF_PARTIAL_INOUT:
		case TEEC_MEMREF_WHOLE:
			if (tee_copy_from_user(ctx, &param->c_shm[idx],
						op.params[idx].memref.parent,
						sizeof(param->c_shm[idx]))) {
				goto out;
			}

			if (type == TEEC_MEMREF_WHOLE) {
				offset = 0;
				size = param->c_shm[idx].size;
			} else { /* for PARTIAL, check the size */
				offset = op.params[idx].memref.offset;
				size = op.params[idx].memref.size;
				if (param->c_shm[idx].size < size + offset) {
					dev_err(_DEV(tee), "A PARTIAL parameter is bigger than the parent %zd < %d + %d\n",
						param->c_shm[idx].size, size,
						offset);
					goto out;
				}
			}

			dev_dbg(_DEV_TEE, "> param[%d]:type=%d,buffer=%p, offset=%d size=%d\n",
					idx, type, param->c_shm[idx].buffer,
					offset, size);

			type = to_memref_type(param->c_shm[idx].flags);
			if (type == 0)
				goto out;

			param->params[idx].shm = tee_shm_get(ctx,
					&param->c_shm[idx], size, offset);

			if (IS_ERR_OR_NULL(param->params[idx].shm)) {
				param->params[idx].shm =
				    tee_context_create_tmpref_buffer(ctx, size,
				       param->c_shm[idx].buffer + offset, type);

				if (IS_ERR_OR_NULL(param->params[idx].shm))
					goto out;
			}

			dev_dbg(_DEV_TEE, "< %d %pad:%zd\n", idx,
				&param->params[idx].shm->paddr,
				param->params[idx].shm->size_req);
			break;

		default:
			BUG_ON(1);
		}

		param->type |= (type << (idx * 4));
	}

	if (cmd_io->uuid != NULL) {
		dev_dbg(_DEV_TEE, "%s: copy UUID value...\n", __func__);
		cmd->uuid = tee_context_alloc_shm_tmp(sess->ctx,
			sizeof(*cmd_io->uuid), cmd_io->uuid, TEEC_MEM_INPUT);
		if (IS_ERR_OR_NULL(cmd->uuid)) {
			ret = -EINVAL;
			goto out;
		}
	}

	ret = 0;

out:
	if (ret)
		_release_tee_cmd(sess, cmd);

	dev_dbg(_DEV_TEE, "%s: < ret=%d\n", __func__, ret);
	return ret;
}

static void _update_client_tee_cmd(struct tee_session *sess,
				   struct tee_cmd_io *cmd_io,
				   struct tee_cmd *cmd)
{
	int idx;
	struct tee_context *ctx;
	TEEC_Operation op;

	BUG_ON(!cmd_io);
	BUG_ON(!cmd_io->op);
	BUG_ON(!cmd_io->op->params);
	BUG_ON(!cmd);
	BUG_ON(!sess->ctx);
	ctx = sess->ctx;

	dev_dbg(_DEV_TEE, "%s: returned err=0x%08x (origin=%d)\n", __func__,
		cmd->err, cmd->origin);

	cmd_io->origin = cmd->origin;
	cmd_io->err = cmd->err;

	if (cmd->param.type_original == TEEC_PARAM_TYPES(TEEC_NONE,
			TEEC_NONE, TEEC_NONE, TEEC_NONE))
		return;

	if (tee_context_copy_from_client(ctx, &op, cmd_io->op, sizeof(op)))
		return;

	for (idx = 0; idx < TEEC_CONFIG_PAYLOAD_REF_COUNT; ++idx) {
		int type = TEEC_PARAM_TYPE_GET(cmd->param.type_original, idx);
		int offset = 0;
		size_t size;
		size_t size_new;
		TEEC_SharedMemory *parent;

		dev_dbg(_DEV_TEE, "%s: id %d type %d\n", __func__, idx, type);
		BUG_ON(!tee_session_is_supported_type(sess, type));
		switch (type) {
		case TEEC_NONE:
		case TEEC_VALUE_INPUT:
		case TEEC_MEMREF_TEMP_INPUT:
		case TEEC_MEMREF_PARTIAL_INPUT:
			break;
		case TEEC_VALUE_OUTPUT:
		case TEEC_VALUE_INOUT:
			dev_dbg(_DEV_TEE, "%s: a=%08x, b=%08x\n",
				__func__,
				cmd->param.params[idx].value.a,
				cmd->param.params[idx].value.b);
			if (tee_copy_to_user
			    (ctx, &cmd_io->op->params[idx].value,
			     &cmd->param.params[idx].value,
			     sizeof(cmd_io->op->params[idx].value)))
				dev_err(_DEV_TEE,
					"%s:%d: can't update %d result to user\n",
					__func__, __LINE__, idx);
			break;
		case TEEC_MEMREF_TEMP_OUTPUT:
		case TEEC_MEMREF_TEMP_INOUT:
			/* Returned updated size */
			size_new = cmd->param.params[idx].shm->size_req;
			if (size_new !=
				op.params[idx].tmpref.size) {
				dev_dbg(_DEV_TEE,
					"Size has been updated by the TA %zd != %zd\n",
					size_new,
					op.params[idx].tmpref.size);
				tee_put_user(ctx, size_new,
				     &cmd_io->op->params[idx].tmpref.size);
			}

			dev_dbg(_DEV_TEE, "%s: tmpref %p\n", __func__,
				cmd->param.params[idx].shm->kaddr);

			/* ensure we do not exceed the shared buffer length */
			if (size_new > op.params[idx].tmpref.size)
				dev_err(_DEV_TEE,
					"  *** Wrong returned size from %d:%zd > %zd\n",
					idx, size_new,
					op.params[idx].tmpref.size);

			else if (tee_copy_to_user
				 (ctx,
				  op.params[idx].tmpref.buffer,
				  cmd->param.params[idx].shm->kaddr,
				  size_new))
				dev_err(_DEV_TEE,
					"%s:%d: can't update %d result to user\n",
					__func__, __LINE__, idx);
			break;

		case TEEC_MEMREF_PARTIAL_OUTPUT:
		case TEEC_MEMREF_PARTIAL_INOUT:
		case TEEC_MEMREF_WHOLE:
			parent = &cmd->param.c_shm[idx];
			if (type == TEEC_MEMREF_WHOLE) {
				offset = 0;
				size = parent->size;
			} else {
				offset = op.params[idx].memref.offset;
				size = op.params[idx].memref.size;
			}

			/* Returned updated size */
			size_new = cmd->param.params[idx].shm->size_req;
			tee_put_user(ctx, size_new,
					&cmd_io->op->params[idx].memref.size);

			/*
			 * If we allocated a tmpref buffer,
			 * copy back data to the user buffer
			 */
			if (is_mapped_temp(cmd->param.params[idx].shm->flags)) {
				if (parent->buffer &&
					offset + size_new <= parent->size) {
					if (tee_copy_to_user(ctx,
					   parent->buffer + offset,
					   cmd->param.params[idx].shm->kaddr,
					   size_new))
							dev_err(_DEV_TEE,
								"%s: can't update %d data to user\n",
								__func__, idx);
				}
			}
			break;
		default:
			BUG_ON(1);
		}
	}

}

static void _release_tee_cmd(struct tee_session *sess, struct tee_cmd *cmd)
{
	int idx;
	struct tee_context *ctx;

	BUG_ON(!cmd);
	BUG_ON(!sess);
	BUG_ON(!sess->ctx);
	BUG_ON(!sess->ctx->tee);

	ctx = sess->ctx;

	dev_dbg(_DEV_TEE, "%s: > free the temporary objects...\n", __func__);

	rk_tee_shm_free(cmd->uuid);

	if (cmd->param.type_original == TEEC_PARAM_TYPES(TEEC_NONE,
			TEEC_NONE, TEEC_NONE, TEEC_NONE))
		goto out;

	for (idx = 0; idx < TEEC_CONFIG_PAYLOAD_REF_COUNT; ++idx) {
		int type = TEEC_PARAM_TYPE_GET(cmd->param.type_original, idx);
		struct tee_shm *shm;
		switch (type) {
		case TEEC_NONE:
		case TEEC_VALUE_INPUT:
		case TEEC_VALUE_OUTPUT:
		case TEEC_VALUE_INOUT:
			break;
		case TEEC_MEMREF_TEMP_INPUT:
		case TEEC_MEMREF_TEMP_OUTPUT:
		case TEEC_MEMREF_TEMP_INOUT:
		case TEEC_MEMREF_WHOLE:
		case TEEC_MEMREF_PARTIAL_INPUT:
		case TEEC_MEMREF_PARTIAL_OUTPUT:
		case TEEC_MEMREF_PARTIAL_INOUT:
			if (IS_ERR_OR_NULL(cmd->param.params[idx].shm))
				break;

			shm = cmd->param.params[idx].shm;

			if (is_mapped_temp(shm->flags))
				rk_tee_shm_free(shm);
			else
				rk_tee_shm_put(ctx, shm);
			break;
		default:
			BUG_ON(1);
		}
	}

out:
	memset(cmd, 0, sizeof(struct tee_cmd));
	dev_dbg(_DEV_TEE, "%s: <\n", __func__);
}
