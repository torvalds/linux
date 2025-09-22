/*	$OpenBSD: scsiconf.h,v 1.202 2023/05/10 15:28:26 krw Exp $	*/
/*	$NetBSD: scsiconf.h,v 1.35 1997/04/02 02:29:38 mycroft Exp $	*/

/*
 * Copyright (c) 1993, 1994, 1995 Charles Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

/*
 * Originally written by Julian Elischer (julian@tfs.com)
 * for TRW Financial Systems for use under the MACH(2.5) operating system.
 *
 * TRW Financial Systems, in accordance with their agreement with Carnegie
 * Mellon University, makes this software available to CMU to distribute
 * or use in any manner that they see fit as long as this message is kept with
 * the software. For this reason TFS also grants any other persons or
 * organisations permission to use or modify this software.
 *
 * TFS supplies this software to be publicly redistributed
 * on the understanding that TFS is not responsible for the correct
 * functioning of this software in any circumstances.
 *
 * Ported to run under 386BSD by Julian Elischer (julian@tfs.com) Sept 1992
 */

#ifndef	_SCSI_SCSICONF_H
#define _SCSI_SCSICONF_H

#include <sys/queue.h>
#include <sys/timeout.h>
#include <sys/mutex.h>

static __inline void _lto2b(u_int32_t val, u_int8_t *bytes);
static __inline void _lto3b(u_int32_t val, u_int8_t *bytes);
static __inline void _lto4b(u_int32_t val, u_int8_t *bytes);
static __inline void _lto8b(u_int64_t val, u_int8_t *bytes);
static __inline u_int32_t _2btol(u_int8_t *bytes);
static __inline u_int32_t _3btol(u_int8_t *bytes);
static __inline u_int32_t _4btol(u_int8_t *bytes);
static __inline u_int64_t _5btol(u_int8_t *bytes);
static __inline u_int64_t _8btol(u_int8_t *bytes);

static __inline void
_lto2b(u_int32_t val, u_int8_t *bytes)
{

	bytes[0] = (val >> 8) & 0xff;
	bytes[1] = val & 0xff;
}

static __inline void
_lto3b(u_int32_t val, u_int8_t *bytes)
{

	bytes[0] = (val >> 16) & 0xff;
	bytes[1] = (val >> 8) & 0xff;
	bytes[2] = val & 0xff;
}

static __inline void
_lto4b(u_int32_t val, u_int8_t *bytes)
{

	bytes[0] = (val >> 24) & 0xff;
	bytes[1] = (val >> 16) & 0xff;
	bytes[2] = (val >> 8) & 0xff;
	bytes[3] = val & 0xff;
}

static __inline void
_lto8b(u_int64_t val, u_int8_t *bytes)
{

	bytes[0] = (val >> 56) & 0xff;
	bytes[1] = (val >> 48) & 0xff;
	bytes[2] = (val >> 40) & 0xff;
	bytes[3] = (val >> 32) & 0xff;
	bytes[4] = (val >> 24) & 0xff;
	bytes[5] = (val >> 16) & 0xff;
	bytes[6] = (val >> 8) & 0xff;
	bytes[7] = val & 0xff;
}

static __inline u_int32_t
_2btol(u_int8_t *bytes)
{
	u_int32_t rv;

	rv = (bytes[0] << 8) | bytes[1];
	return rv;
}

static __inline u_int32_t
_3btol(u_int8_t *bytes)
{
	u_int32_t rv;

	rv = (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];
	return rv;
}

static __inline u_int32_t
_4btol(u_int8_t *bytes)
{
	u_int32_t rv;

	rv = (bytes[0] << 24) | (bytes[1] << 16) |
	    (bytes[2] << 8) | bytes[3];
	return rv;
}

static __inline u_int64_t
_5btol(u_int8_t *bytes)
{
	u_int64_t rv;

	rv = ((u_int64_t)bytes[0] << 32) |
	     ((u_int64_t)bytes[1] << 24) |
	     ((u_int64_t)bytes[2] << 16) |
	     ((u_int64_t)bytes[3] << 8) |
	     (u_int64_t)bytes[4];
	return rv;
}

static __inline u_int64_t
_8btol(u_int8_t *bytes)
{
	u_int64_t rv;

	rv = (((u_int64_t)bytes[0]) << 56) |
	    (((u_int64_t)bytes[1]) << 48) |
	    (((u_int64_t)bytes[2]) << 40) |
	    (((u_int64_t)bytes[3]) << 32) |
	    (((u_int64_t)bytes[4]) << 24) |
	    (((u_int64_t)bytes[5]) << 16) |
	    (((u_int64_t)bytes[6]) << 8) |
	    ((u_int64_t)bytes[7]);
	return rv;
}

