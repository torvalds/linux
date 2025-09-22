/* $OpenBSD: fuse_device.c,v 1.46 2025/09/20 15:01:23 helg Exp $ */
/*
 * Copyright (c) 2012-2013 Sylvestre Gallon <ccna.syl@gmail.com>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/event.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/rwlock.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vnode.h>
#include <sys/fusebuf.h>

#include "fusefs_node.h"
#include "fusefs.h"

#ifdef	FUSE_DEBUG
#define	DPRINTF(fmt, arg...)	printf("%s: " fmt, "fuse", ##arg)
#else
#define	DPRINTF(fmt, arg...)
#endif

/*
 * Locks used to protect struct members and global data
 *	l	fd_lock
 */

SIMPLEQ_HEAD(fusebuf_head, fusebuf);

struct fuse_d {
	struct rwlock fd_lock;
	struct fusefs_mnt *fd_fmp;
	int fd_unit;

	/*fusebufs queues*/
	struct fusebuf_head fd_fbufs_in;	/* [l] */
	struct fusebuf_head fd_fbufs_wait;

	/* kq fields */
	struct klist fd_rklist;			/* [l] */
	LIST_ENTRY(fuse_d) fd_list;
};

int stat_fbufs_in = 0;
int stat_fbufs_wait = 0;
int stat_opened_fusedev = 0;

LIST_HEAD(, fuse_d) fuse_d_list;
struct fuse_d *fuse_lookup(int);

void	fuseattach(int);
int	fuseopen(dev_t, int, int, struct proc *);
int	fuseclose(dev_t, int, int, struct proc *);
int	fuseread(dev_t, struct uio *, int);
int	fusewrite(dev_t, struct uio *, int);
int	fusekqfilter(dev_t dev, struct knote *kn);
int	filt_fuse_read(struct knote *, long);
void	filt_fuse_rdetach(struct knote *);
int	filt_fuse_modify(struct kevent *, struct knote *);
int	filt_fuse_process(struct knote *, struct kevent *);

static const struct filterops fuse_rd_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_fuse_rdetach,
	.f_event	= filt_fuse_read,
	.f_modify	= filt_fuse_modify,
	.f_process	= filt_fuse_process,
};

#ifdef FUSE_DEBUG
static void
fuse_dump_buff(char *buff, int len)
{
	char text[17];
	int i;

	if (len < 0) {
		printf("invalid len: %d", len);
		return;
	}
	if (buff == NULL) {
		printf("invalid buff");
		return;
	}

	memset(text, 0, 17);
	for (i = 0; i < len; i++) {
		if (i != 0 && (i % 16) == 0) {
			printf(": %s\n", text);
			memset(text, 0, 17);
		}

		printf("%.2x ", buff[i] & 0xff);

		if (buff[i] > ' ' && buff[i] < '~')
			text[i%16] = buff[i] & 0xff;
		else
			text[i%16] = '.';
	}

	if ((i % 16) != 0)
		while ((i % 16) != 0) {
			printf("   ");
			i++;
		}

	printf(": %s\n", text);
}
#endif

struct fuse_d *
fuse_lookup(int unit)
{
	struct fuse_d *fd;

	LIST_FOREACH(fd, &fuse_d_list, fd_list)
		if (fd->fd_unit == unit)
			return (fd);
	return (NULL);
}

/*
 * Cleanup all msgs from sc_fbufs_in and sc_fbufs_wait.
 */
void
fuse_device_cleanup(dev_t dev)
{
	struct fuse_d *fd;
	struct fusebuf *f, *ftmp, *lprev;

	fd = fuse_lookup(minor(dev));
	if (fd == NULL)
		return;

	/* clear FIFO IN */
	lprev = NULL;
	rw_enter_write(&fd->fd_lock);
	SIMPLEQ_FOREACH_SAFE(f, &fd->fd_fbufs_in, fb_next, ftmp) {
		DPRINTF("cleanup unprocessed msg in sc_fbufs_in\n");
		if (lprev == NULL)
			SIMPLEQ_REMOVE_HEAD(&fd->fd_fbufs_in, fb_next);
		else
			SIMPLEQ_REMOVE_AFTER(&fd->fd_fbufs_in, lprev,
			    fb_next);

		stat_fbufs_in--;
		f->fb_err = ENXIO;
		wakeup(f);
		lprev = f;
	}
	knote_locked(&fd->fd_rklist, 0);
	rw_exit_write(&fd->fd_lock);

	/* clear FIFO WAIT*/
	lprev = NULL;
	SIMPLEQ_FOREACH_SAFE(f, &fd->fd_fbufs_wait, fb_next, ftmp) {
		DPRINTF("umount unprocessed msg in sc_fbufs_wait\n");
		if (lprev == NULL)
			SIMPLEQ_REMOVE_HEAD(&fd->fd_fbufs_wait, fb_next);
		else
			SIMPLEQ_REMOVE_AFTER(&fd->fd_fbufs_wait, lprev,
			    fb_next);

		stat_fbufs_wait--;
		f->fb_err = ENXIO;
		wakeup(f);
		lprev = f;
	}
}

