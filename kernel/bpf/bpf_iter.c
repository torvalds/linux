// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */

#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/filter.h>
#include <linux/bpf.h>
#include <linux/rcupdate_trace.h>

struct bpf_iter_target_info {
	struct list_head list;
	const struct bpf_iter_reg *reg_info;
	u32 btf_id;	/* cached value */
};

struct bpf_iter_link {
	struct bpf_link link;
	struct bpf_iter_aux_info aux;
	struct bpf_iter_target_info *tinfo;
};

struct bpf_iter_priv_data {
	struct bpf_iter_target_info *tinfo;
	const struct bpf_iter_seq_info *seq_info;
	struct bpf_prog *prog;
	u64 session_id;
	u64 seq_num;
	bool done_stop;
	u8 target_private[] __aligned(8);
};

static struct list_head targets = LIST_HEAD_INIT(targets);
static DEFINE_MUTEX(targets_mutex);

/* protect bpf_iter_link changes */
static DEFINE_MUTEX(link_mutex);

/* incremented on every opened seq_file */
static atomic64_t session_id;

static int prepare_seq_file(struct file *file, struct bpf_iter_link *link,
			    const struct bpf_iter_seq_info *seq_info);

static void bpf_iter_inc_seq_num(struct seq_file *seq)
{
	struct bpf_iter_priv_data *iter_priv;

	iter_priv = container_of(seq->private, struct bpf_iter_priv_data,
				 target_private);
	iter_priv->seq_num++;
}

static void bpf_iter_dec_seq_num(struct seq_file *seq)
{
	struct bpf_iter_priv_data *iter_priv;

	iter_priv = container_of(seq->private, struct bpf_iter_priv_data,
				 target_private);
	iter_priv->seq_num--;
}

static void bpf_iter_done_stop(struct seq_file *seq)
{
	struct bpf_iter_priv_data *iter_priv;

	iter_priv = container_of(seq->private, struct bpf_iter_priv_data,
				 target_private);
	iter_priv->done_stop = true;
}

static inline bool bpf_iter_target_support_resched(const struct bpf_iter_target_info *tinfo)
{
	return tinfo->reg_info->feature & BPF_ITER_RESCHED;
}

static bool bpf_iter_support_resched(struct seq_file *seq)
{
	struct bpf_iter_priv_data *iter_priv;

	iter_priv = container_of(seq->private, struct bpf_iter_priv_data,
				 target_private);
	return bpf_iter_target_support_resched(iter_priv->tinfo);
}

/* maximum visited objects before bailing out */
#define MAX_ITER_OBJECTS	1000000

/* bpf_seq_read, a customized and simpler version for bpf iterator.
 * The following are differences from seq_read():
 *  . fixed buffer size (PAGE_SIZE)
 *  . assuming NULL ->llseek()
 *  . stop() may call bpf program, handling potential overflow there
 */
