/*	$OpenBSD: siop_common.c,v 1.46 2024/09/01 03:08:56 jsg Exp $ */
/*	$NetBSD: siop_common.c,v 1.37 2005/02/27 00:27:02 perry Exp $	*/

/*
 * Copyright (c) 2000, 2002 Manuel Bouyer.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* SYM53c7/8xx PCI-SCSI I/O Processors driver */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/scsiio.h>
#include <sys/endian.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_message.h>
#include <scsi/scsiconf.h>

#define SIOP_NEEDS_PERIOD_TABLES
#include <dev/ic/siopreg.h>
#include <dev/ic/siopvar_common.h>
#include <dev/ic/siopvar.h>

#undef DEBUG
#undef DEBUG_DR
#undef DEBUG_NEG

int
siop_common_attach(struct siop_common_softc *sc)
{
	int error, i, buswidth;
	bus_dma_segment_t seg;
	int rseg;

	/*
	 * Allocate DMA-safe memory for the script and map it.
	 */
	if ((sc->features & SF_CHIP_RAM) == 0) {
		error = bus_dmamem_alloc(sc->sc_dmat, PAGE_SIZE,
		    PAGE_SIZE, 0, &seg, 1, &rseg, BUS_DMA_NOWAIT);
		if (error) {
			printf("%s: unable to allocate script DMA memory, "
			    "error = %d\n", sc->sc_dev.dv_xname, error);
			return error;
		}
		error = bus_dmamem_map(sc->sc_dmat, &seg, rseg, PAGE_SIZE,
		    (caddr_t *)&sc->sc_script,
		    BUS_DMA_NOWAIT|BUS_DMA_COHERENT);
		if (error) {
			printf("%s: unable to map script DMA memory, "
			    "error = %d\n", sc->sc_dev.dv_xname, error);
			return error;
		}
		error = bus_dmamap_create(sc->sc_dmat, PAGE_SIZE, 1,
		    PAGE_SIZE, 0, BUS_DMA_NOWAIT, &sc->sc_scriptdma);
		if (error) {
			printf("%s: unable to create script DMA map, "
			    "error = %d\n", sc->sc_dev.dv_xname, error);
			return error;
		}
		error = bus_dmamap_load(sc->sc_dmat, sc->sc_scriptdma,
		    sc->sc_script, PAGE_SIZE, NULL, BUS_DMA_NOWAIT);
		if (error) {
			printf("%s: unable to load script DMA map, "
			    "error = %d\n", sc->sc_dev.dv_xname, error);
			return error;
		}
		sc->sc_scriptaddr =
		    sc->sc_scriptdma->dm_segs[0].ds_addr;
		sc->ram_size = PAGE_SIZE;
	}

	/*
	 * sc->sc_link is the template for all device sc_link's
	 * for devices attached to this adapter. It is passed to
	 * the upper layers in config_found().
	 */
	buswidth = (sc->features & SF_BUS_WIDE) ? 16 : 8;
	sc->sc_id = bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_SCID);
	if (sc->sc_id == 0 || sc->sc_id >= buswidth)
		sc->sc_id = SIOP_DEFAULT_TARGET;

	for (i = 0; i < 16; i++)
		sc->targets[i] = NULL;

	/* find min/max sync period for this chip */
	sc->st_maxsync = 0;
	sc->dt_maxsync = 0;
	sc->st_minsync = 255;
	sc->dt_minsync = 255;
	for (i = 0; i < sizeof(scf_period) / sizeof(scf_period[0]); i++) {
		if (sc->clock_period != scf_period[i].clock)
			continue;
		if (sc->st_maxsync < scf_period[i].period)
			sc->st_maxsync = scf_period[i].period;
		if (sc->st_minsync > scf_period[i].period)
			sc->st_minsync = scf_period[i].period;
	}
	if (sc->st_maxsync == 255 || sc->st_minsync == 0)
		panic("siop: can't find my sync parameters");
	for (i = 0; i < sizeof(dt_scf_period) / sizeof(dt_scf_period[0]); i++) {
		if (sc->clock_period != dt_scf_period[i].clock)
			continue;
		if (sc->dt_maxsync < dt_scf_period[i].period)
			sc->dt_maxsync = dt_scf_period[i].period;
		if (sc->dt_minsync > dt_scf_period[i].period)
			sc->dt_minsync = dt_scf_period[i].period;
	}
	if (sc->dt_maxsync == 255 || sc->dt_minsync == 0)
		panic("siop: can't find my sync parameters");
	return 0;
}

