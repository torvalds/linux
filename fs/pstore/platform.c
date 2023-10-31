// SPDX-License-Identifier: GPL-2.0-only
/*
 * Persistent Storage - platform driver interface parts.
 *
 * Copyright (C) 2007-2008 Google, Inc.
 * Copyright (C) 2010 Intel Corporation <tony.luck@intel.com>
 */

#define pr_fmt(fmt) "pstore: " fmt

#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kmsg_dump.h>
#include <linux/console.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pstore.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/zlib.h>

#include "internal.h"

/*
 * We defer making "oops" entries appear in pstore - see
 * whether the system is actually still running well enough
 * to let someone see the entry
 */
static int pstore_update_ms = -1;
module_param_named(update_ms, pstore_update_ms, int, 0600);
MODULE_PARM_DESC(update_ms, "milliseconds before pstore updates its content "
		 "(default is -1, which means runtime updates are disabled; "
		 "enabling this option may not be safe; it may lead to further "
		 "corruption on Oopses)");

/* Names should be in the same order as the enum pstore_type_id */
static const char * const pstore_type_names[] = {
	"dmesg",
	"mce",
	"console",
	"ftrace",
	"rtas",
	"powerpc-ofw",
	"powerpc-common",
	"pmsg",
	"powerpc-opal",
};

static int pstore_new_entry;

static void pstore_timefunc(struct timer_list *);
static DEFINE_TIMER(pstore_timer, pstore_timefunc);

static void pstore_dowork(struct work_struct *);
static DECLARE_WORK(pstore_work, pstore_dowork);

/*
 * psinfo_lock protects "psinfo" during calls to
 * pstore_register(), pstore_unregister(), and
 * the filesystem mount/unmount routines.
 */
static DEFINE_MUTEX(psinfo_lock);
struct pstore_info *psinfo;

static char *backend;
module_param(backend, charp, 0444);
MODULE_PARM_DESC(backend, "specific backend to use");

/*
 * pstore no longer implements compression via the crypto API, and only
 * supports zlib deflate compression implemented using the zlib library
 * interface. This removes additional complexity which is hard to justify for a
 * diagnostic facility that has to operate in conditions where the system may
 * have become unstable. Zlib deflate is comparatively small in terms of code
 * size, and compresses ASCII text comparatively well. In terms of compression
 * speed, deflate is not the best performer but for recording the log output on
 * a kernel panic, this is not considered critical.
 *
 * The only remaining arguments supported by the compress= module parameter are
 * 'deflate' and 'none'. To retain compatibility with existing installations,
 * all other values are logged and replaced with 'deflate'.
 */
static char *compress = "deflate";
module_param(compress, charp, 0444);
MODULE_PARM_DESC(compress, "compression to use");

/* How much of the kernel log to snapshot */
unsigned long kmsg_bytes = CONFIG_PSTORE_DEFAULT_KMSG_BYTES;
module_param(kmsg_bytes, ulong, 0444);
MODULE_PARM_DESC(kmsg_bytes, "amount of kernel log to snapshot (in bytes)");

static void *compress_workspace;

/*
 * Compression is only used for dmesg output, which consists of low-entropy
 * ASCII text, and so we can assume worst-case 60%.
 */
#define DMESG_COMP_PERCENT	60

static char *big_oops_buf;
static size_t max_compressed_size;

void pstore_set_kmsg_bytes(int bytes)
{
	kmsg_bytes = bytes;
}

/* Tag each group of saved records with a sequence number */
static int	oopscount;

const char *pstore_type_to_name(enum pstore_type_id type)
{
	BUILD_BUG_ON(ARRAY_SIZE(pstore_type_names) != PSTORE_TYPE_MAX);

	if (WARN_ON_ONCE(type >= PSTORE_TYPE_MAX))
		return "unknown";

	return pstore_type_names[type];
}
EXPORT_SYMBOL_GPL(pstore_type_to_name);

enum pstore_type_id pstore_name_to_type(const char *name)
{
	int i;

	for (i = 0; i < PSTORE_TYPE_MAX; i++) {
		if (!strcmp(pstore_type_names[i], name))
			return i;
	}

	return PSTORE_TYPE_MAX;
}
EXPORT_SYMBOL_GPL(pstore_name_to_type);

static void pstore_timer_kick(void)
{
	if (pstore_update_ms < 0)
		return;

	mod_timer(&pstore_timer, jiffies + msecs_to_jiffies(pstore_update_ms));
}