static ssize_t bpf_seq_read(struct file *file, char __user *buf, size_t size,
			    loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	size_t n, offs, copied = 0;
	int err = 0, num_objs = 0;
	bool can_resched;
	void *p;

	mutex_lock(&seq->lock);

	if (!seq->buf) {
		seq->size = PAGE_SIZE << 3;
		seq->buf = kvmalloc(seq->size, GFP_KERNEL);
		if (!seq->buf) {
			err = -ENOMEM;
			goto done;
		}
	}

	if (seq->count) {
		n = min(seq->count, size);
		err = copy_to_user(buf, seq->buf + seq->from, n);
		if (err) {
			err = -EFAULT;
			goto done;
		}
		seq->count -= n;
		seq->from += n;
		copied = n;
		goto done;
	}

	seq->from = 0;
	p = seq->op->start(seq, &seq->index);
	if (!p)
		goto stop;
	if (IS_ERR(p)) {
		err = PTR_ERR(p);
		seq->op->stop(seq, p);
		seq->count = 0;
		goto done;
	}

	err = seq->op->show(seq, p);
	if (err > 0) {
		/* object is skipped, decrease seq_num, so next
		 * valid object can reuse the same seq_num.
		 */
		bpf_iter_dec_seq_num(seq);
		seq->count = 0;
	} else if (err < 0 || seq_has_overflowed(seq)) {
		if (!err)
			err = -E2BIG;
		seq->op->stop(seq, p);
		seq->count = 0;
		goto done;
	}

	can_resched = bpf_iter_support_resched(seq);
	while (1) {
		loff_t pos = seq->index;

		num_objs++;
		offs = seq->count;
		p = seq->op->next(seq, p, &seq->index);
		if (pos == seq->index) {
			pr_info_ratelimited("buggy seq_file .next function %ps "
				"did not updated position index\n",
				seq->op->next);
			seq->index++;
		}

		if (IS_ERR_OR_NULL(p))
			break;

		/* got a valid next object, increase seq_num */
		bpf_iter_inc_seq_num(seq);

		if (seq->count >= size)
			break;

		if (num_objs >= MAX_ITER_OBJECTS) {
			if (offs == 0) {
				err = -EAGAIN;
				seq->op->stop(seq, p);
				goto done;
			}
			break;
		}

		err = seq->op->show(seq, p);
		if (err > 0) {
			bpf_iter_dec_seq_num(seq);
			seq->count = offs;
		} else if (err < 0 || seq_has_overflowed(seq)) {
			seq->count = offs;
			if (offs == 0) {
				if (!err)
					err = -E2BIG;
				seq->op->stop(seq, p);
				goto done;
			}
			break;
		}

		if (can_resched)
			cond_resched();
	}
stop:
	offs = seq->count;
	if (IS_ERR(p)) {
		seq->op->stop(seq, NULL);
		err = PTR_ERR(p);
		goto done;
	}
	/* bpf program called if !p */
	seq->op->stop(seq, p);
	if (!p) {
		if (!seq_has_overflowed(seq)) {
			bpf_iter_done_stop(seq);
		} else {
			seq->count = offs;
			if (offs == 0) {
				err = -E2BIG;
				goto done;
			}
		}
	}

	n = min(seq->count, size);
	err = copy_to_user(buf, seq->buf, n);
	if (err) {
		err = -EFAULT;
		goto done;
	}
	copied = n;
	seq->count -= n;
	seq->from = n;
done:
	if (!copied)
		copied = err;
	else
		*ppos += copied;
	mutex_unlock(&seq->lock);
	return copied;
}

static const struct bpf_iter_seq_info *
__get_seq_info(struct bpf_iter_link *link)
{
	const struct bpf_iter_seq_info *seq_info;

	if (link->aux.map) {
		seq_info = link->aux.map->ops->iter_seq_info;
		if (seq_info)
			return seq_info;
	}

	return link->tinfo->reg_info->seq_info;
}

static int iter_open(struct inode *inode, struct file *file)
{
	struct bpf_iter_link *link = inode->i_private;

	return prepare_seq_file(file, link, __get_seq_info(link));
}

static int iter_release(struct inode *inode, struct file *file)
{
	struct bpf_iter_priv_data *iter_priv;
	struct seq_file *seq;

	seq = file->private_data;
	if (!seq)
		return 0;

	iter_priv = container_of(seq->private, struct bpf_iter_priv_data,
				 target_private);

	if (iter_priv->seq_info->fini_seq_private)
		iter_priv->seq_info->fini_seq_private(seq->private);

	bpf_prog_put(iter_priv->prog);
	seq->private = iter_priv;

	return seq_release_private(inode, file);
}

const struct file_operations bpf_iter_fops = {
	.open		= iter_open,
	.read		= bpf_seq_read,
	.release	= iter_release,
};

/* The argument reg_info will be cached in bpf_iter_target_info.
 * The common practice is to declare target reg_info as
 * a const static variable and passed as an argument to
 * bpf_iter_reg_target().
 */
int bpf_iter_reg_target(const struct bpf_iter_reg *reg_info)
{
	struct bpf_iter_target_info *tinfo;

	tinfo = kzalloc(sizeof(*tinfo), GFP_KERNEL);
	if (!tinfo)
		return -ENOMEM;

	tinfo->reg_info = reg_info;
	INIT_LIST_HEAD(&tinfo->list);

	mutex_lock(&targets_mutex);
	list_add(&tinfo->list, &targets);
	mutex_unlock(&targets_mutex);

	return 0;
}

