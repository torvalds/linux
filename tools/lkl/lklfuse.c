#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#define FUSE_USE_VERSION 35
#include <fuse3/fuse.h>
#include <fuse3/fuse_opt.h>
#include <fuse3/fuse_lowlevel.h>
#include <lkl.h>
#include <lkl_host.h>

#define LKLFUSE_VERSION "0.3"

struct lklfuse {
	const char *file;
	const char *log;
	const char *type;
	const char *opts;
	struct lkl_disk disk;
	int disk_id;
	int part;
	int ro;
	int mb;
} lklfuse = {
	.mb = 64,
};

#define LKLFUSE_OPT(t, p, v) { t, offsetof(struct lklfuse, p), v }

enum {
	KEY_HELP,
	KEY_VERSION,
};

static struct fuse_opt lklfuse_opts[] = {
	LKLFUSE_OPT("log=%s", log, 0),
	LKLFUSE_OPT("type=%s", type, 0),
	LKLFUSE_OPT("mb=%d", mb, 0),
	LKLFUSE_OPT("opts=%s", opts, 0),
	LKLFUSE_OPT("part=%d", part, 0),
	FUSE_OPT_KEY("-h", KEY_HELP),
	FUSE_OPT_KEY("--help", KEY_HELP),
	FUSE_OPT_KEY("-V",             KEY_VERSION),
	FUSE_OPT_KEY("--version",      KEY_VERSION),
	FUSE_OPT_END
};

static void usage(void)
{
	printf(
"usage: lklfuse file mountpoint [options]\n"
"\n"
"general options:\n"
"    -o opt,[opt...]        mount options\n"
"    -h   --help            print help\n"
"    -V   --version         print version\n"
"\n"
"lklfuse options:\n"
"    -o log=FILE            log file\n"
"    -o type=fstype         filesystem type\n"
"    -o mb=memory           amount of memory to allocate in MB (default: 64)\n"
"    -o part=parition       partition to mount\n"
"    -o ro                  open file read-only\n"
"    -o opts=options        mount options (use \\ to escape , and =)\n"
);
}

static int lklfuse_opt_proc(void *data, const char *arg, int key,
			  struct fuse_args *args)
{
	switch (key) {
	case FUSE_OPT_KEY_OPT:
		if (strcmp(arg, "ro") == 0)
			lklfuse.ro = 1;
		return 1;

	case FUSE_OPT_KEY_NONOPT:
		if (!lklfuse.file) {
			lklfuse.file = strdup(arg);
			return 0;
		}
		return 1;

	case KEY_HELP:
		usage();
		/* suppress fuse usage */
		args->argv[0] = "";
		fuse_opt_add_arg(args, "-h");
		fuse_main(args->argc, args->argv, NULL, NULL);
		exit(1);

	case KEY_VERSION:
		printf("lklfuse version %s\n", LKLFUSE_VERSION);
		fuse_opt_add_arg(args, "--version");
		fuse_main(args->argc, args->argv, NULL, NULL);
		exit(0);

	default:
		fprintf(stderr, "internal error\n");
		abort();
	}
}

static void lklfuse_xlat_stat(const struct lkl_stat *in, struct stat *st)
{
	st->st_dev = in->st_dev;
	st->st_ino = in->st_ino;
	st->st_mode = in->st_mode;
	st->st_nlink = in->st_nlink;
	st->st_uid = in->st_uid;
	st->st_gid = in->st_gid;
	st->st_rdev = in->st_rdev;
	st->st_size = in->st_size;
	st->st_blksize = in->st_blksize;
	st->st_blocks = in->st_blocks;
	st->st_atim.tv_sec = in->lkl_st_atime;
	st->st_atim.tv_nsec = in->st_atime_nsec;
	st->st_mtim.tv_sec = in->lkl_st_mtime;
	st->st_mtim.tv_nsec = in->st_mtime_nsec;
	st->st_ctim.tv_sec = in->lkl_st_ctime;
	st->st_ctim.tv_nsec = in->st_ctime_nsec;
}

