/*-
 * Copyright (c) 2015 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/chip.h>

#include "mixer_if.h"

#include "interface/compat/vchi_bsd.h"
#include "interface/vchi/vchi.h"
#include "interface/vchiq_arm/vchiq.h"

#include "vc_vchi_audioserv_defs.h"

SND_DECLARE_FILE("$FreeBSD$");

/* Audio destination */
#define	DEST_AUTO		0
#define	DEST_HEADPHONES		1
#define	DEST_HDMI		2

/* Playback state */
#define	PLAYBACK_IDLE		0
#define	PLAYBACK_PLAYING	1
#define	PLAYBACK_STOPPING	2

/* Worker thread state */
#define	WORKER_RUNNING		0
#define	WORKER_STOPPING		1
#define	WORKER_STOPPED		2

/*
 * Worker thread flags, set to 1 in flags_pending
 * when driver requests one or another operation
 * from worker. Cleared to 0 once worker performs
 * the operations.
 */
#define	AUDIO_PARAMS		(1 << 0)
#define	AUDIO_PLAY		(1 << 1)
#define	AUDIO_STOP		(1 << 2)

#define	VCHIQ_AUDIO_PACKET_SIZE	4000
#define	VCHIQ_AUDIO_BUFFER_SIZE	10*VCHIQ_AUDIO_PACKET_SIZE

#define	VCHIQ_AUDIO_MAX_VOLUME	
/* volume in terms of 0.01dB */
#define VCHIQ_AUDIO_VOLUME_MIN -10239
#define VCHIQ_AUDIO_VOLUME(db100) (uint32_t)(-((db100) << 8)/100)

/* dB levels with 5% volume step */
static int db_levels[] = {
	VCHIQ_AUDIO_VOLUME_MIN, -4605, -3794, -3218, -2772,
	-2407, -2099, -1832, -1597, -1386,
	-1195, -1021, -861, -713, -575,
	-446, -325, -210, -102, 0,
};

static uint32_t bcm2835_audio_playfmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S8, 1, 0),
	SND_FORMAT(AFMT_S8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	SND_FORMAT(AFMT_U16_LE, 1, 0),
	SND_FORMAT(AFMT_U16_LE, 2, 0),
	0
};

static struct pcmchan_caps bcm2835_audio_playcaps = {8000, 48000, bcm2835_audio_playfmt, 0};

struct bcm2835_audio_info;

struct bcm2835_audio_chinfo {
	struct bcm2835_audio_info *parent;
	struct pcm_channel *channel;
	struct snd_dbuf *buffer;
	uint32_t fmt, spd, blksz;

	/* Pointer to first unsubmitted sample */
	uint32_t unsubmittedptr;
	/*
	 * Number of bytes in "submitted but not played"
	 * pseudo-buffer
	 */
	int available_space;
	int playback_state;
	uint64_t callbacks;
	uint64_t submitted_samples;
	uint64_t retrieved_samples;
	uint64_t underruns;
	int starved;
};

struct bcm2835_audio_info {
	device_t dev;
	unsigned int bufsz;
    	struct bcm2835_audio_chinfo pch;
	uint32_t dest, volume;
	struct intr_config_hook intr_hook;

	/* VCHI data */
	VCHI_INSTANCE_T vchi_instance;
	VCHI_CONNECTION_T *vchi_connection;
	VCHI_SERVICE_HANDLE_T vchi_handle;

	struct mtx lock;
	struct cv worker_cv;

	uint32_t flags_pending;

	/* Worker thread state */
	int worker_state;
};

#define BCM2835_AUDIO_LOCK(sc)		mtx_lock(&(sc)->lock)
#define BCM2835_AUDIO_LOCKED(sc)	mtx_assert(&(sc)->lock, MA_OWNED)
#define BCM2835_AUDIO_UNLOCK(sc)	mtx_unlock(&(sc)->lock)

