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
#include <sys/bitstring.h>
#include <sys/conf.h>
#include <sys/filio.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/selinfo.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <dev/evdev/evdev.h>
#include <dev/evdev/evdev_private.h>
#include <dev/evdev/input.h>

#ifdef EVDEV_DEBUG
#define	debugf(client, fmt, args...)	printf("evdev cdev: "fmt"\n", ##args)
#else
#define	debugf(client, fmt, args...)
#endif

#define	DEF_RING_REPORTS	8

static d_open_t		evdev_open;
static d_read_t		evdev_read;
static d_write_t	evdev_write;
static d_ioctl_t	evdev_ioctl;
static d_poll_t		evdev_poll;
static d_kqfilter_t	evdev_kqfilter;

static int evdev_kqread(struct knote *kn, long hint);
static void evdev_kqdetach(struct knote *kn);
static void evdev_dtor(void *);
static int evdev_ioctl_eviocgbit(struct evdev_dev *, int, int, caddr_t);
static void evdev_client_filter_queue(struct evdev_client *, uint16_t);

static struct cdevsw evdev_cdevsw = {
	.d_version = D_VERSION,
	.d_open = evdev_open,
	.d_read = evdev_read,
	.d_write = evdev_write,
	.d_ioctl = evdev_ioctl,
	.d_poll = evdev_poll,
	.d_kqfilter = evdev_kqfilter,
	.d_name = "evdev",
};

static struct filterops evdev_cdev_filterops = {
	.f_isfd = 1,
	.f_attach = NULL,
	.f_detach = evdev_kqdetach,
	.f_event = evdev_kqread,
};

static int
evdev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct evdev_dev *evdev = dev->si_drv1;
	struct evdev_client *client;
	size_t buffer_size;
	int ret;

	if (evdev == NULL)
		return (ENODEV);

	/* Initialize client structure */
	buffer_size = evdev->ev_report_size * DEF_RING_REPORTS;
	client = malloc(offsetof(struct evdev_client, ec_buffer) +
	    sizeof(struct input_event) * buffer_size,
	    M_EVDEV, M_WAITOK | M_ZERO);

	/* Initialize ring buffer */
	client->ec_buffer_size = buffer_size;
	client->ec_buffer_head = 0;
	client->ec_buffer_tail = 0;
	client->ec_buffer_ready = 0;

	client->ec_evdev = evdev;
	mtx_init(&client->ec_buffer_mtx, "evclient", "evdev", MTX_DEF);
	knlist_init_mtx(&client->ec_selp.si_note, &client->ec_buffer_mtx);

	/* Avoid race with evdev_unregister */
	EVDEV_LOCK(evdev);
	if (dev->si_drv1 == NULL)
		ret = ENODEV;
	else
		ret = evdev_register_client(evdev, client);

	if (ret != 0)
		evdev_revoke_client(client);
	/*
	 * Unlock evdev here because non-sleepable lock held 
	 * while calling devfs_set_cdevpriv upsets WITNESS
	 */
	EVDEV_UNLOCK(evdev);

	if (!ret)
		ret = devfs_set_cdevpriv(client, evdev_dtor);

	if (ret != 0) {
		debugf(client, "cannot register evdev client");
		evdev_dtor(client);
	}

	return (ret);
}

static void
evdev_dtor(void *data)
{
	struct evdev_client *client = (struct evdev_client *)data;

	EVDEV_LOCK(client->ec_evdev);
	if (!client->ec_revoked)
		evdev_dispose_client(client->ec_evdev, client);
	EVDEV_UNLOCK(client->ec_evdev);

	knlist_clear(&client->ec_selp.si_note, 0);
	seldrain(&client->ec_selp);
	knlist_destroy(&client->ec_selp.si_note);
	funsetown(&client->ec_sigio);
	mtx_destroy(&client->ec_buffer_mtx);
	free(client, M_EVDEV);
}

