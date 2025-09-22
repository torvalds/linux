/* $OpenBSD: fuse.c,v 1.55 2025/09/20 15:01:23 helg Exp $ */
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

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <miscfs/fuse/fusefs.h>

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fuse_opt.h"
#include "fuse_private.h"
#include "debug.h"

static struct fuse_context *ictx = NULL;

enum {
	KEY_DEBUG,
	KEY_FOREGROUND,
	KEY_HELP,
	KEY_HELP_WITHOUT_HEADER,
	KEY_VERSION,
	KEY_MAXREAD,
	KEY_STUB
};

/* options supported by fuse_parse_cmdline */
static struct fuse_opt fuse_core_opts[] = {
	FUSE_OPT_KEY("-d",			KEY_DEBUG),
	FUSE_OPT_KEY("debug",			KEY_DEBUG),
	FUSE_OPT_KEY("-f",			KEY_FOREGROUND),
	FUSE_OPT_KEY("-h",			KEY_HELP),
	FUSE_OPT_KEY("--help",			KEY_HELP),
	FUSE_OPT_KEY("-ho",			KEY_HELP_WITHOUT_HEADER),
	FUSE_OPT_KEY("-s",			KEY_STUB),
	FUSE_OPT_KEY("-V",			KEY_VERSION),
	FUSE_OPT_KEY("--version",		KEY_VERSION),
	FUSE_OPT_END
};

/* options supported by fuse_new */
#define FUSE_LIB_OPT(o, m) {o, offsetof(struct fuse_config, m), 1}
static struct fuse_opt fuse_lib_opts[] = {
	FUSE_OPT_KEY("ac_attr_timeout=",	KEY_STUB),
	FUSE_OPT_KEY("attr_timeout=",		KEY_STUB),
	FUSE_OPT_KEY("auto_cache",		KEY_STUB),
	FUSE_OPT_KEY("noauto_cache",		KEY_STUB),
	FUSE_OPT_KEY("big_writes",		KEY_STUB),
	FUSE_OPT_KEY("debug",			KEY_DEBUG),
	FUSE_OPT_KEY("-d",			KEY_DEBUG),
	FUSE_OPT_KEY("entry_timeout=",		KEY_STUB),
	FUSE_LIB_OPT("gid=",			set_gid),
	FUSE_LIB_OPT("gid=%u",			gid),
	FUSE_OPT_KEY("hard_remove",		KEY_STUB),
	FUSE_OPT_KEY("intr_signal",		KEY_STUB),
	FUSE_OPT_KEY("kernel_cache",		KEY_STUB),
	FUSE_OPT_KEY("large_read",		KEY_STUB),
	FUSE_OPT_KEY("modules=",		KEY_STUB),
	FUSE_OPT_KEY("negative_timeout=",	KEY_STUB),
	FUSE_OPT_KEY("readdir_ino",		KEY_STUB),
	FUSE_OPT_KEY("relatime",		KEY_STUB),
	FUSE_OPT_KEY("subtype=",		KEY_STUB),
	FUSE_LIB_OPT("uid=",			set_uid),
	FUSE_LIB_OPT("uid=%u",			uid),
	FUSE_LIB_OPT("use_ino",			use_ino),
	FUSE_OPT_KEY("dmask=%o",		KEY_STUB),
	FUSE_OPT_KEY("fmask=%o",		KEY_STUB),
	FUSE_LIB_OPT("umask=",			set_mode),
	FUSE_LIB_OPT("umask=%o",		umask),
	FUSE_OPT_END
};

/* options supported by fuse_mount */
#define FUSE_MOUNT_OPT(o, m) {o, offsetof(struct fuse_mount_opts, m), 1}
static struct fuse_opt fuse_mount_opts[] = {
	FUSE_MOUNT_OPT("allow_other",		allow_other),
	FUSE_OPT_KEY("allow_root",		KEY_STUB),
	FUSE_OPT_KEY("async_read",		KEY_STUB),
	FUSE_OPT_KEY("blkdev",			KEY_STUB),
	FUSE_OPT_KEY("blksize=",		KEY_STUB),
	FUSE_MOUNT_OPT("default_permissions",	def_perms),
	FUSE_OPT_KEY("direct_io",		KEY_STUB),
	FUSE_MOUNT_OPT("fsname=%s",		fsname),
	FUSE_MOUNT_OPT("max_read=%u",		max_read),
	FUSE_OPT_KEY("max_readahead",		KEY_STUB),
	FUSE_OPT_KEY("max_write",		KEY_STUB),
	FUSE_MOUNT_OPT("noatime",		noatime),
	FUSE_MOUNT_OPT("nonempty",		nonempty),
	FUSE_MOUNT_OPT("-r",			rdonly),
	FUSE_MOUNT_OPT("ro",			rdonly),
	FUSE_OPT_KEY("ro_fallback",		KEY_STUB),
	FUSE_OPT_KEY("sync_read",		KEY_STUB),
	FUSE_OPT_END
};

