// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/fdtable.h>
#include <linux/anon_inodes.h>
#include <linux/uio.h>
#include "internal.h"

static int cachefiles_ondemand_fd_release(struct inode *inode,
					  struct file *file)
{
	struct cachefiles_object *object = file->private_data;
	struct cachefiles_cache *cache = object->volume->cache;
	struct cachefiles_ondemand_info *info = object->ondemand;
	int object_id = info->ondemand_id;
	struct cachefiles_req *req;
	XA_STATE(xas, &cache->reqs, 0);

	xa_lock(&cache->reqs);
	info->ondemand_id = CACHEFILES_ONDEMAND_ID_CLOSED;
	cachefiles_ondemand_set_object_close(object);

	/* Only flush CACHEFILES_REQ_NEW marked req to avoid race with daemon_read */
	xas_for_each_marked(&xas, req, ULONG_MAX, CACHEFILES_REQ_NEW) {
		if (req->msg.object_id == object_id &&
		    req->msg.opcode == CACHEFILES_OP_CLOSE) {
			complete(&req->done);
			xas_store(&xas, NULL);
		}
	}
	xa_unlock(&cache->reqs);

	xa_erase(&cache->ondemand_ids, object_id);
	trace_cachefiles_ondemand_fd_release(object, object_id);
	cachefiles_put_object(object, cachefiles_obj_put_ondemand_fd);
	cachefiles_put_unbind_pincount(cache);
	return 0;
}

static ssize_t cachefiles_ondemand_fd_write_iter(struct kiocb *kiocb,
						 struct iov_iter *iter)
{
	struct cachefiles_object *object = kiocb->ki_filp->private_data;
	struct cachefiles_cache *cache = object->volume->cache;
	struct file *file = object->file;
	size_t len = iter->count;
	loff_t pos = kiocb->ki_pos;
	const struct cred *saved_cred;
	int ret;

	if (!file)
		return -ENOBUFS;

	cachefiles_begin_secure(cache, &saved_cred);
	ret = __cachefiles_prepare_write(object, file, &pos, &len, len, true);
	cachefiles_end_secure(cache, saved_cred);
	if (ret < 0)
		return ret;

	trace_cachefiles_ondemand_fd_write(object, file_inode(file), pos, len);
	ret = __cachefiles_write(object, file, pos, iter, NULL, NULL);
	if (!ret)
		ret = len;

	return ret;
}

static loff_t cachefiles_ondemand_fd_llseek(struct file *filp, loff_t pos,
					    int whence)
{
	struct cachefiles_object *object = filp->private_data;
	struct file *file = object->file;

	if (!file)
		return -ENOBUFS;

	return vfs_llseek(file, pos, whence);
}

static long cachefiles_ondemand_fd_ioctl(struct file *filp, unsigned int ioctl,
					 unsigned long arg)
{
	struct cachefiles_object *object = filp->private_data;
	struct cachefiles_cache *cache = object->volume->cache;
	struct cachefiles_req *req;
	unsigned long id;

	if (ioctl != CACHEFILES_IOC_READ_COMPLETE)
		return -EINVAL;

	if (!test_bit(CACHEFILES_ONDEMAND_MODE, &cache->flags))
		return -EOPNOTSUPP;

	id = arg;
	req = xa_erase(&cache->reqs, id);
	if (!req)
		return -EINVAL;

	trace_cachefiles_ondemand_cread(object, id);
	complete(&req->done);
	return 0;
}

static const struct file_operations cachefiles_ondemand_fd_fops = {
	.owner		= THIS_MODULE,
	.release	= cachefiles_ondemand_fd_release,
	.write_iter	= cachefiles_ondemand_fd_write_iter,
	.llseek		= cachefiles_ondemand_fd_llseek,
	.unlocked_ioctl	= cachefiles_ondemand_fd_ioctl,
};

/*
 * OPEN request Completion (copen)
 * - command: "copen <id>,<cache_size>"
 *   <cache_size> indicates the object size if >=0, error code if negative
 */
