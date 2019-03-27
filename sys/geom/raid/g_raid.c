/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Alexander Motin <mav@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/eventhandler.h>
#include <vm/uma.h>
#include <geom/geom.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/sched.h>
#include <geom/raid/g_raid.h>
#include "g_raid_md_if.h"
#include "g_raid_tr_if.h"

static MALLOC_DEFINE(M_RAID, "raid_data", "GEOM_RAID Data");

SYSCTL_DECL(_kern_geom);
SYSCTL_NODE(_kern_geom, OID_AUTO, raid, CTLFLAG_RW, 0, "GEOM_RAID stuff");
int g_raid_enable = 1;
SYSCTL_INT(_kern_geom_raid, OID_AUTO, enable, CTLFLAG_RWTUN,
    &g_raid_enable, 0, "Enable on-disk metadata taste");
u_int g_raid_aggressive_spare = 0;
SYSCTL_UINT(_kern_geom_raid, OID_AUTO, aggressive_spare, CTLFLAG_RWTUN,
    &g_raid_aggressive_spare, 0, "Use disks without metadata as spare");
u_int g_raid_debug = 0;
SYSCTL_UINT(_kern_geom_raid, OID_AUTO, debug, CTLFLAG_RWTUN, &g_raid_debug, 0,
    "Debug level");
int g_raid_read_err_thresh = 10;
SYSCTL_UINT(_kern_geom_raid, OID_AUTO, read_err_thresh, CTLFLAG_RWTUN,
    &g_raid_read_err_thresh, 0,
    "Number of read errors equated to disk failure");
u_int g_raid_start_timeout = 30;
SYSCTL_UINT(_kern_geom_raid, OID_AUTO, start_timeout, CTLFLAG_RWTUN,
    &g_raid_start_timeout, 0,
    "Time to wait for all array components");
static u_int g_raid_clean_time = 5;
SYSCTL_UINT(_kern_geom_raid, OID_AUTO, clean_time, CTLFLAG_RWTUN,
    &g_raid_clean_time, 0, "Mark volume as clean when idling");
static u_int g_raid_disconnect_on_failure = 1;
SYSCTL_UINT(_kern_geom_raid, OID_AUTO, disconnect_on_failure, CTLFLAG_RWTUN,
    &g_raid_disconnect_on_failure, 0, "Disconnect component on I/O failure.");
static u_int g_raid_name_format = 0;
SYSCTL_UINT(_kern_geom_raid, OID_AUTO, name_format, CTLFLAG_RWTUN,
    &g_raid_name_format, 0, "Providers name format.");
static u_int g_raid_idle_threshold = 1000000;
SYSCTL_UINT(_kern_geom_raid, OID_AUTO, idle_threshold, CTLFLAG_RWTUN,
    &g_raid_idle_threshold, 1000000,
    "Time in microseconds to consider a volume idle.");

#define	MSLEEP(rv, ident, mtx, priority, wmesg, timeout)	do {	\
	G_RAID_DEBUG(4, "%s: Sleeping %p.", __func__, (ident));		\
	rv = msleep((ident), (mtx), (priority), (wmesg), (timeout));	\
	G_RAID_DEBUG(4, "%s: Woken up %p.", __func__, (ident));		\
} while (0)

LIST_HEAD(, g_raid_md_class) g_raid_md_classes =
    LIST_HEAD_INITIALIZER(g_raid_md_classes);

LIST_HEAD(, g_raid_tr_class) g_raid_tr_classes =
    LIST_HEAD_INITIALIZER(g_raid_tr_classes);

LIST_HEAD(, g_raid_volume) g_raid_volumes =
    LIST_HEAD_INITIALIZER(g_raid_volumes);

static eventhandler_tag g_raid_post_sync = NULL;
static int g_raid_started = 0;
static int g_raid_shutdown = 0;

static int g_raid_destroy_geom(struct gctl_req *req, struct g_class *mp,
    struct g_geom *gp);
static g_taste_t g_raid_taste;
static void g_raid_init(struct g_class *mp);
static void g_raid_fini(struct g_class *mp);

struct g_class g_raid_class = {
	.name = G_RAID_CLASS_NAME,
	.version = G_VERSION,
	.ctlreq = g_raid_ctl,
	.taste = g_raid_taste,
	.destroy_geom = g_raid_destroy_geom,
	.init = g_raid_init,
	.fini = g_raid_fini
};

static void g_raid_destroy_provider(struct g_raid_volume *vol);
static int g_raid_update_disk(struct g_raid_disk *disk, u_int event);
static int g_raid_update_subdisk(struct g_raid_subdisk *subdisk, u_int event);
static int g_raid_update_volume(struct g_raid_volume *vol, u_int event);
static int g_raid_update_node(struct g_raid_softc *sc, u_int event);
static void g_raid_dumpconf(struct sbuf *sb, const char *indent,
    struct g_geom *gp, struct g_consumer *cp, struct g_provider *pp);
static void g_raid_start(struct bio *bp);
static void g_raid_start_request(struct bio *bp);
static void g_raid_disk_done(struct bio *bp);
static void g_raid_poll(struct g_raid_softc *sc);

static const char *
g_raid_node_event2str(int event)
{

	switch (event) {
	case G_RAID_NODE_E_WAKE:
		return ("WAKE");
	case G_RAID_NODE_E_START:
		return ("START");
	default:
		return ("INVALID");
	}
}

const char *
g_raid_disk_state2str(int state)
{

	switch (state) {
	case G_RAID_DISK_S_NONE:
		return ("NONE");
	case G_RAID_DISK_S_OFFLINE:
		return ("OFFLINE");
	case G_RAID_DISK_S_DISABLED:
		return ("DISABLED");
	case G_RAID_DISK_S_FAILED:
		return ("FAILED");
	case G_RAID_DISK_S_STALE_FAILED:
		return ("STALE_FAILED");
	case G_RAID_DISK_S_SPARE:
		return ("SPARE");
	case G_RAID_DISK_S_STALE:
		return ("STALE");
	case G_RAID_DISK_S_ACTIVE:
		return ("ACTIVE");
	default:
		return ("INVALID");
	}
}

static const char *
g_raid_disk_event2str(int event)
{

	switch (event) {
	case G_RAID_DISK_E_DISCONNECTED:
		return ("DISCONNECTED");
	default:
		return ("INVALID");
	}
}

const char *
g_raid_subdisk_state2str(int state)
{

	switch (state) {
	case G_RAID_SUBDISK_S_NONE:
		return ("NONE");
	case G_RAID_SUBDISK_S_FAILED:
		return ("FAILED");
	case G_RAID_SUBDISK_S_NEW:
		return ("NEW");
	case G_RAID_SUBDISK_S_REBUILD:
		return ("REBUILD");
	case G_RAID_SUBDISK_S_UNINITIALIZED:
		return ("UNINITIALIZED");
	case G_RAID_SUBDISK_S_STALE:
		return ("STALE");
	case G_RAID_SUBDISK_S_RESYNC:
		return ("RESYNC");
	case G_RAID_SUBDISK_S_ACTIVE:
		return ("ACTIVE");
	default:
		return ("INVALID");
	}
}

static const char *
g_raid_subdisk_event2str(int event)
{

	switch (event) {
	case G_RAID_SUBDISK_E_NEW:
		return ("NEW");
	case G_RAID_SUBDISK_E_FAILED:
		return ("FAILED");
	case G_RAID_SUBDISK_E_DISCONNECTED:
		return ("DISCONNECTED");
	default:
		return ("INVALID");
	}
}

const char *
g_raid_volume_state2str(int state)
{

	switch (state) {
	case G_RAID_VOLUME_S_STARTING:
		return ("STARTING");
	case G_RAID_VOLUME_S_BROKEN:
		return ("BROKEN");
	case G_RAID_VOLUME_S_DEGRADED:
		return ("DEGRADED");
	case G_RAID_VOLUME_S_SUBOPTIMAL:
		return ("SUBOPTIMAL");
	case G_RAID_VOLUME_S_OPTIMAL:
		return ("OPTIMAL");
	case G_RAID_VOLUME_S_UNSUPPORTED:
		return ("UNSUPPORTED");
	case G_RAID_VOLUME_S_STOPPED:
		return ("STOPPED");
	default:
		return ("INVALID");
	}
}

static const char *
g_raid_volume_event2str(int event)
{

	switch (event) {
	case G_RAID_VOLUME_E_UP:
		return ("UP");
	case G_RAID_VOLUME_E_DOWN:
		return ("DOWN");
	case G_RAID_VOLUME_E_START:
		return ("START");
	case G_RAID_VOLUME_E_STARTMD:
		return ("STARTMD");
	default:
		return ("INVALID");
	}
}

const char *
g_raid_volume_level2str(int level, int qual)
{

	switch (level) {
	case G_RAID_VOLUME_RL_RAID0:
		return ("RAID0");
	case G_RAID_VOLUME_RL_RAID1:
		return ("RAID1");
	case G_RAID_VOLUME_RL_RAID3:
		if (qual == G_RAID_VOLUME_RLQ_R3P0)
			return ("RAID3-P0");
		if (qual == G_RAID_VOLUME_RLQ_R3PN)
			return ("RAID3-PN");
		return ("RAID3");
	case G_RAID_VOLUME_RL_RAID4:
		if (qual == G_RAID_VOLUME_RLQ_R4P0)
			return ("RAID4-P0");
		if (qual == G_RAID_VOLUME_RLQ_R4PN)
			return ("RAID4-PN");
		return ("RAID4");
	case G_RAID_VOLUME_RL_RAID5:
		if (qual == G_RAID_VOLUME_RLQ_R5RA)
			return ("RAID5-RA");
		if (qual == G_RAID_VOLUME_RLQ_R5RS)
			return ("RAID5-RS");
		if (qual == G_RAID_VOLUME_RLQ_R5LA)
			return ("RAID5-LA");
		if (qual == G_RAID_VOLUME_RLQ_R5LS)
			return ("RAID5-LS");
		return ("RAID5");
	case G_RAID_VOLUME_RL_RAID6:
		if (qual == G_RAID_VOLUME_RLQ_R6RA)
			return ("RAID6-RA");
		if (qual == G_RAID_VOLUME_RLQ_R6RS)
			return ("RAID6-RS");
		if (qual == G_RAID_VOLUME_RLQ_R6LA)
			return ("RAID6-LA");
		if (qual == G_RAID_VOLUME_RLQ_R6LS)
			return ("RAID6-LS");
		return ("RAID6");
	case G_RAID_VOLUME_RL_RAIDMDF:
		if (qual == G_RAID_VOLUME_RLQ_RMDFRA)
			return ("RAIDMDF-RA");
		if (qual == G_RAID_VOLUME_RLQ_RMDFRS)
			return ("RAIDMDF-RS");
		if (qual == G_RAID_VOLUME_RLQ_RMDFLA)
			return ("RAIDMDF-LA");
		if (qual == G_RAID_VOLUME_RLQ_RMDFLS)
			return ("RAIDMDF-LS");
		return ("RAIDMDF");
	case G_RAID_VOLUME_RL_RAID1E:
		if (qual == G_RAID_VOLUME_RLQ_R1EA)
			return ("RAID1E-A");
		if (qual == G_RAID_VOLUME_RLQ_R1EO)
			return ("RAID1E-O");
		return ("RAID1E");
	case G_RAID_VOLUME_RL_SINGLE:
		return ("SINGLE");
	case G_RAID_VOLUME_RL_CONCAT:
		return ("CONCAT");
	case G_RAID_VOLUME_RL_RAID5E:
		if (qual == G_RAID_VOLUME_RLQ_R5ERA)
			return ("RAID5E-RA");
		if (qual == G_RAID_VOLUME_RLQ_R5ERS)
			return ("RAID5E-RS");
		if (qual == G_RAID_VOLUME_RLQ_R5ELA)
			return ("RAID5E-LA");
		if (qual == G_RAID_VOLUME_RLQ_R5ELS)
			return ("RAID5E-LS");
		return ("RAID5E");
	case G_RAID_VOLUME_RL_RAID5EE:
		if (qual == G_RAID_VOLUME_RLQ_R5EERA)
			return ("RAID5EE-RA");
		if (qual == G_RAID_VOLUME_RLQ_R5EERS)
			return ("RAID5EE-RS");
		if (qual == G_RAID_VOLUME_RLQ_R5EELA)
			return ("RAID5EE-LA");
		if (qual == G_RAID_VOLUME_RLQ_R5EELS)
			return ("RAID5EE-LS");
		return ("RAID5EE");
	case G_RAID_VOLUME_RL_RAID5R:
		if (qual == G_RAID_VOLUME_RLQ_R5RRA)
			return ("RAID5R-RA");
		if (qual == G_RAID_VOLUME_RLQ_R5RRS)
			return ("RAID5R-RS");
		if (qual == G_RAID_VOLUME_RLQ_R5RLA)
			return ("RAID5R-LA");
		if (qual == G_RAID_VOLUME_RLQ_R5RLS)
			return ("RAID5R-LS");
		return ("RAID5E");
	default:
		return ("UNKNOWN");
	}
}