static int
evdev_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct evdev_client *client;
	struct input_event event;
	int ret = 0;
	int remaining;

	ret = devfs_get_cdevpriv((void **)&client);
	if (ret != 0)
		return (ret);

	debugf(client, "read %zd bytes by thread %d", uio->uio_resid,
	    uio->uio_td->td_tid);

	if (client->ec_revoked)
		return (ENODEV);

	/* Zero-sized reads are allowed for error checking */
	if (uio->uio_resid != 0 && uio->uio_resid < sizeof(struct input_event))
		return (EINVAL);

	remaining = uio->uio_resid / sizeof(struct input_event);

	EVDEV_CLIENT_LOCKQ(client);

	if (EVDEV_CLIENT_EMPTYQ(client)) {
		if (ioflag & O_NONBLOCK)
			ret = EWOULDBLOCK;
		else {
			if (remaining != 0) {
				client->ec_blocked = true;
				ret = mtx_sleep(client, &client->ec_buffer_mtx,
				    PCATCH, "evread", 0);
			}
		}
	}

	while (ret == 0 && !EVDEV_CLIENT_EMPTYQ(client) && remaining > 0) {
		memcpy(&event, &client->ec_buffer[client->ec_buffer_head],
		    sizeof(struct input_event));
		client->ec_buffer_head =
		    (client->ec_buffer_head + 1) % client->ec_buffer_size;
		remaining--;

		EVDEV_CLIENT_UNLOCKQ(client);
		ret = uiomove(&event, sizeof(struct input_event), uio);
		EVDEV_CLIENT_LOCKQ(client);
	}

	EVDEV_CLIENT_UNLOCKQ(client);

	return (ret);
}

static int
evdev_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct evdev_dev *evdev = dev->si_drv1;
	struct evdev_client *client;
	struct input_event event;
	int ret = 0;

	ret = devfs_get_cdevpriv((void **)&client);
	if (ret != 0)
		return (ret);

	debugf(client, "write %zd bytes by thread %d", uio->uio_resid,
	    uio->uio_td->td_tid);

	if (client->ec_revoked || evdev == NULL)
		return (ENODEV);

	if (uio->uio_resid % sizeof(struct input_event) != 0) {
		debugf(client, "write size not multiple of input_event size");
		return (EINVAL);
	}

	while (uio->uio_resid > 0 && ret == 0) {
		ret = uiomove(&event, sizeof(struct input_event), uio);
		if (ret == 0)
			ret = evdev_inject_event(evdev, event.type, event.code,
			    event.value);
	}

	return (ret);
}

static int
evdev_poll(struct cdev *dev, int events, struct thread *td)
{
	struct evdev_client *client;
	int ret;
	int revents = 0;

	ret = devfs_get_cdevpriv((void **)&client);
	if (ret != 0)
		return (POLLNVAL);

	debugf(client, "poll by thread %d", td->td_tid);

	if (client->ec_revoked)
		return (POLLHUP);

	if (events & (POLLIN | POLLRDNORM)) {
		EVDEV_CLIENT_LOCKQ(client);
		if (!EVDEV_CLIENT_EMPTYQ(client))
			revents = events & (POLLIN | POLLRDNORM);
		else {
			client->ec_selected = true;
			selrecord(td, &client->ec_selp);
		}
		EVDEV_CLIENT_UNLOCKQ(client);
	}

	return (revents);
}

static int
evdev_kqfilter(struct cdev *dev, struct knote *kn)
{
	struct evdev_client *client;
	int ret;

	ret = devfs_get_cdevpriv((void **)&client);
	if (ret != 0)
		return (ret);

	if (client->ec_revoked)
		return (ENODEV);

	switch(kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &evdev_cdev_filterops;
		break;
	default:
		return(EINVAL);
	}
	kn->kn_hook = (caddr_t)client;

	knlist_add(&client->ec_selp.si_note, kn, 0);
	return (0);
}

static int
evdev_kqread(struct knote *kn, long hint)
{
	struct evdev_client *client;
	int ret;

	client = (struct evdev_client *)kn->kn_hook;

	EVDEV_CLIENT_LOCKQ_ASSERT(client);

	if (client->ec_revoked) {
		kn->kn_flags |= EV_EOF;
		ret = 1;
	} else {
		kn->kn_data = EVDEV_CLIENT_SIZEQ(client) *
		    sizeof(struct input_event);
		ret = !EVDEV_CLIENT_EMPTYQ(client);
	}
	return (ret);
}

