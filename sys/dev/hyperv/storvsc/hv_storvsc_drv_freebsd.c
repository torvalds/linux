/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2012,2016-2017 Microsoft Corp.
 * Copyright (c) 2012 NetApp Inc.
 * Copyright (c) 2012 Citrix Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 */

/**
 * StorVSC driver for Hyper-V.  This driver presents a SCSI HBA interface
 * to the Comman Access Method (CAM) layer.  CAM control blocks (CCBs) are
 * converted into VSCSI protocol messages which are delivered to the parent
 * partition StorVSP driver over the Hyper-V VMBUS.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/condvar.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/smp.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/uma.h>
#include <sys/lock.h>
#include <sys/sema.h>
#include <sys/sglist.h>
#include <sys/eventhandler.h>
#include <machine/bus.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_xpt_internal.h>
#include <cam/cam_debug.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

#include <dev/hyperv/include/hyperv.h>
#include <dev/hyperv/include/vmbus.h>
#include "hv_vstorage.h"
#include "vmbus_if.h"

#define STORVSC_MAX_LUNS_PER_TARGET	(64)
#define STORVSC_MAX_IO_REQUESTS		(STORVSC_MAX_LUNS_PER_TARGET * 2)
#define BLKVSC_MAX_IDE_DISKS_PER_TARGET	(1)
#define BLKVSC_MAX_IO_REQUESTS		STORVSC_MAX_IO_REQUESTS
#define STORVSC_MAX_TARGETS		(2)

#define VSTOR_PKT_SIZE	(sizeof(struct vstor_packet) - vmscsi_size_delta)

/*
 * 33 segments are needed to allow 128KB maxio, in case the data
 * in the first page is _not_ PAGE_SIZE aligned, e.g.
 *
 *     |<----------- 128KB ----------->|
 *     |                               |
 *  0  2K 4K    8K   16K   124K  128K  130K
 *  |  |  |     |     |       |     |  |
 *  +--+--+-----+-----+.......+-----+--+--+
 *  |  |  |     |     |       |     |  |  | DATA
 *  |  |  |     |     |       |     |  |  |
 *  +--+--+-----+-----+.......------+--+--+
 *     |  |                         |  |
 *     | 1|            31           | 1| ...... # of segments
 */
#define STORVSC_DATA_SEGCNT_MAX		33
#define STORVSC_DATA_SEGSZ_MAX		PAGE_SIZE
#define STORVSC_DATA_SIZE_MAX		\
	((STORVSC_DATA_SEGCNT_MAX - 1) * STORVSC_DATA_SEGSZ_MAX)

struct storvsc_softc;

struct hv_sgl_node {
	LIST_ENTRY(hv_sgl_node) link;
	struct sglist *sgl_data;
};

struct hv_sgl_page_pool{
	LIST_HEAD(, hv_sgl_node) in_use_sgl_list;
	LIST_HEAD(, hv_sgl_node) free_sgl_list;
	boolean_t                is_init;
} g_hv_sgl_page_pool;

enum storvsc_request_type {
	WRITE_TYPE,
	READ_TYPE,
	UNKNOWN_TYPE
};

SYSCTL_NODE(_hw, OID_AUTO, storvsc, CTLFLAG_RD | CTLFLAG_MPSAFE, NULL,
	"Hyper-V storage interface");

static u_int hv_storvsc_use_win8ext_flags = 1;
SYSCTL_UINT(_hw_storvsc, OID_AUTO, use_win8ext_flags, CTLFLAG_RW,
	&hv_storvsc_use_win8ext_flags, 0,
	"Use win8 extension flags or not");

static u_int hv_storvsc_use_pim_unmapped = 1;
SYSCTL_UINT(_hw_storvsc, OID_AUTO, use_pim_unmapped, CTLFLAG_RDTUN,
	&hv_storvsc_use_pim_unmapped, 0,
	"Optimize storvsc by using unmapped I/O");

static u_int hv_storvsc_ringbuffer_size = (64 * PAGE_SIZE);
SYSCTL_UINT(_hw_storvsc, OID_AUTO, ringbuffer_size, CTLFLAG_RDTUN,
	&hv_storvsc_ringbuffer_size, 0, "Hyper-V storage ringbuffer size");

static u_int hv_storvsc_max_io = 512;
SYSCTL_UINT(_hw_storvsc, OID_AUTO, max_io, CTLFLAG_RDTUN,
	&hv_storvsc_max_io, 0, "Hyper-V storage max io limit");

static int hv_storvsc_chan_cnt = 0;
SYSCTL_INT(_hw_storvsc, OID_AUTO, chan_cnt, CTLFLAG_RDTUN,
	&hv_storvsc_chan_cnt, 0, "# of channels to use");

#define STORVSC_MAX_IO						\
	vmbus_chan_prplist_nelem(hv_storvsc_ringbuffer_size,	\
	   STORVSC_DATA_SEGCNT_MAX, VSTOR_PKT_SIZE)

struct hv_storvsc_sysctl {
	u_long		data_bio_cnt;
	u_long		data_vaddr_cnt;
	u_long		data_sg_cnt;
	u_long		chan_send_cnt[MAXCPU];
};

struct storvsc_gpa_range {
	struct vmbus_gpa_range	gpa_range;
	uint64_t		gpa_page[STORVSC_DATA_SEGCNT_MAX];
} __packed;

struct hv_storvsc_request {
	LIST_ENTRY(hv_storvsc_request)	link;
	struct vstor_packet		vstor_packet;
	int				prp_cnt;
	struct storvsc_gpa_range	prp_list;
	void				*sense_data;
	uint8_t				sense_info_len;
	uint8_t				retries;
	union ccb			*ccb;
	struct storvsc_softc		*softc;
	struct callout			callout;
	struct sema			synch_sema; /*Synchronize the request/response if needed */
	struct sglist			*bounce_sgl;
	unsigned int			bounce_sgl_count;
	uint64_t			not_aligned_seg_bits;
	bus_dmamap_t			data_dmap;
};

struct storvsc_softc {
	struct vmbus_channel		*hs_chan;
	LIST_HEAD(, hv_storvsc_request)	hs_free_list;
	struct mtx			hs_lock;
	struct storvsc_driver_props	*hs_drv_props;
	int 				hs_unit;
	uint32_t			hs_frozen;
	struct cam_sim			*hs_sim;
	struct cam_path 		*hs_path;
	uint32_t			hs_num_out_reqs;
	boolean_t			hs_destroy;
	boolean_t			hs_drain_notify;
	struct sema 			hs_drain_sema;	
	struct hv_storvsc_request	hs_init_req;
	struct hv_storvsc_request	hs_reset_req;
	device_t			hs_dev;
	bus_dma_tag_t			storvsc_req_dtag;
	struct hv_storvsc_sysctl	sysctl_data;
	uint32_t			hs_nchan;
	struct vmbus_channel		*hs_sel_chan[MAXCPU];
};

static eventhandler_tag storvsc_handler_tag;
/*
 * The size of the vmscsi_request has changed in win8. The
 * additional size is for the newly added elements in the
 * structure. These elements are valid only when we are talking
 * to a win8 host.
 * Track the correct size we need to apply.
 */
static int vmscsi_size_delta = sizeof(struct vmscsi_win8_extension);

/**
 * HyperV storvsc timeout testing cases:
 * a. IO returned after first timeout;
 * b. IO returned after second timeout and queue freeze;
 * c. IO returned while timer handler is running
 * The first can be tested by "sg_senddiag -vv /dev/daX",
 * and the second and third can be done by
 * "sg_wr_mode -v -p 08 -c 0,1a -m 0,ff /dev/daX".
 */
#define HVS_TIMEOUT_TEST 0

/*
 * Bus/adapter reset functionality on the Hyper-V host is
 * buggy and it will be disabled until
 * it can be further tested.
 */
#define HVS_HOST_RESET 0

struct storvsc_driver_props {
	char		*drv_name;
	char		*drv_desc;
	uint8_t		drv_max_luns_per_target;
	uint32_t	drv_max_ios_per_target;
	uint32_t	drv_ringbuffer_size;
};

enum hv_storage_type {
	DRIVER_BLKVSC,
	DRIVER_STORVSC,
	DRIVER_UNKNOWN
};

#define HS_MAX_ADAPTERS 10

#define HV_STORAGE_SUPPORTS_MULTI_CHANNEL 0x1

/* {ba6163d9-04a1-4d29-b605-72e2ffb1dc7f} */
static const struct hyperv_guid gStorVscDeviceType={
	.hv_guid = {0xd9, 0x63, 0x61, 0xba, 0xa1, 0x04, 0x29, 0x4d,
		 0xb6, 0x05, 0x72, 0xe2, 0xff, 0xb1, 0xdc, 0x7f}
};

/* {32412632-86cb-44a2-9b5c-50d1417354f5} */
static const struct hyperv_guid gBlkVscDeviceType={
	.hv_guid = {0x32, 0x26, 0x41, 0x32, 0xcb, 0x86, 0xa2, 0x44,
		 0x9b, 0x5c, 0x50, 0xd1, 0x41, 0x73, 0x54, 0xf5}
};

static struct storvsc_driver_props g_drv_props_table[] = {
	{"blkvsc", "Hyper-V IDE",
	 BLKVSC_MAX_IDE_DISKS_PER_TARGET, BLKVSC_MAX_IO_REQUESTS,
	 20*PAGE_SIZE},
	{"storvsc", "Hyper-V SCSI",
	 STORVSC_MAX_LUNS_PER_TARGET, STORVSC_MAX_IO_REQUESTS,
	 20*PAGE_SIZE}
};

/*
 * Sense buffer size changed in win8; have a run-time
 * variable to track the size we should use.
 */
static int sense_buffer_size = PRE_WIN8_STORVSC_SENSE_BUFFER_SIZE;

/*
 * The storage protocol version is determined during the
 * initial exchange with the host.  It will indicate which
 * storage functionality is available in the host.
*/
static int vmstor_proto_version;

struct vmstor_proto {
        int proto_version;
        int sense_buffer_size;
        int vmscsi_size_delta;
};

static const struct vmstor_proto vmstor_proto_list[] = {
        {
                VMSTOR_PROTOCOL_VERSION_WIN10,
                POST_WIN7_STORVSC_SENSE_BUFFER_SIZE,
                0
        },
        {
                VMSTOR_PROTOCOL_VERSION_WIN8_1,
                POST_WIN7_STORVSC_SENSE_BUFFER_SIZE,
                0
        },
        {
                VMSTOR_PROTOCOL_VERSION_WIN8,
                POST_WIN7_STORVSC_SENSE_BUFFER_SIZE,
                0
        },
        {
                VMSTOR_PROTOCOL_VERSION_WIN7,
                PRE_WIN8_STORVSC_SENSE_BUFFER_SIZE,
                sizeof(struct vmscsi_win8_extension),
        },
        {
                VMSTOR_PROTOCOL_VERSION_WIN6,
                PRE_WIN8_STORVSC_SENSE_BUFFER_SIZE,
                sizeof(struct vmscsi_win8_extension),
        }
};

