/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Mathew Kanner
 * Copyright (c) 1993 Hannu Savolainen
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
 */

/*
 * The sequencer personality manager.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ioccom.h>

#include <sys/filio.h>
#include <sys/lock.h>
#include <sys/sockio.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <sys/kernel.h>			/* for DATA_SET */

#include <sys/module.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/syslog.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <machine/clock.h>		/* for DELAY */
#include <sys/soundcard.h>
#include <sys/rman.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/kthread.h>
#include <sys/unistd.h>
#include <sys/selinfo.h>

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/midi/midi.h>
#include <dev/sound/midi/midiq.h>
#include "synth_if.h"

#include <dev/sound/midi/sequencer.h>

#define TMR_TIMERBASE 13

#define SND_DEV_SEQ	1		/* Sequencer output /dev/sequencer (FM
					 * synthesizer and MIDI output) */
#define SND_DEV_MUSIC	8		/* /dev/music, level 2 interface */

/* Length of a sequencer event. */
#define EV_SZ 8
#define IEV_SZ 8

/* Lookup modes */
#define LOOKUP_EXIST	(0)
#define LOOKUP_OPEN	(1)
#define LOOKUP_CLOSE	(2)

#define PCMMKMINOR(u, d, c) \
	    ((((c) & 0xff) << 16) | (((u) & 0x0f) << 4) | ((d) & 0x0f))
#define MIDIMKMINOR(u, d, c) PCMMKMINOR(u, d, c)
#define MIDIUNIT(y) ((dev2unit(y) >> 4) & 0x0f)
#define MIDIDEV(y) (dev2unit(y) & 0x0f)

/* These are the entries to the sequencer driver. */
static d_open_t mseq_open;
static d_close_t mseq_close;
static d_ioctl_t mseq_ioctl;
static d_read_t mseq_read;
static d_write_t mseq_write;
static d_poll_t mseq_poll;

static struct cdevsw seq_cdevsw = {
	.d_version = D_VERSION,
	.d_open = mseq_open,
	.d_close = mseq_close,
	.d_read = mseq_read,
	.d_write = mseq_write,
	.d_ioctl = mseq_ioctl,
	.d_poll = mseq_poll,
	.d_name = "sequencer",
};

struct seq_softc {
	KOBJ_FIELDS;

	struct mtx seq_lock, q_lock;
	struct cv empty_cv, reset_cv, in_cv, out_cv, state_cv, th_cv;

	MIDIQ_HEAD(, u_char) in_q, out_q;

	u_long	flags;
	/* Flags (protected by flag_mtx of mididev_info) */
	int	fflags;			/* Access mode */
	int	music;

	int	out_water;		/* Sequence output threshould */
	snd_sync_parm sync_parm;	/* AIOSYNC parameter set */
	struct thread *sync_thread;	/* AIOSYNCing thread */
	struct selinfo in_sel, out_sel;
	int	midi_number;
	struct cdev *seqdev, *musicdev;
	int	unit;
	int	maxunits;
	kobj_t *midis;
	int    *midi_flags;
	kobj_t	mapper;
	void   *mapper_cookie;
	struct timeval timerstop, timersub;
	int	timerbase, tempo;
	int	timerrun;
	int	done;
	int	playing;
	int	recording;
	int	busy;
	int	pre_event_timeout;
	int	waiting;
};

/*
 * Module specific stuff, including how many sequecers
 * we currently own.
 */

SYSCTL_NODE(_hw_midi, OID_AUTO, seq, CTLFLAG_RD, 0, "Midi sequencer");

int					seq_debug;
/* XXX: should this be moved into debug.midi? */
SYSCTL_INT(_hw_midi_seq, OID_AUTO, debug, CTLFLAG_RW, &seq_debug, 0, "");

midi_cmdtab	cmdtab_seqevent[] = {
	{SEQ_NOTEOFF,		"SEQ_NOTEOFF"},
	{SEQ_NOTEON,		"SEQ_NOTEON"},
	{SEQ_WAIT,		"SEQ_WAIT"},
	{SEQ_PGMCHANGE,		"SEQ_PGMCHANGE"},
	{SEQ_SYNCTIMER,		"SEQ_SYNCTIMER"},
	{SEQ_MIDIPUTC,		"SEQ_MIDIPUTC"},
	{SEQ_DRUMON,		"SEQ_DRUMON"},
	{SEQ_DRUMOFF,		"SEQ_DRUMOFF"},
	{SEQ_ECHO,		"SEQ_ECHO"},
	{SEQ_AFTERTOUCH,	"SEQ_AFTERTOUCH"},
	{SEQ_CONTROLLER,	"SEQ_CONTROLLER"},
	{SEQ_BALANCE,		"SEQ_BALANCE"},
	{SEQ_VOLMODE,		"SEQ_VOLMODE"},
	{SEQ_FULLSIZE,		"SEQ_FULLSIZE"},
	{SEQ_PRIVATE,		"SEQ_PRIVATE"},
	{SEQ_EXTENDED,		"SEQ_EXTENDED"},
	{EV_SEQ_LOCAL,		"EV_SEQ_LOCAL"},
	{EV_TIMING,		"EV_TIMING"},
	{EV_CHN_COMMON,		"EV_CHN_COMMON"},
	{EV_CHN_VOICE,		"EV_CHN_VOICE"},
	{EV_SYSEX,		"EV_SYSEX"},
	{-1,			NULL},
};

midi_cmdtab	cmdtab_seqioctl[] = {
	{SNDCTL_SEQ_RESET,	"SNDCTL_SEQ_RESET"},
	{SNDCTL_SEQ_SYNC,	"SNDCTL_SEQ_SYNC"},
	{SNDCTL_SYNTH_INFO,	"SNDCTL_SYNTH_INFO"},
	{SNDCTL_SEQ_CTRLRATE,	"SNDCTL_SEQ_CTRLRATE"},
	{SNDCTL_SEQ_GETOUTCOUNT,	"SNDCTL_SEQ_GETOUTCOUNT"},
	{SNDCTL_SEQ_GETINCOUNT,	"SNDCTL_SEQ_GETINCOUNT"},
	{SNDCTL_SEQ_PERCMODE,	"SNDCTL_SEQ_PERCMODE"},
	{SNDCTL_FM_LOAD_INSTR,	"SNDCTL_FM_LOAD_INSTR"},
	{SNDCTL_SEQ_TESTMIDI,	"SNDCTL_SEQ_TESTMIDI"},
	{SNDCTL_SEQ_RESETSAMPLES,	"SNDCTL_SEQ_RESETSAMPLES"},
	{SNDCTL_SEQ_NRSYNTHS,	"SNDCTL_SEQ_NRSYNTHS"},
	{SNDCTL_SEQ_NRMIDIS,	"SNDCTL_SEQ_NRMIDIS"},
	{SNDCTL_SEQ_GETTIME,	"SNDCTL_SEQ_GETTIME"},
	{SNDCTL_MIDI_INFO,	"SNDCTL_MIDI_INFO"},
	{SNDCTL_SEQ_THRESHOLD,	"SNDCTL_SEQ_THRESHOLD"},
	{SNDCTL_SYNTH_MEMAVL,	"SNDCTL_SYNTH_MEMAVL"},
	{SNDCTL_FM_4OP_ENABLE,	"SNDCTL_FM_4OP_ENABLE"},
	{SNDCTL_PMGR_ACCESS,	"SNDCTL_PMGR_ACCESS"},
	{SNDCTL_SEQ_PANIC,	"SNDCTL_SEQ_PANIC"},
	{SNDCTL_SEQ_OUTOFBAND,	"SNDCTL_SEQ_OUTOFBAND"},
	{SNDCTL_TMR_TIMEBASE,	"SNDCTL_TMR_TIMEBASE"},
	{SNDCTL_TMR_START,	"SNDCTL_TMR_START"},
	{SNDCTL_TMR_STOP,	"SNDCTL_TMR_STOP"},
	{SNDCTL_TMR_CONTINUE,	"SNDCTL_TMR_CONTINUE"},
	{SNDCTL_TMR_TEMPO,	"SNDCTL_TMR_TEMPO"},
	{SNDCTL_TMR_SOURCE,	"SNDCTL_TMR_SOURCE"},
	{SNDCTL_TMR_METRONOME,	"SNDCTL_TMR_METRONOME"},
	{SNDCTL_TMR_SELECT,	"SNDCTL_TMR_SELECT"},
	{SNDCTL_MIDI_PRETIME,	"SNDCTL_MIDI_PRETIME"},
	{AIONWRITE,		"AIONWRITE"},
	{AIOGSIZE,		"AIOGSIZE"},
	{AIOSSIZE,		"AIOSSIZE"},
	{AIOGFMT,		"AIOGFMT"},
	{AIOSFMT,		"AIOSFMT"},
	{AIOGMIX,		"AIOGMIX"},
	{AIOSMIX,		"AIOSMIX"},
	{AIOSTOP,		"AIOSTOP"},
	{AIOSYNC,		"AIOSYNC"},
	{AIOGCAP,		"AIOGCAP"},
	{-1,			NULL},
};