static void
evdev_kqdetach(struct knote *kn)
{
	struct evdev_client *client;

	client = (struct evdev_client *)kn->kn_hook;
	knlist_remove(&client->ec_selp.si_note, kn, 0);
}

static int
evdev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct evdev_dev *evdev = dev->si_drv1;
	struct evdev_client *client;
	struct input_keymap_entry *ke;
	int ret, len, limit, type_num;
	uint32_t code;
	size_t nvalues;

	ret = devfs_get_cdevpriv((void **)&client);
	if (ret != 0)
		return (ret);

	if (client->ec_revoked || evdev == NULL)
		return (ENODEV);

	/*
	 * Fix evdev state corrupted with discarding of kdb events.
	 * EVIOCGKEY and EVIOCGLED ioctls can suffer from this.
	 */
	if (evdev->ev_kdb_active) {
		EVDEV_LOCK(evdev);
		if (evdev->ev_kdb_active) {
			evdev->ev_kdb_active = false;
			evdev_restore_after_kdb(evdev);
		}
		EVDEV_UNLOCK(evdev);
	}

	/* file I/O ioctl handling */
	switch (cmd) {
	case FIOSETOWN:
		return (fsetown(*(int *)data, &client->ec_sigio));

	case FIOGETOWN:
		*(int *)data = fgetown(&client->ec_sigio);
		return (0);

	case FIONBIO:
		return (0);

	case FIOASYNC:
		if (*(int *)data)
			client->ec_async = true;
		else
			client->ec_async = false;

		return (0);

	case FIONREAD:
		EVDEV_CLIENT_LOCKQ(client);
		*(int *)data =
		    EVDEV_CLIENT_SIZEQ(client) * sizeof(struct input_event);
		EVDEV_CLIENT_UNLOCKQ(client);
		return (0);
	}

	len = IOCPARM_LEN(cmd);
	debugf(client, "ioctl called: cmd=0x%08lx, data=%p", cmd, data);

	/* evdev fixed-length ioctls handling */
	switch (cmd) {
	case EVIOCGVERSION:
		*(int *)data = EV_VERSION;
		return (0);

	case EVIOCGID:
		debugf(client, "EVIOCGID: bus=%d vendor=0x%04x product=0x%04x",
		    evdev->ev_id.bustype, evdev->ev_id.vendor,
		    evdev->ev_id.product);
		memcpy(data, &evdev->ev_id, sizeof(struct input_id));
		return (0);

	case EVIOCGREP:
		if (!evdev_event_supported(evdev, EV_REP))
			return (ENOTSUP);

		memcpy(data, evdev->ev_rep, sizeof(evdev->ev_rep));
		return (0);

	case EVIOCSREP:
		if (!evdev_event_supported(evdev, EV_REP))
			return (ENOTSUP);

		evdev_inject_event(evdev, EV_REP, REP_DELAY, ((int *)data)[0]);
		evdev_inject_event(evdev, EV_REP, REP_PERIOD,
		    ((int *)data)[1]);
		return (0);

	case EVIOCGKEYCODE:
		/* Fake unsupported ioctl */
		return (0);

	case EVIOCGKEYCODE_V2:
		if (evdev->ev_methods == NULL ||
		    evdev->ev_methods->ev_get_keycode == NULL)
			return (ENOTSUP);

		ke = (struct input_keymap_entry *)data;
		evdev->ev_methods->ev_get_keycode(evdev, ke);
		return (0);

	case EVIOCSKEYCODE:
		/* Fake unsupported ioctl */
		return (0);

	case EVIOCSKEYCODE_V2:
		if (evdev->ev_methods == NULL ||
		    evdev->ev_methods->ev_set_keycode == NULL)
			return (ENOTSUP);

		ke = (struct input_keymap_entry *)data;
		evdev->ev_methods->ev_set_keycode(evdev, ke);
		return (0);

	case EVIOCGABS(0) ... EVIOCGABS(ABS_MAX):
		if (evdev->ev_absinfo == NULL)
			return (EINVAL);

		memcpy(data, &evdev->ev_absinfo[cmd - EVIOCGABS(0)],
		    sizeof(struct input_absinfo));
		return (0);

	case EVIOCSABS(0) ... EVIOCSABS(ABS_MAX):
		if (evdev->ev_absinfo == NULL)
			return (EINVAL);

		code = cmd - EVIOCSABS(0);
		/* mt-slot number can not be changed */
		if (code == ABS_MT_SLOT)
			return (EINVAL);

		EVDEV_LOCK(evdev);
		evdev_set_absinfo(evdev, code, (struct input_absinfo *)data);
		EVDEV_UNLOCK(evdev);
		return (0);

	case EVIOCSFF:
	case EVIOCRMFF:
	case EVIOCGEFFECTS:
		/* Fake unsupported ioctls */
		return (0);

	case EVIOCGRAB:
		EVDEV_LOCK(evdev);
		if (*(int *)data)
			ret = evdev_grab_client(evdev, client);
		else
			ret = evdev_release_client(evdev, client);
		EVDEV_UNLOCK(evdev);
		return (ret);

	case EVIOCREVOKE:
		if (*(int *)data != 0)
			return (EINVAL);

		EVDEV_LOCK(evdev);
		if (dev->si_drv1 != NULL && !client->ec_revoked) {
			evdev_dispose_client(evdev, client);
			evdev_revoke_client(client);
		}
		EVDEV_UNLOCK(evdev);
		return (0);

	case EVIOCSCLOCKID:
		switch (*(int *)data) {
		case CLOCK_REALTIME:
			client->ec_clock_id = EV_CLOCK_REALTIME;
			return (0);
		case CLOCK_MONOTONIC:
			client->ec_clock_id = EV_CLOCK_MONOTONIC;
			return (0);
		default:
			return (EINVAL);
		}
	}

	/* evdev variable-length ioctls handling */
	switch (IOCBASECMD(cmd)) {
	case EVIOCGNAME(0):
		strlcpy(data, evdev->ev_name, len);
		return (0);

	case EVIOCGPHYS(0):
		if (evdev->ev_shortname[0] == 0)
			return (ENOENT);

		strlcpy(data, evdev->ev_shortname, len);
		return (0);

	case EVIOCGUNIQ(0):
		if (evdev->ev_serial[0] == 0)
			return (ENOENT);

		strlcpy(data, evdev->ev_serial, len);
		return (0);

	case EVIOCGPROP(0):
		limit = MIN(len, bitstr_size(INPUT_PROP_CNT));
		memcpy(data, evdev->ev_prop_flags, limit);
		return (0);

	case EVIOCGMTSLOTS(0):
		if (evdev->ev_mt == NULL)
			return (EINVAL);
		if (len < sizeof(uint32_t))
			return (EINVAL);
		code = *(uint32_t *)data;
		if (!ABS_IS_MT(code))
			return (EINVAL);

		nvalues =
		    MIN(len / sizeof(int32_t) - 1, MAXIMAL_MT_SLOT(evdev) + 1);
		for (int i = 0; i < nvalues; i++)
			((int32_t *)data)[i + 1] =
			    evdev_get_mt_value(evdev, i, code);
		return (0);

	case EVIOCGKEY(0):
		limit = MIN(len, bitstr_size(KEY_CNT));
		EVDEV_LOCK(evdev);
		evdev_client_filter_queue(client, EV_KEY);
		memcpy(data, evdev->ev_key_states, limit);
		EVDEV_UNLOCK(evdev);
		return (0);

	case EVIOCGLED(0):
		limit = MIN(len, bitstr_size(LED_CNT));
		EVDEV_LOCK(evdev);
		evdev_client_filter_queue(client, EV_LED);
		memcpy(data, evdev->ev_led_states, limit);
		EVDEV_UNLOCK(evdev);
		return (0);

	case EVIOCGSND(0):
		limit = MIN(len, bitstr_size(SND_CNT));
		EVDEV_LOCK(evdev);
		evdev_client_filter_queue(client, EV_SND);
		memcpy(data, evdev->ev_snd_states, limit);
		EVDEV_UNLOCK(evdev);
		return (0);

	case EVIOCGSW(0):
		limit = MIN(len, bitstr_size(SW_CNT));
		EVDEV_LOCK(evdev);
		evdev_client_filter_queue(client, EV_SW);
		memcpy(data, evdev->ev_sw_states, limit);
		EVDEV_UNLOCK(evdev);
		return (0);

	case EVIOCGBIT(0, 0) ... EVIOCGBIT(EV_MAX, 0):
		type_num = IOCBASECMD(cmd) - EVIOCGBIT(0, 0);
		debugf(client, "EVIOCGBIT(%d): data=%p, len=%d", type_num,
		    data, len);
		return (evdev_ioctl_eviocgbit(evdev, type_num, len, data));
	}

	return (EINVAL);
}

