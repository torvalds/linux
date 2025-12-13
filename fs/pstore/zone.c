// SPDX-License-Identifier: GPL-2.0
/*
 * Provide a pstore intermediate backend, organized into kernel memory
 * allocated zones that are then mapped and flushed into a single
 * contiguous region on a storage backend of some kind (block, mtd, etc).
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/pstore_zone.h>
#include <linux/kdev_t.h>
#include <linux/device.h>
#include <linux/namei.h>
#include <linux/fcntl.h>
#include <linux/uio.h>
#include <linux/writeback.h>
#include "internal.h"

/**
 * struct psz_buffer - header of zone to flush to storage
 *
 * @sig: signature to indicate header (PSZ_SIG xor PSZONE-type value)
 * @datalen: length of data in @data
 * @start: offset into @data where the beginning of the stored bytes begin
 * @data: zone data.
 */
struct psz_buffer {
#define PSZ_SIG (0x43474244) /* DBGC */
	uint32_t sig;
	atomic_t datalen;
	atomic_t start;
	uint8_t data[];
};

/**
 * struct psz_kmsg_header - kmsg dump-specific header to flush to storage
 *
 * @magic: magic num for kmsg dump header
 * @time: kmsg dump trigger time
 * @compressed: whether compressed
 * @counter: kmsg dump counter
 * @reason: the kmsg dump reason (e.g. oops, panic, etc)
 * @data: pointer to log data
 *
 * This is a sub-header for a kmsg dump, trailing after &psz_buffer.
 */
struct psz_kmsg_header {
#define PSTORE_KMSG_HEADER_MAGIC 0x4dfc3ae5 /* Just a random number */
	uint32_t magic;
	struct timespec64 time;
	bool compressed;
	uint32_t counter;
	enum kmsg_dump_reason reason;
	uint8_t data[];
};

/**
 * struct pstore_zone - single stored buffer
 *
 * @off: zone offset of storage
 * @type: front-end type for this zone
 * @name: front-end name for this zone
 * @buffer: pointer to data buffer managed by this zone
 * @oldbuf: pointer to old data buffer
 * @buffer_size: bytes in @buffer->data
 * @should_recover: whether this zone should recover from storage
 * @dirty: whether the data in @buffer dirty
 *
 * zone structure in memory.
 */
struct pstore_zone {
	loff_t off;
	const char *name;
	enum pstore_type_id type;

	struct psz_buffer *buffer;
	struct psz_buffer *oldbuf;
	size_t buffer_size;
	bool should_recover;
	atomic_t dirty;
};

/**
 * struct psz_context - all about running state of pstore/zone
 *
 * @kpszs: kmsg dump storage zones
 * @ppsz: pmsg storage zone
 * @cpsz: console storage zone
 * @fpszs: ftrace storage zones
 * @kmsg_max_cnt: max count of @kpszs
 * @kmsg_read_cnt: counter of total read kmsg dumps
 * @kmsg_write_cnt: counter of total kmsg dump writes
 * @pmsg_read_cnt: counter of total read pmsg zone
 * @console_read_cnt: counter of total read console zone
 * @ftrace_max_cnt: max count of @fpszs
 * @ftrace_read_cnt: counter of max read ftrace zone
 * @oops_counter: counter of oops dumps
 * @panic_counter: counter of panic dumps
 * @recovered: whether finished recovering data from storage
 * @on_panic: whether panic is happening
 * @pstore_zone_info_lock: lock to @pstore_zone_info
 * @pstore_zone_info: information from backend
 * @pstore: structure for pstore
 */
struct psz_context {
	struct pstore_zone **kpszs;
	struct pstore_zone *ppsz;
	struct pstore_zone *cpsz;
	struct pstore_zone **fpszs;
	unsigned int kmsg_max_cnt;
	unsigned int kmsg_read_cnt;
	unsigned int kmsg_write_cnt;
	unsigned int pmsg_read_cnt;
	unsigned int console_read_cnt;
	unsigned int ftrace_max_cnt;
	unsigned int ftrace_read_cnt;
	/*
	 * These counters should be calculated during recovery.
	 * It records the oops/panic times after crashes rather than boots.
	 */
	unsigned int oops_counter;
	unsigned int panic_counter;
	atomic_t recovered;
	atomic_t on_panic;

	/*
	 * pstore_zone_info_lock protects this entire structure during calls
	 * to register_pstore_zone()/unregister_pstore_zone().
	 */
	struct mutex pstore_zone_info_lock;
	struct pstore_zone_info *pstore_zone_info;
	struct pstore_info pstore;
};
static struct psz_context pstore_zone_cxt;

static void psz_flush_all_dirty_zones(struct work_struct *);
static DECLARE_DELAYED_WORK(psz_cleaner, psz_flush_all_dirty_zones);

/**
 * enum psz_flush_mode - flush mode for psz_zone_write()
 *
 * @FLUSH_NONE: do not flush to storage but update data on memory
 * @FLUSH_PART: just flush part of data including meta data to storage
 * @FLUSH_META: just flush meta data of zone to storage
 * @FLUSH_ALL: flush all of zone
 */
enum psz_flush_mode {
	FLUSH_NONE = 0,
	FLUSH_PART,
	FLUSH_META,
	FLUSH_ALL,
};

static inline int buffer_datalen(struct pstore_zone *zone)
{
	return atomic_read(&zone->buffer->datalen);
}

static inline int buffer_start(struct pstore_zone *zone)
{
	return atomic_read(&zone->buffer->start);
}

static inline bool is_on_panic(void)
{
	return atomic_read(&pstore_zone_cxt.on_panic);
}

static ssize_t psz_zone_read_buffer(struct pstore_zone *zone, char *buf,
		size_t len, unsigned long off)
{
	if (!buf || !zone || !zone->buffer)
		return -EINVAL;
	if (off > zone->buffer_size)
		return -EINVAL;
	len = min_t(size_t, len, zone->buffer_size - off);
	memcpy(buf, zone->buffer->data + off, len);
	return len;
}

