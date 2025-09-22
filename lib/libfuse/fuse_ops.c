/* $OpenBSD: fuse_ops.c,v 1.40 2025/09/20 15:01:23 helg Exp $ */
/*
 * Copyright (c) 2013 Sylvestre Gallon <ccna.syl@gmail.com>
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

#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "fuse_private.h"
#include "debug.h"

#define CHECK_OPT(opname)	DPRINTF("Opcode: %s\t", #opname);	\
				DPRINTF("Inode: %llu\t",		\
				    (unsigned long long)fbuf->fb_ino);	\
				DPRINTF("Size: %lu\t", fbuf->fb_io_len);\
				if (!f->op.opname) {			\
					fbuf->fb_err = -ENOSYS;		\
					return (0);			\
				}

static int
update_attr(struct fuse *f, struct stat *attr, const char *realname,
    struct fuse_vnode *vn)
{
	int ret;

	memset(attr, 0, sizeof(struct stat));
	ret = f->op.getattr(realname, attr);

	if (attr->st_blksize == 0)
		attr->st_blksize = 512;
	if (attr->st_blocks == 0)
		attr->st_blocks = 4;

	if (!f->conf.use_ino)
		attr->st_ino = vn->ino;

	if (f->conf.set_mode)
		attr->st_mode = (attr->st_mode & S_IFMT) | (0777 & ~f->conf.umask);

	if (f->conf.set_uid)
		attr->st_uid = f->conf.uid;

	if (f->conf.set_gid)
		attr->st_gid = f->conf.gid;

	return (ret);
}

static int
ifuse_ops_init(struct fuse *f)
{
	struct fuse_conn_info fci;

	DPRINTF("Opcode: init\t");

	if (f->op.init) {
		memset(&fci, 0, sizeof(fci));
		fci.proto_minor = FUSE_MINOR_VERSION;
		fci.proto_major = FUSE_MAJOR_VERSION;

		f->op.init(&fci);
	}

	f->fc->init = 1;

	return (0);
}

static int
ifuse_ops_getattr(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_vnode *vn;
	char *realname;

	DPRINTF("Opcode: getattr\t");
	DPRINTF("Inode: %llu\t", (unsigned long long)fbuf->fb_ino);

	memset(&fbuf->fb_attr, 0, sizeof(struct stat));

	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	fbuf->fb_err = update_attr(f, &fbuf->fb_attr, realname, vn);
	free(realname);

	return (0);
}

static int
ifuse_ops_access(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_vnode *vn;
	char *realname;

	CHECK_OPT(access);

	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	fbuf->fb_err = f->op.access(realname, fbuf->fb_io_mode);
	free(realname);

	return (0);
}

static int
ifuse_ops_open(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_file_info ffi;
	struct fuse_vnode *vn;
	char *realname;

	CHECK_OPT(open);

	memset(&ffi, 0, sizeof(ffi));
	ffi.flags = fbuf->fb_io_flags;

	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	fbuf->fb_err = f->op.open(realname, &ffi);
	free(realname);

	if (!fbuf->fb_err)
		fbuf->fb_io_fd = ffi.fh;

	return (0);
}

static int
ifuse_ops_opendir(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_file_info ffi;
	struct fuse_vnode *vn;
	char *realname;

	DPRINTF("Opcode: opendir\t");
	DPRINTF("Inode: %llu\t", (unsigned long long)fbuf->fb_ino);

	memset(&ffi, 0, sizeof(ffi));
	ffi.flags = fbuf->fb_io_flags;

	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	if (f->op.opendir) {
		realname = build_realname(f, vn->ino);
		if (realname == NULL) {
			fbuf->fb_err = -errno;
			return (0);
		}

		fbuf->fb_err = f->op.opendir(realname, &ffi);
		free(realname);
	}

	if (!fbuf->fb_err)
		fbuf->fb_io_fd = ffi.fh;

	return (0);
}

#define GENERIC_DIRSIZ(NLEN) \
((sizeof (struct dirent) - (MAXNAMLEN+1)) + ((NLEN+1 + 7) &~ 7))

/*
 * This function adds one directory entry to the buffer.
 * FUSE file systems can implement readdir in one of two ways.
 *
 * 1. Read all directory entries in one operation. The off parameter
 *    will always be 0 and this filler function always returns 0.
 * 2. The file system keeps track of the directory entry offsets and
 *    this filler function returns 1 when the buffer is full.
 *
 * OpenBSD currently supports 1. but will still call the file system's
 * readdir function multiple times if either the kernel buffer or the
 * buffer supplied by the calling application is too small to fit all
 * entries. Each call to the file system's readdir function will fill
 * the buffer with the next set of entries.
 */
