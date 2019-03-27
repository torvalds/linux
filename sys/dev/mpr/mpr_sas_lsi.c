/*-
 * Copyright (c) 2011-2015 LSI Corp.
 * Copyright (c) 2013-2016 Avago Technologies
 * Copyright 2000-2020 Broadcom Inc.
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
 * Broadcom Inc. (LSI) MPT-Fusion Host Adapter FreeBSD
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Communications core for Avago Technologies (LSI) MPT3 */

/* TODO Move headers to mprvar */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/selinfo.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/uio.h>
#include <sys/sysctl.h>
#include <sys/endian.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>
#include <sys/sbuf.h>
#include <sys/reboot.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <machine/stdarg.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_debug.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_periph.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

#include <dev/mpr/mpi/mpi2_type.h>
#include <dev/mpr/mpi/mpi2.h>
#include <dev/mpr/mpi/mpi2_ioc.h>
#include <dev/mpr/mpi/mpi2_sas.h>
#include <dev/mpr/mpi/mpi2_pci.h>
#include <dev/mpr/mpi/mpi2_cnfg.h>
#include <dev/mpr/mpi/mpi2_init.h>
#include <dev/mpr/mpi/mpi2_raid.h>
#include <dev/mpr/mpi/mpi2_tool.h>
#include <dev/mpr/mpr_ioctl.h>
#include <dev/mpr/mprvar.h>
#include <dev/mpr/mpr_table.h>
#include <dev/mpr/mpr_sas.h>

/* For Hashed SAS Address creation for SATA Drives */
#define MPT2SAS_SN_LEN 20
#define MPT2SAS_MN_LEN 40

struct mpr_fw_event_work {
	u16			event;
	void			*event_data;
	TAILQ_ENTRY(mpr_fw_event_work)	ev_link;
};

union _sata_sas_address {
	u8 wwid[8];
	struct {
		u32 high;
		u32 low;
	} word;
};

/*
 * define the IDENTIFY DEVICE structure
 */
struct _ata_identify_device_data {
	u16 reserved1[10];	/* 0-9 */
	u16 serial_number[10];	/* 10-19 */
	u16 reserved2[7];	/* 20-26 */
	u16 model_number[20];	/* 27-46*/
	u16 reserved3[170];	/* 47-216 */
	u16 rotational_speed;	/* 217 */
	u16 reserved4[38];	/* 218-255 */
};
static u32 event_count;
static void mprsas_fw_work(struct mpr_softc *sc,
    struct mpr_fw_event_work *fw_event);
static void mprsas_fw_event_free(struct mpr_softc *,
    struct mpr_fw_event_work *);
static int mprsas_add_device(struct mpr_softc *sc, u16 handle, u8 linkrate);
static int mprsas_add_pcie_device(struct mpr_softc *sc, u16 handle,
    u8 linkrate);
static int mprsas_get_sata_identify(struct mpr_softc *sc, u16 handle,
    Mpi2SataPassthroughReply_t *mpi_reply, char *id_buffer, int sz,
    u32 devinfo);
static void mprsas_ata_id_timeout(struct mpr_softc *, struct mpr_command *);
int mprsas_get_sas_address_for_sata_disk(struct mpr_softc *sc,
    u64 *sas_address, u16 handle, u32 device_info, u8 *is_SATA_SSD);
static int mprsas_volume_add(struct mpr_softc *sc,
    u16 handle);
static void mprsas_SSU_to_SATA_devices(struct mpr_softc *sc, int howto);
static void mprsas_stop_unit_done(struct cam_periph *periph,
    union ccb *done_ccb);

void
mprsas_evt_handler(struct mpr_softc *sc, uintptr_t data,
    MPI2_EVENT_NOTIFICATION_REPLY *event)
{
	struct mpr_fw_event_work *fw_event;
	u16 sz;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);
	MPR_DPRINT_EVENT(sc, sas, event);
	mprsas_record_event(sc, event);

	fw_event = malloc(sizeof(struct mpr_fw_event_work), M_MPR,
	     M_ZERO|M_NOWAIT);
	if (!fw_event) {
		printf("%s: allocate failed for fw_event\n", __func__);
		return;
	}
	sz = le16toh(event->EventDataLength) * 4;
	fw_event->event_data = malloc(sz, M_MPR, M_ZERO|M_NOWAIT);
	if (!fw_event->event_data) {
		printf("%s: allocate failed for event_data\n", __func__);
		free(fw_event, M_MPR);
		return;
	}

	bcopy(event->EventData, fw_event->event_data, sz);
	fw_event->event = event->Event;
	if ((event->Event == MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST ||
	    event->Event == MPI2_EVENT_PCIE_TOPOLOGY_CHANGE_LIST ||
	    event->Event == MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE ||
	    event->Event == MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST) &&
	    sc->track_mapping_events)
		sc->pending_map_events++;

	/*
	 * When wait_for_port_enable flag is set, make sure that all the events
	 * are processed. Increment the startup_refcount and decrement it after
	 * events are processed.
	 */
	if ((event->Event == MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST ||
	    event->Event == MPI2_EVENT_PCIE_TOPOLOGY_CHANGE_LIST ||
	    event->Event == MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST) &&
	    sc->wait_for_port_enable)
		mprsas_startup_increment(sc->sassc);

	TAILQ_INSERT_TAIL(&sc->sassc->ev_queue, fw_event, ev_link);
	taskqueue_enqueue(sc->sassc->ev_tq, &sc->sassc->ev_task);
}

static void
mprsas_fw_event_free(struct mpr_softc *sc, struct mpr_fw_event_work *fw_event)
{

	free(fw_event->event_data, M_MPR);
	free(fw_event, M_MPR);
}