/* static functions */
static int storvsc_probe(device_t dev);
static int storvsc_attach(device_t dev);
static int storvsc_detach(device_t dev);
static void storvsc_poll(struct cam_sim * sim);
static void storvsc_action(struct cam_sim * sim, union ccb * ccb);
static int create_storvsc_request(union ccb *ccb, struct hv_storvsc_request *reqp);
static void storvsc_free_request(struct storvsc_softc *sc, struct hv_storvsc_request *reqp);
static enum hv_storage_type storvsc_get_storage_type(device_t dev);
static void hv_storvsc_rescan_target(struct storvsc_softc *sc);
static void hv_storvsc_on_channel_callback(struct vmbus_channel *chan, void *xsc);
static void hv_storvsc_on_iocompletion( struct storvsc_softc *sc,
					struct vstor_packet *vstor_packet,
					struct hv_storvsc_request *request);
static int hv_storvsc_connect_vsp(struct storvsc_softc *);
static void storvsc_io_done(struct hv_storvsc_request *reqp);
static void storvsc_copy_sgl_to_bounce_buf(struct sglist *bounce_sgl,
				bus_dma_segment_t *orig_sgl,
				unsigned int orig_sgl_count,
				uint64_t seg_bits);
void storvsc_copy_from_bounce_buf_to_sgl(bus_dma_segment_t *dest_sgl,
				unsigned int dest_sgl_count,
				struct sglist* src_sgl,
				uint64_t seg_bits);

static device_method_t storvsc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		storvsc_probe),
	DEVMETHOD(device_attach,	storvsc_attach),
	DEVMETHOD(device_detach,	storvsc_detach),
	DEVMETHOD(device_shutdown,      bus_generic_shutdown),
	DEVMETHOD_END
};

static driver_t storvsc_driver = {
	"storvsc", storvsc_methods, sizeof(struct storvsc_softc),
};

static devclass_t storvsc_devclass;
DRIVER_MODULE(storvsc, vmbus, storvsc_driver, storvsc_devclass, 0, 0);
MODULE_VERSION(storvsc, 1);
MODULE_DEPEND(storvsc, vmbus, 1, 1, 1);

static void
storvsc_subchan_attach(struct storvsc_softc *sc,
    struct vmbus_channel *new_channel)
{
	struct vmstor_chan_props props;
	int ret = 0;

	memset(&props, 0, sizeof(props));

	vmbus_chan_cpu_rr(new_channel);
	ret = vmbus_chan_open(new_channel,
	    sc->hs_drv_props->drv_ringbuffer_size,
  	    sc->hs_drv_props->drv_ringbuffer_size,
	    (void *)&props,
	    sizeof(struct vmstor_chan_props),
	    hv_storvsc_on_channel_callback, sc);
}

/**
 * @brief Send multi-channel creation request to host
 *
 * @param device  a Hyper-V device pointer
 * @param max_chans  the max channels supported by vmbus
 */
static void
storvsc_send_multichannel_request(struct storvsc_softc *sc, int max_subch)
{
	struct vmbus_channel **subchan;
	struct hv_storvsc_request *request;
	struct vstor_packet *vstor_packet;	
	int request_subch;
	int ret, i;

	/* get sub-channel count that need to create */
	request_subch = MIN(max_subch, mp_ncpus - 1);

	request = &sc->hs_init_req;

	/* request the host to create multi-channel */
	memset(request, 0, sizeof(struct hv_storvsc_request));
	
	sema_init(&request->synch_sema, 0, ("stor_synch_sema"));

	vstor_packet = &request->vstor_packet;
	
	vstor_packet->operation = VSTOR_OPERATION_CREATE_MULTI_CHANNELS;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;
	vstor_packet->u.multi_channels_cnt = request_subch;

	ret = vmbus_chan_send(sc->hs_chan,
	    VMBUS_CHANPKT_TYPE_INBAND, VMBUS_CHANPKT_FLAG_RC,
	    vstor_packet, VSTOR_PKT_SIZE, (uint64_t)(uintptr_t)request);

	sema_wait(&request->synch_sema);

	if (vstor_packet->operation != VSTOR_OPERATION_COMPLETEIO ||
	    vstor_packet->status != 0) {		
		printf("Storvsc_error: create multi-channel invalid operation "
		    "(%d) or statue (%u)\n",
		    vstor_packet->operation, vstor_packet->status);
		return;
	}

	/* Update channel count */
	sc->hs_nchan = request_subch + 1;

	/* Wait for sub-channels setup to complete. */
	subchan = vmbus_subchan_get(sc->hs_chan, request_subch);

	/* Attach the sub-channels. */
	for (i = 0; i < request_subch; ++i)
		storvsc_subchan_attach(sc, subchan[i]);

	/* Release the sub-channels. */
	vmbus_subchan_rel(subchan, request_subch);

	if (bootverbose)
		printf("Storvsc create multi-channel success!\n");
}

/**
 * @brief initialize channel connection to parent partition
 *
 * @param dev  a Hyper-V device pointer
 * @returns  0 on success, non-zero error on failure
 */
static int
hv_storvsc_channel_init(struct storvsc_softc *sc)
{
	int ret = 0, i;
	struct hv_storvsc_request *request;
	struct vstor_packet *vstor_packet;
	uint16_t max_subch;
	boolean_t support_multichannel;
	uint32_t version;

	max_subch = 0;
	support_multichannel = FALSE;

	request = &sc->hs_init_req;
	memset(request, 0, sizeof(struct hv_storvsc_request));
	vstor_packet = &request->vstor_packet;
	request->softc = sc;

	/**
	 * Initiate the vsc/vsp initialization protocol on the open channel
	 */
	sema_init(&request->synch_sema, 0, ("stor_synch_sema"));

	vstor_packet->operation = VSTOR_OPERATION_BEGININITIALIZATION;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;


	ret = vmbus_chan_send(sc->hs_chan,
	    VMBUS_CHANPKT_TYPE_INBAND, VMBUS_CHANPKT_FLAG_RC,
	    vstor_packet, VSTOR_PKT_SIZE, (uint64_t)(uintptr_t)request);

	if (ret != 0)
		goto cleanup;

	sema_wait(&request->synch_sema);

	if (vstor_packet->operation != VSTOR_OPERATION_COMPLETEIO ||
		vstor_packet->status != 0) {
		goto cleanup;
	}

	for (i = 0; i < nitems(vmstor_proto_list); i++) {
		/* reuse the packet for version range supported */

		memset(vstor_packet, 0, sizeof(struct vstor_packet));
		vstor_packet->operation = VSTOR_OPERATION_QUERYPROTOCOLVERSION;
		vstor_packet->flags = REQUEST_COMPLETION_FLAG;

		vstor_packet->u.version.major_minor =
			vmstor_proto_list[i].proto_version;

		/* revision is only significant for Windows guests */
		vstor_packet->u.version.revision = 0;

		ret = vmbus_chan_send(sc->hs_chan,
		    VMBUS_CHANPKT_TYPE_INBAND, VMBUS_CHANPKT_FLAG_RC,
		    vstor_packet, VSTOR_PKT_SIZE, (uint64_t)(uintptr_t)request);

		if (ret != 0)
			goto cleanup;

		sema_wait(&request->synch_sema);

		if (vstor_packet->operation != VSTOR_OPERATION_COMPLETEIO) {
			ret = EINVAL;
			goto cleanup;	
		}
		if (vstor_packet->status == 0) {
			vmstor_proto_version =
				vmstor_proto_list[i].proto_version;
			sense_buffer_size =
				vmstor_proto_list[i].sense_buffer_size;
			vmscsi_size_delta =
				vmstor_proto_list[i].vmscsi_size_delta;
			break;
		}
	}

	if (vstor_packet->status != 0) {
		ret = EINVAL;
		goto cleanup;
	}
	/**
	 * Query channel properties
	 */
	memset(vstor_packet, 0, sizeof(struct vstor_packet));
	vstor_packet->operation = VSTOR_OPERATION_QUERYPROPERTIES;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;

	ret = vmbus_chan_send(sc->hs_chan,
	    VMBUS_CHANPKT_TYPE_INBAND, VMBUS_CHANPKT_FLAG_RC,
	    vstor_packet, VSTOR_PKT_SIZE, (uint64_t)(uintptr_t)request);

	if ( ret != 0)
		goto cleanup;

	sema_wait(&request->synch_sema);

	/* TODO: Check returned version */
	if (vstor_packet->operation != VSTOR_OPERATION_COMPLETEIO ||
	    vstor_packet->status != 0) {
		goto cleanup;
	}

	max_subch = vstor_packet->u.chan_props.max_channel_cnt;
	if (hv_storvsc_chan_cnt > 0 && hv_storvsc_chan_cnt < (max_subch + 1))
		max_subch = hv_storvsc_chan_cnt - 1;

	/* multi-channels feature is supported by WIN8 and above version */
	version = VMBUS_GET_VERSION(device_get_parent(sc->hs_dev), sc->hs_dev);
	if (version != VMBUS_VERSION_WIN7 && version != VMBUS_VERSION_WS2008 &&
	    (vstor_packet->u.chan_props.flags &
	     HV_STORAGE_SUPPORTS_MULTI_CHANNEL)) {
		support_multichannel = TRUE;
	}
	if (bootverbose) {
		device_printf(sc->hs_dev, "max chans %d%s\n", max_subch + 1,
		    support_multichannel ? ", multi-chan capable" : "");
	}

	memset(vstor_packet, 0, sizeof(struct vstor_packet));
	vstor_packet->operation = VSTOR_OPERATION_ENDINITIALIZATION;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;

	ret = vmbus_chan_send(sc->hs_chan,
	    VMBUS_CHANPKT_TYPE_INBAND, VMBUS_CHANPKT_FLAG_RC,
	    vstor_packet, VSTOR_PKT_SIZE, (uint64_t)(uintptr_t)request);

	if (ret != 0) {
		goto cleanup;
	}

	sema_wait(&request->synch_sema);

	if (vstor_packet->operation != VSTOR_OPERATION_COMPLETEIO ||
	    vstor_packet->status != 0)
		goto cleanup;

	/*
	 * If multi-channel is supported, send multichannel create
	 * request to host.
	 */
	if (support_multichannel && max_subch > 0)
		storvsc_send_multichannel_request(sc, max_subch);
cleanup:
	sema_destroy(&request->synch_sema);
	return (ret);
}

