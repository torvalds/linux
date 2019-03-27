/* $FreeBSD$ */
/*-
 * Generic defines for LSI '909 FC  adapters.
 * FreeBSD Version.
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-3-Clause
 *
 * Copyright (c)  2000, 2001 by Greg Ansley
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
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
/*-
 * Copyright (c) 2002, 2006 by Matthew Jacob
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon including
 *    a substantially similar Disclaimer requirement for further binary
 *    redistribution.
 * 3. Neither the names of the above listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF THE COPYRIGHT
 * OWNER OR CONTRIBUTOR IS ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Support from Chris Ellsworth in order to make SAS adapters work
 * is gratefully acknowledged.
 *
 *
 * Support from LSI-Logic has also gone a great deal toward making this a
 * workable subsystem and is gratefully acknowledged.
 */
/*
 * Copyright (c) 2004, Avid Technology, Inc. and its contributors.
 * Copyright (c) 2004, 2005 Justin T. Gibbs
 * Copyright (c) 2005, WHEEL Sp. z o.o.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon including
 *    a substantially similar Disclaimer requirement for further binary
 *    redistribution.
 * 3. Neither the names of the above listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF THE COPYRIGHT
 * OWNER OR CONTRIBUTOR IS ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MPT_H_
#define _MPT_H_

/********************************* OS Includes ********************************/
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/endian.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/types.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/resource.h>

#ifdef __sparc64__
#include <dev/ofw/openfirm.h>
#include <machine/ofw_machdep.h>
#endif

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "opt_ddb.h"

/**************************** Register Definitions ****************************/
#include <dev/mpt/mpt_reg.h>

/******************************* MPI Definitions ******************************/
#include <dev/mpt/mpilib/mpi_type.h>
#include <dev/mpt/mpilib/mpi.h>
#include <dev/mpt/mpilib/mpi_cnfg.h>
#include <dev/mpt/mpilib/mpi_ioc.h>
#include <dev/mpt/mpilib/mpi_raid.h>

/* XXX For mpt_debug.c */
#include <dev/mpt/mpilib/mpi_init.h>

#define	MPT_S64_2_SCALAR(y)	((((int64_t)y.High) << 32) | (y.Low))
#define	MPT_U64_2_SCALAR(y)	((((uint64_t)y.High) << 32) | (y.Low))

/****************************** Misc Definitions ******************************/
/* #define MPT_TEST_MULTIPATH	1 */
#define MPT_OK (0)
#define MPT_FAIL (0x10000)

#define NUM_ELEMENTS(array) (sizeof(array) / sizeof(*array))

#define	MPT_ROLE_NONE		0
#define	MPT_ROLE_INITIATOR	1
#define	MPT_ROLE_TARGET		2
#define	MPT_ROLE_BOTH		3
#define	MPT_ROLE_DEFAULT	MPT_ROLE_INITIATOR

#define	MPT_INI_ID_NONE		-1

/**************************** Forward Declarations ****************************/
struct mpt_softc;
struct mpt_personality;
typedef struct req_entry request_t;

/************************* Personality Module Support *************************/
typedef int mpt_load_handler_t(struct mpt_personality *);
typedef int mpt_probe_handler_t(struct mpt_softc *);
typedef int mpt_attach_handler_t(struct mpt_softc *);
typedef int mpt_enable_handler_t(struct mpt_softc *);
typedef void mpt_ready_handler_t(struct mpt_softc *);
typedef int mpt_event_handler_t(struct mpt_softc *, request_t *,
				MSG_EVENT_NOTIFY_REPLY *);
typedef void mpt_reset_handler_t(struct mpt_softc *, int /*type*/);
/* XXX Add return value and use for veto? */
typedef void mpt_shutdown_handler_t(struct mpt_softc *);
typedef void mpt_detach_handler_t(struct mpt_softc *);
typedef int mpt_unload_handler_t(struct mpt_personality *);

struct mpt_personality
{
	const char		*name;
	uint32_t		 id;		/* Assigned identifier. */
	u_int			 use_count;	/* Instances using personality*/
	mpt_load_handler_t	*load;		/* configure personailty */
#define MPT_PERS_FIRST_HANDLER(pers) (&(pers)->load)
	mpt_probe_handler_t	*probe;		/* configure personailty */
	mpt_attach_handler_t	*attach;	/* initialize device instance */
	mpt_enable_handler_t	*enable;	/* enable device */
	mpt_ready_handler_t	*ready;		/* final open for business */
	mpt_event_handler_t	*event;		/* Handle MPI event. */
	mpt_reset_handler_t	*reset;		/* Re-init after reset. */
	mpt_shutdown_handler_t	*shutdown;	/* Shutdown instance. */
	mpt_detach_handler_t	*detach;	/* release device instance */
	mpt_unload_handler_t	*unload;	/* Shutdown personality */
#define MPT_PERS_LAST_HANDLER(pers) (&(pers)->unload)
};

int mpt_modevent(module_t, int, void *);

/* Maximum supported number of personalities. */
#define MPT_MAX_PERSONALITIES	(15)

#define MPT_PERSONALITY_DEPEND(name, dep, vmin, vpref, vmax) \
	MODULE_DEPEND(name, dep, vmin, vpref, vmax)