void
siop_common_reset(struct siop_common_softc *sc)
{
	u_int32_t stest3;

	/* reset the chip */
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_ISTAT, ISTAT_SRST);
	delay(1000);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_ISTAT, 0);

	/* init registers */
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SCNTL0,
	    SCNTL0_ARB_MASK | SCNTL0_EPC | SCNTL0_AAP);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SCNTL1, 0);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SCNTL3, sc->clock_div);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SXFER, 0);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_DIEN, 0xff);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SIEN0,
	    0xff & ~(SIEN0_CMP | SIEN0_SEL | SIEN0_RSL));
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SIEN1,
	    0xff & ~(SIEN1_HTH | SIEN1_GEN));
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_STEST2, 0);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_STEST3, STEST3_TE);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_STIME0,
	    (0xb << STIME0_SEL_SHIFT));
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SCID,
	    sc->sc_id | SCID_RRE);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_RESPID0,
	    1 << sc->sc_id);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_DCNTL,
	    (sc->features & SF_CHIP_PF) ? DCNTL_COM | DCNTL_PFEN : DCNTL_COM);
	if (sc->features & SF_CHIP_AAIP)
		bus_space_write_1(sc->sc_rt, sc->sc_rh,
		    SIOP_AIPCNTL1, AIPCNTL1_DIS);

	/* enable clock doubler or quadrupler if appropriate */
	if (sc->features & (SF_CHIP_DBLR | SF_CHIP_QUAD)) {
		stest3 = bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_STEST3);
		bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_STEST1,
		    STEST1_DBLEN);
		if (sc->features & SF_CHIP_QUAD) {
			/* wait for PPL to lock */
			while ((bus_space_read_1(sc->sc_rt, sc->sc_rh,
			    SIOP_STEST4) & STEST4_LOCK) == 0)
				delay(10);
		} else {
			/* data sheet says 20us - more won't hurt */
			delay(100);
		}
		/* halt scsi clock, select doubler/quad, restart clock */
		bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_STEST3,
		    stest3 | STEST3_HSC);
		bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_STEST1,
		    STEST1_DBLEN | STEST1_DBLSEL);
		bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_STEST3, stest3);
	} else {
		bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_STEST1, 0);
	}
	if (sc->features & SF_CHIP_FIFO)
		bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_CTEST5,
		    bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_CTEST5) |
		    CTEST5_DFS);
	if (sc->features & SF_CHIP_LED0) {
		/* Set GPIO0 as output if software LED control is required */
		bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_GPCNTL,
		    bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_GPCNTL) & 0xfe);
	}
	if (sc->features & SF_BUS_ULTRA3) {
		/* reset SCNTL4 */
		bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SCNTL4, 0);
	}
	sc->mode = bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_STEST4) &
	    STEST4_MODE_MASK;

	/*
	 * initialise the RAM. Without this we may get scsi gross errors on
	 * the 1010
	 */
	if (sc->features & SF_CHIP_RAM)
		bus_space_set_region_4(sc->sc_ramt, sc->sc_ramh,
			0, 0, sc->ram_size / 4);
	sc->sc_reset(sc);
}

/* prepare tables before sending a cmd */
void
siop_setuptables(struct siop_common_cmd *siop_cmd)
{
	int i;
	struct siop_common_softc *sc = siop_cmd->siop_sc;
	struct scsi_xfer *xs = siop_cmd->xs;
	int target = xs->sc_link->target;
	int lun = xs->sc_link->lun;
	int msgoffset = 1;
	int *targ_flags = &sc->targets[target]->flags;
	int quirks;

	siop_cmd->siop_tables->id = siop_htoc32(sc, sc->targets[target]->id);
	memset(siop_cmd->siop_tables->msg_out, 0,
	    sizeof(siop_cmd->siop_tables->msg_out));
	/* request sense doesn't disconnect */
	if (siop_cmd->status == CMDST_SENSE)
		siop_cmd->siop_tables->msg_out[0] = MSG_IDENTIFY(lun, 0);
	else if ((sc->features & SF_CHIP_GEBUG) &&
	    (sc->targets[target]->flags & TARF_ISWIDE) == 0)
		/*
		 * 1010 bug: it seems that the 1010 has problems with reselect
		 * when not in wide mode (generate false SCSI gross error).
		 * The FreeBSD sym driver has comments about it but their
		 * workaround (disable SCSI gross error reporting) doesn't
		 * work with my adapter. So disable disconnect when not
		 * wide.
		 */
		siop_cmd->siop_tables->msg_out[0] = MSG_IDENTIFY(lun, 0);
	else
		siop_cmd->siop_tables->msg_out[0] = MSG_IDENTIFY(lun, 1);
	siop_cmd->siop_tables->t_msgout.count = siop_htoc32(sc, msgoffset);
	if (sc->targets[target]->status == TARST_ASYNC) {
		*targ_flags &= TARF_DT; /* Save TARF_DT 'cuz we don't set it here */
		quirks = xs->sc_link->quirks;

		if ((quirks & SDEV_NOTAGS) == 0)
			*targ_flags |= TARF_TAG;
		if (((quirks & SDEV_NOWIDE) == 0) &&
		    (sc->features & SF_BUS_WIDE))
			*targ_flags |= TARF_WIDE;
		if ((quirks & SDEV_NOSYNC) == 0)
			*targ_flags |= TARF_SYNC;

		if ((sc->features & SF_CHIP_GEBUG) &&
		    (*targ_flags & TARF_WIDE) == 0)
			/* 
			 * 1010 workaround: can't do disconnect if not wide,
			 * so can't do tag
			 */
			*targ_flags &= ~TARF_TAG;

		/* Safe to call siop_add_dev() multiple times */
		siop_add_dev((struct siop_softc *)sc, target, lun);

		if ((*targ_flags & TARF_DT) &&
		    (sc->mode == STEST4_MODE_LVD)) {
			sc->targets[target]->status = TARST_PPR_NEG;
			siop_ppr_msg(siop_cmd, msgoffset, sc->dt_minsync,
			    sc->maxoff);
		} else if (*targ_flags & TARF_WIDE) {
			sc->targets[target]->status = TARST_WIDE_NEG;
			siop_wdtr_msg(siop_cmd, msgoffset,
			    MSG_EXT_WDTR_BUS_16_BIT);
		} else if (*targ_flags & TARF_SYNC) {
			sc->targets[target]->status = TARST_SYNC_NEG;
			siop_sdtr_msg(siop_cmd, msgoffset, sc->st_minsync,
			(sc->maxoff > 31) ? 31 :  sc->maxoff);
		} else {
			sc->targets[target]->status = TARST_OK;
			siop_update_xfer_mode(sc, target);
		}
	} else if (sc->targets[target]->status == TARST_OK &&
	    (*targ_flags & TARF_TAG) &&
	    siop_cmd->status != CMDST_SENSE) {
		siop_cmd->flags |= CMDFL_TAG;
	}
	siop_cmd->siop_tables->status =
	    siop_htoc32(sc, SCSI_SIOP_NOSTATUS); /* set invalid status */

	if ((xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) ||
	    siop_cmd->status == CMDST_SENSE) {
		bzero(siop_cmd->siop_tables->data,
		    sizeof(siop_cmd->siop_tables->data));
		for (i = 0; i < siop_cmd->dmamap_data->dm_nsegs; i++) {
			siop_cmd->siop_tables->data[i].count =
			    siop_htoc32(sc,
				siop_cmd->dmamap_data->dm_segs[i].ds_len);
			siop_cmd->siop_tables->data[i].addr =
			    siop_htoc32(sc,
				siop_cmd->dmamap_data->dm_segs[i].ds_addr);
		}
	}
}