/**
 * @brief Open channel connection to paraent partition StorVSP driver
 *
 * Open and initialize channel connection to parent partition StorVSP driver.
 *
 * @param pointer to a Hyper-V device
 * @returns 0 on success, non-zero error on failure
 */
static int
hv_storvsc_connect_vsp(struct storvsc_softc *sc)
{	
	int ret = 0;
	struct vmstor_chan_props props;

	memset(&props, 0, sizeof(struct vmstor_chan_props));

	/*
	 * Open the channel
	 */
	vmbus_chan_cpu_rr(sc->hs_chan);
	ret = vmbus_chan_open(
		sc->hs_chan,
		sc->hs_drv_props->drv_ringbuffer_size,
		sc->hs_drv_props->drv_ringbuffer_size,
		(void *)&props,
		sizeof(struct vmstor_chan_props),
		hv_storvsc_on_channel_callback, sc);

	if (ret != 0) {
		return ret;
	}

	ret = hv_storvsc_channel_init(sc);
	return (ret);
}

#if HVS_HOST_RESET
static int
hv_storvsc_host_reset(struct storvsc_softc *sc)
{
	int ret = 0;

	struct hv_storvsc_request *request;
	struct vstor_packet *vstor_packet;

	request = &sc->hs_reset_req;
	request->softc = sc;
	vstor_packet = &request->vstor_packet;

	sema_init(&request->synch_sema, 0, "stor synch sema");

	vstor_packet->operation = VSTOR_OPERATION_RESETBUS;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;

	ret = vmbus_chan_send(dev->channel,
	    VMBUS_CHANPKT_TYPE_INBAND, VMBUS_CHANPKT_FLAG_RC,
	    vstor_packet, VSTOR_PKT_SIZE,
	    (uint64_t)(uintptr_t)&sc->hs_reset_req);

	if (ret != 0) {
		goto cleanup;
	}

	sema_wait(&request->synch_sema);

	/*
	 * At this point, all outstanding requests in the adapter
	 * should have been flushed out and return to us
	 */

cleanup:
	sema_destroy(&request->synch_sema);
	return (ret);
}
#endif /* HVS_HOST_RESET */

/**
 * @brief Function to initiate an I/O request
 *
 * @param device Hyper-V device pointer
 * @param request pointer to a request structure
 * @returns 0 on success, non-zero error on failure
 */
static int
hv_storvsc_io_request(struct storvsc_softc *sc,
					  struct hv_storvsc_request *request)
{
	struct vstor_packet *vstor_packet = &request->vstor_packet;
	struct vmbus_channel* outgoing_channel = NULL;
	int ret = 0, ch_sel;

	vstor_packet->flags |= REQUEST_COMPLETION_FLAG;

	vstor_packet->u.vm_srb.length =
	    sizeof(struct vmscsi_req) - vmscsi_size_delta;
	
	vstor_packet->u.vm_srb.sense_info_len = sense_buffer_size;

	vstor_packet->u.vm_srb.transfer_len =
	    request->prp_list.gpa_range.gpa_len;

	vstor_packet->operation = VSTOR_OPERATION_EXECUTESRB;

	ch_sel = (vstor_packet->u.vm_srb.lun + curcpu) % sc->hs_nchan;
	outgoing_channel = sc->hs_sel_chan[ch_sel];

	mtx_unlock(&request->softc->hs_lock);
	if (request->prp_list.gpa_range.gpa_len) {
		ret = vmbus_chan_send_prplist(outgoing_channel,
		    &request->prp_list.gpa_range, request->prp_cnt,
		    vstor_packet, VSTOR_PKT_SIZE, (uint64_t)(uintptr_t)request);
	} else {
		ret = vmbus_chan_send(outgoing_channel,
		    VMBUS_CHANPKT_TYPE_INBAND, VMBUS_CHANPKT_FLAG_RC,
		    vstor_packet, VSTOR_PKT_SIZE, (uint64_t)(uintptr_t)request);
	}
	/* statistic for successful request sending on each channel */
	if (!ret) {
		sc->sysctl_data.chan_send_cnt[ch_sel]++;
	}
	mtx_lock(&request->softc->hs_lock);

	if (ret != 0) {
		printf("Unable to send packet %p ret %d", vstor_packet, ret);
	} else {
		atomic_add_int(&sc->hs_num_out_reqs, 1);
	}

	return (ret);
}


/**
 * Process IO_COMPLETION_OPERATION and ready
 * the result to be completed for upper layer
 * processing by the CAM layer.
 */
static void
hv_storvsc_on_iocompletion(struct storvsc_softc *sc,
			   struct vstor_packet *vstor_packet,
			   struct hv_storvsc_request *request)
{
	struct vmscsi_req *vm_srb;

	vm_srb = &vstor_packet->u.vm_srb;

	/*
	 * Copy some fields of the host's response into the request structure,
	 * because the fields will be used later in storvsc_io_done().
	 */
	request->vstor_packet.u.vm_srb.scsi_status = vm_srb->scsi_status;
	request->vstor_packet.u.vm_srb.srb_status = vm_srb->srb_status;
	request->vstor_packet.u.vm_srb.transfer_len = vm_srb->transfer_len;

	if (((vm_srb->scsi_status & 0xFF) == SCSI_STATUS_CHECK_COND) &&
			(vm_srb->srb_status & SRB_STATUS_AUTOSENSE_VALID)) {
		/* Autosense data available */

		KASSERT(vm_srb->sense_info_len <= request->sense_info_len,
				("vm_srb->sense_info_len <= "
				 "request->sense_info_len"));

		memcpy(request->sense_data, vm_srb->u.sense_data,
			vm_srb->sense_info_len);

		request->sense_info_len = vm_srb->sense_info_len;
	}

	/* Complete request by passing to the CAM layer */
	storvsc_io_done(request);
	atomic_subtract_int(&sc->hs_num_out_reqs, 1);
	if (sc->hs_drain_notify && (sc->hs_num_out_reqs == 0)) {
		sema_post(&sc->hs_drain_sema);
	}
}

static void
hv_storvsc_rescan_target(struct storvsc_softc *sc)
{
	path_id_t pathid;
	target_id_t targetid;
	union ccb *ccb;

	pathid = cam_sim_path(sc->hs_sim);
	targetid = CAM_TARGET_WILDCARD;

	/*
	 * Allocate a CCB and schedule a rescan.
	 */
	ccb = xpt_alloc_ccb_nowait();
	if (ccb == NULL) {
		printf("unable to alloc CCB for rescan\n");
		return;
	}

	if (xpt_create_path(&ccb->ccb_h.path, NULL, pathid, targetid,
	    CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		printf("unable to create path for rescan, pathid: %u,"
		    "targetid: %u\n", pathid, targetid);
		xpt_free_ccb(ccb);
		return;
	}

	if (targetid == CAM_TARGET_WILDCARD)
		ccb->ccb_h.func_code = XPT_SCAN_BUS;
	else
		ccb->ccb_h.func_code = XPT_SCAN_TGT;

	xpt_rescan(ccb);
}

static void
hv_storvsc_on_channel_callback(struct vmbus_channel *channel, void *xsc)
{
	int ret = 0;
	struct storvsc_softc *sc = xsc;
	uint32_t bytes_recvd;
	uint64_t request_id;
	uint8_t packet[roundup2(sizeof(struct vstor_packet), 8)];
	struct hv_storvsc_request *request;
	struct vstor_packet *vstor_packet;

	bytes_recvd = roundup2(VSTOR_PKT_SIZE, 8);
	ret = vmbus_chan_recv(channel, packet, &bytes_recvd, &request_id);
	KASSERT(ret != ENOBUFS, ("storvsc recvbuf is not large enough"));
	/* XXX check bytes_recvd to make sure that it contains enough data */

	while ((ret == 0) && (bytes_recvd > 0)) {
		request = (struct hv_storvsc_request *)(uintptr_t)request_id;

		if ((request == &sc->hs_init_req) ||
			(request == &sc->hs_reset_req)) {
			memcpy(&request->vstor_packet, packet,
				   sizeof(struct vstor_packet));
			sema_post(&request->synch_sema);
		} else {
			vstor_packet = (struct vstor_packet *)packet;
			switch(vstor_packet->operation) {
			case VSTOR_OPERATION_COMPLETEIO:
				if (request == NULL)
					panic("VMBUS: storvsc received a "
					    "packet with NULL request id in "
					    "COMPLETEIO operation.");

				hv_storvsc_on_iocompletion(sc,
							vstor_packet, request);
				break;
			case VSTOR_OPERATION_REMOVEDEVICE:
				printf("VMBUS: storvsc operation %d not "
				    "implemented.\n", vstor_packet->operation);
				/* TODO: implement */
				break;
			case VSTOR_OPERATION_ENUMERATE_BUS:
				hv_storvsc_rescan_target(sc);
				break;
			default:
				break;
			}			
		}

		bytes_recvd = roundup2(VSTOR_PKT_SIZE, 8),
		ret = vmbus_chan_recv(channel, packet, &bytes_recvd,
		    &request_id);
		KASSERT(ret != ENOBUFS,
		    ("storvsc recvbuf is not large enough"));
		/*
		 * XXX check bytes_recvd to make sure that it contains
		 * enough data
		 */
	}
}

/**
 * @brief StorVSC probe function
 *
 * Device probe function.  Returns 0 if the input device is a StorVSC
 * device.  Otherwise, a ENXIO is returned.  If the input device is
 * for BlkVSC (paravirtual IDE) device and this support is disabled in
 * favor of the emulated ATA/IDE device, return ENXIO.
 *
 * @param a device
 * @returns 0 on success, ENXIO if not a matcing StorVSC device
 */
static int
storvsc_probe(device_t dev)
{
	int ret	= ENXIO;
	
	switch (storvsc_get_storage_type(dev)) {
	case DRIVER_BLKVSC:
		if(bootverbose)
			device_printf(dev,
			    "Enlightened ATA/IDE detected\n");
		device_set_desc(dev, g_drv_props_table[DRIVER_BLKVSC].drv_desc);
		ret = BUS_PROBE_DEFAULT;
		break;
	case DRIVER_STORVSC:
		if(bootverbose)
			device_printf(dev, "Enlightened SCSI device detected\n");
		device_set_desc(dev, g_drv_props_table[DRIVER_STORVSC].drv_desc);
		ret = BUS_PROBE_DEFAULT;
		break;
	default:
		ret = ENXIO;
	}
	return (ret);
}

static void
storvsc_create_chan_sel(struct storvsc_softc *sc)
{
	struct vmbus_channel **subch;
	int i, nsubch;

	sc->hs_sel_chan[0] = sc->hs_chan;
	nsubch = sc->hs_nchan - 1;
	if (nsubch == 0)
		return;

	subch = vmbus_subchan_get(sc->hs_chan, nsubch);
	for (i = 0; i < nsubch; i++)
		sc->hs_sel_chan[i + 1] = subch[i];
	vmbus_subchan_rel(subch, nsubch);
}

static int
storvsc_init_requests(device_t dev)
{
	struct storvsc_softc *sc = device_get_softc(dev);
	struct hv_storvsc_request *reqp;
	int error, i;

	LIST_INIT(&sc->hs_free_list);

	error = bus_dma_tag_create(
		bus_get_dma_tag(dev),		/* parent */
		1,				/* alignment */
		PAGE_SIZE,			/* boundary */
		BUS_SPACE_MAXADDR,		/* lowaddr */
		BUS_SPACE_MAXADDR,		/* highaddr */
		NULL, NULL,			/* filter, filterarg */
		STORVSC_DATA_SIZE_MAX,		/* maxsize */
		STORVSC_DATA_SEGCNT_MAX,	/* nsegments */
		STORVSC_DATA_SEGSZ_MAX,		/* maxsegsize */
		0,				/* flags */
		NULL,				/* lockfunc */
		NULL,				/* lockfuncarg */
		&sc->storvsc_req_dtag);
	if (error) {
		device_printf(dev, "failed to create storvsc dma tag\n");
		return (error);
	}

	for (i = 0; i < sc->hs_drv_props->drv_max_ios_per_target; ++i) {
		reqp = malloc(sizeof(struct hv_storvsc_request),
				 M_DEVBUF, M_WAITOK|M_ZERO);
		reqp->softc = sc;
		error = bus_dmamap_create(sc->storvsc_req_dtag, 0,
				&reqp->data_dmap);
		if (error) {
			device_printf(dev, "failed to allocate storvsc "
			    "data dmamap\n");
			goto cleanup;
		}
		LIST_INSERT_HEAD(&sc->hs_free_list, reqp, link);
	}
	return (0);

cleanup:
	while ((reqp = LIST_FIRST(&sc->hs_free_list)) != NULL) {
		LIST_REMOVE(reqp, link);
		bus_dmamap_destroy(sc->storvsc_req_dtag, reqp->data_dmap);
		free(reqp, M_DEVBUF);
	}
	return (error);
}

static void
storvsc_sysctl(device_t dev)
{
	struct sysctl_oid_list *child;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *ch_tree, *chid_tree;
	struct storvsc_softc *sc;
	char name[16];
	int i;

	sc = device_get_softc(dev);
	ctx = device_get_sysctl_ctx(dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));

	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "data_bio_cnt", CTLFLAG_RW,
		&sc->sysctl_data.data_bio_cnt, "# of bio data block");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "data_vaddr_cnt", CTLFLAG_RW,
		&sc->sysctl_data.data_vaddr_cnt, "# of vaddr data block");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "data_sg_cnt", CTLFLAG_RW,
		&sc->sysctl_data.data_sg_cnt, "# of sg data block");

	/* dev.storvsc.UNIT.channel */
	ch_tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "channel",
		CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "");
	if (ch_tree == NULL)
		return;

	for (i = 0; i < sc->hs_nchan; i++) {
		uint32_t ch_id;

		ch_id = vmbus_chan_id(sc->hs_sel_chan[i]);
		snprintf(name, sizeof(name), "%d", ch_id);
		/* dev.storvsc.UNIT.channel.CHID */
		chid_tree = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(ch_tree),
			OID_AUTO, name, CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "");
		if (chid_tree == NULL)
			return;
		/* dev.storvsc.UNIT.channel.CHID.send_req */
		SYSCTL_ADD_ULONG(ctx, SYSCTL_CHILDREN(chid_tree), OID_AUTO,
			"send_req", CTLFLAG_RD, &sc->sysctl_data.chan_send_cnt[i],
			"# of request sending from this channel");
	}
}