int
g_raid_volume_str2level(const char *str, int *level, int *qual)
{

	*level = G_RAID_VOLUME_RL_UNKNOWN;
	*qual = G_RAID_VOLUME_RLQ_NONE;
	if (strcasecmp(str, "RAID0") == 0)
		*level = G_RAID_VOLUME_RL_RAID0;
	else if (strcasecmp(str, "RAID1") == 0)
		*level = G_RAID_VOLUME_RL_RAID1;
	else if (strcasecmp(str, "RAID3-P0") == 0) {
		*level = G_RAID_VOLUME_RL_RAID3;
		*qual = G_RAID_VOLUME_RLQ_R3P0;
	} else if (strcasecmp(str, "RAID3-PN") == 0 ||
		   strcasecmp(str, "RAID3") == 0) {
		*level = G_RAID_VOLUME_RL_RAID3;
		*qual = G_RAID_VOLUME_RLQ_R3PN;
	} else if (strcasecmp(str, "RAID4-P0") == 0) {
		*level = G_RAID_VOLUME_RL_RAID4;
		*qual = G_RAID_VOLUME_RLQ_R4P0;
	} else if (strcasecmp(str, "RAID4-PN") == 0 ||
		   strcasecmp(str, "RAID4") == 0) {
		*level = G_RAID_VOLUME_RL_RAID4;
		*qual = G_RAID_VOLUME_RLQ_R4PN;
	} else if (strcasecmp(str, "RAID5-RA") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5;
		*qual = G_RAID_VOLUME_RLQ_R5RA;
	} else if (strcasecmp(str, "RAID5-RS") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5;
		*qual = G_RAID_VOLUME_RLQ_R5RS;
	} else if (strcasecmp(str, "RAID5") == 0 ||
		   strcasecmp(str, "RAID5-LA") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5;
		*qual = G_RAID_VOLUME_RLQ_R5LA;
	} else if (strcasecmp(str, "RAID5-LS") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5;
		*qual = G_RAID_VOLUME_RLQ_R5LS;
	} else if (strcasecmp(str, "RAID6-RA") == 0) {
		*level = G_RAID_VOLUME_RL_RAID6;
		*qual = G_RAID_VOLUME_RLQ_R6RA;
	} else if (strcasecmp(str, "RAID6-RS") == 0) {
		*level = G_RAID_VOLUME_RL_RAID6;
		*qual = G_RAID_VOLUME_RLQ_R6RS;
	} else if (strcasecmp(str, "RAID6") == 0 ||
		   strcasecmp(str, "RAID6-LA") == 0) {
		*level = G_RAID_VOLUME_RL_RAID6;
		*qual = G_RAID_VOLUME_RLQ_R6LA;
	} else if (strcasecmp(str, "RAID6-LS") == 0) {
		*level = G_RAID_VOLUME_RL_RAID6;
		*qual = G_RAID_VOLUME_RLQ_R6LS;
	} else if (strcasecmp(str, "RAIDMDF-RA") == 0) {
		*level = G_RAID_VOLUME_RL_RAIDMDF;
		*qual = G_RAID_VOLUME_RLQ_RMDFRA;
	} else if (strcasecmp(str, "RAIDMDF-RS") == 0) {
		*level = G_RAID_VOLUME_RL_RAIDMDF;
		*qual = G_RAID_VOLUME_RLQ_RMDFRS;
	} else if (strcasecmp(str, "RAIDMDF") == 0 ||
		   strcasecmp(str, "RAIDMDF-LA") == 0) {
		*level = G_RAID_VOLUME_RL_RAIDMDF;
		*qual = G_RAID_VOLUME_RLQ_RMDFLA;
	} else if (strcasecmp(str, "RAIDMDF-LS") == 0) {
		*level = G_RAID_VOLUME_RL_RAIDMDF;
		*qual = G_RAID_VOLUME_RLQ_RMDFLS;
	} else if (strcasecmp(str, "RAID10") == 0 ||
		   strcasecmp(str, "RAID1E") == 0 ||
		   strcasecmp(str, "RAID1E-A") == 0) {
		*level = G_RAID_VOLUME_RL_RAID1E;
		*qual = G_RAID_VOLUME_RLQ_R1EA;
	} else if (strcasecmp(str, "RAID1E-O") == 0) {
		*level = G_RAID_VOLUME_RL_RAID1E;
		*qual = G_RAID_VOLUME_RLQ_R1EO;
	} else if (strcasecmp(str, "SINGLE") == 0)
		*level = G_RAID_VOLUME_RL_SINGLE;
	else if (strcasecmp(str, "CONCAT") == 0)
		*level = G_RAID_VOLUME_RL_CONCAT;
	else if (strcasecmp(str, "RAID5E-RA") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5E;
		*qual = G_RAID_VOLUME_RLQ_R5ERA;
	} else if (strcasecmp(str, "RAID5E-RS") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5E;
		*qual = G_RAID_VOLUME_RLQ_R5ERS;
	} else if (strcasecmp(str, "RAID5E") == 0 ||
		   strcasecmp(str, "RAID5E-LA") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5E;
		*qual = G_RAID_VOLUME_RLQ_R5ELA;
	} else if (strcasecmp(str, "RAID5E-LS") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5E;
		*qual = G_RAID_VOLUME_RLQ_R5ELS;
	} else if (strcasecmp(str, "RAID5EE-RA") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5EE;
		*qual = G_RAID_VOLUME_RLQ_R5EERA;
	} else if (strcasecmp(str, "RAID5EE-RS") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5EE;
		*qual = G_RAID_VOLUME_RLQ_R5EERS;
	} else if (strcasecmp(str, "RAID5EE") == 0 ||
		   strcasecmp(str, "RAID5EE-LA") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5EE;
		*qual = G_RAID_VOLUME_RLQ_R5EELA;
	} else if (strcasecmp(str, "RAID5EE-LS") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5EE;
		*qual = G_RAID_VOLUME_RLQ_R5EELS;
	} else if (strcasecmp(str, "RAID5R-RA") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5R;
		*qual = G_RAID_VOLUME_RLQ_R5RRA;
	} else if (strcasecmp(str, "RAID5R-RS") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5R;
		*qual = G_RAID_VOLUME_RLQ_R5RRS;
	} else if (strcasecmp(str, "RAID5R") == 0 ||
		   strcasecmp(str, "RAID5R-LA") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5R;
		*qual = G_RAID_VOLUME_RLQ_R5RLA;
	} else if (strcasecmp(str, "RAID5R-LS") == 0) {
		*level = G_RAID_VOLUME_RL_RAID5R;
		*qual = G_RAID_VOLUME_RLQ_R5RLS;
	} else
		return (-1);
	return (0);
}

const char *
g_raid_get_diskname(struct g_raid_disk *disk)
{

	if (disk->d_consumer == NULL || disk->d_consumer->provider == NULL)
		return ("[unknown]");
	return (disk->d_consumer->provider->name);
}

void
g_raid_get_disk_info(struct g_raid_disk *disk)
{
	struct g_consumer *cp = disk->d_consumer;
	int error, len;

	/* Read kernel dumping information. */
	disk->d_kd.offset = 0;
	disk->d_kd.length = OFF_MAX;
	len = sizeof(disk->d_kd);
	error = g_io_getattr("GEOM::kerneldump", cp, &len, &disk->d_kd);
	if (error)
		disk->d_kd.di.dumper = NULL;
	if (disk->d_kd.di.dumper == NULL)
		G_RAID_DEBUG1(2, disk->d_softc,
		    "Dumping not supported by %s: %d.", 
		    cp->provider->name, error);

	/* Read BIO_DELETE support. */
	error = g_getattr("GEOM::candelete", cp, &disk->d_candelete);
	if (error)
		disk->d_candelete = 0;
	if (!disk->d_candelete)
		G_RAID_DEBUG1(2, disk->d_softc,
		    "BIO_DELETE not supported by %s: %d.", 
		    cp->provider->name, error);
}

void
g_raid_report_disk_state(struct g_raid_disk *disk)
{
	struct g_raid_subdisk *sd;
	int len, state;
	uint32_t s;

	if (disk->d_consumer == NULL)
		return;
	if (disk->d_state == G_RAID_DISK_S_DISABLED) {
		s = G_STATE_ACTIVE; /* XXX */
	} else if (disk->d_state == G_RAID_DISK_S_FAILED ||
	    disk->d_state == G_RAID_DISK_S_STALE_FAILED) {
		s = G_STATE_FAILED;
	} else {
		state = G_RAID_SUBDISK_S_ACTIVE;
		TAILQ_FOREACH(sd, &disk->d_subdisks, sd_next) {
			if (sd->sd_state < state)
				state = sd->sd_state;
		}
		if (state == G_RAID_SUBDISK_S_FAILED)
			s = G_STATE_FAILED;
		else if (state == G_RAID_SUBDISK_S_NEW ||
		    state == G_RAID_SUBDISK_S_REBUILD)
			s = G_STATE_REBUILD;
		else if (state == G_RAID_SUBDISK_S_STALE ||
		    state == G_RAID_SUBDISK_S_RESYNC)
			s = G_STATE_RESYNC;
		else
			s = G_STATE_ACTIVE;
	}
	len = sizeof(s);
	g_io_getattr("GEOM::setstate", disk->d_consumer, &len, &s);
	G_RAID_DEBUG1(2, disk->d_softc, "Disk %s state reported as %d.",
	    g_raid_get_diskname(disk), s);
}

void
g_raid_change_disk_state(struct g_raid_disk *disk, int state)
{

	G_RAID_DEBUG1(0, disk->d_softc, "Disk %s state changed from %s to %s.",
	    g_raid_get_diskname(disk),
	    g_raid_disk_state2str(disk->d_state),
	    g_raid_disk_state2str(state));
	disk->d_state = state;
	g_raid_report_disk_state(disk);
}

