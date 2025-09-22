/*	$OpenBSD: sndio.h,v 1.15 2024/05/24 15:10:26 ratchov Exp $	*/
/*
 * Copyright (c) 2008 Alexandre Ratchov <alex@caoua.org>
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
#ifndef SNDIO_H
#define SNDIO_H

#include <sys/types.h>

/*
 * default audio device and MIDI port
 */
#define SIO_DEVANY	"default"
#define MIO_PORTANY	"default"

/*
 * limits
 *
 * For now SIOCTL_DISPLAYMAX is 12 byte only. It nicely fits in the
 * padding of the sioctl_desc structure: this allows any binary linked
 * to the library version with no sioctl_desc->display to work with
 * this library version. Currently, any string reported by the lower
 * layers fits in the 12-byte buffer. Once larger strings start
 * being used (or the ABI changes for any other reason) increase
 * SIOCTL_DISPLAYMAX and properly pad the sioctl_desc structure.
 */
#define SIOCTL_NAMEMAX		12	/* max name length */
#define SIOCTL_DISPLAYMAX	12	/* max display string length */

/*
 * private ``handle'' structure
 */
struct sio_hdl;
struct mio_hdl;
struct sioctl_hdl;

/*
 * parameters of a full-duplex stream
 */
struct sio_par {
	unsigned int bits;	/* bits per sample */
	unsigned int bps;	/* bytes per sample */
	unsigned int sig;	/* 1 = signed, 0 = unsigned */
	unsigned int le;	/* 1 = LE, 0 = BE byte order */
	unsigned int msb;	/* 1 = MSB, 0 = LSB aligned */
	unsigned int rchan;	/* number channels for recording direction */
	unsigned int pchan;	/* number channels for playback direction */
	unsigned int rate;	/* frames per second */
	unsigned int bufsz;	/* end-to-end buffer size */
#define SIO_IGNORE	0	/* pause during xrun */
#define SIO_SYNC	1	/* resync after xrun */
#define SIO_ERROR	2	/* terminate on xrun */
	unsigned int xrun;	/* what to do on overruns/underruns */
	unsigned int round;	/* optimal bufsz divisor */
	unsigned int appbufsz;	/* minimum buffer size */
	int __pad[3];		/* for future use */
	unsigned int __magic;	/* for internal/debug purposes only */
};

/*
 * capabilities of a stream
 */
struct sio_cap {
#define SIO_NENC	8
#define SIO_NCHAN	8
#define SIO_NRATE	16
#define SIO_NCONF	4
	struct sio_enc {			/* allowed sample encodings */
		unsigned int bits;
		unsigned int bps;
		unsigned int sig;
		unsigned int le;
		unsigned int msb;
	} enc[SIO_NENC];
	unsigned int rchan[SIO_NCHAN];	/* allowed values for rchan */
	unsigned int pchan[SIO_NCHAN];	/* allowed values for pchan */
	unsigned int rate[SIO_NRATE];	/* allowed rates */
	int __pad[7];			/* for future use */
	unsigned int nconf;		/* number of elements in confs[] */
	struct sio_conf {
		unsigned int enc;	/* mask of enc[] indexes */
		unsigned int rchan;	/* mask of chan[] indexes (rec) */
		unsigned int pchan;	/* mask of chan[] indexes (play) */
		unsigned int rate;	/* mask of rate[] indexes */
	} confs[SIO_NCONF];
};

#define SIO_XSTRINGS { "ignore", "sync", "error" }

/*
 * controlled component of the device
 */
struct sioctl_node {
	char name[SIOCTL_NAMEMAX];	/* ex. "spkr" */
	int unit;			/* optional number or -1 */
};

/*
 * description of a control (index, value) pair
 */