static int psz_zone_read_oldbuf(struct pstore_zone *zone, char *buf,
		size_t len, unsigned long off)
{
	if (!buf || !zone || !zone->oldbuf)
		return -EINVAL;
	if (off > zone->buffer_size)
		return -EINVAL;
	len = min_t(size_t, len, zone->buffer_size - off);
	memcpy(buf, zone->oldbuf->data + off, len);
	return 0;
}

static int psz_zone_write(struct pstore_zone *zone,
		enum psz_flush_mode flush_mode, const char *buf,
		size_t len, unsigned long off)
{
	struct pstore_zone_info *info = pstore_zone_cxt.pstore_zone_info;
	ssize_t wcnt = 0;
	ssize_t (*writeop)(const char *buf, size_t bytes, loff_t pos);
	size_t wlen;

	if (off > zone->buffer_size)
		return -EINVAL;

	wlen = min_t(size_t, len, zone->buffer_size - off);
	if (buf && wlen) {
		memcpy(zone->buffer->data + off, buf, wlen);
		atomic_set(&zone->buffer->datalen, wlen + off);
	}

	/* avoid damaging old records */
	if (!is_on_panic() && !atomic_read(&pstore_zone_cxt.recovered))
		goto dirty;

	writeop = is_on_panic() ? info->panic_write : info->write;
	if (!writeop)
		goto dirty;

	switch (flush_mode) {
	case FLUSH_NONE:
		if (unlikely(buf && wlen))
			goto dirty;
		return 0;
	case FLUSH_PART:
		wcnt = writeop((const char *)zone->buffer->data + off, wlen,
				zone->off + sizeof(*zone->buffer) + off);
		if (wcnt != wlen)
			goto dirty;
		fallthrough;
	case FLUSH_META:
		wlen = sizeof(struct psz_buffer);
		wcnt = writeop((const char *)zone->buffer, wlen, zone->off);
		if (wcnt != wlen)
			goto dirty;
		break;
	case FLUSH_ALL:
		wlen = zone->buffer_size + sizeof(*zone->buffer);
		wcnt = writeop((const char *)zone->buffer, wlen, zone->off);
		if (wcnt != wlen)
			goto dirty;
		break;
	}

	return 0;
dirty:
	/* no need to mark it dirty if going to try next zone */
	if (wcnt == -ENOMSG)
		return -ENOMSG;
	atomic_set(&zone->dirty, true);
	/* flush dirty zones nicely */
	if (wcnt == -EBUSY && !is_on_panic())
		schedule_delayed_work(&psz_cleaner, msecs_to_jiffies(500));
	return -EBUSY;
}

static int psz_flush_dirty_zone(struct pstore_zone *zone)
{
	int ret;

	if (unlikely(!zone))
		return -EINVAL;

	if (unlikely(!atomic_read(&pstore_zone_cxt.recovered)))
		return -EBUSY;

	if (!atomic_xchg(&zone->dirty, false))
		return 0;

	ret = psz_zone_write(zone, FLUSH_ALL, NULL, 0, 0);
	if (ret)
		atomic_set(&zone->dirty, true);
	return ret;
}

static int psz_flush_dirty_zones(struct pstore_zone **zones, unsigned int cnt)
{
	int i, ret;
	struct pstore_zone *zone;

	if (!zones)
		return -EINVAL;

	for (i = 0; i < cnt; i++) {
		zone = zones[i];
		if (!zone)
			return -EINVAL;
		ret = psz_flush_dirty_zone(zone);
		if (ret)
			return ret;
	}
	return 0;
}

static int psz_move_zone(struct pstore_zone *old, struct pstore_zone *new)
{
	const char *data = (const char *)old->buffer->data;
	int ret;

	ret = psz_zone_write(new, FLUSH_ALL, data, buffer_datalen(old), 0);
	if (ret) {
		atomic_set(&new->buffer->datalen, 0);
		atomic_set(&new->dirty, false);
		return ret;
	}
	atomic_set(&old->buffer->datalen, 0);
	return 0;
}

static void psz_flush_all_dirty_zones(struct work_struct *work)
{
	struct psz_context *cxt = &pstore_zone_cxt;
	int ret = 0;

	if (cxt->ppsz)
		ret |= psz_flush_dirty_zone(cxt->ppsz);
	if (cxt->cpsz)
		ret |= psz_flush_dirty_zone(cxt->cpsz);
	if (cxt->kpszs)
		ret |= psz_flush_dirty_zones(cxt->kpszs, cxt->kmsg_max_cnt);
	if (cxt->fpszs)
		ret |= psz_flush_dirty_zones(cxt->fpszs, cxt->ftrace_max_cnt);
	if (ret && cxt->pstore_zone_info)
		schedule_delayed_work(&psz_cleaner, msecs_to_jiffies(1000));
}

static int psz_kmsg_recover_data(struct psz_context *cxt)
{
	struct pstore_zone_info *info = cxt->pstore_zone_info;
	struct pstore_zone *zone = NULL;
	struct psz_buffer *buf;
	unsigned long i;
	ssize_t rcnt;

	if (!info->read)
		return -EINVAL;

	for (i = 0; i < cxt->kmsg_max_cnt; i++) {
		zone = cxt->kpszs[i];
		if (unlikely(!zone))
			return -EINVAL;
		if (atomic_read(&zone->dirty)) {
			unsigned int wcnt = cxt->kmsg_write_cnt;
			struct pstore_zone *new = cxt->kpszs[wcnt];
			int ret;

			ret = psz_move_zone(zone, new);
			if (ret) {
				pr_err("move zone from %lu to %d failed\n",
						i, wcnt);
				return ret;
			}
			cxt->kmsg_write_cnt = (wcnt + 1) % cxt->kmsg_max_cnt;
		}
		if (!zone->should_recover)
			continue;
		buf = zone->buffer;
		rcnt = info->read((char *)buf, zone->buffer_size + sizeof(*buf),
				zone->off);
		if (rcnt != zone->buffer_size + sizeof(*buf))
			return rcnt < 0 ? rcnt : -EIO;
	}
	return 0;
}

