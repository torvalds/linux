/*	$OpenBSD: sili.c,v 1.62 2024/09/04 07:54:52 mglocker Exp $ */

/*
 * Copyright (c) 2007 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2010 Conformal Systems LLC <info@conformal.com>
 * Copyright (c) 2010 Jonathan Matthew <jonathan@d14n.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/mutex.h>

#include <machine/bus.h>

#include <dev/ata/atascsi.h>
#include <dev/ata/pmreg.h>

#include <dev/ic/silireg.h>
#include <dev/ic/silivar.h>

/* use SILI_DEBUG for dmesg spam */
#define NO_SILI_DEBUG

#ifdef SILI_DEBUG
#define SILI_D_VERBOSE		(1<<0)
#define SILI_D_INTR		(1<<1)

int silidebug = SILI_D_VERBOSE;

#define DPRINTF(m, a...)	do { if ((m) & silidebug) printf(a); } while (0)
#else
#define DPRINTF(m, a...)
#endif

/* these can be used to simulate read and write errors on specific PMP ports */
#undef SILI_ERROR_TEST
int sili_error_pmp_ports = 0;		/* bitmask containing ports to fail*/
int sili_error_test_inv_p = 500;	/* 1/P(error) */
int sili_error_restart_type = SILI_PREG_PCS_PORTINIT; /* or _DEVRESET */

struct cfdriver sili_cd = {
	NULL, "sili", DV_DULL
};

/* wrapper around dma memory */
struct sili_dmamem {
	bus_dmamap_t		sdm_map;
	bus_dma_segment_t	sdm_seg;
	size_t			sdm_size;
	caddr_t			sdm_kva;
};
#define SILI_DMA_MAP(_sdm)	((_sdm)->sdm_map)
#define SILI_DMA_DVA(_sdm)	((_sdm)->sdm_map->dm_segs[0].ds_addr)
#define SILI_DMA_KVA(_sdm)	((_sdm)->sdm_kva)

struct sili_dmamem	*sili_dmamem_alloc(struct sili_softc *, bus_size_t,
			    bus_size_t);
void			sili_dmamem_free(struct sili_softc *,
			    struct sili_dmamem *);

/* per port goo */
struct sili_ccb;

/* size of scratch space for use in error recovery. */
#define SILI_SCRATCH_LEN	512	/* must be at least 1 sector */

struct sili_port {
	struct sili_softc	*sp_sc;
	bus_space_handle_t	sp_ioh;

	struct sili_ccb		*sp_ccbs;
	struct sili_dmamem	*sp_cmds;
	struct sili_dmamem	*sp_scratch;

	TAILQ_HEAD(, sili_ccb)	sp_free_ccbs;
	struct mutex		sp_free_ccb_mtx;

	volatile u_int32_t	sp_active;
	TAILQ_HEAD(, sili_ccb)	sp_active_ccbs;
	TAILQ_HEAD(, sili_ccb)	sp_deferred_ccbs;

	int			sp_port;
	int			sp_pmp_ports;
	int			sp_active_pmp_ports;
	int			sp_pmp_error_recovery;	/* port bitmask */
	volatile u_int32_t	sp_err_active;		/* cmd bitmask */
	volatile u_int32_t	sp_err_cmds;		/* cmd bitmask */

#ifdef SILI_DEBUG
	char			sp_name[16];
#define PORTNAME(_sp)	((_sp)->sp_name)
#else
#define PORTNAME(_sp)	DEVNAME((_sp)->sp_sc)
#endif
};

int			sili_ports_alloc(struct sili_softc *);
void			sili_ports_free(struct sili_softc *);

/* ccb shizz */

/*
 * the dma memory for each command will be made up of a prb followed by
 * 7 sgts, this is a neat 512 bytes.
 */
#define SILI_CMD_LEN		512

/*
 * you can fit 22 sge's into 7 sgts and a prb:
 * there's 1 sgl in an atapi prb (two in the ata one, but we can't over
 * advertise), but that's needed for the chain element. you get three sges
 * per sgt cos you lose the 4th sge for the chaining, but you keep it in
 * the last sgt. so 3 x 6 + 4 is 22.
 */
#define SILI_DMA_SEGS		22

struct sili_ccb {
	struct ata_xfer		ccb_xa;

	void			*ccb_cmd;
	u_int64_t		ccb_cmd_dva;
	bus_dmamap_t		ccb_dmamap;

	struct sili_port	*ccb_port;

	TAILQ_ENTRY(sili_ccb)	ccb_entry;
};

int			sili_ccb_alloc(struct sili_port *);
void			sili_ccb_free(struct sili_port *);
struct sili_ccb		*sili_get_ccb(struct sili_port *);
void			sili_put_ccb(struct sili_ccb *);

/* bus space ops */
u_int32_t		sili_read(struct sili_softc *, bus_size_t);
void			sili_write(struct sili_softc *, bus_size_t, u_int32_t);
u_int32_t		sili_pread(struct sili_port *, bus_size_t);
void			sili_pwrite(struct sili_port *, bus_size_t, u_int32_t);
int			sili_pwait_eq(struct sili_port *, bus_size_t,
			    u_int32_t, u_int32_t, int);
int			sili_pwait_ne(struct sili_port *, bus_size_t,
			    u_int32_t, u_int32_t, int);

/* command handling */
void			sili_post_direct(struct sili_port *, u_int,
			    void *, size_t buflen);
void			sili_post_indirect(struct sili_port *,
			    struct sili_ccb *);
void			sili_pread_fis(struct sili_port *, u_int,
			    struct ata_fis_d2h *);
u_int32_t		sili_signature(struct sili_port *, u_int);
u_int32_t		sili_port_softreset(struct sili_port *sp);
int			sili_load(struct sili_ccb *, struct sili_sge *, int);
void			sili_unload(struct sili_ccb *);
int			sili_poll(struct sili_ccb *, int, void (*)(void *));
void			sili_start(struct sili_port *, struct sili_ccb *);
int			sili_read_ncq_error(struct sili_port *, int *, int);
int			sili_pmp_port_start_error_recovery(struct sili_port *,
			    int);
void			sili_pmp_port_do_error_recovery(struct sili_port *,
			    int, u_int32_t *);
void			sili_port_clear_commands(struct sili_port *sp);

/* pmp operations */
int			sili_pmp_read(struct sili_port *, int, int,
			    u_int32_t *);
int			sili_pmp_write(struct sili_port *, int, int, u_int32_t);
int			sili_pmp_phy_status(struct sili_port *, int,
			    u_int32_t *);
int 			sili_pmp_identify(struct sili_port *, int *);

/* port interrupt handler */
u_int32_t		sili_port_intr(struct sili_port *, int);

/* atascsi interface */
int			sili_ata_probe(void *, int, int);
void			sili_ata_free(void *, int, int);
struct ata_xfer		*sili_ata_get_xfer(void *, int);
void			sili_ata_put_xfer(struct ata_xfer *);
void			sili_ata_cmd(struct ata_xfer *);
int			sili_pmp_portreset(struct sili_softc *, int, int);
int			sili_pmp_softreset(struct sili_softc *, int, int);

#ifdef SILI_ERROR_TEST
void 			sili_simulate_error(struct sili_ccb *ccb,
			    int *need_restart, int *err_port);
#endif

const struct atascsi_methods sili_atascsi_methods = {
	sili_ata_probe,
	sili_ata_free,
	sili_ata_get_xfer,
	sili_ata_put_xfer,
	sili_ata_cmd
};

/* completion paths */
void			sili_ata_cmd_done(struct sili_ccb *, int);
void			sili_ata_cmd_timeout(void *);
void			sili_dummy_done(struct ata_xfer *);

void			sili_pmp_op_timeout(void *);

int
sili_attach(struct sili_softc *sc)
{
	struct atascsi_attach_args	aaa;

	printf("\n");

	if (sili_ports_alloc(sc) != 0) {
		/* error already printed by sili_port_alloc */
		return (1);
	}

	/* bounce the controller */
	sili_write(sc, SILI_REG_GC, SILI_REG_GC_GR);
	sili_write(sc, SILI_REG_GC, 0x0);

	bzero(&aaa, sizeof(aaa));
	aaa.aaa_cookie = sc;
	aaa.aaa_methods = &sili_atascsi_methods;
	aaa.aaa_minphys = NULL;
	aaa.aaa_nports = sc->sc_nports;
	aaa.aaa_ncmds = SILI_MAX_CMDS;
	aaa.aaa_capability = ASAA_CAP_NCQ | ASAA_CAP_PMP_NCQ;

	sc->sc_atascsi = atascsi_attach(&sc->sc_dev, &aaa);

	return (0);
}