#ifdef _KERNEL

#define DEVID_NONE	0
#define DEVID_NAA	1
#define DEVID_EUI	2
#define DEVID_T10	3
#define DEVID_SERIAL	4
#define DEVID_WWN	5

struct devid {
	u_int8_t	d_type;
	u_int8_t	d_flags;
#define DEVID_F_PRINT		(1<<0)
	u_int8_t	d_refcount;
	u_int8_t	d_len;

	/*
	 * the devid struct is basically a header, the actual id is allocated
	 * immediately after it.
	 */
};

#define DEVID_CMP(_a, _b) (					\
	(_a) != NULL && (_b) != NULL &&				\
	((_a) == (_b) ||					\
	((_a)->d_type != DEVID_NONE &&				\
	 (_a)->d_type == (_b)->d_type &&			\
	 (_a)->d_len == (_b)->d_len &&				\
	 bcmp((_a) + 1, (_b) + 1, (_a)->d_len) == 0))		\
)

struct devid *	devid_alloc(u_int8_t, u_int8_t, u_int8_t, u_int8_t *);
struct devid *	devid_copy(struct devid *);
void		devid_free(struct devid *);

/*
 * Each existing device (scsibus + target + lun)
 *    - is described by a scsi_link struct.
 * Each scsi_link struct
 *    - identifies the device's softc and scsi_adapter.
 * Each scsi_adapter struct
 *    - contains pointers to the device's scsi functions.
 * Each scsibus_softc has an SLIST
 *    - holding pointers to the scsi_link structs of devices on that scsi bus.
 * Each individual device
 *    - knows the address of its scsi_link structure.
 */

struct scsi_xfer;
struct scsi_link;
struct scsibus_softc;

/*
 * Temporary hack
 */
extern int scsi_autoconf;

/*
 * These entrypoints are called by the high-end drivers to get services from
 * whatever low-end drivers they are attached to.  Each adapter type has one
 * of these statically allocated.
 */
struct scsi_adapter {
	void		(*scsi_cmd)(struct scsi_xfer *);
	void		(*dev_minphys)(struct buf *, struct scsi_link *);
	int		(*dev_probe)(struct scsi_link *);
	void		(*dev_free)(struct scsi_link *);
	int		(*ioctl)(struct scsi_link *, u_long, caddr_t, int);
};

struct scsi_iopool;

struct scsi_iohandler {
	TAILQ_ENTRY(scsi_iohandler) q_entry;
	u_int q_state;

	struct scsi_iopool *pool;
	void (*handler)(void *, void *);
	void *cookie;
};
TAILQ_HEAD(scsi_runq, scsi_iohandler);

struct scsi_iopool {
	/* access to the IOs */
	void	*iocookie;
	/*
	 * Get an IO. This must reserve all resources that are necessary
	 * to send the transfer to the device. The resources must stay
	 * reserved during the lifetime of the IO, as the IO may be re-used
	 * without being io_put(), first.
	 */
	void	*(*io_get)(void *);
	void	 (*io_put)(void *, void *);

	/* the runqueue */
	struct scsi_runq queue;
	/* runqueue semaphore */
	u_int running;
	/* protection for the runqueue and its semaphore */
	struct mutex mtx;
};

struct scsi_xshandler {
	struct scsi_iohandler ioh; /* must be first */

	struct scsi_link *link;
	void (*handler)(struct scsi_xfer *);
};

/*
 * This structure describes the connection between an adapter driver and
 * a device driver, and is used by each to call services provided by
 * the other, and to allow generic scsi glue code to call these services
 * as well.
 */
struct scsi_link {
	SLIST_ENTRY(scsi_link)	bus_list;

	u_int		state;
#define SDEV_S_DYING		(1<<1)

