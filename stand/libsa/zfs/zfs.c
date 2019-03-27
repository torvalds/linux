/*-
 * Copyright (c) 2007 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 *	Stand-alone file reading package.
 */

#include <stand.h>
#include <sys/disk.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <disk.h>
#include <part.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <bootstrap.h>

#include "libzfs.h"

#include "zfsimpl.c"

/* Define the range of indexes to be populated with ZFS Boot Environments */
#define		ZFS_BE_FIRST	4
#define		ZFS_BE_LAST	8

static int	zfs_open(const char *path, struct open_file *f);
static int	zfs_close(struct open_file *f);
static int	zfs_read(struct open_file *f, void *buf, size_t size, size_t *resid);
static off_t	zfs_seek(struct open_file *f, off_t offset, int where);
static int	zfs_stat(struct open_file *f, struct stat *sb);
static int	zfs_readdir(struct open_file *f, struct dirent *d);

static void	zfs_bootenv_initial(const char *);

struct devsw zfs_dev;

struct fs_ops zfs_fsops = {
	"zfs",
	zfs_open,
	zfs_close,
	zfs_read,
	null_write,
	zfs_seek,
	zfs_stat,
	zfs_readdir
};

/*
 * In-core open file.
 */
struct file {
	off_t		f_seekp;	/* seek pointer */
	dnode_phys_t	f_dnode;
	uint64_t	f_zap_type;	/* zap type for readdir */
	uint64_t	f_num_leafs;	/* number of fzap leaf blocks */
	zap_leaf_phys_t	*f_zap_leaf;	/* zap leaf buffer */
};

static int	zfs_env_index;
static int	zfs_env_count;

SLIST_HEAD(zfs_be_list, zfs_be_entry) zfs_be_head = SLIST_HEAD_INITIALIZER(zfs_be_head);
struct zfs_be_list *zfs_be_headp;
struct zfs_be_entry {
	const char *name;
	SLIST_ENTRY(zfs_be_entry) entries;
} *zfs_be, *zfs_be_tmp;

/*
 * Open a file.
 */
static int
zfs_open(const char *upath, struct open_file *f)
{
	struct zfsmount *mount = (struct zfsmount *)f->f_devdata;
	struct file *fp;
	int rc;

	if (f->f_dev != &zfs_dev)
		return (EINVAL);

	/* allocate file system specific data structure */
	fp = malloc(sizeof(struct file));
	bzero(fp, sizeof(struct file));
	f->f_fsdata = (void *)fp;

	rc = zfs_lookup(mount, upath, &fp->f_dnode);
	fp->f_seekp = 0;
	if (rc) {
		f->f_fsdata = NULL;
		free(fp);
	}
	return (rc);
}

static int
zfs_close(struct open_file *f)
{
	struct file *fp = (struct file *)f->f_fsdata;

	dnode_cache_obj = NULL;
	f->f_fsdata = (void *)0;
	if (fp == (struct file *)0)
		return (0);

	free(fp);
	return (0);
}

/*
 * Copy a portion of a file into kernel memory.
 * Cross block boundaries when necessary.
 */
static int
zfs_read(struct open_file *f, void *start, size_t size, size_t *resid	/* out */)
{
	const spa_t *spa = ((struct zfsmount *)f->f_devdata)->spa;
	struct file *fp = (struct file *)f->f_fsdata;
	struct stat sb;
	size_t n;
	int rc;

	rc = zfs_stat(f, &sb);
	if (rc)
		return (rc);
	n = size;
	if (fp->f_seekp + n > sb.st_size)
		n = sb.st_size - fp->f_seekp;

	rc = dnode_read(spa, &fp->f_dnode, fp->f_seekp, start, n);
	if (rc)
		return (rc);

	if (0) {
	    int i;
	    for (i = 0; i < n; i++)
		putchar(((char*) start)[i]);
	}
	fp->f_seekp += n;
	if (resid)
		*resid = size - n;

	return (0);
}

