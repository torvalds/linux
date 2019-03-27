/*-
 * Copyright (c) 2014 Jakub Wojciech Klama <jceel@FreeBSD.org>
 * Copyright (c) 2015-2016 Vladimir Kondratyev <wulf@FreeBSD.org>
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
 * $FreeBSD$
 */

#include "opt_evdev.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/selinfo.h>
#include <sys/systm.h>
#include <sys/sx.h>
#include <sys/uio.h>

#include <dev/evdev/evdev.h>
#include <dev/evdev/evdev_private.h>
#include <dev/evdev/input.h>
#include <dev/evdev/uinput.h>

#ifdef UINPUT_DEBUG
#define	debugf(state, fmt, args...)	printf("uinput: " fmt "\n", ##args)
#else
#define	debugf(state, fmt, args...)
#endif

#define	UINPUT_BUFFER_SIZE	16

#define	UINPUT_LOCK(state)		sx_xlock(&(state)->ucs_lock)
#define	UINPUT_UNLOCK(state)		sx_unlock(&(state)->ucs_lock)
#define	UINPUT_LOCK_ASSERT(state)	sx_assert(&(state)->ucs_lock, SA_LOCKED)
#define UINPUT_EMPTYQ(state) \
    ((state)->ucs_buffer_head == (state)->ucs_buffer_tail)

enum uinput_state
{
	UINPUT_NEW = 0,
	UINPUT_CONFIGURED,
	UINPUT_RUNNING
};

static evdev_event_t	uinput_ev_event;

static d_open_t		uinput_open;
static d_read_t		uinput_read;
static d_write_t	uinput_write;
static d_ioctl_t	uinput_ioctl;
static d_poll_t		uinput_poll;
static d_kqfilter_t	uinput_kqfilter;
static void uinput_dtor(void *);

static int uinput_kqread(struct knote *kn, long hint);
static void uinput_kqdetach(struct knote *kn);

static struct cdevsw uinput_cdevsw = {
	.d_version = D_VERSION,
	.d_open = uinput_open,
	.d_read = uinput_read,
	.d_write = uinput_write,
	.d_ioctl = uinput_ioctl,
	.d_poll = uinput_poll,
	.d_kqfilter = uinput_kqfilter,
	.d_name = "uinput",
};

static struct cdev *uinput_cdev;

static struct evdev_methods uinput_ev_methods = {
	.ev_open = NULL,
	.ev_close = NULL,
	.ev_event = uinput_ev_event,
};

static struct filterops uinput_filterops = {
	.f_isfd = 1,
	.f_attach = NULL,
	.f_detach = uinput_kqdetach,
	.f_event = uinput_kqread,
};

struct uinput_cdev_state
{
	enum uinput_state	ucs_state;
	struct evdev_dev *	ucs_evdev;
	struct sx		ucs_lock;
	size_t			ucs_buffer_head;
	size_t			ucs_buffer_tail;
	struct selinfo		ucs_selp;
	bool			ucs_blocked;
	bool			ucs_selected;
	struct input_event      ucs_buffer[UINPUT_BUFFER_SIZE];
};

static void uinput_enqueue_event(struct uinput_cdev_state *, uint16_t,
    uint16_t, int32_t);
static int uinput_setup_provider(struct uinput_cdev_state *,
    struct uinput_user_dev *);
static int uinput_cdev_create(void);
static void uinput_notify(struct uinput_cdev_state *);

static void
uinput_knllock(void *arg)
{
	struct sx *sx = arg;

	sx_xlock(sx);
}

static void
uinput_knlunlock(void *arg)
{
	struct sx *sx = arg;

	sx_unlock(sx);
}

static void
uinput_knl_assert_locked(void *arg)
{

	sx_assert((struct sx*)arg, SA_XLOCKED);
}

static void
uinput_knl_assert_unlocked(void *arg)
{

	sx_assert((struct sx*)arg, SA_UNLOCKED);
}

