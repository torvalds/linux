/*	$OpenBSD: vioqcow2.c,v 1.25 2024/09/26 01:45:13 jsg Exp $	*/

/*
 * Copyright (c) 2018 Ori Bernstein <ori@eigenstate.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "virtio.h"

#define QCOW2_COMPRESSED	0x4000000000000000ull
#define QCOW2_INPLACE		0x8000000000000000ull

#define QCOW2_DIRTY		(1 << 0)
#define QCOW2_CORRUPT		(1 << 1)

enum {
	ICFEATURE_DIRTY		= 1 << 0,
	ICFEATURE_CORRUPT	= 1 << 1,
};

enum {
	ACFEATURE_BITEXT	= 1 << 0,
};

struct qcheader {
	char magic[4];
	uint32_t version;
	uint64_t backingoff;
	uint32_t backingsz;
	uint32_t clustershift;
	uint64_t disksz;
	uint32_t cryptmethod;
	uint32_t l1sz;
	uint64_t l1off;
	uint64_t refoff;
	uint32_t refsz;
	uint32_t snapcount;
	uint64_t snapsz;
	/* v3 additions */
	uint64_t incompatfeatures;
	uint64_t compatfeatures;
	uint64_t autoclearfeatures;
	uint32_t reforder;	/* Bits = 1 << reforder */
	uint32_t headersz;
} __packed;

struct qcdisk {
	pthread_rwlock_t lock;
	struct qcdisk *base;
	struct qcheader header;

	int       fd;
	uint64_t *l1;
	off_t     end;
	off_t	  clustersz;
	off_t	  disksz; /* In bytes */
	uint32_t  cryptmethod;

	uint32_t l1sz;
	off_t	 l1off;

	off_t	 refoff;
	off_t	 refsz;

	uint32_t nsnap;
	off_t	 snapoff;

	/* v3 features */
	uint64_t incompatfeatures;
	uint64_t autoclearfeatures;
	uint32_t refssz;
	uint32_t headersz;
};

extern char *__progname;

static off_t xlate(struct qcdisk *, off_t, int *);
static void copy_cluster(struct qcdisk *, struct qcdisk *, off_t, off_t);
static void inc_refs(struct qcdisk *, off_t, int);
static off_t mkcluster(struct qcdisk *, struct qcdisk *, off_t, off_t);
static int qc2_open(struct qcdisk *, int *, size_t);
static ssize_t qc2_pread(void *, char *, size_t, off_t);
static ssize_t qc2_preadv(void *, struct iovec *, int, off_t);
static ssize_t qc2_pwrite(void *, char *, size_t, off_t);
static ssize_t qc2_pwritev(void *, struct iovec *, int, off_t);
static void qc2_close(void *, int);

/*
 * Initializes a raw disk image backing file from an fd. Stores the
 * number of bytes in *szp, returning -1 for error, 0 for success.
 *
 * May open snapshot base images.
 */
int
virtio_qcow2_init(struct virtio_backing *file, off_t *szp, int *fd, size_t nfd)
{
	struct qcdisk *diskp;

	diskp = malloc(sizeof(struct qcdisk));
	if (diskp == NULL)
		return -1;
	if (qc2_open(diskp, fd, nfd) == -1) {
		log_warnx("could not open qcow2 disk");
		return -1;
	}
	file->p = diskp;
	file->pread = qc2_pread;
	file->preadv = qc2_preadv;
	file->pwrite = qc2_pwrite;
	file->pwritev = qc2_pwritev;
	file->close = qc2_close;
	*szp = diskp->disksz;
	return 0;
}

/*
 * Return the path to the base image given a disk image.
 * Called from vmctl.
 */
