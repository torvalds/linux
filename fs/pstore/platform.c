/*
 * Persistent Storage - platform driver interface parts.
 *
 * Copyright (C) 2007-2008 Google, Inc.
 * Copyright (C) 2010 Intel Corporation <tony.luck@intel.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define pr_fmt(fmt) "pstore: " fmt

#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kmsg_dump.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/pstore.h>
#if IS_ENABLED(CONFIG_PSTORE_LZO_COMPRESS)
#include <linux/lzo.h>
#endif
#if IS_ENABLED(CONFIG_PSTORE_LZ4_COMPRESS) || IS_ENABLED(CONFIG_PSTORE_LZ4HC_COMPRESS)
#include <linux/lz4.h>
#endif
#include <linux/crypto.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>

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
		 "enabling this option is not safe, it may lead to further "
		 "corruption on Oopses)");

static int pstore_new_entry;

static void pstore_timefunc(struct timer_list *);
static DEFINE_TIMER(pstore_timer, pstore_timefunc);

static void pstore_dowork(struct work_struct *);
static DECLARE_WORK(pstore_work, pstore_dowork);

/*
 * pstore_lock just protects "psinfo" during
 * calls to pstore_register()
 */
static DEFINE_SPINLOCK(pstore_lock);
struct pstore_info *psinfo;

static char *backend;
static char *compress =
#ifdef CONFIG_PSTORE_COMPRESS_DEFAULT
		CONFIG_PSTORE_COMPRESS_DEFAULT;
#else
		NULL;
#endif

/* Compression parameters */
static struct crypto_comp *tfm;

struct pstore_zbackend {
	int (*zbufsize)(size_t size);
	const char *name;
};

static char *big_oops_buf;
static size_t big_oops_buf_sz;

/* How much of the console log to snapshot */
unsigned long kmsg_bytes = PSTORE_DEFAULT_KMSG_BYTES;

void pstore_set_kmsg_bytes(int bytes)
{
	kmsg_bytes = bytes;
}

/* Tag each group of saved records with a sequence number */
static int	oopscount;

static const char *get_reason_str(enum kmsg_dump_reason reason)
{
	switch (reason) {
	case KMSG_DUMP_PANIC:
		return "Panic";
	case KMSG_DUMP_OOPS:
		return "Oops";
	case KMSG_DUMP_EMERG:
		return "Emergency";
	case KMSG_DUMP_RESTART:
		return "Restart";
	case KMSG_DUMP_HALT:
		return "Halt";
	case KMSG_DUMP_POWEROFF:
		return "Poweroff";
	default:
		return "Unknown";
	}
}

bool pstore_cannot_block_path(enum kmsg_dump_reason reason)
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
	/* Emergency restart shouldn't be blocked by spin lock. */
	case KMSG_DUMP_EMERG:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(pstore_cannot_block_path);

#if IS_ENABLED(CONFIG_PSTORE_DEFLATE_COMPRESS)
static int zbufsize_deflate(size_t size)
{
	size_t cmpr;

	switch (size) {
	/* buffer range for efivars */
	case 1000 ... 2000:
		cmpr = 56;
		break;
	case 2001 ... 3000:
		cmpr = 54;
		break;
	case 3001 ... 3999:
		cmpr = 52;
		break;
	/* buffer range for nvram, erst */
	case 4000 ... 10000:
		cmpr = 45;
		break;
	default:
		cmpr = 60;
		break;
	}

	return (size * 100) / cmpr;
}
#endif

#if IS_ENABLED(CONFIG_PSTORE_LZO_COMPRESS)
static int zbufsize_lzo(size_t size)
{
	return lzo1x_worst_compress(size);
}
#endif

#if IS_ENABLED(CONFIG_PSTORE_LZ4_COMPRESS) || IS_ENABLED(CONFIG_PSTORE_LZ4HC_COMPRESS)
static int zbufsize_lz4(size_t size)
{
	return LZ4_compressBound(size);
}
#endif

#if IS_ENABLED(CONFIG_PSTORE_842_COMPRESS)
static int zbufsize_842(size_t size)
{
	return size;
}
#endif