int
siop_wdtr_neg(struct siop_common_cmd *siop_cmd)
{
	struct siop_common_softc *sc = siop_cmd->siop_sc;
	struct siop_common_target *siop_target = siop_cmd->siop_target;
	int target = siop_cmd->xs->sc_link->target;
	struct siop_common_xfer *tables = siop_cmd->siop_tables;

	if (siop_target->status == TARST_WIDE_NEG) {
		/* we initiated wide negotiation */
		switch (tables->msg_in[3]) {
		case MSG_EXT_WDTR_BUS_8_BIT:
			siop_target->flags &= ~TARF_ISWIDE;
			sc->targets[target]->id &= ~(SCNTL3_EWS << 24);
			break;
		case MSG_EXT_WDTR_BUS_16_BIT:
			if (siop_target->flags & TARF_WIDE) {
				siop_target->flags |= TARF_ISWIDE;
				sc->targets[target]->id |= (SCNTL3_EWS << 24);
				break;
			}
		/* FALLTHROUGH */
		default:
			/*
 			 * hum, we got more than what we can handle, shouldn't
			 * happen. Reject, and stay async
			 */
			siop_target->flags &= ~TARF_ISWIDE;
			siop_target->status = TARST_OK;
			siop_target->offset = siop_target->period = 0;
			siop_update_xfer_mode(sc, target);
			printf("%s: rejecting invalid wide negotiation from "
			    "target %d (%d)\n", sc->sc_dev.dv_xname, target,
			    tables->msg_in[3]);
			tables->t_msgout.count = siop_htoc32(sc, 1);
			tables->msg_out[0] = MSG_MESSAGE_REJECT;
			return SIOP_NEG_MSGOUT;
		}
		tables->id = siop_htoc32(sc, sc->targets[target]->id);
		bus_space_write_1(sc->sc_rt, sc->sc_rh,
		    SIOP_SCNTL3,
		    (sc->targets[target]->id >> 24) & 0xff);
		/* we now need to do sync */
		if (siop_target->flags & TARF_SYNC) {
			siop_target->status = TARST_SYNC_NEG;
			siop_sdtr_msg(siop_cmd, 0, sc->st_minsync,
			    (sc->maxoff > 31) ? 31 : sc->maxoff);
			return SIOP_NEG_MSGOUT;
		} else {
			siop_target->status = TARST_OK;
			siop_update_xfer_mode(sc, target);
			return SIOP_NEG_ACK;
		}
	} else {
		/* target initiated wide negotiation */
		if (tables->msg_in[3] >= MSG_EXT_WDTR_BUS_16_BIT
		    && (siop_target->flags & TARF_WIDE)) {
			siop_target->flags |= TARF_ISWIDE;
			sc->targets[target]->id |= SCNTL3_EWS << 24;
		} else {
			siop_target->flags &= ~TARF_ISWIDE;
			sc->targets[target]->id &= ~(SCNTL3_EWS << 24);
		}
		tables->id = siop_htoc32(sc, sc->targets[target]->id);
		bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SCNTL3,
		    (sc->targets[target]->id >> 24) & 0xff);
		/*
		 * we did reset wide parameters, so fall back to async,
		 * but don't schedule a sync neg, target should initiate it
		 */
		siop_target->status = TARST_OK;
		siop_target->offset = siop_target->period = 0;
		siop_update_xfer_mode(sc, target);
		siop_wdtr_msg(siop_cmd, 0, (siop_target->flags & TARF_ISWIDE) ?
		    MSG_EXT_WDTR_BUS_16_BIT : MSG_EXT_WDTR_BUS_8_BIT);
		return SIOP_NEG_MSGOUT;
	}
}