ssize_t
virtio_qcow2_get_base(int fd, char *path, size_t npath, const char *dpath)
{
	char dpathbuf[PATH_MAX];
	char expanded[PATH_MAX];
	struct qcheader header;
	uint64_t backingoff;
	uint32_t backingsz;
	char *s = NULL;

	if (pread(fd, &header, sizeof(header), 0) != sizeof(header)) {
		log_warnx("short read on header");
		return -1;
	}
	if (strncmp(header.magic, VM_MAGIC_QCOW, strlen(VM_MAGIC_QCOW)) != 0) {
		log_warnx("invalid magic numbers");
		return -1;
	}
	backingoff = be64toh(header.backingoff);
	backingsz = be32toh(header.backingsz);
	if (backingsz == 0)
		return 0;

	if (backingsz >= npath - 1) {
		log_warnx("snapshot path too long");
		return -1;
	}
	if (pread(fd, path, backingsz, backingoff) != backingsz) {
		log_warnx("could not read snapshot base name");
		return -1;
	}
	path[backingsz] = '\0';

	/*
	 * Relative paths should be interpreted relative to the disk image,
	 * rather than relative to the directory vmd happens to be running in,
	 * since this is the only useful interpretation.
	 */
	if (path[0] == '/') {
		if (realpath(path, expanded) == NULL ||
		    strlcpy(path, expanded, npath) >= npath) {
			log_warnx("unable to resolve %s", path);
			return -1;
		}
	} else {
		if (strlcpy(dpathbuf, dpath, sizeof(dpathbuf)) >=
		    sizeof(dpathbuf)) {
			log_warnx("path too long: %s", dpath);
			return -1;
		}
		s = dirname(dpathbuf);
		if (snprintf(expanded, sizeof(expanded),
		    "%s/%s", s, path) >= (int)sizeof(expanded)) {
			log_warnx("path too long: %s/%s", s, path);
			return -1;
		}
		if (npath < PATH_MAX ||
		    realpath(expanded, path) == NULL) {
			log_warnx("unable to resolve %s", path);
			return -1;
		}
	}

	return strlen(path);
}

static int
qc2_open(struct qcdisk *disk, int *fds, size_t nfd)
{
	char basepath[PATH_MAX];
	struct stat st;
	struct qcheader header;
	uint64_t backingoff;
	uint32_t backingsz;
	off_t i;
	int version, fd;

	pthread_rwlock_init(&disk->lock, NULL);
	fd = fds[0];
	disk->fd = fd;
	disk->base = NULL;
	disk->l1 = NULL;

	if (pread(fd, &header, sizeof(header), 0) != sizeof(header))
		fatalx("short read on header");
	if (strncmp(header.magic, VM_MAGIC_QCOW, strlen(VM_MAGIC_QCOW)) != 0)
		fatalx("invalid magic numbers");

	disk->clustersz		= (1ull << be32toh(header.clustershift));
	disk->disksz		= be64toh(header.disksz);
	disk->cryptmethod	= be32toh(header.cryptmethod);
	disk->l1sz		= be32toh(header.l1sz);
	disk->l1off		= be64toh(header.l1off);
	disk->refsz		= be32toh(header.refsz);
	disk->refoff		= be64toh(header.refoff);
	disk->nsnap		= be32toh(header.snapcount);
	disk->snapoff		= be64toh(header.snapsz);

	/*
	 * The additional features here are defined as 0 in the v2 format,
	 * so as long as we clear the buffer before parsing, we don't need
	 * to check versions here.
	 */
	disk->incompatfeatures = be64toh(header.incompatfeatures);
	disk->autoclearfeatures = be64toh(header.autoclearfeatures);
	disk->refssz = be32toh(header.refsz);
	disk->headersz = be32toh(header.headersz);

	/*
	 * We only know about the dirty or corrupt bits here.
	 */
	if (disk->incompatfeatures & ~(QCOW2_DIRTY|QCOW2_CORRUPT))
		fatalx("unsupported features %llx",
		    disk->incompatfeatures & ~(QCOW2_DIRTY|QCOW2_CORRUPT));
	if (be32toh(header.reforder) != 4)
		fatalx("unsupported refcount size\n");

	disk->l1 = calloc(disk->l1sz, sizeof(*disk->l1));
	if (!disk->l1)
		fatal("%s: could not allocate l1 table", __func__);
	if (pread(disk->fd, disk->l1, 8 * disk->l1sz, disk->l1off)
	    != 8 * disk->l1sz)
		fatalx("%s: unable to read qcow2 L1 table", __func__);
	for (i = 0; i < disk->l1sz; i++)
		disk->l1[i] = be64toh(disk->l1[i]);
	version = be32toh(header.version);
	if (version != 2 && version != 3)
		fatalx("%s: unknown qcow2 version %d", __func__, version);

	backingoff = be64toh(header.backingoff);
	backingsz = be32toh(header.backingsz);
	if (backingsz != 0) {
		if (backingsz >= sizeof(basepath) - 1) {
			fatalx("%s: snapshot path too long", __func__);
		}
		if (pread(fd, basepath, backingsz, backingoff) != backingsz) {
			fatalx("%s: could not read snapshot base name",
			    __func__);
		}
		basepath[backingsz] = 0;
		if (nfd <= 1) {
			fatalx("%s: missing base image %s", __func__,
			    basepath);
		}


		disk->base = calloc(1, sizeof(struct qcdisk));
		if (!disk->base)
			fatal("%s: could not open %s", __func__, basepath);
		if (qc2_open(disk->base, fds + 1, nfd - 1) == -1)
			fatalx("%s: could not open %s", __func__, basepath);
		if (disk->base->clustersz != disk->clustersz)
			fatalx("%s: all disk parts must share clustersize",
			    __func__);
	}
	if (fstat(fd, &st) == -1)
		fatal("%s: unable to stat disk", __func__);

	disk->end = st.st_size;

	log_debug("%s: qcow2 disk version %d size %lld end %lld snap %d",
	    __func__, version, disk->disksz, disk->end, disk->nsnap);

	return 0;
}

