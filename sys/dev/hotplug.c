/*	$OpenBSD: hotplug.c,v 1.25 2024/12/30 02:46:00 guenther Exp $	*/
/*
 * Copyright (c) 2004 Alexander Yurchenko <grange@openbsd.org>
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

/*
 * Device attachment and detachment notifications.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/hotplug.h>
#include <sys/ioctl.h>
#include <sys/mutex.h>
#include <sys/vnode.h>

#define HOTPLUG_MAXEVENTS	64

/*
 * Locks used to protect struct members and global data
 *	 M	hotplug_mtx
 */

static struct mutex hotplug_mtx = MUTEX_INITIALIZER(IPL_MPFLOOR);

static int opened;
static struct hotplug_event evqueue[HOTPLUG_MAXEVENTS];
static int evqueue_head, evqueue_tail, evqueue_count;	/* [M] */
static struct klist hotplug_klist;			/* [M] */

void filt_hotplugrdetach(struct knote *);
int  filt_hotplugread(struct knote *, long);
int  filt_hotplugmodify(struct kevent *, struct knote *);
int  filt_hotplugprocess(struct knote *, struct kevent *);

const struct filterops hotplugread_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_hotplugrdetach,
	.f_event	= filt_hotplugread,
	.f_modify	= filt_hotplugmodify,
	.f_process	= filt_hotplugprocess,
};

#define EVQUEUE_NEXT(p) (p == HOTPLUG_MAXEVENTS - 1 ? 0 : p + 1)


int hotplug_put_event(struct hotplug_event *);
int hotplug_get_event(struct hotplug_event *);

void hotplugattach(int);

void
hotplugattach(int count)
{
	opened = 0;
	evqueue_head = 0;
	evqueue_tail = 0;
	evqueue_count = 0;

	klist_init_mutex(&hotplug_klist, &hotplug_mtx);
}

void
hotplug_device_attach(enum devclass class, char *name)
{
	struct hotplug_event he;

	he.he_type = HOTPLUG_DEVAT;
	he.he_devclass = class;
	strlcpy(he.he_devname, name, sizeof(he.he_devname));
	hotplug_put_event(&he);
}

void
hotplug_device_detach(enum devclass class, char *name)
{
	struct hotplug_event he;

	he.he_type = HOTPLUG_DEVDT;
	he.he_devclass = class;
	strlcpy(he.he_devname, name, sizeof(he.he_devname));
	hotplug_put_event(&he);
}

int
hotplug_put_event(struct hotplug_event *he)
{
	mtx_enter(&hotplug_mtx);
	if (evqueue_count == HOTPLUG_MAXEVENTS && opened) {
		mtx_leave(&hotplug_mtx);
		printf("hotplug: event lost, queue full\n");
		return (1);
	}

	evqueue[evqueue_head] = *he;
	evqueue_head = EVQUEUE_NEXT(evqueue_head);
	if (evqueue_count == HOTPLUG_MAXEVENTS)
		evqueue_tail = EVQUEUE_NEXT(evqueue_tail);
	else 
		evqueue_count++;
	knote_locked(&hotplug_klist, 0);
	wakeup(&evqueue);
	mtx_leave(&hotplug_mtx);
	return (0);
}

int
hotplug_get_event(struct hotplug_event *he)
{
	if (evqueue_count == 0)
		return (1);

	*he = evqueue[evqueue_tail];
	evqueue_tail = EVQUEUE_NEXT(evqueue_tail);
	evqueue_count--;
	return (0);
}

int
hotplugopen(dev_t dev, int flag, int mode, struct proc *p)
{
	if (minor(dev) != 0)
		return (ENXIO);
	if ((flag & FWRITE))
		return (EPERM);
	if (opened)
		return (EBUSY);
	opened = 1;
	return (0);
}

int
hotplugclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct hotplug_event he;

	mtx_enter(&hotplug_mtx);
	while (hotplug_get_event(&he) == 0)
		continue;
	mtx_leave(&hotplug_mtx);
	klist_invalidate(&hotplug_klist);
	opened = 0;
	return (0);
}

int
hotplugread(dev_t dev, struct uio *uio, int flags)
{
	struct hotplug_event he;
	int error;

	if (uio->uio_resid != sizeof(he))
		return (EINVAL);

	mtx_enter(&hotplug_mtx);
	while (hotplug_get_event(&he)) {
		if (flags & IO_NDELAY) {
			mtx_leave(&hotplug_mtx);
			return (EAGAIN);
		}
	
		error = msleep_nsec(&evqueue, &hotplug_mtx, PRIBIO | PCATCH,
		    "htplev", INFSLP);
		if (error) {
			mtx_leave(&hotplug_mtx);
			return (error);
		}
	}
	mtx_leave(&hotplug_mtx);

	return (uiomove(&he, sizeof(he), uio));
}

int
hotplugioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	switch (cmd) {
	case FIOASYNC:
		/* ignore */
	default:
		return (ENOTTY);
	}

	return (0);
}

int
hotplugkqfilter(dev_t dev, struct knote *kn)
{
	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &hotplugread_filtops;
		break;
	default:
		return (EINVAL);
	}

	klist_insert(&hotplug_klist, kn);
	return (0);
}

void
filt_hotplugrdetach(struct knote *kn)
{
	klist_remove(&hotplug_klist, kn);
}

int
filt_hotplugread(struct knote *kn, long hint)
{
	kn->kn_data = evqueue_count;

	return (evqueue_count > 0);
}

int
filt_hotplugmodify(struct kevent *kev, struct knote *kn)
{
	int active;

	mtx_enter(&hotplug_mtx);
	active = knote_modify(kev, kn);
	mtx_leave(&hotplug_mtx);

	return (active);
}

int
filt_hotplugprocess(struct knote *kn, struct kevent *kev)
{
	int active;

	mtx_enter(&hotplug_mtx);
	active = knote_process(kn, kev);
	mtx_leave(&hotplug_mtx);

	return (active);
}
