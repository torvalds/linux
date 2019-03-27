/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Yahoo! Inc.
 * Copyright (c) 2011-2015 LSI Corp.
 * Copyright (c) 2013-2015 Avago Technologies
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Avago Technologies (LSI) MPT-Fusion Host Adapter FreeBSD
 *
 * $FreeBSD$
 */

#ifndef _MPSVAR_H
#define _MPSVAR_H

#define MPS_DRIVER_VERSION	"21.02.00.00-fbsd"

#define MPS_DB_MAX_WAIT		2500

#define MPS_REQ_FRAMES		2048
#define MPS_PRI_REQ_FRAMES	128
#define MPS_EVT_REPLY_FRAMES	32
#define MPS_REPLY_FRAMES	MPS_REQ_FRAMES
#define MPS_CHAIN_FRAMES	16384
#define MPS_MAXIO_PAGES		(-1)
#define MPS_SENSE_LEN		SSD_FULL_SIZE
#define MPS_MSI_MAX		1
#define MPS_MSIX_MAX		16
#define MPS_SGE64_SIZE		12
#define MPS_SGE32_SIZE		8
#define MPS_SGC_SIZE		8

#define	 CAN_SLEEP			1
#define  NO_SLEEP			0

#define MPS_PERIODIC_DELAY	1	/* 1 second heartbeat/watchdog check */
#define MPS_ATA_ID_TIMEOUT	5	/* 5 second timeout for SATA ID cmd */
#define MPS_MISSING_CHECK_DELAY	10	/* 10 seconds between missing check */

#define MPS_SCSI_RI_INVALID_FRAME	(0x00000002)

#define DEFAULT_SPINUP_WAIT	3	/* seconds to wait for spinup */

#include <sys/endian.h>

/*
 * host mapping related macro definitions
 */
#define MPS_MAPTABLE_BAD_IDX	0xFFFFFFFF
#define MPS_DPM_BAD_IDX		0xFFFF
#define MPS_ENCTABLE_BAD_IDX	0xFF
#define MPS_MAX_MISSING_COUNT	0x0F
#define MPS_DEV_RESERVED	0x20000000
#define MPS_MAP_IN_USE		0x10000000
#define MPS_MAP_BAD_ID		0xFFFFFFFF

/*
 * WarpDrive controller
 */
#define	MPS_CHIP_WD_DEVICE_ID	0x007E
#define	MPS_WD_LSI_OEM		0x80
#define	MPS_WD_HIDE_EXPOSE_MASK	0x03
#define	MPS_WD_HIDE_ALWAYS	0x00
#define	MPS_WD_EXPOSE_ALWAYS	0x01
#define	MPS_WD_HIDE_IF_VOLUME	0x02
#define	MPS_WD_RETRY		0x01
#define	MPS_MAN_PAGE10_SIZE	0x5C	/* Hardcode for now */
#define MPS_MAX_DISKS_IN_VOL	10

/*
 * WarpDrive Event Logging
 */
#define	MPI2_WD_LOG_ENTRY	0x8002
#define	MPI2_WD_SSD_THROTTLING	0x0041
#define	MPI2_WD_DRIVE_LIFE_WARN	0x0043
#define	MPI2_WD_DRIVE_LIFE_DEAD	0x0044
#define	MPI2_WD_RAIL_MON_FAIL	0x004D

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/**
 * struct dev_mapping_table - device mapping information
 * @physical_id: SAS address for drives or WWID for RAID volumes
 * @device_info: bitfield provides detailed info about the device
 * @phy_bits: bitfields indicating controller phys
 * @dpm_entry_num: index of this device in device persistent map table
 * @dev_handle: device handle for the device pointed by this entry
 * @id: target id
 * @missing_count: number of times the device not detected by driver
 * @hide_flag: Hide this physical disk/not (foreign configuration)
 * @init_complete: Whether the start of the day checks completed or not
 */
struct dev_mapping_table {
	u64	physical_id;
	u32	device_info;
	u32	phy_bits;
	u16	dpm_entry_num;
	u16	dev_handle;
	u16	reserved1;
	u16	id;
	u8	missing_count;
	u8	init_complete;
	u8	TLR_bits;
	u8	reserved2;
};