void
g_raid_change_subdisk_state(struct g_raid_subdisk *sd, int state)
{

	G_RAID_DEBUG1(0, sd->sd_softc,
	    "Subdisk %s:%d-%s state changed from %s to %s.",
	    sd->sd_volume->v_name, sd->sd_pos,
	    sd->sd_disk ? g_raid_get_diskname(sd->sd_disk) : "[none]",
	    g_raid_subdisk_state2str(sd->sd_state),
	    g_raid_subdisk_state2str(state));
	sd->sd_state = state;
	if (sd->sd_disk)
		g_raid_report_disk_state(sd->sd_disk);
}

void
g_raid_change_volume_state(struct g_raid_volume *vol, int state)
{

	G_RAID_DEBUG1(0, vol->v_softc,
	    "Volume %s state changed from %s to %s.",
	    vol->v_name,
	    g_raid_volume_state2str(vol->v_state),
	    g_raid_volume_state2str(state));
	vol->v_state = state;
}

/*
 * --- Events handling functions ---
 * Events in geom_raid are used to maintain subdisks and volumes status
 * from one thread to simplify locking.
 */
static void
g_raid_event_free(struct g_raid_event *ep)
{

	free(ep, M_RAID);
}

int
g_raid_event_send(void *arg, int event, int flags)
{
	struct g_raid_softc *sc;
	struct g_raid_event *ep;
	int error;

	if ((flags & G_RAID_EVENT_VOLUME) != 0) {
		sc = ((struct g_raid_volume *)arg)->v_softc;
	} else if ((flags & G_RAID_EVENT_DISK) != 0) {
		sc = ((struct g_raid_disk *)arg)->d_softc;
	} else if ((flags & G_RAID_EVENT_SUBDISK) != 0) {
		sc = ((struct g_raid_subdisk *)arg)->sd_softc;
	} else {
		sc = arg;
	}
	ep = malloc(sizeof(*ep), M_RAID,
	    sx_xlocked(&sc->sc_lock) ? M_WAITOK : M_NOWAIT);
	if (ep == NULL)
		return (ENOMEM);
	ep->e_tgt = arg;
	ep->e_event = event;
	ep->e_flags = flags;
	ep->e_error = 0;
	G_RAID_DEBUG1(4, sc, "Sending event %p. Waking up %p.", ep, sc);
	mtx_lock(&sc->sc_queue_mtx);
	TAILQ_INSERT_TAIL(&sc->sc_events, ep, e_next);
	mtx_unlock(&sc->sc_queue_mtx);
	wakeup(sc);

	if ((flags & G_RAID_EVENT_WAIT) == 0)
		return (0);

	sx_assert(&sc->sc_lock, SX_XLOCKED);
	G_RAID_DEBUG1(4, sc, "Sleeping on %p.", ep);
	sx_xunlock(&sc->sc_lock);
	while ((ep->e_flags & G_RAID_EVENT_DONE) == 0) {
		mtx_lock(&sc->sc_queue_mtx);
		MSLEEP(error, ep, &sc->sc_queue_mtx, PRIBIO | PDROP, "m:event",
		    hz * 5);
	}
	error = ep->e_error;
	g_raid_event_free(ep);
	sx_xlock(&sc->sc_lock);
	return (error);
}

static void
g_raid_event_cancel(struct g_raid_softc *sc, void *tgt)
{
	struct g_raid_event *ep, *tmpep;

	sx_assert(&sc->sc_lock, SX_XLOCKED);

	mtx_lock(&sc->sc_queue_mtx);
	TAILQ_FOREACH_SAFE(ep, &sc->sc_events, e_next, tmpep) {
		if (ep->e_tgt != tgt)
			continue;
		TAILQ_REMOVE(&sc->sc_events, ep, e_next);
		if ((ep->e_flags & G_RAID_EVENT_WAIT) == 0)
			g_raid_event_free(ep);
		else {
			ep->e_error = ECANCELED;
			wakeup(ep);
		}
	}
	mtx_unlock(&sc->sc_queue_mtx);
}

static int
g_raid_event_check(struct g_raid_softc *sc, void *tgt)
{
	struct g_raid_event *ep;
	int	res = 0;

	sx_assert(&sc->sc_lock, SX_XLOCKED);

	mtx_lock(&sc->sc_queue_mtx);
	TAILQ_FOREACH(ep, &sc->sc_events, e_next) {
		if (ep->e_tgt != tgt)
			continue;
		res = 1;
		break;
	}
	mtx_unlock(&sc->sc_queue_mtx);
	return (res);
}

/*
 * Return the number of disks in given state.
 * If state is equal to -1, count all connected disks.
 */
u_int
g_raid_ndisks(struct g_raid_softc *sc, int state)
{
	struct g_raid_disk *disk;
	u_int n;

	sx_assert(&sc->sc_lock, SX_LOCKED);

	n = 0;
	TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
		if (disk->d_state == state || state == -1)
			n++;
	}
	return (n);
}

/*
 * Return the number of subdisks in given state.
 * If state is equal to -1, count all connected disks.
 */
u_int
g_raid_nsubdisks(struct g_raid_volume *vol, int state)
{
	struct g_raid_subdisk *subdisk;
	struct g_raid_softc *sc;
	u_int i, n ;

	sc = vol->v_softc;
	sx_assert(&sc->sc_lock, SX_LOCKED);

	n = 0;
	for (i = 0; i < vol->v_disks_count; i++) {
		subdisk = &vol->v_subdisks[i];
		if ((state == -1 &&
		     subdisk->sd_state != G_RAID_SUBDISK_S_NONE) ||
		    subdisk->sd_state == state)
			n++;
	}
	return (n);
}

/*
 * Return the first subdisk in given state.
 * If state is equal to -1, then the first connected disks.
 */
struct g_raid_subdisk *
g_raid_get_subdisk(struct g_raid_volume *vol, int state)
{
	struct g_raid_subdisk *sd;
	struct g_raid_softc *sc;
	u_int i;

	sc = vol->v_softc;
	sx_assert(&sc->sc_lock, SX_LOCKED);

	for (i = 0; i < vol->v_disks_count; i++) {
		sd = &vol->v_subdisks[i];
		if ((state == -1 &&
		     sd->sd_state != G_RAID_SUBDISK_S_NONE) ||
		    sd->sd_state == state)
			return (sd);
	}
	return (NULL);
}

struct g_consumer *
g_raid_open_consumer(struct g_raid_softc *sc, const char *name)
{
	struct g_consumer *cp;
	struct g_provider *pp;

	g_topology_assert();

	if (strncmp(name, "/dev/", 5) == 0)
		name += 5;
	pp = g_provider_by_name(name);
	if (pp == NULL)
		return (NULL);
	cp = g_new_consumer(sc->sc_geom);
	cp->flags |= G_CF_DIRECT_RECEIVE;
	if (g_attach(cp, pp) != 0) {
		g_destroy_consumer(cp);
		return (NULL);
	}
	if (g_access(cp, 1, 1, 1) != 0) {
		g_detach(cp);
		g_destroy_consumer(cp);
		return (NULL);
	}
	return (cp);
}

static u_int
g_raid_nrequests(struct g_raid_softc *sc, struct g_consumer *cp)
{
	struct bio *bp;
	u_int nreqs = 0;

	mtx_lock(&sc->sc_queue_mtx);
	TAILQ_FOREACH(bp, &sc->sc_queue.queue, bio_queue) {
		if (bp->bio_from == cp)
			nreqs++;
	}
	mtx_unlock(&sc->sc_queue_mtx);
	return (nreqs);
}

u_int
g_raid_nopens(struct g_raid_softc *sc)
{
	struct g_raid_volume *vol;
	u_int opens;

	opens = 0;
	TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
		if (vol->v_provider_open != 0)
			opens++;
	}
	return (opens);
}

static int
g_raid_consumer_is_busy(struct g_raid_softc *sc, struct g_consumer *cp)
{

	if (cp->index > 0) {
		G_RAID_DEBUG1(2, sc,
		    "I/O requests for %s exist, can't destroy it now.",
		    cp->provider->name);
		return (1);
	}
	if (g_raid_nrequests(sc, cp) > 0) {
		G_RAID_DEBUG1(2, sc,
		    "I/O requests for %s in queue, can't destroy it now.",
		    cp->provider->name);
		return (1);
	}
	return (0);
}

static void
g_raid_destroy_consumer(void *arg, int flags __unused)
{
	struct g_consumer *cp;

	g_topology_assert();

	cp = arg;
	G_RAID_DEBUG(1, "Consumer %s destroyed.", cp->provider->name);
	g_detach(cp);
	g_destroy_consumer(cp);
}

void
g_raid_kill_consumer(struct g_raid_softc *sc, struct g_consumer *cp)
{
	struct g_provider *pp;
	int retaste_wait;

	g_topology_assert_not();

	g_topology_lock();
	cp->private = NULL;
	if (g_raid_consumer_is_busy(sc, cp))
		goto out;
	pp = cp->provider;
	retaste_wait = 0;
	if (cp->acw == 1) {
		if ((pp->geom->flags & G_GEOM_WITHER) == 0)
			retaste_wait = 1;
	}
	if (cp->acr > 0 || cp->acw > 0 || cp->ace > 0)
		g_access(cp, -cp->acr, -cp->acw, -cp->ace);
	if (retaste_wait) {
		/*
		 * After retaste event was send (inside g_access()), we can send
		 * event to detach and destroy consumer.
		 * A class, which has consumer to the given provider connected
		 * will not receive retaste event for the provider.
		 * This is the way how I ignore retaste events when I close
		 * consumers opened for write: I detach and destroy consumer
		 * after retaste event is sent.
		 */
		g_post_event(g_raid_destroy_consumer, cp, M_WAITOK, NULL);
		goto out;
	}
	G_RAID_DEBUG(1, "Consumer %s destroyed.", pp->name);
	g_detach(cp);
	g_destroy_consumer(cp);
out:
	g_topology_unlock();
}

static void
g_raid_orphan(struct g_consumer *cp)
{
	struct g_raid_disk *disk;

	g_topology_assert();

	disk = cp->private;
	if (disk == NULL)
		return;
	g_raid_event_send(disk, G_RAID_DISK_E_DISCONNECTED,
	    G_RAID_EVENT_DISK);
}

static void
g_raid_clean(struct g_raid_volume *vol, int acw)
{
	struct g_raid_softc *sc;
	int timeout;

	sc = vol->v_softc;
	g_topology_assert_not();
	sx_assert(&sc->sc_lock, SX_XLOCKED);

//	if ((sc->sc_flags & G_RAID_DEVICE_FLAG_NOFAILSYNC) != 0)
//		return;
	if (!vol->v_dirty)
		return;
	if (vol->v_writes > 0)
		return;
	if (acw > 0 || (acw == -1 &&
	    vol->v_provider != NULL && vol->v_provider->acw > 0)) {
		timeout = g_raid_clean_time - (time_uptime - vol->v_last_write);
		if (!g_raid_shutdown && timeout > 0)
			return;
	}
	vol->v_dirty = 0;
	G_RAID_DEBUG1(1, sc, "Volume %s marked as clean.",
	    vol->v_name);
	g_raid_write_metadata(sc, vol, NULL, NULL);
}

static void
g_raid_dirty(struct g_raid_volume *vol)
{
	struct g_raid_softc *sc;

	sc = vol->v_softc;
	g_topology_assert_not();
	sx_assert(&sc->sc_lock, SX_XLOCKED);

//	if ((sc->sc_flags & G_RAID_DEVICE_FLAG_NOFAILSYNC) != 0)
//		return;
	vol->v_dirty = 1;
	G_RAID_DEBUG1(1, sc, "Volume %s marked as dirty.",
	    vol->v_name);
	g_raid_write_metadata(sc, vol, NULL, NULL);
}