#define DECLARE_MPT_PERSONALITY(name, order)				  \
	static moduledata_t name##_mod = {				  \
		#name, mpt_modevent, &name##_personality		  \
	};								  \
	DECLARE_MODULE(name, name##_mod, SI_SUB_DRIVERS, order);	  \
	MODULE_VERSION(name, 1);					  \
	MPT_PERSONALITY_DEPEND(name, mpt_core, 1, 1, 1)

/******************************* Bus DMA Support ******************************/
/* XXX Need to update bus_dmamap_sync to take a range argument. */
#define bus_dmamap_sync_range(dma_tag, dmamap, offset, len, op)	\
	bus_dmamap_sync(dma_tag, dmamap, op)

#define mpt_dma_tag_create(mpt, parent_tag, alignment, boundary,	\
			   lowaddr, highaddr, filter, filterarg,	\
			   maxsize, nsegments, maxsegsz, flags,		\
			   dma_tagp)					\
	bus_dma_tag_create(parent_tag, alignment, boundary,		\
			   lowaddr, highaddr, filter, filterarg,	\
			   maxsize, nsegments, maxsegsz, flags,		\
			   busdma_lock_mutex, &(mpt)->mpt_lock,		\
			   dma_tagp)
struct mpt_map_info {
	struct mpt_softc *mpt;
	int		  error;
	uint32_t	  phys;
};

void mpt_map_rquest(void *, bus_dma_segment_t *, int, int);

/********************************* Endianness *********************************/
#define	MPT_2_HOST64(ptr, tag)	ptr->tag = le64toh(ptr->tag)
#define	MPT_2_HOST32(ptr, tag)	ptr->tag = le32toh(ptr->tag)
#define	MPT_2_HOST16(ptr, tag)	ptr->tag = le16toh(ptr->tag)

#define	HOST_2_MPT64(ptr, tag)	ptr->tag = htole64(ptr->tag)
#define	HOST_2_MPT32(ptr, tag)	ptr->tag = htole32(ptr->tag)
#define	HOST_2_MPT16(ptr, tag)	ptr->tag = htole16(ptr->tag)

#if	_BYTE_ORDER == _BIG_ENDIAN
void mpt2host_sge_simple_union(SGE_SIMPLE_UNION *);
void mpt2host_iocfacts_reply(MSG_IOC_FACTS_REPLY *);
void mpt2host_portfacts_reply(MSG_PORT_FACTS_REPLY *);
void mpt2host_config_page_ioc2(CONFIG_PAGE_IOC_2 *);
void mpt2host_config_page_ioc3(CONFIG_PAGE_IOC_3 *);
void mpt2host_config_page_scsi_port_0(CONFIG_PAGE_SCSI_PORT_0 *);
void mpt2host_config_page_scsi_port_1(CONFIG_PAGE_SCSI_PORT_1 *);
void host2mpt_config_page_scsi_port_1(CONFIG_PAGE_SCSI_PORT_1 *);
void mpt2host_config_page_scsi_port_2(CONFIG_PAGE_SCSI_PORT_2 *);
void mpt2host_config_page_scsi_device_0(CONFIG_PAGE_SCSI_DEVICE_0 *);
void mpt2host_config_page_scsi_device_1(CONFIG_PAGE_SCSI_DEVICE_1 *);
void host2mpt_config_page_scsi_device_1(CONFIG_PAGE_SCSI_DEVICE_1 *);
void mpt2host_config_page_fc_port_0(CONFIG_PAGE_FC_PORT_0 *);
void mpt2host_config_page_fc_port_1(CONFIG_PAGE_FC_PORT_1 *);
void host2mpt_config_page_fc_port_1(CONFIG_PAGE_FC_PORT_1 *);
void mpt2host_config_page_raid_vol_0(CONFIG_PAGE_RAID_VOL_0 *);
void mpt2host_config_page_raid_phys_disk_0(CONFIG_PAGE_RAID_PHYS_DISK_0 *);
void mpt2host_mpi_raid_vol_indicator(MPI_RAID_VOL_INDICATOR *);
#else
#define	mpt2host_sge_simple_union(x)		do { ; } while (0)
#define	mpt2host_iocfacts_reply(x)		do { ; } while (0)
#define	mpt2host_portfacts_reply(x)		do { ; } while (0)
#define	mpt2host_config_page_ioc2(x)		do { ; } while (0)
#define	mpt2host_config_page_ioc3(x)		do { ; } while (0)
#define	mpt2host_config_page_scsi_port_0(x)	do { ; } while (0)
#define	mpt2host_config_page_scsi_port_1(x)	do { ; } while (0)
#define	host2mpt_config_page_scsi_port_1(x)	do { ; } while (0)
#define	mpt2host_config_page_scsi_port_2(x)	do { ; } while (0)
#define	mpt2host_config_page_scsi_device_0(x)	do { ; } while (0)
#define	mpt2host_config_page_scsi_device_1(x)	do { ; } while (0)
#define	host2mpt_config_page_scsi_device_1(x)	do { ; } while (0)
#define	mpt2host_config_page_fc_port_0(x)	do { ; } while (0)
#define	mpt2host_config_page_fc_port_1(x)	do { ; } while (0)
#define	host2mpt_config_page_fc_port_1(x)	do { ; } while (0)
#define	mpt2host_config_page_raid_vol_0(x)	do { ; } while (0)
#define	mpt2host_config_page_raid_phys_disk_0(x)			\
	do { ; } while (0)
#define	mpt2host_mpi_raid_vol_indicator(x)	do { ; } while (0)
#endif

/**************************** MPI Transaction State ***************************/
typedef enum {
	REQ_STATE_NIL		= 0x00,
	REQ_STATE_FREE		= 0x01,
	REQ_STATE_ALLOCATED	= 0x02,
	REQ_STATE_QUEUED	= 0x04,
	REQ_STATE_DONE		= 0x08,
	REQ_STATE_TIMEDOUT	= 0x10,
	REQ_STATE_NEED_WAKEUP	= 0x20,
	REQ_STATE_LOCKED	= 0x80,	/* can't be freed */
	REQ_STATE_MASK		= 0xFF
} mpt_req_state_t; 

