/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2009 Ariff Abdullah <ariff@FreeBSD.org>
 * Copyright (c) 2001 Cameron Grant <cg@FreeBSD.org>
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

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/pcm.h>
#include <dev/sound/version.h>
#include <sys/sx.h>

SND_DECLARE_FILE("$FreeBSD$");

#define	SS_TYPE_MODULE		0
#define	SS_TYPE_PCM		1
#define	SS_TYPE_MIDI		2
#define	SS_TYPE_SEQUENCER	3

static d_open_t sndstat_open;
static void sndstat_close(void *);
static d_read_t sndstat_read;
static d_write_t sndstat_write;

static struct cdevsw sndstat_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	sndstat_open,
	.d_read =	sndstat_read,
	.d_write =	sndstat_write,
	.d_name =	"sndstat",
	.d_flags =	D_TRACKCLOSE,
};

struct sndstat_entry {
	TAILQ_ENTRY(sndstat_entry) link;
	device_t dev;
	char *str;
	sndstat_handler handler;
	int type, unit;
};

struct sndstat_file {
	TAILQ_ENTRY(sndstat_file) entry;
	struct sbuf sbuf;
	int out_offset;
  	int in_offset;
};

static struct sx sndstat_lock;
static struct cdev *sndstat_dev;

#define	SNDSTAT_LOCK() sx_xlock(&sndstat_lock)
#define	SNDSTAT_UNLOCK() sx_xunlock(&sndstat_lock)

static TAILQ_HEAD(, sndstat_entry) sndstat_devlist = TAILQ_HEAD_INITIALIZER(sndstat_devlist);
static TAILQ_HEAD(, sndstat_file) sndstat_filelist = TAILQ_HEAD_INITIALIZER(sndstat_filelist);

int snd_verbose = 0;

static int sndstat_prepare(struct sndstat_file *);

static int
sysctl_hw_sndverbose(SYSCTL_HANDLER_ARGS)
{
	int error, verbose;

	verbose = snd_verbose;
	error = sysctl_handle_int(oidp, &verbose, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (verbose < 0 || verbose > 4)
			error = EINVAL;
		else
			snd_verbose = verbose;
	}
	return (error);
}
SYSCTL_PROC(_hw_snd, OID_AUTO, verbose, CTLTYPE_INT | CTLFLAG_RWTUN,
            0, sizeof(int), sysctl_hw_sndverbose, "I", "verbosity level");

static int
sndstat_open(struct cdev *i_dev, int flags, int mode, struct thread *td)
{
	struct sndstat_file *pf;

	pf = malloc(sizeof(*pf), M_DEVBUF, M_WAITOK | M_ZERO);

	SNDSTAT_LOCK();
	if (sbuf_new(&pf->sbuf, NULL, 4096, SBUF_AUTOEXTEND) == NULL) {
	  	SNDSTAT_UNLOCK();
		free(pf, M_DEVBUF);
		return (ENOMEM);
	}
	TAILQ_INSERT_TAIL(&sndstat_filelist, pf, entry);
	SNDSTAT_UNLOCK();

	devfs_set_cdevpriv(pf, &sndstat_close);

	return (0);
}

static void
sndstat_close(void *sndstat_file)
{
	struct sndstat_file *pf = (struct sndstat_file *)sndstat_file;

	SNDSTAT_LOCK();
	sbuf_delete(&pf->sbuf);
	TAILQ_REMOVE(&sndstat_filelist, pf, entry);
	SNDSTAT_UNLOCK();

	free(pf, M_DEVBUF);
}

