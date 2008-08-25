/*
 *  fs/partitions/check.c
 *
 *  Code extracted from drivers/block/genhd.c
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 *
 *  We now have independent partition support from the
 *  block drivers, which allows all the partition code to
 *  be grouped in one location, and it to be mostly self
 *  contained.
 *
 *  Added needed MAJORS for new pairs, {hdi,hdj}, {hdk,hdl}
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kmod.h>
#include <linux/ctype.h>
#include <linux/genhd.h>

#include "check.h"

#include "acorn.h"
#include "amiga.h"
#include "atari.h"
#include "ldm.h"
#include "mac.h"
#include "msdos.h"
#include "osf.h"
#include "sgi.h"
#include "sun.h"
#include "ibm.h"
#include "ultrix.h"
#include "efi.h"
#include "karma.h"
#include "sysv68.h"

#ifdef CONFIG_BLK_DEV_MD
extern void md_autodetect_dev(dev_t dev);
#endif

int warn_no_part = 1; /*This is ugly: should make genhd removable media aware*/

static int (*check_part[])(struct parsed_partitions *, struct block_device *) = {
	/*
	 * Probe partition formats with tables at disk address 0
	 * that also have an ADFS boot block at 0xdc0.
	 */
#ifdef CONFIG_ACORN_PARTITION_ICS
	adfspart_check_ICS,
#endif
#ifdef CONFIG_ACORN_PARTITION_POWERTEC
	adfspart_check_POWERTEC,
#endif
#ifdef CONFIG_ACORN_PARTITION_EESOX
	adfspart_check_EESOX,
#endif

	/*
	 * Now move on to formats that only have partition info at
	 * disk address 0xdc0.  Since these may also have stale
	 * PC/BIOS partition tables, they need to come before
	 * the msdos entry.
	 */
#ifdef CONFIG_ACORN_PARTITION_CUMANA
	adfspart_check_CUMANA,
#endif
#ifdef CONFIG_ACORN_PARTITION_ADFS
	adfspart_check_ADFS,
#endif

#ifdef CONFIG_EFI_PARTITION
	efi_partition,		/* this must come before msdos */
#endif
#ifdef CONFIG_SGI_PARTITION
	sgi_partition,
#endif
#ifdef CONFIG_LDM_PARTITION
	ldm_partition,		/* this must come before msdos */
#endif
#ifdef CONFIG_MSDOS_PARTITION
	msdos_partition,
#endif
#ifdef CONFIG_OSF_PARTITION
	osf_partition,
#endif
#ifdef CONFIG_SUN_PARTITION
	sun_partition,
#endif
#ifdef CONFIG_AMIGA_PARTITION
	amiga_partition,
#endif
#ifdef CONFIG_ATARI_PARTITION
	atari_partition,
#endif
#ifdef CONFIG_MAC_PARTITION
	mac_partition,
#endif
#ifdef CONFIG_ULTRIX_PARTITION
	ultrix_partition,
#endif
#ifdef CONFIG_IBM_PARTITION
	ibm_partition,
#endif
#ifdef CONFIG_KARMA_PARTITION
	karma_partition,
#endif
#ifdef CONFIG_SYSV68_PARTITION
	sysv68_partition,
#endif
	NULL
};
 
/*
 * disk_name() is used by partition check code and the genhd driver.
 * It formats the devicename of the indicated disk into
 * the supplied buffer (of size at least 32), and returns
 * a pointer to that same buffer (for convenience).
 */

char *disk_name(struct gendisk *hd, int partno, char *buf)
{
	if (!partno)
		snprintf(buf, BDEVNAME_SIZE, "%s", hd->disk_name);
	else if (isdigit(hd->disk_name[strlen(hd->disk_name)-1]))
		snprintf(buf, BDEVNAME_SIZE, "%sp%d", hd->disk_name, partno);
	else
		snprintf(buf, BDEVNAME_SIZE, "%s%d", hd->disk_name, partno);

	return buf;
}

const char *bdevname(struct block_device *bdev, char *buf)
{
	int partno = 0;

	if (bdev->bd_part)
		partno = bdev->bd_part->partno;

	return disk_name(bdev->bd_disk, partno, buf);
}

EXPORT_SYMBOL(bdevname);

/*
 * There's very little reason to use this, you should really
 * have a struct block_device just about everywhere and use
 * bdevname() instead.
 */
const char *__bdevname(dev_t dev, char *buffer)
{
	scnprintf(buffer, BDEVNAME_SIZE, "unknown-block(%u,%u)",
				MAJOR(dev), MINOR(dev));
	return buffer;
}

