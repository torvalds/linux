/*
 * kvm trace
 *
 * It is designed to allow debugging traces of kvm to be generated
 * on UP / SMP machines.  Each trace entry can be timestamped so that
 * it's possible to reconstruct a chronological record of trace events.
 * The implementation refers to blktrace kernel support.
 *
 * Copyright (c) 2008 Intel Corporation
 * Copyright (C) 2006 Jens Axboe <axboe@kernel.dk>
 *
 * Authors: Feng(Eric) Liu, eric.e.liu@intel.com
 *
 * Date:    Feb 2008
 */

#include <linux/module.h>
#include <linux/relay.h>
#include <linux/debugfs.h>

#include <linux/kvm_host.h>

#define KVM_TRACE_STATE_RUNNING 	(1 << 0)
#define KVM_TRACE_STATE_PAUSE 		(1 << 1)
#define KVM_TRACE_STATE_CLEARUP 	(1 << 2)

struct kvm_trace {
	int trace_state;
	struct rchan *rchan;
	struct dentry *lost_file;
	atomic_t lost_records;
};
static struct kvm_trace *kvm_trace;

struct kvm_trace_probe {
	const char *name;
	const char *format;
	u32 cycle_in;
	marker_probe_func *probe_func;
};

static inline int calc_rec_size(int cycle, int extra)
{
	int rec_size = KVM_TRC_HEAD_SIZE;

	rec_size += extra;
	return cycle ? rec_size += KVM_TRC_CYCLE_SIZE : rec_size;
}

static void kvm_add_trace(void *probe_private, void *call_data,
			  const char *format, va_list *args)
{
	struct kvm_trace_probe *p = probe_private;
	struct kvm_trace *kt = kvm_trace;
	struct kvm_trace_rec rec;
	struct kvm_vcpu *vcpu;
	int    i, extra, size;

	if (unlikely(kt->trace_state != KVM_TRACE_STATE_RUNNING))
		return;

	rec.event	= va_arg(*args, u32);
	vcpu		= va_arg(*args, struct kvm_vcpu *);
	rec.pid		= current->tgid;
	rec.vcpu_id	= vcpu->vcpu_id;

	extra   	= va_arg(*args, u32);
	WARN_ON(!(extra <= KVM_TRC_EXTRA_MAX));
	extra 		= min_t(u32, extra, KVM_TRC_EXTRA_MAX);
	rec.extra_u32   = extra;

	rec.cycle_in 	= p->cycle_in;

	if (rec.cycle_in) {
		rec.u.cycle.cycle_u64 = get_cycles();

		for (i = 0; i < rec.extra_u32; i++)
			rec.u.cycle.extra_u32[i] = va_arg(*args, u32);
	} else {
		for (i = 0; i < rec.extra_u32; i++)
			rec.u.nocycle.extra_u32[i] = va_arg(*args, u32);
	}

	size = calc_rec_size(rec.cycle_in, rec.extra_u32 * sizeof(u32));
	relay_write(kt->rchan, &rec, size);
}

static struct kvm_trace_probe kvm_trace_probes[] = {
	{ "kvm_trace_entryexit", "%u %p %u %u %u %u %u %u", 1, kvm_add_trace },
	{ "kvm_trace_handler", "%u %p %u %u %u %u %u %u", 0, kvm_add_trace },
};