struct sioctl_desc {
	unsigned int addr;		/* control address */
#define SIOCTL_NONE		0	/* deleted */
#define SIOCTL_NUM		2	/* integer in the 0..maxval range */
#define SIOCTL_SW		3	/* on/off switch (0 or 1) */
#define SIOCTL_VEC		4	/* number, element of vector */
#define SIOCTL_LIST		5	/* switch, element of a list */
#define SIOCTL_SEL		6	/* element of a selector */
	unsigned int type;		/* one of above */
	char func[SIOCTL_NAMEMAX];	/* function name, ex. "level" */
	char group[SIOCTL_NAMEMAX];	/* group this control belongs to */
	struct sioctl_node node0;	/* affected node */
	struct sioctl_node node1;	/* dito for SIOCTL_{VEC,LIST,SEL} */
	unsigned int maxval;		/* max value */
	char display[SIOCTL_DISPLAYMAX];	/* free-format hint */
};

/*
 * mode bitmap
 */
#define SIO_PLAY	1
#define SIO_REC		2
#define MIO_OUT		4
#define MIO_IN		8
#define SIOCTL_READ	0x100
#define SIOCTL_WRITE	0x200

/*
 * default bytes per sample for the given bits per sample
 */
#define SIO_BPS(bits) (((bits) <= 8) ? 1 : (((bits) <= 16) ? 2 : 4))

/*
 * default value of "sio_par->le" flag
 */
#if BYTE_ORDER == LITTLE_ENDIAN
#define SIO_LE_NATIVE 1
#else
#define SIO_LE_NATIVE 0
#endif

/*
 * maximum value of volume, eg. for sio_setvol()
 */
#define SIO_MAXVOL 127

#ifdef __cplusplus
extern "C" {
#endif

struct pollfd;

void sio_initpar(struct sio_par *);
struct sio_hdl *sio_open(const char *, unsigned int, int);
void sio_close(struct sio_hdl *);
int sio_setpar(struct sio_hdl *, struct sio_par *);
int sio_getpar(struct sio_hdl *, struct sio_par *);
int sio_getcap(struct sio_hdl *, struct sio_cap *);
void sio_onmove(struct sio_hdl *, void (*)(void *, int), void *);
size_t sio_write(struct sio_hdl *, const void *, size_t);
size_t sio_read(struct sio_hdl *, void *, size_t);
int sio_start(struct sio_hdl *);
int sio_stop(struct sio_hdl *);
int sio_flush(struct sio_hdl *);
int sio_nfds(struct sio_hdl *);
int sio_pollfd(struct sio_hdl *, struct pollfd *, int);
int sio_revents(struct sio_hdl *, struct pollfd *);
int sio_eof(struct sio_hdl *);
int sio_setvol(struct sio_hdl *, unsigned int);
int sio_onvol(struct sio_hdl *, void (*)(void *, unsigned int), void *);

struct mio_hdl *mio_open(const char *, unsigned int, int);
void mio_close(struct mio_hdl *);
size_t mio_write(struct mio_hdl *, const void *, size_t);
size_t mio_read(struct mio_hdl *, void *, size_t);
int mio_nfds(struct mio_hdl *);
int mio_pollfd(struct mio_hdl *, struct pollfd *, int);
int mio_revents(struct mio_hdl *, struct pollfd *);
int mio_eof(struct mio_hdl *);

struct sioctl_hdl *sioctl_open(const char *, unsigned int, int);
void sioctl_close(struct sioctl_hdl *);
int sioctl_ondesc(struct sioctl_hdl *,
    void (*)(void *, struct sioctl_desc *, int), void *);
int sioctl_onval(struct sioctl_hdl *,
    void (*)(void *, unsigned int, unsigned int), void *);
int sioctl_setval(struct sioctl_hdl *, unsigned int, unsigned int);
int sioctl_nfds(struct sioctl_hdl *);
int sioctl_pollfd(struct sioctl_hdl *, struct pollfd *, int);
int sioctl_revents(struct sioctl_hdl *, struct pollfd *);
int sioctl_eof(struct sioctl_hdl *);

int mio_rmidi_getfd(const char *, unsigned int, int);
struct mio_hdl *mio_rmidi_fdopen(int, unsigned int, int);
int sio_sun_getfd(const char *, unsigned int, int);
struct sio_hdl *sio_sun_fdopen(int, unsigned int, int);
int sioctl_sun_getfd(const char *, unsigned int, int);
struct sioctl_hdl *sioctl_sun_fdopen(int, unsigned int, int);

#ifdef __cplusplus
}
#endif

#endif /* !defined(SNDIO_H) */