static ssize_t
qc2_preadv(void *p, struct iovec *iov, int cnt, off_t offset)
{
	int i;
	off_t pos = offset;
	ssize_t sz = 0, total = 0;

	for (i = 0; i < cnt; i++, iov++) {
		sz = qc2_pread(p, iov->iov_base, iov->iov_len, pos);
		if (sz == -1)
			return (sz);
		total += sz;
		pos += sz;
	}

	return (total);
}

static ssize_t
qc2_pread(void *p, char *buf, size_t len, off_t off)
{
	struct qcdisk *disk, *d;
	off_t phys_off, end, cluster_off;
	ssize_t sz, rem;

	disk = p;
	end = off + len;
	if (off < 0 || end > disk->disksz)
		return -1;

	/* handle head chunk separately */
	rem = len;
	while (off != end) {
		for (d = disk; d; d = d->base)
			if ((phys_off = xlate(d, off, NULL)) > 0)
				break;
		/* Break out into chunks. This handles
		 * three cases:
		 *
		 *    |----+====|========|====+-----|
		 *
		 * Either we are at the start of the read,
		 * and the cluster has some leading bytes.
		 * This means that we are reading the tail
		 * of the cluster, and our size is:
		 *
		 * 	clustersz - (off % clustersz).
		 *
		 * Otherwise, we're reading the middle section.
		 * We're already aligned here, so we can just
		 * read the whole cluster size. Or we're at the
		 * tail, at which point we just want to read the
		 * remaining bytes.
		 */
		cluster_off = off % disk->clustersz;
		sz = disk->clustersz - cluster_off;
		if (sz > rem)
			sz = rem;
		/*
		 * If we're within the disk, but don't have backing bytes,
		 * just read back zeros.
		 */
		if (!d)
			bzero(buf, sz);
		else if (pread(d->fd, buf, sz, phys_off) != sz)
			return -1;
		off += sz;
		buf += sz;
		rem -= sz;
	}
	return len;
}

static ssize_t
qc2_pwritev(void *p, struct iovec *iov, int cnt, off_t offset)
{
	int i;
	off_t pos = offset;
	ssize_t sz = 0, total = 0;

	for (i = 0; i < cnt; i++, iov++) {
		sz = qc2_pwrite(p, iov->iov_base, iov->iov_len, pos);
		if (sz == -1)
			return (sz);
		total += sz;
		pos += sz;
	}

	return (total);
}

