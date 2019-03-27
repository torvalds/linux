/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Michael Smith
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
 *	$FreeBSD$
 */

/*
 * Driver ioctl interface.
 *
 * Note that this interface is API-compatible with the Linux implementation
 * except as noted, and thus this header bears a striking resemblance to 
 * the Linux driver's cciss_ioctl.h.
 *
 */

#include <sys/ioccom.h>

#pragma pack(1)

typedef struct
{
    u_int8_t	bus;
    u_int8_t	dev_fn;
    u_int32_t	board_id;
} cciss_pci_info_struct; 

typedef struct
{
    u_int32_t	delay;
    u_int32_t	count;
} cciss_coalint_struct;

typedef char		NodeName_type[16];
typedef u_int32_t	Heartbeat_type;

#define CISS_PARSCSIU2	0x0001
#define CISS_PARCSCIU3	0x0002
#define CISS_FIBRE1G	0x0100
#define CISS_FIBRE2G	0x0200
typedef u_int32_t	BusTypes_type;

typedef char		FirmwareVer_type[4];
typedef u_int32_t	DriverVer_type;

/* passthrough command definitions */
#define SENSEINFOBYTES          32
#define CISS_MAX_LUN		16	
#define LEVEL2LUN		1
#define LEVEL3LUN		0

/* command status value */
#define CMD_SUCCESS		0x0000
#define CMD_TARGET_STATUS	0x0001
#define CMD_DATA_UNDERRUN	0x0002
#define CMD_DATA_OVERRUN	0x0003
#define CMD_INVALID		0x0004
#define CMD_PROTOCOL_ERR	0x0005
#define CMD_HARDWARE_ERR	0x0006
#define CMD_CONNECTION_LOST	0x0007
#define CMD_ABORTED		0x0008
#define CMD_ABORT_FAILED	0x0009
#define CMD_UNSOLICITED_ABORT	0x000A
#define CMD_TIMEOUT		0x000B
#define CMD_UNABORTABLE		0x000C

/* transfer direction */
#define XFER_NONE		0x00
#define XFER_WRITE		0x01
#define XFER_READ		0x02
#define XFER_RSVD		0x03

/* task attribute */
#define ATTR_UNTAGGED		0x00
#define ATTR_SIMPLE		0x04
#define ATTR_HEADOFQUEUE	0x05
#define ATTR_ORDERED		0x06
#define ATTR_ACA		0x07

/* CDB type */
#define TYPE_CMD		0x00
#define TYPE_MSG		0x01

/* command list structure */
typedef union {
    struct {
	u_int8_t	Dev;
	u_int8_t	Bus:6;
	u_int8_t	Mode:2;
    } __packed PeripDev;
    struct {
	u_int8_t	DevLSB;
	u_int8_t	DevMSB:6;
	u_int8_t	Mode:2;
    } __packed LogDev;
    struct {
	u_int8_t	Dev:5;
	u_int8_t	Bus:3;
	u_int8_t	Targ:6;
	u_int8_t	Mode:2;
    } __packed LogUnit;
} SCSI3Addr_struct;

typedef struct {
    u_int32_t		TargetId:24;
    u_int32_t		Bus:6;
    u_int32_t		Mode:2;
    SCSI3Addr_struct	Target[2];
} __packed PhysDevAddr_struct;
  
typedef struct {
    u_int32_t		VolId:30;
    u_int32_t		Mode:2;
    u_int8_t		reserved[4];
} __packed LogDevAddr_struct;

typedef union {
    u_int8_t		LunAddrBytes[8];
    SCSI3Addr_struct	SCSI3Lun[4];
    PhysDevAddr_struct	PhysDev;
    LogDevAddr_struct	LogDev;
} __packed LUNAddr_struct;

typedef struct {
    u_int8_t	CDBLen;
    struct {
	u_int8_t	Type:3;
	u_int8_t	Attribute:3;
	u_int8_t	Direction:2;
    } __packed Type;
    u_int16_t	Timeout;
    u_int8_t	CDB[16];
} __packed RequestBlock_struct;

typedef union {
    struct {
	u_int8_t	Reserved[3];
	u_int8_t	Type;
	u_int32_t	ErrorInfo;
    } __packed Common_Info;
    struct {
	u_int8_t	Reserved[2];
	u_int8_t	offense_size;
	u_int8_t	offense_num;
	u_int32_t	offense_value;
    } __packed Invalid_Cmd;
} __packed MoreErrInfo_struct;

typedef struct {
    u_int8_t		ScsiStatus;
    u_int8_t		SenseLen;
    u_int16_t		CommandStatus;
    u_int32_t		ResidualCnt;
    MoreErrInfo_struct	MoreErrInfo;
    u_int8_t		SenseInfo[SENSEINFOBYTES];
} __packed ErrorInfo_struct;

typedef struct {
    LUNAddr_struct	LUN_info;	/* 8 */
    RequestBlock_struct	Request;	/* 20 */
    ErrorInfo_struct	error_info;	/* 48 */
    u_int16_t		buf_size;	/* 2 */
    u_int8_t		*buf;		/* 4 */
} __packed IOCTL_Command_struct;

#ifdef __amd64__
typedef struct {
    LUNAddr_struct	LUN_info;	/* 8 */
    RequestBlock_struct	Request;	/* 20 */
    ErrorInfo_struct	error_info;	/* 48 */
    u_int16_t		buf_size;	/* 2 */
    u_int32_t		buf;		/* 4 */
} __packed IOCTL_Command_struct32;
#endif

/************************************************************************
 * Command queue statistics
 */

#define CISSQ_FREE	0
#define CISSQ_NOTIFY	1
#define CISSQ_COUNT	2

struct ciss_qstat {
    uint32_t		q_length;
    uint32_t		q_max;
};

union ciss_statrequest {
    uint32_t		cs_item;
    struct ciss_qstat	cs_qstat;
};

/*
 * Note that we'd normally pass the struct in directly, but
 * this code is trying to be compatible with other drivers.
 */
#define CCISS_GETPCIINFO	_IOR ('C', 200, cciss_pci_info_struct)
#define CCISS_GETINTINFO	_IOR ('C', 201, cciss_coalint_struct)
#define CCISS_SETINTINFO	_IOW ('C', 202, cciss_coalint_struct)
#define CCISS_GETNODENAME	_IOR ('C', 203, NodeName_type)
#define CCISS_SETNODENAME	_IOW ('C', 204, NodeName_type)
#define CCISS_GETHEARTBEAT	_IOR ('C', 205, Heartbeat_type)
#define CCISS_GETBUSTYPES	_IOR ('C', 206, BusTypes_type)
#define CCISS_GETFIRMVER	_IOR ('C', 207, FirmwareVer_type)
#define CCISS_GETDRIVERVER	_IOR ('C', 208, DriverVer_type)
#define CCISS_REVALIDVOLS	_IO  ('C', 209)
#define CCISS_PASSTHRU		_IOWR ('C', 210, IOCTL_Command_struct)
#ifdef __amd64
#define CCISS_PASSTHRU32	_IOWR ('C', 210, IOCTL_Command_struct32)
#endif
#define CCISS_GETQSTATS		_IOWR ('C', 211, union ciss_statrequest)

#pragma pack()
