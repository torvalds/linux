/*-
 * Debug routines for LSI '909 FC  adapters.
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
 *
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
 * Support from LSI-Logic has also gone a great deal toward making this a
 * workable subsystem and is gratefully acknowledged.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/mpt/mpt.h>

#include <dev/mpt/mpilib/mpi_ioc.h>
#include <dev/mpt/mpilib/mpi_init.h>
#include <dev/mpt/mpilib/mpi_fc.h>
#include <dev/mpt/mpilib/mpi_targ.h>

#include <cam/scsi/scsi_all.h>

#include <machine/stdarg.h>	/* for use by mpt_prt below */

struct Error_Map {
	int 	 Error_Code;
	char    *Error_String;
};

static const struct Error_Map IOC_Status[] = {
{ MPI_IOCSTATUS_SUCCESS,                  "Success" },
{ MPI_IOCSTATUS_INVALID_FUNCTION,         "IOC: Invalid Function" },
{ MPI_IOCSTATUS_BUSY,                     "IOC: Busy" },
{ MPI_IOCSTATUS_INVALID_SGL,              "IOC: Invalid SGL" },
{ MPI_IOCSTATUS_INTERNAL_ERROR,           "IOC: Internal Error" },
{ MPI_IOCSTATUS_RESERVED,                 "IOC: Reserved" },
{ MPI_IOCSTATUS_INSUFFICIENT_RESOURCES,   "IOC: Insufficient Resources" },
{ MPI_IOCSTATUS_INVALID_FIELD,            "IOC: Invalid Field" },
{ MPI_IOCSTATUS_INVALID_STATE,            "IOC: Invalid State" },
{ MPI_IOCSTATUS_CONFIG_INVALID_ACTION,    "Invalid Action" },
{ MPI_IOCSTATUS_CONFIG_INVALID_TYPE,      "Invalid Type" },
{ MPI_IOCSTATUS_CONFIG_INVALID_PAGE,      "Invalid Page" },
{ MPI_IOCSTATUS_CONFIG_INVALID_DATA,      "Invalid Data" },
{ MPI_IOCSTATUS_CONFIG_NO_DEFAULTS,       "No Defaults" },
{ MPI_IOCSTATUS_CONFIG_CANT_COMMIT,       "Can't Commit" },
{ MPI_IOCSTATUS_SCSI_RECOVERED_ERROR,     "SCSI: Recoverd Error" },
{ MPI_IOCSTATUS_SCSI_INVALID_BUS,         "SCSI: Invalid Bus" },
{ MPI_IOCSTATUS_SCSI_INVALID_TARGETID,    "SCSI: Invalid Target ID" },
{ MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE,    "SCSI: Device Not There" },
{ MPI_IOCSTATUS_SCSI_DATA_OVERRUN,        "SCSI: Data Overrun" },
{ MPI_IOCSTATUS_SCSI_DATA_UNDERRUN,       "SCSI: Data Underrun" },
{ MPI_IOCSTATUS_SCSI_IO_DATA_ERROR,       "SCSI: Data Error" },
{ MPI_IOCSTATUS_SCSI_PROTOCOL_ERROR,      "SCSI: Protocol Error" },
{ MPI_IOCSTATUS_SCSI_TASK_TERMINATED,     "SCSI: Task Terminated" },
{ MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH,   "SCSI: Residual Mismatch" },
{ MPI_IOCSTATUS_SCSI_TASK_MGMT_FAILED,    "SCSI: Task Management Failed" },
{ MPI_IOCSTATUS_SCSI_IOC_TERMINATED,      "SCSI: IOC Bus Reset" },
{ MPI_IOCSTATUS_SCSI_EXT_TERMINATED,      "SCSI: External Bus Reset" },
{ MPI_IOCSTATUS_TARGET_PRIORITY_IO,       "SCSI Target: Priority I/O" },
{ MPI_IOCSTATUS_TARGET_INVALID_PORT,      "SCSI Target: Invalid Port" },
{ MPI_IOCSTATUS_TARGET_INVALID_IOCINDEX,  "SCSI Target: Invalid IOC Index" },
{ MPI_IOCSTATUS_TARGET_ABORTED,           "SCSI Target: Aborted" },
{ MPI_IOCSTATUS_TARGET_NO_CONN_RETRYABLE, "SCSI Target: No Connection (Retryable)" },
{ MPI_IOCSTATUS_TARGET_NO_CONNECTION,     "SCSI Target: No Connection" },
{ MPI_IOCSTATUS_TARGET_XFER_COUNT_MISMATCH,"SCSI Target: Transfer Count Mismatch" },
{ MPI_IOCSTATUS_TARGET_FC_ABORTED,        "FC: Aborted" },
{ MPI_IOCSTATUS_TARGET_FC_RX_ID_INVALID,  "FC: Receive ID Invalid" },
{ MPI_IOCSTATUS_TARGET_FC_DID_INVALID,    "FC: Receive DID Invalid" },
{ MPI_IOCSTATUS_TARGET_FC_NODE_LOGGED_OUT,"FC: Node Logged Out" },
{ MPI_IOCSTATUS_LAN_DEVICE_NOT_FOUND,     "LAN: Device Not Found" },
{ MPI_IOCSTATUS_LAN_DEVICE_FAILURE,       "LAN: Device Not Failure" },
{ MPI_IOCSTATUS_LAN_TRANSMIT_ERROR,       "LAN: Transmit Error" },
{ MPI_IOCSTATUS_LAN_TRANSMIT_ABORTED,     "LAN: Transmit Aborted" },
{ MPI_IOCSTATUS_LAN_RECEIVE_ERROR,        "LAN: Receive Error" },
{ MPI_IOCSTATUS_LAN_RECEIVE_ABORTED,      "LAN: Receive Aborted" },
{ MPI_IOCSTATUS_LAN_PARTIAL_PACKET,       "LAN: Partial Packet" },
{ MPI_IOCSTATUS_LAN_CANCELED,             "LAN: Canceled" },
{ -1, 0},
};

