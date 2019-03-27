/*-
 * Copyright (c) 2008 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * LSI MPT-Fusion Host Adapter FreeBSD userland interface
 *
 * $FreeBSD$
 */
/*-
 * Copyright (c) 2011-2014 LSI Corp.
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
 * LSI MPT-Fusion Host Adapter FreeBSD
 *
 * $FreeBSD$
 */

#ifndef _MPR_IOCTL_H_
#define	_MPR_IOCTL_H_

#include <dev/mpr/mpi/mpi2_type.h>
#include <dev/mpr/mpi/mpi2.h>
#include <dev/mpr/mpi/mpi2_cnfg.h>
#include <dev/mpr/mpi/mpi2_sas.h>

/*
 * For the read header requests, the header should include the page
 * type or extended page type, page number, and page version.  The
 * buffer and length are unused.  The completed header is returned in
 * the 'header' member.
 *
 * For the read page and write page requests, 'buf' should point to a
 * buffer of 'len' bytes which holds the entire page (including the
 * header).
 *
 * All requests specify the page address in 'page_address'.
 */
struct mpr_cfg_page_req {	
	MPI2_CONFIG_PAGE_HEADER header;
	uint32_t page_address;
	void	*buf;
	int	len;
	uint16_t ioc_status;
};

struct mpr_ext_cfg_page_req {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER header;
	uint32_t page_address;
	void	*buf;
	int	len;
	uint16_t ioc_status;
};

struct mpr_raid_action {
	uint8_t action;
	uint8_t volume_bus;
	uint8_t volume_id;
	uint8_t phys_disk_num;
	uint32_t action_data_word;
	void *buf;
	int len;
	uint32_t volume_status;
	uint32_t action_data[4];
	uint16_t action_status;
	uint16_t ioc_status;
	uint8_t write;
};

struct mpr_usr_command {
	void *req;
	uint32_t req_len;
	void *rpl;
	uint32_t rpl_len;
	void *buf;
	int len;
	uint32_t flags;
};

typedef struct mpr_pci_bits
{
	union {
		struct {
			uint32_t	DeviceNumber	:5;
			uint32_t	FunctionNumber	:3;
			uint32_t	BusNumber	:24;
		} bits;
		uint32_t	AsDWORD;
	} u;
	uint32_t	PciSegmentId;
} mpr_pci_bits_t;

/*
 *  The following is the MPRIOCTL_GET_ADAPTER_DATA data structure.  This data
 *  structure is setup so that we hopefully are properly aligned for both
 *  32-bit and 64-bit mode applications.
 *
 *  Adapter Type - Value = 6 = SCSI Protocol through SAS-3 adapter
 *
 *  MPI Port Number - The PCI Function number for this device
 *
 *  PCI Device HW Id - The PCI device number for this device
 *
 */
#define	MPRIOCTL_ADAPTER_TYPE_SAS3		6
typedef struct mpr_adapter_data
{
	uint32_t	StructureLength;
	uint32_t	AdapterType;
	uint32_t	MpiPortNumber;
	uint32_t	PCIDeviceHwId;
	uint32_t	PCIDeviceHwRev;
	uint32_t	SubSystemId;
	uint32_t	SubsystemVendorId;
	uint32_t	Reserved1;
	uint32_t	MpiFirmwareVersion;
	uint32_t	BiosVersion;
	uint8_t		DriverVersion[32];
	uint8_t		Reserved2;
	uint8_t		ScsiId;
	uint16_t	Reserved3;
	mpr_pci_bits_t	PciInformation;
} mpr_adapter_data_t;


typedef struct mpr_update_flash
{
	uint64_t	PtrBuffer;
	uint32_t	ImageChecksum;
	uint32_t	ImageOffset;
	uint32_t	ImageSize;
	uint32_t	ImageType;
} mpr_update_flash_t;


#define	MPR_PASS_THRU_DIRECTION_NONE	0
#define	MPR_PASS_THRU_DIRECTION_READ	1
#define	MPR_PASS_THRU_DIRECTION_WRITE	2
#define	MPR_PASS_THRU_DIRECTION_BOTH	3