int
sili_detach(struct sili_softc *sc, int flags)
{
	int				rv;

	if (sc->sc_atascsi != NULL) {
		rv = atascsi_detach(sc->sc_atascsi, flags);
		if (rv != 0)
			return (rv);
	}

	if (sc->sc_ports != NULL)
		sili_ports_free(sc);

	return (0);
}

void
sili_resume(struct sili_softc *sc)
{
	int i, j;

	/* bounce the controller */
	sili_write(sc, SILI_REG_GC, SILI_REG_GC_GR);
	sili_write(sc, SILI_REG_GC, 0x0);

	for (i = 0; i < sc->sc_nports; i++) {
		if (sili_ata_probe(sc, i, 0) == ATA_PORT_T_PM) {
			struct sili_port *sp = &sc->sc_ports[i];
			for (j = 0; j < sp->sp_pmp_ports; j++) {
				sili_ata_probe(sc, i, j);
			}
		}
	}
}

int
sili_pmp_port_start_error_recovery(struct sili_port *sp, int err_port)
{
	struct sili_ccb *ccb;

	sp->sp_pmp_error_recovery |= (1 << err_port);

	/* create a bitmask of active commands on non-error ports */
	sp->sp_err_active = 0;
	TAILQ_FOREACH(ccb, &sp->sp_active_ccbs, ccb_entry) {
		int bit = (1 << ccb->ccb_xa.pmp_port);
		if ((sp->sp_pmp_error_recovery & bit) == 0) {
			DPRINTF(SILI_D_VERBOSE, "%s: slot %d active on port "
			    "%d\n", PORTNAME(sp), ccb->ccb_xa.tag,
			    ccb->ccb_xa.pmp_port);
			sp->sp_err_active |= (1 << ccb->ccb_xa.tag);
		}
	}

	if (sp->sp_err_active == 0) {
		DPRINTF(SILI_D_VERBOSE, "%s: no other PMP ports active\n",
		    PORTNAME(sp));
		sp->sp_pmp_error_recovery = 0;
		return (0);
	}

	/* set port resume */
	sili_pwrite(sp, SILI_PREG_PCS, SILI_PREG_PCS_RESUME);

	DPRINTF(SILI_D_VERBOSE, "%s: beginning error recovery (port %d); "
	    "error port mask %x, active slot mask %x\n", PORTNAME(sp), err_port,
	    sp->sp_pmp_error_recovery, sp->sp_err_active);
	return (1);
}

void
sili_port_clear_commands(struct sili_port *sp)
{
	int port;

	DPRINTF(SILI_D_VERBOSE, "%s: clearing active commands\n",
	    PORTNAME(sp));

	/* clear port resume */
	sili_pwrite(sp, SILI_PREG_PCC, SILI_PREG_PCC_RESUME);
	delay(10000);

	/* clear port status and port active for all ports */
	for (port = 0; port < 16; port++) {
		sili_pwrite(sp, SILI_PREG_PMP_STATUS(port), 0);
		sili_pwrite(sp, SILI_PREG_PMP_QACTIVE(port), 0);
	}
}

void
sili_pmp_port_do_error_recovery(struct sili_port *sp, int slot,
    u_int32_t *need_restart)
{
	if (sp->sp_pmp_error_recovery == 0) {
		return;
	}

	/* have all outstanding commands finished yet? */
	if (sp->sp_err_active != 0) {
		DPRINTF(SILI_D_VERBOSE, "%s: PMP error recovery still waiting "
		    "for %x\n", PORTNAME(sp), sp->sp_err_active);
		*need_restart = 0;
		return;
	}

	sili_port_clear_commands(sp);

	/* get the main error recovery code to reset the port and
	 * resubmit commands.  it will also reset the error recovery flags.
	 */
	*need_restart = SILI_PREG_PCS_PORTINIT;
	DPRINTF(SILI_D_VERBOSE, "%s: PMP error recovery complete\n",
	    PORTNAME(sp));
}

#ifdef SILI_ERROR_TEST
void
sili_simulate_error(struct sili_ccb *ccb, int *need_restart, int *err_port)
{
	struct sili_port *sp = ccb->ccb_port;

	if (*need_restart == 0 &&
	    ((1 << ccb->ccb_xa.pmp_port) & sili_error_pmp_ports)) {
		switch (ccb->ccb_xa.fis->command) {
		case ATA_C_WRITE_FPDMA:
		case ATA_C_READ_FPDMA:
		case ATA_C_WRITEDMA_EXT:
		case ATA_C_READDMA_EXT:
		case ATA_C_WRITEDMA:
		case ATA_C_READDMA:
			if (arc4random_uniform(sili_error_test_inv_p) == 0) {
				printf("%s: faking error on slot %d\n",
				    PORTNAME(sp), ccb->ccb_xa.tag);
				ccb->ccb_xa.state = ATA_S_ERROR;
				*need_restart = sili_error_restart_type;
				*err_port = ccb->ccb_xa.pmp_port;

				ccb->ccb_port->sp_err_cmds |=
				    (1 << ccb->ccb_xa.tag);
			}
			break;

		default:
			/* leave other commands alone, we only want to mess
			 * with normal read/write ops
			 */
			break;
		}
	}
}
#endif