int cachefiles_ondemand_copen(struct cachefiles_cache *cache, char *args)
{
	struct cachefiles_req *req;
	struct fscache_cookie *cookie;
	char *pid, *psize;
	unsigned long id;
	long size;
	int ret;

	if (!test_bit(CACHEFILES_ONDEMAND_MODE, &cache->flags))
		return -EOPNOTSUPP;

	if (!*args) {
		pr_err("Empty id specified\n");
		return -EINVAL;
	}

	pid = args;
	psize = strchr(args, ',');
	if (!psize) {
		pr_err("Cache size is not specified\n");
		return -EINVAL;
	}

	*psize = 0;
	psize++;

	ret = kstrtoul(pid, 0, &id);
	if (ret)
		return ret;

	req = xa_erase(&cache->reqs, id);
	if (!req)
		return -EINVAL;

	/* fail OPEN request if copen format is invalid */
	ret = kstrtol(psize, 0, &size);
	if (ret) {
		req->error = ret;
		goto out;
	}

	/* fail OPEN request if daemon reports an error */
	if (size < 0) {
		if (!IS_ERR_VALUE(size)) {
			req->error = -EINVAL;
			ret = -EINVAL;
		} else {
			req->error = size;
			ret = 0;
		}
		goto out;
	}

	cookie = req->object->cookie;
	cookie->object_size = size;
	if (size)
		clear_bit(FSCACHE_COOKIE_NO_DATA_TO_READ, &cookie->flags);
	else
		set_bit(FSCACHE_COOKIE_NO_DATA_TO_READ, &cookie->flags);
	trace_cachefiles_ondemand_copen(req->object, id, size);

	cachefiles_ondemand_set_object_open(req->object);
	wake_up_all(&cache->daemon_pollwq);

out:
	complete(&req->done);
	return ret;
}

int cachefiles_ondemand_restore(struct cachefiles_cache *cache, char *args)
{
	struct cachefiles_req *req;

	XA_STATE(xas, &cache->reqs, 0);

	if (!test_bit(CACHEFILES_ONDEMAND_MODE, &cache->flags))
		return -EOPNOTSUPP;

	/*
	 * Reset the requests to CACHEFILES_REQ_NEW state, so that the
	 * requests have been processed halfway before the crash of the
	 * user daemon could be reprocessed after the recovery.
	 */
	xas_lock(&xas);
	xas_for_each(&xas, req, ULONG_MAX)
		xas_set_mark(&xas, CACHEFILES_REQ_NEW);
	xas_unlock(&xas);

	wake_up_all(&cache->daemon_pollwq);
	return 0;
}

static int cachefiles_ondemand_get_fd(struct cachefiles_req *req)
{
	struct cachefiles_object *object;
	struct cachefiles_cache *cache;
	struct cachefiles_open *load;
	struct file *file;
	u32 object_id;
	int ret, fd;

	object = cachefiles_grab_object(req->object,
			cachefiles_obj_get_ondemand_fd);
	cache = object->volume->cache;

	ret = xa_alloc_cyclic(&cache->ondemand_ids, &object_id, NULL,
			      XA_LIMIT(1, INT_MAX),
			      &cache->ondemand_id_next, GFP_KERNEL);
	if (ret < 0)
		goto err;

	fd = get_unused_fd_flags(O_WRONLY);
	if (fd < 0) {
		ret = fd;
		goto err_free_id;
	}

	file = anon_inode_getfile("[cachefiles]", &cachefiles_ondemand_fd_fops,
				  object, O_WRONLY);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto err_put_fd;
	}

	file->f_mode |= FMODE_PWRITE | FMODE_LSEEK;
	fd_install(fd, file);

	load = (void *)req->msg.data;
	load->fd = fd;
	object->ondemand->ondemand_id = object_id;

	cachefiles_get_unbind_pincount(cache);
	trace_cachefiles_ondemand_open(object, &req->msg, load);
	return 0;

err_put_fd:
	put_unused_fd(fd);
err_free_id:
	xa_erase(&cache->ondemand_ids, object_id);
err:
	cachefiles_put_object(object, cachefiles_obj_put_ondemand_fd);
	return ret;
}

static void ondemand_object_worker(struct work_struct *work)
{
	struct cachefiles_ondemand_info *info =
		container_of(work, struct cachefiles_ondemand_info, ondemand_work);

	cachefiles_ondemand_init_object(info->object);
}

/*
 * If there are any inflight or subsequent READ requests on the
 * closed object, reopen it.
 * Skip read requests whose related object is reopening.
 */
static struct cachefiles_req *cachefiles_ondemand_select_req(struct xa_state *xas,
							      unsigned long xa_max)
{
	struct cachefiles_req *req;
	struct cachefiles_object *object;
	struct cachefiles_ondemand_info *info;

	xas_for_each_marked(xas, req, xa_max, CACHEFILES_REQ_NEW) {
		if (req->msg.opcode != CACHEFILES_OP_READ)
			return req;
		object = req->object;
		info = object->ondemand;
		if (cachefiles_ondemand_object_is_close(object)) {
			cachefiles_ondemand_set_object_reopening(object);
			queue_work(fscache_wq, &info->ondemand_work);
			continue;
		}
		if (cachefiles_ondemand_object_is_reopening(object))
			continue;
		return req;
	}
	return NULL;
}