int
siop_ppr_neg(struct siop_common_cmd *siop_cmd)
{
	struct siop_common_softc *sc = siop_cmd->siop_sc;
	struct siop_common_target *siop_target = siop_cmd->siop_target;
	int target = siop_cmd->xs->sc_link->target;
	struct siop_common_xfer *tables = siop_cmd->siop_tables;
	int sync, offset, options, scf = 0;
	int i;

#ifdef DEBUG_NEG
	printf("%s: answer on ppr negotiation:", sc->sc_dev.dv_xname);
	for (i = 0; i < 8; i++)
		printf(" 0x%x", tables->msg_in[i]);
	printf("\n");
#endif

	if (siop_target->status == TARST_PPR_NEG) {
		/* we initiated PPR negotiation */
		sync = tables->msg_in[3];
		offset = tables->msg_in[5];
		options = tables->msg_in[7];
		if (options != MSG_EXT_PPR_PROT_DT) {
			/* shouldn't happen */
			printf("%s: ppr negotiation for target %d: "
			    "no DT option\n", sc->sc_dev.dv_xname, target);
			siop_target->status = TARST_ASYNC;
			siop_target->flags &= ~(TARF_DT | TARF_ISDT);
			siop_target->offset = 0;
			siop_target->period = 0;
			goto reject;
		}

		if (offset > sc->maxoff || sync < sc->dt_minsync ||
		    sync > sc->dt_maxsync) {
			printf("%s: ppr negotiation for target %d: "
			    "offset (%d) or sync (%d) out of range\n",
			    sc->sc_dev.dv_xname, target, offset, sync);
			/* should not happen */
			siop_target->status = TARST_ASYNC;
			siop_target->flags &= ~(TARF_DT | TARF_ISDT);
			siop_target->offset = 0;
			siop_target->period = 0;
			goto reject;
		} else {
			for (i = 0; i <
			    sizeof(dt_scf_period) / sizeof(dt_scf_period[0]);
			    i++) {
				if (sc->clock_period != dt_scf_period[i].clock)
					continue;
				if (dt_scf_period[i].period == sync) {
					/* ok, found it. we now are sync. */
					siop_target->offset = offset;
					siop_target->period = sync;
					scf = dt_scf_period[i].scf;
					siop_target->flags |= TARF_ISDT;
				}
			}
			if ((siop_target->flags & TARF_ISDT) == 0) {
				printf("%s: ppr negotiation for target %d: "
				    "sync (%d) incompatible with adapter\n",
				    sc->sc_dev.dv_xname, target, sync);
				/*
				 * we didn't find it in our table, do async
				 * send reject msg, start SDTR/WDTR neg
				 */
				siop_target->status = TARST_ASYNC;
				siop_target->flags &= ~(TARF_DT | TARF_ISDT);
				siop_target->offset = 0;
				siop_target->period = 0;
				goto reject;
			}
		}
		if (tables->msg_in[6] != 1) {
			printf("%s: ppr negotiation for target %d: "
			    "transfer width (%d) incompatible with dt\n",
			    sc->sc_dev.dv_xname, target, tables->msg_in[6]);
			/* DT mode can only be done with wide transfers */
			siop_target->status = TARST_ASYNC;
			siop_target->flags &= ~(TARF_DT | TARF_ISDT);
			siop_target->offset = 0;
			siop_target->period = 0;
			goto reject;
		}
		siop_target->flags |= TARF_ISWIDE;
		sc->targets[target]->id |= (SCNTL3_EWS << 24);
		sc->targets[target]->id &= ~(SCNTL3_SCF_MASK << 24);
		sc->targets[target]->id |= scf << (24 + SCNTL3_SCF_SHIFT);
		sc->targets[target]->id &= ~(SXFER_MO_MASK << 8);
		sc->targets[target]->id |=
		    (siop_target->offset & SXFER_MO_MASK) << 8;
		sc->targets[target]->id &= ~0xff;
		sc->targets[target]->id |= SCNTL4_U3EN;
		siop_target->status = TARST_OK;
		siop_update_xfer_mode(sc, target);
		bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SCNTL3,
		    (sc->targets[target]->id >> 24) & 0xff);
		bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SXFER,
		    (sc->targets[target]->id >> 8) & 0xff);
		bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SCNTL4,
		    sc->targets[target]->id & 0xff);
		return SIOP_NEG_ACK;
	} else {
		/* target initiated PPR negotiation, shouldn't happen */
		printf("%s: rejecting invalid PPR negotiation from "
		    "target %d\n", sc->sc_dev.dv_xname, target);
reject:
		tables->t_msgout.count = siop_htoc32(sc, 1);
		tables->msg_out[0] = MSG_MESSAGE_REJECT;
		return SIOP_NEG_MSGOUT;
	}
}