u_int32_t
sili_port_intr(struct sili_port *sp, int timeout_slot)
{
	u_int32_t			is, pss_saved, pss_masked;
	u_int32_t			processed = 0, need_restart = 0;
	u_int32_t			err_port = 0;
	int				slot;
	struct sili_ccb			*ccb;

	is = sili_pread(sp, SILI_PREG_IS);
	pss_saved = sili_pread(sp, SILI_PREG_PSS); /* reading acks CMDCOMP */

#ifdef SILI_DEBUG
	if ((pss_saved & SILI_PREG_PSS_ALL_SLOTS) != sp->sp_active ||
	    ((is >> 16) & ~SILI_PREG_IS_CMDCOMP)) {
		DPRINTF(SILI_D_INTR, "%s: IS: 0x%08x (0x%b), PSS: %08x, "
		    "active: %08x\n", PORTNAME(sp), is, is >> 16, SILI_PFMT_IS,
		    pss_saved, sp->sp_active);
	}
#endif

	/* Only interested in slot status bits. */
	pss_saved &= SILI_PREG_PSS_ALL_SLOTS;

	if (is & SILI_PREG_IS_CMDERR) {
		int			err_slot, err_code;
		u_int32_t		sactive = 0;

		sili_pwrite(sp, SILI_PREG_IS, SILI_PREG_IS_CMDERR);
		err_slot = SILI_PREG_PCS_ACTIVE(sili_pread(sp, SILI_PREG_PCS));
		err_code = sili_pread(sp, SILI_PREG_CE);
		ccb = &sp->sp_ccbs[err_slot];

		switch (err_code) {
		case SILI_PREG_CE_DEVICEERROR:
		case SILI_PREG_CE_DATAFISERROR:
			/* Extract error from command slot in LRAM. */
			sili_pread_fis(sp, err_slot, &ccb->ccb_xa.rfis);
			err_port = ccb->ccb_xa.pmp_port;
			break;

		case SILI_PREG_CE_SDBERROR:

			if (sp->sp_pmp_ports > 0) {
				/* get the PMP port number for the error */
				err_port = (sili_pread(sp, SILI_PREG_CONTEXT)
				    >> SILI_PREG_CONTEXT_PMPORT_SHIFT) &
				    SILI_PREG_CONTEXT_PMPORT_MASK;
				DPRINTF(SILI_D_VERBOSE, "%s: error port is "
				    "%d\n", PORTNAME(sp), err_port);

				/* were there any NCQ commands active for
				 * the port?
				 */
				sactive = sili_pread(sp,
				    SILI_PREG_PMP_QACTIVE(err_port));
				DPRINTF(SILI_D_VERBOSE, "%s: error SActive "
				    "%x\n", PORTNAME(sp), sactive);
				if (sactive == 0)
					break;
			} else {
				/* No NCQ commands active?  Treat as a normal
				 * error.
				 */
				sactive = sili_pread(sp, SILI_PREG_SACT);
				if (sactive == 0)
					break;
			}

			/* Extract real NCQ error slot & RFIS from
			 * log page.
			 */ 
			if (!sili_read_ncq_error(sp, &err_slot, err_port)) {
				/* got real err_slot */
				DPRINTF(SILI_D_VERBOSE, "%s.%d: error slot "
				    "%d\n", PORTNAME(sp), err_port, err_slot);
				ccb = &sp->sp_ccbs[err_slot];
				break;
			}
			DPRINTF(SILI_D_VERBOSE, "%s.%d: failed to get error "
			    "slot\n", PORTNAME(sp), err_port);

			/* failed to get error or not NCQ */

			/* FALLTHROUGH */
		default:
			/* All other error types are fatal. */
			if (err_code != SILI_PREG_CE_SDBERROR) {
				err_port = (sili_pread(sp, SILI_PREG_CONTEXT)
				    >> SILI_PREG_CONTEXT_PMPORT_SHIFT) &
				    SILI_PREG_CONTEXT_PMPORT_MASK;
			}
			printf("%s.%d: fatal error (%d), aborting active slots "
			    "(%08x) and resetting device.\n", PORTNAME(sp),
			    err_port, err_code, pss_saved);
			while (pss_saved) {
				slot = ffs(pss_saved) - 1;
				pss_saved &= ~(1 << slot);

				ccb = &sp->sp_ccbs[slot];
				KASSERT(ccb->ccb_xa.state == ATA_S_ONCHIP);
				ccb->ccb_xa.state = ATA_S_ERROR;
			}
			need_restart = SILI_PREG_PCS_DEVRESET;
			goto fatal;
		}

		DPRINTF(SILI_D_VERBOSE, "%s.%d: %serror, code %d, slot %d, "
		    "active %08x\n", PORTNAME(sp), err_port,
		    sactive ? "NCQ " : "", err_code, err_slot, sp->sp_active);

		/* Clear the failed command in saved PSS so cmd_done runs. */
		pss_saved &= ~(1 << err_slot);
		/* Track errored commands until we finish recovery */
		sp->sp_err_cmds |= (1 << err_slot);

		KASSERT(ccb->ccb_xa.state == ATA_S_ONCHIP);
		ccb->ccb_xa.state = ATA_S_ERROR;

		need_restart = SILI_PREG_PCS_PORTINIT;
	}
fatal:

	/* Process command timeout request only if command is still active. */
	if (timeout_slot >= 0 && (pss_saved & (1 << timeout_slot))) {
		DPRINTF(SILI_D_VERBOSE, "%s: timing out slot %d, active %08x\n",
		    PORTNAME(sp), timeout_slot, sp->sp_active);

		/* Clear the failed command in saved PSS so cmd_done runs. */
		pss_saved &= ~(1 << timeout_slot);

		ccb = &sp->sp_ccbs[timeout_slot];
		KASSERT(ccb->ccb_xa.state == ATA_S_ONCHIP);
		ccb->ccb_xa.state = ATA_S_TIMEOUT;

		/* Reinitialise the port and clear all active commands */
		need_restart = SILI_PREG_PCS_PORTINIT;

		err_port = ccb->ccb_xa.pmp_port;
		sp->sp_err_cmds |= (1 << timeout_slot);

		sili_port_clear_commands(sp);
	}

	/* Command slot is complete if its bit in PSS is 0 but 1 in active. */
	pss_masked = ~pss_saved & sp->sp_active;
	while (pss_masked) {
		slot = ffs(pss_masked) - 1;
		ccb = &sp->sp_ccbs[slot];
		pss_masked &= ~(1 << slot);

		/* copy the rfis into the ccb if we were asked for it */
		if (ccb->ccb_xa.state == ATA_S_ONCHIP &&
		    ccb->ccb_xa.flags & ATA_F_GET_RFIS) {
			sili_pread_fis(sp, slot, &ccb->ccb_xa.rfis);
		}

#ifdef SILI_ERROR_TEST
		/* introduce random errors on reads and writes for testing */
		sili_simulate_error(ccb, &need_restart, &err_port);
#endif

		DPRINTF(SILI_D_INTR, "%s: slot %d is complete%s%s\n",
		    PORTNAME(sp), slot, ccb->ccb_xa.state == ATA_S_ERROR ?
		    " (error)" : (ccb->ccb_xa.state == ATA_S_TIMEOUT ?
		    " (timeout)" : ""),
		    ccb->ccb_xa.flags & ATA_F_NCQ ? " (ncq)" : "");

		sili_ata_cmd_done(ccb, need_restart);

		processed |= 1 << slot;

		sili_pmp_port_do_error_recovery(sp, slot, &need_restart);
	}

	if (need_restart) {

		if (sp->sp_pmp_error_recovery) {
			if (sp->sp_err_active != 0) {
				DPRINTF(SILI_D_VERBOSE, "%s: still waiting for "
				    "non-error commands to finish; port mask "
				    "%x, slot mask %x\n", PORTNAME(sp),
				    sp->sp_pmp_error_recovery,
				    sp->sp_err_active);
				return (processed);
			}
		} else if (timeout_slot < 0 && sp->sp_pmp_ports > 0) {
			/* wait until all other commands have finished before
			 * attempting to reinit the port.
			 */
			DPRINTF(SILI_D_VERBOSE, "%s: error on port with PMP "
			    "attached, error port %d\n", PORTNAME(sp),
			    err_port);
			if (sili_pmp_port_start_error_recovery(sp, err_port)) {
				DPRINTF(SILI_D_VERBOSE, "%s: need to wait for "
				    "other commands to finish\n", PORTNAME(sp));
				return (processed);
			}
		} else if (sp->sp_pmp_ports > 0) {
			DPRINTF(SILI_D_VERBOSE, "%s: timeout on PMP port\n",
			    PORTNAME(sp));
		} else {
			DPRINTF(SILI_D_VERBOSE, "%s: error on non-PMP port\n",
			    PORTNAME(sp));
		}

		/* Re-enable transfers on port. */
		sili_pwrite(sp, SILI_PREG_PCS, need_restart);
		if (!sili_pwait_eq(sp, SILI_PREG_PCS, need_restart, 0, 5000)) {
			printf("%s: port reset bit didn't clear after error\n",
			    PORTNAME(sp));
		}
		if (!sili_pwait_eq(sp, SILI_PREG_PCS, SILI_PREG_PCS_PORTRDY,
		    SILI_PREG_PCS_PORTRDY, 1000)) {
			printf("%s: couldn't restart port after error\n",
			    PORTNAME(sp));
		}
		sili_pwrite(sp, SILI_PREG_PCC, SILI_PREG_PCC_RESUME);

		/* check that our active CCB list matches the restart mask */
		pss_masked = pss_saved & ~(sp->sp_err_cmds);
		DPRINTF(SILI_D_VERBOSE, "%s: restart mask %x\n",
		    PORTNAME(sp), pss_masked);
		TAILQ_FOREACH(ccb, &sp->sp_active_ccbs, ccb_entry) {
			if (!(pss_masked & (1 << ccb->ccb_xa.tag))) {
				panic("sili_intr: slot %d not active in "
				    "pss_masked: %08x, state %02x",
				    ccb->ccb_xa.tag, pss_masked,
				    ccb->ccb_xa.state);
			}
			pss_masked &= ~(1 << ccb->ccb_xa.tag);
		}
		if (pss_masked != 0) {
			printf("%s: mask excluding active slots: %x\n",
			    PORTNAME(sp), pss_masked);
		}
		KASSERT(pss_masked == 0);
		
		/* if we had a timeout on a PMP port, do a portreset.
		 * exclude the control port here as there isn't a real
		 * device there to reset.
		 */
		if (timeout_slot >= 0 && sp->sp_pmp_ports > 0 &&
		    err_port != 15) {

			DPRINTF(SILI_D_VERBOSE,
			    "%s.%d: doing portreset after timeout\n",
			    PORTNAME(sp), err_port);
			sili_pmp_portreset(sp->sp_sc, sp->sp_port, err_port);

			/* wait a bit to let the port settle down */
			delay(2000000);
		}

		/* if we sent a device reset to a PMP, we need to reset the
		 * devices behind it too.
		 */
		if (need_restart == SILI_PREG_PCS_DEVRESET &&
		    sp->sp_pmp_ports > 0) {
			int port_type;
			int i;

			port_type = sili_port_softreset(sp);
			if (port_type != ATA_PORT_T_PM) {
				/* device disappeared or changed type? */
				printf("%s: expected to find a port multiplier,"
				    " got %d\n", PORTNAME(sp), port_type);
			}

			/* and now portreset all active ports */
			for (i = 0; i < sp->sp_pmp_ports; i++) {
				struct sili_softc *sc = sp->sp_sc;

				if ((sp->sp_active_pmp_ports & (1 << i)) == 0)
					continue;

				if (sili_pmp_portreset(sc, sp->sp_port, i)) {
					printf("%s.%d: failed to portreset "
					    "after error\n", PORTNAME(sp), i);
				}
			}
		}

		/* Restart CCBs in the order they were originally queued. */
		TAILQ_FOREACH(ccb, &sp->sp_active_ccbs, ccb_entry) {
			DPRINTF(SILI_D_VERBOSE, "%s: restarting slot %d "
			    "after error, state %02x\n", PORTNAME(sp),
			    ccb->ccb_xa.tag, ccb->ccb_xa.state);
			KASSERT(ccb->ccb_xa.state == ATA_S_ONCHIP);
			sili_post_indirect(sp, ccb);
		}
		sp->sp_err_cmds = 0;
		sp->sp_pmp_error_recovery = 0;
		
		/*
		 * Finally, run atascsi completion for any finished CCBs.  If
		 * we had run these during cmd_done above, any ccbs that their
		 * completion generated would have been activated out of order.
		 */
		while ((ccb = TAILQ_FIRST(&sp->sp_deferred_ccbs)) != NULL) {
			TAILQ_REMOVE(&sp->sp_deferred_ccbs, ccb, ccb_entry);

			DPRINTF(SILI_D_VERBOSE, "%s: running deferred "
			    "completion for slot %d, state %02x\n",
			    PORTNAME(sp), ccb->ccb_xa.tag, ccb->ccb_xa.state);
			KASSERT(ccb->ccb_xa.state == ATA_S_COMPLETE ||
			    ccb->ccb_xa.state == ATA_S_ERROR ||
			    ccb->ccb_xa.state == ATA_S_TIMEOUT);
			ata_complete(&ccb->ccb_xa);
		}
	}

	return (processed);
}