typedef struct mpr_pass_thru
{
	uint64_t	PtrRequest;
	uint64_t	PtrReply;
	uint64_t	PtrData;
	uint32_t	RequestSize;
	uint32_t	ReplySize;
	uint32_t	DataSize;
	uint32_t	DataDirection;
	uint64_t	PtrDataOut;
	uint32_t	DataOutSize;
	uint32_t	Timeout;
} mpr_pass_thru_t;


/*
 * Event queue defines
 */
#define	MPR_EVENT_QUEUE_SIZE		(50) /* Max Events stored in driver */
#define	MPR_MAX_EVENT_DATA_LENGTH	(48) /* Size of each event in Dwords */

typedef struct mpr_event_query
{
	uint16_t	Entries;
	uint16_t	Reserved;
	uint32_t	Types[4];
} mpr_event_query_t;

typedef struct mpr_event_enable
{
	uint32_t	Types[4];
} mpr_event_enable_t;

/*
 * Event record entry for ioctl.
 */
typedef struct mpr_event_entry
{
	uint32_t	Type;
	uint32_t	Number;
	uint32_t	Data[MPR_MAX_EVENT_DATA_LENGTH];
} mpr_event_entry_t;

typedef struct mpr_event_report
{
	uint32_t	Size;
	uint64_t	PtrEvents;
} mpr_event_report_t;


typedef struct mpr_pci_info
{
	uint32_t	BusNumber;
	uint8_t		DeviceNumber;
	uint8_t		FunctionNumber;
	uint16_t	InterruptVector;
	uint8_t		PciHeader[256];
} mpr_pci_info_t;


typedef struct mpr_diag_action
{
	uint32_t	Action;
	uint32_t	Length;
	uint64_t	PtrDiagAction;
	uint32_t	ReturnCode;
} mpr_diag_action_t;

#define	MPR_FW_DIAGNOSTIC_UID_NOT_FOUND	(0xFF)

#define	MPR_FW_DIAG_NEW				(0x806E6577)

#define	MPR_FW_DIAG_TYPE_REGISTER		(0x00000001)
#define	MPR_FW_DIAG_TYPE_UNREGISTER		(0x00000002)
#define	MPR_FW_DIAG_TYPE_QUERY			(0x00000003)
#define	MPR_FW_DIAG_TYPE_READ_BUFFER		(0x00000004)
#define	MPR_FW_DIAG_TYPE_RELEASE		(0x00000005)

#define	MPR_FW_DIAG_INVALID_UID			(0x00000000)

#define MPR_DIAG_SUCCESS			0
#define MPR_DIAG_FAILURE			1

#define	MPR_FW_DIAG_ERROR_SUCCESS		(0x00000000)
#define	MPR_FW_DIAG_ERROR_FAILURE		(0x00000001)
#define	MPR_FW_DIAG_ERROR_INVALID_PARAMETER	(0x00000002)
#define	MPR_FW_DIAG_ERROR_POST_FAILED		(0x00000010)
#define	MPR_FW_DIAG_ERROR_INVALID_UID		(0x00000011)
#define	MPR_FW_DIAG_ERROR_RELEASE_FAILED	(0x00000012)
#define	MPR_FW_DIAG_ERROR_NO_BUFFER		(0x00000013)
#define	MPR_FW_DIAG_ERROR_ALREADY_RELEASED	(0x00000014)


typedef struct mpr_fw_diag_register
{
	uint8_t		ExtendedType;
	uint8_t		BufferType;
	uint16_t	ApplicationFlags;
	uint32_t	DiagnosticFlags;
	uint32_t	ProductSpecific[23];
	uint32_t	RequestedBufferSize;
	uint32_t	UniqueId;
} mpr_fw_diag_register_t;

typedef struct mpr_fw_diag_unregister
{
	uint32_t	UniqueId;
} mpr_fw_diag_unregister_t;

#define	MPR_FW_DIAG_FLAG_APP_OWNED		(0x0001)
#define	MPR_FW_DIAG_FLAG_BUFFER_VALID		(0x0002)
#define	MPR_FW_DIAG_FLAG_FW_BUFFER_ACCESS	(0x0004)