static int lklfuse_getattr(const char *path, struct stat *st,
			   struct fuse_file_info *fi)
{
	long ret;
	struct lkl_stat lkl_stat;

	/*
	 * With nullpath_ok, path will be provided only if the struct
	 * fuse_file_info argument is NULL.
	 */
	if (fi)
		ret = lkl_sys_fstat(fi->fh, &lkl_stat);
	else
		ret = lkl_sys_lstat(path, &lkl_stat);
	if (!ret)
		lklfuse_xlat_stat(&lkl_stat, st);

	return ret;
}

static int lklfuse_readlink(const char *path, char *buf, size_t len)
{
	long ret;

	ret = lkl_sys_readlink(path, buf, len);
	if (ret < 0)
		return ret;

	if ((size_t)ret == len)
		ret = len - 1;

	buf[ret] = 0;

	return 0;
}

static int lklfuse_mknod(const char *path, mode_t mode, dev_t dev)
{
	return lkl_sys_mknod(path, mode, dev);
}

static int lklfuse_mkdir(const char *path, mode_t mode)
{
	return lkl_sys_mkdir(path, mode);
}

static int lklfuse_unlink(const char *path)
{
	return lkl_sys_unlink(path);
}

static int lklfuse_rmdir(const char *path)
{
	return lkl_sys_rmdir(path);
}

static int lklfuse_symlink(const char *oldname, const char *newname)
{
	return lkl_sys_symlink(oldname, newname);
}


static int lklfuse_rename(const char *oldname, const char *newname,
			  unsigned int flags)
{
	/* libfuse: *flags* may be `RENAME_EXCHANGE` or `RENAME_NOREPLACE` */
	return lkl_sys_renameat2(LKL_AT_FDCWD, oldname, LKL_AT_FDCWD, newname,
				 flags);
}

static int lklfuse_link(const char *oldname, const char *newname)
{
	return lkl_sys_link(oldname, newname);
}

static int lklfuse_chmod(const char *path, mode_t mode,
			 struct fuse_file_info *fi)
{
	int ret;

	if (fi)
		ret = lkl_sys_fchmod(fi->fh, mode);
	else
		ret = lkl_sys_fchmodat(LKL_AT_FDCWD, path, mode);

	return ret;
}

static int lklfuse_chown(const char *path, uid_t uid, gid_t gid,
			 struct fuse_file_info *fi)
{
	int ret;

	if (fi)
		ret = lkl_sys_fchown(fi->fh, uid, gid);
	else
		ret = lkl_sys_fchownat(LKL_AT_FDCWD, path, uid, gid,
				LKL_AT_SYMLINK_NOFOLLOW);
	return ret;
}

static int lklfuse_truncate(const char *path, off_t off,
			    struct fuse_file_info *fi)
{
	int ret;

	if (fi)
		ret = lkl_sys_ftruncate(fi->fh, off);
	else
		ret = lkl_sys_truncate(path, off);

	return ret;
}

static int lklfuse_open3(const char *path, bool create, mode_t mode,
	                 struct fuse_file_info *fi)
{
	long ret;
	int flags;

	if ((fi->flags & O_ACCMODE) == O_RDONLY)
		flags = LKL_O_RDONLY;
	else if ((fi->flags & O_ACCMODE) == O_WRONLY)
		flags = LKL_O_WRONLY;
	else if ((fi->flags & O_ACCMODE) == O_RDWR)
		flags = LKL_O_RDWR;
	else
		return -EINVAL;

	if (create)
		flags |= LKL_O_CREAT;

	ret = lkl_sys_open(path, flags, mode);
	if (ret < 0)
		return ret;

	fi->fh = ret;

	return 0;
}

static int lklfuse_create(const char *path, mode_t mode,
	                  struct fuse_file_info *fi)
{
	return lklfuse_open3(path, true, mode, fi);
}

static int lklfuse_open(const char *path, struct fuse_file_info *fi)
{
	return lklfuse_open3(path, false, 0, fi);
}