static const char *
dest_description(uint32_t dest)
{
	switch (dest) {
		case DEST_AUTO:
			return "AUTO";
			break;

		case DEST_HEADPHONES:
			return "HEADPHONES";
			break;

		case DEST_HDMI:
			return "HDMI";
			break;
		default:
			return "UNKNOWN";
			break;
	}
}

static void
bcm2835_worker_update_params(struct bcm2835_audio_info *sc)
{

	BCM2835_AUDIO_LOCKED(sc);

	sc->flags_pending |= AUDIO_PARAMS;
	cv_signal(&sc->worker_cv);
}

static void
bcm2835_worker_play_start(struct bcm2835_audio_info *sc)
{
	BCM2835_AUDIO_LOCK(sc);
	sc->flags_pending &= ~(AUDIO_STOP);
	sc->flags_pending |= AUDIO_PLAY;
	cv_signal(&sc->worker_cv);
	BCM2835_AUDIO_UNLOCK(sc);
}

static void
bcm2835_worker_play_stop(struct bcm2835_audio_info *sc)
{
	BCM2835_AUDIO_LOCK(sc);
	sc->flags_pending &= ~(AUDIO_PLAY);
	sc->flags_pending |= AUDIO_STOP;
	cv_signal(&sc->worker_cv);
	BCM2835_AUDIO_UNLOCK(sc);
}

static void
bcm2835_audio_callback(void *param, const VCHI_CALLBACK_REASON_T reason, void *msg_handle)
{
	struct bcm2835_audio_info *sc = (struct bcm2835_audio_info *)param;
	int32_t status;
	uint32_t msg_len;
	VC_AUDIO_MSG_T m;

	if (reason != VCHI_CALLBACK_MSG_AVAILABLE)
		return;

	status = vchi_msg_dequeue(sc->vchi_handle,
	    &m, sizeof m, &msg_len, VCHI_FLAGS_NONE);
	if (m.type == VC_AUDIO_MSG_TYPE_RESULT) {
		if (m.u.result.success) {
			device_printf(sc->dev,
			    "msg type %08x failed\n",
			    m.type);
		}
	} else if (m.type == VC_AUDIO_MSG_TYPE_COMPLETE) {
		struct bcm2835_audio_chinfo *ch = m.u.complete.cookie;

		int count = m.u.complete.count & 0xffff;
		int perr = (m.u.complete.count & (1U << 30)) != 0;
		ch->callbacks++;
		if (perr)
			ch->underruns++;

		BCM2835_AUDIO_LOCK(sc);
		if (ch->playback_state != PLAYBACK_IDLE) {
			/* Prevent LOR */
			BCM2835_AUDIO_UNLOCK(sc);
			chn_intr(sc->pch.channel);
			BCM2835_AUDIO_LOCK(sc);
		}
		/* We should check again, state might have changed */
		if (ch->playback_state != PLAYBACK_IDLE) {
			if (!perr) {
				if ((ch->available_space + count)> VCHIQ_AUDIO_BUFFER_SIZE) {
					device_printf(sc->dev, "inconsistent data in callback:\n");
					device_printf(sc->dev, "available_space == %d, count = %d, perr=%d\n",
					    ch->available_space, count, perr);
					device_printf(sc->dev,
					    "retrieved_samples = %lld, submitted_samples = %lld\n",
					    ch->retrieved_samples, ch->submitted_samples);
				}
				ch->available_space += count;
				ch->retrieved_samples += count;
			}
			if (perr || (ch->available_space >= VCHIQ_AUDIO_PACKET_SIZE))
				cv_signal(&sc->worker_cv);
		}
		BCM2835_AUDIO_UNLOCK(sc);
	} else
		printf("%s: unknown m.type: %d\n", __func__, m.type);
}

