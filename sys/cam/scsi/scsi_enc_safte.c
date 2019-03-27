/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Matthew Jacob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
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

#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/sx.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>

#include <cam/scsi/scsi_enc.h>
#include <cam/scsi/scsi_enc_internal.h>
#include <cam/scsi/scsi_message.h>

/*
 * SAF-TE Type Device Emulation
 */

static int safte_set_enc_status(enc_softc_t *enc, uint8_t encstat, int slpflag);

#define	ALL_ENC_STAT (SES_ENCSTAT_CRITICAL | SES_ENCSTAT_UNRECOV | \
	SES_ENCSTAT_NONCRITICAL | SES_ENCSTAT_INFO)
/*
 * SAF-TE specific defines- Mandatory ones only...
 */

/*
 * READ BUFFER ('get' commands) IDs- placed in offset 2 of cdb
 */
#define	SAFTE_RD_RDCFG	0x00	/* read enclosure configuration */
#define	SAFTE_RD_RDESTS	0x01	/* read enclosure status */
#define	SAFTE_RD_RDDSTS	0x04	/* read drive slot status */
#define	SAFTE_RD_RDGFLG	0x05	/* read global flags */

/*
 * WRITE BUFFER ('set' commands) IDs- placed in offset 0 of databuf
 */
#define	SAFTE_WT_DSTAT	0x10	/* write device slot status */
#define	SAFTE_WT_SLTOP	0x12	/* perform slot operation */
#define	SAFTE_WT_FANSPD	0x13	/* set fan speed */
#define	SAFTE_WT_ACTPWS	0x14	/* turn on/off power supply */
#define	SAFTE_WT_GLOBAL	0x15	/* send global command */

#define	SAFT_SCRATCH	64
#define	SCSZ		0x8000

typedef enum {
	SAFTE_UPDATE_NONE,
	SAFTE_UPDATE_READCONFIG,
	SAFTE_UPDATE_READGFLAGS,
	SAFTE_UPDATE_READENCSTATUS,
	SAFTE_UPDATE_READSLOTSTATUS,
	SAFTE_PROCESS_CONTROL_REQS,
	SAFTE_NUM_UPDATE_STATES
} safte_update_action;

static fsm_fill_handler_t safte_fill_read_buf_io;
static fsm_fill_handler_t safte_fill_control_request;
static fsm_done_handler_t safte_process_config;
static fsm_done_handler_t safte_process_gflags;
static fsm_done_handler_t safte_process_status;
static fsm_done_handler_t safte_process_slotstatus;
static fsm_done_handler_t safte_process_control_request;

static struct enc_fsm_state enc_fsm_states[SAFTE_NUM_UPDATE_STATES] =
{
	{ "SAFTE_UPDATE_NONE", 0, 0, 0, NULL, NULL, NULL },
	{
		"SAFTE_UPDATE_READCONFIG",
		SAFTE_RD_RDCFG,
		SAFT_SCRATCH,
		60 * 1000,
		safte_fill_read_buf_io,
		safte_process_config,
		enc_error
	},
	{
		"SAFTE_UPDATE_READGFLAGS",
		SAFTE_RD_RDGFLG,
		16,
		60 * 1000,
		safte_fill_read_buf_io,
		safte_process_gflags,
		enc_error
	},
	{
		"SAFTE_UPDATE_READENCSTATUS",
		SAFTE_RD_RDESTS,
		SCSZ,
		60 * 1000,
		safte_fill_read_buf_io,
		safte_process_status,
		enc_error
	},
	{
		"SAFTE_UPDATE_READSLOTSTATUS",
		SAFTE_RD_RDDSTS,
		SCSZ,
		60 * 1000,
		safte_fill_read_buf_io,
		safte_process_slotstatus,
		enc_error
	},
	{
		"SAFTE_PROCESS_CONTROL_REQS",
		0,
		SCSZ,
		60 * 1000,
		safte_fill_control_request,
		safte_process_control_request,
		enc_error
	}
};

typedef struct safte_control_request {
	int	elm_idx;
	uint8_t	elm_stat[4];
	int	result;
	TAILQ_ENTRY(safte_control_request) links;
} safte_control_request_t;
TAILQ_HEAD(safte_control_reqlist, safte_control_request);
typedef struct safte_control_reqlist safte_control_reqlist_t;
enum {
	SES_SETSTATUS_ENC_IDX = -1
};

static void
safte_terminate_control_requests(safte_control_reqlist_t *reqlist, int result)
{
	safte_control_request_t *req;

	while ((req = TAILQ_FIRST(reqlist)) != NULL) {
		TAILQ_REMOVE(reqlist, req, links);
		req->result = result;
		wakeup(req);
	}
}

struct scfg {
	/*
	 * Cached Configuration
	 */
	uint8_t	Nfans;		/* Number of Fans */
	uint8_t	Npwr;		/* Number of Power Supplies */
	uint8_t	Nslots;		/* Number of Device Slots */
	uint8_t	DoorLock;	/* Door Lock Installed */
	uint8_t	Ntherm;		/* Number of Temperature Sensors */
	uint8_t	Nspkrs;		/* Number of Speakers */
	uint8_t	Ntstats;	/* Number of Thermostats */
	/*
	 * Cached Flag Bytes for Global Status
	 */
	uint8_t	flag1;
	uint8_t	flag2;
	/*
	 * What object index ID is where various slots start.
	 */
	uint8_t	pwroff;
	uint8_t	slotoff;
#define	SAFT_ALARM_OFFSET(cc)	(cc)->slotoff - 1