static int psz_kmsg_recover_meta(struct psz_context *cxt)
{
	struct pstore_zone_info *info = cxt->pstore_zone_info;
	struct pstore_zone *zone;
	ssize_t rcnt, len;
	struct psz_buffer *buf;
	struct psz_kmsg_header *hdr;
	struct timespec64 time = { };
	unsigned long i;
	/*
	 * Recover may happen on panic, we can't allocate any memory by kmalloc.
	 * So, we use local array instead.
	 */
	char buffer_header[sizeof(*buf) + sizeof(*hdr)] = {0};

	if (!info->read)
		return -EINVAL;

	len = sizeof(*buf) + sizeof(*hdr);
	buf = (struct psz_buffer *)buffer_header;
	for (i = 0; i < cxt->kmsg_max_cnt; i++) {
		zone = cxt->kpszs[i];
		if (unlikely(!zone))
			return -EINVAL;

		rcnt = info->read((char *)buf, len, zone->off);
		if (rcnt == -ENOMSG) {
			pr_debug("%s with id %lu may be broken, skip\n",
					zone->name, i);
			continue;
		} else if (rcnt != len) {
			pr_err("read %s with id %lu failed\n", zone->name, i);
			return rcnt < 0 ? rcnt : -EIO;
		}

		if (buf->sig != zone->buffer->sig) {
			pr_debug("no valid data in kmsg dump zone %lu\n", i);
			continue;
		}

		if (zone->buffer_size < atomic_read(&buf->datalen)) {
			pr_info("found overtop zone: %s: id %lu, off %lld, size %zu\n",
					zone->name, i, zone->off,
					zone->buffer_size);
			continue;
		}

		hdr = (struct psz_kmsg_header *)buf->data;
		if (hdr->magic != PSTORE_KMSG_HEADER_MAGIC) {
			pr_info("found invalid zone: %s: id %lu, off %lld, size %zu\n",
					zone->name, i, zone->off,
					zone->buffer_size);
			continue;
		}

		/*
		 * we get the newest zone, and the next one must be the oldest
		 * or unused zone, because we do write one by one like a circle.
		 */
		if (hdr->time.tv_sec >= time.tv_sec) {
			time.tv_sec = hdr->time.tv_sec;
			cxt->kmsg_write_cnt = (i + 1) % cxt->kmsg_max_cnt;
		}

		if (hdr->reason == KMSG_DUMP_OOPS)
			cxt->oops_counter =
				max(cxt->oops_counter, hdr->counter);
		else if (hdr->reason == KMSG_DUMP_PANIC)
			cxt->panic_counter =
				max(cxt->panic_counter, hdr->counter);

		if (!atomic_read(&buf->datalen)) {
			pr_debug("found erased zone: %s: id %lu, off %lld, size %zu, datalen %d\n",
					zone->name, i, zone->off,
					zone->buffer_size,
					atomic_read(&buf->datalen));
			continue;
		}

		if (!is_on_panic())
			zone->should_recover = true;
		pr_debug("found nice zone: %s: id %lu, off %lld, size %zu, datalen %d\n",
				zone->name, i, zone->off,
				zone->buffer_size, atomic_read(&buf->datalen));
	}

	return 0;
}

static int psz_kmsg_recover(struct psz_context *cxt)
{
	int ret;

	if (!cxt->kpszs)
		return 0;

	ret = psz_kmsg_recover_meta(cxt);
	if (ret)
		goto recover_fail;

	ret = psz_kmsg_recover_data(cxt);
	if (ret)
		goto recover_fail;

	return 0;
recover_fail:
	pr_debug("psz_recover_kmsg failed\n");
	return ret;
}

static int psz_recover_zone(struct psz_context *cxt, struct pstore_zone *zone)
{
	struct pstore_zone_info *info = cxt->pstore_zone_info;
	struct psz_buffer *oldbuf, tmpbuf;
	int ret = 0;
	char *buf;
	ssize_t rcnt, len, start, off;

	if (!zone || zone->oldbuf)
		return 0;

	if (is_on_panic()) {
		/* save data as much as possible */
		psz_flush_dirty_zone(zone);
		return 0;
	}

	if (unlikely(!info->read))
		return -EINVAL;

	len = sizeof(struct psz_buffer);
	rcnt = info->read((char *)&tmpbuf, len, zone->off);
	if (rcnt != len) {
		pr_debug("read zone %s failed\n", zone->name);
		return rcnt < 0 ? rcnt : -EIO;
	}

	if (tmpbuf.sig != zone->buffer->sig) {
		pr_debug("no valid data in zone %s\n", zone->name);
		return 0;
	}

	if (zone->buffer_size < atomic_read(&tmpbuf.datalen) ||
		zone->buffer_size < atomic_read(&tmpbuf.start)) {
		pr_info("found overtop zone: %s: off %lld, size %zu\n",
				zone->name, zone->off, zone->buffer_size);
		/* just keep going */
		return 0;
	}

	if (!atomic_read(&tmpbuf.datalen)) {
		pr_debug("found erased zone: %s: off %lld, size %zu, datalen %d\n",
				zone->name, zone->off, zone->buffer_size,
				atomic_read(&tmpbuf.datalen));
		return 0;
	}

	pr_debug("found nice zone: %s: off %lld, size %zu, datalen %d\n",
			zone->name, zone->off, zone->buffer_size,
			atomic_read(&tmpbuf.datalen));

	len = atomic_read(&tmpbuf.datalen) + sizeof(*oldbuf);
	oldbuf = kzalloc(len, GFP_KERNEL);
	if (!oldbuf)
		return -ENOMEM;

	memcpy(oldbuf, &tmpbuf, sizeof(*oldbuf));
	buf = (char *)oldbuf + sizeof(*oldbuf);
	len = atomic_read(&oldbuf->datalen);
	start = atomic_read(&oldbuf->start);
	off = zone->off + sizeof(*oldbuf);

	/* get part of data */
	rcnt = info->read(buf, len - start, off + start);
	if (rcnt != len - start) {
		pr_err("read zone %s failed\n", zone->name);
		ret = rcnt < 0 ? rcnt : -EIO;
		goto free_oldbuf;
	}

	/* get the rest of data */
	rcnt = info->read(buf + len - start, start, off);
	if (rcnt != start) {
		pr_err("read zone %s failed\n", zone->name);
		ret = rcnt < 0 ? rcnt : -EIO;
		goto free_oldbuf;
	}

	zone->oldbuf = oldbuf;
	psz_flush_dirty_zone(zone);
	return 0;

free_oldbuf:
	kfree(oldbuf);
	return ret;
}

