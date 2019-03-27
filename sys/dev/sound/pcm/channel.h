/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2009 Ariff Abdullah <ariff@FreeBSD.org>
 * Portions Copyright (c) Ryan Beasley <ryan.beasley@gmail.com> - GSoC 2006
 * Copyright (c) 1999 Cameron Grant <cg@FreeBSD.org>
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

struct pcmchan_caps {
	u_int32_t minspeed, maxspeed;
	u_int32_t *fmtlist;
	u_int32_t caps;
};

struct pcmchan_matrix {
	int id;
	uint8_t channels, ext;
	struct {
		int type;
		uint32_t members;
	} map[SND_CHN_T_MAX + 1];
	uint32_t mask;
	int8_t offset[SND_CHN_T_MAX];
};

/* Forward declarations */
struct pcm_channel;
struct pcmchan_syncgroup;
struct pcmchan_syncmember;

extern struct mtx snd_pcm_syncgroups_mtx;
extern SLIST_HEAD(pcm_synclist, pcmchan_syncgroup) snd_pcm_syncgroups;

#define PCM_SG_LOCK()	    mtx_lock(&snd_pcm_syncgroups_mtx)
#define PCM_SG_TRYLOCK()    mtx_trylock(&snd_pcm_syncgroups_mtx)
#define PCM_SG_UNLOCK()	    mtx_unlock(&snd_pcm_syncgroups_mtx)
#define PCM_SG_LOCKASSERT(arg)	mtx_assert(&snd_pcm_syncgroups_mtx, arg)

/**
 * @brief Specifies an audio device sync group
 */
struct pcmchan_syncgroup {
	SLIST_ENTRY(pcmchan_syncgroup) link;
	SLIST_HEAD(, pcmchan_syncmember) members;
	int id; /**< Group identifier; set to address of group. */
};

/**
 * @brief Specifies a container for members of a sync group
 */
struct pcmchan_syncmember {
	SLIST_ENTRY(pcmchan_syncmember) link;
	struct pcmchan_syncgroup *parent; /**< group head */
	struct pcm_channel *ch;
};

#define	CHN_NAMELEN		32
#define	CHN_COMM_UNUSED		"<UNUSED>"
#define	CHN_COMM_UNKNOWN	"<UNKNOWN>"

struct pcm_channel {
	kobj_t methods;

	pid_t pid;
	int refcount;
	struct pcm_feeder *feeder;
	u_int32_t align;

	int latency;
	u_int32_t speed;
	u_int32_t format;
	u_int32_t flags;
	u_int32_t feederflags;
	u_int64_t blocks;

	int direction;
	unsigned int interrupts, xruns, feedcount;
	unsigned int timeout;
	struct snd_dbuf *bufhard, *bufsoft;
	struct snddev_info *parentsnddev;
	struct pcm_channel *parentchannel;
	void *devinfo;
	device_t dev;
	int unit;
	char name[CHN_NAMELEN];
	char comm[MAXCOMLEN + 1];
	struct mtx *lock;
	int trigger;
	/**
	 * For interrupt manipulations.
	 */
	struct cv intr_cv;
	/**
	 * Increment,decrement this around operations that temporarily yield
	 * lock.
	 */
	unsigned int inprog;
	/**
	 * Special channel operations should examine @c inprog after acquiring
	 * lock.  If zero, operations may continue.  Else, thread should
	 * wait on this cv for previous operation to finish.
	 */
	struct cv cv;
	/**
	 * Low water mark for select()/poll().
	 *
	 * This is initialized to the channel's fragment size, and will be
	 * overwritten if a new fragment size is set.  Users may alter this
	 * value directly with the @c SNDCTL_DSP_LOW_WATER ioctl.
	 */
	unsigned int lw;
	/**
	 * If part of a sync group, this will point to the syncmember
	 * container.
	 */
	struct pcmchan_syncmember *sm;
#ifdef OSSV4_EXPERIMENT
	u_int16_t lpeak, rpeak;	/**< Peak value from 0-32767. */
#endif

	struct {
		SLIST_HEAD(, pcm_channel) head;
		SLIST_ENTRY(pcm_channel) link;
		struct {
			SLIST_HEAD(, pcm_channel) head;
			SLIST_ENTRY(pcm_channel) link;
		} busy;
	} children;

	struct {
		struct {
			SLIST_ENTRY(pcm_channel) link;
			struct {
				SLIST_ENTRY(pcm_channel) link;
			} busy;
			struct {
				SLIST_ENTRY(pcm_channel) link;
			} opened;
		} pcm;
	} channels;