/* VCHIQ stuff */
static void
bcm2835_audio_init(struct bcm2835_audio_info *sc)
{
	int status;

	/* Initialize and create a VCHI connection */
	status = vchi_initialise(&sc->vchi_instance);
	if (status != 0) {
		printf("vchi_initialise failed: %d\n", status);
		return;
	}

	status = vchi_connect(NULL, 0, sc->vchi_instance);
	if (status != 0) {
		printf("vchi_connect failed: %d\n", status);
		return;
	}

	SERVICE_CREATION_T params = {
	    VCHI_VERSION_EX(VC_AUDIOSERV_VER, VC_AUDIOSERV_MIN_VER),
	    VC_AUDIO_SERVER_NAME,   /* 4cc service code */
	    sc->vchi_connection,    /* passed in fn pointers */
	    0,  /* rx fifo size */
	    0,  /* tx fifo size */
	    bcm2835_audio_callback,    /* service callback */
	    sc,   /* service callback parameter */
	    1,
	    1,
	    0   /* want crc check on bulk transfers */
	};

	status = vchi_service_open(sc->vchi_instance, &params,
	    &sc->vchi_handle);

	if (status != 0)
		sc->vchi_handle = VCHIQ_SERVICE_HANDLE_INVALID;
}

static void
bcm2835_audio_release(struct bcm2835_audio_info *sc)
{
	int success;

	if (sc->vchi_handle != VCHIQ_SERVICE_HANDLE_INVALID) {
		success = vchi_service_close(sc->vchi_handle);
		if (success != 0)
			printf("vchi_service_close failed: %d\n", success);
		vchi_service_release(sc->vchi_handle);
		sc->vchi_handle = VCHIQ_SERVICE_HANDLE_INVALID;
	}

	vchi_disconnect(sc->vchi_instance);
}

static void
bcm2835_audio_reset_channel(struct bcm2835_audio_chinfo *ch)
{

	ch->available_space = VCHIQ_AUDIO_BUFFER_SIZE;
	ch->unsubmittedptr = 0;
	sndbuf_reset(ch->buffer);
}

static void
bcm2835_audio_start(struct bcm2835_audio_chinfo *ch)
{
	VC_AUDIO_MSG_T m;
	int ret;
	struct bcm2835_audio_info *sc = ch->parent;

	if (sc->vchi_handle != VCHIQ_SERVICE_HANDLE_INVALID) {
		m.type = VC_AUDIO_MSG_TYPE_START;
		ret = vchi_msg_queue(sc->vchi_handle,
		    &m, sizeof m, VCHI_FLAGS_BLOCK_UNTIL_QUEUED, NULL);

		if (ret != 0)
			printf("%s: vchi_msg_queue failed (err %d)\n", __func__, ret);
	}
}

static void
bcm2835_audio_stop(struct bcm2835_audio_chinfo *ch)
{
	VC_AUDIO_MSG_T m;
	int ret;
	struct bcm2835_audio_info *sc = ch->parent;

	if (sc->vchi_handle != VCHIQ_SERVICE_HANDLE_INVALID) {
		m.type = VC_AUDIO_MSG_TYPE_STOP;
		m.u.stop.draining = 0;

		ret = vchi_msg_queue(sc->vchi_handle,
		    &m, sizeof m, VCHI_FLAGS_BLOCK_UNTIL_QUEUED, NULL);

		if (ret != 0)
			printf("%s: vchi_msg_queue failed (err %d)\n", __func__, ret);
	}
}

static void
bcm2835_audio_open(struct bcm2835_audio_info *sc)
{
	VC_AUDIO_MSG_T m;
	int ret;

	if (sc->vchi_handle != VCHIQ_SERVICE_HANDLE_INVALID) {
		m.type = VC_AUDIO_MSG_TYPE_OPEN;
		ret = vchi_msg_queue(sc->vchi_handle,
		    &m, sizeof m, VCHI_FLAGS_BLOCK_UNTIL_QUEUED, NULL);

		if (ret != 0)
			printf("%s: vchi_msg_queue failed (err %d)\n", __func__, ret);
	}
}