static const struct Error_Map IOC_Func[] = {
{ MPI_FUNCTION_SCSI_IO_REQUEST,              "SCSI IO Request" },
{ MPI_FUNCTION_SCSI_TASK_MGMT,               "SCSI Task Management" },
{ MPI_FUNCTION_IOC_INIT,                     "IOC Init" },
{ MPI_FUNCTION_IOC_FACTS,                    "IOC Facts" },
{ MPI_FUNCTION_CONFIG,                       "Config" },
{ MPI_FUNCTION_PORT_FACTS,                   "Port Facts" },
{ MPI_FUNCTION_PORT_ENABLE,                  "Port Enable" },
{ MPI_FUNCTION_EVENT_NOTIFICATION,           "Event Notification" },
{ MPI_FUNCTION_EVENT_ACK,                    "Event Ack" },
{ MPI_FUNCTION_FW_DOWNLOAD,                  "FW Download" },
{ MPI_FUNCTION_TARGET_CMD_BUFFER_POST,       "SCSI Target Command Buffer" },
{ MPI_FUNCTION_TARGET_ASSIST,                "Target Assist" },
{ MPI_FUNCTION_TARGET_STATUS_SEND,           "Target Status Send" },
{ MPI_FUNCTION_TARGET_MODE_ABORT,            "Target Mode Abort" },
{ -1, 0},
};

static const struct Error_Map IOC_Event[] = {
{ MPI_EVENT_NONE,   	                "None" },
{ MPI_EVENT_LOG_DATA,                   "LogData" },
{ MPI_EVENT_STATE_CHANGE,               "State Change" },
{ MPI_EVENT_UNIT_ATTENTION,             "Unit Attention" },
{ MPI_EVENT_IOC_BUS_RESET,              "IOC Bus Reset" },
{ MPI_EVENT_EXT_BUS_RESET,              "External Bus Reset" },
{ MPI_EVENT_RESCAN,        	        "Rescan" },
{ MPI_EVENT_LINK_STATUS_CHANGE,	        "Link Status Change" },
{ MPI_EVENT_LOOP_STATE_CHANGE, 	        "Loop State Change" },
{ MPI_EVENT_LOGOUT,    	       		"Logout" },
{ MPI_EVENT_EVENT_CHANGE,               "EventChange" },
{ -1, 0},
};

static const struct Error_Map IOC_SCSIState[] = {
{ MPI_SCSI_STATE_AUTOSENSE_VALID,	"AutoSense_Valid" },
{ MPI_SCSI_STATE_AUTOSENSE_FAILED,	"AutoSense_Failed" },
{ MPI_SCSI_STATE_NO_SCSI_STATUS,	"No_SCSI_Status" },
{ MPI_SCSI_STATE_TERMINATED,	   	"State_Terminated" },
{ MPI_SCSI_STATE_RESPONSE_INFO_VALID,	"Repsonse_Info_Valid" },
{ MPI_SCSI_STATE_QUEUE_TAG_REJECTED,	"Queue Tag Rejected" },
{ -1, 0},
};