int
fuse_loop(struct fuse *fuse)
{
	struct fusebuf fbuf;
	struct fuse_context ctx;
	struct kevent event[5];
	struct kevent ev;
	struct iovec iov[2];
	size_t fb_dat_size = FUSEBUFMAXSIZE;
	ssize_t n;
	int ret, intr;

	if (fuse == NULL)
		return (-1);

	fuse->fc->kq = kqueue();
	if (fuse->fc->kq == -1)
		return (-1);

	EV_SET(&event[0], fuse->fc->fd, EVFILT_READ, EV_ADD |
	    EV_ENABLE, 0, 0, 0);

	/* signal events */
	EV_SET(&event[1], SIGCHLD, EVFILT_SIGNAL, EV_ADD |
	    EV_ENABLE, 0, 0, 0);
	EV_SET(&event[2], SIGHUP, EVFILT_SIGNAL, EV_ADD |
	    EV_ENABLE, 0, 0, 0);
	EV_SET(&event[3], SIGINT, EVFILT_SIGNAL, EV_ADD |
	    EV_ENABLE, 0, 0, 0);
	EV_SET(&event[4], SIGTERM, EVFILT_SIGNAL, EV_ADD |
	    EV_ENABLE, 0, 0, 0);

	/* prepare the read and write data buffer */
	fbuf.fb_dat = malloc(fb_dat_size);
	if (fbuf.fb_dat == NULL)
		return (-1);
	iov[0].iov_base = &fbuf;
	iov[0].iov_len  = sizeof(fbuf.fb_hdr) + sizeof(fbuf.FD);
	iov[1].iov_base = fbuf.fb_dat;

	intr = 0;
	while (!intr && !fuse->fc->dead) {
		ret = kevent(fuse->fc->kq, &event[0], 5, &ev, 1, NULL);
		if (ret == -1) {
			if (errno != EINTR)
				DPERROR(__func__);
		} else if (ret > 0 && ev.filter == EVFILT_SIGNAL) {
			int signum = ev.ident;
			switch (signum) {
			case SIGHUP:
			case SIGINT:
			case SIGTERM:
				DPRINTF("%s: %s\n", __func__,
				    strsignal(signum));
				intr = 1;
				break;
			default:
				fprintf(stderr, "%s: %s\n", __func__,
				    strsignal(signum));
			}
		} else if (ret > 0) {
			iov[1].iov_len = fb_dat_size;
			n = readv(fuse->fc->fd, iov, 2);
			if (n == -1) {
				fprintf(stderr, "%s: bad fusebuf read: %s\n",
				    __func__, strerror(errno));
				free(fbuf.fb_dat);
				return (-1);
			}
			if (n < (ssize_t)sizeof(fbuf.fb_hdr)) {
				fprintf(stderr, "%s: bad fusebuf read\n",
				    __func__);
				free(fbuf.fb_dat);
				return (-1);
			}
			if ((size_t)n != sizeof(fbuf.fb_hdr) + sizeof(fbuf.FD) +
                            fbuf.fb_len) {
				fprintf(stderr, "%s: bad fusebuf read\n",
				    __func__);
				free(fbuf.fb_dat);
				return (-1);
			}

			/*
			 * fuse_ops check that they do not write more than
			 * fb_io_len when writing to fb_dat
			 */
			if (fbuf.fb_io_len > fb_dat_size) {
				fprintf(stderr, "%s: io exceeds buffer size\n",
				    __func__);
				free(fbuf.fb_dat);
				return (-1);
			}

			ctx.fuse = fuse;
			ctx.uid = fbuf.fb_uid;
			ctx.gid = fbuf.fb_gid;
			ctx.pid = fbuf.fb_tid;
			ctx.umask = fbuf.fb_umask;
			ctx.private_data = fuse->private_data;
			ictx = &ctx;
			ret = ifuse_exec_opcode(fuse, &fbuf);
			if (ret) {
				ictx = NULL;
				free(fbuf.fb_dat);
				return (-1);
			}

			iov[1].iov_len = fbuf.fb_len;
			n = writev(fuse->fc->fd, iov, 2);

			ictx = NULL;

			if (n == -1 || (size_t)n != sizeof(fbuf.fb_hdr) +
			    sizeof(fbuf.FD) + fbuf.fb_len) {
				errno = EINVAL;
				free(fbuf.fb_dat);
				return (-1);
			}
		}
	}

	free(fbuf.fb_dat);
	return (0);
}
DEF(fuse_loop);