static bool pstore_cannot_block_path(enum kmsg_dump_reason reason)
{
	/*
	 * In case of NMI path, pstore shouldn't be blocked
	 * regardless of reason.
	 */
	if (in_nmi())
		return true;

	switch (reason) {
	/* In panic case, other cpus are stopped by smp_send_stop(). */
	case KMSG_DUMP_PANIC:
	/*
	 * Emergency restart shouldn't be blocked by spinning on
	 * pstore_info::buf_lock.
	 */
	case KMSG_DUMP_EMERG:
		return true;
	default:
		return false;
	}
}

static int pstore_compress(const void *in, void *out,
			   unsigned int inlen, unsigned int outlen)
{
	struct z_stream_s zstream = {
		.next_in	= in,
		.avail_in	= inlen,
		.next_out	= out,
		.avail_out	= outlen,
		.workspace	= compress_workspace,
	};
	int ret;

	if (!IS_ENABLED(CONFIG_PSTORE_COMPRESS))
		return -EINVAL;

	ret = zlib_deflateInit2(&zstream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
				-MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY);
	if (ret != Z_OK)
		return -EINVAL;

	ret = zlib_deflate(&zstream, Z_FINISH);
	if (ret != Z_STREAM_END)
		return -EINVAL;

	ret = zlib_deflateEnd(&zstream);
	if (ret != Z_OK)
		pr_warn_once("zlib_deflateEnd() failed: %d\n", ret);

	return zstream.total_out;
}

static void allocate_buf_for_compression(void)
{
	size_t compressed_size;
	char *buf;

	/* Skip if not built-in or compression disabled. */
	if (!IS_ENABLED(CONFIG_PSTORE_COMPRESS) || !compress ||
	    !strcmp(compress, "none")) {
		compress = NULL;
		return;
	}

	if (strcmp(compress, "deflate")) {
		pr_err("Unsupported compression '%s', falling back to deflate\n",
		       compress);
		compress = "deflate";
	}

	/*
	 * The compression buffer only needs to be as large as the maximum
	 * uncompressed record size, since any record that would be expanded by
	 * compression is just stored uncompressed.
	 */
	compressed_size = (psinfo->bufsize * 100) / DMESG_COMP_PERCENT;
	buf = kvzalloc(compressed_size, GFP_KERNEL);
	if (!buf) {
		pr_err("Failed %zu byte compression buffer allocation for: %s\n",
		       psinfo->bufsize, compress);
		return;
	}

	compress_workspace =
		vmalloc(zlib_deflate_workspacesize(MAX_WBITS, DEF_MEM_LEVEL));
	if (!compress_workspace) {
		pr_err("Failed to allocate zlib deflate workspace\n");
		kvfree(buf);
		return;
	}

	/* A non-NULL big_oops_buf indicates compression is available. */
	big_oops_buf = buf;
	max_compressed_size = compressed_size;

	pr_info("Using crash dump compression: %s\n", compress);
}

static void free_buf_for_compression(void)
{
	if (IS_ENABLED(CONFIG_PSTORE_COMPRESS) && compress_workspace) {
		vfree(compress_workspace);
		compress_workspace = NULL;
	}

	kvfree(big_oops_buf);
	big_oops_buf = NULL;
	max_compressed_size = 0;
}

void pstore_record_init(struct pstore_record *record,
			struct pstore_info *psinfo)
{
	memset(record, 0, sizeof(*record));

	record->psi = psinfo;

	/* Report zeroed timestamp if called before timekeeping has resumed. */
	record->time = ns_to_timespec64(ktime_get_real_fast_ns());
}

/*
 * callback from kmsg_dump. Save as much as we can (up to kmsg_bytes) from the
 * end of the buffer.
 */
static void pstore_dump(struct kmsg_dumper *dumper,
			enum kmsg_dump_reason reason)
{
	struct kmsg_dump_iter iter;
	unsigned long	total = 0;
	const char	*why;
	unsigned int	part = 1;
	unsigned long	flags = 0;
	int		saved_ret = 0;
	int		ret;

	why = kmsg_dump_reason_str(reason);

	if (pstore_cannot_block_path(reason)) {
		if (!spin_trylock_irqsave(&psinfo->buf_lock, flags)) {
			pr_err("dump skipped in %s path because of concurrent dump\n",
					in_nmi() ? "NMI" : why);
			return;
		}
	} else {
		spin_lock_irqsave(&psinfo->buf_lock, flags);
	}

