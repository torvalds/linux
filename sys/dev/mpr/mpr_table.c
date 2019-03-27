/*-
 * Copyright (c) 2009 Yahoo! Inc.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Debugging tables for MPT2 */

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
#include <sys/queue.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <cam/scsi/scsi_all.h>

#include <dev/mpr/mpi/mpi2_type.h>
#include <dev/mpr/mpi/mpi2.h>
#include <dev/mpr/mpi/mpi2_ioc.h>
#include <dev/mpr/mpi/mpi2_cnfg.h>
#include <dev/mpr/mpi/mpi2_init.h>
#include <dev/mpr/mpi/mpi2_tool.h>
#include <dev/mpr/mpi/mpi2_pci.h>
#include <dev/mpr/mpr_ioctl.h>
#include <dev/mpr/mprvar.h>
#include <dev/mpr/mpr_table.h>

char *
mpr_describe_table(struct mpr_table_lookup *table, u_int code)
{
	int i;

	for (i = 0; table[i].string != NULL; i++) {
		if (table[i].code == code)
			return(table[i].string);
	}
	return(table[i+1].string);
}

//SLM-Add new PCIe info to all of these tables
struct mpr_table_lookup mpr_event_names[] = {
	{"LogData",			0x01},
	{"StateChange",			0x02},
	{"HardResetReceived",		0x05},
	{"EventChange",			0x0a},
	{"TaskSetFull",			0x0e},
	{"SasDeviceStatusChange",	0x0f},
	{"IrOperationStatus",		0x14},
	{"SasDiscovery",		0x16},
	{"SasBroadcastPrimitive",	0x17},
	{"SasInitDeviceStatusChange",	0x18},
	{"SasInitTableOverflow",	0x19},
	{"SasTopologyChangeList",	0x1c},
	{"SasEnclDeviceStatusChange",	0x1d},
	{"IrVolume",			0x1e},
	{"IrPhysicalDisk",		0x1f},
	{"IrConfigurationChangeList",	0x20},
	{"LogEntryAdded",		0x21},
	{"SasPhyCounter",		0x22},
	{"GpioInterrupt",		0x23},
	{"HbdPhyEvent",			0x24},
	{"SasQuiesce",			0x25},
	{"SasNotifyPrimitive",		0x26},
	{"TempThreshold",		0x27},
	{"HostMessage",			0x28},
	{"PowerPerformanceChange",	0x29},
	{"PCIeDeviceStatusChange",	0x30},
	{"PCIeEnumeration",		0x31},
	{"PCIeTopologyChangeList",	0x32},
	{"PCIeLinkCounter",		0x33},
	{"CableEvent",			0x34},
	{NULL, 0},
	{"Unknown Event", 0}
};

struct mpr_table_lookup mpr_phystatus_names[] = {
	{"NewTargetAdded",		0x01},
	{"TargetGone",			0x02},
	{"PHYLinkStatusChange",		0x03},
	{"PHYLinkStatusUnchanged",	0x04},
	{"TargetMissing",		0x05},
	{NULL, 0},
	{"Unknown Status", 0}
};

struct mpr_table_lookup mpr_linkrate_names[] = {
	{"PHY disabled",		0x01},
	{"Speed Negotiation Failed",	0x02},
	{"SATA OOB Complete",		0x03},
	{"SATA Port Selector",		0x04},
	{"SMP Reset in Progress",	0x05},
	{"1.5Gbps",			0x08},
	{"3.0Gbps",			0x09},
	{"6.0Gbps",			0x0a},
	{"12.0Gbps",			0x0b},
	{NULL, 0},
	{"LinkRate Unknown",		0x00}
};

struct mpr_table_lookup mpr_sasdev0_devtype[] = {
	{"End Device",			0x01},
	{"Edge Expander",		0x02},
	{"Fanout Expander",		0x03},
	{NULL, 0},
	{"No Device",			0x00}
};

struct mpr_table_lookup mpr_phyinfo_reason_names[] = {
	{"Power On",			0x01},
	{"Hard Reset",			0x02},
	{"SMP Phy Control Link Reset",	0x03},
	{"Loss DWORD Sync",		0x04},
	{"Multiplex Sequence",		0x05},
	{"I-T Nexus Loss Timer",	0x06},
	{"Break Timeout Timer",		0x07},
	{"PHY Test Function",		0x08},
	{NULL, 0},
	{"Unknown Reason",		0x00}
};