static int lklfuse_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	long ret;
	ssize_t orig_size = size;

	do {
		ret = lkl_sys_pread64(fi->fh, buf, size, offset);
		if (ret <= 0)
			break;
		size -= ret;
		offset += ret;
		buf += ret;
	} while (size > 0);

	return ret < 0 ? ret : orig_size - (ssize_t)size;

}

static int lklfuse_write(const char *path, const char *buf, size_t size,
		       off_t offset, struct fuse_file_info *fi)
{
	long ret;
	ssize_t orig_size = size;

	do {
		ret = lkl_sys_pwrite64(fi->fh, buf, size, offset);
		if (ret <= 0)
			break;
		size -= ret;
		offset += ret;
		buf += ret;
	} while (size > 0);

	return ret < 0 ? ret : orig_size - (ssize_t)size;
}


static int lklfuse_statfs(const char *path, struct statvfs *stat)
{
	long ret;
	struct lkl_statfs lkl_statfs;

	ret = lkl_sys_statfs(path, &lkl_statfs);
	if (ret < 0)
		return ret;

	stat->f_bsize = lkl_statfs.f_bsize;
	stat->f_frsize = lkl_statfs.f_frsize;
	stat->f_blocks = lkl_statfs.f_blocks;
	stat->f_bfree = lkl_statfs.f_bfree;
	stat->f_bavail = lkl_statfs.f_bavail;
	stat->f_files = lkl_statfs.f_files;
	stat->f_ffree = lkl_statfs.f_ffree;
	stat->f_favail = stat->f_ffree;
	stat->f_fsid = *(unsigned long *)&lkl_statfs.f_fsid.val[0];
	stat->f_flag = lkl_statfs.f_flags;
	stat->f_namemax = lkl_statfs.f_namelen;

	return 0;
}

static int lklfuse_flush(const char *path, struct fuse_file_info *fi)
{
	return 0;
}

static int lklfuse_release(const char *path, struct fuse_file_info *fi)
{
	return lkl_sys_close(fi->fh);
}

static int lklfuse_fsync(const char *path, int datasync,
		       struct fuse_file_info *fi)
{
	if (datasync)
		return lkl_sys_fdatasync(fi->fh);
	else
		return lkl_sys_fsync(fi->fh);
}

static int lklfuse_setxattr(const char *path, const char *name, const char *val,
		   size_t size, int flags)
{
	return lkl_sys_setxattr(path, name, val, size, flags);
}

static int lklfuse_getxattr(const char *path, const char *name, char *val,
			  size_t size)
{
	return lkl_sys_getxattr(path, name, val, size);
}

static int lklfuse_listxattr(const char *path, char *list, size_t size)
{
	return lkl_sys_listxattr(path, list, size);
}

static int lklfuse_removexattr(const char *path, const char *name)
{
	return lkl_sys_removexattr(path, name);
}