static int
ifuse_fill_readdir(void *dh, const char *name, const struct stat *stbuf,
    off_t off)
{
	struct fuse *f;
	struct fuse_dirhandle *fd = dh;
	struct fuse_vnode *v;
	struct fusebuf *fbuf;
	struct dirent *dir;
	uint32_t namelen;
	uint32_t len;

	f = fd->fuse;
	fbuf = fd->buf;
	namelen = strnlen(name, MAXNAMLEN);
	len = GENERIC_DIRSIZ(namelen);

	/* buffer is full so ignore the remaining entries */
	if (fd->full || (fbuf->fb_len + len > fd->size)) {
		fd->full = 1;
		return (0);
	}

	/* already returned these entries in a previous call so skip */
	if (fd->start != 0 && fd->idx < fd->start) {
		fd->idx += len;
		return (0);
	}

	dir = (struct dirent *) &fbuf->fb_dat[fbuf->fb_len];

	if (stbuf != NULL && f->conf.use_ino)
		dir->d_fileno = stbuf->st_ino;
	else {
		/*
		 * This always behaves as if readdir_ino option is set so
		 * getcwd(3) works.
		 */
		v = get_vn_by_name_and_parent(f, (uint8_t *)name, fbuf->fb_ino);
		if (v == NULL) {
			if (strcmp(name, ".") == 0)
				dir->d_fileno = fbuf->fb_ino;
			else
				dir->d_fileno = 0xffffffff;
		} else
			dir->d_fileno = v->ino;
	}

	if (stbuf != NULL)
		dir->d_type = IFTODT(stbuf->st_mode);
	else
		dir->d_type = DT_UNKNOWN;

	dir->d_reclen = len;
	dir->d_off = off + len;		/* XXX */
	strlcpy(dir->d_name, name, sizeof(dir->d_name));
	dir->d_namlen = strlen(dir->d_name);

	fbuf->fb_len += len;
	fd->idx += len;

	return (0);
}

static int
ifuse_fill_getdir(fuse_dirh_t fd, const char *name, int type, ino_t ino)
{
	struct stat st;

	memset(&st, 0, sizeof(st));
	st.st_mode = type << 12;
	if (ino == 0)
		st.st_ino = 0xffffffff;
	else
		st.st_ino = ino;

	return (fd->filler(fd, name, &st, 0));
}

static int
ifuse_ops_readdir(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_file_info ffi;
	struct fuse_dirhandle fd;
	struct fuse_vnode *vn;
	char *realname;
	uint64_t offset;
	uint32_t size;

	DPRINTF("Opcode: readdir\t");
	DPRINTF("Inode: %llu\t", (unsigned long long)fbuf->fb_ino);
	DPRINTF("Offset: %llu\t", fbuf->fb_io_off);
	DPRINTF("Size: %lu\t", fbuf->fb_io_len);

	memset(&ffi, 0, sizeof(ffi));
	ffi.fh = fbuf->fb_io_fd;
	offset = fbuf->fb_io_off;
	size = fbuf->fb_io_len;

	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	memset(&fd, 0, sizeof(fd));
	fd.filler = ifuse_fill_readdir;
	fd.buf = fbuf;
	fd.full = 0;
	fd.size = size;
	fd.off = offset;
	fd.idx = 0;
	fd.fuse = f;
	fd.start = offset;

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	if (f->op.readdir)
		fbuf->fb_err = f->op.readdir(realname, &fd, ifuse_fill_readdir,
		    offset, &ffi);
	else if (f->op.getdir)
		fbuf->fb_err = f->op.getdir(realname, &fd, ifuse_fill_getdir);
	else
		fbuf->fb_err = -ENOSYS;
	free(realname);

	if (fbuf->fb_err)
		fbuf->fb_len = 0;
	else if (fd.full && fbuf->fb_len == 0)
		fbuf->fb_err = -ENOBUFS;

	return (0);
}