static int
evdev_ioctl_eviocgbit(struct evdev_dev *evdev, int type, int len, caddr_t data)
{
	unsigned long *bitmap;
	int limit;

	switch (type) {
	case 0:
		bitmap = evdev->ev_type_flags;
		limit = EV_CNT;
		break;
	case EV_KEY:
		bitmap = evdev->ev_key_flags;
		limit = KEY_CNT;
		break;
	case EV_REL:
		bitmap = evdev->ev_rel_flags;
		limit = REL_CNT;
		break;
	case EV_ABS:
		bitmap = evdev->ev_abs_flags;
		limit = ABS_CNT;
		break;
	case EV_MSC:
		bitmap = evdev->ev_msc_flags;
		limit = MSC_CNT;
		break;
	case EV_LED:
		bitmap = evdev->ev_led_flags;
		limit = LED_CNT;
		break;
	case EV_SND:
		bitmap = evdev->ev_snd_flags;
		limit = SND_CNT;
		break;
	case EV_SW:
		bitmap = evdev->ev_sw_flags;
		limit = SW_CNT;
		break;
	case EV_FF:
		/*
		 * We don't support EV_FF now, so let's
		 * just fake it returning only zeros.
		 */
		bzero(data, len);
		return (0);
	default:
		return (ENOTTY);
	}

	/*
	 * Clear ioctl data buffer in case it's bigger than
	 * bitmap size
	 */
	bzero(data, len);

	limit = bitstr_size(limit);
	len = MIN(limit, len);
	memcpy(data, bitmap, len);
	return (0);
}