/**
 * _mpr_fw_work - delayed task for processing firmware events
 * @sc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
mprsas_fw_work(struct mpr_softc *sc, struct mpr_fw_event_work *fw_event)
{
	struct mprsas_softc *sassc;
	sassc = sc->sassc;

	mpr_dprint(sc, MPR_EVENT, "(%d)->(%s) Working on  Event: [%x]\n",
	    event_count++, __func__, fw_event->event);
	switch (fw_event->event) {
	case MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST: 
	{
		MPI2_EVENT_DATA_SAS_TOPOLOGY_CHANGE_LIST *data;
		MPI2_EVENT_SAS_TOPO_PHY_ENTRY *phy;
		uint8_t i;

		data = (MPI2_EVENT_DATA_SAS_TOPOLOGY_CHANGE_LIST *)
		    fw_event->event_data;

		mpr_mapping_topology_change_event(sc, fw_event->event_data);

		for (i = 0; i < data->NumEntries; i++) {
			phy = &data->PHY[i];
			switch (phy->PhyStatus & MPI2_EVENT_SAS_TOPO_RC_MASK) {
			case MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED:
				if (mprsas_add_device(sc,
				    le16toh(phy->AttachedDevHandle),
				    phy->LinkRate)) {
					mpr_dprint(sc, MPR_ERROR, "%s: "
					    "failed to add device with handle "
					    "0x%x\n", __func__,
					    le16toh(phy->AttachedDevHandle));
					mprsas_prepare_remove(sassc, le16toh(
					    phy->AttachedDevHandle));
				}
				break;
			case MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING:
				mprsas_prepare_remove(sassc, le16toh(
				    phy->AttachedDevHandle));
				break;
			case MPI2_EVENT_SAS_TOPO_RC_PHY_CHANGED:
			case MPI2_EVENT_SAS_TOPO_RC_NO_CHANGE:
			case MPI2_EVENT_SAS_TOPO_RC_DELAY_NOT_RESPONDING:
			default:
				break;
			}
		}
		/*
		 * refcount was incremented for this event in
		 * mprsas_evt_handler.  Decrement it here because the event has
		 * been processed.
		 */
		mprsas_startup_decrement(sassc);
		break;
	}
	case MPI2_EVENT_SAS_DISCOVERY:
	{
		MPI2_EVENT_DATA_SAS_DISCOVERY *data;

		data = (MPI2_EVENT_DATA_SAS_DISCOVERY *)fw_event->event_data;

		if (data->ReasonCode & MPI2_EVENT_SAS_DISC_RC_STARTED)
			mpr_dprint(sc, MPR_TRACE,"SAS discovery start event\n");
		if (data->ReasonCode & MPI2_EVENT_SAS_DISC_RC_COMPLETED) {
			mpr_dprint(sc, MPR_TRACE,"SAS discovery stop event\n");
			sassc->flags &= ~MPRSAS_IN_DISCOVERY;
			mprsas_discovery_end(sassc);
		}
		break;
	}
	case MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
	{
		Mpi2EventDataSasEnclDevStatusChange_t *data;
		data = (Mpi2EventDataSasEnclDevStatusChange_t *)
		    fw_event->event_data;
		mpr_mapping_enclosure_dev_status_change_event(sc,
		    fw_event->event_data);
		break;
	}
	case MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST:
	{
		Mpi2EventIrConfigElement_t *element;
		int i;
		u8 foreign_config, reason;
		u16 elementType;
		Mpi2EventDataIrConfigChangeList_t *event_data;
		struct mprsas_target *targ;
		unsigned int id;

		event_data = fw_event->event_data;
		foreign_config = (le32toh(event_data->Flags) &
		    MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG) ? 1 : 0;

		element =
		    (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];
		id = mpr_mapping_get_raid_tid_from_handle(sc,
		    element->VolDevHandle);

		mpr_mapping_ir_config_change_event(sc, event_data);
		for (i = 0; i < event_data->NumElements; i++, element++) {
			reason = element->ReasonCode;
			elementType = le16toh(element->ElementFlags) &
			    MPI2_EVENT_IR_CHANGE_EFLAGS_ELEMENT_TYPE_MASK;
			/*
			 * check for element type of Phys Disk or Hot Spare
			 */
			if ((elementType != 
			    MPI2_EVENT_IR_CHANGE_EFLAGS_VOLPHYSDISK_ELEMENT)
			    && (elementType !=
			    MPI2_EVENT_IR_CHANGE_EFLAGS_HOTSPARE_ELEMENT))
				// do next element
				goto skip_fp_send;

			/*
			 * check for reason of Hide, Unhide, PD Created, or PD
			 * Deleted
			 */
			if ((reason != MPI2_EVENT_IR_CHANGE_RC_HIDE) &&
			    (reason != MPI2_EVENT_IR_CHANGE_RC_UNHIDE) &&
			    (reason != MPI2_EVENT_IR_CHANGE_RC_PD_CREATED) &&
			    (reason != MPI2_EVENT_IR_CHANGE_RC_PD_DELETED))
				goto skip_fp_send;

			// check for a reason of Hide or PD Created
			if ((reason == MPI2_EVENT_IR_CHANGE_RC_HIDE) ||
			    (reason == MPI2_EVENT_IR_CHANGE_RC_PD_CREATED))
			{
				// build RAID Action message
				Mpi2RaidActionRequest_t	*action;
				Mpi2RaidActionReply_t *reply = NULL;
				struct mpr_command *cm;
				int error = 0;
				if ((cm = mpr_alloc_command(sc)) == NULL) {
					printf("%s: command alloc failed\n",
					    __func__);
					return;
				}

				mpr_dprint(sc, MPR_EVENT, "Sending FP action "
				    "from "
				    "MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST "
				    ":\n");
				action = (MPI2_RAID_ACTION_REQUEST *)cm->cm_req;
				action->Function = MPI2_FUNCTION_RAID_ACTION;
				action->Action =
				    MPI2_RAID_ACTION_PHYSDISK_HIDDEN;
				action->PhysDiskNum = element->PhysDiskNum;
				cm->cm_desc.Default.RequestFlags =
				    MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
				error = mpr_request_polled(sc, &cm);
				if (cm != NULL)
					reply = (Mpi2RaidActionReply_t *)
					    cm->cm_reply;
				if (error || (reply == NULL)) {
					/* FIXME */
					/*
					 * If the poll returns error then we
					 * need to do diag reset
					 */
					printf("%s: poll for page completed "
					    "with error %d", __func__, error);
				}
				if (reply && (le16toh(reply->IOCStatus) &
				    MPI2_IOCSTATUS_MASK) !=
				    MPI2_IOCSTATUS_SUCCESS) {
					mpr_dprint(sc, MPR_ERROR, "%s: error "
					    "sending RaidActionPage; "
					    "iocstatus = 0x%x\n", __func__,
					    le16toh(reply->IOCStatus));
				}

				if (cm)
					mpr_free_command(sc, cm);
			}
skip_fp_send:
			mpr_dprint(sc, MPR_EVENT, "Received "
			    "MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST Reason "
			    "code %x:\n", element->ReasonCode);
			switch (element->ReasonCode) {
			case MPI2_EVENT_IR_CHANGE_RC_VOLUME_CREATED:
			case MPI2_EVENT_IR_CHANGE_RC_ADDED:
				if (!foreign_config) {
					if (mprsas_volume_add(sc,
					    le16toh(element->VolDevHandle))) {
						printf("%s: failed to add RAID "
						    "volume with handle 0x%x\n",
						    __func__, le16toh(element->
						    VolDevHandle));
					}
				}
				break;
			case MPI2_EVENT_IR_CHANGE_RC_VOLUME_DELETED:
			case MPI2_EVENT_IR_CHANGE_RC_REMOVED:
				/*
				 * Rescan after volume is deleted or removed.
				 */
				if (!foreign_config) {
					if (id == MPR_MAP_BAD_ID) {
						printf("%s: could not get ID "
						    "for volume with handle "
						    "0x%04x\n", __func__,
						    le16toh(element->
						    VolDevHandle));
						break;
					}
					
					targ = &sassc->targets[id];
					targ->handle = 0x0;
					targ->encl_slot = 0x0;
					targ->encl_handle = 0x0;
					targ->encl_level_valid = 0x0;
					targ->encl_level = 0x0;
					targ->connector_name[0] = ' ';
					targ->connector_name[1] = ' ';
					targ->connector_name[2] = ' ';
					targ->connector_name[3] = ' ';
					targ->exp_dev_handle = 0x0;
					targ->phy_num = 0x0;
					targ->linkrate = 0x0;
					mprsas_rescan_target(sc, targ);
					printf("RAID target id 0x%x removed\n",
					    targ->tid);
				}
				break;
			case MPI2_EVENT_IR_CHANGE_RC_PD_CREATED:
			case MPI2_EVENT_IR_CHANGE_RC_HIDE:
				/*
				 * Phys Disk of a volume has been created.  Hide
				 * it from the OS.
				 */
				targ = mprsas_find_target_by_handle(sassc, 0,
				    element->PhysDiskDevHandle);
				if (targ == NULL) 
					break;
				targ->flags |= MPR_TARGET_FLAGS_RAID_COMPONENT;
				mprsas_rescan_target(sc, targ);
				break;
			case MPI2_EVENT_IR_CHANGE_RC_PD_DELETED:
				/*
				 * Phys Disk of a volume has been deleted.
				 * Expose it to the OS.
				 */
				if (mprsas_add_device(sc,
				    le16toh(element->PhysDiskDevHandle), 0)) {
					printf("%s: failed to add device with "
					    "handle 0x%x\n", __func__,
					    le16toh(element->
					    PhysDiskDevHandle));
					mprsas_prepare_remove(sassc,
					    le16toh(element->
					    PhysDiskDevHandle));
				}
				break;
			}
		}
		/*
		 * refcount was incremented for this event in
		 * mprsas_evt_handler.  Decrement it here because the event has
		 * been processed.
		 */
		mprsas_startup_decrement(sassc);
		break;
	}
	case MPI2_EVENT_IR_VOLUME:
	{
		Mpi2EventDataIrVolume_t *event_data = fw_event->event_data;

		/*
		 * Informational only.
		 */
		mpr_dprint(sc, MPR_EVENT, "Received IR Volume event:\n");
		switch (event_data->ReasonCode) {
		case MPI2_EVENT_IR_VOLUME_RC_SETTINGS_CHANGED:
  			mpr_dprint(sc, MPR_EVENT, "   Volume Settings "
  			    "changed from 0x%x to 0x%x for Volome with "
 			    "handle 0x%x", le32toh(event_data->PreviousValue),
 			    le32toh(event_data->NewValue),
 			    le16toh(event_data->VolDevHandle));
			break;
		case MPI2_EVENT_IR_VOLUME_RC_STATUS_FLAGS_CHANGED:
  			mpr_dprint(sc, MPR_EVENT, "   Volume Status "
  			    "changed from 0x%x to 0x%x for Volome with "
 			    "handle 0x%x", le32toh(event_data->PreviousValue),
 			    le32toh(event_data->NewValue),
 			    le16toh(event_data->VolDevHandle));
			break;
		case MPI2_EVENT_IR_VOLUME_RC_STATE_CHANGED:
  			mpr_dprint(sc, MPR_EVENT, "   Volume State "
  			    "changed from 0x%x to 0x%x for Volome with "
 			    "handle 0x%x", le32toh(event_data->PreviousValue),
 			    le32toh(event_data->NewValue),
 			    le16toh(event_data->VolDevHandle));
				u32 state;
				struct mprsas_target *targ;
				state = le32toh(event_data->NewValue);
				switch (state) {
				case MPI2_RAID_VOL_STATE_MISSING:
				case MPI2_RAID_VOL_STATE_FAILED:
					mprsas_prepare_volume_remove(sassc,
					    event_data->VolDevHandle);
					break;
		 
				case MPI2_RAID_VOL_STATE_ONLINE:
				case MPI2_RAID_VOL_STATE_DEGRADED:
				case MPI2_RAID_VOL_STATE_OPTIMAL:
					targ =
					    mprsas_find_target_by_handle(sassc,
					    0, event_data->VolDevHandle);
					if (targ) {
						printf("%s %d: Volume handle "
						    "0x%x is already added \n",
						    __func__, __LINE__,
						    event_data->VolDevHandle);
						break;
					}
					if (mprsas_volume_add(sc,
					    le16toh(event_data->
					    VolDevHandle))) {
						printf("%s: failed to add RAID "
						    "volume with handle 0x%x\n",
						    __func__, le16toh(
						    event_data->VolDevHandle));
					}
					break;
				default:
					break;
				}
			break;
		default:
			break;
		}
		break;
	}
	case MPI2_EVENT_IR_PHYSICAL_DISK:
	{
		Mpi2EventDataIrPhysicalDisk_t *event_data =
		    fw_event->event_data;
		struct mprsas_target *targ;

		/*
		 * Informational only.
		 */
		mpr_dprint(sc, MPR_EVENT, "Received IR Phys Disk event:\n");
		switch (event_data->ReasonCode) {
		case MPI2_EVENT_IR_PHYSDISK_RC_SETTINGS_CHANGED:
  			mpr_dprint(sc, MPR_EVENT, "   Phys Disk Settings "
  			    "changed from 0x%x to 0x%x for Phys Disk Number "
  			    "%d and handle 0x%x at Enclosure handle 0x%x, Slot "
 			    "%d", le32toh(event_data->PreviousValue),
 			    le32toh(event_data->NewValue),
			    event_data->PhysDiskNum,
 			    le16toh(event_data->PhysDiskDevHandle),
 			    le16toh(event_data->EnclosureHandle),
			    le16toh(event_data->Slot));
			break;
		case MPI2_EVENT_IR_PHYSDISK_RC_STATUS_FLAGS_CHANGED:
  			mpr_dprint(sc, MPR_EVENT, "   Phys Disk Status changed "
  			    "from 0x%x to 0x%x for Phys Disk Number %d and "
  			    "handle 0x%x at Enclosure handle 0x%x, Slot %d",
 			    le32toh(event_data->PreviousValue),
 			    le32toh(event_data->NewValue),
			    event_data->PhysDiskNum,
 			    le16toh(event_data->PhysDiskDevHandle),
 			    le16toh(event_data->EnclosureHandle),
			    le16toh(event_data->Slot));
			break;
		case MPI2_EVENT_IR_PHYSDISK_RC_STATE_CHANGED:
  			mpr_dprint(sc, MPR_EVENT, "   Phys Disk State changed "
  			    "from 0x%x to 0x%x for Phys Disk Number %d and "
  			    "handle 0x%x at Enclosure handle 0x%x, Slot %d",
 			    le32toh(event_data->PreviousValue),
 			    le32toh(event_data->NewValue),
			    event_data->PhysDiskNum,
 			    le16toh(event_data->PhysDiskDevHandle),
 			    le16toh(event_data->EnclosureHandle),
			    le16toh(event_data->Slot));
			switch (event_data->NewValue) {
				case MPI2_RAID_PD_STATE_ONLINE:
				case MPI2_RAID_PD_STATE_DEGRADED:
				case MPI2_RAID_PD_STATE_REBUILDING:
				case MPI2_RAID_PD_STATE_OPTIMAL:
				case MPI2_RAID_PD_STATE_HOT_SPARE:
					targ = mprsas_find_target_by_handle(
					    sassc, 0,
					    event_data->PhysDiskDevHandle);
					if (targ) {
						targ->flags |=
						    MPR_TARGET_FLAGS_RAID_COMPONENT;
						printf("%s %d: Found Target "
						    "for handle 0x%x.\n", 
						    __func__, __LINE__ ,
						    event_data->
						    PhysDiskDevHandle);
					}
				break;
				case MPI2_RAID_PD_STATE_OFFLINE:
				case MPI2_RAID_PD_STATE_NOT_CONFIGURED:
				case MPI2_RAID_PD_STATE_NOT_COMPATIBLE:
				default:
					targ = mprsas_find_target_by_handle(
					    sassc, 0,
					    event_data->PhysDiskDevHandle);
					if (targ) {
						targ->flags |=
					    ~MPR_TARGET_FLAGS_RAID_COMPONENT;
						printf("%s %d: Found Target "
						    "for handle 0x%x.  \n",
						    __func__, __LINE__ ,
						    event_data->
						    PhysDiskDevHandle);
					}
				break;
			}
		default:
			break;
		}
		break;
	}
	case MPI2_EVENT_IR_OPERATION_STATUS:
	{
		Mpi2EventDataIrOperationStatus_t *event_data =
		    fw_event->event_data;

		/*
		 * Informational only.
		 */
		mpr_dprint(sc, MPR_EVENT, "Received IR Op Status event:\n");
		mpr_dprint(sc, MPR_EVENT, "   RAID Operation of %d is %d "
		    "percent complete for Volume with handle 0x%x",
		    event_data->RAIDOperation, event_data->PercentComplete,
		    le16toh(event_data->VolDevHandle));
		break;
	}
	case MPI2_EVENT_TEMP_THRESHOLD:
	{
		pMpi2EventDataTemperature_t	temp_event;

		temp_event = (pMpi2EventDataTemperature_t)fw_event->event_data;

		/*
		 * The Temp Sensor Count must be greater than the event's Sensor
		 * Num to be valid.  If valid, print the temp thresholds that
		 * have been exceeded.
		 */
		if (sc->iounit_pg8.NumSensors > temp_event->SensorNum) {
			mpr_dprint(sc, MPR_FAULT, "Temperature Threshold flags "
			    "%s %s %s %s exceeded for Sensor: %d !!!\n",
			    ((temp_event->Status & 0x01) == 1) ? "0 " : " ",
			    ((temp_event->Status & 0x02) == 2) ? "1 " : " ",
			    ((temp_event->Status & 0x04) == 4) ? "2 " : " ",
			    ((temp_event->Status & 0x08) == 8) ? "3 " : " ",
			    temp_event->SensorNum);
			mpr_dprint(sc, MPR_FAULT, "Current Temp in Celsius: "
			    "%d\n", temp_event->CurrentTemperature);
		}
		break;
	}
	case MPI2_EVENT_ACTIVE_CABLE_EXCEPTION:
	{
		pMpi26EventDataActiveCableExcept_t	ace_event_data;
		ace_event_data =
		    (pMpi26EventDataActiveCableExcept_t)fw_event->event_data;

		switch(ace_event_data->ReasonCode) {
		case MPI26_EVENT_ACTIVE_CABLE_INSUFFICIENT_POWER:
		{
			mpr_printf(sc, "Currently a cable with "
			    "ReceptacleID %d cannot be powered and device "
			    "connected to this active cable will not be seen. "
			    "This active cable requires %d mW of power.\n",
			    ace_event_data->ReceptacleID,
			    ace_event_data->ActiveCablePowerRequirement);
			break;
		}
		case MPI26_EVENT_ACTIVE_CABLE_DEGRADED:
		{
			mpr_printf(sc, "Currently a cable with "
			    "ReceptacleID %d is not running at optimal speed "
			    "(12 Gb/s rate)\n", ace_event_data->ReceptacleID);
			break;
		}
		default:
			break;
		}
		break;
	}
	case MPI2_EVENT_PCIE_DEVICE_STATUS_CHANGE:
	{
		pMpi26EventDataPCIeDeviceStatusChange_t	pcie_status_event_data;
		pcie_status_event_data =
		   (pMpi26EventDataPCIeDeviceStatusChange_t)fw_event->event_data;

		switch (pcie_status_event_data->ReasonCode) {
		case MPI26_EVENT_PCIDEV_STAT_RC_PCIE_HOT_RESET_FAILED:
		{
			mpr_printf(sc, "PCIe Host Reset failed on DevHandle "
			    "0x%x\n", pcie_status_event_data->DevHandle);
			break;
		}
		default:
			break;
		}
		break;
	}
	case MPI2_EVENT_SAS_DEVICE_DISCOVERY_ERROR:
	{
		pMpi25EventDataSasDeviceDiscoveryError_t discovery_error_data;
		uint64_t sas_address;

		discovery_error_data =
		    (pMpi25EventDataSasDeviceDiscoveryError_t)
		    fw_event->event_data;
		
		sas_address = discovery_error_data->SASAddress.High;
		sas_address = (sas_address << 32) |
		    discovery_error_data->SASAddress.Low;

		switch(discovery_error_data->ReasonCode) {
		case MPI25_EVENT_SAS_DISC_ERR_SMP_FAILED:
		{
			mpr_printf(sc, "SMP command failed during discovery "
			    "for expander with SAS Address %jx and "
			    "handle 0x%x.\n", sas_address,
			    discovery_error_data->DevHandle);
			break;
		}
		case MPI25_EVENT_SAS_DISC_ERR_SMP_TIMEOUT:
		{
			mpr_printf(sc, "SMP command timed out during "
			    "discovery for expander with SAS Address %jx and "
			    "handle 0x%x.\n", sas_address,
			    discovery_error_data->DevHandle);
			break;
		}
		default:
			break;
		}
		break;
	}
	case MPI2_EVENT_PCIE_TOPOLOGY_CHANGE_LIST: 
	{
		MPI26_EVENT_DATA_PCIE_TOPOLOGY_CHANGE_LIST *data;
		MPI26_EVENT_PCIE_TOPO_PORT_ENTRY *port_entry;
		uint8_t i, link_rate;
		uint16_t handle;

		data = (MPI26_EVENT_DATA_PCIE_TOPOLOGY_CHANGE_LIST *)
		    fw_event->event_data;

		mpr_mapping_pcie_topology_change_event(sc,
		    fw_event->event_data);

		for (i = 0; i < data->NumEntries; i++) {
			port_entry = &data->PortEntry[i];
			handle = le16toh(port_entry->AttachedDevHandle);
			link_rate = port_entry->CurrentPortInfo &
			    MPI26_EVENT_PCIE_TOPO_PI_RATE_MASK;
			switch (port_entry->PortStatus) {
			case MPI26_EVENT_PCIE_TOPO_PS_DEV_ADDED:
				if (link_rate <
				    MPI26_EVENT_PCIE_TOPO_PI_RATE_2_5) {
					mpr_dprint(sc, MPR_ERROR, "%s: Cannot "
					    "add PCIe device with handle 0x%x "
					    "with unknown link rate.\n",
					    __func__, handle);
					break;
				}
				if (mprsas_add_pcie_device(sc, handle,
				    link_rate)) {
					mpr_dprint(sc, MPR_ERROR, "%s: failed "
					    "to add PCIe device with handle "
					    "0x%x\n", __func__, handle);
					mprsas_prepare_remove(sassc, handle);
				}
				break;
			case MPI26_EVENT_PCIE_TOPO_PS_NOT_RESPONDING:
				mprsas_prepare_remove(sassc, handle);
				break;
			case MPI26_EVENT_PCIE_TOPO_PS_PORT_CHANGED:
			case MPI26_EVENT_PCIE_TOPO_PS_NO_CHANGE:
			case MPI26_EVENT_PCIE_TOPO_PS_DELAY_NOT_RESPONDING:
			default:
				break;
			}
		}
		/*
		 * refcount was incremented for this event in
		 * mprsas_evt_handler.  Decrement it here because the event has
		 * been processed.
		 */
		mprsas_startup_decrement(sassc);
		break;
	}
	case MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE:
	case MPI2_EVENT_SAS_BROADCAST_PRIMITIVE:
	default:
		mpr_dprint(sc, MPR_TRACE,"Unhandled event 0x%0X\n",
		    fw_event->event);
		break;

	}
	mpr_dprint(sc, MPR_EVENT, "(%d)->(%s) Event Free: [%x]\n", event_count,
	    __func__, fw_event->event);
	mprsas_fw_event_free(sc, fw_event);
}

