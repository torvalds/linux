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
#ifdef CONFIG_PSTORE_ZLIB_COMPRESS
#include <linux/zlib.h>
#endif
#ifdef CONFIG_PSTORE_LZO_COMPRESS
#include <linux/lzo.h>
#endif
#ifdef CONFIG_PSTORE_LZ4_COMPRESS
#include <linux/lz4.h>
#endif
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
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

static void pstore_timefunc(unsigned long);
static DEFINE_TIMER(pstore_timer, pstore_timefunc, 0, 0);

static void pstore_dowork(struct work_struct *);
static DECLARE_WORK(pstore_work, pstore_dowork);

/*
 * pstore_lock just protects "psinfo" during
 * calls to pstore_register()
 */
static DEFINE_SPINLOCK(pstore_lock);
struct pstore_info *psinfo;

static char *backend;

/* Compression parameters */
#ifdef CONFIG_PSTORE_ZLIB_COMPRESS
#define COMPR_LEVEL 6
#define WINDOW_BITS 12
#define MEM_LEVEL 4
static struct z_stream_s stream;
#else
static unsigned char *workspace;
#endif

struct pstore_zbackend {
	int (*compress)(const void *in, void *out, size_t inlen, size_t outlen);
	int (*decompress)(void *in, void *out, size_t inlen, size_t outlen);
	void (*allocate)(void);
	void (*free)(void);

	const char *name;
};

static char *big_oops_buf;
static size_t big_oops_buf_sz;

/* How much of the console log to snapshot */
static unsigned long kmsg_bytes = 10240;

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

#ifdef CONFIG_PSTORE_ZLIB_COMPRESS
/* Derived from logfs_compress() */
static int compress_zlib(const void *in, void *out, size_t inlen, size_t outlen)
{
	int err, ret;

	ret = -EIO;
	err = zlib_deflateInit2(&stream, COMPR_LEVEL, Z_DEFLATED, WINDOW_BITS,
						MEM_LEVEL, Z_DEFAULT_STRATEGY);
	if (err != Z_OK)
		goto error;

	stream.next_in = in;
	stream.avail_in = inlen;
	stream.total_in = 0;
	stream.next_out = out;
	stream.avail_out = outlen;
	stream.total_out = 0;

	err = zlib_deflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END)
		goto error;

	err = zlib_deflateEnd(&stream);
	if (err != Z_OK)
		goto error;

	if (stream.total_out >= stream.total_in)
		goto error;

	ret = stream.total_out;
error:
	return ret;
}

/* Derived from logfs_uncompress */
static int decompress_zlib(void *in, void *out, size_t inlen, size_t outlen)
{
	int err, ret;

	ret = -EIO;
	err = zlib_inflateInit2(&stream, WINDOW_BITS);
	if (err != Z_OK)
		goto error;

	stream.next_in = in;
	stream.avail_in = inlen;
	stream.total_in = 0;
	stream.next_out = out;
	stream.avail_out = outlen;
	stream.total_out = 0;

	err = zlib_inflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END)
		goto error;

	err = zlib_inflateEnd(&stream);
	if (err != Z_OK)
		goto error;

	ret = stream.total_out;
error:
	return ret;
}