static int lklfuse_opendir(const char *path, struct fuse_file_info *fi)
{
	struct lkl_dir *dir;
	int err;

	dir = lkl_opendir(path, &err);
	if (!dir)
		return err;

	fi->fh = (uintptr_t)dir;

	return 0;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
static int lklfuse_readdir(const char *path, void *buf, fuse_fill_dir_t fill,
			 off_t off, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags)
{
	struct lkl_dir *dir = (struct lkl_dir *)(uintptr_t)fi->fh;
	struct lkl_linux_dirent64 *de;

	while ((de = lkl_readdir(dir))) {
		struct stat st = { 0, };

		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;

		if (fill(buf, de->d_name, &st, 0, 0))
			break;
	}

	if (!de)
		return lkl_errdir(dir);

	return 0;
}

static int lklfuse_releasedir(const char *path, struct fuse_file_info *fi)
{
	struct lkl_dir *dir = (struct lkl_dir *)(uintptr_t)fi->fh;

	return lkl_closedir(dir);
}

static int lklfuse_fsyncdir(const char *path, int datasync,
			  struct fuse_file_info *fi)
{
	struct lkl_dir *dir = (struct lkl_dir *)(uintptr_t)fi->fh;
	int fd = lkl_dirfd(dir);

	if (datasync)
		return lkl_sys_fdatasync(fd);
	else
		return lkl_sys_fsync(fd);
}

static int lklfuse_access(const char *path, int mode)
{
	return lkl_sys_access(path, mode);
}

static int lklfuse_utimens(const char *path, const struct timespec tv[2],
			   struct fuse_file_info *fi)
{
	int ret;
	struct lkl_timespec ts[2] = {
		{ .tv_sec = tv[0].tv_sec, .tv_nsec = tv[0].tv_nsec },
		{ .tv_sec = tv[1].tv_sec, .tv_nsec = tv[1].tv_nsec },
	};

	if (fi)
		ret = lkl_sys_utimensat(fi->fh, NULL,
					(struct __lkl__kernel_timespec *)ts,
					0);
	else
		ret = lkl_sys_utimensat(-1, path,
					(struct __lkl__kernel_timespec *)ts,
					LKL_AT_SYMLINK_NOFOLLOW);
	return ret;
}

static int lklfuse_fallocate(const char *path, int mode, off_t offset,
			     off_t len, struct fuse_file_info *fi)
{
	return lkl_sys_fallocate(fi->fh, mode, offset, len);
}

static void *lklfuse_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	cfg->nullpath_ok = 1;
	return NULL;
}

const struct fuse_operations lklfuse_ops = {
	.init = lklfuse_init,

	.getattr = lklfuse_getattr,
	.readlink = lklfuse_readlink,
	.mknod = lklfuse_mknod,
	.mkdir = lklfuse_mkdir,
	.unlink = lklfuse_unlink,
	.rmdir = lklfuse_rmdir,
	.symlink = lklfuse_symlink,
	.rename = lklfuse_rename,
	.link = lklfuse_link,
	.chmod = lklfuse_chmod,
	.chown = lklfuse_chown,
	.truncate = lklfuse_truncate,
	.open = lklfuse_open,
	.read = lklfuse_read,
	.write = lklfuse_write,
	.statfs = lklfuse_statfs,
	.flush = lklfuse_flush,
	.release = lklfuse_release,
	.fsync = lklfuse_fsync,
	.setxattr = lklfuse_setxattr,
	.getxattr = lklfuse_getxattr,
	.listxattr = lklfuse_listxattr,
	.removexattr = lklfuse_removexattr,
	.opendir = lklfuse_opendir,
	.readdir = lklfuse_readdir,
	.releasedir = lklfuse_releasedir,
	.fsyncdir = lklfuse_fsyncdir,
	.access = lklfuse_access,
	.create = lklfuse_create,
	/* .lock, */
	.utimens = lklfuse_utimens,
	/* .bmap, */
	/* .ioctl, */
	/* .poll, */
	/* .write_buf, (SG io) */
	/* .read_buf, (SG io) */
	/* .flock, */
	.fallocate = lklfuse_fallocate,
};

static int start_lkl(void)
{
	long ret;
	char mpoint[32];
	struct timespec walltime;
	struct lkl_timespec ts;

	ret = lkl_start_kernel("mem=%dM", lklfuse.mb);
	if (ret) {
		fprintf(stderr, "can't start kernel: %s\n", lkl_strerror(ret));
		goto out;
	}

	/* forward host walltime to lkl */
	ret = clock_gettime(CLOCK_REALTIME, &walltime);
	if (ret < 0)
		goto out_halt;

	ts = (struct lkl_timespec){ .tv_sec = walltime.tv_sec,
				    .tv_nsec = walltime.tv_nsec };
	ret = lkl_sys_clock_settime(LKL_CLOCK_REALTIME,
				    (struct __lkl__kernel_timespec *)&ts);
	if (ret < 0) {
		fprintf(stderr, "lkl_sys_clock_settime() failed: %s\n",
			lkl_strerror(ret));
		goto out_halt;
	}

	ret = lkl_mount_dev(lklfuse.disk_id, lklfuse.part, lklfuse.type,
			    lklfuse.ro ? LKL_MS_RDONLY : 0, lklfuse.opts,
			    mpoint, sizeof(mpoint));

	if (ret) {
		fprintf(stderr, "can't mount disk: %s\n", lkl_strerror(ret));
		goto out_halt;
	}

	ret = lkl_sys_chroot(mpoint);
	if (ret) {
		fprintf(stderr, "can't chdir to %s: %s\n", mpoint,
			lkl_strerror(ret));
		goto out_umount;
	}

	return 0;

out_umount:
	lkl_umount_dev(lklfuse.disk_id, lklfuse.part, 0, 1000);

out_halt:
	lkl_sys_halt();

out:
	return ret;
}