struct mpr_table_lookup mpr_whoinit_names[] = {
	{"System BIOS",			0x01},
	{"ROM BIOS",			0x02},
	{"PCI Peer",			0x03},
	{"Host Driver",			0x04},
	{"Manufacturing",		0x05},
	{NULL, 0},
	{"Not Initialized",		0x00}
};

struct mpr_table_lookup mpr_sasdisc_reason[] = {
	{"Discovery Started",		0x01},
	{"Discovery Complete",		0x02},
	{NULL, 0},
	{"Unknown",			0x00}
};

struct mpr_table_lookup mpr_sastopo_exp[] = {
	{"Added",			0x01},
	{"Not Responding",		0x02},
	{"Responding",			0x03},
	{"Delay Not Responding",	0x04},
	{NULL, 0},
	{"Unknown",			0x00}
};

struct mpr_table_lookup mpr_sasdev_reason[] = {
	{"SMART Data",			0x05},
	{"Unsupported",			0x07},
	{"Internal Device Reset",	0x08},
	{"Task Abort Internal",		0x09},
	{"Abort Task Set Internal",	0x0a},
	{"Clear Task Set Internal",	0x0b},
	{"Query Task Internal",		0x0c},
	{"Async Notification",		0x0d},
	{"Cmp Internal Device Reset",	0x0e},
	{"Cmp Task Abort Internal",	0x0f},
	{"Sata Init Failure",		0x10},
	{NULL, 0},
	{"Unknown",			0x00}
};

struct mpr_table_lookup mpr_pcie_linkrate_names[] = {
	{"Port disabled",		0x01},
	{"2.5GT/sec",			0x02},
	{"5.0GT/sec",			0x03},
	{"8.0GT/sec",			0x04},
	{"16.0GT/sec",			0x05},
	{NULL, 0},
	{"LinkRate Unknown",		0x00}
};

struct mpr_table_lookup mpr_iocstatus_string[] = {
	{"success",			MPI2_IOCSTATUS_SUCCESS},
	{"invalid function",		MPI2_IOCSTATUS_INVALID_FUNCTION},
	{"scsi recovered error",	MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR},
	{"scsi invalid dev handle",	MPI2_IOCSTATUS_SCSI_INVALID_DEVHANDLE},
	{"scsi device not there",	MPI2_IOCSTATUS_SCSI_DEVICE_NOT_THERE},
	{"scsi data overrun",		MPI2_IOCSTATUS_SCSI_DATA_OVERRUN},
	{"scsi data underrun",		MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN},
	{"scsi io data error",		MPI2_IOCSTATUS_SCSI_IO_DATA_ERROR},
	{"scsi protocol error",		MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR},
	{"scsi task terminated",	MPI2_IOCSTATUS_SCSI_TASK_TERMINATED},
	{"scsi residual mismatch",	MPI2_IOCSTATUS_SCSI_RESIDUAL_MISMATCH},
	{"scsi task mgmt failed",	MPI2_IOCSTATUS_SCSI_TASK_MGMT_FAILED},
	{"scsi ioc terminated",		MPI2_IOCSTATUS_SCSI_IOC_TERMINATED},
	{"scsi ext terminated",		MPI2_IOCSTATUS_SCSI_EXT_TERMINATED},
	{"eedp guard error",		MPI2_IOCSTATUS_EEDP_GUARD_ERROR},
	{"eedp ref tag error",		MPI2_IOCSTATUS_EEDP_REF_TAG_ERROR},
	{"eedp app tag error",		MPI2_IOCSTATUS_EEDP_APP_TAG_ERROR},
	{NULL, 0},
	{"unknown",			0x00}
};

struct mpr_table_lookup mpr_scsi_status_string[] = {
	{"good",			MPI2_SCSI_STATUS_GOOD},
	{"check condition",		MPI2_SCSI_STATUS_CHECK_CONDITION},
	{"condition met",		MPI2_SCSI_STATUS_CONDITION_MET},
	{"busy",			MPI2_SCSI_STATUS_BUSY},
	{"intermediate",		MPI2_SCSI_STATUS_INTERMEDIATE},
	{"intermediate condmet",	MPI2_SCSI_STATUS_INTERMEDIATE_CONDMET},
	{"reservation conflict",	MPI2_SCSI_STATUS_RESERVATION_CONFLICT},
	{"command terminated",		MPI2_SCSI_STATUS_COMMAND_TERMINATED},
	{"task set full",		MPI2_SCSI_STATUS_TASK_SET_FULL},
	{"aca active",			MPI2_SCSI_STATUS_ACA_ACTIVE},
	{"task aborted",		MPI2_SCSI_STATUS_TASK_ABORTED},
	{NULL, 0},
	{"unknown",			0x00}
};