midi_cmdtab	cmdtab_timer[] = {
	{TMR_WAIT_REL,	"TMR_WAIT_REL"},
	{TMR_WAIT_ABS,	"TMR_WAIT_ABS"},
	{TMR_STOP,	"TMR_STOP"},
	{TMR_START,	"TMR_START"},
	{TMR_CONTINUE,	"TMR_CONTINUE"},
	{TMR_TEMPO,	"TMR_TEMPO"},
	{TMR_ECHO,	"TMR_ECHO"},
	{TMR_CLOCK,	"TMR_CLOCK"},
	{TMR_SPP,	"TMR_SPP"},
	{TMR_TIMESIG,	"TMR_TIMESIG"},
	{-1,		NULL},
};

midi_cmdtab	cmdtab_seqcv[] = {
	{MIDI_NOTEOFF,		"MIDI_NOTEOFF"},
	{MIDI_NOTEON,		"MIDI_NOTEON"},
	{MIDI_KEY_PRESSURE,	"MIDI_KEY_PRESSURE"},
	{-1,			NULL},
};

midi_cmdtab	cmdtab_seqccmn[] = {
	{MIDI_CTL_CHANGE,	"MIDI_CTL_CHANGE"},
	{MIDI_PGM_CHANGE,	"MIDI_PGM_CHANGE"},
	{MIDI_CHN_PRESSURE,	"MIDI_CHN_PRESSURE"},
	{MIDI_PITCH_BEND,	"MIDI_PITCH_BEND"},
	{MIDI_SYSTEM_PREFIX,	"MIDI_SYSTEM_PREFIX"},
	{-1,			NULL},
};

#ifndef KOBJMETHOD_END
#define KOBJMETHOD_END	{ NULL, NULL }
#endif

/*
 * static const char *mpu401_mprovider(kobj_t obj, struct mpu401 *m);
 */

static kobj_method_t seq_methods[] = {
	/* KOBJMETHOD(mpu_provider,mpu401_mprovider), */
	KOBJMETHOD_END
};

DEFINE_CLASS(sequencer, seq_methods, 0);

/* The followings are the local function. */
static int seq_convertold(u_char *event, u_char *out);

/*
 * static void seq_midiinput(struct seq_softc * scp, void *md);
 */
static void seq_reset(struct seq_softc *scp);
static int seq_sync(struct seq_softc *scp);

static int seq_processevent(struct seq_softc *scp, u_char *event);

static int seq_timing(struct seq_softc *scp, u_char *event);
static int seq_local(struct seq_softc *scp, u_char *event);

static int seq_chnvoice(struct seq_softc *scp, kobj_t md, u_char *event);
static int seq_chncommon(struct seq_softc *scp, kobj_t md, u_char *event);
static int seq_sysex(struct seq_softc *scp, kobj_t md, u_char *event);

static int seq_fetch_mid(struct seq_softc *scp, int unit, kobj_t *md);
void	seq_copytoinput(struct seq_softc *scp, u_char *event, int len);
int	seq_modevent(module_t mod, int type, void *data);
struct seq_softc *seqs[10];
static struct mtx seqinfo_mtx;
static u_long nseq = 0;

static void timer_start(struct seq_softc *t);
static void timer_stop(struct seq_softc *t);
static void timer_setvals(struct seq_softc *t, int tempo, int timerbase);
static void timer_wait(struct seq_softc *t, int ticks, int wait_abs);
static int timer_now(struct seq_softc *t);


static void
timer_start(struct seq_softc *t)
{
	t->timerrun = 1;
	getmicrotime(&t->timersub);
}

static void
timer_continue(struct seq_softc *t)
{
	struct timeval now;

	if (t->timerrun == 1)
		return;
	t->timerrun = 1;
	getmicrotime(&now);
	timevalsub(&now, &t->timerstop);
	timevaladd(&t->timersub, &now);
}

static void
timer_stop(struct seq_softc *t)
{
	t->timerrun = 0;
	getmicrotime(&t->timerstop);
}

static void
timer_setvals(struct seq_softc *t, int tempo, int timerbase)
{
	t->tempo = tempo;
	t->timerbase = timerbase;
}

static void
timer_wait(struct seq_softc *t, int ticks, int wait_abs)
{
	struct timeval now, when;
	int ret;
	unsigned long long i;

	while (t->timerrun == 0) {
		SEQ_DEBUG(2, printf("Timer wait when timer isn't running\n"));
		/*
	         * The old sequencer used timeouts that only increased
	         * the timer when the timer was running.
	         * Hence the sequencer would stick (?) if the
	         * timer was disabled.
	         */
		cv_wait(&t->reset_cv, &t->seq_lock);
		if (t->playing == 0)
			return;
	}

	i = ticks * 60ull * 1000000ull / (t->tempo * t->timerbase);

	when.tv_sec = i / 1000000;
	when.tv_usec = i % 1000000;

#if 0
	printf("timer_wait tempo %d timerbase %d ticks %d abs %d u_sec %llu\n",
	    t->tempo, t->timerbase, ticks, wait_abs, i);
#endif

	if (wait_abs != 0) {
		getmicrotime(&now);
		timevalsub(&now, &t->timersub);
		timevalsub(&when, &now);
	}
	if (when.tv_sec < 0 || when.tv_usec < 0) {
		SEQ_DEBUG(3,
		    printf("seq_timer error negative time %lds.%06lds\n",
		    (long)when.tv_sec, (long)when.tv_usec));
		return;
	}
	i = when.tv_sec * 1000000ull;
	i += when.tv_usec;
	i *= hz;
	i /= 1000000ull;
#if 0
	printf("seq_timer usec %llu ticks %llu\n",
	    when.tv_sec * 1000000ull + when.tv_usec, i);
#endif
	t->waiting = 1;
	ret = cv_timedwait(&t->reset_cv, &t->seq_lock, i + 1);
	t->waiting = 0;

	if (ret != EWOULDBLOCK)
		SEQ_DEBUG(3, printf("seq_timer didn't timeout\n"));

}

static int
timer_now(struct seq_softc *t)
{
	struct timeval now;
	unsigned long long i;
	int ret;

	if (t->timerrun == 0)
		now = t->timerstop;
	else
		getmicrotime(&now);

	timevalsub(&now, &t->timersub);

	i = now.tv_sec * 1000000ull;
	i += now.tv_usec;
	i *= t->timerbase;
/*	i /= t->tempo; */
	i /= 1000000ull;

	ret = i;
	/*
	 * printf("timer_now: %llu %d\n", i, ret);
	 */

	return ret;
}

static void
seq_eventthread(void *arg)
{
	struct seq_softc *scp = arg;
	u_char event[EV_SZ];

	mtx_lock(&scp->seq_lock);
	SEQ_DEBUG(2, printf("seq_eventthread started\n"));
	while (scp->done == 0) {
restart:
		while (scp->playing == 0) {
			cv_wait(&scp->state_cv, &scp->seq_lock);
			if (scp->done)
				goto done;
		}

		while (MIDIQ_EMPTY(scp->out_q)) {
			cv_broadcast(&scp->empty_cv);
			cv_wait(&scp->out_cv, &scp->seq_lock);
			if (scp->playing == 0)
				goto restart;
			if (scp->done)
				goto done;
		}

		MIDIQ_DEQ(scp->out_q, event, EV_SZ);

		if (MIDIQ_AVAIL(scp->out_q) < scp->out_water) {
			cv_broadcast(&scp->out_cv);
			selwakeup(&scp->out_sel);
		}
		seq_processevent(scp, event);
	}

done:
	cv_broadcast(&scp->th_cv);
	mtx_unlock(&scp->seq_lock);
	SEQ_DEBUG(2, printf("seq_eventthread finished\n"));
	kproc_exit(0);
}

/*
 * seq_processevent:  This maybe called by the event thread or the IOCTL
 * handler for queued and out of band events respectively.
 */
