/*	$OpenBSD: ch.c,v 1.72 2023/04/11 00:45:09 jsg Exp $	*/
/*	$NetBSD: ch.c,v 1.26 1997/02/21 22:06:52 thorpej Exp $	*/

/*
 * Copyright (c) 1996, 1997 Jason R. Thorpe <thorpej@and.com>
 * All rights reserved.
 *
 * Partially based on an autochanger driver written by Stefan Grefen
 * and on an autochanger driver written by the Systems Programming Group
 * at the University of Utah Computer Science Department.
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
 *    must display the following acknowledgements:
 *	This product includes software developed by Jason R. Thorpe
 *	for And Communications, http://www.and.com/
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/chio.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/conf.h>
#include <sys/fcntl.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_changer.h>
#include <scsi/scsi_debug.h>
#include <scsi/scsiconf.h>

#define CHRETRIES	2
#define CHUNIT(x)	(minor((x)))

struct ch_softc {
	struct device		 sc_dev;	/* generic device info */
	struct scsi_link	*sc_link;	/* link in the SCSI bus */

	int			 sc_picker;	/* current picker */

	/*
	 * The following information is obtained from the
	 * element address assignment page.
	 */
	int			 sc_firsts[4];	/* firsts, indexed by CHET_* */
	int			 sc_counts[4];	/* counts, indexed by CHET_* */

	/*
	 * The following mask defines the legal combinations
	 * of elements for the MOVE MEDIUM command.
	 */
	u_int8_t		 sc_movemask[4];

	/*
	 * As above, but for EXCHANGE MEDIUM.
	 */
	u_int8_t		 sc_exchangemask[4];

	int			 flags;		/* misc. info */

	/*
	 * Quirks; see below.
	 */
	int			 sc_settledelay; /* delay for settle */

};

/* sc_flags */
#define CHF_ROTATE	0x01		/* picker can rotate */

/* Autoconfiguration glue */
int	chmatch(struct device *, void *, void *);
void	chattach(struct device *, struct device *, void *);

const struct cfattach ch_ca = {
	sizeof(struct ch_softc), chmatch, chattach
};

struct cfdriver ch_cd = {
	NULL, "ch", DV_DULL
};

const struct scsi_inquiry_pattern ch_patterns[] = {
	{T_CHANGER, T_REMOV,
	 "",		"",		""},
};

int	ch_move(struct ch_softc *, struct changer_move *);
int	ch_exchange(struct ch_softc *, struct changer_exchange *);
int	ch_position(struct ch_softc *, struct changer_position *);
int	ch_usergetelemstatus(struct ch_softc *,
	    struct changer_element_status_request *);
int	ch_getelemstatus(struct ch_softc *, int, int, caddr_t, size_t, int);
int	ch_get_params(struct ch_softc *, int);
int	ch_interpret_sense(struct scsi_xfer *xs);
void	ch_get_quirks(struct ch_softc *, struct scsi_inquiry_data *);

/*
 * SCSI changer quirks.
 */
struct chquirk {
	struct	scsi_inquiry_pattern cq_match; /* device id pattern */
	int	cq_settledelay;	/* settle delay, in seconds */
};

struct chquirk chquirks[] = {
	{{T_CHANGER, T_REMOV,
	  "SPECTRA",	"9000",		"0200"},
	 75},
};

int
chmatch(struct device *parent, void *match, void *aux)
{
	struct scsi_attach_args		*sa = aux;
	struct scsi_inquiry_data	*inq = &sa->sa_sc_link->inqdata;
	int				 priority;

	(void)scsi_inqmatch(inq, ch_patterns, nitems(ch_patterns),
	    sizeof(ch_patterns[0]), &priority);

	return priority;
}