struct mpr_table_lookup mpr_scsi_taskmgmt_string[] = {
	{"task mgmt request completed",	MPI2_SCSITASKMGMT_RSP_TM_COMPLETE},
	{"invalid frame",		MPI2_SCSITASKMGMT_RSP_INVALID_FRAME},
	{"task mgmt request not supp",	MPI2_SCSITASKMGMT_RSP_TM_NOT_SUPPORTED},
	{"task mgmt request failed",	MPI2_SCSITASKMGMT_RSP_TM_FAILED},
	{"task mgmt request_succeeded",	MPI2_SCSITASKMGMT_RSP_TM_SUCCEEDED},
	{"invalid lun",			MPI2_SCSITASKMGMT_RSP_TM_INVALID_LUN},
	{"overlapped tag attempt",	0xA},
	{"task queued on IOC",		MPI2_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC},
	{NULL, 0},
	{"unknown",			0x00}
};

void
mpr_describe_devinfo(uint32_t devinfo, char *string, int len)
{
	snprintf(string, len, "%b,%s", devinfo,
	    "\20" "\4SataHost" "\5SmpInit" "\6StpInit" "\7SspInit"
	    "\10SataDev" "\11SmpTarg" "\12StpTarg" "\13SspTarg" "\14Direct"
	    "\15LsiDev" "\16AtapiDev" "\17SepDev",
	    mpr_describe_table(mpr_sasdev0_devtype, devinfo & 0x03));
}

void
mpr_print_iocfacts(struct mpr_softc *sc, MPI2_IOC_FACTS_REPLY *facts)
{
	MPR_PRINTFIELD_START(sc, "IOCFacts");
	MPR_PRINTFIELD(sc, facts, MsgVersion, 0x%x);
	MPR_PRINTFIELD(sc, facts, HeaderVersion, 0x%x);
	MPR_PRINTFIELD(sc, facts, IOCNumber, %d);
	MPR_PRINTFIELD(sc, facts, IOCExceptions, 0x%x);
	MPR_PRINTFIELD(sc, facts, MaxChainDepth, %d);
	mpr_print_field(sc, "WhoInit: %s\n",
	    mpr_describe_table(mpr_whoinit_names, facts->WhoInit));
	MPR_PRINTFIELD(sc, facts, NumberOfPorts, %d);
	MPR_PRINTFIELD(sc, facts, MaxMSIxVectors, %d);
	MPR_PRINTFIELD(sc, facts, RequestCredit, %d);
	MPR_PRINTFIELD(sc, facts, ProductID, 0x%x);
	mpr_print_field(sc, "IOCCapabilities: %b\n",
	    facts->IOCCapabilities, "\20" "\3ScsiTaskFull" "\4DiagTrace"
	    "\5SnapBuf" "\6ExtBuf" "\7EEDP" "\10BiDirTarg" "\11Multicast"
	    "\14TransRetry" "\15IR" "\16EventReplay" "\17RaidAccel"
	    "\20MSIXIndex" "\21HostDisc");
	mpr_print_field(sc, "FWVersion= %d-%d-%d-%d\n",
	    facts->FWVersion.Struct.Major,
	    facts->FWVersion.Struct.Minor,
	    facts->FWVersion.Struct.Unit,
	    facts->FWVersion.Struct.Dev);
	MPR_PRINTFIELD(sc, facts, IOCRequestFrameSize, %d);
	MPR_PRINTFIELD(sc, facts, MaxInitiators, %d);
	MPR_PRINTFIELD(sc, facts, MaxTargets, %d);
	MPR_PRINTFIELD(sc, facts, MaxSasExpanders, %d);
	MPR_PRINTFIELD(sc, facts, MaxEnclosures, %d);
	mpr_print_field(sc, "ProtocolFlags: %b\n",
	    facts->ProtocolFlags, "\20" "\1ScsiTarg" "\2ScsiInit");
	MPR_PRINTFIELD(sc, facts, HighPriorityCredit, %d);
	MPR_PRINTFIELD(sc, facts, MaxReplyDescriptorPostQueueDepth, %d);
	MPR_PRINTFIELD(sc, facts, ReplyFrameSize, %d);
	MPR_PRINTFIELD(sc, facts, MaxVolumes, %d);
	MPR_PRINTFIELD(sc, facts, MaxDevHandle, %d);
	MPR_PRINTFIELD(sc, facts, MaxPersistentEntries, %d);
}