static int
ifuse_ops_releasedir(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_file_info ffi;
	struct fuse_vnode *vn;
	char *realname;

	DPRINTF("Opcode: releasedir\t");
	DPRINTF("Inode: %llu\t", (unsigned long long)fbuf->fb_ino);

	memset(&ffi, 0, sizeof(ffi));
	ffi.fh = fbuf->fb_io_fd;
	ffi.fh_old = ffi.fh;
	ffi.flags = fbuf->fb_io_flags;

	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	if (f->op.releasedir) {
		realname = build_realname(f, vn->ino);
		if (realname == NULL) {
			fbuf->fb_err = -errno;
			return (0);
		}

		fbuf->fb_err = f->op.releasedir(realname, &ffi);
		free(realname);
	}

	return (0);
}

static int
ifuse_ops_release(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_file_info ffi;
	struct fuse_vnode *vn;
	char *realname;

	CHECK_OPT(release);

	memset(&ffi, 0, sizeof(ffi));
	ffi.fh = fbuf->fb_io_fd;
	ffi.fh_old = ffi.fh;
	ffi.flags = fbuf->fb_io_flags;

	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}
	fbuf->fb_err = f->op.release(realname, &ffi);
	free(realname);

	return (0);
}

static int
ifuse_ops_fsync(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_file_info ffi;
	struct fuse_vnode *vn;
	char *realname;
	int datasync;

	CHECK_OPT(fsync);

	memset(&ffi, 0, sizeof(ffi));
	ffi.fh = fbuf->fb_io_fd;

	/*
	 * fdatasync(2) is just a wrapper around fsync(2) so datasync
	 * is always false.
	 */
	datasync = 0;

	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}
	fbuf->fb_err = f->op.fsync(realname, datasync, &ffi);
	free(realname);

	return (0);
}

static int
ifuse_ops_flush(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_file_info ffi;
	struct fuse_vnode *vn;
	char *realname;

	CHECK_OPT(flush);

	memset(&ffi, 0, sizeof(ffi));
	ffi.fh = fbuf->fb_io_fd;
	ffi.fh_old = ffi.fh;
	ffi.flush = 1;

	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}
	fbuf->fb_err = f->op.flush(realname, &ffi);
	free(realname);

	return (0);
}