void bpf_iter_unreg_target(const struct bpf_iter_reg *reg_info)
{
	struct bpf_iter_target_info *tinfo;
	bool found = false;

	mutex_lock(&targets_mutex);
	list_for_each_entry(tinfo, &targets, list) {
		if (reg_info == tinfo->reg_info) {
			list_del(&tinfo->list);
			kfree(tinfo);
			found = true;
			break;
		}
	}
	mutex_unlock(&targets_mutex);

	WARN_ON(found == false);
}

static void cache_btf_id(struct bpf_iter_target_info *tinfo,
			 struct bpf_prog *prog)
{
	tinfo->btf_id = prog->aux->attach_btf_id;
}

bool bpf_iter_prog_supported(struct bpf_prog *prog)
{
	const char *attach_fname = prog->aux->attach_func_name;
	struct bpf_iter_target_info *tinfo = NULL, *iter;
	u32 prog_btf_id = prog->aux->attach_btf_id;
	const char *prefix = BPF_ITER_FUNC_PREFIX;
	int prefix_len = strlen(prefix);

	if (strncmp(attach_fname, prefix, prefix_len))
		return false;

	mutex_lock(&targets_mutex);
	list_for_each_entry(iter, &targets, list) {
		if (iter->btf_id && iter->btf_id == prog_btf_id) {
			tinfo = iter;
			break;
		}
		if (!strcmp(attach_fname + prefix_len, iter->reg_info->target)) {
			cache_btf_id(iter, prog);
			tinfo = iter;
			break;
		}
	}
	mutex_unlock(&targets_mutex);

	if (tinfo) {
		prog->aux->ctx_arg_info_size = tinfo->reg_info->ctx_arg_info_size;
		prog->aux->ctx_arg_info = tinfo->reg_info->ctx_arg_info;
	}

	return tinfo != NULL;
}

const struct bpf_func_proto *
bpf_iter_get_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	const struct bpf_iter_target_info *tinfo;
	const struct bpf_func_proto *fn = NULL;

	mutex_lock(&targets_mutex);
	list_for_each_entry(tinfo, &targets, list) {
		if (tinfo->btf_id == prog->aux->attach_btf_id) {
			const struct bpf_iter_reg *reg_info;

			reg_info = tinfo->reg_info;
			if (reg_info->get_func_proto)
				fn = reg_info->get_func_proto(func_id, prog);
			break;
		}
	}
	mutex_unlock(&targets_mutex);

	return fn;
}

static void bpf_iter_link_release(struct bpf_link *link)
{
	struct bpf_iter_link *iter_link =
		container_of(link, struct bpf_iter_link, link);

	if (iter_link->tinfo->reg_info->detach_target)
		iter_link->tinfo->reg_info->detach_target(&iter_link->aux);
}

static void bpf_iter_link_dealloc(struct bpf_link *link)
{
	struct bpf_iter_link *iter_link =
		container_of(link, struct bpf_iter_link, link);

	kfree(iter_link);
}

static int bpf_iter_link_replace(struct bpf_link *link,
				 struct bpf_prog *new_prog,
				 struct bpf_prog *old_prog)
{
	int ret = 0;

	mutex_lock(&link_mutex);
	if (old_prog && link->prog != old_prog) {
		ret = -EPERM;
		goto out_unlock;
	}

	if (link->prog->type != new_prog->type ||
	    link->prog->expected_attach_type != new_prog->expected_attach_type ||
	    link->prog->aux->attach_btf_id != new_prog->aux->attach_btf_id) {
		ret = -EINVAL;
		goto out_unlock;
	}

	old_prog = xchg(&link->prog, new_prog);
	bpf_prog_put(old_prog);

out_unlock:
	mutex_unlock(&link_mutex);
	return ret;
}

static void bpf_iter_link_show_fdinfo(const struct bpf_link *link,
				      struct seq_file *seq)
{
	struct bpf_iter_link *iter_link =
		container_of(link, struct bpf_iter_link, link);
	bpf_iter_show_fdinfo_t show_fdinfo;

	seq_printf(seq,
		   "target_name:\t%s\n",
		   iter_link->tinfo->reg_info->target);