static off_t
zfs_seek(struct open_file *f, off_t offset, int where)
{
	struct file *fp = (struct file *)f->f_fsdata;

	switch (where) {
	case SEEK_SET:
		fp->f_seekp = offset;
		break;
	case SEEK_CUR:
		fp->f_seekp += offset;
		break;
	case SEEK_END:
	    {
		struct stat sb;
		int error;

		error = zfs_stat(f, &sb);
		if (error != 0) {
			errno = error;
			return (-1);
		}
		fp->f_seekp = sb.st_size - offset;
		break;
	    }
	default:
		errno = EINVAL;
		return (-1);
	}
	return (fp->f_seekp);
}

static int
zfs_stat(struct open_file *f, struct stat *sb)
{
	const spa_t *spa = ((struct zfsmount *)f->f_devdata)->spa;
	struct file *fp = (struct file *)f->f_fsdata;

	return (zfs_dnode_stat(spa, &fp->f_dnode, sb));
}

static int
zfs_readdir(struct open_file *f, struct dirent *d)
{
	const spa_t *spa = ((struct zfsmount *)f->f_devdata)->spa;
	struct file *fp = (struct file *)f->f_fsdata;
	mzap_ent_phys_t mze;
	struct stat sb;
	size_t bsize = fp->f_dnode.dn_datablkszsec << SPA_MINBLOCKSHIFT;
	int rc;

	rc = zfs_stat(f, &sb);
	if (rc)
		return (rc);
	if (!S_ISDIR(sb.st_mode))
		return (ENOTDIR);

	/*
	 * If this is the first read, get the zap type.
	 */
	if (fp->f_seekp == 0) {
		rc = dnode_read(spa, &fp->f_dnode,
				0, &fp->f_zap_type, sizeof(fp->f_zap_type));
		if (rc)
			return (rc);

		if (fp->f_zap_type == ZBT_MICRO) {
			fp->f_seekp = offsetof(mzap_phys_t, mz_chunk);
		} else {
			rc = dnode_read(spa, &fp->f_dnode,
					offsetof(zap_phys_t, zap_num_leafs),
					&fp->f_num_leafs,
					sizeof(fp->f_num_leafs));
			if (rc)
				return (rc);

			fp->f_seekp = bsize;
			fp->f_zap_leaf = (zap_leaf_phys_t *)malloc(bsize);
			rc = dnode_read(spa, &fp->f_dnode,
					fp->f_seekp,
					fp->f_zap_leaf,
					bsize);
			if (rc)
				return (rc);
		}
	}

	if (fp->f_zap_type == ZBT_MICRO) {
	mzap_next:
		if (fp->f_seekp >= bsize)
			return (ENOENT);

		rc = dnode_read(spa, &fp->f_dnode,
				fp->f_seekp, &mze, sizeof(mze));
		if (rc)
			return (rc);
		fp->f_seekp += sizeof(mze);

		if (!mze.mze_name[0])
			goto mzap_next;

		d->d_fileno = ZFS_DIRENT_OBJ(mze.mze_value);
		d->d_type = ZFS_DIRENT_TYPE(mze.mze_value);
		strcpy(d->d_name, mze.mze_name);
		d->d_namlen = strlen(d->d_name);
		return (0);
	} else {
		zap_leaf_t zl;
		zap_leaf_chunk_t *zc, *nc;
		int chunk;
		size_t namelen;
		char *p;
		uint64_t value;

		/*
		 * Initialise this so we can use the ZAP size
		 * calculating macros.
		 */
		zl.l_bs = ilog2(bsize);
		zl.l_phys = fp->f_zap_leaf;

		/*
		 * Figure out which chunk we are currently looking at
		 * and consider seeking to the next leaf. We use the
		 * low bits of f_seekp as a simple chunk index.
		 */
	fzap_next:
		chunk = fp->f_seekp & (bsize - 1);
		if (chunk == ZAP_LEAF_NUMCHUNKS(&zl)) {
			fp->f_seekp = rounddown2(fp->f_seekp, bsize) + bsize;
			chunk = 0;

			/*
			 * Check for EOF and read the new leaf.
			 */
			if (fp->f_seekp >= bsize * fp->f_num_leafs)
				return (ENOENT);

			rc = dnode_read(spa, &fp->f_dnode,
					fp->f_seekp,
					fp->f_zap_leaf,
					bsize);
			if (rc)
				return (rc);
		}

		zc = &ZAP_LEAF_CHUNK(&zl, chunk);
		fp->f_seekp++;
		if (zc->l_entry.le_type != ZAP_CHUNK_ENTRY)
			goto fzap_next;

		namelen = zc->l_entry.le_name_numints;
		if (namelen > sizeof(d->d_name))
			namelen = sizeof(d->d_name);

		/*
		 * Paste the name back together.
		 */
		nc = &ZAP_LEAF_CHUNK(&zl, zc->l_entry.le_name_chunk);
		p = d->d_name;
		while (namelen > 0) {
			int len;
			len = namelen;
			if (len > ZAP_LEAF_ARRAY_BYTES)
				len = ZAP_LEAF_ARRAY_BYTES;
			memcpy(p, nc->l_array.la_array, len);
			p += len;
			namelen -= len;
			nc = &ZAP_LEAF_CHUNK(&zl, nc->l_array.la_next);
		}
		d->d_name[sizeof(d->d_name) - 1] = 0;

		/*
		 * Assume the first eight bytes of the value are
		 * a uint64_t.
		 */
		value = fzap_leaf_value(&zl, zc);

		d->d_fileno = ZFS_DIRENT_OBJ(value);
		d->d_type = ZFS_DIRENT_TYPE(value);
		d->d_namlen = strlen(d->d_name);

		return (0);
	}
}