static void
bcm2835_audio_update_controls(struct bcm2835_audio_info *sc, uint32_t volume, uint32_t dest)
{
	VC_AUDIO_MSG_T m;
	int ret, db;

	if (sc->vchi_handle != VCHIQ_SERVICE_HANDLE_INVALID) {
		m.type = VC_AUDIO_MSG_TYPE_CONTROL;
		m.u.control.dest = dest;
		if (volume > 99)
			volume = 99;
		db = db_levels[volume/5];
		m.u.control.volume = VCHIQ_AUDIO_VOLUME(db);

		ret = vchi_msg_queue(sc->vchi_handle,
		    &m, sizeof m, VCHI_FLAGS_BLOCK_UNTIL_QUEUED, NULL);

		if (ret != 0)
			printf("%s: vchi_msg_queue failed (err %d)\n", __func__, ret);
	}
}

static void
bcm2835_audio_update_params(struct bcm2835_audio_info *sc, uint32_t fmt, uint32_t speed)
{
	VC_AUDIO_MSG_T m;
	int ret;

	if (sc->vchi_handle != VCHIQ_SERVICE_HANDLE_INVALID) {
		m.type = VC_AUDIO_MSG_TYPE_CONFIG;
		m.u.config.channels = AFMT_CHANNEL(fmt);
		m.u.config.samplerate = speed;
		m.u.config.bps = AFMT_BIT(fmt);

		ret = vchi_msg_queue(sc->vchi_handle,
		    &m, sizeof m, VCHI_FLAGS_BLOCK_UNTIL_QUEUED, NULL);

		if (ret != 0)
			printf("%s: vchi_msg_queue failed (err %d)\n", __func__, ret);
	}
}

static bool
bcm2835_audio_buffer_should_sleep(struct bcm2835_audio_chinfo *ch)
{
	
	if (ch->playback_state != PLAYBACK_PLAYING)
		return (true);

	/* Not enough data */
	if (sndbuf_getready(ch->buffer) < VCHIQ_AUDIO_PACKET_SIZE) {
		printf("starve\n");
		ch->starved++;
		return (true);
	}

	/* Not enough free space */
	if (ch->available_space < VCHIQ_AUDIO_PACKET_SIZE) {
		return (true);
	}

	return (false);
}

static void
bcm2835_audio_write_samples(struct bcm2835_audio_chinfo *ch, void *buf, uint32_t count)
{
	struct bcm2835_audio_info *sc = ch->parent;
	VC_AUDIO_MSG_T m;
	int ret;

	if (sc->vchi_handle == VCHIQ_SERVICE_HANDLE_INVALID) {
		return;
	}

	m.type = VC_AUDIO_MSG_TYPE_WRITE;
	m.u.write.count = count;
	m.u.write.max_packet = VCHIQ_AUDIO_PACKET_SIZE;
	m.u.write.callback = NULL;
	m.u.write.cookie = ch;
	m.u.write.silence = 0;

	ret = vchi_msg_queue(sc->vchi_handle,
	    &m, sizeof m, VCHI_FLAGS_BLOCK_UNTIL_QUEUED, NULL);

	if (ret != 0)
		printf("%s: vchi_msg_queue failed (err %d)\n", __func__, ret);

	while (count > 0) {
		int bytes = MIN((int)m.u.write.max_packet, (int)count);
		ret = vchi_msg_queue(sc->vchi_handle,
		    buf, bytes, VCHI_FLAGS_BLOCK_UNTIL_QUEUED, NULL);
		if (ret != 0)
			printf("%s: vchi_msg_queue failed: %d\n",
			    __func__, ret);
		buf = (char *)buf + bytes;
		count -= bytes;
	}
}