	encioc_enc_status_t	adm_status;
	encioc_enc_status_t	enc_status;
	encioc_enc_status_t	slot_status;

	safte_control_reqlist_t	requests;
	safte_control_request_t	*current_request;
	int			current_request_stage;
	int			current_request_stages;
};

#define	SAFT_FLG1_ALARM		0x1
#define	SAFT_FLG1_GLOBFAIL	0x2
#define	SAFT_FLG1_GLOBWARN	0x4
#define	SAFT_FLG1_ENCPWROFF	0x8
#define	SAFT_FLG1_ENCFANFAIL	0x10
#define	SAFT_FLG1_ENCPWRFAIL	0x20
#define	SAFT_FLG1_ENCDRVFAIL	0x40
#define	SAFT_FLG1_ENCDRVWARN	0x80

#define	SAFT_FLG2_LOCKDOOR	0x4
#define	SAFT_PRIVATE		sizeof (struct scfg)

static char *safte_2little = "Too Little Data Returned (%d) at line %d\n";
#define	SAFT_BAIL(r, x)	\
	if ((r) >= (x)) { \
		ENC_VLOG(enc, safte_2little, x, __LINE__);\
		return (EIO); \
	}

int emulate_array_devices = 1;
SYSCTL_DECL(_kern_cam_enc);
SYSCTL_INT(_kern_cam_enc, OID_AUTO, emulate_array_devices, CTLFLAG_RWTUN,
           &emulate_array_devices, 0, "Emulate Array Devices for SAF-TE");

static int
safte_fill_read_buf_io(enc_softc_t *enc, struct enc_fsm_state *state,
		       union ccb *ccb, uint8_t *buf)
{

	if (state->page_code != SAFTE_RD_RDCFG &&
	    enc->enc_cache.nelms == 0) {
		enc_update_request(enc, SAFTE_UPDATE_READCONFIG);
		return (-1);
	}

	if (enc->enc_type == ENC_SEMB_SAFT) {
		semb_read_buffer(&ccb->ataio, /*retries*/5,
				NULL, MSG_SIMPLE_Q_TAG,
				state->page_code, buf, state->buf_size,
				state->timeout);
	} else {
		scsi_read_buffer(&ccb->csio, /*retries*/5,
				NULL, MSG_SIMPLE_Q_TAG, 1,
				state->page_code, 0, buf, state->buf_size,
				SSD_FULL_SIZE, state->timeout);
	}
	return (0);
}

static int
safte_process_config(enc_softc_t *enc, struct enc_fsm_state *state,
    union ccb *ccb, uint8_t **bufp, int error, int xfer_len)
{
	struct scfg *cfg;
	uint8_t *buf = *bufp;
	int i, r;

	cfg = enc->enc_private;
	if (cfg == NULL)
		return (ENXIO);
	if (error != 0)
		return (error);
	if (xfer_len < 6) {
		ENC_VLOG(enc, "too little data (%d) for configuration\n",
		    xfer_len);
		return (EIO);
	}
	cfg->Nfans = buf[0];
	cfg->Npwr = buf[1];
	cfg->Nslots = buf[2];
	cfg->DoorLock = buf[3];
	cfg->Ntherm = buf[4];
	cfg->Nspkrs = buf[5];
	if (xfer_len >= 7)
		cfg->Ntstats = buf[6] & 0x0f;
	else
		cfg->Ntstats = 0;
	ENC_VLOG(enc, "Nfans %d Npwr %d Nslots %d Lck %d Ntherm %d Nspkrs %d "
	    "Ntstats %d\n",
	    cfg->Nfans, cfg->Npwr, cfg->Nslots, cfg->DoorLock, cfg->Ntherm,
	    cfg->Nspkrs, cfg->Ntstats);

	enc->enc_cache.nelms = cfg->Nfans + cfg->Npwr + cfg->Nslots +
	    cfg->DoorLock + cfg->Ntherm + cfg->Nspkrs + cfg->Ntstats + 1;
	ENC_FREE_AND_NULL(enc->enc_cache.elm_map);
	enc->enc_cache.elm_map =
	    malloc(enc->enc_cache.nelms * sizeof(enc_element_t),
	    M_SCSIENC, M_WAITOK|M_ZERO);