static const struct pstore_zbackend *zbackend __ro_after_init;

static const struct pstore_zbackend zbackends[] = {
#if IS_ENABLED(CONFIG_PSTORE_DEFLATE_COMPRESS)
	{
		.zbufsize	= zbufsize_deflate,
		.name		= "deflate",
	},
#endif
#if IS_ENABLED(CONFIG_PSTORE_LZO_COMPRESS)
	{
		.zbufsize	= zbufsize_lzo,
		.name		= "lzo",
	},
#endif
#if IS_ENABLED(CONFIG_PSTORE_LZ4_COMPRESS)
	{
		.zbufsize	= zbufsize_lz4,
		.name		= "lz4",
	},
#endif
#if IS_ENABLED(CONFIG_PSTORE_LZ4HC_COMPRESS)
	{
		.zbufsize	= zbufsize_lz4,
		.name		= "lz4hc",
	},
#endif
#if IS_ENABLED(CONFIG_PSTORE_842_COMPRESS)
	{
		.zbufsize	= zbufsize_842,
		.name		= "842",
	},
#endif
	{ }
};

static int pstore_compress(const void *in, void *out,
			   unsigned int inlen, unsigned int outlen)
{
	int ret;

	ret = crypto_comp_compress(tfm, in, inlen, out, &outlen);
	if (ret) {
		pr_err("crypto_comp_compress failed, ret = %d!\n", ret);
		return ret;
	}

	return outlen;
}

static int pstore_decompress(void *in, void *out,
			     unsigned int inlen, unsigned int outlen)
{
	int ret;

	ret = crypto_comp_decompress(tfm, in, inlen, out, &outlen);
	if (ret) {
		pr_err("crypto_comp_decompress failed, ret = %d!\n", ret);
		return ret;
	}

	return outlen;
}

static void allocate_buf_for_compression(void)
{
	if (!IS_ENABLED(CONFIG_PSTORE_COMPRESS) || !zbackend)
		return;

	if (!crypto_has_comp(zbackend->name, 0, 0)) {
		pr_err("No %s compression\n", zbackend->name);
		return;
	}

	big_oops_buf_sz = zbackend->zbufsize(psinfo->bufsize);
	if (big_oops_buf_sz <= 0)
		return;

	big_oops_buf = kmalloc(big_oops_buf_sz, GFP_KERNEL);
	if (!big_oops_buf) {
		pr_err("allocate compression buffer error!\n");
		return;
	}

	tfm = crypto_alloc_comp(zbackend->name, 0, 0);
	if (IS_ERR_OR_NULL(tfm)) {
		kfree(big_oops_buf);
		big_oops_buf = NULL;
		pr_err("crypto_alloc_comp() failed!\n");
		return;
	}
}

static void free_buf_for_compression(void)
{
	if (IS_ENABLED(CONFIG_PSTORE_COMPRESS) && !IS_ERR_OR_NULL(tfm))
		crypto_free_comp(tfm);
	kfree(big_oops_buf);
	big_oops_buf = NULL;
	big_oops_buf_sz = 0;
}

/*
 * Called when compression fails, since the printk buffer
 * would be fetched for compression calling it again when
 * compression fails would have moved the iterator of
 * printk buffer which results in fetching old contents.
 * Copy the recent messages from big_oops_buf to psinfo->buf
 */