static void
uinput_ev_event(struct evdev_dev *evdev, uint16_t type, uint16_t code,
    int32_t value)
{
	struct uinput_cdev_state *state = evdev_get_softc(evdev);

	if (type == EV_LED)
		evdev_push_event(evdev, type, code, value);

	UINPUT_LOCK(state);
	if (state->ucs_state == UINPUT_RUNNING) {
		uinput_enqueue_event(state, type, code, value);
		uinput_notify(state);
	}
	UINPUT_UNLOCK(state);
}

static void
uinput_enqueue_event(struct uinput_cdev_state *state, uint16_t type,
    uint16_t code, int32_t value)
{
	size_t head, tail;

	UINPUT_LOCK_ASSERT(state);

	head = state->ucs_buffer_head;
	tail = (state->ucs_buffer_tail + 1) % UINPUT_BUFFER_SIZE;

	microtime(&state->ucs_buffer[tail].time);
	state->ucs_buffer[tail].type = type;
	state->ucs_buffer[tail].code = code;
	state->ucs_buffer[tail].value = value;
	state->ucs_buffer_tail = tail;

	/* If queue is full remove oldest event */
	if (tail == head) {
		debugf(state, "state %p: buffer overflow", state);

		head = (head + 1) % UINPUT_BUFFER_SIZE;
		state->ucs_buffer_head = head;
	}
}

static int
uinput_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct uinput_cdev_state *state;

	state = malloc(sizeof(struct uinput_cdev_state), M_EVDEV,
	    M_WAITOK | M_ZERO);
	state->ucs_evdev = evdev_alloc();

	sx_init(&state->ucs_lock, "uinput");
	knlist_init(&state->ucs_selp.si_note, &state->ucs_lock, uinput_knllock,
	    uinput_knlunlock, uinput_knl_assert_locked,
	    uinput_knl_assert_unlocked);

	devfs_set_cdevpriv(state, uinput_dtor);
	return (0);
}

static void
uinput_dtor(void *data)
{
	struct uinput_cdev_state *state = (struct uinput_cdev_state *)data;

	evdev_free(state->ucs_evdev);

	knlist_clear(&state->ucs_selp.si_note, 0);
	seldrain(&state->ucs_selp);
	knlist_destroy(&state->ucs_selp.si_note);
	sx_destroy(&state->ucs_lock);
	free(data, M_EVDEV);
}

static int
uinput_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct uinput_cdev_state *state;
	struct input_event *event;
	int remaining, ret;

	ret = devfs_get_cdevpriv((void **)&state);
	if (ret != 0)
		return (ret);

	debugf(state, "read %zd bytes by thread %d", uio->uio_resid,
	    uio->uio_td->td_tid);

	/* Zero-sized reads are allowed for error checking */
	if (uio->uio_resid != 0 && uio->uio_resid < sizeof(struct input_event))
		return (EINVAL);

	remaining = uio->uio_resid / sizeof(struct input_event);

	UINPUT_LOCK(state);

	if (state->ucs_state != UINPUT_RUNNING)
		ret = EINVAL;

	if (ret == 0 && UINPUT_EMPTYQ(state)) {
		if (ioflag & O_NONBLOCK)
			ret = EWOULDBLOCK;
		else {
			if (remaining != 0) {
				state->ucs_blocked = true;
				ret = sx_sleep(state, &state->ucs_lock,
				    PCATCH, "uiread", 0);
			}
		}
	}

	while (ret == 0 && !UINPUT_EMPTYQ(state) && remaining > 0) {
		event = &state->ucs_buffer[state->ucs_buffer_head];
		state->ucs_buffer_head = (state->ucs_buffer_head + 1) %
		    UINPUT_BUFFER_SIZE;
		remaining--;
		ret = uiomove(event, sizeof(struct input_event), uio);
	}

	UINPUT_UNLOCK(state);

	return (ret);
}