EXPORT_SYMBOL(__bdevname);

static struct parsed_partitions *
check_partition(struct gendisk *hd, struct block_device *bdev)
{
	struct parsed_partitions *state;
	int i, res, err;

	state = kmalloc(sizeof(struct parsed_partitions), GFP_KERNEL);
	if (!state)
		return NULL;

	disk_name(hd, 0, state->name);
	printk(KERN_INFO " %s:", state->name);
	if (isdigit(state->name[strlen(state->name)-1]))
		sprintf(state->name, "p");

	state->limit = disk_max_parts(hd) + 1;
	i = res = err = 0;
	while (!res && check_part[i]) {
		memset(&state->parts, 0, sizeof(state->parts));
		res = check_part[i++](state, bdev);
		if (res < 0) {
			/* We have hit an I/O error which we don't report now.
		 	* But record it, and let the others do their job.
		 	*/
			err = res;
			res = 0;
		}

	}
	if (res > 0)
		return state;
	if (err)
	/* The partition is unrecognized. So report I/O errors if there were any */
		res = err;
	if (!res)
		printk(" unknown partition table\n");
	else if (warn_no_part)
		printk(" unable to read partition table\n");
	kfree(state);
	return ERR_PTR(res);
}

static ssize_t part_start_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct hd_struct *p = dev_to_part(dev);

	return sprintf(buf, "%llu\n",(unsigned long long)p->start_sect);
}

static ssize_t part_size_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct hd_struct *p = dev_to_part(dev);
	return sprintf(buf, "%llu\n",(unsigned long long)p->nr_sects);
}

static ssize_t part_stat_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct hd_struct *p = dev_to_part(dev);
	int cpu;

	cpu = disk_stat_lock();
	part_round_stats(cpu, p);
	disk_stat_unlock();
	return sprintf(buf,
		"%8lu %8lu %8llu %8u "
		"%8lu %8lu %8llu %8u "
		"%8u %8u %8u"
		"\n",
		part_stat_read(p, ios[READ]),
		part_stat_read(p, merges[READ]),
		(unsigned long long)part_stat_read(p, sectors[READ]),
		jiffies_to_msecs(part_stat_read(p, ticks[READ])),
		part_stat_read(p, ios[WRITE]),
		part_stat_read(p, merges[WRITE]),
		(unsigned long long)part_stat_read(p, sectors[WRITE]),
		jiffies_to_msecs(part_stat_read(p, ticks[WRITE])),
		p->in_flight,
		jiffies_to_msecs(part_stat_read(p, io_ticks)),
		jiffies_to_msecs(part_stat_read(p, time_in_queue)));
}

#ifdef CONFIG_FAIL_MAKE_REQUEST
static ssize_t part_fail_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct hd_struct *p = dev_to_part(dev);

	return sprintf(buf, "%d\n", p->make_it_fail);
}

static ssize_t part_fail_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct hd_struct *p = dev_to_part(dev);
	int i;

	if (count > 0 && sscanf(buf, "%d", &i) > 0)
		p->make_it_fail = (i == 0) ? 0 : 1;

	return count;
}
#endif

static DEVICE_ATTR(start, S_IRUGO, part_start_show, NULL);
static DEVICE_ATTR(size, S_IRUGO, part_size_show, NULL);
static DEVICE_ATTR(stat, S_IRUGO, part_stat_show, NULL);
#ifdef CONFIG_FAIL_MAKE_REQUEST
static struct device_attribute dev_attr_fail =
	__ATTR(make-it-fail, S_IRUGO|S_IWUSR, part_fail_show, part_fail_store);
#endif

static struct attribute *part_attrs[] = {
	&dev_attr_start.attr,
	&dev_attr_size.attr,
	&dev_attr_stat.attr,
#ifdef CONFIG_FAIL_MAKE_REQUEST
	&dev_attr_fail.attr,
#endif
	NULL
};

static struct attribute_group part_attr_group = {
	.attrs = part_attrs,
};

static struct attribute_group *part_attr_groups[] = {
	&part_attr_group,
	NULL
};

static void part_release(struct device *dev)
{
	struct hd_struct *p = dev_to_part(dev);
	free_part_stats(p);
	kfree(p);
}

struct device_type part_type = {
	.name		= "partition",
	.groups		= part_attr_groups,
	.release	= part_release,
};