static void allocate_zlib(void)
{
	size_t size;
	size_t cmpr;

	switch (psinfo->bufsize) {
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

	big_oops_buf_sz = (psinfo->bufsize * 100) / cmpr;
	big_oops_buf = kmalloc(big_oops_buf_sz, GFP_KERNEL);
	if (big_oops_buf) {
		size = max(zlib_deflate_workspacesize(WINDOW_BITS, MEM_LEVEL),
			zlib_inflate_workspacesize());
		stream.workspace = kmalloc(size, GFP_KERNEL);
		if (!stream.workspace) {
			pr_err("No memory for compression workspace; skipping compression\n");
			kfree(big_oops_buf);
			big_oops_buf = NULL;
		}
	} else {
		pr_err("No memory for uncompressed data; skipping compression\n");
		stream.workspace = NULL;
	}

}

static void free_zlib(void)
{
	kfree(stream.workspace);
	stream.workspace = NULL;
	kfree(big_oops_buf);
	big_oops_buf = NULL;
	big_oops_buf_sz = 0;
}

static struct pstore_zbackend backend_zlib = {
	.compress	= compress_zlib,
	.decompress	= decompress_zlib,
	.allocate	= allocate_zlib,
	.free		= free_zlib,
	.name		= "zlib",
};
#endif

#ifdef CONFIG_PSTORE_LZO_COMPRESS
static int compress_lzo(const void *in, void *out, size_t inlen, size_t outlen)
{
	int ret;

	ret = lzo1x_1_compress(in, inlen, out, &outlen, workspace);
	if (ret != LZO_E_OK) {
		pr_err("lzo_compress error, ret = %d!\n", ret);
		return -EIO;
	}

	return outlen;
}

static int decompress_lzo(void *in, void *out, size_t inlen, size_t outlen)
{
	int ret;

	ret = lzo1x_decompress_safe(in, inlen, out, &outlen);
	if (ret != LZO_E_OK) {
		pr_err("lzo_decompress error, ret = %d!\n", ret);
		return -EIO;
	}

	return outlen;
}

static void allocate_lzo(void)
{
	big_oops_buf_sz = lzo1x_worst_compress(psinfo->bufsize);
	big_oops_buf = kmalloc(big_oops_buf_sz, GFP_KERNEL);
	if (big_oops_buf) {
		workspace = kmalloc(LZO1X_MEM_COMPRESS, GFP_KERNEL);
		if (!workspace) {
			pr_err("No memory for compression workspace; skipping compression\n");
			kfree(big_oops_buf);
			big_oops_buf = NULL;
		}
	} else {
		pr_err("No memory for uncompressed data; skipping compression\n");
		workspace = NULL;
	}
}

static void free_lzo(void)
{
	kfree(workspace);
	kfree(big_oops_buf);
	big_oops_buf = NULL;
	big_oops_buf_sz = 0;
}

static struct pstore_zbackend backend_lzo = {
	.compress	= compress_lzo,
	.decompress	= decompress_lzo,
	.allocate	= allocate_lzo,
	.free		= free_lzo,
	.name		= "lzo",
};
#endif

#ifdef CONFIG_PSTORE_LZ4_COMPRESS
static int compress_lz4(const void *in, void *out, size_t inlen, size_t outlen)
{
	int ret;

	ret = LZ4_compress_default(in, out, inlen, outlen, workspace);
	if (!ret) {
		pr_err("LZ4_compress_default error; compression failed!\n");
		return -EIO;
	}

	return ret;
}

static int decompress_lz4(void *in, void *out, size_t inlen, size_t outlen)
{
	int ret;

	ret = LZ4_decompress_safe(in, out, inlen, outlen);
	if (ret < 0) {
		/*
		 * LZ4_decompress_safe will return an error code
		 * (< 0) if decompression failed
		 */
		pr_err("LZ4_decompress_safe error, ret = %d!\n", ret);
		return -EIO;
	}

	return ret;
}

static void allocate_lz4(void)
{
	big_oops_buf_sz = LZ4_compressBound(psinfo->bufsize);
	big_oops_buf = kmalloc(big_oops_buf_sz, GFP_KERNEL);
	if (big_oops_buf) {
		workspace = kmalloc(LZ4_MEM_COMPRESS, GFP_KERNEL);
		if (!workspace) {
			pr_err("No memory for compression workspace; skipping compression\n");
			kfree(big_oops_buf);
			big_oops_buf = NULL;
		}
	} else {
		pr_err("No memory for uncompressed data; skipping compression\n");
		workspace = NULL;
	}
}

static void free_lz4(void)
{
	kfree(workspace);
	kfree(big_oops_buf);
	big_oops_buf = NULL;
	big_oops_buf_sz = 0;
}

static struct pstore_zbackend backend_lz4 = {
	.compress	= compress_lz4,
	.decompress	= decompress_lz4,
	.allocate	= allocate_lz4,
	.free		= free_lz4,
	.name		= "lz4",
};
#endif

static struct pstore_zbackend *zbackend =
#if defined(CONFIG_PSTORE_ZLIB_COMPRESS)
	&backend_zlib;
#elif defined(CONFIG_PSTORE_LZO_COMPRESS)
	&backend_lzo;
#elif defined(CONFIG_PSTORE_LZ4_COMPRESS)
	&backend_lz4;
#else
	NULL;
#endif

static int pstore_compress(const void *in, void *out,
			   size_t inlen, size_t outlen)
{
	if (zbackend)
		return zbackend->compress(in, out, inlen, outlen);
	else
		return -EIO;
}

static int pstore_decompress(void *in, void *out, size_t inlen, size_t outlen)
{
	if (zbackend)
		return zbackend->decompress(in, out, inlen, outlen);
	else
		return -EIO;
}

static void allocate_buf_for_compression(void)
{
	if (zbackend) {
		pr_info("using %s compression\n", zbackend->name);
		zbackend->allocate();
	} else {
		pr_err("allocate compression buffer error!\n");
	}
}

static void free_buf_for_compression(void)
{
	if (zbackend)
		zbackend->free();
	else
		pr_err("free compression buffer error!\n");
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
	u64		id;
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
		unsigned long size;
		int hsize;
		int zipped_len = -1;
		size_t len;
		bool compressed = false;
		size_t total_len;

		if (big_oops_buf && is_locked) {
			dst = big_oops_buf;
			size = big_oops_buf_sz;
		} else {
			dst = psinfo->buf;
			size = psinfo->bufsize;
		}

		hsize = sprintf(dst, "%s#%d Part%u\n", why, oopscount, part);
		size -= hsize;

		if (!kmsg_dump_get_buffer(dumper, true, dst + hsize,
					  size, &len))
			break;

		if (big_oops_buf && is_locked) {
			zipped_len = pstore_compress(dst, psinfo->buf,
						hsize + len, psinfo->bufsize);

			if (zipped_len > 0) {
				compressed = true;
				total_len = zipped_len;
			} else {
				total_len = copy_kmsg_to_buffer(hsize, len);
			}
		} else {
			total_len = hsize + len;
		}

		ret = psinfo->write(PSTORE_TYPE_DMESG, reason, &id, part,
				    oopscount, compressed, total_len, psinfo);
		if (ret == 0 && reason == KMSG_DUMP_OOPS && pstore_is_mounted())
			pstore_new_entry = 1;

		total += total_len;
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
		unsigned long flags;
		u64 id;

		if (c > psinfo->bufsize)
			c = psinfo->bufsize;

		if (oops_in_progress) {
			if (!spin_trylock_irqsave(&psinfo->buf_lock, flags))
				break;
		} else {
			spin_lock_irqsave(&psinfo->buf_lock, flags);
		}
		psinfo->write_buf(PSTORE_TYPE_CONSOLE, 0, &id, 0,
				  s, 0, c, psinfo);
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

static int pstore_write_compat(enum pstore_type_id type,
			       enum kmsg_dump_reason reason,
			       u64 *id, unsigned int part, int count,
			       bool compressed, size_t size,
			       struct pstore_info *psi)
{
	return psi->write_buf(type, reason, id, part, psinfo->buf, compressed,
			     size, psi);
}

static int pstore_write_buf_user_compat(enum pstore_type_id type,
			       enum kmsg_dump_reason reason,
			       u64 *id, unsigned int part,
			       const char __user *buf,
			       bool compressed, size_t size,
			       struct pstore_info *psi)
{
	unsigned long flags = 0;
	size_t i, bufsize = size;
	long ret = 0;

	if (unlikely(!access_ok(VERIFY_READ, buf, size)))
		return -EFAULT;
	if (bufsize > psinfo->bufsize)
		bufsize = psinfo->bufsize;
	spin_lock_irqsave(&psinfo->buf_lock, flags);
	for (i = 0; i < size; ) {
		size_t c = min(size - i, bufsize);

		ret = __copy_from_user(psinfo->buf, buf + i, c);
		if (unlikely(ret != 0)) {
			ret = -EFAULT;
			break;
		}
		ret = psi->write_buf(type, reason, id, part, psinfo->buf,
				     compressed, c, psi);
		if (unlikely(ret < 0))
			break;
		i += c;
	}
	spin_unlock_irqrestore(&psinfo->buf_lock, flags);
	return unlikely(ret < 0) ? ret : size;
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

	if (backend && strcmp(backend, psi->name))
		return -EPERM;

	spin_lock(&pstore_lock);
	if (psinfo) {
		spin_unlock(&pstore_lock);
		return -EBUSY;
	}

	if (!psi->write)
		psi->write = pstore_write_compat;
	if (!psi->write_buf_user)
		psi->write_buf_user = pstore_write_buf_user_compat;
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

	module_put(owner);

	pr_info("Registered %s as persistent store backend\n", psi->name);

	return 0;
}
EXPORT_SYMBOL_GPL(pstore_register);

void pstore_unregister(struct pstore_info *psi)
{
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

/*
 * Read all the records from the persistent store. Create
 * files in our filesystem.  Don't warn about -EEXIST errors
 * when we are re-scanning the backing store looking to add new
 * error records.
 */
void pstore_get_records(int quiet)
{
	struct pstore_info *psi = psinfo;
	char			*buf = NULL;
	ssize_t			size;
	u64			id;
	int			count;
	enum pstore_type_id	type;
	struct timespec		time;
	int			failed = 0, rc;
	bool			compressed;
	int			unzipped_len = -1;
	ssize_t			ecc_notice_size = 0;

	if (!psi)
		return;

	mutex_lock(&psi->read_mutex);
	if (psi->open && psi->open(psi))
		goto out;

	while ((size = psi->read(&id, &type, &count, &time, &buf, &compressed,
				 &ecc_notice_size, psi)) > 0) {
		if (compressed && (type == PSTORE_TYPE_DMESG)) {
			if (big_oops_buf)
				unzipped_len = pstore_decompress(buf,
							big_oops_buf, size,
							big_oops_buf_sz);

			if (unzipped_len > 0) {
				if (ecc_notice_size)
					memcpy(big_oops_buf + unzipped_len,
					       buf + size, ecc_notice_size);
				kfree(buf);
				buf = big_oops_buf;
				size = unzipped_len;
				compressed = false;
			} else {
				pr_err("decompression failed;returned %d\n",
				       unzipped_len);
				compressed = true;
			}
		}
		rc = pstore_mkfile(type, psi->name, id, count, buf,
				   compressed, size + ecc_notice_size,
				   time, psi);
		if (unzipped_len < 0) {
			/* Free buffer other than big oops */
			kfree(buf);
			buf = NULL;
		} else
			unzipped_len = -1;
		if (rc && (rc != -EEXIST || !quiet))
			failed++;
	}
	if (psi->close)
		psi->close(psi);
out:
	mutex_unlock(&psi->read_mutex);

	if (failed)
		pr_warn("failed to load %d record(s) from '%s'\n",
			failed, psi->name);
}

static void pstore_dowork(struct work_struct *work)
{
	pstore_get_records(1);
}

static void pstore_timefunc(unsigned long dummy)
{
	if (pstore_new_entry) {
		pstore_new_entry = 0;
		schedule_work(&pstore_work);
	}

	mod_timer(&pstore_timer, jiffies + msecs_to_jiffies(pstore_update_ms));
}

module_param(backend, charp, 0444);
MODULE_PARM_DESC(backend, "Pstore backend to use");