int
sili_intr(void *arg)
{
	struct sili_softc		*sc = arg;
	u_int32_t			is;
	int				port;

	/* If the card has gone away, this will return 0xffffffff. */
	is = sili_read(sc, SILI_REG_GIS);
	if (is == 0 || is == 0xffffffff)
		return (0);
	sili_write(sc, SILI_REG_GIS, is);
	DPRINTF(SILI_D_INTR, "sili_intr, GIS: %08x\n", is);

	while (is & SILI_REG_GIS_PIS_MASK) {
		port = ffs(is) - 1;
		sili_port_intr(&sc->sc_ports[port], -1);
		is &= ~(1 << port);
	}

	return (1);
}

int
sili_ports_alloc(struct sili_softc *sc)
{
	struct sili_port		*sp;
	int				i;

	sc->sc_ports = mallocarray(sc->sc_nports, sizeof(struct sili_port),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	for (i = 0; i < sc->sc_nports; i++) {
		sp = &sc->sc_ports[i];

		sp->sp_sc = sc;
		sp->sp_port = i;
#ifdef SILI_DEBUG
		snprintf(sp->sp_name, sizeof(sp->sp_name), "%s.%d",
		    DEVNAME(sc), i);
#endif
		if (bus_space_subregion(sc->sc_iot_port, sc->sc_ioh_port,
		    SILI_PORT_OFFSET(i), SILI_PORT_SIZE, &sp->sp_ioh) != 0) {
			printf("%s: unable to create register window "
			    "for port %d\n", DEVNAME(sc), i);
			goto freeports;
		}
	}

	return (0);

freeports:
	/* bus_space(9) says subregions dont have to be freed */
	free(sc->sc_ports, M_DEVBUF, sc->sc_nports * sizeof(struct sili_port));
	sc->sc_ports = NULL;
	return (1);
}

void
sili_ports_free(struct sili_softc *sc)
{
	struct sili_port		*sp;
	int				i;

	for (i = 0; i < sc->sc_nports; i++) {
		sp = &sc->sc_ports[i];

		if (sp->sp_ccbs != NULL)
			sili_ccb_free(sp);
	}

	/* bus_space(9) says subregions dont have to be freed */
	free(sc->sc_ports, M_DEVBUF, sc->sc_nports * sizeof(struct sili_port));
	sc->sc_ports = NULL;
}

int
sili_ccb_alloc(struct sili_port *sp)
{
	struct sili_softc		*sc = sp->sp_sc;
	struct sili_ccb			*ccb;
	struct sili_prb			*prb;
	int				i;

	TAILQ_INIT(&sp->sp_free_ccbs);
	mtx_init(&sp->sp_free_ccb_mtx, IPL_BIO);
	TAILQ_INIT(&sp->sp_active_ccbs);
	TAILQ_INIT(&sp->sp_deferred_ccbs);

	sp->sp_ccbs = mallocarray(SILI_MAX_CMDS, sizeof(struct sili_ccb),
	    M_DEVBUF, M_WAITOK);
	sp->sp_cmds = sili_dmamem_alloc(sc, SILI_CMD_LEN * SILI_MAX_CMDS,
	    SILI_PRB_ALIGN);
	if (sp->sp_cmds == NULL)
		goto free_ccbs;
	sp->sp_scratch = sili_dmamem_alloc(sc, SILI_SCRATCH_LEN, PAGE_SIZE);
	if (sp->sp_scratch == NULL)
		goto free_cmds;

	bzero(sp->sp_ccbs, sizeof(struct sili_ccb) * SILI_MAX_CMDS);

	for (i = 0; i < SILI_MAX_CMDS; i++) {
		ccb = &sp->sp_ccbs[i];
		ccb->ccb_port = sp;
		ccb->ccb_cmd = SILI_DMA_KVA(sp->sp_cmds) + i * SILI_CMD_LEN;
		ccb->ccb_cmd_dva = SILI_DMA_DVA(sp->sp_cmds) + i * SILI_CMD_LEN;
		if (bus_dmamap_create(sc->sc_dmat, MAXPHYS, SILI_DMA_SEGS,
		    MAXPHYS, 0, BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW,
		    &ccb->ccb_dmamap) != 0)
			goto free_scratch;

		prb = ccb->ccb_cmd;
		ccb->ccb_xa.fis = (struct ata_fis_h2d *)&prb->fis;
		ccb->ccb_xa.packetcmd = ((struct sili_prb_packet *)prb)->cdb;
		ccb->ccb_xa.tag = i;
		ccb->ccb_xa.state = ATA_S_COMPLETE;

		sili_put_ccb(ccb);
	}

	return (0);

free_scratch:
	sili_dmamem_free(sc, sp->sp_scratch);
free_cmds:
	sili_dmamem_free(sc, sp->sp_cmds);
free_ccbs:
	sili_ccb_free(sp);
	return (1);
}

void
sili_ccb_free(struct sili_port *sp)
{
	struct sili_softc		*sc = sp->sp_sc;
	struct sili_ccb			*ccb;

	while ((ccb = sili_get_ccb(sp)) != NULL)
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmamap);

	free(sp->sp_ccbs, M_DEVBUF, 0);
	sp->sp_ccbs = NULL;
}

struct sili_ccb *
sili_get_ccb(struct sili_port *sp)
{
	struct sili_ccb			*ccb;

	/*
	 * Don't allow new commands to start while doing PMP error
	 * recovery
	 */
	if (sp->sp_pmp_error_recovery != 0) {
		return (NULL);
	}

	mtx_enter(&sp->sp_free_ccb_mtx);
	ccb = TAILQ_FIRST(&sp->sp_free_ccbs);
	if (ccb != NULL) {
		KASSERT(ccb->ccb_xa.state == ATA_S_PUT);
		TAILQ_REMOVE(&sp->sp_free_ccbs, ccb, ccb_entry);
		ccb->ccb_xa.state = ATA_S_SETUP;
	}
	mtx_leave(&sp->sp_free_ccb_mtx);

	return (ccb);
}

void
sili_put_ccb(struct sili_ccb *ccb)
{
	struct sili_port		*sp = ccb->ccb_port;

#ifdef DIAGNOSTIC
	if (ccb->ccb_xa.state != ATA_S_COMPLETE &&
	    ccb->ccb_xa.state != ATA_S_TIMEOUT &&
	    ccb->ccb_xa.state != ATA_S_ERROR) {
		printf("%s: invalid ata_xfer state %02x in sili_put_ccb, "
		    "slot %d\n", PORTNAME(sp), ccb->ccb_xa.state,
		    ccb->ccb_xa.tag);
	}
#endif

	ccb->ccb_xa.state = ATA_S_PUT;
	mtx_enter(&sp->sp_free_ccb_mtx);
	TAILQ_INSERT_TAIL(&sp->sp_free_ccbs, ccb, ccb_entry);
	mtx_leave(&sp->sp_free_ccb_mtx);
}

struct sili_dmamem *
sili_dmamem_alloc(struct sili_softc *sc, bus_size_t size, bus_size_t align)
{
	struct sili_dmamem		*sdm;
	int				nsegs;

	sdm = malloc(sizeof(*sdm), M_DEVBUF, M_WAITOK | M_ZERO);
	sdm->sdm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &sdm->sdm_map) != 0)
		goto sdmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, align, 0, &sdm->sdm_seg,
	    1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &sdm->sdm_seg, nsegs, size,
	    &sdm->sdm_kva, BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, sdm->sdm_map, sdm->sdm_kva, size,
	    NULL, BUS_DMA_NOWAIT) != 0)
		goto unmap;

	return (sdm);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, sdm->sdm_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &sdm->sdm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, sdm->sdm_map);