	show_fdinfo = iter_link->tinfo->reg_info->show_fdinfo;
	if (show_fdinfo)
		show_fdinfo(&iter_link->aux, seq);
}

static int bpf_iter_link_fill_link_info(const struct bpf_link *link,
					struct bpf_link_info *info)
{
	struct bpf_iter_link *iter_link =
		container_of(link, struct bpf_iter_link, link);
	char __user *ubuf = u64_to_user_ptr(info->iter.target_name);
	bpf_iter_fill_link_info_t fill_link_info;
	u32 ulen = info->iter.target_name_len;
	const char *target_name;
	u32 target_len;

	if (!ulen ^ !ubuf)
		return -EINVAL;

	target_name = iter_link->tinfo->reg_info->target;
	target_len =  strlen(target_name);
	info->iter.target_name_len = target_len + 1;

	if (ubuf) {
		if (ulen >= target_len + 1) {
			if (copy_to_user(ubuf, target_name, target_len + 1))
				return -EFAULT;
		} else {
			char zero = '\0';

			if (copy_to_user(ubuf, target_name, ulen - 1))
				return -EFAULT;
			if (put_user(zero, ubuf + ulen - 1))
				return -EFAULT;
			return -ENOSPC;
		}
	}

	fill_link_info = iter_link->tinfo->reg_info->fill_link_info;
	if (fill_link_info)
		return fill_link_info(&iter_link->aux, info);

	return 0;
}

static const struct bpf_link_ops bpf_iter_link_lops = {
	.release = bpf_iter_link_release,
	.dealloc = bpf_iter_link_dealloc,
	.update_prog = bpf_iter_link_replace,
	.show_fdinfo = bpf_iter_link_show_fdinfo,
	.fill_link_info = bpf_iter_link_fill_link_info,
};

bool bpf_link_is_iter(struct bpf_link *link)
{
	return link->ops == &bpf_iter_link_lops;
}

int bpf_iter_link_attach(const union bpf_attr *attr, bpfptr_t uattr,
			 struct bpf_prog *prog)
{
	struct bpf_iter_target_info *tinfo = NULL, *iter;
	struct bpf_link_primer link_primer;
	union bpf_iter_link_info linfo;
	struct bpf_iter_link *link;
	u32 prog_btf_id, linfo_len;
	bpfptr_t ulinfo;
	int err;

	if (attr->link_create.target_fd || attr->link_create.flags)
		return -EINVAL;

	memset(&linfo, 0, sizeof(union bpf_iter_link_info));

	ulinfo = make_bpfptr(attr->link_create.iter_info, uattr.is_kernel);
	linfo_len = attr->link_create.iter_info_len;
	if (bpfptr_is_null(ulinfo) ^ !linfo_len)
		return -EINVAL;

	if (!bpfptr_is_null(ulinfo)) {
		err = bpf_check_uarg_tail_zero(ulinfo, sizeof(linfo),
					       linfo_len);
		if (err)
			return err;
		linfo_len = min_t(u32, linfo_len, sizeof(linfo));
		if (copy_from_bpfptr(&linfo, ulinfo, linfo_len))
			return -EFAULT;
	}

	prog_btf_id = prog->aux->attach_btf_id;
	mutex_lock(&targets_mutex);
	list_for_each_entry(iter, &targets, list) {
		if (iter->btf_id == prog_btf_id) {
			tinfo = iter;
			break;
		}
	}
	mutex_unlock(&targets_mutex);
	if (!tinfo)
		return -ENOENT;

	/* Only allow sleepable program for resched-able iterator */
	if (prog->sleepable && !bpf_iter_target_support_resched(tinfo))
		return -EINVAL;

	link = kzalloc(sizeof(*link), GFP_USER | __GFP_NOWARN);
	if (!link)
		return -ENOMEM;

	bpf_link_init(&link->link, BPF_LINK_TYPE_ITER, &bpf_iter_link_lops, prog);
	link->tinfo = tinfo;

	err = bpf_link_prime(&link->link, &link_primer);
	if (err) {
		kfree(link);
		return err;
	}