void
fuse_device_queue_fbuf(dev_t dev, struct fusebuf *fbuf)
{
	struct fuse_d *fd;

	fd = fuse_lookup(minor(dev));
	if (fd == NULL)
		return;

	rw_enter_write(&fd->fd_lock);
	SIMPLEQ_INSERT_TAIL(&fd->fd_fbufs_in, fbuf, fb_next);
	knote_locked(&fd->fd_rklist, 0);
	rw_exit_write(&fd->fd_lock);
	stat_fbufs_in++;
}

void
fuse_device_set_fmp(struct fusefs_mnt *fmp, int set)
{
	struct fuse_d *fd;

	fd = fuse_lookup(minor(fmp->dev));
	if (fd == NULL)
		return;

	fd->fd_fmp = set ? fmp : NULL;
}

void
fuseattach(int num)
{
	LIST_INIT(&fuse_d_list);
}

int
fuseopen(dev_t dev, int flags, int fmt, struct proc * p)
{
	struct fuse_d *fd;
	int unit = minor(dev);

	if (flags & O_EXCL)
		return (EBUSY); /* No exclusive opens */

	if ((fd = fuse_lookup(unit)) != NULL)
		return (EBUSY);

	fd = malloc(sizeof(*fd), M_DEVBUF, M_WAITOK | M_ZERO);
	fd->fd_unit = unit;
	SIMPLEQ_INIT(&fd->fd_fbufs_in);
	SIMPLEQ_INIT(&fd->fd_fbufs_wait);
	rw_init(&fd->fd_lock, "fusedlk");
	klist_init_rwlock(&fd->fd_rklist, &fd->fd_lock);

	LIST_INSERT_HEAD(&fuse_d_list, fd, fd_list);

	stat_opened_fusedev++;
	return (0);
}

int
fuseclose(dev_t dev, int flags, int fmt, struct proc *p)
{
	struct fuse_d *fd;

	fd = fuse_lookup(minor(dev));
	if (fd == NULL)
		return (EINVAL);

	fuse_device_cleanup(dev);

	/*
	 * Let fusefs_unmount know the device is closed so it doesn't try and
	 * send FBT_DESTROY to a dead file system daemon.
	 */
	if (fd->fd_fmp)
		fd->fd_fmp->sess_init = 0;

	LIST_REMOVE(fd, fd_list);

	free(fd, M_DEVBUF, sizeof(*fd));
	stat_opened_fusedev--;
	return (0);
}

int
fuseread(dev_t dev, struct uio *uio, int ioflag)
{
	struct fuse_d *fd;
	struct fusebuf *fbuf;
	struct fb_hdr hdr;
	int error = 0;

	fd = fuse_lookup(minor(dev));
	if (fd == NULL)
		return (ENXIO);

	rw_enter_write(&fd->fd_lock);

	if (SIMPLEQ_EMPTY(&fd->fd_fbufs_in)) {
		if (ioflag & O_NONBLOCK)
			error = EAGAIN;
		goto end;
	}
	fbuf = SIMPLEQ_FIRST(&fd->fd_fbufs_in);

	/* We get the whole fusebuf or nothing */
	if (uio->uio_resid < sizeof(fbuf->fb_hdr) + sizeof(fbuf->FD) +
	    fbuf->fb_len) {
		error = EINVAL;
		goto end;
	}

	/* Do not send kernel pointers */
	memcpy(&hdr.fh_next, &fbuf->fb_next, sizeof(fbuf->fb_next));
	memset(&fbuf->fb_next, 0, sizeof(fbuf->fb_next));

	error = uiomove(&fbuf->fb_hdr, sizeof(fbuf->fb_hdr), uio);
	if (error)
		goto end;
	error = uiomove(&fbuf->FD, sizeof(fbuf->FD), uio);
	if (error)
		goto end;
	if (fbuf->fb_len > 0) {
		error = uiomove(fbuf->fb_dat, fbuf->fb_len, uio);
		if (error)
			goto end;
	}

#ifdef FUSE_DEBUG
	fuse_dump_buff((char *)fbuf, sizeof(struct fusebuf));
#endif
	/* Restore kernel pointers */
	memcpy(&fbuf->fb_next, &hdr.fh_next, sizeof(fbuf->fb_next));

	free(fbuf->fb_dat, M_FUSEFS, fbuf->fb_len);
	fbuf->fb_dat = NULL;

	/* Move the fbuf to the wait queue */
	SIMPLEQ_REMOVE_HEAD(&fd->fd_fbufs_in, fb_next);
	stat_fbufs_in--;
	SIMPLEQ_INSERT_TAIL(&fd->fd_fbufs_wait, fbuf, fb_next);
	stat_fbufs_wait++;

end:
	rw_exit_write(&fd->fd_lock);
	return (error);
}