typedef struct mpr_fw_diag_query
{
	uint8_t		ExtendedType;
	uint8_t		BufferType;
	uint16_t	ApplicationFlags;
	uint32_t	DiagnosticFlags;
	uint32_t	ProductSpecific[23];
	uint32_t	TotalBufferSize;
	uint32_t	DriverAddedBufferSize;
	uint32_t	UniqueId;
} mpr_fw_diag_query_t;

typedef struct mpr_fw_diag_release
{
	uint32_t	UniqueId;
} mpr_fw_diag_release_t;

#define	MPR_FW_DIAG_FLAG_REREGISTER	(0x0001)
#define	MPR_FW_DIAG_FLAG_FORCE_RELEASE	(0x0002)

typedef struct mpr_diag_read_buffer
{
	uint8_t		Status;
	uint8_t		Reserved;
	uint16_t	Flags;
	uint32_t	StartingOffset;
	uint32_t	BytesToRead;
	uint32_t	UniqueId;
	uint64_t	PtrDataBuffer;
} mpr_diag_read_buffer_t;

/*
 * Register Access
 */
#define	REG_IO_READ	1
#define	REG_IO_WRITE	2
#define	REG_MEM_READ	3
#define	REG_MEM_WRITE	4

typedef struct mpr_reg_access
{
	uint32_t	Command;
	uint32_t	RegOffset;
	uint32_t	RegData;
} mpr_reg_access_t;

typedef struct mpr_btdh_mapping
{
	uint16_t	TargetID;
	uint16_t	Bus;
	uint16_t	DevHandle;
	uint16_t	Reserved;
} mpr_btdh_mapping_t;

#define MPRIO_MPR_COMMAND_FLAG_VERBOSE	0x01
#define MPRIO_MPR_COMMAND_FLAG_DEBUG	0x02
#define	MPRIO_READ_CFG_HEADER	_IOWR('M', 200, struct mpr_cfg_page_req)
#define	MPRIO_READ_CFG_PAGE	_IOWR('M', 201, struct mpr_cfg_page_req)
#define	MPRIO_READ_EXT_CFG_HEADER _IOWR('M', 202, struct mpr_ext_cfg_page_req)
#define	MPRIO_READ_EXT_CFG_PAGE	_IOWR('M', 203, struct mpr_ext_cfg_page_req)
#define	MPRIO_WRITE_CFG_PAGE	_IOWR('M', 204, struct mpr_cfg_page_req)
#define	MPRIO_RAID_ACTION	_IOWR('M', 205, struct mpr_raid_action)
#define	MPRIO_MPR_COMMAND	_IOWR('M', 210, struct mpr_usr_command)

#ifndef MPTIOCTL
#define	MPTIOCTL			('I')
#define	MPTIOCTL_GET_ADAPTER_DATA	_IOWR(MPTIOCTL, 1,\
    struct mpr_adapter_data)
#define	MPTIOCTL_UPDATE_FLASH		_IOWR(MPTIOCTL, 2,\
    struct mpr_update_flash)
#define	MPTIOCTL_RESET_ADAPTER		_IO(MPTIOCTL, 3)
#define	MPTIOCTL_PASS_THRU		_IOWR(MPTIOCTL, 4,\
    struct mpr_pass_thru)
#define	MPTIOCTL_EVENT_QUERY		_IOWR(MPTIOCTL, 5,\
    struct mpr_event_query)
#define	MPTIOCTL_EVENT_ENABLE		_IOWR(MPTIOCTL, 6,\
    struct mpr_event_enable)
#define	MPTIOCTL_EVENT_REPORT		_IOWR(MPTIOCTL, 7,\
    struct mpr_event_report)
#define	MPTIOCTL_GET_PCI_INFO		_IOWR(MPTIOCTL, 8,\
    struct mpr_pci_info)
#define	MPTIOCTL_DIAG_ACTION		_IOWR(MPTIOCTL, 9,\
    struct mpr_diag_action)
#define	MPTIOCTL_REG_ACCESS		_IOWR(MPTIOCTL, 10,\
    struct mpr_reg_access)
#define	MPTIOCTL_BTDH_MAPPING		_IOWR(MPTIOCTL, 11,\
    struct mpr_btdh_mapping)
#endif

#endif /* !_MPR_IOCTL_H_ */