void
mprsas_firmware_event_work(void *arg, int pending)
{
	struct mpr_fw_event_work *fw_event;
	struct mpr_softc *sc;

	sc = (struct mpr_softc *)arg;
	mpr_lock(sc);
	while ((fw_event = TAILQ_FIRST(&sc->sassc->ev_queue)) != NULL) {
		TAILQ_REMOVE(&sc->sassc->ev_queue, fw_event, ev_link);
		mprsas_fw_work(sc, fw_event);
	}
	mpr_unlock(sc);
}

static int
mprsas_add_device(struct mpr_softc *sc, u16 handle, u8 linkrate)
{
	char devstring[80];
	struct mprsas_softc *sassc;
	struct mprsas_target *targ;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t config_page;
	uint64_t sas_address, parent_sas_address = 0;
	u32 device_info, parent_devinfo = 0;
	unsigned int id;
	int ret = 1, error = 0, i;
	struct mprsas_lun *lun;
	u8 is_SATA_SSD = 0;
	struct mpr_command *cm;

	sassc = sc->sassc;
	mprsas_startup_increment(sassc);
	if (mpr_config_get_sas_device_pg0(sc, &mpi_reply, &config_page,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle) != 0) {
		mpr_dprint(sc, MPR_INFO|MPR_MAPPING|MPR_FAULT,
		    "Error reading SAS device %#x page0, iocstatus= 0x%x\n",
		    handle, mpi_reply.IOCStatus);
		error = ENXIO;
		goto out;
	}

	device_info = le32toh(config_page.DeviceInfo);

	if (((device_info & MPI2_SAS_DEVICE_INFO_SMP_TARGET) == 0)
	    && (le16toh(config_page.ParentDevHandle) != 0)) {
		Mpi2ConfigReply_t tmp_mpi_reply;
		Mpi2SasDevicePage0_t parent_config_page;

		if (mpr_config_get_sas_device_pg0(sc, &tmp_mpi_reply,
		    &parent_config_page, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
		    le16toh(config_page.ParentDevHandle)) != 0) {
			mpr_dprint(sc, MPR_MAPPING|MPR_FAULT,
			    "Error reading parent SAS device %#x page0, "
			    "iocstatus= 0x%x\n",
			    le16toh(config_page.ParentDevHandle),
			    tmp_mpi_reply.IOCStatus);
		} else {
			parent_sas_address = parent_config_page.SASAddress.High;
			parent_sas_address = (parent_sas_address << 32) |
			    parent_config_page.SASAddress.Low;
			parent_devinfo = le32toh(parent_config_page.DeviceInfo);
		}
	}
	/* TODO Check proper endianness */
	sas_address = config_page.SASAddress.High;
	sas_address = (sas_address << 32) | config_page.SASAddress.Low;
	mpr_dprint(sc, MPR_MAPPING, "Handle 0x%04x SAS Address from SAS device "
	    "page0 = %jx\n", handle, sas_address);

	/*
	 * Always get SATA Identify information because this is used to
	 * determine if Start/Stop Unit should be sent to the drive when the
	 * system is shutdown.
	 */
	if (device_info & MPI2_SAS_DEVICE_INFO_SATA_DEVICE) {
		ret = mprsas_get_sas_address_for_sata_disk(sc, &sas_address,
		    handle, device_info, &is_SATA_SSD);
		if (ret) {
			mpr_dprint(sc, MPR_MAPPING|MPR_ERROR,
			    "%s: failed to get disk type (SSD or HDD) for SATA "
			    "device with handle 0x%04x\n",
			    __func__, handle);
		} else {
			mpr_dprint(sc, MPR_MAPPING, "Handle 0x%04x SAS Address "
			    "from SATA device = %jx\n", handle, sas_address);
		}
	}

	/*
	 * use_phynum:
	 *  1 - use the PhyNum field as a fallback to the mapping logic
	 *  0 - never use the PhyNum field
	 * -1 - only use the PhyNum field
	 *
	 * Note that using the Phy number to map a device can cause device adds
	 * to fail if multiple enclosures/expanders are in the topology. For
	 * example, if two devices are in the same slot number in two different
	 * enclosures within the topology, only one of those devices will be
	 * added. PhyNum mapping should not be used if multiple enclosures are
	 * in the topology.
	 */
	id = MPR_MAP_BAD_ID;
	if (sc->use_phynum != -1) 
		id = mpr_mapping_get_tid(sc, sas_address, handle);
	if (id == MPR_MAP_BAD_ID) {
		if ((sc->use_phynum == 0) ||
		    ((id = config_page.PhyNum) > sassc->maxtargets)) {
			mpr_dprint(sc, MPR_INFO, "failure at %s:%d/%s()! "
			    "Could not get ID for device with handle 0x%04x\n",
			    __FILE__, __LINE__, __func__, handle);
			error = ENXIO;
			goto out;
		}
	}
	mpr_dprint(sc, MPR_MAPPING, "%s: Target ID for added device is %d.\n",
	    __func__, id);

	/*
	 * Only do the ID check and reuse check if the target is not from a
	 * RAID Component. For Physical Disks of a Volume, the ID will be reused
	 * when a volume is deleted because the mapping entry for the PD will
	 * still be in the mapping table. The ID check should not be done here
	 * either since this PD is already being used.
	 */
	targ = &sassc->targets[id];
	if (!(targ->flags & MPR_TARGET_FLAGS_RAID_COMPONENT)) {
		if (mprsas_check_id(sassc, id) != 0) {
			mpr_dprint(sc, MPR_MAPPING|MPR_INFO,
			    "Excluding target id %d\n", id);
			error = ENXIO;
			goto out;
		}

		if (targ->handle != 0x0) {
			mpr_dprint(sc, MPR_MAPPING, "Attempting to reuse "
			    "target id %d handle 0x%04x\n", id, targ->handle);
			error = ENXIO;
			goto out;
		}
	}

	targ->devinfo = device_info;
	targ->devname = le32toh(config_page.DeviceName.High);
	targ->devname = (targ->devname << 32) | 
	    le32toh(config_page.DeviceName.Low);
	targ->encl_handle = le16toh(config_page.EnclosureHandle);
	targ->encl_slot = le16toh(config_page.Slot);
	targ->encl_level = config_page.EnclosureLevel;
	targ->connector_name[0] = config_page.ConnectorName[0];
	targ->connector_name[1] = config_page.ConnectorName[1];
	targ->connector_name[2] = config_page.ConnectorName[2];
	targ->connector_name[3] = config_page.ConnectorName[3];
	targ->handle = handle;
	targ->parent_handle = le16toh(config_page.ParentDevHandle);
	targ->sasaddr = mpr_to_u64(&config_page.SASAddress);
	targ->parent_sasaddr = le64toh(parent_sas_address);
	targ->parent_devinfo = parent_devinfo;
	targ->tid = id;
	targ->linkrate = (linkrate>>4);
	targ->flags = 0;
	if (is_SATA_SSD) {
		targ->flags = MPR_TARGET_IS_SATA_SSD;
	}
	if ((le16toh(config_page.Flags) &
	    MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH) &&
	    (le16toh(config_page.Flags) &
	    MPI25_SAS_DEVICE0_FLAGS_FAST_PATH_CAPABLE)) {
		targ->scsi_req_desc_type =
		    MPI25_REQ_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO;
	}
	if (le16toh(config_page.Flags) &
	    MPI2_SAS_DEVICE0_FLAGS_ENCL_LEVEL_VALID) {
		targ->encl_level_valid = TRUE;
	}
	TAILQ_INIT(&targ->commands);
	TAILQ_INIT(&targ->timedout_commands);
	while (!SLIST_EMPTY(&targ->luns)) {
		lun = SLIST_FIRST(&targ->luns);
		SLIST_REMOVE_HEAD(&targ->luns, lun_link);
		free(lun, M_MPR);
	}
	SLIST_INIT(&targ->luns);

	mpr_describe_devinfo(targ->devinfo, devstring, 80);
	mpr_dprint(sc, (MPR_INFO|MPR_MAPPING), "Found device <%s> <%s> "
	    "handle<0x%04x> enclosureHandle<0x%04x> slot %d\n", devstring,
	    mpr_describe_table(mpr_linkrate_names, targ->linkrate),
	    targ->handle, targ->encl_handle, targ->encl_slot);
	if (targ->encl_level_valid) {
		mpr_dprint(sc, (MPR_INFO|MPR_MAPPING), "At enclosure level %d "
		    "and connector name (%4s)\n", targ->encl_level,
		    targ->connector_name);
	}
#if ((__FreeBSD_version >= 1000000) && (__FreeBSD_version < 1000039)) || \
    (__FreeBSD_version < 902502)
	if ((sassc->flags & MPRSAS_IN_STARTUP) == 0)
#endif
		mprsas_rescan_target(sc, targ);
	mpr_dprint(sc, MPR_MAPPING, "Target id 0x%x added\n", targ->tid);

	/*
	 * Check all commands to see if the SATA_ID_TIMEOUT flag has been set.
	 * If so, send a Target Reset TM to the target that was just created.
	 * An Abort Task TM should be used instead of a Target Reset, but that
	 * would be much more difficult because targets have not been fully
	 * discovered yet, and LUN's haven't been setup.  So, just reset the
	 * target instead of the LUN.
	 */
	for (i = 1; i < sc->num_reqs; i++) {
		cm = &sc->commands[i];
		if (cm->cm_flags & MPR_CM_FLAGS_SATA_ID_TIMEOUT) {
			targ->timeouts++;
			cm->cm_state = MPR_CM_STATE_TIMEDOUT;

			if ((targ->tm = mprsas_alloc_tm(sc)) != NULL) {
				mpr_dprint(sc, MPR_INFO, "%s: sending Target "
				    "Reset for stuck SATA identify command "
				    "(cm = %p)\n", __func__, cm);
				targ->tm->cm_targ = targ;
				mprsas_send_reset(sc, targ->tm,
				    MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET);
			} else {
				mpr_dprint(sc, MPR_ERROR, "Failed to allocate "
				    "tm for Target Reset after SATA ID command "
				    "timed out (cm %p)\n", cm);
			}
			/*
			 * No need to check for more since the target is
			 * already being reset.
			 */
			break;
		}
	}
out:
	/*
	 * Free the commands that may not have been freed from the SATA ID call
	 */
	for (i = 1; i < sc->num_reqs; i++) {
		cm = &sc->commands[i];
		if (cm->cm_flags & MPR_CM_FLAGS_SATA_ID_TIMEOUT) {
			free(cm->cm_data, M_MPR);
			mpr_free_command(sc, cm);
		}
	}
	mprsas_startup_decrement(sassc);
	return (error);
}

