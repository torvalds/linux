/*	$OpenBSD: atascsi.c,v 1.156 2024/09/04 07:54:52 mglocker Exp $ */

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
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/pool.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/ata/atascsi.h>
#include <dev/ata/pmreg.h>

struct atascsi_port;

struct atascsi {
	struct device		*as_dev;
	void			*as_cookie;

	struct atascsi_host_port **as_host_ports;

	const struct atascsi_methods *as_methods;
	struct scsi_adapter	as_switch;
	struct scsibus_softc	*as_scsibus;

	int			as_capability;
	int			as_ncqdepth;
};

/*
 * atascsi_host_port is a port attached to the host controller, and
 * only holds the details relevant to the host controller.
 * atascsi_port is any port, including ports on port multipliers, and
 * it holds details of the device attached to the port.
 *
 * When there is a port multiplier attached to a port, the ahp_ports
 * array in the atascsi_host_port struct contains one atascsi_port for
 * each port, and one for the control port (port 15).  The index into
 * the array is the LUN used to address the port.  For the control port,
 * the LUN is 0, and for the port multiplier ports, the LUN is the
 * port number plus one.
 *
 * When there is no port multiplier attached to a port, the ahp_ports
 * array contains a single entry for the device.  The LUN and port number
 * for this entry are both 0.
 */

struct atascsi_host_port {
	struct scsi_iopool	ahp_iopool;
	struct atascsi		*ahp_as;
	int			ahp_port;
	int			ahp_nports;

	struct atascsi_port	**ahp_ports;
};

struct atascsi_port {
	struct ata_identify	ap_identify;
	struct atascsi_host_port *ap_host_port;
	struct atascsi		*ap_as;
	int			ap_pmp_port;
	int			ap_type;
	int			ap_ncqdepth;
	int			ap_features;
#define ATA_PORT_F_NCQ			0x1
#define ATA_PORT_F_TRIM			0x2
};

void		atascsi_cmd(struct scsi_xfer *);
int		atascsi_probe(struct scsi_link *);
void		atascsi_free(struct scsi_link *);

/* template */
const struct scsi_adapter atascsi_switch = {
	atascsi_cmd, NULL, atascsi_probe, atascsi_free, NULL
};

void		ata_swapcopy(void *, void *, size_t);

void		atascsi_disk_cmd(struct scsi_xfer *);
void		atascsi_disk_cmd_done(struct ata_xfer *);
void		atascsi_disk_inq(struct scsi_xfer *);
void		atascsi_disk_inquiry(struct scsi_xfer *);
void		atascsi_disk_vpd_supported(struct scsi_xfer *);
void		atascsi_disk_vpd_serial(struct scsi_xfer *);
void		atascsi_disk_vpd_ident(struct scsi_xfer *);
void		atascsi_disk_vpd_ata(struct scsi_xfer *);
void		atascsi_disk_vpd_limits(struct scsi_xfer *);
void		atascsi_disk_vpd_info(struct scsi_xfer *);
void		atascsi_disk_vpd_thin(struct scsi_xfer *);
void		atascsi_disk_write_same_16(struct scsi_xfer *);
void		atascsi_disk_write_same_16_done(struct ata_xfer *);
void		atascsi_disk_unmap(struct scsi_xfer *);
void		atascsi_disk_unmap_task(void *);
void		atascsi_disk_unmap_done(struct ata_xfer *);
void		atascsi_disk_capacity(struct scsi_xfer *);
void		atascsi_disk_capacity16(struct scsi_xfer *);
void		atascsi_disk_sync(struct scsi_xfer *);
void		atascsi_disk_sync_done(struct ata_xfer *);
void		atascsi_disk_sense(struct scsi_xfer *);
void		atascsi_disk_start_stop(struct scsi_xfer *);
void		atascsi_disk_start_stop_done(struct ata_xfer *);

void		atascsi_atapi_cmd(struct scsi_xfer *);
void		atascsi_atapi_cmd_done(struct ata_xfer *);

void		atascsi_pmp_cmd(struct scsi_xfer *);
void		atascsi_pmp_sense(struct scsi_xfer *xs);
void		atascsi_pmp_inq(struct scsi_xfer *xs);


void		atascsi_passthru_12(struct scsi_xfer *);
void		atascsi_passthru_16(struct scsi_xfer *);
int		atascsi_passthru_map(struct scsi_xfer *, u_int8_t, u_int8_t);
void		atascsi_passthru_done(struct ata_xfer *);

void		atascsi_done(struct scsi_xfer *, int);

void		ata_exec(struct atascsi *, struct ata_xfer *);

void		ata_polled_complete(struct ata_xfer *);
int		ata_polled(struct ata_xfer *);

u_int64_t	ata_identify_blocks(struct ata_identify *);
u_int		ata_identify_blocksize(struct ata_identify *);
u_int		ata_identify_block_l2p_exp(struct ata_identify *);
u_int		ata_identify_block_logical_align(struct ata_identify *);

void		*atascsi_io_get(void *);
void		atascsi_io_put(void *, void *);
struct atascsi_port * atascsi_lookup_port(struct scsi_link *);

int		atascsi_port_identify(struct atascsi_port *,
		    struct ata_identify *);
int		atascsi_port_set_features(struct atascsi_port *, int, int);


struct atascsi *
atascsi_attach(struct device *self, struct atascsi_attach_args *aaa)
{
	struct scsibus_attach_args	saa;
	struct atascsi			*as;

	as = malloc(sizeof(*as), M_DEVBUF, M_WAITOK | M_ZERO);

	as->as_dev = self;
	as->as_cookie = aaa->aaa_cookie;
	as->as_methods = aaa->aaa_methods;
	as->as_capability = aaa->aaa_capability;
	as->as_ncqdepth = aaa->aaa_ncmds;

	/* copy from template and modify for ourselves */
	as->as_switch = atascsi_switch;
	if (aaa->aaa_minphys != NULL)
		as->as_switch.dev_minphys = aaa->aaa_minphys;

	as->as_host_ports = mallocarray(aaa->aaa_nports,
	    sizeof(struct atascsi_host_port *),	M_DEVBUF, M_WAITOK | M_ZERO);

	saa.saa_adapter = &as->as_switch;
	saa.saa_adapter_softc = as;
	saa.saa_adapter_buswidth = aaa->aaa_nports;
	saa.saa_luns = SATA_PMP_MAX_PORTS;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_openings = 1;
	saa.saa_pool = NULL;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	as->as_scsibus = (struct scsibus_softc *)config_found(self, &saa,
	    scsiprint);

	return (as);
}

int
atascsi_detach(struct atascsi *as, int flags)
{
	int				rv;

	rv = config_detach((struct device *)as->as_scsibus, flags);
	if (rv != 0)
		return (rv);

	free(as->as_host_ports, M_DEVBUF, 0);
	free(as, M_DEVBUF, sizeof(*as));

	return (0);
}

struct atascsi_port *
atascsi_lookup_port(struct scsi_link *link)
{
	struct atascsi 			*as = link->bus->sb_adapter_softc;
	struct atascsi_host_port 	*ahp;

	if (link->target >= link->bus->sb_adapter_buswidth)
		return (NULL);

	ahp = as->as_host_ports[link->target];
	if (link->lun >= ahp->ahp_nports)
		return (NULL);

	return (ahp->ahp_ports[link->lun]);
}