static int
vdev_read(vdev_t *vdev, void *priv, off_t offset, void *buf, size_t bytes)
{
	int fd, ret;
	size_t res, head, tail, total_size, full_sec_size;
	unsigned secsz, do_tail_read;
	off_t start_sec;
	char *outbuf, *bouncebuf;

	fd = (uintptr_t) priv;
	outbuf = (char *) buf;
	bouncebuf = NULL;

	ret = ioctl(fd, DIOCGSECTORSIZE, &secsz);
	if (ret != 0)
		return (ret);

	/*
	 * Handling reads of arbitrary offset and size - multi-sector case
	 * and single-sector case.
	 *
	 *                        Multi-sector Case
	 *                (do_tail_read = true if tail > 0)
	 *
	 *   |<----------------------total_size--------------------->|
	 *   |                                                       |
	 *   |<--head-->|<--------------bytes------------>|<--tail-->|
	 *   |          |                                 |          |
	 *   |          |       |<~full_sec_size~>|       |          |
	 *   +------------------+                 +------------------+
	 *   |          |0101010|     .  .  .     |0101011|          |
	 *   +------------------+                 +------------------+
	 *         start_sec                         start_sec + n
	 *
	 *
	 *                      Single-sector Case
	 *                    (do_tail_read = false)
	 *
	 *              |<------total_size = secsz----->|
	 *              |                               |
	 *              |<-head->|<---bytes--->|<-tail->|
	 *              +-------------------------------+
	 *              |        |0101010101010|        |
	 *              +-------------------------------+
	 *                          start_sec
	 */
	start_sec = offset / secsz;
	head = offset % secsz;
	total_size = roundup2(head + bytes, secsz);
	tail = total_size - (head + bytes);
	do_tail_read = ((tail > 0) && (head + bytes > secsz));
	full_sec_size = total_size;
	if (head > 0)
		full_sec_size -= secsz;
	if (do_tail_read)
		full_sec_size -= secsz;

	/* Return of partial sector data requires a bounce buffer. */
	if ((head > 0) || do_tail_read) {
		bouncebuf = zfs_alloc(secsz);
		if (bouncebuf == NULL) {
			printf("vdev_read: out of memory\n");
			return (ENOMEM);
		}
	}

	if (lseek(fd, start_sec * secsz, SEEK_SET) == -1)
		return (errno);

	/* Partial data return from first sector */
	if (head > 0) {
		res = read(fd, bouncebuf, secsz);
		if (res != secsz) {
			ret = EIO;
			goto error;
		}
		memcpy(outbuf, bouncebuf + head, min(secsz - head, bytes));
		outbuf += min(secsz - head, bytes);
	}

	/* Full data return from read sectors */
	if (full_sec_size > 0) {
		res = read(fd, outbuf, full_sec_size);
		if (res != full_sec_size) {
			ret = EIO;
			goto error;
		}
		outbuf += full_sec_size;
	}

	/* Partial data return from last sector */
	if (do_tail_read) {
		res = read(fd, bouncebuf, secsz);
		if (res != secsz) {
			ret = EIO;
			goto error;
		}
		memcpy(outbuf, bouncebuf, secsz - tail);
	}

	ret = 0;
error:
	if (bouncebuf != NULL)
		zfs_free(bouncebuf, secsz);
	return (ret);
}