void
chattach(struct device *parent, struct device *self, void *aux)
{
	struct ch_softc			*sc = (struct ch_softc *)self;
	struct scsi_attach_args		*sa = aux;
	struct scsi_link		*link = sa->sa_sc_link;

	/* Glue into the SCSI bus */
	sc->sc_link = link;
	link->interpret_sense = ch_interpret_sense;
	link->device_softc = sc;
	link->openings = 1;

	printf("\n");

	/*
	 * Store our device's quirks.
	 */
	ch_get_quirks(sc, &link->inqdata);
}

int
chopen(dev_t dev, int flags, int fmt, struct proc *p)
{
	struct ch_softc		*sc;
	int			 oldcounts[4];
	int			 i, unit, error = 0;

	unit = CHUNIT(dev);
	if ((unit >= ch_cd.cd_ndevs) ||
	    ((sc = ch_cd.cd_devs[unit]) == NULL))
		return ENXIO;

	/*
	 * Only allow one open at a time.
	 */
	if (ISSET(sc->sc_link->flags, SDEV_OPEN))
		return EBUSY;

	SET(sc->sc_link->flags, SDEV_OPEN);

	/*
	 * Absorb any unit attention errors. We must notice
	 * "Not ready" errors as a changer will report "In the
	 * process of getting ready" any time it must rescan
	 * itself to determine the state of the changer.
	 */
	error = scsi_test_unit_ready(sc->sc_link, TEST_READY_RETRIES,
	    SCSI_IGNORE_ILLEGAL_REQUEST | SCSI_IGNORE_MEDIA_CHANGE);
	if (error)
		goto bad;

	/*
	 * Get information about the device. Save old information
	 * so we can decide whether to be verbose about new parameters.
	 */
	for (i = 0; i < 4; i++) {
		oldcounts[i] = sc->sc_counts[i];
	}
	error = ch_get_params(sc, scsi_autoconf);
	if (error)
		goto bad;

	for (i = 0; i < 4; i++) {
		if (oldcounts[i] != sc->sc_counts[i]) {
			break;
		}
	}
	if (i < 4) {
#ifdef CHANGER_DEBUG
#define PLURAL(c)	(c) == 1 ? "" : "s"
		printf("%s: %d slot%s, %d drive%s, %d picker%s, %d portal%s\n",
		    sc->sc_dev.dv_xname,
		    sc->sc_counts[CHET_ST], PLURAL(sc->sc_counts[CHET_ST]),
		    sc->sc_counts[CHET_DT], PLURAL(sc->sc_counts[CHET_DT]),
		    sc->sc_counts[CHET_MT], PLURAL(sc->sc_counts[CHET_MT]),
		    sc->sc_counts[CHET_IE], PLURAL(sc->sc_counts[CHET_IE]));
#undef PLURAL
		printf("%s: move mask: 0x%x 0x%x 0x%x 0x%x\n",
		    sc->sc_dev.dv_xname,
		    sc->sc_movemask[CHET_MT], sc->sc_movemask[CHET_ST],
		    sc->sc_movemask[CHET_IE], sc->sc_movemask[CHET_DT]);
		printf("%s: exchange mask: 0x%x 0x%x 0x%x 0x%x\n",
		    sc->sc_dev.dv_xname,
		    sc->sc_exchangemask[CHET_MT], sc->sc_exchangemask[CHET_ST],
		    sc->sc_exchangemask[CHET_IE], sc->sc_exchangemask[CHET_DT]);
#endif /* CHANGER_DEBUG */
	}

	/* Default the current picker. */
	sc->sc_picker = sc->sc_firsts[CHET_MT];

	return 0;

bad:
	CLR(sc->sc_link->flags, SDEV_OPEN);
	return error;
}

int
chclose(dev_t dev, int flags, int fmt, struct proc *p)
{
	struct ch_softc *sc = ch_cd.cd_devs[CHUNIT(dev)];

	CLR(sc->sc_link->flags, SDEV_OPEN);
	return 0;
}