static const struct Error_Map IOC_SCSIStatus[] = {
{ SCSI_STATUS_OK,			"OK" },
{ SCSI_STATUS_CHECK_COND,		"Check Condition" },
{ SCSI_STATUS_COND_MET,			"Check Condition Met" },
{ SCSI_STATUS_BUSY,			"Busy" },
{ SCSI_STATUS_INTERMED,			"Intermidiate Condition" },
{ SCSI_STATUS_INTERMED_COND_MET,	"Intermidiate Condition Met" },
{ SCSI_STATUS_RESERV_CONFLICT,		"Reservation Conflict" },
{ SCSI_STATUS_CMD_TERMINATED,		"Command Terminated" },
{ SCSI_STATUS_QUEUE_FULL,		"Queue Full" },
{ -1, 0},
};

static const struct Error_Map IOC_Diag[] = {
{ MPI_DIAG_DRWE,		"DWE" },
{ MPI_DIAG_FLASH_BAD_SIG,	"FLASH_Bad" },
{ MPI_DIAGNOSTIC_OFFSET,	"Offset" },
{ MPI_DIAG_RESET_ADAPTER,	"Reset" },
{ MPI_DIAG_DISABLE_ARM,		"DisARM" },
{ MPI_DIAG_MEM_ENABLE,		"DME" },
{ -1, 0 },
};

static const struct Error_Map IOC_SCSITMType[] = {
{ MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK,		"Abort Task" },
{ MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET,	"Abort Task Set" },
{ MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET,	"Target Reset" },
{ MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS,		"Reset Bus" },
{ MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET,	"Logical Unit Reset" },
{ -1, 0 },
};

static char *
mpt_ioc_status(int code)
{
	const struct Error_Map *status = IOC_Status;
	static char buf[64];
	while (status->Error_Code >= 0) {
		if (status->Error_Code == (code & MPI_IOCSTATUS_MASK))
			return status->Error_String;
		status++;
	}
	snprintf(buf, sizeof buf, "Unknown (0x%08x)", code);
	return buf;
}

char *
mpt_ioc_diag(u_int32_t code)
{
	const struct Error_Map *status = IOC_Diag;
	static char buf[128];
	char *ptr = buf;
	char *end = &buf[128];
	buf[0] = '\0';
	ptr += snprintf(buf, sizeof buf, "(0x%08x)", code);
	while (status->Error_Code >= 0) {
		if ((status->Error_Code & code) != 0)
			ptr += snprintf(ptr, (size_t)(end-ptr), "%s ",
				status->Error_String);
		status++;
	}
	return buf;
}

static char *
mpt_ioc_function(int code)
{
	const struct Error_Map *status = IOC_Func;
	static char buf[64];
	while (status->Error_Code >= 0) {
		if (status->Error_Code == code)
			return status->Error_String;
		status++;
	}
	snprintf(buf, sizeof buf, "Unknown (0x%08x)", code);
	return buf;
}

static char *
mpt_ioc_event(int code)
{
	const struct Error_Map *status = IOC_Event;
	static char buf[64];
	while (status->Error_Code >= 0) {
		if (status->Error_Code == code)
			return status->Error_String;
		status++;
	}
	snprintf(buf, sizeof buf, "Unknown (0x%08x)", code);
	return buf;
}

static char *
mpt_scsi_state(int code)
{
	const struct Error_Map *status = IOC_SCSIState;
	static char buf[128];
	char *ptr = buf;
	char *end = &buf[128];
	buf[0] = '\0';
	ptr += snprintf(buf, sizeof buf, "(0x%08x)", code);
	while (status->Error_Code >= 0) {
		if ((status->Error_Code & code) != 0)
			ptr += snprintf(ptr, (size_t)(end-ptr), "%s ",
				status->Error_String);
		status++;
	}
	return buf;
}

static char *
mpt_scsi_status(int code)
{
	const struct Error_Map *status = IOC_SCSIStatus;
	static char buf[64];
	while (status->Error_Code >= 0) {
		if (status->Error_Code == code)
			return status->Error_String;
		status++;
	}
	snprintf(buf, sizeof buf, "Unknown (0x%08x)", code);
	return buf;
}