/**
 * struct enc_mapping_table -  mapping information about an enclosure
 * @enclosure_id: Logical ID of this enclosure
 * @start_index: index to the entry in dev_mapping_table
 * @phy_bits: bitfields indicating controller phys
 * @dpm_entry_num: index of this enclosure in device persistent map table
 * @enc_handle: device handle for the enclosure pointed by this entry
 * @num_slots: number of slots in the enclosure
 * @start_slot: Starting slot id
 * @missing_count: number of times the device not detected by driver
 * @removal_flag: used to mark the device for removal
 * @skip_search: used as a flag to include/exclude enclosure for search
 * @init_complete: Whether the start of the day checks completed or not
 */
struct enc_mapping_table {
	u64	enclosure_id;
	u32	start_index;
	u32	phy_bits;
	u16	dpm_entry_num;
	u16	enc_handle;
	u16	num_slots;
	u16	start_slot;
	u8	missing_count;
	u8	removal_flag;
	u8	skip_search;
	u8	init_complete;
};

/**
 * struct map_removal_table - entries to be removed from mapping table
 * @dpm_entry_num: index of this device in device persistent map table
 * @dev_handle: device handle for the device pointed by this entry
 */
struct map_removal_table{
	u16	dpm_entry_num;
	u16	dev_handle;
};

typedef struct mps_fw_diagnostic_buffer {
	size_t		size;
	uint8_t		extended_type;
	uint8_t		buffer_type;
	uint8_t		force_release;
	uint32_t	product_specific[23];
	uint8_t		immediate;
	uint8_t		enabled;
	uint8_t		valid_data;
	uint8_t		owned_by_firmware;
	uint32_t	unique_id;
} mps_fw_diagnostic_buffer_t;

struct mps_softc;
struct mps_command;
struct mpssas_softc;
union ccb;
struct mpssas_target;
struct mps_column_map;

MALLOC_DECLARE(M_MPT2);

typedef void mps_evt_callback_t(struct mps_softc *, uintptr_t,
    MPI2_EVENT_NOTIFICATION_REPLY *reply);
typedef void mps_command_callback_t(struct mps_softc *, struct mps_command *cm);

struct mps_chain {
	TAILQ_ENTRY(mps_chain)		chain_link;
	MPI2_SGE_IO_UNION		*chain;
	uint32_t			chain_busaddr;
};

/*
 * This needs to be at least 2 to support SMP passthrough.
 */
#define       MPS_IOVEC_COUNT 2

struct mps_command {
	TAILQ_ENTRY(mps_command)	cm_link;
	TAILQ_ENTRY(mps_command)	cm_recovery;
	struct mps_softc		*cm_sc;
	union ccb			*cm_ccb;
	void				*cm_data;
	u_int				cm_length;
	u_int				cm_out_len;
	struct uio			cm_uio;
	struct iovec			cm_iovec[MPS_IOVEC_COUNT];
	u_int				cm_max_segs;
	u_int				cm_sglsize;
	MPI2_SGE_IO_UNION		*cm_sge;
	uint8_t				*cm_req;
	uint8_t				*cm_reply;
	uint32_t			cm_reply_data;
	mps_command_callback_t		*cm_complete;
	void				*cm_complete_data;
	struct mpssas_target		*cm_targ;
	MPI2_REQUEST_DESCRIPTOR_UNION	cm_desc;
	u_int	                	cm_lun;
	u_int				cm_flags;
#define MPS_CM_FLAGS_POLLED		(1 << 0)
#define MPS_CM_FLAGS_COMPLETE		(1 << 1)
#define MPS_CM_FLAGS_SGE_SIMPLE		(1 << 2)
#define MPS_CM_FLAGS_DATAOUT		(1 << 3)
#define MPS_CM_FLAGS_DATAIN		(1 << 4)
#define MPS_CM_FLAGS_WAKEUP		(1 << 5)
#define MPS_CM_FLAGS_DD_IO		(1 << 6)
#define MPS_CM_FLAGS_USE_UIO		(1 << 7)
#define MPS_CM_FLAGS_SMP_PASS		(1 << 8)
#define	MPS_CM_FLAGS_CHAIN_FAILED	(1 << 9)
#define	MPS_CM_FLAGS_ERROR_MASK		MPS_CM_FLAGS_CHAIN_FAILED
#define	MPS_CM_FLAGS_USE_CCB		(1 << 10)
#define	MPS_CM_FLAGS_SATA_ID_TIMEOUT	(1 << 11)
	u_int				cm_state;
#define MPS_CM_STATE_FREE		0
#define MPS_CM_STATE_BUSY		1
#define MPS_CM_STATE_TIMEDOUT		2
#define MPS_CM_STATE_INQUEUE		3
	bus_dmamap_t			cm_dmamap;
	struct scsi_sense_data		*cm_sense;
	TAILQ_HEAD(, mps_chain)		cm_chain_list;
	uint32_t			cm_req_busaddr;
	uint32_t			cm_sense_busaddr;
	struct callout			cm_callout;
	mps_command_callback_t		*cm_timeout_handler;
};