int
mprsas_get_sas_address_for_sata_disk(struct mpr_softc *sc,
    u64 *sas_address, u16 handle, u32 device_info, u8 *is_SATA_SSD)
{
	Mpi2SataPassthroughReply_t mpi_reply;
	int i, rc, try_count;
	u32 *bufferptr;
	union _sata_sas_address hash_address;
	struct _ata_identify_device_data ata_identify;
	u8 buffer[MPT2SAS_MN_LEN + MPT2SAS_SN_LEN];
	u32 ioc_status;
	u8 sas_status;

	memset(&ata_identify, 0, sizeof(ata_identify));
	memset(&mpi_reply, 0, sizeof(mpi_reply));
	try_count = 0;
	do {
		rc = mprsas_get_sata_identify(sc, handle, &mpi_reply,
		    (char *)&ata_identify, sizeof(ata_identify), device_info);
		try_count++;
		ioc_status = le16toh(mpi_reply.IOCStatus)
		    & MPI2_IOCSTATUS_MASK;
		sas_status = mpi_reply.SASStatus;
		switch (ioc_status) {
		case MPI2_IOCSTATUS_SUCCESS:
			break;
		case MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR:
			/* No sense sleeping.  this error won't get better */
			break;
		default:
			if (sc->spinup_wait_time > 0) {
				mpr_dprint(sc, MPR_INFO, "Sleeping %d seconds "
				    "after SATA ID error to wait for spinup\n",
				    sc->spinup_wait_time);
				msleep(&sc->msleep_fake_chan, &sc->mpr_mtx, 0,
				    "mprid", sc->spinup_wait_time * hz);
			}
		}
	} while (((rc && (rc != EWOULDBLOCK)) ||
	    (ioc_status && (ioc_status != MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR))
	    || sas_status) && (try_count < 5));

	if (rc == 0 && !ioc_status && !sas_status) {
		mpr_dprint(sc, MPR_MAPPING, "%s: got SATA identify "
		    "successfully for handle = 0x%x with try_count = %d\n",
		    __func__, handle, try_count);
	} else {
		mpr_dprint(sc, MPR_MAPPING, "%s: handle = 0x%x failed\n",
		    __func__, handle);
		return -1;
	}
	/* Copy & byteswap the 40 byte model number to a buffer */
	for (i = 0; i < MPT2SAS_MN_LEN; i += 2) {
		buffer[i] = ((u8 *)ata_identify.model_number)[i + 1];
		buffer[i + 1] = ((u8 *)ata_identify.model_number)[i];
	}
	/* Copy & byteswap the 20 byte serial number to a buffer */
	for (i = 0; i < MPT2SAS_SN_LEN; i += 2) {
		buffer[MPT2SAS_MN_LEN + i] =
		    ((u8 *)ata_identify.serial_number)[i + 1];
		buffer[MPT2SAS_MN_LEN + i + 1] =
		    ((u8 *)ata_identify.serial_number)[i];
	}
	bufferptr = (u32 *)buffer;
	/* There are 60 bytes to hash down to 8. 60 isn't divisible by 8,
	 * so loop through the first 56 bytes (7*8),
	 * and then add in the last dword.
	 */
	hash_address.word.low  = 0;
	hash_address.word.high = 0;
	for (i = 0; (i < ((MPT2SAS_MN_LEN+MPT2SAS_SN_LEN)/8)); i++) {
		hash_address.word.low += *bufferptr;
		bufferptr++;
		hash_address.word.high += *bufferptr;
		bufferptr++;
	}
	/* Add the last dword */
	hash_address.word.low += *bufferptr;
	/* Make sure the hash doesn't start with 5, because it could clash
	 * with a SAS address. Change 5 to a D.
	 */
	if ((hash_address.word.high & 0x000000F0) == (0x00000050))
		hash_address.word.high |= 0x00000080;
	*sas_address = (u64)hash_address.wwid[0] << 56 |
	    (u64)hash_address.wwid[1] << 48 | (u64)hash_address.wwid[2] << 40 |
	    (u64)hash_address.wwid[3] << 32 | (u64)hash_address.wwid[4] << 24 |
	    (u64)hash_address.wwid[5] << 16 | (u64)hash_address.wwid[6] <<  8 |
	    (u64)hash_address.wwid[7];
	if (ata_identify.rotational_speed == 1) {
		*is_SATA_SSD = 1;
	}

	return 0;
}