int
atascsi_probe(struct scsi_link *link)
{
	struct atascsi			*as = link->bus->sb_adapter_softc;
	struct atascsi_host_port 	*ahp;
	struct atascsi_port		*ap;
	struct ata_xfer			*xa;
	struct ata_identify		*identify;
	int				port, type, qdepth;
	int				rv;
	u_int16_t			cmdset;
	u_int16_t			validinfo, ultradma;
	int				i, xfermode = -1;

	port = link->target;
	if (port >= link->bus->sb_adapter_buswidth)
		return (ENXIO);

	/* if this is a PMP port, check it's valid */
	if (link->lun > 0) {
		if (link->lun >= as->as_host_ports[port]->ahp_nports)
			return (ENXIO);
	}

	type = as->as_methods->ata_probe(as->as_cookie, port, link->lun);
	switch (type) {
	case ATA_PORT_T_DISK:
		break;
	case ATA_PORT_T_ATAPI:
		link->flags |= SDEV_ATAPI;
		break;
	case ATA_PORT_T_PM:
		if (link->lun != 0) {
			printf("%s.%d.%d: Port multipliers cannot be nested\n",
			    as->as_dev->dv_xname, port, link->lun);
			rv = ENODEV;
			goto unsupported;
		}
		break;
	default:
		rv = ENODEV;
		goto unsupported;
	}

	ap = malloc(sizeof(*ap), M_DEVBUF, M_WAITOK | M_ZERO);
	ap->ap_as = as;

	if (link->lun == 0) {
		ahp = malloc(sizeof(*ahp), M_DEVBUF, M_WAITOK | M_ZERO);
		ahp->ahp_as = as;
		ahp->ahp_port = port;

		scsi_iopool_init(&ahp->ahp_iopool, ahp, atascsi_io_get,
		    atascsi_io_put);

		as->as_host_ports[port] = ahp;

		if (type == ATA_PORT_T_PM) {
			ahp->ahp_nports = SATA_PMP_MAX_PORTS;
			ap->ap_pmp_port = SATA_PMP_CONTROL_PORT;
		} else {
			ahp->ahp_nports = 1;
			ap->ap_pmp_port = 0;
		}
		ahp->ahp_ports = mallocarray(ahp->ahp_nports,
		    sizeof(struct atascsi_port *), M_DEVBUF, M_WAITOK | M_ZERO);
	} else {
		ahp = as->as_host_ports[port];
		ap->ap_pmp_port = link->lun - 1;
	}

	ap->ap_host_port = ahp;
	ap->ap_type = type;

	link->pool = &ahp->ahp_iopool;

	/* fetch the device info, except for port multipliers */
	if (type != ATA_PORT_T_PM) {

		/* devices attached to port multipliers tend not to be
		 * spun up at this point, and sometimes this prevents
		 * identification from working, so we retry a few times
		 * with a fairly long delay.
		 */

		identify = dma_alloc(sizeof(*identify), PR_WAITOK | PR_ZERO);

		int count = (link->lun > 0) ? 6 : 2;
		while (count--) {
			rv = atascsi_port_identify(ap, identify);
			if (rv == 0) {
				ap->ap_identify = *identify;
				break;
			}
			if (count > 0)
				delay(5000000);
		}

		dma_free(identify, sizeof(*identify));

		if (rv != 0) {
			goto error;
		}
	}

	ahp->ahp_ports[link->lun] = ap;

	if (type != ATA_PORT_T_DISK)
		return (0);

	/*
	 * Early SATA drives (as well as PATA drives) need to have
	 * their transfer mode set properly, otherwise commands that
	 * use DMA will time out.
	 */
	validinfo = letoh16(ap->ap_identify.validinfo);
	if (ISSET(validinfo, ATA_ID_VALIDINFO_ULTRADMA)) {
		ultradma = letoh16(ap->ap_identify.ultradma);
		for (i = 7; i >= 0; i--) {
			if (ultradma & (1 << i)) {
				xfermode = ATA_SF_XFERMODE_UDMA | i;
				break;
			}
		}
	}
	if (xfermode != -1)
		(void)atascsi_port_set_features(ap, ATA_SF_XFERMODE, xfermode);

	if (as->as_capability & ASAA_CAP_NCQ &&
	    ISSET(letoh16(ap->ap_identify.satacap), ATA_SATACAP_NCQ) &&
	    (link->lun == 0 || as->as_capability & ASAA_CAP_PMP_NCQ)) {
		ap->ap_ncqdepth = ATA_QDEPTH(letoh16(ap->ap_identify.qdepth));
		qdepth = MIN(ap->ap_ncqdepth, as->as_ncqdepth);
		if (ISSET(as->as_capability, ASAA_CAP_NEEDS_RESERVED))
			qdepth--;

		if (qdepth > 1) {
			SET(ap->ap_features, ATA_PORT_F_NCQ);

			/* Raise the number of openings */
			link->openings = qdepth;

			/*
			 * XXX for directly attached devices, throw away any xfers
			 * that have tag numbers higher than what the device supports.
			 */
			if (link->lun == 0) {
				while (qdepth--) {
					xa = scsi_io_get(&ahp->ahp_iopool, SCSI_NOSLEEP);
					if (xa->tag < link->openings) {
						xa->state = ATA_S_COMPLETE;
						scsi_io_put(&ahp->ahp_iopool, xa);
					}
				}
			}
		}
	}

	if (ISSET(letoh16(ap->ap_identify.data_set_mgmt),
	    ATA_ID_DATA_SET_MGMT_TRIM))
		SET(ap->ap_features, ATA_PORT_F_TRIM);

	cmdset = letoh16(ap->ap_identify.cmdset82);

	/* Enable write cache if supported */
	if (ISSET(cmdset, ATA_IDENTIFY_WRITECACHE)) {
		/* We don't care if it fails. */
		(void)atascsi_port_set_features(ap, ATA_SF_WRITECACHE_EN, 0);
	}

	/* Enable read lookahead if supported */
	if (ISSET(cmdset, ATA_IDENTIFY_LOOKAHEAD)) {
		/* We don't care if it fails. */
		(void)atascsi_port_set_features(ap, ATA_SF_LOOKAHEAD_EN, 0);
	}

	/*
	 * FREEZE LOCK the device so malicious users can't lock it on us.
	 * As there is no harm in issuing this to devices that don't
	 * support the security feature set we just send it, and don't bother
	 * checking if the device sends a command abort to tell us it doesn't
	 * support it
	 */
	xa = scsi_io_get(&ahp->ahp_iopool, SCSI_NOSLEEP);
	if (xa == NULL)
		panic("no free xfers on a new port");
	xa->fis->command = ATA_C_SEC_FREEZE_LOCK;
	xa->fis->flags = ATA_H2D_FLAGS_CMD | ap->ap_pmp_port;
	xa->flags = ATA_F_POLL;
	xa->timeout = 1000;
	xa->complete = ata_polled_complete;
	xa->pmp_port = ap->ap_pmp_port;
	xa->atascsi_private = &ahp->ahp_iopool;
	ata_exec(as, xa);
	ata_polled(xa); /* we don't care if it doesn't work */

	return (0);
error:
	free(ap, M_DEVBUF, sizeof(*ap));
unsupported:

	as->as_methods->ata_free(as->as_cookie, port, link->lun);
	return (rv);
}

void
atascsi_free(struct scsi_link *link)
{
	struct atascsi			*as = link->bus->sb_adapter_softc;
	struct atascsi_host_port	*ahp;
	struct atascsi_port		*ap;
	int				port;

	port = link->target;
	if (port >= link->bus->sb_adapter_buswidth)
		return;

	ahp = as->as_host_ports[port];
	if (ahp == NULL)
		return;

	if (link->lun >= ahp->ahp_nports)
		return;

	ap = ahp->ahp_ports[link->lun];
	free(ap, M_DEVBUF, sizeof(*ap));
	ahp->ahp_ports[link->lun] = NULL;

	as->as_methods->ata_free(as->as_cookie, port, link->lun);

	if (link->lun == ahp->ahp_nports - 1) {
		/* we've already freed all of ahp->ahp_ports, now
		 * free ahp itself.  this relies on the order luns are
		 * detached in scsi_detach_target().
		 */
		free(ahp, M_DEVBUF, sizeof(*ahp));
		as->as_host_ports[port] = NULL;
	}
}