static inline void disk_sysfs_add_subdirs(struct gendisk *disk)
{
	struct kobject *k;

	k = kobject_get(&disk_to_dev(disk)->kobj);
	disk->holder_dir = kobject_create_and_add("holders", k);
	disk->slave_dir = kobject_create_and_add("slaves", k);
	kobject_put(k);
}

static void delete_partition_rcu_cb(struct rcu_head *head)
{
	struct hd_struct *part = container_of(head, struct hd_struct, rcu_head);

	part->start_sect = 0;
	part->nr_sects = 0;
	part_stat_set_all(part, 0);
	put_device(part_to_dev(part));
}

void delete_partition(struct gendisk *disk, int partno)
{
	struct hd_struct *part;

	part = disk->__part[partno-1];
	if (!part)
		return;

	blk_free_devt(part_devt(part));
	rcu_assign_pointer(disk->__part[partno-1], NULL);
	kobject_put(part->holder_dir);
	device_del(part_to_dev(part));

	call_rcu(&part->rcu_head, delete_partition_rcu_cb);
}

static ssize_t whole_disk_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return 0;
}
static DEVICE_ATTR(whole_disk, S_IRUSR | S_IRGRP | S_IROTH,
		   whole_disk_show, NULL);

int add_partition(struct gendisk *disk, int partno,
		  sector_t start, sector_t len, int flags)
{
	struct hd_struct *p;
	dev_t devt = MKDEV(0, 0);
	struct device *ddev = disk_to_dev(disk);
	struct device *pdev;
	const char *dname;
	int err;

	if (disk->__part[partno - 1])
		return -EBUSY;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	if (!init_part_stats(p)) {
		err = -ENOMEM;
		goto out_free;
	}
	pdev = part_to_dev(p);

	p->start_sect = start;
	p->nr_sects = len;
	p->partno = partno;
	p->policy = disk->policy;

	dname = dev_name(ddev);
	if (isdigit(dname[strlen(dname) - 1]))
		snprintf(pdev->bus_id, BUS_ID_SIZE, "%sp%d", dname, partno);
	else
		snprintf(pdev->bus_id, BUS_ID_SIZE, "%s%d", dname, partno);

	device_initialize(pdev);
	pdev->class = &block_class;
	pdev->type = &part_type;
	pdev->parent = ddev;

	err = blk_alloc_devt(p, &devt);
	if (err)
		goto out_free;
	pdev->devt = devt;

	/* delay uevent until 'holders' subdir is created */
	pdev->uevent_suppress = 1;
	err = device_add(pdev);
	if (err)
		goto out_put;

	err = -ENOMEM;
	p->holder_dir = kobject_create_and_add("holders", &pdev->kobj);
	if (!p->holder_dir)
		goto out_del;

	pdev->uevent_suppress = 0;
	if (flags & ADDPART_FLAG_WHOLEDISK) {
		err = device_create_file(pdev, &dev_attr_whole_disk);
		if (err)
			goto out_del;
	}

	/* everything is up and running, commence */
	INIT_RCU_HEAD(&p->rcu_head);
	rcu_assign_pointer(disk->__part[partno - 1], p);

	/* suppress uevent if the disk supresses it */
	if (!ddev->uevent_suppress)
		kobject_uevent(&pdev->kobj, KOBJ_ADD);

	return 0;

out_free:
	kfree(p);
	return err;
out_del:
	kobject_put(p->holder_dir);
	device_del(pdev);
out_put:
	put_device(pdev);
	blk_free_devt(devt);
	return err;
}

/* Not exported, helper to add_disk(). */
void register_disk(struct gendisk *disk)
{
	struct device *ddev = disk_to_dev(disk);
	struct block_device *bdev;
	struct disk_part_iter piter;
	struct hd_struct *part;
	char *s;
	int err;

	ddev->parent = disk->driverfs_dev;

	strlcpy(ddev->bus_id, disk->disk_name, BUS_ID_SIZE);
	/* ewww... some of these buggers have / in the name... */
	s = strchr(ddev->bus_id, '/');
	if (s)
		*s = '!';

	/* delay uevents, until we scanned partition table */
	ddev->uevent_suppress = 1;

	if (device_add(ddev))
		return;
#ifndef CONFIG_SYSFS_DEPRECATED
	err = sysfs_create_link(block_depr, &ddev->kobj,
				kobject_name(&ddev->kobj));
	if (err) {
		device_del(ddev);
		return;
	}
#endif
	disk_sysfs_add_subdirs(disk);

	/* No minors to use for partitions */
	if (!disk_max_parts(disk))
		goto exit;

	/* No such device (e.g., media were just removed) */
	if (!get_capacity(disk))
		goto exit;

	bdev = bdget_disk(disk, 0);
	if (!bdev)
		goto exit;

	bdev->bd_invalidated = 1;
	err = blkdev_get(bdev, FMODE_READ, 0);
	if (err < 0)
		goto exit;
	blkdev_put(bdev);

exit:
	/* announce disk after possible partitions are created */
	ddev->uevent_suppress = 0;
	kobject_uevent(&ddev->kobj, KOBJ_ADD);

	/* announce possible partitions */
	disk_part_iter_init(&piter, disk, 0);
	while ((part = disk_part_iter_next(&piter)))
		kobject_uevent(&part_to_dev(part)->kobj, KOBJ_ADD);
	disk_part_iter_exit(&piter);
}