struct fuse_chan *
fuse_mount(const char *dir, struct fuse_args *args)
{
	struct fusefs_args fargs;
	struct fuse_mount_opts opts;
	struct fuse_chan *fc;
	const char *errcause;
	int mnt_flags;

	if (dir == NULL)
		return (NULL);

	fc = calloc(1, sizeof(*fc));
	if (fc == NULL)
		return (NULL);

	fc->dir = realpath(dir, NULL);
	if (fc->dir == NULL)
		goto bad;

	if ((fc->fd = open("/dev/fuse0", O_RDWR)) == -1) {
		perror(__func__);
		goto bad;
	}

	memset(&opts, 0, sizeof(opts));
	if (fuse_opt_parse(args, &opts, fuse_mount_opts, NULL) == -1)
		goto bad;

	mnt_flags = 0;
	if (opts.rdonly)
		mnt_flags |= MNT_RDONLY;
	if (opts.noatime)
		mnt_flags |= MNT_NOATIME;

	if (opts.max_read > FUSEBUFMAXSIZE) {
		fprintf(stderr, "fuse: invalid max_read (%d > %d)\n",
		    opts.max_read, FUSEBUFMAXSIZE);
		goto bad;
	}

	memset(&fargs, 0, sizeof(fargs));
	fargs.fd = fc->fd;
	fargs.max_read = opts.max_read;
	fargs.allow_other = opts.allow_other;

	if (mount(MOUNT_FUSEFS, fc->dir, mnt_flags, &fargs)) {
		switch (errno) {
		case EMFILE:
			errcause = "mount table full";
			break;
		case EOPNOTSUPP:
			errcause = "filesystem not supported by kernel";
			break;
		default:
			errcause = strerror(errno);
			break;
		}
		fprintf(stderr, "%s on %s: %s\n", __func__, dir, errcause);
		goto bad;
	}

	return (fc);
bad:
	if (fc->fd != -1)
		close(fc->fd);
	free(fc->dir);
	free(fc);
	return (NULL);
}
DEF(fuse_mount);

void
fuse_unmount(const char *dir, struct fuse_chan *ch)
{
	if (ch == NULL)
		return;

	/*
	 * Close the device before unmounting to prevent deadlocks with
	 * FBT_DESTROY if fuse_loop() has already terminated.
	 */
	if (close(ch->fd) == -1)
		DPERROR(__func__);

	if (!ch->dead)
		if (unmount(dir, MNT_FORCE) == -1)
			DPERROR(__func__);
}
DEF(fuse_unmount);

int
fuse_is_lib_option(const char *opt)
{
	return (fuse_opt_match(fuse_lib_opts, opt));
}

int
fuse_chan_fd(struct fuse_chan *ch)
{
	if (ch == NULL)
		return (-1);

	return (ch->fd);
}

struct fuse_session *
fuse_get_session(struct fuse *f)
{
	return (&f->se);
}
DEF(fuse_get_session);

int
fuse_loop_mt(unused struct fuse *fuse)
{
	return (-1);
}

static int
ifuse_lib_opt_proc(void *data, const char *arg, int key,
    unused struct fuse_args *args)
{
	switch (key) {
	case KEY_STUB:
		return (0);
	case KEY_DEBUG:
		ifuse_debug_init();
		break;
	default:
		fprintf(stderr, "fuse: unrecognised option %s\n", arg);
		return (-1);
	}

	/* Keep unknown options. */
	return (1);
}