static void
bcm2835_audio_worker(void *data)
{
	struct bcm2835_audio_info *sc = (struct bcm2835_audio_info *)data;
	struct bcm2835_audio_chinfo *ch = &sc->pch;
	uint32_t speed, format;
	uint32_t volume, dest;
	uint32_t flags;
	uint32_t count, size, readyptr;
	uint8_t *buf;

	ch->playback_state = PLAYBACK_IDLE;

	while (1) {
		if (sc->worker_state != WORKER_RUNNING)
			break;

		BCM2835_AUDIO_LOCK(sc);
		/*
		 * wait until there are flags set or buffer is ready
		 * to consume more samples
		 */
		while ((sc->flags_pending == 0) &&
		    bcm2835_audio_buffer_should_sleep(ch)) {
			cv_wait_sig(&sc->worker_cv, &sc->lock);
		}
		flags = sc->flags_pending;
		/* Clear pending flags */
		sc->flags_pending = 0;
		BCM2835_AUDIO_UNLOCK(sc);

		/* Requested to change parameters */
		if (flags & AUDIO_PARAMS) {
			BCM2835_AUDIO_LOCK(sc);
			speed = ch->spd;
			format = ch->fmt;
			volume = sc->volume;
			dest = sc->dest;
			BCM2835_AUDIO_UNLOCK(sc);
			if (ch->playback_state == PLAYBACK_IDLE)
				bcm2835_audio_update_params(sc, format, speed);
			bcm2835_audio_update_controls(sc, volume, dest);
		}

		/* Requested to stop playback */
		if ((flags & AUDIO_STOP) &&
		    (ch->playback_state == PLAYBACK_PLAYING)) {
			bcm2835_audio_stop(ch);
			BCM2835_AUDIO_LOCK(sc);
			bcm2835_audio_reset_channel(&sc->pch);
			ch->playback_state = PLAYBACK_IDLE;
			BCM2835_AUDIO_UNLOCK(sc);
			continue;
		}

		/* Requested to start playback */
		if ((flags & AUDIO_PLAY) &&
		    (ch->playback_state == PLAYBACK_IDLE)) {
			BCM2835_AUDIO_LOCK(sc);
			ch->playback_state = PLAYBACK_PLAYING;
			BCM2835_AUDIO_UNLOCK(sc);
			bcm2835_audio_start(ch);
		}

		if (ch->playback_state == PLAYBACK_IDLE)
			continue;

		if (sndbuf_getready(ch->buffer) == 0)
			continue;

		count = sndbuf_getready(ch->buffer);
		size = sndbuf_getsize(ch->buffer);
		readyptr = sndbuf_getreadyptr(ch->buffer);

		BCM2835_AUDIO_LOCK(sc);
		if (readyptr + count > size)
			count = size - readyptr;
		count = min(count, ch->available_space);
		count -= (count % VCHIQ_AUDIO_PACKET_SIZE);
		BCM2835_AUDIO_UNLOCK(sc);

		if (count < VCHIQ_AUDIO_PACKET_SIZE)
			continue;

		buf = (uint8_t*)sndbuf_getbuf(ch->buffer) + readyptr;

		bcm2835_audio_write_samples(ch, buf, count);
		BCM2835_AUDIO_LOCK(sc);
		ch->unsubmittedptr = (ch->unsubmittedptr + count) % sndbuf_getsize(ch->buffer);
		ch->available_space -= count;
		ch->submitted_samples += count;
		KASSERT(ch->available_space >= 0, ("ch->available_space == %d\n", ch->available_space));
		BCM2835_AUDIO_UNLOCK(sc);
	}

	BCM2835_AUDIO_LOCK(sc);
	sc->worker_state = WORKER_STOPPED;
	cv_signal(&sc->worker_cv);
	BCM2835_AUDIO_UNLOCK(sc);

	kproc_exit(0);
}

