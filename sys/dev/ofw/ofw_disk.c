/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2002 Benno Rice <benno@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>

#include <geom/geom.h>

#include <dev/ofw/openfirm.h>

#define	OFWD_BLOCKSIZE	512

struct ofwd_softc
{
	struct bio_queue_head ofwd_bio_queue;
	struct mtx	ofwd_queue_mtx;
	ihandle_t	ofwd_instance;
	off_t		ofwd_mediasize;
	unsigned	ofwd_sectorsize;
	unsigned	ofwd_fwheads;
	unsigned	ofwd_fwsectors;
	struct proc	*ofwd_procp;
	struct g_geom	*ofwd_gp;
	struct g_provider *ofwd_pp;
} ofwd_softc;

static g_init_t g_ofwd_init;
static g_start_t g_ofwd_start;
static g_access_t g_ofwd_access;

struct g_class g_ofwd_class = {
	.name = "OFWD",
	.version = G_VERSION,
	.init = g_ofwd_init,
	.start = g_ofwd_start,
	.access = g_ofwd_access,
};

DECLARE_GEOM_CLASS(g_ofwd_class, g_ofwd);

static int ofwd_enable = 0;
TUNABLE_INT("kern.ofw.disk", &ofwd_enable);

static int
ofwd_startio(struct ofwd_softc *sc, struct bio *bp)
{
	u_int r;

	r = OF_seek(sc->ofwd_instance, bp->bio_offset);

	switch (bp->bio_cmd) {
	case BIO_READ:
		r = OF_read(sc->ofwd_instance, (void *)bp->bio_data,
		   bp->bio_length);
		break;
	case BIO_WRITE:
		r = OF_write(sc->ofwd_instance, (void *)bp->bio_data,
		   bp->bio_length);
		break;
	}
	if (r != bp->bio_length)
		panic("ofwd: incorrect i/o count");

	bp->bio_resid = 0;
	return (0);
}

static void
ofwd_kthread(void *arg)
{
	struct ofwd_softc *sc;
	struct bio *bp;
	int error;

	sc = arg;
	curthread->td_base_pri = PRIBIO;

	for (;;) {
		mtx_lock(&sc->ofwd_queue_mtx);
		bp = bioq_takefirst(&sc->ofwd_bio_queue);
		if (!bp) {
			msleep(sc, &sc->ofwd_queue_mtx, PRIBIO | PDROP,
			    "ofwdwait", 0);
			continue;
		}
		mtx_unlock(&sc->ofwd_queue_mtx);
		if (bp->bio_cmd == BIO_GETATTR) {
			error = EOPNOTSUPP;
		} else
			error = ofwd_startio(sc, bp);

		if (error != -1) {
			bp->bio_completed = bp->bio_length;
			g_io_deliver(bp, error);
		}
	}
}

static void
g_ofwd_init(struct g_class *mp __unused)
{
	char path[128];
	char fname[32];
	phandle_t ofd;
	struct ofwd_softc *sc;
	struct g_geom *gp;
	struct g_provider *pp;
	ihandle_t ifd;
	int error;

	if (ofwd_enable == 0)
		return;

	ofd = OF_finddevice("ofwdisk");
	if (ofd == -1)
		return;

	bzero(path, 128);
	OF_package_to_path(ofd, path, 128);
	OF_getprop(ofd, "file", fname, sizeof(fname));
	printf("ofw_disk located at %s, file %s\n", path, fname);
	ifd = OF_open(path);
	if (ifd == -1) {
		printf("ofw_disk: could not create instance\n");
		return;
	}

	sc = (struct ofwd_softc *)malloc(sizeof *sc, M_DEVBUF,
	    M_WAITOK | M_ZERO);
	bioq_init(&sc->ofwd_bio_queue);
	mtx_init(&sc->ofwd_queue_mtx, "ofwd bio queue", NULL, MTX_DEF);
	sc->ofwd_instance = ifd;
	sc->ofwd_mediasize = (off_t)2 * 33554432;
	sc->ofwd_sectorsize = OFWD_BLOCKSIZE;
	sc->ofwd_fwsectors = 0;
	sc->ofwd_fwheads = 0;
	error = kproc_create(ofwd_kthread, sc, &sc->ofwd_procp, 0, 0,
	    "ofwd0");
	if (error != 0) {
		free(sc, M_DEVBUF);
		return;
	}

	gp = g_new_geomf(&g_ofwd_class, "ofwd0");
	gp->softc = sc;
	pp = g_new_providerf(gp, "ofwd0");
	pp->mediasize = sc->ofwd_mediasize;
	pp->sectorsize = sc->ofwd_sectorsize;
	sc->ofwd_gp = gp;
	sc->ofwd_pp = pp;
	g_error_provider(pp, 0);
}

static void
g_ofwd_start(struct bio *bp)
{
	struct ofwd_softc *sc;

	sc = bp->bio_to->geom->softc;
	mtx_lock(&sc->ofwd_queue_mtx);
	bioq_disksort(&sc->ofwd_bio_queue, bp);
	mtx_unlock(&sc->ofwd_queue_mtx);
	wakeup(sc);
}

static int
g_ofwd_access(struct g_provider *pp, int r, int w, int e)
{

	if (pp->geom->softc == NULL)
		return (ENXIO);
	return (0);
}