	struct pcmchan_matrix matrix;
  	struct pcmchan_matrix matrix_scratch;

	int volume[SND_VOL_C_MAX][SND_CHN_T_VOL_MAX];

	void *data1, *data2;
};

#define CHN_HEAD(x, y)			&(x)->y.head
#define CHN_INIT(x, y)			SLIST_INIT(CHN_HEAD(x, y))
#define CHN_LINK(y)			y.link
#define CHN_EMPTY(x, y)			SLIST_EMPTY(CHN_HEAD(x, y))
#define CHN_FIRST(x, y)			SLIST_FIRST(CHN_HEAD(x, y))

#define CHN_FOREACH(x, y, z)						\
	SLIST_FOREACH(x, CHN_HEAD(y, z), CHN_LINK(z))

#define CHN_FOREACH_SAFE(w, x, y, z)					\
	SLIST_FOREACH_SAFE(w, CHN_HEAD(x, z), CHN_LINK(z), y)

#define CHN_INSERT_HEAD(x, y, z)					\
	SLIST_INSERT_HEAD(CHN_HEAD(x, z), y, CHN_LINK(z))

#define CHN_INSERT_AFTER(x, y, z)					\
	SLIST_INSERT_AFTER(x, y, CHN_LINK(z))

#define CHN_REMOVE(x, y, z)						\
	SLIST_REMOVE(CHN_HEAD(x, z), y, pcm_channel, CHN_LINK(z))

#define CHN_INSERT_HEAD_SAFE(x, y, z)		do {			\
	struct pcm_channel *t = NULL;					\
	CHN_FOREACH(t, x, z) {						\
		if (t == y)						\
			break;						\
	} 								\
	if (t != y)							\
		CHN_INSERT_HEAD(x, y, z);				\
} while (0)

#define CHN_INSERT_AFTER_SAFE(w, x, y, z)	do {			\
	struct pcm_channel *t = NULL;					\
	CHN_FOREACH(t, w, z) {						\
		if (t == y)						\
			break;						\
	} 								\
	if (t != y)							\
		CHN_INSERT_AFTER(x, y, z);				\
} while (0)

#define CHN_REMOVE_SAFE(x, y, z)		do {			\
	struct pcm_channel *t = NULL;					\
	CHN_FOREACH(t, x, z) {						\
		if (t == y)						\
			break;						\
	} 								\
	if (t == y)							\
		CHN_REMOVE(x, y, z);					\
} while (0)

#define CHN_INSERT_SORT(w, x, y, z)		do {			\
	struct pcm_channel *t, *a = NULL;				\
	CHN_FOREACH(t, x, z) {						\
		if ((y)->unit w t->unit)				\
			a = t;						\
		else							\
			break;						\
	}								\
	if (a != NULL)							\
		CHN_INSERT_AFTER(a, y, z);				\
	else								\
		CHN_INSERT_HEAD(x, y, z);				\
} while (0)

#define CHN_INSERT_SORT_ASCEND(x, y, z)		CHN_INSERT_SORT(>, x, y, z)
#define CHN_INSERT_SORT_DESCEND(x, y, z)	CHN_INSERT_SORT(<, x, y, z)

#define CHN_UNIT(x)	(snd_unit2u((x)->unit))
#define CHN_DEV(x)	(snd_unit2d((x)->unit))
#define CHN_CHAN(x)	(snd_unit2c((x)->unit))

#define CHN_BUF_PARENT(x, y)						\
	(((x) != NULL && (x)->parentchannel != NULL &&			\
	(x)->parentchannel->bufhard != NULL) ?				\
	(x)->parentchannel->bufhard : (y))

#define CHN_BROADCAST(x)	do {					\
	if ((x)->cv_waiters != 0)					\
		cv_broadcastpri(x, PRIBIO);				\
} while (0)

#include "channel_if.h"

int chn_reinit(struct pcm_channel *c);
int chn_write(struct pcm_channel *c, struct uio *buf);
int chn_read(struct pcm_channel *c, struct uio *buf);
u_int32_t chn_start(struct pcm_channel *c, int force);
int chn_sync(struct pcm_channel *c, int threshold);
int chn_flush(struct pcm_channel *c);
int chn_poll(struct pcm_channel *c, int ev, struct thread *td);

int chn_init(struct pcm_channel *c, void *devinfo, int dir, int direction);
int chn_kill(struct pcm_channel *c);
int chn_reset(struct pcm_channel *c, u_int32_t fmt, u_int32_t spd);
int chn_setvolume(struct pcm_channel *c, int left, int right);
int chn_setvolume_multi(struct pcm_channel *c, int vc, int left, int right,
    int center);