static int psz_recover_zones(struct psz_context *cxt,
		struct pstore_zone **zones, unsigned int cnt)
{
	int ret;
	unsigned int i;
	struct pstore_zone *zone;

	if (!zones)
		return 0;

	for (i = 0; i < cnt; i++) {
		zone = zones[i];
		if (unlikely(!zone))
			continue;
		ret = psz_recover_zone(cxt, zone);
		if (ret)
			goto recover_fail;
	}

	return 0;
recover_fail:
	pr_debug("recover %s[%u] failed\n", zone->name, i);
	return ret;
}

/**
 * psz_recovery() - recover data from storage
 * @cxt: the context of pstore/zone
 *
 * recovery means reading data back from storage after rebooting
 *
 * Return: 0 on success, others on failure.
 */
static inline int psz_recovery(struct psz_context *cxt)
{
	int ret;

	if (atomic_read(&cxt->recovered))
		return 0;

	ret = psz_kmsg_recover(cxt);
	if (ret)
		goto out;

	ret = psz_recover_zone(cxt, cxt->ppsz);
	if (ret)
		goto out;

	ret = psz_recover_zone(cxt, cxt->cpsz);
	if (ret)
		goto out;

	ret = psz_recover_zones(cxt, cxt->fpszs, cxt->ftrace_max_cnt);

out:
	if (unlikely(ret))
		pr_err("recover failed\n");
	else {
		pr_debug("recover end!\n");
		atomic_set(&cxt->recovered, 1);
	}
	return ret;
}

static int psz_pstore_open(struct pstore_info *psi)
{
	struct psz_context *cxt = psi->data;

	cxt->kmsg_read_cnt = 0;
	cxt->pmsg_read_cnt = 0;
	cxt->console_read_cnt = 0;
	cxt->ftrace_read_cnt = 0;
	return 0;
}

static inline bool psz_old_ok(struct pstore_zone *zone)
{
	if (zone && zone->oldbuf && atomic_read(&zone->oldbuf->datalen))
		return true;
	return false;
}

static inline bool psz_ok(struct pstore_zone *zone)
{
	if (zone && zone->buffer && buffer_datalen(zone))
		return true;
	return false;
}

static inline int psz_kmsg_erase(struct psz_context *cxt,
		struct pstore_zone *zone, struct pstore_record *record)
{
	struct psz_buffer *buffer = zone->buffer;
	struct psz_kmsg_header *hdr =
		(struct psz_kmsg_header *)buffer->data;
	size_t size;

	if (unlikely(!psz_ok(zone)))
		return 0;

	/* this zone is already updated, no need to erase */
	if (record->count != hdr->counter)
		return 0;

	size = buffer_datalen(zone) + sizeof(*zone->buffer);
	atomic_set(&zone->buffer->datalen, 0);
	if (cxt->pstore_zone_info->erase)
		return cxt->pstore_zone_info->erase(size, zone->off);
	else
		return psz_zone_write(zone, FLUSH_META, NULL, 0, 0);
}

static inline int psz_record_erase(struct psz_context *cxt,
		struct pstore_zone *zone)
{
	if (unlikely(!psz_old_ok(zone)))
		return 0;

	kfree(zone->oldbuf);
	zone->oldbuf = NULL;
	/*
	 * if there are new data in zone buffer, that means the old data
	 * are already invalid. It is no need to flush 0 (erase) to
	 * block device.
	 */
	if (!buffer_datalen(zone))
		return psz_zone_write(zone, FLUSH_META, NULL, 0, 0);
	psz_flush_dirty_zone(zone);
	return 0;
}

static int psz_pstore_erase(struct pstore_record *record)
{
	struct psz_context *cxt = record->psi->data;

	switch (record->type) {
	case PSTORE_TYPE_DMESG:
		if (record->id >= cxt->kmsg_max_cnt)
			return -EINVAL;
		return psz_kmsg_erase(cxt, cxt->kpszs[record->id], record);
	case PSTORE_TYPE_PMSG:
		return psz_record_erase(cxt, cxt->ppsz);
	case PSTORE_TYPE_CONSOLE:
		return psz_record_erase(cxt, cxt->cpsz);
	case PSTORE_TYPE_FTRACE:
		if (record->id >= cxt->ftrace_max_cnt)
			return -EINVAL;
		return psz_record_erase(cxt, cxt->fpszs[record->id]);
	default: return -EINVAL;
	}
}

static void psz_write_kmsg_hdr(struct pstore_zone *zone,
		struct pstore_record *record)
{
	struct psz_context *cxt = record->psi->data;
	struct psz_buffer *buffer = zone->buffer;
	struct psz_kmsg_header *hdr =
		(struct psz_kmsg_header *)buffer->data;

	hdr->magic = PSTORE_KMSG_HEADER_MAGIC;
	hdr->compressed = record->compressed;
	hdr->time.tv_sec = record->time.tv_sec;
	hdr->time.tv_nsec = record->time.tv_nsec;
	hdr->reason = record->reason;
	if (hdr->reason == KMSG_DUMP_OOPS)
		hdr->counter = ++cxt->oops_counter;
	else if (hdr->reason == KMSG_DUMP_PANIC)
		hdr->counter = ++cxt->panic_counter;
	else
		hdr->counter = 0;
}

