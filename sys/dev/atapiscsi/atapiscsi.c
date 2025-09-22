/*      $OpenBSD: atapiscsi.c,v 1.122 2024/06/22 10:22:29 jsg Exp $     */

/*
 * This code is derived from code with the copyright below.
 */

/*
 * Copyright (c) 1996, 1998 Manuel Bouyer.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <scsi/scsi_all.h>
#include <scsi/scsi_tape.h>
#include <scsi/scsiconf.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/ata/atareg.h>
#include <dev/ata/atavar.h>
#include <dev/ic/wdcreg.h>
#include <dev/ic/wdcvar.h>
#include <dev/ic/wdcevent.h>

/* drive states stored in ata_drive_datas */
enum atapi_drive_states {
	ATAPI_RESET_BASE_STATE = 0,
	ATAPI_DEVICE_RESET_WAIT_STATE = 1,
	ATAPI_IDENTIFY_STATE = 2,
	ATAPI_IDENTIFY_WAIT_STATE = 3,
	ATAPI_PIOMODE_STATE = 4,
	ATAPI_PIOMODE_WAIT_STATE = 5,
	ATAPI_DMAMODE_STATE = 6,
	ATAPI_DMAMODE_WAIT_STATE = 7,
	ATAPI_READY_STATE = 8
};

#define DEBUG_INTR   0x01
#define DEBUG_XFERS  0x02
#define DEBUG_STATUS 0x04
#define DEBUG_FUNCS  0x08
#define DEBUG_PROBE  0x10
#define DEBUG_DSC    0x20
#define DEBUG_POLL   0x40
#define DEBUG_ERRORS 0x80   /* Debug error handling code */

#if defined(WDCDEBUG)
#ifndef WDCDEBUG_ATAPI_MASK
#define WDCDEBUG_ATAPI_MASK 0x00
#endif
int wdcdebug_atapi_mask = WDCDEBUG_ATAPI_MASK;
#define WDCDEBUG_PRINT(args, level) do {		\
	if ((wdcdebug_atapi_mask & (level)) != 0)	\
		printf args;				\
} while (0)
#else
#define WDCDEBUG_PRINT(args, level)
#endif

/* 10 ms, this is used only before sending a cmd.  */
#define ATAPI_DELAY 10
#define ATAPI_RESET_DELAY 1000
#define ATAPI_RESET_WAIT 2000
#define ATAPI_CTRL_WAIT 4000

/* When polling, let the exponential backoff max out at 1 second's interval. */
#define ATAPI_POLL_MAXTIC (hz)

void  wdc_atapi_start(struct channel_softc *,struct wdc_xfer *);

void  wdc_atapi_timer_handler(void *);

void  wdc_atapi_real_start(struct channel_softc *, struct wdc_xfer *,
    int, struct atapi_return_args *);
void  wdc_atapi_real_start_2(struct channel_softc *, struct wdc_xfer *,
    int, struct atapi_return_args *);
void  wdc_atapi_intr_command(struct channel_softc *, struct wdc_xfer *,
    int, struct atapi_return_args *);
void  wdc_atapi_intr_data(struct channel_softc *, struct wdc_xfer *,
    int, struct atapi_return_args *);
void  wdc_atapi_intr_complete(struct channel_softc *, struct wdc_xfer *,
    int, struct atapi_return_args *);
void  wdc_atapi_pio_intr(struct channel_softc *, struct wdc_xfer *,
    int, struct atapi_return_args *);
void  wdc_atapi_send_packet(struct channel_softc *, struct wdc_xfer *,
    int, struct atapi_return_args *);
void  wdc_atapi_ctrl(struct channel_softc *, struct wdc_xfer *,
    int, struct atapi_return_args *);

char  *wdc_atapi_in_data_phase(struct wdc_xfer *, int, int);

int   wdc_atapi_intr(struct channel_softc *, struct wdc_xfer *, int);
void  wdc_atapi_done(struct channel_softc *, struct wdc_xfer *,
	int, struct atapi_return_args *);
void  wdc_atapi_reset(struct channel_softc *, struct wdc_xfer *,
	int, struct atapi_return_args *);
void  wdc_atapi_reset_2(struct channel_softc *, struct wdc_xfer *,
	int, struct atapi_return_args *);

void  wdc_atapi_tape_done(struct channel_softc *, struct wdc_xfer *,
	int, struct atapi_return_args *);

int	atapiscsi_match(struct device *, void *, void *);
void	atapiscsi_attach(struct device *, struct device *, void *);
int	atapiscsi_activate(struct device *, int);
int	atapiscsi_detach(struct device *, int);
int     atapi_to_scsi_sense(struct scsi_xfer *, u_int8_t);

enum atapi_state { as_none, as_data, as_completed };

struct atapiscsi_softc {
	struct device  sc_dev;
	struct channel_softc *chp;
	enum atapi_state protocol_phase;

	int drive;
};

int   wdc_atapi_ioctl(struct scsi_link *, u_long, caddr_t, int);
void  wdc_atapi_send_cmd(struct scsi_xfer *sc_xfer);

static const struct scsi_adapter atapiscsi_switch = {
	wdc_atapi_send_cmd, NULL, NULL, NULL, wdc_atapi_ioctl
};

/* Initial version shares bus_link structure so it can easily
   be "attached to current" wdc driver */

const struct cfattach atapiscsi_ca = {
	sizeof(struct atapiscsi_softc), atapiscsi_match, atapiscsi_attach,
	    atapiscsi_detach, atapiscsi_activate
};

struct cfdriver atapiscsi_cd = {
	NULL, "atapiscsi", DV_DULL
};


int
atapiscsi_match(struct device *parent, void *match, void *aux)
{
	struct ata_atapi_attach *aa_link = aux;
	struct cfdata *cf = match;

	if (aa_link == NULL)
		return (0);

	if (aa_link->aa_type != T_ATAPI)
		return (0);

	if (cf->cf_loc[0] != aa_link->aa_channel &&
	    cf->cf_loc[0] != -1)
		return (0);

	return (1);
}