struct mps_column_map {
	uint16_t			dev_handle;
	uint8_t				phys_disk_num;
};

struct mps_event_handle {
	TAILQ_ENTRY(mps_event_handle)	eh_list;
	mps_evt_callback_t		*callback;
	void				*data;
	u32				mask[MPI2_EVENT_NOTIFY_EVENTMASK_WORDS];
};

struct mps_busdma_context {
	int				completed;
	int				abandoned;
	int				error;
	bus_addr_t			*addr;
	struct mps_softc		*softc;
	bus_dmamap_t			buffer_dmamap;
	bus_dma_tag_t			buffer_dmat;
};

struct mps_queue {
	struct mps_softc		*sc;
	int				qnum;
	MPI2_REPLY_DESCRIPTORS_UNION	*post_queue;
	int				replypostindex;
#ifdef notyet
	ck_ring_buffer_t		*ringmem;
	ck_ring_buffer_t		*chainmem;
	ck_ring_t			req_ring;
	ck_ring_t			chain_ring;
#endif
	bus_dma_tag_t			buffer_dmat;
	int				io_cmds_highwater;
	int				chain_free_lowwater;
	int				chain_alloc_fail;
	struct resource			*irq;
	void				*intrhand;
	int				irq_rid;
};

struct mps_softc {
	device_t			mps_dev;
	struct cdev			*mps_cdev;
	u_int				mps_flags;
#define MPS_FLAGS_INTX		(1 << 0)
#define MPS_FLAGS_MSI		(1 << 1)
#define MPS_FLAGS_BUSY		(1 << 2)
#define MPS_FLAGS_SHUTDOWN	(1 << 3)
#define MPS_FLAGS_DIAGRESET	(1 << 4)
#define	MPS_FLAGS_ATTACH_DONE	(1 << 5)
#define	MPS_FLAGS_WD_AVAILABLE	(1 << 6)
#define	MPS_FLAGS_REALLOCATED	(1 << 7)
	u_int				mps_debug;
	u_int				msi_msgs;
	u_int				reqframesz;
	u_int				replyframesz;
	int				tm_cmds_active;
	int				io_cmds_active;
	int				io_cmds_highwater;
	int				chain_free;
	int				max_chains;
	int				max_io_pages;
	u_int				maxio;
	int				chain_free_lowwater;
	u_int				enable_ssu;
	int				spinup_wait_time;
	int				use_phynum;
	uint64_t			chain_alloc_fail;
	struct sysctl_ctx_list		sysctl_ctx;
	struct sysctl_oid		*sysctl_tree;
	char                            fw_version[16];
	struct mps_command		*commands;
	struct mps_chain		*chains;
	struct callout			periodic;
	struct callout			device_check_callout;
	struct mps_queue		*queues;

	struct mpssas_softc		*sassc;
	TAILQ_HEAD(, mps_command)	req_list;
	TAILQ_HEAD(, mps_command)	high_priority_req_list;
	TAILQ_HEAD(, mps_chain)		chain_list;
	TAILQ_HEAD(, mps_command)	tm_list;
	int				replypostindex;
	int				replyfreeindex;

	struct resource			*mps_regs_resource;
	bus_space_handle_t		mps_bhandle;
	bus_space_tag_t			mps_btag;
	int				mps_regs_rid;

	bus_dma_tag_t			mps_parent_dmat;
	bus_dma_tag_t			buffer_dmat;