void
evdev_revoke_client(struct evdev_client *client)
{

	EVDEV_LOCK_ASSERT(client->ec_evdev);

	client->ec_revoked = true;
}

void
evdev_notify_event(struct evdev_client *client)
{

	EVDEV_CLIENT_LOCKQ_ASSERT(client);

	if (client->ec_blocked) {
		client->ec_blocked = false;
		wakeup(client);
	}
	if (client->ec_selected) {
		client->ec_selected = false;
		selwakeup(&client->ec_selp);
	}
	KNOTE_LOCKED(&client->ec_selp.si_note, 0);

	if (client->ec_async && client->ec_sigio != NULL)
		pgsigio(&client->ec_sigio, SIGIO, 0);
}

int
evdev_cdev_create(struct evdev_dev *evdev)
{
	struct make_dev_args mda;
	int ret, unit = 0;

	make_dev_args_init(&mda);
	mda.mda_flags = MAKEDEV_WAITOK | MAKEDEV_CHECKNAME;
	mda.mda_devsw = &evdev_cdevsw;
	mda.mda_uid = UID_ROOT;
	mda.mda_gid = GID_WHEEL;
	mda.mda_mode = 0600;
	mda.mda_si_drv1 = evdev;

	/* Try to coexist with cuse-backed input/event devices */
	while ((ret = make_dev_s(&mda, &evdev->ev_cdev, "input/event%d", unit))
	    == EEXIST)
		unit++;

	if (ret == 0)
		evdev->ev_unit = unit;

	return (ret);
}

int
evdev_cdev_destroy(struct evdev_dev *evdev)
{

	destroy_dev(evdev->ev_cdev);
	return (0);
}

static void
evdev_client_gettime(struct evdev_client *client, struct timeval *tv)
{

	switch (client->ec_clock_id) {
	case EV_CLOCK_BOOTTIME:
		/*
		 * XXX: FreeBSD does not support true POSIX monotonic clock.
		 *      So aliase EV_CLOCK_BOOTTIME to EV_CLOCK_MONOTONIC.
		 */
	case EV_CLOCK_MONOTONIC:
		microuptime(tv);
		break;

	case EV_CLOCK_REALTIME:
	default:
		microtime(tv);
		break;
	}
}