static int
seq_processevent(struct seq_softc *scp, u_char *event)
{
	int ret;
	kobj_t m;

	ret = 0;

	if (event[0] == EV_SEQ_LOCAL)
		ret = seq_local(scp, event);
	else if (event[0] == EV_TIMING)
		ret = seq_timing(scp, event);
	else if (event[0] != EV_CHN_VOICE &&
		    event[0] != EV_CHN_COMMON &&
		    event[0] != EV_SYSEX &&
	    event[0] != SEQ_MIDIPUTC) {
		ret = 1;
		SEQ_DEBUG(2, printf("seq_processevent not known %d\n",
		    event[0]));
	} else if (seq_fetch_mid(scp, event[1], &m) != 0) {
		ret = 1;
		SEQ_DEBUG(2, printf("seq_processevent midi unit not found %d\n",
		    event[1]));
	} else
		switch (event[0]) {
		case EV_CHN_VOICE:
			ret = seq_chnvoice(scp, m, event);
			break;
		case EV_CHN_COMMON:
			ret = seq_chncommon(scp, m, event);
			break;
		case EV_SYSEX:
			ret = seq_sysex(scp, m, event);
			break;
		case SEQ_MIDIPUTC:
			mtx_unlock(&scp->seq_lock);
			ret = SYNTH_WRITERAW(m, &event[2], 1);
			mtx_lock(&scp->seq_lock);
			break;
		}
	return ret;
}