static ssize_t
qc2_pwrite(void *p, char *buf, size_t len, off_t off)
{
	struct qcdisk *disk, *d;
	off_t phys_off, cluster_off, end;
	ssize_t sz, rem;
	int inplace;

	d = p;
	disk = p;
	inplace = 1;
	end = off + len;
	if (off < 0 || end > disk->disksz)
		return -1;
	rem = len;
	while (off != end) {
		/* See the read code for a summary of the computation */
		cluster_off = off % disk->clustersz;
		sz = disk->clustersz - cluster_off;
		if (sz > rem)
			sz = rem;

		phys_off = xlate(disk, off, &inplace);
		if (phys_off == -1)
			return -1;
		/*
		 * If we couldn't find the cluster in the writable disk,
		 * see if it exists in the base image. If it does, we
		 * need to copy it before the write. The copy happens
		 * in the '!inplace' if clause below te search.
		 */
		if (phys_off == 0)
			for (d = disk->base; d; d = d->base)
				if ((phys_off = xlate(d, off, NULL)) > 0)
					break;
		if (!inplace || phys_off == 0)
			phys_off = mkcluster(disk, d, off, phys_off);
		if (phys_off == -1)
			return -1;
		if (phys_off < disk->clustersz)
			fatalx("%s: writing reserved cluster", __func__);
		if (pwrite(disk->fd, buf, sz, phys_off) != sz)
			return -1;
		off += sz;
		buf += sz;
		rem -= sz;
	}
	return len;
}

static void
qc2_close(void *p, int stayopen)
{
	struct qcdisk *disk;

	disk = p;
	if (disk->base)
		qc2_close(disk->base, stayopen);
	if (!stayopen)
		close(disk->fd);
	free(disk->l1);
	free(disk);
}

/*
 * Translates a virtual offset into an on-disk offset.
 * Returns:
 * 	-1 on error
 * 	 0 on 'not found'
 * 	>0 on found
 */
static off_t
xlate(struct qcdisk *disk, off_t off, int *inplace)
{
	off_t l2sz, l1off, l2tab, l2off, cluster, clusteroff;
	uint64_t buf;


	/*
	 * Clear out inplace flag -- xlate misses should not
	 * be flagged as updatable in place. We will still
	 * return 0 from them, but this leaves less surprises
	 * in the API.
	 */
	if (inplace)
		*inplace = 0;
	pthread_rwlock_rdlock(&disk->lock);
	if (off < 0)
		goto err;

	l2sz = disk->clustersz / 8;
	l1off = (off / disk->clustersz) / l2sz;
	if (l1off >= disk->l1sz)
		goto err;

	l2tab = disk->l1[l1off];
	l2tab &= ~QCOW2_INPLACE;
	if (l2tab == 0) {
		pthread_rwlock_unlock(&disk->lock);
		return 0;
	}
	l2off = (off / disk->clustersz) % l2sz;
	pread(disk->fd, &buf, sizeof(buf), l2tab + l2off * 8);
	cluster = be64toh(buf);
	/*
	 * cluster may be 0, but all future operations don't affect
	 * the return value.
	 */
	if (inplace)
		*inplace = !!(cluster & QCOW2_INPLACE);
	if (cluster & QCOW2_COMPRESSED)
		fatalx("%s: compressed clusters unsupported", __func__);
	pthread_rwlock_unlock(&disk->lock);
	clusteroff = 0;
	cluster &= ~QCOW2_INPLACE;
	if (cluster)
		clusteroff = off % disk->clustersz;
	return cluster + clusteroff;
err:
	pthread_rwlock_unlock(&disk->lock);
	return -1;
}

/*
 * Allocates a new cluster on disk, creating a new L2 table
 * if needed. The cluster starts off with a refs of one,
 * and the writable bit set.
 *
 * Returns -1 on error, and the physical address within the
 * cluster of the write offset if it exists.
 */
