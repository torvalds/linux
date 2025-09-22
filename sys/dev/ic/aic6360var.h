/*	$OpenBSD: aic6360var.h,v 1.11 2024/09/04 07:54:52 mglocker Exp $	*/
/*	$NetBSD: aic6360.c,v 1.52 1996/12/10 21:27:51 thorpej Exp $	*/

/*
 * Copyright (c) 1994, 1995, 1996 Charles Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Charles M. Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Copyright (c) 1994 Jarle Greipsland
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Acknowledgements: Many of the algorithms used in this driver are
 * inspired by the work of Julian Elischer (julian@tfs.com) and
 * Charles Hannum (mycroft@duality.gnu.ai.mit.edu).  Thanks a million!
 */

#define AIC_NPORTS	0x20	/* I/O port space used */

typedef u_long physaddr;
typedef u_long physlen;

#ifdef notyet
struct aic_dma_seg {
	physaddr seg_addr;
	physlen seg_len;
};

#define AIC_NSEG	16
#endif

/*
 * ACB. Holds additional information for each SCSI command Comments: We
 * need a separate scsi command block because we may need to overwrite it
 * with a request sense command.  Basically, we refrain from fiddling with
 * the scsi_xfer struct (except do the expected updating of return values).
 * We'll generally update: xs->{flags,resid,error,sense,status} and
 * occasionally xs->retries.
 */
struct aic_acb {
	struct scsi_generic scsi_cmd;
	int scsi_cmd_length;
	u_char *data_addr;		/* Saved data pointer */
	int data_length;		/* Residue */

	u_char target_stat;		/* SCSI status byte */

#ifdef notdef
	struct aic_dma_seg dma[AIC_NSEG]; /* Physical addresses+len */
#endif

	TAILQ_ENTRY(aic_acb) chain;
	struct scsi_xfer *xs;	/* SCSI xfer ctrl block from above */
	int flags;
#define ACB_ALLOC	0x01
#define	ACB_NEXUS	0x02
#define ACB_SENSE	0x04
#define	ACB_ABORT	0x40
#define	ACB_RESET	0x80
	int timeout;
};

/*
 * Some info about each (possible) target on the SCSI bus.  This should
 * probably have been a "per target+lunit" structure, but we'll leave it at
 * this for now.
 */
struct aic_tinfo {
	int	cmds;		/* #commands processed */
	int	dconns;		/* #disconnects */
	int	touts;		/* #timeouts */
	int	perrs;		/* #parity errors */
	int	senses;		/* #request sense commands sent */
	ushort	lubusy;		/* What local units/subr. are busy? */
	u_char  flags;
#define DO_SYNC		0x01	/* (Re)Negotiate synchronous options */
#define	DO_WIDE		0x02	/* (Re)Negotiate wide options */
	u_char  period;		/* Period suggestion */
	u_char  offset;		/* Offset suggestion */
	u_char	width;		/* Width suggestion */
};

struct aic_softc {
	struct device sc_dev;
	void *sc_ih;

	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	int sc_irq, sc_drq;

	TAILQ_HEAD(, aic_acb) free_list, ready_list, nexus_list;
	struct aic_acb *sc_nexus;	/* current command */
	struct aic_acb sc_acb[8];
	struct aic_tinfo sc_tinfo[8];

	struct mutex		sc_acb_mtx;
	struct scsi_iopool	sc_iopool;

	/* Data about the current nexus (updated for every cmd switch) */
	u_char	*sc_dp;		/* Current data pointer */
	size_t	sc_dleft;	/* Data bytes left to transfer */
	u_char	*sc_cp;		/* Current command pointer */
	size_t	sc_cleft;	/* Command bytes left to transfer */

	/* Adapter state */
	u_char	 sc_phase;	/* Current bus phase */
	u_char	 sc_prevphase;	/* Previous bus phase */
	u_char	 sc_state;	/* State applicable to the adapter */
#define	AIC_INIT	0
#define AIC_IDLE	1
#define AIC_SELECTING	2	/* SCSI command is arbiting  */
#define AIC_RESELECTED	3	/* Has been reselected */
#define AIC_CONNECTED	4	/* Actively using the SCSI bus */
#define	AIC_DISCONNECT	5	/* MSG_DISCONNECT received */
#define	AIC_CMDCOMPLETE	6	/* MSG_CMDCOMPLETE received */
#define AIC_CLEANING	7
	u_char	 sc_flags;
#define AIC_DROP_MSGIN	0x01	/* Discard all msgs (parity err detected) */
#define	AIC_ABORTING	0x02	/* Bailing out */
#define AIC_DOINGDMA	0x04	/* The FIFO data path is active! */
	u_char	sc_selid;	/* Reselection ID */

	/* Message stuff */
	u_char	sc_msgpriq;	/* Messages we want to send */
	u_char	sc_msgoutq;	/* Messages sent during last MESSAGE OUT */
	u_char	sc_lastmsg;	/* Message last transmitted */
	u_char	sc_currmsg;	/* Message currently ready to transmit */
#define SEND_DEV_RESET		0x01
#define SEND_PARITY_ERROR	0x02
#define SEND_INIT_DET_ERR	0x04
#define SEND_REJECT		0x08
#define SEND_IDENTIFY  		0x10
#define SEND_ABORT		0x20
#define SEND_SDTR		0x40
#define	SEND_WDTR		0x80
#define AIC_MAX_MSG_LEN 8
	u_char  sc_omess[AIC_MAX_MSG_LEN];
	u_char	*sc_omp;		/* Outgoing message pointer */
	u_char	sc_imess[AIC_MAX_MSG_LEN];
	u_char	*sc_imp;		/* Incoming message pointer */

	/* Hardware stuff */
	int	sc_initiator;		/* Our scsi id */
	int	sc_freq;		/* Clock frequency in MHz */
	int	sc_minsync;		/* Minimum sync period / 4 */
	int	sc_maxsync;		/* Maximum sync period / 4 */
};

#if AIC_DEBUG
#define AIC_SHOWACBS	0x01
#define AIC_SHOWINTS	0x02
#define AIC_SHOWCMDS	0x04
#define AIC_SHOWMISC	0x08
#define AIC_SHOWTRACE	0x10
#define AIC_SHOWSTART	0x20
#define AIC_DOBREAK	0x40
#define	AIC_PRINT(b, s)	do {if ((aic_debug & (b)) != 0) printf s;} while (0)
#define	AIC_BREAK() \
	do { if ((aic_debug & AIC_DOBREAK) != 0) db_enter(); } while (0)
#define	AIC_ASSERT(x) \
	do { \
		if (!(x)) { \
			printf("%s at line %d: assertion failed\n", \
			    sc->sc_dev.dv_xname, __LINE__); \
			db_enter(); \
		} \
	} while (0)
#else
#define	AIC_PRINT(b, s)
#define	AIC_BREAK()
#define	AIC_ASSERT(x)
#endif

#define AIC_ACBS(s)	AIC_PRINT(AIC_SHOWACBS, s)
#define AIC_INTS(s)	AIC_PRINT(AIC_SHOWINTS, s)
#define AIC_CMDS(s)	AIC_PRINT(AIC_SHOWCMDS, s)
#define AIC_MISC(s)	AIC_PRINT(AIC_SHOWMISC, s)
#define AIC_TRACE(s)	AIC_PRINT(AIC_SHOWTRACE, s)
#define AIC_START(s)	AIC_PRINT(AIC_SHOWSTART, s)

void	aicattach(struct aic_softc *);
int	aic_detach(struct device *, int);
int	aicintr(void *);
int 	aic_find(bus_space_tag_t, bus_space_handle_t);