/**
 * @brief StorVSC attach function
 *
 * Function responsible for allocating per-device structures,
 * setting up CAM interfaces and scanning for available LUNs to
 * be used for SCSI device peripherals.
 *
 * @param a device
 * @returns 0 on success or an error on failure
 */
static int
storvsc_attach(device_t dev)
{
	enum hv_storage_type stor_type;
	struct storvsc_softc *sc;
	struct cam_devq *devq;
	int ret, i, j;
	struct hv_storvsc_request *reqp;
	struct root_hold_token *root_mount_token = NULL;
	struct hv_sgl_node *sgl_node = NULL;
	void *tmp_buff = NULL;

	/*
	 * We need to serialize storvsc attach calls.
	 */
	root_mount_token = root_mount_hold("storvsc");

	sc = device_get_softc(dev);
	sc->hs_nchan = 1;
	sc->hs_chan = vmbus_get_channel(dev);

	stor_type = storvsc_get_storage_type(dev);

	if (stor_type == DRIVER_UNKNOWN) {
		ret = ENODEV;
		goto cleanup;
	}

	/* fill in driver specific properties */
	sc->hs_drv_props = &g_drv_props_table[stor_type];
	sc->hs_drv_props->drv_ringbuffer_size = hv_storvsc_ringbuffer_size;
	sc->hs_drv_props->drv_max_ios_per_target =
		MIN(STORVSC_MAX_IO, hv_storvsc_max_io);
	if (bootverbose) {
		printf("storvsc ringbuffer size: %d, max_io: %d\n",
			sc->hs_drv_props->drv_ringbuffer_size,
			sc->hs_drv_props->drv_max_ios_per_target);
	}
	/* fill in device specific properties */
	sc->hs_unit	= device_get_unit(dev);
	sc->hs_dev	= dev;

	mtx_init(&sc->hs_lock, "hvslck", NULL, MTX_DEF);

	ret = storvsc_init_requests(dev);
	if (ret != 0)
		goto cleanup;

	/* create sg-list page pool */
	if (FALSE == g_hv_sgl_page_pool.is_init) {
		g_hv_sgl_page_pool.is_init = TRUE;
		LIST_INIT(&g_hv_sgl_page_pool.in_use_sgl_list);
		LIST_INIT(&g_hv_sgl_page_pool.free_sgl_list);

		/*
		 * Pre-create SG list, each SG list with
		 * STORVSC_DATA_SEGCNT_MAX segments, each
		 * segment has one page buffer
		 */
		for (i = 0; i < sc->hs_drv_props->drv_max_ios_per_target; i++) {
	        	sgl_node = malloc(sizeof(struct hv_sgl_node),
			    M_DEVBUF, M_WAITOK|M_ZERO);

			sgl_node->sgl_data =
			    sglist_alloc(STORVSC_DATA_SEGCNT_MAX,
			    M_WAITOK|M_ZERO);

			for (j = 0; j < STORVSC_DATA_SEGCNT_MAX; j++) {
				tmp_buff = malloc(PAGE_SIZE,
				    M_DEVBUF, M_WAITOK|M_ZERO);

				sgl_node->sgl_data->sg_segs[j].ss_paddr =
				    (vm_paddr_t)tmp_buff;
			}

			LIST_INSERT_HEAD(&g_hv_sgl_page_pool.free_sgl_list,
			    sgl_node, link);
		}
	}

	sc->hs_destroy = FALSE;
	sc->hs_drain_notify = FALSE;
	sema_init(&sc->hs_drain_sema, 0, "Store Drain Sema");

	ret = hv_storvsc_connect_vsp(sc);
	if (ret != 0) {
		goto cleanup;
	}

	/* Construct cpu to channel mapping */
	storvsc_create_chan_sel(sc);

	/*
	 * Create the device queue.
	 * Hyper-V maps each target to one SCSI HBA
	 */
	devq = cam_simq_alloc(sc->hs_drv_props->drv_max_ios_per_target);
	if (devq == NULL) {
		device_printf(dev, "Failed to alloc device queue\n");
		ret = ENOMEM;
		goto cleanup;
	}

	sc->hs_sim = cam_sim_alloc(storvsc_action,
				storvsc_poll,
				sc->hs_drv_props->drv_name,
				sc,
				sc->hs_unit,
				&sc->hs_lock, 1,
				sc->hs_drv_props->drv_max_ios_per_target,
				devq);

	if (sc->hs_sim == NULL) {
		device_printf(dev, "Failed to alloc sim\n");
		cam_simq_free(devq);
		ret = ENOMEM;
		goto cleanup;
	}

	mtx_lock(&sc->hs_lock);
	/* bus_id is set to 0, need to get it from VMBUS channel query? */
	if (xpt_bus_register(sc->hs_sim, dev, 0) != CAM_SUCCESS) {
		cam_sim_free(sc->hs_sim, /*free_devq*/TRUE);
		mtx_unlock(&sc->hs_lock);
		device_printf(dev, "Unable to register SCSI bus\n");
		ret = ENXIO;
		goto cleanup;
	}

	if (xpt_create_path(&sc->hs_path, /*periph*/NULL,
		 cam_sim_path(sc->hs_sim),
		CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_bus_deregister(cam_sim_path(sc->hs_sim));
		cam_sim_free(sc->hs_sim, /*free_devq*/TRUE);
		mtx_unlock(&sc->hs_lock);
		device_printf(dev, "Unable to create path\n");
		ret = ENXIO;
		goto cleanup;
	}

	mtx_unlock(&sc->hs_lock);

	storvsc_sysctl(dev);

	root_mount_rel(root_mount_token);
	return (0);


cleanup:
	root_mount_rel(root_mount_token);
	while (!LIST_EMPTY(&sc->hs_free_list)) {
		reqp = LIST_FIRST(&sc->hs_free_list);
		LIST_REMOVE(reqp, link);
		bus_dmamap_destroy(sc->storvsc_req_dtag, reqp->data_dmap);
		free(reqp, M_DEVBUF);
	}

	while (!LIST_EMPTY(&g_hv_sgl_page_pool.free_sgl_list)) {
		sgl_node = LIST_FIRST(&g_hv_sgl_page_pool.free_sgl_list);
		LIST_REMOVE(sgl_node, link);
		for (j = 0; j < STORVSC_DATA_SEGCNT_MAX; j++) {
			if (NULL !=
			    (void*)sgl_node->sgl_data->sg_segs[j].ss_paddr) {
				free((void*)sgl_node->sgl_data->sg_segs[j].ss_paddr, M_DEVBUF);
			}
		}
		sglist_free(sgl_node->sgl_data);
		free(sgl_node, M_DEVBUF);
	}

	return (ret);
}

/**
 * @brief StorVSC device detach function
 *
 * This function is responsible for safely detaching a
 * StorVSC device.  This includes waiting for inbound responses
 * to complete and freeing associated per-device structures.
 *
 * @param dev a device
 * returns 0 on success
 */
static int
storvsc_detach(device_t dev)
{
	struct storvsc_softc *sc = device_get_softc(dev);
	struct hv_storvsc_request *reqp = NULL;
	struct hv_sgl_node *sgl_node = NULL;
	int j = 0;

	sc->hs_destroy = TRUE;

	/*
	 * At this point, all outbound traffic should be disabled. We
	 * only allow inbound traffic (responses) to proceed so that
	 * outstanding requests can be completed.
	 */

	sc->hs_drain_notify = TRUE;
	sema_wait(&sc->hs_drain_sema);
	sc->hs_drain_notify = FALSE;

	/*
	 * Since we have already drained, we don't need to busy wait.
	 * The call to close the channel will reset the callback
	 * under the protection of the incoming channel lock.
	 */

	vmbus_chan_close(sc->hs_chan);

	mtx_lock(&sc->hs_lock);
	while (!LIST_EMPTY(&sc->hs_free_list)) {
		reqp = LIST_FIRST(&sc->hs_free_list);
		LIST_REMOVE(reqp, link);
		bus_dmamap_destroy(sc->storvsc_req_dtag, reqp->data_dmap);
		free(reqp, M_DEVBUF);
	}
	mtx_unlock(&sc->hs_lock);

	while (!LIST_EMPTY(&g_hv_sgl_page_pool.free_sgl_list)) {
		sgl_node = LIST_FIRST(&g_hv_sgl_page_pool.free_sgl_list);
		LIST_REMOVE(sgl_node, link);
		for (j = 0; j < STORVSC_DATA_SEGCNT_MAX; j++){
			if (NULL !=
			    (void*)sgl_node->sgl_data->sg_segs[j].ss_paddr) {
				free((void*)sgl_node->sgl_data->sg_segs[j].ss_paddr, M_DEVBUF);
			}
		}
		sglist_free(sgl_node->sgl_data);
		free(sgl_node, M_DEVBUF);
	}
	
	return (0);
}

#if HVS_TIMEOUT_TEST
/**
 * @brief unit test for timed out operations
 *
 * This function provides unit testing capability to simulate
 * timed out operations.  Recompilation with HV_TIMEOUT_TEST=1
 * is required.
 *
 * @param reqp pointer to a request structure
 * @param opcode SCSI operation being performed
 * @param wait if 1, wait for I/O to complete
 */
static void
storvsc_timeout_test(struct hv_storvsc_request *reqp,
		uint8_t opcode, int wait)
{
	int ret;
	union ccb *ccb = reqp->ccb;
	struct storvsc_softc *sc = reqp->softc;

	if (reqp->vstor_packet.vm_srb.cdb[0] != opcode) {
		return;
	}

	if (wait) {
		mtx_lock(&reqp->event.mtx);
	}
	ret = hv_storvsc_io_request(sc, reqp);
	if (ret != 0) {
		if (wait) {
			mtx_unlock(&reqp->event.mtx);
		}
		printf("%s: io_request failed with %d.\n",
				__func__, ret);
		ccb->ccb_h.status = CAM_PROVIDE_FAIL;
		mtx_lock(&sc->hs_lock);
		storvsc_free_request(sc, reqp);
		xpt_done(ccb);
		mtx_unlock(&sc->hs_lock);
		return;
	}

	if (wait) {
		xpt_print(ccb->ccb_h.path,
				"%u: %s: waiting for IO return.\n",
				ticks, __func__);
		ret = cv_timedwait(&reqp->event.cv, &reqp->event.mtx, 60*hz);
		mtx_unlock(&reqp->event.mtx);
		xpt_print(ccb->ccb_h.path, "%u: %s: %s.\n",
				ticks, __func__, (ret == 0)?
				"IO return detected" :
				"IO return not detected");
		/*
		 * Now both the timer handler and io done are running
		 * simultaneously. We want to confirm the io done always
		 * finishes after the timer handler exits. So reqp used by
		 * timer handler is not freed or stale. Do busy loop for
		 * another 1/10 second to make sure io done does
		 * wait for the timer handler to complete.
		 */
		DELAY(100*1000);
		mtx_lock(&sc->hs_lock);
		xpt_print(ccb->ccb_h.path,
				"%u: %s: finishing, queue frozen %d, "
				"ccb status 0x%x scsi_status 0x%x.\n",
				ticks, __func__, sc->hs_frozen,
				ccb->ccb_h.status,
				ccb->csio.scsi_status);
		mtx_unlock(&sc->hs_lock);
	}
}
#endif /* HVS_TIMEOUT_TEST */

#ifdef notyet
/**
 * @brief timeout handler for requests
 *
 * This function is called as a result of a callout expiring.
 *
 * @param arg pointer to a request
 */
static void
storvsc_timeout(void *arg)
{
	struct hv_storvsc_request *reqp = arg;
	struct storvsc_softc *sc = reqp->softc;
	union ccb *ccb = reqp->ccb;

	if (reqp->retries == 0) {
		mtx_lock(&sc->hs_lock);
		xpt_print(ccb->ccb_h.path,
		    "%u: IO timed out (req=0x%p), wait for another %u secs.\n",
		    ticks, reqp, ccb->ccb_h.timeout / 1000);
		cam_error_print(ccb, CAM_ESF_ALL, CAM_EPF_ALL);
		mtx_unlock(&sc->hs_lock);

		reqp->retries++;
		callout_reset_sbt(&reqp->callout, SBT_1MS * ccb->ccb_h.timeout,
		    0, storvsc_timeout, reqp, 0);
#if HVS_TIMEOUT_TEST
		storvsc_timeout_test(reqp, SEND_DIAGNOSTIC, 0);
#endif
		return;
	}

	mtx_lock(&sc->hs_lock);
	xpt_print(ccb->ccb_h.path,
		"%u: IO (reqp = 0x%p) did not return for %u seconds, %s.\n",
		ticks, reqp, ccb->ccb_h.timeout * (reqp->retries+1) / 1000,
		(sc->hs_frozen == 0)?
		"freezing the queue" : "the queue is already frozen");
	if (sc->hs_frozen == 0) {
		sc->hs_frozen = 1;
		xpt_freeze_simq(xpt_path_sim(ccb->ccb_h.path), 1);
	}
	mtx_unlock(&sc->hs_lock);
	
#if HVS_TIMEOUT_TEST
	storvsc_timeout_test(reqp, MODE_SELECT_10, 1);
#endif
}
#endif

/**
 * @brief StorVSC device poll function
 *
 * This function is responsible for servicing requests when
 * interrupts are disabled (i.e when we are dumping core.)
 *
 * @param sim a pointer to a CAM SCSI interface module
 */
static void
storvsc_poll(struct cam_sim *sim)
{
	struct storvsc_softc *sc = cam_sim_softc(sim);

	mtx_assert(&sc->hs_lock, MA_OWNED);
	mtx_unlock(&sc->hs_lock);
	hv_storvsc_on_channel_callback(sc->hs_chan, sc);
	mtx_lock(&sc->hs_lock);
}

/**
 * @brief StorVSC device action function
 *
 * This function is responsible for handling SCSI operations which
 * are passed from the CAM layer.  The requests are in the form of
 * CAM control blocks which indicate the action being performed.
 * Not all actions require converting the request to a VSCSI protocol
 * message - these actions can be responded to by this driver.
 * Requests which are destined for a backend storage device are converted
 * to a VSCSI protocol message and sent on the channel connection associated
 * with this device.
 *
 * @param sim pointer to a CAM SCSI interface module
 * @param ccb pointer to a CAM control block
 */
static void
storvsc_action(struct cam_sim *sim, union ccb *ccb)
{
	struct storvsc_softc *sc = cam_sim_softc(sim);
	int res;

	mtx_assert(&sc->hs_lock, MA_OWNED);
	switch (ccb->ccb_h.func_code) {
	case XPT_PATH_INQ: {
		struct ccb_pathinq *cpi = &ccb->cpi;

		cpi->version_num = 1;
		cpi->hba_inquiry = PI_TAG_ABLE|PI_SDTR_ABLE;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_NOBUSRESET;
		if (hv_storvsc_use_pim_unmapped)
			cpi->hba_misc |= PIM_UNMAPPED;
		cpi->maxio = STORVSC_DATA_SIZE_MAX;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = STORVSC_MAX_TARGETS;
		cpi->max_lun = sc->hs_drv_props->drv_max_luns_per_target;
		cpi->initiator_id = cpi->max_target;
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = 300000;
		cpi->transport = XPORT_SAS;
		cpi->transport_version = 0;
		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_SPC2;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, sc->hs_drv_props->drv_name, HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);

		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		return;
	}
	case XPT_GET_TRAN_SETTINGS: {
		struct  ccb_trans_settings *cts = &ccb->cts;

		cts->transport = XPORT_SAS;
		cts->transport_version = 0;
		cts->protocol = PROTO_SCSI;
		cts->protocol_version = SCSI_REV_SPC2;

		/* enable tag queuing and disconnected mode */
		cts->proto_specific.valid = CTS_SCSI_VALID_TQ;
		cts->proto_specific.scsi.valid = CTS_SCSI_VALID_TQ;
		cts->proto_specific.scsi.flags = CTS_SCSI_FLAGS_TAG_ENB;
		cts->xport_specific.valid = CTS_SPI_VALID_DISC;
		cts->xport_specific.spi.flags = CTS_SPI_FLAGS_DISC_ENB;
			
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		return;
	}
	case XPT_SET_TRAN_SETTINGS:	{
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		return;
	}
	case XPT_CALC_GEOMETRY:{
		cam_calc_geometry(&ccb->ccg, 1);
		xpt_done(ccb);
		return;
	}
	case  XPT_RESET_BUS:
	case  XPT_RESET_DEV:{
#if HVS_HOST_RESET
		if ((res = hv_storvsc_host_reset(sc)) != 0) {
			xpt_print(ccb->ccb_h.path,
				"hv_storvsc_host_reset failed with %d\n", res);
			ccb->ccb_h.status = CAM_PROVIDE_FAIL;
			xpt_done(ccb);
			return;
		}
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		return;
#else
		xpt_print(ccb->ccb_h.path,
				  "%s reset not supported.\n",
				  (ccb->ccb_h.func_code == XPT_RESET_BUS)?
				  "bus" : "dev");
		ccb->ccb_h.status = CAM_REQ_INVALID;
		xpt_done(ccb);
		return;
#endif	/* HVS_HOST_RESET */
	}
	case XPT_SCSI_IO:
	case XPT_IMMED_NOTIFY: {
		struct hv_storvsc_request *reqp = NULL;
		bus_dmamap_t dmap_saved;

		if (ccb->csio.cdb_len == 0) {
			panic("cdl_len is 0\n");
		}

		if (LIST_EMPTY(&sc->hs_free_list)) {
			ccb->ccb_h.status = CAM_REQUEUE_REQ;
			if (sc->hs_frozen == 0) {
				sc->hs_frozen = 1;
				xpt_freeze_simq(sim, /* count*/1);
			}
			xpt_done(ccb);
			return;
		}

		reqp = LIST_FIRST(&sc->hs_free_list);
		LIST_REMOVE(reqp, link);

		/* Save the data_dmap before reset request */
		dmap_saved = reqp->data_dmap;

		/* XXX this is ugly */
		bzero(reqp, sizeof(struct hv_storvsc_request));

		/* Restore necessary bits */
		reqp->data_dmap = dmap_saved;
		reqp->softc = sc;
		
		ccb->ccb_h.status |= CAM_SIM_QUEUED;
		if ((res = create_storvsc_request(ccb, reqp)) != 0) {
			ccb->ccb_h.status = CAM_REQ_INVALID;
			xpt_done(ccb);
			return;
		}

#ifdef notyet
		if (ccb->ccb_h.timeout != CAM_TIME_INFINITY) {
			callout_init(&reqp->callout, 1);
			callout_reset_sbt(&reqp->callout,
			    SBT_1MS * ccb->ccb_h.timeout, 0,
			    storvsc_timeout, reqp, 0);
#if HVS_TIMEOUT_TEST
			cv_init(&reqp->event.cv, "storvsc timeout cv");
			mtx_init(&reqp->event.mtx, "storvsc timeout mutex",
					NULL, MTX_DEF);
			switch (reqp->vstor_packet.vm_srb.cdb[0]) {
				case MODE_SELECT_10:
				case SEND_DIAGNOSTIC:
					/* To have timer send the request. */
					return;
				default:
					break;
			}
#endif /* HVS_TIMEOUT_TEST */
		}
#endif

		if ((res = hv_storvsc_io_request(sc, reqp)) != 0) {
			xpt_print(ccb->ccb_h.path,
				"hv_storvsc_io_request failed with %d\n", res);
			ccb->ccb_h.status = CAM_PROVIDE_FAIL;
			storvsc_free_request(sc, reqp);
			xpt_done(ccb);
			return;
		}
		return;
	}

	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		xpt_done(ccb);
		return;
	}
}