static int
mprsas_get_sata_identify(struct mpr_softc *sc, u16 handle,
    Mpi2SataPassthroughReply_t *mpi_reply, char *id_buffer, int sz, u32 devinfo)
{
	Mpi2SataPassthroughRequest_t *mpi_request;
	Mpi2SataPassthroughReply_t *reply;
	struct mpr_command *cm;
	char *buffer;
	int error = 0;

	buffer = malloc( sz, M_MPR, M_NOWAIT | M_ZERO);
	if (!buffer)
		return ENOMEM;

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		free(buffer, M_MPR);
		return (EBUSY);
	}
	mpi_request = (MPI2_SATA_PASSTHROUGH_REQUEST *)cm->cm_req;
	bzero(mpi_request,sizeof(MPI2_SATA_PASSTHROUGH_REQUEST));
	mpi_request->Function = MPI2_FUNCTION_SATA_PASSTHROUGH;
	mpi_request->VF_ID = 0;
	mpi_request->DevHandle = htole16(handle);
	mpi_request->PassthroughFlags = (MPI2_SATA_PT_REQ_PT_FLAGS_PIO |
	    MPI2_SATA_PT_REQ_PT_FLAGS_READ);
	mpi_request->DataLength = htole32(sz);
	mpi_request->CommandFIS[0] = 0x27;
	mpi_request->CommandFIS[1] = 0x80;
	mpi_request->CommandFIS[2] =  (devinfo &
	    MPI2_SAS_DEVICE_INFO_ATAPI_DEVICE) ? 0xA1 : 0xEC;
	cm->cm_sge = &mpi_request->SGL;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPR_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = buffer;
	cm->cm_length = htole32(sz);

	/*
	 * Use a custom handler to avoid reinit'ing the controller on timeout.
	 * This fixes a problem where the FW does not send a reply sometimes
	 * when a bad disk is in the topology. So, this is used to timeout the
	 * command so that processing can continue normally.
	 */
	cm->cm_timeout_handler = mprsas_ata_id_timeout;

	error = mpr_wait_command(sc, &cm, MPR_ATA_ID_TIMEOUT, CAN_SLEEP);

	/* mprsas_ata_id_timeout does not reset controller */
	KASSERT(cm != NULL, ("%s: surprise command freed", __func__));

	reply = (Mpi2SataPassthroughReply_t *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */
		mpr_dprint(sc, MPR_INFO|MPR_FAULT|MPR_MAPPING,
		    "Request for SATA PASSTHROUGH page completed with error %d",
		    error);
		error = ENXIO;
		goto out;
	}
	bcopy(buffer, id_buffer, sz);
	bcopy(reply, mpi_reply, sizeof(Mpi2SataPassthroughReply_t));
	if ((le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK) !=
	    MPI2_IOCSTATUS_SUCCESS) {
		mpr_dprint(sc, MPR_INFO|MPR_MAPPING|MPR_FAULT,
		    "Error reading device %#x SATA PASSTHRU; iocstatus= 0x%x\n",
		    handle, reply->IOCStatus);
		error = ENXIO;
		goto out;
	}