static int
zfs_dev_init(void)
{
	spa_t *spa;
	spa_t *next;
	spa_t *prev;

	zfs_init();
	if (archsw.arch_zfs_probe == NULL)
		return (ENXIO);
	archsw.arch_zfs_probe();

	prev = NULL;
	spa = STAILQ_FIRST(&zfs_pools);
	while (spa != NULL) {
		next = STAILQ_NEXT(spa, spa_link);
		if (zfs_spa_init(spa)) {
			if (prev == NULL)
				STAILQ_REMOVE_HEAD(&zfs_pools, spa_link);
			else
				STAILQ_REMOVE_AFTER(&zfs_pools, prev, spa_link);
		} else
			prev = spa;
		spa = next;
	}
	return (0);
}

struct zfs_probe_args {
	int		fd;
	const char	*devname;
	uint64_t	*pool_guid;
	u_int		secsz;
};

static int
zfs_diskread(void *arg, void *buf, size_t blocks, uint64_t offset)
{
	struct zfs_probe_args *ppa;

	ppa = (struct zfs_probe_args *)arg;
	return (vdev_read(NULL, (void *)(uintptr_t)ppa->fd,
	    offset * ppa->secsz, buf, blocks * ppa->secsz));
}

static int
zfs_probe(int fd, uint64_t *pool_guid)
{
	spa_t *spa;
	int ret;

	spa = NULL;
	ret = vdev_probe(vdev_read, (void *)(uintptr_t)fd, &spa);
	if (ret == 0 && pool_guid != NULL)
		*pool_guid = spa->spa_guid;
	return (ret);
}

static int
zfs_probe_partition(void *arg, const char *partname,
    const struct ptable_entry *part)
{
	struct zfs_probe_args *ppa, pa;
	struct ptable *table;
	char devname[32];
	int ret;

	/* Probe only freebsd-zfs and freebsd partitions */
	if (part->type != PART_FREEBSD &&
	    part->type != PART_FREEBSD_ZFS)
		return (0);

	ppa = (struct zfs_probe_args *)arg;
	strncpy(devname, ppa->devname, strlen(ppa->devname) - 1);
	devname[strlen(ppa->devname) - 1] = '\0';
	sprintf(devname, "%s%s:", devname, partname);
	pa.fd = open(devname, O_RDONLY);
	if (pa.fd == -1)
		return (0);
	ret = zfs_probe(pa.fd, ppa->pool_guid);
	if (ret == 0)
		return (0);
	/* Do we have BSD label here? */
	if (part->type == PART_FREEBSD) {
		pa.devname = devname;
		pa.pool_guid = ppa->pool_guid;
		pa.secsz = ppa->secsz;
		table = ptable_open(&pa, part->end - part->start + 1,
		    ppa->secsz, zfs_diskread);
		if (table != NULL) {
			ptable_iterate(table, &pa, zfs_probe_partition);
			ptable_close(table);
		}
	}
	close(pa.fd);
	return (0);
}