void
mpr_print_portfacts(struct mpr_softc *sc, MPI2_PORT_FACTS_REPLY *facts)
{

	MPR_PRINTFIELD_START(sc, "PortFacts");
	MPR_PRINTFIELD(sc, facts, PortNumber, %d);
	MPR_PRINTFIELD(sc, facts, PortType, 0x%x);
	MPR_PRINTFIELD(sc, facts, MaxPostedCmdBuffers, %d);
}

void
mpr_print_evt_generic(struct mpr_softc *sc, MPI2_EVENT_NOTIFICATION_REPLY *event)
{

	MPR_PRINTFIELD_START(sc, "EventReply");
	MPR_PRINTFIELD(sc, event, EventDataLength, %d);
	MPR_PRINTFIELD(sc, event, AckRequired, %d);
	mpr_print_field(sc, "Event: %s (0x%x)\n",
	    mpr_describe_table(mpr_event_names, event->Event), event->Event);
	MPR_PRINTFIELD(sc, event, EventContext, 0x%x);
}

void
mpr_print_sasdev0(struct mpr_softc *sc, MPI2_CONFIG_PAGE_SAS_DEV_0 *buf)
{
	MPR_PRINTFIELD_START(sc, "SAS Device Page 0");
	MPR_PRINTFIELD(sc, buf, Slot, %d);
	MPR_PRINTFIELD(sc, buf, EnclosureHandle, 0x%x);
	mpr_print_field(sc, "SASAddress: 0x%jx\n",
	    mpr_to_u64(&buf->SASAddress));
	MPR_PRINTFIELD(sc, buf, ParentDevHandle, 0x%x);
	MPR_PRINTFIELD(sc, buf, PhyNum, %d);
	MPR_PRINTFIELD(sc, buf, AccessStatus, 0x%x);
	MPR_PRINTFIELD(sc, buf, DevHandle, 0x%x);
	MPR_PRINTFIELD(sc, buf, AttachedPhyIdentifier, 0x%x);
	MPR_PRINTFIELD(sc, buf, ZoneGroup, %d);
	mpr_print_field(sc, "DeviceInfo: %b,%s\n", buf->DeviceInfo,
	    "\20" "\4SataHost" "\5SmpInit" "\6StpInit" "\7SspInit"
	    "\10SataDev" "\11SmpTarg" "\12StpTarg" "\13SspTarg" "\14Direct"
	    "\15LsiDev" "\16AtapiDev" "\17SepDev",
	    mpr_describe_table(mpr_sasdev0_devtype, buf->DeviceInfo & 0x03));
	MPR_PRINTFIELD(sc, buf, Flags, 0x%x);
	MPR_PRINTFIELD(sc, buf, PhysicalPort, %d);
	MPR_PRINTFIELD(sc, buf, MaxPortConnections, %d);
	mpr_print_field(sc, "DeviceName: 0x%jx\n",
	    mpr_to_u64(&buf->DeviceName));
	MPR_PRINTFIELD(sc, buf, PortGroups, %d);
	MPR_PRINTFIELD(sc, buf, DmaGroup, %d);
	MPR_PRINTFIELD(sc, buf, ControlGroup, %d);
}