int
siop_sdtr_neg(struct siop_common_cmd *siop_cmd)
{
	struct siop_common_softc *sc = siop_cmd->siop_sc;
	struct siop_common_target *siop_target = siop_cmd->siop_target;
	int target = siop_cmd->xs->sc_link->target;
	int sync, maxoffset, offset, i;
	int send_msgout = 0;
	struct siop_common_xfer *tables = siop_cmd->siop_tables;

	/* limit to Ultra/2 parameters, need PPR for Ultra/3 */
	maxoffset = (sc->maxoff > 31) ? 31 : sc->maxoff;

	sync = tables->msg_in[3];
	offset = tables->msg_in[4];

	if (siop_target->status == TARST_SYNC_NEG) {
		/* we initiated sync negotiation */
		siop_target->status = TARST_OK;
#ifdef DEBUG
		printf("sdtr: sync %d offset %d\n", sync, offset);
#endif
		if (offset > maxoffset || sync < sc->st_minsync ||
			sync > sc->st_maxsync)
			goto reject;
		for (i = 0; i < sizeof(scf_period) / sizeof(scf_period[0]);
		    i++) {
			if (sc->clock_period != scf_period[i].clock)
				continue;
			if (scf_period[i].period == sync) {
				/* ok, found it. we now are sync. */
				siop_target->offset = offset;
				siop_target->period = sync;
				sc->targets[target]->id &=
				    ~(SCNTL3_SCF_MASK << 24);
				sc->targets[target]->id |= scf_period[i].scf
				    << (24 + SCNTL3_SCF_SHIFT);
				if (sync < 25 && /* Ultra */
				    (sc->features & SF_BUS_ULTRA3) == 0)
					sc->targets[target]->id |=
					    SCNTL3_ULTRA << 24;
				else
					sc->targets[target]->id &=
					    ~(SCNTL3_ULTRA << 24);
				sc->targets[target]->id &=
				    ~(SXFER_MO_MASK << 8);
				sc->targets[target]->id |=
				    (offset & SXFER_MO_MASK) << 8;
				sc->targets[target]->id &= ~0xff; /* scntl4 */
				goto end;
			}
		}
		/*
		 * we didn't find it in our table, do async and send reject
		 * msg
		 */
reject:
		send_msgout = 1;
		tables->t_msgout.count = siop_htoc32(sc, 1);
		tables->msg_out[0] = MSG_MESSAGE_REJECT;
		sc->targets[target]->id &= ~(SCNTL3_SCF_MASK << 24);
		sc->targets[target]->id &= ~(SCNTL3_ULTRA << 24);
		sc->targets[target]->id &= ~(SXFER_MO_MASK << 8);
		sc->targets[target]->id &= ~0xff; /* scntl4 */
		siop_target->offset = siop_target->period = 0;
	} else { /* target initiated sync neg */
#ifdef DEBUG
		printf("sdtr (target): sync %d offset %d\n", sync, offset);
#endif
		if (offset == 0 || sync > sc->st_maxsync) { /* async */
			goto async;
		}
		if (offset > maxoffset)
			offset = maxoffset;
		if (sync < sc->st_minsync)
			sync = sc->st_minsync;
		/* look for sync period */
		for (i = 0; i < sizeof(scf_period) / sizeof(scf_period[0]);
		    i++) {
			if (sc->clock_period != scf_period[i].clock)
				continue;
			if (scf_period[i].period == sync) {
				/* ok, found it. we now are sync. */
				siop_target->offset = offset;
				siop_target->period = sync;
				sc->targets[target]->id &=
				    ~(SCNTL3_SCF_MASK << 24);
				sc->targets[target]->id |= scf_period[i].scf
				    << (24 + SCNTL3_SCF_SHIFT);
				if (sync < 25 && /* Ultra */
				    (sc->features & SF_BUS_ULTRA3) == 0)
					sc->targets[target]->id |=
					    SCNTL3_ULTRA << 24;
				else
					sc->targets[target]->id &=
					    ~(SCNTL3_ULTRA << 24);
				sc->targets[target]->id &=
				    ~(SXFER_MO_MASK << 8);
				sc->targets[target]->id |=
				    (offset & SXFER_MO_MASK) << 8;
				sc->targets[target]->id &= ~0xff; /* scntl4 */
				siop_sdtr_msg(siop_cmd, 0, sync, offset);
				send_msgout = 1;
				goto end;
			}
		}
async:
		siop_target->offset = siop_target->period = 0;
		sc->targets[target]->id &= ~(SCNTL3_SCF_MASK << 24);
		sc->targets[target]->id &= ~(SCNTL3_ULTRA << 24);
		sc->targets[target]->id &= ~(SXFER_MO_MASK << 8);
		sc->targets[target]->id &= ~0xff; /* scntl4 */
		siop_sdtr_msg(siop_cmd, 0, 0, 0);
		send_msgout = 1;
	}
end:
	if (siop_target->status == TARST_OK)
		siop_update_xfer_mode(sc, target);
#ifdef DEBUG
	printf("id now 0x%x\n", sc->targets[target]->id);
#endif
	tables->id = siop_htoc32(sc, sc->targets[target]->id);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SCNTL3,
	    (sc->targets[target]->id >> 24) & 0xff);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SXFER,
	    (sc->targets[target]->id >> 8) & 0xff);
	if (send_msgout) {
		return SIOP_NEG_MSGOUT;
	} else {
		return SIOP_NEG_ACK;
	}
}