	r = 0;
	/*
	 * Note that this is all arranged for the convenience
	 * in later fetches of status.
	 */
	for (i = 0; i < cfg->Nfans; i++)
		enc->enc_cache.elm_map[r++].enctype = ELMTYP_FAN;
	cfg->pwroff = (uint8_t) r;
	for (i = 0; i < cfg->Npwr; i++)
		enc->enc_cache.elm_map[r++].enctype = ELMTYP_POWER;
	for (i = 0; i < cfg->DoorLock; i++)
		enc->enc_cache.elm_map[r++].enctype = ELMTYP_DOORLOCK;
	if (cfg->Nspkrs > 0)
		enc->enc_cache.elm_map[r++].enctype = ELMTYP_ALARM;
	for (i = 0; i < cfg->Ntherm; i++)
		enc->enc_cache.elm_map[r++].enctype = ELMTYP_THERM;
	for (i = 0; i <= cfg->Ntstats; i++)
		enc->enc_cache.elm_map[r++].enctype = ELMTYP_THERM;
	cfg->slotoff = (uint8_t) r;
	for (i = 0; i < cfg->Nslots; i++)
		enc->enc_cache.elm_map[r++].enctype =
		    emulate_array_devices ? ELMTYP_ARRAY_DEV :
		     ELMTYP_DEVICE;

	enc_update_request(enc, SAFTE_UPDATE_READGFLAGS);
	enc_update_request(enc, SAFTE_UPDATE_READENCSTATUS);
	enc_update_request(enc, SAFTE_UPDATE_READSLOTSTATUS);

	return (0);
}

static int
safte_process_gflags(enc_softc_t *enc, struct enc_fsm_state *state,
    union ccb *ccb, uint8_t **bufp, int error, int xfer_len)
{
	struct scfg *cfg;
	uint8_t *buf = *bufp;

	cfg = enc->enc_private;
	if (cfg == NULL)
		return (ENXIO);
	if (error != 0)
		return (error);
	SAFT_BAIL(3, xfer_len);
	cfg->flag1 = buf[1];
	cfg->flag2 = buf[2];

	cfg->adm_status = 0;
	if (cfg->flag1 & SAFT_FLG1_GLOBFAIL)
		cfg->adm_status |= SES_ENCSTAT_CRITICAL;
	else if (cfg->flag1 & SAFT_FLG1_GLOBWARN)
		cfg->adm_status |= SES_ENCSTAT_NONCRITICAL;

	return (0);
}

static int
safte_process_status(enc_softc_t *enc, struct enc_fsm_state *state,
    union ccb *ccb, uint8_t **bufp, int error, int xfer_len)
{
	struct scfg *cfg;
	uint8_t *buf = *bufp;
	int oid, r, i, nitems;
	uint16_t tempflags;
	enc_cache_t *cache = &enc->enc_cache;

	cfg = enc->enc_private;
	if (cfg == NULL)
		return (ENXIO);
	if (error != 0)
		return (error);

	oid = r = 0;
	cfg->enc_status = 0;

	for (nitems = i = 0; i < cfg->Nfans; i++) {
		SAFT_BAIL(r, xfer_len);
		/*
		 * 0 = Fan Operational
		 * 1 = Fan is malfunctioning
		 * 2 = Fan is not present
		 * 0x80 = Unknown or Not Reportable Status
		 */
		cache->elm_map[oid].encstat[1] = 0;	/* resvd */
		cache->elm_map[oid].encstat[2] = 0;	/* resvd */
		if (cfg->flag1 & SAFT_FLG1_ENCFANFAIL)
			cache->elm_map[oid].encstat[3] |= 0x40;
		else
			cache->elm_map[oid].encstat[3] &= ~0x40;
		switch ((int)buf[r]) {
		case 0:
			nitems++;
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_OK;
			if ((cache->elm_map[oid].encstat[3] & 0x37) == 0)
				cache->elm_map[oid].encstat[3] |= 0x27;
			break;

		case 1:
			cache->elm_map[oid].encstat[0] =
			    SES_OBJSTAT_CRIT;
			/*
			 * FAIL and FAN STOPPED synthesized
			 */
			cache->elm_map[oid].encstat[3] |= 0x10;
			cache->elm_map[oid].encstat[3] &= ~0x07;
			/*
			 * Enclosure marked with CRITICAL error
			 * if only one fan or no thermometers,
			 * else the NONCRITICAL error is set.
			 */
			if (cfg->Nfans == 1 || (cfg->Ntherm + cfg->Ntstats) == 0)
				cfg->enc_status |= SES_ENCSTAT_CRITICAL;
			else
				cfg->enc_status |= SES_ENCSTAT_NONCRITICAL;
			break;
		case 2:
			cache->elm_map[oid].encstat[0] =
			    SES_OBJSTAT_NOTINSTALLED;
			cache->elm_map[oid].encstat[3] |= 0x10;
			cache->elm_map[oid].encstat[3] &= ~0x07;
			/*
			 * Enclosure marked with CRITICAL error
			 * if only one fan or no thermometers,
			 * else the NONCRITICAL error is set.
			 */
			if (cfg->Nfans == 1)
				cfg->enc_status |= SES_ENCSTAT_CRITICAL;
			else
				cfg->enc_status |= SES_ENCSTAT_NONCRITICAL;
			break;
		case 0x80:
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_UNKNOWN;
			cache->elm_map[oid].encstat[3] = 0;
			cfg->enc_status |= SES_ENCSTAT_INFO;
			break;
		default:
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_UNSUPPORTED;
			ENC_VLOG(enc, "Unknown fan%d status 0x%x\n", i,
			    buf[r] & 0xff);
			break;
		}
		cache->elm_map[oid++].svalid = 1;
		r++;
	}