int
zfs_probe_dev(const char *devname, uint64_t *pool_guid)
{
	struct disk_devdesc *dev;
	struct ptable *table;
	struct zfs_probe_args pa;
	uint64_t mediasz;
	int ret;

	if (pool_guid)
		*pool_guid = 0;
	pa.fd = open(devname, O_RDONLY);
	if (pa.fd == -1)
		return (ENXIO);
	/*
	 * We will not probe the whole disk, we can not boot from such
	 * disks and some systems will misreport the disk sizes and will
	 * hang while accessing the disk.
	 */
	if (archsw.arch_getdev((void **)&dev, devname, NULL) == 0) {
		int partition = dev->d_partition;
		int slice = dev->d_slice;

		free(dev);
		if (partition != D_PARTNONE && slice != D_SLICENONE) {
			ret = zfs_probe(pa.fd, pool_guid);
			if (ret == 0)
				return (0);
		}
	}

	/* Probe each partition */
	ret = ioctl(pa.fd, DIOCGMEDIASIZE, &mediasz);
	if (ret == 0)
		ret = ioctl(pa.fd, DIOCGSECTORSIZE, &pa.secsz);
	if (ret == 0) {
		pa.devname = devname;
		pa.pool_guid = pool_guid;
		table = ptable_open(&pa, mediasz / pa.secsz, pa.secsz,
		    zfs_diskread);
		if (table != NULL) {
			ptable_iterate(table, &pa, zfs_probe_partition);
			ptable_close(table);
		}
	}
	close(pa.fd);
	if (pool_guid && *pool_guid == 0)
		ret = ENXIO;
	return (ret);
}

/*
 * Print information about ZFS pools
 */
static int
zfs_dev_print(int verbose)
{
	spa_t *spa;
	char line[80];
	int ret = 0;

	if (STAILQ_EMPTY(&zfs_pools))
		return (0);

	printf("%s devices:", zfs_dev.dv_name);
	if ((ret = pager_output("\n")) != 0)
		return (ret);

	if (verbose) {
		return (spa_all_status());
	}
	STAILQ_FOREACH(spa, &zfs_pools, spa_link) {
		snprintf(line, sizeof(line), "    zfs:%s\n", spa->spa_name);
		ret = pager_output(line);
		if (ret != 0)
			break;
	}
	return (ret);
}

/*
 * Attempt to open the pool described by (dev) for use by (f).
 */
static int
zfs_dev_open(struct open_file *f, ...)
{
	va_list		args;
	struct zfs_devdesc	*dev;
	struct zfsmount	*mount;
	spa_t		*spa;
	int		rv;

	va_start(args, f);
	dev = va_arg(args, struct zfs_devdesc *);
	va_end(args);

	if (dev->pool_guid == 0)
		spa = STAILQ_FIRST(&zfs_pools);
	else
		spa = spa_find_by_guid(dev->pool_guid);
	if (!spa)
		return (ENXIO);
	mount = malloc(sizeof(*mount));
	rv = zfs_mount(spa, dev->root_guid, mount);
	if (rv != 0) {
		free(mount);
		return (rv);
	}
	if (mount->objset.os_type != DMU_OST_ZFS) {
		printf("Unexpected object set type %ju\n",
		    (uintmax_t)mount->objset.os_type);
		free(mount);
		return (EIO);
	}
	f->f_devdata = mount;
	free(dev);
	return (0);
}

static int
zfs_dev_close(struct open_file *f)
{

	free(f->f_devdata);
	f->f_devdata = NULL;
	return (0);
}

static int
zfs_dev_strategy(void *devdata, int rw, daddr_t dblk, size_t size, char *buf, size_t *rsize)
{

	return (ENOSYS);
}