/**
 * @brief destroy bounce buffer
 *
 * This function is responsible for destroy a Scatter/Gather list
 * that create by storvsc_create_bounce_buffer()
 *
 * @param sgl- the Scatter/Gather need be destroy
 * @param sg_count- page count of the SG list.
 *
 */
static void
storvsc_destroy_bounce_buffer(struct sglist *sgl)
{
	struct hv_sgl_node *sgl_node = NULL;
	if (LIST_EMPTY(&g_hv_sgl_page_pool.in_use_sgl_list)) {
		printf("storvsc error: not enough in use sgl\n");
		return;
	}
	sgl_node = LIST_FIRST(&g_hv_sgl_page_pool.in_use_sgl_list);
	LIST_REMOVE(sgl_node, link);
	sgl_node->sgl_data = sgl;
	LIST_INSERT_HEAD(&g_hv_sgl_page_pool.free_sgl_list, sgl_node, link);
}

/**
 * @brief create bounce buffer
 *
 * This function is responsible for create a Scatter/Gather list,
 * which hold several pages that can be aligned with page size.
 *
 * @param seg_count- SG-list segments count
 * @param write - if WRITE_TYPE, set SG list page used size to 0,
 * otherwise set used size to page size.
 *
 * return NULL if create failed
 */
static struct sglist *
storvsc_create_bounce_buffer(uint16_t seg_count, int write)
{
	int i = 0;
	struct sglist *bounce_sgl = NULL;
	unsigned int buf_len = ((write == WRITE_TYPE) ? 0 : PAGE_SIZE);
	struct hv_sgl_node *sgl_node = NULL;	

	/* get struct sglist from free_sgl_list */
	if (LIST_EMPTY(&g_hv_sgl_page_pool.free_sgl_list)) {
		printf("storvsc error: not enough free sgl\n");
		return NULL;
	}
	sgl_node = LIST_FIRST(&g_hv_sgl_page_pool.free_sgl_list);
	LIST_REMOVE(sgl_node, link);
	bounce_sgl = sgl_node->sgl_data;
	LIST_INSERT_HEAD(&g_hv_sgl_page_pool.in_use_sgl_list, sgl_node, link);

	bounce_sgl->sg_maxseg = seg_count;

	if (write == WRITE_TYPE)
		bounce_sgl->sg_nseg = 0;
	else
		bounce_sgl->sg_nseg = seg_count;

	for (i = 0; i < seg_count; i++)
	        bounce_sgl->sg_segs[i].ss_len = buf_len;

	return bounce_sgl;
}