	/*
	 * No matter how you cut it, no cooling elements when there
	 * should be some there is critical.
	 */
	if (cfg->Nfans && nitems == 0)
		cfg->enc_status |= SES_ENCSTAT_CRITICAL;

	for (i = 0; i < cfg->Npwr; i++) {
		SAFT_BAIL(r, xfer_len);
		cache->elm_map[oid].encstat[0] = SES_OBJSTAT_UNKNOWN;
		cache->elm_map[oid].encstat[1] = 0;	/* resvd */
		cache->elm_map[oid].encstat[2] = 0;	/* resvd */
		cache->elm_map[oid].encstat[3] = 0x20;	/* requested on */
		switch (buf[r]) {
		case 0x00:	/* pws operational and on */
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_OK;
			break;
		case 0x01:	/* pws operational and off */
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_OK;
			cache->elm_map[oid].encstat[3] = 0x10;
			cfg->enc_status |= SES_ENCSTAT_INFO;
			break;
		case 0x10:	/* pws is malfunctioning and commanded on */
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_CRIT;
			cache->elm_map[oid].encstat[3] = 0x61;
			cfg->enc_status |= SES_ENCSTAT_NONCRITICAL;
			break;

		case 0x11:	/* pws is malfunctioning and commanded off */
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_NONCRIT;
			cache->elm_map[oid].encstat[3] = 0x51;
			cfg->enc_status |= SES_ENCSTAT_NONCRITICAL;
			break;
		case 0x20:	/* pws is not present */
			cache->elm_map[oid].encstat[0] =
			    SES_OBJSTAT_NOTINSTALLED;
			cache->elm_map[oid].encstat[3] = 0;
			cfg->enc_status |= SES_ENCSTAT_INFO;
			break;
		case 0x21:	/* pws is present */
			/*
			 * This is for enclosures that cannot tell whether the
			 * device is on or malfunctioning, but know that it is
			 * present. Just fall through.
			 */
			/* FALLTHROUGH */
		case 0x80:	/* Unknown or Not Reportable Status */
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_UNKNOWN;
			cache->elm_map[oid].encstat[3] = 0;
			cfg->enc_status |= SES_ENCSTAT_INFO;
			break;
		default:
			ENC_VLOG(enc, "unknown power supply %d status (0x%x)\n",
			    i, buf[r] & 0xff);
			break;
		}
		enc->enc_cache.elm_map[oid++].svalid = 1;
		r++;
	}

	/*
	 * Copy Slot SCSI IDs
	 */
	for (i = 0; i < cfg->Nslots; i++) {
		SAFT_BAIL(r, xfer_len);
		if (cache->elm_map[cfg->slotoff + i].enctype == ELMTYP_DEVICE)
			cache->elm_map[cfg->slotoff + i].encstat[1] = buf[r];
		r++;
	}

	/*
	 * We always have doorlock status, no matter what,
	 * but we only save the status if we have one.
	 */
	SAFT_BAIL(r, xfer_len);
	if (cfg->DoorLock) {
		/*
		 * 0 = Door Locked
		 * 1 = Door Unlocked, or no Lock Installed
		 * 0x80 = Unknown or Not Reportable Status
		 */
		cache->elm_map[oid].encstat[1] = 0;
		cache->elm_map[oid].encstat[2] = 0;
		switch (buf[r]) {
		case 0:
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_OK;
			cache->elm_map[oid].encstat[3] = 0;
			break;
		case 1:
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_OK;
			cache->elm_map[oid].encstat[3] = 1;
			break;
		case 0x80:
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_UNKNOWN;
			cache->elm_map[oid].encstat[3] = 0;
			cfg->enc_status |= SES_ENCSTAT_INFO;
			break;
		default:
			cache->elm_map[oid].encstat[0] =
			    SES_OBJSTAT_UNSUPPORTED;
			ENC_VLOG(enc, "unknown lock status 0x%x\n",
			    buf[r] & 0xff);
			break;
		}
		cache->elm_map[oid++].svalid = 1;
	}
	r++;

	/*
	 * We always have speaker status, no matter what,
	 * but we only save the status if we have one.
	 */
	SAFT_BAIL(r, xfer_len);
	if (cfg->Nspkrs) {
		cache->elm_map[oid].encstat[0] = SES_OBJSTAT_OK;
		cache->elm_map[oid].encstat[1] = 0;
		cache->elm_map[oid].encstat[2] = 0;
		if (buf[r] == 0) {
			cache->elm_map[oid].encstat[0] |= SESCTL_DISABLE;
			cache->elm_map[oid].encstat[3] |= 0x40;
		}
		cache->elm_map[oid++].svalid = 1;
	}
	r++;

	/*
	 * Now, for "pseudo" thermometers, we have two bytes
	 * of information in enclosure status- 16 bits. Actually,
	 * the MSB is a single TEMP ALERT flag indicating whether
	 * any other bits are set, but, thanks to fuzzy thinking,
	 * in the SAF-TE spec, this can also be set even if no
	 * other bits are set, thus making this really another
	 * binary temperature sensor.
	 */