struct devsw zfs_dev = {
	.dv_name = "zfs",
	.dv_type = DEVT_ZFS,
	.dv_init = zfs_dev_init,
	.dv_strategy = zfs_dev_strategy,
	.dv_open = zfs_dev_open,
	.dv_close = zfs_dev_close,
	.dv_ioctl = noioctl,
	.dv_print = zfs_dev_print,
	.dv_cleanup = NULL
};

int
zfs_parsedev(struct zfs_devdesc *dev, const char *devspec, const char **path)
{
	static char	rootname[ZFS_MAXNAMELEN];
	static char	poolname[ZFS_MAXNAMELEN];
	spa_t		*spa;
	const char	*end;
	const char	*np;
	const char	*sep;
	int		rv;

	np = devspec;
	if (*np != ':')
		return (EINVAL);
	np++;
	end = strrchr(np, ':');
	if (end == NULL)
		return (EINVAL);
	sep = strchr(np, '/');
	if (sep == NULL || sep >= end)
		sep = end;
	memcpy(poolname, np, sep - np);
	poolname[sep - np] = '\0';
	if (sep < end) {
		sep++;
		memcpy(rootname, sep, end - sep);
		rootname[end - sep] = '\0';
	}
	else
		rootname[0] = '\0';

	spa = spa_find_by_name(poolname);
	if (!spa)
		return (ENXIO);
	dev->pool_guid = spa->spa_guid;
	rv = zfs_lookup_dataset(spa, rootname, &dev->root_guid);
	if (rv != 0)
		return (rv);
	if (path != NULL)
		*path = (*end == '\0') ? end : end + 1;
	dev->dd.d_dev = &zfs_dev;
	return (0);
}

char *
zfs_fmtdev(void *vdev)
{
	static char		rootname[ZFS_MAXNAMELEN];
	static char		buf[2 * ZFS_MAXNAMELEN + 8];
	struct zfs_devdesc	*dev = (struct zfs_devdesc *)vdev;
	spa_t			*spa;

	buf[0] = '\0';
	if (dev->dd.d_dev->dv_type != DEVT_ZFS)
		return (buf);

	if (dev->pool_guid == 0) {
		spa = STAILQ_FIRST(&zfs_pools);
		dev->pool_guid = spa->spa_guid;
	} else
		spa = spa_find_by_guid(dev->pool_guid);
	if (spa == NULL) {
		printf("ZFS: can't find pool by guid\n");
		return (buf);
	}
	if (dev->root_guid == 0 && zfs_get_root(spa, &dev->root_guid)) {
		printf("ZFS: can't find root filesystem\n");
		return (buf);
	}
	if (zfs_rlookup(spa, dev->root_guid, rootname)) {
		printf("ZFS: can't find filesystem by guid\n");
		return (buf);
	}

	if (rootname[0] == '\0')
		sprintf(buf, "%s:%s:", dev->dd.d_dev->dv_name, spa->spa_name);
	else
		sprintf(buf, "%s:%s/%s:", dev->dd.d_dev->dv_name, spa->spa_name,
		    rootname);
	return (buf);
}

int
zfs_list(const char *name)
{
	static char	poolname[ZFS_MAXNAMELEN];
	uint64_t	objid;
	spa_t		*spa;
	const char	*dsname;
	int		len;
	int		rv;

	len = strlen(name);
	dsname = strchr(name, '/');
	if (dsname != NULL) {
		len = dsname - name;
		dsname++;
	} else
		dsname = "";
	memcpy(poolname, name, len);
	poolname[len] = '\0';

	spa = spa_find_by_name(poolname);
	if (!spa)
		return (ENXIO);
	rv = zfs_lookup_dataset(spa, dsname, &objid);
	if (rv != 0)
		return (rv);

	return (zfs_list_dataset(spa, objid));
}