static int lost_records_get(void *data, u64 *val)
{
	struct kvm_trace *kt = data;

	*val = atomic_read(&kt->lost_records);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(kvm_trace_lost_ops, lost_records_get, NULL, "%llu\n");

/*
 *  The relay channel is used in "no-overwrite" mode, it keeps trace of how
 *  many times we encountered a full subbuffer, to tell user space app the
 *  lost records there were.
 */
static int kvm_subbuf_start_callback(struct rchan_buf *buf, void *subbuf,
				     void *prev_subbuf, size_t prev_padding)
{
	struct kvm_trace *kt;

	if (!relay_buf_full(buf)) {
		if (!prev_subbuf) {
			/*
			 * executed only once when the channel is opened
			 * save metadata as first record
			 */
			subbuf_start_reserve(buf, sizeof(u32));
			*(u32 *)subbuf = 0x12345678;
		}

		return 1;
	}

	kt = buf->chan->private_data;
	atomic_inc(&kt->lost_records);

	return 0;
}

static struct dentry *kvm_create_buf_file_callack(const char *filename,
						 struct dentry *parent,
						 int mode,
						 struct rchan_buf *buf,
						 int *is_global)
{
	return debugfs_create_file(filename, mode, parent, buf,
				   &relay_file_operations);
}

static int kvm_remove_buf_file_callback(struct dentry *dentry)
{
	debugfs_remove(dentry);
	return 0;
}

static struct rchan_callbacks kvm_relay_callbacks = {
	.subbuf_start 		= kvm_subbuf_start_callback,
	.create_buf_file 	= kvm_create_buf_file_callack,
	.remove_buf_file 	= kvm_remove_buf_file_callback,
};

static int do_kvm_trace_enable(struct kvm_user_trace_setup *kuts)
{
	struct kvm_trace *kt;
	int i, r = -ENOMEM;

	if (!kuts->buf_size || !kuts->buf_nr)
		return -EINVAL;

	kt = kzalloc(sizeof(*kt), GFP_KERNEL);
	if (!kt)
		goto err;

	r = -EIO;
	atomic_set(&kt->lost_records, 0);
	kt->lost_file = debugfs_create_file("lost_records", 0444, kvm_debugfs_dir,
					    kt, &kvm_trace_lost_ops);
	if (!kt->lost_file)
		goto err;

	kt->rchan = relay_open("trace", kvm_debugfs_dir, kuts->buf_size,
				kuts->buf_nr, &kvm_relay_callbacks, kt);
	if (!kt->rchan)
		goto err;

	kvm_trace = kt;

	for (i = 0; i < ARRAY_SIZE(kvm_trace_probes); i++) {
		struct kvm_trace_probe *p = &kvm_trace_probes[i];

		r = marker_probe_register(p->name, p->format, p->probe_func, p);
		if (r)
			printk(KERN_INFO "Unable to register probe %s\n",
			       p->name);
	}

	kvm_trace->trace_state = KVM_TRACE_STATE_RUNNING;

	return 0;
err:
	if (kt) {
		if (kt->lost_file)
			debugfs_remove(kt->lost_file);
		if (kt->rchan)
			relay_close(kt->rchan);
		kfree(kt);
	}
	return r;
}

static int kvm_trace_enable(char __user *arg)
{
	struct kvm_user_trace_setup kuts;
	int ret;

	ret = copy_from_user(&kuts, arg, sizeof(kuts));
	if (ret)
		return -EFAULT;

	ret = do_kvm_trace_enable(&kuts);
	if (ret)
		return ret;

	return 0;
}

static int kvm_trace_pause(void)
{
	struct kvm_trace *kt = kvm_trace;
	int r = -EINVAL;

	if (kt == NULL)
		return r;

	if (kt->trace_state == KVM_TRACE_STATE_RUNNING) {
		kt->trace_state = KVM_TRACE_STATE_PAUSE;
		relay_flush(kt->rchan);
		r = 0;
	}

	return r;
}

void kvm_trace_cleanup(void)
{
	struct kvm_trace *kt = kvm_trace;
	int i;

	if (kt == NULL)
		return;

	if (kt->trace_state == KVM_TRACE_STATE_RUNNING ||
	    kt->trace_state == KVM_TRACE_STATE_PAUSE) {

		kt->trace_state = KVM_TRACE_STATE_CLEARUP;

		for (i = 0; i < ARRAY_SIZE(kvm_trace_probes); i++) {
			struct kvm_trace_probe *p = &kvm_trace_probes[i];
			marker_probe_unregister(p->name, p->probe_func, p);
		}

		relay_close(kt->rchan);
		debugfs_remove(kt->lost_file);
		kfree(kt);
	}
}

int kvm_trace_ioctl(unsigned int ioctl, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	long r = -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (ioctl) {
	case KVM_TRACE_ENABLE:
		r = kvm_trace_enable(argp);
		break;
	case KVM_TRACE_PAUSE:
		r = kvm_trace_pause();
		break;
	case KVM_TRACE_DISABLE:
		r = 0;
		kvm_trace_cleanup();
		break;
	}

	return r;
}