struct req_entry {
	TAILQ_ENTRY(req_entry) links;	/* Pointer to next in list */
	mpt_req_state_t	state;		/* Request State Information */
	uint16_t	index;		/* Index of this entry */
	uint16_t	IOCStatus;	/* Completion status */
	uint16_t	ResponseCode;	/* TMF Response Code */
	uint16_t	serno;		/* serial number */
	union ccb      *ccb;		/* CAM request */
	void	       *req_vbuf;	/* Virtual Address of Entry */
	void	       *sense_vbuf;	/* Virtual Address of sense data */
	bus_addr_t	req_pbuf;	/* Physical Address of Entry */
	bus_addr_t	sense_pbuf;	/* Physical Address of sense data */
	bus_dmamap_t	dmap;		/* DMA map for data buffers */
	struct req_entry *chain;	/* for SGE overallocations */
	struct callout  callout;	/* Timeout for the request */
};

typedef struct mpt_config_params {
	u_int		Action;
	u_int		PageVersion;
	u_int		PageLength;
	u_int		PageNumber;
	u_int		PageType;
	u_int		PageAddress;
	u_int		ExtPageLength;
	u_int		ExtPageType;
} cfgparms_t;

/**************************** MPI Target State Info ***************************/
typedef struct {
	uint32_t reply_desc;	/* current reply descriptor */
	uint32_t bytes_xfered;	/* current relative offset */
	int resid;		/* current data residual */
	union ccb *ccb;		/* pointer to currently active ccb */
	request_t *req;		/* pointer to currently active assist request */
	uint32_t
		is_local : 1,
		nxfers	 : 31;
	uint32_t tag_id;	/* Our local tag. */
	uint16_t itag;		/* Initiator tag. */
	enum {
		TGT_STATE_NIL,
		TGT_STATE_LOADING,
		TGT_STATE_LOADED,
		TGT_STATE_IN_CAM,
                TGT_STATE_SETTING_UP_FOR_DATA,
                TGT_STATE_MOVING_DATA,
                TGT_STATE_MOVING_DATA_AND_STATUS,
                TGT_STATE_SENDING_STATUS
	} state;
} mpt_tgt_state_t;

/*
 * When we get an incoming command it has its own tag which is called the
 * IoIndex. This is the value we gave that particular command buffer when
 * we originally assigned it. It's just a number, really. The FC card uses
 * it as an RX_ID. We can use it to index into mpt->tgt_cmd_ptrs, which
 * contains pointers the request_t structures related to that IoIndex.
 *
 * What *we* do is construct a tag out of the index for the target command
 * which owns the incoming ATIO plus a rolling sequence number.
 */
#define	MPT_MAKE_TAGID(mpt, req, ioindex)	\
 ((ioindex << 18) | (((mpt->sequence++) & 0x3f) << 12) | (req->index & 0xfff))

#ifdef	INVARIANTS
#define	MPT_TAG_2_REQ(a, b)		mpt_tag_2_req(a, (uint32_t) b)
#else
#define	MPT_TAG_2_REQ(mpt, tag)		mpt->tgt_cmd_ptrs[tag >> 18]
#endif

#define	MPT_TGT_STATE(mpt, req) ((mpt_tgt_state_t *) \
    (&((uint8_t *)req->req_vbuf)[MPT_RQSL(mpt) - sizeof (mpt_tgt_state_t)]))

STAILQ_HEAD(mpt_hdr_stailq, ccb_hdr);
#define	MPT_MAX_LUNS	256
typedef struct {
	struct mpt_hdr_stailq	atios;
	struct mpt_hdr_stailq	inots;
	int enabled;
} tgt_resource_t;
#define	MPT_MAX_ELS	64

/**************************** Handler Registration ****************************/
/*
 * Global table of registered reply handlers.  The
 * handler is indicated by byte 3 of the request
 * index submitted to the IOC.  This allows the
 * driver core to perform generic processing without
 * any knowledge of per-personality behavior.
 *
 * MPT_NUM_REPLY_HANDLERS must be a power of 2
 * to allow the easy generation of a mask.
 *
 * The handler offsets used by the core are hard coded
 * allowing faster code generation when assigning a handler
 * to a request.  All "personalities" must use the
 * the handler registration mechanism.
 *
 * The IOC handlers that are rarely executed are placed
 * at the tail of the table to make it more likely that
 * all commonly executed handlers fit in a single cache
 * line.
 */
#define MPT_NUM_REPLY_HANDLERS		(32)
#define MPT_REPLY_HANDLER_EVENTS	MPT_CBI_TO_HID(0)
#define MPT_REPLY_HANDLER_CONFIG	MPT_CBI_TO_HID(MPT_NUM_REPLY_HANDLERS-1)
#define MPT_REPLY_HANDLER_HANDSHAKE	MPT_CBI_TO_HID(MPT_NUM_REPLY_HANDLERS-2)
typedef int mpt_reply_handler_t(struct mpt_softc *mpt, request_t *request,
    uint32_t reply_desc, MSG_DEFAULT_REPLY *reply_frame);
typedef union {
	mpt_reply_handler_t	*reply_handler;
} mpt_handler_t;

typedef enum {
	MPT_HANDLER_REPLY,
	MPT_HANDLER_EVENT,
	MPT_HANDLER_RESET,
	MPT_HANDLER_SHUTDOWN
} mpt_handler_type;

struct mpt_handler_record
{
	LIST_ENTRY(mpt_handler_record)	links;
	mpt_handler_t			handler;
};

LIST_HEAD(mpt_handler_list, mpt_handler_record);