static off_t
mkcluster(struct qcdisk *disk, struct qcdisk *base, off_t off, off_t src_phys)
{
	off_t l2sz, l1off, l2tab, l2off, cluster, clusteroff, orig;
	uint64_t buf;

	pthread_rwlock_wrlock(&disk->lock);

	cluster = -1;
	/* L1 entries always exist */
	l2sz = disk->clustersz / 8;
	l1off = off / (disk->clustersz * l2sz);
	if (l1off >= disk->l1sz)
		fatalx("l1 offset outside disk");

	disk->end = (disk->end + disk->clustersz - 1) & ~(disk->clustersz - 1);

	l2tab = disk->l1[l1off];
	l2off = (off / disk->clustersz) % l2sz;
	/* We may need to create or clone an L2 entry to map the block */
	if (l2tab == 0 || (l2tab & QCOW2_INPLACE) == 0) {
		orig = l2tab & ~QCOW2_INPLACE;
		l2tab = disk->end;
		disk->end += disk->clustersz;
		if (ftruncate(disk->fd, disk->end) == -1)
			fatal("%s: ftruncate failed", __func__);

		/*
		 * If we translated, found a L2 entry, but it needed to
		 * be copied, copy it.
		 */
		if (orig != 0)
			copy_cluster(disk, disk, l2tab, orig);
		/* Update l1 -- we flush it later */
		disk->l1[l1off] = l2tab | QCOW2_INPLACE;
		inc_refs(disk, l2tab, 1);
	}
	l2tab &= ~QCOW2_INPLACE;

	/* Grow the disk */
	if (ftruncate(disk->fd, disk->end + disk->clustersz) < 0)
		fatal("%s: could not grow disk", __func__);
	if (src_phys > 0)
		copy_cluster(disk, base, disk->end, src_phys);
	cluster = disk->end;
	disk->end += disk->clustersz;
	buf = htobe64(cluster | QCOW2_INPLACE);
	if (pwrite(disk->fd, &buf, sizeof(buf), l2tab + l2off * 8) != 8)
		fatalx("%s: could not write cluster", __func__);

	/* TODO: lazily sync: currently VMD doesn't close things */
	buf = htobe64(disk->l1[l1off]);
	if (pwrite(disk->fd, &buf, sizeof(buf), disk->l1off + 8 * l1off) != 8)
		fatalx("%s: could not write l1", __func__);
	inc_refs(disk, cluster, 1);

	pthread_rwlock_unlock(&disk->lock);
	clusteroff = off % disk->clustersz;
	if (cluster + clusteroff < disk->clustersz)
		fatalx("write would clobber header");
	return cluster + clusteroff;
}

/* Copies a cluster containing src to dst. Src and dst need not be aligned. */
static void
copy_cluster(struct qcdisk *disk, struct qcdisk *base, off_t dst, off_t src)
{
	char *scratch;

	scratch = malloc(disk->clustersz);
	if (!scratch)
		fatal("out of memory");
	src &= ~(disk->clustersz - 1);
	dst &= ~(disk->clustersz - 1);
	if (pread(base->fd, scratch, disk->clustersz, src) == -1)
		fatal("%s: could not read cluster", __func__);
	if (pwrite(disk->fd, scratch, disk->clustersz, dst) == -1)
		fatal("%s: could not write cluster", __func__);
	free(scratch);
}

static void
inc_refs(struct qcdisk *disk, off_t off, int newcluster)
{
	off_t l1off, l1idx, l2idx, l2cluster;
	size_t nper;
	uint16_t refs;
	uint64_t buf;

	off &= ~QCOW2_INPLACE;
	nper = disk->clustersz / 2;
	l1idx = (off / disk->clustersz) / nper;
	l2idx = (off / disk->clustersz) % nper;
	l1off = disk->refoff + 8 * l1idx;
	if (pread(disk->fd, &buf, sizeof(buf), l1off) != 8)
		fatal("could not read refs");

	l2cluster = be64toh(buf);
	if (l2cluster == 0) {
		l2cluster = disk->end;
		disk->end += disk->clustersz;
		if (ftruncate(disk->fd, disk->end) < 0)
			fatal("%s: failed to allocate ref block", __func__);
		buf = htobe64(l2cluster);
		if (pwrite(disk->fd, &buf, sizeof(buf), l1off) != 8)
			fatal("%s: failed to write ref block", __func__);
	}

	refs = 1;
	if (!newcluster) {
		if (pread(disk->fd, &refs, sizeof(refs),
		    l2cluster + 2 * l2idx) != 2)
			fatal("could not read ref cluster");
		refs = be16toh(refs) + 1;
	}
	refs = htobe16(refs);
	if (pwrite(disk->fd, &refs, sizeof(refs), l2cluster + 2 * l2idx) != 2)
		fatal("%s: could not write ref block", __func__);
}

/*
 * virtio_qcow2_create
 *
 * Create an empty qcow2 imagefile with the specified path and size.
 *
 * Parameters:
 *  imgfile_path: path to the image file to create
 *  imgsize     : size of the image file to create (in bytes)
 *
 * Return:
 *  EEXIST: The requested image file already exists
 *  0     : Image file successfully created
 *  Exxxx : Various other Exxxx errno codes due to other I/O errors
 */
