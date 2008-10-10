/*
 * Copyright (C) 2008 Pekka Enberg, Eduard - Gabriel Munteanu
 *
 * This file is released under GPL version 2.
 */

#include <linux/string.h>
#include <linux/debugfs.h>
#include <linux/relay.h>
#include <linux/module.h>
#include <linux/marker.h>
#include <linux/gfp.h>
#include <linux/kmemtrace.h>

#define KMEMTRACE_SUBBUF_SIZE		524288
#define KMEMTRACE_DEF_N_SUBBUFS		20

static struct rchan *kmemtrace_chan;
static u32 kmemtrace_buf_overruns;

static unsigned int kmemtrace_n_subbufs;

/* disabled by default */
static unsigned int kmemtrace_enabled;

/*
 * The sequence number is used for reordering kmemtrace packets
 * in userspace, since they are logged as per-CPU data.
 *
 * atomic_t should always be a 32-bit signed integer. Wraparound is not
 * likely to occur, but userspace can deal with it by expecting a certain
 * sequence number in the next packet that will be read.
 */
static atomic_t kmemtrace_seq_num;

#define KMEMTRACE_ABI_VERSION		1

static u32 kmemtrace_abi_version __read_mostly = KMEMTRACE_ABI_VERSION;

enum kmemtrace_event_id {
	KMEMTRACE_EVENT_ALLOC = 0,
	KMEMTRACE_EVENT_FREE,
};

struct kmemtrace_event {
	u8		event_id;
	u8		type_id;
	u16		event_size;
	s32		seq_num;
	u64		call_site;
	u64		ptr;
} __attribute__ ((__packed__));

struct kmemtrace_stats_alloc {
	u64		bytes_req;
	u64		bytes_alloc;
	u32		gfp_flags;
	s32		numa_node;
} __attribute__ ((__packed__));

static void kmemtrace_probe_alloc(void *probe_data, void *call_data,
				  const char *format, va_list *args)
{
	unsigned long flags;
	struct kmemtrace_event *ev;
	struct kmemtrace_stats_alloc *stats;
	void *buf;

	local_irq_save(flags);

	buf = relay_reserve(kmemtrace_chan,
			    sizeof(struct kmemtrace_event) +
			    sizeof(struct kmemtrace_stats_alloc));
	if (!buf)
		goto failed;

	/*
	 * Don't convert this to use structure initializers,
	 * C99 does not guarantee the rvalues evaluation order.
	 */

	ev = buf;
	ev->event_id = KMEMTRACE_EVENT_ALLOC;
	ev->type_id = va_arg(*args, int);
	ev->event_size = sizeof(struct kmemtrace_event) +
			 sizeof(struct kmemtrace_stats_alloc);
	ev->seq_num = atomic_add_return(1, &kmemtrace_seq_num);
	ev->call_site = va_arg(*args, unsigned long);
	ev->ptr = va_arg(*args, unsigned long);

	stats = buf + sizeof(struct kmemtrace_event);
	stats->bytes_req = va_arg(*args, unsigned long);
	stats->bytes_alloc = va_arg(*args, unsigned long);
	stats->gfp_flags = va_arg(*args, unsigned long);
	stats->numa_node = va_arg(*args, int);

failed:
	local_irq_restore(flags);
}

static void kmemtrace_probe_free(void *probe_data, void *call_data,
				 const char *format, va_list *args)
{
	unsigned long flags;
	struct kmemtrace_event *ev;

	local_irq_save(flags);

	ev = relay_reserve(kmemtrace_chan, sizeof(struct kmemtrace_event));
	if (!ev)
		goto failed;

	/*
	 * Don't convert this to use structure initializers,
	 * C99 does not guarantee the rvalues evaluation order.
	 */
	ev->event_id = KMEMTRACE_EVENT_FREE;
	ev->type_id = va_arg(*args, int);
	ev->event_size = sizeof(struct kmemtrace_event);
	ev->seq_num = atomic_add_return(1, &kmemtrace_seq_num);
	ev->call_site = va_arg(*args, unsigned long);
	ev->ptr = va_arg(*args, unsigned long);

failed:
	local_irq_restore(flags);
}

static struct dentry *
kmemtrace_create_buf_file(const char *filename, struct dentry *parent,
			  int mode, struct rchan_buf *buf, int *is_global)
{
	return debugfs_create_file(filename, mode, parent, buf,
				   &relay_file_operations);
}

static int kmemtrace_remove_buf_file(struct dentry *dentry)
{
	debugfs_remove(dentry);

	return 0;
}

static int kmemtrace_subbuf_start(struct rchan_buf *buf,
				  void *subbuf,
				  void *prev_subbuf,
				  size_t prev_padding)
{
	if (relay_buf_full(buf)) {
		/*
		 * We know it's not SMP-safe, but neither
		 * debugfs_create_u32() is.
		 */
		kmemtrace_buf_overruns++;
		return 0;
	}

	return 1;
}

static struct rchan_callbacks relay_callbacks = {
	.create_buf_file = kmemtrace_create_buf_file,
	.remove_buf_file = kmemtrace_remove_buf_file,
	.subbuf_start = kmemtrace_subbuf_start,
};