static size_t copy_kmsg_to_buffer(int hsize, size_t len)
{
	size_t total_len;
	size_t diff;

	total_len = hsize + len;

	if (total_len > psinfo->bufsize) {
		diff = total_len - psinfo->bufsize + hsize;
		memcpy(psinfo->buf, big_oops_buf, hsize);
		memcpy(psinfo->buf + hsize, big_oops_buf + diff,
					psinfo->bufsize - hsize);
		total_len = psinfo->bufsize;
	} else
		memcpy(psinfo->buf, big_oops_buf, total_len);

	return total_len;
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
 * callback from kmsg_dump. (s2,l2) has the most recently
 * written bytes, older bytes are in (s1,l1). Save as much
 * as we can from the end of the buffer.
 */
static void pstore_dump(struct kmsg_dumper *dumper,
			enum kmsg_dump_reason reason)
{
	unsigned long	total = 0;
	const char	*why;
	unsigned int	part = 1;
	unsigned long	flags = 0;
	int		is_locked;
	int		ret;

	why = get_reason_str(reason);

	if (pstore_cannot_block_path(reason)) {
		is_locked = spin_trylock_irqsave(&psinfo->buf_lock, flags);
		if (!is_locked) {
			pr_err("pstore dump routine blocked in %s path, may corrupt error record\n"
				       , in_nmi() ? "NMI" : why);
			return;
		}
	} else {
		spin_lock_irqsave(&psinfo->buf_lock, flags);
		is_locked = 1;
	}
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

		if (big_oops_buf && is_locked) {
			dst = big_oops_buf;
			dst_size = big_oops_buf_sz;
		} else {
			dst = psinfo->buf;
			dst_size = psinfo->bufsize;
		}

		/* Write dump header. */
		header_size = snprintf(dst, dst_size, "%s#%d Part%u\n", why,
				 oopscount, part);
		dst_size -= header_size;

		/* Write dump contents. */
		if (!kmsg_dump_get_buffer(dumper, true, dst + header_size,
					  dst_size, &dump_size))
			break;

		if (big_oops_buf && is_locked) {
			zipped_len = pstore_compress(dst, psinfo->buf,
						header_size + dump_size,
						psinfo->bufsize);

			if (zipped_len > 0) {
				record.compressed = true;
				record.size = zipped_len;
			} else {
				record.size = copy_kmsg_to_buffer(header_size,
								  dump_size);
			}
		} else {
			record.size = header_size + dump_size;
		}

		ret = psinfo->write(&record);
		if (ret == 0 && reason == KMSG_DUMP_OOPS && pstore_is_mounted())
			pstore_new_entry = 1;

		total += record.size;
		part++;
	}
	if (is_locked)
		spin_unlock_irqrestore(&psinfo->buf_lock, flags);
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
	const char *e = s + c;

	while (s < e) {
		struct pstore_record record;
		unsigned long flags;

		pstore_record_init(&record, psinfo);
		record.type = PSTORE_TYPE_CONSOLE;

		if (c > psinfo->bufsize)
			c = psinfo->bufsize;

		if (oops_in_progress) {
			if (!spin_trylock_irqsave(&psinfo->buf_lock, flags))
				break;
		} else {
			spin_lock_irqsave(&psinfo->buf_lock, flags);
		}
		record.buf = (char *)s;
		record.size = c;
		psinfo->write(&record);
		spin_unlock_irqrestore(&psinfo->buf_lock, flags);
		s += c;
		c = e - s;
	}
}

static struct console pstore_console = {
	.name	= "pstore",
	.write	= pstore_console_write,
	.flags	= CON_PRINTBUFFER | CON_ENABLED | CON_ANYTIME,
	.index	= -1,
};