int
virtio_qcow2_create(const char *imgfile_path,
    const char *base_path, uint64_t disksz)
{
	struct qcheader hdr, basehdr;
	int fd, ret;
	ssize_t base_len;
	uint64_t l1sz, refsz, initsz, clustersz;
	uint64_t l1off, refoff, v, i, l1entrysz, refentrysz;
	uint16_t refs;

	if (base_path) {
		fd = open(base_path, O_RDONLY);
		if (read(fd, &basehdr, sizeof(basehdr)) != sizeof(basehdr))
			errx(1, "failure to read base image header");
		close(fd);
		if (strncmp(basehdr.magic,
		    VM_MAGIC_QCOW, strlen(VM_MAGIC_QCOW)) != 0)
			errx(1, "base image is not a qcow2 file");
		if (!disksz)
			disksz = betoh64(basehdr.disksz);
		else if (disksz != betoh64(basehdr.disksz))
			errx(1, "base size does not match requested size");
	}
	if (!base_path && !disksz)
		errx(1, "missing disk size");

	clustersz = (1<<16);
	l1off = ALIGNSZ(sizeof(hdr), clustersz);

	l1entrysz = clustersz * clustersz / 8;
	l1sz = (disksz + l1entrysz - 1) / l1entrysz;

	refoff = ALIGNSZ(l1off + 8*l1sz, clustersz);
	refentrysz = clustersz * clustersz * clustersz / 2;
	refsz = (disksz + refentrysz - 1) / refentrysz;

	initsz = ALIGNSZ(refoff + refsz*clustersz, clustersz);
	base_len = base_path ? strlen(base_path) : 0;

	memcpy(hdr.magic, VM_MAGIC_QCOW, strlen(VM_MAGIC_QCOW));
	hdr.version		= htobe32(3);
	hdr.backingoff		= htobe64(base_path ? sizeof(hdr) : 0);
	hdr.backingsz		= htobe32(base_len);
	hdr.clustershift	= htobe32(16);
	hdr.disksz		= htobe64(disksz);
	hdr.cryptmethod		= htobe32(0);
	hdr.l1sz		= htobe32(l1sz);
	hdr.l1off		= htobe64(l1off);
	hdr.refoff		= htobe64(refoff);
	hdr.refsz		= htobe32(refsz);
	hdr.snapcount		= htobe32(0);
	hdr.snapsz		= htobe64(0);
	hdr.incompatfeatures	= htobe64(0);
	hdr.compatfeatures	= htobe64(0);
	hdr.autoclearfeatures	= htobe64(0);
	hdr.reforder		= htobe32(4);
	hdr.headersz		= htobe32(sizeof(hdr));

	/* Refuse to overwrite an existing image */
	fd = open(imgfile_path, O_RDWR | O_CREAT | O_TRUNC | O_EXCL,
	    S_IRUSR | S_IWUSR);
	if (fd == -1)
		return (errno);

	/* Write out the header */
	if (write(fd, &hdr, sizeof(hdr)) != sizeof(hdr))
		goto error;

	/* Add the base image */
	if (base_path && write(fd, base_path, base_len) != base_len)
		goto error;

	/* Extend to desired size, and add one refcount cluster */
	if (ftruncate(fd, (off_t)initsz + clustersz) == -1)
		goto error;

	/*
	 * Paranoia: if our disk image takes more than one cluster
	 * to refcount the initial image, fail.
	 */
	if (initsz/clustersz > clustersz/2) {
		errno = ERANGE;
		goto error;
	}

	/* Add a refcount block, and refcount ourselves. */
	v = htobe64(initsz);
	if (pwrite(fd, &v, 8, refoff) != 8)
		goto error;
	for (i = 0; i < initsz/clustersz + 1; i++) {
		refs = htobe16(1);
		if (pwrite(fd, &refs, 2, initsz + 2*i) != 2)
			goto error;
	}

	ret = close(fd);
	return (ret);
error:
	ret = errno;
	close(fd);
	unlink(imgfile_path);
	return (errno);
}