static int
ifuse_ops_lookup(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_vnode *vn;
	char *realname;

	DPRINTF("Opcode: lookup\t");
	DPRINTF("Inode: %llu\t", (unsigned long long)fbuf->fb_ino);

	if (strcmp((const char *)fbuf->fb_dat, "..") == 0) {
		vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
		if (vn == NULL || vn->parent == NULL) {
			fbuf->fb_err = -ENOENT;
			return (0);
		}
		vn = vn->parent;
		if (vn->ino != FUSE_ROOT_INO)
			ref_vn(vn);
	} else {
		vn = get_vn_by_name_and_parent(f, fbuf->fb_dat, fbuf->fb_ino);
		if (vn == NULL) {
			vn = alloc_vn(f, (const char *)fbuf->fb_dat, -1,
			    fbuf->fb_ino);
			if (vn == NULL) {
				fbuf->fb_err = -errno;
				return (0);
			}
			set_vn(f, vn); /*XXX*/
		} else if (vn->ino != FUSE_ROOT_INO)
			ref_vn(vn);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	fbuf->fb_err = update_attr(f, &fbuf->fb_attr, realname, vn);
	fbuf->fb_ino = vn->ino;
	free(realname);

	return (0);
}

static int
ifuse_ops_read(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_file_info ffi;
	struct fuse_vnode *vn;
	char *realname;
	uint64_t offset;
	uint32_t size;
	int ret;

	CHECK_OPT(read);

	memset(&ffi, 0, sizeof(ffi));
	ffi.fh = fbuf->fb_io_fd;
	size = fbuf->fb_io_len;
	offset = fbuf->fb_io_off;

	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	ret = f->op.read(realname, (char *)fbuf->fb_dat, size, offset, &ffi);
	free(realname);
	if (ret >= 0)
		fbuf->fb_len = ret;
	else
		fbuf->fb_err = ret;

	return (0);
}

static int
ifuse_ops_write(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_file_info ffi;
	struct fuse_vnode *vn;
	char *realname;
	uint64_t offset;
	uint32_t size;
	int ret;

	CHECK_OPT(write);

	memset(&ffi, 0, sizeof(ffi));
	ffi.fh = fbuf->fb_io_fd;
	ffi.fh_old = ffi.fh;
	ffi.writepage = fbuf->fb_io_flags & 1;
	size = fbuf->fb_io_len;
	offset = fbuf->fb_io_off;

	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	ret = f->op.write(realname, (char *)fbuf->fb_dat, size, offset, &ffi);
	free(realname);

	if (ret >= 0)
		fbuf->fb_io_len = ret;
	else
		fbuf->fb_err = ret;

	return (0);
}

static int
ifuse_ops_mkdir(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_vnode *vn;
	char *realname;
	uint32_t mode;

	CHECK_OPT(mkdir);

	mode = fbuf->fb_io_mode;
	vn = get_vn_by_name_and_parent(f, fbuf->fb_dat, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	fbuf->fb_err = f->op.mkdir(realname, mode);

	if (!fbuf->fb_err) {
		fbuf->fb_err = update_attr(f, &fbuf->fb_attr, realname, vn);
		fbuf->fb_io_mode = fbuf->fb_attr.st_mode;
		fbuf->fb_ino = vn->ino;
	}
	free(realname);

	return (0);
}

static int
ifuse_ops_rmdir(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_vnode *vn;
	char *realname;

	CHECK_OPT(rmdir);
	vn = get_vn_by_name_and_parent(f, fbuf->fb_dat, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	fbuf->fb_err = f->op.rmdir(realname);
	free(realname);

	return (0);
}

static int
ifuse_ops_readlink(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_vnode *vn;
	size_t bufsize;
	char *name;
	char *realname;

	CHECK_OPT(readlink);
	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	bufsize = fbuf->fb_io_len + 1;
	name = calloc(bufsize, sizeof(*name));
	if (name == NULL) {
		fbuf->fb_err = -errno;
		free(realname);
		return (0);
	}

	fbuf->fb_err = f->op.readlink(realname, name, bufsize);

	if (!fbuf->fb_err) {
		/* file system should return a NUL-terminated string */
		fbuf->fb_len = strlcpy(fbuf->fb_dat, name, bufsize);
		if (fbuf->fb_len >= bufsize)
			fbuf->fb_err = -ENAMETOOLONG;
	} else
		fbuf->fb_len = 0;

	free(realname);
	free(name);

	return (0);
}

static int
ifuse_ops_unlink(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_vnode *vn;
	char *realname;

	CHECK_OPT(unlink);

	vn = get_vn_by_name_and_parent(f, fbuf->fb_dat, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	fbuf->fb_err = f->op.unlink(realname);
	free(realname);

	return (0);
}

static int
ifuse_ops_statfs(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_vnode *vn;
	char *realname;

	memset(&fbuf->fb_stat, 0, sizeof(fbuf->fb_stat));

	CHECK_OPT(statfs);

	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	fbuf->fb_err = f->op.statfs(realname, &fbuf->fb_stat);
	free(realname);

	return (0);
}

static int
ifuse_ops_link(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_vnode *vn;
	char *realname;
	char *realname_ln;
	ino_t oldnodeid;

	CHECK_OPT(link);
	oldnodeid = fbuf->fb_io_ino;
	vn = get_vn_by_name_and_parent(f, fbuf->fb_dat, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, oldnodeid);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname_ln = build_realname(f, vn->ino);
	if (realname_ln == NULL) {
		fbuf->fb_err = -errno;
		free(realname);
		return (0);
	}

	fbuf->fb_err = f->op.link(realname, realname_ln);
	free(realname);
	free(realname_ln);

	return (0);
}

static int
ifuse_ops_setattr(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_vnode *vn;
	struct timespec ts[2];
	struct utimbuf tbuf;
	struct fb_io *io;
	char *realname;
	uid_t uid;
	gid_t gid;

	DPRINTF("Opcode: setattr\t");
	DPRINTF("Inode: %llu\t", (unsigned long long)fbuf->fb_ino);

	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}
	io = fbtod(fbuf, struct fb_io *);

	if (io->fi_flags & FUSE_FATTR_MODE) {
		if (f->op.chmod)
			fbuf->fb_err = f->op.chmod(realname,
			    fbuf->fb_attr.st_mode);
		else
			fbuf->fb_err = -ENOSYS;
	}

	if (!fbuf->fb_err && (io->fi_flags & FUSE_FATTR_UID ||
	    io->fi_flags & FUSE_FATTR_GID) ) {
		uid = (io->fi_flags & FUSE_FATTR_UID) ?
		    fbuf->fb_attr.st_uid : (uid_t)-1;
		gid = (io->fi_flags & FUSE_FATTR_GID) ?
		    fbuf->fb_attr.st_gid : (gid_t)-1;
		if (f->op.chown)
			fbuf->fb_err = f->op.chown(realname, uid, gid);
		else
			fbuf->fb_err = -ENOSYS;
	}

	if (!fbuf->fb_err && ( io->fi_flags & FUSE_FATTR_MTIME ||
		io->fi_flags & FUSE_FATTR_ATIME)) {

		if (f->op.utimens) {
			ts[0] = fbuf->fb_attr.st_atim;
			ts[1] = fbuf->fb_attr.st_mtim;
			fbuf->fb_err = f->op.utimens(realname, ts);
		} else if (f->op.utime) {
			tbuf.actime = fbuf->fb_attr.st_atim.tv_sec;
			tbuf.modtime = fbuf->fb_attr.st_mtim.tv_sec;
			fbuf->fb_err = f->op.utime(realname, &tbuf);
		} else
			fbuf->fb_err = -ENOSYS;
	}

	if (!fbuf->fb_err && (io->fi_flags & FUSE_FATTR_SIZE)) {
		if (f->op.truncate)
			fbuf->fb_err = f->op.truncate(realname,
			    fbuf->fb_attr.st_size);
		else
			fbuf->fb_err = -ENOSYS;
	}

	memset(&fbuf->fb_attr, 0, sizeof(struct stat));

	if (!fbuf->fb_err)
		fbuf->fb_err = update_attr(f, &fbuf->fb_attr, realname, vn);
	free(realname);

	return (0);
}

static int
ifuse_ops_symlink(unused struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_vnode *vn;
	char *realname;
	int len;

	CHECK_OPT(symlink);

	vn = get_vn_by_name_and_parent(f, fbuf->fb_dat, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	len = strlen((char *)fbuf->fb_dat);

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	/* fuse invert the symlink params */
	fbuf->fb_err = f->op.symlink((const char *)&fbuf->fb_dat[len + 1],
	    realname);
	fbuf->fb_ino = vn->ino;
	free(realname);

	return (0);
}

static int
ifuse_ops_rename(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_vnode *vnt;
	struct fuse_vnode *vnf;
	char *realnamef;
	char *realnamet;
	int len;

	CHECK_OPT(rename);

	len = strlen((char *)fbuf->fb_dat);
	vnf = get_vn_by_name_and_parent(f, fbuf->fb_dat, fbuf->fb_ino);
	if (vnf == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	vnt = get_vn_by_name_and_parent(f, &fbuf->fb_dat[len + 1],
	    fbuf->fb_io_ino);
	if (vnt == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realnamef = build_realname(f, vnf->ino);
	if (realnamef == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realnamet = build_realname(f, vnt->ino);
	if (realnamet == NULL) {
		fbuf->fb_err = -errno;
		free(realnamef);
		return (0);
	}

	fbuf->fb_err = f->op.rename(realnamef, realnamet);
	free(realnamef);
	free(realnamet);

	return (0);
}

static int
ifuse_ops_destroy(struct fuse *f)
{
	DPRINTF("Opcode: destroy\n");

	f->fc->dead = 1;

	return (0);
}

static int
ifuse_ops_reclaim(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_vnode *vn;

	DPRINTF("Opcode: reclaim\t");
	DPRINTF("Inode: %llu\t", (unsigned long long)fbuf->fb_ino);

	vn = tree_get(&f->vnode_tree, fbuf->fb_ino);
	if (vn != NULL)
		unref_vn(f, vn);

	return (0);
}

static int
ifuse_ops_mknod(struct fuse *f, struct fusebuf *fbuf)
{
	struct fuse_vnode *vn;
	char *realname;
	uint32_t mode;
	dev_t dev;

	CHECK_OPT(mknod);

	mode = fbuf->fb_io_mode;
	dev = fbuf->fb_io_rdev;
	vn = get_vn_by_name_and_parent(f, fbuf->fb_dat, fbuf->fb_ino);
	if (vn == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	realname = build_realname(f, vn->ino);
	if (realname == NULL) {
		fbuf->fb_err = -errno;
		return (0);
	}

	fbuf->fb_err = f->op.mknod(realname, mode, dev);

	if (!fbuf->fb_err) {
		fbuf->fb_err = update_attr(f, &fbuf->fb_attr, realname, vn);
		fbuf->fb_io_mode = fbuf->fb_attr.st_mode;
		fbuf->fb_ino = vn->ino;
	}
	free(realname);

	return (0);
}

int
ifuse_exec_opcode(struct fuse *f, struct fusebuf *fbuf)
{
	int ret = 0;

	fbuf->fb_len = 0;
	fbuf->fb_err = 0;

	switch (fbuf->fb_type) {
	case FBT_LOOKUP:
		ret = ifuse_ops_lookup(f, fbuf);
		break;
	case FBT_GETATTR:
		ret = ifuse_ops_getattr(f, fbuf);
		break;
	case FBT_SETATTR:
		ret = ifuse_ops_setattr(f, fbuf);
		break;
	case FBT_READLINK:
		ret = ifuse_ops_readlink(f, fbuf);
		break;
	case FBT_MKDIR:
		ret = ifuse_ops_mkdir(f, fbuf);
		break;
	case FBT_UNLINK:
		ret = ifuse_ops_unlink(f, fbuf);
		break;
	case FBT_RMDIR:
		ret = ifuse_ops_rmdir(f, fbuf);
		break;
	case FBT_LINK:
		ret = ifuse_ops_link(f, fbuf);
		break;
	case FBT_OPEN:
		ret = ifuse_ops_open(f, fbuf);
		break;
	case FBT_READ:
		ret = ifuse_ops_read(f, fbuf);
		break;
	case FBT_WRITE:
		ret = ifuse_ops_write(f, fbuf);
		break;
	case FBT_STATFS:
		ret = ifuse_ops_statfs(f, fbuf);
		break;
	case FBT_RELEASE:
		ret = ifuse_ops_release(f, fbuf);
		break;
	case FBT_FSYNC:
		ret = ifuse_ops_fsync(f, fbuf);
		break;
	case FBT_FLUSH:
		ret = ifuse_ops_flush(f, fbuf);
		break;
	case FBT_INIT:
		ret = ifuse_ops_init(f);
		break;
	case FBT_OPENDIR:
		ret = ifuse_ops_opendir(f, fbuf);
		break;
	case FBT_READDIR:
		ret = ifuse_ops_readdir(f, fbuf);
		break;
	case FBT_RELEASEDIR:
		ret = ifuse_ops_releasedir(f, fbuf);
		break;
	case FBT_ACCESS:
		ret = ifuse_ops_access(f, fbuf);
		break;
	case FBT_SYMLINK:
		ret = ifuse_ops_symlink(f, fbuf);
		break;
	case FBT_RENAME:
		ret = ifuse_ops_rename(f, fbuf);
		break;
	case FBT_DESTROY:
		ret = ifuse_ops_destroy(f);
		break;
	case FBT_RECLAIM:
		ret = ifuse_ops_reclaim(f, fbuf);
		break;
	case FBT_MKNOD:
		ret = ifuse_ops_mknod(f, fbuf);
		break;
	default:
		DPRINTF("Opcode: %i not supported\t", fbuf->fb_type);
		DPRINTF("Inode: %llu\t", (unsigned long long)fbuf->fb_ino);

		fbuf->fb_err = -ENOSYS;
		fbuf->fb_len = 0;
	}
	DPRINTF("\n");

	/* fuse api use negative errno */
	fbuf->fb_err = -fbuf->fb_err;
	return (ret);
}