void
mpr_print_evt_sas(struct mpr_softc *sc, MPI2_EVENT_NOTIFICATION_REPLY *event)
{

	mpr_print_evt_generic(sc, event);

	switch(event->Event) {
	case MPI2_EVENT_SAS_DISCOVERY:
	{
		MPI2_EVENT_DATA_SAS_DISCOVERY *data;

		data = (MPI2_EVENT_DATA_SAS_DISCOVERY *)&event->EventData;
		mpr_print_field(sc, "Flags: %b\n", data->Flags,
		    "\20" "\1InProgress" "\2DeviceChange");
		mpr_print_field(sc, "ReasonCode: %s\n",
		    mpr_describe_table(mpr_sasdisc_reason, data->ReasonCode));
		MPR_PRINTFIELD(sc, data, PhysicalPort, %d);
		mpr_print_field(sc, "DiscoveryStatus: %b\n",
		    data->DiscoveryStatus,  "\20"
		    "\1Loop" "\2UnaddressableDev" "\3DupSasAddr" "\5SmpTimeout"
		    "\6ExpRouteFull" "\7RouteIndexError" "\10SmpFailed"
		    "\11SmpCrcError" "\12SubSubLink" "\13TableTableLink"
		    "\14UnsupDevice" "\15TableSubLink" "\16MultiDomain"
		    "\17MultiSub" "\20MultiSubSub" "\34DownstreamInit"
		    "\35MaxPhys" "\36MaxTargs" "\37MaxExpanders"
		    "\40MaxEnclosures");
		break;
	}
//SLM-add for PCIE EVENT too
	case MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
	{
		MPI2_EVENT_DATA_SAS_TOPOLOGY_CHANGE_LIST *data;
		MPI2_EVENT_SAS_TOPO_PHY_ENTRY *phy;
		int i, phynum;

		data = (MPI2_EVENT_DATA_SAS_TOPOLOGY_CHANGE_LIST *)
		    &event->EventData;
		MPR_PRINTFIELD(sc, data, EnclosureHandle, 0x%x);
		MPR_PRINTFIELD(sc, data, ExpanderDevHandle, 0x%x);
		MPR_PRINTFIELD(sc, data, NumPhys, %d);
		MPR_PRINTFIELD(sc, data, NumEntries, %d);
		MPR_PRINTFIELD(sc, data, StartPhyNum, %d);
		mpr_print_field(sc, "ExpStatus: %s (0x%x)\n",
		    mpr_describe_table(mpr_sastopo_exp, data->ExpStatus),
		    data->ExpStatus);
		MPR_PRINTFIELD(sc, data, PhysicalPort, %d);
		for (i = 0; i < data->NumEntries; i++) {
			phy = &data->PHY[i];
			phynum = data->StartPhyNum + i;
			mpr_print_field(sc,
			    "PHY[%d].AttachedDevHandle: 0x%04x\n", phynum,
			    phy->AttachedDevHandle);
			mpr_print_field(sc,
			    "PHY[%d].LinkRate: %s (0x%x)\n", phynum,
			    mpr_describe_table(mpr_linkrate_names,
			    (phy->LinkRate >> 4) & 0xf), phy->LinkRate);
			mpr_print_field(sc, "PHY[%d].PhyStatus: %s\n",
			    phynum, mpr_describe_table(mpr_phystatus_names,
			    phy->PhyStatus));
		}
		break;
	}
	case MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
	{
		MPI2_EVENT_DATA_SAS_ENCL_DEV_STATUS_CHANGE *data;

		data = (MPI2_EVENT_DATA_SAS_ENCL_DEV_STATUS_CHANGE *)
		    &event->EventData;
		MPR_PRINTFIELD(sc, data, EnclosureHandle, 0x%x);
		mpr_print_field(sc, "ReasonCode: %s\n",
		    mpr_describe_table(mpr_sastopo_exp, data->ReasonCode));
		MPR_PRINTFIELD(sc, data, PhysicalPort, %d);
		MPR_PRINTFIELD(sc, data, NumSlots, %d);
		MPR_PRINTFIELD(sc, data, StartSlot, %d);
		MPR_PRINTFIELD(sc, data, PhyBits, 0x%x);
		break;
	}
	case MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE:
	{
		MPI2_EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *data;

		data = (MPI2_EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *)
		    &event->EventData;
		MPR_PRINTFIELD(sc, data, TaskTag, 0x%x);
		mpr_print_field(sc, "ReasonCode: %s\n",
		    mpr_describe_table(mpr_sasdev_reason, data->ReasonCode));
		MPR_PRINTFIELD(sc, data, ASC, 0x%x);
		MPR_PRINTFIELD(sc, data, ASCQ, 0x%x);
		MPR_PRINTFIELD(sc, data, DevHandle, 0x%x);
		mpr_print_field(sc, "SASAddress: 0x%jx\n",
		    mpr_to_u64(&data->SASAddress));
	}
	case MPI2_EVENT_SAS_BROADCAST_PRIMITIVE:
	{
		MPI2_EVENT_DATA_SAS_BROADCAST_PRIMITIVE *data;

		data = (MPI2_EVENT_DATA_SAS_BROADCAST_PRIMITIVE *)&event->EventData;
		MPR_PRINTFIELD(sc, data, PhyNum, %d);
		MPR_PRINTFIELD(sc, data, Port, %d);
		MPR_PRINTFIELD(sc, data, PortWidth, %d);
		MPR_PRINTFIELD(sc, data, Primitive, 0x%x);
	}
	default:
		break;
	}
}