/*
 * The handler_id is currently unused but would contain the
 * handler ID used in the MsgContext field to allow direction
 * of replies to the handler.  Registrations that don't require
 * a handler id can pass in NULL for the handler_id.
 *
 * Deregistrations for handlers without a handler id should
 * pass in MPT_HANDLER_ID_NONE.
 */
#define MPT_HANDLER_ID_NONE		(0xFFFFFFFF)
int mpt_register_handler(struct mpt_softc *, mpt_handler_type,
			 mpt_handler_t, uint32_t *);
int mpt_deregister_handler(struct mpt_softc *, mpt_handler_type,
			   mpt_handler_t, uint32_t);

/******************* Per-Controller Instance Data Structures ******************/
TAILQ_HEAD(req_queue, req_entry);

/* Structure for saving proper values for modifyable PCI config registers */
struct mpt_pci_cfg {
	uint16_t Command;
	uint16_t LatencyTimer_LineSize;
	uint32_t IO_BAR;
	uint32_t Mem0_BAR[2];
	uint32_t Mem1_BAR[2];
	uint32_t ROM_BAR;
	uint8_t  IntLine;
	uint32_t PMCSR;
};

typedef enum {
	MPT_RVF_NONE		= 0x0,
	MPT_RVF_ACTIVE		= 0x1,
	MPT_RVF_ANNOUNCED	= 0x2,
	MPT_RVF_UP2DATE		= 0x4,
	MPT_RVF_REFERENCED	= 0x8,
	MPT_RVF_WCE_CHANGED	= 0x10
} mpt_raid_volume_flags;

struct mpt_raid_volume {
	CONFIG_PAGE_RAID_VOL_0	       *config_page;
	MPI_RAID_VOL_INDICATOR		sync_progress;
	mpt_raid_volume_flags		flags;
	u_int				quiesced_disks;
};

typedef enum {
	MPT_RDF_NONE		= 0x00,
	MPT_RDF_ACTIVE		= 0x01,
	MPT_RDF_ANNOUNCED	= 0x02,
	MPT_RDF_UP2DATE		= 0x04,
	MPT_RDF_REFERENCED	= 0x08,
	MPT_RDF_QUIESCING	= 0x10,
	MPT_RDF_QUIESCED	= 0x20
} mpt_raid_disk_flags;

struct mpt_raid_disk {
	CONFIG_PAGE_RAID_PHYS_DISK_0	config_page;
	struct mpt_raid_volume	       *volume;
	u_int				member_number;
	u_int				pass_thru_active;
	mpt_raid_disk_flags		flags;
};

struct mpt_evtf_record {
	MSG_EVENT_NOTIFY_REPLY		reply;
	uint32_t			context;
	LIST_ENTRY(mpt_evtf_record)	links;
};

LIST_HEAD(mpt_evtf_list, mpt_evtf_record);

struct mptsas_devinfo {
	uint16_t	dev_handle;
	uint16_t	parent_dev_handle;
	uint16_t	enclosure_handle;
	uint16_t	slot;
	uint8_t		phy_num;
	uint8_t		physical_port;
	uint8_t		target_id;
	uint8_t		bus;
	uint64_t	sas_address;
	uint32_t	device_info;
};

struct mptsas_phyinfo {
	uint16_t	handle;
	uint8_t		phy_num;
	uint8_t		port_id;
	uint8_t		negotiated_link_rate;
	uint8_t		hw_link_rate;
	uint8_t		programmed_link_rate;
	uint8_t		sas_port_add_phy;
	struct mptsas_devinfo identify;
	struct mptsas_devinfo attached;
};

struct mptsas_portinfo {
	uint16_t			num_phys;
	struct mptsas_phyinfo		*phy_info;
};

struct mpt_softc {
	device_t		dev;
	struct mtx		mpt_lock;
	int			mpt_locksetup;
	uint32_t		mpt_pers_mask;
	uint32_t
				: 7,
		unit		: 8,
		ready		: 1,
		fw_uploaded	: 1,
		msi_enable	: 1,
		twildcard	: 1,
		tenabled	: 1,
		do_cfg_role	: 1,
		raid_enabled	: 1,
		raid_mwce_set	: 1,
		getreqwaiter	: 1,
		shutdwn_raid    : 1,
		shutdwn_recovery: 1,
		outofbeer	: 1,
		disabled	: 1,
		is_spi		: 1,
		is_sas		: 1,
		is_fc		: 1,
		is_1078		: 1;

	u_int			cfg_role;
	u_int			role;	/* role: none, ini, target, both */

	u_int			verbose;
#ifdef	MPT_TEST_MULTIPATH
	int			failure_id;
#endif

	/*
	 * IOC Facts
	 */
	MSG_IOC_FACTS_REPLY	ioc_facts;

	/*
	 * Port Facts
	 */
	MSG_PORT_FACTS_REPLY *	port_facts;
#define	mpt_max_tgtcmds	port_facts[0].MaxPostedCmdBuffers