int
chioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct ch_softc		*sc = ch_cd.cd_devs[CHUNIT(dev)];
	int			 error = 0;

	/*
	 * If this command can change the device's state, we must
	 * have the device open for writing.
	 */
	switch (cmd) {
	case CHIOGPICKER:
	case CHIOGPARAMS:
	case CHIOGSTATUS:
		break;

	default:
		if (!ISSET(flags, FWRITE))
			return EBADF;
	}

	switch (cmd) {
	case CHIOMOVE:
		error = ch_move(sc, (struct changer_move *)data);
		break;

	case CHIOEXCHANGE:
		error = ch_exchange(sc, (struct changer_exchange *)data);
		break;

	case CHIOPOSITION:
		error = ch_position(sc, (struct changer_position *)data);
		break;

	case CHIOGPICKER:
		*(int *)data = sc->sc_picker - sc->sc_firsts[CHET_MT];
		break;

	case CHIOSPICKER:	{
		int new_picker = *(int *)data;

		if (new_picker > (sc->sc_counts[CHET_MT] - 1))
			return EINVAL;
		sc->sc_picker = sc->sc_firsts[CHET_MT] + new_picker;
		break;		}

	case CHIOGPARAMS:	{
		struct changer_params *cp = (struct changer_params *)data;

		cp->cp_curpicker = sc->sc_picker - sc->sc_firsts[CHET_MT];
		cp->cp_npickers = sc->sc_counts[CHET_MT];
		cp->cp_nslots = sc->sc_counts[CHET_ST];
		cp->cp_nportals = sc->sc_counts[CHET_IE];
		cp->cp_ndrives = sc->sc_counts[CHET_DT];
		break;		}

	case CHIOGSTATUS:	{
		struct changer_element_status_request *cesr =
		    (struct changer_element_status_request *)data;

		error = ch_usergetelemstatus(sc, cesr);
		break;		}

	/* Implement prevent/allow? */

	default:
		error = scsi_do_ioctl(sc->sc_link, cmd, data, flags);
		break;
	}

	return error;
}

