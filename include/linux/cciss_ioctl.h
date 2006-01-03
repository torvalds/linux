#ifndef CCISS_IOCTLH
#define CCISS_IOCTLH

#include <linux/types.h>
#include <linux/ioctl.h>

#define CCISS_IOC_MAGIC 'B'


typedef struct _cciss_pci_info_struct
{
	unsigned char 	bus;
	unsigned char 	dev_fn;
	unsigned short	domain;
	__u32 		board_id;
} cciss_pci_info_struct; 

typedef struct _cciss_coalint_struct
{
	__u32  delay;
	__u32  count;
} cciss_coalint_struct;

typedef char NodeName_type[16];

typedef __u32 Heartbeat_type;

#define CISS_PARSCSIU2 	0x0001
#define CISS_PARCSCIU3 	0x0002
#define CISS_FIBRE1G	0x0100
#define CISS_FIBRE2G	0x0200
typedef __u32 BusTypes_type;

typedef char FirmwareVer_type[4];
typedef __u32 DriverVer_type;

#define MAX_KMALLOC_SIZE 128000

#ifndef CCISS_CMD_H
// This defines are duplicated in cciss_cmd.h in the driver directory 

//general boundary defintions
#define SENSEINFOBYTES          32//note that this value may vary between host implementations

//Command Status value
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

//transfer direction
#define XFER_NONE               0x00
#define XFER_WRITE              0x01
#define XFER_READ               0x02
#define XFER_RSVD               0x03

//task attribute
#define ATTR_UNTAGGED           0x00
#define ATTR_SIMPLE             0x04
#define ATTR_HEADOFQUEUE        0x05
#define ATTR_ORDERED            0x06
#define ATTR_ACA                0x07

//cdb type
#define TYPE_CMD				0x00
#define TYPE_MSG				0x01

// Type defs used in the following structs
#define BYTE __u8
#define WORD __u16
#define HWORD __u16
#define DWORD __u32

#define CISS_MAX_LUN	16	

#define LEVEL2LUN   1   // index into Target(x) structure, due to byte swapping
#define LEVEL3LUN   0

#pragma pack(1)

//Command List Structure
typedef union _SCSI3Addr_struct {
   struct {
    BYTE Dev;
    BYTE Bus:6;
    BYTE Mode:2;        // b00
  } PeripDev;
   struct {
    BYTE DevLSB;
    BYTE DevMSB:6;
    BYTE Mode:2;        // b01
  } LogDev;
   struct {
    BYTE Dev:5;
    BYTE Bus:3;
    BYTE Targ:6;
    BYTE Mode:2;        // b10
  } LogUnit;
} SCSI3Addr_struct;

typedef struct _PhysDevAddr_struct {
  DWORD             TargetId:24;
  DWORD             Bus:6;
  DWORD             Mode:2;
  SCSI3Addr_struct  Target[2]; //2 level target device addr
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
  }Common_Info;
  struct{
    BYTE  Reserved[2];
    BYTE  offense_size;//size of offending entry
    BYTE  offense_num; //byte # of offense 0-base
    DWORD offense_value;
  }Invalid_Cmd;
}MoreErrInfo_struct;
typedef struct _ErrorInfo_struct {
  BYTE               ScsiStatus;
  BYTE               SenseLen;
  HWORD              CommandStatus;
  DWORD              ResidualCnt;
  MoreErrInfo_struct MoreErrInfo;
  BYTE               SenseInfo[SENSEINFOBYTES];
} ErrorInfo_struct;

#pragma pack()
#endif /* CCISS_CMD_H */ 

typedef struct _IOCTL_Command_struct {
  LUNAddr_struct	   LUN_info;
  RequestBlock_struct      Request;
  ErrorInfo_struct  	   error_info; 
  WORD			   buf_size;  /* size in bytes of the buf */
  BYTE			   __user *buf;
} IOCTL_Command_struct;