static const char *
mpt_who(int who_init)
{
	const char *who;

	switch (who_init) {
	case MPT_DB_INIT_NOONE:       who = "No One";        break;
	case MPT_DB_INIT_BIOS:        who = "BIOS";          break;
	case MPT_DB_INIT_ROMBIOS:     who = "ROM BIOS";      break;
	case MPT_DB_INIT_PCIPEER:     who = "PCI Peer";      break;
	case MPT_DB_INIT_HOST:        who = "Host Driver";   break;
	case MPT_DB_INIT_MANUFACTURE: who = "Manufacturing"; break;
	default:                      who = "Unknown";       break;
	}
	return who;
}

static const char *
mpt_state(u_int32_t mb)
{
	const char *text;

	switch (MPT_STATE(mb)) {
		case MPT_DB_STATE_RESET:  text = "Reset";   break;
		case MPT_DB_STATE_READY:  text = "Ready";   break;
		case MPT_DB_STATE_RUNNING:text = "Running"; break;
		case MPT_DB_STATE_FAULT:  text = "Fault";   break;
		default: 		  text = "Unknown"; break;
	}
	return text;
}

static char *
mpt_scsi_tm_type(int code)
{
	const struct Error_Map *status = IOC_SCSITMType;
	static char buf[64];
	while (status->Error_Code >= 0) {
		if (status->Error_Code == code)
			return status->Error_String;
		status++;
	}
	snprintf(buf, sizeof buf, "Unknown (0x%08x)", code);
	return buf;
}

void
mpt_print_db(u_int32_t mb)
{

	printf("mpt mailbox: (0x%x) State %s  WhoInit %s\n",
	    mb, mpt_state(mb), mpt_who(MPT_WHO(mb)));
}

/*****************************************************************************/
/*  Reply functions                                                          */
/*****************************************************************************/
static void
mpt_print_reply_hdr(MSG_DEFAULT_REPLY *msg)
{

	printf("%s Reply @ %p\n", mpt_ioc_function(msg->Function), msg);
	printf("\tIOC Status    %s\n", mpt_ioc_status(msg->IOCStatus));
	printf("\tIOCLogInfo    0x%08x\n", msg->IOCLogInfo);
	printf("\tMsgLength     0x%02x\n", msg->MsgLength);
	printf("\tMsgFlags      0x%02x\n", msg->MsgFlags);
	printf("\tMsgContext    0x%08x\n", msg->MsgContext);
}

static void
mpt_print_init_reply(MSG_IOC_INIT_REPLY *msg)
{

	mpt_print_reply_hdr((MSG_DEFAULT_REPLY *)msg);
	printf("\tWhoInit       %s\n", mpt_who(msg->WhoInit));
	printf("\tMaxDevices    0x%02x\n", msg->MaxDevices);
	printf("\tMaxBuses     0x%02x\n", msg->MaxBuses);
}

static void
mpt_print_ioc_facts(MSG_IOC_FACTS_REPLY *msg)
{

	mpt_print_reply_hdr((MSG_DEFAULT_REPLY *)msg);
	printf("\tIOCNumber     %d\n",		msg->IOCNumber);
	printf("\tMaxChainDepth %d\n",		msg->MaxChainDepth);
	printf("\tWhoInit       %s\n",		mpt_who(msg->WhoInit));
	printf("\tBlockSize     %d\n",		msg->BlockSize);
	printf("\tFlags         %d\n",		msg->Flags);
	printf("\tReplyQueueDepth %d\n",	msg->ReplyQueueDepth);
	printf("\tReqFrameSize  0x%04x\n",	msg->RequestFrameSize);
	printf("\tFW Version    0x%08x\n",	msg->FWVersion.Word);
	printf("\tProduct ID    0x%04x\n",	msg->ProductID);
	printf("\tCredits       0x%04x\n",	msg->GlobalCredits);
	printf("\tPorts         %d\n",		msg->NumberOfPorts);
	printf("\tEventState    0x%02x\n",	msg->EventState);
	printf("\tHostMFA_HA    0x%08x\n",	msg->CurrentHostMfaHighAddr);
	printf("\tSenseBuf_HA   0x%08x\n",
	    msg->CurrentSenseBufferHighAddr);
	printf("\tRepFrameSize  0x%04x\n",	msg->CurReplyFrameSize);
	printf("\tMaxDevices    0x%02x\n",	msg->MaxDevices);
	printf("\tMaxBuses      0x%02x\n",	msg->MaxBuses);
	printf("\tFWImageSize   0x%04x\n",	msg->FWImageSize);
}

static void
mpt_print_enable_reply(MSG_PORT_ENABLE_REPLY *msg)
{

	mpt_print_reply_hdr((MSG_DEFAULT_REPLY *)msg);
	printf("\tPort:         %d\n", msg->PortNumber);
}