void
init_zfs_bootenv(const char *currdev_in)
{
	char *beroot, *currdev;
	int currdev_len;

	currdev = NULL;
	currdev_len = strlen(currdev_in);
	if (currdev_len == 0)
		return;
	if (strncmp(currdev_in, "zfs:", 4) != 0)
		return;
	currdev = strdup(currdev_in);
	if (currdev == NULL)
		return;
	/* Remove the trailing : */
	currdev[currdev_len - 1] = '\0';
	setenv("zfs_be_active", currdev, 1);
	setenv("zfs_be_currpage", "1", 1);
	/* Remove the last element (current bootenv) */
	beroot = strrchr(currdev, '/');
	if (beroot != NULL)
		beroot[0] = '\0';
	beroot = strchr(currdev, ':') + 1;
	setenv("zfs_be_root", beroot, 1);
	zfs_bootenv_initial(beroot);
	free(currdev);
}

static void
zfs_bootenv_initial(const char *name)
{
	char		poolname[ZFS_MAXNAMELEN], *dsname;
	char envname[32], envval[256];
	uint64_t	objid;
	spa_t		*spa;
	int		bootenvs_idx, len, rv;

	SLIST_INIT(&zfs_be_head);
	zfs_env_count = 0;
	len = strlen(name);
	dsname = strchr(name, '/');
	if (dsname != NULL) {
		len = dsname - name;
		dsname++;
	} else
		dsname = "";
	strlcpy(poolname, name, len + 1);
	spa = spa_find_by_name(poolname);
	if (spa == NULL)
		return;
	rv = zfs_lookup_dataset(spa, dsname, &objid);
	if (rv != 0)
		return;
	rv = zfs_callback_dataset(spa, objid, zfs_belist_add);
	bootenvs_idx = 0;
	/* Populate the initial environment variables */
	SLIST_FOREACH_SAFE(zfs_be, &zfs_be_head, entries, zfs_be_tmp) {
		/* Enumerate all bootenvs for general usage */
		snprintf(envname, sizeof(envname), "bootenvs[%d]", bootenvs_idx);
		snprintf(envval, sizeof(envval), "zfs:%s/%s", name, zfs_be->name);
		rv = setenv(envname, envval, 1);
		if (rv != 0)
			break;
		bootenvs_idx++;
	}
	snprintf(envval, sizeof(envval), "%d", bootenvs_idx);
	setenv("bootenvs_count", envval, 1);

	/* Clean up the SLIST of ZFS BEs */
	while (!SLIST_EMPTY(&zfs_be_head)) {
		zfs_be = SLIST_FIRST(&zfs_be_head);
		SLIST_REMOVE_HEAD(&zfs_be_head, entries);
		free(zfs_be);
	}

	return;

}

int
zfs_bootenv(const char *name)
{
	static char	poolname[ZFS_MAXNAMELEN], *dsname, *root;
	char		becount[4];
	uint64_t	objid;
	spa_t		*spa;
	int		len, rv, pages, perpage, currpage;

	if (name == NULL)
		return (EINVAL);
	if ((root = getenv("zfs_be_root")) == NULL)
		return (EINVAL);

	if (strcmp(name, root) != 0) {
		if (setenv("zfs_be_root", name, 1) != 0)
			return (ENOMEM);
	}

	SLIST_INIT(&zfs_be_head);
	zfs_env_count = 0;
	len = strlen(name);
	dsname = strchr(name, '/');
	if (dsname != NULL) {
		len = dsname - name;
		dsname++;
	} else
		dsname = "";
	memcpy(poolname, name, len);
	poolname[len] = '\0';

	spa = spa_find_by_name(poolname);
	if (!spa)
		return (ENXIO);
	rv = zfs_lookup_dataset(spa, dsname, &objid);
	if (rv != 0)
		return (rv);
	rv = zfs_callback_dataset(spa, objid, zfs_belist_add);

	/* Calculate and store the number of pages of BEs */
	perpage = (ZFS_BE_LAST - ZFS_BE_FIRST + 1);
	pages = (zfs_env_count / perpage) + ((zfs_env_count % perpage) > 0 ? 1 : 0);
	snprintf(becount, 4, "%d", pages);
	if (setenv("zfs_be_pages", becount, 1) != 0)
		return (ENOMEM);

	/* Roll over the page counter if it has exceeded the maximum */
	currpage = strtol(getenv("zfs_be_currpage"), NULL, 10);
	if (currpage > pages) {
		if (setenv("zfs_be_currpage", "1", 1) != 0)
			return (ENOMEM);
	}

	/* Populate the menu environment variables */
	zfs_set_env();

	/* Clean up the SLIST of ZFS BEs */
	while (!SLIST_EMPTY(&zfs_be_head)) {
		zfs_be = SLIST_FIRST(&zfs_be_head);
		SLIST_REMOVE_HEAD(&zfs_be_head, entries);
		free(zfs_be);
	}

	return (rv);
}