void
mpr_print_expander1(struct mpr_softc *sc, MPI2_CONFIG_PAGE_EXPANDER_1 *buf)
{
	MPR_PRINTFIELD_START(sc, "SAS Expander Page 1 #%d", buf->Phy);
	MPR_PRINTFIELD(sc, buf, PhysicalPort, %d);
	MPR_PRINTFIELD(sc, buf, NumPhys, %d);
	MPR_PRINTFIELD(sc, buf, Phy, %d);
	MPR_PRINTFIELD(sc, buf, NumTableEntriesProgrammed, %d);
	mpr_print_field(sc, "ProgrammedLinkRate: %s (0x%x)\n",
	    mpr_describe_table(mpr_linkrate_names,
	    (buf->ProgrammedLinkRate >> 4) & 0xf), buf->ProgrammedLinkRate);
	mpr_print_field(sc, "HwLinkRate: %s (0x%x)\n",
	    mpr_describe_table(mpr_linkrate_names,
	    (buf->HwLinkRate >> 4) & 0xf), buf->HwLinkRate);
	MPR_PRINTFIELD(sc, buf, AttachedDevHandle, 0x%04x);
	mpr_print_field(sc, "PhyInfo Reason: %s (0x%x)\n",
	    mpr_describe_table(mpr_phyinfo_reason_names,
	    (buf->PhyInfo >> 16) & 0xf), buf->PhyInfo);
	mpr_print_field(sc, "AttachedDeviceInfo: %b,%s\n",
	    buf->AttachedDeviceInfo, "\20" "\4SATAhost" "\5SMPinit" "\6STPinit"
	    "\7SSPinit" "\10SATAdev" "\11SMPtarg" "\12STPtarg" "\13SSPtarg"
	    "\14Direct" "\15LSIdev" "\16ATAPIdev" "\17SEPdev",
	    mpr_describe_table(mpr_sasdev0_devtype,
	    buf->AttachedDeviceInfo & 0x03));
	MPR_PRINTFIELD(sc, buf, ExpanderDevHandle, 0x%04x);
	MPR_PRINTFIELD(sc, buf, ChangeCount, %d);
	mpr_print_field(sc, "NegotiatedLinkRate: %s (0x%x)\n",
	    mpr_describe_table(mpr_linkrate_names,
	    buf->NegotiatedLinkRate & 0xf), buf->NegotiatedLinkRate);
	MPR_PRINTFIELD(sc, buf, PhyIdentifier, %d);
	MPR_PRINTFIELD(sc, buf, AttachedPhyIdentifier, %d);
	MPR_PRINTFIELD(sc, buf, DiscoveryInfo, 0x%x);
	MPR_PRINTFIELD(sc, buf, AttachedPhyInfo, 0x%x);
	mpr_print_field(sc, "AttachedPhyInfo Reason: %s (0x%x)\n",
	    mpr_describe_table(mpr_phyinfo_reason_names,
	    buf->AttachedPhyInfo & 0xf), buf->AttachedPhyInfo);
	MPR_PRINTFIELD(sc, buf, ZoneGroup, %d);
	MPR_PRINTFIELD(sc, buf, SelfConfigStatus, 0x%x);
}