ssize_t cachefiles_ondemand_daemon_read(struct cachefiles_cache *cache,
					char __user *_buffer, size_t buflen)
{
	struct cachefiles_req *req;
	struct cachefiles_msg *msg;
	unsigned long id = 0;
	size_t n;
	int ret = 0;
	XA_STATE(xas, &cache->reqs, cache->req_id_next);

	xa_lock(&cache->reqs);
	/*
	 * Cyclically search for a request that has not ever been processed,
	 * to prevent requests from being processed repeatedly, and make
	 * request distribution fair.
	 */
	req = cachefiles_ondemand_select_req(&xas, ULONG_MAX);
	if (!req && cache->req_id_next > 0) {
		xas_set(&xas, 0);
		req = cachefiles_ondemand_select_req(&xas, cache->req_id_next - 1);
	}
	if (!req) {
		xa_unlock(&cache->reqs);
		return 0;
	}

	msg = &req->msg;
	n = msg->len;

	if (n > buflen) {
		xa_unlock(&cache->reqs);
		return -EMSGSIZE;
	}

	xas_clear_mark(&xas, CACHEFILES_REQ_NEW);
	cache->req_id_next = xas.xa_index + 1;
	xa_unlock(&cache->reqs);

	id = xas.xa_index;

	if (msg->opcode == CACHEFILES_OP_OPEN) {
		ret = cachefiles_ondemand_get_fd(req);
		if (ret) {
			cachefiles_ondemand_set_object_close(req->object);
			goto error;
		}
	}

	msg->msg_id = id;
	msg->object_id = req->object->ondemand->ondemand_id;

	if (copy_to_user(_buffer, msg, n) != 0) {
		ret = -EFAULT;
		goto err_put_fd;
	}

	/* CLOSE request has no reply */
	if (msg->opcode == CACHEFILES_OP_CLOSE) {
		xa_erase(&cache->reqs, id);
		complete(&req->done);
	}

	return n;

err_put_fd:
	if (msg->opcode == CACHEFILES_OP_OPEN)
		close_fd(((struct cachefiles_open *)msg->data)->fd);
error:
	xa_erase(&cache->reqs, id);
	req->error = ret;
	complete(&req->done);
	return ret;
}

typedef int (*init_req_fn)(struct cachefiles_req *req, void *private);

static int cachefiles_ondemand_send_req(struct cachefiles_object *object,
					enum cachefiles_opcode opcode,
					size_t data_len,
					init_req_fn init_req,
					void *private)
{
	struct cachefiles_cache *cache = object->volume->cache;
	struct cachefiles_req *req = NULL;
	XA_STATE(xas, &cache->reqs, 0);
	int ret;

	if (!test_bit(CACHEFILES_ONDEMAND_MODE, &cache->flags))
		return 0;

	if (test_bit(CACHEFILES_DEAD, &cache->flags)) {
		ret = -EIO;
		goto out;
	}

	req = kzalloc(sizeof(*req) + data_len, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto out;
	}

	req->object = object;
	init_completion(&req->done);
	req->msg.opcode = opcode;
	req->msg.len = sizeof(struct cachefiles_msg) + data_len;

	ret = init_req(req, private);
	if (ret)
		goto out;

	do {
		/*
		 * Stop enqueuing the request when daemon is dying. The
		 * following two operations need to be atomic as a whole.
		 *   1) check cache state, and
		 *   2) enqueue request if cache is alive.
		 * Otherwise the request may be enqueued after xarray has been
		 * flushed, leaving the orphan request never being completed.
		 *
		 * CPU 1			CPU 2
		 * =====			=====
		 *				test CACHEFILES_DEAD bit
		 * set CACHEFILES_DEAD bit
		 * flush requests in the xarray
		 *				enqueue the request
		 */
		xas_lock(&xas);

		if (test_bit(CACHEFILES_DEAD, &cache->flags)) {
			xas_unlock(&xas);
			ret = -EIO;
			goto out;
		}

		/* coupled with the barrier in cachefiles_flush_reqs() */
		smp_mb();

		if (opcode == CACHEFILES_OP_CLOSE &&
			!cachefiles_ondemand_object_is_open(object)) {
			WARN_ON_ONCE(object->ondemand->ondemand_id == 0);
			xas_unlock(&xas);
			ret = -EIO;
			goto out;
		}

		xas.xa_index = 0;
		xas_find_marked(&xas, UINT_MAX, XA_FREE_MARK);
		if (xas.xa_node == XAS_RESTART)
			xas_set_err(&xas, -EBUSY);
		xas_store(&xas, req);
		xas_clear_mark(&xas, XA_FREE_MARK);
		xas_set_mark(&xas, CACHEFILES_REQ_NEW);
		xas_unlock(&xas);
	} while (xas_nomem(&xas, GFP_KERNEL));

	ret = xas_error(&xas);
	if (ret)
		goto out;

	wake_up_all(&cache->daemon_pollwq);
	wait_for_completion(&req->done);
	ret = req->error;
	kfree(req);
	return ret;
out:
	/* Reset the object to close state in error handling path.
	 * If error occurs after creating the anonymous fd,
	 * cachefiles_ondemand_fd_release() will set object to close.
	 */
	if (opcode == CACHEFILES_OP_OPEN)
		cachefiles_ondemand_set_object_close(object);
	kfree(req);
	return ret;
}