	SAFT_BAIL(r + cfg->Ntherm, xfer_len);
	tempflags = buf[r + cfg->Ntherm];
	SAFT_BAIL(r + cfg->Ntherm + 1, xfer_len);
	tempflags |= (tempflags << 8) | buf[r + cfg->Ntherm + 1];

	for (i = 0; i < cfg->Ntherm; i++) {
		SAFT_BAIL(r, xfer_len);
		/*
		 * Status is a range from -10 to 245 deg Celsius,
		 * which we need to normalize to -20 to -245 according
		 * to the latest SCSI spec, which makes little
		 * sense since this would overflow an 8bit value.
		 * Well, still, the base normalization is -20,
		 * not -10, so we have to adjust.
		 *
		 * So what's over and under temperature?
		 * Hmm- we'll state that 'normal' operating
		 * is 10 to 40 deg Celsius.
		 */

		/*
		 * Actually.... All of the units that people out in the world
		 * seem to have do not come even close to setting a value that
		 * complies with this spec.
		 *
		 * The closest explanation I could find was in an
		 * LSI-Logic manual, which seemed to indicate that
		 * this value would be set by whatever the I2C code
		 * would interpolate from the output of an LM75
		 * temperature sensor.
		 *
		 * This means that it is impossible to use the actual
		 * numeric value to predict anything. But we don't want
		 * to lose the value. So, we'll propagate the *uncorrected*
		 * value and set SES_OBJSTAT_NOTAVAIL. We'll depend on the
		 * temperature flags for warnings.
		 */
		if (tempflags & (1 << i)) {
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_CRIT;
			cfg->enc_status |= SES_ENCSTAT_CRITICAL;
		} else
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_OK;
		cache->elm_map[oid].encstat[1] = 0;
		cache->elm_map[oid].encstat[2] = buf[r];
		cache->elm_map[oid].encstat[3] = 0;
		cache->elm_map[oid++].svalid = 1;
		r++;
	}

	for (i = 0; i <= cfg->Ntstats; i++) {
		cache->elm_map[oid].encstat[1] = 0;
		if (tempflags & (1 <<
		    ((i == cfg->Ntstats) ? 15 : (cfg->Ntherm + i)))) {
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_CRIT;
			cache->elm_map[4].encstat[2] = 0xff;
			/*
			 * Set 'over temperature' failure.
			 */
			cache->elm_map[oid].encstat[3] = 8;
			cfg->enc_status |= SES_ENCSTAT_CRITICAL;
		} else {
			/*
			 * We used to say 'not available' and synthesize a
			 * nominal 30 deg (C)- that was wrong. Actually,
			 * Just say 'OK', and use the reserved value of
			 * zero.
			 */
			if ((cfg->Ntherm + cfg->Ntstats) == 0)
				cache->elm_map[oid].encstat[0] =
				    SES_OBJSTAT_NOTAVAIL;
			else
				cache->elm_map[oid].encstat[0] =
				    SES_OBJSTAT_OK;
			cache->elm_map[oid].encstat[2] = 0;
			cache->elm_map[oid].encstat[3] = 0;
		}
		cache->elm_map[oid++].svalid = 1;
	}
	r += 2;

	cache->enc_status =
	    cfg->enc_status | cfg->slot_status | cfg->adm_status;
	return (0);
}

static int
safte_process_slotstatus(enc_softc_t *enc, struct enc_fsm_state *state,
    union ccb *ccb, uint8_t **bufp, int error, int xfer_len)
{
	struct scfg *cfg;
	uint8_t *buf = *bufp;
	enc_cache_t *cache = &enc->enc_cache;
	int oid, r, i;

	cfg = enc->enc_private;
	if (cfg == NULL)
		return (ENXIO);
	if (error != 0)
		return (error);
	cfg->slot_status = 0;
	oid = cfg->slotoff;
	for (r = i = 0; i < cfg->Nslots; i++, r += 4) {
		SAFT_BAIL(r+3, xfer_len);
		if (cache->elm_map[oid].enctype == ELMTYP_ARRAY_DEV)
			cache->elm_map[oid].encstat[1] = 0;
		cache->elm_map[oid].encstat[2] &= SESCTL_RQSID;
		cache->elm_map[oid].encstat[3] = 0;
		if ((buf[r+3] & 0x01) == 0) {	/* no device */
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_NOTINSTALLED;
		} else if (buf[r+0] & 0x02) {
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_CRIT;
			cfg->slot_status |= SES_ENCSTAT_CRITICAL;
		} else if (buf[r+0] & 0x40) {
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_NONCRIT;
			cfg->slot_status |= SES_ENCSTAT_NONCRITICAL;
		} else {
			cache->elm_map[oid].encstat[0] = SES_OBJSTAT_OK;
		}
		if (buf[r+3] & 0x2) {
			if (buf[r+3] & 0x01)
				cache->elm_map[oid].encstat[2] |= SESCTL_RQSRMV;
			else
				cache->elm_map[oid].encstat[2] |= SESCTL_RQSINS;
		}
		if ((buf[r+3] & 0x04) == 0)
			cache->elm_map[oid].encstat[3] |= SESCTL_DEVOFF;
		if (buf[r+0] & 0x02)
			cache->elm_map[oid].encstat[3] |= SESCTL_RQSFLT;
		if (buf[r+0] & 0x40)
			cache->elm_map[oid].encstat[0] |= SESCTL_PRDFAIL;
		if (cache->elm_map[oid].enctype == ELMTYP_ARRAY_DEV) {
			if (buf[r+0] & 0x01)
				cache->elm_map[oid].encstat[1] |= 0x80;
			if (buf[r+0] & 0x04)
				cache->elm_map[oid].encstat[1] |= 0x02;
			if (buf[r+0] & 0x08)
				cache->elm_map[oid].encstat[1] |= 0x04;
			if (buf[r+0] & 0x10)
				cache->elm_map[oid].encstat[1] |= 0x08;
			if (buf[r+0] & 0x20)
				cache->elm_map[oid].encstat[1] |= 0x10;
			if (buf[r+1] & 0x01)
				cache->elm_map[oid].encstat[1] |= 0x20;
			if (buf[r+1] & 0x02)
				cache->elm_map[oid].encstat[1] |= 0x01;
		}
		cache->elm_map[oid++].svalid = 1;
	}