static int
sndstat_read(struct cdev *i_dev, struct uio *buf, int flag)
{
	struct sndstat_file *pf;
	int err;
	int len;

	err = devfs_get_cdevpriv((void **)&pf);
	if (err != 0)
		return (err);

	/* skip zero-length reads */
	if (buf->uio_resid == 0)
		return (0);

	SNDSTAT_LOCK();
	if (pf->out_offset != 0) {
		/* don't allow both reading and writing */
		err = EINVAL;
		goto done;
	} else if (pf->in_offset == 0) {
		err = sndstat_prepare(pf);
		if (err <= 0) {
			err = ENOMEM;
			goto done;
		}
	}
	len = sbuf_len(&pf->sbuf) - pf->in_offset;
	if (len > buf->uio_resid)
		len = buf->uio_resid;
	if (len > 0)
		err = uiomove(sbuf_data(&pf->sbuf) + pf->in_offset, len, buf);
	pf->in_offset += len;
done:
	SNDSTAT_UNLOCK();
	return (err);
}

static int
sndstat_write(struct cdev *i_dev, struct uio *buf, int flag)
{
	struct sndstat_file *pf;
	uint8_t temp[64];
	int err;
	int len;

	err = devfs_get_cdevpriv((void **)&pf);
	if (err != 0)
		return (err);

	/* skip zero-length writes */
	if (buf->uio_resid == 0)
		return (0);

	/* don't allow writing more than 64Kbytes */
	if (buf->uio_resid > 65536)
		return (ENOMEM);

	SNDSTAT_LOCK();
	if (pf->in_offset != 0) {
		/* don't allow both reading and writing */
		err = EINVAL;
	} else {
		/* only remember the last write - allows for updates */
		sbuf_clear(&pf->sbuf);
		while (1) {
			len = sizeof(temp);
			if (len > buf->uio_resid)
				len = buf->uio_resid;
			if (len > 0) {
				err = uiomove(temp, len, buf);
				if (err)
					break;
			} else {
				break;
			}
			if (sbuf_bcat(&pf->sbuf, temp, len) < 0) {
				err = ENOMEM;
				break;
			}
		}
		sbuf_finish(&pf->sbuf);
		if (err == 0)
			pf->out_offset = sbuf_len(&pf->sbuf);
		else
			pf->out_offset = 0;
	}
	SNDSTAT_UNLOCK();
	return (err);
}

/************************************************************************/

int
sndstat_register(device_t dev, char *str, sndstat_handler handler)
{
	struct sndstat_entry *ent;
	struct sndstat_entry *pre;
	const char *devtype;
	int type, unit;

	if (dev) {
		unit = device_get_unit(dev);
		devtype = device_get_name(dev);
		if (!strcmp(devtype, "pcm"))
			type = SS_TYPE_PCM;
		else if (!strcmp(devtype, "midi"))
			type = SS_TYPE_MIDI;
		else if (!strcmp(devtype, "sequencer"))
			type = SS_TYPE_SEQUENCER;
		else
			return (EINVAL);
	} else {
		type = SS_TYPE_MODULE;
		unit = -1;
	}

	ent = malloc(sizeof *ent, M_DEVBUF, M_WAITOK | M_ZERO);
	ent->dev = dev;
	ent->str = str;
	ent->type = type;
	ent->unit = unit;
	ent->handler = handler;

	SNDSTAT_LOCK();
	/* sorted list insertion */
	TAILQ_FOREACH(pre, &sndstat_devlist, link) {
		if (pre->unit > unit)
			break;
		else if (pre->unit < unit)
			continue;
		if (pre->type > type)
			break;
		else if (pre->type < unit)
			continue;
	}
	if (pre == NULL) {
		TAILQ_INSERT_TAIL(&sndstat_devlist, ent, link);
	} else {
		TAILQ_INSERT_BEFORE(pre, ent, link);
	}
	SNDSTAT_UNLOCK();

	return (0);
}

int
sndstat_registerfile(char *str)
{
	return (sndstat_register(NULL, str, NULL));
}

int
sndstat_unregister(device_t dev)
{
	struct sndstat_entry *ent;
	int error = ENXIO;

	SNDSTAT_LOCK();
	TAILQ_FOREACH(ent, &sndstat_devlist, link) {
		if (ent->dev == dev) {
			TAILQ_REMOVE(&sndstat_devlist, ent, link);
			free(ent, M_DEVBUF);
			error = 0;
			break;
		}
	}
	SNDSTAT_UNLOCK();

	return (error);
}