	MPI2_IOC_FACTS_REPLY		*facts;
	int				num_reqs;
	int				num_prireqs;
	int				num_replies;
	int				num_chains;
	int				fqdepth;	/* Free queue */
	int				pqdepth;	/* Post queue */

	u32             event_mask[MPI2_EVENT_NOTIFY_EVENTMASK_WORDS];
	TAILQ_HEAD(, mps_event_handle)	event_list;
	struct mps_event_handle		*mps_log_eh;

	struct mtx			mps_mtx;
	struct intr_config_hook		mps_ich;

	uint8_t				*req_frames;
	bus_addr_t			req_busaddr;
	bus_dma_tag_t			req_dmat;
	bus_dmamap_t			req_map;

	uint8_t				*reply_frames;
	bus_addr_t			reply_busaddr;
	bus_dma_tag_t			reply_dmat;
	bus_dmamap_t			reply_map;

	struct scsi_sense_data		*sense_frames;
	bus_addr_t			sense_busaddr;
	bus_dma_tag_t			sense_dmat;
	bus_dmamap_t			sense_map;

	uint8_t				*chain_frames;
	bus_dma_tag_t			chain_dmat;
	bus_dmamap_t			chain_map;

	MPI2_REPLY_DESCRIPTORS_UNION	*post_queue;
	bus_addr_t			post_busaddr;
	uint32_t			*free_queue;
	bus_addr_t			free_busaddr;
	bus_dma_tag_t			queues_dmat;
	bus_dmamap_t			queues_map;

	uint8_t				*fw_diag_buffer;
	bus_addr_t			fw_diag_busaddr;
	bus_dma_tag_t			fw_diag_dmat;
	bus_dmamap_t			fw_diag_map;

	uint8_t				ir_firmware;

	/* static config pages */
	Mpi2IOCPage8_t			ioc_pg8;

	/* host mapping support */
	struct dev_mapping_table	*mapping_table;
	struct enc_mapping_table	*enclosure_table;
	struct map_removal_table	*removal_table;
	uint8_t				*dpm_entry_used;
	uint8_t				*dpm_flush_entry;
	Mpi2DriverMappingPage0_t	*dpm_pg0;
	uint16_t			max_devices;
	uint16_t			max_enclosures;
	uint16_t			max_expanders;
	uint8_t				max_volumes;
	uint8_t				num_enc_table_entries;
	uint8_t				num_rsvd_entries;
	uint16_t			max_dpm_entries;
	uint8_t				is_dpm_enable;
	uint8_t				track_mapping_events;
	uint32_t			pending_map_events;

	/* FW diag Buffer List */
	mps_fw_diagnostic_buffer_t
				fw_diag_buffer_list[MPI2_DIAG_BUF_TYPE_COUNT];

	/* Event Recording IOCTL support */
	uint32_t			events_to_record[4];
	mps_event_entry_t		recorded_events[MPS_EVENT_QUEUE_SIZE];
	uint8_t				event_index;
	uint32_t			event_number;

	/* EEDP and TLR support */
	uint8_t				eedp_enabled;
	uint8_t				control_TLR;

	/* Shutdown Event Handler */
	eventhandler_tag		shutdown_eh;

	/* To track topo events during reset */
#define	MPS_DIAG_RESET_TIMEOUT	300000
	uint8_t				wait_for_port_enable;
	uint8_t				port_enable_complete;
	uint8_t				msleep_fake_chan;

	/* WD controller */
	uint8_t             WD_available;
	uint8_t				WD_valid_config;
	uint8_t				WD_hide_expose;

	/* Direct Drive for WarpDrive */
	uint8_t				DD_num_phys_disks;
	uint16_t			DD_dev_handle;
	uint32_t			DD_stripe_size;
	uint32_t			DD_stripe_exponent;
	uint32_t			DD_block_size;
	uint16_t			DD_block_exponent;
	uint64_t			DD_max_lba;
	struct mps_column_map		DD_column_map[MPS_MAX_DISKS_IN_VOL];

	/* StartStopUnit command handling at shutdown */
	uint32_t			SSU_refcount;
	uint8_t				SSU_started;