	cache->enc_status =
	    cfg->enc_status | cfg->slot_status | cfg->adm_status;
	return (0);
}

static int
safte_fill_control_request(enc_softc_t *enc, struct enc_fsm_state *state,
		       union ccb *ccb, uint8_t *buf)
{
	struct scfg *cfg;
	enc_element_t *ep, *ep1;
	safte_control_request_t *req;
	int i, idx, xfer_len;

	cfg = enc->enc_private;
	if (cfg == NULL)
		return (ENXIO);

	if (enc->enc_cache.nelms == 0) {
		enc_update_request(enc, SAFTE_UPDATE_READCONFIG);
		return (-1);
	}

	if (cfg->current_request == NULL) {
		cfg->current_request = TAILQ_FIRST(&cfg->requests);
		TAILQ_REMOVE(&cfg->requests, cfg->current_request, links);
		cfg->current_request_stage = 0;
		cfg->current_request_stages = 1;
	}
	req = cfg->current_request;

	idx = (int)req->elm_idx;
	if (req->elm_idx == SES_SETSTATUS_ENC_IDX) {
		cfg->adm_status = req->elm_stat[0] & ALL_ENC_STAT;
		cfg->flag1 &= ~(SAFT_FLG1_GLOBFAIL|SAFT_FLG1_GLOBWARN);
		if (req->elm_stat[0] & (SES_ENCSTAT_CRITICAL|SES_ENCSTAT_UNRECOV))
			cfg->flag1 |= SAFT_FLG1_GLOBFAIL;
		else if (req->elm_stat[0] & SES_ENCSTAT_NONCRITICAL)
			cfg->flag1 |= SAFT_FLG1_GLOBWARN;
		buf[0] = SAFTE_WT_GLOBAL;
		buf[1] = cfg->flag1;
		buf[2] = cfg->flag2;
		buf[3] = 0;
		xfer_len = 16;
	} else {
		ep = &enc->enc_cache.elm_map[idx];

		switch (ep->enctype) {
		case ELMTYP_DEVICE:
		case ELMTYP_ARRAY_DEV:
			switch (cfg->current_request_stage) {
			case 0:
				ep->priv = 0;
				if (req->elm_stat[0] & SESCTL_PRDFAIL)
					ep->priv |= 0x40;
				if (req->elm_stat[3] & SESCTL_RQSFLT)
					ep->priv |= 0x02;
				if (ep->enctype == ELMTYP_ARRAY_DEV) {
					if (req->elm_stat[1] & 0x01)
						ep->priv |= 0x200;
					if (req->elm_stat[1] & 0x02)
						ep->priv |= 0x04;
					if (req->elm_stat[1] & 0x04)
						ep->priv |= 0x08;
					if (req->elm_stat[1] & 0x08)
						ep->priv |= 0x10;
					if (req->elm_stat[1] & 0x10)
						ep->priv |= 0x20;
					if (req->elm_stat[1] & 0x20)
						ep->priv |= 0x100;
					if (req->elm_stat[1] & 0x80)
						ep->priv |= 0x01;
				}
				if (ep->priv == 0)
					ep->priv |= 0x01;	/* no errors */

				buf[0] = SAFTE_WT_DSTAT;
				for (i = 0; i < cfg->Nslots; i++) {
					ep1 = &enc->enc_cache.elm_map[cfg->slotoff + i];
					buf[1 + (3 * i)] = ep1->priv;
					buf[2 + (3 * i)] = ep1->priv >> 8;
				}
				xfer_len = cfg->Nslots * 3 + 1;
#define DEVON(x)	(!(((x)[2] & SESCTL_RQSINS) |	\
			   ((x)[2] & SESCTL_RQSRMV) |	\
			   ((x)[3] & SESCTL_DEVOFF)))
				if (DEVON(req->elm_stat) != DEVON(ep->encstat))
					cfg->current_request_stages++;
#define IDON(x)		(!!((x)[2] & SESCTL_RQSID))
				if (IDON(req->elm_stat) != IDON(ep->encstat))
					cfg->current_request_stages++;
				break;
			case 1:
			case 2:
				buf[0] = SAFTE_WT_SLTOP;
				buf[1] = idx - cfg->slotoff;
				if (cfg->current_request_stage == 1 &&
				    DEVON(req->elm_stat) != DEVON(ep->encstat)) {
					if (DEVON(req->elm_stat))
						buf[2] = 0x01;
					else
						buf[2] = 0x02;
				} else {
					if (IDON(req->elm_stat))
						buf[2] = 0x04;
					else
						buf[2] = 0x00;
					ep->encstat[2] &= ~SESCTL_RQSID;
					ep->encstat[2] |= req->elm_stat[2] &
					    SESCTL_RQSID;
				}
				xfer_len = 64;
				break;
			default:
				return (EINVAL);
			}
			break;
		case ELMTYP_POWER:
			cfg->current_request_stages = 2;
			switch (cfg->current_request_stage) {
			case 0:
				if (req->elm_stat[3] & SESCTL_RQSTFAIL) {
					cfg->flag1 |= SAFT_FLG1_ENCPWRFAIL;
				} else {
					cfg->flag1 &= ~SAFT_FLG1_ENCPWRFAIL;
				}
				buf[0] = SAFTE_WT_GLOBAL;
				buf[1] = cfg->flag1;
				buf[2] = cfg->flag2;
				buf[3] = 0;
				xfer_len = 16;
				break;
			case 1:
				buf[0] = SAFTE_WT_ACTPWS;
				buf[1] = idx - cfg->pwroff;
				if (req->elm_stat[3] & SESCTL_RQSTON)
					buf[2] = 0x01;
				else
					buf[2] = 0x00;
				buf[3] = 0;
				xfer_len = 16;
			default:
				return (EINVAL);
			}
			break;
		case ELMTYP_FAN:
			if ((req->elm_stat[3] & 0x7) != 0)
				cfg->current_request_stages = 2;
			switch (cfg->current_request_stage) {
			case 0:
				if (req->elm_stat[3] & SESCTL_RQSTFAIL)
					cfg->flag1 |= SAFT_FLG1_ENCFANFAIL;
				else
					cfg->flag1 &= ~SAFT_FLG1_ENCFANFAIL;
				buf[0] = SAFTE_WT_GLOBAL;
				buf[1] = cfg->flag1;
				buf[2] = cfg->flag2;
				buf[3] = 0;
				xfer_len = 16;
				break;
			case 1:
				buf[0] = SAFTE_WT_FANSPD;
				buf[1] = idx;
				if (req->elm_stat[3] & SESCTL_RQSTON) {
					if ((req->elm_stat[3] & 0x7) == 7)
						buf[2] = 4;
					else if ((req->elm_stat[3] & 0x7) >= 5)
						buf[2] = 3;
					else if ((req->elm_stat[3] & 0x7) >= 3)
						buf[2] = 2;
					else
						buf[2] = 1;
				} else
					buf[2] = 0;
				buf[3] = 0;
				xfer_len = 16;
				ep->encstat[3] = req->elm_stat[3] & 0x67;
			default:
				return (EINVAL);
			}
			break;
		case ELMTYP_DOORLOCK:
			if (req->elm_stat[3] & 0x1)
				cfg->flag2 &= ~SAFT_FLG2_LOCKDOOR;
			else
				cfg->flag2 |= SAFT_FLG2_LOCKDOOR;
			buf[0] = SAFTE_WT_GLOBAL;
			buf[1] = cfg->flag1;
			buf[2] = cfg->flag2;
			buf[3] = 0;
			xfer_len = 16;
			break;
		case ELMTYP_ALARM:
			if ((req->elm_stat[0] & SESCTL_DISABLE) ||
			    (req->elm_stat[3] & 0x40)) {
				cfg->flag2 &= ~SAFT_FLG1_ALARM;
			} else if ((req->elm_stat[3] & 0x0f) != 0) {
				cfg->flag2 |= SAFT_FLG1_ALARM;
			} else {
				cfg->flag2 &= ~SAFT_FLG1_ALARM;
			}
			buf[0] = SAFTE_WT_GLOBAL;
			buf[1] = cfg->flag1;
			buf[2] = cfg->flag2;
			buf[3] = 0;
			xfer_len = 16;
			ep->encstat[3] = req->elm_stat[3];
			break;
		default:
			return (EINVAL);
		}
	}

	if (enc->enc_type == ENC_SEMB_SAFT) {
		semb_write_buffer(&ccb->ataio, /*retries*/5,
				NULL, MSG_SIMPLE_Q_TAG,
				buf, xfer_len, state->timeout);
	} else {
		scsi_write_buffer(&ccb->csio, /*retries*/5,
				NULL, MSG_SIMPLE_Q_TAG, 1,
				0, 0, buf, xfer_len,
				SSD_FULL_SIZE, state->timeout);
	}
	return (0);
}