sdmfree:
	free(sdm, M_DEVBUF, sizeof *sdm);

	return (NULL);
}

void
sili_dmamem_free(struct sili_softc *sc, struct sili_dmamem *sdm)
{
	bus_dmamap_unload(sc->sc_dmat, sdm->sdm_map);
	bus_dmamem_unmap(sc->sc_dmat, sdm->sdm_kva, sdm->sdm_size);
	bus_dmamem_free(sc->sc_dmat, &sdm->sdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, sdm->sdm_map);
	free(sdm, M_DEVBUF, sizeof *sdm);
}

u_int32_t
sili_read(struct sili_softc *sc, bus_size_t r)
{
	u_int32_t			rv;

	bus_space_barrier(sc->sc_iot_global, sc->sc_ioh_global, r, 4,
	    BUS_SPACE_BARRIER_READ);
	rv = bus_space_read_4(sc->sc_iot_global, sc->sc_ioh_global, r);

	return (rv);
}

void
sili_write(struct sili_softc *sc, bus_size_t r, u_int32_t v)
{
	bus_space_write_4(sc->sc_iot_global, sc->sc_ioh_global, r, v);
	bus_space_barrier(sc->sc_iot_global, sc->sc_ioh_global, r, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

u_int32_t
sili_pread(struct sili_port *sp, bus_size_t r)
{
	u_int32_t			rv;

	bus_space_barrier(sp->sp_sc->sc_iot_port, sp->sp_ioh, r, 4,
	    BUS_SPACE_BARRIER_READ);
	rv = bus_space_read_4(sp->sp_sc->sc_iot_port, sp->sp_ioh, r);

	return (rv);
}

void
sili_pwrite(struct sili_port *sp, bus_size_t r, u_int32_t v)
{
	bus_space_write_4(sp->sp_sc->sc_iot_port, sp->sp_ioh, r, v);
	bus_space_barrier(sp->sp_sc->sc_iot_port, sp->sp_ioh, r, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

int
sili_pwait_eq(struct sili_port *sp, bus_size_t r, u_int32_t mask,
    u_int32_t value, int timeout)
{
	while ((sili_pread(sp, r) & mask) != value) {
		if (timeout == 0)
			return (0);

		delay(1000);
		timeout--;
	}

	return (1);
}

int
sili_pwait_ne(struct sili_port *sp, bus_size_t r, u_int32_t mask,
    u_int32_t value, int timeout)
{
	while ((sili_pread(sp, r) & mask) == value) {
		if (timeout == 0)
			return (0);

		delay(1000);
		timeout--;
	}

	return (1);
}

void
sili_post_direct(struct sili_port *sp, u_int slot, void *buf, size_t buflen)
{
	bus_size_t			r = SILI_PREG_SLOT(slot);

#ifdef DIAGNOSTIC
	if (buflen != 64 && buflen != 128)
		panic("sili_pcopy: buflen of %lu is not 64 or 128", buflen);
#endif

	bus_space_write_raw_region_4(sp->sp_sc->sc_iot_port, sp->sp_ioh, r,
	    buf, buflen);
	bus_space_barrier(sp->sp_sc->sc_iot_port, sp->sp_ioh, r, buflen,
	    BUS_SPACE_BARRIER_WRITE);

	sili_pwrite(sp, SILI_PREG_FIFO, slot);
}

void
sili_pread_fis(struct sili_port *sp, u_int slot, struct ata_fis_d2h *fis)
{
	bus_size_t			r = SILI_PREG_SLOT(slot) + 8;

	bus_space_barrier(sp->sp_sc->sc_iot_port, sp->sp_ioh, r,
	    sizeof(struct ata_fis_d2h), BUS_SPACE_BARRIER_READ);
	bus_space_read_raw_region_4(sp->sp_sc->sc_iot_port, sp->sp_ioh, r,
	    fis, sizeof(struct ata_fis_d2h));
}

void
sili_post_indirect(struct sili_port *sp, struct sili_ccb *ccb)
{
	sili_pwrite(sp, SILI_PREG_CAR_LO(ccb->ccb_xa.tag),
	    (u_int32_t)ccb->ccb_cmd_dva);
	sili_pwrite(sp, SILI_PREG_CAR_HI(ccb->ccb_xa.tag),
	    (u_int32_t)(ccb->ccb_cmd_dva >> 32));
}

u_int32_t
sili_signature(struct sili_port *sp, u_int slot)
{
	u_int32_t			sig_hi, sig_lo;

	sig_hi = sili_pread(sp, SILI_PREG_SIG_HI(slot));
	sig_hi <<= SILI_PREG_SIG_HI_SHIFT;
	sig_lo = sili_pread(sp, SILI_PREG_SIG_LO(slot));
	sig_lo &= SILI_PREG_SIG_LO_MASK;

	return (sig_hi | sig_lo);
}

void
sili_dummy_done(struct ata_xfer *xa)
{
}

int
sili_pmp_portreset(struct sili_softc *sc, int port, int pmp_port)
{
	struct sili_port	*sp;
	u_int32_t 		data;
	int			loop;

	sp = &sc->sc_ports[port];
	DPRINTF(SILI_D_VERBOSE, "%s: resetting pmp port %d\n", PORTNAME(sp),
	    pmp_port);

	if (sili_pmp_write(sp, pmp_port, SATA_PMREG_SERR, -1))
		goto err;
	if (sili_pmp_write(sp, pmp_port, SATA_PMREG_SCTL,
	    SATA_PM_SCTL_IPM_DISABLED))
		goto err;
	delay(10000);

	/* enable PHY by writing 1 then 0 to Scontrol DET field, using
	 * Write Port Multiplier commands
	 */
	data = SATA_PM_SCTL_IPM_DISABLED | SATA_PM_SCTL_DET_INIT |
	    SATA_PM_SCTL_SPD_ANY;
	if (sili_pmp_write(sp, pmp_port, SATA_PMREG_SCTL, data))
		goto err;
	delay(100000);
	
	if (sili_pmp_phy_status(sp, pmp_port, &data)) {
		printf("%s: cannot clear phy status for PMP probe\n",
			PORTNAME(sp));
		goto err;
	}
	
	sili_pmp_write(sp, pmp_port, SATA_PMREG_SERR, -1);
	data = SATA_PM_SCTL_IPM_DISABLED | SATA_PM_SCTL_DET_NONE;
	if (sili_pmp_write(sp, pmp_port, SATA_PMREG_SCTL, data))
		goto err;
	delay(100000);
	
	/* wait for PHYRDY by polling SStatus */
	for (loop = 3; loop; loop--) {
		if (sili_pmp_read(sp, pmp_port, SATA_PMREG_SSTS, &data))
			goto err;
		if (data & SATA_PM_SSTS_DET)
			break;
		delay(100000);
	}
	if (loop == 0) {
		DPRINTF(SILI_D_VERBOSE, "%s.%d: port appears to be unplugged\n",
		    PORTNAME(sp), pmp_port);
		goto err;
	}
	
	/* give it a bit more time to complete negotiation */
	for (loop = 30; loop; loop--) {
		if (sili_pmp_read(sp, pmp_port, SATA_PMREG_SSTS, &data))
			goto err;
		if ((data & SATA_PM_SSTS_DET) == SATA_PM_SSTS_DET_DEV)
			break;
		delay(10000);
	}
	if (loop == 0) {
		printf("%s.%d: device may be powered down\n", PORTNAME(sp),
		    pmp_port);
		goto err;
	}

	DPRINTF(SILI_D_VERBOSE, "%s.%d: device detected; SStatus=%08x\n",
	    PORTNAME(sp), pmp_port, data);

	/* clear the X-bit and all other error bits in Serror (PCSR[1]) */
	sili_pmp_write(sp, pmp_port, SATA_PMREG_SERR, -1);
	return (0);

err:
	DPRINTF(SILI_D_VERBOSE, "%s.%d: port reset failed\n", PORTNAME(sp),
	    pmp_port);
	sili_pmp_write(sp, pmp_port, SATA_PMREG_SERR, -1);
	return (1);
}

void
sili_pmp_op_timeout(void *cookie)
{
	struct sili_ccb *ccb = cookie;
	struct sili_port *sp = ccb->ccb_port;
	int s;

	switch (ccb->ccb_xa.state) {
	case ATA_S_PENDING:
		TAILQ_REMOVE(&sp->sp_active_ccbs, ccb, ccb_entry);
		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		break;
	case ATA_S_ONCHIP:
		KASSERT(sp->sp_active == (1 << ccb->ccb_xa.tag));
		s = splbio();
		sili_port_intr(sp, ccb->ccb_xa.tag);
		splx(s);
		break;
	case ATA_S_ERROR:
		/* don't do anything? */
		break;
	default:
		panic("%s: sili_pmp_op_timeout: ccb in bad state %d",
		      PORTNAME(sp), ccb->ccb_xa.state);
	}
}

int
sili_pmp_softreset(struct sili_softc *sc, int port, int pmp_port)
{
	struct sili_ccb		*ccb;
	struct sili_prb		*prb;
	struct sili_port	*sp;
	struct ata_fis_h2d	*fis;
	u_int32_t 		data;
	u_int32_t		signature;

	sp = &sc->sc_ports[port];

	ccb = sili_get_ccb(sp);
	if (ccb == NULL) {
		printf("%s: sili_pmp_softreset NULL ccb!\n", PORTNAME(sp));
		return (-1);
	}

	ccb->ccb_xa.flags = ATA_F_POLL | ATA_F_GET_RFIS;
	ccb->ccb_xa.complete = sili_dummy_done;
	ccb->ccb_xa.pmp_port = pmp_port;
	
	prb = ccb->ccb_cmd;
	bzero(prb, sizeof(*prb));
	fis = (struct ata_fis_h2d *)&prb->fis;
	fis->flags = pmp_port;
	prb->control = SILI_PRB_SOFT_RESET;

	ccb->ccb_xa.state = ATA_S_PENDING;

	if (sili_poll(ccb, 8000, sili_pmp_op_timeout) != 0) {
		DPRINTF(SILI_D_VERBOSE, "%s.%d: softreset FIS failed\n",
		    PORTNAME(sp), pmp_port);

		sili_put_ccb(ccb);
		/* don't return a valid device type here so the caller knows
		 * it can retry if it wants to
		 */
		return (-1);
	}

	signature = ccb->ccb_xa.rfis.sector_count |
	    (ccb->ccb_xa.rfis.lba_low << 8) |
	    (ccb->ccb_xa.rfis.lba_mid << 16) |
	    (ccb->ccb_xa.rfis.lba_high << 24);
	DPRINTF(SILI_D_VERBOSE, "%s.%d: signature: %08x\n", PORTNAME(sp),
	    pmp_port, signature);

	sili_put_ccb(ccb);

	/* clear phy status and error bits */
	if (sili_pmp_phy_status(sp, pmp_port, &data)) {
		printf("%s.%d: cannot clear phy status after softreset\n",
		       PORTNAME(sp), pmp_port);
	}
	sili_pmp_write(sp, pmp_port, SATA_PMREG_SERR, -1);

	/* classify the device based on its signature */
	switch (signature) {
	case SATA_SIGNATURE_DISK:
		return (ATA_PORT_T_DISK);
	case SATA_SIGNATURE_ATAPI:
		return (ATA_PORT_T_ATAPI);
	case SATA_SIGNATURE_PORT_MULTIPLIER:
		return (ATA_PORT_T_NONE);
	default:
		return (ATA_PORT_T_NONE);
	}
}

u_int32_t
sili_port_softreset(struct sili_port *sp)
{
	struct sili_prb_softreset	sreset;
	u_int32_t			signature;

	bzero(&sreset, sizeof(sreset));
	sreset.control = htole16(SILI_PRB_SOFT_RESET | SILI_PRB_INTERRUPT_MASK);
	sreset.fis[1] = SATA_PMP_CONTROL_PORT;

	/* we use slot 0 */
	sili_post_direct(sp, 0, &sreset, sizeof(sreset));
	if (!sili_pwait_eq(sp, SILI_PREG_PSS, (1 << 0), 0, 1000)) {
		DPRINTF(SILI_D_VERBOSE, "%s: timed out while waiting for soft "
		    "reset\n", PORTNAME(sp));
		return (ATA_PORT_T_NONE);
	}

	/* Read device signature from command slot. */
	signature = sili_signature(sp, 0);

	DPRINTF(SILI_D_VERBOSE, "%s: signature 0x%08x\n", PORTNAME(sp),
	    signature);

	switch (signature) {
	case SATA_SIGNATURE_DISK:
		return (ATA_PORT_T_DISK);
	case SATA_SIGNATURE_ATAPI:
		return (ATA_PORT_T_ATAPI);
	case SATA_SIGNATURE_PORT_MULTIPLIER:
		return (ATA_PORT_T_PM);
	default:
		return (ATA_PORT_T_NONE);
	}
}

int
sili_ata_probe(void *xsc, int port, int lun)
{
	struct sili_softc		*sc = xsc;
	struct sili_port		*sp = &sc->sc_ports[port];
	int				port_type;

	/* handle pmp port probes */
	if (lun != 0) {
		int i;
		int rc;
		int pmp_port = lun - 1;

		if (lun > sp->sp_pmp_ports)
			return (ATA_PORT_T_NONE);

		for (i = 0; i < 2; i++) {
			if (sili_pmp_portreset(sc, port, pmp_port)) {
				continue;
			}

			/* small delay between attempts to allow error
			 * conditions to settle down.  this doesn't seem
			 * to affect portreset operations, just
			 * commands sent to the device.
			 */
			if (i != 0) {
				delay(5000000);
			}

			rc = sili_pmp_softreset(sc, port, pmp_port);
			switch (rc) {
			case -1:
				/* possibly try again */
				break;
			case ATA_PORT_T_DISK:
			case ATA_PORT_T_ATAPI:
				/* mark this port as active */
				sp->sp_active_pmp_ports |= (1 << pmp_port);
			default:
				return (rc);
			}
		}
		DPRINTF(SILI_D_VERBOSE, "%s.%d: probe failed\n", PORTNAME(sp),
		    pmp_port);
		return (ATA_PORT_T_NONE);
	}

	sili_pwrite(sp, SILI_PREG_PCS, SILI_PREG_PCS_PORTRESET);
	delay(10000);
	sili_pwrite(sp, SILI_PREG_PCC, SILI_PREG_PCC_PORTRESET);

	sili_pwrite(sp, SILI_PREG_PCS, SILI_PREG_PCS_PORTINIT);
	if (!sili_pwait_eq(sp, SILI_PREG_PCS, SILI_PREG_PCS_PORTRDY,
	    SILI_PREG_PCS_PORTRDY, 1000)) {
		printf("%s: couldn't initialize port\n", PORTNAME(sp));
		return (ATA_PORT_T_NONE);
	}

	sili_pwrite(sp, SILI_PREG_PCC, SILI_PREG_PCC_A32B);

	if (!sili_pwait_eq(sp, SILI_PREG_SSTS, SATA_SStatus_DET,
	    SATA_SStatus_DET_DEV, 2000)) {
		DPRINTF(SILI_D_VERBOSE, "%s: unattached\n", PORTNAME(sp));
		return (ATA_PORT_T_NONE);
	}

	DPRINTF(SILI_D_VERBOSE, "%s: SSTS 0x%08x\n", PORTNAME(sp),
	    sili_pread(sp, SILI_PREG_SSTS));

	port_type = sili_port_softreset(sp);
	if (port_type == ATA_PORT_T_NONE)
		return (port_type);

	/* allocate port resources */
	if (sili_ccb_alloc(sp) != 0)
		return (ATA_PORT_T_NONE);

	/* do PMP probe now that we can talk to the device */
	if (port_type == ATA_PORT_T_PM) {
		int i;

		sili_pwrite(sp, SILI_PREG_PCS, SILI_PREG_PCS_PMEN);

		if (sili_pmp_identify(sp, &sp->sp_pmp_ports)) {
			return (ATA_PORT_T_NONE);
		}

		/* reset all the PMP ports to wake devices up */
		for (i = 0; i < sp->sp_pmp_ports; i++) {
			sili_pmp_portreset(sp->sp_sc, sp->sp_port, i);
		}
	}

	/* enable port interrupts */
	sili_write(sc, SILI_REG_GC, sili_read(sc, SILI_REG_GC) | 1 << port);
	sili_pwrite(sp, SILI_PREG_IES, SILI_PREG_IE_CMDERR |
	    SILI_PREG_IE_CMDCOMP);

	return (port_type);
}

void
sili_ata_free(void *xsc, int port, int lun)
{
	struct sili_softc		*sc = xsc;
	struct sili_port		*sp = &sc->sc_ports[port];

	if (lun == 0) {
		if (sp->sp_ccbs != NULL)
			sili_ccb_free(sp);

		/* XXX we should do more here */
	}
}

void
sili_ata_cmd(struct ata_xfer *xa)
{
	struct sili_ccb			*ccb = (struct sili_ccb *)xa;
	struct sili_port		*sp = ccb->ccb_port;
	struct sili_softc		*sc = sp->sp_sc;
	struct sili_prb_ata		*ata;
	struct sili_prb_packet		*atapi;
	struct sili_sge			*sgl;
	int				sgllen;
	int				s;

	KASSERT(xa->state == ATA_S_SETUP || xa->state == ATA_S_TIMEOUT);

	if (xa->flags & ATA_F_PACKET) {
		atapi = ccb->ccb_cmd;

		if (xa->flags & ATA_F_WRITE)
			atapi->control = htole16(SILI_PRB_PACKET_WRITE);
		else
			atapi->control = htole16(SILI_PRB_PACKET_READ);

		sgl = atapi->sgl;
		sgllen = nitems(atapi->sgl);
	} else {
		ata = ccb->ccb_cmd;

		ata->control = 0;

		sgl = ata->sgl;
		sgllen = nitems(ata->sgl);
	}

	if (sili_load(ccb, sgl, sgllen) != 0)
		goto failcmd;

	bus_dmamap_sync(sc->sc_dmat, SILI_DMA_MAP(sp->sp_cmds),
	    xa->tag * SILI_CMD_LEN, SILI_CMD_LEN, BUS_DMASYNC_PREWRITE);

	timeout_set(&xa->stimeout, sili_ata_cmd_timeout, ccb);

	xa->state = ATA_S_PENDING;

	if (xa->flags & ATA_F_POLL)
		sili_poll(ccb, xa->timeout, sili_ata_cmd_timeout);
	else {
		s = splbio();
		timeout_add_msec(&xa->stimeout, xa->timeout);
		sili_start(sp, ccb);
		splx(s);
	}

	return;

failcmd:
	s = splbio();
	xa->state = ATA_S_ERROR;
	ata_complete(xa);
	splx(s);
}

void
sili_ata_cmd_done(struct sili_ccb *ccb, int defer_completion)
{
	struct sili_port		*sp = ccb->ccb_port;
	struct sili_softc		*sc = sp->sp_sc;
	struct ata_xfer			*xa = &ccb->ccb_xa;

	splassert(IPL_BIO);

	timeout_del(&xa->stimeout);

	bus_dmamap_sync(sc->sc_dmat, SILI_DMA_MAP(sp->sp_cmds),
	    xa->tag * SILI_CMD_LEN, SILI_CMD_LEN, BUS_DMASYNC_POSTWRITE);

	sili_unload(ccb);

	TAILQ_REMOVE(&sp->sp_active_ccbs, ccb, ccb_entry);
	sp->sp_active &= ~(1 << xa->tag);
	if (sp->sp_err_active & (1 << xa->tag)) {
		sp->sp_err_active &= ~(1 << xa->tag);
		DPRINTF(SILI_D_VERBOSE, "%s: slot %d complete, error mask now "
		    "%x\n", PORTNAME(sp), xa->tag, sp->sp_err_active);
	}

	if (xa->state == ATA_S_ONCHIP)
		xa->state = ATA_S_COMPLETE;
#ifdef DIAGNOSTIC
	else if (xa->state != ATA_S_ERROR && xa->state != ATA_S_TIMEOUT)
		printf("%s: invalid ata_xfer state %02x in sili_ata_cmd_done, "
		    "slot %d\n", PORTNAME(sp), xa->state, xa->tag);
#endif
	if (defer_completion)
		TAILQ_INSERT_TAIL(&sp->sp_deferred_ccbs, ccb, ccb_entry);
	else if (xa->state == ATA_S_COMPLETE)
		ata_complete(xa);
#ifdef DIAGNOSTIC
	else
		printf("%s: completion not deferred, but xa->state is %02x?\n",
		    PORTNAME(sp), xa->state);
#endif
}

void
sili_ata_cmd_timeout(void *xccb)
{
	struct sili_ccb			*ccb = xccb;
	struct sili_port		*sp = ccb->ccb_port;
	int				s;

	s = splbio();
	sili_port_intr(sp, ccb->ccb_xa.tag);
	splx(s);
}

int
sili_load(struct sili_ccb *ccb, struct sili_sge *sgl, int sgllen)
{
	struct sili_port		*sp = ccb->ccb_port;
	struct sili_softc		*sc = sp->sp_sc;
	struct ata_xfer			*xa = &ccb->ccb_xa;
	struct sili_sge			*nsge = sgl, *ce = NULL;
	bus_dmamap_t			dmap = ccb->ccb_dmamap;
	u_int64_t			addr;
	int				error;
	int				i;

	if (xa->datalen == 0)
		return (0);

	error = bus_dmamap_load(sc->sc_dmat, dmap, xa->data, xa->datalen, NULL,
	    (xa->flags & ATA_F_NOWAIT) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
	if (error != 0) {
		printf("%s: error %d loading dmamap\n", PORTNAME(sp), error);
		return (1);
	}

	if (dmap->dm_nsegs > sgllen)
		ce = &sgl[sgllen - 1];

	for (i = 0; i < dmap->dm_nsegs; i++) {
		if (nsge == ce) {
			nsge++;

			addr = ccb->ccb_cmd_dva;
			addr += ((u_int8_t *)nsge - (u_int8_t *)ccb->ccb_cmd);

			ce->addr_lo = htole32((u_int32_t)addr);
			ce->addr_hi = htole32((u_int32_t)(addr >> 32));
			ce->flags = htole32(SILI_SGE_LNK);

			if ((dmap->dm_nsegs - i) > SILI_SGT_SGLLEN)
				ce += SILI_SGT_SGLLEN;
			else
				ce = NULL;
		}

		sgl = nsge;

		addr = dmap->dm_segs[i].ds_addr;
		sgl->addr_lo = htole32((u_int32_t)addr);
		sgl->addr_hi = htole32((u_int32_t)(addr >> 32));
		sgl->data_count = htole32(dmap->dm_segs[i].ds_len);
		sgl->flags = 0;

		nsge++;
	}
	sgl->flags |= htole32(SILI_SGE_TRM);

	bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    (xa->flags & ATA_F_READ) ? BUS_DMASYNC_PREREAD :
	    BUS_DMASYNC_PREWRITE);

	return (0);
}

void
sili_unload(struct sili_ccb *ccb)
{
	struct sili_port		*sp = ccb->ccb_port;
	struct sili_softc		*sc = sp->sp_sc;
	struct ata_xfer			*xa = &ccb->ccb_xa;
	bus_dmamap_t			dmap = ccb->ccb_dmamap;

	if (xa->datalen == 0)
		return;

	bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    (xa->flags & ATA_F_READ) ? BUS_DMASYNC_POSTREAD :
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, dmap);

	if (xa->flags & ATA_F_READ)
		xa->resid = xa->datalen - sili_pread(sp,
		    SILI_PREG_RX_COUNT(xa->tag));
	else
		xa->resid = 0;
}

int
sili_poll(struct sili_ccb *ccb, int timeout, void (*timeout_fn)(void *))
{
	struct sili_port		*sp = ccb->ccb_port;
	int				s;

	s = splbio();
	sili_start(sp, ccb);
	do {
		if (sili_port_intr(sp, -1) & (1 << ccb->ccb_xa.tag)) {
			splx(s);
			return (ccb->ccb_xa.state != ATA_S_COMPLETE);
		}

		delay(1000);
	} while (--timeout > 0);

	/* Run timeout while at splbio, otherwise sili_intr could interfere. */
	if (timeout_fn != NULL)
		timeout_fn(ccb);

	splx(s);

	return (1);
}

void
sili_start(struct sili_port *sp, struct sili_ccb *ccb)
{
	int				slot = ccb->ccb_xa.tag;

	splassert(IPL_BIO);
	KASSERT(ccb->ccb_xa.state == ATA_S_PENDING);
	KASSERT(sp->sp_pmp_error_recovery == 0);

	TAILQ_INSERT_TAIL(&sp->sp_active_ccbs, ccb, ccb_entry);
	sp->sp_active |= 1 << slot;
	ccb->ccb_xa.state = ATA_S_ONCHIP;

	sili_post_indirect(sp, ccb);
}

int
sili_read_ncq_error(struct sili_port *sp, int *err_slotp, int pmp_port)
{
	struct sili_softc		*sc = sp->sp_sc;
	struct sili_prb_ata		read_10h;
	u_int64_t			addr;
	struct ata_fis_h2d		*fis;
	struct ata_log_page_10h		*log;
	struct sili_ccb			*ccb;
	int				rc;

	sili_pwrite(sp, SILI_PREG_PCS, SILI_PREG_PCS_PORTINIT);
	if (!sili_pwait_eq(sp, SILI_PREG_PCS, SILI_PREG_PCS_PORTRDY,
	    SILI_PREG_PCS_PORTRDY, 1000)) {
		printf("%s: couldn't ready port during log page read\n",
		    PORTNAME(sp));
		return (1);
	}

	/* READ LOG EXT 10h into scratch space */
	bzero(&read_10h, sizeof(read_10h));
	read_10h.control = htole16(SILI_PRB_INTERRUPT_MASK);

	addr = SILI_DMA_DVA(sp->sp_scratch);
	read_10h.sgl[0].addr_lo = htole32((u_int32_t)addr);
	read_10h.sgl[0].addr_hi = htole32((u_int32_t)(addr >> 32));
	read_10h.sgl[0].data_count = htole32(512);
	read_10h.sgl[0].flags = htole32(SILI_SGE_TRM);

	fis = (struct ata_fis_h2d *)read_10h.fis;
	fis->type = ATA_FIS_TYPE_H2D;
	fis->flags = ATA_H2D_FLAGS_CMD | pmp_port;
	fis->command = ATA_C_READ_LOG_EXT;
	fis->lba_low = 0x10;		/* queued error log page (10h) */
	fis->sector_count = 1;		/* number of sectors (1) */
	fis->sector_count_exp = 0;
	fis->lba_mid = 0;		/* starting offset */
	fis->lba_mid_exp = 0;
	fis->device = 0;

	bus_dmamap_sync(sc->sc_dmat, SILI_DMA_MAP(sp->sp_scratch), 0,
	    512, BUS_DMASYNC_PREREAD);

	/* issue read and poll for completion */
	sili_post_direct(sp, 0, &read_10h, sizeof(read_10h));
	rc = sili_pwait_eq(sp, SILI_PREG_PSS, (1 << 0), 0, 1000);

	bus_dmamap_sync(sc->sc_dmat, SILI_DMA_MAP(sp->sp_scratch), 0,
	    512, BUS_DMASYNC_POSTREAD);

	if (!rc) {
		DPRINTF(SILI_D_VERBOSE, "%s: timed out while waiting for log "
		    "page read\n", PORTNAME(sp));
		return (1);
	}

	/* Extract failed register set and tags from the scratch space. */
	log = (struct ata_log_page_10h *)SILI_DMA_KVA(sp->sp_scratch);
	if (ISSET(log->err_regs.type, ATA_LOG_10H_TYPE_NOTQUEUED)) {
		/* Not queued bit was set - wasn't an NCQ error? */
		printf("%s: read NCQ error page, but not an NCQ error?\n",
		    PORTNAME(sp));
		return (1);
	}

	/* Copy back the log record as a D2H register FIS. */
	*err_slotp = log->err_regs.type & ATA_LOG_10H_TYPE_TAG_MASK;

	ccb = &sp->sp_ccbs[*err_slotp];
	memcpy(&ccb->ccb_xa.rfis, &log->err_regs, sizeof(struct ata_fis_d2h));
	ccb->ccb_xa.rfis.type = ATA_FIS_TYPE_D2H;
	ccb->ccb_xa.rfis.flags = 0;

	return (0);
}

struct ata_xfer *
sili_ata_get_xfer(void *xsc, int port)
{
	struct sili_softc		*sc = xsc;
	struct sili_port		*sp = &sc->sc_ports[port];
	struct sili_ccb			*ccb;

	ccb = sili_get_ccb(sp);
	if (ccb == NULL) {
		printf("%s: sili_ata_get_xfer NULL ccb!\n", PORTNAME(sp));
		return (NULL);
	}

	bzero(ccb->ccb_cmd, SILI_CMD_LEN);

	return ((struct ata_xfer *)ccb);
}

void
sili_ata_put_xfer(struct ata_xfer *xa)
{
	struct sili_ccb			*ccb = (struct sili_ccb *)xa;

	sili_put_ccb(ccb);
}

/* PMP register ops */
int
sili_pmp_read(struct sili_port *sp, int target, int which, u_int32_t *datap)
{
	struct sili_ccb	*ccb;
	struct sili_prb	*prb;
	struct ata_fis_h2d *fis;
	int error;

	ccb = sili_get_ccb(sp);
	if (ccb == NULL) {
		printf("%s: sili_pmp_read NULL ccb!\n", PORTNAME(sp));
		return (1);
	}
	ccb->ccb_xa.flags = ATA_F_POLL | ATA_F_GET_RFIS;
	ccb->ccb_xa.complete = sili_dummy_done;
	ccb->ccb_xa.pmp_port = SATA_PMP_CONTROL_PORT;
	ccb->ccb_xa.state = ATA_S_PENDING;
	
	prb = ccb->ccb_cmd;
	bzero(prb, sizeof(*prb));
	fis = (struct ata_fis_h2d *)&prb->fis;
	fis->type = ATA_FIS_TYPE_H2D;
	fis->flags = ATA_H2D_FLAGS_CMD | SATA_PMP_CONTROL_PORT;
	fis->command = ATA_C_READ_PM;
	fis->features = which;
	fis->device = target | ATA_H2D_DEVICE_LBA;
	fis->control = ATA_FIS_CONTROL_4BIT;

	if (sili_poll(ccb, 1000, sili_pmp_op_timeout) != 0) {
		printf("sili_pmp_read(%d, %d) failed\n", target, which);
		error = 1;
	} else {
		*datap = ccb->ccb_xa.rfis.sector_count |
		    (ccb->ccb_xa.rfis.lba_low << 8) |
		    (ccb->ccb_xa.rfis.lba_mid << 16) |
		    (ccb->ccb_xa.rfis.lba_high << 24);
		error = 0;
	}
	sili_put_ccb(ccb);
	return (error);
}

int
sili_pmp_write(struct sili_port *sp, int target, int which, u_int32_t data)
{
	struct sili_ccb	*ccb;
	struct sili_prb	*prb;
	struct ata_fis_h2d *fis;
	int error;

	ccb = sili_get_ccb(sp);
	if (ccb == NULL) {
		printf("%s: sili_pmp_write NULL ccb!\n", PORTNAME(sp));
		return (1);
	}
	ccb->ccb_xa.complete = sili_dummy_done;
	ccb->ccb_xa.flags = ATA_F_POLL;
	ccb->ccb_xa.pmp_port = SATA_PMP_CONTROL_PORT;
	ccb->ccb_xa.state = ATA_S_PENDING;

	prb = ccb->ccb_cmd;
	bzero(prb, sizeof(*prb));
	fis = (struct ata_fis_h2d *)&prb->fis;
	fis->type = ATA_FIS_TYPE_H2D;
	fis->flags = ATA_H2D_FLAGS_CMD | SATA_PMP_CONTROL_PORT;
	fis->command = ATA_C_WRITE_PM;
	fis->features = which;
	fis->device = target | ATA_H2D_DEVICE_LBA;
	fis->sector_count = (u_int8_t)data;
	fis->lba_low = (u_int8_t)(data >> 8);
	fis->lba_mid = (u_int8_t)(data >> 16);
	fis->lba_high = (u_int8_t)(data >> 24);
	fis->control = ATA_FIS_CONTROL_4BIT;

	error = sili_poll(ccb, 1000, sili_pmp_op_timeout);
	sili_put_ccb(ccb);
	return (error);
}

int
sili_pmp_phy_status(struct sili_port *sp, int target, u_int32_t *datap)
{
	int error;

	error = sili_pmp_read(sp, target, SATA_PMREG_SSTS, datap);
	if (error == 0)
		error = sili_pmp_write(sp, target, SATA_PMREG_SERR, -1);
	if (error)
		*datap = 0;

	return (error);
}

int
sili_pmp_identify(struct sili_port *sp, int *ret_nports)
{
	u_int32_t chipid;
	u_int32_t rev;
	u_int32_t nports;
	u_int32_t features;
	u_int32_t enabled;

	if (sili_pmp_read(sp, 15, 0, &chipid) ||
	    sili_pmp_read(sp, 15, 1, &rev) ||
	    sili_pmp_read(sp, 15, 2, &nports) ||
	    sili_pmp_read(sp, 15, SATA_PMREG_FEA, &features) ||
	    sili_pmp_read(sp, 15, SATA_PMREG_FEAEN, &enabled)) {
		printf("%s: port multiplier identification failed\n",
		    PORTNAME(sp));
		return (1);
	}

	nports &= 0x0F;

	/* ignore SEMB port on SiI3726 port multiplier chips */
	if (chipid == 0x37261095) {
		nports--;
	}

	printf("%s: port multiplier found: chip=%08x rev=0x%b nports=%d, "
	    "features: 0x%b, enabled: 0x%b\n", PORTNAME(sp), chipid, rev,
	    SATA_PFMT_PM_REV, nports, features, SATA_PFMT_PM_FEA, enabled,
	    SATA_PFMT_PM_FEA);

	*ret_nports = nports;
	return (0);
}