static void
bcm2835_audio_create_worker(struct bcm2835_audio_info *sc)
{
	struct proc *newp;

	sc->worker_state = WORKER_RUNNING;
	if (kproc_create(bcm2835_audio_worker, (void*)sc, &newp, 0, 0,
	    "bcm2835_audio_worker") != 0) {
		printf("failed to create bcm2835_audio_worker\n");
	}
}

/* -------------------------------------------------------------------- */
/* channel interface for VCHI audio */
static void *
bcmchan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct bcm2835_audio_info *sc = devinfo;
	struct bcm2835_audio_chinfo *ch = &sc->pch;
	void *buffer;

	if (dir == PCMDIR_REC)
		return NULL;

	ch->parent = sc;
	ch->channel = c;
	ch->buffer = b;

	/* default values */
	ch->spd = 44100;
	ch->fmt = SND_FORMAT(AFMT_S16_LE, 2, 0);
	ch->blksz = VCHIQ_AUDIO_PACKET_SIZE;

	buffer = malloc(sc->bufsz, M_DEVBUF, M_WAITOK | M_ZERO);

	if (sndbuf_setup(ch->buffer, buffer, sc->bufsz) != 0) {
		device_printf(sc->dev, "sndbuf_setup failed\n");
		free(buffer, M_DEVBUF);
		return NULL;
	}

	BCM2835_AUDIO_LOCK(sc);
	bcm2835_worker_update_params(sc);
	BCM2835_AUDIO_UNLOCK(sc);

	return ch;
}

static int
bcmchan_free(kobj_t obj, void *data)
{
	struct bcm2835_audio_chinfo *ch = data;
	void *buffer;

	buffer = sndbuf_getbuf(ch->buffer);
	if (buffer)
		free(buffer, M_DEVBUF);

	return (0);
}

static int
bcmchan_setformat(kobj_t obj, void *data, uint32_t format)
{
	struct bcm2835_audio_chinfo *ch = data;
	struct bcm2835_audio_info *sc = ch->parent;

	BCM2835_AUDIO_LOCK(sc);
	ch->fmt = format;
	bcm2835_worker_update_params(sc);
	BCM2835_AUDIO_UNLOCK(sc);

	return 0;
}

static uint32_t
bcmchan_setspeed(kobj_t obj, void *data, uint32_t speed)
{
	struct bcm2835_audio_chinfo *ch = data;
	struct bcm2835_audio_info *sc = ch->parent;

	BCM2835_AUDIO_LOCK(sc);
	ch->spd = speed;
	bcm2835_worker_update_params(sc);
	BCM2835_AUDIO_UNLOCK(sc);

	return ch->spd;
}

static uint32_t
bcmchan_setblocksize(kobj_t obj, void *data, uint32_t blocksize)
{
	struct bcm2835_audio_chinfo *ch = data;

	return ch->blksz;
}

static int
bcmchan_trigger(kobj_t obj, void *data, int go)
{
	struct bcm2835_audio_chinfo *ch = data;
	struct bcm2835_audio_info *sc = ch->parent;

	if (!PCMTRIG_COMMON(go))
		return (0);

	switch (go) {
	case PCMTRIG_START:
		/* kickstart data flow */
		chn_intr(sc->pch.channel);
		ch->submitted_samples = 0;
		ch->retrieved_samples = 0;
		bcm2835_worker_play_start(sc);
		break;

	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		bcm2835_worker_play_stop(sc);
		break;

	default:
		break;
	}
	return 0;
}

static uint32_t
bcmchan_getptr(kobj_t obj, void *data)
{
	struct bcm2835_audio_chinfo *ch = data;
	struct bcm2835_audio_info *sc = ch->parent;
	uint32_t ret;

	BCM2835_AUDIO_LOCK(sc);
	ret = ch->unsubmittedptr;
	BCM2835_AUDIO_UNLOCK(sc);

	return ret;
}

static struct pcmchan_caps *
bcmchan_getcaps(kobj_t obj, void *data)
{

	return &bcm2835_audio_playcaps;
}