	/*
	 * Device Configuration Information
	 */
	union {
		struct mpt_spi_cfg {
			CONFIG_PAGE_SCSI_PORT_0		_port_page0;
			CONFIG_PAGE_SCSI_PORT_1		_port_page1;
			CONFIG_PAGE_SCSI_PORT_2		_port_page2;
			CONFIG_PAGE_SCSI_DEVICE_0	_dev_page0[16];
			CONFIG_PAGE_SCSI_DEVICE_1	_dev_page1[16];
			int				_ini_id;
			uint16_t			_tag_enable;
			uint16_t			_disc_enable;
		} spi;
#define	mpt_port_page0		cfg.spi._port_page0
#define	mpt_port_page1		cfg.spi._port_page1
#define	mpt_port_page2		cfg.spi._port_page2
#define	mpt_dev_page0		cfg.spi._dev_page0
#define	mpt_dev_page1		cfg.spi._dev_page1
#define	mpt_ini_id		cfg.spi._ini_id
#define	mpt_tag_enable		cfg.spi._tag_enable
#define	mpt_disc_enable		cfg.spi._disc_enable
		struct mpi_fc_cfg {
			CONFIG_PAGE_FC_PORT_0 _port_page0;
			uint32_t _port_speed;
#define	mpt_fcport_page0	cfg.fc._port_page0
#define	mpt_fcport_speed	cfg.fc._port_speed
		} fc;
	} cfg;
	/*
	 * Device config information stored up for sysctl to access
	 */
	union {
		struct {
			unsigned int initiator_id;
		} spi;
		struct {
			uint64_t wwnn;
			uint64_t wwpn;
			uint32_t portid;
		} fc;
	} scinfo;

	/* Controller Info for RAID information */
	CONFIG_PAGE_IOC_2 *	ioc_page2;
	CONFIG_PAGE_IOC_3 *	ioc_page3;

	/* Raid Data */
	struct mpt_raid_volume* raid_volumes;
	struct mpt_raid_disk*	raid_disks;
	u_int			raid_max_volumes;
	u_int			raid_max_disks;
	u_int			raid_page0_len;
	u_int			raid_wakeup;
	u_int			raid_rescan;
	u_int			raid_resync_rate;
	u_int			raid_mwce_setting;
	u_int			raid_queue_depth;
	u_int			raid_nonopt_volumes;
	struct proc	       *raid_thread;
	struct callout		raid_timer;

	/*
	 * PCI Hardware info
	 */
	struct resource *	pci_irq;	/* Interrupt map for chip */
	void *			ih;		/* Interrupt handle */
#if 0
	struct mpt_pci_cfg	pci_cfg;	/* saved PCI conf registers */
#endif

	/*
	 * DMA Mapping Stuff
	 */
	struct resource *	pci_reg;	/* Register map for chip */
	bus_space_tag_t		pci_st;		/* Bus tag for registers */
	bus_space_handle_t	pci_sh;		/* Bus handle for registers */
	/* PIO versions of above. */
	struct resource *	pci_pio_reg;
	bus_space_tag_t		pci_pio_st;
	bus_space_handle_t	pci_pio_sh;

	bus_dma_tag_t		parent_dmat;	/* DMA tag for parent PCI bus */
	bus_dma_tag_t		reply_dmat;	/* DMA tag for reply memory */
	bus_dmamap_t		reply_dmap;	/* DMA map for reply memory */
	uint8_t		       *reply;		/* KVA of reply memory */
	bus_addr_t		reply_phys;	/* BusAddr of reply memory */

	bus_dma_tag_t		buffer_dmat;	/* DMA tag for buffers */
	bus_dma_tag_t		request_dmat;	/* DMA tag for request memory */
	bus_dmamap_t		request_dmap;	/* DMA map for request memory */
	uint8_t		       *request;	/* KVA of Request memory */
	bus_addr_t		request_phys;	/* BusAddr of request memory */

	uint32_t		max_seg_cnt;	/* calculated after IOC facts */
	uint32_t		max_cam_seg_cnt;/* calculated from MAXPHYS*/

	/*
	 * Hardware management
	 */
	u_int			reset_cnt;

	/*
	 * CAM && Software Management
	 */
	request_t	       *request_pool;
	struct req_queue	request_free_list;
	struct req_queue	request_pending_list;
	struct req_queue	request_timeout_list;


	struct cam_sim	       *sim;
	struct cam_path	       *path;

	struct cam_sim	       *phydisk_sim;
	struct cam_path	       *phydisk_path;

	struct proc	       *recovery_thread;
	request_t	       *tmf_req;

	/*
	 * Deferred frame acks due to resource shortage.
	 */
	struct mpt_evtf_list	ack_frames;

	/*
	 * Target Mode Support
	 */
	uint32_t		scsi_tgt_handler_id;
	request_t **		tgt_cmd_ptrs;
	request_t **		els_cmd_ptrs;	/* FC only */

	/*
	 * *snork*- this is chosen to be here *just in case* somebody
	 * forgets to point to it exactly and we index off of trt with
	 * CAM_LUN_WILDCARD.
	 */
	tgt_resource_t		trt_wildcard;		/* wildcard luns */
	tgt_resource_t		trt[MPT_MAX_LUNS];
	uint16_t		tgt_cmds_allocated;
	uint16_t		els_cmds_allocated;	/* FC only */

	uint16_t		timeouts;	/* timeout count */
	uint16_t		success;	/* successes afer timeout */
	uint16_t		sequence;	/* Sequence Number */
	uint16_t		pad3;

#if 0
	/* Paired port in some dual adapters configurations */
	struct mpt_softc *	mpt2;
#endif

	/* FW Image management */
	uint32_t		fw_image_size;
	uint8_t		       *fw_image;
	bus_dma_tag_t		fw_dmat;	/* DMA tag for firmware image */
	bus_dmamap_t		fw_dmap;	/* DMA map for firmware image */
	bus_addr_t		fw_phys;	/* BusAddr of firmware image */

	/* SAS Topology */
	struct mptsas_portinfo	*sas_portinfo;

	/* Shutdown Event Handler. */
	eventhandler_tag         eh;

	/* Userland management interface. */
	struct cdev		*cdev;

	TAILQ_ENTRY(mpt_softc)	links;
};

static __inline void mpt_assign_serno(struct mpt_softc *, request_t *);