void
atapiscsi_attach(struct device *parent, struct device *self, void *aux)
{
	struct atapiscsi_softc *as = (struct atapiscsi_softc *)self;
	struct ata_atapi_attach *aa_link = aux;
	struct scsibus_attach_args saa;
	struct ata_drive_datas *drvp = aa_link->aa_drv_data;
	struct channel_softc *chp = drvp->chnl_softc;
	struct ataparams *id = &drvp->id;
	struct device *child;

	extern struct scsi_iopool wdc_xfer_iopool;

	printf("\n");

	/* Initialize shared data. */
	scsi_init();

#ifdef WDCDEBUG
	if (chp->wdc->sc_dev.dv_cfdata->cf_flags & WDC_OPTION_PROBE_VERBOSE)
		wdcdebug_atapi_mask |= DEBUG_PROBE;
#endif

	as->chp = chp;
	as->drive = drvp->drive;

	strlcpy(drvp->drive_name, as->sc_dev.dv_xname,
	    sizeof(drvp->drive_name));
	drvp->cf_flags = as->sc_dev.dv_cfdata->cf_flags;

	wdc_probe_caps(drvp, id);

	WDCDEBUG_PRINT(
		("general config %04x capabilities %04x ",
		    id->atap_config, id->atap_capabilities1),
		    DEBUG_PROBE);

	if ((NERRS_MAX - 2) > 0)
		drvp->n_dmaerrs = NERRS_MAX - 2;
	else
		drvp->n_dmaerrs = 0;
	drvp->drive_flags |= DRIVE_DEVICE_RESET;

	/* Tape drives do funny DSC stuff */
	if (ATAPI_CFG_TYPE(id->atap_config) ==
	    ATAPI_CFG_TYPE_SEQUENTIAL)
		drvp->atapi_cap |= ACAP_DSC;

	if ((id->atap_config & ATAPI_CFG_CMD_MASK) ==
	    ATAPI_CFG_CMD_16)
		drvp->atapi_cap |= ACAP_LEN;

	drvp->atapi_cap |=
	    (id->atap_config & ATAPI_CFG_DRQ_MASK);

	WDCDEBUG_PRINT(("driver caps %04x\n", drvp->atapi_cap),
	    DEBUG_PROBE);


	saa.saa_adapter_softc = as;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_adapter_buswidth = 2;
	saa.saa_adapter = &atapiscsi_switch;
	saa.saa_luns = 1;
	saa.saa_openings = 1;
	saa.saa_flags = SDEV_ATAPI;
	saa.saa_pool = &wdc_xfer_iopool;
	saa.saa_quirks = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	child = config_found((struct device *)as, &saa, scsiprint);

	if (child != NULL) {
		struct scsibus_softc *scsi = (struct scsibus_softc *)child;
		struct scsi_link *link = scsi_get_link(scsi, 0, 0);

		if (link) {
			strlcpy(drvp->drive_name,
			    ((struct device *)(link->device_softc))->dv_xname,
			    sizeof(drvp->drive_name));

			wdc_print_caps(drvp);
		}
	}

#ifdef WDCDEBUG
	if (chp->wdc->sc_dev.dv_cfdata->cf_flags & WDC_OPTION_PROBE_VERBOSE)
		wdcdebug_atapi_mask &= ~DEBUG_PROBE;
#endif
}

int
atapiscsi_activate(struct device *self, int act)
{
	struct atapiscsi_softc *as = (void *)self;
 	struct channel_softc *chp = as->chp;
	struct ata_drive_datas *drvp = &chp->ch_drive[as->drive];

	switch (act) {
	case DVACT_SUSPEND:
		break;
	case DVACT_RESUME:
		/*
		 * Do two resets separated by a small delay. The
		 * first wakes the controller, the second resets
		 * the channel
		 */
		wdc_disable_intr(chp);
		wdc_reset_channel(drvp, 1);
		delay(10000);
		wdc_reset_channel(drvp, 0);
		wdc_enable_intr(chp);
		break;
	}
	return (0);
}

int
atapiscsi_detach(struct device *dev, int flags)
{
	return (config_detach_children(dev, flags));
}