void
g_raid_tr_flush_common(struct g_raid_tr_object *tr, struct bio *bp)
{
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct bio_queue_head queue;
	struct bio *cbp;
	int i;

	vol = tr->tro_volume;

	/*
	 * Allocate all bios before sending any request, so we can return
	 * ENOMEM in nice and clean way.
	 */
	bioq_init(&queue);
	for (i = 0; i < vol->v_disks_count; i++) {
		sd = &vol->v_subdisks[i];
		if (sd->sd_state == G_RAID_SUBDISK_S_NONE ||
		    sd->sd_state == G_RAID_SUBDISK_S_FAILED)
			continue;
		cbp = g_clone_bio(bp);
		if (cbp == NULL)
			goto failure;
		cbp->bio_caller1 = sd;
		bioq_insert_tail(&queue, cbp);
	}
	while ((cbp = bioq_takefirst(&queue)) != NULL) {
		sd = cbp->bio_caller1;
		cbp->bio_caller1 = NULL;
		g_raid_subdisk_iostart(sd, cbp);
	}
	return;
failure:
	while ((cbp = bioq_takefirst(&queue)) != NULL)
		g_destroy_bio(cbp);
	if (bp->bio_error == 0)
		bp->bio_error = ENOMEM;
	g_raid_iodone(bp, bp->bio_error);
}

static void
g_raid_tr_kerneldump_common_done(struct bio *bp)
{

	bp->bio_flags |= BIO_DONE;
}

int
g_raid_tr_kerneldump_common(struct g_raid_tr_object *tr,
    void *virtual, vm_offset_t physical, off_t offset, size_t length)
{
	struct g_raid_softc *sc;
	struct g_raid_volume *vol;
	struct bio bp;

	vol = tr->tro_volume;
	sc = vol->v_softc;

	g_reset_bio(&bp);
	bp.bio_cmd = BIO_WRITE;
	bp.bio_done = g_raid_tr_kerneldump_common_done;
	bp.bio_attribute = NULL;
	bp.bio_offset = offset;
	bp.bio_length = length;
	bp.bio_data = virtual;
	bp.bio_to = vol->v_provider;

	g_raid_start(&bp);
	while (!(bp.bio_flags & BIO_DONE)) {
		G_RAID_DEBUG1(4, sc, "Poll...");
		g_raid_poll(sc);
		DELAY(10);
	}

	return (bp.bio_error != 0 ? EIO : 0);
}

static int
g_raid_dump(void *arg,
    void *virtual, vm_offset_t physical, off_t offset, size_t length)
{
	struct g_raid_volume *vol;
	int error;

	vol = (struct g_raid_volume *)arg;
	G_RAID_DEBUG1(3, vol->v_softc, "Dumping at off %llu len %llu.",
	    (long long unsigned)offset, (long long unsigned)length);

	error = G_RAID_TR_KERNELDUMP(vol->v_tr,
	    virtual, physical, offset, length);
	return (error);
}

static void
g_raid_kerneldump(struct g_raid_softc *sc, struct bio *bp)
{
	struct g_kerneldump *gkd;
	struct g_provider *pp;
	struct g_raid_volume *vol;

	gkd = (struct g_kerneldump*)bp->bio_data;
	pp = bp->bio_to;
	vol = pp->private;
	g_trace(G_T_TOPOLOGY, "g_raid_kerneldump(%s, %jd, %jd)",
		pp->name, (intmax_t)gkd->offset, (intmax_t)gkd->length);
	gkd->di.dumper = g_raid_dump;
	gkd->di.priv = vol;
	gkd->di.blocksize = vol->v_sectorsize;
	gkd->di.maxiosize = DFLTPHYS;
	gkd->di.mediaoffset = gkd->offset;
	if ((gkd->offset + gkd->length) > vol->v_mediasize)
		gkd->length = vol->v_mediasize - gkd->offset;
	gkd->di.mediasize = gkd->length;
	g_io_deliver(bp, 0);
}

static void
g_raid_candelete(struct g_raid_softc *sc, struct bio *bp)
{
	struct g_provider *pp;
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	int i, val;

	pp = bp->bio_to;
	vol = pp->private;
	for (i = 0; i < vol->v_disks_count; i++) {
		sd = &vol->v_subdisks[i];
		if (sd->sd_state == G_RAID_SUBDISK_S_NONE)
			continue;
		if (sd->sd_disk->d_candelete)
			break;
	}
	val = i < vol->v_disks_count;
	g_handleattr(bp, "GEOM::candelete", &val, sizeof(val));
}

static void
g_raid_start(struct bio *bp)
{
	struct g_raid_softc *sc;

	sc = bp->bio_to->geom->softc;
	/*
	 * If sc == NULL or there are no valid disks, provider's error
	 * should be set and g_raid_start() should not be called at all.
	 */
//	KASSERT(sc != NULL && sc->sc_state == G_RAID_VOLUME_S_RUNNING,
//	    ("Provider's error should be set (error=%d)(mirror=%s).",
//	    bp->bio_to->error, bp->bio_to->name));
	G_RAID_LOGREQ(3, bp, "Request received.");

	switch (bp->bio_cmd) {
	case BIO_READ:
	case BIO_WRITE:
	case BIO_DELETE:
	case BIO_FLUSH:
		break;
	case BIO_GETATTR:
		if (!strcmp(bp->bio_attribute, "GEOM::candelete"))
			g_raid_candelete(sc, bp);
		else if (!strcmp(bp->bio_attribute, "GEOM::kerneldump"))
			g_raid_kerneldump(sc, bp);
		else
			g_io_deliver(bp, EOPNOTSUPP);
		return;
	default:
		g_io_deliver(bp, EOPNOTSUPP);
		return;
	}
	mtx_lock(&sc->sc_queue_mtx);
	bioq_insert_tail(&sc->sc_queue, bp);
	mtx_unlock(&sc->sc_queue_mtx);
	if (!dumping) {
		G_RAID_DEBUG1(4, sc, "Waking up %p.", sc);
		wakeup(sc);
	}
}

static int
g_raid_bio_overlaps(const struct bio *bp, off_t lstart, off_t len)
{
	/*
	 * 5 cases:
	 * (1) bp entirely below NO
	 * (2) bp entirely above NO
	 * (3) bp start below, but end in range YES
	 * (4) bp entirely within YES
	 * (5) bp starts within, ends above YES
	 *
	 * lock range 10-19 (offset 10 length 10)
	 * (1) 1-5: first if kicks it out
	 * (2) 30-35: second if kicks it out
	 * (3) 5-15: passes both ifs
	 * (4) 12-14: passes both ifs
	 * (5) 19-20: passes both
	 */
	off_t lend = lstart + len - 1;
	off_t bstart = bp->bio_offset;
	off_t bend = bp->bio_offset + bp->bio_length - 1;

	if (bend < lstart)
		return (0);
	if (lend < bstart)
		return (0);
	return (1);
}

static int
g_raid_is_in_locked_range(struct g_raid_volume *vol, const struct bio *bp)
{
	struct g_raid_lock *lp;

	sx_assert(&vol->v_softc->sc_lock, SX_LOCKED);

	LIST_FOREACH(lp, &vol->v_locks, l_next) {
		if (g_raid_bio_overlaps(bp, lp->l_offset, lp->l_length))
			return (1);
	}
	return (0);
}

static void
g_raid_start_request(struct bio *bp)
{
	struct g_raid_softc *sc;
	struct g_raid_volume *vol;

	sc = bp->bio_to->geom->softc;
	sx_assert(&sc->sc_lock, SX_LOCKED);
	vol = bp->bio_to->private;

	/*
	 * Check to see if this item is in a locked range.  If so,
	 * queue it to our locked queue and return.  We'll requeue
	 * it when the range is unlocked.  Internal I/O for the
	 * rebuild/rescan/recovery process is excluded from this
	 * check so we can actually do the recovery.
	 */
	if (!(bp->bio_cflags & G_RAID_BIO_FLAG_SPECIAL) &&
	    g_raid_is_in_locked_range(vol, bp)) {
		G_RAID_LOGREQ(3, bp, "Defer request.");
		bioq_insert_tail(&vol->v_locked, bp);
		return;
	}

	/*
	 * If we're actually going to do the write/delete, then
	 * update the idle stats for the volume.
	 */
	if (bp->bio_cmd == BIO_WRITE || bp->bio_cmd == BIO_DELETE) {
		if (!vol->v_dirty)
			g_raid_dirty(vol);
		vol->v_writes++;
	}

	/*
	 * Put request onto inflight queue, so we can check if new
	 * synchronization requests don't collide with it.  Then tell
	 * the transformation layer to start the I/O.
	 */
	bioq_insert_tail(&vol->v_inflight, bp);
	G_RAID_LOGREQ(4, bp, "Request started");
	G_RAID_TR_IOSTART(vol->v_tr, bp);
}

static void
g_raid_finish_with_locked_ranges(struct g_raid_volume *vol, struct bio *bp)
{
	off_t off, len;
	struct bio *nbp;
	struct g_raid_lock *lp;

	vol->v_pending_lock = 0;
	LIST_FOREACH(lp, &vol->v_locks, l_next) {
		if (lp->l_pending) {
			off = lp->l_offset;
			len = lp->l_length;
			lp->l_pending = 0;
			TAILQ_FOREACH(nbp, &vol->v_inflight.queue, bio_queue) {
				if (g_raid_bio_overlaps(nbp, off, len))
					lp->l_pending++;
			}
			if (lp->l_pending) {
				vol->v_pending_lock = 1;
				G_RAID_DEBUG1(4, vol->v_softc,
				    "Deferred lock(%jd, %jd) has %d pending",
				    (intmax_t)off, (intmax_t)(off + len),
				    lp->l_pending);
				continue;
			}
			G_RAID_DEBUG1(4, vol->v_softc,
			    "Deferred lock of %jd to %jd completed",
			    (intmax_t)off, (intmax_t)(off + len));
			G_RAID_TR_LOCKED(vol->v_tr, lp->l_callback_arg);
		}
	}
}

void
g_raid_iodone(struct bio *bp, int error)
{
	struct g_raid_softc *sc;
	struct g_raid_volume *vol;

	sc = bp->bio_to->geom->softc;
	sx_assert(&sc->sc_lock, SX_LOCKED);
	vol = bp->bio_to->private;
	G_RAID_LOGREQ(3, bp, "Request done: %d.", error);

	/* Update stats if we done write/delete. */
	if (bp->bio_cmd == BIO_WRITE || bp->bio_cmd == BIO_DELETE) {
		vol->v_writes--;
		vol->v_last_write = time_uptime;
	}

	bioq_remove(&vol->v_inflight, bp);
	if (vol->v_pending_lock && g_raid_is_in_locked_range(vol, bp))
		g_raid_finish_with_locked_ranges(vol, bp);
	getmicrouptime(&vol->v_last_done);
	g_io_deliver(bp, error);
}

int
g_raid_lock_range(struct g_raid_volume *vol, off_t off, off_t len,
    struct bio *ignore, void *argp)
{
	struct g_raid_softc *sc;
	struct g_raid_lock *lp;
	struct bio *bp;

	sc = vol->v_softc;
	lp = malloc(sizeof(*lp), M_RAID, M_WAITOK | M_ZERO);
	LIST_INSERT_HEAD(&vol->v_locks, lp, l_next);
	lp->l_offset = off;
	lp->l_length = len;
	lp->l_callback_arg = argp;

	lp->l_pending = 0;
	TAILQ_FOREACH(bp, &vol->v_inflight.queue, bio_queue) {
		if (bp != ignore && g_raid_bio_overlaps(bp, off, len))
			lp->l_pending++;
	}	

