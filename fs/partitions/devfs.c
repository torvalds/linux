/*
 * This tries to keep block devices away from devfs as much as possible.
 */
#include <linux/fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/bitops.h>
#include <asm/semaphore.h>


struct unique_numspace {
	u32		  num_free;          /*  Num free in bits       */
	u32		  length;            /*  Array length in bytes  */
	unsigned long	  *bits;
	struct semaphore  mutex;
};

static DECLARE_MUTEX(numspace_mutex);

static int expand_numspace(struct unique_numspace *s)
{
	u32 length;
	void *bits;

	if (s->length < 16)
		length = 16;
	else
		length = s->length << 1;

	bits = vmalloc(length);
	if (!bits)
		return -ENOMEM;
	if (s->bits) {
		memcpy(bits, s->bits, s->length);
		vfree(s->bits);
	}
		
	s->num_free = (length - s->length) << 3;
	s->bits = bits;
	memset(bits + s->length, 0, length - s->length);
	s->length = length;

	return 0;
}

static int alloc_unique_number(struct unique_numspace *s)
{
	int rval = 0;

	down(&numspace_mutex);
	if (s->num_free < 1)
		rval = expand_numspace(s);
	if (!rval) {
		rval = find_first_zero_bit(s->bits, s->length << 3);
		--s->num_free;
		__set_bit(rval, s->bits);
	}
	up(&numspace_mutex);

	return rval;
}

static void dealloc_unique_number(struct unique_numspace *s, int number)
{
	int old_val;

	if (number >= 0) {
		down(&numspace_mutex);
		old_val = __test_and_clear_bit(number, s->bits);
		if (old_val)
			++s->num_free;
		up(&numspace_mutex);
	}
}

static struct unique_numspace disc_numspace;
static struct unique_numspace cdrom_numspace;

void devfs_add_partitioned(struct gendisk *disk)
{
	char dirname[64], symlink[16];

	devfs_mk_dir(disk->devfs_name);
	devfs_mk_bdev(MKDEV(disk->major, disk->first_minor),
			S_IFBLK|S_IRUSR|S_IWUSR,
			"%s/disc", disk->devfs_name);

	disk->number = alloc_unique_number(&disc_numspace);

	sprintf(symlink, "discs/disc%d", disk->number);
	sprintf(dirname, "../%s", disk->devfs_name);
	devfs_mk_symlink(symlink, dirname);

}

void devfs_add_disk(struct gendisk *disk)
{
	devfs_mk_bdev(MKDEV(disk->major, disk->first_minor),
			(disk->flags & GENHD_FL_CD) ?
				S_IFBLK|S_IRUGO|S_IWUGO :
				S_IFBLK|S_IRUSR|S_IWUSR,
			"%s", disk->devfs_name);

	if (disk->flags & GENHD_FL_CD) {
		char dirname[64], symlink[16];

		disk->number = alloc_unique_number(&cdrom_numspace);

		sprintf(symlink, "cdroms/cdrom%d", disk->number);
		sprintf(dirname, "../%s", disk->devfs_name);
		devfs_mk_symlink(symlink, dirname);
	}
}

void devfs_remove_disk(struct gendisk *disk)
{
	if (disk->minors != 1) {
		devfs_remove("discs/disc%d", disk->number);
		dealloc_unique_number(&disc_numspace, disk->number);
		devfs_remove("%s/disc", disk->devfs_name);
	}
	if (disk->flags & GENHD_FL_CD) {
		devfs_remove("cdroms/cdrom%d", disk->number);
		dealloc_unique_number(&cdrom_numspace, disk->number);
	}
	devfs_remove(disk->devfs_name);
}