static void
mpt_print_scsi_io_reply(MSG_SCSI_IO_REPLY *msg)
{

	mpt_print_reply_hdr((MSG_DEFAULT_REPLY *)msg);
	printf("\tBus:          %d\n", msg->Bus);
	printf("\tTargetID      %d\n", msg->TargetID);
	printf("\tCDBLength     %d\n", msg->CDBLength);
	printf("\tSCSI Status:  %s\n", mpt_scsi_status(msg->SCSIStatus));
	printf("\tSCSI State:   %s\n", mpt_scsi_state(msg->SCSIState));
	printf("\tTransferCnt   0x%04x\n", msg->TransferCount);
	printf("\tSenseCnt      0x%04x\n", msg->SenseCount);
	printf("\tResponseInfo  0x%08x\n", msg->ResponseInfo);
}

static void
mpt_print_event_notice(MSG_EVENT_NOTIFY_REPLY *msg)
{

	mpt_print_reply_hdr((MSG_DEFAULT_REPLY *)msg);
	printf("\tEvent:        %s\n", mpt_ioc_event(msg->Event));
	printf("\tEventContext  0x%04x\n", msg->EventContext);
	printf("\tAckRequired     %d\n", msg->AckRequired);
	printf("\tEventDataLength %d\n", msg->EventDataLength);
	printf("\tContinuation    %d\n", msg->MsgFlags & 0x80);
	switch(msg->Event) {
	case MPI_EVENT_LOG_DATA:
		printf("\tEvtLogData:   0x%04x\n", msg->Data[0]);
		break;

	case MPI_EVENT_UNIT_ATTENTION:
		printf("\tTargetID:     0x%04x\n",
			msg->Data[0] & 0xff);
		printf("\tBus:          0x%04x\n",
			(msg->Data[0] >> 8) & 0xff);
		break;

	case MPI_EVENT_IOC_BUS_RESET:
	case MPI_EVENT_EXT_BUS_RESET:
	case MPI_EVENT_RESCAN:
		printf("\tPort:           %d\n",
			(msg->Data[0] >> 8) & 0xff);
		break;

	case MPI_EVENT_LINK_STATUS_CHANGE:
		printf("\tLinkState:    %d\n",
			msg->Data[0] & 0xff);
		printf("\tPort:         %d\n",
			(msg->Data[1] >> 8) & 0xff);
		break;

	case MPI_EVENT_LOOP_STATE_CHANGE:
		printf("\tType:         %d\n",
			(msg->Data[0] >> 16) & 0xff);
		printf("\tChar3:      0x%02x\n",
			(msg->Data[0] >> 8) & 0xff);
		printf("\tChar4:      0x%02x\n",
			(msg->Data[0]     ) & 0xff);
		printf("\tPort:         %d\n",
			(msg->Data[1] >> 8) & 0xff);
		break;

	case MPI_EVENT_LOGOUT:
		printf("\tN_PortId:   0x%04x\n", msg->Data[0]);
		printf("\tPort:         %d\n",
			(msg->Data[1] >> 8) & 0xff);
		break;
	}

}

void
mpt_print_reply(void *vmsg)
{
	MSG_DEFAULT_REPLY *msg = vmsg;

	switch (msg->Function) {
	case MPI_FUNCTION_EVENT_NOTIFICATION:
		mpt_print_event_notice((MSG_EVENT_NOTIFY_REPLY *)msg);
		break;
	case MPI_FUNCTION_PORT_ENABLE:
		mpt_print_enable_reply((MSG_PORT_ENABLE_REPLY *)msg);
		break;
	case MPI_FUNCTION_IOC_FACTS:
		mpt_print_ioc_facts((MSG_IOC_FACTS_REPLY *)msg);
		break;
	case MPI_FUNCTION_IOC_INIT:
		mpt_print_init_reply((MSG_IOC_INIT_REPLY *)msg);
		break;
	case MPI_FUNCTION_SCSI_IO_REQUEST:
	case MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH:
		mpt_print_scsi_io_reply((MSG_SCSI_IO_REPLY *)msg);
		break;
	default:
		mpt_print_reply_hdr((MSG_DEFAULT_REPLY *)msg);
		break;
	}
}