	/*
	 * If there are any writes that are pending, we return EBUSY.  All
	 * callers will have to wait until all pending writes clear.
	 */
	if (lp->l_pending > 0) {
		vol->v_pending_lock = 1;
		G_RAID_DEBUG1(4, sc, "Locking range %jd to %jd deferred %d pend",
		    (intmax_t)off, (intmax_t)(off+len), lp->l_pending);
		return (EBUSY);
	}
	G_RAID_DEBUG1(4, sc, "Locking range %jd to %jd",
	    (intmax_t)off, (intmax_t)(off+len));
	G_RAID_TR_LOCKED(vol->v_tr, lp->l_callback_arg);
	return (0);
}

int
g_raid_unlock_range(struct g_raid_volume *vol, off_t off, off_t len)
{
	struct g_raid_lock *lp;
	struct g_raid_softc *sc;
	struct bio *bp;

	sc = vol->v_softc;
	LIST_FOREACH(lp, &vol->v_locks, l_next) {
		if (lp->l_offset == off && lp->l_length == len) {
			LIST_REMOVE(lp, l_next);
			/* XXX
			 * Right now we just put them all back on the queue
			 * and hope for the best.  We hope this because any
			 * locked ranges will go right back on this list
			 * when the worker thread runs.
			 * XXX
			 */
			G_RAID_DEBUG1(4, sc, "Unlocked %jd to %jd",
			    (intmax_t)lp->l_offset,
			    (intmax_t)(lp->l_offset+lp->l_length));
			mtx_lock(&sc->sc_queue_mtx);
			while ((bp = bioq_takefirst(&vol->v_locked)) != NULL)
				bioq_insert_tail(&sc->sc_queue, bp);
			mtx_unlock(&sc->sc_queue_mtx);
			free(lp, M_RAID);
			return (0);
		}
	}
	return (EINVAL);
}

void
g_raid_subdisk_iostart(struct g_raid_subdisk *sd, struct bio *bp)
{
	struct g_consumer *cp;
	struct g_raid_disk *disk, *tdisk;

	bp->bio_caller1 = sd;

	/*
	 * Make sure that the disk is present. Generally it is a task of
	 * transformation layers to not send requests to absent disks, but
	 * it is better to be safe and report situation then sorry.
	 */
	if (sd->sd_disk == NULL) {
		G_RAID_LOGREQ(0, bp, "Warning! I/O request to an absent disk!");
nodisk:
		bp->bio_from = NULL;
		bp->bio_to = NULL;
		bp->bio_error = ENXIO;
		g_raid_disk_done(bp);
		return;
	}
	disk = sd->sd_disk;
	if (disk->d_state != G_RAID_DISK_S_ACTIVE &&
	    disk->d_state != G_RAID_DISK_S_FAILED) {
		G_RAID_LOGREQ(0, bp, "Warning! I/O request to a disk in a "
		    "wrong state (%s)!", g_raid_disk_state2str(disk->d_state));
		goto nodisk;
	}

	cp = disk->d_consumer;
	bp->bio_from = cp;
	bp->bio_to = cp->provider;
	cp->index++;

	/* Update average disks load. */
	TAILQ_FOREACH(tdisk, &sd->sd_softc->sc_disks, d_next) {
		if (tdisk->d_consumer == NULL)
			tdisk->d_load = 0;
		else
			tdisk->d_load = (tdisk->d_consumer->index *
			    G_RAID_SUBDISK_LOAD_SCALE + tdisk->d_load * 7) / 8;
	}

	disk->d_last_offset = bp->bio_offset + bp->bio_length;
	if (dumping) {
		G_RAID_LOGREQ(3, bp, "Sending dumping request.");
		if (bp->bio_cmd == BIO_WRITE) {
			bp->bio_error = g_raid_subdisk_kerneldump(sd,
			    bp->bio_data, 0, bp->bio_offset, bp->bio_length);
		} else
			bp->bio_error = EOPNOTSUPP;
		g_raid_disk_done(bp);
	} else {
		bp->bio_done = g_raid_disk_done;
		bp->bio_offset += sd->sd_offset;
		G_RAID_LOGREQ(3, bp, "Sending request.");
		g_io_request(bp, cp);
	}
}

int
g_raid_subdisk_kerneldump(struct g_raid_subdisk *sd,
    void *virtual, vm_offset_t physical, off_t offset, size_t length)
{

	if (sd->sd_disk == NULL)
		return (ENXIO);
	if (sd->sd_disk->d_kd.di.dumper == NULL)
		return (EOPNOTSUPP);
	return (dump_write(&sd->sd_disk->d_kd.di,
	    virtual, physical,
	    sd->sd_disk->d_kd.di.mediaoffset + sd->sd_offset + offset,
	    length));
}

static void
g_raid_disk_done(struct bio *bp)
{
	struct g_raid_softc *sc;
	struct g_raid_subdisk *sd;

	sd = bp->bio_caller1;
	sc = sd->sd_softc;
	mtx_lock(&sc->sc_queue_mtx);
	bioq_insert_tail(&sc->sc_queue, bp);
	mtx_unlock(&sc->sc_queue_mtx);
	if (!dumping)
		wakeup(sc);
}

static void
g_raid_disk_done_request(struct bio *bp)
{
	struct g_raid_softc *sc;
	struct g_raid_disk *disk;
	struct g_raid_subdisk *sd;
	struct g_raid_volume *vol;

	g_topology_assert_not();

	G_RAID_LOGREQ(3, bp, "Disk request done: %d.", bp->bio_error);
	sd = bp->bio_caller1;
	sc = sd->sd_softc;
	vol = sd->sd_volume;
	if (bp->bio_from != NULL) {
		bp->bio_from->index--;
		disk = bp->bio_from->private;
		if (disk == NULL)
			g_raid_kill_consumer(sc, bp->bio_from);
	}
	bp->bio_offset -= sd->sd_offset;

	G_RAID_TR_IODONE(vol->v_tr, sd, bp);
}

static void
g_raid_handle_event(struct g_raid_softc *sc, struct g_raid_event *ep)
{

	if ((ep->e_flags & G_RAID_EVENT_VOLUME) != 0)
		ep->e_error = g_raid_update_volume(ep->e_tgt, ep->e_event);
	else if ((ep->e_flags & G_RAID_EVENT_DISK) != 0)
		ep->e_error = g_raid_update_disk(ep->e_tgt, ep->e_event);
	else if ((ep->e_flags & G_RAID_EVENT_SUBDISK) != 0)
		ep->e_error = g_raid_update_subdisk(ep->e_tgt, ep->e_event);
	else
		ep->e_error = g_raid_update_node(ep->e_tgt, ep->e_event);
	if ((ep->e_flags & G_RAID_EVENT_WAIT) == 0) {
		KASSERT(ep->e_error == 0,
		    ("Error cannot be handled."));
		g_raid_event_free(ep);
	} else {
		ep->e_flags |= G_RAID_EVENT_DONE;
		G_RAID_DEBUG1(4, sc, "Waking up %p.", ep);
		mtx_lock(&sc->sc_queue_mtx);
		wakeup(ep);
		mtx_unlock(&sc->sc_queue_mtx);
	}
}

/*
 * Worker thread.
 */
static void
g_raid_worker(void *arg)
{
	struct g_raid_softc *sc;
	struct g_raid_event *ep;
	struct g_raid_volume *vol;
	struct bio *bp;
	struct timeval now, t;
	int timeout, rv;

	sc = arg;
	thread_lock(curthread);
	sched_prio(curthread, PRIBIO);
	thread_unlock(curthread);

	sx_xlock(&sc->sc_lock);
	for (;;) {
		mtx_lock(&sc->sc_queue_mtx);
		/*
		 * First take a look at events.
		 * This is important to handle events before any I/O requests.
		 */
		bp = NULL;
		vol = NULL;
		rv = 0;
		ep = TAILQ_FIRST(&sc->sc_events);
		if (ep != NULL)
			TAILQ_REMOVE(&sc->sc_events, ep, e_next);
		else if ((bp = bioq_takefirst(&sc->sc_queue)) != NULL)
			;
		else {
			getmicrouptime(&now);
			t = now;
			TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
				if (bioq_first(&vol->v_inflight) == NULL &&
				    vol->v_tr &&
				    timevalcmp(&vol->v_last_done, &t, < ))
					t = vol->v_last_done;
			}
			timevalsub(&t, &now);
			timeout = g_raid_idle_threshold +
			    t.tv_sec * 1000000 + t.tv_usec;
			if (timeout > 0) {
				/*
				 * Two steps to avoid overflows at HZ=1000
				 * and idle timeouts > 2.1s.  Some rounding
				 * errors can occur, but they are < 1tick,
				 * which is deemed to be close enough for
				 * this purpose.
				 */
				int micpertic = 1000000 / hz;
				timeout = (timeout + micpertic - 1) / micpertic;
				sx_xunlock(&sc->sc_lock);
				MSLEEP(rv, sc, &sc->sc_queue_mtx,
				    PRIBIO | PDROP, "-", timeout);
				sx_xlock(&sc->sc_lock);
				goto process;
			} else
				rv = EWOULDBLOCK;
		}
		mtx_unlock(&sc->sc_queue_mtx);
process:
		if (ep != NULL) {
			g_raid_handle_event(sc, ep);
		} else if (bp != NULL) {
			if (bp->bio_to != NULL &&
			    bp->bio_to->geom == sc->sc_geom)
				g_raid_start_request(bp);
			else
				g_raid_disk_done_request(bp);
		} else if (rv == EWOULDBLOCK) {
			TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
				g_raid_clean(vol, -1);
				if (bioq_first(&vol->v_inflight) == NULL &&
				    vol->v_tr) {
					t.tv_sec = g_raid_idle_threshold / 1000000;
					t.tv_usec = g_raid_idle_threshold % 1000000;
					timevaladd(&t, &vol->v_last_done);
					getmicrouptime(&now);
					if (timevalcmp(&t, &now, <= )) {
						G_RAID_TR_IDLE(vol->v_tr);
						vol->v_last_done = now;
					}
				}
			}
		}
		if (sc->sc_stopping == G_RAID_DESTROY_HARD)
			g_raid_destroy_node(sc, 1);	/* May not return. */
	}
}

static void
g_raid_poll(struct g_raid_softc *sc)
{
	struct g_raid_event *ep;
	struct bio *bp;

	sx_xlock(&sc->sc_lock);
	mtx_lock(&sc->sc_queue_mtx);
	/*
	 * First take a look at events.
	 * This is important to handle events before any I/O requests.
	 */
	ep = TAILQ_FIRST(&sc->sc_events);
	if (ep != NULL) {
		TAILQ_REMOVE(&sc->sc_events, ep, e_next);
		mtx_unlock(&sc->sc_queue_mtx);
		g_raid_handle_event(sc, ep);
		goto out;
	}
	bp = bioq_takefirst(&sc->sc_queue);
	if (bp != NULL) {
		mtx_unlock(&sc->sc_queue_mtx);
		if (bp->bio_from == NULL ||
		    bp->bio_from->geom != sc->sc_geom)
			g_raid_start_request(bp);
		else
			g_raid_disk_done_request(bp);
	}
out:
	sx_xunlock(&sc->sc_lock);
}