static int
uinput_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct uinput_cdev_state *state;
	struct uinput_user_dev userdev;
	struct input_event event;
	int ret = 0;

	ret = devfs_get_cdevpriv((void **)&state);
	if (ret != 0)
		return (ret);

	debugf(state, "write %zd bytes by thread %d", uio->uio_resid,
	    uio->uio_td->td_tid);

	UINPUT_LOCK(state);

	if (state->ucs_state != UINPUT_RUNNING) {
		/* Process written struct uinput_user_dev */
		if (uio->uio_resid != sizeof(struct uinput_user_dev)) {
			debugf(state, "write size not multiple of "
			    "struct uinput_user_dev size");
			ret = EINVAL;
		} else {
			ret = uiomove(&userdev, sizeof(struct uinput_user_dev),
			    uio);
			if (ret == 0)
				uinput_setup_provider(state, &userdev);
		}
	} else {
		/* Process written event */
		if (uio->uio_resid % sizeof(struct input_event) != 0) {
			debugf(state, "write size not multiple of "
			    "struct input_event size");
			ret = EINVAL;
		}

		while (ret == 0 && uio->uio_resid > 0) {
			uiomove(&event, sizeof(struct input_event), uio);
			ret = evdev_push_event(state->ucs_evdev, event.type,
			    event.code, event.value);
		}
	}

	UINPUT_UNLOCK(state);

	return (ret);
}

static int
uinput_setup_dev(struct uinput_cdev_state *state, struct input_id *id,
    char *name, uint32_t ff_effects_max)
{

	if (name[0] == 0)
		return (EINVAL);

	evdev_set_name(state->ucs_evdev, name);
	evdev_set_id(state->ucs_evdev, id->bustype, id->vendor, id->product,
	    id->version);
	state->ucs_state = UINPUT_CONFIGURED;

	return (0);
}

static int
uinput_setup_provider(struct uinput_cdev_state *state,
    struct uinput_user_dev *udev)
{
	struct input_absinfo absinfo;
	int i, ret;

	debugf(state, "setup_provider called, udev=%p", udev);

	ret = uinput_setup_dev(state, &udev->id, udev->name,
	    udev->ff_effects_max);
	if (ret)
		return (ret);

	bzero(&absinfo, sizeof(struct input_absinfo));
	for (i = 0; i < ABS_CNT; i++) {
		if (!bit_test(state->ucs_evdev->ev_abs_flags, i))
			continue;

		absinfo.minimum = udev->absmin[i];
		absinfo.maximum = udev->absmax[i];
		absinfo.fuzz = udev->absfuzz[i];
		absinfo.flat = udev->absflat[i];
		evdev_set_absinfo(state->ucs_evdev, i, &absinfo);
	}

	return (0);
}

static int
uinput_poll(struct cdev *dev, int events, struct thread *td)
{
	struct uinput_cdev_state *state;
	int revents = 0;

	if (devfs_get_cdevpriv((void **)&state) != 0)
		return (POLLNVAL);

	debugf(state, "poll by thread %d", td->td_tid);

	/* Always allow write */
	if (events & (POLLOUT | POLLWRNORM))
		revents |= (events & (POLLOUT | POLLWRNORM));

	if (events & (POLLIN | POLLRDNORM)) {
		UINPUT_LOCK(state);
		if (!UINPUT_EMPTYQ(state))
			revents = events & (POLLIN | POLLRDNORM);
		else {
			state->ucs_selected = true;
			selrecord(td, &state->ucs_selp);
		}
		UINPUT_UNLOCK(state);
	}

	return (revents);
}

static int
uinput_kqfilter(struct cdev *dev, struct knote *kn)
{
	struct uinput_cdev_state *state;
	int ret;

	ret = devfs_get_cdevpriv((void **)&state);
	if (ret != 0)
		return (ret);

	switch(kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &uinput_filterops;
		break;
	default:
		return(EINVAL);
	}
	kn->kn_hook = (caddr_t)state;

	knlist_add(&state->ucs_selp.si_note, kn, 0);
	return (0);
}

static int
uinput_kqread(struct knote *kn, long hint)
{
	struct uinput_cdev_state *state;
	int ret;

	state = (struct uinput_cdev_state *)kn->kn_hook;

	UINPUT_LOCK_ASSERT(state);

	ret = !UINPUT_EMPTYQ(state);
	return (ret);
}

static void
uinput_kqdetach(struct knote *kn)
{
	struct uinput_cdev_state *state;

	state = (struct uinput_cdev_state *)kn->kn_hook;
	knlist_remove(&state->ucs_selp.si_note, kn, 0);
}