static __inline void
mpt_assign_serno(struct mpt_softc *mpt, request_t *req)
{
	if ((req->serno = mpt->sequence++) == 0) {
		req->serno = mpt->sequence++;
	}
}

/***************************** Locking Primitives *****************************/
#define	MPT_IFLAGS		INTR_TYPE_CAM | INTR_ENTROPY | INTR_MPSAFE
#define	MPT_LOCK_SETUP(mpt)						\
		mtx_init(&mpt->mpt_lock, "mpt", NULL, MTX_DEF);		\
		mpt->mpt_locksetup = 1
#define	MPT_LOCK_DESTROY(mpt)						\
	if (mpt->mpt_locksetup) {					\
		mtx_destroy(&mpt->mpt_lock);				\
		mpt->mpt_locksetup = 0;					\
	}

#define	MPT_LOCK(mpt)		mtx_lock(&(mpt)->mpt_lock)
#define	MPT_UNLOCK(mpt)		mtx_unlock(&(mpt)->mpt_lock)
#define	MPT_OWNED(mpt)		mtx_owned(&(mpt)->mpt_lock)
#define	MPT_LOCK_ASSERT(mpt)	mtx_assert(&(mpt)->mpt_lock, MA_OWNED)
#define mpt_sleep(mpt, ident, priority, wmesg, sbt) \
    msleep_sbt(ident, &(mpt)->mpt_lock, priority, wmesg, sbt, 0, 0)
#define mpt_req_timeout(req, sbt, func, arg) \
    callout_reset_sbt(&(req)->callout, (sbt), 0, (func), (arg), 0)
#define mpt_req_untimeout(req, func, arg) \
	callout_stop(&(req)->callout)
#define mpt_callout_init(mpt, c) \
	callout_init_mtx(c, &(mpt)->mpt_lock, 0)
#define mpt_callout_drain(mpt, c) \
	callout_drain(c)

/******************************* Register Access ******************************/
static __inline void mpt_write(struct mpt_softc *, size_t, uint32_t);
static __inline void mpt_write_stream(struct mpt_softc *, size_t, uint32_t);
static __inline uint32_t mpt_read(struct mpt_softc *, int);
static __inline void mpt_pio_write(struct mpt_softc *, size_t, uint32_t);
static __inline uint32_t mpt_pio_read(struct mpt_softc *, int);

static __inline void
mpt_write(struct mpt_softc *mpt, size_t offset, uint32_t val)
{
	bus_space_write_4(mpt->pci_st, mpt->pci_sh, offset, val);
}

static __inline void
mpt_write_stream(struct mpt_softc *mpt, size_t offset, uint32_t val)
{
	bus_space_write_stream_4(mpt->pci_st, mpt->pci_sh, offset, val);
}

static __inline uint32_t
mpt_read(struct mpt_softc *mpt, int offset)
{
	return (bus_space_read_4(mpt->pci_st, mpt->pci_sh, offset));
}

/*
 * Some operations (e.g. diagnostic register writes while the ARM proccessor
 * is disabled), must be performed using "PCI pio" operations.  On non-PCI
 * buses, these operations likely map to normal register accesses.
 */
static __inline void
mpt_pio_write(struct mpt_softc *mpt, size_t offset, uint32_t val)
{
	KASSERT(mpt->pci_pio_reg != NULL, ("no PIO resource"));
	bus_space_write_4(mpt->pci_pio_st, mpt->pci_pio_sh, offset, val);
}

static __inline uint32_t
mpt_pio_read(struct mpt_softc *mpt, int offset)
{
	KASSERT(mpt->pci_pio_reg != NULL, ("no PIO resource"));
	return (bus_space_read_4(mpt->pci_pio_st, mpt->pci_pio_sh, offset));
}

/*********************** Reply Frame/Request Management ***********************/
/* Max MPT Reply we are willing to accept (must be power of 2) */
#define MPT_REPLY_SIZE   	256

/*
 * Must be less than 16384 in order for target mode to work
 */
#define MPT_MAX_REQUESTS(mpt)	512
#define MPT_REQUEST_AREA	512
#define MPT_SENSE_SIZE		32	/* included in MPT_REQUEST_AREA */
#define MPT_REQ_MEM_SIZE(mpt)	(MPT_MAX_REQUESTS(mpt) * MPT_REQUEST_AREA)

#define MPT_CONTEXT_CB_SHIFT	(16)
#define MPT_CBI(handle)		(handle >> MPT_CONTEXT_CB_SHIFT)
#define MPT_CBI_TO_HID(cbi)	((cbi) << MPT_CONTEXT_CB_SHIFT)
#define MPT_CONTEXT_TO_CBI(x)	\
    (((x) >> MPT_CONTEXT_CB_SHIFT) & (MPT_NUM_REPLY_HANDLERS - 1))
#define MPT_CONTEXT_REQI_MASK	0xFFFF
#define MPT_CONTEXT_TO_REQI(x)	((x) & MPT_CONTEXT_REQI_MASK)

/*
 * Convert a 32bit physical address returned from IOC to an
 * offset into our reply frame memory or the kvm address needed
 * to access the data.  The returned address is only the low
 * 32 bits, so mask our base physical address accordingly.
 */
#define MPT_REPLY_BADDR(x)		\
	(x << 1)
#define MPT_REPLY_OTOV(m, i) 		\
	((void *)(&m->reply[i]))

#define	MPT_DUMP_REPLY_FRAME(mpt, reply_frame)		\
do {							\
	if (mpt->verbose > MPT_PRT_DEBUG)		\
		mpt_dump_reply_frame(mpt, reply_frame);	\
} while(0)