static void
g_raid_launch_provider(struct g_raid_volume *vol)
{
	struct g_raid_disk *disk;
	struct g_raid_subdisk *sd;
	struct g_raid_softc *sc;
	struct g_provider *pp;
	char name[G_RAID_MAX_VOLUMENAME];
	off_t off;
	int i;

	sc = vol->v_softc;
	sx_assert(&sc->sc_lock, SX_LOCKED);

	g_topology_lock();
	/* Try to name provider with volume name. */
	snprintf(name, sizeof(name), "raid/%s", vol->v_name);
	if (g_raid_name_format == 0 || vol->v_name[0] == 0 ||
	    g_provider_by_name(name) != NULL) {
		/* Otherwise use sequential volume number. */
		snprintf(name, sizeof(name), "raid/r%d", vol->v_global_id);
	}

	pp = g_new_providerf(sc->sc_geom, "%s", name);
	pp->flags |= G_PF_DIRECT_RECEIVE;
	if (vol->v_tr->tro_class->trc_accept_unmapped) {
		pp->flags |= G_PF_ACCEPT_UNMAPPED;
		for (i = 0; i < vol->v_disks_count; i++) {
			sd = &vol->v_subdisks[i];
			if (sd->sd_state == G_RAID_SUBDISK_S_NONE)
				continue;
			if ((sd->sd_disk->d_consumer->provider->flags &
			    G_PF_ACCEPT_UNMAPPED) == 0)
				pp->flags &= ~G_PF_ACCEPT_UNMAPPED;
		}
	}
	pp->private = vol;
	pp->mediasize = vol->v_mediasize;
	pp->sectorsize = vol->v_sectorsize;
	pp->stripesize = 0;
	pp->stripeoffset = 0;
	if (vol->v_raid_level == G_RAID_VOLUME_RL_RAID1 ||
	    vol->v_raid_level == G_RAID_VOLUME_RL_RAID3 ||
	    vol->v_raid_level == G_RAID_VOLUME_RL_SINGLE ||
	    vol->v_raid_level == G_RAID_VOLUME_RL_CONCAT) {
		if ((disk = vol->v_subdisks[0].sd_disk) != NULL &&
		    disk->d_consumer != NULL &&
		    disk->d_consumer->provider != NULL) {
			pp->stripesize = disk->d_consumer->provider->stripesize;
			off = disk->d_consumer->provider->stripeoffset;
			pp->stripeoffset = off + vol->v_subdisks[0].sd_offset;
			if (off > 0)
				pp->stripeoffset %= off;
		}
		if (vol->v_raid_level == G_RAID_VOLUME_RL_RAID3) {
			pp->stripesize *= (vol->v_disks_count - 1);
			pp->stripeoffset *= (vol->v_disks_count - 1);
		}
	} else
		pp->stripesize = vol->v_strip_size;
	vol->v_provider = pp;
	g_error_provider(pp, 0);
	g_topology_unlock();
	G_RAID_DEBUG1(0, sc, "Provider %s for volume %s created.",
	    pp->name, vol->v_name);
}

static void
g_raid_destroy_provider(struct g_raid_volume *vol)
{
	struct g_raid_softc *sc;
	struct g_provider *pp;
	struct bio *bp, *tmp;

	g_topology_assert_not();
	sc = vol->v_softc;
	pp = vol->v_provider;
	KASSERT(pp != NULL, ("NULL provider (volume=%s).", vol->v_name));

	g_topology_lock();
	g_error_provider(pp, ENXIO);
	mtx_lock(&sc->sc_queue_mtx);
	TAILQ_FOREACH_SAFE(bp, &sc->sc_queue.queue, bio_queue, tmp) {
		if (bp->bio_to != pp)
			continue;
		bioq_remove(&sc->sc_queue, bp);
		g_io_deliver(bp, ENXIO);
	}
	mtx_unlock(&sc->sc_queue_mtx);
	G_RAID_DEBUG1(0, sc, "Provider %s for volume %s destroyed.",
	    pp->name, vol->v_name);
	g_wither_provider(pp, ENXIO);
	g_topology_unlock();
	vol->v_provider = NULL;
}

/*
 * Update device state.
 */
static int
g_raid_update_volume(struct g_raid_volume *vol, u_int event)
{
	struct g_raid_softc *sc;

	sc = vol->v_softc;
	sx_assert(&sc->sc_lock, SX_XLOCKED);

	G_RAID_DEBUG1(2, sc, "Event %s for volume %s.",
	    g_raid_volume_event2str(event),
	    vol->v_name);
	switch (event) {
	case G_RAID_VOLUME_E_DOWN:
		if (vol->v_provider != NULL)
			g_raid_destroy_provider(vol);
		break;
	case G_RAID_VOLUME_E_UP:
		if (vol->v_provider == NULL)
			g_raid_launch_provider(vol);
		break;
	case G_RAID_VOLUME_E_START:
		if (vol->v_tr)
			G_RAID_TR_START(vol->v_tr);
		return (0);
	default:
		if (sc->sc_md)
			G_RAID_MD_VOLUME_EVENT(sc->sc_md, vol, event);
		return (0);
	}

	/* Manage root mount release. */
	if (vol->v_starting) {
		vol->v_starting = 0;
		G_RAID_DEBUG1(1, sc, "root_mount_rel %p", vol->v_rootmount);
		root_mount_rel(vol->v_rootmount);
		vol->v_rootmount = NULL;
	}
	if (vol->v_stopping && vol->v_provider_open == 0)
		g_raid_destroy_volume(vol);
	return (0);
}

/*
 * Update subdisk state.
 */
static int
g_raid_update_subdisk(struct g_raid_subdisk *sd, u_int event)
{
	struct g_raid_softc *sc;
	struct g_raid_volume *vol;

	sc = sd->sd_softc;
	vol = sd->sd_volume;
	sx_assert(&sc->sc_lock, SX_XLOCKED);

	G_RAID_DEBUG1(2, sc, "Event %s for subdisk %s:%d-%s.",
	    g_raid_subdisk_event2str(event),
	    vol->v_name, sd->sd_pos,
	    sd->sd_disk ? g_raid_get_diskname(sd->sd_disk) : "[none]");
	if (vol->v_tr)
		G_RAID_TR_EVENT(vol->v_tr, sd, event);

	return (0);
}

/*
 * Update disk state.
 */
static int
g_raid_update_disk(struct g_raid_disk *disk, u_int event)
{
	struct g_raid_softc *sc;

	sc = disk->d_softc;
	sx_assert(&sc->sc_lock, SX_XLOCKED);

	G_RAID_DEBUG1(2, sc, "Event %s for disk %s.",
	    g_raid_disk_event2str(event),
	    g_raid_get_diskname(disk));

	if (sc->sc_md)
		G_RAID_MD_EVENT(sc->sc_md, disk, event);
	return (0);
}

/*
 * Node event.
 */
static int
g_raid_update_node(struct g_raid_softc *sc, u_int event)
{
	sx_assert(&sc->sc_lock, SX_XLOCKED);

	G_RAID_DEBUG1(2, sc, "Event %s for the array.",
	    g_raid_node_event2str(event));

	if (event == G_RAID_NODE_E_WAKE)
		return (0);
	if (sc->sc_md)
		G_RAID_MD_EVENT(sc->sc_md, NULL, event);
	return (0);
}

static int
g_raid_access(struct g_provider *pp, int acr, int acw, int ace)
{
	struct g_raid_volume *vol;
	struct g_raid_softc *sc;
	int dcw, opens, error = 0;

	g_topology_assert();
	sc = pp->geom->softc;
	vol = pp->private;
	KASSERT(sc != NULL, ("NULL softc (provider=%s).", pp->name));
	KASSERT(vol != NULL, ("NULL volume (provider=%s).", pp->name));

	G_RAID_DEBUG1(2, sc, "Access request for %s: r%dw%de%d.", pp->name,
	    acr, acw, ace);
	dcw = pp->acw + acw;

	g_topology_unlock();
	sx_xlock(&sc->sc_lock);
	/* Deny new opens while dying. */
	if (sc->sc_stopping != 0 && (acr > 0 || acw > 0 || ace > 0)) {
		error = ENXIO;
		goto out;
	}
	/* Deny write opens for read-only volumes. */
	if (vol->v_read_only && acw > 0) {
		error = EROFS;
		goto out;
	}
	if (dcw == 0)
		g_raid_clean(vol, dcw);
	vol->v_provider_open += acr + acw + ace;
	/* Handle delayed node destruction. */
	if (sc->sc_stopping == G_RAID_DESTROY_DELAYED &&
	    vol->v_provider_open == 0) {
		/* Count open volumes. */
		opens = g_raid_nopens(sc);
		if (opens == 0) {
			sc->sc_stopping = G_RAID_DESTROY_HARD;
			/* Wake up worker to make it selfdestruct. */
			g_raid_event_send(sc, G_RAID_NODE_E_WAKE, 0);
		}
	}
	/* Handle open volume destruction. */
	if (vol->v_stopping && vol->v_provider_open == 0)
		g_raid_destroy_volume(vol);
out:
	sx_xunlock(&sc->sc_lock);
	g_topology_lock();
	return (error);
}

struct g_raid_softc *
g_raid_create_node(struct g_class *mp,
    const char *name, struct g_raid_md_object *md)
{
	struct g_raid_softc *sc;
	struct g_geom *gp;
	int error;

	g_topology_assert();
	G_RAID_DEBUG(1, "Creating array %s.", name);

	gp = g_new_geomf(mp, "%s", name);
	sc = malloc(sizeof(*sc), M_RAID, M_WAITOK | M_ZERO);
	gp->start = g_raid_start;
	gp->orphan = g_raid_orphan;
	gp->access = g_raid_access;
	gp->dumpconf = g_raid_dumpconf;

	sc->sc_md = md;
	sc->sc_geom = gp;
	sc->sc_flags = 0;
	TAILQ_INIT(&sc->sc_volumes);
	TAILQ_INIT(&sc->sc_disks);
	sx_init(&sc->sc_lock, "graid:lock");
	mtx_init(&sc->sc_queue_mtx, "graid:queue", NULL, MTX_DEF);
	TAILQ_INIT(&sc->sc_events);
	bioq_init(&sc->sc_queue);
	gp->softc = sc;
	error = kproc_create(g_raid_worker, sc, &sc->sc_worker, 0, 0,
	    "g_raid %s", name);
	if (error != 0) {
		G_RAID_DEBUG(0, "Cannot create kernel thread for %s.", name);
		mtx_destroy(&sc->sc_queue_mtx);
		sx_destroy(&sc->sc_lock);
		g_destroy_geom(sc->sc_geom);
		free(sc, M_RAID);
		return (NULL);
	}

	G_RAID_DEBUG1(0, sc, "Array %s created.", name);
	return (sc);
}

struct g_raid_volume *
g_raid_create_volume(struct g_raid_softc *sc, const char *name, int id)
{
	struct g_raid_volume	*vol, *vol1;
	int i;

	G_RAID_DEBUG1(1, sc, "Creating volume %s.", name);
	vol = malloc(sizeof(*vol), M_RAID, M_WAITOK | M_ZERO);
	vol->v_softc = sc;
	strlcpy(vol->v_name, name, G_RAID_MAX_VOLUMENAME);
	vol->v_state = G_RAID_VOLUME_S_STARTING;
	vol->v_raid_level = G_RAID_VOLUME_RL_UNKNOWN;
	vol->v_raid_level_qualifier = G_RAID_VOLUME_RLQ_UNKNOWN;
	vol->v_rotate_parity = 1;
	bioq_init(&vol->v_inflight);
	bioq_init(&vol->v_locked);
	LIST_INIT(&vol->v_locks);
	for (i = 0; i < G_RAID_MAX_SUBDISKS; i++) {
		vol->v_subdisks[i].sd_softc = sc;
		vol->v_subdisks[i].sd_volume = vol;
		vol->v_subdisks[i].sd_pos = i;
		vol->v_subdisks[i].sd_state = G_RAID_DISK_S_NONE;
	}

	/* Find free ID for this volume. */
	g_topology_lock();
	vol1 = vol;
	if (id >= 0) {
		LIST_FOREACH(vol1, &g_raid_volumes, v_global_next) {
			if (vol1->v_global_id == id)
				break;
		}
	}
	if (vol1 != NULL) {
		for (id = 0; ; id++) {
			LIST_FOREACH(vol1, &g_raid_volumes, v_global_next) {
				if (vol1->v_global_id == id)
					break;
			}
			if (vol1 == NULL)
				break;
		}
	}
	vol->v_global_id = id;
	LIST_INSERT_HEAD(&g_raid_volumes, vol, v_global_next);
	g_topology_unlock();

	/* Delay root mounting. */
	vol->v_rootmount = root_mount_hold("GRAID");
	G_RAID_DEBUG1(1, sc, "root_mount_hold %p", vol->v_rootmount);
	vol->v_starting = 1;
	TAILQ_INSERT_TAIL(&sc->sc_volumes, vol, v_next);
	return (vol);
}