static struct dentry *kmemtrace_dir;
static struct dentry *kmemtrace_overruns_dentry;
static struct dentry *kmemtrace_abi_version_dentry;

static struct dentry *kmemtrace_enabled_dentry;

static int kmemtrace_start_probes(void)
{
	int err;

	err = marker_probe_register("kmemtrace_alloc", "type_id %d "
				    "call_site %lu ptr %lu "
				    "bytes_req %lu bytes_alloc %lu "
				    "gfp_flags %lu node %d",
				    kmemtrace_probe_alloc, NULL);
	if (err)
		return err;
	err = marker_probe_register("kmemtrace_free", "type_id %d "
				    "call_site %lu ptr %lu",
				    kmemtrace_probe_free, NULL);

	return err;
}

static void kmemtrace_stop_probes(void)
{
	marker_probe_unregister("kmemtrace_alloc",
				kmemtrace_probe_alloc, NULL);
	marker_probe_unregister("kmemtrace_free",
				kmemtrace_probe_free, NULL);
}

static int kmemtrace_enabled_get(void *data, u64 *val)
{
	*val = *((int *) data);

	return 0;
}

static int kmemtrace_enabled_set(void *data, u64 val)
{
	u64 old_val = kmemtrace_enabled;

	*((int *) data) = !!val;

	if (old_val == val)
		return 0;
	if (val)
		kmemtrace_start_probes();
	else
		kmemtrace_stop_probes();

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(kmemtrace_enabled_fops,
			kmemtrace_enabled_get,
			kmemtrace_enabled_set, "%llu\n");

static void kmemtrace_cleanup(void)
{
	if (kmemtrace_enabled_dentry)
		debugfs_remove(kmemtrace_enabled_dentry);

	kmemtrace_stop_probes();

	if (kmemtrace_abi_version_dentry)
		debugfs_remove(kmemtrace_abi_version_dentry);
	if (kmemtrace_overruns_dentry)
		debugfs_remove(kmemtrace_overruns_dentry);

	relay_close(kmemtrace_chan);
	kmemtrace_chan = NULL;

	if (kmemtrace_dir)
		debugfs_remove(kmemtrace_dir);
}

static int __init kmemtrace_setup_late(void)
{
	if (!kmemtrace_chan)
		goto failed;

	kmemtrace_dir = debugfs_create_dir("kmemtrace", NULL);
	if (!kmemtrace_dir)
		goto cleanup;

	kmemtrace_abi_version_dentry =
		debugfs_create_u32("abi_version", S_IRUSR,
				   kmemtrace_dir, &kmemtrace_abi_version);
	kmemtrace_overruns_dentry =
		debugfs_create_u32("total_overruns", S_IRUSR,
				   kmemtrace_dir, &kmemtrace_buf_overruns);
	if (!kmemtrace_overruns_dentry || !kmemtrace_abi_version_dentry)
		goto cleanup;

	kmemtrace_enabled_dentry =
		debugfs_create_file("enabled", S_IRUSR | S_IWUSR,
				    kmemtrace_dir, &kmemtrace_enabled,
				    &kmemtrace_enabled_fops);
	if (!kmemtrace_enabled_dentry)
		goto cleanup;

	if (relay_late_setup_files(kmemtrace_chan, "cpu", kmemtrace_dir))
		goto cleanup;

	printk(KERN_INFO "kmemtrace: fully up.\n");

	return 0;

cleanup:
	kmemtrace_cleanup();
failed:
	return 1;
}
late_initcall(kmemtrace_setup_late);

static int __init kmemtrace_set_boot_enabled(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "yes"))
		kmemtrace_enabled = 1;
	else if (!strcmp(str, "no"))
		kmemtrace_enabled = 0;
	else
		return -EINVAL;

	return 0;
}
early_param("kmemtrace.enable", kmemtrace_set_boot_enabled);

static int __init kmemtrace_set_subbufs(char *str)
{
	get_option(&str, &kmemtrace_n_subbufs);
	return 0;
}
early_param("kmemtrace.subbufs", kmemtrace_set_subbufs);

void kmemtrace_init(void)
{
	if (!kmemtrace_n_subbufs)
		kmemtrace_n_subbufs = KMEMTRACE_DEF_N_SUBBUFS;

	kmemtrace_chan = relay_open(NULL, NULL, KMEMTRACE_SUBBUF_SIZE,
				    kmemtrace_n_subbufs, &relay_callbacks,
				    NULL);
	if (!kmemtrace_chan) {
		printk(KERN_ERR "kmemtrace: could not open relay channel.\n");
		return;
	}

	if (!kmemtrace_enabled) {
		printk(KERN_INFO "kmemtrace: disabled. Pass "
			"kemtrace.enable=yes as kernel parameter for "
			"boot-time tracing.");
		return;
	}
	if (kmemtrace_start_probes()) {
		printk(KERN_ERR "kmemtrace: could not register marker probes!\n");
		kmemtrace_cleanup();
		return;
	}

	printk(KERN_INFO "kmemtrace: enabled.\n");
}