static int cachefiles_ondemand_init_open_req(struct cachefiles_req *req,
					     void *private)
{
	struct cachefiles_object *object = req->object;
	struct fscache_cookie *cookie = object->cookie;
	struct fscache_volume *volume = object->volume->vcookie;
	struct cachefiles_open *load = (void *)req->msg.data;
	size_t volume_key_size, cookie_key_size;
	void *volume_key, *cookie_key;

	/*
	 * Volume key is a NUL-terminated string. key[0] stores strlen() of the
	 * string, followed by the content of the string (excluding '\0').
	 */
	volume_key_size = volume->key[0] + 1;
	volume_key = volume->key + 1;

	/* Cookie key is binary data, which is netfs specific. */
	cookie_key_size = cookie->key_len;
	cookie_key = fscache_get_key(cookie);

	if (!(object->cookie->advice & FSCACHE_ADV_WANT_CACHE_SIZE)) {
		pr_err("WANT_CACHE_SIZE is needed for on-demand mode\n");
		return -EINVAL;
	}

	load->volume_key_size = volume_key_size;
	load->cookie_key_size = cookie_key_size;
	memcpy(load->data, volume_key, volume_key_size);
	memcpy(load->data + volume_key_size, cookie_key, cookie_key_size);

	return 0;
}

static int cachefiles_ondemand_init_close_req(struct cachefiles_req *req,
					      void *private)
{
	struct cachefiles_object *object = req->object;

	if (!cachefiles_ondemand_object_is_open(object))
		return -ENOENT;

	trace_cachefiles_ondemand_close(object, &req->msg);
	return 0;
}

struct cachefiles_read_ctx {
	loff_t off;
	size_t len;
};

static int cachefiles_ondemand_init_read_req(struct cachefiles_req *req,
					     void *private)
{
	struct cachefiles_object *object = req->object;
	struct cachefiles_read *load = (void *)req->msg.data;
	struct cachefiles_read_ctx *read_ctx = private;

	load->off = read_ctx->off;
	load->len = read_ctx->len;
	trace_cachefiles_ondemand_read(object, &req->msg, load);
	return 0;
}

int cachefiles_ondemand_init_object(struct cachefiles_object *object)
{
	struct fscache_cookie *cookie = object->cookie;
	struct fscache_volume *volume = object->volume->vcookie;
	size_t volume_key_size, cookie_key_size, data_len;

	if (!object->ondemand)
		return 0;

	/*
	 * CacheFiles will firstly check the cache file under the root cache
	 * directory. If the coherency check failed, it will fallback to
	 * creating a new tmpfile as the cache file. Reuse the previously
	 * allocated object ID if any.
	 */
	if (cachefiles_ondemand_object_is_open(object))
		return 0;

	volume_key_size = volume->key[0] + 1;
	cookie_key_size = cookie->key_len;
	data_len = sizeof(struct cachefiles_open) +
		   volume_key_size + cookie_key_size;

	return cachefiles_ondemand_send_req(object, CACHEFILES_OP_OPEN,
			data_len, cachefiles_ondemand_init_open_req, NULL);
}

void cachefiles_ondemand_clean_object(struct cachefiles_object *object)
{
	cachefiles_ondemand_send_req(object, CACHEFILES_OP_CLOSE, 0,
			cachefiles_ondemand_init_close_req, NULL);
}

int cachefiles_ondemand_init_obj_info(struct cachefiles_object *object,
				struct cachefiles_volume *volume)
{
	if (!cachefiles_in_ondemand_mode(volume->cache))
		return 0;

	object->ondemand = kzalloc(sizeof(struct cachefiles_ondemand_info),
					GFP_KERNEL);
	if (!object->ondemand)
		return -ENOMEM;

	object->ondemand->object = object;
	INIT_WORK(&object->ondemand->ondemand_work, ondemand_object_worker);
	return 0;
}

void cachefiles_ondemand_deinit_obj_info(struct cachefiles_object *object)
{
	kfree(object->ondemand);
	object->ondemand = NULL;
}

int cachefiles_ondemand_read(struct cachefiles_object *object,
			     loff_t pos, size_t len)
{
	struct cachefiles_read_ctx read_ctx = {pos, len};

	return cachefiles_ondemand_send_req(object, CACHEFILES_OP_READ,
			sizeof(struct cachefiles_read),
			cachefiles_ondemand_init_read_req, &read_ctx);
}