static kobj_method_t bcmchan_methods[] = {
    	KOBJMETHOD(channel_init,		bcmchan_init),
    	KOBJMETHOD(channel_free,		bcmchan_free),
    	KOBJMETHOD(channel_setformat,		bcmchan_setformat),
    	KOBJMETHOD(channel_setspeed,		bcmchan_setspeed),
    	KOBJMETHOD(channel_setblocksize,	bcmchan_setblocksize),
    	KOBJMETHOD(channel_trigger,		bcmchan_trigger),
    	KOBJMETHOD(channel_getptr,		bcmchan_getptr),
    	KOBJMETHOD(channel_getcaps,		bcmchan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(bcmchan);

/************************************************************/

static int
bcmmix_init(struct snd_mixer *m)
{

	mix_setdevs(m, SOUND_MASK_VOLUME);

	return (0);
}

static int
bcmmix_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
    	struct bcm2835_audio_info *sc = mix_getdevinfo(m);

	switch (dev) {
	case SOUND_MIXER_VOLUME:
		BCM2835_AUDIO_LOCK(sc);
		sc->volume = left;
		bcm2835_worker_update_params(sc);
		BCM2835_AUDIO_UNLOCK(sc);

		break;

	default:
		break;
	}

    	return left | (left << 8);
}

static kobj_method_t bcmmixer_methods[] = {
    	KOBJMETHOD(mixer_init,		bcmmix_init),
    	KOBJMETHOD(mixer_set,		bcmmix_set),
	KOBJMETHOD_END
};

MIXER_DECLARE(bcmmixer);

static int
sysctl_bcm2835_audio_dest(SYSCTL_HANDLER_ARGS)
{
	struct bcm2835_audio_info *sc = arg1;
	int val;
	int err;

	val = sc->dest;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err || !req->newptr) /* error || read request */
		return (err);

	if ((val < 0) || (val > 2))
		return (EINVAL);

	BCM2835_AUDIO_LOCK(sc);
	sc->dest = val;
	bcm2835_worker_update_params(sc);
	BCM2835_AUDIO_UNLOCK(sc);

	if (bootverbose)
		device_printf(sc->dev, "destination set to %s\n", dest_description(val));

	return (0);
}

static void
vchi_audio_sysctl_init(struct bcm2835_audio_info *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree_node;
	struct sysctl_oid_list *tree;

	/*
	 * Add system sysctl tree/handlers.
	 */
	ctx = device_get_sysctl_ctx(sc->dev);
	tree_node = device_get_sysctl_tree(sc->dev);
	tree = SYSCTL_CHILDREN(tree_node);
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "dest",
	    CTLFLAG_RW | CTLTYPE_UINT, sc, sizeof(*sc),
	    sysctl_bcm2835_audio_dest, "IU", "audio destination, "
	    "0 - auto, 1 - headphones, 2 - HDMI");
	SYSCTL_ADD_UQUAD(ctx, tree, OID_AUTO, "callbacks",
			CTLFLAG_RD, &sc->pch.callbacks,
			"callbacks total");
	SYSCTL_ADD_UQUAD(ctx, tree, OID_AUTO, "submitted",
			CTLFLAG_RD, &sc->pch.submitted_samples,
			"last play submitted samples");
	SYSCTL_ADD_UQUAD(ctx, tree, OID_AUTO, "retrieved",
			CTLFLAG_RD, &sc->pch.retrieved_samples,
			"last play retrieved samples");
	SYSCTL_ADD_UQUAD(ctx, tree, OID_AUTO, "underruns",
			CTLFLAG_RD, &sc->pch.underruns,
			"callback underruns");
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO, "freebuffer",
			CTLFLAG_RD, &sc->pch.available_space,
			sc->pch.available_space, "callbacks total");
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO, "starved",
			CTLFLAG_RD, &sc->pch.starved,
			sc->pch.starved, "number of starved conditions");
}