static void
uinput_notify(struct uinput_cdev_state *state)
{

	UINPUT_LOCK_ASSERT(state);

	if (state->ucs_blocked) {
		state->ucs_blocked = false;
		wakeup(state);
	}
	if (state->ucs_selected) {
		state->ucs_selected = false;
		selwakeup(&state->ucs_selp);
	}
	KNOTE_LOCKED(&state->ucs_selp.si_note, 0);
}

static int
uinput_ioctl_sub(struct uinput_cdev_state *state, u_long cmd, caddr_t data)
{
	struct uinput_setup *us;
	struct uinput_abs_setup *uabs;
	int ret, len, intdata;
	char buf[NAMELEN];

	UINPUT_LOCK_ASSERT(state);

	len = IOCPARM_LEN(cmd);
	if ((cmd & IOC_DIRMASK) == IOC_VOID && len == sizeof(int))
		intdata = *(int *)data;

	switch (IOCBASECMD(cmd)) {
	case UI_GET_SYSNAME(0):
		if (state->ucs_state != UINPUT_RUNNING)
			return (ENOENT);
		if (len == 0)
			return (EINVAL);
		snprintf(data, len, "event%d", state->ucs_evdev->ev_unit);
		return (0);
	}

	switch (cmd) {
	case UI_DEV_CREATE:
		if (state->ucs_state != UINPUT_CONFIGURED)
			return (EINVAL);

		evdev_set_methods(state->ucs_evdev, state, &uinput_ev_methods);
		evdev_set_flag(state->ucs_evdev, EVDEV_FLAG_SOFTREPEAT);
		ret = evdev_register(state->ucs_evdev);
		if (ret == 0)
			state->ucs_state = UINPUT_RUNNING;
		return (ret);

	case UI_DEV_DESTROY:
		if (state->ucs_state != UINPUT_RUNNING)
			return (0);

		evdev_unregister(state->ucs_evdev);
		bzero(state->ucs_evdev, sizeof(struct evdev_dev));
		state->ucs_state = UINPUT_NEW;
		return (0);

	case UI_DEV_SETUP:
		if (state->ucs_state == UINPUT_RUNNING)
			return (EINVAL);

		us = (struct uinput_setup *)data;
		return (uinput_setup_dev(state, &us->id, us->name,
		    us->ff_effects_max));

	case UI_ABS_SETUP:
		if (state->ucs_state == UINPUT_RUNNING)
			return (EINVAL);

		uabs = (struct uinput_abs_setup *)data;
		if (uabs->code > ABS_MAX)
			return (EINVAL);

		evdev_support_abs(state->ucs_evdev, uabs->code,
		    uabs->absinfo.value, uabs->absinfo.minimum,
		    uabs->absinfo.maximum, uabs->absinfo.fuzz,
		    uabs->absinfo.flat, uabs->absinfo.resolution);
		return (0);

	case UI_SET_EVBIT:
		if (state->ucs_state == UINPUT_RUNNING ||
		    intdata > EV_MAX || intdata < 0)
			return (EINVAL);
		evdev_support_event(state->ucs_evdev, intdata);
		return (0);

	case UI_SET_KEYBIT:
		if (state->ucs_state == UINPUT_RUNNING ||
		    intdata > KEY_MAX || intdata < 0)
			return (EINVAL);
		evdev_support_key(state->ucs_evdev, intdata);
		return (0);

	case UI_SET_RELBIT:
		if (state->ucs_state == UINPUT_RUNNING ||
		    intdata > REL_MAX || intdata < 0)
			return (EINVAL);
		evdev_support_rel(state->ucs_evdev, intdata);
		return (0);

	case UI_SET_ABSBIT:
		if (state->ucs_state == UINPUT_RUNNING ||
		    intdata > ABS_MAX || intdata < 0)
			return (EINVAL);
		evdev_set_abs_bit(state->ucs_evdev, intdata);
		return (0);

	case UI_SET_MSCBIT:
		if (state->ucs_state == UINPUT_RUNNING ||
		    intdata > MSC_MAX || intdata < 0)
			return (EINVAL);
		evdev_support_msc(state->ucs_evdev, intdata);
		return (0);

	case UI_SET_LEDBIT:
		if (state->ucs_state == UINPUT_RUNNING ||
		    intdata > LED_MAX || intdata < 0)
			return (EINVAL);
		evdev_support_led(state->ucs_evdev, intdata);
		return (0);

	case UI_SET_SNDBIT:
		if (state->ucs_state == UINPUT_RUNNING ||
		    intdata > SND_MAX || intdata < 0)
			return (EINVAL);
		evdev_support_snd(state->ucs_evdev, intdata);
		return (0);

	case UI_SET_FFBIT:
		if (state->ucs_state == UINPUT_RUNNING ||
		    intdata > FF_MAX || intdata < 0)
			return (EINVAL);
		/* Fake unsupported ioctl */
		return (0);

	case UI_SET_PHYS:
		if (state->ucs_state == UINPUT_RUNNING)
			return (EINVAL);
		ret = copyinstr(*(void **)data, buf, sizeof(buf), NULL);
		/* Linux returns EINVAL when string does not fit the buffer */
		if (ret == ENAMETOOLONG)
			ret = EINVAL;
		if (ret != 0)
			return (ret);
		evdev_set_phys(state->ucs_evdev, buf);
		return (0);

	case UI_SET_BSDUNIQ:
		if (state->ucs_state == UINPUT_RUNNING)
			return (EINVAL);
		ret = copyinstr(*(void **)data, buf, sizeof(buf), NULL);
		if (ret != 0)
			return (ret);
		evdev_set_serial(state->ucs_evdev, buf);
		return (0);

	case UI_SET_SWBIT:
		if (state->ucs_state == UINPUT_RUNNING ||
		    intdata > SW_MAX || intdata < 0)
			return (EINVAL);
		evdev_support_sw(state->ucs_evdev, intdata);
		return (0);

	case UI_SET_PROPBIT:
		if (state->ucs_state == UINPUT_RUNNING ||
		    intdata > INPUT_PROP_MAX || intdata < 0)
			return (EINVAL);
		evdev_support_prop(state->ucs_evdev, intdata);
		return (0);

	case UI_BEGIN_FF_UPLOAD:
	case UI_END_FF_UPLOAD:
	case UI_BEGIN_FF_ERASE:
	case UI_END_FF_ERASE:
		if (state->ucs_state == UINPUT_RUNNING)
			return (EINVAL);
		/* Fake unsupported ioctl */
		return (0);

	case UI_GET_VERSION:
		*(unsigned int *)data = UINPUT_VERSION;
		return (0);
	}

	return (EINVAL);
}