	kmsg_dump_rewind(&iter);

	oopscount++;
	while (total < kmsg_bytes) {
		char *dst;
		size_t dst_size;
		int header_size;
		int zipped_len = -1;
		size_t dump_size;
		struct pstore_record record;

		pstore_record_init(&record, psinfo);
		record.type = PSTORE_TYPE_DMESG;
		record.count = oopscount;
		record.reason = reason;
		record.part = part;
		record.buf = psinfo->buf;

		dst = big_oops_buf ?: psinfo->buf;
		dst_size = max_compressed_size ?: psinfo->bufsize;

		/* Write dump header. */
		header_size = snprintf(dst, dst_size, "%s#%d Part%u\n", why,
				 oopscount, part);
		dst_size -= header_size;

		/* Write dump contents. */
		if (!kmsg_dump_get_buffer(&iter, true, dst + header_size,
					  dst_size, &dump_size))
			break;

		if (big_oops_buf) {
			zipped_len = pstore_compress(dst, psinfo->buf,
						header_size + dump_size,
						psinfo->bufsize);

			if (zipped_len > 0) {
				record.compressed = true;
				record.size = zipped_len;
			} else {
				/*
				 * Compression failed, so the buffer is most
				 * likely filled with binary data that does not
				 * compress as well as ASCII text. Copy as much
				 * of the uncompressed data as possible into
				 * the pstore record, and discard the rest.
				 */
				record.size = psinfo->bufsize;
				memcpy(psinfo->buf, dst, psinfo->bufsize);
			}
		} else {
			record.size = header_size + dump_size;
		}

		ret = psinfo->write(&record);
		if (ret == 0 && reason == KMSG_DUMP_OOPS) {
			pstore_new_entry = 1;
			pstore_timer_kick();
		} else {
			/* Preserve only the first non-zero returned value. */
			if (!saved_ret)
				saved_ret = ret;
		}

		total += record.size;
		part++;
	}
	spin_unlock_irqrestore(&psinfo->buf_lock, flags);

	if (saved_ret) {
		pr_err_once("backend (%s) writing error (%d)\n", psinfo->name,
			    saved_ret);
	}
}

static struct kmsg_dumper pstore_dumper = {
	.dump = pstore_dump,
};

/*
 * Register with kmsg_dump to save last part of console log on panic.
 */
static void pstore_register_kmsg(void)
{
	kmsg_dump_register(&pstore_dumper);
}

static void pstore_unregister_kmsg(void)
{
	kmsg_dump_unregister(&pstore_dumper);
}

#ifdef CONFIG_PSTORE_CONSOLE
static void pstore_console_write(struct console *con, const char *s, unsigned c)
{
	struct pstore_record record;

	if (!c)
		return;

	pstore_record_init(&record, psinfo);
	record.type = PSTORE_TYPE_CONSOLE;

	record.buf = (char *)s;
	record.size = c;
	psinfo->write(&record);
}

static struct console pstore_console = {
	.write	= pstore_console_write,
	.index	= -1,
};

static void pstore_register_console(void)
{
	/* Show which backend is going to get console writes. */
	strscpy(pstore_console.name, psinfo->name,
		sizeof(pstore_console.name));
	/*
	 * Always initialize flags here since prior unregister_console()
	 * calls may have changed settings (specifically CON_ENABLED).
	 */
	pstore_console.flags = CON_PRINTBUFFER | CON_ENABLED | CON_ANYTIME;
	register_console(&pstore_console);
}

static void pstore_unregister_console(void)
{
	unregister_console(&pstore_console);
}
#else
static void pstore_register_console(void) {}
static void pstore_unregister_console(void) {}
#endif

static int pstore_write_user_compat(struct pstore_record *record,
				    const char __user *buf)
{
	int ret = 0;

	if (record->buf)
		return -EINVAL;

	record->buf = vmemdup_user(buf, record->size);
	if (IS_ERR(record->buf)) {
		ret = PTR_ERR(record->buf);
		goto out;
	}

	ret = record->psi->write(record);

	kvfree(record->buf);
out:
	record->buf = NULL;

	return unlikely(ret < 0) ? ret : record->size;
}