static int
safte_process_control_request(enc_softc_t *enc, struct enc_fsm_state *state,
    union ccb *ccb, uint8_t **bufp, int error, int xfer_len)
{
	struct scfg *cfg;
	safte_control_request_t *req;
	int idx, type;

	cfg = enc->enc_private;
	if (cfg == NULL)
		return (ENXIO);

	req = cfg->current_request;
	if (req->result == 0)
		req->result = error;
	if (++cfg->current_request_stage >= cfg->current_request_stages) {
		idx = req->elm_idx;
		if (idx == SES_SETSTATUS_ENC_IDX)
			type = -1;
		else
			type = enc->enc_cache.elm_map[idx].enctype;
		if (type == ELMTYP_DEVICE || type == ELMTYP_ARRAY_DEV)
			enc_update_request(enc, SAFTE_UPDATE_READSLOTSTATUS);
		else
			enc_update_request(enc, SAFTE_UPDATE_READENCSTATUS);
		cfg->current_request = NULL;
		wakeup(req);
	} else {
		enc_update_request(enc, SAFTE_PROCESS_CONTROL_REQS);
	}
	return (0);
}

static void
safte_softc_invalidate(enc_softc_t *enc)
{
	struct scfg *cfg;

	cfg = enc->enc_private;
	safte_terminate_control_requests(&cfg->requests, ENXIO);
}