static __inline uint32_t mpt_pop_reply_queue(struct mpt_softc *mpt);
static __inline void mpt_free_reply(struct mpt_softc *mpt, uint32_t ptr);

/*
 * Give the reply buffer back to the IOC after we have
 * finished processing it.
 */
static __inline void
mpt_free_reply(struct mpt_softc *mpt, uint32_t ptr)
{
     mpt_write(mpt, MPT_OFFSET_REPLY_Q, ptr);
}

/* Get a reply from the IOC */
static __inline uint32_t
mpt_pop_reply_queue(struct mpt_softc *mpt)
{
     return mpt_read(mpt, MPT_OFFSET_REPLY_Q);
}

void
mpt_complete_request_chain(struct mpt_softc *, struct req_queue *, u_int);

/************************** Scatter Gather Management **************************/
/* MPT_RQSL- size of request frame, in bytes */
#define	MPT_RQSL(mpt)		(mpt->ioc_facts.RequestFrameSize << 2)

/* MPT_NSGL- how many SG entries can fit in a request frame size */
#define	MPT_NSGL(mpt)		(MPT_RQSL(mpt) / sizeof (SGE_IO_UNION))

/* MPT_NRFM- how many request frames can fit in each request alloc we make */
#define	MPT_NRFM(mpt)		(MPT_REQUEST_AREA / MPT_RQSL(mpt))

/*
 * MPT_NSGL_FIRST- # of SG elements that can fit after
 * an I/O request but still within the request frame.
 * Do this safely based upon SGE_IO_UNION.
 *
 * Note that the first element is *within* the SCSI request.
 */
#define	MPT_NSGL_FIRST(mpt)	\
    ((MPT_RQSL(mpt) - sizeof (MSG_SCSI_IO_REQUEST) + sizeof (SGE_IO_UNION)) / \
    sizeof (SGE_IO_UNION))

/***************************** IOC Initialization *****************************/
int mpt_reset(struct mpt_softc *, int /*reinit*/);

/****************************** Debugging ************************************/
void mpt_dump_data(struct mpt_softc *, const char *, void *, int);
void mpt_dump_request(struct mpt_softc *, request_t *);

enum {
	MPT_PRT_ALWAYS,
	MPT_PRT_FATAL,
	MPT_PRT_ERROR,
	MPT_PRT_WARN,
	MPT_PRT_INFO,
	MPT_PRT_NEGOTIATION,
	MPT_PRT_DEBUG,
	MPT_PRT_DEBUG1,
	MPT_PRT_DEBUG2,
	MPT_PRT_DEBUG3,
	MPT_PRT_TRACE,
	MPT_PRT_NONE=100
};

#define mpt_lprt(mpt, level, ...)		\
do {						\
	if ((level) <= (mpt)->verbose)		\
		mpt_prt(mpt, __VA_ARGS__);	\
} while (0)

#if 0
#define mpt_lprtc(mpt, level, ...)		\
do {						\
	if ((level) <= (mpt)->verbose)		\
		mpt_prtc(mpt, __VA_ARGS__);	\
} while (0)
#endif

void mpt_prt(struct mpt_softc *, const char *, ...)
	__printflike(2, 3);
void mpt_prtc(struct mpt_softc *, const char *, ...)
	__printflike(2, 3);

/**************************** Target Mode Related ***************************/
#ifdef	INVARIANTS
static __inline request_t * mpt_tag_2_req(struct mpt_softc *, uint32_t);
static __inline request_t *
mpt_tag_2_req(struct mpt_softc *mpt, uint32_t tag)
{
	uint16_t rtg = (tag >> 18);
	KASSERT(rtg < mpt->tgt_cmds_allocated, ("bad tag %d", tag));
	KASSERT(mpt->tgt_cmd_ptrs, ("no cmd backpointer array"));
	KASSERT(mpt->tgt_cmd_ptrs[rtg], ("no cmd backpointer"));
	return (mpt->tgt_cmd_ptrs[rtg]);
}
#endif

static __inline int
mpt_req_on_free_list(struct mpt_softc *, request_t *);
static __inline int
mpt_req_on_pending_list(struct mpt_softc *, request_t *);

/*
 * Is request on freelist?
 */
static __inline int
mpt_req_on_free_list(struct mpt_softc *mpt, request_t *req)
{
	request_t *lrq;

	TAILQ_FOREACH(lrq, &mpt->request_free_list, links) {
		if (lrq == req) {
			return (1);
		}
	}
	return (0);
}

/*
 * Is request on pending list?
 */
static __inline int
mpt_req_on_pending_list(struct mpt_softc *mpt, request_t *req)
{
	request_t *lrq;

	TAILQ_FOREACH(lrq, &mpt->request_pending_list, links) {
		if (lrq == req) {
			return (1);
		}
	}
	return (0);
}

#ifdef	INVARIANTS
static __inline void
mpt_req_spcl(struct mpt_softc *, request_t *, const char *, int);
static __inline void
mpt_req_not_spcl(struct mpt_softc *, request_t *, const char *, int);

/*
 * Make sure that req *is* part of one of the special lists
 */
static __inline void
mpt_req_spcl(struct mpt_softc *mpt, request_t *req, const char *s, int line)
{
	int i;
	for (i = 0; i < mpt->els_cmds_allocated; i++) {
		if (req == mpt->els_cmd_ptrs[i]) {
			return;
		}
	}
	for (i = 0; i < mpt->tgt_cmds_allocated; i++) {
		if (req == mpt->tgt_cmd_ptrs[i]) {
			return;
		}
	}
	panic("%s(%d): req %p:%u function %x not in els or tgt ptrs",
	    s, line, req, req->serno,
	    ((PTR_MSG_REQUEST_HEADER)req->req_vbuf)->Function);
}