int
fusewrite(dev_t dev, struct uio *uio, int ioflag)
{
	struct fusebuf *lastfbuf;
	struct fuse_d *fd;
	struct fusebuf *fbuf;
	struct fb_hdr hdr;
	int error = 0;

	fd = fuse_lookup(minor(dev));
	if (fd == NULL)
		return (ENXIO);

	/* Check for sanity - must receive more than just the header */
	if (uio->uio_resid <= sizeof(hdr))
		return (EINVAL);

	if ((error = uiomove(&hdr, sizeof(hdr), uio)) != 0)
		return (error);

	/* Check for sanity */
	if (hdr.fh_len > FUSEBUFMAXSIZE)
		return (EINVAL);

	/* We get the whole fusebuf or nothing */
	if (uio->uio_resid != sizeof(fbuf->FD) + hdr.fh_len)
		return (EINVAL);

	/* looking for uuid in fd_fbufs_wait */
	SIMPLEQ_FOREACH(fbuf, &fd->fd_fbufs_wait, fb_next) {
		if (fbuf->fb_uuid == hdr.fh_uuid)
			break;

		lastfbuf = fbuf;
	}
	if (fbuf == NULL)
		return (EINVAL);

	/* Update fb_hdr */
	fbuf->fb_len = hdr.fh_len;
	fbuf->fb_err = hdr.fh_err;
	fbuf->fb_ino = hdr.fh_ino;

	/* Check for corrupted fbufs */
	if ((fbuf->fb_len && fbuf->fb_err) || fbuf->fb_len > fbuf->fb_io_len ||
	    SIMPLEQ_EMPTY(&fd->fd_fbufs_wait)) {
		printf("fuse: dropping corrupted fusebuf\n");
		error = EINVAL;
		goto end;
	}

	/* Get the missing data from the fbuf */
	error = uiomove(&fbuf->FD, sizeof(fbuf->FD), uio);
	if (error)
		return error;

	fbuf->fb_dat = NULL;
	if (fbuf->fb_len > 0) {
		fbuf->fb_dat = malloc(fbuf->fb_len, M_FUSEFS,
		    M_WAITOK | M_ZERO);
		error = uiomove(fbuf->fb_dat, fbuf->fb_len, uio);
		if (error) {
			free(fbuf->fb_dat, M_FUSEFS, fbuf->fb_len);
			fbuf->fb_dat = NULL;
			return (error);
		}
	}

#ifdef FUSE_DEBUG
	fuse_dump_buff((char *)fbuf, sizeof(struct fusebuf));
#endif

	switch (fbuf->fb_type) {
	case FBT_INIT:
		fd->fd_fmp->sess_init = 1;
		break ;
	case FBT_DESTROY:
		fd->fd_fmp = NULL;
		break ;
	}
end:
	/* Remove the fbuf from the wait queue */
	if (fbuf == SIMPLEQ_FIRST(&fd->fd_fbufs_wait))
		SIMPLEQ_REMOVE_HEAD(&fd->fd_fbufs_wait, fb_next);
	else
		SIMPLEQ_REMOVE_AFTER(&fd->fd_fbufs_wait, lastfbuf,
		    fb_next);
	stat_fbufs_wait--;
	if (fbuf->fb_type == FBT_INIT)
		fb_delete(fbuf);
	else
		wakeup(fbuf);

	return (error);
}

int
fusekqfilter(dev_t dev, struct knote *kn)
{
	struct fuse_d *fd;
	struct klist *klist;

	fd = fuse_lookup(minor(dev));
	if (fd == NULL)
		return (EINVAL);

	switch (kn->kn_filter) {
	case EVFILT_READ:
		klist = &fd->fd_rklist;
		kn->kn_fop = &fuse_rd_filtops;
		break;
	case EVFILT_WRITE:
		return (seltrue_kqfilter(dev, kn));
	default:
		return (EINVAL);
	}

	kn->kn_hook = fd;

	klist_insert(klist, kn);

	return (0);
}

void
filt_fuse_rdetach(struct knote *kn)
{
	struct fuse_d *fd = kn->kn_hook;
	struct klist *klist = &fd->fd_rklist;

	klist_remove(klist, kn);
}

int
filt_fuse_read(struct knote *kn, long hint)
{
	struct fuse_d *fd = kn->kn_hook;
	int event = 0;

	rw_assert_wrlock(&fd->fd_lock);

	if (!SIMPLEQ_EMPTY(&fd->fd_fbufs_in))
		event = 1;

	return (event);
}

int
filt_fuse_modify(struct kevent *kev, struct knote *kn)
{
	struct fuse_d *fd = kn->kn_hook;
	int active;

	rw_enter_write(&fd->fd_lock);
	active = knote_modify(kev, kn);
	rw_exit_write(&fd->fd_lock);

	return (active);
}

int
filt_fuse_process(struct knote *kn, struct kevent *kev)
{
	struct fuse_d *fd = kn->kn_hook;
	int active;

	rw_enter_write(&fd->fd_lock);
	active = knote_process(kn, kev); 
	rw_exit_write(&fd->fd_lock);

	return (active);
}