	/* Configuration tunables */
	u_int				disable_msix;
	u_int				disable_msi;
	u_int				max_msix;
	u_int				max_reqframes;
	u_int				max_prireqframes;
	u_int				max_replyframes;
	u_int				max_evtframes;
	char				exclude_ids[80];

	struct timeval			lastfail;
};

struct mps_config_params {
	MPI2_CONFIG_EXT_PAGE_HEADER_UNION	hdr;
	u_int		action;
	u_int		page_address;	/* Attributes, not a phys address */
	u_int		status;
	void		*buffer;
	u_int		length;
	int		timeout;
	void		(*callback)(struct mps_softc *, struct mps_config_params *);
	void		*cbdata;
};

struct scsi_read_capacity_eedp
{
	uint8_t addr[8];
	uint8_t length[4];
	uint8_t protect;
};

static __inline uint32_t
mps_regread(struct mps_softc *sc, uint32_t offset)
{
	return (bus_space_read_4(sc->mps_btag, sc->mps_bhandle, offset));
}

static __inline void
mps_regwrite(struct mps_softc *sc, uint32_t offset, uint32_t val)
{
	bus_space_write_4(sc->mps_btag, sc->mps_bhandle, offset, val);
}

/* free_queue must have Little Endian address 
 * TODO- cm_reply_data is unwanted. We can remove it.
 * */
static __inline void
mps_free_reply(struct mps_softc *sc, uint32_t busaddr)
{
	if (++sc->replyfreeindex >= sc->fqdepth)
		sc->replyfreeindex = 0;
	sc->free_queue[sc->replyfreeindex] = htole32(busaddr);
	mps_regwrite(sc, MPI2_REPLY_FREE_HOST_INDEX_OFFSET, sc->replyfreeindex);
}

static __inline struct mps_chain *
mps_alloc_chain(struct mps_softc *sc)
{
	struct mps_chain *chain;

	if ((chain = TAILQ_FIRST(&sc->chain_list)) != NULL) {
		TAILQ_REMOVE(&sc->chain_list, chain, chain_link);
		sc->chain_free--;
		if (sc->chain_free < sc->chain_free_lowwater)
			sc->chain_free_lowwater = sc->chain_free;
	} else
		sc->chain_alloc_fail++;
	return (chain);
}

static __inline void
mps_free_chain(struct mps_softc *sc, struct mps_chain *chain)
{
	sc->chain_free++;
	TAILQ_INSERT_TAIL(&sc->chain_list, chain, chain_link);
}

static __inline void
mps_free_command(struct mps_softc *sc, struct mps_command *cm)
{
	struct mps_chain *chain, *chain_temp;

	KASSERT(cm->cm_state == MPS_CM_STATE_BUSY, ("state not busy\n"));

	if (cm->cm_reply != NULL)
		mps_free_reply(sc, cm->cm_reply_data);
	cm->cm_reply = NULL;
	cm->cm_flags = 0;
	cm->cm_complete = NULL;
	cm->cm_complete_data = NULL;
	cm->cm_ccb = NULL;
	cm->cm_targ = NULL;
	cm->cm_max_segs = 0;
	cm->cm_lun = 0;
	cm->cm_state = MPS_CM_STATE_FREE;
	cm->cm_data = NULL;
	cm->cm_length = 0;
	cm->cm_out_len = 0;
	cm->cm_sglsize = 0;
	cm->cm_sge = NULL;

	TAILQ_FOREACH_SAFE(chain, &cm->cm_chain_list, chain_link, chain_temp) {
		TAILQ_REMOVE(&cm->cm_chain_list, chain, chain_link);
		mps_free_chain(sc, chain);
	}
	TAILQ_INSERT_TAIL(&sc->req_list, cm, cm_link);
}

static __inline struct mps_command *
mps_alloc_command(struct mps_softc *sc)
{
	struct mps_command *cm;

	cm = TAILQ_FIRST(&sc->req_list);
	if (cm == NULL)
		return (NULL);

	KASSERT(cm->cm_state == MPS_CM_STATE_FREE,
	    ("mps: Allocating busy command\n"));

	TAILQ_REMOVE(&sc->req_list, cm, cm_link);
	cm->cm_state = MPS_CM_STATE_BUSY;
	cm->cm_timeout_handler = NULL;
	return (cm);
}