	if (tinfo->reg_info->attach_target) {
		err = tinfo->reg_info->attach_target(prog, &linfo, &link->aux);
		if (err) {
			bpf_link_cleanup(&link_primer);
			return err;
		}
	}

	return bpf_link_settle(&link_primer);
}

static void init_seq_meta(struct bpf_iter_priv_data *priv_data,
			  struct bpf_iter_target_info *tinfo,
			  const struct bpf_iter_seq_info *seq_info,
			  struct bpf_prog *prog)
{
	priv_data->tinfo = tinfo;
	priv_data->seq_info = seq_info;
	priv_data->prog = prog;
	priv_data->session_id = atomic64_inc_return(&session_id);
	priv_data->seq_num = 0;
	priv_data->done_stop = false;
}

static int prepare_seq_file(struct file *file, struct bpf_iter_link *link,
			    const struct bpf_iter_seq_info *seq_info)
{
	struct bpf_iter_priv_data *priv_data;
	struct bpf_iter_target_info *tinfo;
	struct bpf_prog *prog;
	u32 total_priv_dsize;
	struct seq_file *seq;
	int err = 0;

	mutex_lock(&link_mutex);
	prog = link->link.prog;
	bpf_prog_inc(prog);
	mutex_unlock(&link_mutex);

	tinfo = link->tinfo;
	total_priv_dsize = offsetof(struct bpf_iter_priv_data, target_private) +
			   seq_info->seq_priv_size;
	priv_data = __seq_open_private(file, seq_info->seq_ops,
				       total_priv_dsize);
	if (!priv_data) {
		err = -ENOMEM;
		goto release_prog;
	}

	if (seq_info->init_seq_private) {
		err = seq_info->init_seq_private(priv_data->target_private, &link->aux);
		if (err)
			goto release_seq_file;
	}

	init_seq_meta(priv_data, tinfo, seq_info, prog);
	seq = file->private_data;
	seq->private = priv_data->target_private;

	return 0;

release_seq_file:
	seq_release_private(file->f_inode, file);
	file->private_data = NULL;
release_prog:
	bpf_prog_put(prog);
	return err;
}

int bpf_iter_new_fd(struct bpf_link *link)
{
	struct bpf_iter_link *iter_link;
	struct file *file;
	unsigned int flags;
	int err, fd;

	if (link->ops != &bpf_iter_link_lops)
		return -EINVAL;

	flags = O_RDONLY | O_CLOEXEC;
	fd = get_unused_fd_flags(flags);
	if (fd < 0)
		return fd;

	file = anon_inode_getfile("bpf_iter", &bpf_iter_fops, NULL, flags);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		goto free_fd;
	}

	iter_link = container_of(link, struct bpf_iter_link, link);
	err = prepare_seq_file(file, iter_link, __get_seq_info(iter_link));
	if (err)
		goto free_file;

	fd_install(fd, file);
	return fd;

free_file:
	fput(file);
free_fd:
	put_unused_fd(fd);
	return err;
}

struct bpf_prog *bpf_iter_get_info(struct bpf_iter_meta *meta, bool in_stop)
{
	struct bpf_iter_priv_data *iter_priv;
	struct seq_file *seq;
	void *seq_priv;

	seq = meta->seq;
	if (seq->file->f_op != &bpf_iter_fops)
		return NULL;

	seq_priv = seq->private;
	iter_priv = container_of(seq_priv, struct bpf_iter_priv_data,
				 target_private);

	if (in_stop && iter_priv->done_stop)
		return NULL;

	meta->session_id = iter_priv->session_id;
	meta->seq_num = iter_priv->seq_num;

	return iter_priv->prog;
}

int bpf_iter_run_prog(struct bpf_prog *prog, void *ctx)
{
	struct bpf_run_ctx run_ctx, *old_run_ctx;
	int ret;

	if (prog->sleepable) {
		rcu_read_lock_trace();
		migrate_disable();
		might_fault();
		old_run_ctx = bpf_set_run_ctx(&run_ctx);
		ret = bpf_prog_run(prog, ctx);
		bpf_reset_run_ctx(old_run_ctx);
		migrate_enable();
		rcu_read_unlock_trace();
	} else {
		rcu_read_lock();
		migrate_disable();
		old_run_ctx = bpf_set_run_ctx(&run_ctx);
		ret = bpf_prog_run(prog, ctx);
		bpf_reset_run_ctx(old_run_ctx);
		migrate_enable();
		rcu_read_unlock();
	}

	/* bpf program can only return 0 or 1:
	 *  0 : okay
	 *  1 : retry the same object
	 * The bpf_iter_run_prog() return value
	 * will be seq_ops->show() return value.
	 */
	return ret == 0 ? 0 : -EAGAIN;
}