/*
 * In case zone is broken, which may occur to MTD device, we try each zones,
 * start at cxt->kmsg_write_cnt.
 */
static inline int notrace psz_kmsg_write_record(struct psz_context *cxt,
		struct pstore_record *record)
{
	size_t size, hlen;
	struct pstore_zone *zone;
	unsigned int i;

	for (i = 0; i < cxt->kmsg_max_cnt; i++) {
		unsigned int zonenum, len;
		int ret;

		zonenum = (cxt->kmsg_write_cnt + i) % cxt->kmsg_max_cnt;
		zone = cxt->kpszs[zonenum];
		if (unlikely(!zone))
			return -ENOSPC;

		/* avoid destroying old data, allocate a new one */
		len = zone->buffer_size + sizeof(*zone->buffer);
		zone->oldbuf = zone->buffer;
		zone->buffer = kzalloc(len, GFP_ATOMIC);
		if (!zone->buffer) {
			zone->buffer = zone->oldbuf;
			return -ENOMEM;
		}
		zone->buffer->sig = zone->oldbuf->sig;

		pr_debug("write %s to zone id %d\n", zone->name, zonenum);
		psz_write_kmsg_hdr(zone, record);
		hlen = sizeof(struct psz_kmsg_header);
		size = min_t(size_t, record->size, zone->buffer_size - hlen);
		ret = psz_zone_write(zone, FLUSH_ALL, record->buf, size, hlen);
		if (likely(!ret || ret != -ENOMSG)) {
			cxt->kmsg_write_cnt = zonenum + 1;
			cxt->kmsg_write_cnt %= cxt->kmsg_max_cnt;
			/* no need to try next zone, free last zone buffer */
			kfree(zone->oldbuf);
			zone->oldbuf = NULL;
			return ret;
		}

		pr_debug("zone %u may be broken, try next dmesg zone\n",
				zonenum);
		kfree(zone->buffer);
		zone->buffer = zone->oldbuf;
		zone->oldbuf = NULL;
	}

	return -EBUSY;
}

static int notrace psz_kmsg_write(struct psz_context *cxt,
		struct pstore_record *record)
{
	int ret;

	/*
	 * Explicitly only take the first part of any new crash.
	 * If our buffer is larger than kmsg_bytes, this can never happen,
	 * and if our buffer is smaller than kmsg_bytes, we don't want the
	 * report split across multiple records.
	 */
	if (record->part != 1)
		return -ENOSPC;

	if (!cxt->kpszs)
		return -ENOSPC;

	ret = psz_kmsg_write_record(cxt, record);
	if (!ret && is_on_panic()) {
		/* ensure all data are flushed to storage when panic */
		pr_debug("try to flush other dirty zones\n");
		psz_flush_all_dirty_zones(NULL);
	}

	/* always return 0 as we had handled it on buffer */
	return 0;
}

static int notrace psz_record_write(struct pstore_zone *zone,
		struct pstore_record *record)
{
	size_t start, rem;
	bool is_full_data = false;
	char *buf;
	int cnt;

	if (!zone || !record)
		return -ENOSPC;

	if (atomic_read(&zone->buffer->datalen) >= zone->buffer_size)
		is_full_data = true;

	cnt = record->size;
	buf = record->buf;
	if (unlikely(cnt > zone->buffer_size)) {
		buf += cnt - zone->buffer_size;
		cnt = zone->buffer_size;
	}

	start = buffer_start(zone);
	rem = zone->buffer_size - start;
	if (unlikely(rem < cnt)) {
		psz_zone_write(zone, FLUSH_PART, buf, rem, start);
		buf += rem;
		cnt -= rem;
		start = 0;
		is_full_data = true;
	}

	atomic_set(&zone->buffer->start, cnt + start);
	psz_zone_write(zone, FLUSH_PART, buf, cnt, start);

	/**
	 * psz_zone_write will set datalen as start + cnt.
	 * It works if actual data length is lesser than buffer size.
	 * If data length is greater than buffer size, pmsg will rewrite to
	 * the beginning of the zone, which makes buffer->datalen wrong.
	 * So we should reset datalen as buffer size once actual data length
	 * is greater than buffer size.
	 */
	if (is_full_data) {
		atomic_set(&zone->buffer->datalen, zone->buffer_size);
		psz_zone_write(zone, FLUSH_META, NULL, 0, 0);
	}
	return 0;
}

static int notrace psz_pstore_write(struct pstore_record *record)
{
	struct psz_context *cxt = record->psi->data;

	if (record->type == PSTORE_TYPE_DMESG &&
			record->reason == KMSG_DUMP_PANIC)
		atomic_set(&cxt->on_panic, 1);

	/*
	 * If on panic, do not write anything except panic records.
	 * Fix the case when panic_write prints log that wakes up
	 * console backend.
	 */
	if (is_on_panic() && record->type != PSTORE_TYPE_DMESG)
		return -EBUSY;

	switch (record->type) {
	case PSTORE_TYPE_DMESG:
		return psz_kmsg_write(cxt, record);
	case PSTORE_TYPE_CONSOLE:
		return psz_record_write(cxt->cpsz, record);
	case PSTORE_TYPE_PMSG:
		return psz_record_write(cxt->ppsz, record);
	case PSTORE_TYPE_FTRACE: {
		int zonenum = smp_processor_id();

		if (!cxt->fpszs)
			return -ENOSPC;
		return psz_record_write(cxt->fpszs[zonenum], record);
	}
	default:
		return -EINVAL;
	}
}

static struct pstore_zone *psz_read_next_zone(struct psz_context *cxt)
{
	struct pstore_zone *zone = NULL;

	while (cxt->kmsg_read_cnt < cxt->kmsg_max_cnt) {
		zone = cxt->kpszs[cxt->kmsg_read_cnt++];
		if (psz_ok(zone))
			return zone;
	}