void
atascsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct atascsi_port	*ap;

	ap = atascsi_lookup_port(link);
	if (ap == NULL) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	switch (ap->ap_type) {
	case ATA_PORT_T_DISK:
		atascsi_disk_cmd(xs);
		break;
	case ATA_PORT_T_ATAPI:
		atascsi_atapi_cmd(xs);
		break;
	case ATA_PORT_T_PM:
		atascsi_pmp_cmd(xs);
		break;

	case ATA_PORT_T_NONE:
	default:
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		break;
	}
}

void
atascsi_disk_cmd(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct atascsi		*as = link->bus->sb_adapter_softc;
	struct atascsi_port	*ap;
	struct ata_xfer		*xa = xs->io;
	int			flags = 0;
	struct ata_fis_h2d	*fis;
	u_int64_t		lba;
	u_int32_t		sector_count;

	ap = atascsi_lookup_port(link);

	switch (xs->cmd.opcode) {
	case READ_COMMAND:
	case READ_10:
	case READ_12:
	case READ_16:
		flags = ATA_F_READ;
		break;
	case WRITE_COMMAND:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
		flags = ATA_F_WRITE;
		/* deal with io outside the switch */
		break;

	case WRITE_SAME_16:
		atascsi_disk_write_same_16(xs);
		return;
	case UNMAP:
		atascsi_disk_unmap(xs);
		return;

	case SYNCHRONIZE_CACHE:
		atascsi_disk_sync(xs);
		return;
	case REQUEST_SENSE:
		atascsi_disk_sense(xs);
		return;
	case INQUIRY:
		atascsi_disk_inq(xs);
		return;
	case READ_CAPACITY:
		atascsi_disk_capacity(xs);
		return;
	case READ_CAPACITY_16:
		atascsi_disk_capacity16(xs);
		return;

	case ATA_PASSTHRU_12:
		atascsi_passthru_12(xs);
		return;
	case ATA_PASSTHRU_16:
		atascsi_passthru_16(xs);
		return;

	case START_STOP:
		atascsi_disk_start_stop(xs);
		return;

	case TEST_UNIT_READY:
	case PREVENT_ALLOW:
		atascsi_done(xs, XS_NOERROR);
		return;

	default:
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	xa->flags = flags;
	scsi_cmd_rw_decode(&xs->cmd, &lba, &sector_count);
	if ((lba >> 48) != 0 || (sector_count >> 16) != 0) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	fis = xa->fis;

	fis->flags = ATA_H2D_FLAGS_CMD | ap->ap_pmp_port;
	fis->lba_low = lba & 0xff;
	fis->lba_mid = (lba >> 8) & 0xff;
	fis->lba_high = (lba >> 16) & 0xff;

	if (ISSET(ap->ap_features, ATA_PORT_F_NCQ) &&
	    (xa->tag < ap->ap_ncqdepth) &&
	    !(xs->flags & SCSI_POLL)) {
		/* Use NCQ */
		xa->flags |= ATA_F_NCQ;
		fis->command = (xa->flags & ATA_F_WRITE) ?
		    ATA_C_WRITE_FPDMA : ATA_C_READ_FPDMA;
		fis->device = ATA_H2D_DEVICE_LBA;
		fis->lba_low_exp = (lba >> 24) & 0xff;
		fis->lba_mid_exp = (lba >> 32) & 0xff;
		fis->lba_high_exp = (lba >> 40) & 0xff;
		fis->sector_count = xa->tag << 3;
		fis->features = sector_count & 0xff;
		fis->features_exp = (sector_count >> 8) & 0xff;
	} else if (sector_count > 0x100 || lba > 0xfffffff) {
		/* Use LBA48 */
		fis->command = (xa->flags & ATA_F_WRITE) ?
		    ATA_C_WRITEDMA_EXT : ATA_C_READDMA_EXT;
		fis->device = ATA_H2D_DEVICE_LBA;
		fis->lba_low_exp = (lba >> 24) & 0xff;
		fis->lba_mid_exp = (lba >> 32) & 0xff;
		fis->lba_high_exp = (lba >> 40) & 0xff;
		fis->sector_count = sector_count & 0xff;
		fis->sector_count_exp = (sector_count >> 8) & 0xff;
	} else {
		/* Use LBA */
		fis->command = (xa->flags & ATA_F_WRITE) ?
		    ATA_C_WRITEDMA : ATA_C_READDMA;
		fis->device = ATA_H2D_DEVICE_LBA | ((lba >> 24) & 0x0f);
		fis->sector_count = sector_count & 0xff;
	}

	xa->data = xs->data;
	xa->datalen = xs->datalen;
	xa->complete = atascsi_disk_cmd_done;
	xa->timeout = xs->timeout;
	xa->pmp_port = ap->ap_pmp_port;
	xa->atascsi_private = xs;
	if (xs->flags & SCSI_POLL)
		xa->flags |= ATA_F_POLL;

	ata_exec(as, xa);
}

void
atascsi_disk_cmd_done(struct ata_xfer *xa)
{
	struct scsi_xfer	*xs = xa->atascsi_private;

	switch (xa->state) {
	case ATA_S_COMPLETE:
		xs->error = XS_NOERROR;
		break;
	case ATA_S_ERROR:
		/* fake sense? */
		xs->error = XS_DRIVER_STUFFUP;
		break;
	case ATA_S_TIMEOUT:
		xs->error = XS_TIMEOUT;
		break;
	default:
		panic("atascsi_disk_cmd_done: unexpected ata_xfer state (%d)",
		    xa->state);
	}

	xs->resid = xa->resid;

	scsi_done(xs);
}

void
atascsi_disk_inq(struct scsi_xfer *xs)
{
	struct scsi_inquiry	*inq = (struct scsi_inquiry *)&xs->cmd;

	if (xs->cmdlen != sizeof(*inq)) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	if (ISSET(inq->flags, SI_EVPD)) {
		switch (inq->pagecode) {
		case SI_PG_SUPPORTED:
			atascsi_disk_vpd_supported(xs);
			break;
		case SI_PG_SERIAL:
			atascsi_disk_vpd_serial(xs);
			break;
		case SI_PG_DEVID:
			atascsi_disk_vpd_ident(xs);
			break;
		case SI_PG_ATA:
			atascsi_disk_vpd_ata(xs);
			break;
		case SI_PG_DISK_LIMITS:
			atascsi_disk_vpd_limits(xs);
			break;
		case SI_PG_DISK_INFO:
			atascsi_disk_vpd_info(xs);
			break;
		case SI_PG_DISK_THIN:
			atascsi_disk_vpd_thin(xs);
			break;
		default:
			atascsi_done(xs, XS_DRIVER_STUFFUP);
			break;
		}
	} else
		atascsi_disk_inquiry(xs);
}

void
atascsi_disk_inquiry(struct scsi_xfer *xs)
{
	struct scsi_inquiry_data inq;
	struct scsi_link        *link = xs->sc_link;
	struct atascsi_port	*ap;

	ap = atascsi_lookup_port(link);

	bzero(&inq, sizeof(inq));

	inq.device = T_DIRECT;
	inq.version = SCSI_REV_SPC3;
	inq.response_format = SID_SCSI2_RESPONSE;
	inq.additional_length = SID_SCSI2_ALEN;
	inq.flags |= SID_CmdQue;
	bcopy("ATA     ", inq.vendor, sizeof(inq.vendor));
	ata_swapcopy(ap->ap_identify.model, inq.product,
	    sizeof(inq.product));
	ata_swapcopy(ap->ap_identify.firmware, inq.revision,
	    sizeof(inq.revision));

	scsi_copy_internal_data(xs, &inq, sizeof(inq));

	atascsi_done(xs, XS_NOERROR);
}

void
atascsi_disk_vpd_supported(struct scsi_xfer *xs)
{
	struct {
		struct scsi_vpd_hdr	hdr;
		u_int8_t		list[7];
	}			pg;
	struct scsi_link        *link = xs->sc_link;
	struct atascsi_port	*ap;
	int			fat;

	ap = atascsi_lookup_port(link);
	fat = ISSET(ap->ap_features, ATA_PORT_F_TRIM) ? 0 : 1;

	bzero(&pg, sizeof(pg));

	pg.hdr.device = T_DIRECT;
	pg.hdr.page_code = SI_PG_SUPPORTED;
	_lto2b(sizeof(pg.list) - fat, pg.hdr.page_length);
	pg.list[0] = SI_PG_SUPPORTED;
	pg.list[1] = SI_PG_SERIAL;
	pg.list[2] = SI_PG_DEVID;
	pg.list[3] = SI_PG_ATA;
	pg.list[4] = SI_PG_DISK_LIMITS;
	pg.list[5] = SI_PG_DISK_INFO;
	pg.list[6] = SI_PG_DISK_THIN; /* "trimmed" if fat. get it? tehe. */

	bcopy(&pg, xs->data, MIN(sizeof(pg) - fat, xs->datalen));

	atascsi_done(xs, XS_NOERROR);
}

void
atascsi_disk_vpd_serial(struct scsi_xfer *xs)
{
	struct scsi_link        *link = xs->sc_link;
	struct atascsi_port	*ap;
	struct scsi_vpd_serial	pg;

	ap = atascsi_lookup_port(link);
	bzero(&pg, sizeof(pg));

	pg.hdr.device = T_DIRECT;
	pg.hdr.page_code = SI_PG_SERIAL;
	_lto2b(sizeof(ap->ap_identify.serial), pg.hdr.page_length);
	ata_swapcopy(ap->ap_identify.serial, pg.serial,
	    sizeof(ap->ap_identify.serial));

	bcopy(&pg, xs->data, MIN(sizeof(pg), xs->datalen));

	atascsi_done(xs, XS_NOERROR);
}

void
atascsi_disk_vpd_ident(struct scsi_xfer *xs)
{
	struct scsi_link        *link = xs->sc_link;
	struct atascsi_port	*ap;
	struct {
		struct scsi_vpd_hdr	hdr;
		struct scsi_vpd_devid_hdr devid_hdr;
		u_int8_t		devid[68];
	}			pg;
	u_int8_t		*p;
	size_t			pg_len;

	ap = atascsi_lookup_port(link);
	bzero(&pg, sizeof(pg));
	if (letoh16(ap->ap_identify.features87) & ATA_ID_F87_WWN) {
		pg_len = 8;

		pg.devid_hdr.pi_code = VPD_DEVID_CODE_BINARY;
		pg.devid_hdr.flags = VPD_DEVID_ASSOC_LU | VPD_DEVID_TYPE_NAA;

		ata_swapcopy(&ap->ap_identify.naa_ieee_oui, pg.devid, pg_len);
	} else {
		pg_len = 68;

		pg.devid_hdr.pi_code = VPD_DEVID_CODE_ASCII;
		pg.devid_hdr.flags = VPD_DEVID_ASSOC_LU | VPD_DEVID_TYPE_T10;

		p = pg.devid;
		bcopy("ATA     ", p, 8);
		p += 8;
		ata_swapcopy(ap->ap_identify.model, p,
		    sizeof(ap->ap_identify.model));
		p += sizeof(ap->ap_identify.model);
		ata_swapcopy(ap->ap_identify.serial, p,
		    sizeof(ap->ap_identify.serial));
	}

	pg.devid_hdr.len = pg_len;
	pg_len += sizeof(pg.devid_hdr);

	pg.hdr.device = T_DIRECT;
	pg.hdr.page_code = SI_PG_DEVID;
	_lto2b(pg_len, pg.hdr.page_length);
	pg_len += sizeof(pg.hdr);

	bcopy(&pg, xs->data, MIN(pg_len, xs->datalen));

	atascsi_done(xs, XS_NOERROR);
}

void
atascsi_disk_vpd_ata(struct scsi_xfer *xs)
{
	struct scsi_link        *link = xs->sc_link;
	struct atascsi_port	*ap;
	struct scsi_vpd_ata	pg;

	ap = atascsi_lookup_port(link);
	bzero(&pg, sizeof(pg));

	pg.hdr.device = T_DIRECT;
	pg.hdr.page_code = SI_PG_ATA;
	_lto2b(sizeof(pg) - sizeof(pg.hdr), pg.hdr.page_length);

	memset(pg.sat_vendor, ' ', sizeof(pg.sat_vendor));
	memcpy(pg.sat_vendor, "OpenBSD",
	    MIN(strlen("OpenBSD"), sizeof(pg.sat_vendor)));
	memset(pg.sat_product, ' ', sizeof(pg.sat_product));
	memcpy(pg.sat_product, "atascsi",
	    MIN(strlen("atascsi"), sizeof(pg.sat_product)));
	memset(pg.sat_revision, ' ', sizeof(pg.sat_revision));
	memcpy(pg.sat_revision, osrelease,
	    MIN(strlen(osrelease), sizeof(pg.sat_revision)));

	/* XXX device signature */

	switch (ap->ap_type) {
	case ATA_PORT_T_DISK:
		pg.command_code = VPD_ATA_COMMAND_CODE_ATA;
		break;
	case ATA_PORT_T_ATAPI:
		pg.command_code = VPD_ATA_COMMAND_CODE_ATAPI;
		break;
	}

	memcpy(pg.identify, &ap->ap_identify, sizeof(pg.identify));

	bcopy(&pg, xs->data, MIN(sizeof(pg), xs->datalen));

	atascsi_done(xs, XS_NOERROR);
}

void
atascsi_disk_vpd_limits(struct scsi_xfer *xs)
{
	struct scsi_link        *link = xs->sc_link;
	struct atascsi_port	*ap;
	struct scsi_vpd_disk_limits pg;

	ap = atascsi_lookup_port(link);
	bzero(&pg, sizeof(pg));
	pg.hdr.device = T_DIRECT;
	pg.hdr.page_code = SI_PG_DISK_LIMITS;
	_lto2b(SI_PG_DISK_LIMITS_LEN_THIN, pg.hdr.page_length);

	_lto2b(1 << ata_identify_block_l2p_exp(&ap->ap_identify),
	    pg.optimal_xfer_granularity);

	if (ISSET(ap->ap_features, ATA_PORT_F_TRIM)) {
		/*
		 * ATA only supports 65535 blocks per TRIM descriptor, so
		 * avoid having to split UNMAP descriptors and overflow the page
		 * limit by using that as a max.
		 */
		_lto4b(ATA_DSM_TRIM_MAX_LEN, pg.max_unmap_lba_count);
		_lto4b(512 / 8, pg.max_unmap_desc_count);
        }

	bcopy(&pg, xs->data, MIN(sizeof(pg), xs->datalen));

	atascsi_done(xs, XS_NOERROR);
}

void
atascsi_disk_vpd_info(struct scsi_xfer *xs)
{
	struct scsi_link        *link = xs->sc_link;
	struct atascsi_port	*ap;
	struct scsi_vpd_disk_info pg;

	ap = atascsi_lookup_port(link);
	bzero(&pg, sizeof(pg));
	pg.hdr.device = T_DIRECT;
	pg.hdr.page_code = SI_PG_DISK_INFO;
	_lto2b(sizeof(pg) - sizeof(pg.hdr), pg.hdr.page_length);

	_lto2b(letoh16(ap->ap_identify.rpm), pg.rpm);
	pg.form_factor = letoh16(ap->ap_identify.form) & ATA_ID_FORM_MASK;

	bcopy(&pg, xs->data, MIN(sizeof(pg), xs->datalen));

	atascsi_done(xs, XS_NOERROR);
}

void
atascsi_disk_vpd_thin(struct scsi_xfer *xs)
{
	struct scsi_link        *link = xs->sc_link;
	struct atascsi_port	*ap;
	struct scsi_vpd_disk_thin pg;

	ap = atascsi_lookup_port(link);
	if (!ISSET(ap->ap_features, ATA_PORT_F_TRIM)) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	bzero(&pg, sizeof(pg));
	pg.hdr.device = T_DIRECT;
	pg.hdr.page_code = SI_PG_DISK_THIN;
	_lto2b(sizeof(pg) - sizeof(pg.hdr), pg.hdr.page_length);

	pg.flags = VPD_DISK_THIN_TPU | VPD_DISK_THIN_TPWS;

	bcopy(&pg, xs->data, MIN(sizeof(pg), xs->datalen));

	atascsi_done(xs, XS_NOERROR);
}

void
atascsi_disk_write_same_16(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct atascsi		*as = link->bus->sb_adapter_softc;
	struct atascsi_port	*ap;
	struct scsi_write_same_16 *cdb;
	struct ata_xfer		*xa = xs->io;
	struct ata_fis_h2d	*fis;
	u_int64_t		lba;
	u_int32_t		length;
	u_int64_t		desc;

	if (xs->cmdlen != sizeof(*cdb)) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	ap = atascsi_lookup_port(link);
	cdb = (struct scsi_write_same_16 *)&xs->cmd;

	if (!ISSET(cdb->flags, WRITE_SAME_F_UNMAP) ||
	   !ISSET(ap->ap_features, ATA_PORT_F_TRIM)) {
		/* generate sense data */
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	if (xs->datalen < 512) {
		/* generate sense data */
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	lba = _8btol(cdb->lba);
	length = _4btol(cdb->length);

	if (length > ATA_DSM_TRIM_MAX_LEN) {
		/* XXX we dont support requests over 65535 blocks */
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	xa->data = xs->data;
	xa->datalen = 512;
	xa->flags = ATA_F_WRITE;
	xa->pmp_port = ap->ap_pmp_port;
	if (xs->flags & SCSI_POLL)
		xa->flags |= ATA_F_POLL;
	xa->complete = atascsi_disk_write_same_16_done;
	xa->atascsi_private = xs;
	xa->timeout = (xs->timeout < 45000) ? 45000 : xs->timeout;

	/* TRIM sends a list of blocks to discard in the databuf. */
	memset(xa->data, 0, xa->datalen);
	desc = htole64(ATA_DSM_TRIM_DESC(lba, length));
	memcpy(xa->data, &desc, sizeof(desc));

	fis = xa->fis;
	fis->flags = ATA_H2D_FLAGS_CMD | ap->ap_pmp_port;
	fis->command = ATA_C_DSM;
	fis->features = ATA_DSM_TRIM;
	fis->sector_count = 1;

	ata_exec(as, xa);
}

void
atascsi_disk_write_same_16_done(struct ata_xfer *xa)
{
	struct scsi_xfer	*xs = xa->atascsi_private;

	switch (xa->state) {
	case ATA_S_COMPLETE:
		xs->error = XS_NOERROR;
		break;
	case ATA_S_ERROR:
		xs->error = XS_DRIVER_STUFFUP;
		break;
	case ATA_S_TIMEOUT:
		xs->error = XS_TIMEOUT;
		break;

	default:
		panic("atascsi_disk_write_same_16_done: "
		    "unexpected ata_xfer state (%d)", xa->state);
	}

	scsi_done(xs);
}

void
atascsi_disk_unmap(struct scsi_xfer *xs)
{
	struct ata_xfer		*xa = xs->io;
	struct scsi_unmap	*cdb;
	struct scsi_unmap_data	*unmap;
	u_int			len;

	if (ISSET(xs->flags, SCSI_POLL) || xs->cmdlen != sizeof(*cdb))
		atascsi_done(xs, XS_DRIVER_STUFFUP);

	cdb = (struct scsi_unmap *)&xs->cmd;
	len = _2btol(cdb->list_len);
	if (xs->datalen != len || len < sizeof(*unmap)) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	unmap = (struct scsi_unmap_data *)xs->data;
	if (_2btol(unmap->data_length) != len) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	len = _2btol(unmap->desc_length);
	if (len != xs->datalen - sizeof(*unmap)) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	if (len < sizeof(struct scsi_unmap_desc)) {
		/* no work, no error according to sbc3 */
		atascsi_done(xs, XS_NOERROR);
	}

	if (len > sizeof(struct scsi_unmap_desc) * 64) {
		/* more work than we advertised */
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	/* let's go */
	if (ISSET(xs->flags, SCSI_NOSLEEP)) {
		task_set(&xa->task, atascsi_disk_unmap_task, xs);
		task_add(systq, &xa->task);
	} else {
		/* we can already sleep for memory */
		atascsi_disk_unmap_task(xs);
	}
}

void
atascsi_disk_unmap_task(void *xxs)
{
	struct scsi_xfer	*xs = xxs;
	struct scsi_link	*link = xs->sc_link;
	struct atascsi		*as = link->bus->sb_adapter_softc;
	struct atascsi_port	*ap;
	struct ata_xfer		*xa = xs->io;
	struct ata_fis_h2d	*fis;
	struct scsi_unmap_data	*unmap;
	struct scsi_unmap_desc	*descs, *d;
	u_int64_t		*trims;
	u_int			len, i;

	trims = dma_alloc(512, PR_WAITOK | PR_ZERO);

	ap = atascsi_lookup_port(link);
	unmap = (struct scsi_unmap_data *)xs->data;
	descs = (struct scsi_unmap_desc *)(unmap + 1);

	len = _2btol(unmap->desc_length) / sizeof(*d);
	for (i = 0; i < len; i++) {
		d = &descs[i];
		if (_4btol(d->logical_blocks) > ATA_DSM_TRIM_MAX_LEN)
			goto fail;

		trims[i] = htole64(ATA_DSM_TRIM_DESC(_8btol(d->logical_addr),
		    _4btol(d->logical_blocks)));
	}

	xa->data = trims;
	xa->datalen = 512;
	xa->flags = ATA_F_WRITE;
	xa->pmp_port = ap->ap_pmp_port;
	xa->complete = atascsi_disk_unmap_done;
	xa->atascsi_private = xs;
	xa->timeout = (xs->timeout < 45000) ? 45000 : xs->timeout;

	fis = xa->fis;
	fis->flags = ATA_H2D_FLAGS_CMD | ap->ap_pmp_port;
	fis->command = ATA_C_DSM;
	fis->features = ATA_DSM_TRIM;
	fis->sector_count = 1;

	ata_exec(as, xa);
	return;

 fail:
	dma_free(xa->data, 512);
	atascsi_done(xs, XS_DRIVER_STUFFUP);
}

void
atascsi_disk_unmap_done(struct ata_xfer *xa)
{
	struct scsi_xfer	*xs = xa->atascsi_private;

	dma_free(xa->data, 512);

	switch (xa->state) {
	case ATA_S_COMPLETE:
		xs->error = XS_NOERROR;
		break;
	case ATA_S_ERROR:
		xs->error = XS_DRIVER_STUFFUP;
		break;
	case ATA_S_TIMEOUT:
		xs->error = XS_TIMEOUT;
		break;

	default:
		panic("atascsi_disk_unmap_done: "
		    "unexpected ata_xfer state (%d)", xa->state);
	}

	scsi_done(xs);
}

void
atascsi_disk_sync(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct atascsi		*as = link->bus->sb_adapter_softc;
	struct atascsi_port	*ap;
	struct ata_xfer		*xa = xs->io;

	if (xs->cmdlen != sizeof(struct scsi_synchronize_cache)) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	ap = atascsi_lookup_port(link);
	xa->datalen = 0;
	xa->flags = ATA_F_READ;
	xa->complete = atascsi_disk_sync_done;
	/* Spec says flush cache can take >30 sec, so give it at least 45. */
	xa->timeout = (xs->timeout < 45000) ? 45000 : xs->timeout;
	xa->atascsi_private = xs;
	xa->pmp_port = ap->ap_pmp_port;
	if (xs->flags & SCSI_POLL)
		xa->flags |= ATA_F_POLL;

	xa->fis->flags = ATA_H2D_FLAGS_CMD | ap->ap_pmp_port;
	xa->fis->command = ATA_C_FLUSH_CACHE;
	xa->fis->device = 0;

	ata_exec(as, xa);
}

void
atascsi_disk_sync_done(struct ata_xfer *xa)
{
	struct scsi_xfer	*xs = xa->atascsi_private;

	switch (xa->state) {
	case ATA_S_COMPLETE:
		xs->error = XS_NOERROR;
		break;

	case ATA_S_ERROR:
	case ATA_S_TIMEOUT:
		printf("atascsi_disk_sync_done: %s\n",
		    xa->state == ATA_S_TIMEOUT ? "timeout" : "error");
		xs->error = (xa->state == ATA_S_TIMEOUT ? XS_TIMEOUT :
		    XS_DRIVER_STUFFUP);
		break;

	default:
		panic("atascsi_disk_sync_done: unexpected ata_xfer state (%d)",
		    xa->state);
	}

	scsi_done(xs);
}

u_int64_t
ata_identify_blocks(struct ata_identify *id)
{
	u_int64_t		blocks = 0;
	int			i;

	if (letoh16(id->cmdset83) & 0x0400) {
		/* LBA48 feature set supported */
		for (i = 3; i >= 0; --i) {
			blocks <<= 16;
			blocks += letoh16(id->addrsecxt[i]);
		}
	} else {
		blocks = letoh16(id->addrsec[1]);
		blocks <<= 16;
		blocks += letoh16(id->addrsec[0]);
	}

	return (blocks - 1);
}

u_int
ata_identify_blocksize(struct ata_identify *id)
{
	u_int			blocksize = 512;
	u_int16_t		p2l_sect = letoh16(id->p2l_sect);

	if ((p2l_sect & ATA_ID_P2L_SECT_MASK) == ATA_ID_P2L_SECT_VALID &&
	    ISSET(p2l_sect, ATA_ID_P2L_SECT_SIZESET)) {
		blocksize = letoh16(id->words_lsec[1]);
		blocksize <<= 16;
		blocksize += letoh16(id->words_lsec[0]);
		blocksize <<= 1;
	}

	return (blocksize);
}

u_int
ata_identify_block_l2p_exp(struct ata_identify *id)
{
	u_int			exponent = 0;
	u_int16_t		p2l_sect = letoh16(id->p2l_sect);

	if ((p2l_sect & ATA_ID_P2L_SECT_MASK) == ATA_ID_P2L_SECT_VALID &&
	    ISSET(p2l_sect, ATA_ID_P2L_SECT_SET)) {
		exponent = (p2l_sect & ATA_ID_P2L_SECT_SIZE);
	}

	return (exponent);
}

u_int
ata_identify_block_logical_align(struct ata_identify *id)
{
	u_int			align = 0;
	u_int16_t		p2l_sect = letoh16(id->p2l_sect);
	u_int16_t		logical_align = letoh16(id->logical_align);

	if ((p2l_sect & ATA_ID_P2L_SECT_MASK) == ATA_ID_P2L_SECT_VALID &&
	    ISSET(p2l_sect, ATA_ID_P2L_SECT_SET) &&
	    (logical_align & ATA_ID_LALIGN_MASK) == ATA_ID_LALIGN_VALID)
		align = logical_align & ATA_ID_LALIGN;

	return (align);
}

void
atascsi_disk_capacity(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct atascsi_port	*ap;
	struct scsi_read_cap_data rcd;
	u_int64_t		capacity;

	ap = atascsi_lookup_port(link);
	if (xs->cmdlen != sizeof(struct scsi_read_capacity)) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	bzero(&rcd, sizeof(rcd));
	capacity = ata_identify_blocks(&ap->ap_identify);
	if (capacity > 0xffffffff)
		capacity = 0xffffffff;

	_lto4b(capacity, rcd.addr);
	_lto4b(ata_identify_blocksize(&ap->ap_identify), rcd.length);

	bcopy(&rcd, xs->data, MIN(sizeof(rcd), xs->datalen));

	atascsi_done(xs, XS_NOERROR);
}

void
atascsi_disk_capacity16(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct atascsi_port	*ap;
	struct scsi_read_cap_data_16 rcd;
	u_int			align;
	u_int16_t		lowest_aligned = 0;

	ap = atascsi_lookup_port(link);
	if (xs->cmdlen != sizeof(struct scsi_read_capacity_16)) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	bzero(&rcd, sizeof(rcd));

	_lto8b(ata_identify_blocks(&ap->ap_identify), rcd.addr);
	_lto4b(ata_identify_blocksize(&ap->ap_identify), rcd.length);
	rcd.logical_per_phys = ata_identify_block_l2p_exp(&ap->ap_identify);
	align = ata_identify_block_logical_align(&ap->ap_identify);
	if (align > 0)
		lowest_aligned = (1 << rcd.logical_per_phys) - align;

	if (ISSET(ap->ap_features, ATA_PORT_F_TRIM)) {
		SET(lowest_aligned, READ_CAP_16_TPE);

		if (ISSET(letoh16(ap->ap_identify.add_support),
		    ATA_ID_ADD_SUPPORT_DRT))
			SET(lowest_aligned, READ_CAP_16_TPRZ);
	}
	_lto2b(lowest_aligned, rcd.lowest_aligned);

	bcopy(&rcd, xs->data, MIN(sizeof(rcd), xs->datalen));

	atascsi_done(xs, XS_NOERROR);
}

int
atascsi_passthru_map(struct scsi_xfer *xs, u_int8_t count_proto, u_int8_t flags)
{
	struct ata_xfer		*xa = xs->io;

	xa->data = xs->data;
	xa->datalen = xs->datalen;
	xa->timeout = xs->timeout;
	xa->flags = 0;
	if (xs->flags & SCSI_DATA_IN)
		xa->flags |= ATA_F_READ;
	if (xs->flags & SCSI_DATA_OUT)
		xa->flags |= ATA_F_WRITE;
	if (xs->flags & SCSI_POLL)
		xa->flags |= ATA_F_POLL;

	switch (count_proto & ATA_PASSTHRU_PROTO_MASK) {
	case ATA_PASSTHRU_PROTO_NON_DATA:
	case ATA_PASSTHRU_PROTO_PIO_DATAIN:
	case ATA_PASSTHRU_PROTO_PIO_DATAOUT:
		xa->flags |= ATA_F_PIO;
		break;
	default:
		/* we dont support this yet */
		return (1);
	}

	xa->atascsi_private = xs;
	xa->complete = atascsi_passthru_done;

	return (0);
}

void
atascsi_passthru_12(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct atascsi		*as = link->bus->sb_adapter_softc;
	struct atascsi_port	*ap;
	struct ata_xfer		*xa = xs->io;
	struct scsi_ata_passthru_12 *cdb;
	struct ata_fis_h2d	*fis;

	if (xs->cmdlen != sizeof(*cdb)) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	cdb = (struct scsi_ata_passthru_12 *)&xs->cmd;
	/* validate cdb */

	if (atascsi_passthru_map(xs, cdb->count_proto, cdb->flags) != 0) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	ap = atascsi_lookup_port(link);
	fis = xa->fis;
	fis->flags = ATA_H2D_FLAGS_CMD | ap->ap_pmp_port;
	fis->command = cdb->command;
	fis->features = cdb->features;
	fis->lba_low = cdb->lba_low;
	fis->lba_mid = cdb->lba_mid;
	fis->lba_high = cdb->lba_high;
	fis->device = cdb->device;
	fis->sector_count = cdb->sector_count;
	xa->pmp_port = ap->ap_pmp_port;

	ata_exec(as, xa);
}

void
atascsi_passthru_16(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct atascsi		*as = link->bus->sb_adapter_softc;
	struct atascsi_port	*ap;
	struct ata_xfer		*xa = xs->io;
	struct scsi_ata_passthru_16 *cdb;
	struct ata_fis_h2d	*fis;

	if (xs->cmdlen != sizeof(*cdb)) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	cdb = (struct scsi_ata_passthru_16 *)&xs->cmd;
	/* validate cdb */

	if (atascsi_passthru_map(xs, cdb->count_proto, cdb->flags) != 0) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	ap = atascsi_lookup_port(link);
	fis = xa->fis;
	fis->flags = ATA_H2D_FLAGS_CMD | ap->ap_pmp_port;
	fis->command = cdb->command;
	fis->features = cdb->features[1];
	fis->lba_low = cdb->lba_low[1];
	fis->lba_mid = cdb->lba_mid[1];
	fis->lba_high = cdb->lba_high[1];
	fis->device = cdb->device;
	fis->lba_low_exp = cdb->lba_low[0];
	fis->lba_mid_exp = cdb->lba_mid[0];
	fis->lba_high_exp = cdb->lba_high[0];
	fis->features_exp = cdb->features[0];
	fis->sector_count = cdb->sector_count[1];
	fis->sector_count_exp = cdb->sector_count[0];
	xa->pmp_port = ap->ap_pmp_port;

	ata_exec(as, xa);
}

void
atascsi_passthru_done(struct ata_xfer *xa)
{
	struct scsi_xfer	*xs = xa->atascsi_private;

	/*
	 * XXX need to generate sense if cdb wants it
	 */

	switch (xa->state) {
	case ATA_S_COMPLETE:
		xs->error = XS_NOERROR;
		break;
	case ATA_S_ERROR:
		xs->error = XS_DRIVER_STUFFUP;
		break;
	case ATA_S_TIMEOUT:
		printf("atascsi_passthru_done, timeout\n");
		xs->error = XS_TIMEOUT;
		break;
	default:
		panic("atascsi_atapi_cmd_done: unexpected ata_xfer state (%d)",
		    xa->state);
	}

	xs->resid = xa->resid;

	scsi_done(xs);
}

void
atascsi_disk_sense(struct scsi_xfer *xs)
{
	struct scsi_sense_data	*sd = (struct scsi_sense_data *)xs->data;

	bzero(xs->data, xs->datalen);
	/* check datalen > sizeof(struct scsi_sense_data)? */
	sd->error_code = SSD_ERRCODE_CURRENT;
	sd->flags = SKEY_NO_SENSE;

	atascsi_done(xs, XS_NOERROR);
}

void
atascsi_disk_start_stop(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct atascsi		*as = link->bus->sb_adapter_softc;
	struct atascsi_port	*ap;
	struct ata_xfer		*xa = xs->io;
	struct scsi_start_stop	*ss = (struct scsi_start_stop *)&xs->cmd;

	if (xs->cmdlen != sizeof(*ss)) {
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	if (ss->how != SSS_STOP) {
		atascsi_done(xs, XS_NOERROR);
		return;
	}

	/*
	 * A SCSI START STOP UNIT command with the START bit set to
	 * zero gets translated into an ATA FLUSH CACHE command
	 * followed by an ATA STANDBY IMMEDIATE command.
	 */
	ap = atascsi_lookup_port(link);
	xa->datalen = 0;
	xa->flags = ATA_F_READ;
	xa->complete = atascsi_disk_start_stop_done;
	/* Spec says flush cache can take >30 sec, so give it at least 45. */
	xa->timeout = (xs->timeout < 45000) ? 45000 : xs->timeout;
	xa->pmp_port = ap->ap_pmp_port;
	xa->atascsi_private = xs;
	if (xs->flags & SCSI_POLL)
		xa->flags |= ATA_F_POLL;

	xa->fis->flags = ATA_H2D_FLAGS_CMD | ap->ap_pmp_port;
	xa->fis->command = ATA_C_FLUSH_CACHE;
	xa->fis->device = 0;

	ata_exec(as, xa);
}

void
atascsi_disk_start_stop_done(struct ata_xfer *xa)
{
	struct scsi_xfer	*xs = xa->atascsi_private;
	struct scsi_link	*link = xs->sc_link;
	struct atascsi		*as = link->bus->sb_adapter_softc;
	struct atascsi_port	*ap;

	switch (xa->state) {
	case ATA_S_COMPLETE:
		break;

	case ATA_S_ERROR:
	case ATA_S_TIMEOUT:
		xs->error = (xa->state == ATA_S_TIMEOUT ? XS_TIMEOUT :
		    XS_DRIVER_STUFFUP);
		xs->resid = xa->resid;
		scsi_done(xs);
		return;

	default:
		panic("atascsi_disk_start_stop_done: unexpected ata_xfer state (%d)",
		    xa->state);
	}

	/*
	 * The FLUSH CACHE command completed successfully; now issue
	 * the STANDBY IMMEDIATE command.
	 */
	ap = atascsi_lookup_port(link);
	xa->datalen = 0;
	xa->flags = ATA_F_READ;
	xa->state = ATA_S_SETUP;
	xa->complete = atascsi_disk_cmd_done;
	/* Spec says flush cache can take >30 sec, so give it at least 45. */
	xa->timeout = (xs->timeout < 45000) ? 45000 : xs->timeout;
	xa->pmp_port = ap->ap_pmp_port;
	xa->atascsi_private = xs;
	if (xs->flags & SCSI_POLL)
		xa->flags |= ATA_F_POLL;

	xa->fis->flags = ATA_H2D_FLAGS_CMD | ap->ap_pmp_port;
	xa->fis->command = ATA_C_STANDBY_IMMED;
	xa->fis->device = 0;

	ata_exec(as, xa);
}

void
atascsi_atapi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct atascsi		*as = link->bus->sb_adapter_softc;
	struct atascsi_port	*ap;
	struct ata_xfer		*xa = xs->io;
	struct ata_fis_h2d	*fis;

	switch (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
	case SCSI_DATA_IN:
		xa->flags = ATA_F_PACKET | ATA_F_READ;
		break;
	case SCSI_DATA_OUT:
		xa->flags = ATA_F_PACKET | ATA_F_WRITE;
		break;
	default:
		xa->flags = ATA_F_PACKET;
	}
	xa->flags |= ATA_F_GET_RFIS;

	ap = atascsi_lookup_port(link);
	xa->data = xs->data;
	xa->datalen = xs->datalen;
	xa->complete = atascsi_atapi_cmd_done;
	xa->timeout = xs->timeout;
	xa->pmp_port = ap->ap_pmp_port;
	xa->atascsi_private = xs;
	if (xs->flags & SCSI_POLL)
		xa->flags |= ATA_F_POLL;

	fis = xa->fis;
	fis->flags = ATA_H2D_FLAGS_CMD | ap->ap_pmp_port;
	fis->command = ATA_C_PACKET;
	fis->device = 0;
	fis->sector_count = xa->tag << 3;
	fis->features = ATA_H2D_FEATURES_DMA | ((xa->flags & ATA_F_WRITE) ?
	    ATA_H2D_FEATURES_DIR_WRITE : ATA_H2D_FEATURES_DIR_READ);
	fis->lba_mid = 0x00;
	fis->lba_high = 0x20;

	/* Copy SCSI command into ATAPI packet. */
	memcpy(xa->packetcmd, &xs->cmd, xs->cmdlen);

	ata_exec(as, xa);
}

void
atascsi_atapi_cmd_done(struct ata_xfer *xa)
{
	struct scsi_xfer	*xs = xa->atascsi_private;
	struct scsi_sense_data  *sd = &xs->sense;

	switch (xa->state) {
	case ATA_S_COMPLETE:
		xs->error = XS_NOERROR;
		break;
	case ATA_S_ERROR:
		/* Return PACKET sense data */
		sd->error_code = SSD_ERRCODE_CURRENT;
		sd->flags = (xa->rfis.error & 0xf0) >> 4;
		if (xa->rfis.error & 0x04)
			sd->flags = SKEY_ILLEGAL_REQUEST;
		if (xa->rfis.error & 0x02)
			sd->flags |= SSD_EOM;
		if (xa->rfis.error & 0x01)
			sd->flags |= SSD_ILI;
		xs->error = XS_SENSE;
		break;
	case ATA_S_TIMEOUT:
		printf("atascsi_atapi_cmd_done, timeout\n");
		xs->error = XS_TIMEOUT;
		break;
	default:
		panic("atascsi_atapi_cmd_done: unexpected ata_xfer state (%d)",
		    xa->state);
	}

	xs->resid = xa->resid;

	scsi_done(xs);
}

void
atascsi_pmp_cmd(struct scsi_xfer *xs)
{
	switch (xs->cmd.opcode) {
	case REQUEST_SENSE:
		atascsi_pmp_sense(xs);
		return;
	case INQUIRY:
		atascsi_pmp_inq(xs);
		return;

	case TEST_UNIT_READY:
	case PREVENT_ALLOW:
		atascsi_done(xs, XS_NOERROR);
		return;

	default:
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}
}

void
atascsi_pmp_sense(struct scsi_xfer *xs)
{
	struct scsi_sense_data *sd = (struct scsi_sense_data *)xs->data;

	bzero(xs->data, xs->datalen);
	sd->error_code = SSD_ERRCODE_CURRENT;
	sd->flags = SKEY_NO_SENSE;

	atascsi_done(xs, XS_NOERROR);
}

void
atascsi_pmp_inq(struct scsi_xfer *xs)
{
	struct scsi_inquiry_data inq;
	struct scsi_inquiry *in_inq = (struct scsi_inquiry *)&xs->cmd;

	if (ISSET(in_inq->flags, SI_EVPD)) {
		/* any evpd pages we need to support here? */
		atascsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	bzero(&inq, sizeof(inq));
	inq.device = 0x1E;	/* "well known logical unit" seems reasonable */
	inq.version = SCSI_REV_SPC3;
	inq.response_format = SID_SCSI2_RESPONSE;
	inq.additional_length = SID_SCSI2_ALEN;
	inq.flags |= SID_CmdQue;
	bcopy("ATA     ", inq.vendor, sizeof(inq.vendor));

	/* should use the data from atascsi_pmp_identify here?
	 * not sure how useful the chip id is, but maybe it'd be
	 * nice to include the number of ports.
	 */
	bcopy("Port Multiplier", inq.product, sizeof(inq.product));
	bcopy("    ", inq.revision, sizeof(inq.revision));

	scsi_copy_internal_data(xs, &inq, sizeof(inq));

	atascsi_done(xs, XS_NOERROR);
}

void
atascsi_done(struct scsi_xfer *xs, int error)
{
	xs->error = error;
	scsi_done(xs);
}

void
ata_exec(struct atascsi *as, struct ata_xfer *xa)
{
	as->as_methods->ata_cmd(xa);
}

void *
atascsi_io_get(void *cookie)
{
	struct atascsi_host_port	*ahp = cookie;
	struct atascsi			*as = ahp->ahp_as;
	struct ata_xfer			*xa;

	xa = as->as_methods->ata_get_xfer(as->as_cookie, ahp->ahp_port);
	if (xa != NULL)
		xa->fis->type = ATA_FIS_TYPE_H2D;

	return (xa);
}

void
atascsi_io_put(void *cookie, void *io)
{
	struct atascsi_host_port	*ahp = cookie;
	struct atascsi			*as = ahp->ahp_as;
	struct ata_xfer			*xa = io;

	xa->state = ATA_S_COMPLETE; /* XXX this state machine is dumb */
	as->as_methods->ata_put_xfer(xa);
}

void
ata_polled_complete(struct ata_xfer *xa)
{
	/* do nothing */
}

int
ata_polled(struct ata_xfer *xa)
{
	int			rv;

	if (!ISSET(xa->flags, ATA_F_DONE))
		panic("ata_polled: xa isn't complete");

	switch (xa->state) {
	case ATA_S_COMPLETE:
		rv = 0;
		break;
	case ATA_S_ERROR:
	case ATA_S_TIMEOUT:
		rv = EIO;
		break;
	default:
		panic("ata_polled: xa state (%d)",
		    xa->state);
	}

	scsi_io_put(xa->atascsi_private, xa);

	return (rv);
}

void
ata_complete(struct ata_xfer *xa)
{
	SET(xa->flags, ATA_F_DONE);
	xa->complete(xa);
}

void
ata_swapcopy(void *src, void *dst, size_t len)
{
	u_int16_t *s = src, *d = dst;
	int i;

	len /= 2;

	for (i = 0; i < len; i++)
		d[i] = swap16(s[i]);
}

int
atascsi_port_identify(struct atascsi_port *ap, struct ata_identify *identify)
{
	struct atascsi			*as = ap->ap_as;
	struct atascsi_host_port	*ahp = ap->ap_host_port;
	struct ata_xfer			*xa;

	xa = scsi_io_get(&ahp->ahp_iopool, SCSI_NOSLEEP);
	if (xa == NULL)
		panic("no free xfers on a new port");
	xa->pmp_port = ap->ap_pmp_port;
	xa->data = identify;
	xa->datalen = sizeof(*identify);
	xa->fis->flags = ATA_H2D_FLAGS_CMD | ap->ap_pmp_port;
	xa->fis->command = (ap->ap_type == ATA_PORT_T_DISK) ?
	    ATA_C_IDENTIFY : ATA_C_IDENTIFY_PACKET;
	xa->fis->device = 0;
	xa->flags = ATA_F_READ | ATA_F_PIO | ATA_F_POLL;
	xa->timeout = 1000;
	xa->complete = ata_polled_complete;
	xa->atascsi_private = &ahp->ahp_iopool;
	ata_exec(as, xa);
	return (ata_polled(xa));
}

int
atascsi_port_set_features(struct atascsi_port *ap, int subcommand, int arg)
{
	struct atascsi			*as = ap->ap_as;
	struct atascsi_host_port	*ahp = ap->ap_host_port;
	struct ata_xfer			*xa;

	xa = scsi_io_get(&ahp->ahp_iopool, SCSI_NOSLEEP);
	if (xa == NULL)
		panic("no free xfers on a new port");
	xa->fis->command = ATA_C_SET_FEATURES;
	xa->fis->features = subcommand;
	xa->fis->sector_count = arg;
	xa->fis->flags = ATA_H2D_FLAGS_CMD | ap->ap_pmp_port;
	xa->flags = ATA_F_POLL;
	xa->timeout = 1000;
	xa->complete = ata_polled_complete;
	xa->pmp_port = ap->ap_pmp_port;
	xa->atascsi_private = &ahp->ahp_iopool;
	ata_exec(as, xa);
	return (ata_polled(xa));
}