static __inline void
mps_free_high_priority_command(struct mps_softc *sc, struct mps_command *cm)
{
	struct mps_chain *chain, *chain_temp;

	KASSERT(cm->cm_state == MPS_CM_STATE_BUSY, ("state not busy\n"));

	if (cm->cm_reply != NULL)
		mps_free_reply(sc, cm->cm_reply_data);
	cm->cm_reply = NULL;
	cm->cm_flags = 0;
	cm->cm_complete = NULL;
	cm->cm_complete_data = NULL;
	cm->cm_ccb = NULL;
	cm->cm_targ = NULL;
	cm->cm_lun = 0;
	cm->cm_state = MPS_CM_STATE_FREE;
	TAILQ_FOREACH_SAFE(chain, &cm->cm_chain_list, chain_link, chain_temp) {
		TAILQ_REMOVE(&cm->cm_chain_list, chain, chain_link);
		mps_free_chain(sc, chain);
	}
	TAILQ_INSERT_TAIL(&sc->high_priority_req_list, cm, cm_link);
}

static __inline struct mps_command *
mps_alloc_high_priority_command(struct mps_softc *sc)
{
	struct mps_command *cm;

	cm = TAILQ_FIRST(&sc->high_priority_req_list);
	if (cm == NULL)
		return (NULL);

	KASSERT(cm->cm_state == MPS_CM_STATE_FREE,
	    ("mps: Allocating busy command\n"));

	TAILQ_REMOVE(&sc->high_priority_req_list, cm, cm_link);
	cm->cm_state = MPS_CM_STATE_BUSY;
	cm->cm_timeout_handler = NULL;
	cm->cm_desc.HighPriority.RequestFlags =
	    MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY;
	return (cm);
}

static __inline void
mps_lock(struct mps_softc *sc)
{
	mtx_lock(&sc->mps_mtx);
}

static __inline void
mps_unlock(struct mps_softc *sc)
{
	mtx_unlock(&sc->mps_mtx);
}

#define MPS_INFO	(1 << 0)	/* Basic info */
#define MPS_FAULT	(1 << 1)	/* Hardware faults */
#define MPS_EVENT	(1 << 2)	/* Event data from the controller */
#define MPS_LOG		(1 << 3)	/* Log data from the controller */
#define MPS_RECOVERY	(1 << 4)	/* Command error recovery tracing */
#define MPS_ERROR	(1 << 5)	/* Parameter errors, programming bugs */
#define MPS_INIT	(1 << 6)	/* Things related to system init */
#define MPS_XINFO	(1 << 7)	/* More detailed/noisy info */
#define MPS_USER	(1 << 8)	/* Trace user-generated commands */
#define MPS_MAPPING	(1 << 9)	/* Trace device mappings */
#define MPS_TRACE	(1 << 10)	/* Function-by-function trace */

#define	MPS_SSU_DISABLE_SSD_DISABLE_HDD	0
#define	MPS_SSU_ENABLE_SSD_DISABLE_HDD	1
#define	MPS_SSU_DISABLE_SSD_ENABLE_HDD	2
#define	MPS_SSU_ENABLE_SSD_ENABLE_HDD	3