static int
uinput_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct uinput_cdev_state *state;
	int ret;

	ret = devfs_get_cdevpriv((void **)&state);
	if (ret != 0)
		return (ret);

	debugf(state, "ioctl called: cmd=0x%08lx, data=%p", cmd, data);

	UINPUT_LOCK(state);
	ret = uinput_ioctl_sub(state, cmd, data);
	UINPUT_UNLOCK(state);

	return (ret);
}

static int
uinput_cdev_create(void)
{
	struct make_dev_args mda;
	int ret;

	make_dev_args_init(&mda);
	mda.mda_flags = MAKEDEV_WAITOK | MAKEDEV_CHECKNAME;
	mda.mda_devsw = &uinput_cdevsw;
	mda.mda_uid = UID_ROOT;
	mda.mda_gid = GID_WHEEL;
	mda.mda_mode = 0600;

	ret = make_dev_s(&mda, &uinput_cdev, "uinput");

	return (ret);
}

static int
uinput_cdev_destroy(void)
{

	destroy_dev(uinput_cdev);

	return (0);
}

static int
uinput_modevent(module_t mod __unused, int cmd, void *data)
{
	int ret = 0;

	switch (cmd) {
	case MOD_LOAD:
		ret = uinput_cdev_create();
		break;

	case MOD_UNLOAD:
		ret = uinput_cdev_destroy();
		break;

	case MOD_SHUTDOWN:
		break;

	default:
		ret = EINVAL;
		break;
	}

	return (ret);
}

DEV_MODULE(uinput, uinput_modevent, NULL);
MODULE_VERSION(uinput, 1);
MODULE_DEPEND(uinput, evdev, 1, 1, 1);