static void
bcm2835_audio_identify(driver_t *driver, device_t parent)
{

	BUS_ADD_CHILD(parent, 0, "pcm", 0);
}

static int
bcm2835_audio_probe(device_t dev)
{

	device_set_desc(dev, "VCHIQ audio");
	return (BUS_PROBE_DEFAULT);
}

static void
bcm2835_audio_delayed_init(void *xsc)
{
    	struct bcm2835_audio_info *sc;
    	char status[SND_STATUSLEN];

	sc = xsc;

	config_intrhook_disestablish(&sc->intr_hook);

	bcm2835_audio_init(sc);
	bcm2835_audio_open(sc);
	sc->volume = 75;
	sc->dest = DEST_AUTO;

    	if (mixer_init(sc->dev, &bcmmixer_class, sc)) {
		device_printf(sc->dev, "mixer_init failed\n");
		goto no;
	}

    	if (pcm_register(sc->dev, sc, 1, 0)) {
		device_printf(sc->dev, "pcm_register failed\n");
		goto no;
	}

	pcm_addchan(sc->dev, PCMDIR_PLAY, &bcmchan_class, sc);
    	snprintf(status, SND_STATUSLEN, "at VCHIQ");
	pcm_setstatus(sc->dev, status);

	bcm2835_audio_reset_channel(&sc->pch);
	bcm2835_audio_create_worker(sc);

	vchi_audio_sysctl_init(sc);

no:
	;
}

static int
bcm2835_audio_attach(device_t dev)
{
    	struct bcm2835_audio_info *sc;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);

	sc->dev = dev;
	sc->bufsz = VCHIQ_AUDIO_BUFFER_SIZE;

	mtx_init(&sc->lock, device_get_nameunit(dev),
	    "bcm_audio_lock", MTX_DEF);
	cv_init(&sc->worker_cv, "worker_cv");
	sc->vchi_handle = VCHIQ_SERVICE_HANDLE_INVALID;

	/*
	 * We need interrupts enabled for VCHI to work properly,
	 * so delay initialization until it happens.
	 */
	sc->intr_hook.ich_func = bcm2835_audio_delayed_init;
	sc->intr_hook.ich_arg = sc;

	if (config_intrhook_establish(&sc->intr_hook) != 0)
		goto no;

    	return 0;

no:
    	return ENXIO;
}

static int
bcm2835_audio_detach(device_t dev)
{
	int r;
	struct bcm2835_audio_info *sc;
	sc = pcm_getdevinfo(dev);

	/* Stop worker thread */
	BCM2835_AUDIO_LOCK(sc);
	sc->worker_state = WORKER_STOPPING;
	cv_signal(&sc->worker_cv);
	/* Wait for thread to exit */
	while (sc->worker_state != WORKER_STOPPED)
		cv_wait_sig(&sc->worker_cv, &sc->lock);
	BCM2835_AUDIO_UNLOCK(sc);

	r = pcm_unregister(dev);
	if (r)
		return r;

	mtx_destroy(&sc->lock);
	cv_destroy(&sc->worker_cv);

	bcm2835_audio_release(sc);

    	free(sc, M_DEVBUF);

	return 0;
}

static device_method_t bcm2835_audio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	bcm2835_audio_identify),
	DEVMETHOD(device_probe,		bcm2835_audio_probe),
	DEVMETHOD(device_attach,	bcm2835_audio_attach),
	DEVMETHOD(device_detach,	bcm2835_audio_detach),

	{ 0, 0 }
};

static driver_t bcm2835_audio_driver = {
	"pcm",
	bcm2835_audio_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(bcm2835_audio, vchiq, bcm2835_audio_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(bcm2835_audio, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_DEPEND(bcm2835_audio, vchiq, 1, 1, 1);
MODULE_VERSION(bcm2835_audio, 1);