/**
 * @brief copy data from SG list to bounce buffer
 *
 * This function is responsible for copy data from one SG list's segments
 * to another SG list which used as bounce buffer.
 *
 * @param bounce_sgl - the destination SG list
 * @param orig_sgl - the segment of the source SG list.
 * @param orig_sgl_count - the count of segments.
 * @param orig_sgl_count - indicate which segment need bounce buffer,
 *  set 1 means need.
 *
 */
static void
storvsc_copy_sgl_to_bounce_buf(struct sglist *bounce_sgl,
			       bus_dma_segment_t *orig_sgl,
			       unsigned int orig_sgl_count,
			       uint64_t seg_bits)
{
	int src_sgl_idx = 0;

	for (src_sgl_idx = 0; src_sgl_idx < orig_sgl_count; src_sgl_idx++) {
		if (seg_bits & (1 << src_sgl_idx)) {
			memcpy((void*)bounce_sgl->sg_segs[src_sgl_idx].ss_paddr,
			    (void*)orig_sgl[src_sgl_idx].ds_addr,
			    orig_sgl[src_sgl_idx].ds_len);

			bounce_sgl->sg_segs[src_sgl_idx].ss_len =
			    orig_sgl[src_sgl_idx].ds_len;
		}
	}
}

/**
 * @brief copy data from SG list which used as bounce to another SG list
 *
 * This function is responsible for copy data from one SG list with bounce
 * buffer to another SG list's segments.
 *
 * @param dest_sgl - the destination SG list's segments
 * @param dest_sgl_count - the count of destination SG list's segment.
 * @param src_sgl - the source SG list.
 * @param seg_bits - indicate which segment used bounce buffer of src SG-list.
 *
 */
void
storvsc_copy_from_bounce_buf_to_sgl(bus_dma_segment_t *dest_sgl,
				    unsigned int dest_sgl_count,
				    struct sglist* src_sgl,
				    uint64_t seg_bits)
{
	int sgl_idx = 0;
	
	for (sgl_idx = 0; sgl_idx < dest_sgl_count; sgl_idx++) {
		if (seg_bits & (1 << sgl_idx)) {
			memcpy((void*)(dest_sgl[sgl_idx].ds_addr),
			    (void*)(src_sgl->sg_segs[sgl_idx].ss_paddr),
			    src_sgl->sg_segs[sgl_idx].ss_len);
		}
	}
}

/**
 * @brief check SG list with bounce buffer or not
 *
 * This function is responsible for check if need bounce buffer for SG list.
 *
 * @param sgl - the SG list's segments
 * @param sg_count - the count of SG list's segment.
 * @param bits - segmengs number that need bounce buffer
 *
 * return -1 if SG list needless bounce buffer
 */
static int
storvsc_check_bounce_buffer_sgl(bus_dma_segment_t *sgl,
				unsigned int sg_count,
				uint64_t *bits)
{
	int i = 0;
	int offset = 0;
	uint64_t phys_addr = 0;
	uint64_t tmp_bits = 0;
	boolean_t found_hole = FALSE;
	boolean_t pre_aligned = TRUE;

	if (sg_count < 2){
		return -1;
	}

	*bits = 0;
	
	phys_addr = vtophys(sgl[0].ds_addr);
	offset =  phys_addr - trunc_page(phys_addr);

	if (offset != 0) {
		pre_aligned = FALSE;
		tmp_bits |= 1;
	}

	for (i = 1; i < sg_count; i++) {
		phys_addr = vtophys(sgl[i].ds_addr);
		offset =  phys_addr - trunc_page(phys_addr);

		if (offset == 0) {
			if (FALSE == pre_aligned){
				/*
				 * This segment is aligned, if the previous
				 * one is not aligned, find a hole
				 */
				found_hole = TRUE;
			}
			pre_aligned = TRUE;
		} else {
			tmp_bits |= 1ULL << i;
			if (!pre_aligned) {
				if (phys_addr != vtophys(sgl[i-1].ds_addr +
				    sgl[i-1].ds_len)) {
					/*
					 * Check whether connect to previous
					 * segment,if not, find the hole
					 */
					found_hole = TRUE;
				}
			} else {
				found_hole = TRUE;
			}
			pre_aligned = FALSE;
		}
	}

	if (!found_hole) {
		return (-1);
	} else {
		*bits = tmp_bits;
		return 0;
	}
}

/**
 * Copy bus_dma segments to multiple page buffer, which requires
 * the pages are compact composed except for the 1st and last pages.
 */
static void
storvsc_xferbuf_prepare(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct hv_storvsc_request *reqp = arg;
	union ccb *ccb = reqp->ccb;
	struct ccb_scsiio *csio = &ccb->csio;
	struct storvsc_gpa_range *prplist;
	int i;

	prplist = &reqp->prp_list;
	prplist->gpa_range.gpa_len = csio->dxfer_len;
	prplist->gpa_range.gpa_ofs = segs[0].ds_addr & PAGE_MASK;

	for (i = 0; i < nsegs; i++) {
#ifdef INVARIANTS
		if (nsegs > 1) {
			if (i == 0) {
				KASSERT((segs[i].ds_addr & PAGE_MASK) +
				    segs[i].ds_len == PAGE_SIZE,
				    ("invalid 1st page, ofs 0x%jx, len %zu",
				     (uintmax_t)segs[i].ds_addr,
				     segs[i].ds_len));
			} else if (i == nsegs - 1) {
				KASSERT((segs[i].ds_addr & PAGE_MASK) == 0,
				    ("invalid last page, ofs 0x%jx",
				     (uintmax_t)segs[i].ds_addr));
			} else {
				KASSERT((segs[i].ds_addr & PAGE_MASK) == 0 &&
				    segs[i].ds_len == PAGE_SIZE,
				    ("not a full page, ofs 0x%jx, len %zu",
				     (uintmax_t)segs[i].ds_addr,
				     segs[i].ds_len));
			}
		}
#endif
		prplist->gpa_page[i] = atop(segs[i].ds_addr);
	}
	reqp->prp_cnt = nsegs;
}