int
sndstat_unregisterfile(char *str)
{
	struct sndstat_entry *ent;
	int error = ENXIO;

	SNDSTAT_LOCK();
	TAILQ_FOREACH(ent, &sndstat_devlist, link) {
		if (ent->dev == NULL && ent->str == str) {
			TAILQ_REMOVE(&sndstat_devlist, ent, link);
			free(ent, M_DEVBUF);
			error = 0;
			break;
		}
	}
	SNDSTAT_UNLOCK();

	return (error);
}

/************************************************************************/

static int
sndstat_prepare(struct sndstat_file *pf_self)
{
	struct sbuf *s = &pf_self->sbuf;
	struct sndstat_entry *ent;
	struct snddev_info *d;
	struct sndstat_file *pf;
    	int k;

	/* make sure buffer is reset */
	sbuf_clear(s);
	
	if (snd_verbose > 0) {
		sbuf_printf(s, "FreeBSD Audio Driver (%ubit %d/%s)\n",
		    (u_int)sizeof(intpcm32_t) << 3, SND_DRV_VERSION,
		    MACHINE_ARCH);
	}

	/* generate list of installed devices */
	k = 0;
	TAILQ_FOREACH(ent, &sndstat_devlist, link) {
		if (ent->dev == NULL)
			continue;
		d = device_get_softc(ent->dev);
		if (!PCM_REGISTERED(d))
			continue;
		if (!k++)
			sbuf_printf(s, "Installed devices:\n");
		sbuf_printf(s, "%s:", device_get_nameunit(ent->dev));
		sbuf_printf(s, " <%s>", device_get_desc(ent->dev));
		if (snd_verbose > 0)
			sbuf_printf(s, " %s", ent->str);
		if (ent->handler) {
			/* XXX Need Giant magic entry ??? */
			PCM_ACQUIRE_QUICK(d);
			ent->handler(s, ent->dev, snd_verbose);
			PCM_RELEASE_QUICK(d);
		}
		sbuf_printf(s, "\n");
	}
	if (k == 0)
		sbuf_printf(s, "No devices installed.\n");

	/* append any input from userspace */
	k = 0;
	TAILQ_FOREACH(pf, &sndstat_filelist, entry) {
		if (pf == pf_self)
			continue;
		if (pf->out_offset == 0)
			continue;
		if (!k++)
			sbuf_printf(s, "Installed devices from userspace:\n");
		sbuf_bcat(s, sbuf_data(&pf->sbuf),
		    sbuf_len(&pf->sbuf));
	}
	if (k == 0)
		sbuf_printf(s, "No devices installed from userspace.\n");

	/* append any file versions */
	if (snd_verbose >= 3) {
		k = 0;
		TAILQ_FOREACH(ent, &sndstat_devlist, link) {
			if (ent->dev == NULL && ent->str != NULL) {
				if (!k++)
					sbuf_printf(s, "\nFile Versions:\n");
				sbuf_printf(s, "%s\n", ent->str);
			}
		}
		if (k == 0)
			sbuf_printf(s, "\nNo file versions.\n");
	}
	sbuf_finish(s);
    	return (sbuf_len(s));
}

static void
sndstat_sysinit(void *p)
{
	sx_init(&sndstat_lock, "sndstat lock");
	sndstat_dev = make_dev(&sndstat_cdevsw, SND_DEV_STATUS,
	    UID_ROOT, GID_WHEEL, 0644, "sndstat");
}
SYSINIT(sndstat_sysinit, SI_SUB_DRIVERS, SI_ORDER_FIRST, sndstat_sysinit, NULL);

static void
sndstat_sysuninit(void *p)
{
	if (sndstat_dev != NULL) {
		/* destroy_dev() will wait for all references to go away */
		destroy_dev(sndstat_dev);
	}
	sx_destroy(&sndstat_lock);
}
SYSUNINIT(sndstat_sysuninit, SI_SUB_DRIVERS, SI_ORDER_FIRST, sndstat_sysuninit, NULL);