void
wdc_atapi_send_cmd(struct scsi_xfer *sc_xfer)
{
	struct atapiscsi_softc *as = sc_xfer->sc_link->bus->sb_adapter_softc;
 	struct channel_softc *chp = as->chp;
	struct ata_drive_datas *drvp = &chp->ch_drive[as->drive];
	struct wdc_xfer *xfer;
	int s;
	int idx;

	WDCDEBUG_PRINT(("wdc_atapi_send_cmd %s:%d:%d start\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, as->drive), DEBUG_XFERS);

	if (sc_xfer->sc_link->target != 0) {
		sc_xfer->error = XS_DRIVER_STUFFUP;
		scsi_done(sc_xfer);
		return;
	}

	xfer = sc_xfer->io;
	wdc_scrub_xfer(xfer);
	if (sc_xfer->flags & SCSI_POLL)
		xfer->c_flags |= C_POLL;
	xfer->drive = as->drive;
	xfer->c_flags |= C_ATAPI;
	xfer->cmd = sc_xfer;
	xfer->databuf = sc_xfer->data;
	xfer->c_bcount = sc_xfer->datalen;
	xfer->c_start = wdc_atapi_start;
	xfer->c_intr = wdc_atapi_intr;

	timeout_set(&xfer->atapi_poll_to, wdc_atapi_timer_handler, chp);

	WDCDEBUG_PRINT(("wdc_atapi_send_cmd %s:%d:%d ",
	    chp->wdc->sc_dev.dv_xname, chp->channel, as->drive),
	    DEBUG_XFERS | DEBUG_ERRORS);

	for (idx = 0; idx < sc_xfer->cmdlen; idx++) {
		WDCDEBUG_PRINT((" %02x",
				   ((unsigned char *)&sc_xfer->cmd)[idx]),
		    DEBUG_XFERS | DEBUG_ERRORS);
	}
	WDCDEBUG_PRINT(("\n"), DEBUG_XFERS | DEBUG_ERRORS);

	s = splbio();

	if (drvp->atapi_cap & ACAP_DSC) {
		WDCDEBUG_PRINT(("about to send cmd 0x%x ",
		    sc_xfer->cmd.opcode), DEBUG_DSC);
		switch (sc_xfer->cmd.opcode) {
		case READ:
		case WRITE:
			xfer->c_flags |= C_MEDIA_ACCESS;

			/* If we are not in buffer availability mode,
			   we limit the first request to 0 bytes, which
			   gets us into buffer availability mode without
			   holding the bus.  */
			if (!(drvp->drive_flags & DRIVE_DSCBA)) {
				xfer->c_bcount = 0;
				xfer->transfer_len =
				  _3btol(((struct scsi_rw_tape *)
					  &sc_xfer->cmd)->len);
				_lto3b(0,
				    ((struct scsi_rw_tape *)
				    &sc_xfer->cmd)->len);
				xfer->c_done = wdc_atapi_tape_done;
				WDCDEBUG_PRINT(
				    ("R/W in completion mode, do 0 blocks\n"),
				    DEBUG_DSC);
			} else
				WDCDEBUG_PRINT(("R/W %d blocks %d bytes\n",
				    _3btol(((struct scsi_rw_tape *)
					&sc_xfer->cmd)->len),
				    sc_xfer->datalen),
				    DEBUG_DSC);

			/* DSC will change to buffer availability mode.
			   We reflect this in wdc_atapi_intr.  */
			break;

		case ERASE:		/* Media access commands */
		case LOAD:
		case REWIND:
		case SPACE:
		case WRITE_FILEMARKS:
#if 0
		case LOCATE:
		case READ_POSITION:
#endif

			xfer->c_flags |= C_MEDIA_ACCESS;
			break;

		default:
			WDCDEBUG_PRINT(("no media access\n"), DEBUG_DSC);
		}
	}

	wdc_exec_xfer(chp, xfer);
	splx(s);
}

int
wdc_atapi_ioctl(struct scsi_link *sc_link, u_long cmd, caddr_t addr, int flag)
{
	struct atapiscsi_softc *as = sc_link->bus->sb_adapter_softc;
	struct channel_softc *chp = as->chp;
	struct ata_drive_datas *drvp = &chp->ch_drive[as->drive];

	if (sc_link->target != 0)
		return ENOTTY;

	return (wdc_ioctl(drvp, cmd, addr, flag, curproc));
}


/*
 * Returns 1 if we experienced an ATA-level abort command
 *           (ABRT bit set but no additional sense)
 *         0 if normal command processing
 */
int
atapi_to_scsi_sense(struct scsi_xfer *xfer, u_int8_t flags)
{
	struct scsi_sense_data *sense = &xfer->sense;
	int ret = 0;

	xfer->error = XS_SHORTSENSE;

	sense->error_code = SSD_ERRCODE_VALID | SSD_ERRCODE_CURRENT;
	sense->flags = (flags >> 4);

	WDCDEBUG_PRINT(("Atapi error: %d ", (flags >> 4)), DEBUG_ERRORS);

	if ((flags & 4) && (sense->flags == 0)) {
		sense->flags = SKEY_ABORTED_COMMAND;
		WDCDEBUG_PRINT(("ABRT "), DEBUG_ERRORS);
		ret = 1;
	}

	if (flags & 0x1) {
		sense->flags |= SSD_ILI;
		WDCDEBUG_PRINT(("ILI "), DEBUG_ERRORS);
	}

	if (flags & 0x2) {
		sense->flags |= SSD_EOM;
		WDCDEBUG_PRINT(("EOM "), DEBUG_ERRORS);
	}

	/* Media change requested */
	/* Let's ignore these in version 1 */
	if (flags & 0x8) {
		WDCDEBUG_PRINT(("MCR "), DEBUG_ERRORS);
		if (sense->flags == 0)
			xfer->error = XS_NOERROR;
	}

	WDCDEBUG_PRINT(("\n"), DEBUG_ERRORS);
	return (ret);
}

int wdc_atapi_drive_selected(struct channel_softc *, int);

int
wdc_atapi_drive_selected(struct channel_softc *chp, int drive)
{
	u_int8_t reg = CHP_READ_REG(chp, wdr_sdh);

	WDC_LOG_REG(chp, wdr_sdh, reg);

	return ((reg & 0x10) == (drive << 4));
}

enum atapi_context {
	ctxt_process = 0,
	ctxt_timer = 1,
	ctxt_interrupt = 2
};

void wdc_atapi_the_machine(struct channel_softc *, struct wdc_xfer *,
    enum atapi_context);

void wdc_atapi_the_poll_machine(struct channel_softc *, struct wdc_xfer *);

void
wdc_atapi_start(struct channel_softc *chp, struct wdc_xfer *xfer)
{
	xfer->next = wdc_atapi_real_start;

	wdc_atapi_the_machine(chp, xfer, ctxt_process);
}


void
wdc_atapi_timer_handler(void *arg)
{
	struct channel_softc *chp = arg;
	struct wdc_xfer *xfer;
	int s;

	s = splbio();
	xfer = TAILQ_FIRST(&chp->ch_queue->sc_xfer);
	if (xfer == NULL ||
	    !timeout_triggered(&xfer->atapi_poll_to)) {
		splx(s);
		return;
	}
	xfer->c_flags &= ~C_POLL_MACHINE;
	timeout_del(&xfer->atapi_poll_to);
	chp->ch_flags &= ~WDCF_IRQ_WAIT;
	wdc_atapi_the_machine(chp, xfer, ctxt_timer);
	splx(s);
}


int
wdc_atapi_intr(struct channel_softc *chp, struct wdc_xfer *xfer, int irq)
{
	timeout_del(&chp->ch_timo);

	/* XXX we should consider an alternate signaling regime here */
	if (xfer->c_flags & C_TIMEOU) {
		xfer->c_flags &= ~C_TIMEOU;
		wdc_atapi_the_machine(chp, xfer, ctxt_timer);
		return (0);
	}

	wdc_atapi_the_machine(chp, xfer, ctxt_interrupt);

	return (-1);
}

struct atapi_return_args {
	int timeout;
	int delay;
	int expect_irq;
};

#define ARGS_INIT {-1, 0, 0}

void
wdc_atapi_the_poll_machine(struct channel_softc *chp, struct wdc_xfer *xfer)
{
	int  idx = 0;
	int  current_timeout = 10;


	while (1) {
		struct atapi_return_args retargs = ARGS_INIT;
		idx++;

		(xfer->next)(chp, xfer, (current_timeout * 1000 <= idx),
		    &retargs);

		if (xfer->next == NULL) {
			wdc_free_xfer(chp, xfer);
			wdcstart(chp);
			return;
		}

		if (retargs.timeout != -1) {
			current_timeout = retargs.timeout;
			idx = 0;
		}

		if (retargs.delay != 0) {
			delay (1000 * retargs.delay);
			idx += 1000 * retargs.delay;
		}

		DELAY(1);
	}
}


void
wdc_atapi_the_machine(struct channel_softc *chp, struct wdc_xfer *xfer,
    enum atapi_context ctxt)
{
	int idx = 0;
	extern int ticks;
	int timeout_delay = hz / 10;

	if (xfer->c_flags & C_POLL) {
		wdc_disable_intr(chp);

		if (ctxt != ctxt_process) {
			if (ctxt == ctxt_interrupt)
				xfer->endticks = 1;

			return;
		}

		wdc_atapi_the_poll_machine(chp, xfer);
		return;
	}

	/* Don't go through more than 50 state machine steps
	   before yielding. This tries to limit the amount of time
	   spent at high SPL */
	for (idx = 0; idx < 50; idx++) {
		struct atapi_return_args retargs = ARGS_INIT;

		(xfer->next)(chp, xfer,
		    xfer->endticks && (ticks - xfer->endticks >= 0),
		    &retargs);

		if (retargs.timeout != -1)
			/*
			 * Add 1 tick to compensate for the fact that we
			 * can be just microseconds before the tick changes.
			 */
			xfer->endticks =
			    max((retargs.timeout * hz) / 1000, 1) + 1 + ticks;

		if (xfer->next == NULL) {
			if (xfer->c_flags & C_POLL_MACHINE)
				timeout_del(&xfer->atapi_poll_to);

			wdc_free_xfer(chp, xfer);
			wdcstart(chp);

			return;
		}

		if (retargs.expect_irq) {
			int timeout_period;
			chp->ch_flags |= WDCF_IRQ_WAIT;
			timeout_period =  xfer->endticks - ticks;
			if (timeout_period < 1)
				timeout_period = 1;
			timeout_add(&chp->ch_timo, timeout_period);
			return;
		}

		if (retargs.delay != 0) {
			timeout_delay = max(retargs.delay * hz / 1000, 1);
			break;
		}

		DELAY(1);
	}

	timeout_add(&xfer->atapi_poll_to, timeout_delay);
	xfer->c_flags |= C_POLL_MACHINE;

	return;
}


void wdc_atapi_update_status(struct channel_softc *);

void
wdc_atapi_update_status(struct channel_softc *chp)
{
	chp->ch_status = CHP_READ_REG(chp, wdr_status);

	WDC_LOG_STATUS(chp, chp->ch_status);

	if (chp->ch_status == 0xff && (chp->ch_flags & WDCF_ONESLAVE)) {
		wdc_set_drive(chp, 1);

		chp->ch_status = CHP_READ_REG(chp, wdr_status);
		WDC_LOG_STATUS(chp, chp->ch_status);
	}

	if ((chp->ch_status & (WDCS_BSY | WDCS_ERR)) == WDCS_ERR) {
		chp->ch_error = CHP_READ_REG(chp, wdr_error);
		WDC_LOG_ERROR(chp, chp->ch_error);
	}
}

void
wdc_atapi_real_start(struct channel_softc *chp, struct wdc_xfer *xfer,
    int timeout, struct atapi_return_args *ret)
{
#ifdef WDCDEBUG
	struct scsi_xfer *sc_xfer = xfer->cmd;
#endif
	struct ata_drive_datas *drvp = &chp->ch_drive[xfer->drive];

	/*
	 * Only set the DMA flag if the transfer is reasonably large.
	 * At least one older drive failed to complete a 4 byte DMA transfer.
	 */

	/* Turn off DMA flag on REQUEST SENSE */

	if (!(xfer->c_flags & (C_POLL | C_SENSE | C_MEDIA_ACCESS)) &&
	    (drvp->drive_flags & (DRIVE_DMA | DRIVE_UDMA)) &&
	    (xfer->c_bcount > 100))
		xfer->c_flags |= C_DMA;
	else
		xfer->c_flags &= ~C_DMA;


	wdc_set_drive(chp, xfer->drive);

	DELAY(1);

	xfer->next = wdc_atapi_real_start_2;
	ret->timeout = ATAPI_DELAY;

	WDCDEBUG_PRINT(("wdc_atapi_start %s:%d:%d, scsi flags 0x%x, "
	    "ATA flags 0x%x\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, drvp->drive,
	    sc_xfer->flags, xfer->c_flags), DEBUG_XFERS);


	return;
}


void
wdc_atapi_real_start_2(struct channel_softc *chp, struct wdc_xfer *xfer,
    int timeout, struct atapi_return_args *ret)
{
	struct scsi_xfer *sc_xfer = xfer->cmd;
	struct ata_drive_datas *drvp = &chp->ch_drive[xfer->drive];

	if (timeout) {
		printf("wdc_atapi_start: not ready, st = %02x\n",
		    chp->ch_status);

		sc_xfer->error = XS_TIMEOUT;
		xfer->next = wdc_atapi_reset;
		return;
	} else {
		wdc_atapi_update_status(chp);

		if (chp->ch_status & (WDCS_BSY | WDCS_DRQ))
			return;
	}

	/* Do control operations specially. */
	if (drvp->state < ATAPI_READY_STATE) {
		xfer->next = wdc_atapi_ctrl;
		return;
	}

	xfer->next = wdc_atapi_send_packet;
	return;
}


void
wdc_atapi_send_packet(struct channel_softc *chp, struct wdc_xfer *xfer,
    int timeout, struct atapi_return_args *ret)
{
	struct scsi_xfer *sc_xfer = xfer->cmd;
	struct ata_drive_datas *drvp = &chp->ch_drive[xfer->drive];

	/*
	 * Even with WDCS_ERR, the device should accept a command packet.
	 * Limit length to what can be stuffed into the cylinder register
	 * (16 bits).  Some CD-ROMs seem to interpret '0' as 65536,
	 * but not all devices do that and it's not obvious from the
	 * ATAPI spec that this behaviour should be expected.  If more
	 * data is necessary, multiple data transfer phases will be done.
	 */

	wdccommand(chp, xfer->drive, ATAPI_PKT_CMD,
	    xfer->c_bcount <= 0xfffe ? xfer->c_bcount : 0xfffe,
	    0, 0, 0,
	    (xfer->c_flags & C_DMA) ? ATAPI_PKT_CMD_FTRE_DMA : 0);

	if (xfer->c_flags & C_DMA)
		drvp->n_xfers++;

	DELAY(1);

	xfer->next = wdc_atapi_intr_command;
	ret->timeout = sc_xfer->timeout;

	if ((drvp->atapi_cap & ATAPI_CFG_DRQ_MASK) == ATAPI_CFG_IRQ_DRQ) {
		/* We expect an IRQ to tell us of the next state */
		ret->expect_irq = 1;
	}

	WDCDEBUG_PRINT(("wdc_atapi_send_packet %s:%d:%d command sent\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, drvp->drive
	    ), DEBUG_XFERS);
	return;
}

void
wdc_atapi_intr_command(struct channel_softc *chp, struct wdc_xfer *xfer,
    int timeout, struct atapi_return_args *ret)
{
	struct scsi_xfer *sc_xfer = xfer->cmd;
	struct ata_drive_datas *drvp = &chp->ch_drive[xfer->drive];
	struct atapiscsi_softc *as = sc_xfer->sc_link->bus->sb_adapter_softc;
	int i;
	u_int8_t cmd[16];
	struct scsi_sense *cmd_reqsense;
	int cmdlen = (drvp->atapi_cap & ACAP_LEN) ? 16 : 12;
	int dma_flags = ((sc_xfer->flags & SCSI_DATA_IN) ||
	    (xfer->c_flags & C_SENSE)) ?  WDC_DMA_READ : 0;

	wdc_atapi_update_status(chp);

	if ((chp->ch_status & WDCS_BSY) || !(chp->ch_status & WDCS_DRQ)) {
		if (timeout)
			goto timeout;

		return;
	}

	if (chp->wdc->cap & WDC_CAPABILITY_IRQACK)
		chp->wdc->irqack(chp);

	bzero(cmd, sizeof(cmd));

	if (xfer->c_flags & C_SENSE) {
		cmd_reqsense = (struct scsi_sense *)&cmd[0];
		cmd_reqsense->opcode = REQUEST_SENSE;
		cmd_reqsense->length = xfer->c_bcount;
	} else
		bcopy(&sc_xfer->cmd, cmd, sc_xfer->cmdlen);

	WDC_LOG_ATAPI_CMD(chp, xfer->drive, xfer->c_flags,
	    cmdlen, cmd);

	for (i = 0; i < 12; i++)
		WDCDEBUG_PRINT(("%02x ", cmd[i]), DEBUG_INTR);
	WDCDEBUG_PRINT((": PHASE_CMDOUT\n"), DEBUG_INTR);

	/* Init the DMA channel if necessary */
	if (xfer->c_flags & C_DMA) {
		if ((*chp->wdc->dma_init)(chp->wdc->dma_arg,
		    chp->channel, xfer->drive, xfer->databuf,
		    xfer->c_bcount, dma_flags) != 0) {
			sc_xfer->error = XS_DRIVER_STUFFUP;

			xfer->next = wdc_atapi_reset;
			return;
		}
	}

	wdc_output_bytes(drvp, cmd, cmdlen);

	/* Start the DMA channel if necessary */
	if (xfer->c_flags & C_DMA) {
		(*chp->wdc->dma_start)(chp->wdc->dma_arg,
		    chp->channel, xfer->drive);
		xfer->next = wdc_atapi_intr_complete;
	} else {
		if (xfer->c_bcount == 0)
			as->protocol_phase = as_completed;
		else
			as->protocol_phase = as_data;

		xfer->next = wdc_atapi_pio_intr;
	}

	ret->expect_irq = 1;

	/* If we read/write to a tape we will get into buffer
	   availability mode.  */
	if (drvp->atapi_cap & ACAP_DSC) {
		if ((sc_xfer->cmd.opcode == READ ||
		       sc_xfer->cmd.opcode == WRITE)) {
			drvp->drive_flags |= DRIVE_DSCBA;
			WDCDEBUG_PRINT(("set DSCBA\n"), DEBUG_DSC);
		} else if ((xfer->c_flags & C_MEDIA_ACCESS) &&
		    (drvp->drive_flags & DRIVE_DSCBA)) {
			/* Clause 3.2.4 of QIC-157 D.

			   Any media access command other than read or
			   write will switch DSC back to completion
			   mode */
			drvp->drive_flags &= ~DRIVE_DSCBA;
			WDCDEBUG_PRINT(("clear DCSBA\n"), DEBUG_DSC);
		}
	}

	return;

 timeout:
	printf ("%s:%d:%d: device timeout waiting to send SCSI packet\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive);

	sc_xfer->error = XS_TIMEOUT;
	xfer->next = wdc_atapi_reset;
	return;
}


char *
wdc_atapi_in_data_phase(struct wdc_xfer *xfer, int len, int ire)
{
	struct scsi_xfer *sc_xfer = xfer->cmd;
	struct atapiscsi_softc *as = sc_xfer->sc_link->bus->sb_adapter_softc;
	char *message;

	if (as->protocol_phase != as_data) {
		message = "unexpected data phase";
		goto unexpected_state;
	}

	if (ire & WDCI_CMD) {
		message = "unexpectedly in command phase";
		goto unexpected_state;
	}

	if (!(xfer->c_flags & C_SENSE)) {
		if (!(sc_xfer->flags & (SCSI_DATA_IN | SCSI_DATA_OUT))) {
			message = "data phase where none expected";
			goto unexpected_state;
		}

		/* Make sure polarities match */
		if (((ire & WDCI_IN) == WDCI_IN) ==
		    ((sc_xfer->flags & SCSI_DATA_OUT) == SCSI_DATA_OUT)) {
			message = "data transfer direction disagreement";
			goto unexpected_state;
		}
	} else {
		if (!(ire & WDCI_IN)) {
			message = "data transfer direction disagreement during sense";
			goto unexpected_state;
		}
	}

	if (len == 0) {
		message = "zero length transfer requested in data phase";
		goto unexpected_state;
	}


	return (0);

 unexpected_state:

	return (message);
}

void
wdc_atapi_intr_data(struct channel_softc *chp, struct wdc_xfer *xfer,
    int timeout, struct atapi_return_args *ret)
{
	struct scsi_xfer *sc_xfer = xfer->cmd;
	struct ata_drive_datas *drvp = &chp->ch_drive[xfer->drive];
	int len, ire;
	char *message;
	int tohost;

	len = (CHP_READ_REG(chp, wdr_cyl_hi) << 8) |
	    CHP_READ_REG(chp, wdr_cyl_lo);
	WDC_LOG_REG(chp, wdr_cyl_lo, len);

	ire = CHP_READ_REG(chp, wdr_ireason);
	WDC_LOG_REG(chp, wdr_ireason, ire);

	if ((message = wdc_atapi_in_data_phase(xfer, len, ire))) {
		/* The drive has dropped BSY before setting up the
		   registers correctly for DATA phase. This drive is
		   not compliant with ATA/ATAPI-4.

		   Give the drive 100ms to get its house in order
		   before we try again.  */
		WDCDEBUG_PRINT(("wdc_atapi_intr: %s\n", message),
		    DEBUG_ERRORS);

		if (!timeout) {
			ret->delay = 100;
			return;
		}
	}

	tohost = ((sc_xfer->flags & SCSI_DATA_IN) != 0 ||
	    (xfer->c_flags & C_SENSE) != 0);

	if (xfer->c_bcount >= len) {
		WDCDEBUG_PRINT(("wdc_atapi_intr: c_bcount %d len %d "
		    "st 0x%b err 0x%x "
		    "ire 0x%x\n", xfer->c_bcount,
		    len, chp->ch_status, WDCS_BITS, chp->ch_error, ire),
		    DEBUG_INTR);

		/* Common case */
		if (!tohost)
			wdc_output_bytes(drvp, (u_int8_t *)xfer->databuf +
			    xfer->c_skip, len);
		else
			wdc_input_bytes(drvp, (u_int8_t *)xfer->databuf +
			    xfer->c_skip, len);

		xfer->c_skip += len;
		xfer->c_bcount -= len;
	} else {
		/* Exceptional case - drive want to transfer more
		   data than we have buffer for */
		if (!tohost) {
			/* Wouldn't it be better to just abort here rather
			   than to write random stuff to drive? */
			printf("wdc_atapi_intr: warning: device requesting "
			    "%d bytes, only %d left in buffer\n", len, xfer->c_bcount);

			wdc_output_bytes(drvp, (u_int8_t *)xfer->databuf +
			    xfer->c_skip, xfer->c_bcount);

			CHP_WRITE_RAW_MULTI_2(chp, NULL,
			    len - xfer->c_bcount);
		} else {
			printf("wdc_atapi_intr: warning: reading only "
			    "%d of %d bytes\n", xfer->c_bcount, len);

			wdc_input_bytes(drvp,
			    (char *)xfer->databuf + xfer->c_skip,
			    xfer->c_bcount);
			wdcbit_bucket(chp, len - xfer->c_bcount);
		}

		xfer->c_skip += xfer->c_bcount;
		xfer->c_bcount = 0;
	}

	ret->expect_irq = 1;
	xfer->next = wdc_atapi_pio_intr;

	return;
}

void
wdc_atapi_intr_complete(struct channel_softc *chp, struct wdc_xfer *xfer,
    int timeout, struct atapi_return_args *ret)
{
	struct scsi_xfer *sc_xfer = xfer->cmd;
	struct ata_drive_datas *drvp = &chp->ch_drive[xfer->drive];
	struct atapiscsi_softc *as = sc_xfer->sc_link->bus->sb_adapter_softc;

	WDCDEBUG_PRINT(("PHASE_COMPLETED\n"), DEBUG_INTR);

	if (xfer->c_flags & C_DMA) {
		int retry;

		if (timeout) {
			sc_xfer->error = XS_TIMEOUT;
			ata_dmaerr(drvp);

			xfer->next = wdc_atapi_reset;
			return;
		}

		for (retry = 5; retry > 0; retry--) {
			wdc_atapi_update_status(chp);
			if ((chp->ch_status & (WDCS_BSY | WDCS_DRQ)) == 0)
				break;
			DELAY(5);
		}
		if (retry == 0) {
			ret->expect_irq = 1;
			return;
		}

		chp->wdc->dma_status =
		    (*chp->wdc->dma_finish)
		    (chp->wdc->dma_arg, chp->channel,
			xfer->drive, 1);

		if (chp->wdc->dma_status & WDC_DMAST_UNDER)
			xfer->c_bcount = 1;
		else
			xfer->c_bcount = 0;
	}

	as->protocol_phase = as_none;

	if (xfer->c_flags & C_SENSE) {
		if (chp->ch_status & WDCS_ERR) {
			if (chp->ch_error & WDCE_ABRT) {
				WDCDEBUG_PRINT(("wdc_atapi_intr: request_sense aborted, "
						"calling wdc_atapi_done()"
					), DEBUG_INTR);
				xfer->next = wdc_atapi_done;
				return;
			}

			/*
			 * request sense failed ! it's not supposed
 			 * to be possible
			 */
			sc_xfer->error = XS_SHORTSENSE;
		} else if (xfer->c_bcount < sizeof(sc_xfer->sense)) {
			/* use the sense we just read */
			sc_xfer->error = XS_SENSE;
		} else {
			/*
			 * command completed, but no data was read.
			 * use the short sense we saved previously.
			 */
			sc_xfer->error = XS_SHORTSENSE;
		}
	} else {
		sc_xfer->resid = xfer->c_bcount;
		if (chp->ch_status & WDCS_ERR) {
			if (!atapi_to_scsi_sense(sc_xfer, chp->ch_error) &&
			    (sc_xfer->sc_link->quirks &
			     ADEV_NOSENSE) == 0) {
				/*
				 * let the driver issue a
				 * 'request sense'
				 */
				xfer->databuf = &sc_xfer->sense;
				xfer->c_bcount = sizeof(sc_xfer->sense);
				xfer->c_skip = 0;
				xfer->c_done = NULL;
				xfer->c_flags |= C_SENSE;
				xfer->next = wdc_atapi_real_start;
				return;
			}
		}
	}

        if ((xfer->c_flags & C_DMA) &&
	    (chp->wdc->dma_status & ~WDC_DMAST_UNDER)) {
		ata_dmaerr(drvp);
		sc_xfer->error = XS_RESET;

		xfer->next = wdc_atapi_reset;
		return;
	}


	if (xfer->c_bcount != 0) {
		WDCDEBUG_PRINT(("wdc_atapi_intr: bcount value is "
				"%d after io\n", xfer->c_bcount), DEBUG_XFERS);
	}
#ifdef DIAGNOSTIC
	if (xfer->c_bcount < 0) {
		printf("wdc_atapi_intr warning: bcount value "
		       "is %d after io\n", xfer->c_bcount);
	}
#endif

	WDCDEBUG_PRINT(("wdc_atapi_intr: wdc_atapi_done() (end), error 0x%x "
			"\n", sc_xfer->error),
		       DEBUG_INTR);


	if (xfer->c_done)
		xfer->next = xfer->c_done;
	else
		xfer->next = wdc_atapi_done;

	return;
}

void
wdc_atapi_pio_intr(struct channel_softc *chp, struct wdc_xfer *xfer,
    int timeout, struct atapi_return_args *ret)
{
	struct scsi_xfer *sc_xfer = xfer->cmd;
	struct atapiscsi_softc *as = sc_xfer->sc_link->bus->sb_adapter_softc;
	u_int8_t ireason;

	wdc_atapi_update_status(chp);

	if (chp->ch_status & WDCS_BSY) {
		if (timeout)
			goto timeout;

		return;
	}

	if (!wdc_atapi_drive_selected(chp, xfer->drive)) {
		WDCDEBUG_PRINT(("wdc_atapi_intr_for_us: wrong drive selected\n"), DEBUG_INTR);
		wdc_set_drive(chp, xfer->drive);
		delay (1);

		if (!timeout)
			return;
	}

	if ((xfer->c_flags & C_MEDIA_ACCESS) &&
	    !(chp->ch_status & (WDCS_DSC | WDCS_DRQ))) {
		if (timeout)
			goto timeout;

		ret->delay = 100;
		return;
	}

	if (chp->wdc->cap & WDC_CAPABILITY_IRQACK)
		chp->wdc->irqack(chp);

	ireason = CHP_READ_REG(chp, wdr_ireason);
	WDC_LOG_REG(chp, wdr_ireason, ireason);

	WDCDEBUG_PRINT(("Phase %d, (0x%b, 0x%x) ", as->protocol_phase,
	    chp->ch_status, WDCS_BITS, ireason), DEBUG_INTR );

	switch (as->protocol_phase) {
	case as_data:
		if ((chp->ch_status & WDCS_DRQ) ||
		    (ireason & 3) != 3) {
			if (timeout)
				goto timeout;

			wdc_atapi_intr_data(chp, xfer, timeout, ret);
			return;
		}

		wdc_atapi_intr_complete(chp, xfer, timeout, ret);
		return;

	case as_completed:
		if ((chp->ch_status & WDCS_DRQ) ||
		    (ireason & 3) != 3) {
			if (timeout)
				goto timeout;

			ret->delay = 100;
			return;
		}

		wdc_atapi_intr_complete(chp, xfer, timeout, ret);
		return;

	default:
		printf ("atapiscsi: Shouldn't get here\n");
		sc_xfer->error = XS_DRIVER_STUFFUP;
		xfer->next = wdc_atapi_reset;
		return;
	}

	return;
timeout:
	ireason = CHP_READ_REG(chp, wdr_ireason);
	WDC_LOG_REG(chp, wdr_ireason, ireason);

	printf("%s:%d:%d: device timeout, c_bcount=%d, c_skip=%d, "
	    "status=0x%b, ireason=0x%x\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive,
	    xfer->c_bcount, xfer->c_skip, chp->ch_status, WDCS_BITS, ireason);

	sc_xfer->error = XS_TIMEOUT;
	xfer->next = wdc_atapi_reset;
	return;
}

void
wdc_atapi_ctrl(struct channel_softc *chp, struct wdc_xfer *xfer,
    int timeout, struct atapi_return_args *ret)
{
	struct scsi_xfer *sc_xfer = xfer->cmd;
	struct ata_drive_datas *drvp = &chp->ch_drive[xfer->drive];
	char *errstring = NULL;

 	wdc_atapi_update_status(chp);

	if (!timeout) {
		switch (drvp->state) {
		case ATAPI_IDENTIFY_WAIT_STATE:
			if (chp->ch_status & WDCS_BSY)
				return;
			break;
		default:
			if (chp->ch_status & (WDCS_BSY | WDCS_DRQ))
				return;
			break;
		}
	}

	if (!wdc_atapi_drive_selected(chp, xfer->drive))
	{
		wdc_set_drive(chp, xfer->drive);
		delay (1);
	}

	if (timeout) {
		int trigger_timeout = 1;

		switch (drvp->state) {
		case ATAPI_DEVICE_RESET_WAIT_STATE:
			errstring = "Device Reset Wait";
			drvp->drive_flags &= ~DRIVE_DEVICE_RESET;
			break;

		case ATAPI_IDENTIFY_WAIT_STATE:
			errstring = "Identify";
			if (!(chp->ch_status & WDCS_BSY) &&
			    (chp->ch_status & (WDCS_DRQ | WDCS_ERR)))
				trigger_timeout = 0;

			break;

		case ATAPI_PIOMODE_STATE:
			errstring = "Post-Identify";
			if (!(chp->ch_status & (WDCS_BSY | WDCS_DRQ)))
				trigger_timeout = 0;
			break;

		case ATAPI_PIOMODE_WAIT_STATE:
			errstring = "PIOMODE";
			if (chp->ch_status & (WDCS_BSY | WDCS_DRQ))
				drvp->drive_flags &= ~DRIVE_MODE;
			else
				trigger_timeout = 0;
			break;
		case ATAPI_DMAMODE_WAIT_STATE:
			errstring = "dmamode";
			if (chp->ch_status & (WDCS_BSY | WDCS_DRQ))
				drvp->drive_flags &= ~(DRIVE_DMA | DRIVE_UDMA);
			else
				trigger_timeout = 0;
			break;

		default:
			errstring = "unknown state";
			break;
		}

		if (trigger_timeout)
			goto timeout;
	}

	WDCDEBUG_PRINT(("wdc_atapi_ctrl %s:%d:%d state %d\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, drvp->drive, drvp->state),
	    DEBUG_INTR | DEBUG_FUNCS);

	switch (drvp->state) {
		/* My ATAPI slave device likes to assert DASP-/PDIAG- until
		   it is DEVICE RESET. This causes the LED to stay on.

		   There is a trade-off here. This drive will cause any
		   play-back or seeks happening to be interrupted.

		   Note that the bus reset that triggered this state
		   (which may have been caused by the other drive on
		   the chain) need not interrupt this playback. It happens
		   to on my Smart & Friendly CD burner.

		   - csapuntz@
		*/
	case ATAPI_RESET_BASE_STATE:
		if ((drvp->drive_flags & DRIVE_DEVICE_RESET) == 0) {
			drvp->state = ATAPI_IDENTIFY_STATE;
			break;
		}

		wdccommandshort(chp, drvp->drive, ATAPI_DEVICE_RESET);
		drvp->state = ATAPI_DEVICE_RESET_WAIT_STATE;
		ret->delay = ATAPI_RESET_DELAY;
		ret->timeout = ATAPI_RESET_WAIT;
		break;

	case ATAPI_DEVICE_RESET_WAIT_STATE:
		/* FALLTHROUGH */

	case ATAPI_IDENTIFY_STATE:
		wdccommandshort(chp, drvp->drive, ATAPI_IDENTIFY_DEVICE);
		drvp->state = ATAPI_IDENTIFY_WAIT_STATE;
		ret->delay = 10;
		ret->timeout = ATAPI_RESET_WAIT;
		break;

	case ATAPI_IDENTIFY_WAIT_STATE: {
		int idx = 0;

		while ((chp->ch_status & WDCS_DRQ) &&
		    idx++ < 20) {
			wdcbit_bucket(chp, 512);

			DELAY(1);
			wdc_atapi_update_status(chp);
		}

		drvp->state = ATAPI_PIOMODE_STATE;
		/*
		 * Note, we can't go directly to set PIO mode
		 * because the drive is free to assert BSY
		 * after the transfer
		 */
		break;
	}

	case ATAPI_PIOMODE_STATE:
		/* Don't try to set mode if controller can't be adjusted */
		if ((chp->wdc->cap & WDC_CAPABILITY_MODE) == 0)
			goto ready;
		/* Also don't try if the drive didn't report its mode */
		if ((drvp->drive_flags & DRIVE_MODE) == 0)
			goto ready;
		/* SET FEATURES 0x08 is only for PIO mode > 2 */
		if (drvp->PIO_mode <= 2)
			goto ready;
		wdccommand(chp, drvp->drive, SET_FEATURES, 0, 0, 0,
		    0x08 | drvp->PIO_mode, WDSF_SET_MODE);
		drvp->state = ATAPI_PIOMODE_WAIT_STATE;
		ret->timeout = ATAPI_CTRL_WAIT;
		ret->expect_irq = 1;
		break;
	case ATAPI_PIOMODE_WAIT_STATE:
		if (chp->wdc->cap & WDC_CAPABILITY_IRQACK)
			chp->wdc->irqack(chp);
		if (chp->ch_status & WDCS_ERR) {
			/* Downgrade straight to PIO mode 3 */
			drvp->PIO_mode = 3;
			chp->wdc->set_modes(chp);
		}
	/* FALLTHROUGH */

	case ATAPI_DMAMODE_STATE:
		if (drvp->drive_flags & DRIVE_UDMA) {
			wdccommand(chp, drvp->drive, SET_FEATURES, 0, 0, 0,
			    0x40 | drvp->UDMA_mode, WDSF_SET_MODE);
		} else if (drvp->drive_flags & DRIVE_DMA) {
			wdccommand(chp, drvp->drive, SET_FEATURES, 0, 0, 0,
			    0x20 | drvp->DMA_mode, WDSF_SET_MODE);
		} else {
			goto ready;
		}
		drvp->state = ATAPI_DMAMODE_WAIT_STATE;

		ret->timeout = ATAPI_CTRL_WAIT;
		ret->expect_irq = 1;
		break;

	case ATAPI_DMAMODE_WAIT_STATE:
		if (chp->wdc->cap & WDC_CAPABILITY_IRQACK)
			chp->wdc->irqack(chp);
		if (chp->ch_status & WDCS_ERR)
			drvp->drive_flags &= ~(DRIVE_DMA | DRIVE_UDMA);
	/* FALLTHROUGH */

	case ATAPI_READY_STATE:
	ready:
		drvp->state = ATAPI_READY_STATE;
		xfer->next = wdc_atapi_real_start;
		break;
	}
	return;

timeout:
	printf("%s:%d:%d: %s timed out\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive, errstring);
	sc_xfer->error = XS_TIMEOUT;
	xfer->next = wdc_atapi_reset;
	return;

}

void
wdc_atapi_tape_done(struct channel_softc *chp, struct wdc_xfer *xfer,
    int timeout, struct atapi_return_args *ret)
{
	struct scsi_xfer *sc_xfer = xfer->cmd;

	if (sc_xfer->error != XS_NOERROR) {
		xfer->next = wdc_atapi_done;
		return;
	}

	_lto3b(xfer->transfer_len,
	    ((struct scsi_rw_tape *)
		&sc_xfer->cmd)->len);

	xfer->c_bcount = sc_xfer->datalen;
	xfer->c_done = NULL;
	xfer->c_skip = 0;

	xfer->next = wdc_atapi_real_start;
	return;
}


void
wdc_atapi_done(struct channel_softc *chp, struct wdc_xfer *xfer,
    int timeout, struct atapi_return_args *ret)
{
	struct scsi_xfer *sc_xfer = xfer->cmd;

	WDCDEBUG_PRINT(("wdc_atapi_done %s:%d:%d: flags 0x%x error 0x%x\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive,
	    (u_int)xfer->c_flags, sc_xfer->error), DEBUG_XFERS);
	WDC_LOG_ATAPI_DONE(chp, xfer->drive, xfer->c_flags, sc_xfer->error);

	if (xfer->c_flags & C_POLL)
		wdc_enable_intr(chp);

	scsi_done(sc_xfer);

	xfer->next = NULL;
	return;
}


void
wdc_atapi_reset(struct channel_softc *chp, struct wdc_xfer *xfer,
    int timeout, struct atapi_return_args *ret)
{
	struct ata_drive_datas *drvp = &chp->ch_drive[xfer->drive];

	if (drvp->state == 0) {
		xfer->next = wdc_atapi_done;
		return;
	}

	WDCDEBUG_PRINT(("wdc_atapi_reset\n"), DEBUG_XFERS);
	wdccommandshort(chp, xfer->drive, ATAPI_SOFT_RESET);
	drvp->state = ATAPI_IDENTIFY_STATE;

	drvp->n_resets++;
	/* Some ATAPI devices need extra time to find their
	   brains after a reset
	 */
	xfer->next = wdc_atapi_reset_2;
	ret->delay = ATAPI_RESET_DELAY;
	ret->timeout = ATAPI_RESET_WAIT;
	return;
}

void
wdc_atapi_reset_2(struct channel_softc *chp, struct wdc_xfer *xfer,
    int timeout, struct atapi_return_args *ret)
{
	struct ata_drive_datas *drvp = &chp->ch_drive[xfer->drive];
	struct scsi_xfer *sc_xfer = xfer->cmd;

	if (timeout) {
		printf("%s:%d:%d: soft reset failed\n",
		    chp->wdc->sc_dev.dv_xname, chp->channel,
		    xfer->drive);
		sc_xfer->error = XS_SELTIMEOUT;
		wdc_reset_channel(drvp, 0);

		xfer->next = wdc_atapi_done;
		return;
	}

	wdc_atapi_update_status(chp);

	if (chp->ch_status & (WDCS_BSY | WDCS_DRQ)) {
		return;
	}

	xfer->next = wdc_atapi_done;
	return;
}