static void stop_lkl(void)
{
	int ret;

	ret = lkl_sys_chdir("/");
	if (ret)
		fprintf(stderr, "can't chdir to /: %s\n", lkl_strerror(ret));
	ret = lkl_sys_umount("/", 0);
	if (ret)
		fprintf(stderr, "failed to umount disk: %d: %s\n",
			lklfuse.disk_id, lkl_strerror(ret));
	lkl_sys_halt();
}

int main(int argc, char **argv)
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_cmdline_opts cli_opts;
	struct fuse *fuse;
	struct stat st;
	int ret;

	if (fuse_opt_parse(&args, &lklfuse, lklfuse_opts, lklfuse_opt_proc))
		return 1;

	if (!lklfuse.file || !lklfuse.type) {
		fprintf(stderr, "no file or filesystem type specified\n");
		return 1;
	}

	if (fuse_parse_cmdline(&args, &cli_opts))
		return 1;

	ret = stat(cli_opts.mountpoint, &st);
	if (ret) {
		perror(cli_opts.mountpoint);
		goto out_free;
	}

	ret = open(lklfuse.file, lklfuse.ro ? O_RDONLY : O_RDWR);
	if (ret < 0) {
		perror(lklfuse.file);
		goto out_free;
	}

	lklfuse.disk.fd = ret;

	ret = lkl_init(&lkl_host_ops);
	if (ret < 0) {
		fprintf(stderr, "lkl init failed: %s\n", lkl_strerror(ret));
		goto out_close_disk;
	}

	ret = lkl_disk_add(&lklfuse.disk);
	if (ret < 0) {
		fprintf(stderr, "can't add disk: %s\n", lkl_strerror(ret));
		goto out_lkl_cleanup;
	}

	lklfuse.disk_id = ret;

	fuse = fuse_new(&args, &lklfuse_ops, sizeof(lklfuse_ops), NULL);
	if (!fuse) {
		ret = -1;
		goto out_close_disk;
	}

	ret = fuse_set_signal_handlers(fuse_get_session(fuse));
	if (ret < 0)
		goto out_fuse_destroy;

	ret = fuse_mount(fuse, cli_opts.mountpoint);
	if (ret < 0)
		goto out_remove_signals;

	fuse_opt_free_args(&args);

	ret = fuse_daemonize(cli_opts.foreground);
	if (ret < 0)
		goto out_fuse_unmount;

	ret = start_lkl();
	if (ret) {
		ret = -1;
		goto out_fuse_unmount;
	}

	if (!cli_opts.singlethread)
		fprintf(stderr, "warning: multithreaded mode not supported\n");

	ret = fuse_loop(fuse);

	stop_lkl();

out_fuse_unmount:
	fuse_unmount(fuse);

out_remove_signals:
	fuse_remove_signal_handlers(fuse_get_session(fuse));

out_fuse_destroy:
	fuse_destroy(fuse);

out_lkl_cleanup:
	lkl_cleanup();

out_close_disk:
	close(lklfuse.disk.fd);

out_free:
	free(cli_opts.mountpoint);

	return ret < 0 ? 1 : 0;
}
