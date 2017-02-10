#ifdef __FreeBSD__
#include <sys/param.h>
#endif

#include <stdio.h>
#include <time.h>
#include <argp.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <archive.h>
#include <archive_entry.h>
#include <lkl.h>
#include <lkl_host.h>

char doc[] = "";
char args_doc[] = "-t fstype fsimage_path tar_path";
static struct argp_option options[] = {
	{"enable-printk", 'p', 0, 0, "show Linux printks"},
	{"partition", 'P', "int", 0, "partition number"},
	{"filesystem-type", 't', "string", 0,
	 "select filesystem type - mandatory"},
	{"selinux-contexts", 's', "file", 0,
	 "export selinux contexts to file"},
	{0},
};

static struct cl_args {
	int printk;
	int part;
	const char *fsimg_type;
	const char *fsimg_path;
	const char *tar_path;
	FILE *selinux;
} cla;

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct cl_args *cla = state->input;

	switch (key) {
	case 'p':
		cla->printk = 1;
		break;
	case 'P':
		cla->part = atoi(arg);
		break;
	case 't':
		cla->fsimg_type = arg;
		break;
	case 's':
		cla->selinux = fopen(arg, "w");
		if (!cla->selinux) {
			fprintf(stderr, "failed to open selinux contexts file: %s\n",
				strerror(errno));
			return -1;
		}
		break;
	case ARGP_KEY_ARG:
		if (!cla->fsimg_path)
			cla->fsimg_path = arg;
		else if (!cla->tar_path)
			cla->tar_path = arg;
		else
			return -1;
		break;
	case ARGP_KEY_END:
		if (state->arg_num < 2 || !cla->fsimg_type)
			argp_usage(state);
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc };

static struct archive *tar;

static int searchdir(const char *fsimg_path, const char *path);

static int copy_file(const char *fsimg_path, const char *path)
{
	long fsimg_fd;
	char buff[4096];
	long len, wrote;
	int ret = 0;

	fsimg_fd = lkl_sys_open(fsimg_path, LKL_O_RDONLY, 0);
	if (fsimg_fd < 0) {
		fprintf(stderr, "fsimg error opening %s: %s\n", fsimg_path,
			lkl_strerror(fsimg_fd));
		return fsimg_fd;
	}

	do {
		len = lkl_sys_read(fsimg_fd, buff, sizeof(buff));
		if (len > 0) {
			wrote = archive_write_data(tar, buff, len);
			if (wrote != len) {
				fprintf(stderr, "error writing file %s to archive: %s [%d %ld]\n",
					path, archive_error_string(tar), ret,
					len);
				ret = -archive_errno(tar);
				break;
			}
		}

		if (len < 0) {
			fprintf(stderr, "error reading fsimg file %s: %s\n",
				fsimg_path, lkl_strerror(len));
			ret = len;
		}

	} while (len > 0);

	lkl_sys_close(fsimg_fd);

	return ret;
}

static int add_link(const char *fsimg_path, const char *path,
		    struct archive_entry *entry)
{
	char buf[4096] = { 0, };
	long len;

	len = lkl_sys_readlink(fsimg_path, buf, sizeof(buf));
	if (len < 0) {
		fprintf(stderr, "fsimg readlink error %s: %s\n",
			fsimg_path, lkl_strerror(len));
		return len;
	}

	archive_entry_set_symlink(entry, buf);

	return 0;
}

static inline void fsimg_copy_stat(struct stat *st, struct lkl_stat *fst)
{
	st->st_dev = fst->st_dev;
	st->st_ino = fst->st_ino;
	st->st_mode = fst->st_mode;
	st->st_nlink = fst->st_nlink;
	st->st_uid = fst->st_uid;
	st->st_gid = fst->st_gid;
	st->st_rdev = fst->st_rdev;
	st->st_size = fst->st_size;
	st->st_blksize = fst->st_blksize;
	st->st_blocks = fst->st_blocks;
	st->st_atim.tv_sec = fst->lkl_st_atime;
	st->st_atim.tv_nsec = fst->st_atime_nsec;
	st->st_mtim.tv_sec = fst->lkl_st_mtime;
	st->st_mtim.tv_nsec = fst->st_mtime_nsec;
	st->st_ctim.tv_sec = fst->lkl_st_ctime;
	st->st_ctim.tv_nsec = fst->st_ctime_nsec;
}

static int copy_xattr(const char *fsimg_path, const char *path,
		      struct archive_entry *entry)
{
	long ret;
	char *xattr_list, *i;
	long xattr_list_size;

	ret = lkl_sys_llistxattr(fsimg_path, NULL, 0);
	if (ret < 0) {
		fprintf(stderr, "fsimg llistxattr(%s) error: %s\n",
			path, lkl_strerror(ret));
		return ret;
	}

	if (!ret)
		return 0;

	xattr_list = malloc(ret);

	ret = lkl_sys_llistxattr(fsimg_path, xattr_list, ret);
	if (ret < 0) {
		fprintf(stderr, "fsimg llistxattr(%s) error: %s\n", path,
			lkl_strerror(ret));
		free(xattr_list);
		return ret;
	}

	xattr_list_size = ret;