/*****************************************************************************/
/*  Request functions                                                        */
/*****************************************************************************/
static void
mpt_print_request_hdr(MSG_REQUEST_HEADER *req)
{

	printf("%s @ %p\n", mpt_ioc_function(req->Function), req);
	printf("\tChain Offset  0x%02x\n", req->ChainOffset);
	printf("\tMsgFlags      0x%02x\n", req->MsgFlags);
	printf("\tMsgContext    0x%08x\n", req->MsgContext);
}

static void
mpt_print_scsi_io_request(MSG_SCSI_IO_REQUEST *orig_msg)
{
	MSG_SCSI_IO_REQUEST local, *msg = &local;
	int i;

	bcopy(orig_msg, msg, sizeof (MSG_SCSI_IO_REQUEST));
	mpt_print_request_hdr((MSG_REQUEST_HEADER *)msg);
	printf("\tBus:                %d\n", msg->Bus);
	printf("\tTargetID            %d\n", msg->TargetID);
	printf("\tSenseBufferLength   %d\n", msg->SenseBufferLength);
	printf("\tLUN:              0x%jx\n", (uintmax_t)be64dec(msg->LUN));
	printf("\tControl           0x%08x ", msg->Control);
#define MPI_PRINT_FIELD(x)						\
	case MPI_SCSIIO_CONTROL_ ## x :					\
		printf(" " #x " ");					\
		break

	switch (msg->Control & MPI_SCSIIO_CONTROL_DATADIRECTION_MASK) {
	MPI_PRINT_FIELD(NODATATRANSFER);
	MPI_PRINT_FIELD(WRITE);
	MPI_PRINT_FIELD(READ);
	default:
		printf(" Invalid DIR! ");
		break;
	}
	switch (msg->Control & MPI_SCSIIO_CONTROL_TASKATTRIBUTE_MASK) {
	MPI_PRINT_FIELD(SIMPLEQ);
	MPI_PRINT_FIELD(HEADOFQ);
	MPI_PRINT_FIELD(ORDEREDQ);
	MPI_PRINT_FIELD(ACAQ);
	MPI_PRINT_FIELD(UNTAGGED);
	MPI_PRINT_FIELD(NO_DISCONNECT);
	default:
		printf(" Unknown attribute! ");
		break;
	}

	printf("\n");
#undef MPI_PRINT_FIELD

	printf("\tDataLength\t0x%08x\n", msg->DataLength);
	printf("\tSenseBufAddr\t0x%08x\n", msg->SenseBufferLowAddr);
	printf("\tCDB[0:%d]\t", msg->CDBLength);
	for (i = 0; i < msg->CDBLength; i++)
		printf("%02x ", msg->CDB[i]);
	printf("\n");

	if ((msg->Control & MPI_SCSIIO_CONTROL_DATADIRECTION_MASK) !=
	   MPI_SCSIIO_CONTROL_NODATATRANSFER ) {
		mpt_dump_sgl(&orig_msg->SGL,
		    ((char *)&orig_msg->SGL)-(char *)orig_msg);
	}
}

static void
mpt_print_scsi_tmf_request(MSG_SCSI_TASK_MGMT *msg)
{

	mpt_print_request_hdr((MSG_REQUEST_HEADER *)msg);
	printf("\tLun             0x%jx\n", (uintmax_t)be64dec(msg->LUN));
	printf("\tTaskType        %s\n", mpt_scsi_tm_type(msg->TaskType));
	printf("\tTaskMsgContext  0x%08x\n", msg->TaskMsgContext);
}


static void
mpt_print_scsi_target_assist_request(PTR_MSG_TARGET_ASSIST_REQUEST msg)
{

	mpt_print_request_hdr((MSG_REQUEST_HEADER *)msg);
	printf("\tStatusCode    0x%02x\n", msg->StatusCode);
	printf("\tTargetAssist  0x%02x\n", msg->TargetAssistFlags);
	printf("\tQueueTag      0x%04x\n", msg->QueueTag);
	printf("\tReplyWord     0x%08x\n", msg->ReplyWord);
	printf("\tLun           0x%jx\n", (uintmax_t)be64dec(msg->LUN));
	printf("\tRelativeOff   0x%08x\n", msg->RelativeOffset);
	printf("\tDataLength    0x%08x\n", msg->DataLength);
	mpt_dump_sgl(msg->SGL, 0);
}

static void
mpt_print_scsi_target_status_send_request(MSG_TARGET_STATUS_SEND_REQUEST *msg)
{
	SGE_IO_UNION x;

	mpt_print_request_hdr((MSG_REQUEST_HEADER *)msg);
	printf("\tStatusCode    0x%02x\n", msg->StatusCode);
	printf("\tStatusFlags   0x%02x\n", msg->StatusFlags);
	printf("\tQueueTag      0x%04x\n", msg->QueueTag);
	printf("\tReplyWord     0x%08x\n", msg->ReplyWord);
	printf("\tLun           0x%jx\n", (uintmax_t)be64dec(msg->LUN));
	x.u.Simple = msg->StatusDataSGE;
	mpt_dump_sgl(&x, 0);
}

void
mpt_print_request(void *vreq)
{
	MSG_REQUEST_HEADER *req = vreq;

	switch (req->Function) {
	case MPI_FUNCTION_SCSI_IO_REQUEST:
	case MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH:
		mpt_print_scsi_io_request((MSG_SCSI_IO_REQUEST *)req);
		break;
	case MPI_FUNCTION_SCSI_TASK_MGMT:
		mpt_print_scsi_tmf_request((MSG_SCSI_TASK_MGMT *)req);
		break;
	case MPI_FUNCTION_TARGET_ASSIST:
		mpt_print_scsi_target_assist_request(
		    (PTR_MSG_TARGET_ASSIST_REQUEST)req);
		break;
	case MPI_FUNCTION_TARGET_STATUS_SEND:
		mpt_print_scsi_target_status_send_request(
		    (MSG_TARGET_STATUS_SEND_REQUEST *)req);
		break;
	default:
		mpt_print_request_hdr(req);
		break;
	}
}

#if 0
typedef struct mpt_decode_entry {
	char    *name;
	u_int	 value;
	u_int	 mask;
} mpt_decode_entry_t;

static int
mpt_decode_value(mpt_decode_entry_t *table, u_int num_entries,
		 const char *name, u_int value, u_int *cur_column,
		 u_int wrap_point)
{
        int     printed;
        u_int   printed_mask;
	u_int	dummy_column;

	if (cur_column == NULL) {
		dummy_column = 0;
		cur_column = &dummy_column;
	}

	if (*cur_column >= wrap_point) {
		printf("\n");
		*cur_column = 0;
	}
	printed = printf("%s[0x%x]", name, value);
	if (table == NULL) {
		printed += printf(" ");
		*cur_column += printed;
		return (printed);
	}
	printed_mask = 0;
	while (printed_mask != 0xFF) {
		int entry;

		for (entry = 0; entry < num_entries; entry++) {
			if (((value & table[entry].mask)
			  != table[entry].value)
			 || ((printed_mask & table[entry].mask)
			  == table[entry].mask))
				continue;

			printed += printf("%s%s",
					  printed_mask == 0 ? ":(" : "|",
					  table[entry].name);
			printed_mask |= table[entry].mask;
			break;
                }
		if (entry >= num_entries)
			break;
        }
        if (printed_mask != 0)
		printed += printf(") ");
        else
		printed += printf(" ");
	*cur_column += printed;
	return (printed);
}

static const mpt_decode_entry_t req_state_parse_table[] = {
	{ "REQ_FREE",		0x00, 0xff },
	{ "REQ_ALLOCATED",	0x01, 0x01 },
	{ "REQ_QUEUED",		0x02, 0x02 },
	{ "REQ_DONE",		0x04, 0x04 },
	{ "REQ_TIMEDOUT",	0x08, 0x08 },
	{ "REQ_NEED_WAKEUP",	0x10, 0x10 }
};

static void
mpt_req_state(mpt_req_state_t state)
{

	mpt_decode_value(req_state_parse_table,
			 NUM_ELEMENTS(req_state_parse_table),
			 "REQ_STATE", state, NULL, 80);
}
#endif

#define	LAST_SGE	(		\
	MPI_SGE_FLAGS_END_OF_LIST |	\
	MPI_SGE_FLAGS_END_OF_BUFFER|	\
	MPI_SGE_FLAGS_LAST_ELEMENT) 