void
siop_sdtr_msg(struct siop_common_cmd *siop_cmd, int offset, int ssync, int soff)
{
	siop_cmd->siop_tables->msg_out[offset + 0] = MSG_EXTENDED;
	siop_cmd->siop_tables->msg_out[offset + 1] = MSG_EXT_SDTR_LEN;
	siop_cmd->siop_tables->msg_out[offset + 2] = MSG_EXT_SDTR;
	siop_cmd->siop_tables->msg_out[offset + 3] = ssync;
	siop_cmd->siop_tables->msg_out[offset + 4] = soff;
	siop_cmd->siop_tables->t_msgout.count =
	    siop_htoc32(siop_cmd->siop_sc, offset + MSG_EXT_SDTR_LEN + 2);
}

void
siop_wdtr_msg(struct siop_common_cmd *siop_cmd, int offset, int wide)
{
	siop_cmd->siop_tables->msg_out[offset + 0] = MSG_EXTENDED;
	siop_cmd->siop_tables->msg_out[offset + 1] = MSG_EXT_WDTR_LEN;
	siop_cmd->siop_tables->msg_out[offset + 2] = MSG_EXT_WDTR;
	siop_cmd->siop_tables->msg_out[offset + 3] = wide;
	siop_cmd->siop_tables->t_msgout.count =
	    siop_htoc32(siop_cmd->siop_sc, offset + MSG_EXT_WDTR_LEN + 2);
}

void
siop_ppr_msg(struct siop_common_cmd *siop_cmd, int offset, int ssync, int soff)
{
	siop_cmd->siop_tables->msg_out[offset + 0] = MSG_EXTENDED;
	siop_cmd->siop_tables->msg_out[offset + 1] = MSG_EXT_PPR_LEN;
	siop_cmd->siop_tables->msg_out[offset + 2] = MSG_EXT_PPR;
	siop_cmd->siop_tables->msg_out[offset + 3] = ssync;
	siop_cmd->siop_tables->msg_out[offset + 4] = 0; /* reserved */
	siop_cmd->siop_tables->msg_out[offset + 5] = soff;
	siop_cmd->siop_tables->msg_out[offset + 6] = 1; /* wide */
	siop_cmd->siop_tables->msg_out[offset + 7] = MSG_EXT_PPR_PROT_DT;
	siop_cmd->siop_tables->t_msgout.count =
	    siop_htoc32(siop_cmd->siop_sc, offset + MSG_EXT_PPR_LEN + 2);
}

void
siop_ma(struct siop_common_cmd *siop_cmd)
{
	int offset, dbc, sstat;
	struct siop_common_softc *sc = siop_cmd->siop_sc;
	scr_table_t *table; /* table with partial xfer */

	/*
	 * compute how much of the current table didn't get handled when
	 * a phase mismatch occurs
	 */
	if ((siop_cmd->xs->flags & (SCSI_DATA_OUT | SCSI_DATA_IN))
	    == 0)
	    return; /* no valid data transfer */

	offset = bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_SCRATCHA + 1);
	if (offset >= SIOP_NSG) {
		printf("%s: bad offset in siop_sdp (%d)\n",
		    sc->sc_dev.dv_xname, offset);
		return;
	}
	table = &siop_cmd->siop_tables->data[offset];
#ifdef DEBUG_DR
	printf("siop_ma: offset %d count=%d addr=0x%x ", offset,
	    table->count, table->addr);
#endif
	dbc = bus_space_read_4(sc->sc_rt, sc->sc_rh, SIOP_DBC) & 0x00ffffff;
	if (siop_cmd->xs->flags & SCSI_DATA_OUT) {
		if (sc->features & SF_CHIP_DFBC) {
			dbc +=
			    bus_space_read_2(sc->sc_rt, sc->sc_rh, SIOP_DFBC);
		} else {
			/* need to account stale data in FIFO */
			int dfifo =
			    bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_DFIFO);
			if (sc->features & SF_CHIP_FIFO) {
				dfifo |= (bus_space_read_1(sc->sc_rt, sc->sc_rh,
				    SIOP_CTEST5) & CTEST5_BOMASK) << 8;
				dbc += (dfifo - (dbc & 0x3ff)) & 0x3ff;
			} else {
				dbc += (dfifo - (dbc & 0x7f)) & 0x7f;
			}
		}
		sstat = bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_SSTAT0);
		if (sstat & SSTAT0_OLF)
			dbc++;
		if ((sstat & SSTAT0_ORF) && (sc->features & SF_CHIP_DFBC) == 0)
			dbc++;
		if (siop_cmd->siop_target->flags & TARF_ISWIDE) {
			sstat = bus_space_read_1(sc->sc_rt, sc->sc_rh,
			    SIOP_SSTAT2);
			if (sstat & SSTAT2_OLF1)
				dbc++;
			if ((sstat & SSTAT2_ORF1) &&
			    (sc->features & SF_CHIP_DFBC) == 0)
				dbc++;
		}
		/* clear the FIFO */
		bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_CTEST3,
		    bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_CTEST3) |
		    CTEST3_CLF);
	}
	siop_cmd->flags |= CMDFL_RESID;
	siop_cmd->resid = dbc;
}