static void
safte_softc_cleanup(enc_softc_t *enc)
{

	ENC_FREE_AND_NULL(enc->enc_cache.elm_map);
	ENC_FREE_AND_NULL(enc->enc_private);
	enc->enc_cache.nelms = 0;
}

static int
safte_init_enc(enc_softc_t *enc)
{
	struct scfg *cfg;
	int err;
	static char cdb0[6] = { SEND_DIAGNOSTIC };

	cfg = enc->enc_private;
	if (cfg == NULL)
		return (ENXIO);

	err = enc_runcmd(enc, cdb0, 6, NULL, 0);
	if (err) {
		return (err);
	}
	DELAY(5000);
	cfg->flag1 = 0;
	cfg->flag2 = 0;
	err = safte_set_enc_status(enc, 0, 1);
	return (err);
}

static int
safte_get_enc_status(enc_softc_t *enc, int slpflg)
{

	return (0);
}

static int
safte_set_enc_status(enc_softc_t *enc, uint8_t encstat, int slpflag)
{
	struct scfg *cfg;
	safte_control_request_t req;

	cfg = enc->enc_private;
	if (cfg == NULL)
		return (ENXIO);

	req.elm_idx = SES_SETSTATUS_ENC_IDX;
	req.elm_stat[0] = encstat & 0xf;
	req.result = 0;
	
	TAILQ_INSERT_TAIL(&cfg->requests, &req, links);
	enc_update_request(enc, SAFTE_PROCESS_CONTROL_REQS);
	cam_periph_sleep(enc->periph, &req, PUSER, "encstat", 0);

	return (req.result);
}

static int
safte_get_elm_status(enc_softc_t *enc, encioc_elm_status_t *elms, int slpflg)
{
	int i = (int)elms->elm_idx;

	elms->cstat[0] = enc->enc_cache.elm_map[i].encstat[0];
	elms->cstat[1] = enc->enc_cache.elm_map[i].encstat[1];
	elms->cstat[2] = enc->enc_cache.elm_map[i].encstat[2];
	elms->cstat[3] = enc->enc_cache.elm_map[i].encstat[3];
	return (0);
}

static int
safte_set_elm_status(enc_softc_t *enc, encioc_elm_status_t *elms, int slpflag)
{
	struct scfg *cfg;
	safte_control_request_t req;

	cfg = enc->enc_private;
	if (cfg == NULL)
		return (ENXIO);

	/* If this is clear, we don't do diddly.  */
	if ((elms->cstat[0] & SESCTL_CSEL) == 0)
		return (0);

	req.elm_idx = elms->elm_idx;
	memcpy(&req.elm_stat, elms->cstat, sizeof(req.elm_stat));
	req.result = 0;

	TAILQ_INSERT_TAIL(&cfg->requests, &req, links);
	enc_update_request(enc, SAFTE_PROCESS_CONTROL_REQS);
	cam_periph_sleep(enc->periph, &req, PUSER, "encstat", 0);

	return (req.result);
}

static void
safte_poll_status(enc_softc_t *enc)
{

	enc_update_request(enc, SAFTE_UPDATE_READENCSTATUS);
	enc_update_request(enc, SAFTE_UPDATE_READSLOTSTATUS);
}

static struct enc_vec safte_enc_vec =
{
	.softc_invalidate	= safte_softc_invalidate,
	.softc_cleanup	= safte_softc_cleanup,
	.init_enc	= safte_init_enc,
	.get_enc_status	= safte_get_enc_status,
	.set_enc_status	= safte_set_enc_status,
	.get_elm_status	= safte_get_elm_status,
	.set_elm_status	= safte_set_elm_status,
	.poll_status	= safte_poll_status
};

int
safte_softc_init(enc_softc_t *enc)
{
	struct scfg *cfg;

	enc->enc_vec = safte_enc_vec;
	enc->enc_fsm_states = enc_fsm_states;

	if (enc->enc_private == NULL) {
		enc->enc_private = ENC_MALLOCZ(SAFT_PRIVATE);
		if (enc->enc_private == NULL)
			return (ENOMEM);
	}
	cfg = enc->enc_private;

	enc->enc_cache.nelms = 0;
	enc->enc_cache.enc_status = 0;

	TAILQ_INIT(&cfg->requests);
	return (0);
}