out:
	/*
	 * If the SATA_ID_TIMEOUT flag has been set for this command, don't free
	 * it.  The command and buffer will be freed after sending an Abort
	 * Task TM.
	 */
	if ((cm->cm_flags & MPR_CM_FLAGS_SATA_ID_TIMEOUT) == 0) {
		mpr_free_command(sc, cm);
		free(buffer, M_MPR);
	}
	return (error);
}

static void
mprsas_ata_id_timeout(struct mpr_softc *sc, struct mpr_command *cm)
{

	mpr_dprint(sc, MPR_INFO, "%s ATA ID command timeout cm %p sc %p\n",
	    __func__, cm, sc);

	/*
	 * The Abort Task cannot be sent from here because the driver has not
	 * completed setting up targets.  Instead, the command is flagged so
	 * that special handling will be used to send the abort.
	 */
	cm->cm_flags |= MPR_CM_FLAGS_SATA_ID_TIMEOUT;
}

static int
mprsas_add_pcie_device(struct mpr_softc *sc, u16 handle, u8 linkrate)
{
	char devstring[80];
	struct mprsas_softc *sassc;
	struct mprsas_target *targ;
	Mpi2ConfigReply_t mpi_reply;
	Mpi26PCIeDevicePage0_t config_page;
	Mpi26PCIeDevicePage2_t config_page2;
	uint64_t pcie_wwid, parent_wwid = 0;
	u32 device_info, parent_devinfo = 0;
	unsigned int id;
	int error = 0;
	struct mprsas_lun *lun;

	sassc = sc->sassc;
	mprsas_startup_increment(sassc);
	if ((mpr_config_get_pcie_device_pg0(sc, &mpi_reply, &config_page,
	     MPI26_PCIE_DEVICE_PGAD_FORM_HANDLE, handle))) {
		printf("%s: error reading PCIe device page0\n", __func__);
		error = ENXIO;
		goto out;
	}

	device_info = le32toh(config_page.DeviceInfo);

	if (((device_info & MPI26_PCIE_DEVINFO_PCI_SWITCH) == 0)
	    && (le16toh(config_page.ParentDevHandle) != 0)) {
		Mpi2ConfigReply_t tmp_mpi_reply;
		Mpi26PCIeDevicePage0_t parent_config_page;

		if ((mpr_config_get_pcie_device_pg0(sc, &tmp_mpi_reply,
		     &parent_config_page, MPI26_PCIE_DEVICE_PGAD_FORM_HANDLE,
		     le16toh(config_page.ParentDevHandle)))) {
			printf("%s: error reading PCIe device %#x page0\n",
			    __func__, le16toh(config_page.ParentDevHandle));
		} else {
			parent_wwid = parent_config_page.WWID.High;
			parent_wwid = (parent_wwid << 32) |
			    parent_config_page.WWID.Low;
			parent_devinfo = le32toh(parent_config_page.DeviceInfo);
		}
	}
	/* TODO Check proper endianness */
	pcie_wwid = config_page.WWID.High;
	pcie_wwid = (pcie_wwid << 32) | config_page.WWID.Low;
	mpr_dprint(sc, MPR_INFO, "PCIe WWID from PCIe device page0 = %jx\n",
	    pcie_wwid);

	if ((mpr_config_get_pcie_device_pg2(sc, &mpi_reply, &config_page2,
	     MPI26_PCIE_DEVICE_PGAD_FORM_HANDLE, handle))) {
		printf("%s: error reading PCIe device page2\n", __func__);
		error = ENXIO;
		goto out;
	}

	id = mpr_mapping_get_tid(sc, pcie_wwid, handle);
	if (id == MPR_MAP_BAD_ID) {
		mpr_dprint(sc, MPR_ERROR | MPR_INFO, "failure at %s:%d/%s()! "
		    "Could not get ID for device with handle 0x%04x\n",
		    __FILE__, __LINE__, __func__, handle);
		error = ENXIO;
		goto out;
	}
	mpr_dprint(sc, MPR_MAPPING, "%s: Target ID for added device is %d.\n",
	    __func__, id);

	if (mprsas_check_id(sassc, id) != 0) {
		mpr_dprint(sc, MPR_MAPPING|MPR_INFO,
		    "Excluding target id %d\n", id);
		error = ENXIO;
		goto out;
	}

	mpr_dprint(sc, MPR_MAPPING, "WWID from PCIe device page0 = %jx\n",
	    pcie_wwid);
	targ = &sassc->targets[id];
	targ->devinfo = device_info;
	targ->encl_handle = le16toh(config_page.EnclosureHandle);
	targ->encl_slot = le16toh(config_page.Slot);
	targ->encl_level = config_page.EnclosureLevel;
	targ->connector_name[0] = ((char *)&config_page.ConnectorName)[0];
	targ->connector_name[1] = ((char *)&config_page.ConnectorName)[1];
	targ->connector_name[2] = ((char *)&config_page.ConnectorName)[2];
	targ->connector_name[3] = ((char *)&config_page.ConnectorName)[3];
	targ->is_nvme = device_info & MPI26_PCIE_DEVINFO_NVME;
	targ->MDTS = config_page2.MaximumDataTransferSize;
	if (targ->is_nvme)
		targ->controller_reset_timeout = config_page2.ControllerResetTO;
	/*
	 * Assume always TRUE for encl_level_valid because there is no valid
	 * flag for PCIe.
	 */
	targ->encl_level_valid = TRUE;
	targ->handle = handle;
	targ->parent_handle = le16toh(config_page.ParentDevHandle);
	targ->sasaddr = mpr_to_u64(&config_page.WWID);
	targ->parent_sasaddr = le64toh(parent_wwid);
	targ->parent_devinfo = parent_devinfo;
	targ->tid = id;
	targ->linkrate = linkrate;
	targ->flags = 0;
	if ((le16toh(config_page.Flags) &
	    MPI26_PCIEDEV0_FLAGS_ENABLED_FAST_PATH) && 
	    (le16toh(config_page.Flags) &
	    MPI26_PCIEDEV0_FLAGS_FAST_PATH_CAPABLE)) {
		targ->scsi_req_desc_type =
		    MPI25_REQ_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO;
	}
	TAILQ_INIT(&targ->commands);
	TAILQ_INIT(&targ->timedout_commands);
	while (!SLIST_EMPTY(&targ->luns)) {
		lun = SLIST_FIRST(&targ->luns);
		SLIST_REMOVE_HEAD(&targ->luns, lun_link);
		free(lun, M_MPR);
	}
	SLIST_INIT(&targ->luns);

	mpr_describe_devinfo(targ->devinfo, devstring, 80);
	mpr_dprint(sc, (MPR_INFO|MPR_MAPPING), "Found PCIe device <%s> <%s> "
	    "handle<0x%04x> enclosureHandle<0x%04x> slot %d\n", devstring,
	    mpr_describe_table(mpr_pcie_linkrate_names, targ->linkrate),
	    targ->handle, targ->encl_handle, targ->encl_slot);
	if (targ->encl_level_valid) {
		mpr_dprint(sc, (MPR_INFO|MPR_MAPPING), "At enclosure level %d "
		    "and connector name (%4s)\n", targ->encl_level,
		    targ->connector_name);
	}
#if ((__FreeBSD_version >= 1000000) && (__FreeBSD_version < 1000039)) || \
    (__FreeBSD_version < 902502)
	if ((sassc->flags & MPRSAS_IN_STARTUP) == 0)
#endif
		mprsas_rescan_target(sc, targ);
	mpr_dprint(sc, MPR_MAPPING, "Target id 0x%x added\n", targ->tid);

out:
	mprsas_startup_decrement(sassc);
	return (error);
}