typedef struct _BIG_IOCTL_Command_struct {
  LUNAddr_struct	   LUN_info;
  RequestBlock_struct      Request;
  ErrorInfo_struct  	   error_info;
  DWORD			   malloc_size; /* < MAX_KMALLOC_SIZE in cciss.c */
  DWORD			   buf_size;    /* size in bytes of the buf */
  				        /* < malloc_size * MAXSGENTRIES */
  BYTE			   __user *buf;
} BIG_IOCTL_Command_struct;

typedef struct _LogvolInfo_struct{
	__u32	LunID;
	int	num_opens;  /* number of opens on the logical volume */
	int	num_parts;  /* number of partitions configured on logvol */
} LogvolInfo_struct;

#define CCISS_GETPCIINFO _IOR(CCISS_IOC_MAGIC, 1, cciss_pci_info_struct)

#define CCISS_GETINTINFO _IOR(CCISS_IOC_MAGIC, 2, cciss_coalint_struct)
#define CCISS_SETINTINFO _IOW(CCISS_IOC_MAGIC, 3, cciss_coalint_struct)

#define CCISS_GETNODENAME _IOR(CCISS_IOC_MAGIC, 4, NodeName_type)
#define CCISS_SETNODENAME _IOW(CCISS_IOC_MAGIC, 5, NodeName_type)

#define CCISS_GETHEARTBEAT _IOR(CCISS_IOC_MAGIC, 6, Heartbeat_type)
#define CCISS_GETBUSTYPES  _IOR(CCISS_IOC_MAGIC, 7, BusTypes_type)
#define CCISS_GETFIRMVER   _IOR(CCISS_IOC_MAGIC, 8, FirmwareVer_type)
#define CCISS_GETDRIVVER   _IOR(CCISS_IOC_MAGIC, 9, DriverVer_type)
#define CCISS_REVALIDVOLS  _IO(CCISS_IOC_MAGIC, 10)
#define CCISS_PASSTHRU	   _IOWR(CCISS_IOC_MAGIC, 11, IOCTL_Command_struct)
#define CCISS_DEREGDISK	   _IO(CCISS_IOC_MAGIC, 12)

/* no longer used... use REGNEWD instead */ 
#define CCISS_REGNEWDISK  _IOW(CCISS_IOC_MAGIC, 13, int)

#define CCISS_REGNEWD	   _IO(CCISS_IOC_MAGIC, 14)
#define CCISS_RESCANDISK   _IO(CCISS_IOC_MAGIC, 16)
#define CCISS_GETLUNINFO   _IOR(CCISS_IOC_MAGIC, 17, LogvolInfo_struct)
#define CCISS_BIG_PASSTHRU _IOWR(CCISS_IOC_MAGIC, 18, BIG_IOCTL_Command_struct)

#ifdef __KERNEL__
#ifdef CONFIG_COMPAT

/* 32 bit compatible ioctl structs */
typedef struct _IOCTL32_Command_struct {
  LUNAddr_struct	   LUN_info;
  RequestBlock_struct      Request;
  ErrorInfo_struct  	   error_info;
  WORD			   buf_size;  /* size in bytes of the buf */
  __u32			   buf; /* 32 bit pointer to data buffer */
} IOCTL32_Command_struct;

typedef struct _BIG_IOCTL32_Command_struct {
  LUNAddr_struct	   LUN_info;
  RequestBlock_struct      Request;
  ErrorInfo_struct  	   error_info;
  DWORD			   malloc_size; /* < MAX_KMALLOC_SIZE in cciss.c */
  DWORD			   buf_size;    /* size in bytes of the buf */
  				        /* < malloc_size * MAXSGENTRIES */
  __u32 		buf;	/* 32 bit pointer to data buffer */
} BIG_IOCTL32_Command_struct;

#define CCISS_PASSTHRU32   _IOWR(CCISS_IOC_MAGIC, 11, IOCTL32_Command_struct)
#define CCISS_BIG_PASSTHRU32 _IOWR(CCISS_IOC_MAGIC, 18, BIG_IOCTL32_Command_struct)

#endif /* CONFIG_COMPAT */
#endif /* __KERNEL__ */
#endif  