int rescan_partitions(struct gendisk *disk, struct block_device *bdev)
{
	struct disk_part_iter piter;
	struct hd_struct *part;
	struct parsed_partitions *state;
	int p, res;

	if (bdev->bd_part_count)
		return -EBUSY;
	res = invalidate_partition(disk, 0);
	if (res)
		return res;
	bdev->bd_invalidated = 0;

	disk_part_iter_init(&piter, disk, DISK_PITER_INCL_EMPTY);
	while ((part = disk_part_iter_next(&piter)))
		delete_partition(disk, part->partno);
	disk_part_iter_exit(&piter);

	if (disk->fops->revalidate_disk)
		disk->fops->revalidate_disk(disk);
	if (!get_capacity(disk) || !(state = check_partition(disk, bdev)))
		return 0;
	if (IS_ERR(state))	/* I/O error reading the partition table */
		return -EIO;

	/* tell userspace that the media / partition table may have changed */
	kobject_uevent(&disk_to_dev(disk)->kobj, KOBJ_CHANGE);

	for (p = 1; p < state->limit; p++) {
		sector_t size = state->parts[p].size;
		sector_t from = state->parts[p].from;
		if (!size)
			continue;
		if (from + size > get_capacity(disk)) {
			printk(KERN_WARNING
				"%s: p%d exceeds device capacity\n",
				disk->disk_name, p);
		}
		res = add_partition(disk, p, from, size, state->parts[p].flags);
		if (res) {
			printk(KERN_ERR " %s: p%d could not be added: %d\n",
				disk->disk_name, p, -res);
			continue;
		}
#ifdef CONFIG_BLK_DEV_MD
		if (state->parts[p].flags & ADDPART_FLAG_RAID)
			md_autodetect_dev(bdev->bd_dev+p);
#endif
	}
	kfree(state);
	return 0;
}

unsigned char *read_dev_sector(struct block_device *bdev, sector_t n, Sector *p)
{
	struct address_space *mapping = bdev->bd_inode->i_mapping;
	struct page *page;

	page = read_mapping_page(mapping, (pgoff_t)(n >> (PAGE_CACHE_SHIFT-9)),
				 NULL);
	if (!IS_ERR(page)) {
		if (PageError(page))
			goto fail;
		p->v = page;
		return (unsigned char *)page_address(page) +  ((n & ((1 << (PAGE_CACHE_SHIFT - 9)) - 1)) << 9);
fail:
		page_cache_release(page);
	}
	p->v = NULL;
	return NULL;
}

EXPORT_SYMBOL(read_dev_sector);

void del_gendisk(struct gendisk *disk)
{
	struct disk_part_iter piter;
	struct hd_struct *part;

	/* invalidate stuff */
	disk_part_iter_init(&piter, disk,
			     DISK_PITER_INCL_EMPTY | DISK_PITER_REVERSE);
	while ((part = disk_part_iter_next(&piter))) {
		invalidate_partition(disk, part->partno);
		delete_partition(disk, part->partno);
	}
	disk_part_iter_exit(&piter);

	invalidate_partition(disk, 0);
	disk->capacity = 0;
	disk->flags &= ~GENHD_FL_UP;
	unlink_gendisk(disk);
	disk_stat_set_all(disk, 0);
	disk->stamp = 0;

	kobject_put(disk->holder_dir);
	kobject_put(disk->slave_dir);
	disk->driverfs_dev = NULL;
#ifndef CONFIG_SYSFS_DEPRECATED
	sysfs_remove_link(block_depr, dev_name(disk_to_dev(disk)));
#endif
	device_del(disk_to_dev(disk));
}