struct g_raid_disk *
g_raid_create_disk(struct g_raid_softc *sc)
{
	struct g_raid_disk	*disk;

	G_RAID_DEBUG1(1, sc, "Creating disk.");
	disk = malloc(sizeof(*disk), M_RAID, M_WAITOK | M_ZERO);
	disk->d_softc = sc;
	disk->d_state = G_RAID_DISK_S_NONE;
	TAILQ_INIT(&disk->d_subdisks);
	TAILQ_INSERT_TAIL(&sc->sc_disks, disk, d_next);
	return (disk);
}

int g_raid_start_volume(struct g_raid_volume *vol)
{
	struct g_raid_tr_class *class;
	struct g_raid_tr_object *obj;
	int status;

	G_RAID_DEBUG1(2, vol->v_softc, "Starting volume %s.", vol->v_name);
	LIST_FOREACH(class, &g_raid_tr_classes, trc_list) {
		if (!class->trc_enable)
			continue;
		G_RAID_DEBUG1(2, vol->v_softc,
		    "Tasting volume %s for %s transformation.",
		    vol->v_name, class->name);
		obj = (void *)kobj_create((kobj_class_t)class, M_RAID,
		    M_WAITOK);
		obj->tro_class = class;
		obj->tro_volume = vol;
		status = G_RAID_TR_TASTE(obj, vol);
		if (status != G_RAID_TR_TASTE_FAIL)
			break;
		kobj_delete((kobj_t)obj, M_RAID);
	}
	if (class == NULL) {
		G_RAID_DEBUG1(0, vol->v_softc,
		    "No transformation module found for %s.",
		    vol->v_name);
		vol->v_tr = NULL;
		g_raid_change_volume_state(vol, G_RAID_VOLUME_S_UNSUPPORTED);
		g_raid_event_send(vol, G_RAID_VOLUME_E_DOWN,
		    G_RAID_EVENT_VOLUME);
		return (-1);
	}
	G_RAID_DEBUG1(2, vol->v_softc,
	    "Transformation module %s chosen for %s.",
	    class->name, vol->v_name);
	vol->v_tr = obj;
	return (0);
}

int
g_raid_destroy_node(struct g_raid_softc *sc, int worker)
{
	struct g_raid_volume *vol, *tmpv;
	struct g_raid_disk *disk, *tmpd;
	int error = 0;

	sc->sc_stopping = G_RAID_DESTROY_HARD;
	TAILQ_FOREACH_SAFE(vol, &sc->sc_volumes, v_next, tmpv) {
		if (g_raid_destroy_volume(vol))
			error = EBUSY;
	}
	if (error)
		return (error);
	TAILQ_FOREACH_SAFE(disk, &sc->sc_disks, d_next, tmpd) {
		if (g_raid_destroy_disk(disk))
			error = EBUSY;
	}
	if (error)
		return (error);
	if (sc->sc_md) {
		G_RAID_MD_FREE(sc->sc_md);
		kobj_delete((kobj_t)sc->sc_md, M_RAID);
		sc->sc_md = NULL;
	}
	if (sc->sc_geom != NULL) {
		G_RAID_DEBUG1(0, sc, "Array %s destroyed.", sc->sc_name);
		g_topology_lock();
		sc->sc_geom->softc = NULL;
		g_wither_geom(sc->sc_geom, ENXIO);
		g_topology_unlock();
		sc->sc_geom = NULL;
	} else
		G_RAID_DEBUG(1, "Array destroyed.");
	if (worker) {
		g_raid_event_cancel(sc, sc);
		mtx_destroy(&sc->sc_queue_mtx);
		sx_xunlock(&sc->sc_lock);
		sx_destroy(&sc->sc_lock);
		wakeup(&sc->sc_stopping);
		free(sc, M_RAID);
		curthread->td_pflags &= ~TDP_GEOM;
		G_RAID_DEBUG(1, "Thread exiting.");
		kproc_exit(0);
	} else {
		/* Wake up worker to make it selfdestruct. */
		g_raid_event_send(sc, G_RAID_NODE_E_WAKE, 0);
	}
	return (0);
}

int
g_raid_destroy_volume(struct g_raid_volume *vol)
{
	struct g_raid_softc *sc;
	struct g_raid_disk *disk;
	int i;

	sc = vol->v_softc;
	G_RAID_DEBUG1(2, sc, "Destroying volume %s.", vol->v_name);
	vol->v_stopping = 1;
	if (vol->v_state != G_RAID_VOLUME_S_STOPPED) {
		if (vol->v_tr) {
			G_RAID_TR_STOP(vol->v_tr);
			return (EBUSY);
		} else
			vol->v_state = G_RAID_VOLUME_S_STOPPED;
	}
	if (g_raid_event_check(sc, vol) != 0)
		return (EBUSY);
	if (vol->v_provider != NULL)
		return (EBUSY);
	if (vol->v_provider_open != 0)
		return (EBUSY);
	if (vol->v_tr) {
		G_RAID_TR_FREE(vol->v_tr);
		kobj_delete((kobj_t)vol->v_tr, M_RAID);
		vol->v_tr = NULL;
	}
	if (vol->v_rootmount)
		root_mount_rel(vol->v_rootmount);
	g_topology_lock();
	LIST_REMOVE(vol, v_global_next);
	g_topology_unlock();
	TAILQ_REMOVE(&sc->sc_volumes, vol, v_next);
	for (i = 0; i < G_RAID_MAX_SUBDISKS; i++) {
		g_raid_event_cancel(sc, &vol->v_subdisks[i]);
		disk = vol->v_subdisks[i].sd_disk;
		if (disk == NULL)
			continue;
		TAILQ_REMOVE(&disk->d_subdisks, &vol->v_subdisks[i], sd_next);
	}
	G_RAID_DEBUG1(2, sc, "Volume %s destroyed.", vol->v_name);
	if (sc->sc_md)
		G_RAID_MD_FREE_VOLUME(sc->sc_md, vol);
	g_raid_event_cancel(sc, vol);
	free(vol, M_RAID);
	if (sc->sc_stopping == G_RAID_DESTROY_HARD) {
		/* Wake up worker to let it selfdestruct. */
		g_raid_event_send(sc, G_RAID_NODE_E_WAKE, 0);
	}
	return (0);
}

int
g_raid_destroy_disk(struct g_raid_disk *disk)
{
	struct g_raid_softc *sc;
	struct g_raid_subdisk *sd, *tmp;

	sc = disk->d_softc;
	G_RAID_DEBUG1(2, sc, "Destroying disk.");
	if (disk->d_consumer) {
		g_raid_kill_consumer(sc, disk->d_consumer);
		disk->d_consumer = NULL;
	}
	TAILQ_FOREACH_SAFE(sd, &disk->d_subdisks, sd_next, tmp) {
		g_raid_change_subdisk_state(sd, G_RAID_SUBDISK_S_NONE);
		g_raid_event_send(sd, G_RAID_SUBDISK_E_DISCONNECTED,
		    G_RAID_EVENT_SUBDISK);
		TAILQ_REMOVE(&disk->d_subdisks, sd, sd_next);
		sd->sd_disk = NULL;
	}
	TAILQ_REMOVE(&sc->sc_disks, disk, d_next);
	if (sc->sc_md)
		G_RAID_MD_FREE_DISK(sc->sc_md, disk);
	g_raid_event_cancel(sc, disk);
	free(disk, M_RAID);
	return (0);
}

int
g_raid_destroy(struct g_raid_softc *sc, int how)
{
	int error, opens;

	g_topology_assert_not();
	if (sc == NULL)
		return (ENXIO);
	sx_assert(&sc->sc_lock, SX_XLOCKED);

	/* Count open volumes. */
	opens = g_raid_nopens(sc);

	/* React on some opened volumes. */
	if (opens > 0) {
		switch (how) {
		case G_RAID_DESTROY_SOFT:
			G_RAID_DEBUG1(1, sc,
			    "%d volumes are still open.",
			    opens);
			sx_xunlock(&sc->sc_lock);
			return (EBUSY);
		case G_RAID_DESTROY_DELAYED:
			G_RAID_DEBUG1(1, sc,
			    "Array will be destroyed on last close.");
			sc->sc_stopping = G_RAID_DESTROY_DELAYED;
			sx_xunlock(&sc->sc_lock);
			return (EBUSY);
		case G_RAID_DESTROY_HARD:
			G_RAID_DEBUG1(1, sc,
			    "%d volumes are still open.",
			    opens);
		}
	}

	/* Mark node for destruction. */
	sc->sc_stopping = G_RAID_DESTROY_HARD;
	/* Wake up worker to let it selfdestruct. */
	g_raid_event_send(sc, G_RAID_NODE_E_WAKE, 0);
	/* Sleep until node destroyed. */
	error = sx_sleep(&sc->sc_stopping, &sc->sc_lock,
	    PRIBIO | PDROP, "r:destroy", hz * 3);
	return (error == EWOULDBLOCK ? EBUSY : 0);
}

static void
g_raid_taste_orphan(struct g_consumer *cp)
{

	KASSERT(1 == 0, ("%s called while tasting %s.", __func__,
	    cp->provider->name));
}

static struct g_geom *
g_raid_taste(struct g_class *mp, struct g_provider *pp, int flags __unused)
{
	struct g_consumer *cp;
	struct g_geom *gp, *geom;
	struct g_raid_md_class *class;
	struct g_raid_md_object *obj;
	int status;

	g_topology_assert();
	g_trace(G_T_TOPOLOGY, "%s(%s, %s)", __func__, mp->name, pp->name);
	if (!g_raid_enable)
		return (NULL);
	G_RAID_DEBUG(2, "Tasting provider %s.", pp->name);

	geom = NULL;
	status = G_RAID_MD_TASTE_FAIL;
	gp = g_new_geomf(mp, "raid:taste");
	/*
	 * This orphan function should be never called.
	 */
	gp->orphan = g_raid_taste_orphan;
	cp = g_new_consumer(gp);
	cp->flags |= G_CF_DIRECT_RECEIVE;
	g_attach(cp, pp);
	if (g_access(cp, 1, 0, 0) != 0)
		goto ofail;

	LIST_FOREACH(class, &g_raid_md_classes, mdc_list) {
		if (!class->mdc_enable)
			continue;
		G_RAID_DEBUG(2, "Tasting provider %s for %s metadata.",
		    pp->name, class->name);
		obj = (void *)kobj_create((kobj_class_t)class, M_RAID,
		    M_WAITOK);
		obj->mdo_class = class;
		status = G_RAID_MD_TASTE(obj, mp, cp, &geom);
		if (status != G_RAID_MD_TASTE_NEW)
			kobj_delete((kobj_t)obj, M_RAID);
		if (status != G_RAID_MD_TASTE_FAIL)
			break;
	}

	if (status == G_RAID_MD_TASTE_FAIL)
		(void)g_access(cp, -1, 0, 0);
ofail:
	g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_geom(gp);
	G_RAID_DEBUG(2, "Tasting provider %s done.", pp->name);
	return (geom);
}