BPF_CALL_4(bpf_for_each_map_elem, struct bpf_map *, map, void *, callback_fn,
	   void *, callback_ctx, u64, flags)
{
	return map->ops->map_for_each_callback(map, callback_fn, callback_ctx, flags);
}

const struct bpf_func_proto bpf_for_each_map_elem_proto = {
	.func		= bpf_for_each_map_elem,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_FUNC,
	.arg3_type	= ARG_PTR_TO_STACK_OR_NULL,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_loop, u32, nr_loops, void *, callback_fn, void *, callback_ctx,
	   u64, flags)
{
	bpf_callback_t callback = (bpf_callback_t)callback_fn;
	u64 ret;
	u32 i;

	/* Note: these safety checks are also verified when bpf_loop
	 * is inlined, be careful to modify this code in sync. See
	 * function verifier.c:inline_bpf_loop.
	 */
	if (flags)
		return -EINVAL;
	if (nr_loops > BPF_MAX_LOOPS)
		return -E2BIG;

	for (i = 0; i < nr_loops; i++) {
		ret = callback((u64)i, (u64)(long)callback_ctx, 0, 0, 0);
		/* return value: 0 - continue, 1 - stop and return */
		if (ret)
			return i + 1;
	}

	return i;
}

const struct bpf_func_proto bpf_loop_proto = {
	.func		= bpf_loop,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_ANYTHING,
	.arg2_type	= ARG_PTR_TO_FUNC,
	.arg3_type	= ARG_PTR_TO_STACK_OR_NULL,
	.arg4_type	= ARG_ANYTHING,
};

struct bpf_iter_num_kern {
	int cur; /* current value, inclusive */
	int end; /* final value, exclusive */
} __aligned(8);

__bpf_kfunc_start_defs();

__bpf_kfunc int bpf_iter_num_new(struct bpf_iter_num *it, int start, int end)
{
	struct bpf_iter_num_kern *s = (void *)it;

	BUILD_BUG_ON(sizeof(struct bpf_iter_num_kern) != sizeof(struct bpf_iter_num));
	BUILD_BUG_ON(__alignof__(struct bpf_iter_num_kern) != __alignof__(struct bpf_iter_num));

	/* start == end is legit, it's an empty range and we'll just get NULL
	 * on first (and any subsequent) bpf_iter_num_next() call
	 */
	if (start > end) {
		s->cur = s->end = 0;
		return -EINVAL;
	}

	/* avoid overflows, e.g., if start == INT_MIN and end == INT_MAX */
	if ((s64)end - (s64)start > BPF_MAX_LOOPS) {
		s->cur = s->end = 0;
		return -E2BIG;
	}

	/* user will call bpf_iter_num_next() first,
	 * which will set s->cur to exactly start value;
	 * underflow shouldn't matter
	 */
	s->cur = start - 1;
	s->end = end;

	return 0;
}

__bpf_kfunc int *bpf_iter_num_next(struct bpf_iter_num* it)
{
	struct bpf_iter_num_kern *s = (void *)it;

	/* check failed initialization or if we are done (same behavior);
	 * need to be careful about overflow, so convert to s64 for checks,
	 * e.g., if s->cur == s->end == INT_MAX, we can't just do
	 * s->cur + 1 >= s->end
	 */
	if ((s64)(s->cur + 1) >= s->end) {
		s->cur = s->end = 0;
		return NULL;
	}

	s->cur++;

	return &s->cur;
}

__bpf_kfunc void bpf_iter_num_destroy(struct bpf_iter_num *it)
{
	struct bpf_iter_num_kern *s = (void *)it;

	s->cur = s->end = 0;
}

__bpf_kfunc_end_defs();