/*
 * Make sure that req is *not* part of one of the special lists.
 */
static __inline void
mpt_req_not_spcl(struct mpt_softc *mpt, request_t *req, const char *s, int line)
{
	int i;
	for (i = 0; i < mpt->els_cmds_allocated; i++) {
		KASSERT(req != mpt->els_cmd_ptrs[i],
		    ("%s(%d): req %p:%u func %x in els ptrs at ioindex %d",
		    s, line, req, req->serno,
		    ((PTR_MSG_REQUEST_HEADER)req->req_vbuf)->Function, i));
	}
	for (i = 0; i < mpt->tgt_cmds_allocated; i++) {
		KASSERT(req != mpt->tgt_cmd_ptrs[i],
		    ("%s(%d): req %p:%u func %x in tgt ptrs at ioindex %d",
		    s, line, req, req->serno,
		    ((PTR_MSG_REQUEST_HEADER)req->req_vbuf)->Function, i));
	}
}
#endif

/*
 * Task Management Types, purely for internal consumption
 */
typedef enum {
	MPT_QUERY_TASK_SET=1234,
	MPT_ABORT_TASK_SET,
	MPT_CLEAR_TASK_SET,
	MPT_QUERY_ASYNC_EVENT,
	MPT_LOGICAL_UNIT_RESET,
	MPT_TARGET_RESET,
	MPT_CLEAR_ACA,
	MPT_NIL_TMT_VALUE=5678
} mpt_task_mgmt_t;

/**************************** Unclassified Routines ***************************/
void		mpt_send_cmd(struct mpt_softc *mpt, request_t *req);
int		mpt_recv_handshake_reply(struct mpt_softc *mpt,
					 size_t reply_len, void *reply);
int		mpt_wait_req(struct mpt_softc *mpt, request_t *req,
			     mpt_req_state_t state, mpt_req_state_t mask,
			     int sleep_ok, int time_ms);
void		mpt_enable_ints(struct mpt_softc *mpt);
void		mpt_disable_ints(struct mpt_softc *mpt);
int		mpt_attach(struct mpt_softc *mpt);
int		mpt_shutdown(struct mpt_softc *mpt);
int		mpt_detach(struct mpt_softc *mpt);
int		mpt_send_handshake_cmd(struct mpt_softc *mpt,
				       size_t len, void *cmd);
request_t *	mpt_get_request(struct mpt_softc *mpt, int sleep_ok);
void		mpt_free_request(struct mpt_softc *mpt, request_t *req);
void		mpt_intr(void *arg);
void		mpt_check_doorbell(struct mpt_softc *mpt);
void		mpt_dump_reply_frame(struct mpt_softc *mpt,
				     MSG_DEFAULT_REPLY *reply_frame);

int		mpt_issue_cfg_req(struct mpt_softc */*mpt*/, request_t */*req*/,
				  cfgparms_t *params,
				  bus_addr_t /*addr*/, bus_size_t/*len*/,
				  int /*sleep_ok*/, int /*timeout_ms*/);
int		mpt_read_extcfg_header(struct mpt_softc *mpt, int PageVersion,
				       int PageNumber, uint32_t PageAddress,
				       int ExtPageType,
				       CONFIG_EXTENDED_PAGE_HEADER *rslt,
				       int sleep_ok, int timeout_ms);
int		mpt_read_extcfg_page(struct mpt_softc *mpt, int Action,
				     uint32_t PageAddress,
				     CONFIG_EXTENDED_PAGE_HEADER *hdr,
				     void *buf, size_t len, int sleep_ok,
				     int timeout_ms);
int		mpt_read_cfg_header(struct mpt_softc *, int /*PageType*/,
				    int /*PageNumber*/,
				    uint32_t /*PageAddress*/,
				    CONFIG_PAGE_HEADER *,
				    int /*sleep_ok*/, int /*timeout_ms*/);
int		mpt_read_cfg_page(struct mpt_softc *t, int /*Action*/,
				  uint32_t /*PageAddress*/,
				  CONFIG_PAGE_HEADER *, size_t /*len*/,
				  int /*sleep_ok*/, int /*timeout_ms*/);
int		mpt_write_cfg_page(struct mpt_softc *, int /*Action*/,
				   uint32_t /*PageAddress*/,
				   CONFIG_PAGE_HEADER *, size_t /*len*/,
				   int /*sleep_ok*/, int /*timeout_ms*/);
static __inline int
mpt_read_cur_cfg_page(struct mpt_softc *mpt, uint32_t PageAddress,
		      CONFIG_PAGE_HEADER *hdr, size_t len,
		      int sleep_ok, int timeout_ms)
{
	return (mpt_read_cfg_page(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
				  PageAddress, hdr, len, sleep_ok, timeout_ms));
}

static __inline int
mpt_write_cur_cfg_page(struct mpt_softc *mpt, uint32_t PageAddress,
		       CONFIG_PAGE_HEADER *hdr, size_t len, int sleep_ok,
		       int timeout_ms)
{
	return (mpt_write_cfg_page(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
				   PageAddress, hdr, len, sleep_ok,
				   timeout_ms));
}

/* mpt_debug.c functions */
void mpt_print_reply(void *vmsg);
void mpt_print_db(uint32_t mb);
void mpt_print_config_reply(void *vmsg);
char *mpt_ioc_diag(uint32_t diag);
void mpt_req_state(mpt_req_state_t state);
void mpt_print_config_request(void *vmsg);
void mpt_print_request(void *vmsg);
void mpt_dump_sgl(SGE_IO_UNION *se, int offset);

#endif /* _MPT_H_ */