int chn_setvolume_matrix(struct pcm_channel *c, int vc, int vt, int val);
int chn_getvolume_matrix(struct pcm_channel *c, int vc, int vt);
void chn_vpc_reset(struct pcm_channel *c, int vc, int force);
int chn_setparam(struct pcm_channel *c, uint32_t format, uint32_t speed);
int chn_setspeed(struct pcm_channel *c, uint32_t speed);
int chn_setformat(struct pcm_channel *c, uint32_t format);
int chn_setblocksize(struct pcm_channel *c, int blkcnt, int blksz);
int chn_setlatency(struct pcm_channel *c, int latency);
void chn_syncstate(struct pcm_channel *c);
int chn_trigger(struct pcm_channel *c, int go);
int chn_getptr(struct pcm_channel *c);
struct pcmchan_caps *chn_getcaps(struct pcm_channel *c);
u_int32_t chn_getformats(struct pcm_channel *c);

struct pcmchan_matrix *chn_getmatrix(struct pcm_channel *);
int chn_setmatrix(struct pcm_channel *, struct pcmchan_matrix *);

int chn_oss_getorder(struct pcm_channel *, unsigned long long *);
int chn_oss_setorder(struct pcm_channel *, unsigned long long *);
int chn_oss_getmask(struct pcm_channel *, uint32_t *);

void chn_resetbuf(struct pcm_channel *c);
void chn_intr_locked(struct pcm_channel *c);
void chn_intr(struct pcm_channel *c);
int chn_abort(struct pcm_channel *c);

int chn_notify(struct pcm_channel *c, u_int32_t flags);

int chn_getrates(struct pcm_channel *c, int **rates);
int chn_syncdestroy(struct pcm_channel *c);

#define CHN_SETVOLUME(...)		chn_setvolume_matrix(__VA_ARGS__)
#if defined(SND_DIAGNOSTIC) || defined(INVARIANTS)
#define CHN_GETVOLUME(...)		chn_getvolume_matrix(__VA_ARGS__)
#else
#define CHN_GETVOLUME(x, y, z)		((x)->volume[y][z])
#endif

#ifdef OSSV4_EXPERIMENT
int chn_getpeaks(struct pcm_channel *c, int *lpeak, int *rpeak);
#endif

#define CHN_LOCKOWNED(c)	mtx_owned((c)->lock)
#define CHN_LOCK(c)		mtx_lock((c)->lock)
#define CHN_UNLOCK(c)		mtx_unlock((c)->lock)
#define CHN_TRYLOCK(c)		mtx_trylock((c)->lock)
#define CHN_LOCKASSERT(c)	mtx_assert((c)->lock, MA_OWNED)
#define CHN_UNLOCKASSERT(c)	mtx_assert((c)->lock, MA_NOTOWNED)

int snd_fmtvalid(uint32_t fmt, uint32_t *fmtlist);

uint32_t snd_str2afmt(const char *);
uint32_t snd_afmt2str(uint32_t, char *, size_t);

#define AFMTSTR_LEN	16


extern int chn_latency;
extern int chn_latency_profile;
extern int report_soft_formats;
extern int report_soft_matrix;

#define PCMDIR_PLAY		1
#define PCMDIR_PLAY_VIRTUAL	2
#define PCMDIR_REC		-1
#define PCMDIR_REC_VIRTUAL	-2

#define PCMTRIG_START 1
#define PCMTRIG_EMLDMAWR 2
#define PCMTRIG_EMLDMARD 3
#define PCMTRIG_STOP 0
#define PCMTRIG_ABORT -1

#define PCMTRIG_COMMON(x)	((x) == PCMTRIG_START ||		\
				 (x) == PCMTRIG_STOP ||			\
				 (x) == PCMTRIG_ABORT)

#define CHN_F_CLOSING           0x00000001  /* a pending close */
#define CHN_F_ABORTING          0x00000002  /* a pending abort */
#define CHN_F_RUNNING		0x00000004  /* dma is running */
#define CHN_F_TRIGGERED		0x00000008
#define CHN_F_NOTRIGGER		0x00000010
#define CHN_F_SLEEPING		0x00000020

#define CHN_F_NBIO              0x00000040  /* do non-blocking i/o */
#define CHN_F_MMAP		0x00000080  /* has been mmap()ed */