/**
 * @brief Fill in a request structure based on a CAM control block
 *
 * Fills in a request structure based on the contents of a CAM control
 * block.  The request structure holds the payload information for
 * VSCSI protocol request.
 *
 * @param ccb pointer to a CAM contorl block
 * @param reqp pointer to a request structure
 */
static int
create_storvsc_request(union ccb *ccb, struct hv_storvsc_request *reqp)
{
	struct ccb_scsiio *csio = &ccb->csio;
	uint64_t phys_addr;
	uint32_t pfn;
	uint64_t not_aligned_seg_bits = 0;
	int error;
	
	/* refer to struct vmscsi_req for meanings of these two fields */
	reqp->vstor_packet.u.vm_srb.port =
		cam_sim_unit(xpt_path_sim(ccb->ccb_h.path));
	reqp->vstor_packet.u.vm_srb.path_id =
		cam_sim_bus(xpt_path_sim(ccb->ccb_h.path));

	reqp->vstor_packet.u.vm_srb.target_id = ccb->ccb_h.target_id;
	reqp->vstor_packet.u.vm_srb.lun = ccb->ccb_h.target_lun;

	reqp->vstor_packet.u.vm_srb.cdb_len = csio->cdb_len;
	if(ccb->ccb_h.flags & CAM_CDB_POINTER) {
		memcpy(&reqp->vstor_packet.u.vm_srb.u.cdb, csio->cdb_io.cdb_ptr,
			csio->cdb_len);
	} else {
		memcpy(&reqp->vstor_packet.u.vm_srb.u.cdb, csio->cdb_io.cdb_bytes,
			csio->cdb_len);
	}

	if (hv_storvsc_use_win8ext_flags) {
		reqp->vstor_packet.u.vm_srb.win8_extension.time_out_value = 60;
		reqp->vstor_packet.u.vm_srb.win8_extension.srb_flags |=
			SRB_FLAGS_DISABLE_SYNCH_TRANSFER;
	}
	switch (ccb->ccb_h.flags & CAM_DIR_MASK) {
	case CAM_DIR_OUT:
		reqp->vstor_packet.u.vm_srb.data_in = WRITE_TYPE;
		if (hv_storvsc_use_win8ext_flags) {
			reqp->vstor_packet.u.vm_srb.win8_extension.srb_flags |=
				SRB_FLAGS_DATA_OUT;
		}
		break;
	case CAM_DIR_IN:
		reqp->vstor_packet.u.vm_srb.data_in = READ_TYPE;
		if (hv_storvsc_use_win8ext_flags) {
			reqp->vstor_packet.u.vm_srb.win8_extension.srb_flags |=
				SRB_FLAGS_DATA_IN;
		}
		break;
	case CAM_DIR_NONE:
		reqp->vstor_packet.u.vm_srb.data_in = UNKNOWN_TYPE;
		if (hv_storvsc_use_win8ext_flags) {
			reqp->vstor_packet.u.vm_srb.win8_extension.srb_flags |=
				SRB_FLAGS_NO_DATA_TRANSFER;
		}
		break;
	default:
		printf("Error: unexpected data direction: 0x%x\n",
			ccb->ccb_h.flags & CAM_DIR_MASK);
		return (EINVAL);
	}

	reqp->sense_data     = &csio->sense_data;
	reqp->sense_info_len = csio->sense_len;

	reqp->ccb = ccb;

	if (0 == csio->dxfer_len) {
		return (0);
	}

	switch (ccb->ccb_h.flags & CAM_DATA_MASK) {
	case CAM_DATA_BIO:
	case CAM_DATA_VADDR:
		error = bus_dmamap_load_ccb(reqp->softc->storvsc_req_dtag,
		    reqp->data_dmap, ccb, storvsc_xferbuf_prepare, reqp,
		    BUS_DMA_NOWAIT);
		if (error) {
			xpt_print(ccb->ccb_h.path,
			    "bus_dmamap_load_ccb failed: %d\n", error);
			return (error);
		}
		if ((ccb->ccb_h.flags & CAM_DATA_MASK) == CAM_DATA_BIO)
			reqp->softc->sysctl_data.data_bio_cnt++;
		else
			reqp->softc->sysctl_data.data_vaddr_cnt++;
		break;

	case CAM_DATA_SG:
	{
		struct storvsc_gpa_range *prplist;
		int i = 0;
		int offset = 0;
		int ret;

		bus_dma_segment_t *storvsc_sglist =
		    (bus_dma_segment_t *)ccb->csio.data_ptr;
		u_int16_t storvsc_sg_count = ccb->csio.sglist_cnt;

		prplist = &reqp->prp_list;
		prplist->gpa_range.gpa_len = csio->dxfer_len;

		printf("Storvsc: get SG I/O operation, %d\n",
		    reqp->vstor_packet.u.vm_srb.data_in);

		if (storvsc_sg_count > STORVSC_DATA_SEGCNT_MAX){
			printf("Storvsc: %d segments is too much, "
			    "only support %d segments\n",
			    storvsc_sg_count, STORVSC_DATA_SEGCNT_MAX);
			return (EINVAL);
		}

		/*
		 * We create our own bounce buffer function currently. Idealy
		 * we should use BUS_DMA(9) framework. But with current BUS_DMA
		 * code there is no callback API to check the page alignment of
		 * middle segments before busdma can decide if a bounce buffer
		 * is needed for particular segment. There is callback,
		 * "bus_dma_filter_t *filter", but the parrameters are not
		 * sufficient for storvsc driver.
		 * TODO:
		 *	Add page alignment check in BUS_DMA(9) callback. Once
		 *	this is complete, switch the following code to use
		 *	BUS_DMA(9) for storvsc bounce buffer support.
		 */
		/* check if we need to create bounce buffer */
		ret = storvsc_check_bounce_buffer_sgl(storvsc_sglist,
		    storvsc_sg_count, &not_aligned_seg_bits);
		if (ret != -1) {
			reqp->bounce_sgl =
			    storvsc_create_bounce_buffer(storvsc_sg_count,
			    reqp->vstor_packet.u.vm_srb.data_in);
			if (NULL == reqp->bounce_sgl) {
				printf("Storvsc_error: "
				    "create bounce buffer failed.\n");
				return (ENOMEM);
			}

			reqp->bounce_sgl_count = storvsc_sg_count;
			reqp->not_aligned_seg_bits = not_aligned_seg_bits;

			/*
			 * if it is write, we need copy the original data
			 *to bounce buffer
			 */
			if (WRITE_TYPE == reqp->vstor_packet.u.vm_srb.data_in) {
				storvsc_copy_sgl_to_bounce_buf(
				    reqp->bounce_sgl,
				    storvsc_sglist,
				    storvsc_sg_count,
				    reqp->not_aligned_seg_bits);
			}

			/* transfer virtual address to physical frame number */
			if (reqp->not_aligned_seg_bits & 0x1){
 				phys_addr =
				    vtophys(reqp->bounce_sgl->sg_segs[0].ss_paddr);
			}else{
 				phys_addr =
					vtophys(storvsc_sglist[0].ds_addr);
			}
			prplist->gpa_range.gpa_ofs = phys_addr & PAGE_MASK;

			pfn = phys_addr >> PAGE_SHIFT;
			prplist->gpa_page[0] = pfn;
			
			for (i = 1; i < storvsc_sg_count; i++) {
				if (reqp->not_aligned_seg_bits & (1 << i)) {
					phys_addr =
					    vtophys(reqp->bounce_sgl->sg_segs[i].ss_paddr);
				} else {
					phys_addr =
					    vtophys(storvsc_sglist[i].ds_addr);
				}

				pfn = phys_addr >> PAGE_SHIFT;
				prplist->gpa_page[i] = pfn;
			}
			reqp->prp_cnt = i;
		} else {
			phys_addr = vtophys(storvsc_sglist[0].ds_addr);

			prplist->gpa_range.gpa_ofs = phys_addr & PAGE_MASK;

			for (i = 0; i < storvsc_sg_count; i++) {
				phys_addr = vtophys(storvsc_sglist[i].ds_addr);
				pfn = phys_addr >> PAGE_SHIFT;
				prplist->gpa_page[i] = pfn;
			}
			reqp->prp_cnt = i;

			/* check the last segment cross boundary or not */
			offset = phys_addr & PAGE_MASK;
			if (offset) {
				/* Add one more PRP entry */
				phys_addr =
				    vtophys(storvsc_sglist[i-1].ds_addr +
				    PAGE_SIZE - offset);
				pfn = phys_addr >> PAGE_SHIFT;
				prplist->gpa_page[i] = pfn;
				reqp->prp_cnt++;
			}
			
			reqp->bounce_sgl_count = 0;
		}
		reqp->softc->sysctl_data.data_sg_cnt++;
		break;
	}
	default:
		printf("Unknow flags: %d\n", ccb->ccb_h.flags);
		return(EINVAL);
	}

	return(0);
}

static uint32_t
is_scsi_valid(const struct scsi_inquiry_data *inq_data)
{
	u_int8_t type;

	type = SID_TYPE(inq_data);
	if (type == T_NODEVICE)
		return (0);
	if (SID_QUAL(inq_data) == SID_QUAL_BAD_LU)
		return (0);
	return (1);
}

/**
 * @brief completion function before returning to CAM
 *
 * I/O process has been completed and the result needs
 * to be passed to the CAM layer.
 * Free resources related to this request.
 *
 * @param reqp pointer to a request structure
 */