void
evdev_client_push(struct evdev_client *client, uint16_t type, uint16_t code,
    int32_t value)
{
	struct timeval time;
	size_t count, head, tail, ready;

	EVDEV_CLIENT_LOCKQ_ASSERT(client);
	head = client->ec_buffer_head;
	tail = client->ec_buffer_tail;
	ready = client->ec_buffer_ready;
	count = client->ec_buffer_size;

	/* If queue is full drop its content and place SYN_DROPPED event */
	if ((tail + 1) % count == head) {
		debugf(client, "client %p: buffer overflow", client);

		head = (tail + count - 1) % count;
		client->ec_buffer[head] = (struct input_event) {
			.type = EV_SYN,
			.code = SYN_DROPPED,
			.value = 0
		};
		/*
		 * XXX: Here is a small race window from now till the end of
		 *      report. The queue is empty but client has been already
		 *      notified of data readyness. Can be fixed in two ways:
		 * 1. Implement bulk insert so queue lock would not be dropped
		 *    till the SYN_REPORT event.
		 * 2. Insert SYN_REPORT just now and skip remaining events
		 */
		client->ec_buffer_head = head;
		client->ec_buffer_ready = head;
	}

	client->ec_buffer[tail].type = type;
	client->ec_buffer[tail].code = code;
	client->ec_buffer[tail].value = value;
	client->ec_buffer_tail = (tail + 1) % count;

	/* Allow users to read events only after report has been completed */
	if (type == EV_SYN && code == SYN_REPORT) {
		evdev_client_gettime(client, &time);
		for (; ready != client->ec_buffer_tail;
		    ready = (ready + 1) % count)
			client->ec_buffer[ready].time = time;
		client->ec_buffer_ready = client->ec_buffer_tail;
	}
}

void
evdev_client_dumpqueue(struct evdev_client *client)
{
	struct input_event *event;
	size_t i, head, tail, ready, size;

	head = client->ec_buffer_head;
	tail = client->ec_buffer_tail;
	ready = client->ec_buffer_ready;
	size = client->ec_buffer_size;

	printf("evdev client: %p\n", client);
	printf("event queue: head=%zu ready=%zu tail=%zu size=%zu\n",
	    head, ready, tail, size);

	printf("queue contents:\n");

	for (i = 0; i < size; i++) {
		event = &client->ec_buffer[i];
		printf("%zu: ", i);

		if (i < head || i > tail)
			printf("unused\n");
		else
			printf("type=%d code=%d value=%d ", event->type,
			    event->code, event->value);

		if (i == head)
			printf("<- head\n");
		else if (i == tail)
			printf("<- tail\n");
		else if (i == ready)
			printf("<- ready\n");
		else
			printf("\n");
	}
}

static void
evdev_client_filter_queue(struct evdev_client *client, uint16_t type)
{
	struct input_event *event;
	size_t head, tail, count, i;
	bool last_was_syn = false;

	EVDEV_CLIENT_LOCKQ(client);

	i = head = client->ec_buffer_head;
	tail = client->ec_buffer_tail;
	count = client->ec_buffer_size;
	client->ec_buffer_ready = client->ec_buffer_tail;

	while (i != client->ec_buffer_tail) {
		event = &client->ec_buffer[i];
		i = (i + 1) % count;

		/* Skip event of given type */
		if (event->type == type)
			continue;

		/* Remove empty SYN_REPORT events */
		if (event->type == EV_SYN && event->code == SYN_REPORT) {
			if (last_was_syn)
				continue;
			else
				client->ec_buffer_ready = (tail + 1) % count;
		}

		/* Rewrite entry */
		memcpy(&client->ec_buffer[tail], event,
		    sizeof(struct input_event));

		last_was_syn = (event->type == EV_SYN &&
		    event->code == SYN_REPORT);

		tail = (tail + 1) % count;
	}

	client->ec_buffer_head = i;
	client->ec_buffer_tail = tail;

	EVDEV_CLIENT_UNLOCKQ(client);
}