void
siop_sdp(struct siop_common_cmd *siop_cmd, int offset)
{
	struct siop_common_softc *sc = siop_cmd->siop_sc;
	scr_table_t *table;

	if ((siop_cmd->xs->flags & (SCSI_DATA_OUT | SCSI_DATA_IN))== 0)
		return; /* no data pointers to save */

	/*
	 * offset == SIOP_NSG may be a valid condition if we get a Save data
	 * pointer when the xfer is done. Just ignore the Save data pointer
	 * in this case
	 */
	if (offset == SIOP_NSG)
		return;
#ifdef DIAGNOSTIC
	if (offset > SIOP_NSG) {
		sc_print_addr(siop_cmd->xs->sc_link);
		printf("offset %d > %d\n", offset, SIOP_NSG);
		panic("siop_sdp: offset");
	}
#endif
	/*
	 * Save data pointer. We do this by adjusting the tables to point
	 * at the beginning of the data not yet transferred.
	 * offset points to the first table with untransferred data.
	 */

	/*
	 * before doing that we decrease resid from the amount of data which
	 * has been transferred.
	 */
	siop_update_resid(siop_cmd, offset);

	/*
	 * First let see if we have a resid from a phase mismatch. If so,
	 * we have to adjust the table at offset to remove transferred data.
	 */
	if (siop_cmd->flags & CMDFL_RESID) {
		siop_cmd->flags &= ~CMDFL_RESID;
		table = &siop_cmd->siop_tables->data[offset];
		/* "cut" already transferred data from this table */
		table->addr =
		    siop_htoc32(sc, siop_ctoh32(sc, table->addr) +
		    siop_ctoh32(sc, table->count) - siop_cmd->resid);
		table->count = siop_htoc32(sc, siop_cmd->resid);
	}

	/*
	 * now we can remove entries which have been transferred.
	 * We just move the entries with data left at the beginning of the
	 * tables
	 */
	bcopy(&siop_cmd->siop_tables->data[offset],
	    &siop_cmd->siop_tables->data[0],
	    (SIOP_NSG - offset) * sizeof(scr_table_t));
}

void
siop_update_resid(struct siop_common_cmd *siop_cmd, int offset)
{
	struct siop_common_softc *sc = siop_cmd->siop_sc;
	scr_table_t *table;
	int i;

	if ((siop_cmd->xs->flags & (SCSI_DATA_OUT | SCSI_DATA_IN))
	    == 0)
	    return; /* no data to transfer */

	/*
	 * update resid. First account for the table entries which have
	 * been fully completed.
	 */
	for (i = 0; i < offset; i++)
		siop_cmd->xs->resid -=
		    siop_ctoh32(sc, siop_cmd->siop_tables->data[i].count);
	/*
	 * if CMDFL_RESID is set, the last table (pointed by offset) is a
	 * partial transfers. If not, offset points to the entry following
	 * the last full transfer.
	 */
	if (siop_cmd->flags & CMDFL_RESID) {
		table = &siop_cmd->siop_tables->data[offset];
		siop_cmd->xs->resid -=
		    siop_ctoh32(sc, table->count) - siop_cmd->resid;
	}
}

int
siop_iwr(struct siop_common_cmd *siop_cmd)
{
	int offset;
	scr_table_t *table; /* table with IWR */
	struct siop_common_softc *sc = siop_cmd->siop_sc;
	/* handle ignore wide residue messages */

	/* if target isn't wide, reject */
	if ((siop_cmd->siop_target->flags & TARF_ISWIDE) == 0) {
		siop_cmd->siop_tables->t_msgout.count = siop_htoc32(sc, 1);
		siop_cmd->siop_tables->msg_out[0] = MSG_MESSAGE_REJECT;
		return SIOP_NEG_MSGOUT;
	}
	/* get index of current command in table */
	offset = bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_SCRATCHA + 1);
	/*
	 * if the current table did complete, we're now pointing at the
	 * next one. Go back one if we didn't see a phase mismatch.
	 */
	if ((siop_cmd->flags & CMDFL_RESID) == 0)
		offset--;
	table = &siop_cmd->siop_tables->data[offset];

	if ((siop_cmd->flags & CMDFL_RESID) == 0) {
		if (siop_ctoh32(sc, table->count) & 1) {
			/* we really got the number of bytes we expected */
			return SIOP_NEG_ACK;
		} else {
			/*
			 * now we really had a short xfer, by one byte.
			 * handle it just as if we had a phase mismatch
			 * (there is a resid of one for this table).
			 * Update scratcha1 to reflect the fact that
			 * this xfer isn't complete.
			 */
			 siop_cmd->flags |= CMDFL_RESID;
			 siop_cmd->resid = 1;
			 bus_space_write_1(sc->sc_rt, sc->sc_rh,
			     SIOP_SCRATCHA + 1, offset);
			 return SIOP_NEG_ACK;
		}
	} else {
		/*
		 * we already have a short xfer for this table; it's
		 * just one byte less than we though it was
		 */
		siop_cmd->resid--;
		return SIOP_NEG_ACK;
	}
}