static int
mprsas_volume_add(struct mpr_softc *sc, u16 handle)
{
	struct mprsas_softc *sassc;
	struct mprsas_target *targ;
	u64 wwid;
	unsigned int id;
	int error = 0;
	struct mprsas_lun *lun;

	sassc = sc->sassc;
	mprsas_startup_increment(sassc);
	/* wwid is endian safe */
	mpr_config_get_volume_wwid(sc, handle, &wwid);
	if (!wwid) {
		printf("%s: invalid WWID; cannot add volume to mapping table\n",
		    __func__);
		error = ENXIO;
		goto out;
	}

	id = mpr_mapping_get_raid_tid(sc, wwid, handle);
	if (id == MPR_MAP_BAD_ID) {
		printf("%s: could not get ID for volume with handle 0x%04x and "
		    "WWID 0x%016llx\n", __func__, handle,
		    (unsigned long long)wwid);
		error = ENXIO;
		goto out;
	}

	targ = &sassc->targets[id];
	targ->tid = id;
	targ->handle = handle;
	targ->devname = wwid;
	TAILQ_INIT(&targ->commands);
	TAILQ_INIT(&targ->timedout_commands);
	while (!SLIST_EMPTY(&targ->luns)) {
		lun = SLIST_FIRST(&targ->luns);
		SLIST_REMOVE_HEAD(&targ->luns, lun_link);
		free(lun, M_MPR);
	}
	SLIST_INIT(&targ->luns);
#if ((__FreeBSD_version >= 1000000) && (__FreeBSD_version < 1000039)) || \
    (__FreeBSD_version < 902502)
	if ((sassc->flags & MPRSAS_IN_STARTUP) == 0)
#endif
		mprsas_rescan_target(sc, targ);
	mpr_dprint(sc, MPR_MAPPING, "RAID target id %d added (WWID = 0x%jx)\n",
	    targ->tid, wwid);
out:
	mprsas_startup_decrement(sassc);
	return (error);
}

/**
 * mprsas_SSU_to_SATA_devices 
 * @sc: per adapter object
 *
 * Looks through the target list and issues a StartStopUnit SCSI command to each
 * SATA direct-access device.  This helps to ensure that data corruption is
 * avoided when the system is being shut down.  This must be called after the IR
 * System Shutdown RAID Action is sent if in IR mode.
 *
 * Return nothing.
 */