/*
 * platform specific persistent storage driver registers with
 * us here. If pstore is already mounted, call the platform
 * read function right away to populate the file system. If not
 * then the pstore mount code will call us later to fill out
 * the file system.
 */
int pstore_register(struct pstore_info *psi)
{
	if (backend && strcmp(backend, psi->name)) {
		pr_warn("backend '%s' already in use: ignoring '%s'\n",
			backend, psi->name);
		return -EBUSY;
	}

	/* Sanity check flags. */
	if (!psi->flags) {
		pr_warn("backend '%s' must support at least one frontend\n",
			psi->name);
		return -EINVAL;
	}

	/* Check for required functions. */
	if (!psi->read || !psi->write) {
		pr_warn("backend '%s' must implement read() and write()\n",
			psi->name);
		return -EINVAL;
	}

	mutex_lock(&psinfo_lock);
	if (psinfo) {
		pr_warn("backend '%s' already loaded: ignoring '%s'\n",
			psinfo->name, psi->name);
		mutex_unlock(&psinfo_lock);
		return -EBUSY;
	}

	if (!psi->write_user)
		psi->write_user = pstore_write_user_compat;
	psinfo = psi;
	mutex_init(&psinfo->read_mutex);
	spin_lock_init(&psinfo->buf_lock);

	if (psi->flags & PSTORE_FLAGS_DMESG)
		allocate_buf_for_compression();

	pstore_get_records(0);

	if (psi->flags & PSTORE_FLAGS_DMESG) {
		pstore_dumper.max_reason = psinfo->max_reason;
		pstore_register_kmsg();
	}
	if (psi->flags & PSTORE_FLAGS_CONSOLE)
		pstore_register_console();
	if (psi->flags & PSTORE_FLAGS_FTRACE)
		pstore_register_ftrace();
	if (psi->flags & PSTORE_FLAGS_PMSG)
		pstore_register_pmsg();

	/* Start watching for new records, if desired. */
	pstore_timer_kick();

	/*
	 * Update the module parameter backend, so it is visible
	 * through /sys/module/pstore/parameters/backend
	 */
	backend = kstrdup(psi->name, GFP_KERNEL);

	pr_info("Registered %s as persistent store backend\n", psi->name);

	mutex_unlock(&psinfo_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(pstore_register);

void pstore_unregister(struct pstore_info *psi)
{
	/* It's okay to unregister nothing. */
	if (!psi)
		return;

	mutex_lock(&psinfo_lock);

	/* Only one backend can be registered at a time. */
	if (WARN_ON(psi != psinfo)) {
		mutex_unlock(&psinfo_lock);
		return;
	}

	/* Unregister all callbacks. */
	if (psi->flags & PSTORE_FLAGS_PMSG)
		pstore_unregister_pmsg();
	if (psi->flags & PSTORE_FLAGS_FTRACE)
		pstore_unregister_ftrace();
	if (psi->flags & PSTORE_FLAGS_CONSOLE)
		pstore_unregister_console();
	if (psi->flags & PSTORE_FLAGS_DMESG)
		pstore_unregister_kmsg();

	/* Stop timer and make sure all work has finished. */
	del_timer_sync(&pstore_timer);
	flush_work(&pstore_work);

	/* Remove all backend records from filesystem tree. */
	pstore_put_backend_records(psi);

	free_buf_for_compression();

	psinfo = NULL;
	kfree(backend);
	backend = NULL;

	pr_info("Unregistered %s as persistent store backend\n", psi->name);
	mutex_unlock(&psinfo_lock);
}
EXPORT_SYMBOL_GPL(pstore_unregister);

static void decompress_record(struct pstore_record *record,
			      struct z_stream_s *zstream)
{
	int ret;
	int unzipped_len;
	char *unzipped, *workspace;
	size_t max_uncompressed_size;

	if (!IS_ENABLED(CONFIG_PSTORE_COMPRESS) || !record->compressed)
		return;

	/* Only PSTORE_TYPE_DMESG support compression. */
	if (record->type != PSTORE_TYPE_DMESG) {
		pr_warn("ignored compressed record type %d\n", record->type);
		return;
	}

	/* Missing compression buffer means compression was not initialized. */
	if (!zstream->workspace) {
		pr_warn("no decompression method initialized!\n");
		return;
	}

	ret = zlib_inflateReset(zstream);
	if (ret != Z_OK) {
		pr_err("zlib_inflateReset() failed, ret = %d!\n", ret);
		return;
	}

	/* Allocate enough space to hold max decompression and ECC. */
	max_uncompressed_size = 3 * psinfo->bufsize;
	workspace = kvzalloc(max_uncompressed_size + record->ecc_notice_size,
			     GFP_KERNEL);
	if (!workspace)
		return;

	zstream->next_in	= record->buf;
	zstream->avail_in	= record->size;
	zstream->next_out	= workspace;
	zstream->avail_out	= max_uncompressed_size;

	ret = zlib_inflate(zstream, Z_FINISH);
	if (ret != Z_STREAM_END) {
		pr_err_ratelimited("zlib_inflate() failed, ret = %d!\n", ret);
		kvfree(workspace);
		return;
	}

	unzipped_len = zstream->total_out;

	/* Append ECC notice to decompressed buffer. */
	memcpy(workspace + unzipped_len, record->buf + record->size,
	       record->ecc_notice_size);

	/* Copy decompressed contents into an minimum-sized allocation. */
	unzipped = kvmemdup(workspace, unzipped_len + record->ecc_notice_size,
			    GFP_KERNEL);
	kvfree(workspace);
	if (!unzipped)
		return;

	/* Swap out compressed contents with decompressed contents. */
	kvfree(record->buf);
	record->buf = unzipped;
	record->size = unzipped_len;
	record->compressed = false;
}

/*
 * Read all the records from one persistent store backend. Create
 * files in our filesystem.  Don't warn about -EEXIST errors
 * when we are re-scanning the backing store looking to add new
 * error records.
 */
void pstore_get_backend_records(struct pstore_info *psi,
				struct dentry *root, int quiet)
{
	int failed = 0;
	unsigned int stop_loop = 65536;
	struct z_stream_s zstream = {};

	if (!psi || !root)
		return;

	if (IS_ENABLED(CONFIG_PSTORE_COMPRESS) && compress) {
		zstream.workspace = kvmalloc(zlib_inflate_workspacesize(),
					     GFP_KERNEL);
		zlib_inflateInit2(&zstream, -DEF_WBITS);
	}

	mutex_lock(&psi->read_mutex);
	if (psi->open && psi->open(psi))
		goto out;

	/*
	 * Backend callback read() allocates record.buf. decompress_record()
	 * may reallocate record.buf. On success, pstore_mkfile() will keep
	 * the record.buf, so free it only on failure.
	 */
	for (; stop_loop; stop_loop--) {
		struct pstore_record *record;
		int rc;

		record = kzalloc(sizeof(*record), GFP_KERNEL);
		if (!record) {
			pr_err("out of memory creating record\n");
			break;
		}
		pstore_record_init(record, psi);

		record->size = psi->read(record);

		/* No more records left in backend? */
		if (record->size <= 0) {
			kfree(record);
			break;
		}

		decompress_record(record, &zstream);
		rc = pstore_mkfile(root, record);
		if (rc) {
			/* pstore_mkfile() did not take record, so free it. */
			kvfree(record->buf);
			kfree(record->priv);
			kfree(record);
			if (rc != -EEXIST || !quiet)
				failed++;
		}
	}
	if (psi->close)
		psi->close(psi);
out:
	mutex_unlock(&psi->read_mutex);

	if (IS_ENABLED(CONFIG_PSTORE_COMPRESS) && compress) {
		if (zlib_inflateEnd(&zstream) != Z_OK)
			pr_warn("zlib_inflateEnd() failed\n");
		kvfree(zstream.workspace);
	}

	if (failed)
		pr_warn("failed to create %d record(s) from '%s'\n",
			failed, psi->name);
	if (!stop_loop)
		pr_err("looping? Too many records seen from '%s'\n",
			psi->name);
}

static void pstore_dowork(struct work_struct *work)
{
	pstore_get_records(1);
}

static void pstore_timefunc(struct timer_list *unused)
{
	if (pstore_new_entry) {
		pstore_new_entry = 0;
		schedule_work(&pstore_work);
	}

	pstore_timer_kick();
}

static int __init pstore_init(void)
{
	int ret;

	ret = pstore_init_fs();
	if (ret)
		free_buf_for_compression();

	return ret;
}
late_initcall(pstore_init);

static void __exit pstore_exit(void)
{
	pstore_exit_fs();
}
module_exit(pstore_exit)

MODULE_AUTHOR("Tony Luck <tony.luck@intel.com>");
MODULE_LICENSE("GPL");