	if (cxt->ftrace_read_cnt < cxt->ftrace_max_cnt)
		/*
		 * No need psz_old_ok(). Let psz_ftrace_read() do so for
		 * combination. psz_ftrace_read() should traverse over
		 * all zones in case of some zone without data.
		 */
		return cxt->fpszs[cxt->ftrace_read_cnt++];

	if (cxt->pmsg_read_cnt == 0) {
		cxt->pmsg_read_cnt++;
		zone = cxt->ppsz;
		if (psz_old_ok(zone))
			return zone;
	}

	if (cxt->console_read_cnt == 0) {
		cxt->console_read_cnt++;
		zone = cxt->cpsz;
		if (psz_old_ok(zone))
			return zone;
	}

	return NULL;
}

static int psz_kmsg_read_hdr(struct pstore_zone *zone,
		struct pstore_record *record)
{
	struct psz_buffer *buffer = zone->buffer;
	struct psz_kmsg_header *hdr =
		(struct psz_kmsg_header *)buffer->data;

	if (hdr->magic != PSTORE_KMSG_HEADER_MAGIC)
		return -EINVAL;
	record->compressed = hdr->compressed;
	record->time.tv_sec = hdr->time.tv_sec;
	record->time.tv_nsec = hdr->time.tv_nsec;
	record->reason = hdr->reason;
	record->count = hdr->counter;
	return 0;
}

static ssize_t psz_kmsg_read(struct pstore_zone *zone,
		struct pstore_record *record)
{
	ssize_t size, hlen = 0;

	size = buffer_datalen(zone);
	/* Clear and skip this kmsg dump record if it has no valid header */
	if (psz_kmsg_read_hdr(zone, record)) {
		atomic_set(&zone->buffer->datalen, 0);
		atomic_set(&zone->dirty, 0);
		return -ENOMSG;
	}
	size -= sizeof(struct psz_kmsg_header);

	if (!record->compressed) {
		char *buf = kasprintf(GFP_KERNEL, "%s: Total %d times\n",
				      kmsg_dump_reason_str(record->reason),
				      record->count);
		if (!buf)
			return -ENOMEM;
		hlen = strlen(buf);
		record->buf = krealloc(buf, hlen + size, GFP_KERNEL);
		if (!record->buf) {
			kfree(buf);
			return -ENOMEM;
		}
	} else {
		record->buf = kmalloc(size, GFP_KERNEL);
		if (!record->buf)
			return -ENOMEM;
	}

	size = psz_zone_read_buffer(zone, record->buf + hlen, size,
			sizeof(struct psz_kmsg_header));
	if (unlikely(size < 0)) {
		kfree(record->buf);
		return -ENOMSG;
	}

	return size + hlen;
}

/* try to combine all ftrace zones */
static ssize_t psz_ftrace_read(struct pstore_zone *zone,
		struct pstore_record *record)
{
	struct psz_context *cxt;
	struct psz_buffer *buf;
	int ret;

	if (!zone || !record)
		return -ENOSPC;

	if (!psz_old_ok(zone))
		goto out;

	buf = (struct psz_buffer *)zone->oldbuf;
	if (!buf)
		return -ENOMSG;

	ret = pstore_ftrace_combine_log(&record->buf, &record->size,
			(char *)buf->data, atomic_read(&buf->datalen));
	if (unlikely(ret))
		return ret;

out:
	cxt = record->psi->data;
	if (cxt->ftrace_read_cnt < cxt->ftrace_max_cnt)
		/* then, read next ftrace zone */
		return -ENOMSG;
	record->id = 0;
	return record->size ? record->size : -ENOMSG;
}

static ssize_t psz_record_read(struct pstore_zone *zone,
		struct pstore_record *record)
{
	size_t len;
	struct psz_buffer *buf;

	if (!zone || !record)
		return -ENOSPC;

	buf = (struct psz_buffer *)zone->oldbuf;
	if (!buf)
		return -ENOMSG;

	len = atomic_read(&buf->datalen);
	record->buf = kmalloc(len, GFP_KERNEL);
	if (!record->buf)
		return -ENOMEM;

	if (unlikely(psz_zone_read_oldbuf(zone, record->buf, len, 0))) {
		kfree(record->buf);
		return -ENOMSG;
	}

	return len;
}

static ssize_t psz_pstore_read(struct pstore_record *record)
{
	struct psz_context *cxt = record->psi->data;
	ssize_t (*readop)(struct pstore_zone *zone,
			struct pstore_record *record);
	struct pstore_zone *zone;
	ssize_t ret;

	/* before read, we must recover from storage */
	ret = psz_recovery(cxt);
	if (ret)
		return ret;

next_zone:
	zone = psz_read_next_zone(cxt);
	if (!zone)
		return 0;

	record->type = zone->type;
	switch (record->type) {
	case PSTORE_TYPE_DMESG:
		readop = psz_kmsg_read;
		record->id = cxt->kmsg_read_cnt - 1;
		break;
	case PSTORE_TYPE_FTRACE:
		readop = psz_ftrace_read;
		break;
	case PSTORE_TYPE_CONSOLE:
	case PSTORE_TYPE_PMSG:
		readop = psz_record_read;
		break;
	default:
		goto next_zone;
	}

	ret = readop(zone, record);
	if (ret == -ENOMSG)
		goto next_zone;
	return ret;
}

static struct psz_context pstore_zone_cxt = {
	.pstore_zone_info_lock =
		__MUTEX_INITIALIZER(pstore_zone_cxt.pstore_zone_info_lock),
	.recovered = ATOMIC_INIT(0),
	.on_panic = ATOMIC_INIT(0),
	.pstore = {
		.owner = THIS_MODULE,
		.open = psz_pstore_open,
		.read = psz_pstore_read,
		.write = psz_pstore_write,
		.erase = psz_pstore_erase,
	},
};

static void psz_free_zone(struct pstore_zone **pszone)
{
	struct pstore_zone *zone = *pszone;

	if (!zone)
		return;

	kfree(zone->buffer);
	kfree(zone);
	*pszone = NULL;
}