#define mps_printf(sc, args...)				\
	device_printf((sc)->mps_dev, ##args)

#define mps_print_field(sc, msg, args...)		\
	printf("\t" msg, ##args)

#define mps_vprintf(sc, args...)			\
do {							\
	if (bootverbose)				\
		mps_printf(sc, ##args);			\
} while (0)

#define mps_dprint(sc, level, msg, args...)		\
do {							\
	if ((sc)->mps_debug & (level))			\
		device_printf((sc)->mps_dev, msg, ##args);	\
} while (0)

#define MPS_PRINTFIELD_START(sc, tag...)	\
	mps_printf((sc), ##tag);			\
	mps_print_field((sc), ":\n")
#define MPS_PRINTFIELD_END(sc, tag)		\
	mps_printf((sc), tag "\n")
#define MPS_PRINTFIELD(sc, facts, attr, fmt)	\
	mps_print_field((sc), #attr ": " #fmt "\n", (facts)->attr)

#define MPS_FUNCTRACE(sc)			\
	mps_dprint((sc), MPS_TRACE, "%s\n", __func__)

#define  CAN_SLEEP                      1
#define  NO_SLEEP                       0

static __inline void
mps_from_u64(uint64_t data, U64 *mps)
{
	(mps)->High = htole32((uint32_t)((data) >> 32));
	(mps)->Low = htole32((uint32_t)((data) & 0xffffffff));
}

static __inline uint64_t
mps_to_u64(U64 *data)
{

	return (((uint64_t)le32toh(data->High) << 32) | le32toh(data->Low));
}

static __inline void
mps_mask_intr(struct mps_softc *sc)
{
	uint32_t mask;

	mask = mps_regread(sc, MPI2_HOST_INTERRUPT_MASK_OFFSET);
	mask |= MPI2_HIM_REPLY_INT_MASK;
	mps_regwrite(sc, MPI2_HOST_INTERRUPT_MASK_OFFSET, mask);
}

static __inline void
mps_unmask_intr(struct mps_softc *sc)
{
	uint32_t mask;

	mask = mps_regread(sc, MPI2_HOST_INTERRUPT_MASK_OFFSET);
	mask &= ~MPI2_HIM_REPLY_INT_MASK;
	mps_regwrite(sc, MPI2_HOST_INTERRUPT_MASK_OFFSET, mask);
}

int mps_pci_setup_interrupts(struct mps_softc *sc);
void mps_pci_free_interrupts(struct mps_softc *sc);
int mps_pci_restore(struct mps_softc *sc);

void mps_get_tunables(struct mps_softc *sc);
int mps_attach(struct mps_softc *sc);
int mps_free(struct mps_softc *sc);
void mps_intr(void *);
void mps_intr_msi(void *);
void mps_intr_locked(void *);
int mps_register_events(struct mps_softc *, u32 *, mps_evt_callback_t *,
    void *, struct mps_event_handle **);
int mps_restart(struct mps_softc *);
int mps_update_events(struct mps_softc *, struct mps_event_handle *, u32 *);
void mps_deregister_events(struct mps_softc *, struct mps_event_handle *);
int mps_push_sge(struct mps_command *, void *, size_t, int);
int mps_add_dmaseg(struct mps_command *, vm_paddr_t, size_t, u_int, int);
int mps_attach_sas(struct mps_softc *sc);
int mps_detach_sas(struct mps_softc *sc);
int mps_read_config_page(struct mps_softc *, struct mps_config_params *);
int mps_write_config_page(struct mps_softc *, struct mps_config_params *);
void mps_memaddr_cb(void *, bus_dma_segment_t *, int , int );
void mps_memaddr_wait_cb(void *, bus_dma_segment_t *, int , int );
void mpi_init_sge(struct mps_command *cm, void *req, void *sge);
int mps_attach_user(struct mps_softc *);
void mps_detach_user(struct mps_softc *);
void mpssas_record_event(struct mps_softc *sc,
    MPI2_EVENT_NOTIFICATION_REPLY *event_reply);

int mps_map_command(struct mps_softc *sc, struct mps_command *cm);
int mps_wait_command(struct mps_softc *sc, struct mps_command **cm, int timeout,
    int sleep_flag);

int mps_config_get_bios_pg3(struct mps_softc *sc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2BiosPage3_t *config_page);
int mps_config_get_raid_volume_pg0(struct mps_softc *sc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2RaidVolPage0_t *config_page, u32 page_address);
int mps_config_get_ioc_pg8(struct mps_softc *sc, Mpi2ConfigReply_t *,
    Mpi2IOCPage8_t *);
int mps_config_get_man_pg10(struct mps_softc *sc, Mpi2ConfigReply_t *mpi_reply);
int mps_config_get_sas_device_pg0(struct mps_softc *, Mpi2ConfigReply_t *,
    Mpi2SasDevicePage0_t *, u32 , u16 );
int mps_config_get_dpm_pg0(struct mps_softc *, Mpi2ConfigReply_t *,
    Mpi2DriverMappingPage0_t *, u16 );
int mps_config_get_raid_volume_pg1(struct mps_softc *sc,
    Mpi2ConfigReply_t *mpi_reply, Mpi2RaidVolPage1_t *config_page, u32 form,
    u16 handle);
int mps_config_get_volume_wwid(struct mps_softc *sc, u16 volume_handle,
    u64 *wwid);
int mps_config_get_raid_pd_pg0(struct mps_softc *sc,
    Mpi2ConfigReply_t *mpi_reply, Mpi2RaidPhysDiskPage0_t *config_page,
    u32 page_address);
void mpssas_ir_shutdown(struct mps_softc *sc, int howto);

int mps_reinit(struct mps_softc *sc);
void mpssas_handle_reinit(struct mps_softc *sc);

void mps_base_static_config_pages(struct mps_softc *sc);
void mps_wd_config_pages(struct mps_softc *sc);

int mps_mapping_initialize(struct mps_softc *);
void mps_mapping_topology_change_event(struct mps_softc *,
    Mpi2EventDataSasTopologyChangeList_t *);
void mps_mapping_free_memory(struct mps_softc *sc);
int mps_config_set_dpm_pg0(struct mps_softc *, Mpi2ConfigReply_t *,
    Mpi2DriverMappingPage0_t *, u16 );
void mps_mapping_exit(struct mps_softc *);
void mps_mapping_check_devices(void *);
int mps_mapping_allocate_memory(struct mps_softc *sc);
unsigned int mps_mapping_get_tid(struct mps_softc *, uint64_t , u16);
unsigned int mps_mapping_get_tid_from_handle(struct mps_softc *sc,
    u16 handle);
unsigned int mps_mapping_get_raid_tid(struct mps_softc *sc, u64 wwid,
     u16 volHandle);
unsigned int mps_mapping_get_raid_tid_from_handle(struct mps_softc *sc,
    u16 volHandle);
void mps_mapping_enclosure_dev_status_change_event(struct mps_softc *,
    Mpi2EventDataSasEnclDevStatusChange_t *event_data);
void mps_mapping_ir_config_change_event(struct mps_softc *sc,
    Mpi2EventDataIrConfigChangeList_t *event_data);
int mps_mapping_dump(SYSCTL_HANDLER_ARGS);
int mps_mapping_encl_dump(SYSCTL_HANDLER_ARGS);

void mpssas_evt_handler(struct mps_softc *sc, uintptr_t data,
    MPI2_EVENT_NOTIFICATION_REPLY *event);
void mpssas_prepare_remove(struct mpssas_softc *sassc, uint16_t handle);
void mpssas_prepare_volume_remove(struct mpssas_softc *sassc, uint16_t handle);
int mpssas_startup(struct mps_softc *sc);
struct mpssas_target * mpssas_find_target_by_handle(struct mpssas_softc *, int, uint16_t);
void mpssas_realloc_targets(struct mps_softc *sc, int maxtargets);
struct mps_command * mpssas_alloc_tm(struct mps_softc *sc);
void mpssas_free_tm(struct mps_softc *sc, struct mps_command *tm);
void mpssas_release_simq_reinit(struct mpssas_softc *sassc);
int mpssas_send_reset(struct mps_softc *sc, struct mps_command *tm,
    uint8_t type);

SYSCTL_DECL(_hw_mps);

/* Compatibility shims for different OS versions */
#if __FreeBSD_version >= 800001
#define mps_kproc_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg) \
    kproc_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg)
#define mps_kproc_exit(arg)	kproc_exit(arg)
#else
#define mps_kproc_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg) \
    kthread_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg)
#define mps_kproc_exit(arg)	kthread_exit(arg)
#endif

#if defined(CAM_PRIORITY_XPT)
#define MPS_PRIORITY_XPT	CAM_PRIORITY_XPT
#else
#define MPS_PRIORITY_XPT	5
#endif

#if __FreeBSD_version < 800107
// Prior to FreeBSD-8.0 scp3_flags was not defined.
#define spc3_flags reserved

#define SPC3_SID_PROTECT    0x01
#define SPC3_SID_3PC        0x08
#define SPC3_SID_TPGS_MASK  0x30
#define SPC3_SID_TPGS_IMPLICIT  0x10
#define SPC3_SID_TPGS_EXPLICIT  0x20
#define SPC3_SID_ACC        0x40
#define SPC3_SID_SCCS       0x80

#define CAM_PRIORITY_NORMAL CAM_PRIORITY_NONE
#endif

#endif