static void
mprsas_SSU_to_SATA_devices(struct mpr_softc *sc, int howto)
{
	struct mprsas_softc *sassc = sc->sassc;
	union ccb *ccb;
	path_id_t pathid = cam_sim_path(sassc->sim);
	target_id_t targetid;
	struct mprsas_target *target;
	char path_str[64];
	int timeout;

	mpr_lock(sc);

	/*
	 * For each target, issue a StartStopUnit command to stop the device.
	 */
	sc->SSU_started = TRUE;
	sc->SSU_refcount = 0;
	for (targetid = 0; targetid < sc->max_devices; targetid++) {
		target = &sassc->targets[targetid];
		if (target->handle == 0x0) {
			continue;
		}

		/*
		 * The stop_at_shutdown flag will be set if this device is
		 * a SATA direct-access end device.
		 */
		if (target->stop_at_shutdown) {
			ccb = xpt_alloc_ccb_nowait();
			if (ccb == NULL) {
				mpr_dprint(sc, MPR_FAULT, "Unable to alloc CCB "
				    "to stop unit.\n");
				return;
			}

			if (xpt_create_path(&ccb->ccb_h.path, xpt_periph,
			    pathid, targetid, CAM_LUN_WILDCARD) !=
			    CAM_REQ_CMP) {
				mpr_dprint(sc, MPR_ERROR, "Unable to create "
				    "path to stop unit.\n");
				xpt_free_ccb(ccb);
				return;
			}
			xpt_path_string(ccb->ccb_h.path, path_str,
			    sizeof(path_str));

			mpr_dprint(sc, MPR_INFO, "Sending StopUnit: path %s "
			    "handle %d\n", path_str, target->handle);

			/*
			 * Issue a START STOP UNIT command for the target.
			 * Increment the SSU counter to be used to count the
			 * number of required replies.
			 */
			mpr_dprint(sc, MPR_INFO, "Incrementing SSU count\n");
			sc->SSU_refcount++;
			ccb->ccb_h.target_id =
			    xpt_path_target_id(ccb->ccb_h.path);
			ccb->ccb_h.ppriv_ptr1 = sassc;
			scsi_start_stop(&ccb->csio,
			    /*retries*/0,
			    mprsas_stop_unit_done,
			    MSG_SIMPLE_Q_TAG,
			    /*start*/FALSE,
			    /*load/eject*/0,
			    /*immediate*/FALSE,
			    MPR_SENSE_LEN,
			    /*timeout*/10000);
			xpt_action(ccb);
		}
	}

	mpr_unlock(sc);

	/*
	 * Timeout after 60 seconds by default or 10 seconds if howto has
	 * RB_NOSYNC set which indicates we're likely handling a panic.
	 */
	timeout = 600;
	if (howto & RB_NOSYNC)
		timeout = 100;

	/*
	 * Wait until all of the SSU commands have completed or time
	 * has expired. Pause for 100ms each time through.  If any
	 * command times out, the target will be reset in the SCSI
	 * command timeout routine.
	 */
	while (sc->SSU_refcount > 0) {
		pause("mprwait", hz/10);
		if (SCHEDULER_STOPPED())
			xpt_sim_poll(sassc->sim);
		
		if (--timeout == 0) {
			mpr_dprint(sc, MPR_ERROR, "Time has expired waiting "
			    "for SSU commands to complete.\n");
			break;
		}
	}
}

static void
mprsas_stop_unit_done(struct cam_periph *periph, union ccb *done_ccb)
{
	struct mprsas_softc *sassc;
	char path_str[64];

	if (done_ccb == NULL)
		return;

	sassc = (struct mprsas_softc *)done_ccb->ccb_h.ppriv_ptr1;

	xpt_path_string(done_ccb->ccb_h.path, path_str, sizeof(path_str));
	mpr_dprint(sassc->sc, MPR_INFO, "Completing stop unit for %s\n",
	    path_str);

	/*
	 * Nothing more to do except free the CCB and path.  If the command
	 * timed out, an abort reset, then target reset will be issued during
	 * the SCSI Command process.
	 */
	xpt_free_path(done_ccb->ccb_h.path);
	xpt_free_ccb(done_ccb);
}

/**
 * mprsas_ir_shutdown - IR shutdown notification
 * @sc: per adapter object
 *
 * Sending RAID Action to alert the Integrated RAID subsystem of the IOC that
 * the host system is shutting down.
 *
 * Return nothing.
 */
void
mprsas_ir_shutdown(struct mpr_softc *sc, int howto)
{
	u16 volume_mapping_flags;
	u16 ioc_pg8_flags = le16toh(sc->ioc_pg8.Flags);
	struct dev_mapping_table *mt_entry;
	u32 start_idx, end_idx;
	unsigned int id, found_volume = 0;
	struct mpr_command *cm;
	Mpi2RaidActionRequest_t	*action;
	target_id_t targetid;
	struct mprsas_target *target;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	/* is IR firmware build loaded? */
	if (!sc->ir_firmware)
		goto out;

	/* are there any volumes?  Look at IR target IDs. */
	// TODO-later, this should be looked up in the RAID config structure
	// when it is implemented.
	volume_mapping_flags = le16toh(sc->ioc_pg8.IRVolumeMappingFlags) &
	    MPI2_IOCPAGE8_IRFLAGS_MASK_VOLUME_MAPPING_MODE;
	if (volume_mapping_flags == MPI2_IOCPAGE8_IRFLAGS_LOW_VOLUME_MAPPING) {
		start_idx = 0;
		if (ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_RESERVED_TARGETID_0)
			start_idx = 1;
	} else
		start_idx = sc->max_devices - sc->max_volumes;
	end_idx = start_idx + sc->max_volumes - 1;

	for (id = start_idx; id < end_idx; id++) {
		mt_entry = &sc->mapping_table[id];
		if ((mt_entry->physical_id != 0) &&
		    (mt_entry->missing_count == 0)) {
			found_volume = 1;
			break;
		}
	}

	if (!found_volume)
		goto out;

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed\n", __func__);
		goto out;
	}

	action = (MPI2_RAID_ACTION_REQUEST *)cm->cm_req;
	action->Function = MPI2_FUNCTION_RAID_ACTION;
	action->Action = MPI2_RAID_ACTION_SYSTEM_SHUTDOWN_INITIATED;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	mpr_lock(sc);
	mpr_wait_command(sc, &cm, 5, CAN_SLEEP);
	mpr_unlock(sc);

	/*
	 * Don't check for reply, just leave.
	 */
	if (cm)
		mpr_free_command(sc, cm);

out:
	/*
	 * All of the targets must have the correct value set for
	 * 'stop_at_shutdown' for the current 'enable_ssu' sysctl variable.
	 *
	 * The possible values for the 'enable_ssu' variable are:
	 * 0: disable to SSD and HDD
	 * 1: disable only to HDD (default)
	 * 2: disable only to SSD
	 * 3: enable to SSD and HDD
	 * anything else will default to 1.
	 */
	for (targetid = 0; targetid < sc->max_devices; targetid++) {
		target = &sc->sassc->targets[targetid];
		if (target->handle == 0x0) {
			continue;
		}

		if (target->supports_SSU) {
			switch (sc->enable_ssu) {
			case MPR_SSU_DISABLE_SSD_DISABLE_HDD:
				target->stop_at_shutdown = FALSE;
				break;
			case MPR_SSU_DISABLE_SSD_ENABLE_HDD:
				target->stop_at_shutdown = TRUE;
				if (target->flags & MPR_TARGET_IS_SATA_SSD) {
					target->stop_at_shutdown = FALSE;
				}
				break;
			case MPR_SSU_ENABLE_SSD_ENABLE_HDD:
				target->stop_at_shutdown = TRUE;
				break;
			case MPR_SSU_ENABLE_SSD_DISABLE_HDD:
			default:
				target->stop_at_shutdown = TRUE;
				if ((target->flags &
				    MPR_TARGET_IS_SATA_SSD) == 0) {
					target->stop_at_shutdown = FALSE;
				}
				break;
			}
		}
	}
	mprsas_SSU_to_SATA_devices(sc, howto);
}