	for (i = xattr_list; i - xattr_list < xattr_list_size;
	     i += strlen(i) + 1) {
		void *xattr_buf;

		ret = lkl_sys_lgetxattr(fsimg_path, i, NULL, 0);
		if (ret < 0) {
			fprintf(stderr, "fsimg lgetxattr(%s) error: %s\n", path,
				lkl_strerror(ret));
			free(xattr_list);
			return ret;
		}

		xattr_buf = malloc(ret);

		ret = lkl_sys_lgetxattr(fsimg_path, i, xattr_buf, ret);
		if (ret < 0) {
			fprintf(stderr, "fsimg lgetxattr2(%s) error: %s\n",
				path, lkl_strerror(ret));
			free(xattr_list);
			free(xattr_buf);
			return ret;
		}

		if (cla.selinux && strcmp(i, "security.selinux") == 0)
			fprintf(cla.selinux, "%s %s\n", path,
				(char *)xattr_buf);

		archive_entry_xattr_clear(entry);
		archive_entry_xattr_add_entry(entry, i, xattr_buf, ret);

		free(xattr_buf);
	}

	free(xattr_list);

	return 0;
}

static int do_entry(const char *fsimg_path, const char *path,
		    const struct lkl_linux_dirent64 *de)
{
	char fsimg_new_path[PATH_MAX], new_path[PATH_MAX];
	struct lkl_stat fsimg_stat;
	struct stat stat;
	struct archive_entry *entry;
	int ftype;
	long ret;

	snprintf(new_path, sizeof(new_path), "%s/%s", path, de->d_name);
	snprintf(fsimg_new_path, sizeof(fsimg_new_path), "%s/%s", fsimg_path,
		 de->d_name);

	ret = lkl_sys_lstat(fsimg_new_path, &fsimg_stat);
	if (ret) {
		fprintf(stderr, "fsimg lstat(%s) error: %s\n",
			path, lkl_strerror(ret));
		return ret;
	}

	entry = archive_entry_new();

	archive_entry_set_pathname(entry, new_path);
	fsimg_copy_stat(&stat, &fsimg_stat);
	archive_entry_copy_stat(entry, &stat);
	ret = copy_xattr(fsimg_new_path, new_path, entry);
	if (ret)
		return ret;
	/* TODO: ACLs */

	ftype = stat.st_mode & S_IFMT;

	switch (ftype) {
	case S_IFREG:
		archive_write_header(tar, entry);
		ret = copy_file(fsimg_new_path, new_path);
		break;
	case S_IFDIR:
		archive_write_header(tar, entry);
		ret = searchdir(fsimg_new_path, new_path);
		break;
	case S_IFLNK:
		ret = add_link(fsimg_new_path, new_path, entry);
		/* fall through */
	case S_IFSOCK:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO:
		if (ret)
			break;
		archive_write_header(tar, entry);
		break;
	default:
		printf("skipping %s: unsupported entry type %d\n", new_path,
		       ftype);
	}

	archive_entry_free(entry);

	if (ret)
		printf("error processing entry %s, aborting\n", new_path);

	return ret;
}

static int searchdir(const char *fsimg_path, const char *path)
{
	long ret, fd;
	char buf[1024], *pos;
	long buf_len;

	fd = lkl_sys_open(fsimg_path, LKL_O_RDONLY | LKL_O_DIRECTORY, 0);
	if (fd < 0) {
		fprintf(stderr, "failed to open dir %s: %s", fsimg_path,
			lkl_strerror(fd));
		return fd;
	}

	do {
		struct lkl_linux_dirent64 *de;

		de = (struct lkl_linux_dirent64 *) buf;
		buf_len = lkl_sys_getdents64(fd, de, sizeof(buf));
		if (buf_len < 0) {
			fprintf(stderr, "gentdents64 error: %s\n",
				lkl_strerror(buf_len));
			break;
		}

		for (pos = buf; pos - buf < buf_len; pos += de->d_reclen) {
			de = (struct lkl_linux_dirent64 *)pos;

			if (!strcmp(de->d_name, ".") ||
			    !strcmp(de->d_name, ".."))
				continue;

			ret = do_entry(fsimg_path, path, de);
			if (ret)
				goto out;
		}

	} while (buf_len > 0);

out:
	lkl_sys_close(fd);
	return ret;
}

int main(int argc, char **argv)
{
	struct lkl_disk disk;
	long ret;
	char mpoint[32];
	unsigned int disk_id;

	if (argp_parse(&argp, argc, argv, 0, 0, &cla) < 0)
		return -1;

	if (!cla.printk)
		lkl_host_ops.print = NULL;

	disk.fd = open(cla.fsimg_path, O_RDONLY);
	if (disk.fd < 0) {
		fprintf(stderr, "can't open fsimg %s: %s\n", cla.fsimg_path,
			strerror(errno));
		ret = 1;
		goto out;
	}

	disk.ops = NULL;

	ret = lkl_disk_add(&disk);
	if (ret < 0) {
		fprintf(stderr, "can't add disk: %s\n", lkl_strerror(ret));
		goto out_close;
	}
	disk_id = ret;

	lkl_start_kernel(&lkl_host_ops, "mem=10M");

	ret = lkl_mount_dev(disk_id, cla.part, cla.fsimg_type, LKL_MS_RDONLY,
			    NULL, mpoint, sizeof(mpoint));
	if (ret) {
		fprintf(stderr, "can't mount disk: %s\n", lkl_strerror(ret));
		goto out_close;
	}

	ret = lkl_sys_chdir(mpoint);
	if (ret) {
		fprintf(stderr, "can't chdir to %s: %s\n", mpoint,
			lkl_strerror(ret));
		goto out_umount;
	}

	tar = archive_write_new();
	archive_write_set_format_pax_restricted(tar);
	archive_write_open_filename(tar, cla.tar_path);

	ret = searchdir(mpoint, "");

	archive_write_free(tar);

	if (cla.selinux)
		fclose(cla.selinux);

out_umount:
	lkl_umount_dev(disk_id, cla.part, 0, 1000);

out_close:
	close(disk.fd);

out:
	lkl_sys_halt();

	return ret;
}