	u_int16_t target;		/* targ of this dev */
	u_int16_t lun;			/* lun of this dev */
	u_int16_t openings;		/* available operations per lun */
	u_int64_t port_wwn;		/* world wide name of port */
	u_int64_t node_wwn;		/* world wide name of node */
	u_int16_t flags;		/* flags that all devices have */
#define	SDEV_REMOVABLE		0x0001	/* media is removable */
#define	SDEV_MEDIA_LOADED	0x0002	/* device figures are still valid */
#define	SDEV_READONLY		0x0004	/* device is read-only */
#define	SDEV_OPEN		0x0008	/* at least 1 open session */
#define	SDEV_DBX		0x00f0	/* debugging flags (scsi_debug.h) */
#define	SDEV_EJECTING		0x0100	/* eject on device close */
#define	SDEV_ATAPI		0x0200	/* device is ATAPI */
#define SDEV_UMASS		0x0400	/* device is UMASS SCSI */
#define SDEV_VIRTUAL		0x0800	/* device is virtualised on the hba */
#define SDEV_OWN_IOPL		0x1000	/* scsibus */
#define SDEV_UFI		0x2000	/* Universal Floppy Interface */
	u_int16_t quirks;		/* per-device oddities */
#define	SDEV_AUTOSAVE		0x0001	/* do implicit SAVEDATAPOINTER on disconnect */
#define	SDEV_NOSYNC		0x0002	/* does not grok SDTR */
#define	SDEV_NOWIDE		0x0004	/* does not grok WDTR */
#define	SDEV_NOTAGS		0x0008	/* lies about having tagged queueing */
#define	SDEV_NOSYNCCACHE	0x0010	/* no SYNCHRONIZE_CACHE */
#define	ADEV_NOSENSE		0x0020	/* No request sense - ATAPI */
#define	ADEV_LITTLETOC		0x0040	/* little-endian TOC - ATAPI */
#define	ADEV_NOCAPACITY		0x0080	/* no READ CD CAPACITY */
#define	ADEV_NODOORLOCK		0x0100	/* can't lock door */
	int	(*interpret_sense)(struct scsi_xfer *);
	void	*device_softc;		/* needed for call to foo_start */
	struct	scsibus_softc *bus;	/* link to the scsibus we're on */
	struct	scsi_inquiry_data inqdata; /* copy of INQUIRY data from probe */
	struct  devid *id;

	struct	scsi_runq queue;
	u_int	running;
	u_short	pending;

	struct	scsi_iopool *pool;
};

int	scsiprint(void *, const char *);

/*
 * This describes matching information for scsi_inqmatch().  The more things
 * match, the higher the configuration priority.
 */
struct scsi_inquiry_pattern {
	u_int8_t type;
	int removable;
	char *vendor;
	char *product;
	char *revision;
};

struct scsibus_attach_args {
	const struct scsi_adapter *saa_adapter;
	void			*saa_adapter_softc;
	struct	scsi_iopool	*saa_pool;
	u_int64_t		 saa_wwpn;
	u_int64_t		 saa_wwnn;
	u_int16_t		 saa_quirks;
	u_int16_t		 saa_flags;
	u_int16_t		 saa_openings;
	u_int16_t		 saa_adapter_target;
#define	SDEV_NO_ADAPTER_TARGET	0xffff
	u_int16_t		 saa_adapter_buswidth;
	u_int8_t		 saa_luns;
};

/*
 * One of these is allocated and filled in for each scsi bus.
 * It holds pointers to allow the scsi bus to get to the driver
 * that is running each LUN on the bus.
 * It also has a template entry which is the prototype struct
 * supplied by the adapter driver.  This is used to initialise
 * the others, before they have the rest of the fields filled in.
 */
struct scsibus_softc {
	struct device		 sc_dev;
	SLIST_HEAD(, scsi_link)  sc_link_list;
	void			*sb_adapter_softc;
	const struct scsi_adapter *sb_adapter;
	struct	scsi_iopool	*sb_pool;
	u_int16_t		 sb_quirks;
	u_int16_t		 sb_flags;
	u_int16_t		 sb_openings;
	u_int16_t		 sb_adapter_buswidth;
	u_int16_t		 sb_adapter_target;
	u_int8_t		 sb_luns;
};

/*
 * This is used to pass information from the high-level configuration code
 * to the device-specific drivers.
 */
struct scsi_attach_args {
	struct scsi_link *sa_sc_link;
};

/*
 * Each scsi transaction is fully described by one of these structures.
 * It includes information about the source of the command and also the
 * device and adapter for which the command is destined.
 * (via the scsi_link structure)
 */