void
mpt_dump_sgl(SGE_IO_UNION *su, int offset)
{
	SGE_SIMPLE32 *se = (SGE_SIMPLE32 *) su;
	const char allfox[4] = { 0xff, 0xff, 0xff, 0xff };
	void *nxtaddr = se;
	void *lim;
	int flags;

	/*
	 * Can't be any bigger than this.
	 */
	lim = &((char *)se)[MPT_REQUEST_AREA - offset];

	do {
		int iprt;

		printf("\t");
		if (memcmp(se, allfox, 4) == 0) {
			uint32_t *nxt = (uint32_t *)se;
			printf("PAD  %p\n", se);
			nxtaddr = nxt + 1;
			se = nxtaddr;
			flags = 0;
			continue;
		}
		nxtaddr = se + 1;
		flags = MPI_SGE_GET_FLAGS(se->FlagsLength);
		switch (flags & MPI_SGE_FLAGS_ELEMENT_MASK) {
		case MPI_SGE_FLAGS_SIMPLE_ELEMENT:
			if (flags & MPI_SGE_FLAGS_64_BIT_ADDRESSING) {
				SGE_SIMPLE64 *se64 = (SGE_SIMPLE64 *)se;
				printf("SE64 %p: Addr=0x%08x%08x FlagsLength"
				    "=0x%0x\n", se64, se64->Address.High,
				    se64->Address.Low, se64->FlagsLength);
				nxtaddr = se64 + 1;
			} else {
				printf("SE32 %p: Addr=0x%0x FlagsLength=0x%0x"
	                            "\n", se, se->Address, se->FlagsLength);
			}
			printf(" ");
			break;
		case MPI_SGE_FLAGS_CHAIN_ELEMENT:
			if (flags & MPI_SGE_FLAGS_64_BIT_ADDRESSING) {
				SGE_CHAIN64 *ce64 = (SGE_CHAIN64 *) se;
				printf("CE64 %p: Addr=0x%08x%08x NxtChnO=0x%x "
				    "Flgs=0x%x Len=0x%0x\n", ce64,
				    ce64->Address.High, ce64->Address.Low,
				    ce64->NextChainOffset,
				    ce64->Flags, ce64->Length);
				nxtaddr = ce64 + 1;
			} else {
				SGE_CHAIN32 *ce = (SGE_CHAIN32 *) se;
				printf("CE32 %p: Addr=0x%0x NxtChnO=0x%x "
				    " Flgs=0x%x Len=0x%0x\n", ce, ce->Address,
				    ce->NextChainOffset, ce->Flags, ce->Length);
			}
			flags = 0;
			break;
		case MPI_SGE_FLAGS_TRANSACTION_ELEMENT:
			printf("TE32 @ %p\n", se);
			flags = 0;
			break;
		}
		iprt = 0;
#define MPT_PRINT_FLAG(x)						\
		if (flags & MPI_SGE_FLAGS_ ## x ) { 			\
			if (iprt == 0) {				\
				printf("\t");				\
			}						\
			printf(" ");					\
			printf( #x );					\
			iprt++;						\
		}
		MPT_PRINT_FLAG(LOCAL_ADDRESS);
		MPT_PRINT_FLAG(HOST_TO_IOC);
		MPT_PRINT_FLAG(64_BIT_ADDRESSING);
		MPT_PRINT_FLAG(LAST_ELEMENT);
		MPT_PRINT_FLAG(END_OF_BUFFER);
		MPT_PRINT_FLAG(END_OF_LIST);
#undef MPT_PRINT_FLAG
		if (iprt)
			printf("\n");
		se = nxtaddr;
		if ((flags & LAST_SGE) == LAST_SGE) {
			break;
		}
	} while ((flags & MPI_SGE_FLAGS_END_OF_LIST) == 0 && nxtaddr < lim);
}

void
mpt_dump_data(struct mpt_softc *mpt, const char *msg, void *addr, int len)
{
	int offset;
	uint8_t *cp = addr;

	mpt_prt(mpt, "%s:", msg);
	for (offset = 0; offset < len; offset++) {
		if ((offset & 0xf) == 0) {
			mpt_prtc(mpt, "\n");
		}
		mpt_prtc(mpt, " %02x", cp[offset]);
	}
	mpt_prtc(mpt, "\n");
}

void
mpt_dump_request(struct mpt_softc *mpt, request_t *req)
{
        uint32_t *pReq = req->req_vbuf;
	int o;

	mpt_prt(mpt, "Send Request %d (%jx):",
	    req->index, (uintmax_t) req->req_pbuf);
	for (o = 0; o < mpt->ioc_facts.RequestFrameSize; o++) {
		if ((o & 0x7) == 0) {
			mpt_prtc(mpt, "\n");
			mpt_prt(mpt, " ");
		}
		mpt_prtc(mpt, " %08x", pReq[o]);
	}
	mpt_prtc(mpt, "\n");
}

void
mpt_prt(struct mpt_softc *mpt, const char *fmt, ...)
{
	va_list ap;

	printf("%s: ", device_get_nameunit(mpt->dev));
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

void
mpt_prtc(struct mpt_softc *mpt, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}