#define CHN_F_BUSY              0x00000100  /* has been opened 	*/
#define CHN_F_DIRTY		0x00000200  /* need re-config */
#define CHN_F_DEAD		0x00000400  /* too many errors, dead, mdk */
#define CHN_F_SILENCE		0x00000800  /* silence, nil, null, yada */

#define	CHN_F_HAS_SIZE		0x00001000  /* user set block size */
#define CHN_F_HAS_VCHAN		0x00002000  /* vchan master */

#define CHN_F_VCHAN_PASSTHROUGH	0x00004000  /* digital ac3/dts passthrough */
#define CHN_F_VCHAN_ADAPTIVE	0x00008000  /* adaptive format/rate selection */
#define CHN_F_VCHAN_DYNAMIC	(CHN_F_VCHAN_PASSTHROUGH | CHN_F_VCHAN_ADAPTIVE)

#define	CHN_F_VIRTUAL		0x10000000  /* not backed by hardware */
#define CHN_F_BITPERFECT	0x20000000  /* un-cooked, Heh.. */
#define CHN_F_PASSTHROUGH	0x40000000  /* passthrough re-config */
#define CHN_F_EXCLUSIVE		0x80000000  /* exclusive access */

#define CHN_F_BITS		"\020"					\
				"\001CLOSING"				\
				"\002ABORTING"				\
				"\003RUNNING"				\
				"\004TRIGGERED"				\
				"\005NOTRIGGER"				\
				"\006SLEEPING"				\
				"\007NBIO"				\
				"\010MMAP"				\
				"\011BUSY"				\
				"\012DIRTY"				\
				"\013DEAD"				\
				"\014SILENCE"				\
				"\015HAS_SIZE"				\
				"\016HAS_VCHAN"				\
				"\017VCHAN_PASSTHROUGH"			\
				"\020VCHAN_ADAPTIVE"			\
				"\035VIRTUAL"				\
				"\036BITPERFECT"			\
				"\037PASSTHROUGH"			\
				"\040EXCLUSIVE"


#define CHN_F_RESET		(CHN_F_BUSY | CHN_F_DEAD |		\
				 CHN_F_VIRTUAL | CHN_F_HAS_VCHAN |	\
				 CHN_F_VCHAN_DYNAMIC |			\
				 CHN_F_PASSTHROUGH | CHN_F_EXCLUSIVE)

#define CHN_F_MMAP_INVALID	(CHN_F_DEAD | CHN_F_RUNNING)

					

#define CHN_N_RATE		0x00000001
#define CHN_N_FORMAT		0x00000002
#define CHN_N_VOLUME		0x00000004
#define CHN_N_BLOCKSIZE		0x00000008
#define CHN_N_TRIGGER		0x00000010

#define CHN_LATENCY_MIN		0
#define CHN_LATENCY_MAX		10
#define CHN_LATENCY_DEFAULT	5
#define CHN_POLICY_MIN		CHN_LATENCY_MIN
#define CHN_POLICY_MAX		CHN_LATENCY_MAX
#define CHN_POLICY_DEFAULT	CHN_LATENCY_DEFAULT

#define CHN_LATENCY_PROFILE_MIN		0
#define CHN_LATENCY_PROFILE_MAX		1
#define CHN_LATENCY_PROFILE_DEFAULT	CHN_LATENCY_PROFILE_MAX

#define CHN_STARTED(c)		((c)->flags & CHN_F_TRIGGERED)
#define CHN_STOPPED(c)		(!CHN_STARTED(c))
#define CHN_DIRSTR(c)		(((c)->direction == PCMDIR_PLAY) ? \
				"PCMDIR_PLAY" : "PCMDIR_REC")
#define CHN_BITPERFECT(c)	((c)->flags & CHN_F_BITPERFECT)
#define CHN_PASSTHROUGH(c)	((c)->flags & CHN_F_PASSTHROUGH)

#define CHN_TIMEOUT		5
#define CHN_TIMEOUT_MIN		1
#define CHN_TIMEOUT_MAX		10

/*
 * This should be large enough to hold all pcm data between
 * tsleeps in chn_{read,write} at the highest sample rate.
 * (which is usually 48kHz * 16bit * stereo = 192000 bytes/sec)
 */
#define CHN_2NDBUFBLKSIZE	(2 * 1024)
/* The total number of blocks per secondary bufhard. */
#define CHN_2NDBUFBLKNUM	(32)
/* The size of a whole secondary bufhard. */
#define CHN_2NDBUFMAXSIZE	(131072)

#define CHANNEL_DECLARE(name) static DEFINE_CLASS(name, name ## _methods, sizeof(struct kobj))