int
zfs_belist_add(const char *name, uint64_t value __unused)
{

	/* Skip special datasets that start with a $ character */
	if (strncmp(name, "$", 1) == 0) {
		return (0);
	}
	/* Add the boot environment to the head of the SLIST */
	zfs_be = malloc(sizeof(struct zfs_be_entry));
	if (zfs_be == NULL) {
		return (ENOMEM);
	}
	zfs_be->name = name;
	SLIST_INSERT_HEAD(&zfs_be_head, zfs_be, entries);
	zfs_env_count++;

	return (0);
}

int
zfs_set_env(void)
{
	char envname[32], envval[256];
	char *beroot, *pagenum;
	int rv, page, ctr;

	beroot = getenv("zfs_be_root");
	if (beroot == NULL) {
		return (1);
	}

	pagenum = getenv("zfs_be_currpage");
	if (pagenum != NULL) {
		page = strtol(pagenum, NULL, 10);
	} else {
		page = 1;
	}

	ctr = 1;
	rv = 0;
	zfs_env_index = ZFS_BE_FIRST;
	SLIST_FOREACH_SAFE(zfs_be, &zfs_be_head, entries, zfs_be_tmp) {
		/* Skip to the requested page number */
		if (ctr <= ((ZFS_BE_LAST - ZFS_BE_FIRST + 1) * (page - 1))) {
			ctr++;
			continue;
		}
		
		snprintf(envname, sizeof(envname), "bootenvmenu_caption[%d]", zfs_env_index);
		snprintf(envval, sizeof(envval), "%s", zfs_be->name);
		rv = setenv(envname, envval, 1);
		if (rv != 0) {
			break;
		}

		snprintf(envname, sizeof(envname), "bootenvansi_caption[%d]", zfs_env_index);
		rv = setenv(envname, envval, 1);
		if (rv != 0){
			break;
		}

		snprintf(envname, sizeof(envname), "bootenvmenu_command[%d]", zfs_env_index);
		rv = setenv(envname, "set_bootenv", 1);
		if (rv != 0){
			break;
		}

		snprintf(envname, sizeof(envname), "bootenv_root[%d]", zfs_env_index);
		snprintf(envval, sizeof(envval), "zfs:%s/%s", beroot, zfs_be->name);
		rv = setenv(envname, envval, 1);
		if (rv != 0){
			break;
		}

		zfs_env_index++;
		if (zfs_env_index > ZFS_BE_LAST) {
			break;
		}

	}
	
	for (; zfs_env_index <= ZFS_BE_LAST; zfs_env_index++) {
		snprintf(envname, sizeof(envname), "bootenvmenu_caption[%d]", zfs_env_index);
		(void)unsetenv(envname);
		snprintf(envname, sizeof(envname), "bootenvansi_caption[%d]", zfs_env_index);
		(void)unsetenv(envname);
		snprintf(envname, sizeof(envname), "bootenvmenu_command[%d]", zfs_env_index);
		(void)unsetenv(envname);
		snprintf(envname, sizeof(envname), "bootenv_root[%d]", zfs_env_index);
		(void)unsetenv(envname);
	}

	return (rv);
}