struct scsi_xfer {
	SIMPLEQ_ENTRY(scsi_xfer) xfer_list;
	int	flags;
	struct	scsi_link *sc_link;	/* all about our device and adapter */
	int	retries;		/* the number of times to retry */
	int	timeout;		/* in milliseconds */
	struct	scsi_generic cmd;	/* The scsi command to execute */
	int	cmdlen;			/* how long it is */
	u_char	*data;			/* dma address OR a uio address */
	int	datalen;		/* data len (blank if uio)    */
	size_t	resid;			/* how much buffer was not touched */
	int	error;			/* an error value	*/
	struct	buf *bp;		/* If we need to associate with a buf */
	struct	scsi_sense_data	sense;	/* 18 bytes*/
	u_int8_t status;		/* SCSI status */
	/*
	 * timeout structure for hba's to use for a command
	 */
	struct timeout stimeout;
	void *cookie;
	void (*done)(struct scsi_xfer *);

	void *io;			/* adapter io resource */
};
SIMPLEQ_HEAD(scsi_xfer_list, scsi_xfer);

/*
 * Per-request Flag values
 */
#define	SCSI_NOSLEEP	0x00001	/* don't sleep */
#define	SCSI_POLL	0x00002	/* poll for completion */
#define	SCSI_AUTOCONF	0x00003	/* shorthand for SCSI_POLL | SCSI_NOSLEEP */
#define	ITSDONE		0x00008	/* the transfer is as done as it gets	*/
#define	SCSI_SILENT	0x00020	/* don't announce NOT READY or MEDIA CHANGE */
#define	SCSI_IGNORE_NOT_READY		0x00040	/* ignore NOT READY */
#define	SCSI_IGNORE_MEDIA_CHANGE	0x00080	/* ignore MEDIA CHANGE */
#define	SCSI_IGNORE_ILLEGAL_REQUEST	0x00100	/* ignore ILLEGAL REQUEST */
#define	SCSI_RESET	0x00200	/* Reset the device in question		*/
#define	SCSI_DATA_IN	0x00800	/* expect data to come INTO memory	*/
#define	SCSI_DATA_OUT	0x01000	/* expect data to flow OUT of memory	*/
#define	SCSI_TARGET	0x02000	/* This defines a TARGET mode op.	*/
#define	SCSI_ESCAPE	0x04000	/* Escape operation			*/
#define	SCSI_PRIVATE	0xf0000	/* private to each HBA flags */

/*
 * Escape op-codes.  This provides an extensible setup for operations
 * that are not scsi commands.  They are intended for modal operations.
 */

#define SCSI_OP_TARGET	0x0001
#define	SCSI_OP_RESET	0x0002
#define	SCSI_OP_BDINFO	0x0003

/*
 * Error values an adapter driver may return
 */
#define XS_NOERROR	0	/* there is no error, (sense is invalid)  */
#define XS_SENSE	1	/* Check the returned sense for the error */
#define	XS_DRIVER_STUFFUP 2	/* Driver failed to perform operation	  */
#define XS_SELTIMEOUT	3	/* The device timed out.. turned off?	  */
#define XS_TIMEOUT	4	/* The Timeout reported was caught by SW  */
#define XS_BUSY		5	/* The device busy, try again later?	  */
#define XS_SHORTSENSE   6	/* Check the ATAPI sense for the error */
#define XS_RESET	8	/* bus was reset; possible retry command  */

/*
 * Possible retries for scsi_test_unit_ready()
 */
#define TEST_READY_RETRIES	5

/*
 * Possible retries for most SCSI commands.
 */
#define SCSI_RETRIES		4

const void *scsi_inqmatch(struct scsi_inquiry_data *, const void *, int,
	    int, int *);

void	scsi_init(void);
int	scsi_test_unit_ready(struct scsi_link *, int, int);
int	scsi_inquire(struct scsi_link *, struct scsi_inquiry_data *, int);
int	scsi_read_cap_10(struct scsi_link *, struct scsi_read_cap_data *, int);
int	scsi_read_cap_16(struct scsi_link *, struct scsi_read_cap_data_16 *,
	    int);
int	scsi_inquire_vpd(struct scsi_link *, void *, u_int, u_int8_t, int);
void	scsi_init_inquiry(struct scsi_xfer *, u_int8_t, u_int8_t,
	    void *, size_t);
int	scsi_prevent(struct scsi_link *, int, int);
int	scsi_start(struct scsi_link *, int, int);
void	scsi_parse_blkdesc(struct scsi_link *, union scsi_mode_sense_buf *, int,
	    u_int32_t *, u_int64_t *, u_int32_t *);
int	scsi_do_mode_sense(struct scsi_link *, int,
	    union scsi_mode_sense_buf *, void **, int, int, int *);
void	scsi_parse_blkdesc(struct scsi_link *, union scsi_mode_sense_buf *, int,
	    u_int32_t *, u_int64_t *, u_int32_t *);