struct fuse *
fuse_new(struct fuse_chan *fc, struct fuse_args *args,
    const struct fuse_operations *ops, unused size_t size,
    void *userdata)
{
	struct fuse *fuse;
	struct fuse_vnode *root;

	if (fc == NULL || ops == NULL)
		return (NULL);

	if ((fuse = calloc(1, sizeof(*fuse))) == NULL)
		return (NULL);

	/* copy fuse ops to their own structure */
	memcpy(&fuse->op, ops, sizeof(fuse->op));

	if (fuse_opt_parse(args, &fuse->conf, fuse_lib_opts,
	    ifuse_lib_opt_proc) == -1) {
		free(fuse);
		return (NULL);
	}

	fuse->fc = fc;
	fuse->max_ino = FUSE_ROOT_INO;
	fuse->se.args = fuse;
	fuse->private_data = userdata;

	if ((root = alloc_vn(fuse, "/", FUSE_ROOT_INO, 0)) == NULL) {
		free(fuse);
		return (NULL);
	}

	tree_init(&fuse->vnode_tree);
	tree_init(&fuse->name_tree);
	if (!set_vn(fuse, root)) {
		free(fuse);
		return (NULL);
	}

	return (fuse);
}
DEF(fuse_new);

int
fuse_daemonize(int foreground)
{
	if (foreground)
		return (0);

	return (daemon(0, 0));
}
DEF(fuse_daemonize);

void
fuse_destroy(struct fuse *fuse)
{
        struct fuse_context ctx;

	if (fuse == NULL)
		return;

	if (fuse->fc->init && fuse->op.destroy) {
		/* setup a basic fuse context for the callback */
		memset(&ctx, 0, sizeof(ctx));
		ctx.fuse = fuse;
		ctx.private_data = fuse->private_data;
		ictx = &ctx;

		fuse->op.destroy(fuse->private_data);

		ictx = NULL;
	}

	/*
  	 * Even though these were allocated in fuse_mount(), we can't free them
 	 * in fuse_unmount() since they are still needed, so we free them here.
 	 */
	free(fuse->fc->dir);
	free(fuse->fc);
	free(fuse);
}
DEF(fuse_destroy);

void
fuse_remove_signal_handlers(unused struct fuse_session *se)
{
	struct sigaction old_sa;

	if (sigaction(SIGHUP, NULL, &old_sa) == 0)
		if (old_sa.sa_handler == SIG_IGN)
			signal(SIGHUP, SIG_DFL);

	if (sigaction(SIGINT, NULL, &old_sa) == 0)
		if (old_sa.sa_handler == SIG_IGN)
			signal(SIGINT, SIG_DFL);

	if (sigaction(SIGTERM, NULL, &old_sa) == 0)
		if (old_sa.sa_handler == SIG_IGN)
			signal(SIGTERM, SIG_DFL);

	if (sigaction(SIGPIPE, NULL, &old_sa) == 0)
		if (old_sa.sa_handler == SIG_IGN)
			signal(SIGPIPE, SIG_DFL);

	if (sigaction(SIGCHLD, NULL, &old_sa) == 0)
		if (old_sa.sa_handler == SIG_IGN)
			signal(SIGCHLD, SIG_DFL);
}
DEF(fuse_remove_signal_handlers);

int
fuse_set_signal_handlers(unused struct fuse_session *se)
{
	struct sigaction old_sa;

	if (sigaction(SIGHUP, NULL, &old_sa) == -1)
		return (-1);
	if (old_sa.sa_handler == SIG_DFL)
		signal(SIGHUP, SIG_IGN);

	if (sigaction(SIGINT, NULL, &old_sa) == -1)
		return (-1);
	if (old_sa.sa_handler == SIG_DFL)
		signal(SIGINT, SIG_IGN);

	if (sigaction(SIGTERM, NULL, &old_sa) == -1)
		return (-1);
	if (old_sa.sa_handler == SIG_DFL)
		signal(SIGTERM, SIG_IGN);

	if (sigaction(SIGPIPE, NULL, &old_sa) == -1)
		return (-1);
	if (old_sa.sa_handler == SIG_DFL)
		signal(SIGPIPE, SIG_IGN);

	return (0);
}

static void
dump_help(void)
{
	fprintf(stderr, "FUSE options:\n"
	    "    -d   -o debug          enable debug output (implies -f)\n"
	    "    -f                     run in foreground\n"
	    "    -V   --version         print fuse version\n"
	    "\n");
}

static void
dump_version(void)
{
	fprintf(stderr, "FUSE library version: %d.%d\n", FUSE_MAJOR_VERSION,
	    FUSE_MINOR_VERSION);
}