static void psz_free_zones(struct pstore_zone ***pszones, unsigned int *cnt)
{
	struct pstore_zone **zones = *pszones;

	if (!zones)
		return;

	while (*cnt > 0) {
		(*cnt)--;
		psz_free_zone(&(zones[*cnt]));
	}
	kfree(zones);
	*pszones = NULL;
}

static void psz_free_all_zones(struct psz_context *cxt)
{
	if (cxt->kpszs)
		psz_free_zones(&cxt->kpszs, &cxt->kmsg_max_cnt);
	if (cxt->ppsz)
		psz_free_zone(&cxt->ppsz);
	if (cxt->cpsz)
		psz_free_zone(&cxt->cpsz);
	if (cxt->fpszs)
		psz_free_zones(&cxt->fpszs, &cxt->ftrace_max_cnt);
}

static struct pstore_zone *psz_init_zone(enum pstore_type_id type,
		loff_t *off, size_t size)
{
	struct pstore_zone_info *info = pstore_zone_cxt.pstore_zone_info;
	struct pstore_zone *zone;
	const char *name = pstore_type_to_name(type);

	if (!size)
		return NULL;

	if (*off + size > info->total_size) {
		pr_err("no room for %s (0x%zx@0x%llx over 0x%lx)\n",
			name, size, *off, info->total_size);
		return ERR_PTR(-ENOMEM);
	}

	zone = kzalloc(sizeof(struct pstore_zone), GFP_KERNEL);
	if (!zone)
		return ERR_PTR(-ENOMEM);

	zone->buffer = kmalloc(size, GFP_KERNEL);
	if (!zone->buffer) {
		kfree(zone);
		return ERR_PTR(-ENOMEM);
	}
	memset(zone->buffer, 0xFF, size);
	zone->off = *off;
	zone->name = name;
	zone->type = type;
	zone->buffer_size = size - sizeof(struct psz_buffer);
	zone->buffer->sig = type ^ PSZ_SIG;
	zone->oldbuf = NULL;
	atomic_set(&zone->dirty, 0);
	atomic_set(&zone->buffer->datalen, 0);
	atomic_set(&zone->buffer->start, 0);

	*off += size;

	pr_debug("pszone %s: off 0x%llx, %zu header, %zu data\n", zone->name,
			zone->off, sizeof(*zone->buffer), zone->buffer_size);
	return zone;
}

static struct pstore_zone **psz_init_zones(enum pstore_type_id type,
	loff_t *off, size_t total_size, ssize_t record_size,
	unsigned int *cnt)
{
	struct pstore_zone_info *info = pstore_zone_cxt.pstore_zone_info;
	struct pstore_zone **zones, *zone;
	const char *name = pstore_type_to_name(type);
	int c, i;

	*cnt = 0;
	if (!total_size || !record_size)
		return NULL;

	if (*off + total_size > info->total_size) {
		pr_err("no room for zones %s (0x%zx@0x%llx over 0x%lx)\n",
			name, total_size, *off, info->total_size);
		return ERR_PTR(-ENOMEM);
	}

	c = total_size / record_size;
	if (unlikely(!c)) {
		pr_err("zone %s total_size too small\n", name);
		return ERR_PTR(-EINVAL);
	}

	zones = kcalloc(c, sizeof(*zones), GFP_KERNEL);
	if (!zones) {
		pr_err("allocate for zones %s failed\n", name);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < c; i++) {
		zone = psz_init_zone(type, off, record_size);
		if (!zone || IS_ERR(zone)) {
			pr_err("initialize zones %s failed\n", name);
			psz_free_zones(&zones, &i);
			return (void *)zone;
		}
		zones[i] = zone;
	}

	*cnt = c;
	return zones;
}

static int psz_alloc_zones(struct psz_context *cxt)
{
	struct pstore_zone_info *info = cxt->pstore_zone_info;
	loff_t off = 0;
	int err;
	size_t off_size = 0;

	off_size += info->pmsg_size;
	cxt->ppsz = psz_init_zone(PSTORE_TYPE_PMSG, &off, info->pmsg_size);
	if (IS_ERR(cxt->ppsz)) {
		err = PTR_ERR(cxt->ppsz);
		cxt->ppsz = NULL;
		goto free_out;
	}

	off_size += info->console_size;
	cxt->cpsz = psz_init_zone(PSTORE_TYPE_CONSOLE, &off,
			info->console_size);
	if (IS_ERR(cxt->cpsz)) {
		err = PTR_ERR(cxt->cpsz);
		cxt->cpsz = NULL;
		goto free_out;
	}

	off_size += info->ftrace_size;
	cxt->fpszs = psz_init_zones(PSTORE_TYPE_FTRACE, &off,
			info->ftrace_size,
			info->ftrace_size / nr_cpu_ids,
			&cxt->ftrace_max_cnt);
	if (IS_ERR(cxt->fpszs)) {
		err = PTR_ERR(cxt->fpszs);
		cxt->fpszs = NULL;
		goto free_out;
	}

	cxt->kpszs = psz_init_zones(PSTORE_TYPE_DMESG, &off,
			info->total_size - off_size,
			info->kmsg_size, &cxt->kmsg_max_cnt);
	if (IS_ERR(cxt->kpszs)) {
		err = PTR_ERR(cxt->kpszs);
		cxt->kpszs = NULL;
		goto free_out;
	}

	return 0;
free_out:
	psz_free_all_zones(cxt);
	return err;
}

/**
 * register_pstore_zone() - register to pstore/zone
 *
 * @info: back-end driver information. See &struct pstore_zone_info.
 *
 * Only one back-end at one time.
 *
 * Return: 0 on success, others on failure.
 */