static void
storvsc_io_done(struct hv_storvsc_request *reqp)
{
	union ccb *ccb = reqp->ccb;
	struct ccb_scsiio *csio = &ccb->csio;
	struct storvsc_softc *sc = reqp->softc;
	struct vmscsi_req *vm_srb = &reqp->vstor_packet.u.vm_srb;
	bus_dma_segment_t *ori_sglist = NULL;
	int ori_sg_count = 0;
	const struct scsi_generic *cmd;

	/* destroy bounce buffer if it is used */
	if (reqp->bounce_sgl_count) {
		ori_sglist = (bus_dma_segment_t *)ccb->csio.data_ptr;
		ori_sg_count = ccb->csio.sglist_cnt;

		/*
		 * If it is READ operation, we should copy back the data
		 * to original SG list.
		 */
		if (READ_TYPE == reqp->vstor_packet.u.vm_srb.data_in) {
			storvsc_copy_from_bounce_buf_to_sgl(ori_sglist,
			    ori_sg_count,
			    reqp->bounce_sgl,
			    reqp->not_aligned_seg_bits);
		}

		storvsc_destroy_bounce_buffer(reqp->bounce_sgl);
		reqp->bounce_sgl_count = 0;
	}
		
	if (reqp->retries > 0) {
		mtx_lock(&sc->hs_lock);
#if HVS_TIMEOUT_TEST
		xpt_print(ccb->ccb_h.path,
			"%u: IO returned after timeout, "
			"waking up timer handler if any.\n", ticks);
		mtx_lock(&reqp->event.mtx);
		cv_signal(&reqp->event.cv);
		mtx_unlock(&reqp->event.mtx);
#endif
		reqp->retries = 0;
		xpt_print(ccb->ccb_h.path,
			"%u: IO returned after timeout, "
			"stopping timer if any.\n", ticks);
		mtx_unlock(&sc->hs_lock);
	}

#ifdef notyet
	/*
	 * callout_drain() will wait for the timer handler to finish
	 * if it is running. So we don't need any lock to synchronize
	 * between this routine and the timer handler.
	 * Note that we need to make sure reqp is not freed when timer
	 * handler is using or will use it.
	 */
	if (ccb->ccb_h.timeout != CAM_TIME_INFINITY) {
		callout_drain(&reqp->callout);
	}
#endif
	cmd = (const struct scsi_generic *)
	    ((ccb->ccb_h.flags & CAM_CDB_POINTER) ?
	     csio->cdb_io.cdb_ptr : csio->cdb_io.cdb_bytes);

	ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
	ccb->ccb_h.status &= ~CAM_STATUS_MASK;
	int srb_status = SRB_STATUS(vm_srb->srb_status);
	if (vm_srb->scsi_status == SCSI_STATUS_OK) {
		if (srb_status != SRB_STATUS_SUCCESS) {
			/*
			 * If there are errors, for example, invalid LUN,
			 * host will inform VM through SRB status.
			 */
			if (bootverbose) {
				if (srb_status == SRB_STATUS_INVALID_LUN) {
					xpt_print(ccb->ccb_h.path,
					    "invalid LUN %d for op: %s\n",
					    vm_srb->lun,
					    scsi_op_desc(cmd->opcode, NULL));
				} else {
					xpt_print(ccb->ccb_h.path,
					    "Unknown SRB flag: %d for op: %s\n",
					    srb_status,
					    scsi_op_desc(cmd->opcode, NULL));
				}
			}
			ccb->ccb_h.status |= CAM_DEV_NOT_THERE;
		} else {
			ccb->ccb_h.status |= CAM_REQ_CMP;
		}

		if (cmd->opcode == INQUIRY &&
		    srb_status == SRB_STATUS_SUCCESS) {
			int resp_xfer_len, resp_buf_len, data_len;
			uint8_t *resp_buf = (uint8_t *)csio->data_ptr;
			struct scsi_inquiry_data *inq_data =
			    (struct scsi_inquiry_data *)csio->data_ptr;

			/* Get the buffer length reported by host */
			resp_xfer_len = vm_srb->transfer_len;

			/* Get the available buffer length */
			resp_buf_len = resp_xfer_len >= 5 ? resp_buf[4] + 5 : 0;
			data_len = (resp_buf_len < resp_xfer_len) ?
			    resp_buf_len : resp_xfer_len;
			if (bootverbose && data_len >= 5) {
				xpt_print(ccb->ccb_h.path, "storvsc inquiry "
				    "(%d) [%x %x %x %x %x ... ]\n", data_len,
				    resp_buf[0], resp_buf[1], resp_buf[2],
				    resp_buf[3], resp_buf[4]);
			}
			/*
			 * XXX: Hyper-V (since win2012r2) responses inquiry with
			 * unknown version (0) for GEN-2 DVD device.
			 * Manually set the version number to SPC3 in order to
			 * ask CAM to continue probing with "PROBE_REPORT_LUNS".
			 * see probedone() in scsi_xpt.c
			 */
			if (SID_TYPE(inq_data) == T_CDROM &&
			    inq_data->version == 0 &&
			    (vmstor_proto_version >= VMSTOR_PROTOCOL_VERSION_WIN8)) {
				inq_data->version = SCSI_REV_SPC3;
				if (bootverbose) {
					xpt_print(ccb->ccb_h.path,
					    "set version from 0 to %d\n",
					    inq_data->version);
				}
			}
			/*
			 * XXX: Manually fix the wrong response returned from WS2012
			 */
			if (!is_scsi_valid(inq_data) &&
			    (vmstor_proto_version == VMSTOR_PROTOCOL_VERSION_WIN8_1 ||
			    vmstor_proto_version == VMSTOR_PROTOCOL_VERSION_WIN8 ||
			    vmstor_proto_version == VMSTOR_PROTOCOL_VERSION_WIN7)) {
				if (data_len >= 4 &&
				    (resp_buf[2] == 0 || resp_buf[3] == 0)) {
					resp_buf[2] = SCSI_REV_SPC3;
					resp_buf[3] = 2; // resp fmt must be 2
					if (bootverbose)
						xpt_print(ccb->ccb_h.path,
						    "fix version and resp fmt for 0x%x\n",
						    vmstor_proto_version);
				}
			} else if (data_len >= SHORT_INQUIRY_LENGTH) {
				char vendor[16];

				cam_strvis(vendor, inq_data->vendor,
				    sizeof(inq_data->vendor), sizeof(vendor));
				/*
				 * XXX: Upgrade SPC2 to SPC3 if host is WIN8 or
				 * WIN2012 R2 in order to support UNMAP feature.
				 */
				if (!strncmp(vendor, "Msft", 4) &&
				    SID_ANSI_REV(inq_data) == SCSI_REV_SPC2 &&
				    (vmstor_proto_version ==
				     VMSTOR_PROTOCOL_VERSION_WIN8_1 ||
				     vmstor_proto_version ==
				     VMSTOR_PROTOCOL_VERSION_WIN8)) {
					inq_data->version = SCSI_REV_SPC3;
					if (bootverbose) {
						xpt_print(ccb->ccb_h.path,
						    "storvsc upgrades "
						    "SPC2 to SPC3\n");
					}
				}
			}
		}
	} else {
		/**
		 * On Some Windows hosts TEST_UNIT_READY command can return
		 * SRB_STATUS_ERROR and sense data, for example, asc=0x3a,1
		 * "(Medium not present - tray closed)". This error can be
		 * ignored since it will be sent to host periodically.
		 */
		boolean_t unit_not_ready = \
		    vm_srb->scsi_status == SCSI_STATUS_CHECK_COND &&
		    cmd->opcode == TEST_UNIT_READY &&
		    srb_status == SRB_STATUS_ERROR;
		if (!unit_not_ready && bootverbose) {
			mtx_lock(&sc->hs_lock);
			xpt_print(ccb->ccb_h.path,
				"storvsc scsi_status = %d, srb_status = %d\n",
				vm_srb->scsi_status, srb_status);
			mtx_unlock(&sc->hs_lock);
		}
		ccb->ccb_h.status |= CAM_SCSI_STATUS_ERROR;
	}

	ccb->csio.scsi_status = (vm_srb->scsi_status & 0xFF);
	ccb->csio.resid = ccb->csio.dxfer_len - vm_srb->transfer_len;

	if (reqp->sense_info_len != 0) {
		csio->sense_resid = csio->sense_len - reqp->sense_info_len;
		ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
	}

	mtx_lock(&sc->hs_lock);
	if (reqp->softc->hs_frozen == 1) {
		xpt_print(ccb->ccb_h.path,
			"%u: storvsc unfreezing softc 0x%p.\n",
			ticks, reqp->softc);
		ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
		reqp->softc->hs_frozen = 0;
	}
	storvsc_free_request(sc, reqp);
	mtx_unlock(&sc->hs_lock);

	xpt_done_direct(ccb);
}

/**
 * @brief Free a request structure
 *
 * Free a request structure by returning it to the free list
 *
 * @param sc pointer to a softc
 * @param reqp pointer to a request structure
 */	
static void
storvsc_free_request(struct storvsc_softc *sc, struct hv_storvsc_request *reqp)
{

	LIST_INSERT_HEAD(&sc->hs_free_list, reqp, link);
}

/**
 * @brief Determine type of storage device from GUID
 *
 * Using the type GUID, determine if this is a StorVSC (paravirtual
 * SCSI or BlkVSC (paravirtual IDE) device.
 *
 * @param dev a device
 * returns an enum
 */
static enum hv_storage_type
storvsc_get_storage_type(device_t dev)
{
	device_t parent = device_get_parent(dev);

	if (VMBUS_PROBE_GUID(parent, dev, &gBlkVscDeviceType) == 0)
		return DRIVER_BLKVSC;
	if (VMBUS_PROBE_GUID(parent, dev, &gStorVscDeviceType) == 0)
		return DRIVER_STORVSC;
	return DRIVER_UNKNOWN;
}

#define	PCI_VENDOR_INTEL	0x8086
#define	PCI_PRODUCT_PIIX4	0x7111

static void
storvsc_ada_probe_veto(void *arg __unused, struct cam_path *path,
    struct ata_params *ident_buf __unused, int *veto)
{

	/*
	 * The ATA disks are shared with the controllers managed
	 * by this driver, so veto the ATA disks' attachment; the
	 * ATA disks will be attached as SCSI disks once this driver
	 * attached.
	 */
	if (path->device->protocol == PROTO_ATA) {
		struct ccb_pathinq cpi;

		xpt_path_inq(&cpi, path);
		if (cpi.ccb_h.status == CAM_REQ_CMP &&
		    cpi.hba_vendor == PCI_VENDOR_INTEL &&
		    cpi.hba_device == PCI_PRODUCT_PIIX4) {
			(*veto)++;
			if (bootverbose) {
				xpt_print(path,
				    "Disable ATA disks on "
				    "simulated ATA controller (0x%04x%04x)\n",
				    cpi.hba_device, cpi.hba_vendor);
			}
		}
	}
}

static void
storvsc_sysinit(void *arg __unused)
{
	if (vm_guest == VM_GUEST_HV) {
		storvsc_handler_tag = EVENTHANDLER_REGISTER(ada_probe_veto,
		    storvsc_ada_probe_veto, NULL, EVENTHANDLER_PRI_ANY);
	}
}
SYSINIT(storvsc_sys_init, SI_SUB_DRIVERS, SI_ORDER_SECOND, storvsc_sysinit,
    NULL);

static void
storvsc_sysuninit(void *arg __unused)
{
	if (storvsc_handler_tag != NULL)
		EVENTHANDLER_DEREGISTER(ada_probe_veto, storvsc_handler_tag);
}
SYSUNINIT(storvsc_sys_uninit, SI_SUB_DRIVERS, SI_ORDER_SECOND,
    storvsc_sysuninit, NULL);