static void pstore_register_console(void)
{
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

	record->buf = memdup_user(buf, record->size);
	if (IS_ERR(record->buf)) {
		ret = PTR_ERR(record->buf);
		goto out;
	}

	ret = record->psi->write(record);

	kfree(record->buf);
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
	struct module *owner = psi->owner;

	if (backend && strcmp(backend, psi->name)) {
		pr_warn("ignoring unexpected backend '%s'\n", psi->name);
		return -EPERM;
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

	spin_lock(&pstore_lock);
	if (psinfo) {
		pr_warn("backend '%s' already loaded: ignoring '%s'\n",
			psinfo->name, psi->name);
		spin_unlock(&pstore_lock);
		return -EBUSY;
	}

	if (!psi->write_user)
		psi->write_user = pstore_write_user_compat;
	psinfo = psi;
	mutex_init(&psinfo->read_mutex);
	spin_unlock(&pstore_lock);

	if (owner && !try_module_get(owner)) {
		psinfo = NULL;
		return -EINVAL;
	}

	allocate_buf_for_compression();

	if (pstore_is_mounted())
		pstore_get_records(0);

	if (psi->flags & PSTORE_FLAGS_DMESG)
		pstore_register_kmsg();
	if (psi->flags & PSTORE_FLAGS_CONSOLE)
		pstore_register_console();
	if (psi->flags & PSTORE_FLAGS_FTRACE)
		pstore_register_ftrace();
	if (psi->flags & PSTORE_FLAGS_PMSG)
		pstore_register_pmsg();

	/* Start watching for new records, if desired. */
	if (pstore_update_ms >= 0) {
		pstore_timer.expires = jiffies +
			msecs_to_jiffies(pstore_update_ms);
		add_timer(&pstore_timer);
	}

	/*
	 * Update the module parameter backend, so it is visible
	 * through /sys/module/pstore/parameters/backend
	 */
	backend = psi->name;

	pr_info("Registered %s as persistent store backend\n", psi->name);

	module_put(owner);

	return 0;
}
EXPORT_SYMBOL_GPL(pstore_register);

void pstore_unregister(struct pstore_info *psi)
{
	/* Stop timer and make sure all work has finished. */
	pstore_update_ms = -1;
	del_timer_sync(&pstore_timer);
	flush_work(&pstore_work);

	if (psi->flags & PSTORE_FLAGS_PMSG)
		pstore_unregister_pmsg();
	if (psi->flags & PSTORE_FLAGS_FTRACE)
		pstore_unregister_ftrace();
	if (psi->flags & PSTORE_FLAGS_CONSOLE)
		pstore_unregister_console();
	if (psi->flags & PSTORE_FLAGS_DMESG)
		pstore_unregister_kmsg();

	free_buf_for_compression();

	psinfo = NULL;
	backend = NULL;
}
EXPORT_SYMBOL_GPL(pstore_unregister);

static void decompress_record(struct pstore_record *record)
{
	int unzipped_len;
	char *decompressed;

	if (!record->compressed)
		return;

	/* Only PSTORE_TYPE_DMESG support compression. */
	if (record->type != PSTORE_TYPE_DMESG) {
		pr_warn("ignored compressed record type %d\n", record->type);
		return;
	}

	/* No compression method has created the common buffer. */
	if (!big_oops_buf) {
		pr_warn("no decompression buffer allocated\n");
		return;
	}

	unzipped_len = pstore_decompress(record->buf, big_oops_buf,
					 record->size, big_oops_buf_sz);
	if (unzipped_len <= 0) {
		pr_err("decompression failed: %d\n", unzipped_len);
		return;
	}

	/* Build new buffer for decompressed contents. */
	decompressed = kmalloc(unzipped_len + record->ecc_notice_size,
			       GFP_KERNEL);
	if (!decompressed) {
		pr_err("decompression ran out of memory\n");
		return;
	}
	memcpy(decompressed, big_oops_buf, unzipped_len);

	/* Append ECC notice to decompressed buffer. */
	memcpy(decompressed + unzipped_len, record->buf + record->size,
	       record->ecc_notice_size);

	/* Swap out compresed contents with decompressed contents. */
	kfree(record->buf);
	record->buf = decompressed;
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

	if (!psi || !root)
		return;

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

		decompress_record(record);
		rc = pstore_mkfile(root, record);
		if (rc) {
			/* pstore_mkfile() did not take record, so free it. */
			kfree(record->buf);
			kfree(record);
			if (rc != -EEXIST || !quiet)
				failed++;
		}
	}
	if (psi->close)
		psi->close(psi);
out:
	mutex_unlock(&psi->read_mutex);

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

	if (pstore_update_ms >= 0)
		mod_timer(&pstore_timer,
			  jiffies + msecs_to_jiffies(pstore_update_ms));
}

void __init pstore_choose_compression(void)
{
	const struct pstore_zbackend *step;

	if (!compress)
		return;

	for (step = zbackends; step->name; step++) {
		if (!strcmp(compress, step->name)) {
			zbackend = step;
			pr_info("using %s compression\n", zbackend->name);
			return;
		}
	}
}

module_param(compress, charp, 0444);
MODULE_PARM_DESC(compress, "Pstore compression to use");

module_param(backend, charp, 0444);
MODULE_PARM_DESC(backend, "Pstore backend to use");