int register_pstore_zone(struct pstore_zone_info *info)
{
	int err = -EINVAL;
	struct psz_context *cxt = &pstore_zone_cxt;

	if (info->total_size < 4096) {
		pr_warn("total_size must be >= 4096\n");
		return -EINVAL;
	}
	if (info->total_size > SZ_128M) {
		pr_warn("capping size to 128MiB\n");
		info->total_size = SZ_128M;
	}

	if (!info->kmsg_size && !info->pmsg_size && !info->console_size &&
	    !info->ftrace_size) {
		pr_warn("at least one record size must be non-zero\n");
		return -EINVAL;
	}

	if (!info->name || !info->name[0])
		return -EINVAL;

#define check_size(name, size) {					\
		if (info->name > 0 && info->name < (size)) {		\
			pr_err(#name " must be over %d\n", (size));	\
			return -EINVAL;					\
		}							\
		if (info->name & (size - 1)) {				\
			pr_err(#name " must be a multiple of %d\n",	\
					(size));			\
			return -EINVAL;					\
		}							\
	}

	check_size(total_size, 4096);
	check_size(kmsg_size, SECTOR_SIZE);
	check_size(pmsg_size, SECTOR_SIZE);
	check_size(console_size, SECTOR_SIZE);
	check_size(ftrace_size, SECTOR_SIZE);

#undef check_size

	/*
	 * the @read and @write must be applied.
	 * if no @read, pstore may mount failed.
	 * if no @write, pstore do not support to remove record file.
	 */
	if (!info->read || !info->write) {
		pr_err("no valid general read/write interface\n");
		return -EINVAL;
	}

	mutex_lock(&cxt->pstore_zone_info_lock);
	if (cxt->pstore_zone_info) {
		pr_warn("'%s' already loaded: ignoring '%s'\n",
				cxt->pstore_zone_info->name, info->name);
		mutex_unlock(&cxt->pstore_zone_info_lock);
		return -EBUSY;
	}
	cxt->pstore_zone_info = info;

	pr_debug("register %s with properties:\n", info->name);
	pr_debug("\ttotal size : %ld Bytes\n", info->total_size);
	pr_debug("\tkmsg size : %ld Bytes\n", info->kmsg_size);
	pr_debug("\tpmsg size : %ld Bytes\n", info->pmsg_size);
	pr_debug("\tconsole size : %ld Bytes\n", info->console_size);
	pr_debug("\tftrace size : %ld Bytes\n", info->ftrace_size);

	err = psz_alloc_zones(cxt);
	if (err) {
		pr_err("alloc zones failed\n");
		goto fail_out;
	}

	if (info->kmsg_size) {
		cxt->pstore.bufsize = cxt->kpszs[0]->buffer_size -
			sizeof(struct psz_kmsg_header);
		cxt->pstore.buf = kzalloc(cxt->pstore.bufsize, GFP_KERNEL);
		if (!cxt->pstore.buf) {
			err = -ENOMEM;
			goto fail_free;
		}
	}
	cxt->pstore.data = cxt;

	pr_info("registered %s as backend for", info->name);
	cxt->pstore.max_reason = info->max_reason;
	cxt->pstore.name = info->name;
	if (info->kmsg_size) {
		cxt->pstore.flags |= PSTORE_FLAGS_DMESG;
		pr_cont(" kmsg(%s",
			kmsg_dump_reason_str(cxt->pstore.max_reason));
		if (cxt->pstore_zone_info->panic_write)
			pr_cont(",panic_write");
		pr_cont(")");
	}
	if (info->pmsg_size) {
		cxt->pstore.flags |= PSTORE_FLAGS_PMSG;
		pr_cont(" pmsg");
	}
	if (info->console_size) {
		cxt->pstore.flags |= PSTORE_FLAGS_CONSOLE;
		pr_cont(" console");
	}
	if (info->ftrace_size) {
		cxt->pstore.flags |= PSTORE_FLAGS_FTRACE;
		pr_cont(" ftrace");
	}
	pr_cont("\n");

	err = pstore_register(&cxt->pstore);
	if (err) {
		pr_err("registering with pstore failed\n");
		goto fail_free;
	}
	mutex_unlock(&pstore_zone_cxt.pstore_zone_info_lock);

	return 0;

fail_free:
	kfree(cxt->pstore.buf);
	cxt->pstore.buf = NULL;
	cxt->pstore.bufsize = 0;
	psz_free_all_zones(cxt);
fail_out:
	pstore_zone_cxt.pstore_zone_info = NULL;
	mutex_unlock(&pstore_zone_cxt.pstore_zone_info_lock);
	return err;
}
EXPORT_SYMBOL_GPL(register_pstore_zone);

/**
 * unregister_pstore_zone() - unregister to pstore/zone
 *
 * @info: back-end driver information. See struct pstore_zone_info.
 */
void unregister_pstore_zone(struct pstore_zone_info *info)
{
	struct psz_context *cxt = &pstore_zone_cxt;

	mutex_lock(&cxt->pstore_zone_info_lock);
	if (!cxt->pstore_zone_info) {
		mutex_unlock(&cxt->pstore_zone_info_lock);
		return;
	}

	/* Stop incoming writes from pstore. */
	pstore_unregister(&cxt->pstore);

	/* Flush any pending writes. */
	psz_flush_all_dirty_zones(NULL);
	flush_delayed_work(&psz_cleaner);

	/* Clean up allocations. */
	kfree(cxt->pstore.buf);
	cxt->pstore.buf = NULL;
	cxt->pstore.bufsize = 0;
	cxt->pstore_zone_info = NULL;

	psz_free_all_zones(cxt);

	/* Clear counters and zone state. */
	cxt->oops_counter = 0;
	cxt->panic_counter = 0;
	atomic_set(&cxt->recovered, 0);
	atomic_set(&cxt->on_panic, 0);

	mutex_unlock(&cxt->pstore_zone_info_lock);
}
EXPORT_SYMBOL_GPL(unregister_pstore_zone);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WeiXiong Liao <liaoweixiong@allwinnertech.com>");
MODULE_AUTHOR("Kees Cook <keescook@chromium.org>");
MODULE_DESCRIPTION("Storage Manager for pstore/blk");
