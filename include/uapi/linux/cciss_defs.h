/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef CCISS_DEFS_H
#define CCISS_DEFS_H

#include <linux/types.h>

/* general boundary definitions */
#define SENSEINFOBYTES          32 /* note that this value may vary
				      between host implementations */

/* Command Status value */
#define CMD_SUCCESS             0x0000
#define CMD_TARGET_STATUS       0x0001
#define CMD_DATA_UNDERRUN       0x0002
#define CMD_DATA_OVERRUN        0x0003
#define CMD_INVALID             0x0004
#define CMD_PROTOCOL_ERR        0x0005
#define CMD_HARDWARE_ERR        0x0006
#define CMD_CONNECTION_LOST     0x0007
#define CMD_ABORTED             0x0008
#define CMD_ABORT_FAILED        0x0009
#define CMD_UNSOLICITED_ABORT   0x000A
#define CMD_TIMEOUT             0x000B
#define CMD_UNABORTABLE		0x000C

/* transfer direction */
#define XFER_NONE               0x00
#define XFER_WRITE              0x01
#define XFER_READ               0x02
#define XFER_RSVD               0x03

/* task attribute */
#define ATTR_UNTAGGED           0x00
#define ATTR_SIMPLE             0x04
#define ATTR_HEADOFQUEUE        0x05
#define ATTR_ORDERED            0x06
#define ATTR_ACA                0x07

/* cdb type */
#define TYPE_CMD				0x00
#define TYPE_MSG				0x01

/* Type defs used in the following structs */
#define BYTE __u8
#define WORD __u16
#define HWORD __u16
#define DWORD __u32

#define CISS_MAX_LUN	1024

#define LEVEL2LUN   1 /* index into Target(x) structure, due to byte swapping */
#define LEVEL3LUN   0

#pragma pack(1)

/* Command List Structure */
typedef union _SCSI3Addr_struct {
   struct {
    BYTE Dev;
    BYTE Bus:6;
    BYTE Mode:2;        /* b00 */
  } PeripDev;
   struct {
    BYTE DevLSB;
    BYTE DevMSB:6;
    BYTE Mode:2;        /* b01 */
  } LogDev;
   struct {
    BYTE Dev:5;
    BYTE Bus:3;
    BYTE Targ:6;
    BYTE Mode:2;        /* b10 */
  } LogUnit;
} SCSI3Addr_struct;

typedef struct _PhysDevAddr_struct {
  DWORD             TargetId:24;
  DWORD             Bus:6;
  DWORD             Mode:2;
  SCSI3Addr_struct  Target[2]; /* 2 level target device addr */
} PhysDevAddr_struct;

typedef struct _LogDevAddr_struct {
  DWORD            VolId:30;
  DWORD            Mode:2;
  BYTE             reserved[4];
} LogDevAddr_struct;

typedef union _LUNAddr_struct {
  BYTE               LunAddrBytes[8];
  SCSI3Addr_struct   SCSI3Lun[4];
  PhysDevAddr_struct PhysDev;
  LogDevAddr_struct  LogDev;
} LUNAddr_struct;

typedef struct _RequestBlock_struct {
  BYTE   CDBLen;
  struct {
    BYTE Type:3;
    BYTE Attribute:3;
    BYTE Direction:2;
  } Type;
  HWORD  Timeout;
  BYTE   CDB[16];
} RequestBlock_struct;

typedef union _MoreErrInfo_struct{
  struct {
    BYTE  Reserved[3];
    BYTE  Type;
    DWORD ErrorInfo;
  } Common_Info;
  struct{
    BYTE  Reserved[2];
    BYTE  offense_size; /* size of offending entry */
    BYTE  offense_num;  /* byte # of offense 0-base */
    DWORD offense_value;
  } Invalid_Cmd;
} MoreErrInfo_struct;
typedef struct _ErrorInfo_struct {
  BYTE               ScsiStatus;
  BYTE               SenseLen;
  HWORD              CommandStatus;
  DWORD              ResidualCnt;
  MoreErrInfo_struct MoreErrInfo;
  BYTE               SenseInfo[SENSEINFOBYTES];
} ErrorInfo_struct;

#pragma pack()

#endif /* CCISS_DEFS_H */