void
siop_clearfifo(struct siop_common_softc *sc)
{
	int timeout = 0;
	int ctest3 = bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_CTEST3);

#ifdef DEBUG_INTR
	printf("DMA fifo not empty !\n");
#endif
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_CTEST3,
	    ctest3 | CTEST3_CLF);
	while ((bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_CTEST3) &
	    CTEST3_CLF) != 0) {
		delay(1);
		if (++timeout > 1000) {
			printf("clear fifo failed\n");
			bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_CTEST3,
			    bus_space_read_1(sc->sc_rt, sc->sc_rh,
			    SIOP_CTEST3) & ~CTEST3_CLF);
			return;
		}
	}
}

int
siop_modechange(struct siop_common_softc *sc)
{
	int retry;
	int sist0, sist1, stest2;
	for (retry = 0; retry < 5; retry++) {
		/*
		 * datasheet says to wait 100ms and re-read SIST1,
		 * to check that DIFFSENSE is stable.
		 * We may delay() 5 times for  100ms at interrupt time;
		 * hopefully this will not happen often.
		 */
		delay(100000);
		sist0 = bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_SIST0);
		sist1 = bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_SIST1);
		if (sist1 & SIEN1_SBMC)
			continue; /* we got an irq again */
		sc->mode = bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_STEST4) &
		    STEST4_MODE_MASK;
		stest2 = bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_STEST2);
		switch(sc->mode) {
		case STEST4_MODE_DIF:
			printf("%s: switching to differential mode\n",
			    sc->sc_dev.dv_xname);
			bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_STEST2,
			    stest2 | STEST2_DIF);
			break;
		case STEST4_MODE_SE:
			printf("%s: switching to single-ended mode\n",
			    sc->sc_dev.dv_xname);
			bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_STEST2,
			    stest2 & ~STEST2_DIF);
			break;
		case STEST4_MODE_LVD:
			printf("%s: switching to LVD mode\n",
			    sc->sc_dev.dv_xname);
			bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_STEST2,
			    stest2 & ~STEST2_DIF);
			break;
		default:
			printf("%s: invalid SCSI mode 0x%x\n",
			    sc->sc_dev.dv_xname, sc->mode);
			return 0;
		}
		return 1;
	}
	printf("%s: timeout waiting for DIFFSENSE to stabilise\n",
	    sc->sc_dev.dv_xname);
	return 0;
}

void
siop_resetbus(struct siop_common_softc *sc)
{
	int scntl1;
	scntl1 = bus_space_read_1(sc->sc_rt, sc->sc_rh, SIOP_SCNTL1);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SCNTL1,
	    scntl1 | SCNTL1_RST);
	/* minimum 25 us, more time won't hurt */
	delay(100);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_SCNTL1, scntl1);
}

void
siop_update_xfer_mode(struct siop_common_softc *sc, int target)
{
	struct siop_common_target *siop_target;

	siop_target = sc->targets[target];

	printf("%s: target %d now using %s%s%d bit ",
            sc->sc_dev.dv_xname, target,
	    (siop_target->flags & TARF_TAG) ? "tagged " : "",
	    (siop_target->flags & TARF_ISDT) ? "DT " : "",
	    (siop_target->flags & TARF_ISWIDE) ? 16 : 8);

	if (siop_target->offset == 0)
		printf("async ");
	else {
		switch (siop_target->period) {
		case 9: /*   12.5ns cycle */
			printf("80.0");
			break;
		case 10: /*  25  ns cycle */
			printf("40.0");
			break;
		case 12: /*  48  ns cycle */
			printf("20.0");
			break;
		case 18: /*  72  ns cycle */
			printf("13.3");
			break;
		case 25: /* 100  ns cycle */
			printf("10.0");
			break;
		case 37: /* 118  ns cycle */
			printf("6.67");
			break;
		case 50: /* 200  ns cycle */
			printf("5.0");
			break;
		case 75: /* 300  ns cycle */
			printf("3.33");
			break;
		default:
			printf("??");
			break;
		}
		printf(" MHz %d REQ/ACK offset ", siop_target->offset);
	}

	printf("xfers\n");

	if ((sc->features & SF_CHIP_GEBUG) &&
	    (siop_target->flags & TARF_ISWIDE) == 0)
		/* 1010 workaround: can't do disconnect if not wide, so can't do tag */
		siop_target->flags &= ~TARF_TAG;
}