int
g_raid_create_node_format(const char *format, struct gctl_req *req,
    struct g_geom **gp)
{
	struct g_raid_md_class *class;
	struct g_raid_md_object *obj;
	int status;

	G_RAID_DEBUG(2, "Creating array for %s metadata.", format);
	LIST_FOREACH(class, &g_raid_md_classes, mdc_list) {
		if (strcasecmp(class->name, format) == 0)
			break;
	}
	if (class == NULL) {
		G_RAID_DEBUG(1, "No support for %s metadata.", format);
		return (G_RAID_MD_TASTE_FAIL);
	}
	obj = (void *)kobj_create((kobj_class_t)class, M_RAID,
	    M_WAITOK);
	obj->mdo_class = class;
	status = G_RAID_MD_CREATE_REQ(obj, &g_raid_class, req, gp);
	if (status != G_RAID_MD_TASTE_NEW)
		kobj_delete((kobj_t)obj, M_RAID);
	return (status);
}

static int
g_raid_destroy_geom(struct gctl_req *req __unused,
    struct g_class *mp __unused, struct g_geom *gp)
{
	struct g_raid_softc *sc;
	int error;

	g_topology_unlock();
	sc = gp->softc;
	sx_xlock(&sc->sc_lock);
	g_cancel_event(sc);
	error = g_raid_destroy(gp->softc, G_RAID_DESTROY_SOFT);
	g_topology_lock();
	return (error);
}

void g_raid_write_metadata(struct g_raid_softc *sc, struct g_raid_volume *vol,
    struct g_raid_subdisk *sd, struct g_raid_disk *disk)
{

	if (sc->sc_stopping == G_RAID_DESTROY_HARD)
		return;
	if (sc->sc_md)
		G_RAID_MD_WRITE(sc->sc_md, vol, sd, disk);
}

void g_raid_fail_disk(struct g_raid_softc *sc,
    struct g_raid_subdisk *sd, struct g_raid_disk *disk)
{

	if (disk == NULL)
		disk = sd->sd_disk;
	if (disk == NULL) {
		G_RAID_DEBUG1(0, sc, "Warning! Fail request to an absent disk!");
		return;
	}
	if (disk->d_state != G_RAID_DISK_S_ACTIVE) {
		G_RAID_DEBUG1(0, sc, "Warning! Fail request to a disk in a "
		    "wrong state (%s)!", g_raid_disk_state2str(disk->d_state));
		return;
	}
	if (sc->sc_md)
		G_RAID_MD_FAIL_DISK(sc->sc_md, sd, disk);
}

static void
g_raid_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp, struct g_provider *pp)
{
	struct g_raid_softc *sc;
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct g_raid_disk *disk;
	int i, s;

	g_topology_assert();

	sc = gp->softc;
	if (sc == NULL)
		return;
	if (pp != NULL) {
		vol = pp->private;
		g_topology_unlock();
		sx_xlock(&sc->sc_lock);
		sbuf_printf(sb, "%s<descr>%s %s volume</descr>\n", indent,
		    sc->sc_md->mdo_class->name,
		    g_raid_volume_level2str(vol->v_raid_level,
		    vol->v_raid_level_qualifier));
		sbuf_printf(sb, "%s<Label>%s</Label>\n", indent,
		    vol->v_name);
		sbuf_printf(sb, "%s<RAIDLevel>%s</RAIDLevel>\n", indent,
		    g_raid_volume_level2str(vol->v_raid_level,
		    vol->v_raid_level_qualifier));
		sbuf_printf(sb,
		    "%s<Transformation>%s</Transformation>\n", indent,
		    vol->v_tr ? vol->v_tr->tro_class->name : "NONE");
		sbuf_printf(sb, "%s<Components>%u</Components>\n", indent,
		    vol->v_disks_count);
		sbuf_printf(sb, "%s<Strip>%u</Strip>\n", indent,
		    vol->v_strip_size);
		sbuf_printf(sb, "%s<State>%s</State>\n", indent,
		    g_raid_volume_state2str(vol->v_state));
		sbuf_printf(sb, "%s<Dirty>%s</Dirty>\n", indent,
		    vol->v_dirty ? "Yes" : "No");
		sbuf_printf(sb, "%s<Subdisks>", indent);
		for (i = 0; i < vol->v_disks_count; i++) {
			sd = &vol->v_subdisks[i];
			if (sd->sd_disk != NULL &&
			    sd->sd_disk->d_consumer != NULL) {
				sbuf_printf(sb, "%s ",
				    g_raid_get_diskname(sd->sd_disk));
			} else {
				sbuf_printf(sb, "NONE ");
			}
			sbuf_printf(sb, "(%s",
			    g_raid_subdisk_state2str(sd->sd_state));
			if (sd->sd_state == G_RAID_SUBDISK_S_REBUILD ||
			    sd->sd_state == G_RAID_SUBDISK_S_RESYNC) {
				sbuf_printf(sb, " %d%%",
				    (int)(sd->sd_rebuild_pos * 100 /
				     sd->sd_size));
			}
			sbuf_printf(sb, ")");
			if (i + 1 < vol->v_disks_count)
				sbuf_printf(sb, ", ");
		}
		sbuf_printf(sb, "</Subdisks>\n");
		sx_xunlock(&sc->sc_lock);
		g_topology_lock();
	} else if (cp != NULL) {
		disk = cp->private;
		if (disk == NULL)
			return;
		g_topology_unlock();
		sx_xlock(&sc->sc_lock);
		sbuf_printf(sb, "%s<State>%s", indent,
		    g_raid_disk_state2str(disk->d_state));
		if (!TAILQ_EMPTY(&disk->d_subdisks)) {
			sbuf_printf(sb, " (");
			TAILQ_FOREACH(sd, &disk->d_subdisks, sd_next) {
				sbuf_printf(sb, "%s",
				    g_raid_subdisk_state2str(sd->sd_state));
				if (sd->sd_state == G_RAID_SUBDISK_S_REBUILD ||
				    sd->sd_state == G_RAID_SUBDISK_S_RESYNC) {
					sbuf_printf(sb, " %d%%",
					    (int)(sd->sd_rebuild_pos * 100 /
					     sd->sd_size));
				}
				if (TAILQ_NEXT(sd, sd_next))
					sbuf_printf(sb, ", ");
			}
			sbuf_printf(sb, ")");
		}
		sbuf_printf(sb, "</State>\n");
		sbuf_printf(sb, "%s<Subdisks>", indent);
		TAILQ_FOREACH(sd, &disk->d_subdisks, sd_next) {
			sbuf_printf(sb, "r%d(%s):%d@%ju",
			    sd->sd_volume->v_global_id,
			    sd->sd_volume->v_name,
			    sd->sd_pos, (uintmax_t)sd->sd_offset);
			if (TAILQ_NEXT(sd, sd_next))
				sbuf_printf(sb, ", ");
		}
		sbuf_printf(sb, "</Subdisks>\n");
		sbuf_printf(sb, "%s<ReadErrors>%d</ReadErrors>\n", indent,
		    disk->d_read_errs);
		sx_xunlock(&sc->sc_lock);
		g_topology_lock();
	} else {
		g_topology_unlock();
		sx_xlock(&sc->sc_lock);
		if (sc->sc_md) {
			sbuf_printf(sb, "%s<Metadata>%s</Metadata>\n", indent,
			    sc->sc_md->mdo_class->name);
		}
		if (!TAILQ_EMPTY(&sc->sc_volumes)) {
			s = 0xff;
			TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
				if (vol->v_state < s)
					s = vol->v_state;
			}
			sbuf_printf(sb, "%s<State>%s</State>\n", indent,
			    g_raid_volume_state2str(s));
		}
		sx_xunlock(&sc->sc_lock);
		g_topology_lock();
	}
}

static void
g_raid_shutdown_post_sync(void *arg, int howto)
{
	struct g_class *mp;
	struct g_geom *gp, *gp2;
	struct g_raid_softc *sc;
	struct g_raid_volume *vol;

	mp = arg;
	g_topology_lock();
	g_raid_shutdown = 1;
	LIST_FOREACH_SAFE(gp, &mp->geom, geom, gp2) {
		if ((sc = gp->softc) == NULL)
			continue;
		g_topology_unlock();
		sx_xlock(&sc->sc_lock);
		TAILQ_FOREACH(vol, &sc->sc_volumes, v_next)
			g_raid_clean(vol, -1);
		g_cancel_event(sc);
		g_raid_destroy(sc, G_RAID_DESTROY_DELAYED);
		g_topology_lock();
	}
	g_topology_unlock();
}

static void
g_raid_init(struct g_class *mp)
{

	g_raid_post_sync = EVENTHANDLER_REGISTER(shutdown_post_sync,
	    g_raid_shutdown_post_sync, mp, SHUTDOWN_PRI_FIRST);
	if (g_raid_post_sync == NULL)
		G_RAID_DEBUG(0, "Warning! Cannot register shutdown event.");
	g_raid_started = 1;
}

static void
g_raid_fini(struct g_class *mp)
{

	if (g_raid_post_sync != NULL)
		EVENTHANDLER_DEREGISTER(shutdown_post_sync, g_raid_post_sync);
	g_raid_started = 0;
}

int
g_raid_md_modevent(module_t mod, int type, void *arg)
{
	struct g_raid_md_class *class, *c, *nc;
	int error;

	error = 0;
	class = arg;
	switch (type) {
	case MOD_LOAD:
		c = LIST_FIRST(&g_raid_md_classes);
		if (c == NULL || c->mdc_priority > class->mdc_priority)
			LIST_INSERT_HEAD(&g_raid_md_classes, class, mdc_list);
		else {
			while ((nc = LIST_NEXT(c, mdc_list)) != NULL &&
			    nc->mdc_priority < class->mdc_priority)
				c = nc;
			LIST_INSERT_AFTER(c, class, mdc_list);
		}
		if (g_raid_started)
			g_retaste(&g_raid_class);
		break;
	case MOD_UNLOAD:
		LIST_REMOVE(class, mdc_list);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

int
g_raid_tr_modevent(module_t mod, int type, void *arg)
{
	struct g_raid_tr_class *class, *c, *nc;
	int error;

	error = 0;
	class = arg;
	switch (type) {
	case MOD_LOAD:
		c = LIST_FIRST(&g_raid_tr_classes);
		if (c == NULL || c->trc_priority > class->trc_priority)
			LIST_INSERT_HEAD(&g_raid_tr_classes, class, trc_list);
		else {
			while ((nc = LIST_NEXT(c, trc_list)) != NULL &&
			    nc->trc_priority < class->trc_priority)
				c = nc;
			LIST_INSERT_AFTER(c, class, trc_list);
		}
		break;
	case MOD_UNLOAD:
		LIST_REMOVE(class, trc_list);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

/*
 * Use local implementation of DECLARE_GEOM_CLASS(g_raid_class, g_raid)
 * to reduce module priority, allowing submodules to register them first.
 */
static moduledata_t g_raid_mod = {
	"g_raid",
	g_modevent,
	&g_raid_class
};
DECLARE_MODULE(g_raid, g_raid_mod, SI_SUB_DRIVERS, SI_ORDER_THIRD);
MODULE_VERSION(geom_raid, 0);