int	scsi_mode_select(struct scsi_link *, int, struct scsi_mode_header *,
	    int, int);
int	scsi_mode_select_big(struct scsi_link *, int,
	    struct scsi_mode_header_big *, int, int);
void	scsi_copy_internal_data(struct scsi_xfer *, void *, size_t);
void	scsi_done(struct scsi_xfer *);
int	scsi_do_ioctl(struct scsi_link *, u_long, caddr_t, int);
void	sc_print_addr(struct scsi_link *);
int	scsi_report_luns(struct scsi_link *, int,
	    struct scsi_report_luns_data *, u_int32_t, int, int);
int	scsi_interpret_sense(struct scsi_xfer *);

void	scsi_print_sense(struct scsi_xfer *);
void	scsi_strvis(u_char *, u_char *, int);
int	scsi_delay(struct scsi_xfer *, int);

int	scsi_probe(struct scsibus_softc *, int, int);
int	scsi_probe_bus(struct scsibus_softc *);
int	scsi_probe_target(struct scsibus_softc *, int);
int	scsi_probe_lun(struct scsibus_softc *, int, int);

int	scsi_detach(struct scsibus_softc *, int, int, int);
int	scsi_detach_target(struct scsibus_softc *, int, int);
int	scsi_detach_lun(struct scsibus_softc *, int, int, int);

int	scsi_req_probe(struct scsibus_softc *, int, int);
int	scsi_req_detach(struct scsibus_softc *, int, int, int);

int	scsi_activate(struct scsibus_softc *, int, int, int);

struct scsi_link *	scsi_get_link(struct scsibus_softc *, int, int);

#define SID_ANSII_REV(x)	((x)->version & SID_ANSII)
#define SID_RESPONSE_FORMAT(x)	((x)->response_format & SID_RESPONSE_DATA_FMT)

#define SCSI_REV_0	0x00	/* No conformance to any standard. */
#define SCSI_REV_1	0x01	/* (Obsolete) SCSI-1 in olden times. */
#define SCSI_REV_2	0x02	/* (Obsolete) SCSI-2 in olden times. */
#define SCSI_REV_SPC	0x03	/* ANSI INCITS 301-1997 (SPC).	*/
#define SCSI_REV_SPC2	0x04	/* ANSI INCITS 351-2001 (SPC-2)	*/
#define SCSI_REV_SPC3	0x05	/* ANSI INCITS 408-2005 (SPC-3)	*/
#define SCSI_REV_SPC4	0x06	/* ANSI INCITS 513-2015 (SPC-4)	*/
#define SCSI_REV_SPC5	0x07	/* T10/BSR INCITS 503   (SPC-5)	*/

struct scsi_xfer *	scsi_xs_get(struct scsi_link *, int);
void			scsi_xs_exec(struct scsi_xfer *);
int			scsi_xs_sync(struct scsi_xfer *);
void			scsi_xs_put(struct scsi_xfer *);

/*
 * iopool stuff
 */
void	scsi_iopool_init(struct scsi_iopool *, void *,
	    void *(*)(void *), void (*)(void *, void *));
void	scsi_iopool_run(struct scsi_iopool *);
void	scsi_iopool_destroy(struct scsi_iopool *);
void	scsi_link_shutdown(struct scsi_link *);

void *	scsi_io_get(struct scsi_iopool *, int);
void	scsi_io_put(struct scsi_iopool *, void *);

/*
 * default io allocator.
 */
#define SCSI_IOPOOL_POISON ((void *)0x5c5)
void *	scsi_default_get(void *);
void	scsi_default_put(void *, void *);

/*
 * io handler interface
 */
void	scsi_ioh_set(struct scsi_iohandler *, struct scsi_iopool *,
	    void (*)(void *, void *), void *);
int	scsi_ioh_add(struct scsi_iohandler *);
int	scsi_ioh_del(struct scsi_iohandler *);

void	scsi_xsh_set(struct scsi_xshandler *, struct scsi_link *,
	    void (*)(struct scsi_xfer *));
int	scsi_xsh_add(struct scsi_xshandler *);
int	scsi_xsh_del(struct scsi_xshandler *);

/*
 * utility functions
 */
int	scsi_pending_start(struct mutex *, u_int *);
int	scsi_pending_finish(struct mutex *, u_int *);

/*
 * Utility functions for SCSI HBA emulation.
 */
void	scsi_cmd_rw_decode(struct scsi_generic *, u_int64_t *, u_int32_t *);

#endif /* _KERNEL */
#endif /* _SCSI_SCSICONF_H */