int
ch_move(struct ch_softc *sc, struct changer_move *cm)
{
	struct scsi_move_medium		*cmd;
	struct scsi_xfer		*xs;
	int				 error;
	u_int16_t			 fromelem, toelem;

	/*
	 * Check arguments.
	 */
	if ((cm->cm_fromtype > CHET_DT) || (cm->cm_totype > CHET_DT))
		return EINVAL;
	if ((cm->cm_fromunit > (sc->sc_counts[cm->cm_fromtype] - 1)) ||
	    (cm->cm_tounit > (sc->sc_counts[cm->cm_totype] - 1)))
		return ENODEV;

	/*
	 * Check the request against the changer's capabilities.
	 */
	if ((sc->sc_movemask[cm->cm_fromtype] & (1 << cm->cm_totype)) == 0)
		return EINVAL;

	/*
	 * Calculate the source and destination elements.
	 */
	fromelem = sc->sc_firsts[cm->cm_fromtype] + cm->cm_fromunit;
	toelem = sc->sc_firsts[cm->cm_totype] + cm->cm_tounit;

	/*
	 * Build the SCSI command.
	 */
	xs = scsi_xs_get(sc->sc_link, 0);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->retries = CHRETRIES;
	xs->timeout = 100000;

	cmd = (struct scsi_move_medium *)&xs->cmd;
	cmd->opcode = MOVE_MEDIUM;
	_lto2b(sc->sc_picker, cmd->tea);
	_lto2b(fromelem, cmd->src);
	_lto2b(toelem, cmd->dst);
	if (ISSET(cm->cm_flags, CM_INVERT))
		SET(cmd->flags, MOVE_MEDIUM_INVERT);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

int
ch_exchange(struct ch_softc *sc, struct changer_exchange *ce)
{
	struct scsi_exchange_medium	*cmd;
	struct scsi_xfer		*xs;
	int				 error;
	u_int16_t			 src, dst1, dst2;

	/*
	 * Check arguments.
	 */
	if ((ce->ce_srctype > CHET_DT) || (ce->ce_fdsttype > CHET_DT) ||
	    (ce->ce_sdsttype > CHET_DT))
		return EINVAL;
	if ((ce->ce_srcunit > (sc->sc_counts[ce->ce_srctype] - 1)) ||
	    (ce->ce_fdstunit > (sc->sc_counts[ce->ce_fdsttype] - 1)) ||
	    (ce->ce_sdstunit > (sc->sc_counts[ce->ce_sdsttype] - 1)))
		return ENODEV;

	/*
	 * Check the request against the changer's capabilities.
	 */
	if (((sc->sc_exchangemask[ce->ce_srctype] &
	    (1 << ce->ce_fdsttype)) == 0) ||
	    ((sc->sc_exchangemask[ce->ce_fdsttype] &
	    (1 << ce->ce_sdsttype)) == 0))
		return EINVAL;

	/*
	 * Calculate the source and destination elements.
	 */
	src = sc->sc_firsts[ce->ce_srctype] + ce->ce_srcunit;
	dst1 = sc->sc_firsts[ce->ce_fdsttype] + ce->ce_fdstunit;
	dst2 = sc->sc_firsts[ce->ce_sdsttype] + ce->ce_sdstunit;

	/*
	 * Build the SCSI command.
	 */
	xs = scsi_xs_get(sc->sc_link, 0);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->retries = CHRETRIES;
	xs->timeout = 100000;

	cmd = (struct scsi_exchange_medium *)&xs->cmd;
	cmd->opcode = EXCHANGE_MEDIUM;
	_lto2b(sc->sc_picker, cmd->tea);
	_lto2b(src, cmd->src);
	_lto2b(dst1, cmd->fdst);
	_lto2b(dst2, cmd->sdst);
	if (ISSET(ce->ce_flags, CE_INVERT1))
		SET(cmd->flags, EXCHANGE_MEDIUM_INV1);
	if (ISSET(ce->ce_flags, CE_INVERT2))
		SET(cmd->flags, EXCHANGE_MEDIUM_INV2);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

int
ch_position(struct ch_softc *sc, struct changer_position *cp)
{
	struct scsi_position_to_element		*cmd;
	struct scsi_xfer			*xs;
	int					 error;
	u_int16_t				 dst;

	/*
	 * Check arguments.
	 */
	if (cp->cp_type > CHET_DT)
		return EINVAL;
	if (cp->cp_unit > (sc->sc_counts[cp->cp_type] - 1))
		return ENODEV;

	/*
	 * Calculate the destination element.
	 */
	dst = sc->sc_firsts[cp->cp_type] + cp->cp_unit;

	/*
	 * Build the SCSI command.
	 */
	xs = scsi_xs_get(sc->sc_link, 0);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->retries = CHRETRIES;
	xs->timeout = 100000;

	cmd = (struct scsi_position_to_element *)&xs->cmd;
	cmd->opcode = POSITION_TO_ELEMENT;
	_lto2b(sc->sc_picker, cmd->tea);
	_lto2b(dst, cmd->dst);
	if (ISSET(cp->cp_flags, CP_INVERT))
		SET(cmd->flags, POSITION_TO_ELEMENT_INVERT);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

/*
 * Copy a volume tag to a volume_tag struct, converting SCSI byte order
 * to host native byte order in the volume serial number.  The volume
 * label as returned by the changer is transferred to user mode as
 * nul-terminated string.  Volume labels are truncated at the first
 * space, as suggested by SCSI-2.
 */
static  void
copy_voltag(struct changer_voltag *uvoltag, struct volume_tag *voltag)
{
	int i;

	for (i=0; i<CH_VOLTAG_MAXLEN; i++) {
		char c = voltag->vif[i];
		if (c && c != ' ')
			uvoltag->cv_volid[i] = c;
		else
			break;
	}
	uvoltag->cv_volid[i] = '\0';
	uvoltag->cv_serial = _2btol(voltag->vsn);
}

/*
 * Copy an element status descriptor to a user-mode
 * changer_element_status structure.
 */
static void
copy_element_status(struct ch_softc *sc, int flags,
    struct read_element_status_descriptor *desc,
    struct changer_element_status *ces)
{
	u_int16_t eaddr = _2btol(desc->eaddr);
	u_int16_t et;

	for (et = CHET_MT; et <= CHET_DT; et++) {
		if ((sc->sc_firsts[et] <= eaddr)
		    && ((sc->sc_firsts[et] + sc->sc_counts[et])
		    > eaddr)) {
			ces->ces_addr = eaddr - sc->sc_firsts[et];
			ces->ces_type = et;
			break;
		}
	}

	ces->ces_flags = desc->flags1;

	ces->ces_sensecode = desc->sense_code;
	ces->ces_sensequal = desc->sense_qual;

	if (desc->flags2 & READ_ELEMENT_STATUS_INVERT)
		ces->ces_flags |= READ_ELEMENT_STATUS_EXCEPT;

	if (desc->flags2 & READ_ELEMENT_STATUS_SVALID) {
		eaddr = _2btol(desc->ssea);

		/* convert source address to logical format */
		for (et = CHET_MT; et <= CHET_DT; et++) {
			if ((sc->sc_firsts[et] <= eaddr)
			    && ((sc->sc_firsts[et] + sc->sc_counts[et])
				> eaddr)) {
				ces->ces_source_addr =
					eaddr - sc->sc_firsts[et];
				ces->ces_source_type = et;
				ces->ces_flags |= READ_ELEMENT_STATUS_ACCESS;
				break;
			}
		}

		if (!(ces->ces_flags & READ_ELEMENT_STATUS_ACCESS))
			printf("ch: warning: could not map element source "
			       "address %ud to a valid element type\n",
			       eaddr);
	}

	if (ISSET(flags, READ_ELEMENT_STATUS_PVOLTAG))
		copy_voltag(&ces->ces_pvoltag, &desc->pvoltag);
	if (ISSET(flags, READ_ELEMENT_STATUS_AVOLTAG))
		copy_voltag(&ces->ces_avoltag, &desc->avoltag);
}

/*
 * Perform a READ ELEMENT STATUS on behalf of the user, and return to
 * the user only the data the user is interested in (i.e. an array of
 * changer_element_status structures)
 */
int
ch_usergetelemstatus(struct ch_softc *sc,
    struct changer_element_status_request *cesr)
{
	struct changer_element_status		*user_data = NULL;
	struct read_element_status_header	*st_hdr;
	struct read_element_status_page_header	*pg_hdr;
	caddr_t					 desc;
	caddr_t					 data = NULL;
	size_t					 size, desclen, udsize;
	int					 avail, chet, i, want_voltags;
	int					 error = 0;

	chet = cesr->cesr_type;
	want_voltags = (cesr->cesr_flags & CESR_VOLTAGS) ? 1 : 0;

	/*
	 * If there are no elements of the requested type in the changer,
	 * the request is invalid.
	 */
	if (sc->sc_counts[chet] == 0)
		return EINVAL;

	/*
	 * Request one descriptor for the given element type.  This
	 * is used to determine the size of the descriptor so that
	 * we can allocate enough storage for all of them.  We assume
	 * that the first one can fit into 1k.
	 */
	size = 1024;
	data = dma_alloc(size, PR_WAITOK);
	error = ch_getelemstatus(sc, sc->sc_firsts[chet], 1, data, size,
	    want_voltags);
	if (error)
		goto done;

	st_hdr = (struct read_element_status_header *)data;
	pg_hdr = (struct read_element_status_page_header *) (st_hdr + 1);
	desclen = _2btol(pg_hdr->edl);

	dma_free(data, size);

	/*
	 * Reallocate storage for descriptors and get them from the
	 * device.
	 */
	size = sizeof(struct read_element_status_header) +
	    sizeof(struct read_element_status_page_header) +
	    (desclen * sc->sc_counts[chet]);
	data = dma_alloc(size, PR_WAITOK);
	error = ch_getelemstatus(sc, sc->sc_firsts[chet],
	    sc->sc_counts[chet], data, size, want_voltags);
	if (error)
		goto done;

	/*
	 * Fill in the user status array.
	 */
	st_hdr = (struct read_element_status_header *)data;
	pg_hdr = (struct read_element_status_page_header *) (st_hdr + 1);

	avail = _2btol(st_hdr->count);
	if (avail != sc->sc_counts[chet]) {
		error = EINVAL;
		goto done;
	}

	user_data = mallocarray(avail, sizeof(struct changer_element_status),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	udsize = avail * sizeof(struct changer_element_status);

	desc = (caddr_t)(pg_hdr + 1);
	for (i = 0; i < avail; ++i) {
		struct changer_element_status *ces = &(user_data[i]);
		copy_element_status(sc, pg_hdr->flags,
		    (struct read_element_status_descriptor *)desc, ces);
		desc += desclen;
	}

	/* Copy array out to userspace. */
	error = copyout(user_data, cesr->cesr_data, udsize);

done:
	if (data != NULL)
		dma_free(data, size);
	if (user_data != NULL)
		free(user_data, M_DEVBUF, udsize);
	return error;
}

int
ch_getelemstatus(struct ch_softc *sc, int first, int count, caddr_t data,
    size_t datalen, int voltag)
{
	struct scsi_read_element_status		*cmd;
	struct scsi_xfer			*xs;
	int					 error;

	/*
	 * Build SCSI command.
	 */
	xs = scsi_xs_get(sc->sc_link, SCSI_DATA_IN);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->data = data;
	xs->datalen = datalen;
	xs->retries = CHRETRIES;
	xs->timeout = 100000;

	cmd = (struct scsi_read_element_status *)&xs->cmd;
	cmd->opcode = READ_ELEMENT_STATUS;
	_lto2b(first, cmd->sea);
	_lto2b(count, cmd->count);
	_lto3b(datalen, cmd->len);
	if (voltag)
		SET(cmd->byte2, READ_ELEMENT_STATUS_VOLTAG);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

/*
 * Ask the device about itself and fill in the parameters in our
 * softc.
 */
int
ch_get_params(struct ch_softc *sc, int flags)
{
	union scsi_mode_sense_buf			*data;
	struct page_element_address_assignment		*ea;
	struct page_device_capabilities			*cap;
	u_int8_t					*moves, *exchanges;
	int						 big, error, from;

	data = dma_alloc(sizeof(*data), PR_NOWAIT);
	if (data == NULL)
		return ENOMEM;

	/*
	 * Grab info from the element address assignment page (0x1d).
	 */
	error = scsi_do_mode_sense(sc->sc_link, EA_PAGE, data,
	    (void **)&ea, sizeof(*ea), flags, &big);
	if (error == 0 && ea == NULL)
		error = EIO;
	if (error != 0) {
#ifdef CHANGER_DEBUG
		printf("%s: could not sense element address page\n",
		    sc->sc_dev.dv_xname);
#endif /* CHANGER_DEBUG */
		dma_free(data, sizeof(*data));
		return error;
	}

	sc->sc_firsts[CHET_MT] = _2btol(ea->mtea);
	sc->sc_counts[CHET_MT] = _2btol(ea->nmte);
	sc->sc_firsts[CHET_ST] = _2btol(ea->fsea);
	sc->sc_counts[CHET_ST] = _2btol(ea->nse);
	sc->sc_firsts[CHET_IE] = _2btol(ea->fieea);
	sc->sc_counts[CHET_IE] = _2btol(ea->niee);
	sc->sc_firsts[CHET_DT] = _2btol(ea->fdtea);
	sc->sc_counts[CHET_DT] = _2btol(ea->ndte);

	/* XXX Ask for transport geometry page. */

	/*
	 * Grab info from the capabilities page (0x1f).
	 */
	error = scsi_do_mode_sense(sc->sc_link, CAP_PAGE, data,
	    (void **)&cap, sizeof(*cap), flags, &big);
	if (error == 0 && cap == NULL)
		error = EIO;
	if (error != 0) {
#ifdef CHANGER_DEBUG
		printf("%s: could not sense capabilities page\n",
		    sc->sc_dev.dv_xname);
#endif /* CHANGER_DEBUG */
		dma_free(data, sizeof(*data));
		return error;
	}

	bzero(sc->sc_movemask, sizeof(sc->sc_movemask));
	bzero(sc->sc_exchangemask, sizeof(sc->sc_exchangemask));
	moves = &cap->move_from_mt;
	exchanges = &cap->exchange_with_mt;
	for (from = CHET_MT; from <= CHET_DT; ++from) {
		sc->sc_movemask[from] = moves[from];
		sc->sc_exchangemask[from] = exchanges[from];
	}

	SET(sc->sc_link->flags, SDEV_MEDIA_LOADED);
	dma_free(data, sizeof(*data));
	return 0;
}

void
ch_get_quirks(struct ch_softc *sc, struct scsi_inquiry_data *inqbuf)
{
	const struct chquirk		*match;
	int				 priority;

	sc->sc_settledelay = 0;

	match = (const struct chquirk *)scsi_inqmatch(inqbuf,
	    (caddr_t)chquirks,
	    sizeof(chquirks) / sizeof(chquirks[0]),
	    sizeof(chquirks[0]), &priority);
	if (priority != 0) {
		sc->sc_settledelay = match->cq_settledelay;
	}
}

/*
 * Look at the returned sense and act on the error to determine
 * the unix error number to pass back... (0 = report no error)
 *                            (-1 = continue processing)
 */
int
ch_interpret_sense(struct scsi_xfer *xs)
{
	struct scsi_sense_data		*sense = &xs->sense;
	struct scsi_link		*link = xs->sc_link;
	u_int8_t			 serr, skey;

	serr = sense->error_code & SSD_ERRCODE;
	skey = sense->flags & SSD_KEY;

	if (!ISSET(link->flags, SDEV_OPEN) ||
	    (serr != SSD_ERRCODE_CURRENT && serr != SSD_ERRCODE_DEFERRED))
		return scsi_interpret_sense(xs);

	switch (skey) {

	/*
	 * We do custom processing in ch for the unit becoming ready
	 * case.  in this case we do not allow xs->retries to be
	 * decremented only on the "Unit Becoming Ready" case. This is
	 * because tape changers report "Unit Becoming Ready" when they
	 * rescan their state (i.e. when the door got opened) and can
	 * take a long time for large units. Rather than having a
	 * massive timeout for all operations (which would cause other
	 * problems) we allow changers to wait (but be interruptible
	 * with Ctrl-C) forever as long as they are reporting that they
	 * are becoming ready.  all other cases are handled as per the
	 * default.
	 */
	case SKEY_NOT_READY:
		if (ISSET(xs->flags, SCSI_IGNORE_NOT_READY))
			return 0;
		switch (ASC_ASCQ(sense)) {
		case SENSE_NOT_READY_BECOMING_READY:
			SC_DEBUG(link, SDEV_DB1, ("not ready: busy (%#x)\n",
			    sense->add_sense_code_qual));
			/* don't count this as a retry */
			xs->retries++;
			return scsi_delay(xs, 1);
		default:
			return scsi_interpret_sense(xs);
		}
	default:
		return scsi_interpret_sense(xs);
	}
}