void
mpr_print_sasphy0(struct mpr_softc *sc, MPI2_CONFIG_PAGE_SAS_PHY_0 *buf)
{
	MPR_PRINTFIELD_START(sc, "SAS PHY Page 0");
	MPR_PRINTFIELD(sc, buf, OwnerDevHandle, 0x%04x);
	MPR_PRINTFIELD(sc, buf, AttachedDevHandle, 0x%04x);
	MPR_PRINTFIELD(sc, buf, AttachedPhyIdentifier, %d);
	mpr_print_field(sc, "AttachedPhyInfo Reason: %s (0x%x)\n",
	    mpr_describe_table(mpr_phyinfo_reason_names,
	    buf->AttachedPhyInfo & 0xf), buf->AttachedPhyInfo);
	mpr_print_field(sc, "ProgrammedLinkRate: %s (0x%x)\n",
	    mpr_describe_table(mpr_linkrate_names,
	    (buf->ProgrammedLinkRate >> 4) & 0xf), buf->ProgrammedLinkRate);
	mpr_print_field(sc, "HwLinkRate: %s (0x%x)\n",
	    mpr_describe_table(mpr_linkrate_names,
	    (buf->HwLinkRate >> 4) & 0xf), buf->HwLinkRate);
	MPR_PRINTFIELD(sc, buf, ChangeCount, %d);
	MPR_PRINTFIELD(sc, buf, Flags, 0x%x);
	mpr_print_field(sc, "PhyInfo Reason: %s (0x%x)\n",
	    mpr_describe_table(mpr_phyinfo_reason_names,
	    (buf->PhyInfo >> 16) & 0xf), buf->PhyInfo);
	mpr_print_field(sc, "NegotiatedLinkRate: %s (0x%x)\n",
	    mpr_describe_table(mpr_linkrate_names,
	    buf->NegotiatedLinkRate & 0xf), buf->NegotiatedLinkRate);
}

void
mpr_print_sgl(struct mpr_softc *sc, struct mpr_command *cm, int offset)
{
	MPI2_IEEE_SGE_SIMPLE64 *ieee_sge;
	MPI25_IEEE_SGE_CHAIN64 *ieee_sgc;
	MPI2_SGE_SIMPLE64 *sge;
	MPI2_REQUEST_HEADER *req;
	struct mpr_chain *chain = NULL;
	char *frame;
	u_int i = 0, flags, length;

	req = (MPI2_REQUEST_HEADER *)cm->cm_req;
	frame = (char *)cm->cm_req;
	ieee_sge = (MPI2_IEEE_SGE_SIMPLE64 *)&frame[offset * 4];
	sge = (MPI2_SGE_SIMPLE64 *)&frame[offset * 4];
	printf("SGL for command %p\n", cm);

	hexdump(frame, 128, NULL, 0);
	while ((frame != NULL) && (!(cm->cm_flags & MPR_CM_FLAGS_SGE_SIMPLE))) {
		flags = ieee_sge->Flags;
		length = le32toh(ieee_sge->Length);
		printf("IEEE seg%d flags=0x%02x len=0x%08x addr=0x%016jx\n", i,
		    flags, length, mpr_to_u64(&ieee_sge->Address));
		if (flags & MPI25_IEEE_SGE_FLAGS_END_OF_LIST)
			break;
		ieee_sge++;
		i++;
		if (flags & MPI2_IEEE_SGE_FLAGS_CHAIN_ELEMENT) {
			ieee_sgc = (MPI25_IEEE_SGE_CHAIN64 *)ieee_sge;
			printf("IEEE chain flags=0x%x len=0x%x Offset=0x%x "
			    "Address=0x%016jx\n", ieee_sgc->Flags,
			    le32toh(ieee_sgc->Length),
			    ieee_sgc->NextChainOffset,
			    mpr_to_u64(&ieee_sgc->Address));
			if (chain == NULL)
				chain = TAILQ_FIRST(&cm->cm_chain_list);
			else
				chain = TAILQ_NEXT(chain, chain_link);
			frame = (char *)chain->chain;
			ieee_sge = (MPI2_IEEE_SGE_SIMPLE64 *)frame;
			hexdump(frame, 128, NULL, 0);
		}
	}
	while ((frame != NULL) && (cm->cm_flags & MPR_CM_FLAGS_SGE_SIMPLE)) {
		flags = le32toh(sge->FlagsLength) >> MPI2_SGE_FLAGS_SHIFT;
		printf("seg%d flags=0x%02x len=0x%06x addr=0x%016jx\n", i,
		    flags, le32toh(sge->FlagsLength) & 0xffffff,
		    mpr_to_u64(&sge->Address));
		if (flags & (MPI2_SGE_FLAGS_END_OF_LIST |
		    MPI2_SGE_FLAGS_END_OF_BUFFER))
			break;
		sge++;
		i++;
	}
}

void
mpr_print_scsiio_cmd(struct mpr_softc *sc, struct mpr_command *cm)
{
	MPI2_SCSI_IO_REQUEST *req;

	req = (MPI2_SCSI_IO_REQUEST *)cm->cm_req;
	mpr_print_sgl(sc, cm, req->SGLOffset0);
}