static int
seq_addunit(void)
{
	struct seq_softc *scp;
	int ret;
	u_char *buf;

	/* Allocate the softc. */
	ret = ENOMEM;
	scp = malloc(sizeof(*scp), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (scp == NULL) {
		SEQ_DEBUG(1, printf("seq_addunit: softc allocation failed.\n"));
		goto err;
	}
	kobj_init((kobj_t)scp, &sequencer_class);

	buf = malloc(sizeof(*buf) * EV_SZ * 1024, M_TEMP, M_NOWAIT | M_ZERO);
	if (buf == NULL)
		goto err;
	MIDIQ_INIT(scp->in_q, buf, EV_SZ * 1024);
	buf = malloc(sizeof(*buf) * EV_SZ * 1024, M_TEMP, M_NOWAIT | M_ZERO);
	if (buf == NULL)
		goto err;
	MIDIQ_INIT(scp->out_q, buf, EV_SZ * 1024);
	ret = EINVAL;

	scp->midis = malloc(sizeof(kobj_t) * 32, M_TEMP, M_NOWAIT | M_ZERO);
	scp->midi_flags = malloc(sizeof(*scp->midi_flags) * 32, M_TEMP,
	    M_NOWAIT | M_ZERO);

	if (scp->midis == NULL || scp->midi_flags == NULL)
		goto err;

	scp->flags = 0;

	mtx_init(&scp->seq_lock, "seqflq", NULL, 0);
	cv_init(&scp->state_cv, "seqstate");
	cv_init(&scp->empty_cv, "seqempty");
	cv_init(&scp->reset_cv, "seqtimer");
	cv_init(&scp->out_cv, "seqqout");
	cv_init(&scp->in_cv, "seqqin");
	cv_init(&scp->th_cv, "seqstart");

	/*
	 * Init the damn timer
	 */

	scp->mapper = midimapper_addseq(scp, &scp->unit, &scp->mapper_cookie);
	if (scp->mapper == NULL)
		goto err;

	scp->seqdev = make_dev(&seq_cdevsw,
	    MIDIMKMINOR(scp->unit, SND_DEV_SEQ, 0), UID_ROOT,
	    GID_WHEEL, 0666, "sequencer%d", scp->unit);

	scp->musicdev = make_dev(&seq_cdevsw,
	    MIDIMKMINOR(scp->unit, SND_DEV_MUSIC, 0), UID_ROOT,
	    GID_WHEEL, 0666, "music%d", scp->unit);

	if (scp->seqdev == NULL || scp->musicdev == NULL)
		goto err;
	/*
	 * TODO: Add to list of sequencers this module provides
	 */

	ret =
	    kproc_create
	    (seq_eventthread, scp, NULL, RFHIGHPID, 0,
	    "sequencer %02d", scp->unit);

	if (ret)
		goto err;

	scp->seqdev->si_drv1 = scp->musicdev->si_drv1 = scp;

	SEQ_DEBUG(2, printf("sequencer %d created scp %p\n", scp->unit, scp));

	ret = 0;

	mtx_lock(&seqinfo_mtx);
	seqs[nseq++] = scp;
	mtx_unlock(&seqinfo_mtx);

	goto ok;

err:
	if (scp != NULL) {
		if (scp->seqdev != NULL)
			destroy_dev(scp->seqdev);
		if (scp->musicdev != NULL)
			destroy_dev(scp->musicdev);
		/*
	         * TODO: Destroy mutex and cv
	         */
		if (scp->midis != NULL)
			free(scp->midis, M_TEMP);
		if (scp->midi_flags != NULL)
			free(scp->midi_flags, M_TEMP);
		if (scp->out_q.b)
			free(scp->out_q.b, M_TEMP);
		if (scp->in_q.b)
			free(scp->in_q.b, M_TEMP);
		free(scp, M_DEVBUF);
	}
ok:
	return ret;
}

static int
seq_delunit(int unit)
{
	struct seq_softc *scp = seqs[unit];
	int i;

	//SEQ_DEBUG(4, printf("seq_delunit: %d\n", unit));
	SEQ_DEBUG(1, printf("seq_delunit: 1 \n"));
	mtx_lock(&scp->seq_lock);

	scp->playing = 0;
	scp->done = 1;
	cv_broadcast(&scp->out_cv);
	cv_broadcast(&scp->state_cv);
	cv_broadcast(&scp->reset_cv);
	SEQ_DEBUG(1, printf("seq_delunit: 2 \n"));
	cv_wait(&scp->th_cv, &scp->seq_lock);
	SEQ_DEBUG(1, printf("seq_delunit: 3.0 \n"));
	mtx_unlock(&scp->seq_lock);
	SEQ_DEBUG(1, printf("seq_delunit: 3.1 \n"));

	cv_destroy(&scp->state_cv);
	SEQ_DEBUG(1, printf("seq_delunit: 4 \n"));
	cv_destroy(&scp->empty_cv);
	SEQ_DEBUG(1, printf("seq_delunit: 5 \n"));
	cv_destroy(&scp->reset_cv);
	SEQ_DEBUG(1, printf("seq_delunit: 6 \n"));
	cv_destroy(&scp->out_cv);
	SEQ_DEBUG(1, printf("seq_delunit: 7 \n"));
	cv_destroy(&scp->in_cv);
	SEQ_DEBUG(1, printf("seq_delunit: 8 \n"));
	cv_destroy(&scp->th_cv);

	SEQ_DEBUG(1, printf("seq_delunit: 10 \n"));
	if (scp->seqdev)
		destroy_dev(scp->seqdev);
	SEQ_DEBUG(1, printf("seq_delunit: 11 \n"));
	if (scp->musicdev)
		destroy_dev(scp->musicdev);
	SEQ_DEBUG(1, printf("seq_delunit: 12 \n"));
	scp->seqdev = scp->musicdev = NULL;
	if (scp->midis != NULL)
		free(scp->midis, M_TEMP);
	SEQ_DEBUG(1, printf("seq_delunit: 13 \n"));
	if (scp->midi_flags != NULL)
		free(scp->midi_flags, M_TEMP);
	SEQ_DEBUG(1, printf("seq_delunit: 14 \n"));
	free(scp->out_q.b, M_TEMP);
	SEQ_DEBUG(1, printf("seq_delunit: 15 \n"));
	free(scp->in_q.b, M_TEMP);

	SEQ_DEBUG(1, printf("seq_delunit: 16 \n"));

	mtx_destroy(&scp->seq_lock);
	SEQ_DEBUG(1, printf("seq_delunit: 17 \n"));
	free(scp, M_DEVBUF);

	mtx_lock(&seqinfo_mtx);
	for (i = unit; i < (nseq - 1); i++)
		seqs[i] = seqs[i + 1];
	nseq--;
	mtx_unlock(&seqinfo_mtx);

	return 0;
}

int
seq_modevent(module_t mod, int type, void *data)
{
	int retval, r;

	retval = 0;

	switch (type) {
	case MOD_LOAD:
		mtx_init(&seqinfo_mtx, "seqmod", NULL, 0);
		retval = seq_addunit();
		break;

	case MOD_UNLOAD:
		while (nseq) {
			r = seq_delunit(nseq - 1);
			if (r) {
				retval = r;
				break;
			}
		}
		if (nseq == 0) {
			retval = 0;
			mtx_destroy(&seqinfo_mtx);
		}
		break;

	default:
		break;
	}

	return retval;
}

static int
seq_fetch_mid(struct seq_softc *scp, int unit, kobj_t *md)
{

	if (unit >= scp->midi_number || unit < 0)
		return EINVAL;

	*md = scp->midis[unit];

	return 0;
}

int
mseq_open(struct cdev *i_dev, int flags, int mode, struct thread *td)
{
	struct seq_softc *scp = i_dev->si_drv1;
	int i;

	if (scp == NULL)
		return ENXIO;

	SEQ_DEBUG(3, printf("seq_open: scp %p unit %d, flags 0x%x.\n",
	    scp, scp->unit, flags));

	/*
	 * Mark this device busy.
	 */

	mtx_lock(&scp->seq_lock);
	if (scp->busy) {
		mtx_unlock(&scp->seq_lock);
		SEQ_DEBUG(2, printf("seq_open: unit %d is busy.\n", scp->unit));
		return EBUSY;
	}
	scp->fflags = flags;
	/*
	if ((scp->fflags & O_NONBLOCK) != 0)
		scp->flags |= SEQ_F_NBIO;
		*/
	scp->music = MIDIDEV(i_dev) == SND_DEV_MUSIC;

	/*
	 * Enumerate the available midi devices
	 */
	scp->midi_number = 0;
	scp->maxunits = midimapper_open(scp->mapper, &scp->mapper_cookie);

	if (scp->maxunits == 0)
		SEQ_DEBUG(2, printf("seq_open: no midi devices\n"));

	for (i = 0; i < scp->maxunits; i++) {
		scp->midis[scp->midi_number] =
		    midimapper_fetch_synth(scp->mapper, scp->mapper_cookie, i);
		if (scp->midis[scp->midi_number]) {
			if (SYNTH_OPEN(scp->midis[scp->midi_number], scp,
				scp->fflags) != 0)
				scp->midis[scp->midi_number] = NULL;
			else {
				scp->midi_flags[scp->midi_number] =
				    SYNTH_QUERY(scp->midis[scp->midi_number]);
				scp->midi_number++;
			}
		}
	}

	timer_setvals(scp, 60, 100);

	timer_start(scp);
	timer_stop(scp);
	/*
	 * actually, if we're in rdonly mode, we should start the timer
	 */
	/*
	 * TODO: Handle recording now
	 */

	scp->out_water = MIDIQ_SIZE(scp->out_q) / 2;

	scp->busy = 1;
	mtx_unlock(&scp->seq_lock);

	SEQ_DEBUG(2, printf("seq_open: opened, mode %s.\n",
	    scp->music ? "music" : "sequencer"));
	SEQ_DEBUG(2,
	    printf("Sequencer %d %p opened maxunits %d midi_number %d:\n",
		scp->unit, scp, scp->maxunits, scp->midi_number));
	for (i = 0; i < scp->midi_number; i++)
		SEQ_DEBUG(3, printf("  midi %d %p\n", i, scp->midis[i]));

	return 0;
}

/*
 * mseq_close
 */
int
mseq_close(struct cdev *i_dev, int flags, int mode, struct thread *td)
{
	int i;
	struct seq_softc *scp = i_dev->si_drv1;
	int ret;

	if (scp == NULL)
		return ENXIO;

	SEQ_DEBUG(2, printf("seq_close: unit %d.\n", scp->unit));

	mtx_lock(&scp->seq_lock);

	ret = ENXIO;
	if (scp->busy == 0)
		goto err;

	seq_reset(scp);
	seq_sync(scp);

	for (i = 0; i < scp->midi_number; i++)
		if (scp->midis[i])
			SYNTH_CLOSE(scp->midis[i]);

	midimapper_close(scp->mapper, scp->mapper_cookie);

	timer_stop(scp);

	scp->busy = 0;
	ret = 0;

err:
	SEQ_DEBUG(3, printf("seq_close: closed ret = %d.\n", ret));
	mtx_unlock(&scp->seq_lock);
	return ret;
}

int
mseq_read(struct cdev *i_dev, struct uio *uio, int ioflag)
{
	int retval, used;
	struct seq_softc *scp = i_dev->si_drv1;

#define SEQ_RSIZE 32
	u_char buf[SEQ_RSIZE];

	if (scp == NULL)
		return ENXIO;

	SEQ_DEBUG(7, printf("mseq_read: unit %d, resid %zd.\n",
	    scp->unit, uio->uio_resid));

	mtx_lock(&scp->seq_lock);
	if ((scp->fflags & FREAD) == 0) {
		SEQ_DEBUG(2, printf("mseq_read: unit %d is not for reading.\n",
		    scp->unit));
		retval = EIO;
		goto err1;
	}
	/*
	 * Begin recording.
	 */
	/*
	 * if ((scp->flags & SEQ_F_READING) == 0)
	 */
	/*
	 * TODO, start recording if not alread
	 */

	/*
	 * I think the semantics are to return as soon
	 * as possible.
	 * Second thought, it doens't seem like midimoutain
	 * expects that at all.
	 * TODO: Look up in some sort of spec
	 */

	while (uio->uio_resid > 0) {
		while (MIDIQ_EMPTY(scp->in_q)) {
			retval = EWOULDBLOCK;
			/*
			 * I wish I knew which one to care about
			 */

			if (scp->fflags & O_NONBLOCK)
				goto err1;
			if (ioflag & O_NONBLOCK)
				goto err1;

			retval = cv_wait_sig(&scp->in_cv, &scp->seq_lock);
			if (retval == EINTR)
				goto err1;
		}

		used = MIN(MIDIQ_LEN(scp->in_q), uio->uio_resid);
		used = MIN(used, SEQ_RSIZE);

		SEQ_DEBUG(8, printf("midiread: uiomove cc=%d\n", used));
		MIDIQ_DEQ(scp->in_q, buf, used);
		mtx_unlock(&scp->seq_lock);
		retval = uiomove(buf, used, uio);
		mtx_lock(&scp->seq_lock);
		if (retval)
			goto err1;
	}

	retval = 0;
err1:
	mtx_unlock(&scp->seq_lock);
	SEQ_DEBUG(6, printf("mseq_read: ret %d, resid %zd.\n",
	    retval, uio->uio_resid));

	return retval;
}

int
mseq_write(struct cdev *i_dev, struct uio *uio, int ioflag)
{
	u_char event[EV_SZ], newevent[EV_SZ], ev_code;
	struct seq_softc *scp = i_dev->si_drv1;
	int retval;
	int used;

	SEQ_DEBUG(7, printf("seq_write: unit %d, resid %zd.\n",
	    scp->unit, uio->uio_resid));

	if (scp == NULL)
		return ENXIO;

	mtx_lock(&scp->seq_lock);

	if ((scp->fflags & FWRITE) == 0) {
		SEQ_DEBUG(2, printf("seq_write: unit %d is not for writing.\n",
		    scp->unit));
		retval = EIO;
		goto err0;
	}
	while (uio->uio_resid > 0) {
		while (MIDIQ_AVAIL(scp->out_q) == 0) {
			retval = EWOULDBLOCK;
			if (scp->fflags & O_NONBLOCK)
				goto err0;
			if (ioflag & O_NONBLOCK)
				goto err0;
			SEQ_DEBUG(8, printf("seq_write cvwait\n"));

			scp->playing = 1;
			cv_broadcast(&scp->out_cv);
			cv_broadcast(&scp->state_cv);

			retval = cv_wait_sig(&scp->out_cv, &scp->seq_lock);
			/*
		         * We slept, maybe things have changed since last
		         * dying check
		         */
			if (retval == EINTR)
				goto err0;
#if 0
			/*
		         * Useless test
		         */
			if (scp != i_dev->si_drv1)
				retval = ENXIO;
#endif
		}

		used = MIN(uio->uio_resid, 4);

		SEQ_DEBUG(8, printf("seqout: resid %zd len %jd avail %jd\n",
		    uio->uio_resid, (intmax_t)MIDIQ_LEN(scp->out_q),
		    (intmax_t)MIDIQ_AVAIL(scp->out_q)));

		if (used != 4) {
			retval = ENXIO;
			goto err0;
		}
		mtx_unlock(&scp->seq_lock);
		retval = uiomove(event, used, uio);
		mtx_lock(&scp->seq_lock);
		if (retval)
			goto err0;

		ev_code = event[0];
		SEQ_DEBUG(8, printf("seq_write: unit %d, event %s.\n",
		    scp->unit, midi_cmdname(ev_code, cmdtab_seqevent)));

		/* Have a look at the event code. */
		if (ev_code == SEQ_FULLSIZE) {

			/*
			 * TODO: restore code for SEQ_FULLSIZE
			 */
#if 0
			/*
			 * A long event, these are the patches/samples for a
			 * synthesizer.
			 */
			midiunit = *(u_short *)&event[2];
			mtx_lock(&sd->seq_lock);
			ret = lookup_mididev(scp, midiunit, LOOKUP_OPEN, &md);
			mtx_unlock(&sd->seq_lock);
			if (ret != 0)
				return (ret);

			SEQ_DEBUG(printf("seq_write: loading a patch to the unit %d.\n", midiunit));

			ret = md->synth.loadpatch(md, *(short *)&event[0], buf,
			    p + 4, count, 0);
			return (ret);
#else
			/*
			 * For now, just flush the darn buffer
			 */
			SEQ_DEBUG(2,
			   printf("seq_write: SEQ_FULLSIZE flusing buffer.\n"));
			while (uio->uio_resid > 0) {
				mtx_unlock(&scp->seq_lock);
				retval = uiomove(event, MIN(EV_SZ, uio->uio_resid), uio);
				mtx_lock(&scp->seq_lock);
				if (retval)
					goto err0;

			}
			retval = 0;
			goto err0;
#endif
		}
		retval = EINVAL;
		if (ev_code >= 128) {
			int error;

			/*
			 * Some sort of an extended event. The size is eight
			 * bytes. scoop extra info.
			 */
			if (scp->music && ev_code == SEQ_EXTENDED) {
				SEQ_DEBUG(2, printf("seq_write: invalid level two event %x.\n", ev_code));
				goto err0;
			}
			mtx_unlock(&scp->seq_lock);
			if (uio->uio_resid < 4)
				error = EINVAL;
			else
				error = uiomove((caddr_t)&event[4], 4, uio);
			mtx_lock(&scp->seq_lock);
			if (error) {
				SEQ_DEBUG(2,
				   printf("seq_write: user memory mangled?\n"));
				goto err0;
			}
		} else {
			/*
			 * Size four event.
			 */
			if (scp->music) {
				SEQ_DEBUG(2, printf("seq_write: four byte event in music mode.\n"));
				goto err0;
			}
		}
		if (ev_code == SEQ_MIDIPUTC) {
			/*
			 * TODO: event[2] is unit number to receive char.
			 * Range check it.
			 */
		}
		if (scp->music) {
#ifdef not_ever_ever
			if (event[0] == EV_TIMING &&
			    (event[1] == TMR_START || event[1] == TMR_STOP)) {
				/*
			         * For now, try to make midimoutain work by
			         * forcing these events to be processed
				 * immediatly.
			         */
				seq_processevent(scp, event);
			} else
				MIDIQ_ENQ(scp->out_q, event, EV_SZ);
#else
			MIDIQ_ENQ(scp->out_q, event, EV_SZ);
#endif
		} else {
			if (seq_convertold(event, newevent) > 0)
				MIDIQ_ENQ(scp->out_q, newevent, EV_SZ);
#if 0
			else
				goto err0;
#endif
		}

	}

	scp->playing = 1;
	cv_broadcast(&scp->state_cv);
	cv_broadcast(&scp->out_cv);

	retval = 0;

err0:
	SEQ_DEBUG(6,
	    printf("seq_write done: leftover buffer length %zd retval %d\n",
	    uio->uio_resid, retval));
	mtx_unlock(&scp->seq_lock);
	return retval;
}

int
mseq_ioctl(struct cdev *i_dev, u_long cmd, caddr_t arg, int mode,
    struct thread *td)
{
	int midiunit, ret, tmp;
	struct seq_softc *scp = i_dev->si_drv1;
	struct synth_info *synthinfo;
	struct midi_info *midiinfo;
	u_char event[EV_SZ];
	u_char newevent[EV_SZ];

	kobj_t md;

	/*
	 * struct snd_size *sndsize;
	 */

	if (scp == NULL)
		return ENXIO;

	SEQ_DEBUG(6, printf("seq_ioctl: unit %d, cmd %s.\n",
	    scp->unit, midi_cmdname(cmd, cmdtab_seqioctl)));

	ret = 0;

	switch (cmd) {
	case SNDCTL_SEQ_GETTIME:
		/*
		 * ioctl needed by libtse
		 */
		mtx_lock(&scp->seq_lock);
		*(int *)arg = timer_now(scp);
		mtx_unlock(&scp->seq_lock);
		SEQ_DEBUG(6, printf("seq_ioctl: gettime %d.\n", *(int *)arg));
		ret = 0;
		break;
	case SNDCTL_TMR_METRONOME:
		/* fallthrough */
	case SNDCTL_TMR_SOURCE:
		/*
		 * Not implemented
		 */
		ret = 0;
		break;
	case SNDCTL_TMR_TEMPO:
		event[1] = TMR_TEMPO;
		event[4] = *(int *)arg & 0xFF;
		event[5] = (*(int *)arg >> 8) & 0xFF;
		event[6] = (*(int *)arg >> 16) & 0xFF;
		event[7] = (*(int *)arg >> 24) & 0xFF;
		goto timerevent;
	case SNDCTL_TMR_TIMEBASE:
		event[1] = TMR_TIMERBASE;
		event[4] = *(int *)arg & 0xFF;
		event[5] = (*(int *)arg >> 8) & 0xFF;
		event[6] = (*(int *)arg >> 16) & 0xFF;
		event[7] = (*(int *)arg >> 24) & 0xFF;
		goto timerevent;
	case SNDCTL_TMR_START:
		event[1] = TMR_START;
		goto timerevent;
	case SNDCTL_TMR_STOP:
		event[1] = TMR_STOP;
		goto timerevent;
	case SNDCTL_TMR_CONTINUE:
		event[1] = TMR_CONTINUE;
timerevent:
		event[0] = EV_TIMING;
		mtx_lock(&scp->seq_lock);
		if (!scp->music) {
			ret = EINVAL;
			mtx_unlock(&scp->seq_lock);
			break;
		}
		seq_processevent(scp, event);
		mtx_unlock(&scp->seq_lock);
		break;
	case SNDCTL_TMR_SELECT:
		SEQ_DEBUG(2,
		    printf("seq_ioctl: SNDCTL_TMR_SELECT not supported\n"));
		ret = EINVAL;
		break;
	case SNDCTL_SEQ_SYNC:
		if (mode == O_RDONLY) {
			ret = 0;
			break;
		}
		mtx_lock(&scp->seq_lock);
		ret = seq_sync(scp);
		mtx_unlock(&scp->seq_lock);
		break;
	case SNDCTL_SEQ_PANIC:
		/* fallthrough */
	case SNDCTL_SEQ_RESET:
		/*
		 * SNDCTL_SEQ_PANIC == SNDCTL_SEQ_RESET
		 */
		mtx_lock(&scp->seq_lock);
		seq_reset(scp);
		mtx_unlock(&scp->seq_lock);
		ret = 0;
		break;
	case SNDCTL_SEQ_TESTMIDI:
		mtx_lock(&scp->seq_lock);
		/*
		 * TODO: SNDCTL_SEQ_TESTMIDI now means "can I write to the
		 * device?".
		 */
		mtx_unlock(&scp->seq_lock);
		break;
#if 0
	case SNDCTL_SEQ_GETINCOUNT:
		if (mode == O_WRONLY)
			*(int *)arg = 0;
		else {
			mtx_lock(&scp->seq_lock);
			*(int *)arg = scp->in_q.rl;
			mtx_unlock(&scp->seq_lock);
			SEQ_DEBUG(printf("seq_ioctl: incount %d.\n",
			    *(int *)arg));
		}
		ret = 0;
		break;
	case SNDCTL_SEQ_GETOUTCOUNT:
		if (mode == O_RDONLY)
			*(int *)arg = 0;
		else {
			mtx_lock(&scp->seq_lock);
			*(int *)arg = scp->out_q.fl;
			mtx_unlock(&scp->seq_lock);
			SEQ_DEBUG(printf("seq_ioctl: outcount %d.\n",
			    *(int *)arg));
		}
		ret = 0;
		break;
#endif
	case SNDCTL_SEQ_CTRLRATE:
		if (*(int *)arg != 0) {
			ret = EINVAL;
			break;
		}
		mtx_lock(&scp->seq_lock);
		*(int *)arg = scp->timerbase;
		mtx_unlock(&scp->seq_lock);
		SEQ_DEBUG(3, printf("seq_ioctl: ctrlrate %d.\n", *(int *)arg));
		ret = 0;
		break;
		/*
		 * TODO: ioctl SNDCTL_SEQ_RESETSAMPLES
		 */
#if 0
	case SNDCTL_SEQ_RESETSAMPLES:
		mtx_lock(&scp->seq_lock);
		ret = lookup_mididev(scp, *(int *)arg, LOOKUP_OPEN, &md);
		mtx_unlock(&scp->seq_lock);
		if (ret != 0)
			break;
		ret = midi_ioctl(MIDIMKDEV(major(i_dev), *(int *)arg,
		    SND_DEV_MIDIN), cmd, arg, mode, td);
		break;
#endif
	case SNDCTL_SEQ_NRSYNTHS:
		mtx_lock(&scp->seq_lock);
		*(int *)arg = scp->midi_number;
		mtx_unlock(&scp->seq_lock);
		SEQ_DEBUG(3, printf("seq_ioctl: synths %d.\n", *(int *)arg));
		ret = 0;
		break;
	case SNDCTL_SEQ_NRMIDIS:
		mtx_lock(&scp->seq_lock);
		if (scp->music)
			*(int *)arg = 0;
		else {
			/*
		         * TODO: count the numbder of devices that can WRITERAW
		         */
			*(int *)arg = scp->midi_number;
		}
		mtx_unlock(&scp->seq_lock);
		SEQ_DEBUG(3, printf("seq_ioctl: midis %d.\n", *(int *)arg));
		ret = 0;
		break;
		/*
		 * TODO: ioctl SNDCTL_SYNTH_MEMAVL
		 */
#if 0
	case SNDCTL_SYNTH_MEMAVL:
		mtx_lock(&scp->seq_lock);
		ret = lookup_mididev(scp, *(int *)arg, LOOKUP_OPEN, &md);
		mtx_unlock(&scp->seq_lock);
		if (ret != 0)
			break;
		ret = midi_ioctl(MIDIMKDEV(major(i_dev), *(int *)arg,
		    SND_DEV_MIDIN), cmd, arg, mode, td);
		break;
#endif
	case SNDCTL_SEQ_OUTOFBAND:
		for (ret = 0; ret < EV_SZ; ret++)
			event[ret] = (u_char)arg[0];

		mtx_lock(&scp->seq_lock);
		if (scp->music)
			ret = seq_processevent(scp, event);
		else {
			if (seq_convertold(event, newevent) > 0)
				ret = seq_processevent(scp, newevent);
			else
				ret = EINVAL;
		}
		mtx_unlock(&scp->seq_lock);
		break;
	case SNDCTL_SYNTH_INFO:
		synthinfo = (struct synth_info *)arg;
		midiunit = synthinfo->device;
		mtx_lock(&scp->seq_lock);
		if (seq_fetch_mid(scp, midiunit, &md) == 0) {
			bzero(synthinfo, sizeof(*synthinfo));
			synthinfo->name[0] = 'f';
			synthinfo->name[1] = 'a';
			synthinfo->name[2] = 'k';
			synthinfo->name[3] = 'e';
			synthinfo->name[4] = 's';
			synthinfo->name[5] = 'y';
			synthinfo->name[6] = 'n';
			synthinfo->name[7] = 't';
			synthinfo->name[8] = 'h';
			synthinfo->device = midiunit;
			synthinfo->synth_type = SYNTH_TYPE_MIDI;
			synthinfo->capabilities = scp->midi_flags[midiunit];
			ret = 0;
		} else
			ret = EINVAL;
		mtx_unlock(&scp->seq_lock);
		break;
	case SNDCTL_MIDI_INFO:
		midiinfo = (struct midi_info *)arg;
		midiunit = midiinfo->device;
		mtx_lock(&scp->seq_lock);
		if (seq_fetch_mid(scp, midiunit, &md) == 0) {
			bzero(midiinfo, sizeof(*midiinfo));
			midiinfo->name[0] = 'f';
			midiinfo->name[1] = 'a';
			midiinfo->name[2] = 'k';
			midiinfo->name[3] = 'e';
			midiinfo->name[4] = 'm';
			midiinfo->name[5] = 'i';
			midiinfo->name[6] = 'd';
			midiinfo->name[7] = 'i';
			midiinfo->device = midiunit;
			midiinfo->capabilities = scp->midi_flags[midiunit];
			/*
		         * TODO: What devtype?
		         */
			midiinfo->dev_type = 0x01;
			ret = 0;
		} else
			ret = EINVAL;
		mtx_unlock(&scp->seq_lock);
		break;
	case SNDCTL_SEQ_THRESHOLD:
		mtx_lock(&scp->seq_lock);
		RANGE(*(int *)arg, 1, MIDIQ_SIZE(scp->out_q) - 1);
		scp->out_water = *(int *)arg;
		mtx_unlock(&scp->seq_lock);
		SEQ_DEBUG(3, printf("seq_ioctl: water %d.\n", *(int *)arg));
		ret = 0;
		break;
	case SNDCTL_MIDI_PRETIME:
		tmp = *(int *)arg;
		if (tmp < 0)
			tmp = 0;
		mtx_lock(&scp->seq_lock);
		scp->pre_event_timeout = (hz * tmp) / 10;
		*(int *)arg = scp->pre_event_timeout;
		mtx_unlock(&scp->seq_lock);
		SEQ_DEBUG(3, printf("seq_ioctl: pretime %d.\n", *(int *)arg));
		ret = 0;
		break;
	case SNDCTL_FM_4OP_ENABLE:
	case SNDCTL_PMGR_IFACE:
	case SNDCTL_PMGR_ACCESS:
		/*
		 * Patch manager and fm are ded, ded, ded.
		 */
		/* fallthrough */
	default:
		/*
		 * TODO: Consider ioctl default case.
		 * Old code used to
		 * if ((scp->fflags & O_ACCMODE) == FREAD) {
		 *	ret = EIO;
		 *	break;
		 * }
		 * Then pass on the ioctl to device 0
		 */
		SEQ_DEBUG(2,
		    printf("seq_ioctl: unsupported IOCTL %ld.\n", cmd));
		ret = EINVAL;
		break;
	}

	return ret;
}

int
mseq_poll(struct cdev *i_dev, int events, struct thread *td)
{
	int ret, lim;
	struct seq_softc *scp = i_dev->si_drv1;

	SEQ_DEBUG(3, printf("seq_poll: unit %d.\n", scp->unit));
	SEQ_DEBUG(1, printf("seq_poll: unit %d.\n", scp->unit));

	mtx_lock(&scp->seq_lock);

	ret = 0;

	/* Look up the apropriate queue and select it. */
	if ((events & (POLLOUT | POLLWRNORM)) != 0) {
		/* Start playing. */
		scp->playing = 1;
		cv_broadcast(&scp->state_cv);
		cv_broadcast(&scp->out_cv);

		lim = scp->out_water;

		if (MIDIQ_AVAIL(scp->out_q) < lim)
			/* No enough space, record select. */
			selrecord(td, &scp->out_sel);
		else
			/* We can write now. */
			ret |= events & (POLLOUT | POLLWRNORM);
	}
	if ((events & (POLLIN | POLLRDNORM)) != 0) {
		/* TODO: Start recording. */

		/* Find out the boundary. */
		lim = 1;
		if (MIDIQ_LEN(scp->in_q) < lim)
			/* No data ready, record select. */
			selrecord(td, &scp->in_sel);
		else
			/* We can read now. */
			ret |= events & (POLLIN | POLLRDNORM);
	}
	mtx_unlock(&scp->seq_lock);

	return (ret);
}

#if 0
static void
sein_qtr(void *p, void /* mididev_info */ *md)
{
	struct seq_softc *scp;

	scp = (struct seq_softc *)p;

	mtx_lock(&scp->seq_lock);

	/* Restart playing if we have the data to output. */
	if (scp->queueout_pending)
		seq_callback(scp, SEQ_CB_START | SEQ_CB_WR);
	/* Check the midi device if we are reading. */
	if ((scp->flags & SEQ_F_READING) != 0)
		seq_midiinput(scp, md);

	mtx_unlock(&scp->seq_lock);
}

#endif
/*
 * seq_convertold
 * Was the old playevent.  Use this to convert and old
 * style /dev/sequencer event to a /dev/music event
 */
static int
seq_convertold(u_char *event, u_char *out)
{
	int used;
	u_char dev, chn, note, vel;

	out[0] = out[1] = out[2] = out[3] = out[4] = out[5] = out[6] =
	    out[7] = 0;

	dev = 0;
	chn = event[1];
	note = event[2];
	vel = event[3];

	used = 0;

restart:
	/*
	 * TODO: Debug statement
	 */
	switch (event[0]) {
	case EV_TIMING:
	case EV_CHN_VOICE:
	case EV_CHN_COMMON:
	case EV_SYSEX:
	case EV_SEQ_LOCAL:
		out[0] = event[0];
		out[1] = event[1];
		out[2] = event[2];
		out[3] = event[3];
		out[4] = event[4];
		out[5] = event[5];
		out[6] = event[6];
		out[7] = event[7];
		used += 8;
		break;
	case SEQ_NOTEOFF:
		out[0] = EV_CHN_VOICE;
		out[1] = dev;
		out[2] = MIDI_NOTEOFF;
		out[3] = chn;
		out[4] = note;
		out[5] = 255;
		used += 4;
		break;

	case SEQ_NOTEON:
		out[0] = EV_CHN_VOICE;
		out[1] = dev;
		out[2] = MIDI_NOTEON;
		out[3] = chn;
		out[4] = note;
		out[5] = vel;
		used += 4;
		break;

		/*
		 * wait delay = (event[2] << 16) + (event[3] << 8) + event[4]
		 */

	case SEQ_PGMCHANGE:
		out[0] = EV_CHN_COMMON;
		out[1] = dev;
		out[2] = MIDI_PGM_CHANGE;
		out[3] = chn;
		out[4] = note;
		out[5] = vel;
		used += 4;
		break;
/*
		out[0] = EV_TIMING;
		out[1] = dev;
		out[2] = MIDI_PGM_CHANGE;
		out[3] = chn;
		out[4] = note;
		out[5] = vel;
		SEQ_DEBUG(4,printf("seq_playevent: synctimer\n"));
		break;
*/

	case SEQ_MIDIPUTC:
		SEQ_DEBUG(4,
		    printf("seq_playevent: put data 0x%02x, unit %d.\n",
		    event[1], event[2]));
		/*
		 * Pass through to the midi device.
		 * device = event[2]
		 * data = event[1]
		 */
		out[0] = SEQ_MIDIPUTC;
		out[1] = dev;
		out[2] = chn;
		used += 4;
		break;
#ifdef notyet
	case SEQ_ECHO:
		/*
		 * This isn't handled here yet because I don't know if I can
		 * just use four bytes events.  There might be consequences
		 * in the _read routing
		 */
		if (seq_copytoinput(scp, event, 4) == EAGAIN) {
			ret = QUEUEFULL;
			break;
		}
		ret = MORE;
		break;
#endif
	case SEQ_EXTENDED:
		switch (event[1]) {
		case SEQ_NOTEOFF:
		case SEQ_NOTEON:
		case SEQ_PGMCHANGE:
			event++;
			used = 4;
			goto restart;
			break;
		case SEQ_AFTERTOUCH:
			/*
			 * SYNTH_AFTERTOUCH(md, event[3], event[4])
			 */
		case SEQ_BALANCE:
			/*
			 * SYNTH_PANNING(md, event[3], (char)event[4])
			 */
		case SEQ_CONTROLLER:
			/*
			 * SYNTH_CONTROLLER(md, event[3], event[4], *(short *)&event[5])
			 */
		case SEQ_VOLMODE:
			/*
			 * SYNTH_VOLUMEMETHOD(md, event[3])
			 */
		default:
			SEQ_DEBUG(2,
			    printf("seq_convertold: SEQ_EXTENDED type %d"
			    "not handled\n", event[1]));
			break;
		}
		break;
	case SEQ_WAIT:
		out[0] = EV_TIMING;
		out[1] = TMR_WAIT_REL;
		out[4] = event[2];
		out[5] = event[3];
		out[6] = event[4];

		SEQ_DEBUG(5, printf("SEQ_WAIT %d",
		    event[2] + (event[3] << 8) + (event[4] << 24)));

		used += 4;
		break;

	case SEQ_ECHO:
	case SEQ_SYNCTIMER:
	case SEQ_PRIVATE:
	default:
		SEQ_DEBUG(2,
		  printf("seq_convertold: event type %d not handled %d %d %d\n",
		    event[0], event[1], event[2], event[3]));
		break;
	}
	return used;
}

/*
 * Writting to the sequencer buffer never blocks and drops
 * input which cannot be queued
 */
void
seq_copytoinput(struct seq_softc *scp, u_char *event, int len)
{

	mtx_assert(&scp->seq_lock, MA_OWNED);

	if (MIDIQ_AVAIL(scp->in_q) < len) {
		/*
	         * ENOROOM?  EINPUTDROPPED? ETOUGHLUCK?
	         */
		SEQ_DEBUG(2, printf("seq_copytoinput: queue full\n"));
	} else {
		MIDIQ_ENQ(scp->in_q, event, len);
		selwakeup(&scp->in_sel);
		cv_broadcast(&scp->in_cv);
	}

}

static int
seq_chnvoice(struct seq_softc *scp, kobj_t md, u_char *event)
{
	int ret, voice;
	u_char cmd, chn, note, parm;

	ret = 0;
	cmd = event[2];
	chn = event[3];
	note = event[4];
	parm = event[5];

	mtx_assert(&scp->seq_lock, MA_OWNED);

	SEQ_DEBUG(5, printf("seq_chnvoice: unit %d, dev %d, cmd %s,"
	    " chn %d, note %d, parm %d.\n", scp->unit, event[1],
	    midi_cmdname(cmd, cmdtab_seqcv), chn, note, parm));

	voice = SYNTH_ALLOC(md, chn, note);

	mtx_unlock(&scp->seq_lock);

	switch (cmd) {
	case MIDI_NOTEON:
		if (note < 128 || note == 255) {
#if 0
			if (scp->music && chn == 9) {
				/*
				 * This channel is a percussion. The note
				 * number is the patch number.
				 */
				/*
				mtx_unlock(&scp->seq_lock);
				if (SYNTH_SETINSTR(md, voice, 128 + note)
				    == EAGAIN) {
					mtx_lock(&scp->seq_lock);
					return (QUEUEFULL);
				}
				mtx_lock(&scp->seq_lock);
				*/
				note = 60;	/* Middle C. */
			}
#endif
			if (scp->music) {
				/*
				mtx_unlock(&scp->seq_lock);
				if (SYNTH_SETUPVOICE(md, voice, chn)
				    == EAGAIN) {
					mtx_lock(&scp->seq_lock);
					return (QUEUEFULL);
				}
				mtx_lock(&scp->seq_lock);
				*/
			}
			SYNTH_STARTNOTE(md, voice, note, parm);
		}
		break;
	case MIDI_NOTEOFF:
		SYNTH_KILLNOTE(md, voice, note, parm);
		break;
	case MIDI_KEY_PRESSURE:
		SYNTH_AFTERTOUCH(md, voice, parm);
		break;
	default:
		ret = 1;
		SEQ_DEBUG(2, printf("seq_chnvoice event type %d not handled\n",
		    event[1]));
		break;
	}

	mtx_lock(&scp->seq_lock);
	return ret;
}

static int
seq_chncommon(struct seq_softc *scp, kobj_t md, u_char *event)
{
	int ret;
	u_short w14;
	u_char cmd, chn, p1;

	ret = 0;
	cmd = event[2];
	chn = event[3];
	p1 = event[4];
	w14 = *(u_short *)&event[6];

	SEQ_DEBUG(5, printf("seq_chncommon: unit %d, dev %d, cmd %s, chn %d,"
	    " p1 %d, w14 %d.\n", scp->unit, event[1],
	    midi_cmdname(cmd, cmdtab_seqccmn), chn, p1, w14));
	mtx_unlock(&scp->seq_lock);
	switch (cmd) {
	case MIDI_PGM_CHANGE:
		SEQ_DEBUG(4, printf("seq_chncommon pgmchn chn %d pg %d\n",
		    chn, p1));
		SYNTH_SETINSTR(md, chn, p1);
		break;
	case MIDI_CTL_CHANGE:
		SEQ_DEBUG(4, printf("seq_chncommon ctlch chn %d pg %d %d\n",
		    chn, p1, w14));
		SYNTH_CONTROLLER(md, chn, p1, w14);
		break;
	case MIDI_PITCH_BEND:
		if (scp->music) {
			/*
		         * TODO: MIDI_PITCH_BEND
		         */
#if 0
			mtx_lock(&md->synth.vc_mtx);
			md->synth.chn_info[chn].bender_value = w14;
			if (md->midiunit >= 0) {
				/*
				 * Handle all of the notes playing on this
				 * channel.
				 */
				key = ((int)chn << 8);
				for (i = 0; i < md->synth.alloc.max_voice; i++)
					if ((md->synth.alloc.map[i] & 0xff00) == key) {
						mtx_unlock(&md->synth.vc_mtx);
						mtx_unlock(&scp->seq_lock);
						if (md->synth.bender(md, i, w14) == EAGAIN) {
							mtx_lock(&scp->seq_lock);
							return (QUEUEFULL);
						}
						mtx_lock(&scp->seq_lock);
					}
			} else {
				mtx_unlock(&md->synth.vc_mtx);
				mtx_unlock(&scp->seq_lock);
				if (md->synth.bender(md, chn, w14) == EAGAIN) {
					mtx_lock(&scp->seq_lock);
					return (QUEUEFULL);
				}
				mtx_lock(&scp->seq_lock);
			}
#endif
		} else
			SYNTH_BENDER(md, chn, w14);
		break;
	default:
		ret = 1;
		SEQ_DEBUG(2,
		    printf("seq_chncommon event type %d not handled.\n",
		    event[1]));
		break;

	}
	mtx_lock(&scp->seq_lock);
	return ret;
}

static int
seq_timing(struct seq_softc *scp, u_char *event)
{
	int param;
	int ret;

	ret = 0;
	param = event[4] + (event[5] << 8) +
	    (event[6] << 16) + (event[7] << 24);

	SEQ_DEBUG(5, printf("seq_timing: unit %d, cmd %d, param %d.\n",
	    scp->unit, event[1], param));
	switch (event[1]) {
	case TMR_WAIT_REL:
		timer_wait(scp, param, 0);
		break;
	case TMR_WAIT_ABS:
		timer_wait(scp, param, 1);
		break;
	case TMR_START:
		timer_start(scp);
		cv_broadcast(&scp->reset_cv);
		break;
	case TMR_STOP:
		timer_stop(scp);
		/*
		 * The following cv_broadcast isn't needed since we only
		 * wait for 0->1 transitions.  It probably won't hurt
		 */
		cv_broadcast(&scp->reset_cv);
		break;
	case TMR_CONTINUE:
		timer_continue(scp);
		cv_broadcast(&scp->reset_cv);
		break;
	case TMR_TEMPO:
		if (param < 8)
			param = 8;
		if (param > 360)
			param = 360;
		SEQ_DEBUG(4, printf("Timer set tempo %d\n", param));
		timer_setvals(scp, param, scp->timerbase);
		break;
	case TMR_TIMERBASE:
		if (param < 1)
			param = 1;
		if (param > 1000)
			param = 1000;
		SEQ_DEBUG(4, printf("Timer set timerbase %d\n", param));
		timer_setvals(scp, scp->tempo, param);
		break;
	case TMR_ECHO:
		/*
		 * TODO: Consider making 4-byte events for /dev/sequencer
		 * PRO: Maybe needed by legacy apps
		 * CON: soundcard.h has been warning for a while many years
		 * to expect 8 byte events.
		 */
#if 0
		if (scp->music)
			seq_copytoinput(scp, event, 8);
		else {
			param = (param << 8 | SEQ_ECHO);
			seq_copytoinput(scp, (u_char *)&param, 4);
		}
#else
		seq_copytoinput(scp, event, 8);
#endif
		break;
	default:
		SEQ_DEBUG(2, printf("seq_timing event type %d not handled.\n",
		    event[1]));
		ret = 1;
		break;
	}
	return ret;
}

static int
seq_local(struct seq_softc *scp, u_char *event)
{
	int ret;

	ret = 0;
	mtx_assert(&scp->seq_lock, MA_OWNED);

	SEQ_DEBUG(5, printf("seq_local: unit %d, cmd %d\n", scp->unit,
	    event[1]));
	switch (event[1]) {
	default:
		SEQ_DEBUG(1, printf("seq_local event type %d not handled\n",
		    event[1]));
		ret = 1;
		break;
	}
	return ret;
}

static int
seq_sysex(struct seq_softc *scp, kobj_t md, u_char *event)
{
	int i, l;

	mtx_assert(&scp->seq_lock, MA_OWNED);
	SEQ_DEBUG(5, printf("seq_sysex: unit %d device %d\n", scp->unit,
	    event[1]));
	l = 0;
	for (i = 0; i < 6 && event[i + 2] != 0xff; i++)
		l = i + 1;
	if (l > 0) {
		mtx_unlock(&scp->seq_lock);
		if (SYNTH_SENDSYSEX(md, &event[2], l) == EAGAIN) {
			mtx_lock(&scp->seq_lock);
			return 1;
		}
		mtx_lock(&scp->seq_lock);
	}
	return 0;
}

/*
 * Reset no longer closes the raw devices nor seq_sync's
 * Callers are IOCTL and seq_close
 */
static void
seq_reset(struct seq_softc *scp)
{
	int chn, i;
	kobj_t m;

	mtx_assert(&scp->seq_lock, MA_OWNED);

	SEQ_DEBUG(5, printf("seq_reset: unit %d.\n", scp->unit));

	/*
	 * Stop reading and writing.
	 */

	/* scp->recording = 0; */
	scp->playing = 0;
	cv_broadcast(&scp->state_cv);
	cv_broadcast(&scp->out_cv);
	cv_broadcast(&scp->reset_cv);

	/*
	 * For now, don't reset the timers.
	 */
	MIDIQ_CLEAR(scp->in_q);
	MIDIQ_CLEAR(scp->out_q);

	for (i = 0; i < scp->midi_number; i++) {
		m = scp->midis[i];
		mtx_unlock(&scp->seq_lock);
		SYNTH_RESET(m);
		for (chn = 0; chn < 16; chn++) {
			SYNTH_CONTROLLER(m, chn, 123, 0);
			SYNTH_CONTROLLER(m, chn, 121, 0);
			SYNTH_BENDER(m, chn, 1 << 13);
		}
		mtx_lock(&scp->seq_lock);
	}
}

/*
 * seq_sync
 * *really* flush the output queue
 * flush the event queue, then flush the synthsisers.
 * Callers are IOCTL and close
 */

#define SEQ_SYNC_TIMEOUT 8
static int
seq_sync(struct seq_softc *scp)
{
	int i, rl, sync[16], done;

	mtx_assert(&scp->seq_lock, MA_OWNED);

	SEQ_DEBUG(4, printf("seq_sync: unit %d.\n", scp->unit));

	/*
	 * Wait until output queue is empty.  Check every so often to see if
	 * the queue is moving along.  If it isn't just abort.
	 */
	while (!MIDIQ_EMPTY(scp->out_q)) {

		if (!scp->playing) {
			scp->playing = 1;
			cv_broadcast(&scp->state_cv);
			cv_broadcast(&scp->out_cv);
		}
		rl = MIDIQ_LEN(scp->out_q);

		i = cv_timedwait_sig(&scp->out_cv,
		    &scp->seq_lock, SEQ_SYNC_TIMEOUT * hz);

		if (i == EINTR || i == ERESTART) {
			if (i == EINTR) {
				/*
			         * XXX: I don't know why we stop playing
			         */
				scp->playing = 0;
				cv_broadcast(&scp->out_cv);
			}
			return i;
		}
		if (i == EWOULDBLOCK && rl == MIDIQ_LEN(scp->out_q) &&
		    scp->waiting == 0) {
			/*
			 * A queue seems to be stuck up. Give up and clear
			 * queues.
			 */
			MIDIQ_CLEAR(scp->out_q);
			scp->playing = 0;
			cv_broadcast(&scp->state_cv);
			cv_broadcast(&scp->out_cv);
			cv_broadcast(&scp->reset_cv);

			/*
			 * TODO: Consider if the raw devices need to be flushed
			 */

			SEQ_DEBUG(1, printf("seq_sync queue stuck, aborting\n"));

			return i;
		}
	}

	scp->playing = 0;
	/*
	 * Since syncing a midi device might block, unlock scp->seq_lock.
	 */

	mtx_unlock(&scp->seq_lock);
	for (i = 0; i < scp->midi_number; i++)
		sync[i] = 1;

	do {
		done = 1;
		for (i = 0; i < scp->midi_number; i++)
			if (sync[i]) {
				if (SYNTH_INSYNC(scp->midis[i]) == 0)
					sync[i] = 0;
				else
					done = 0;
			}
		if (!done)
			DELAY(5000);

	} while (!done);

	mtx_lock(&scp->seq_lock);
	return 0;
}

char   *
midi_cmdname(int cmd, midi_cmdtab *tab)
{
	while (tab->name != NULL) {
		if (cmd == tab->cmd)
			return (tab->name);
		tab++;
	}

	return ("unknown");
}