static int
ifuse_process_opt(void *data, const char *arg, int key,
    unused struct fuse_args *args)
{
	struct fuse_core_opts *opt = data;
	struct stat st;
	int res;

	switch (key) {
	case KEY_STUB:
		return (0);
	case KEY_DEBUG:
		ifuse_debug_init();
		/* falls through */
	case KEY_FOREGROUND:
		opt->foreground = 1;
		return (0);
	case KEY_HELP:
	case KEY_HELP_WITHOUT_HEADER:
		dump_help();
		return (-1);
	case KEY_VERSION:
		dump_version();
		return (-1);
	case FUSE_OPT_KEY_NONOPT:
		if (opt->mp == NULL) {
			opt->mp = realpath(arg, opt->mp);
			if (opt->mp == NULL) {
				fprintf(stderr, "fuse: realpath: "
				    "%s : %s\n", arg, strerror(errno));
				return (-1);
			}

			res = stat(opt->mp, &st);
			if (res == -1) {
				fprintf(stderr, "fuse: bad mount point "
				    "%s : %s\n", arg, strerror(errno));
				return (-1);
			}

			if (!S_ISDIR(st.st_mode)) {
				fprintf(stderr, "fuse: bad mount point "
				    "%s : %s\n", arg, strerror(ENOTDIR));
				return (-1);
			}
		}
		return (0);
	}

	/* Pass through unknown options. */
	return (1);
}

int
fuse_parse_cmdline(struct fuse_args *args, char **mp, int *mt, int *fg)
{
	struct fuse_core_opts opt;

	memset(&opt, 0, sizeof(opt));
	if (fuse_opt_parse(args, &opt, fuse_core_opts, ifuse_process_opt) == -1)
		return (-1);

	if (opt.mp == NULL) {
		fprintf(stderr, "fuse: missing mountpoint parameter\n");
		return (-1);
	}

	if (mp != NULL) {
		*mp = strdup(opt.mp);
		if (*mp == NULL)
			return (-1);
	}

	if (mt != NULL)
		*mt = 0;

	if (fg != NULL)
		*fg = opt.foreground;

	return (0);
}
DEF(fuse_parse_cmdline);

struct fuse_context *
fuse_get_context(void)
{
	return (ictx);
}
DEF(fuse_get_context);

int
fuse_version(void)
{
	return (FUSE_VERSION);
}

void
fuse_teardown(struct fuse *fuse, char *mp)
{
	if (fuse == NULL || mp == NULL)
		return;

	fuse_remove_signal_handlers(fuse_get_session(fuse));
	fuse_unmount(mp, fuse->fc);
	fuse_destroy(fuse);
}

int
fuse_invalidate(unused struct fuse *f, unused const char *path)
{
	return (EINVAL);
}

struct fuse *
fuse_setup(int argc, char **argv, const struct fuse_operations *ops,
    size_t size, char **mp, int *mt, void *data)
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_chan *fc;
	struct fuse *fuse;
	char *dir;
	int fg;

	dir = NULL;
	if (fuse_parse_cmdline(&args, &dir, mt, &fg))
		goto err;

	fuse_daemonize(fg);

	if ((fc = fuse_mount(dir, &args)) == NULL)
		goto err;

	if ((fuse = fuse_new(fc, &args, ops, size, data)) == NULL) {
		fuse_unmount(dir, fc);
		close(fc->fd);
		free(fc->dir);
		free(fc);
		goto err;
	}

	/* args are no longer needed */
	fuse_opt_free_args(&args);

	if (fuse_set_signal_handlers(fuse_get_session(fuse)) == -1) {
		fuse_unmount(dir, fc);
		fuse_destroy(fuse);
		goto err;
	}

	/* the caller frees dir, but we do it if the caller doesn't want it */
	if (mp == NULL)
		free(dir);
	else
		*mp = dir;

	return (fuse);
err:
	free(dir);
	return (NULL);
}
DEF(fuse_setup);

int
fuse_main(int argc, char **argv, const struct fuse_operations *ops, void *data)
{
	struct fuse *fuse;
	char *mp;
	int ret;

	fuse = fuse_setup(argc, argv, ops, sizeof(*ops), &mp, NULL, data);
	if (fuse == NULL)
		return (-1);

	ret = fuse_loop(fuse);
	fuse_teardown(fuse, mp);

	return (ret == -1 ? 1 : 0);
}
