/*
********************************************************************************
**        OS    : FreeBSD
**   FILE NAME  : arcmsr.h
**        BY    : Erich Chen, Ching Huang
**   Description: SCSI RAID Device Driver for 
**                ARECA (ARC11XX/ARC12XX/ARC13XX/ARC16XX/ARC188x)
**                SATA/SAS RAID HOST Adapter
********************************************************************************
********************************************************************************
** SPDX-License-Identifier: BSD-3-Clause
**
** Copyright (C) 2002 - 2012, Areca Technology Corporation All rights reserved.
**
** Redistribution and use in source and binary forms,with or without
** modification,are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice,this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice,this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES,INCLUDING,BUT NOT LIMITED TO,THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,INDIRECT,
** INCIDENTAL,SPECIAL,EXEMPLARY,OR CONSEQUENTIAL DAMAGES(INCLUDING,BUT
** NOT LIMITED TO,PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA,OR PROFITS; OR BUSINESS INTERRUPTION)HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY,WHETHER IN CONTRACT,STRICT LIABILITY,OR TORT
**(INCLUDING NEGLIGENCE OR OTHERWISE)ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE,EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**************************************************************************
* $FreeBSD$
*/
#define ARCMSR_SCSI_INITIATOR_ID	255
#define ARCMSR_DEV_SECTOR_SIZE		512
#define ARCMSR_MAX_XFER_SECTORS		4096
#define ARCMSR_MAX_TARGETID		17	/*16 max target id + 1*/
#define ARCMSR_MAX_TARGETLUN		8	/*8*/
#define ARCMSR_MAX_CHIPTYPE_NUM		4
#define ARCMSR_MAX_OUTSTANDING_CMD	256
#define ARCMSR_MAX_START_JOB		256
#define ARCMSR_MAX_CMD_PERLUN		ARCMSR_MAX_OUTSTANDING_CMD
#define ARCMSR_MAX_FREESRB_NUM		384
#define ARCMSR_MAX_QBUFFER		4096	/* ioctl QBUFFER */
#define ARCMSR_MAX_SG_ENTRIES		38	/* max 38*/
#define ARCMSR_MAX_ADAPTER		4
#define ARCMSR_RELEASE_SIMQ_LEVEL	230
#define ARCMSR_MAX_HBB_POSTQUEUE	264	/* (ARCMSR_MAX_OUTSTANDING_CMD+8) */
#define ARCMSR_MAX_HBD_POSTQUEUE	256
#define	ARCMSR_TIMEOUT_DELAY		60	/* in sec */
#define	ARCMSR_NUM_MSIX_VECTORS		4
/*
*********************************************************************
*/
#ifndef TRUE
	#define TRUE  1
#endif
#ifndef FALSE
	#define FALSE 0
#endif
#ifndef INTR_ENTROPY
	# define INTR_ENTROPY 0
#endif

#ifndef offsetof
	#define offsetof(type, member)  ((size_t)(&((type *)0)->member))
#endif

#if __FreeBSD_version >= 500005
    #define ARCMSR_LOCK_INIT(l, s)	mtx_init(l, s, NULL, MTX_DEF)
    #define ARCMSR_LOCK_DESTROY(l)	mtx_destroy(l)
    #define ARCMSR_LOCK_ACQUIRE(l)	mtx_lock(l)
    #define ARCMSR_LOCK_RELEASE(l)	mtx_unlock(l)
    #define ARCMSR_LOCK_TRY(l)		mtx_trylock(l)
    #define arcmsr_htole32(x)		htole32(x)
    typedef struct mtx			arcmsr_lock_t;
#else
    #define ARCMSR_LOCK_INIT(l, s)	simple_lock_init(l)
    #define ARCMSR_LOCK_DESTROY(l)
    #define ARCMSR_LOCK_ACQUIRE(l)	simple_lock(l)
    #define ARCMSR_LOCK_RELEASE(l)	simple_unlock(l)
    #define ARCMSR_LOCK_TRY(l)		simple_lock_try(l)
    #define arcmsr_htole32(x)		(x)
    typedef struct simplelock		arcmsr_lock_t;
#endif

/*
**********************************************************************************
**
**********************************************************************************
*/
#define PCI_VENDOR_ID_ARECA		0x17D3 /* Vendor ID	*/
#define PCI_DEVICE_ID_ARECA_1110        0x1110 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1120        0x1120 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1130        0x1130 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1160        0x1160 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1170        0x1170 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1200        0x1200 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1201        0x1201 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1203        0x1203 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1210        0x1210 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1212        0x1212 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1214        0x1214 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1220        0x1220 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1222        0x1222 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1230        0x1230 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1231        0x1231 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1260        0x1260 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1261        0x1261 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1270        0x1270 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1280        0x1280 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1380        0x1380 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1381        0x1381 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1680        0x1680 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1681        0x1681 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1880        0x1880 /* Device ID	*/
#define PCI_DEVICE_ID_ARECA_1884        0x1884 /* Device ID	*/

#define ARECA_SUB_DEV_ID_1880	0x1880 /* Subsystem Device ID	*/
#define ARECA_SUB_DEV_ID_1882	0x1882 /* Subsystem Device ID	*/
#define ARECA_SUB_DEV_ID_1883	0x1883 /* Subsystem Device ID	*/
#define ARECA_SUB_DEV_ID_1884	0x1884 /* Subsystem Device ID	*/
#define ARECA_SUB_DEV_ID_1212	0x1212 /* Subsystem Device ID	*/
#define ARECA_SUB_DEV_ID_1213	0x1213 /* Subsystem Device ID	*/
#define ARECA_SUB_DEV_ID_1216	0x1216 /* Subsystem Device ID	*/
#define ARECA_SUB_DEV_ID_1222	0x1222 /* Subsystem Device ID	*/
#define ARECA_SUB_DEV_ID_1223	0x1223 /* Subsystem Device ID	*/
#define ARECA_SUB_DEV_ID_1226	0x1226 /* Subsystem Device ID	*/

#define PCIDevVenIDARC1110              0x111017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1120              0x112017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1130              0x113017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1160              0x116017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1170              0x117017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1200              0x120017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1201              0x120117D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1203              0x120317D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1210              0x121017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1212              0x121217D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1213              0x121317D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1214              0x121417D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1220              0x122017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1222              0x122217D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1223              0x122317D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1230              0x123017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1231              0x123117D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1260              0x126017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1261              0x126117D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1270              0x127017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1280              0x128017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1380              0x138017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1381              0x138117D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1680              0x168017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1681              0x168117D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1880              0x188017D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1882              0x188217D3 /* Vendor Device ID	*/
#define PCIDevVenIDARC1884              0x188417D3 /* Vendor Device ID	*/

#ifndef PCIR_BARS
	#define PCIR_BARS	0x10
	#define	PCIR_BAR(x)	(PCIR_BARS + (x) * 4)
#endif

#define PCI_BASE_ADDR0                  0x10
#define PCI_BASE_ADDR1                  0x14
#define PCI_BASE_ADDR2                  0x18
#define PCI_BASE_ADDR3                  0x1C
#define PCI_BASE_ADDR4                  0x20
#define PCI_BASE_ADDR5                  0x24
/*
**********************************************************************************
**
**********************************************************************************
*/
#define ARCMSR_SCSICMD_IOCTL            0x77
#define ARCMSR_CDEVSW_IOCTL             0x88
#define ARCMSR_MESSAGE_FAIL             0x0001
#define	ARCMSR_MESSAGE_SUCCESS          0x0000
/*
**********************************************************************************
**
**********************************************************************************
*/
#define arcmsr_ccbsrb_ptr	spriv_ptr0
#define arcmsr_ccbacb_ptr	spriv_ptr1
#define dma_addr_hi32(addr)	(u_int32_t) ((addr>>16)>>16)
#define dma_addr_lo32(addr)	(u_int32_t) (addr & 0xffffffff)
#define get_min(x,y)		((x) < (y) ? (x) : (y))
#define get_max(x,y)		((x) < (y) ? (y) : (x))
/*
**************************************************************************
**************************************************************************
*/
#define CHIP_REG_READ32(s, b, r)	bus_space_read_4(acb->btag[b], acb->bhandle[b], offsetof(struct s, r))
#define CHIP_REG_WRITE32(s, b, r, d)	bus_space_write_4(acb->btag[b], acb->bhandle[b], offsetof(struct s, r), d)
#define READ_CHIP_REG32(b, r)		bus_space_read_4(acb->btag[b], acb->bhandle[b], r)
#define WRITE_CHIP_REG32(b, r, d)	bus_space_write_4(acb->btag[b], acb->bhandle[b], r, d)
/*
**********************************************************************************
**    IOCTL CONTROL Mail Box
**********************************************************************************
*/
struct CMD_MESSAGE {
      u_int32_t HeaderLength;
      u_int8_t Signature[8];
      u_int32_t Timeout;
      u_int32_t ControlCode;
      u_int32_t ReturnCode;
      u_int32_t Length;
};

struct CMD_MESSAGE_FIELD {
    struct CMD_MESSAGE cmdmessage; /* ioctl header */
    u_int8_t           messagedatabuffer[1032]; /* areca gui program does not accept more than 1031 byte */
};

/************************************************************************/
/************************************************************************/

#define ARCMSR_IOP_ERROR_ILLEGALPCI		0x0001
#define ARCMSR_IOP_ERROR_VENDORID		0x0002
#define ARCMSR_IOP_ERROR_DEVICEID		0x0002
#define ARCMSR_IOP_ERROR_ILLEGALCDB		0x0003
#define ARCMSR_IOP_ERROR_UNKNOW_CDBERR		0x0004
#define ARCMSR_SYS_ERROR_MEMORY_ALLOCATE	0x0005
#define ARCMSR_SYS_ERROR_MEMORY_CROSS4G		0x0006
#define ARCMSR_SYS_ERROR_MEMORY_LACK		0x0007
#define ARCMSR_SYS_ERROR_MEMORY_RANGE		0x0008
#define ARCMSR_SYS_ERROR_DEVICE_BASE		0x0009
#define ARCMSR_SYS_ERROR_PORT_VALIDATE		0x000A

/*DeviceType*/
#define ARECA_SATA_RAID                      	0x90000000

/*FunctionCode*/
#define FUNCTION_READ_RQBUFFER               	0x0801
#define FUNCTION_WRITE_WQBUFFER              	0x0802
#define FUNCTION_CLEAR_RQBUFFER              	0x0803
#define FUNCTION_CLEAR_WQBUFFER              	0x0804
#define FUNCTION_CLEAR_ALLQBUFFER            	0x0805
#define FUNCTION_REQUEST_RETURNCODE_3F         	0x0806
#define FUNCTION_SAY_HELLO                   	0x0807
#define FUNCTION_SAY_GOODBYE                    0x0808
#define FUNCTION_FLUSH_ADAPTER_CACHE           	0x0809
/*
************************************************************************
**      IOCTL CONTROL CODE
************************************************************************
*/
/* ARECA IO CONTROL CODE*/
#define ARCMSR_MESSAGE_READ_RQBUFFER           	_IOWR('F', FUNCTION_READ_RQBUFFER, struct CMD_MESSAGE_FIELD)
#define ARCMSR_MESSAGE_WRITE_WQBUFFER          	_IOWR('F', FUNCTION_WRITE_WQBUFFER, struct CMD_MESSAGE_FIELD)
#define ARCMSR_MESSAGE_CLEAR_RQBUFFER          	_IOWR('F', FUNCTION_CLEAR_RQBUFFER, struct CMD_MESSAGE_FIELD)
#define ARCMSR_MESSAGE_CLEAR_WQBUFFER          	_IOWR('F', FUNCTION_CLEAR_WQBUFFER, struct CMD_MESSAGE_FIELD)
#define ARCMSR_MESSAGE_CLEAR_ALLQBUFFER        	_IOWR('F', FUNCTION_CLEAR_ALLQBUFFER, struct CMD_MESSAGE_FIELD)
#define ARCMSR_MESSAGE_REQUEST_RETURNCODE_3F   	_IOWR('F', FUNCTION_REQUEST_RETURNCODE_3F, struct CMD_MESSAGE_FIELD)
#define ARCMSR_MESSAGE_SAY_HELLO               	_IOWR('F', FUNCTION_SAY_HELLO, struct CMD_MESSAGE_FIELD) 
#define ARCMSR_MESSAGE_SAY_GOODBYE              _IOWR('F', FUNCTION_SAY_GOODBYE, struct CMD_MESSAGE_FIELD)
#define ARCMSR_MESSAGE_FLUSH_ADAPTER_CACHE      _IOWR('F', FUNCTION_FLUSH_ADAPTER_CACHE, struct CMD_MESSAGE_FIELD)

/* ARECA IOCTL ReturnCode */
#define ARCMSR_MESSAGE_RETURNCODE_OK		0x00000001
#define ARCMSR_MESSAGE_RETURNCODE_ERROR		0x00000006
#define ARCMSR_MESSAGE_RETURNCODE_3F		0x0000003F
#define ARCMSR_IOCTL_RETURNCODE_BUS_HANG_ON	0x00000088
/* 
************************************************************************
**                SPEC. for Areca HBA adapter
************************************************************************
*/
/* signature of set and get firmware config */
#define ARCMSR_SIGNATURE_GET_CONFIG		0x87974060
#define ARCMSR_SIGNATURE_SET_CONFIG		0x87974063
/* message code of inbound message register */
#define ARCMSR_INBOUND_MESG0_NOP		0x00000000
#define ARCMSR_INBOUND_MESG0_GET_CONFIG		0x00000001
#define ARCMSR_INBOUND_MESG0_SET_CONFIG		0x00000002
#define ARCMSR_INBOUND_MESG0_ABORT_CMD		0x00000003
#define ARCMSR_INBOUND_MESG0_STOP_BGRB		0x00000004
#define ARCMSR_INBOUND_MESG0_FLUSH_CACHE	0x00000005
#define ARCMSR_INBOUND_MESG0_START_BGRB		0x00000006
#define ARCMSR_INBOUND_MESG0_CHK331PENDING	0x00000007
#define ARCMSR_INBOUND_MESG0_SYNC_TIMER		0x00000008
/* doorbell interrupt generator */
#define ARCMSR_INBOUND_DRIVER_DATA_WRITE_OK	0x00000001
#define ARCMSR_INBOUND_DRIVER_DATA_READ_OK	0x00000002
#define ARCMSR_OUTBOUND_IOP331_DATA_WRITE_OK	0x00000001
#define ARCMSR_OUTBOUND_IOP331_DATA_READ_OK	0x00000002
/* srb areca cdb flag */
#define ARCMSR_SRBPOST_FLAG_SGL_BSIZE		0x80000000
#define ARCMSR_SRBPOST_FLAG_IAM_BIOS		0x40000000
#define ARCMSR_SRBREPLY_FLAG_IAM_BIOS		0x40000000
#define ARCMSR_SRBREPLY_FLAG_ERROR		0x10000000
#define ARCMSR_SRBREPLY_FLAG_ERROR_MODE0        0x10000000
#define ARCMSR_SRBREPLY_FLAG_ERROR_MODE1	0x00000001
/* outbound firmware ok */
#define ARCMSR_OUTBOUND_MESG1_FIRMWARE_OK	0x80000000

#define ARCMSR_ARC1680_BUS_RESET		0x00000003
/* 
************************************************************************
**                SPEC. for Areca HBB adapter
************************************************************************
*/
/* ARECA HBB COMMAND for its FIRMWARE */
#define ARCMSR_DRV2IOP_DOORBELL                 0x00020400    /* window of "instruction flags" from driver to iop */
#define ARCMSR_DRV2IOP_DOORBELL_MASK            0x00020404
#define ARCMSR_IOP2DRV_DOORBELL                 0x00020408    /* window of "instruction flags" from iop to driver */
#define ARCMSR_IOP2DRV_DOORBELL_MASK            0x0002040C

#define ARCMSR_IOP2DRV_DOORBELL_1203            0x00021870    /* window of "instruction flags" from iop to driver */
#define ARCMSR_IOP2DRV_DOORBELL_MASK_1203       0x00021874
#define ARCMSR_DRV2IOP_DOORBELL_1203            0x00021878    /* window of "instruction flags" from driver to iop */
#define ARCMSR_DRV2IOP_DOORBELL_MASK_1203       0x0002187C

/* ARECA FLAG LANGUAGE */
#define ARCMSR_IOP2DRV_DATA_WRITE_OK            0x00000001        /* ioctl transfer */
#define ARCMSR_IOP2DRV_DATA_READ_OK             0x00000002        /* ioctl transfer */
#define ARCMSR_IOP2DRV_CDB_DONE                 0x00000004
#define ARCMSR_IOP2DRV_MESSAGE_CMD_DONE         0x00000008

#define ARCMSR_DOORBELL_HANDLE_INT		0x0000000F
#define ARCMSR_DOORBELL_INT_CLEAR_PATTERN       0xFF00FFF0
#define ARCMSR_MESSAGE_INT_CLEAR_PATTERN        0xFF00FFF7

#define ARCMSR_MESSAGE_GET_CONFIG		0x00010008	/* (ARCMSR_INBOUND_MESG0_GET_CONFIG<<16)|ARCMSR_DRV2IOP_MESSAGE_CMD_POSTED) */
#define ARCMSR_MESSAGE_SET_CONFIG		0x00020008	/* (ARCMSR_INBOUND_MESG0_SET_CONFIG<<16)|ARCMSR_DRV2IOP_MESSAGE_CMD_POSTED) */
#define ARCMSR_MESSAGE_ABORT_CMD		0x00030008	/* (ARCMSR_INBOUND_MESG0_ABORT_CMD<<16)|ARCMSR_DRV2IOP_MESSAGE_CMD_POSTED) */
#define ARCMSR_MESSAGE_STOP_BGRB		0x00040008	/* (ARCMSR_INBOUND_MESG0_STOP_BGRB<<16)|ARCMSR_DRV2IOP_MESSAGE_CMD_POSTED) */
#define ARCMSR_MESSAGE_FLUSH_CACHE              0x00050008	/* (ARCMSR_INBOUND_MESG0_FLUSH_CACHE<<16)|ARCMSR_DRV2IOP_MESSAGE_CMD_POSTED) */
#define ARCMSR_MESSAGE_START_BGRB		0x00060008	/* (ARCMSR_INBOUND_MESG0_START_BGRB<<16)|ARCMSR_DRV2IOP_MESSAGE_CMD_POSTED) */
#define ARCMSR_MESSAGE_START_DRIVER_MODE	0x000E0008	
#define ARCMSR_MESSAGE_SET_POST_WINDOW		0x000F0008	
#define ARCMSR_MESSAGE_ACTIVE_EOI_MODE		0x00100008
#define ARCMSR_MESSAGE_FIRMWARE_OK		0x80000000	/* ARCMSR_OUTBOUND_MESG1_FIRMWARE_OK */

#define ARCMSR_DRV2IOP_DATA_WRITE_OK            0x00000001	/* ioctl transfer */
#define ARCMSR_DRV2IOP_DATA_READ_OK             0x00000002	/* ioctl transfer */
#define ARCMSR_DRV2IOP_CDB_POSTED               0x00000004
#define ARCMSR_DRV2IOP_MESSAGE_CMD_POSTED       0x00000008
#define ARCMSR_DRV2IOP_END_OF_INTERRUPT         0x00000010  /*  */

/* data tunnel buffer between user space program and its firmware */
#define ARCMSR_MSGCODE_RWBUFFER			0x0000fa00    /* iop msgcode_rwbuffer for message command */
#define ARCMSR_IOCTL_WBUFFER			0x0000fe00    /* user space data to iop 128bytes */
#define ARCMSR_IOCTL_RBUFFER			0x0000ff00    /* iop data to user space 128bytes */
#define ARCMSR_HBB_BASE0_OFFSET			0x00000010
#define ARCMSR_HBB_BASE1_OFFSET			0x00000018
#define ARCMSR_HBB_BASE0_LEN			0x00021000
#define ARCMSR_HBB_BASE1_LEN			0x00010000
/* 
************************************************************************
**                SPEC. for Areca HBC adapter
************************************************************************
*/
#define ARCMSR_HBC_ISR_THROTTLING_LEVEL                 12
#define ARCMSR_HBC_ISR_MAX_DONE_QUEUE                   20
/* Host Interrupt Mask */
#define ARCMSR_HBCMU_UTILITY_A_ISR_MASK                 0x00000001 /* When clear, the Utility_A interrupt routes to the host.*/
#define ARCMSR_HBCMU_OUTBOUND_DOORBELL_ISR_MASK         0x00000004 /* When clear, the General Outbound Doorbell interrupt routes to the host.*/
#define ARCMSR_HBCMU_OUTBOUND_POSTQUEUE_ISR_MASK        0x00000008 /* When clear, the Outbound Post List FIFO Not Empty interrupt routes to the host.*/
#define ARCMSR_HBCMU_ALL_INTMASKENABLE                  0x0000000D /* disable all ISR */
/* Host Interrupt Status */
#define ARCMSR_HBCMU_UTILITY_A_ISR                      0x00000001
        /*
        ** Set when the Utility_A Interrupt bit is set in the Outbound Doorbell Register. 
        ** It clears by writing a 1 to the Utility_A bit in the Outbound Doorbell Clear Register or through automatic clearing (if enabled).
        */
#define ARCMSR_HBCMU_OUTBOUND_DOORBELL_ISR              0x00000004
        /*
        ** Set if Outbound Doorbell register bits 30:1 have a non-zero
        ** value. This bit clears only when Outbound Doorbell bits
        ** 30:1 are ALL clear. Only a write to the Outbound Doorbell
        ** Clear register clears bits in the Outbound Doorbell register.
        */
#define ARCMSR_HBCMU_OUTBOUND_POSTQUEUE_ISR             0x00000008
        /*
        ** Set whenever the Outbound Post List Producer/Consumer
        ** Register (FIFO) is not empty. It clears when the Outbound
        ** Post List FIFO is empty.
        */
#define ARCMSR_HBCMU_SAS_ALL_INT                        0x00000010
        /*
        ** This bit indicates a SAS interrupt from a source external to
        ** the PCIe core. This bit is not maskable.
        */
/* DoorBell*/
#define ARCMSR_HBCMU_DRV2IOP_DATA_WRITE_OK                      0x00000002/**/
#define ARCMSR_HBCMU_DRV2IOP_DATA_READ_OK                       0x00000004/**/
#define ARCMSR_HBCMU_DRV2IOP_MESSAGE_CMD_DONE                   0x00000008/*inbound message 0 ready*/
#define ARCMSR_HBCMU_DRV2IOP_POSTQUEUE_THROTTLING               0x00000010/*more than 12 request completed in a time*/
#define ARCMSR_HBCMU_IOP2DRV_DATA_WRITE_OK                      0x00000002/**/
#define ARCMSR_HBCMU_IOP2DRV_DATA_WRITE_DOORBELL_CLEAR          0x00000002/*outbound DATA WRITE isr door bell clear*/
#define ARCMSR_HBCMU_IOP2DRV_DATA_READ_OK                       0x00000004/**/
#define ARCMSR_HBCMU_IOP2DRV_DATA_READ_DOORBELL_CLEAR           0x00000004/*outbound DATA READ isr door bell clear*/
#define ARCMSR_HBCMU_IOP2DRV_MESSAGE_CMD_DONE                   0x00000008/*outbound message 0 ready*/
#define ARCMSR_HBCMU_IOP2DRV_MESSAGE_CMD_DONE_DOORBELL_CLEAR    0x00000008/*outbound message cmd isr door bell clear*/
#define ARCMSR_HBCMU_MESSAGE_FIRMWARE_OK		        0x80000000/*ARCMSR_HBCMU_MESSAGE_FIRMWARE_OK*/
#define ARCMSR_HBCMU_RESET_ADAPTER				0x00000024
#define ARCMSR_HBCMU_DiagWrite_ENABLE				0x00000080

/* 
************************************************************************
**                SPEC. for Areca HBD adapter
************************************************************************
*/
#define ARCMSR_HBDMU_CHIP_ID				0x00004
#define ARCMSR_HBDMU_CPU_MEMORY_CONFIGURATION		0x00008
#define ARCMSR_HBDMU_I2_HOST_INTERRUPT_MASK		0x00034
#define ARCMSR_HBDMU_MAIN_INTERRUPT_STATUS		0x00200
#define ARCMSR_HBDMU_PCIE_F0_INTERRUPT_ENABLE		0x0020C
#define ARCMSR_HBDMU_INBOUND_MESSAGE0			0x00400
#define ARCMSR_HBDMU_INBOUND_MESSAGE1			0x00404
#define ARCMSR_HBDMU_OUTBOUND_MESSAGE0			0x00420
#define ARCMSR_HBDMU_OUTBOUND_MESSAGE1			0x00424
#define ARCMSR_HBDMU_INBOUND_DOORBELL			0x00460
#define ARCMSR_HBDMU_OUTBOUND_DOORBELL			0x00480
#define ARCMSR_HBDMU_OUTBOUND_DOORBELL_ENABLE		0x00484
#define ARCMSR_HBDMU_INBOUND_LIST_BASE_LOW		0x01000
#define ARCMSR_HBDMU_INBOUND_LIST_BASE_HIGH		0x01004
#define ARCMSR_HBDMU_INBOUND_LIST_WRITE_POINTER		0x01018
#define ARCMSR_HBDMU_OUTBOUND_LIST_BASE_LOW		0x01060
#define ARCMSR_HBDMU_OUTBOUND_LIST_BASE_HIGH		0x01064
#define ARCMSR_HBDMU_OUTBOUND_LIST_COPY_POINTER		0x0106C
#define ARCMSR_HBDMU_OUTBOUND_LIST_READ_POINTER		0x01070
#define ARCMSR_HBDMU_OUTBOUND_INTERRUPT_CAUSE		0x01088
#define ARCMSR_HBDMU_OUTBOUND_INTERRUPT_ENABLE		0x0108C

#define ARCMSR_HBDMU_MESSAGE_WBUFFER			0x02000
#define ARCMSR_HBDMU_MESSAGE_RBUFFER			0x02100
#define ARCMSR_HBDMU_MESSAGE_RWBUFFER			0x02200

#define ARCMSR_HBDMU_ISR_THROTTLING_LEVEL		16
#define ARCMSR_HBDMU_ISR_MAX_DONE_QUEUE			20

/* Host Interrupt Mask */
#define ARCMSR_HBDMU_ALL_INT_ENABLE			0x00001010	/* enable all ISR */
#define ARCMSR_HBDMU_ALL_INT_DISABLE			0x00000000	/* disable all ISR */

/* Host Interrupt Status */
#define ARCMSR_HBDMU_OUTBOUND_INT			0x00001010
#define ARCMSR_HBDMU_OUTBOUND_DOORBELL_INT		0x00001000
#define ARCMSR_HBDMU_OUTBOUND_POSTQUEUE_INT		0x00000010

/* DoorBell*/
#define ARCMSR_HBDMU_DRV2IOP_DATA_IN_READY		0x00000001
#define ARCMSR_HBDMU_DRV2IOP_DATA_OUT_READ		0x00000002

#define ARCMSR_HBDMU_IOP2DRV_DATA_WRITE_OK		0x00000001
#define ARCMSR_HBDMU_IOP2DRV_DATA_READ_OK		0x00000002

/*outbound message 0 ready*/
#define ARCMSR_HBDMU_IOP2DRV_MESSAGE_CMD_DONE		0x02000000

#define ARCMSR_HBDMU_F0_DOORBELL_CAUSE			0x02000003

/*outbound message cmd isr door bell clear*/
#define ARCMSR_HBDMU_IOP2DRV_MESSAGE_CMD_DONE_CLEAR	0x02000000

/*outbound list */
#define ARCMSR_HBDMU_OUTBOUND_LIST_INTERRUPT		0x00000001
#define ARCMSR_HBDMU_OUTBOUND_LIST_INTERRUPT_CLEAR	0x00000001

/*ARCMSR_HBAMU_MESSAGE_FIRMWARE_OK*/
#define ARCMSR_HBDMU_MESSAGE_FIRMWARE_OK		0x80000000
/* 
*******************************************************************************
**                SPEC. for Areca HBE adapter
*******************************************************************************
*/
#define ARCMSR_SIGNATURE_1884				0x188417D3
#define ARCMSR_HBEMU_OUTBOUND_DOORBELL_ISR		0x00000001
#define ARCMSR_HBEMU_OUTBOUND_POSTQUEUE_ISR		0x00000008
#define ARCMSR_HBEMU_ALL_INTMASKENABLE			0x00000009 /* disable all ISR */

#define ARCMSR_HBEMU_DRV2IOP_DATA_WRITE_OK		0x00000002
#define ARCMSR_HBEMU_DRV2IOP_DATA_READ_OK		0x00000004
#define ARCMSR_HBEMU_DRV2IOP_MESSAGE_CMD_DONE		0x00000008 /* inbound message 0 ready */
#define ARCMSR_HBEMU_IOP2DRV_DATA_WRITE_OK		0x00000002
#define ARCMSR_HBEMU_IOP2DRV_DATA_READ_OK		0x00000004
#define ARCMSR_HBEMU_IOP2DRV_MESSAGE_CMD_DONE		0x00000008 /* outbound message 0 ready */
#define ARCMSR_HBEMU_MESSAGE_FIRMWARE_OK		0x80000000 /* ARCMSR_HBCMU_MESSAGE_FIRMWARE_OK */
/* ARC-1884 doorbell sync */
#define ARCMSR_HBEMU_DOORBELL_SYNC			0x100
#define ARCMSR_ARC188X_RESET_ADAPTER			0x00000004
/*
*********************************************************************
** Message Unit structure
*********************************************************************
*/
struct HBA_MessageUnit
{
	u_int32_t	resrved0[4];		/*0000 000F*/
	u_int32_t	inbound_msgaddr0;	/*0010 0013*/
	u_int32_t	inbound_msgaddr1;	/*0014 0017*/
	u_int32_t	outbound_msgaddr0;	/*0018 001B*/
	u_int32_t	outbound_msgaddr1;	/*001C 001F*/
	u_int32_t	inbound_doorbell;	/*0020 0023*/
	u_int32_t	inbound_intstatus;	/*0024 0027*/
	u_int32_t	inbound_intmask;	/*0028 002B*/
	u_int32_t	outbound_doorbell;	/*002C 002F*/
	u_int32_t	outbound_intstatus;	/*0030 0033*/
	u_int32_t	outbound_intmask;	/*0034 0037*/
	u_int32_t	reserved1[2];		/*0038 003F*/
	u_int32_t	inbound_queueport;	/*0040 0043*/
	u_int32_t	outbound_queueport;	/*0044 0047*/
	u_int32_t	reserved2[2];		/*0048 004F*/
	u_int32_t	reserved3[492];		/*0050 07FF ......local_buffer 492*/
	u_int32_t	reserved4[128];		/*0800 09FF                    128*/
	u_int32_t	msgcode_rwbuffer[256];	/*0a00 0DFF                    256*/
	u_int32_t	message_wbuffer[32];	/*0E00 0E7F                     32*/
	u_int32_t	reserved5[32];		/*0E80 0EFF                     32*/
	u_int32_t	message_rbuffer[32];	/*0F00 0F7F                     32*/
	u_int32_t	reserved6[32];		/*0F80 0FFF                     32*/
};
/*
*********************************************************************
** 
*********************************************************************
*/
struct HBB_DOORBELL_1203
{
	u_int8_t	doorbell_reserved[ARCMSR_IOP2DRV_DOORBELL_1203]; /*reserved */
	u_int32_t	iop2drv_doorbell;          /*offset 0x00021870:00,01,02,03: window of "instruction flags" from iop to driver */
	u_int32_t	iop2drv_doorbell_mask;     /*                  04,05,06,07: doorbell mask */
	u_int32_t	drv2iop_doorbell;          /*                  08,09,10,11: window of "instruction flags" from driver to iop */
	u_int32_t	drv2iop_doorbell_mask;     /*                  12,13,14,15: doorbell mask */
};
struct HBB_DOORBELL
{
	u_int8_t	doorbell_reserved[ARCMSR_DRV2IOP_DOORBELL]; /*reserved */
	u_int32_t	drv2iop_doorbell;          /*offset 0x00020400:00,01,02,03: window of "instruction flags" from driver to iop */
	u_int32_t	drv2iop_doorbell_mask;     /*                  04,05,06,07: doorbell mask */
	u_int32_t	iop2drv_doorbell;          /*                  08,09,10,11: window of "instruction flags" from iop to driver */
	u_int32_t	iop2drv_doorbell_mask;     /*                  12,13,14,15: doorbell mask */
};
/*
*********************************************************************
** 
*********************************************************************
*/
struct HBB_RWBUFFER
{
	u_int8_t	message_reserved0[ARCMSR_MSGCODE_RWBUFFER];   /*reserved */
	u_int32_t	msgcode_rwbuffer[256];      /*offset 0x0000fa00:   0,   1,   2,   3,...,1023: message code read write 1024bytes */
	u_int32_t	message_wbuffer[32];        /*offset 0x0000fe00:1024,1025,1026,1027,...,1151: user space data to iop 128bytes */
	u_int32_t	message_reserved1[32];      /*                  1152,1153,1154,1155,...,1279: message reserved*/
	u_int32_t	message_rbuffer[32];        /*offset 0x0000ff00:1280,1281,1282,1283,...,1407: iop data to user space 128bytes */ 
};
/*
*********************************************************************
** 
*********************************************************************
*/
struct HBB_MessageUnit
{
	u_int32_t		post_qbuffer[ARCMSR_MAX_HBB_POSTQUEUE];       /* post queue buffer for iop */
	u_int32_t		done_qbuffer[ARCMSR_MAX_HBB_POSTQUEUE];       /* done queue buffer for iop */
	int32_t			postq_index;                                  /* post queue index */
	int32_t			doneq_index;								   /* done queue index */
	struct HBB_DOORBELL    *hbb_doorbell;
	struct HBB_RWBUFFER    *hbb_rwbuffer;
	bus_size_t		drv2iop_doorbell;          /* window of "instruction flags" from driver to iop */
	bus_size_t		drv2iop_doorbell_mask;     /* doorbell mask */
	bus_size_t		iop2drv_doorbell;          /* window of "instruction flags" from iop to driver */
	bus_size_t		iop2drv_doorbell_mask;     /* doorbell mask */
};

/*
*********************************************************************
** 
*********************************************************************
*/
struct HBC_MessageUnit {
	u_int32_t	message_unit_status;                        /*0000 0003*/
	u_int32_t	slave_error_attribute;	                    /*0004 0007*/
	u_int32_t	slave_error_address;	                    /*0008 000B*/
	u_int32_t	posted_outbound_doorbell;	            /*000C 000F*/
	u_int32_t	master_error_attribute;	                    /*0010 0013*/
	u_int32_t	master_error_address_low;	            /*0014 0017*/
	u_int32_t	master_error_address_high;	            /*0018 001B*/
	u_int32_t	hcb_size;                                   /*001C 001F size of the PCIe window used for HCB_Mode accesses*/
	u_int32_t	inbound_doorbell;	                    /*0020 0023*/
	u_int32_t	diagnostic_rw_data;	                    /*0024 0027*/
	u_int32_t	diagnostic_rw_address_low;	            /*0028 002B*/
	u_int32_t	diagnostic_rw_address_high;	            /*002C 002F*/
	u_int32_t	host_int_status;	                    /*0030 0033 host interrupt status*/
	u_int32_t	host_int_mask;     	                    /*0034 0037 host interrupt mask*/
	u_int32_t	dcr_data;	                            /*0038 003B*/
	u_int32_t	dcr_address;                                /*003C 003F*/
	u_int32_t	inbound_queueport;                          /*0040 0043 port32 host inbound queue port*/
	u_int32_t	outbound_queueport;                         /*0044 0047 port32 host outbound queue port*/
	u_int32_t	hcb_pci_address_low;                        /*0048 004B*/
	u_int32_t	hcb_pci_address_high;                       /*004C 004F*/
	u_int32_t	iop_int_status;                             /*0050 0053*/
	u_int32_t	iop_int_mask;                               /*0054 0057*/
	u_int32_t	iop_inbound_queue_port;                     /*0058 005B*/
	u_int32_t	iop_outbound_queue_port;                    /*005C 005F*/
	u_int32_t	inbound_free_list_index;                    /*0060 0063 inbound free list producer consumer index*/
	u_int32_t	inbound_post_list_index;                    /*0064 0067 inbound post list producer consumer index*/
	u_int32_t	outbound_free_list_index;                   /*0068 006B outbound free list producer consumer index*/
	u_int32_t	outbound_post_list_index;                   /*006C 006F outbound post list producer consumer index*/
	u_int32_t	inbound_doorbell_clear;                     /*0070 0073*/
	u_int32_t	i2o_message_unit_control;                   /*0074 0077*/
	u_int32_t	last_used_message_source_address_low;       /*0078 007B*/
	u_int32_t	last_used_message_source_address_high;	    /*007C 007F*/
	u_int32_t	pull_mode_data_byte_count[4];               /*0080 008F pull mode data byte count0..count7*/
	u_int32_t	message_dest_address_index;                 /*0090 0093*/
	u_int32_t	done_queue_not_empty_int_counter_timer;     /*0094 0097*/
	u_int32_t	utility_A_int_counter_timer;                /*0098 009B*/
	u_int32_t	outbound_doorbell;                          /*009C 009F*/
	u_int32_t	outbound_doorbell_clear;                    /*00A0 00A3*/
	u_int32_t	message_source_address_index;               /*00A4 00A7 message accelerator source address consumer producer index*/
	u_int32_t	message_done_queue_index;                   /*00A8 00AB message accelerator completion queue consumer producer index*/
	u_int32_t	reserved0;                                  /*00AC 00AF*/
	u_int32_t	inbound_msgaddr0;                           /*00B0 00B3 scratchpad0*/
	u_int32_t	inbound_msgaddr1;                           /*00B4 00B7 scratchpad1*/
	u_int32_t	outbound_msgaddr0;                          /*00B8 00BB scratchpad2*/
	u_int32_t	outbound_msgaddr1;                          /*00BC 00BF scratchpad3*/
	u_int32_t	inbound_queueport_low;                      /*00C0 00C3 port64 host inbound queue port low*/
	u_int32_t	inbound_queueport_high;                     /*00C4 00C7 port64 host inbound queue port high*/
	u_int32_t	outbound_queueport_low;                     /*00C8 00CB port64 host outbound queue port low*/
	u_int32_t	outbound_queueport_high;                    /*00CC 00CF port64 host outbound queue port high*/
	u_int32_t	iop_inbound_queue_port_low;                 /*00D0 00D3*/
	u_int32_t	iop_inbound_queue_port_high;                /*00D4 00D7*/
	u_int32_t	iop_outbound_queue_port_low;                /*00D8 00DB*/
	u_int32_t	iop_outbound_queue_port_high;               /*00DC 00DF*/
	u_int32_t	message_dest_queue_port_low;                /*00E0 00E3 message accelerator destination queue port low*/
	u_int32_t	message_dest_queue_port_high;               /*00E4 00E7 message accelerator destination queue port high*/
	u_int32_t	last_used_message_dest_address_low;         /*00E8 00EB last used message accelerator destination address low*/
	u_int32_t	last_used_message_dest_address_high;        /*00EC 00EF last used message accelerator destination address high*/
	u_int32_t	message_done_queue_base_address_low;        /*00F0 00F3 message accelerator completion queue base address low*/
	u_int32_t	message_done_queue_base_address_high;       /*00F4 00F7 message accelerator completion queue base address high*/
	u_int32_t	host_diagnostic;                            /*00F8 00FB*/
	u_int32_t	write_sequence;                             /*00FC 00FF*/
	u_int32_t	reserved1[34];                              /*0100 0187*/
	u_int32_t	reserved2[1950];                            /*0188 1FFF*/
	u_int32_t	message_wbuffer[32];                        /*2000 207F*/
	u_int32_t	reserved3[32];                              /*2080 20FF*/
	u_int32_t	message_rbuffer[32];                        /*2100 217F*/
	u_int32_t	reserved4[32];                              /*2180 21FF*/
	u_int32_t	msgcode_rwbuffer[256];                      /*2200 23FF*/
};
/*
*********************************************************************
** 
*********************************************************************
*/
struct InBound_SRB {
	uint32_t addressLow; //pointer to SRB block
	uint32_t addressHigh;
	uint32_t length; // in DWORDs
	uint32_t reserved0;
};

struct OutBound_SRB {
	uint32_t addressLow; //pointer to SRB block
	uint32_t addressHigh;
};

struct HBD_MessageUnit {
	uint32_t reserved0;
	uint32_t chip_id;			//0x0004
	uint32_t cpu_mem_config;		//0x0008
	uint32_t reserved1[10];			//0x000C
	uint32_t i2o_host_interrupt_mask;	//0x0034
	uint32_t reserved2[114];		//0x0038
	uint32_t host_int_status;		//0x0200
	uint32_t host_int_enable;		//0x0204
	uint32_t reserved3[1];			//0x0208
	uint32_t pcief0_int_enable;		//0x020C
	uint32_t reserved4[124];		//0x0210
	uint32_t inbound_msgaddr0;		//0x0400
	uint32_t inbound_msgaddr1;		//0x0404
	uint32_t reserved5[6];			//0x0408
	uint32_t outbound_msgaddr0;		//0x0420
	uint32_t outbound_msgaddr1;		//0x0424
	uint32_t reserved6[14];			//0x0428
	uint32_t inbound_doorbell;		//0x0460
	uint32_t reserved7[7];			//0x0464
	uint32_t outbound_doorbell;		//0x0480
	uint32_t outbound_doorbell_enable;	//0x0484
	uint32_t reserved8[734];		//0x0488
	uint32_t inboundlist_base_low;		//0x1000
	uint32_t inboundlist_base_high;		//0x1004
	uint32_t reserved9[4];			//0x1008
	uint32_t inboundlist_write_pointer;	//0x1018
	uint32_t inboundlist_read_pointer;	//0x101C
	uint32_t reserved10[16];		//0x1020
	uint32_t outboundlist_base_low;		//0x1060
	uint32_t outboundlist_base_high;	//0x1064
	uint32_t reserved11;			//0x1068
	uint32_t outboundlist_copy_pointer;	//0x106C
	uint32_t outboundlist_read_pointer;	//0x1070 0x1072
	uint32_t reserved12[5];			//0x1074
	uint32_t outboundlist_interrupt_cause;	//0x1088
	uint32_t outboundlist_interrupt_enable;	//0x108C
	uint32_t reserved13[988];		//0x1090
	uint32_t message_wbuffer[32];		//0x2000
	uint32_t reserved14[32];		//0x2080
	uint32_t message_rbuffer[32];		//0x2100
	uint32_t reserved15[32];		//0x2180
	uint32_t msgcode_rwbuffer[256];		//0x2200
};

struct HBD_MessageUnit0 {
 	struct InBound_SRB post_qbuffer[ARCMSR_MAX_HBD_POSTQUEUE];
   	struct OutBound_SRB done_qbuffer[ARCMSR_MAX_HBD_POSTQUEUE+1];
	uint16_t postq_index;
	uint16_t doneq_index;
	struct HBD_MessageUnit	*phbdmu;
};
/*
*********************************************************************
** 
*********************************************************************
*/
struct HBE_MessageUnit {
	u_int32_t	iobound_doorbell;                           /*0000 0003*/
	u_int32_t	write_sequence_3xxx;	                    /*0004 0007*/
	u_int32_t	host_diagnostic_3xxx;	                    /*0008 000B*/
	u_int32_t	posted_outbound_doorbell;	            /*000C 000F*/
	u_int32_t	master_error_attribute;	                    /*0010 0013*/
	u_int32_t	master_error_address_low;	            /*0014 0017*/
	u_int32_t	master_error_address_high;	            /*0018 001B*/
	u_int32_t	hcb_size;                                   /*001C 001F*/
	u_int32_t	inbound_doorbell;	                    /*0020 0023*/
	u_int32_t	diagnostic_rw_data;	                    /*0024 0027*/
	u_int32_t	diagnostic_rw_address_low;	            /*0028 002B*/
	u_int32_t	diagnostic_rw_address_high;	            /*002C 002F*/
	u_int32_t	host_int_status;	                    /*0030 0033 host interrupt status*/
	u_int32_t	host_int_mask;     	                    /*0034 0037 host interrupt mask*/
	u_int32_t	dcr_data;	                            /*0038 003B*/
	u_int32_t	dcr_address;                                /*003C 003F*/
	u_int32_t	inbound_queueport;                          /*0040 0043 port32 host inbound queue port*/
	u_int32_t	outbound_queueport;                         /*0044 0047 port32 host outbound queue port*/
	u_int32_t	hcb_pci_address_low;                        /*0048 004B*/
	u_int32_t	hcb_pci_address_high;                       /*004C 004F*/
	u_int32_t	iop_int_status;                             /*0050 0053*/
	u_int32_t	iop_int_mask;                               /*0054 0057*/
	u_int32_t	iop_inbound_queue_port;                     /*0058 005B*/
	u_int32_t	iop_outbound_queue_port;                    /*005C 005F*/
	u_int32_t	inbound_free_list_index;                    /*0060 0063*/
	u_int32_t	inbound_post_list_index;                    /*0064 0067*/
	u_int32_t	outbound_free_list_index;                   /*0068 006B*/
	u_int32_t	outbound_post_list_index;                   /*006C 006F*/
	u_int32_t	inbound_doorbell_clear;                     /*0070 0073*/
	u_int32_t	i2o_message_unit_control;                   /*0074 0077*/
	u_int32_t	last_used_message_source_address_low;       /*0078 007B*/
	u_int32_t	last_used_message_source_address_high;	    /*007C 007F*/
	u_int32_t	pull_mode_data_byte_count[4];               /*0080 008F*/
	u_int32_t	message_dest_address_index;                 /*0090 0093*/
	u_int32_t	done_queue_not_empty_int_counter_timer;     /*0094 0097*/
	u_int32_t	utility_A_int_counter_timer;                /*0098 009B*/
	u_int32_t	outbound_doorbell;                          /*009C 009F*/
	u_int32_t	outbound_doorbell_clear;                    /*00A0 00A3*/
	u_int32_t	message_source_address_index;               /*00A4 00A7*/
	u_int32_t	message_done_queue_index;                   /*00A8 00AB*/
	u_int32_t	reserved0;                                  /*00AC 00AF*/
	u_int32_t	inbound_msgaddr0;                           /*00B0 00B3 scratchpad0*/
	u_int32_t	inbound_msgaddr1;                           /*00B4 00B7 scratchpad1*/
	u_int32_t	outbound_msgaddr0;                          /*00B8 00BB scratchpad2*/
	u_int32_t	outbound_msgaddr1;                          /*00BC 00BF scratchpad3*/
	u_int32_t	inbound_queueport_low;                      /*00C0 00C3 port64 host inbound queue port low*/
	u_int32_t	inbound_queueport_high;                     /*00C4 00C7 port64 host inbound queue port high*/
	u_int32_t	outbound_queueport_low;                     /*00C8 00CB port64 host outbound queue port low*/
	u_int32_t	outbound_queueport_high;                    /*00CC 00CF port64 host outbound queue port high*/
	u_int32_t	iop_inbound_queue_port_low;                 /*00D0 00D3*/
	u_int32_t	iop_inbound_queue_port_high;                /*00D4 00D7*/
	u_int32_t	iop_outbound_queue_port_low;                /*00D8 00DB*/
	u_int32_t	iop_outbound_queue_port_high;               /*00DC 00DF*/
	u_int32_t	message_dest_queue_port_low;                /*00E0 00E3*/
	u_int32_t	message_dest_queue_port_high;               /*00E4 00E7*/
	u_int32_t	last_used_message_dest_address_low;         /*00E8 00EB*/
	u_int32_t	last_used_message_dest_address_high;        /*00EC 00EF*/
	u_int32_t	message_done_queue_base_address_low;        /*00F0 00F3*/
	u_int32_t	message_done_queue_base_address_high;       /*00F4 00F7*/
	u_int32_t	host_diagnostic;                            /*00F8 00FB*/
	u_int32_t	write_sequence;                             /*00FC 00FF*/
	u_int32_t	reserved1[46];                              /*0100 01B7*/
	u_int32_t	reply_post_producer_index;                  /*01B8 01BB*/
	u_int32_t	reply_post_consumer_index;                  /*01BC 01BF*/
	u_int32_t	reserved2[1936];                            /*01C0 1FFF*/
	u_int32_t	message_wbuffer[32];                        /*2000 207F*/
	u_int32_t	reserved3[32];                              /*2080 20FF*/
	u_int32_t	message_rbuffer[32];                        /*2100 217F*/
	u_int32_t	reserved4[32];                              /*2180 21FF*/
	u_int32_t	msgcode_rwbuffer[256];                      /*2200 23FF*/
};

typedef struct deliver_completeQ {
	u_int16_t	cmdFlag;
	u_int16_t	cmdSMID;
	u_int16_t	cmdLMID;        // reserved (0)
	u_int16_t	cmdFlag2;       // reserved (0)
} DeliverQ, CompletionQ, *pDeliver_Q, *pCompletion_Q;

#define	COMPLETION_Q_POOL_SIZE	(sizeof(struct deliver_completeQ) * 512 + 128)

/*
*********************************************************************
** 
*********************************************************************
*/
struct MessageUnit_UNION
{
	union	{
		struct HBA_MessageUnit		hbamu;
		struct HBB_MessageUnit		hbbmu;
        	struct HBC_MessageUnit		hbcmu;
        	struct HBD_MessageUnit0		hbdmu;
        	struct HBE_MessageUnit		hbemu;
	} muu;
};
/* 
*************************************************************
**   structure for holding DMA address data 
*************************************************************
*/
#define IS_SG64_ADDR	0x01000000 /* bit24 */
/*
************************************************************************************************
**                            ARECA FIRMWARE SPEC
************************************************************************************************
**		Usage of IOP331 adapter
**		(All In/Out is in IOP331's view)
**		1. Message 0 --> InitThread message and retrun code
**		2. Doorbell is used for RS-232 emulation
**			inDoorBell :    bit0 -- data in ready            (DRIVER DATA WRITE OK)
**					bit1 -- data out has been read   (DRIVER DATA READ OK)
**			outDooeBell:    bit0 -- data out ready           (IOP331 DATA WRITE OK)
**					bit1 -- data in has been read    (IOP331 DATA READ OK)
**		3. Index Memory Usage
**			offset 0xf00 : for RS232 out (request buffer)
**			offset 0xe00 : for RS232 in  (scratch buffer)
**			offset 0xa00 : for inbound message code msgcode_rwbuffer (driver send to IOP331)
**			offset 0xa00 : for outbound message code msgcode_rwbuffer (IOP331 send to driver)
**		4. RS-232 emulation
**			Currently 128 byte buffer is used
**			          1st u_int32_t : Data length (1--124)
**			        Byte 4--127 : Max 124 bytes of data
**		5. PostQ
**		All SCSI Command must be sent through postQ:
**		(inbound queue port)	Request frame must be 32 bytes aligned 
**              	#   bit27--bit31 => flag for post ccb 
**			#   bit0--bit26 => real address (bit27--bit31) of post arcmsr_cdb  
**					bit31 : 0 : 256 bytes frame
**						1 : 512 bytes frame
**					bit30 : 0 : normal request
**						1 : BIOS request
**                                      bit29 : reserved
**                                      bit28 : reserved
**                                      bit27 : reserved
**  -------------------------------------------------------------------------------
**		(outbount queue port)	Request reply                          
**              	#   bit27--bit31 => flag for reply
**			#   bit0--bit26 => real address (bit27--bit31) of reply arcmsr_cdb 
**			bit31 : must be 0 (for this type of reply)
**			bit30 : reserved for BIOS handshake
**			bit29 : reserved
**			bit28 : 0 : no error, ignore AdapStatus/DevStatus/SenseData
**				1 : Error, error code in AdapStatus/DevStatus/SenseData
**			bit27 : reserved
**		6. BIOS request
**			All BIOS request is the same with request from PostQ
**			Except :
**				Request frame is sent from configuration space
**					offset: 0x78 : Request Frame (bit30 == 1)
**					offset: 0x18 : writeonly to generate IRQ to IOP331
**				Completion of request:
**				        (bit30 == 0, bit28==err flag)
**		7. Definition of SGL entry (structure)
**		8. Message1 Out - Diag Status Code (????)
**		9. Message0 message code :
**			0x00 : NOP
**			0x01 : Get Config ->offset 0xa00 :for outbound message code msgcode_rwbuffer (IOP331 send to driver)
**					Signature             0x87974060(4)
**					Request len           0x00000200(4)
**					numbers of queue      0x00000100(4)
**					SDRAM Size            0x00000100(4)-->256 MB
**					IDE Channels          0x00000008(4)
**					vendor                40 bytes char
**					model                  8 bytes char
**					FirmVer               16 bytes char
**					Device Map            16 bytes char
**	
**					FirmwareVersion DWORD <== Added for checking of new firmware capability
**			0x02 : Set Config ->offset 0xa00 : for inbound message code msgcode_rwbuffer (driver send to IOP331)
**					Signature             0x87974063(4)
**					UPPER32 of Request Frame  (4)-->Driver Only
**			0x03 : Reset (Abort all queued Command)
**			0x04 : Stop Background Activity
**			0x05 : Flush Cache
**			0x06 : Start Background Activity (re-start if background is halted)
**			0x07 : Check If Host Command Pending (Novell May Need This Function)
**			0x08 : Set controller time ->offset 0xa00 : for inbound message code msgcode_rwbuffer (driver to IOP331)
**					byte 0 : 0xaa <-- signature
**					byte 1 : 0x55 <-- signature
**					byte 2 : year (04)
**					byte 3 : month (1..12)
**					byte 4 : date (1..31)
**					byte 5 : hour (0..23)
**					byte 6 : minute (0..59)
**					byte 7 : second (0..59)
**      *********************************************************************************
**      Porting Of LSI2108/2116 Based PCIE SAS/6G host raid adapter
**      ==> Difference from IOP348
**      <1> Message Register 0,1 (the same usage) Init Thread message and retrun code
**           Inbound Message 0  (inbound_msgaddr0) : at offset 0xB0 (Scratchpad0) for inbound message code msgcode_rwbuffer (driver send to IOP)
**           Inbound Message 1  (inbound_msgaddr1) : at offset 0xB4 (Scratchpad1) Out.... Diag Status Code 
**           Outbound Message 0 (outbound_msgaddr0): at offset 0xB8 (Scratchpad3) Out.... Diag Status Code 
**           Outbound Message 1 (outbound_msgaddr1): at offset 0xBC (Scratchpad2) for outbound message code msgcode_rwbuffer (IOP send to driver)
**           <A> use doorbell to generate interrupt
**
**               inbound doorbell: bit3 --  inbound message 0 ready (driver to iop)
**              outbound doorbell: bit3 -- outbound message 0 ready (iop to driver)
**
**		        a. Message1: Out - Diag Status Code (????)
**
**		        b. Message0: message code 
**		        	    0x00 : NOP
**		        	    0x01 : Get Config ->offset 0xB8 :for outbound message code msgcode_rwbuffer (IOP send to driver)
**		        	    			Signature             0x87974060(4)
**		        	    			Request len           0x00000200(4)
**		        	    			numbers of queue      0x00000100(4)
**		        	    			SDRAM Size            0x00000100(4)-->256 MB
**		        	    			IDE Channels          0x00000008(4)
**		        	    			vendor                40 bytes char
**		        	    			model                  8 bytes char
**		        	    			FirmVer               16 bytes char
**                                         Device Map            16 bytes char
**                                         cfgVersion    ULONG <== Added for checking of new firmware capability
**		        	    0x02 : Set Config ->offset 0xB0 :for inbound message code msgcode_rwbuffer (driver send to IOP)
**		        	    			Signature             0x87974063(4)
**		        	    			UPPER32 of Request Frame  (4)-->Driver Only
**		        	    0x03 : Reset (Abort all queued Command)
**		        	    0x04 : Stop Background Activity
**		        	    0x05 : Flush Cache
**		        	    0x06 : Start Background Activity (re-start if background is halted)
**		        	    0x07 : Check If Host Command Pending (Novell May Need This Function)
**		        	    0x08 : Set controller time ->offset 0xB0 : for inbound message code msgcode_rwbuffer (driver to IOP)
**		        	            		byte 0 : 0xaa <-- signature
**                                      		byte 1 : 0x55 <-- signature
**		        	            		byte 2 : year (04)
**		        	            		byte 3 : month (1..12)
**		        	            		byte 4 : date (1..31)
**		        	            		byte 5 : hour (0..23)
**		        	            		byte 6 : minute (0..59)
**		        	            		byte 7 : second (0..59)
**
**      <2> Doorbell Register is used for RS-232 emulation
**           <A> different clear register
**           <B> different bit0 definition (bit0 is reserved)
**
**           inbound doorbell        : at offset 0x20
**           inbound doorbell clear  : at offset 0x70
**
**           inbound doorbell        : bit0 -- reserved
**                                     bit1 -- data in ready             (DRIVER DATA WRITE OK)
**                                     bit2 -- data out has been read    (DRIVER DATA READ OK)
**                                     bit3 -- inbound message 0 ready
**                                     bit4 -- more than 12 request completed in a time
**
**           outbound doorbell       : at offset 0x9C
**           outbound doorbell clear : at offset 0xA0
**
**           outbound doorbell       : bit0 -- reserved
**                                     bit1 -- data out ready            (IOP DATA WRITE OK)
**                                     bit2 -- data in has been read     (IOP DATA READ OK)
**                                     bit3 -- outbound message 0 ready
**
**      <3> Index Memory Usage (Buffer Area)
**           COMPORT_IN     at  0x2000: message_wbuffer  --  128 bytes (to be sent to ROC) : for RS232 in  (scratch buffer)
**           COMPORT_OUT    at  0x2100: message_rbuffer  --  128 bytes (to be sent to host): for RS232 out (request buffer)
**           BIOS_CFG_AREA  at  0x2200: msgcode_rwbuffer -- 1024 bytes for outbound message code msgcode_rwbuffer (IOP send to driver)
**           BIOS_CFG_AREA  at  0x2200: msgcode_rwbuffer -- 1024 bytes for  inbound message code msgcode_rwbuffer (driver send to IOP)
**
**      <4> PostQ (Command Post Address)
**          All SCSI Command must be sent through postQ:
**              inbound  queue port32 at offset 0x40 , 0x41, 0x42, 0x43
**              inbound  queue port64 at offset 0xC0 (lower)/0xC4 (upper)
**              outbound queue port32 at offset 0x44
**              outbound queue port64 at offset 0xC8 (lower)/0xCC (upper)
**              <A> For 32bit queue, access low part is enough to send/receive request
**                  i.e. write 0x40/0xC0, ROC will get the request with high part == 0, the
**                  same for outbound queue port
**              <B> For 64bit queue, if 64bit instruction is supported, use 64bit instruction
**                  to post inbound request in a single instruction, and use 64bit instruction
**                  to retrieve outbound request in a single instruction.
**                  If in 32bit environment, when sending inbound queue, write high part first
**                  then write low part. For receiving outbound request, read high part first
**                  then low part, to check queue empty, ONLY check high part to be 0xFFFFFFFF.
**                  If high part is 0xFFFFFFFF, DO NOT read low part, this may corrupt the
**                  consistency of the FIFO. Another way to check empty is to check status flag
**                  at 0x30 bit3.
**              <C> Post Address IS NOT shifted (must be 16 bytes aligned)
**                  For   BIOS, 16bytes aligned   is OK
**                  For Driver, 32bytes alignment is recommended.
**                  POST Command bit0 to bit3 is defined differently
**                  ----------------------------
**                  bit0:1 for PULL mode (must be 1)
**                  ----------------------------
**                  bit3/2/1: for arcmsr cdb size (arccdbsize)
**                      000: <= 0x0080 (128)
**                      001: <= 0x0100 (256)
**                      010: <= 0x0180 (384)
**                      011: <= 0x0200 (512)
**                      100: <= 0x0280 (640)
**                      101: <= 0x0300 (768)
**                      110: <= 0x0300 (reserved)
**                      111: <= 0x0300 (reserved)
**                  -----------------------------
**                  if len > 0x300 the len always set as 0x300
**                  -----------------------------   
**                  post addr = addr | ((len-1) >> 6) | 1
**                  -----------------------------
**                  page length in command buffer still required, 
**
**                  if page length > 3, 
**                     firmware will assume more request data need to be retrieved 
**
**              <D> Outbound Posting
**                  bit0:0 , no error, 1 with error, refer to status buffer
**                  bit1:0 , reserved (will be 0)
**                  bit2:0 , reserved (will be 0)
**                  bit3:0 , reserved (will be 0)
**                  bit63-4: Completed command address
**
**              <E> BIOS support, no special support is required. 
**                  LSI2108 support I/O register
**                  All driver functionality is supported through I/O address
**
************************************************************************************************
*/
/*
**********************************
**
**********************************
*/
/* size 8 bytes */
/* 32bit Scatter-Gather list */
struct SG32ENTRY {                 /* length bit 24 == 0 */
	u_int32_t	length;    /* high 8 bit == flag,low 24 bit == length */
	u_int32_t	address;
};
/* size 12 bytes */
/* 64bit Scatter-Gather list */
struct SG64ENTRY {                 /* length bit 24 == 1 */
  	u_int32_t       length;    /* high 8 bit == flag,low 24 bit == length */
   	u_int32_t       address; 
   	u_int32_t       addresshigh;
};
struct SGENTRY_UNION {
	union {
  		struct SG32ENTRY	sg32entry;   /* 30h   Scatter gather address  */
  		struct SG64ENTRY	sg64entry;   /* 30h */
	}u;
};
/*
**********************************
**
**********************************
*/
struct QBUFFER {
	u_int32_t     data_len;
	u_int8_t      data[124];
};
/*
**********************************
*/
typedef struct PHYS_ADDR64 {
	u_int32_t	phyadd_low;
	u_int32_t	phyadd_high;
}PHYSADDR64;
/*
************************************************************************************************
**      FIRMWARE INFO
************************************************************************************************
*/
#define	ARCMSR_FW_MODEL_OFFSET		15
#define	ARCMSR_FW_VERS_OFFSET		17
#define	ARCMSR_FW_DEVMAP_OFFSET		21
#define	ARCMSR_FW_CFGVER_OFFSET		25

struct FIRMWARE_INFO {
	u_int32_t      signature;           /*0,00-03*/
	u_int32_t      request_len;         /*1,04-07*/
	u_int32_t      numbers_queue;       /*2,08-11*/
	u_int32_t      sdram_size;          /*3,12-15*/
	u_int32_t      ide_channels;        /*4,16-19*/
	char           vendor[40];          /*5,20-59*/
	char           model[8];            /*15,60-67*/
	char           firmware_ver[16];    /*17,68-83*/
	char           device_map[16];      /*21,84-99*/
	u_int32_t      cfgVersion;          /*25,100-103 Added for checking of new firmware capability*/
	char           cfgSerial[16];       /*26,104-119*/
	u_int32_t      cfgPicStatus;        /*30,120-123*/
};
/*   (A) For cfgVersion in FIRMWARE_INFO
**        if low BYTE (byte#0) >= 3 (version 3)
**        then byte#1 report the capability of the firmware can xfer in a single request
**        
**        byte#1
**        0         256K
**        1         512K
**        2         1M
**        3         2M
**        4         4M
**        5         8M
**        6         16M
**    (B) Byte offset 7 (Reserved1) of CDB is changed to msgPages
**        Driver support new xfer method need to set this field to indicate
**        large CDB block in 0x100 unit (we use 0x100 byte as one page)
**        e.g. If the length of CDB including MSG header and SGL is 0x1508
**        driver need to set the msgPages to 0x16
**    (C) REQ_LEN_512BYTE must be used also to indicate SRB length
**        e.g. CDB len      msgPages    REQ_LEN_512BYTE flag
**             <= 0x100     1               0
**             <= 0x200     2               1
**             <= 0x300     3               1
**             <= 0x400     4               1
**             .
**             .
*/

/*
************************************************************************************************
**    size 0x1F8 (504)
************************************************************************************************
*/
struct ARCMSR_CDB {
	u_int8_t     	Bus;              /* 00h   should be 0            */
	u_int8_t     	TargetID;         /* 01h   should be 0--15        */
	u_int8_t     	LUN;              /* 02h   should be 0--7         */
	u_int8_t     	Function;         /* 03h   should be 1            */
	
	u_int8_t     	CdbLength;        /* 04h   not used now           */
	u_int8_t     	sgcount;          /* 05h                          */
	u_int8_t     	Flags;            /* 06h                          */
	u_int8_t     	msgPages;         /* 07h                          */
	
	u_int32_t    	Context;          /* 08h   Address of this request */
	u_int32_t    	DataLength;       /* 0ch   not used now           */
	
	u_int8_t     	Cdb[16];          /* 10h   SCSI CDB               */
	/*
	********************************************************
	** Device Status : the same from SCSI bus if error occur 
	** SCSI bus status codes.
	********************************************************
	*/
	u_int8_t     	DeviceStatus;     /* 20h   if error                */
	
	u_int8_t     	SenseData[15];    /* 21h   output                  */        
	
	union {
		struct SG32ENTRY	sg32entry[ARCMSR_MAX_SG_ENTRIES];        /* 30h   Scatter gather address  */
		struct SG64ENTRY	sg64entry[ARCMSR_MAX_SG_ENTRIES];        /* 30h                           */
	} u;
};
/* CDB flag */
#define ARCMSR_CDB_FLAG_SGL_BSIZE		0x01	/* bit 0: 0(256) / 1(512) bytes         */
#define ARCMSR_CDB_FLAG_BIOS			0x02	/* bit 1: 0(from driver) / 1(from BIOS) */
#define ARCMSR_CDB_FLAG_WRITE			0x04	/* bit 2: 0(Data in) / 1(Data out)      */
#define ARCMSR_CDB_FLAG_SIMPLEQ			0x00	/* bit 4/3 ,00 : simple Q,01 : head of Q,10 : ordered Q */
#define ARCMSR_CDB_FLAG_HEADQ			0x08
#define ARCMSR_CDB_FLAG_ORDEREDQ		0x10
/* scsi status */
#define SCSISTAT_GOOD                  		0x00
#define SCSISTAT_CHECK_CONDITION       		0x02
#define SCSISTAT_CONDITION_MET         		0x04
#define SCSISTAT_BUSY                  		0x08
#define SCSISTAT_INTERMEDIATE          		0x10
#define SCSISTAT_INTERMEDIATE_COND_MET 		0x14
#define SCSISTAT_RESERVATION_CONFLICT  		0x18
#define SCSISTAT_COMMAND_TERMINATED    		0x22
#define SCSISTAT_QUEUE_FULL            		0x28
/* DeviceStatus */
#define ARCMSR_DEV_SELECT_TIMEOUT		0xF0
#define ARCMSR_DEV_ABORTED			0xF1
#define ARCMSR_DEV_INIT_FAIL			0xF2
/*
*********************************************************************
**                   Command Control Block (SrbExtension)
** SRB must be not cross page boundary,and the order from offset 0
**         structure describing an ATA disk request
**             this SRB length must be 32 bytes boundary
*********************************************************************
*/
struct CommandControlBlock {
	struct ARCMSR_CDB	arcmsr_cdb;		/* 0  -503 (size of CDB=504): arcmsr messenger scsi command descriptor size 504 bytes */
	u_int32_t		cdb_phyaddr_low;	/* 504-507 */
	u_int32_t		arc_cdb_size;		/* 508-511 */
	/*  ======================512+32 bytes============================  */
	union ccb		*pccb;			/* 512-515 516-519 pointer of freebsd scsi command */
	struct AdapterControlBlock	*acb;		/* 520-523 524-527 */
	bus_dmamap_t		dm_segs_dmamap;		/* 528-531 532-535 */
	u_int16_t   		srb_flags;		/* 536-537 */
	u_int16_t		srb_state;              /* 538-539 */
	u_int32_t		cdb_phyaddr_high;	/* 540-543 */
	struct	callout		ccb_callout;
	u_int32_t		smid;
    /*  ==========================================================  */
};
/*	srb_flags */
#define		SRB_FLAG_READ			0x0000
#define		SRB_FLAG_WRITE			0x0001
#define		SRB_FLAG_ERROR			0x0002
#define		SRB_FLAG_FLUSHCACHE		0x0004
#define		SRB_FLAG_MASTER_ABORTED 	0x0008
#define		SRB_FLAG_DMAVALID		0x0010
#define		SRB_FLAG_DMACONSISTENT  	0x0020
#define		SRB_FLAG_DMAWRITE		0x0040
#define		SRB_FLAG_PKTBIND		0x0080
#define		SRB_FLAG_TIMER_START		0x0080
/*	srb_state */
#define		ARCMSR_SRB_DONE   		0x0000
#define		ARCMSR_SRB_UNBUILD 		0x0000
#define		ARCMSR_SRB_TIMEOUT 		0x1111
#define		ARCMSR_SRB_RETRY 		0x2222
#define		ARCMSR_SRB_START   		0x55AA
#define		ARCMSR_SRB_PENDING		0xAA55
#define		ARCMSR_SRB_RESET		0xA5A5
#define		ARCMSR_SRB_ABORTED		0x5A5A
#define		ARCMSR_SRB_ILLEGAL		0xFFFF

#define		SRB_SIZE	((sizeof(struct CommandControlBlock)+0x1f) & 0xffe0)
#define 	ARCMSR_SRBS_POOL_SIZE   (SRB_SIZE * ARCMSR_MAX_FREESRB_NUM)

/*
*********************************************************************
**                 Adapter Control Block
*********************************************************************
*/
#define ACB_ADAPTER_TYPE_A	0x00000000	/* hba I IOP */
#define ACB_ADAPTER_TYPE_B	0x00000001	/* hbb M IOP */
#define ACB_ADAPTER_TYPE_C	0x00000002	/* hbc L IOP */
#define ACB_ADAPTER_TYPE_D	0x00000003	/* hbd M IOP */
#define ACB_ADAPTER_TYPE_E	0x00000004	/* hbd L IOP */

struct AdapterControlBlock {
	u_int32_t		adapter_type;		/* adapter A,B..... */
	
	bus_space_tag_t		btag[2];
	bus_space_handle_t	bhandle[2];
	bus_dma_tag_t		parent_dmat;
	bus_dma_tag_t		dm_segs_dmat;		/* dmat for buffer I/O */  
	bus_dma_tag_t		srb_dmat;		/* dmat for freesrb */
	bus_dmamap_t		srb_dmamap;
	device_t		pci_dev;
#if __FreeBSD_version < 503000
	dev_t			ioctl_dev;
#else
	struct cdev		*ioctl_dev;
#endif
	int			pci_unit;
	
	struct resource		*sys_res_arcmsr[2];
	struct resource		*irqres[ARCMSR_NUM_MSIX_VECTORS];
	void			*ih[ARCMSR_NUM_MSIX_VECTORS]; /* interrupt handle */
	int			irq_id[ARCMSR_NUM_MSIX_VECTORS];
	
	/* Hooks into the CAM XPT */
	struct			cam_sim *psim;
	struct			cam_path *ppath;
	u_int8_t		*uncacheptr;
	unsigned long		vir2phy_offset;
	union	{
		unsigned long	phyaddr;
		struct {
			u_int32_t	phyadd_low;
			u_int32_t	phyadd_high;
		}B;
	}srb_phyaddr;
//	unsigned long				srb_phyaddr;
	/* Offset is used in making arc cdb physical to virtual calculations */
	u_int32_t		outbound_int_enable;
	
	struct MessageUnit_UNION	*pmu;		/* message unit ATU inbound base address0 */
	
	u_int8_t		adapter_index;
	u_int8_t		irq;
	u_int16_t		acb_flags;
	
	struct CommandControlBlock *psrb_pool[ARCMSR_MAX_FREESRB_NUM];     /* serial srb pointer array */
	struct CommandControlBlock *srbworkingQ[ARCMSR_MAX_FREESRB_NUM];   /* working srb pointer array */
	int32_t			workingsrb_doneindex;		/* done srb array index */
	int32_t			workingsrb_startindex;		/* start srb array index  */
	int32_t			srboutstandingcount;
	
	u_int8_t		rqbuffer[ARCMSR_MAX_QBUFFER];	/* data collection buffer for read from 80331 */
	u_int32_t		rqbuf_firstindex;		/* first of read buffer  */
	u_int32_t		rqbuf_lastindex;		/* last of read buffer   */
	
	u_int8_t		wqbuffer[ARCMSR_MAX_QBUFFER];	/* data collection buffer for write to 80331  */
	u_int32_t		wqbuf_firstindex;		/* first of write buffer */
	u_int32_t		wqbuf_lastindex;		/* last of write buffer  */
	
	arcmsr_lock_t		isr_lock;
	arcmsr_lock_t		srb_lock;
	arcmsr_lock_t		postDone_lock;
	arcmsr_lock_t		qbuffer_lock;
	
	u_int8_t		devstate[ARCMSR_MAX_TARGETID][ARCMSR_MAX_TARGETLUN]; /* id0 ..... id15,lun0...lun7 */
	u_int32_t		num_resets;
	u_int32_t		num_aborts;
	u_int32_t		firm_request_len;	/*1,04-07*/
	u_int32_t		firm_numbers_queue;	/*2,08-11*/
	u_int32_t		firm_sdram_size;	/*3,12-15*/
	u_int32_t		firm_ide_channels;	/*4,16-19*/
	u_int32_t		firm_cfg_version;
	char			firm_model[12];		/*15,60-67*/
	char			firm_version[20];	/*17,68-83*/
	char			device_map[20];		/*21,84-99 */
	struct	callout		devmap_callout;
	u_int32_t		pktRequestCount;
	u_int32_t		pktReturnCount;
	u_int32_t		vendor_device_id;
	u_int32_t		adapter_bus_speed;
	u_int32_t		maxOutstanding;
	u_int16_t		sub_device_id;
	u_int32_t		doneq_index;
	u_int32_t		in_doorbell;
	u_int32_t		out_doorbell;
	u_int32_t		completionQ_entry;
	pCompletion_Q		pCompletionQ;
	int			msix_vectors;
	int			rid[2];
};/* HW_DEVICE_EXTENSION */
/* acb_flags */
#define ACB_F_SCSISTOPADAPTER           0x0001
#define ACB_F_MSG_STOP_BGRB             0x0002		/* stop RAID background rebuild */
#define ACB_F_MSG_START_BGRB            0x0004		/* stop RAID background rebuild */
#define ACB_F_IOPDATA_OVERFLOW          0x0008		/* iop ioctl data rqbuffer overflow */
#define ACB_F_MESSAGE_WQBUFFER_CLEARED  0x0010		/* ioctl clear wqbuffer */
#define ACB_F_MESSAGE_RQBUFFER_CLEARED  0x0020		/* ioctl clear rqbuffer */
#define ACB_F_MESSAGE_WQBUFFER_READ     0x0040
#define ACB_F_BUS_RESET                 0x0080
#define ACB_F_IOP_INITED                0x0100		/* iop init */
#define ACB_F_MAPFREESRB_FAILD		0x0200		/* arcmsr_map_freesrb faild */
#define ACB_F_CAM_DEV_QFRZN             0x0400
#define ACB_F_BUS_HANG_ON               0x0800		/* need hardware reset bus */
#define ACB_F_SRB_FUNCTION_POWER        0x1000
#define	ACB_F_MSIX_ENABLED		0x2000
/* devstate */
#define ARECA_RAID_GONE         	0x55
#define ARECA_RAID_GOOD         	0xaa
/* adapter_bus_speed */
#define	ACB_BUS_SPEED_3G	0
#define	ACB_BUS_SPEED_6G	1
#define	ACB_BUS_SPEED_12G	2
/*
*************************************************************
*************************************************************
*/
struct SENSE_DATA {
    u_int8_t 	ErrorCode:7;
    u_int8_t 	Valid:1;
    u_int8_t 	SegmentNumber;
    u_int8_t 	SenseKey:4;
    u_int8_t 	Reserved:1;
    u_int8_t 	IncorrectLength:1;
    u_int8_t 	EndOfMedia:1;
    u_int8_t 	FileMark:1;
    u_int8_t 	Information[4];
    u_int8_t 	AdditionalSenseLength;
    u_int8_t 	CommandSpecificInformation[4];
    u_int8_t 	AdditionalSenseCode;
    u_int8_t 	AdditionalSenseCodeQualifier;
    u_int8_t 	FieldReplaceableUnitCode;
    u_int8_t 	SenseKeySpecific[3];
};
/* 
**********************************
**  Peripheral Device Type definitions 
**********************************
*/
#define SCSI_DASD		0x00	   /* Direct-access Device	   */
#define SCSI_SEQACESS		0x01	   /* Sequential-access device     */
#define SCSI_PRINTER		0x02	   /* Printer device		   */
#define SCSI_PROCESSOR		0x03	   /* Processor device		   */
#define SCSI_WRITEONCE		0x04	   /* Write-once device 	   */
#define SCSI_CDROM		0x05	   /* CD-ROM device		   */
#define SCSI_SCANNER		0x06	   /* Scanner device		   */
#define SCSI_OPTICAL		0x07	   /* Optical memory device	   */
#define SCSI_MEDCHGR		0x08	   /* Medium changer device	   */
#define SCSI_COMM		0x09	   /* Communications device	   */
#define SCSI_NODEV		0x1F	   /* Unknown or no device type    */
/*
************************************************************************************************************
**				         @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
**				                          80331 PCI-to-PCI Bridge
**				                          PCI Configuration Space 
**				
**				         @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
**				                            Programming Interface
**				                          ========================
**				            Configuration Register Address Space Groupings and Ranges
**				         =============================================================
**				                 Register Group                      Configuration  Offset
**				         -------------------------------------------------------------
**				            Standard PCI Configuration                      00-3Fh
**				         -------------------------------------------------------------
**				             Device Specific Registers                      40-A7h
**				         -------------------------------------------------------------
**				                   Reserved                                 A8-CBh
**				         -------------------------------------------------------------
**				              Enhanced Capability List                      CC-FFh
** ==========================================================================================================
**                         Standard PCI [Type 1] Configuration Space Address Map
** **********************************************************************************************************
** |    Byte 3              |         Byte 2         |        Byte 1          |       Byte 0              |   Configu-ration Byte Offset
** ----------------------------------------------------------------------------------------------------------
** |                    Device ID                    |                     Vendor ID                      | 00h
** ----------------------------------------------------------------------------------------------------------
** |                 Primary Status                  |                  Primary Command                   | 04h
** ----------------------------------------------------------------------------------------------------------
** |                   Class Code                                             |        RevID              | 08h
** ----------------------------------------------------------------------------------------------------------
** |        reserved        |      Header Type       |      Primary MLT       |      Primary CLS          | 0Ch
** ----------------------------------------------------------------------------------------------------------
** |                                             Reserved                                                 | 10h
** ----------------------------------------------------------------------------------------------------------
** |                                             Reserved                                                 | 14h
** ----------------------------------------------------------------------------------------------------------
** |     Secondary MLT      | Subordinate Bus Number |  Secondary Bus Number  |     Primary Bus Number    | 18h
** ----------------------------------------------------------------------------------------------------------
** |                 Secondary Status                |       I/O Limit        |        I/O Base           | 1Ch
** ----------------------------------------------------------------------------------------------------------
** |      Non-prefetchable Memory Limit Address      |       Non-prefetchable Memory Base Address         | 20h
** ----------------------------------------------------------------------------------------------------------
** |        Prefetchable Memory Limit Address        |           Prefetchable Memory Base Address         | 24h
** ----------------------------------------------------------------------------------------------------------
** |                          Prefetchable Memory Base Address Upper 32 Bits                              | 28h
** ----------------------------------------------------------------------------------------------------------
** |                          Prefetchable Memory Limit Address Upper 32 Bits                             | 2Ch
** ----------------------------------------------------------------------------------------------------------
** |             I/O Limit Upper 16 Bits             |                 I/O Base Upper 16                  | 30h
** ----------------------------------------------------------------------------------------------------------
** |                                Reserved                                  |   Capabilities Pointer    | 34h
** ----------------------------------------------------------------------------------------------------------
** |                                             Reserved                                                 | 38h
** ----------------------------------------------------------------------------------------------------------
** |                   Bridge Control                |  Primary Interrupt Pin | Primary Interrupt Line    | 3Ch
**=============================================================================================================
*/
/*
**=============================================================================================================
**  0x03-0x00 : 
** Bit       Default             Description
**31:16       0335h            Device ID (DID): Indicates the unique device ID that is assigned to bridge by the PCI SIG.
**                             ID is unique per product speed as indicated.
**15:00       8086h            Vendor ID (VID): 16-bit field which indicates that Intel is the vendor.
**=============================================================================================================
*/
#define     ARCMSR_PCI2PCI_VENDORID_REG		         0x00    /*word*/
#define     ARCMSR_PCI2PCI_DEVICEID_REG		         0x02    /*word*/
/*
**==============================================================================
**  0x05-0x04 : command register 
** Bit       Default 		               Description
**15:11        00h		   		             Reserved
** 10          0		   		           Interrupt Disable: Disables/Enables the generation of Interrupts on the primary bus. 
**                		   		                              The bridge does not support interrupts.
** 09          0		   		                 FB2B Enable: Enables/Disables the generation of fast back to back 
**										transactions on the primary bus. 
**                		   		                              The bridge does not generate fast back to back 
**										transactions on the primary bus.
** 08          0		   		          SERR# Enable (SEE): Enables primary bus SERR# assertions.
**                		   		                              0=The bridge does not assert P_SERR#.
**                		   		                              1=The bridge may assert P_SERR#, subject to other programmable criteria.
** 07          0		   		    Wait Cycle Control (WCC): Always returns 0bzero indicating 
**										that bridge does not perform address or data stepping,
** 06          0		   		 Parity Error Response (PER): Controls bridge response to a detected primary bus parity error.
**                		   		                              0=When a data parity error is detected bridge does not assert S_PERR#. 
**                		   		                                  Also bridge does not assert P_SERR# in response to 
**											a detected address or attribute parity error.
**                		   		                              1=When a data parity error is detected bridge asserts S_PERR#. 
**                		   		                                  The bridge also asserts P_SERR# 
**											(when enabled globally via bit(8) of this register) 
**											in response to a detected address or attribute parity error.
** 05          0		  VGA Palette Snoop Enable (VGA_PSE): Controls bridge response to VGA-compatible palette write transactions. 
**                		                                      VGA palette write transactions are I/O transactions
**										 whose address bits are: P_AD[9:0] equal to 3C6h, 3C8h or 3C9h
**                		                                      P_AD[15:10] are not decoded (i.e. aliases are claimed), 
**										or are fully decoding 
**										(i.e., must be all 0's depending upon the VGA 
**										aliasing bit in the Bridge Control Register, offset 3Eh.
**                		                                      P_AD[31:16] equal to 0000h
**                		                                      0=The bridge ignores VGA palette write transactions, 
**										unless decoded by the standard I/O address range window.
**                		                                      1=The bridge responds to VGA palette write transactions 
**										with medium DEVSEL# timing and forwards them to the secondary bus.
** 04          0   Memory Write and Invalidate Enable (MWIE): The bridge does not promote MW transactions to MWI transactions. 
**                                                            MWI transactions targeting resources on the opposite side of the bridge, 
**										however, are forwarded as MWI transactions.
** 03          0                  Special Cycle Enable (SCE): The bridge ignores special cycle transactions. 
**                                                            This bit is read only and always returns 0 when read
** 02          0                     Bus Master Enable (BME): Enables bridge to initiate memory and I/O transactions on the primary interface.
**                                                            Initiation of configuration transactions is not affected by the state of this bit.
**                                                            0=The bridge does not initiate memory or I/O transactions on the primary interface.
**                                                            1=The bridge is enabled to function as an initiator on the primary interface.
** 01          0                   Memory Space Enable (MSE): Controls target response to memory transactions on the primary interface.
**                                                            0=The bridge target response to memory transactions on the primary interface is disabled.
**                                                            1=The bridge target response to memory transactions on the primary interface is enabled.
** 00          0                     I/O Space Enable (IOSE): Controls target response to I/O transactions on the primary interface.
**                                                            0=The bridge target response to I/O transactions on the primary interface is disabled.
**                                                            1=The bridge target response to I/O transactions on the primary interface is enabled.
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_PRIMARY_COMMAND_REG		0x04    /*word*/
#define     PCI_DISABLE_INTERRUPT					0x0400
/*
**==============================================================================
**  0x07-0x06 : status register 
** Bit       Default                       Description
** 15          0                       Detected Parity Error: The bridge sets this bit to a 1b whenever it detects an address, 
**									attribute or data parity error. 
**                                                            This bit is set regardless of the state of the PER bit in the command register.
** 14          0                       Signaled System Error: The bridge sets this bit to a 1b whenever it asserts SERR# on the primary bus.
** 13          0                       Received Master Abort: The bridge sets this bit to a 1b when, 
**									acting as the initiator on the primary bus, 
**									its transaction (with the exception of special cycles) 
**									has been terminated with a Master Abort.
** 12          0                       Received Target Abort: The bridge sets this bit to a 1b when, 
**									acting as the initiator on the primary bus, 
**									its transaction has been terminated with a Target Abort.
** 11          0                       Signaled Target Abort: The bridge sets this bit to a 1b when it, 
**									as the target of a transaction, terminates it with a Target Abort. 
**                                                            In PCI-X mode this bit is also set when it forwards a SCM with a target abort error code.
** 10:09       01                             DEVSEL# Timing: Indicates slowest response to a non-configuration command on the primary interface. 
**                                                            Returns ¡§01b¡¨ when read, indicating that bridge responds no slower than with medium timing.
** 08          0                    Master Data Parity Error: The bridge sets this bit to a 1b when all of the following conditions are true: 
**									The bridge is the current master on the primary bus
**                                                            S_PERR# is detected asserted or is asserted by bridge
**                                                            The Parity Error Response bit is set in the Command register
** 07          1                   Fast Back to Back Capable: Returns a 1b when read indicating that bridge 
**									is able to respond to fast back to back transactions on its primary interface.
** 06          0                             Reserved
** 05          1                   66 MHz Capable Indication: Returns a 1b when read indicating that bridge primary interface is 66 MHz capable.
**                                                            1 =
** 04          1                    Capabilities List Enable: Returns 1b when read indicating that bridge supports PCI standard enhanced capabilities. 
**                                                            Offset 34h (Capability Pointer register) 
**										provides the offset for the first entry 
**										in the linked list of enhanced capabilities.
** 03          0                            Interrupt Status: Reflects the state of the interrupt in the device/function.
**                                                            The bridge does not support interrupts.
** 02:00       000                           Reserved
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_PRIMARY_STATUS_REG	     0x06    /*word: 06,07 */
#define          ARCMSR_ADAP_66MHZ                   0x20
/*
**==============================================================================
**  0x08 : revision ID 
** Bit       Default                       Description
** 07:00       00000000                  Revision ID (RID): '00h' indicating bridge A-0 stepping.
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_REVISIONID_REG		     0x08    /*byte*/
/*
**==============================================================================
**  0x0b-0x09 : 0180_00 (class code 1,native pci mode ) 
** Bit       Default                       Description
** 23:16       06h                     Base Class Code (BCC): Indicates that this is a bridge device.
** 15:08       04h                      Sub Class Code (SCC): Indicates this is of type PCI-to-PCI bridge.
** 07:00       00h               Programming Interface (PIF): Indicates that this is standard (non-subtractive) PCI-PCI bridge.
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_CLASSCODE_REG	         0x09    /*3bytes*/
/*
**==============================================================================
**  0x0c : cache line size 
** Bit       Default                       Description
** 07:00       00h                     Cache Line Size (CLS): Designates the cache line size in 32-bit dword units.
**                                                            The contents of this register are factored into 
**									internal policy decisions associated with memory read prefetching, 
**									and the promotion of Memory Write transactions to MWI transactions.
**                                                            Valid cache line sizes are 8 and 16 dwords. 
**                                                            When the cache line size is set to an invalid value, 
**									bridge behaves as though the cache line size was set to 00h.
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_PRIMARY_CACHELINESIZE_REG 0x0C    /*byte*/
/*
**==============================================================================
**  0x0d : latency timer (number of pci clock 00-ff ) 
** Bit       Default                       Description
**                                   Primary Latency Timer (PTV):
** 07:00      00h (Conventional PCI)   Conventional PCI Mode: Primary bus Master latency timer. Indicates the number of PCI clock cycles,
**                                                            referenced from the assertion of FRAME# to the expiration of the timer, 
**                                                            when bridge may continue as master of the current transaction. All bits are writable, 
**                                                            resulting in a granularity of 1 PCI clock cycle. 
**                                                            When the timer expires (i.e., equals 00h) 
**									bridge relinquishes the bus after the first data transfer 
**									when its PCI bus grant has been deasserted.
**         or 40h (PCI-X)                         PCI-X Mode: Primary bus Master latency timer. 
**                                                            Indicates the number of PCI clock cycles,
**                                                            referenced from the assertion of FRAME# to the expiration of the timer, 
**                                                            when bridge may continue as master of the current transaction. 
**                                                            All bits are writable, resulting in a granularity of 1 PCI clock cycle. 
**                                                            When the timer expires (i.e., equals 00h) bridge relinquishes the bus at the next ADB. 
**                                                            (Except in the case where MLT expires within 3 data phases 
**								of an ADB.In this case bridge continues on 
**								until it reaches the next ADB before relinquishing the bus.)
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_PRIMARY_LATENCYTIMER_REG	 0x0D    /*byte*/
/*
**==============================================================================
**  0x0e : (header type,single function ) 
** Bit       Default                       Description
** 07           0                Multi-function device (MVD): 80331 is a single-function device.
** 06:00       01h                       Header Type (HTYPE): Defines the layout of addresses 10h through 3Fh in configuration space. 
**                                                            Returns ¡§01h¡¨ when read indicating 
**								that the register layout conforms to the standard PCI-to-PCI bridge layout.
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_HEADERTYPE_REG	         0x0E    /*byte*/
/*
**==============================================================================
**     0x0f   : 
**==============================================================================
*/
/*
**==============================================================================
**  0x13-0x10 : 
**  PCI CFG Base Address #0 (0x10) 
**==============================================================================
*/
/*
**==============================================================================
**  0x17-0x14 : 
**  PCI CFG Base Address #1 (0x14) 
**==============================================================================
*/
/*
**==============================================================================
**  0x1b-0x18 : 
**  PCI CFG Base Address #2 (0x18) 
**-----------------0x1A,0x19,0x18--Bus Number Register - BNR
** Bit       Default                       Description
** 23:16       00h             Subordinate Bus Number (SBBN): Indicates the highest PCI bus number below this bridge. 
**                                                            Any Type 1 configuration cycle 
**									on the primary bus whose bus number is greater than the secondary bus number,
**                                                            and less than or equal to the subordinate bus number 
**									is forwarded unaltered as a Type 1 configuration cycle on the secondary PCI bus.
** 15:08       00h               Secondary Bus Number (SCBN): Indicates the bus number of PCI to which the secondary interface is connected. 
**                                                            Any Type 1 configuration cycle matching this bus number 
**									is translated to a Type 0 configuration cycle (or a Special Cycle) 
**									before being executed on bridge's secondary PCI bus.
** 07:00       00h                  Primary Bus Number (PBN): Indicates bridge primary bus number. 
**                                                            Any Type 1 configuration cycle on the primary interface 
**									with a bus number that is less than the contents 
**									of this register field does not be claimed by bridge.
**-----------------0x1B--Secondary Latency Timer Register - SLTR
** Bit       Default                       Description
**                             Secondary Latency Timer (STV):
** 07:00       00h (Conventional PCI)  Conventional PCI Mode: Secondary bus Master latency timer. 
**                                                            Indicates the number of PCI clock cycles,
**									referenced from the assertion of FRAME# to the expiration of the timer, 
**                                                            when bridge may continue as master of the current transaction. All bits are writable, 
**                                                            resulting in a granularity of 1 PCI clock cycle.
**                                                            When the timer expires (i.e., equals 00h) 
**								bridge relinquishes the bus after the first data transfer 
**								when its PCI bus grant has been deasserted.
**          or 40h (PCI-X)                        PCI-X Mode: Secondary bus Master latency timer. 
**                                                            Indicates the number of PCI clock cycles,referenced from the assertion of FRAME# 
**								to the expiration of the timer, 
**                                                            when bridge may continue as master of the current transaction. All bits are writable, 
**                                                            resulting in a granularity of 1 PCI clock cycle.
**                                                            When the timer expires (i.e., equals 00h) bridge relinquishes the bus at the next ADB. 
**                                                            (Except in the case where MLT expires within 3 data phases of an ADB. 
**								In this case bridge continues on until it reaches the next ADB 
**								before relinquishing the bus)
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_PRIMARY_BUSNUMBER_REG	         0x18    /*3byte 0x1A,0x19,0x18*/
#define     ARCMSR_PCI2PCI_SECONDARY_BUSNUMBER_REG	         0x19    /*byte*/
#define     ARCMSR_PCI2PCI_SUBORDINATE_BUSNUMBER_REG             0x1A    /*byte*/
#define     ARCMSR_PCI2PCI_SECONDARY_LATENCYTIMER_REG	         0x1B    /*byte*/
/*
**==============================================================================
**  0x1f-0x1c : 
**  PCI CFG Base Address #3 (0x1C) 
**-----------------0x1D,0x1C--I/O Base and Limit Register - IOBL
** Bit       Default                       Description
** 15:12        0h            I/O Limit Address Bits [15:12]: Defines the top address of an address range to 
**								determine when to forward I/O transactions from one interface to the other. 
**                                                            These bits correspond to address lines 15:12 for 4KB alignment. 
**                                                            Bits 11:0 are assumed to be FFFh.
** 11:08        1h           I/O Limit Addressing Capability: This field is hard-wired to 1h, indicating support 32-bit I/O addressing.
** 07:04        0h             I/O Base Address Bits [15:12]: Defines the bottom address of 
**								an address range to determine when to forward I/O transactions 
**								from one interface to the other. 
**                                                            These bits correspond to address lines 15:12 for 4KB alignment. 
**								Bits 11:0 are assumed to be 000h.
** 03:00        1h            I/O Base Addressing Capability: This is hard-wired to 1h, indicating support for 32-bit I/O addressing.
**-----------------0x1F,0x1E--Secondary Status Register - SSR
** Bit       Default                       Description
** 15           0b                     Detected Parity Error: The bridge sets this bit to a 1b whenever it detects an address, 
**								attribute or data parity error on its secondary interface.
** 14           0b                     Received System Error: The bridge sets this bit when it samples SERR# asserted on its secondary bus interface.
** 13           0b                     Received Master Abort: The bridge sets this bit to a 1b when, 
**								acting as the initiator on the secondary bus, 
**								it's transaction (with the exception of special cycles) 
**								has been terminated with a Master Abort.
** 12           0b                     Received Target Abort: The bridge sets this bit to a 1b when, 
**								acting as the initiator on the secondary bus, 
**								it's transaction has been terminated with a Target Abort.
** 11           0b                     Signaled Target Abort: The bridge sets this bit to a 1b when it, 
**								as the target of a transaction, terminates it with a Target Abort. 
**                                                            In PCI-X mode this bit is also set when it forwards a SCM with a target abort error code.
** 10:09       01b                            DEVSEL# Timing: Indicates slowest response to a non-configuration command on the secondary interface. 
**                                                            Returns ¡§01b¡¨ when read, indicating that bridge responds no slower than with medium timing.
** 08           0b                  Master Data Parity Error: The bridge sets this bit to a 1b when all of the following conditions are true:
**                                                            The bridge is the current master on the secondary bus
**                                                            S_PERR# is detected asserted or is asserted by bridge
**                                                            The Parity Error Response bit is set in the Command register 
** 07           1b           Fast Back-to-Back Capable (FBC): Indicates that the secondary interface of bridge can receive fast back-to-back cycles.
** 06           0b                           Reserved
** 05           1b                      66 MHz Capable (C66): Indicates the secondary interface of the bridge is 66 MHz capable.
**                                                            1 =
** 04:00       00h                           Reserved
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_IO_BASE_REG	                     0x1C    /*byte*/
#define     ARCMSR_PCI2PCI_IO_LIMIT_REG	                     0x1D    /*byte*/
#define     ARCMSR_PCI2PCI_SECONDARY_STATUS_REG	             0x1E    /*word: 0x1F,0x1E */
/*
**==============================================================================
**  0x23-0x20 : 
**  PCI CFG Base Address #4 (0x20)
**-----------------0x23,0x22,0x21,0x20--Memory Base and Limit Register - MBL
** Bit       Default                       Description
** 31:20      000h                              Memory Limit: These 12 bits are compared with P_AD[31:20] of the incoming address to determine
**                                                            the upper 1MB aligned value (exclusive) of the range. 
**                                                            The incoming address must be less than or equal to this value. 
**                                                            For the purposes of address decoding the lower 20 address bits (P_AD[19:0] 
**									are assumed to be F FFFFh.
** 19:16        0h                            Reserved.
** 15:04      000h                               Memory Base: These 12 bits are compared with bits P_AD[31:20] 
**								of the incoming address to determine the lower 1MB 
**								aligned value (inclusive) of the range. 
**                                                            The incoming address must be greater than or equal to this value.
**                                                            For the purposes of address decoding the lower 20 address bits (P_AD[19:0]) 
**								are assumed to be 0 0000h.
** 03:00        0h                            Reserved.
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_NONPREFETCHABLE_MEMORY_BASE_REG   0x20    /*word: 0x21,0x20 */
#define     ARCMSR_PCI2PCI_NONPREFETCHABLE_MEMORY_LIMIT_REG  0x22    /*word: 0x23,0x22 */
/*
**==============================================================================
**  0x27-0x24 : 
**  PCI CFG Base Address #5 (0x24) 
**-----------------0x27,0x26,0x25,0x24--Prefetchable Memory Base and Limit Register - PMBL
** Bit       Default                       Description
** 31:20      000h                 Prefetchable Memory Limit: These 12 bits are compared with P_AD[31:20] of the incoming address to determine
**                                                            the upper 1MB aligned value (exclusive) of the range. 
**                                                            The incoming address must be less than or equal to this value. 
**                                                            For the purposes of address decoding the lower 20 address bits (P_AD[19:0] 
**									are assumed to be F FFFFh.
** 19:16        1h                          64-bit Indicator: Indicates that 64-bit addressing is supported.
** 15:04      000h                  Prefetchable Memory Base: These 12 bits are compared with bits P_AD[31:20] 
**								of the incoming address to determine the lower 1MB aligned value (inclusive) 
**								of the range. 
**                                                            The incoming address must be greater than or equal to this value. 
**                                                            For the purposes of address decoding the lower 20 address bits (P_AD[19:0])
**								 are assumed to be 0 0000h.
** 03:00        1h                          64-bit Indicator: Indicates that 64-bit addressing is supported.
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_PREFETCHABLE_MEMORY_BASE_REG      0x24    /*word: 0x25,0x24 */
#define     ARCMSR_PCI2PCI_PREFETCHABLE_MEMORY_LIMIT_REG     0x26    /*word: 0x27,0x26 */
/*
**==============================================================================
**  0x2b-0x28 : 
** Bit       Default                       Description
** 31:00    00000000h Prefetchable Memory Base Upper Portion: All bits are read/writable  
**                                                            bridge supports full 64-bit addressing.
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_PREFETCHABLE_MEMORY_BASE_UPPER32_REG     0x28    /*dword: 0x2b,0x2a,0x29,0x28 */
/*
**==============================================================================
**  0x2f-0x2c : 
** Bit       Default                       Description
** 31:00    00000000h Prefetchable Memory Limit Upper Portion: All bits are read/writable 
**                                                             bridge supports full 64-bit addressing.
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_PREFETCHABLE_MEMORY_LIMIT_UPPER32_REG    0x2C    /*dword: 0x2f,0x2e,0x2d,0x2c */
/*
**==============================================================================
**  0x33-0x30 : 
** Bit       Default                       Description
** 07:00       DCh                      Capabilities Pointer: Pointer to the first CAP ID entry in the capabilities list is at DCh in PCI configuration
**                                                            space. (Power Management Capability Registers)
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_CAPABILITIES_POINTER_REG	                 0x34    /*byte*/ 
/*
**==============================================================================
**  0x3b-0x35 : reserved
**==============================================================================
*/
/*
**==============================================================================
**  0x3d-0x3c : 
**
** Bit       Default                       Description
** 15:08       00h                       Interrupt Pin (PIN): Bridges do not support the generation of interrupts.
** 07:00       00h                     Interrupt Line (LINE): The bridge does not generate interrupts, so this is reserved as '00h'.
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_PRIMARY_INTERRUPT_LINE_REG                0x3C    /*byte*/ 
#define     ARCMSR_PCI2PCI_PRIMARY_INTERRUPT_PIN_REG                 0x3D    /*byte*/ 
/*
**==============================================================================
**  0x3f-0x3e : 
** Bit       Default                       Description
** 15:12        0h                          Reserved
** 11           0b                Discard Timer SERR# Enable: Controls the generation of SERR# on the primary interface (P_SERR#) in response
**                                                            to a timer discard on either the primary or secondary interface.
**                                                            0b=SERR# is not asserted.
**                                                            1b=SERR# is asserted.
** 10           0b                Discard Timer Status (DTS): This bit is set to a '1b' when either the primary or secondary discard timer expires.
**                                                            The delayed completion is then discarded.
** 09           0b             Secondary Discard Timer (SDT): Sets the maximum number of PCI clock cycles 
**									that bridge waits for an initiator on the secondary bus 
**									to repeat a delayed transaction request. 
**                                                            The counter starts when the delayed transaction completion is ready 
**									to be returned to the initiator. 
**                                                            When the initiator has not repeated the transaction 
**									at least once before the counter expires,bridge 
**										discards the delayed transaction from its queues.
**                                                            0b=The secondary master time-out counter is 2 15 PCI clock cycles.
**                                                            1b=The secondary master time-out counter is 2 10 PCI clock cycles.
** 08           0b               Primary Discard Timer (PDT): Sets the maximum number of PCI clock cycles 
**									that bridge waits for an initiator on the primary bus 
**									to repeat a delayed transaction request. 
**                                                            The counter starts when the delayed transaction completion 
**									is ready to be returned to the initiator. 
**                                                            When the initiator has not repeated the transaction 
**									at least once before the counter expires, 
**									bridge discards the delayed transaction from its queues.
**                                                            0b=The primary master time-out counter is 2 15 PCI clock cycles.
**                                                            1b=The primary master time-out counter is 2 10 PCI clock cycles.
** 07           0b            Fast Back-to-Back Enable (FBE): The bridge does not initiate back to back transactions.
** 06           0b                 Secondary Bus Reset (SBR): 
**                                                            When cleared to 0b: The bridge deasserts S_RST#, 
**									when it had been asserted by writing this bit to a 1b.
**                                                                When set to 1b: The bridge asserts S_RST#.
** 05           0b                   Master Abort Mode (MAM): Dictates bridge behavior on the initiator bus 
**									when a master abort termination occurs in response to 
**										a delayed transaction initiated by bridge on the target bus.
**                                                            0b=The bridge asserts TRDY# in response to a non-locked delayed transaction,
**										and returns FFFF FFFFh when a read.
**                                                            1b=When the transaction had not yet been completed on the initiator bus 
**										(e.g.,delayed reads, or non-posted writes), 
**                                                                 then bridge returns a Target Abort in response to the original requester 
**                                                                 when it returns looking for its delayed completion on the initiator bus. 
**                                                                 When the transaction had completed on the initiator bus (e.g., a PMW), 
**										then bridge asserts P_SERR# (when enabled).
**                                   For PCI-X transactions this bit is an enable for the assertion of P_SERR# due to a master abort 
**								while attempting to deliver a posted memory write on the destination bus.
** 04           0b                   VGA Alias Filter Enable: This bit dictates bridge behavior in conjunction with the VGA enable bit 
**								(also of this register), 
**                                                            and the VGA Palette Snoop Enable bit (Command Register). 
**                                                            When the VGA enable, or VGA Palette Snoop enable bits are on (i.e., 1b) 
**									the VGA Aliasing bit for the corresponding enabled functionality,:
**                                                            0b=Ignores address bits AD[15:10] when decoding VGA I/O addresses.
**                                                            1b=Ensures that address bits AD[15:10] equal 000000b when decoding VGA I/O addresses.
**                                   When all VGA cycle forwarding is disabled, (i.e., VGA Enable bit =0b and VGA Palette Snoop bit =0b), 
**									then this bit has no impact on bridge behavior.
** 03           0b                                VGA Enable: Setting this bit enables address decoding
**								 and transaction forwarding of the following VGA transactions from the primary bus 
**									to the secondary bus:
**                                                            frame buffer memory addresses 000A0000h:000BFFFFh, 
**									VGA I/O addresses 3B0:3BBh and 3C0h:3DFh, where AD[31:16]=¡§0000h?**									?and AD[15:10] are either not decoded (i.e., don't cares),
**										 or must be ¡§000000b¡¨
**                                                            depending upon the state of the VGA Alias Filter Enable bit. (bit(4) of this register)
**                                                            I/O and Memory Enable bits must be set in the Command register 
**										to enable forwarding of VGA cycles.
** 02           0b                                ISA Enable: Setting this bit enables special handling 
**								for the forwarding of ISA I/O transactions that fall within the address range 
**									specified by the I/O Base and Limit registers, 
**										and are within the lowest 64Kbyte of the I/O address map 
**											(i.e., 0000 0000h - 0000 FFFFh).
**                                                            0b=All I/O transactions that fall within the I/O Base 
**										and Limit registers' specified range are forwarded 
**											from primary to secondary unfiltered.
**                                                            1b=Blocks the forwarding from primary to secondary 
**											of the top 768 bytes of each 1Kbyte alias. 
**												On the secondary the top 768 bytes of each 1K alias 
**													are inversely decoded and forwarded 
**														from secondary to primary.
** 01           0b                      SERR# Forward Enable: 0b=The bridge does not assert P_SERR# as a result of an S_SERR# assertion.
**                                                            1b=The bridge asserts P_SERR# whenever S_SERR# is detected 
**									asserted provided the SERR# Enable bit is set (PCI Command Register bit(8)=1b).
** 00           0b                     Parity Error Response: This bit controls bridge response to a parity error 
**										that is detected on its secondary interface.
**                                                            0b=When a data parity error is detected bridge does not assert S_PERR#. 
**                                                            Also bridge does not assert P_SERR# in response to a detected address 
**										or attribute parity error.
**                                                            1b=When a data parity error is detected bridge asserts S_PERR#. 
**										The bridge also asserts P_SERR# (when enabled globally via bit(8) 
**											of the Command register)
**                                                            in response to a detected address or attribute parity error.
**==============================================================================
*/
#define     ARCMSR_PCI2PCI_BRIDGE_CONTROL_REG	                     0x3E    /*word*/ 
/*
**************************************************************************
**                  Device Specific Registers 40-A7h
**************************************************************************
** ----------------------------------------------------------------------------------------------------------
** |    Byte 3              |         Byte 2         |        Byte 1          |       Byte 0              | Configu-ration Byte Offset
** ----------------------------------------------------------------------------------------------------------
** |    Bridge Control 0    |             Arbiter Control/Status              |      Reserved             | 40h
** ----------------------------------------------------------------------------------------------------------
** |                 Bridge Control 2                |                 Bridge Control 1                   | 44h
** ----------------------------------------------------------------------------------------------------------
** |                    Reserved                     |                 Bridge Status                      | 48h
** ----------------------------------------------------------------------------------------------------------
** |                                             Reserved                                                 | 4Ch
** ----------------------------------------------------------------------------------------------------------
** |                 Prefetch Policy                 |               Multi-Transaction Timer              | 50h
** ----------------------------------------------------------------------------------------------------------
** |       Reserved         |      Pre-boot Status   |             P_SERR# Assertion Control              | 54h
** ----------------------------------------------------------------------------------------------------------
** |       Reserved         |        Reserved        |             Secondary Decode Enable                | 58h
** ----------------------------------------------------------------------------------------------------------
** |                    Reserved                     |                 Secondary IDSEL                    | 5Ch
** ----------------------------------------------------------------------------------------------------------
** |                                              Reserved                                                | 5Ch
** ----------------------------------------------------------------------------------------------------------
** |                                              Reserved                                                | 68h:CBh
** ----------------------------------------------------------------------------------------------------------
**************************************************************************
**==============================================================================
**  0x42-0x41: Secondary Arbiter Control/Status Register - SACSR
** Bit       Default                       Description
** 15:12      1111b                  Grant Time-out Violator: This field indicates the agent that violated the Grant Time-out rule 
**							(PCI=16 clocks,PCI-X=6 clocks). 
**                                   Note that this field is only meaningful when:
**                                                              # Bit[11] of this register is set to 1b, 
**									indicating that a Grant Time-out violation had occurred. 
**                                                              # bridge internal arbiter is enabled.
**                                           Bits[15:12] Violating Agent (REQ#/GNT# pair number)
**                                                 0000b REQ#/GNT#[0]
**                                                 0001b REQ#/GNT#[1]
**                                                 0010b REQ#/GNT#[2]
**                                                 0011b REQ#/GNT#[3]
**                                                 1111b Default Value (no violation detected)
**                                   When bit[11] is cleared by software, this field reverts back to its default value.
**                                   All other values are Reserved
** 11            0b                  Grant Time-out Occurred: When set to 1b, 
**                                   this indicates that a Grant Time-out error had occurred involving one of the secondary bus agents.
**                                   Software clears this bit by writing a 1b to it.
** 10            0b                      Bus Parking Control: 0=During bus idle, bridge parks the bus on the last master to use the bus.
**                                                            1=During bus idle, bridge parks the bus on itself. 
**									The bus grant is removed from the last master and internally asserted to bridge.
** 09:08        00b                          Reserved
** 07:00      0000 0000b  Secondary Bus Arbiter Priority Configuration: The bridge secondary arbiter provides two rings of arbitration priority. 
**                                                                      Each bit of this field assigns its corresponding secondary 
**										bus master to either the high priority arbiter ring (1b) 
**											or to the low priority arbiter ring (0b). 
**                                                                      Bits [3:0] correspond to request inputs S_REQ#[3:0], respectively. 
**                                                                      Bit [6] corresponds to the bridge internal secondary bus request 
**										while Bit [7] corresponds to the SATU secondary bus request. 
**                                                                      Bits [5:4] are unused.
**                                                                      0b=Indicates that the master belongs to the low priority group.
**                                                                      1b=Indicates that the master belongs to the high priority group
**=================================================================================
**  0x43: Bridge Control Register 0 - BCR0
** Bit       Default                       Description
** 07           0b                  Fully Dynamic Queue Mode: 0=The number of Posted write transactions is limited to eight 
**									and the Posted Write data is limited to 4KB.
**                                                            1=Operation in fully dynamic queue mode. The bridge enqueues up to 
**									14 Posted Memory Write transactions and 8KB of posted write data.
** 06:03        0H                          Reserved.
** 02           0b                 Upstream Prefetch Disable: This bit disables bridge ability 
**									to perform upstream prefetch operations for Memory 
**										Read requests received on its secondary interface. 
**                                 This bit also controls the bridge's ability to generate advanced read commands 
**								when forwarding a Memory Read Block transaction request upstream from a PCI-X bus 
**										to a Conventional PCI bus.
**                                 0b=bridge treats all upstream Memory Read requests as though they target prefetchable memory.
**										The use of Memory Read Line and Memory Read
**                                      Multiple is enabled when forwarding a PCI-X Memory Read Block request 
**										to an upstream bus operating in Conventional PCI mode.
**                                 1b=bridge treats upstream PCI Memory Read requests as though 
**									they target non-prefetchable memory and forwards upstream PCI-X Memory 
**											Read Block commands as Memory Read 
**												when the primary bus is operating 
**													in Conventional PCI mode.
**                                 NOTE: This bit does not affect bridge ability to perform read prefetching 
**									when the received command is Memory Read Line or Memory Read Multiple.
**=================================================================================
**  0x45-0x44: Bridge Control Register 1 - BCR1 (Sheet 2 of 2)
** Bit       Default                       Description
** 15:08    0000000b                         Reserved
** 07:06         00b                   Alias Command Mapping: This two bit field determines how bridge handles PCI-X ¡§Alias¡¨ commands, 
**								specifically the Alias to Memory Read Block and Alias to Memory Write Block commands. 
**                                                            The three options for handling these alias commands are to either pass it as is, 
**									re-map to the actual block memory read/write command encoding, or ignore
**                                                            			the transaction forcing a Master Abort to occur on the Origination Bus.
**                                                   Bit (7:6) Handling of command
**                                                        0 0 Re-map to Memory Read/Write Block before forwarding
**                                                        0 1 Enqueue and forward the alias command code unaltered
**                                                        1 0 Ignore the transaction, forcing Master Abort
**                                                        1 1 Reserved
** 05            1b                  Watchdog Timers Disable: Disables or enables all 2 24 Watchdog Timers in both directions. 
**                                                            The watchdog timers are used to detect prohibitively long latencies in the system. 
**                                                            The watchdog timer expires when any Posted Memory Write (PMW), Delayed Request, 
**                                                            or Split Requests (PCI-X mode) is not completed within 2 24 events 
**                                                            (¡§events¡¨ are defined as PCI Clocks when operating in PCI-X mode, 
**								and as the number of times being retried when operating in Conventional PCI mode)
**                                                            0b=All 2 24 watchdog timers are enabled.
**                                                            1b=All 2 24 watchdog timers are disabled and there is no limits to 
**									the number of attempts bridge makes when initiating a PMW, 
**                                                                 transacting a Delayed Transaction, or how long it waits for 
**									a split completion corresponding to one of its requests.
** 04            0b                  GRANT# time-out disable: This bit enables/disables the GNT# time-out mechanism. 
**                                                            Grant time-out is 16 clocks for conventional PCI, and 6 clocks for PCI-X.
**                                                            0b=The Secondary bus arbiter times out an agent 
**									that does not assert FRAME# within 16/6 clocks of receiving its grant, 
**										once the bus has gone idle. 
**                                                                 The time-out counter begins as soon as the bus goes idle with the new GNT# asserted. 
**                                                                 An infringing agent does not receive a subsequent GNT# 
**									until it de-asserts its REQ# for at least one clock cycle.
**                                                            1b=GNT# time-out mechanism is disabled.
** 03           00b                           Reserved.
** 02            0b          Secondary Discard Timer Disable: This bit enables/disables bridge secondary delayed transaction discard mechanism.
**                                                            The time out mechanism is used to ensure that initiators 
**									of delayed transactions return for their delayed completion data/status 
**										within a reasonable amount of time after it is available from bridge.
**                                                            0b=The secondary master time-out counter is enabled 
**										and uses the value specified by the Secondary Discard Timer bit 
**											(see Bridge Control Register).
**                                                            1b=The secondary master time-out counter is disabled. 
**											The bridge waits indefinitely for a secondary bus master 
**												to repeat a delayed transaction.
** 01            0b            Primary Discard Timer Disable: This bit enables/disables bridge primary delayed transaction discard mechanism. 
**								The time out mechanism is used to ensure that initiators 
**									of delayed transactions return for their delayed completion data/status 
**										within a reasonable amount of time after it is available from bridge.
**                                                            0b=The primary master time-out counter is enabled and uses the value specified 
**									by the Primary Discard Timer bit (see Bridge Control Register).
**                                                            1b=The secondary master time-out counter is disabled. 
**									The bridge waits indefinitely for a secondary bus master 
**										to repeat a delayed transaction.
** 00            0b                           Reserved
**=================================================================================
**  0x47-0x46: Bridge Control Register 2 - BCR2
** Bit       Default                       Description
** 15:07      0000b                          Reserved.
** 06            0b Global Clock Out Disable (External Secondary Bus Clock Source Enable): 
**									This bit disables all of the secondary PCI clock outputs including 
**										the feedback clock S_CLKOUT. 
**                                                            This means that the user is required to provide an S_CLKIN input source.
** 05:04        11 (66 MHz)                  Preserved.
**              01 (100 MHz)
**              00 (133 MHz)
** 03:00        Fh (100 MHz & 66 MHz)
**              7h (133 MHz)
**                                        This 4 bit field provides individual enable/disable mask bits for each of bridge
**                                        secondary PCI clock outputs. Some, or all secondary clock outputs (S_CLKO[3:0])
**                                        default to being enabled following the rising edge of P_RST#, depending on the
**                                        frequency of the secondary bus clock:
**                                               ¡E Designs with 100 MHz (or lower) Secondary PCI clock power up with 
**								all four S_CLKOs enabled by default. (SCLKO[3:0])¡P
**                                               ¡E Designs with 133 MHz Secondary PCI clock power up 
**								with the lower order 3 S_CLKOs enabled by default. 
**								(S_CLKO[2:0]) Only those SCLKs that power up enabled by can be connected 
**								to downstream device clock inputs.
**=================================================================================
**  0x49-0x48: Bridge Status Register - BSR
** Bit       Default                       Description
** 15           0b  Upstream Delayed Transaction Discard Timer Expired: This bit is set to a 1b and P_SERR# 
**									is conditionally asserted when the secondary discard timer expires.
** 14           0b  Upstream Delayed/Split Read Watchdog Timer Expired: 
**                                                     Conventional PCI Mode: This bit is set to a 1b and P_SERR#
**									is conditionally asserted when bridge discards an upstream delayed read **	**									transaction request after 2 24 retries following the initial retry.
**                                                                PCI-X Mode: This bit is set to a 1b and P_SERR# is conditionally asserted 
**									when bridge discards an upstream split read request 
**									after waiting in excess of 2 24 clocks for the corresponding 
**									Split Completion to arrive.
** 13           0b Upstream Delayed/Split Write Watchdog Timer Expired: 
**                                                     Conventional PCI Mode: This bit is set to a 1b and P_SERR# 
**									is conditionally asserted when bridge discards an upstream delayed write **	**									transaction request after 2 24 retries following the initial retry.
**                                                                PCI-X Mode: This bit is set to a 1b and P_SERR# 
**									is conditionally asserted when bridge discards an upstream split write request **									after waiting in excess of 2 24 clocks for the corresponding 
**									Split Completion to arrive.
** 12           0b           Master Abort during Upstream Posted Write: This bit is set to a 1b and P_SERR# 
**									is conditionally asserted when a Master Abort occurs as a result of an attempt, 
**									by bridge, to retire a PMW upstream.
** 11           0b           Target Abort during Upstream Posted Write: This bit is set to a 1b and P_SERR# 
**									is conditionally asserted when a Target Abort occurs as a result of an attempt,
**									by bridge, to retire a PMW upstream.
** 10           0b                Upstream Posted Write Data Discarded: This bit is set to a 1b and P_SERR# 
**									is conditionally asserted when bridge discards an upstream PMW transaction 
**									after receiving 2 24 target retries from the primary bus target
** 09           0b             Upstream Posted Write Data Parity Error: This bit is set to a 1b and P_SERR# 
**									is conditionally asserted when a data parity error is detected by bridge 
**									while attempting to retire a PMW upstream
** 08           0b                  Secondary Bus Address Parity Error: This bit is set to a 1b and P_SERR# 
**									is conditionally asserted when bridge detects an address parity error on 
**									the secondary bus.
** 07           0b Downstream Delayed Transaction Discard Timer Expired: This bit is set to a 1b and P_SERR# 
**									is conditionally asserted when the primary bus discard timer expires.
** 06           0b Downstream Delayed/Split Read Watchdog Timer Expired:
**                                                     Conventional PCI Mode: This bit is set to a 1b and P_SERR# 
**									is conditionally asserted when bridge discards a downstream delayed read **	**										transaction request after receiving 2 24 target retries
**											 from the secondary bus target.
**                                                                PCI-X Mode: This bit is set to a 1b and P_SERR# is conditionally asserted 
**										when bridge discards a downstream split read request 
**											after waiting in excess of 2 24 clocks for the corresponding 
**												Split Completion to arrive.
** 05           0b Downstream Delayed Write/Split Watchdog Timer Expired:
**                                                     Conventional PCI Mode: This bit is set to a 1b and P_SERR# is conditionally asserted 
**									when bridge discards a downstream delayed write transaction request 
**										after receiving 2 24 target retries from the secondary bus target.
**                                                                PCI-X Mode: This bit is set to a 1b and P_SERR# 
**									is conditionally asserted when bridge discards a downstream 
**										split write request after waiting in excess of 2 24 clocks 
**											for the corresponding Split Completion to arrive.
** 04           0b          Master Abort during Downstream Posted Write: This bit is set to a 1b and P_SERR# 
**									is conditionally asserted when a Master Abort occurs as a result of an attempt, 
**										by bridge, to retire a PMW downstream.
** 03           0b          Target Abort during Downstream Posted Write: This bit is set to a 1b and P_SERR# is conditionally asserted 
**										when a Target Abort occurs as a result of an attempt, by bridge, 
**											to retire a PMW downstream.
** 02           0b               Downstream Posted Write Data Discarded: This bit is set to a 1b and P_SERR#
**									is conditionally asserted when bridge discards a downstream PMW transaction 
**										after receiving 2 24 target retries from the secondary bus target
** 01           0b            Downstream Posted Write Data Parity Error: This bit is set to a 1b and P_SERR# 
**									is conditionally asserted when a data parity error is detected by bridge 
**										while attempting to retire a PMW downstream.
** 00           0b                     Primary Bus Address Parity Error: This bit is set to a 1b and P_SERR# is conditionally asserted 
**										when bridge detects an address parity error on the primary bus.
**==================================================================================
**  0x51-0x50: Bridge Multi-Transaction Timer Register - BMTTR
** Bit       Default                       Description
** 15:13       000b                          Reserved
** 12:10       000b                          GRANT# Duration: This field specifies the count (PCI clocks) 
**							that a secondary bus master has its grant maintained in order to enable 
**								multiple transactions to execute within the same arbitration cycle.
**                                                    Bit[02:00] GNT# Extended Duration
**                                                               000 MTT Disabled (Default=no GNT# extension)
**                                                               001 16 clocks
**                                                               010 32 clocks
**                                                               011 64 clocks
**                                                               100 128 clocks
**                                                               101 256 clocks
**                                                               110 Invalid (treated as 000)
**                                                               111 Invalid (treated as 000)
** 09:08        00b                          Reserved
** 07:00        FFh                                 MTT Mask: This field enables/disables MTT usage for each REQ#/GNT# 
**								pair supported by bridge secondary arbiter. 
**                                                            Bit(7) corresponds to SATU internal REQ#/GNT# pair,
**                                                            bit(6) corresponds to bridge internal REQ#/GNT# pair, 
**                                                            bit(5) corresponds to REQ#/GNT#(5) pair, etc.
**                                                  When a given bit is set to 1b, its corresponding REQ#/GNT# 
**								pair is enabled for MTT functionality as determined by bits(12:10) of this register.
**                                                  When a given bit is cleared to 0b, its corresponding REQ#/GNT# pair is disabled from using the MTT.
**==================================================================================
**  0x53-0x52: Read Prefetch Policy Register - RPPR
** Bit       Default                       Description
** 15:13       000b                    ReRead_Primary Bus: 3-bit field indicating the multiplication factor 
**							to be used in calculating the number of bytes to prefetch from the secondary bus interface on **								subsequent PreFetch operations given that the read demands were not satisfied 
**									using the FirstRead parameter.
**                                           The default value of 000b correlates to: Command Type Hardwired pre-fetch amount Memory Read 4 DWORDs 
**							Memory Read Line 1 cache lines Memory Read Multiple 2 cache lines
** 12:10       000b                 FirstRead_Primary Bus: 3-bit field indicating the multiplication factor to be used in calculating 
**							the number of bytes to prefetch from the secondary bus interface 
**								on the initial PreFetch operation.
**                                           The default value of 000b correlates to: Command Type Hardwired pre-fetch amount Memory Read 4 DWORDs 
**								Memory Read Line 1 cache line Memory Read Multiple 2 cache lines
** 09:07       010b                  ReRead_Secondary Bus: 3-bit field indicating the multiplication factor to be used 
**								in calculating the number of bytes to prefetch from the primary 
**									bus interface on subsequent PreFetch operations given 
**										that the read demands were not satisfied using 
**											the FirstRead parameter.
**                                           The default value of 010b correlates to: Command Type Hardwired pre-fetch a
**							mount Memory Read 3 cache lines Memory Read Line 3 cache lines 
**								Memory Read Multiple 6 cache lines
** 06:04       000b               FirstRead_Secondary Bus: 3-bit field indicating the multiplication factor to be used 
**							in calculating the number of bytes to prefetch from 
**								the primary bus interface on the initial PreFetch operation.
**                                           The default value of 000b correlates to: Command Type Hardwired pre-fetch amount 
**							Memory Read 4 DWORDs Memory Read Line 1 cache line Memory Read Multiple 2 cache lines
** 03:00      1111b                Staged Prefetch Enable: This field enables/disables the FirstRead/ReRead pre-fetch 
**							algorithm for the secondary and the primary bus interfaces.
**                                                         Bit(3) is a ganged enable bit for REQ#/GNT#[7:3], and bits(2:0) provide individual
**                                                                            enable bits for REQ#/GNT#[2:0]. 
**							  (bit(2) is the enable bit for REQ#/GNT#[2], etc...)
**                                                                            1b: enables the staged pre-fetch feature
**                                                                            0b: disables staged pre-fetch,
**                                                         and hardwires read pre-fetch policy to the following for 
**                                                         Memory Read, 
**                                                         Memory Read Line, 
**                                                     and Memory Read Multiple commands: 
**                                                     Command Type Hardwired Pre-Fetch Amount...
**                                                                                      Memory Read 4 DWORDs
**                                                                                      Memory Read Line 1 cache line
**                                                                                      Memory Read Multiple 2 cache lines
** NOTE: When the starting address is not cache line aligned, bridge pre-fetches Memory Read line commands 
** only to the next higher cache line boundary.For non-cache line aligned Memory Read 
** Multiple commands bridge pre-fetches only to the second cache line boundary encountered.
**==================================================================================
**  0x55-0x54: P_SERR# Assertion Control - SERR_CTL
** Bit       Default                       Description
**  15          0b   Upstream Delayed Transaction Discard Timer Expired: Dictates the bridge behavior 
** 						in response to its discarding of a delayed transaction that was initiated from the primary bus.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  14          0b   Upstream Delayed/Split Read Watchdog Timer Expired: Dictates bridge behavior following expiration of the subject watchdog timer.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  13          0b   Upstream Delayed/Split Write Watchdog Timer Expired: Dictates bridge behavior following expiration of the subject watchdog timer.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  12          0b             Master Abort during Upstream Posted Write: Dictates bridge behavior following 
**						its having detected a Master Abort while attempting to retire one of its PMWs upstream.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  11          0b             Target Abort during Upstream Posted Write: Dictates bridge behavior following 
**						its having been terminated with Target Abort while attempting to retire one of its PMWs upstream.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  10          0b                  Upstream Posted Write Data Discarded: Dictates bridge behavior in the event that 
**						it discards an upstream posted write transaction.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  09          0b               Upstream Posted Write Data Parity Error: Dictates bridge behavior 
**						when a data parity error is detected while attempting to retire on of its PMWs upstream.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  08          0b                    Secondary Bus Address Parity Error: This bit dictates bridge behavior 
**						when it detects an address parity error on the secondary bus.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  07          0b  Downstream Delayed Transaction Discard Timer Expired: Dictates bridge behavior in response to 
**						its discarding of a delayed transaction that was initiated on the secondary bus.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  06          0b  Downstream Delayed/Split Read Watchdog Timer Expired: Dictates bridge behavior following expiration of the subject watchdog timer.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  05          0b Downstream Delayed/Split Write Watchdog Timer Expired: Dictates bridge behavior following expiration of the subject watchdog timer.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  04          0b           Master Abort during Downstream Posted Write: Dictates bridge behavior following 
**						its having detected a Master Abort while attempting to retire one of its PMWs downstream.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  03          0b           Target Abort during Downstream Posted Write: Dictates bridge behavior following 
**						its having been terminated with Target Abort while attempting to retire one of its PMWs downstream.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  02          0b                Downstream Posted Write Data Discarded: Dictates bridge behavior in the event 
**						that it discards a downstream posted write transaction.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  01          0b             Downstream Posted Write Data Parity Error: Dictates bridge behavior 
**						when a data parity error is detected while attempting to retire on of its PMWs downstream.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**  00          0b                      Primary Bus Address Parity Error: This bit dictates bridge behavior 
**						when it detects an address parity error on the primary bus.
**                                                                       0b=bridge asserts P_SERR#.
**                                                                       1b=bridge does not assert P_SERR#
**===============================================================================
**  0x56: Pre-Boot Status Register - PBSR
** Bit       Default                       							Description
** 07           1                          							 Reserved
** 06           -                          							 Reserved - value indeterminate
** 05:02        0                          							 Reserved
** 01      Varies with External State of S_133EN at PCI Bus Reset    Secondary Bus Max Frequency Setting:
**									 This bit reflect captured S_133EN strap, 
**										indicating the maximum secondary bus clock frequency when in PCI-X mode.
**                                                                   Max Allowable Secondary Bus Frequency
**																			**						S_133EN PCI-X Mode
**																			**						0 100 MHz
**																			**						1 133 MH
** 00          0b                                                    Reserved
**===============================================================================
**  0x59-0x58: Secondary Decode Enable Register - SDER
** Bit       Default                       							Description
** 15:03      FFF1h                        							 Preserved.
** 02     Varies with External State of PRIVMEM at PCI Bus Reset   Private Memory Space Enable - when set, 
**									bridge overrides its secondary inverse decode logic and not
**                                                                 forward upstream any secondary bus initiated DAC Memory transactions with AD(63)=1b.
**                                                                 This creates a private memory space on the Secondary PCI bus 
**									that allows peer-to-peer transactions.
** 01:00      10 2                                                   Preserved.
**===============================================================================
**  0x5D-0x5C: Secondary IDSEL Select Register - SISR
** Bit       Default                       							Description
** 15:10     000000 2                      							 Reserved.
** 09    Varies with External State of PRIVDEV at PCI Bus Reset     AD25- IDSEL Disable - When this bit is set, 
**							AD25 is deasserted for any possible Type 1 to Type 0 conversion.
**                                                                                        When this bit is clear, 
**							AD25 is asserted when Primary addresses AD[15:11]=01001 2 during a Type 1 to Type 0 conversion.
** 08    Varies with External State of PRIVDEV at PCI Bus Reset     AD24- IDSEL Disable - When this bit is set, 
**							AD24 is deasserted for any possible Type 1 to Type 0 conversion.
**                                                                                        When this bit is clear, 
**							AD24 is asserted when Primary addresses AD[15:11]=01000 2 during a Type 1 to Type 0 conversion.
** 07    Varies with External State of PRIVDEV at PCI Bus Reset     AD23- IDSEL Disable - When this bit is set, 
**							AD23 is deasserted for any possible Type 1 to Type 0 conversion. 
**                                                                                        When this bit is clear, 
**							AD23 is asserted when Primary addresses AD[15:11]=00111 2 during a Type 1 to Type 0 conversion.
** 06    Varies with External State of PRIVDEV at PCI Bus Reset     AD22- IDSEL Disable - When this bit is set, 
**							AD22 is deasserted for any possible Type 1 to Type 0 conversion.
**                                                                                        When this bit is clear, 
**							AD22 is asserted when Primary addresses AD[15:11]=00110 2 during a Type 1 to Type 0 conversion.
** 05    Varies with External State of PRIVDEV at PCI Bus Reset     AD21- IDSEL Disable - When this bit is set, 
**							AD21 is deasserted for any possible Type 1 to Type 0 conversion.
**                                                                                        When this bit is clear, 
**							AD21 is asserted when Primary addresses AD[15:11]=00101 2 during a Type 1 to Type 0 conversion.
** 04    Varies with External State of PRIVDEV at PCI Bus Reset     AD20- IDSEL Disable - When this bit is set, 
**							AD20 is deasserted for any possible Type 1 to Type 0 conversion.
**                                                                                        When this bit is clear, 
**							AD20 is asserted when Primary addresses AD[15:11]=00100 2 during a Type 1 to Type 0 conversion.
** 03    Varies with External State of PRIVDEV at PCI Bus Reset     AD19- IDSEL Disable - When this bit is set, 
**							AD19 is deasserted for any possible Type 1 to Type 0 conversion.
**                                                                                        When this bit is clear,
**							AD19 is asserted when Primary addresses AD[15:11]=00011 2 during a Type 1 to Type 0 conversion.
** 02    Varies with External State of PRIVDEV at PCI Bus Reset     AD18- IDSEL Disable - When this bit is set, 
**							AD18 is deasserted for any possible Type 1 to Type 0 conversion.
**                                                                                        When this bit is clear,
**							AD18 is asserted when Primary addresses AD[15:11]=00010 2 during a Type 1 to Type 0 conversion.
** 01    Varies with External State of PRIVDEV at PCI Bus Reset     AD17- IDSEL Disable - When this bit is set, 
**							AD17 is deasserted for any possible Type 1 to Type 0 conversion.
**                                                                                        When this bit is clear, 
**							AD17 is asserted when Primary addresses AD[15:11]=00001 2 during a Type 1 to Type 0 conversion.
** 00    Varies with External State of PRIVDEV at PCI Bus Reset     AD16- IDSEL Disable - When this bit is set, 
**							AD16 is deasserted for any possible Type 1 to Type 0 conversion.
**                                                                                        When this bit is clear, 
**							AD16 is asserted when Primary addresses AD[15:11]=00000 2 during a Type 1 to Type 0 conversion.
**************************************************************************
*/
/*
**************************************************************************
**                 Reserved      A8-CBh           
**************************************************************************
*/
/*
**************************************************************************
**                  PCI Extended Enhanced Capabilities List CC-FFh
**************************************************************************
** ----------------------------------------------------------------------------------------------------------
** |    Byte 3              |         Byte 2         |        Byte 1          |       Byte 0              | Configu-ration Byte Offset
** ----------------------------------------------------------------------------------------------------------
** |           Power Management Capabilities         |        Next Item Ptr   |     Capability ID         | DCh
** ----------------------------------------------------------------------------------------------------------
** |        PM Data         |       PPB Support      |            Extensions Power Management CSR         | E0h
** ----------------------------------------------------------------------------------------------------------
** |                    Reserved                     |        Reserved        |        Reserved           | E4h
** ----------------------------------------------------------------------------------------------------------
** |                                              Reserved                                                | E8h
** ----------------------------------------------------------------------------------------------------------
** |       Reserved         |        Reserved        |        Reserved        |         Reserved          | ECh
** ----------------------------------------------------------------------------------------------------------
** |              PCI-X Secondary Status             |       Next Item Ptr    |       Capability ID       | F0h
** ----------------------------------------------------------------------------------------------------------
** |                                         PCI-X Bridge Status                                          | F4h
** ----------------------------------------------------------------------------------------------------------
** |                                PCI-X Upstream Split Transaction Control                              | F8h
** ----------------------------------------------------------------------------------------------------------
** |                               PCI-X Downstream Split Transaction Control                             | FCh
** ----------------------------------------------------------------------------------------------------------
**===============================================================================
**  0xDC: Power Management Capabilities Identifier - PM_CAPID
** Bit       Default                       Description
** 07:00       01h                        Identifier (ID): PCI SIG assigned ID for PCI-PM register block
**===============================================================================
**  0xDD: Next Item Pointer - PM_NXTP
** Bit       Default                       Description
** 07:00       F0H                Next Capabilities Pointer (PTR): The register defaults to F0H pointing to the PCI-X Extended Capability Header.
**===============================================================================
**  0xDF-0xDE: Power Management Capabilities Register - PMCR
** Bit       Default                       Description
** 15:11       00h                     PME Supported (PME): PME# cannot be asserted by bridge.
** 10           0h                 State D2 Supported (D2): Indicates no support for state D2. No power management action in this state.
** 09           1h                 State D1 Supported (D1): Indicates support for state D1. No power management action in this state.
** 08:06        0h                Auxiliary Current (AUXC): This 3 bit field reports the 3.3Vaux auxiliary current requirements for the PCI function. 
**                                                          This returns 000b as PME# wake-up for bridge is not implemented.
** 05           0   Special Initialization Required (SINT): Special initialization is not required for bridge.
** 04:03       00                            Reserved
** 02:00       010                            Version (VS): Indicates that this supports PCI Bus Power Management Interface Specification, Revision 1.1.
**===============================================================================
**  0xE1-0xE0: Power Management Control / Status - Register - PMCSR
** Bit       Default                       Description
** 15:09       00h                          Reserved
** 08          0b                          PME_Enable: This bit, when set to 1b enables bridge to assert PME#. 
**	Note that bridge never has occasion to assert PME# and implements this dummy R/W bit only for the purpose of working around an OS PCI-PM bug.
** 07:02       00h                          Reserved
** 01:00       00                Power State (PSTATE): This 2-bit field is used both to determine the current power state of 
**									a function and to set the Function into a new power state.
**  													00 - D0 state
**  													01 - D1 state
**  													10 - D2 state
**  													11 - D3 hot state
**===============================================================================
**  0xE2: Power Management Control / Status PCI to PCI Bridge Support - PMCSR_BSE
** Bit       Default                       Description
** 07          0         Bus Power/Clock Control Enable (BPCC_En): Indicates that the bus power/clock control policies have been disabled.
** 06          0                B2/B3 support for D3 Hot (B2_B3#): The state of this bit determines the action that 
**									is to occur as a direct result of programming the function to D3 hot.
**                                                                 This bit is only meaningful when bit 7 (BPCC_En) is a ¡§1¡¨.
** 05:00     00h                            Reserved
**===============================================================================
**  0xE3: Power Management Data Register - PMDR
** Bit       Default                       Description
** 07:00       00h                          Reserved
**===============================================================================
**  0xF0: PCI-X Capabilities Identifier - PX_CAPID
** Bit       Default                       Description
** 07:00       07h                       Identifier (ID): Indicates this is a PCI-X capabilities list.
**===============================================================================
**  0xF1: Next Item Pointer - PX_NXTP
** Bit       Default                       Description
** 07:00       00h                     Next Item Pointer: Points to the next capability in the linked list The power on default value of this
**                                                        register is 00h indicating that this is the last entry in the linked list of capabilities.
**===============================================================================
**  0xF3-0xF2: PCI-X Secondary Status - PX_SSTS
** Bit       Default                       Description
** 15:09       00h                          Reserved
** 08:06       Xxx                Secondary Clock Frequency (SCF): This field is set with the frequency of the secondary bus. 
**                                                                 The values are:
** 																			**		BitsMax FrequencyClock Period
** 																			**		000PCI ModeN/A
** 																			**		00166 15
** 																			**		01010010
** 																			**		0111337.5
** 																			**		1xxreservedreserved
** 																			**		The default value for this register is the operating frequency of the secondary bus
** 05           0b                   Split Request Delayed. (SRD):  This bit is supposed to be set by a bridge when it cannot forward a transaction on the
** 						secondary bus to the primary bus because there is not enough room within the limit
** 						specified in the Split Transaction Commitment Limit field in the Downstream Split
** 						Transaction Control register. The bridge does not set this bit.
** 04           0b                 Split Completion Overrun (SCO): This bit is supposed to be set when a bridge terminates a Split Completion on the **	**						secondary bus with retry or Disconnect at next ADB because its buffers are full. 
**						The bridge does not set this bit.
** 03           0b              Unexpected Split Completion (USC): This bit is set when an unexpected split completion with a requester ID 
**						equal to bridge secondary bus number, device number 00h,
**						and function number 0 is received on the secondary interface. 
**						This bit is cleared by software writing a '1'.
** 02           0b               Split Completion Discarded (SCD): This bit is set 
**						when bridge discards a split completion moving toward the secondary bus 
**						because the requester would not accept it. This bit cleared by software writing a '1'.
** 01           1b                                133 MHz Capable: Indicates that bridge is capable of running its secondary bus at 133 MHz
** 00           1b                            64-bit Device (D64): Indicates the width of the secondary bus as 64-bits.
**===============================================================================
**  0xF7-0xF6-0xf5-0xF4: PCI-X Bridge Status - PX_BSTS
** Bit       Default      								                 Description
** 31:22        0         								                  Reserved
** 21           0         							Split Request Delayed (SRD): This bit does not be set by bridge.
** 20           0         							Split Completion Overrun (SCO): This bit does not be set by bridge
**										because bridge throttles traffic on the completion side.
** 19           0         							Unexpected Split Completion (USC): The bridge sets this bit to 1b 
**										when it encounters a corrupted Split Completion, possibly with an **	**										inconsistent remaining byte count.Software clears 
**										this bit by writing a 1b to it.
** 18           0         							Split Completion Discarded (SCD): The bridge sets this bit to 1b 
**										when it has discarded a Split Completion.Software clears this bit by **	**										writing a 1b to it.
** 17           1         							133 MHz Capable: This bit indicates that the bridge primary interface is **										capable of 133 MHz operation in PCI-X mode.
**										0=The maximum operating frequency is 66 MHz.
**										1=The maximum operating frequency is 133 MHz.
** 16 Varies with the external state of P_32BITPCI# at PCI Bus Reset    64-bit Device (D64): Indicates bus width of the Primary PCI bus interface.
**										 0=Primary Interface is connected as a 32-bit PCI bus.
**										 1=Primary Interface is connected as a 64-bit PCI bus.
** 15:08       00h 								Bus Number (BNUM): This field is simply an alias to the PBN field 
**											of the BNUM register at offset 18h.
**								Apparently it was deemed necessary reflect it here for diagnostic purposes.
** 07:03       1fh						Device Number (DNUM): Indicates which IDSEL bridge consumes. 
**								May be updated whenever a PCI-X
**								 configuration write cycle that targets bridge scores a hit.
** 02:00        0h                                                   Function Number (FNUM): The bridge Function #
**===============================================================================
**  0xFB-0xFA-0xF9-0xF8: PCI-X Upstream Split Transaction Control - PX_USTC
** Bit       Default                       Description
** 31:16      003Eh                 Split Transaction Limit (STL): This register indicates the size of the commitment limit in units of ADQs.
**                                                                 Software is permitted to program this register to any value greater than or equal to
**                                                                 the contents of the Split Transaction Capacity register. A value less than the contents
**                                                                 of the Split Transaction Capacity register causes unspecified results.
**                                                                 A value of 003Eh or greater enables the bridge to forward all Split Requests of any
**                                                                 size regardless of the amount of buffer space available.
** 15:00      003Eh              Split Transaction Capacity (STC): This read-only field indicates the size of the buffer (number of ADQs) for storing
** 				   split completions. This register controls behavior of the bridge buffers for forwarding
** 				   Split Transactions from a primary bus requester to a secondary bus completer.
** 				   The default value of 003Eh indicates there is available buffer space for 62 ADQs (7936 bytes).
**===============================================================================
**  0xFF-0xFE-0xFD-0xFC: PCI-X Downstream Split Transaction Control - PX_DSTC
** Bit       Default                       Description
** 31:16      003Eh                 Split Transaction Limit (STL):  This register indicates the size of the commitment limit in units of ADQs.
**							Software is permitted to program this register to any value greater than or equal to
**							the contents of the Split Transaction Capacity register. A value less than the contents
**							of the Split Transaction Capacity register causes unspecified results.
**							A value of 003Eh or greater enables the bridge to forward all Split Requests of any
**							size regardless of the amount of buffer space available.
** 15:00      003Eh              Split Transaction Capacity (STC): This read-only field indicates the size of the buffer (number of ADQs) for storing
**                                                                 split completions. This register controls behavior of the bridge buffers for forwarding
**                                                                 Split Transactions from a primary bus requester to a secondary bus completer.
**                                                                 The default value of 003Eh indicates there is available buffer space for 62 ADQs 
**									(7936 bytes).
**************************************************************************
*/




/*
*************************************************************************************************************************************
**                       80331 Address Translation Unit Register Definitions
**                               ATU Interface Configuration Header Format
**               The ATU is programmed via a [Type 0] configuration command on the PCI interface.
*************************************************************************************************************************************
** |    Byte 3              |         Byte 2         |        Byte 1          |       Byte 0              | Configuration Byte Offset
**===================================================================================================================================
** |                ATU Device ID                    |                     Vendor ID                      | 00h
** ----------------------------------------------------------------------------------------------------------
** |                     Status                      |                     Command                        | 04H
** ----------------------------------------------------------------------------------------------------------
** |                              ATU Class Code                              |       Revision ID         | 08H
** ----------------------------------------------------------------------------------------------------------
** |         ATUBISTR       |     Header Type        |      Latency Timer     |      Cacheline Size       | 0CH
** ----------------------------------------------------------------------------------------------------------
** |                                     Inbound ATU Base Address 0                                       | 10H
** ----------------------------------------------------------------------------------------------------------
** |                               Inbound ATU Upper Base Address 0                                       | 14H
** ----------------------------------------------------------------------------------------------------------
** |                                     Inbound ATU Base Address 1                                       | 18H
** ----------------------------------------------------------------------------------------------------------
** |                               Inbound ATU Upper Base Address 1                                       | 1CH
** ----------------------------------------------------------------------------------------------------------
** |                                     Inbound ATU Base Address 2                                       | 20H
** ----------------------------------------------------------------------------------------------------------
** |                               Inbound ATU Upper Base Address 2                                       | 24H
** ----------------------------------------------------------------------------------------------------------
** |                                             Reserved                                                 | 28H   
** ----------------------------------------------------------------------------------------------------------
** |                ATU Subsystem ID                 |                ATU Subsystem Vendor ID             | 2CH
** ----------------------------------------------------------------------------------------------------------
** |                                       Expansion ROM Base Address                                     | 30H
** ----------------------------------------------------------------------------------------------------------
** |                                    Reserved Capabilities Pointer                                     | 34H
** ----------------------------------------------------------------------------------------------------------
** |                                             Reserved                                                 | 38H
** ----------------------------------------------------------------------------------------------------------
** |     Maximum Latency    |     Minimum Grant      |       Interrupt Pin    |      Interrupt Line       | 3CH
** ----------------------------------------------------------------------------------------------------------
*********************************************************************************************************************
*/
/*
***********************************************************************************
**  ATU Vendor ID Register - ATUVID
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  15:00      8086H (0x17D3)               ATU Vendor ID - This is a 16-bit value assigned to Intel. 
**						This register, combined with the DID, uniquely identify the PCI device. 
**      Access type is Read/Write to allow the 80331 to configure the register as a different vendor ID 
**	to simulate the interface of a standard mechanism currently used by existing application software.
***********************************************************************************
*/
#define     ARCMSR_ATU_VENDOR_ID_REG		         0x00    /*word*/
/*
***********************************************************************************
**  ATU Device ID Register - ATUDID
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  15:00      0336H (0x1110)               ATU Device ID - This is a 16-bit value assigned to the ATU. 
**	This ID, combined with the VID, uniquely identify any PCI device.
***********************************************************************************
*/
#define     ARCMSR_ATU_DEVICE_ID_REG		         0x02    /*word*/
/*
***********************************************************************************
**  ATU Command Register - ATUCMD
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  15:11      000000 2                     Reserved
**  10           0                          Interrupt Disable - This bit disables 80331 from asserting the ATU interrupt signal.
**                                                              0=enables the assertion of interrupt signal.
**                                                              1=disables the assertion of its interrupt signal.
**  09          0 2                         Fast Back to Back Enable - When cleared, 
**						the ATU interface is not allowed to generate fast back-to-back cycles on its bus.
**						Ignored when operating in the PCI-X mode.
**  08          0 2                         SERR# Enable - When cleared, the ATU interface is not allowed to assert SERR# on the PCI interface.
**  07          1 2                         Address/Data Stepping Control - Address stepping is implemented for configuration transactions. The
**                                          ATU inserts 2 clock cycles of address stepping for Conventional Mode and 4 clock cycles 
**						of address stepping for PCI-X mode.
**  06          0 2                         Parity Error Response - When set, the ATU takes normal action when a parity error 
**						is detected. When cleared, parity checking is disabled.
**  05          0 2                         VGA Palette Snoop Enable - The ATU interface does not support I/O writes and therefore, 
**						does not perform VGA palette snooping.
**  04          0 2                         Memory Write and Invalidate Enable - When set, ATU may generate MWI commands. 
**						When clear, ATU use Memory Write commands instead of MWI. Ignored when operating in the PCI-X mode.
**  03          0 2                         Special Cycle Enable - The ATU interface does not respond to special cycle commands in any way. 
**						Not implemented and a reserved bit field.
**  02          0 2                         Bus Master Enable - The ATU interface can act as a master on the PCI bus. 
**						When cleared, disables the device from generating PCI accesses. 
**						When set, allows the device to behave as a PCI bus master.
**                                          When operating in the PCI-X mode, ATU initiates a split completion transaction regardless 
**						of the state of this bit.
**  01          0 2                         Memory Enable - Controls the ATU interface¡¦s response to PCI memory addresses. 
**						When cleared, the ATU interface does not respond to any memory access on the PCI bus.
**  00          0 2                         I/O Space Enable - Controls the ATU interface response to I/O transactions. 
**						Not implemented and a reserved bit field.
***********************************************************************************
*/
#define     ARCMSR_ATU_COMMAND_REG		         0x04    /*word*/
/*
***********************************************************************************
**  ATU Status Register - ATUSR (Sheet 1 of 2)
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  15          0 2                         Detected Parity Error - set when a parity error is detected in data received by the ATU on the PCI bus even
**  					when the ATUCMD register¡¦s Parity Error Response bit is cleared. Set under the following conditions:
**  										¡E Write Data Parity Error when the ATU is a target (inbound write).
**  										¡E Read Data Parity Error when the ATU is a requester (outbound read).
**  										¡E Any Address or Attribute (PCI-X Only) Parity Error on the Bus **	** **  								(including one generated by the ATU).
**  14          0 2                         SERR# Asserted - set when SERR# is asserted on the PCI bus by the ATU.
**  13          0 2                         Master Abort - set when a transaction initiated by the ATU PCI master interface, ends in a Master-Abort
**                                          or when the ATU receives a Master Abort Split Completion Error Message in PCI-X mode.
**  12          0 2                         Target Abort (master) - set when a transaction initiated by the ATU PCI master interface, ends in a target
**                                          abort or when the ATU receives a Target Abort Split Completion Error Message in PCI-X mode.
**  11          0 2                         Target Abort (target) - set when the ATU interface, acting as a target, 
**						terminates the transaction on the PCI bus with a target abort.
**  10:09       01 2                        DEVSEL# Timing - These bits are read-only and define the slowest DEVSEL# 
**						timing for a target device in Conventional PCI Mode regardless of the operating mode 
**							(except configuration accesses).
**  										00 2=Fast
**  										01 2=Medium
**  										10 2=Slow
**  										11 2=Reserved
**                                          The ATU interface uses Medium timing.
**  08           0 2                        Master Parity Error - The ATU interface sets this bit under the following conditions:
**  										¡E The ATU asserted PERR# itself or the ATU observed PERR# asserted.
**  										¡E And the ATU acted as the requester 
**											for the operation in which the error occurred.
**  										¡E And the ATUCMD register¡¦s Parity Error Response bit is set
**  										¡E Or (PCI-X Mode Only) the ATU received a Write Data Parity Error Message
**  										¡E And the ATUCMD register¡¦s Parity Error Response bit is set
**  07           1 2  (Conventional mode)
**               0 2  (PCI-X mode)
**  							Fast Back-to-Back - The ATU/Messaging Unit interface is capable of accepting fast back-to-back
**  							transactions in Conventional PCI mode when the transactions are not to the same target. Since fast
**  							back-to-back transactions do not exist in PCI-X mode, this bit is forced to 0 in the PCI-X mode.
**  06           0 2                        UDF Supported - User Definable Features are not supported
**  05           1 2                        66 MHz. Capable - 66 MHz operation is supported.
**  04           1 2                        Capabilities - When set, this function implements extended capabilities.
**  03             0                        Interrupt Status - reflects the state of the ATU interrupt 
**						when the Interrupt Disable bit in the command register is a 0.
**  										0=ATU interrupt signal deasserted.
**  										1=ATU interrupt signal asserted.
**  		NOTE: Setting the Interrupt Disable bit to a 1 has no effect on the state of this bit. Refer to
**  		Section 3.10.23, ¡§ATU Interrupt Pin Register - ATUIPR¡¨ on page 236 for details on the ATU
**  										interrupt signal.
**  02:00      00000 2                      Reserved.
***********************************************************************************
*/
#define     ARCMSR_ATU_STATUS_REG		         0x06    /*word*/
/*
***********************************************************************************
**  ATU Revision ID Register - ATURID
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07:00        00H                        ATU Revision - identifies the 80331 revision number.
***********************************************************************************
*/
#define     ARCMSR_ATU_REVISION_REG		         0x08    /*byte*/
/*
***********************************************************************************
**  ATU Class Code Register - ATUCCR
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  23:16        05H                        Base Class - Memory Controller
**  15:08        80H                        Sub Class - Other Memory Controller
**  07:00        00H                        Programming Interface - None defined
***********************************************************************************
*/
#define     ARCMSR_ATU_CLASS_CODE_REG		         0x09    /*3bytes 0x0B,0x0A,0x09*/
/*
***********************************************************************************
**  ATU Cacheline Size Register - ATUCLSR
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07:00        00H                        ATU Cacheline Size - specifies the system cacheline size in DWORDs. Cacheline size is restricted to either 0, 8 or 16 DWORDs.
***********************************************************************************
*/
#define     ARCMSR_ATU_CACHELINE_SIZE_REG		         0x0C    /*byte*/
/*
***********************************************************************************
**  ATU Latency Timer Register - ATULT
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07:03     00000 2   (for Conventional mode)
**            01000 2   (for PCI-X mode)
**  			Programmable Latency Timer - This field varies the latency timer for the interface from 0 to 248 clocks.
**  			The default value is 0 clocks for Conventional PCI mode, and 64 clocks for PCI-X mode.
**  02:00       000 2   Latency Timer Granularity - These Bits are read only giving a programmable granularity of 8 clocks for the latency timer.
***********************************************************************************
*/
#define     ARCMSR_ATU_LATENCY_TIMER_REG		         0x0D    /*byte*/
/*
***********************************************************************************
**  ATU Header Type Register - ATUHTR
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07           0 2                        Single Function/Multi-Function Device - Identifies the 80331 as a single-function PCI device.
**  06:00   000000 2                        PCI Header Type - This bit field indicates the type of PCI header implemented. The ATU interface
**                                          header conforms to PCI Local Bus Specification, Revision 2.3.
***********************************************************************************
*/
#define     ARCMSR_ATU_HEADER_TYPE_REG		         0x0E    /*byte*/
/*
***********************************************************************************
**  ATU BIST Register - ATUBISTR
**
**  The ATU BIST Register controls the functions the Intel XScale core performs when BIST is
**  initiated. This register is the interface between the host processor requesting BIST functions and
**  the 80331 replying with the results from the software implementation of the BIST functionality.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07           0 2                        BIST Capable - This bit value is always equal to the ATUCR ATU BIST Interrupt Enable bit. 
**  06           0 2                        Start BIST - When the ATUCR BIST Interrupt Enable bit is set:
**  				 Setting this bit generates an interrupt to the Intel XScale core to perform a software BIST function.
**  				 The Intel XScale core clears this bit when the BIST software has completed with the BIST results
**  				 found in ATUBISTR register bits [3:0].
**  				 When the ATUCR BIST Interrupt Enable bit is clear:
**  				 Setting this bit does not generate an interrupt to the Intel XScale core and no BIST functions is performed. 
**                                                       The Intel XScale core does not clear this bit.
**  05:04       00 2             Reserved
**  03:00     0000 2             BIST Completion Code - when the ATUCR BIST Interrupt Enable bit is set and the ATUBISTR Start BIST bit is set (bit 6):
**                               The Intel XScale  core places the results of the software BIST in these bits. 
**				 A nonzero value indicates a device-specific error.
***********************************************************************************
*/
#define     ARCMSR_ATU_BIST_REG		         0x0F    /*byte*/

/*
***************************************************************************************  
**            ATU Base Registers and Associated Limit Registers
***************************************************************************************
**           Base Address                         Register Limit                          Register Description
**  Inbound ATU Base Address Register 0           Inbound ATU Limit Register 0            Defines the inbound translation window 0 from the PCI bus.
**  Inbound ATU Upper Base Address Register 0     N/A                                     Together with ATU Base Address Register 0 defines the inbound **								translation window 0 from the PCI bus for DACs.
**  Inbound ATU Base Address Register 1           Inbound ATU Limit Register 1            Defines inbound window 1 from the PCI bus.
**  Inbound ATU Upper Base Address Register 1     N/A                                     Together with ATU Base Address Register 1 defines inbound window **  1 from the PCI bus for DACs.
**  Inbound ATU Base Address Register 2           Inbound ATU Limit Register 2            Defines the inbound translation window 2 from the PCI bus.
**  Inbound ATU Upper Base Address Register 2     N/A                                     Together with ATU Base Address Register 2 defines the inbound ** **  translation window 2 from the PCI bus for DACs.
**  Inbound ATU Base Address Register 3           Inbound ATU Limit Register 3            Defines the inbound translation window 3 from the PCI bus.
**  Inbound ATU Upper Base Address Register 3     N/A                                     Together with ATU Base Address Register 3 defines the inbound ** **  translation window 3 from the PCI bus for DACs.
**     NOTE: This is a private BAR that resides outside of the standard PCI configuration header space (offsets 00H-3FH).
**  Expansion ROM Base Address Register           Expansion ROM Limit Register            Defines the window of addresses used by a bus master for reading **  from an Expansion ROM.
**--------------------------------------------------------------------------------------
**  ATU Inbound Window 1 is not a translate window. 
**  The ATU does not claim any PCI accesses that fall within this range. 
**  This window is used to allocate host memory for use by Private Devices. 
**  When enabled, the ATU interrupts the Intel  XScale core when either the IABAR1 register or the IAUBAR1 register is written from the PCI bus. 
***********************************************************************************
*/

/*
***********************************************************************************
**  Inbound ATU Base Address Register 0 - IABAR0
**
**  . The Inbound ATU Base Address Register 0 (IABAR0) together with the Inbound ATU Upper Base Address Register 0 (IAUBAR0) 
**    defines the block of memory addresses where the inbound translation window 0 begins. 
**  . The inbound ATU decodes and forwards the bus request to the 80331 internal bus with a translated address to map into 80331 local memory. 
**  . The IABAR0 and IAUBAR0 define the base address and describes the required memory block size.
**  . Bits 31 through 12 of the IABAR0 is either read/write bits or read only with a value of 0 
**    depending on the value located within the IALR0. 
**    This configuration allows the IABAR0 to be programmed per PCI Local Bus Specification.
**    The first 4 Kbytes of memory defined by the IABAR0, IAUBAR0 and the IALR0 is reserved for the Messaging Unit.
**    The programmed value within the base address register must comply with the PCI programming requirements for address alignment. 
**  Warning: 
**    When IALR0 is cleared prior to host configuration:
**                          the user should also clear the Prefetchable Indicator and the Type Indicator. 
**    Assuming IALR0 is not cleared:
**                          a. Since non prefetchable memory windows can never be placed above the 4 Gbyte address boundary,
**                             when the Prefetchable Indicator is cleared prior to host configuration, 
**                             the user should also set the Type Indicator for 32 bit addressability.
**                          b. For compliance to the PCI-X Addendum to the PCI Local Bus Specification,
**                             when the Prefetchable Indicator is set prior to host configuration, the user
**                             should also set the Type Indicator for 64 bit addressability. 
**                             This is the default for IABAR0.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     00000H                        Translation Base Address 0 - These bits define the actual location 
**						the translation function is to respond to when addressed from the PCI bus.
**  11:04        00H                        Reserved.
**  03           1 2                        Prefetchable Indicator - When set, defines the memory space as prefetchable.
**  02:01       10 2                        Type Indicator - Defines the width of the addressability for this memory window:
**  						00 - Memory Window is locatable anywhere in 32 bit address space
**  						10 - Memory Window is locatable anywhere in 64 bit address space
**  00           0 2                        Memory Space Indicator - This bit field describes memory or I/O space base address. 
**                                                                   The ATU does not occupy I/O space, 
**                                                                   thus this bit must be zero.
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_BASE_ADDRESS0_REG		         0x10    /*dword 0x13,0x12,0x11,0x10*/
#define     ARCMSR_INBOUND_ATU_MEMORY_PREFETCHABLE	                 0x08
#define     ARCMSR_INBOUND_ATU_MEMORY_WINDOW64		                 0x04
/*
***********************************************************************************
**  Inbound ATU Upper Base Address Register 0 - IAUBAR0
**
**  This register contains the upper base address when decoding PCI addresses beyond 4 GBytes.
**  Together with the Translation Base Address this register defines the actual location the translation
**  function is to respond to when addressed from the PCI bus for addresses > 4GBytes (for DACs).
**  The programmed value within the base address register must comply with the PCI programming requirements for address alignment. 
**  Note: 
**      When the Type indicator of IABAR0 is set to indicate 32 bit addressability, 
**      the IAUBAR0 register attributes are read-only.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:0      00000H                        Translation Upper Base Address 0 - Together with the Translation Base Address 0 these bits define the
**                           actual location the translation function is to respond to when addressed from the PCI bus for addresses > 4GBytes.
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_UPPER_BASE_ADDRESS0_REG		     0x14    /*dword 0x17,0x16,0x15,0x14*/
/*
***********************************************************************************
**  Inbound ATU Base Address Register 1 - IABAR1
**
**  . The Inbound ATU Base Address Register (IABAR1) together with the Inbound ATU Upper Base Address Register 1 (IAUBAR1) 
**    defines the block of memory addresses where the inbound translation window 1 begins. 
**  . This window is used merely to allocate memory on the PCI bus and, the ATU does not process any PCI bus transactions to this memory range.
**  . The programmed value within the base address register must comply with the PCI programming requirements for address alignment. 
**  . When enabled, the ATU interrupts the Intel XScale core when the IABAR1 register is written from the PCI bus. 
**    Warning: 
**    When a non-zero value is not written to IALR1 prior to host configuration, 
**                          the user should not set either the Prefetchable Indicator or the Type Indicator for 64 bit addressability. 
**                          This is the default for IABAR1.
**    Assuming a non-zero value is written to IALR1, 
**               			the user may set the Prefetchable Indicator 
**               			              or the Type         Indicator:
**  						a. Since non prefetchable memory windows can never be placed above the 4 Gbyte address
**  						   boundary, when the Prefetchable Indicator is not set prior to host configuration, 
**                             the user should also leave the Type Indicator set for 32 bit addressability. 
**                             This is the default for IABAR1.
**  						b. when the Prefetchable Indicator is set prior to host configuration, 
**                             the user should also set the Type Indicator for 64 bit addressability.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     00000H                        Translation Base Address 1 - These bits define the actual location of window 1 on the PCI bus.
**  11:04        00H                        Reserved.
**  03           0 2                        Prefetchable Indicator - When set, defines the memory space as prefetchable.
**  02:01       00 2                        Type Indicator - Defines the width of the addressability for this memory window:
**  			00 - Memory Window is locatable anywhere in 32 bit address space
**  			10 - Memory Window is locatable anywhere in 64 bit address space
**  00           0 2                        Memory Space Indicator - This bit field describes memory or I/O space base address. 
**                                                                   The ATU does not occupy I/O space, 
**                                                                   thus this bit must be zero.
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_BASE_ADDRESS1_REG		         0x18    /*dword 0x1B,0x1A,0x19,0x18*/
/*
***********************************************************************************
**  Inbound ATU Upper Base Address Register 1 - IAUBAR1
**
**  This register contains the upper base address when locating this window for PCI addresses beyond 4 GBytes. 
**  Together with the IABAR1 this register defines the actual location for this memory window for addresses > 4GBytes (for DACs). 
**  This window is used merely to allocate memory on the PCI bus and, the ATU does not process any PCI bus transactions to this memory range.
**  The programmed value within the base address register must comply with the PCI programming
**  requirements for address alignment. 
**  When enabled, the ATU interrupts the Intel XScale core when the IAUBAR1 register is written
**  from the PCI bus. 
**  Note: 
**      When the Type indicator of IABAR1 is set to indicate 32 bit addressability, 
**      the IAUBAR1 register attributes are read-only. 
**      This is the default for IABAR1.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:0      00000H                        Translation Upper Base Address 1 - Together with the Translation Base Address 1 
**						these bits define the actual location for this memory window on the PCI bus for addresses > 4GBytes.
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_UPPER_BASE_ADDRESS1_REG		         0x1C    /*dword 0x1F,0x1E,0x1D,0x1C*/
/*
***********************************************************************************
**  Inbound ATU Base Address Register 2 - IABAR2
**
**  . The Inbound ATU Base Address Register 2 (IABAR2) together with the Inbound ATU Upper Base Address Register 2 (IAUBAR2) 
**           defines the block of memory addresses where the inbound translation window 2 begins. 
**  . The inbound ATU decodes and forwards the bus request to the 80331 internal bus with a translated address to map into 80331 local memory. 
**  . The IABAR2 and IAUBAR2 define the base address and describes the required memory block size
**  . Bits 31 through 12 of the IABAR2 is either read/write bits or read only with a value of 0 depending on the value located within the IALR2.
**    The programmed value within the base address register must comply with the PCI programming requirements for address alignment. 
**  Warning: 
**    When a non-zero value is not written to IALR2 prior to host configuration, 
**                          the user should not set either the Prefetchable Indicator 
**                                                      or the Type         Indicator for 64 bit addressability. 
**                          This is the default for IABAR2.
**  Assuming a non-zero value is written to IALR2, 
**                          the user may set the Prefetchable Indicator 
**                                        or the Type         Indicator:
**  						a. Since non prefetchable memory windows can never be placed above the 4 Gbyte address boundary, 
**                             when the Prefetchable Indicator is not set prior to host configuration, 
**                             the user should also leave the Type Indicator set for 32 bit addressability. 
**                             This is the default for IABAR2.
**  						b. when the Prefetchable Indicator is set prior to host configuration, 
**                             the user should also set the Type Indicator for 64 bit addressability.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     00000H                        Translation Base Address 2 - These bits define the actual location 
**						the translation function is to respond to when addressed from the PCI bus.
**  11:04        00H                        Reserved.
**  03           0 2                        Prefetchable Indicator - When set, defines the memory space as prefetchable.
**  02:01       00 2                        Type Indicator - Defines the width of the addressability for this memory window:
**  			00 - Memory Window is locatable anywhere in 32 bit address space
**  			10 - Memory Window is locatable anywhere in 64 bit address space
**  00           0 2                        Memory Space Indicator - This bit field describes memory or I/O space base address. 
**                                                                   The ATU does not occupy I/O space, 
**                                                                   thus this bit must be zero.
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_BASE_ADDRESS2_REG		         0x20    /*dword 0x23,0x22,0x21,0x20*/
/*
***********************************************************************************
**  Inbound ATU Upper Base Address Register 2 - IAUBAR2
**
**  This register contains the upper base address when decoding PCI addresses beyond 4 GBytes.
**  Together with the Translation Base Address this register defines the actual location 
**  the translation function is to respond to when addressed from the PCI bus for addresses > 4GBytes (for DACs).
**  The programmed value within the base address register must comply with the PCI programming
**  requirements for address alignment.
**  Note: 
**      When the Type indicator of IABAR2 is set to indicate 32 bit addressability,
**      the IAUBAR2 register attributes are read-only. 
**      This is the default for IABAR2.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:0      00000H                        Translation Upper Base Address 2 - Together with the Translation Base Address 2 
**                                          these bits define the actual location the translation function is to respond to 
**                                          when addressed from the PCI bus for addresses > 4GBytes.
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_UPPER_BASE_ADDRESS2_REG		         0x24    /*dword 0x27,0x26,0x25,0x24*/
/*
***********************************************************************************
**  ATU Subsystem Vendor ID Register - ASVIR
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  15:0      0000H                         Subsystem Vendor ID - This register uniquely identifies the add-in board or subsystem vendor.
***********************************************************************************
*/
#define     ARCMSR_ATU_SUBSYSTEM_VENDOR_ID_REG		         0x2C    /*word 0x2D,0x2C*/
/*
***********************************************************************************
**  ATU Subsystem ID Register - ASIR
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  15:0      0000H                         Subsystem ID - uniquely identifies the add-in board or subsystem.
***********************************************************************************
*/
#define     ARCMSR_ATU_SUBSYSTEM_ID_REG		         0x2E    /*word 0x2F,0x2E*/
/*
***********************************************************************************
**  Expansion ROM Base Address Register -ERBAR
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     00000H                        Expansion ROM Base Address - These bits define the actual location 
**						where the Expansion ROM address window resides when addressed from the PCI bus on any 4 Kbyte boundary.
**  11:01     000H                          Reserved
**  00        0 2                           Address Decode Enable - This bit field shows the ROM address 
**						decoder is enabled or disabled. When cleared, indicates the address decoder is disabled.
***********************************************************************************
*/
#define     ARCMSR_EXPANSION_ROM_BASE_ADDRESS_REG		         0x30    /*dword 0x33,0x32,0v31,0x30*/
#define     ARCMSR_EXPANSION_ROM_ADDRESS_DECODE_ENABLE   		     0x01    
/*
***********************************************************************************
**  ATU Capabilities Pointer Register - ATU_CAP_PTR
**  -----------------------------------------------------------------
**  Bit Default Description
**  07:00     C0H                           Capability List Pointer - This provides an offset in this function¡¦s configuration space 
**						that points to the 80331 PCl Bus Power Management extended capability.
***********************************************************************************
*/
#define     ARCMSR_ATU_CAPABILITY_PTR_REG		     0x34    /*byte*/
/*
***********************************************************************************  
**  Determining Block Sizes for Base Address Registers
**  The required address size and type can be determined by writing ones to a base address register and
**  reading from the registers. By scanning the returned value from the least-significant bit of the base
**  address registers upwards, the programmer can determine the required address space size. The
**  binary-weighted value of the first non-zero bit found indicates the required amount of space.
**  Table 105 describes the relationship between the values read back and the byte sizes the base
**  address register requires.
**  As an example, assume that FFFF.FFFFH is written to the ATU Inbound Base Address Register 0
**  (IABAR0) and the value read back is FFF0.0008H. Bit zero is a zero, so the device requires
**  memory address space. Bit three is one, so the memory does supports prefetching. Scanning
**  upwards starting at bit four, bit twenty is the first one bit found. The binary-weighted value of this
**  bit is 1,048,576, indicated that the device requires 1 Mbyte of memory space.
**  The ATU Base Address Registers and the Expansion ROM Base Address Register use their
**  associated limit registers to enable which bits within the base address register are read/write and
**  which bits are read only (0). This allows the programming of these registers in a manner similar to
**  other PCI devices even though the limit is variable.
**  Table 105. Memory Block Size Read Response
**  Response After Writing all 1s
**  to the Base Address Register
**  Size
**  (Bytes)
**  Response After Writing all 1s
**  to the Base Address Register
**  Size
**  (Bytes)
**  FFFFFFF0H 16 FFF00000H 1 M
**  FFFFFFE0H 32 FFE00000H 2 M
**  FFFFFFC0H 64 FFC00000H 4 M
**  FFFFFF80H 128 FF800000H 8 M
**  FFFFFF00H 256 FF000000H 16 M
**  FFFFFE00H 512 FE000000H 32 M
**  FFFFFC00H 1K FC000000H 64 M
**  FFFFF800H 2K F8000000H 128 M
**  FFFFF000H 4K F0000000H 256 M
**  FFFFE000H 8K E0000000H 512 M
**  FFFFC000H 16K C0000000H 1 G
**  FFFF8000H 32K 80000000H 2 G
**  FFFF0000H 64K
**  00000000H
**  Register not
**  imple-mented,
**  no
**  address
**  space
**  required.
**  FFFE0000H 128K
**  FFFC0000H 256K
**  FFF80000H 512K
**
***************************************************************************************  
*/



/*
***********************************************************************************
**  ATU Interrupt Line Register - ATUILR
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07:00       FFH                         Interrupt Assigned - system-assigned value identifies which system interrupt controller¡¦s interrupt
**                                                               request line connects to the device's PCI interrupt request lines 
**								(as specified in the interrupt pin register).
**                                                               A value of FFH signifies ¡§no connection¡¨ or ¡§unknown¡¨.
***********************************************************************************
*/
#define     ARCMSR_ATU_INTERRUPT_LINE_REG		     0x3C    /*byte*/
/*
***********************************************************************************
**  ATU Interrupt Pin Register - ATUIPR
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07:00       01H                         Interrupt Used - A value of 01H signifies that the ATU interface unit uses INTA# as the interrupt pin.
***********************************************************************************
*/
#define     ARCMSR_ATU_INTERRUPT_PIN_REG		     0x3D    /*byte*/
/*
***********************************************************************************
**  ATU Minimum Grant Register - ATUMGNT
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07:00       80H                         This register specifies how long a burst period the device needs in increments of 8 PCI clocks.
***********************************************************************************
*/
#define     ARCMSR_ATU_MINIMUM_GRANT_REG		     0x3E    /*byte*/
/*
***********************************************************************************
**  ATU Maximum Latency Register - ATUMLAT
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07:00       00H                         Specifies frequency (how often) the device needs to access the PCI bus 
**						in increments of 8 PCI clocks. A zero value indicates the device has no stringent requirement.
***********************************************************************************
*/
#define     ARCMSR_ATU_MAXIMUM_LATENCY_REG		     0x3F    /*byte*/
/*
***********************************************************************************
**  Inbound Address Translation
**  
**  The ATU allows external PCI bus initiators to directly access the internal bus. 
**  These PCI bus initiators can read or write 80331 memory-mapped registers or 80331 local memory space. 
**  The process of inbound address translation involves two steps:
**  1. Address Detection.
**             ¡E Determine when the 32-bit PCI address (64-bit PCI address during DACs) is
**                within the address windows defined for the inbound ATU.
**             ¡E Claim the PCI transaction with medium DEVSEL# timing in the conventional PCI
**                mode and with Decode A DEVSEL# timing in the PCI-X mode.
**  2. Address Translation.
**             ¡E Translate the 32-bit PCI address (lower 32-bit PCI address during DACs) to a 32-bit 80331 internal bus address.
**  				The ATU uses the following registers in inbound address window 0 translation:
**  				¡E Inbound ATU Base Address Register 0
**  				¡E Inbound ATU Limit Register 0
**  				¡E Inbound ATU Translate Value Register 0
**  				The ATU uses the following registers in inbound address window 2 translation:
**  				¡E Inbound ATU Base Address Register 2
**  				¡E Inbound ATU Limit Register 2
**  				¡E Inbound ATU Translate Value Register 2
**  				The ATU uses the following registers in inbound address window 3 translation:
**  				¡E Inbound ATU Base Address Register 3
**  				¡E Inbound ATU Limit Register 3
**  				¡E Inbound ATU Translate Value Register 3
**    Note: Inbound Address window 1 is not a translate window. 
**          Instead, window 1 may be used to allocate host memory for Private Devices.
**          Inbound Address window 3 does not reside in the standard section of the configuration header (offsets 00H - 3CH), 
**          thus the host BIOS does not configure window 3.
**          Window 3 is intended to be used as a special window into local memory for private PCI
**          agents controlled by the 80331 in conjunction with the Private Memory Space of the bridge.
**          PCI-to-PCI Bridge in 80331 or
**          Inbound address detection is determined from the 32-bit PCI address, 
**          (64-bit PCI address during DACs) the base address register and the limit register. 
**          In the case of DACs none of the upper 32-bits of the address is masked during address comparison. 
**  
**  The algorithm for detection is:
**  
**  Equation 1. Inbound Address Detection
**              When (PCI_Address [31:0] & Limit_Register[31:0]) == (Base_Register[31:0] & PCI_Address [63:32]) == Base_Register[63:32] (for DACs only) 
**              the PCI Address is claimed by the Inbound ATU.
**  
**  			The incoming 32-bit PCI address (lower 32-bits of the address in case of DACs) is bitwise ANDed
**  			with the associated inbound limit register. 
**              When the result matches the base register (and upper base address matches upper PCI address in case of DACs), 
**              the inbound PCI address is detected as being within the inbound translation window and is claimed by the ATU.
**
**  			Note:   The first 4 Kbytes of the ATU inbound address translation window 0 are reserved for the Messaging Unit. 
**  					Once the transaction is claimed, the address must be translated from a PCI address to a 32-bit
**  					internal bus address. In case of DACs upper 32-bits of the address is simply discarded and only the
**  					lower 32-bits are used during address translation. 
**              		The algorithm is:
**  
**  
**  Equation 2. Inbound Translation
**              Intel I/O processor Internal Bus Address=(PCI_Address[31:0] & ~Limit_Register[31:0]) | ATU_Translate_Value_Register[31:0].
**  
**  			The incoming 32-bit PCI address (lower 32-bits in case of DACs) is first bitwise ANDed with the
**  			bitwise inverse of the limit register. This result is bitwise ORed with the ATU Translate Value and
**  			the result is the internal bus address. This translation mechanism is used for all inbound memory
**  			read and write commands excluding inbound configuration read and writes. 
**  			In the PCI mode for inbound memory transactions, the only burst order supported is Linear
**  			Incrementing. For any other burst order, the ATU signals a Disconnect after the first data phase.
**  			The PCI-X supports linear incrementing only, and hence above situation is not encountered in the PCI-X mode.
**  example:
**  	    Register Values
**  		         Base_Register=3A00 0000H
**  		        Limit_Register=FF80 0000H (8 Mbyte limit value)
**  		        Value_Register=B100 0000H
**  		        Inbound Translation Window ranges from 3A00 0000H to 3A7F FFFFH (8 Mbytes)
**  		
**  		Address Detection (32-bit address)
**
**  						PCI_Address & Limit_Register == Base_Register
**  						3A45 012CH  &   FF80 0000H   ==  3A00 0000H
**
**  					ANS: PCI_Address is in the Inbound Translation Window
**  		Address Translation (to get internal bus address)
**
**  						IB_Address=(PCI_Address & ~Limit_Register) | Value_Reg
**  						IB_Address=(3A45 012CH & 007F FFFFH) | B100 0000H
**
**  					ANS:IB_Address=B145 012CH
***********************************************************************************
*/



/*
***********************************************************************************
**  Inbound ATU Limit Register 0 - IALR0
**
**  Inbound address translation for memory window 0 occurs for data transfers occurring from the PCI
**  bus (originated from the PCI bus) to the 80331 internal bus. The address translation block converts
**  PCI addresses to internal bus addresses.
**  The 80331 translate value register¡¦s programmed value must be naturally aligned with the base
**  address register¡¦s programmed value. The limit register is used as a mask; thus, the lower address
**  bits programmed into the 80331 translate value register are invalid. Refer to the PCI Local Bus
**  Specification, Revision 2.3 for additional information on programming base address registers.
**  Bits 31 to 12 within the IALR0 have a direct effect on the IABAR0 register, bits 31 to 12, with a
**  one to one correspondence. A value of 0 in a bit within the IALR0 makes the corresponding bit
**  within the IABAR0 a read only bit which always returns 0. A value of 1 in a bit within the IALR0
**  makes the corresponding bit within the IABAR0 read/write from PCI. Note that a consequence of
**  this programming scheme is that unless a valid value exists within the IALR0, all writes to the
**  IABAR0 has no effect since a value of all zeros within the IALR0 makes the IABAR0 a read only  register.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     FF000H                        Inbound Translation Limit 0 - This readback value determines the memory block size required for
**                                          inbound memory window 0 of the address translation unit. This defaults to an inbound window of 16MB.
**  11:00       000H                        Reserved
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_LIMIT0_REG		     0x40    /*dword 0x43,0x42,0x41,0x40*/
/*
***********************************************************************************
**  Inbound ATU Translate Value Register 0 - IATVR0
**
**  The Inbound ATU Translate Value Register 0 (IATVR0) contains the internal bus address used to
**  convert PCI bus addresses. The converted address is driven on the internal bus as a result of the
**  inbound ATU address translation.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     FF000H                        Inbound ATU Translation Value 0 - This value is used to convert the PCI address to internal bus addresses. 
**                                          This value must be 64-bit aligned on the internal bus. 
**						The default address allows the ATU to access the internal 80331 memory-mapped registers.
**  11:00       000H                        Reserved
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_TRANSLATE_VALUE0_REG		     0x44    /*dword 0x47,0x46,0x45,0x44*/
/*
***********************************************************************************
**  Expansion ROM Limit Register - ERLR
**
**  The Expansion ROM Limit Register (ERLR) defines the block size of addresses the ATU defines
**  as Expansion ROM address space. The block size is programmed by writing a value into the ERLR.
**  Bits 31 to 12 within the ERLR have a direct effect on the ERBAR register, bits 31 to 12, with a one
**  to one correspondence. A value of 0 in a bit within the ERLR makes the corresponding bit within
**  the ERBAR a read only bit which always returns 0. A value of 1 in a bit within the ERLR makes
**  the corresponding bit within the ERBAR read/write from PCI.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     000000H                       Expansion ROM Limit - Block size of memory required for the Expansion ROM translation unit. Default
**                         value is 0, which indicates no Expansion ROM address space and all bits within the ERBAR are read only with a value of 0.
**  11:00        000H                       Reserved.
***********************************************************************************
*/
#define     ARCMSR_EXPANSION_ROM_LIMIT_REG		          0x48    /*dword 0x4B,0x4A,0x49,0x48*/
/*
***********************************************************************************
**  Expansion ROM Translate Value Register - ERTVR
**
**  The Expansion ROM Translate Value Register contains the 80331 internal bus address which the
**  ATU converts the PCI bus access. This address is driven on the internal bus as a result of the
**  Expansion ROM address translation.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     00000H                        Expansion ROM Translation Value - Used to convert PCI addresses to 80331 internal bus addresses
**                          for Expansion ROM accesses. The Expansion ROM address translation value must be word aligned on the internal bus.
**  11:00       000H                        Reserved
***********************************************************************************
*/
#define     ARCMSR_EXPANSION_ROM_TRANSLATE_VALUE_REG		          0x4C    /*dword 0x4F,0x4E,0x4D,0x4C*/
/*
***********************************************************************************
**  Inbound ATU Limit Register 1 - IALR1
**
**  Bits 31 to 12 within the IALR1 have a direct effect on the IABAR1 register, bits 31 to 12, with a
**  one to one correspondence. A value of 0 in a bit within the IALR1 makes the corresponding bit
**  within the IABAR1 a read only bit which always returns 0. A value of 1 in a bit within the IALR1
**  makes the corresponding bit within the IABAR1 read/write from PCI. Note that a consequence of
**  this programming scheme is that unless a valid value exists within the IALR1, all writes to the
**  IABAR1 has no effect since a value of all zeros within the IALR1 makes the IABAR1 a read only
**  register.
**  The inbound memory window 1 is used merely to allocate memory on the PCI bus. The ATU does
**  not process any PCI bus transactions to this memory range.
**  Warning: The ATU does not claim any PCI accesses that fall within the range defined by IABAR1,
**  IAUBAR1, and IALR1.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     00000H                        Inbound Translation Limit 1 - This readback value determines the memory block size 
**						required for the ATUs memory window 1.
**  11:00 000H Reserved
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_LIMIT1_REG		          0x50    /*dword 0x53,0x52,0x51,0x50*/
/*
***********************************************************************************
**  Inbound ATU Limit Register 2 - IALR2
**  
**  Inbound address translation for memory window 2 occurs for data transfers occurring from the PCI
**  bus (originated from the PCI bus) to the 80331 internal bus. The address translation block converts
**  PCI addresses to internal bus addresses.
**  The inbound translation base address for inbound window 2 is specified in Section 3.10.15. When
**  determining block size requirements ¡X as described in Section 3.10.21 ¡X the translation limit
**  register provides the block size requirements for the base address register. The remaining registers
**  used for performing address translation are discussed in Section 3.2.1.1.
**  The 80331 translate value register¡¦s programmed value must be naturally aligned with the base
**  address register¡¦s programmed value. The limit register is used as a mask; thus, the lower address
**  bits programmed into the 80331 translate value register are invalid. Refer to the PCI Local Bus
**  Specification, Revision 2.3 for additional information on programming base address registers.
**  Bits 31 to 12 within the IALR2 have a direct effect on the IABAR2 register, bits 31 to 12, with a
**  one to one correspondence. A value of 0 in a bit within the IALR2 makes the corresponding bit
**  within the IABAR2 a read only bit which always returns 0. A value of 1 in a bit within the IALR2
**  makes the corresponding bit within the IABAR2 read/write from PCI. Note that a consequence of
**  this programming scheme is that unless a valid value exists within the IALR2, all writes to the
**  IABAR2 has no effect since a value of all zeros within the IALR2 makes the IABAR2 a read only
**  register.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     00000H                        Inbound Translation Limit 2 - This readback value determines the memory block size 
**						required for the ATUs memory window 2.
**  11:00       000H                        Reserved
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_LIMIT2_REG		          0x54    /*dword 0x57,0x56,0x55,0x54*/
/*
***********************************************************************************
**  Inbound ATU Translate Value Register 2 - IATVR2
**
**  The Inbound ATU Translate Value Register 2 (IATVR2) contains the internal bus address used to
**  convert PCI bus addresses. The converted address is driven on the internal bus as a result of the
**  inbound ATU address translation.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     00000H                        Inbound ATU Translation Value 2 - This value is used to convert the PCI address to internal bus addresses. 
**                                                                            This value must be 64-bit aligned on the internal bus. 
**										The default address allows the ATU to access the internal 80331 **	**										memory-mapped registers.
**  11:00       000H                        Reserved
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_TRANSLATE_VALUE2_REG		          0x58    /*dword 0x5B,0x5A,0x59,0x58*/
/*
***********************************************************************************
**  Outbound I/O Window Translate Value Register - OIOWTVR
**
**  The Outbound I/O Window Translate Value Register (OIOWTVR) contains the PCI I/O address
**  used to convert the internal bus access to a PCI address. This address is driven on the PCI bus as a
**  result of the outbound ATU address translation. 
**  The I/O window is from 80331 internal bus address 9000 000H to 9000 FFFFH with the fixed
**  length of 64 Kbytes.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:16     0000H                         Outbound I/O Window Translate Value - Used to convert internal bus addresses to PCI addresses.
**  15:00     0000H                         Reserved
***********************************************************************************
*/
#define     ARCMSR_OUTBOUND_IO_WINDOW_TRANSLATE_VALUE_REG		          0x5C    /*dword 0x5F,0x5E,0x5D,0x5C*/
/*
***********************************************************************************
**  Outbound Memory Window Translate Value Register 0 -OMWTVR0
**
**  The Outbound Memory Window Translate Value Register 0 (OMWTVR0) contains the PCI
**  address used to convert 80331 internal bus addresses for outbound transactions. This address is
**  driven on the PCI bus as a result of the outbound ATU address translation. 
**  The memory window is from internal bus address 8000 000H to 83FF FFFFH with the fixed length
**  of 64 Mbytes.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:26       00H                         Outbound MW Translate Value - Used to convert 80331 internal bus addresses to PCI addresses.
**  25:02     00 0000H                      Reserved
**  01:00      00 2                         Burst Order - This bit field shows the address sequence during a memory burst. 
**								Only linear incrementing mode is supported.
***********************************************************************************
*/
#define     ARCMSR_OUTBOUND_MEMORY_WINDOW_TRANSLATE_VALUE0_REG		          0x60    /*dword 0x63,0x62,0x61,0x60*/
/*
***********************************************************************************
**  Outbound Upper 32-bit Memory Window Translate Value Register 0 - OUMWTVR0
**
**  The Outbound Upper 32-bit Memory Window Translate Value Register 0 (OUMWTVR0) defines
**  the upper 32-bits of address used during a dual address cycle. This enables the outbound ATU to
**  directly address anywhere within the 64-bit host address space. When this register is all-zero, then
**  a SAC is generated on the PCI bus.
**  The memory window is from internal bus address 8000 000H to 83FF FFFFH with the fixed
**  length of 64 Mbytes.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:00     0000 0000H                    These bits define the upper 32-bits of address driven during the dual address cycle (DAC).
***********************************************************************************
*/
#define     ARCMSR_OUTBOUND_UPPER32_MEMORY_WINDOW_TRANSLATE_VALUE0_REG		          0x64    /*dword 0x67,0x66,0x65,0x64*/
/*
***********************************************************************************
**  Outbound Memory Window Translate Value Register 1 -OMWTVR1
**
**  The Outbound Memory Window Translate Value Register 1 (OMWTVR1) contains the PCI
**  address used to convert 80331 internal bus addresses for outbound transactions. This address is
**  driven on the PCI bus as a result of the outbound ATU address translation. 
**  The memory window is from internal bus address 8400 000H to 87FF FFFFH with the fixed length
**  of 64 Mbytes.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:26       00H                         Outbound MW Translate Value - Used to convert 80331 internal bus addresses to PCI addresses.
**  25:02     00 0000H                      Reserved
**  01:00       00 2                        Burst Order - This bit field shows the address sequence during a memory burst. 
**						Only linear incrementing mode is supported.
***********************************************************************************
*/
#define     ARCMSR_OUTBOUND_MEMORY_WINDOW_TRANSLATE_VALUE1_REG		          0x68    /*dword 0x6B,0x6A,0x69,0x68*/
/*
***********************************************************************************
**  Outbound Upper 32-bit Memory Window Translate Value Register 1 - OUMWTVR1
**
**  The Outbound Upper 32-bit Memory Window Translate Value Register 1 (OUMWTVR1) defines
**  the upper 32-bits of address used during a dual address cycle. This enables the outbound ATU to
**  directly address anywhere within the 64-bit host address space. When this register is all-zero, then
**  a SAC is generated on the PCI bus.
**  The memory window is from internal bus address 8400 000H to 87FF FFFFH with the fixed length
**  of 64 Mbytes.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:00    0000 0000H                     These bits define the upper 32-bits of address driven during the dual address cycle (DAC).
***********************************************************************************
*/
#define     ARCMSR_OUTBOUND_UPPER32_MEMORY_WINDOW_TRANSLATE_VALUE1_REG		          0x6C    /*dword 0x6F,0x6E,0x6D,0x6C*/
/*
***********************************************************************************
**  Outbound Upper 32-bit Direct Window Translate Value Register - OUDWTVR
**
**  The Outbound Upper 32-bit Direct Window Translate Value Register (OUDWTVR) defines the
**  upper 32-bits of address used during a dual address cycle for the transactions via Direct Addressing
**  Window. This enables the outbound ATU to directly address anywhere within the 64-bit host
**  address space. When this register is all-zero, then a SAC is generated on the PCI bus.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:00    0000 0000H                     These bits define the upper 32-bits of address driven during the dual address cycle (DAC).
***********************************************************************************
*/
#define     ARCMSR_OUTBOUND_UPPER32_DIRECT_WINDOW_TRANSLATE_VALUE_REG		          0x78    /*dword 0x7B,0x7A,0x79,0x78*/
/*
***********************************************************************************
**  ATU Configuration Register - ATUCR
**
**  The ATU Configuration Register controls the outbound address translation for address translation
**  unit. It also contains bits for Conventional PCI Delayed Read Command (DRC) aliasing, discard
**  timer status, SERR# manual assertion, SERR# detection interrupt masking, and ATU BIST
**  interrupt enabling.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:20       00H                         Reserved
**  19          0 2                         ATU DRC Alias - when set, the ATU does not distinguish read commands when attempting to match a
**  			current PCI read transaction with read data enqueued within the DRC buffer. When clear, a current read
**  			transaction must have the exact same read command as the DRR for the ATU to deliver DRC data. Not
**  			applicable in the PCI-X mode.
**  18          0 2                         Direct Addressing Upper 2Gbytes Translation Enable - When set, 
**						with Direct Addressing enabled (bit 7 of the ATUCR set), 
**							the ATU forwards internal bus cycles with an address between 0000.0040H and
**								7FFF.FFFFH to the PCI bus with bit 31 of the address set (8000.0000H - FFFF.FFFFH).
**									 When clear, no translation occurs.
**  17          0 2                         Reserved
**  16          0 2                         SERR# Manual Assertion - when set, the ATU asserts SERR# for one clock on the PCI interface. Until
**						cleared, SERR# may not be manually asserted again. Once cleared, operation proceeds as specified.
**  15          0 2                         ATU Discard Timer Status - when set, one of the 4 discard timers within the ATU has expired and
** 						discarded the delayed completion transaction within the queue. When clear, no timer has expired.
**  14:10    00000 2                        Reserved
**  09          0 2                         SERR# Detected Interrupt Enable - When set, the Intel XScale core is signalled an HPI# interrupt
**						when the ATU detects that SERR# was asserted. When clear, 
**							the Intel XScale core is not interrupted when SERR# is detected.
**  08          0 2                         Direct Addressing Enable - Setting this bit enables direct outbound addressing through the ATU.
**  						Internal bus cycles with an address between 0000.0040H and 7FFF.FFFFH automatically forwards to
**  						the PCI bus with or without translation of address bit 31 based on the setting of bit 18 of 
**							the ATUCR.
**  07:04    0000 2                         Reserved
**  03          0 2                         ATU BIST Interrupt Enable - When set, enables an interrupt to the Intel XScale core when the start
**						BIST bit is set in the ATUBISTR register. This bit is also reflected as the BIST Capable bit 7 
**							in the ATUBISTR register.
**  02          0 2                         Reserved
**  01          0 2                         Outbound ATU Enable - When set, enables the outbound address translation unit.
**						When cleared, disables the outbound ATU.
**  00          0 2                         Reserved
***********************************************************************************
*/
#define     ARCMSR_ATU_CONFIGURATION_REG		          0x80    /*dword 0x83,0x82,0x81,0x80*/
/*
***********************************************************************************
**  PCI Configuration and Status Register - PCSR
**  
**  The PCI Configuration and Status Register has additional bits for controlling and monitoring
**  various features of the PCI bus interface.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:19      0000H                        Reserved
**  18          0 2                         Detected Address or Attribute Parity Error - set when a parity error is detected during either the address
**  					or attribute phase of a transaction on the PCI bus even when the ATUCMD register Parity Error
**  					Response bit is cleared. Set under the following conditions:
**  					¡E Any Address or Attribute (PCI-X Only) Parity Error on the Bus (including one generated by the ATU).
**  17:16  Varies with
**  										external state
**  										of DEVSEL#,
**  										STOP#, and
**  										TRDY#,
**  										during
**  										P_RST#
**  										PCI-X capability - These two bits define the mode of 
**  										the PCI bus (conventional or PCI-X) as well as the
**  										operating frequency in the case of PCI-X mode.
**  										00 - Conventional PCI mode
**  										01 - PCI-X 66
**  										10 - PCI-X 100
**  										11 - PCI-X 133
**  										As defined by the PCI-X Addendum to the PCI Local Bus Specification,
**  										Revision 1.0a, the operating
**  										mode is determined by an initialization pattern on the PCI bus during
**  										P_RST# assertion:
**  										DEVSEL# STOP# TRDY# Mode
**  										Deasserted Deasserted Deasserted Conventional
**  										Deasserted Deasserted Asserted PCI-X 66
**  										Deasserted Asserted Deasserted PCI-X 100
**  										Deasserted Asserted Asserted PCI-X 133
**  										All other patterns are reserved.
**  15          0 2
**  										Outbound Transaction Queue Busy:
**  										    0=Outbound Transaction Queue Empty
**  										    1=Outbound Transaction Queue Busy
**  14          0 2
**  										Inbound Transaction Queue Busy:
**  										    0=Inbound Transaction Queue Empty
**  										    1=Inbound Transaction Queue Busy
**  13          0 2                         Reserved.
**  12          0 2								Discard Timer Value - This bit controls the time-out value 
**  										for the four discard timers attached to the queues holding read data. 
**                                                         A value of 0 indicates the time-out value is 2 15 clocks. 
**                                                         A value of 1 indicates the time-out value is 2 10 clocks.
**  11          0 2                         Reserved.
**  10      Varies with
**  										external state
**  										of M66EN
**  										during
**  										P_RST#
**  							Bus Operating at 66 MHz - When set, the interface has been initialized to function at 66 MHz in
**  										Conventional PCI mode by the assertion of M66EN during bus initialization.
**  										When clear, the interface
**  										has been initialized as a 33 MHz bus.
**  		NOTE: When PCSR bits 17:16 are not equal to zero, then this bit is meaningless since the 80331 is operating in PCI-X mode.
**  09          0 2                         Reserved
**  08      Varies with
**  										external state
**  										of REQ64#
**  										during
**  										P_RST#
**  										PCI Bus 64-Bit Capable - When clear, the PCI bus interface has been
**  										configured as 64-bit capable by
**  										the assertion of REQ64# on the rising edge of P_RST#. When set, 
**  										the PCI interface is configured as
**  										32-bit only.
**  07:06      00 2                         Reserved.
**  05         0 2   						Reset Internal Bus - This bit controls the reset of the Intel XScale core 
**  								and all units on the internal
**  								bus. In addition to the internal bus initialization, 
**  								this bit triggers the assertion of the M_RST# pin for
**  								initialization of registered DIMMs. When set:
**  								When operating in the conventional PCI mode:
**  								¡E All current PCI transactions being mastered by the ATU completes, 
**  								and the ATU master interfaces
**  								proceeds to an idle state. No additional transactions is mastered by these units
**  								until the internal bus reset is complete.
**  								¡E All current transactions being slaved by the ATU on either the PCI bus 
**  								or the internal bus
**  								completes, and the ATU target interfaces proceeds to an idle state. 
**  								All future slave transactions master aborts, 
**  								with the exception of the completion cycle for the transaction that set the Reset
**  								Internal Bus bit in the PCSR.
**  								¡E When the value of the Core Processor Reset bit in the PCSR (upon P_RST# assertion) 
**  								is set, the Intel XScale core is held in reset when the internal bus reset is complete.
**  								¡E The ATU ignores configuration cycles, and they appears as master aborts for: 32 
**  								Internal Bus clocks.
**  								¡E The 80331 hardware clears this bit after the reset operation completes.
**  								When operating in the PCI-X mode:
**  								The ATU hardware responds the same as in Conventional PCI-X mode. 
**  								However, this may create a problem in PCI-X mode for split requests in 
**  								that there may still be an outstanding split completion that the
**  								ATU is either waiting to receive (Outbound Request) or initiate 
**  								(Inbound Read Request). For a cleaner
**  								internal bus reset, host software can take the following steps prior 
**  								to asserting Reset Internal bus:
**  					1. Clear the Bus Master (bit 2 of the ATUCMD) and the Memory Enable (bit 1 of the ATUCMD) bits in
**  						the ATUCMD. This ensures that no new transactions, either outbound or inbound are enqueued.
**  					2. Wait for both the Outbound (bit 15 of the PCSR) and Inbound Read (bit 14 of the PCSR) Transaction
**  						queue busy bits to be clear.
**  					3. Set the Reset Internal Bus bit
**  	As a result, the ATU hardware resets the internal bus using the same logic as in conventional mode,
**  	however the user is now assured that the ATU no longer has any pending inbound or outbound split
**  	completion transactions.
**  	NOTE: Since the Reset Internal Bus bit is set using an inbound configuration cycle, the user is
**  	guaranteed that any prior configuration cycles have properly completed since there is only a one
**  	deep transaction queue for configuration transaction requests. The ATU sends the appropriate
**  	Split Write Completion Message to the Requester prior to the onset of Internal Bus Reset.
**  04      0 2						        Bus Master Indicator Enable: Provides software control for the
**  								Bus Master Indicator signal P_BMI used
**  		for external RAIDIOS logic control of private devices. Only valid when operating with the bridge and
**  		central resource/arbiter disabled (BRG_EN =low, ARB_EN=low).
**  03		Varies with external state of PRIVDEV during
**  							P_RST#
**  			Private Device Enable - This bit indicates the state of the reset strap which enables the private device
**  			control mechanism within the PCI-to-PCI Bridge SISR configuration register.
**  			0=Private Device control Disabled - SISR register bits default to zero
**  			1=Private Device control Enabled - SISR register bits default to one
**  	02	Varies with external state of RETRY during P_RST#
**  			Configuration Cycle Retry - When this bit is set, the PCI interface of the 80331 responds to all
**  			configuration cycles with a Retry condition. When clear, the 80331 responds to the appropriate
**  			configuration cycles.
**  		The default condition for this bit is based on the external state of the RETRY pin at the rising edge of
**  			P_RST#. When the external state of the pin is high, the bit is set. When the external state of the pin is
**  			low, the bit is cleared.
**  01		Varies with external state of CORE_RST# during P_RST#
**  			Core Processor Reset - This bit is set to its default value by the hardware when either P_RST# is
**  			asserted or the Reset Internal Bus bit in PCSR is set. When this bit is set, the Intel XScale core is
**  			being held in reset. Software cannot set this bit. Software is required to clear this bit to deassert Intel 
**  			XScale  core reset.
**  			The default condition for this bit is based on the external state of the CORE_RST# pin at the rising edge
**  			of P_RST#. When the external state of the pin is low, the bit is set. When the external state of the pin is
**  			high, the bit is clear.
**  00		Varies with external state of PRIVMEM during P_RST#
**  			Private Memory Enable - This bit indicates the state of the reset strap which enables the private device
**  			control mechanism within the PCI-to-PCI Bridge SDER configuration register.
**  			0=Private Memory control Disabled - SDER register bit 2 default to zero
**  			1=Private Memory control Enabled - SDER register bits 2 default to one
***********************************************************************************
*/
#define     ARCMSR_PCI_CONFIGURATION_STATUS_REG		          0x84    /*dword 0x87,0x86,0x85,0x84*/
/*
***********************************************************************************
**  ATU Interrupt Status Register - ATUISR
**  
**  The ATU Interrupt Status Register is used to notify the core processor of the source of an ATU
**  interrupt. In addition, this register is written to clear the source of the interrupt to the interrupt unit
**  of the 80331. All bits in this register are Read/Clear.
**  Bits 4:0 are a direct reflection of bits 14:11 and bit 8 (respectively) of the ATU Status Register
**  (these bits are set at the same time by hardware but need to be cleared independently). Bit 7 is set
**  by an error associated with the internal bus of the 80331. Bit 8 is for software BIST. The
**  conditions that result in an ATU interrupt are cleared by writing a 1 to the appropriate bits in this
**  register.
**  Note: Bits 4:0, and bits 15 and 13:7 can result in an interrupt being driven to the Intel XScale core.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:18      0000H                        Reserved
**  17          0 2                         VPD Address Register Updated - This bit is set when a PCI bus configuration write occurs to the VPDAR
**  														register. Configuration register writes to the VPDAR does NOT result in bit 15 also being set. When set,
**  														this bit results in the assertion of the ATU Configure Register Write Interrupt.
**  16          0 2                         Reserved
**  15          0 2                         ATU Configuration Write - This bit is set when a PCI bus configuration write occurs to any ATU register.
**                                                          When set, this bit results in the assertion of the ATU Configure Register Write Interrupt.
**  14          0 2                         ATU Inbound Memory Window 1 Base Updated - This bit is set when a PCI bus configuration write
**  														occurs to either the IABAR1 register or the IAUBAR1 register. Configuration register writes to these
**  														registers deos NOT result in bit 15 also being set. When set, this bit results in the assertion of the ATU
**  														Configure Register Write Interrupt.
**  13          0 2                         Initiated Split Completion Error Message - This bit is set when the device initiates a Split Completion
**                                                          Message on the PCI Bus with the Split Completion Error attribute bit set.
**  12          0 2                         Received Split Completion Error Message - This bit is set when the device receives a Split Completion
**                                                          Message from the PCI Bus with the Split Completion Error attribute bit set.
**  11          0 2                         Power State Transition - When the Power State Field of the ATU Power Management Control/Status
**  														Register is written to transition the ATU function Power State from D0 to D3, D0 to D1, or D3 to D0 and
**  														the ATU Power State Transition Interrupt mask bit is cleared, this bit is set.
**  10          0 2                         P_SERR# Asserted - set when P_SERR# is asserted on the PCI bus by the ATU.
**  09          0 2                         Detected Parity Error - set when a parity error is detected on the PCI bus even when the ATUCMD
**  														register¡¦s Parity Error Response bit is cleared. Set under the following conditions:
**  														¡E Write Data Parity Error when the ATU is a target (inbound write).
**  														¡E Read Data Parity Error when the ATU is an initiator (outbound read).
**  														¡E Any Address or Attribute (PCI-X Only) Parity Error on the Bus.
**  08          0 2                         ATU BIST Interrupt - When set, generates the ATU BIST Start Interrupt and indicates the host processor
**  														has set the Start BIST bit (ATUBISTR register bit 6), when the ATU BIST interrupt is enabled (ATUCR
**  														register bit 3). The Intel XScale core can initiate the software BIST and store the result in ATUBISTR
**  														register bits 3:0.
**  														Configuration register writes to the ATUBISTR does NOT result in bit 15 also being set or the assertion
**  														of the ATU Configure Register Write Interrupt.
**  07          0 2                         Internal Bus Master Abort - set when a transaction initiated by the ATU internal bus initiator interface ends in a Master-abort.
**  06:05      00 2                         Reserved.
**  04          0 2                         P_SERR# Detected - set when P_SERR# is detected on the PCI bus by the ATU.
**  03          0 2                         PCI Master Abort - set when a transaction initiated by the ATU PCI initiator interface ends in a Master-abort.
**  02          0 2                         PCI Target Abort (master) - set when a transaction initiated by the ATU PCI master interface ends in a Target-abort.
**  01          0 2                         PCI Target Abort (target) - set when the ATU interface, acting as a target, terminates the transaction on the PCI bus with a target abort.
**  00          0 2                         PCI Master Parity Error - Master Parity Error - The ATU interface sets this bit under the following
**  														conditions:
**  														¡E The ATU asserted PERR# itself or the ATU observed PERR# asserted.
**  														¡E And the ATU acted as the requester for the operation in which the error occurred.
**  														¡E And the ATUCMD register¡¦s Parity Error Response bit is set
**  														¡E Or (PCI-X Mode Only) the ATU received a Write Data Parity Error Message
**  														¡E And the ATUCMD register¡¦s Parity Error Response bit is set
***********************************************************************************
*/
#define     ARCMSR_ATU_INTERRUPT_STATUS_REG		          0x88    /*dword 0x8B,0x8A,0x89,0x88*/
/*
***********************************************************************************
**  ATU Interrupt Mask Register - ATUIMR
**
**  The ATU Interrupt Mask Register contains the control bit to enable and disable interrupts
**  generated by the ATU.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:15     0 0000H                       Reserved
**  14        0 2                           VPD Address Register Updated Mask - Controls the setting of bit 17 of the ATUISR and generation of the
**  					ATU Configuration Register Write interrupt when a PCI bus write occurs to the VPDAR register.
**  					0=Not Masked
**  					1=Masked
**  13        0 2                           Reserved
**  12        0 2                           Configuration Register Write Mask - Controls the setting of bit 15 of the ATUISR and generation of the
**  					ATU Configuration Register Write interrupt when a PCI bus write occurs to any ATU configuration register
**  					except those covered by mask bit 11 and bit 14 of this register, and ATU BIST enable bit 3 of the ATUCR.
**  										0=Not Masked
**  										1=Masked
**  11        1 2                           ATU Inbound Memory Window 1 Base Updated Mask - Controls the setting of bit 14 of the ATUISR and
**  					generation of the ATU Configuration Register Write interrupt when a PCI bus write occurs to either the
**  														IABAR1 register or the IAUBAR1 register.
**  														0=Not Masked
**  														1=Masked
**  10        0 2                           Initiated Split Completion Error Message Interrupt Mask - Controls the setting of bit 13 of the ATUISR and
**  					generation of the ATU Error interrupt when the ATU initiates a Split Completion Error Message.
**  														0=Not Masked
**  														1=Masked
**  09        0 2                           Received Split Completion Error Message Interrupt Mask- Controls the setting of bit 12 of the ATUISR
**  					and generation of the ATU Error interrupt when a Split Completion Error Message results in bit 29 of the
**  					PCIXSR being set.
**  					0=Not Masked
**  					1=Masked
**  08        1 2                           Power State Transition Interrupt Mask - Controls the setting of bit 12 of the ATUISR and generation of the
**  					ATU Error interrupt when ATU Power Management Control/Status Register is written to transition the
**  					ATU Function Power State from D0 to D3, D0 to D1, D1 to D3 or D3 to D0.
**  														0=Not Masked
**  														1=Masked
**  07        0 2                           ATU Detected Parity Error Interrupt Mask - Controls the setting of bit 9 of the ATUISR and generation of
**  					the ATU Error interrupt when a parity error detected on the PCI bus that sets bit 15 of the ATUSR.
**  														0=Not Masked
**  														1=Masked
**  06        0 2                           ATU SERR# Asserted Interrupt Mask - Controls the setting of bit 10 of the ATUISR and generation of the
**  					ATU Error interrupt when SERR# is asserted on the PCI interface resulting in bit 14 of the ATUSR being set.
**  														0=Not Masked
**  														1=Masked
**  		NOTE: This bit is specific to the ATU asserting SERR# and not detecting SERR# from another master.
**  05        0 2                           ATU PCI Master Abort Interrupt Mask - Controls the setting of bit 3 of the ATUISR and generation of the
**  					ATU Error interrupt when a master abort error resulting in bit 13 of the ATUSR being set.
**  														0=Not Masked
**  														1=Masked
**  04        0 2                           ATU PCI Target Abort (Master) Interrupt Mask- Controls the setting of bit 12 of the ATUISR and ATU Error
**  					generation of the interrupt when a target abort error resulting in bit 12 of the ATUSR being set
**  														0=Not Masked
**  														1=Masked
**  03        0 2                           ATU PCI Target Abort (Target) Interrupt Mask- Controls the setting of bit 1 of the ATUISR and generation
**  					of the ATU Error interrupt when a target abort error resulting in bit 11 of the ATUSR being set.
**  														0=Not Masked
**  														1=Masked
**  02        0 2                           ATU PCI Master Parity Error Interrupt Mask - Controls the setting of bit 0 of the ATUISR and generation
**  					of the ATU Error interrupt when a parity error resulting in bit 8 of the ATUSR being set.
**  														0=Not Masked
**  														1=Masked
**  01        0 2                           ATU Inbound Error SERR# Enable - Controls when the ATU asserts (when enabled through the
**  					ATUCMD) SERR# on the PCI interface in response to a master abort on the internal bus during an
**  														inbound write transaction.
**  														0=SERR# Not Asserted due to error
**  														1=SERR# Asserted due to error
**  00        0 2                           ATU ECC Target Abort Enable - Controls the ATU response on the PCI interface to a target abort (ECC
**  					error) from the memory controller on the internal bus. In conventional mode, this action only occurs
**  					during an inbound read transaction where the data phase that was target aborted on the internal bus is
**  					actually requested from the inbound read queue.
**  														0=Disconnect with data 
**  														(the data being up to 64 bits of 1¡¦s)
**  														1=Target Abort
**  		NOTE: In PCI-X Mode, The ATU initiates a Split Completion Error Message (with message class=2h -
**  			completer error and message index=81h - 80331 internal bus target abort) on the PCI bus,
**  			independent of the setting of this bit.
*********************************************************************************** 
*/
#define     ARCMSR_ATU_INTERRUPT_MASK_REG		          0x8C    /*dword 0x8F,0x8E,0x8D,0x8C*/
/*
***********************************************************************************
**  Inbound ATU Base Address Register 3 - IABAR3
**
**  . The Inbound ATU Base Address Register 3 (IABAR3) together with the Inbound ATU Upper Base Address Register 3 (IAUBAR3) defines the block 
**    of memory addresses where the inbound translation window 3 begins. 
**  . The inbound ATU decodes and forwards the bus request to the 80331 internal bus with a translated address to map into 80331 local memory. 
**  . The IABAR3 and IAUBAR3 define the base address and describes the required memory block size.
**  . Bits 31 through 12 of the IABAR3 is either read/write bits or read only with a value of 0 depending on the value located within the IALR3. 
**    The programmed value within the base address register must comply with the PCI programming requirements for address alignment. 
**  Note: 
**      Since IABAR3 does not appear in the standard PCI configuration header space (offsets 00H - 3CH), 
**      IABAR3 is not configured by the host during normal system initialization.
**  Warning: 
**    When a non-zero value is not written to IALR3, 
**                          the user should not set either the Prefetchable Indicator 
**                                                      or the Type         Indicator for 64 bit addressability.
**                          This is the default for IABAR3. 
**  Assuming a non-zero value is written to IALR3,
**                          the user may set the Prefetchable Indicator 
**                                        or the Type         Indicator:
**  						a. Since non prefetchable memory windows can never be placed above the 4 Gbyte address boundary,
**                             when the Prefetchable Indicator is not set, 
**                             the user should also leave the Type Indicator set for 32 bit addressability.
**                             This is the default for IABAR3.
**  						b. when the Prefetchable Indicator is set, 
**                             the user should also set the Type Indicator for 64 bit addressability.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     00000H                        Translation Base Address 3 - These bits define the actual location 
**                                          the translation function is to respond to when addressed from the PCI bus.
**  11:04        00H                        Reserved.
**  03           0 2                        Prefetchable Indicator - When set, defines the memory space as prefetchable.
**  02:01       00 2                        Type Indicator - Defines the width of the addressability for this memory window:
**  						00 - Memory Window is locatable anywhere in 32 bit address space
**  						10 - Memory Window is locatable anywhere in 64 bit address space
**  00           0 2                        Memory Space Indicator - This bit field describes memory or I/O space base address.
**                                                                   The ATU does not occupy I/O space, 
**                                                                   thus this bit must be zero.
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_BASE_ADDRESS3_REG		          0x90    /*dword 0x93,0x92,0x91,0x90*/
/*
***********************************************************************************
**  Inbound ATU Upper Base Address Register 3 - IAUBAR3
**
**  This register contains the upper base address when decoding PCI addresses beyond 4 GBytes.
**  Together with the Translation Base Address this register defines the actual location
**  the translation function is to respond to when addressed from the PCI bus for addresses > 4GBytes (for DACs).
**  The programmed value within the base address register must comply with the PCI programming
**  requirements for address alignment.
**  Note: 
**      When the Type indicator of IABAR3 is set to indicate 32 bit addressability, 
**      the IAUBAR3 register attributes are read-only. 
**      This is the default for IABAR3.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:0      00000H                        Translation Upper Base Address 3 - Together with the Translation Base Address 3 these bits define 
**                        the actual location the translation function is to respond to when addressed from the PCI bus for addresses > 4GBytes.
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_UPPER_BASE_ADDRESS3_REG		          0x94    /*dword 0x97,0x96,0x95,0x94*/
/*
***********************************************************************************
**  Inbound ATU Limit Register 3 - IALR3
**
**  Inbound address translation for memory window 3 occurs for data transfers occurring from the PCI
**  bus (originated from the PCI bus) to the 80331 internal bus. The address translation block converts
**  PCI addresses to internal bus addresses.
**  The inbound translation base address for inbound window 3 is specified in Section 3.10.15. When
**  determining block size requirements ¡X as described in Section 3.10.21 ¡X the translation limit
**  register provides the block size requirements for the base address register. The remaining registers
**  used for performing address translation are discussed in Section 3.2.1.1.
**  The 80331 translate value register¡¦s programmed value must be naturally aligned with the base
**  address register¡¦s programmed value. The limit register is used as a mask; thus, the lower address
**  bits programmed into the 80331 translate value register are invalid. Refer to the PCI Local Bus
**  Specification, Revision 2.3 for additional information on programming base address registers.
**  Bits 31 to 12 within the IALR3 have a direct effect on the IABAR3 register, bits 31 to 12, with a
**  one to one correspondence. A value of 0 in a bit within the IALR3 makes the corresponding bit
**  within the IABAR3 a read only bit which always returns 0. A value of 1 in a bit within the IALR3
**  makes the corresponding bit within the IABAR3 read/write from PCI. Note that a consequence of
**  this programming scheme is that unless a valid value exists within the IALR3, all writes to the
**  IABAR3 has no effect since a value of all zeros within the IALR3 makes the IABAR3 a read only
**  register.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     00000H                        Inbound Translation Limit 3 - This readback value determines the memory block size required 
**                                          for the ATUs memory window 3.
**  11:00       000H                        Reserved
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_LIMIT3_REG		          0x98    /*dword 0x9B,0x9A,0x99,0x98*/
/*
***********************************************************************************
**  Inbound ATU Translate Value Register 3 - IATVR3
**
**  The Inbound ATU Translate Value Register 3 (IATVR3) contains the internal bus address used to
**  convert PCI bus addresses. The converted address is driven on the internal bus as a result of the
**  inbound ATU address translation.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     00000H                        Inbound ATU Translation Value 3 - This value is used to convert the PCI address to internal bus addresses. 
**                                                          This value must be 64-bit aligned on the internal bus. The default address allows the ATU to
**                                                          access the internal 80331 memory-mapped registers.
**  11:00       000H                        Reserved
***********************************************************************************
*/
#define     ARCMSR_INBOUND_ATU_TRANSLATE_VALUE3_REG		          0x9C    /*dword 0x9F,0x9E,0x9D,0x9C*/
/*
***********************************************************************************
**  Outbound Configuration Cycle Address Register - OCCAR
**  
**  The Outbound Configuration Cycle Address Register is used to hold the 32-bit PCI configuration
**  cycle address. The Intel XScale core writes the PCI configuration cycles address which then
**  enables the outbound configuration read or write. The Intel XScale core then performs a read or
**  write to the Outbound Configuration Cycle Data Register to initiate the configuration cycle on the
**  PCI bus.
**  Note: Bits 15:11 of the configuration cycle address for Type 0 configuration cycles are defined differently
**  for Conventional versus PCI-X modes. When 80331 software programs the OCCAR to initiate a
**  Type 0 configuration cycle, the OCCAR should always be loaded based on the PCI-X definition for
**  the Type 0 configuration cycle address. When operating in Conventional mode, the 80331 clears
**  bits 15:11 of the OCCAR prior to initiating an outbound Type 0 configuration cycle. See the PCI-X
**  Addendum to the PCI Local Bus Specification, Revision 1.0a for details on the two formats.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:00    0000 0000H                     Configuration Cycle Address - These bits define the 32-bit PCI address used during an outbound 
**                                          configuration read or write cycle.
***********************************************************************************
*/
#define     ARCMSR_OUTBOUND_CONFIGURATION_CYCLE_ADDRESS_REG		          0xA4    /*dword 0xA7,0xA6,0xA5,0xA4*/
/*
***********************************************************************************
**  Outbound Configuration Cycle Data Register - OCCDR
**
**  The Outbound Configuration Cycle Data Register is used to initiate a configuration read or write
**  on the PCI bus. The register is logical rather than physical meaning that it is an address not a
**  register. The Intel XScale core reads or writes the data registers memory-mapped address to
**  initiate the configuration cycle on the PCI bus with the address found in the OCCAR. For a
**  configuration write, the data is latched from the internal bus and forwarded directly to the OWQ.
**  For a read, the data is returned directly from the ORQ to the Intel XScale core and is never
**  actually entered into the data register (which does not physically exist).
**  The OCCDR is only visible from 80331 internal bus address space and appears as a reserved value
**  within the ATU configuration space.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:00    0000 0000H                     Configuration Cycle Data - These bits define the data used during an outbound configuration read 
**                                          or write cycle.
***********************************************************************************
*/
#define     ARCMSR_OUTBOUND_CONFIGURATION_CYCLE_DATA_REG		          0xAC    /*dword 0xAF,0xAE,0xAD,0xAC*/
/*
***********************************************************************************
**  VPD Capability Identifier Register - VPD_CAPID
**  
**  The Capability Identifier Register bits adhere to the definitions in the PCI Local Bus Specification,
**  Revision 2.3. This register in the PCI Extended Capability header identifies the type of Extended
**  Capability contained in that header. In the case of the 80331, this is the VPD extended capability
**  with an ID of 03H as defined by the PCI Local Bus Specification, Revision 2.3.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07:00       03H               Cap_Id - This field with its¡¦ 03H value identifies this item in the linked list of Extended Capability
**                                Headers as being the VPD capability registers.
***********************************************************************************
*/
#define     ARCMSR_VPD_CAPABILITY_IDENTIFIER_REG		      0xB8    /*byte*/
/*
***********************************************************************************
**  VPD Next Item Pointer Register - VPD_NXTP
**  
**  The Next Item Pointer Register bits adhere to the definitions in the PCI Local Bus Specification,
**  Revision 2.3. This register describes the location of the next item in the function¡¦s capability list.
**  For the 80331, this the final capability list, and hence, this register is set to 00H.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07:00       00H               Next_ Item_ Pointer - This field provides an offset into the function¡¦s configuration space pointing to the
**                                next item in the function¡¦s capability list. Since the VPD capabilities are the last in the linked list of
**                                extended capabilities in the 80331, the register is set to 00H.
***********************************************************************************
*/
#define     ARCMSR_VPD_NEXT_ITEM_PTR_REG		          0xB9    /*byte*/
/*
***********************************************************************************
**  VPD Address Register - VPD_AR
**
**  The VPD Address register (VPDAR) contains the DWORD-aligned byte address of the VPD to be
**  accessed. The register is read/write and the initial value at power-up is indeterminate.
**  A PCI Configuration Write to the VPDAR interrupts the Intel XScale core. Software can use
**  the Flag setting to determine whether the configuration write was intended to initiate a read or
**  write of the VPD through the VPD Data Register.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  15          0 2          Flag - A flag is used to indicate when a transfer of data between the VPD Data Register and the storage
**                           component has completed. Please see Section 3.9, ¡§Vital Product Data¡¨ on page 201 for more details on
**                           how the 80331 handles the data transfer.
**  14:0       0000H         VPD Address - This register is written to set the DWORD-aligned byte address used to read or write
**                           Vital Product Data from the VPD storage component.
***********************************************************************************
*/
#define     ARCMSR_VPD_ADDRESS_REG		          0xBA    /*word 0xBB,0xBA*/
/*
***********************************************************************************
**  VPD Data Register - VPD_DR
**
**  This register is used to transfer data between the 80331 and the VPD storage component.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:00      0000H                        VPD Data - Four bytes are always read or written through this register to/from the VPD storage component.
***********************************************************************************
*/
#define     ARCMSR_VPD_DATA_REG		          0xBC    /*dword 0xBF,0xBE,0xBD,0xBC*/
/*
***********************************************************************************
**  Power Management Capability Identifier Register -PM_CAPID
**
**  The Capability Identifier Register bits adhere to the definitions in the PCI Local Bus Specification,
**  Revision 2.3. This register in the PCI Extended Capability header identifies the type of Extended
**  Capability contained in that header. In the case of the 80331, this is the PCI Bus Power
**  Management extended capability with an ID of 01H as defined by the PCI Bus Power Management
**  Interface Specification, Revision 1.1.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07:00       01H                         Cap_Id - This field with its¡¦ 01H value identifies this item in the linked list of Extended Capability
**                                          Headers as being the PCI Power Management Registers.
***********************************************************************************
*/
#define     ARCMSR_POWER_MANAGEMENT_CAPABILITY_IDENTIFIER_REG		          0xC0    /*byte*/
/*
***********************************************************************************
**  Power Management Next Item Pointer Register - PM_NXTP
**
**  The Next Item Pointer Register bits adhere to the definitions in the PCI Local Bus Specification,
**  Revision 2.3. This register describes the location of the next item in the function¡¦s capability list.
**  For the 80331, the next capability (MSI capability list) is located at off-set D0H.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07:00       D0H                         Next_ Item_ Pointer - This field provides an offset into the function¡¦s configuration space pointing to the
**                          next item in the function¡¦s capability list which in the 80331 is the MSI extended capabilities header.
***********************************************************************************
*/
#define     ARCMSR_POWER_NEXT_ITEM_PTR_REG		          0xC1    /*byte*/
/*
***********************************************************************************
**  Power Management Capabilities Register - PM_CAP
**  
**  Power Management Capabilities bits adhere to the definitions in the PCI Bus Power Management
**  Interface Specification, Revision 1.1. This register is a 16-bit read-only register which provides
**  information on the capabilities of the ATU function related to power management.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  15:11   00000 2                         PME_Support - This function is not capable of asserting the PME# signal in any state, since PME# 
**                                          is not supported by the 80331.
**  10          0 2                         D2_Support - This bit is set to 0 2 indicating that the 80331 does not support the D2 Power Management State
**  9           1 2                         D1_Support - This bit is set to 1 2 indicating that the 80331 supports the D1 Power Management State
**  8:6       000 2                         Aux_Current - This field is set to 000 2 indicating that the 80331 has no current requirements for the
**                                                          3.3Vaux signal as defined in the PCI Bus Power Management Interface Specification, Revision 1.1
**  5           0 2                         DSI - This field is set to 0 2 meaning that this function requires a device specific initialization sequence
**                                                          following the transition to the D0 uninitialized state.
**  4           0 2                         Reserved.
**  3           0 2                         PME Clock - Since the 80331 does not support PME# signal generation this bit is cleared to 0 2 .
**  2:0       010 2                         Version - Setting these bits to 010 2 means that this function complies with PCI Bus Power Management 
**                                          Interface Specification, Revision 1.1
***********************************************************************************
*/
#define     ARCMSR_POWER_MANAGEMENT_CAPABILITY_REG		          0xC2    /*word 0xC3,0xC2*/
/*
***********************************************************************************
**  Power Management Control/Status Register - PM_CSR
**
**  Power Management Control/Status bits adhere to the definitions in the PCI Bus Power
**  Management Interface Specification, Revision 1.1. This 16-bit register is the control and status
**  interface for the power management extended capability.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  15          0 2                         PME_Status - This function is not capable of asserting the PME# signal in any state, since PME## is not
**                                          supported by the 80331.
**  14:9        00H                         Reserved
**  8           0 2                         PME_En - This bit is hardwired to read-only 0 2 since this function does not support PME# 
**                                          generation from any power state.
**  7:2    000000 2                         Reserved
**  1:0        00 2                         Power State - This 2-bit field is used both to determine the current power state 
**                                          of a function and to set the function into a new power state. The definition of the values is:
**  							00 2 - D0
**  							01 2 - D1
**  							10 2 - D2 (Unsupported)
**  							11 2 - D3 hot
**  							The 80331 supports only the D0 and D3 hot states.
**
***********************************************************************************
*/
#define     ARCMSR_POWER_MANAGEMENT_CONTROL_STATUS_REG		          0xC4    /*word 0xC5,0xC4*/
/*
***********************************************************************************
**  PCI-X Capability Identifier Register - PX_CAPID
**  
**  The Capability Identifier Register bits adhere to the definitions in the PCI Local Bus Specification,
**  Revision 2.3. This register in the PCI Extended Capability header identifies the type of Extended
**  Capability contained in that header. In the case of the 80331, this is the PCI-X extended capability with
**  an ID of 07H as defined by the PCI-X Addendum to the PCI Local Bus Specification, Revision 1.0a.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07:00       07H                         Cap_Id - This field with its¡¦ 07H value identifies this item in the linked list of Extended Capability
**                                          Headers as being the PCI-X capability registers.
***********************************************************************************
*/
#define     ARCMSR_PCIX_CAPABILITY_IDENTIFIER_REG		          0xE0    /*byte*/
/*
***********************************************************************************
**  PCI-X Next Item Pointer Register - PX_NXTP
**  
**  The Next Item Pointer Register bits adhere to the definitions in the PCI Local Bus Specification,
**  Revision 2.3. This register describes the location of the next item in the function¡¦s capability list.
**  By default, the PCI-X capability is the last capabilities list for the 80331, thus this register defaults
**  to 00H.
**  However, this register may be written to B8H prior to host configuration to include the VPD
**  capability located at off-set B8H.
**  Warning: Writing this register to any value other than 00H (default) or B8H is not supported and may
**  produce unpredictable system behavior.
**  In order to guarantee that this register is written prior to host configuration, the 80331 must be
**  initialized at P_RST# assertion to Retry Type 0 configuration cycles (bit 2 of PCSR). Typically,
**  the Intel XScale core would be enabled to boot immediately following P_RST# assertion in
**  this case (bit 1 of PCSR), as well. Please see Table 125, ¡§PCI Configuration and Status Register -
**  PCSR¡¨ on page 253 for more details on the 80331 initialization modes.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  07:00       00H                         Next_ Item_ Pointer - This field provides an offset into the function¡¦s configuration space pointing to the
**  			next item in the function¡¦s capability list. Since the PCI-X capabilities are the last in the linked list of
**  			extended capabilities in the 80331, the register is set to 00H.
**  			However, this field may be written prior to host configuration with B8H to extend the list to include the
**  			VPD extended capabilities header.
***********************************************************************************
*/
#define     ARCMSR_PCIX_NEXT_ITEM_PTR_REG		          0xE1    /*byte*/
/*
***********************************************************************************
**  PCI-X Command Register - PX_CMD
**  
**  This register controls various modes and features of ATU and Message Unit when operating in the
**  PCI-X mode.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  15:7     000000000 2                    Reserved.
**  6:4        011 2                        Maximum Outstanding Split Transactions - This register sets the maximum number of Split Transactions
**  			the device is permitted to have outstanding at one time.
**  			Register Maximum Outstanding
**  					0 1
**  					1 2
**  					2 3
**  					3 4
**  					4 8
**  					5 12
**  					6 16
**  					7 32
**  3:2        00 2                         Maximum Memory Read Byte Count - This register sets the maximum byte count the device uses when
**  			initiating a Sequence with one of the burst memory read commands.
**  			Register Maximum Byte Count
**  					0 512
**  					1 1024
**  					2 2048
**  					3 4096
**  					1 0 2
**  			Enable Relaxed Ordering - The 80331 does not set the relaxed ordering bit in the Requester Attributes
**  			of Transactions.
**  0          0 2                          Data Parity Error Recovery Enable - The device driver sets this bit to enable the device to attempt to
**  			recover from data parity errors. When this bit is 0 and the device is in PCI-X mode, the device asserts
**  			SERR# (when enabled) whenever the Master Data Parity Error bit (Status register, bit 8) is set.
***********************************************************************************
*/
#define     ARCMSR_PCIX_COMMAND_REG		          0xE2    /*word 0xE3,0xE2*/
/*
***********************************************************************************
**  PCI-X Status Register - PX_SR
**  
**  This register identifies the capabilities and current operating mode of ATU, DMAs and Message
**  Unit when operating in the PCI-X mode.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:30       00 2                        Reserved
**  29           0 2                        Received Split Completion Error Message - This bit is set when the device receives a Split Completion
**  					Message with the Split Completion Error attribute bit set. Once set, this bit remains set until software
**  					writes a 1 to this location.
**  					0=no Split Completion error message received.
**  					1=a Split Completion error message has been received.
**  28:26      001 2                        Designed Maximum Cumulative Read Size (DMCRS) - The value of this register depends on the setting
**  					of the Maximum Memory Read Byte Count field of the PCIXCMD register:
**  					DMCRS Max ADQs Maximum Memory Read Byte Count Register Setting
**  					1 16 512 (Default)
**  					2 32 1024
**  					2 32 2048
**  					2 32 4096
**  25:23      011 2                        Designed Maximum Outstanding Split Transactions - The 80331 can have up to four outstanding split transactions.
**  22:21       01 2                        Designed Maximum Memory Read Byte Count - The 80331 can generate memory reads with byte counts up 
**                                          to 1024 bytes.
**  20           1 2                        80331 is a complex device.
**  19           0 2                        Unexpected Split Completion - This bit is set when an unexpected Split Completion with this device¡¦s
**  					Requester ID is received. Once set, this bit remains set until software writes a 1 to this location.
**  					0=no unexpected Split Completion has been received.
**  					1=an unexpected Split Completion has been received.
**  18           0 2                        Split Completion Discarded - This bit is set when the device discards a Split Completion because the
**  					requester would not accept it. See Section 5.4.4 of the PCI-X Addendum to the PCI Local Bus
**  					Specification, Revision 1.0a for details. Once set, this bit remains set until software writes a 1 to this
**  					location.
**  					0=no Split Completion has been discarded.
**  					1=a Split Completion has been discarded.
**  		NOTE: The 80331 does not set this bit since there is no Inbound address responding to Inbound Read
**  			Requests with Split Responses (Memory or Register) that has ¡§read side effects.¡¨
**  17           1 2                        80331 is a 133 MHz capable device.
**  16           1 2 or P_32BITPCI#	80331 with bridge enabled (BRG_EN=1) implements the ATU with a 64-bit interface on the secondary PCI bus, 
**  					therefore this bit is always set.
**  			80331 with no bridge and central resource disabled (BRG_EN=0, ARB_EN=0), 
**  			use this bit to identify the add-in card to the system as 64-bit or 32-bit wide via a user-configurable strap (P_32BITPCI#). 
**  			This strap, by default, identifies the add in card based on 80331 with bridge disabled 
**  			as 64-bit unless the user attaches the appropriate pull-down resistor to the strap.
**  			0=The bus is 32 bits wide.
**  			1=The bus is 64 bits wide.
**  15:8         FFH                        Bus Number - This register is read for diagnostic purposes only. It indicates the number of the bus
**  			segment for the device containing this function. The function uses this number as part of its Requester
**  			ID and Completer ID. For all devices other than the source bridge, each time the function is addressed
**  			by a Configuration Write transaction, the function must update this register with the contents of AD[7::0]
**  			of the attribute phase of the Configuration Write, regardless of which register in the function is
**  			addressed by the transaction. The function is addressed by a Configuration Write transaction when all of
**  			the following are true:
**  			1. The transaction uses a Configuration Write command.
**  			2. IDSEL is asserted during the address phase.
**  			3. AD[1::0] are 00b (Type 0 configuration transaction).
**  			4. AD[10::08] of the configuration address contain the appropriate function number.
**  7:3          1FH                        Device Number - This register is read for diagnostic purposes only. It indicates the number of the device
**  			containing this function, i.e., the number in the Device Number field (AD[15::11]) of the address of a
**  			Type 0 configuration transaction that is assigned to the device containing this function by the connection
**  			of the system hardware. The system must assign a device number other than 00h (00h is reserved for
**  			the source bridge). The function uses this number as part of its Requester ID and Completer ID. Each
**  			time the function is addressed by a Configuration Write transaction, the device must update this register
**  			with the contents of AD[15::11] of the address phase of the Configuration Write, regardless of which
**  			register in the function is addressed by the transaction. The function is addressed by a Configuration
**  			Write transaction when all of the following are true:
**  			1. The transaction uses a Configuration Write command.
**  			2. IDSEL is asserted during the address phase.
**  			3. AD[1::0] are 00b (Type 0 configuration transaction).
**  			4. AD[10::08] of the configuration address contain the appropriate function number.
**  2:0        000 2                        Function Number - This register is read for diagnostic purposes only. It indicates the number of this
**  			function; i.e., the number in the Function Number field (AD[10::08]) of the address of a Type 0
**  			configuration transaction to which this function responds. The function uses this number as part of its
**  			Requester ID and Completer ID.
**
**************************************************************************
*/
#define     ARCMSR_PCIX_STATUS_REG		          0xE4    /*dword 0xE7,0xE6,0xE5,0xE4*/

/*
**************************************************************************
**                 Inbound Read Transaction
**  ========================================================================
**	An inbound read transaction is initiated by a PCI initiator and is targeted at either 80331 local
**	memory or a 80331 memory-mapped register space. The read transaction is propagated through
**	the inbound transaction queue (ITQ) and read data is returned through the inbound read queue
**	(IRQ).
**	When operating in the conventional PCI mode, all inbound read transactions are processed as
**	delayed read transactions. When operating in the PCI-X mode, all inbound read transactions are
**	processed as split transactions. The ATUs PCI interface claims the read transaction and forwards
**	the read request through to the internal bus and returns the read data to the PCI bus. Data flow for
**	an inbound read transaction on the PCI bus is summarized in the following statements:
**	¡E The ATU claims the PCI read transaction when the PCI address is within the inbound
**	translation window defined by ATU Inbound Base Address Register (and Inbound Upper Base
**	Address Register during DACs) and Inbound Limit Register.
**	¡E When operating in the conventional PCI mode, when the ITQ is currently holding transaction
**	information from a previous delayed read, the current transaction information is compared to
**	the previous transaction information (based on the setting of the DRC Alias bit in
**	Section 3.10.39, ¡§ATU Configuration Register - ATUCR¡¨ on page 252). When there is a
**	match and the data is in the IRQ, return the data to the master on the PCI bus. When there is a
**	match and the data is not available, a Retry is signaled with no other action taken. When there
**	is not a match and when the ITQ has less than eight entries, capture the transaction
**	information, signal a Retry and initiate a delayed transaction. When there is not a match and
**	when the ITQ is full, then signal a Retry with no other action taken.
**	¡X When an address parity error is detected, the address parity response defined in
**	Section 3.7 is used.
**	¡E When operating in the conventional PCI mode, once read data is driven onto the PCI bus from
**	the IRQ, it continues until one of the following is true:
**	¡X The initiator completes the PCI transaction. When there is data left unread in the IRQ, the
**	data is flushed.
**	¡X An internal bus Target Abort was detected. In this case, the QWORD associated with the
**	Target Abort is never entered into the IRQ, and therefore is never returned.
**	¡X Target Abort or a Disconnect with Data is returned in response to the Internal Bus Error.
**	¡X The IRQ becomes empty. In this case, the PCI interface signals a Disconnect with data to
**	the initiator on the last data word available.
**	¡E When operating in the PCI-X mode, when ITQ is not full, the PCI address, attribute and
**	command are latched into the available ITQ and a Split Response Termination is signalled to
**	the initiator.
**	¡E When operating in the PCI-X mode, when the transaction does not cross a 1024 byte aligned
**	boundary, then the ATU waits until it receives the full byte count from the internal bus target
**	before returning read data by generating the split completion transaction on the PCI-X bus.
**	When the read requested crosses at least one 1024 byte boundary, then ATU completes the
**	transfer by returning data in 1024 byte aligned chunks.
**	¡E When operating in the PCI-X mode, once a split completion transaction has started, it
**	continues until one of the following is true:
**	¡X The requester (now the target) generates a Retry Termination, or a Disconnection at Next
**	ADB (when the requester is a bridge)
**	¡X The byte count is satisfied.
**	¡X An internal bus Target Abort was detected. The ATU generates a Split Completion
**	Message (message class=2h - completer error, and message index=81h - target abort) to
**	inform the requester about the abnormal condition. The ITQ for this transaction is flushed.
**	Refer to Section 3.7.1.
**	¡X An internal bus Master Abort was detected. The ATU generates a Split Completion
**	Message (message class=2h - completer error, and message index=80h - Master abort) to
**	inform the requester about the abnormal condition. The ITQ for this transaction is flushed.
**	Refer to Section 3.7.1
**	¡E When operating in the conventional PCI mode, when the master inserts wait states on the PCI
**	bus, the ATU PCI slave interface waits with no premature disconnects.
**	¡E When a data parity error occurs signified by PERR# asserted from the initiator, no action is
**	taken by the target interface. Refer to Section 3.7.2.5.
**	¡E When operating in the conventional PCI mode, when the read on the internal bus is
**	target-aborted, either a target-abort or a disconnect with data is signaled to the initiator. This is
**	based on the ATU ECC Target Abort Enable bit (bit 0 of the ATUIMR for ATU). When set, a
**	target abort is used, when clear, a disconnect is used.
**	¡E When operating in the PCI-X mode (with the exception of the MU queue ports at offsets 40h
**	and 44h), when the transaction on the internal bus resulted in a target abort, the ATU generates
**	a Split Completion Message (message class=2h - completer error, and message index=81h -
**	internal bus target abort) to inform the requester about the abnormal condition. For the MU
**	queue ports, the ATU returns either a target abort or a single data phase disconnect depending
**	on the ATU ECC Target Abort Enable bit (bit 0 of the ATUIMR for ATU). The ITQ for this
**	transaction is flushed. Refer to Section 3.7.1.
**	¡E When operating in the conventional PCI mode, when the transaction on the internal bus
**	resulted in a master abort, the ATU returns a target abort to inform the requester about the
**	abnormal condition. The ITQ for this transaction is flushed. Refer to Section 3.7.1
**	¡E When operating in the PCI-X mode, when the transaction on the internal bus resulted in a
**	master abort, the ATU generates a Split Completion Message (message class=2h - completer
**	error, and message index=80h - internal bus master abort) to inform the requester about the
**	abnormal condition. The ITQ for this transaction is flushed. Refer to Section 3.7.1.
**	¡E When operating in the PCI-X mode, when the Split Completion transaction completes with
**	either Master-Abort or Target-Abort, the requester is indicating a failure condition that
**	prevents it from accepting the completion it requested. In this case, since the Split Request
**	addresses a location that has no read side effects, the completer must discard the Split
**	Completion and take no further action.
**	The data flow for an inbound read transaction on the internal bus is summarized in the following
**	statements:
**	¡E The ATU internal bus master interface requests the internal bus when a PCI address appears in
**		an ITQ and transaction ordering has been satisfied. When operating in the PCI-X mode the
**		ATU does not use the information provided by the Relax Ordering Attribute bit. That is, ATU
**		always uses conventional PCI ordering rules.
**	¡E Once the internal bus is granted, the internal bus master interface drives the translated address
**		onto the bus and wait for IB_DEVSEL#. When a Retry is signaled, the request is repeated.
**		When a master abort occurs, the transaction is considered complete and a target abort is loaded
**		into the associated IRQ for return to the PCI initiator (transaction is flushed once the PCI
**		master has been delivered the target abort).
**	¡E Once the translated address is on the bus and the transaction has been accepted, the internal
**		bus target starts returning data with the assertion of IB_TRDY#. Read data is continuously
**		received by the IRQ until one of the following is true:
**	¡X The full byte count requested by the ATU read request is received. The ATU internal bus
**	    initiator interface performs a initiator completion in this case.
**	¡X When operating in the conventional PCI mode, a Target Abort is received on the internal
**		bus from the internal bus target. In this case, the transaction is aborted and the PCI side is
**		informed.
**	¡X When operating in the PCI-X mode, a Target Abort is received on the internal bus from
**		the internal bus target. In this case, the transaction is aborted. The ATU generates a Split
**		Completion Message (message class=2h - completer error, and message index=81h -
**		target abort) on the PCI bus to inform the requester about the abnormal condition. The
**		ITQ for this transaction is flushed.
**	¡X When operating in the conventional PCI mode, a single data phase disconnection is
**		received from the internal bus target. When the data has not been received up to the next
**		QWORD boundary, the ATU internal bus master interface attempts to reacquire the bus.
**		When not, the bus returns to idle.
**	¡X When operating in the PCI-X mode, a single data phase disconnection is received from
**		the internal bus target. The ATU IB initiator interface attempts to reacquire the bus to
**		obtain remaining data.
**	¡X When operating in the conventional PCI mode, a disconnection at Next ADB is received
**	    from the internal bus target. The bus returns to idle.
**	¡X When operating in the PCI-X mode, a disconnection at Next ADB is received from the
**		internal bus target. The ATU IB initiator interface attempts to reacquire the bus to obtain
**		remaining data.
**		To support PCI Local Bus Specification, Revision 2.0 devices, the ATU can be programmed to
**		ignore the memory read command (Memory Read, Memory Read Line, and Memory Read
**		Multiple) when trying to match the current inbound read transaction with data in a DRC queue
**		which was read previously (DRC on target bus). When the Read Command Alias Bit in the
**		ATUCR register is set, the ATU does not distinguish the read commands on transactions. For
**		example, the ATU enqueues a DRR with a Memory Read Multiple command and performs the read
**		on the internal bus. Some time later, a PCI master attempts a Memory Read with the same address
**		as the previous Memory Read Multiple. When the Read Command Bit is set, the ATU would return
**		the read data from the DRC queue and consider the Delayed Read transaction complete. When the
**		Read Command bit in the ATUCR was clear, the ATU would not return data since the PCI read
**		commands did not match, only the address.
**************************************************************************
*/
/*
**************************************************************************
**                    Inbound Write Transaction
**========================================================================
**	  An inbound write transaction is initiated by a PCI master and is targeted at either 80331 local
**	  memory or a 80331 memory-mapped register.
**	Data flow for an inbound write transaction on the PCI bus is summarized as:
**	¡E The ATU claims the PCI write transaction when the PCI address is within the inbound
**	  translation window defined by the ATU Inbound Base Address Register (and Inbound Upper
**	  Base Address Register during DACs) and Inbound Limit Register.
**	¡E When the IWADQ has at least one address entry available and the IWQ has at least one buffer
**	  available, the address is captured and the first data phase is accepted.
**	¡E The PCI interface continues to accept write data until one of the following is true:
**	  ¡X The initiator performs a disconnect.
**	  ¡X The transaction crosses a buffer boundary.
**	¡E When an address parity error is detected during the address phase of the transaction, the
**	  address parity error mechanisms are used. Refer to Section 3.7.1 for details of the address
**	  parity error response.
**	¡E When operating in the PCI-X mode when an attribute parity error is detected, the attribute
**	  parity error mechanism described in Section 3.7.1 is used.
**	¡E When a data parity error is detected while accepting data, the slave interface sets the
**	  appropriate bits based on PCI specifications. No other action is taken. Refer to Section 3.7.2.6
**	  for details of the inbound write data parity error response.
**	  Once the PCI interface places a PCI address in the IWADQ, when IWQ has received data sufficient
**	  to cross a buffer boundary or the master disconnects on the PCI bus, the ATUs internal bus
**	  interface becomes aware of the inbound write. When there are additional write transactions ahead
**	  in the IWQ/IWADQ, the current transaction remains posted until ordering and priority have been
**	  satisfied (Refer to Section 3.5.3) and the transaction is attempted on the internal bus by the ATU
**	  internal master interface. The ATU does not insert target wait states nor do data merging on the PCI
**	  interface, when operating in the PCI mode.
**	  In the PCI-X mode memory writes are always executed as immediate transactions, while
**	  configuration write transactions are processed as split transactions. The ATU generates a Split
**	  Completion Message, (with Message class=0h - Write Completion Class and Message index =
**	  00h - Write Completion Message) once a configuration write is successfully executed.
**	  Also, when operating in the PCI-X mode a write sequence may contain multiple write transactions.
**	  The ATU handles such transactions as independent transactions.
**	  Data flow for the inbound write transaction on the internal bus is summarized as:
**	¡E The ATU internal bus master requests the internal bus when IWADQ has at least one entry
**	  with associated data in the IWQ.
**	¡E When the internal bus is granted, the internal bus master interface initiates the write
**	  transaction by driving the translated address onto the internal bus. For details on inbound
**	  address translation.
**	¡E When IB_DEVSEL# is not returned, a master abort condition is signaled on the internal bus.
**	  The current transaction is flushed from the queue and SERR# may be asserted on the PCI
**	  interface.
**	¡E The ATU initiator interface asserts IB_REQ64# to attempt a 64-bit transfer. When
**	  IB_ACK64# is not returned, a 32-bit transfer is used. Transfers of less than 64-bits use the
**	  IB_C/BE[7:0]# to mask the bytes not written in the 64-bit data phase. Write data is transferred
**	  from the IWQ to the internal bus when data is available and the internal bus interface retains
**	  internal bus ownership.
**	¡E The internal bus interface stops transferring data from the current transaction to the internal
**	  bus when one of the following conditions becomes true:
**	¡X The internal bus initiator interface loses bus ownership. The ATU internal initiator
**	  terminates the transfer (initiator disconnection) at the next ADB (for the internal bus ADB
**	  is defined as a naturally aligned 128-byte boundary) and attempt to reacquire the bus to
**	  complete the delivery of remaining data using the same sequence ID but with the
**	  modified starting address and byte count.
**	¡X A Disconnect at Next ADB is signaled on the internal bus from the internal target. When
**	  the transaction in the IWQ completes at that ADB, the initiator returns to idle. When the
**	  transaction in the IWQ is not complete, the initiator attempts to reacquire the bus to
**	  complete the delivery of remaining data using the same sequence ID but with the
**	  modified starting address and byte count.
**	¡X A Single Data Phase Disconnect is signaled on the internal bus from the internal target.
**	  When the transaction in the IWQ needs only a single data phase, the master returns to idle.
**	  When the transaction in the IWQ is not complete, the initiator attempts to reacquire the
**	  bus to complete the delivery of remaining data using the same sequence ID but with the
**	  modified starting address and byte count.
**	¡X The data from the current transaction has completed (satisfaction of byte count). An
**	  initiator termination is performed and the bus returns to idle.
**	¡X A Master Abort is signaled on the internal bus. SERR# may be asserted on the PCI bus.
**	  Data is flushed from the IWQ.
*****************************************************************
*/



/*
**************************************************************************
**               Inbound Read Completions Data Parity Errors
**========================================================================
**	As an initiator, the ATU may encounter this error condition when operating in the PCI-X mode.
**	When as the completer of a Split Read Request the ATU observes PERR# assertion during the split
**	completion transaction, the ATU attempts to complete the transaction normally and no further
**	action is taken.
**************************************************************************
*/

/*
**************************************************************************
**               Inbound Configuration Write Completion Message Data Parity Errors
**========================================================================
**  As an initiator, the ATU may encounter this error condition when operating in the PCI-X mode.
**  When as the completer of a Configuration (Split) Write Request the ATU observes PERR#
**  assertion during the split completion transaction, the ATU attempts to complete the transaction
**  normally and no further action is taken.
**************************************************************************
*/

/*
**************************************************************************
**              Inbound Read Request Data Parity Errors
**===================== Immediate Data Transfer ==========================
**  As a target, the ATU may encounter this error when operating in the Conventional PCI or PCI-X modes.
**  Inbound read data parity errors occur when read data delivered from the IRQ is detected as having
**  bad parity by the initiator of the transaction who is receiving the data. The initiator may optionally
**  report the error to the system by asserting PERR#. As a target device in this scenario, no action is
**  required and no error bits are set.
**=====================Split Response Termination=========================
**  As a target, the ATU may encounter this error when operating in the PCI-X mode.
**  Inbound read data parity errors occur during the Split Response Termination. The initiator may
**  optionally report the error to the system by asserting PERR#. As a target device in this scenario, no
**  action is required and no error bits are set.
**************************************************************************
*/

/*
**************************************************************************
**              Inbound Write Request Data Parity Errors
**========================================================================
**	As a target, the ATU may encounter this error when operating in the Conventional or PCI-X modes.
**	Data parity errors occurring during write operations received by the ATU may assert PERR# on
**	the PCI Bus. When an error occurs, the ATU continues accepting data until the initiator of the write
**	transaction completes or a queue fill condition is reached. Specifically, the following actions with
**	the given constraints are taken by the ATU:
**	¡E PERR# is asserted two clocks cycles (three clock cycles when operating in the PCI-X mode)
**	following the data phase in which the data parity error is detected on the bus. This is only
**	done when the Parity Error Response bit in the ATUCMD is set.
**	¡E The Detected Parity Error bit in the ATUSR is set. When the ATU sets this bit, additional
**	actions is taken:
**	¡X When the ATU Detected Parity Error Interrupt Mask bit in the ATUIMR is clear, set the
**	Detected Parity Error bit in the ATUISR. When set, no action.
***************************************************************************
*/


/*
***************************************************************************
**                 Inbound Configuration Write Request
**  =====================================================================
**  As a target, the ATU may encounter this error when operating in the Conventional or PCI-X modes.
**  ===============================================
**              Conventional PCI Mode
**  ===============================================
**  To allow for correct data parity calculations for delayed write transactions, the ATU delays the
**  assertion of STOP# (signalling a Retry) until PAR is driven by the master. A parity error during a
**  delayed write transaction (inbound configuration write cycle) can occur in any of the following
**  parts of the transactions:
**  ¡E During the initial Delayed Write Request cycle on the PCI bus when the ATU latches the
**  address/command and data for delayed delivery to the internal configuration register.
**  ¡E During the Delayed Write Completion cycle on the PCI bus when the ATU delivers the status
**  of the operation back to the original master.
**  The 80331 ATU PCI interface has the following responses to a delayed write parity error for
**  inbound transactions during Delayed Write Request cycles with the given constraints:
**  ¡E When the Parity Error Response bit in the ATUCMD is set, the ATU asserts TRDY#
**  (disconnects with data) and two clock cycles later asserts PERR# notifying the initiator of the
**  parity error. The delayed write cycle is not enqueued and forwarded to the internal bus.
**  When the Parity Error Response bit in the ATUCMD is cleared, the ATU retries the
**  transaction by asserting STOP# and enqueues the Delayed Write Request cycle to be
**  forwarded to the internal bus. PERR# is not asserted.
**  ¡E The Detected Parity Error bit in the ATUSR is set. When the ATU sets this bit, additional
**  actions is taken:
**  ¡X When the ATU Detected Parity Error Interrupt Mask bit in the ATUIMR is clear, set the
**  Detected Parity Error bit in the ATUISR. When set, no action.
**  For the original write transaction to be completed, the initiator retries the transaction on the PCI
**  bus and the ATU returns the status from the internal bus, completing the transaction.
**  For the Delayed Write Completion transaction on the PCI bus where a data parity error occurs and
**  therefore does not agree with the status being returned from the internal bus (i.e. status being
**  returned is normal completion) the ATU performs the following actions with the given constraints:
**  ¡E When the Parity Error Response Bit is set in the ATUCMD, the ATU asserts TRDY#
**  (disconnects with data) and two clocks later asserts PERR#. The Delayed Completion cycle in
**  the IDWQ remains since the data of retried command did not match the data within the queue.
**  ¡E The Detected Parity Error bit in the ATUSR is set. When the ATU sets this bit, additional
**  actions is taken:
**  ¡X When the ATU Detected Parity Error Interrupt Mask bit in the ATUIMR is clear, set the
**  Detected Parity Error bit in the ATUISR. When set, no action.
**  =================================================== 
**                       PCI-X Mode
**  ===================================================
**  Data parity errors occurring during configuration write operations received by the ATU may cause
**  PERR# assertion and delivery of a Split Completion Error Message on the PCI Bus. When an error
**  occurs, the ATU accepts the write data and complete with a Split Response Termination.
**  Specifically, the following actions with the given constraints are then taken by the ATU:
**  ¡E When the Parity Error Response bit in the ATUCMD is set, PERR# is asserted three clocks
**  cycles following the Split Response Termination in which the data parity error is detected on
**  the bus. When the ATU asserts PERR#, additional actions is taken:
**  ¡X A Split Write Data Parity Error message (with message class=2h - completer error and
**  message index=01h - Split Write Data Parity Error) is initiated by the ATU on the PCI bus
**  that addresses the requester of the configuration write.
**  ¡X When the Initiated Split Completion Error Message Interrupt Mask in the ATUIMR is
**  clear, set the Initiated Split Completion Error Message bit in the ATUISR. When set, no
**  action.
**  ¡X The Split Write Request is not enqueued and forwarded to the internal bus.
**  ¡E The Detected Parity Error bit in the ATUSR is set. When the ATU sets this bit, additional
**  actions is taken:
**  ¡X When the ATU Detected Parity Error Interrupt Mask bit in the ATUIMR is clear, set the
**  Detected Parity Error bit in the ATUISR. When set, no action.
**
***************************************************************************
*/

/*
***************************************************************************
**                       Split Completion Messages
**  =======================================================================
**  As a target, the ATU may encounter this error when operating in the PCI-X mode.
**  Data parity errors occurring during Split Completion Messages claimed by the ATU may assert
**  PERR# (when enabled) or SERR# (when enabled) on the PCI Bus. When an error occurs, the
**  ATU accepts the data and complete normally. Specifically, the following actions with the given
**  constraints are taken by the ATU:
**  ¡E PERR# is asserted three clocks cycles following the data phase in which the data parity error
**  is detected on the bus. This is only done when the Parity Error Response bit in the ATUCMD
**  is set. When the ATU asserts PERR#, additional actions is taken:
**  ¡X The Master Parity Error bit in the ATUSR is set.
**  ¡X When the ATU PCI Master Parity Error Interrupt Mask Bit in the ATUIMR is clear, set the
**  PCI Master Parity Error bit in the ATUISR. When set, no action.
**  ¡X When the SERR# Enable bit in the ATUCMD is set, and the Data Parity Error Recover
**  Enable bit in the PCIXCMD register is clear, assert SERR#; otherwise no action is taken.
**  When the ATU asserts SERR#, additional actions is taken:
**  Set the SERR# Asserted bit in the ATUSR.
**  When the ATU SERR# Asserted Interrupt Mask Bit in the ATUIMR is clear, set the
**  SERR# Asserted bit in the ATUISR. When set, no action.
**  When the ATU SERR# Detected Interrupt Enable Bit in the ATUCR is set, set the
**  SERR# Detected bit in the ATUISR. When clear, no action.
**  ¡E When the SCE bit (Split Completion Error -- bit 30 of the Completer Attributes) is set during
**  the Attribute phase, the Received Split Completion Error Message bit in the PCIXSR is set.
**  When the ATU sets this bit, additional actions is taken:
**  ¡X When the ATU Received Split Completion Error Message Interrupt Mask bit in the
**  ATUIMR is clear, set the Received Split Completion Error Message bit in the ATUISR.
**  When set, no action.
**  ¡E The Detected Parity Error bit in the ATUSR is set. When the ATU sets this bit, additional
**  actions is taken:
**  ¡X When the ATU Detected Parity Error Interrupt Mask bit in the ATUIMR is clear, set the
**  Detected Parity Error bit in the ATUISR. When set, no action.
**  ¡E The transaction associated with the Split Completion Message is discarded.
**  ¡E When the discarded transaction was a read, a completion error message (with message
**  class=2h - completer error and message index=82h - PCI bus read parity error) is generated on
**  the internal bus of the 80331.
*****************************************************************************
*/


/*
******************************************************************************************************
**                 Messaging Unit (MU) of the Intel R 80331 I/O processor (80331)
**  ==================================================================================================
**	The Messaging Unit (MU) transfers data between the PCI system and the 80331 
**  notifies the respective system when new data arrives.
**	The PCI window for messaging transactions is always the first 4 Kbytes of the inbound translation.
**	window defined by: 
**                    1.Inbound ATU Base Address Register 0 (IABAR0) 
**                    2.Inbound ATU Limit Register 0 (IALR0)
**	All of the Messaging Unit errors are reported in the same manner as ATU errors. 
**  Error conditions and status can be found in :
**                                               1.ATUSR 
**                                               2.ATUISR
**====================================================================================================
**     Mechanism        Quantity               Assert PCI Interrupt Signals      Generate I/O Processor Interrupt
**----------------------------------------------------------------------------------------------------
**  Message Registers      2 Inbound                   Optional                              Optional
**                         2 Outbound                
**----------------------------------------------------------------------------------------------------
**  Doorbell Registers     1 Inbound                   Optional                              Optional
**                         1 Outbound  
**----------------------------------------------------------------------------------------------------
**  Circular Queues        4 Circular Queues           Under certain conditions              Under certain conditions
**----------------------------------------------------------------------------------------------------
**  Index Registers     1004 32-bit Memory Locations   No                                    Optional
**====================================================================================================
**     PCI Memory Map: First 4 Kbytes of the ATU Inbound PCI Address Space
**====================================================================================================
**  0000H           Reserved
**  0004H           Reserved
**  0008H           Reserved
**  000CH           Reserved
**------------------------------------------------------------------------
**  0010H 			Inbound Message Register 0              ]
**  0014H 			Inbound Message Register 1              ]
**  0018H 			Outbound Message Register 0             ]
**  001CH 			Outbound Message Register 1             ]   4 Message Registers
**------------------------------------------------------------------------
**  0020H 			Inbound Doorbell Register               ]
**  0024H 			Inbound Interrupt Status Register       ]
**  0028H 			Inbound Interrupt Mask Register         ]
**  002CH 			Outbound Doorbell Register              ]
**  0030H 			Outbound Interrupt Status Register      ]
**  0034H 			Outbound Interrupt Mask Register        ]   2 Doorbell Registers and 4 Interrupt Registers
**------------------------------------------------------------------------
**  0038H 			Reserved
**  003CH 			Reserved
**------------------------------------------------------------------------
**  0040H 			Inbound Queue Port                      ]
**  0044H 			Outbound Queue Port                     ]   2 Queue Ports
**------------------------------------------------------------------------
**  0048H 			Reserved
**  004CH 			Reserved
**------------------------------------------------------------------------
**  0050H                                                   ]
**    :                                                     ]
**    :      Intel Xscale Microarchitecture Local Memory    ]
**    :                                                     ]
**  0FFCH                                                   ]   1004 Index Registers
*******************************************************************************
*/
/*
*****************************************************************************
**                      Theory of MU Operation
*****************************************************************************
**--------------------
**   inbound_msgaddr0:
**   inbound_msgaddr1:
**  outbound_msgaddr0:
**  outbound_msgaddr1:
**  .  The MU has four independent messaging mechanisms.
**     There are four Message Registers that are similar to a combination of mailbox and doorbell registers. 
**     Each holds a 32-bit value and generates an interrupt when written.
**--------------------
**   inbound_doorbell:
**  outbound_doorbell:
**  .  The two Doorbell Registers support software interrupts. 
**     When a bit is set in a Doorbell Register, an interrupt is generated.
**--------------------
**  inbound_queueport:
** outbound_queueport:
**
**
**  .  The Circular Queues support a message passing scheme that uses 4 circular queues. 
**     The 4 circular queues are implemented in 80331 local memory. 
**     Two queues are used for inbound messages and two are used for outbound messages. 
**     Interrupts may be generated when the queue is written.
**--------------------
** local_buffer 0x0050 ....0x0FFF
**  .  The Index Registers use a portion of the 80331 local memory to implement a large set of message registers. 
**     When one of the Index Registers is written, an interrupt is generated and the address of the register written is captured.
**     Interrupt status for all interrupts is recorded in the Inbound Interrupt Status Register and the Outbound Interrupt Status Register. 
**     Each interrupt generated by the Messaging Unit can be masked.
**--------------------
**  .  Multi-DWORD PCI burst accesses are not supported by the Messaging Unit, 
**     with the exception of Multi-DWORD reads to the index registers. 
**     In Conventional mode: the MU terminates   Multi-DWORD PCI transactions 
**     (other than index register reads) with a disconnect at the next Qword boundary, with the exception of queue ports. 
**     In PCI-X mode       : the MU terminates a Multi-DWORD PCI read transaction with a Split Response 
**     and the data is returned through split completion transaction(s).
**     however, when the burst request crosses into or through the range of  offsets 40h to 4Ch 
**     (e.g., this includes the queue ports) the transaction is signaled target-abort immediately on the PCI bus. 
**     In PCI-X mode, Multi-DWORD PCI writes is signaled a Single-Data-Phase Disconnect 
**     which means that no data beyond the first Qword (Dword when the MU does not assert P_ACK64#) is written.
**--------------------
**  .  All registers needed to configure and control the Messaging Unit are memory-mapped registers.
**     The MU uses the first 4 Kbytes of the inbound translation window in the Address Translation Unit (ATU).
**     This PCI address window is used for PCI transactions that access the 80331 local memory.
**     The  PCI address of the inbound translation window is contained in the Inbound ATU Base Address Register.
**--------------------
**  .  From the PCI perspective, the Messaging Unit is part of the Address Translation Unit.
**     The Messaging Unit uses the PCI configuration registers of the ATU for control and status information.
**     The Messaging Unit must observe all PCI control bits in the ATU Command Register and ATU Configuration Register.
**     The Messaging Unit reports all PCI errors in the ATU Status Register.
**--------------------
**  .  Parts of the Messaging Unit can be accessed as a 64-bit PCI device. 
**     The register interface, message registers, doorbell registers, 
**     and index registers returns a P_ACK64# in response to a P_REQ64# on the PCI interface. 
**     Up to 1 Qword of data can be read or written per transaction (except Index Register reads). 
**     The Inbound and Outbound Queue Ports are always 32-bit addresses and the MU does not assert P_ACK64# to offsets 40H and 44H.
**************************************************************************
*/
/*
**************************************************************************
**  Message Registers
**  ==============================
**  . Messages can be sent and received by the 80331 through the use of the Message Registers. 
**  . When written, the message registers may cause an interrupt to be generated to either the Intel XScale core or the host processor.
**  . Inbound messages are sent by the host processor and received by the 80331.
**    Outbound messages are sent by the 80331 and received by the host processor.
**  . The interrupt status for outbound messages is recorded in the Outbound Interrupt Status Register.
**    Interrupt status for inbound messages is recorded in the Inbound Interrupt Status Register.
**
**  Inbound Messages:
**  -----------------
**  . When an inbound message register is written by an external PCI agent, an interrupt may be generated to the Intel XScale core. 
**  . The interrupt may be masked by the mask bits in the Inbound Interrupt Mask Register.
**  . The Intel XScale core interrupt is recorded in the Inbound Interrupt Status Register. 
**    The interrupt causes the Inbound Message Interrupt bit to be set in the Inbound Interrupt Status Register. 
**    This is a Read/Clear bit that is set by the MU hardware and cleared by software.
**    The interrupt is cleared when the Intel XScale core writes a value of 
**    1 to the Inbound Message Interrupt bit in the Inbound Interrupt Status Register.
**  ------------------------------------------------------------------------
**  Inbound Message Register - IMRx
**
**  . There are two Inbound Message Registers: IMR0 and IMR1. 
**  . When the IMR register is written, an interrupt to the Intel XScale core may be generated.
**    The interrupt is recorded in the Inbound Interrupt Status Register and may be masked 
**    by the Inbound Message Interrupt Mask bit in the Inbound Interrupt Mask Register.
**  -----------------------------------------------------------------
**  Bit       Default                       Description
**  31:00    0000 0000H                     Inbound Message - This is a 32-bit message written by an external PCI agent. 
**                                                            When written, an interrupt to the Intel XScale core may be generated.
**************************************************************************
*/
#define     ARCMSR_MU_INBOUND_MESSAGE_REG0		          0x10    /*dword 0x13,0x12,0x11,0x10*/
#define     ARCMSR_MU_INBOUND_MESSAGE_REG1		          0x14    /*dword 0x17,0x16,0x15,0x14*/
/*
**************************************************************************
**  Outbound Message Register - OMRx
**  --------------------------------
**  There are two Outbound Message Registers: OMR0 and OMR1. When the OMR register is
**  written, a PCI interrupt may be generated. The interrupt is recorded in the Outbound Interrupt
**  Status Register and may be masked by the Outbound Message Interrupt Mask bit in the Outbound
**  Interrupt Mask Register.
**
**  Bit       Default                       Description
**  31:00    00000000H                      Outbound Message - This is 32-bit message written by the Intel  XScale  core. When written, an
**                                                             interrupt may be generated on the PCI Interrupt pin determined by the ATU Interrupt Pin Register.
**************************************************************************
*/
#define     ARCMSR_MU_OUTBOUND_MESSAGE_REG0		          0x18    /*dword 0x1B,0x1A,0x19,0x18*/
#define     ARCMSR_MU_OUTBOUND_MESSAGE_REG1		          0x1C    /*dword 0x1F,0x1E,0x1D,0x1C*/
/*
**************************************************************************
**        Doorbell Registers
**  ==============================
**  There are two Doorbell Registers: 
**                                  Inbound Doorbell Register 
**                                  Outbound Doorbell Register
**  The Inbound Doorbell Register allows external PCI agents to generate interrupts to the Intel R XScale core. 
**  The Outbound Doorbell Register allows the Intel R XScale core to generate a PCI interrupt. 
**  Both Doorbell Registers may generate interrupts whenever a bit in the register is set.
**
**  Inbound Doorbells:
**  ------------------
**  . When the Inbound Doorbell Register is written by an external PCI agent, an interrupt may be generated to the Intel R XScale  core.
**    An interrupt is generated when any of the bits in the doorbell register is written to a value of 1.
**    Writing a value of 0 to any bit does not change the value of that bit and does not cause an interrupt to be generated. 
**  . Once a bit is set in the Inbound Doorbell Register, it cannot be cleared by any external PCI agent. 
**    The interrupt is recorded in the Inbound Interrupt Status Register.
**  . The interrupt may be masked by the Inbound Doorbell Interrupt mask bit in the Inbound Interrupt Mask Register.
**    When the mask bit is set for a particular bit, no interrupt is generated for that bit.
**    The Inbound Interrupt Mask Register affects only the generation of the normal messaging unit interrupt
**    and not the values written to the Inbound Doorbell Register. 
**    One bit in the Inbound Doorbell Register is reserved for an Error Doorbell interrupt.
**  . The interrupt is cleared when the Intel R XScale core writes a value of 1 to the bits in the Inbound Doorbell Register that are set. 
**    Writing a value of 0 to any bit does not change the value of that bit and does not clear the interrupt.
**  ------------------------------------------------------------------------
**  Inbound Doorbell Register - IDR
**
**  . The Inbound Doorbell Register (IDR) is used to generate interrupts to the Intel XScale core. 
**  . Bit 31 is reserved for generating an Error Doorbell interrupt. 
**    When bit 31 is set, an Error interrupt may be generated to the Intel XScale core. 
**    All other bits, when set, cause the Normal Messaging Unit interrupt line of the Intel XScale core to be asserted, 
**    when the interrupt is not masked by the Inbound Doorbell Interrupt Mask bit in the Inbound Interrupt Mask Register.
**    The bits in the IDR register can only be set by an external PCI agent and can only be cleared by the Intel XScale  core.
**  ------------------------------------------------------------------------
**  Bit       Default                       Description
**  31          0 2                         Error Interrupt - Generate an Error Interrupt to the Intel XScale core.
**  30:00    00000000H                      Normal Interrupt - When any bit is set, generate a Normal interrupt to the Intel XScale core. 
**                                                             When all bits are clear, do not generate a Normal Interrupt.
**************************************************************************
*/
#define     ARCMSR_MU_INBOUND_DOORBELL_REG		          0x20    /*dword 0x23,0x22,0x21,0x20*/
/*
**************************************************************************
**  Inbound Interrupt Status Register - IISR
**
**  . The Inbound Interrupt Status Register (IISR) contains hardware interrupt status. 
**    It records the status of Intel XScale core interrupts generated by the Message Registers, Doorbell Registers, and the Circular Queues. 
**    All interrupts are routed to the Normal Messaging Unit interrupt input of the Intel XScale core, 
**    except for the Error Doorbell Interrupt and the Outbound Free Queue Full interrupt; 
**    these two are routed to the Messaging Unit Error interrupt input. 
**    The generation of interrupts recorded in the Inbound Interrupt Status Register 
**    may be masked by setting the corresponding bit in the Inbound Interrupt Mask Register. 
**    Some of the bits in this register are Read Only. 
**    For those bits, the interrupt must be cleared through another register.
**
**  Bit       Default                       Description
**  31:07    0000000H 0 2                   Reserved
**  06          0 2              Index Register Interrupt - This bit is set by the MU hardware 
**                               when an Index Register has been written after a PCI transaction.
**  05          0 2              Outbound Free Queue Full Interrupt - This bit is set 
**                               when the Outbound Free Head Pointer becomes equal to the Tail Pointer and the queue is full. 
**                               An Error interrupt is generated for this condition.
**  04          0 2              Inbound Post Queue Interrupt - This bit is set by the MU hardware when the Inbound Post Queue has been written. 
**                               Once cleared, an interrupt does NOT be generated 
**                               when the head and tail pointers remain unequal (i.e. queue status is Not Empty).
**                               Therefore, when software leaves any unprocessed messages in the post queue when the interrupt is cleared, 
**                               software must retain the information that the Inbound Post queue status is not empty.
**          NOTE: This interrupt is provided with dedicated support in the 80331 Interrupt Controller.
**  03          0 2              Error Doorbell Interrupt - This bit is set when the Error Interrupt of the Inbound Doorbell Register is set.
**                               To clear this bit (and the interrupt), the Error Interrupt bit of the Inbound Doorbell Register must be clear.
**  02          0 2              Inbound Doorbell Interrupt - This bit is set when at least one 
**                               Normal Interrupt bit in the Inbound Doorbell Register is set.
**                               To clear this bit (and the interrupt), the Normal Interrupt bits in the Inbound Doorbell Register must all be clear.
**  01          0 2              Inbound Message 1 Interrupt - This bit is set by the MU hardware when the Inbound Message 1 Register has been written.
**  00          0 2              Inbound Message 0 Interrupt - This bit is set by the MU hardware when the Inbound Message 0 Register has been written.
**************************************************************************
*/
#define     ARCMSR_MU_INBOUND_INTERRUPT_STATUS_REG	      0x24    /*dword 0x27,0x26,0x25,0x24*/
#define     ARCMSR_MU_INBOUND_INDEX_INT                      0x40
#define     ARCMSR_MU_INBOUND_QUEUEFULL_INT                  0x20
#define     ARCMSR_MU_INBOUND_POSTQUEUE_INT                  0x10         
#define     ARCMSR_MU_INBOUND_ERROR_DOORBELL_INT             0x08
#define     ARCMSR_MU_INBOUND_DOORBELL_INT                   0x04
#define     ARCMSR_MU_INBOUND_MESSAGE1_INT                   0x02
#define     ARCMSR_MU_INBOUND_MESSAGE0_INT                   0x01
/*
**************************************************************************
**  Inbound Interrupt Mask Register - IIMR
**
**  . The Inbound Interrupt Mask Register (IIMR) provides the ability to mask Intel XScale core interrupts generated by the Messaging Unit. 
**    Each bit in the Mask register corresponds to an interrupt bit in the Inbound Interrupt Status Register.
**    Setting or clearing bits in this register does not affect the Inbound Interrupt Status Register. 
**    They only affect the generation of the Intel XScale core interrupt.
**  ------------------------------------------------------------------------
**  Bit       Default                       Description
**  31:07     000000H 0 2                   Reserved
**  06        0 2               Index Register Interrupt Mask - When set, this bit masks the interrupt generated by the MU hardware 
**				when an Index Register has been written after a PCI transaction.
**  05        0 2               Outbound Free Queue Full Interrupt Mask - When set, this bit masks the Error interrupt generated 
**				when the Outbound Free Head Pointer becomes equal to the Tail Pointer and the queue is full.
**  04        0 2               Inbound Post Queue Interrupt Mask - When set, this bit masks the interrupt generated 
**				by the MU hardware when the Inbound Post Queue has been written.
**  03        0 2               Error Doorbell Interrupt Mask - When set, this bit masks the Error Interrupt 
**				when the Error Interrupt bit of the Inbound Doorbell Register is set.
**  02        0 2               Inbound Doorbell Interrupt Mask - When set, this bit masks the interrupt generated 
**				when at least one Normal Interrupt bit in the Inbound Doorbell Register is set.
**  01        0 2               Inbound Message 1 Interrupt Mask - When set, this bit masks the Inbound Message 1 
**				Interrupt generated by a write to the Inbound Message 1 Register.
**  00        0 2               Inbound Message 0 Interrupt Mask - When set, 
**                              this bit masks the Inbound Message 0 Interrupt generated by a write to the Inbound Message 0 Register.
**************************************************************************
*/
#define     ARCMSR_MU_INBOUND_INTERRUPT_MASK_REG	      0x28    /*dword 0x2B,0x2A,0x29,0x28*/
#define     ARCMSR_MU_INBOUND_INDEX_INTMASKENABLE               0x40
#define     ARCMSR_MU_INBOUND_QUEUEFULL_INTMASKENABLE           0x20
#define     ARCMSR_MU_INBOUND_POSTQUEUE_INTMASKENABLE           0x10         
#define     ARCMSR_MU_INBOUND_DOORBELL_ERROR_INTMASKENABLE      0x08
#define     ARCMSR_MU_INBOUND_DOORBELL_INTMASKENABLE            0x04
#define     ARCMSR_MU_INBOUND_MESSAGE1_INTMASKENABLE            0x02
#define     ARCMSR_MU_INBOUND_MESSAGE0_INTMASKENABLE            0x01
/*
**************************************************************************
**  Outbound Doorbell Register - ODR
**
**  The Outbound Doorbell Register (ODR) allows software interrupt generation. It allows the Intel 
**  XScale  core to generate PCI interrupts to the host processor by writing to this register. The
**  generation of PCI interrupts through the Outbound Doorbell Register may be masked by setting the
**  Outbound Doorbell Interrupt Mask bit in the Outbound Interrupt Mask Register.
**  The Software Interrupt bits in this register can only be set by the Intel  XScale  core and can only
**  be cleared by an external PCI agent.
**  ----------------------------------------------------------------------
**  Bit       Default                       Description
**  31          0 2                          Reserved
**  30          0 2                          Reserved.
**  29          0 2                          Reserved
**  28       0000 0000H                      PCI Interrupt - When set, this bit causes the P_INTC# interrupt output 
**                                                           (P_INTA# with BRG_EN and ARB_EN straps low)
**                                                           signal to be asserted or a Message-signaled Interrupt is generated (when enabled). 
**                                                           When this bit is cleared, the P_INTC# interrupt output 
**                                                           (P_INTA# with BRG_EN and ARB_EN straps low) 
**                                                           signal is deasserted.
**  27:00     000 0000H                      Software Interrupts - When any bit is set the P_INTC# interrupt output 
**                                           (P_INTA# with BRG_EN and ARB_EN straps low) 
**                                           signal is asserted or a Message-signaled Interrupt is generated (when enabled).
**                                           When all bits are cleared, the P_INTC# interrupt output (P_INTA# with BRG_EN and ARB_EN straps low)
**                                           signal is deasserted.
**************************************************************************
*/
#define     ARCMSR_MU_OUTBOUND_DOORBELL_REG		          0x2C    /*dword 0x2F,0x2E,0x2D,0x2C*/
/*
**************************************************************************
**  Outbound Interrupt Status Register - OISR
**
**  The Outbound Interrupt Status Register (OISR) contains hardware interrupt status. It records the
**  status of PCI interrupts generated by the Message Registers, Doorbell Registers, and the Circular
**  Queues. The generation of PCI interrupts recorded in the Outbound Interrupt Status Register may
**  be masked by setting the corresponding bit in the Outbound Interrupt Mask Register. Some of the
**  bits in this register are Read Only. For those bits, the interrupt must be cleared through another
**  register.
**  ----------------------------------------------------------------------
**  Bit       Default                       Description
**  31:05     000000H 000 2                 Reserved
**  04        0 2                           PCI Interrupt - This bit is set when the PCI Interrupt bit (bit 28) is set in the Outbound Doorbell Register.
**                                                          To clear this bit (and the interrupt), the PCI Interrupt bit must be cleared.
**  03        0 2                           Outbound Post Queue Interrupt - This bit is set when data in the prefetch buffer is valid. This bit is
**                                                          cleared when any prefetch data has been read from the Outbound Queue Port.
**  02        0 2                           Outbound Doorbell Interrupt - This bit is set when at least one Software Interrupt bit in the Outbound
**                                          Doorbell Register is set. To clear this bit (and the interrupt), the Software Interrupt bits in the Outbound
**                                          Doorbell Register must all be clear.
**  01        0 2                           Outbound Message 1 Interrupt - This bit is set by the MU when the Outbound Message 1 Register is
**                                                          written. Clearing this bit clears the interrupt.
**  00        0 2                           Outbound Message 0 Interrupt - This bit is set by the MU when the Outbound Message 0 Register is
**                                                          written. Clearing this bit clears the interrupt.
**************************************************************************
*/
#define     ARCMSR_MU_OUTBOUND_INTERRUPT_STATUS_REG	      0x30    /*dword 0x33,0x32,0x31,0x30*/
#define     ARCMSR_MU_OUTBOUND_PCI_INT       	              0x10
#define     ARCMSR_MU_OUTBOUND_POSTQUEUE_INT    	          0x08 
#define     ARCMSR_MU_OUTBOUND_DOORBELL_INT 		          0x04 
#define     ARCMSR_MU_OUTBOUND_MESSAGE1_INT 		          0x02 
#define     ARCMSR_MU_OUTBOUND_MESSAGE0_INT 		          0x01 
/*
**************************************************************************
**  Outbound Interrupt Mask Register - OIMR
**  The Outbound Interrupt Mask Register (OIMR) provides the ability to mask outbound PCI
**  interrupts generated by the Messaging Unit. Each bit in the mask register corresponds to a
**  hardware interrupt bit in the Outbound Interrupt Status Register. When the bit is set, the PCI
**  interrupt is not generated. When the bit is clear, the interrupt is allowed to be generated.
**  Setting or clearing bits in this register does not affect the Outbound Interrupt Status Register. They
**  only affect the generation of the PCI interrupt.
**  ----------------------------------------------------------------------
**  Bit       Default                       Description
**  31:05     000000H                       Reserved
**  04          0 2                         PCI Interrupt Mask - When set, this bit masks the interrupt generation when the PCI Interrupt bit (bit 28)
**                                                               in the Outbound Doorbell Register is set.
**  03          0 2                         Outbound Post Queue Interrupt Mask - When set, this bit masks the interrupt generated when data in
**                                                               the prefetch buffer is valid.
**  02          0 2                         Outbound Doorbell Interrupt Mask - When set, this bit masks the interrupt generated by the Outbound
**                                                               Doorbell Register.
**  01          0 2                         Outbound Message 1 Interrupt Mask - When set, this bit masks the Outbound Message 1 Interrupt
**                                                               generated by a write to the Outbound Message 1 Register.
**  00          0 2                         Outbound Message 0 Interrupt Mask- When set, this bit masks the Outbound Message 0 Interrupt
**                                                               generated by a write to the Outbound Message 0 Register.
**************************************************************************
*/
#define     ARCMSR_MU_OUTBOUND_INTERRUPT_MASK_REG		  0x34    /*dword 0x37,0x36,0x35,0x34*/
#define     ARCMSR_MU_OUTBOUND_PCI_INTMASKENABLE   	          0x10
#define     ARCMSR_MU_OUTBOUND_POSTQUEUE_INTMASKENABLE	      0x08 
#define     ARCMSR_MU_OUTBOUND_DOORBELL_INTMASKENABLE		  0x04 
#define     ARCMSR_MU_OUTBOUND_MESSAGE1_INTMASKENABLE		  0x02 
#define     ARCMSR_MU_OUTBOUND_MESSAGE0_INTMASKENABLE		  0x01 
#define     ARCMSR_MU_OUTBOUND_ALL_INTMASKENABLE		      0x1F 
/*
**************************************************************************
**
**************************************************************************
*/
#define     ARCMSR_MU_INBOUND_QUEUE_PORT_REG        	  0x40    /*dword 0x43,0x42,0x41,0x40*/
#define     ARCMSR_MU_OUTBOUND_QUEUE_PORT_REG  	          0x44    /*dword 0x47,0x46,0x45,0x44*/
/*
**************************************************************************
**                          Circular Queues
**  ======================================================================
**  The MU implements four circular queues. There are 2 inbound queues and 2 outbound queues. In
**  this case, inbound and outbound refer to the direction of the flow of posted messages.
**  Inbound messages are either:
**  						¡E posted messages by other processors for the Intel XScale core to process or
**  						¡E free (or empty) messages that can be reused by other processors.
**  Outbound messages are either:
** 							¡E posted messages by the Intel XScale core for other processors to process or
** 							¡E free (or empty) messages that can be reused by the Intel XScale core.
**  Therefore, free inbound messages flow away from the 80331 and free outbound messages flow toward the 80331.
**  The four Circular Queues are used to pass messages in the following manner. 
**  	. The two inbound queues are used to handle inbound messages 
**  	  and the two outbound queues are used to handle  outbound messages. 
**  	. One of the inbound queues is designated the Free queue and it contains inbound free messages. 
**  	  The other inbound queue is designated the Post queue and it contains inbound posted messages.
**  	  Similarly, one of the outbound queues is designated the Free queue and the other outbound queue is designated the Post queue. 
**
**  =============================================================================================================
**  Circular Queue Summary
**   _____________________________________________________________________________________________________________
**  |    Queue Name        |                     Purpose                                |  Action on PCI Interface|
**  |______________________|____________________________________________________________|_________________________|
**  |Inbound Post  Queue   |    Queue for inbound messages from other processors        |          Written        |
**  |                      |     waiting to be processed by the 80331                   |                         |
**  |Inbound Free  Queue   |    Queue for empty inbound messages from the 80331         |          Read           |
**  |                      |    available for use by other processors                   |                         |
**  |Outbound Post Queue   |    Queue for outbound messages from the 80331              |          Read           |
**  |                      |    that are being posted to the other processors           |                         |
**  |Outbound Free Queue   |    Queue for empty outbound messages from other processors |          Written        |
**  |                      |    available for use by the 80331                          |                         |
**  |______________________|____________________________________________________________|_________________________|
**
**  . The two inbound queues allow the host processor to post inbound messages for the 80331 in one
**    queue and to receive free messages returning from the 80331. 
**    The host processor posts inbound messages, 
**    the Intel XScale core receives the posted message and when it is finished with the message,
**    places it back on the inbound free queue for reuse by the host processor.
**
**  The circular queues are accessed by external PCI agents through two port locations in the PCI
**  address space: 
**              Inbound Queue Port 
**          and Outbound Queue Port. 
**  The Inbound Queue Port is used by external PCI agents to read the Inbound Free Queue and write the Inbound Post Queue. 
**  The Outbound Queue Port is used by external PCI agents to read the Outbound Post Queue and write the Outbound Free Queue.
**  Note that a PCI transaction to the inbound or outbound queue ports with null byte enables (P_C/BE[3:0]#=1111 2 ) 
**  does not cause the MU hardware to increment the queue pointers. 
**  This is treated as when the PCI transaction did not occur. 
**  The Inbound and Outbound Queue Ports never respond with P_ACK64# on the PCI interface.
**  ======================================================================================
**  Overview of Circular Queue Operation
**  ======================================================================================
**  . The data storage for the circular queues must be provided by the 80331 local memory.
**  . The base address of the circular queues is contained in the Queue Base Address Register.
**    Each entry in the queue is a 32-bit data value. 
**  . Each read from or write to the queue may access only one queue entry. 
**  . Multi-DWORD accesses to the circular queues are not allowed. 
**    Sub-DWORD accesses are promoted to DWORD accesses.
**  . Each circular queue has a head pointer and a tail pointer. 
**    The pointers are offsets from the Queue Base Address.
**  . Writes to a queue occur at the head of the queue and reads occur from the tail. 
**    The head and tail pointers are incremented by either the Intel XScale core or the Messaging Unit hardware.
**    Which unit maintains the pointer is determined by the writer of the queue. 
**    More details about the pointers are given in the queue descriptions below. 
**    The pointers are incremented after the queue access.
**    Both pointers wrap around to the first address of the circular queue when they reach the circular queue size.
**
**  Messaging Unit...
**
**  The Messaging Unit generates an interrupt to the Intel XScale core or generate a PCI interrupt under certain conditions.
**  . In general, when a Post queue is written, an interrupt is generated to notify the receiver that a message was posted.
**    The size of each circular queue can range from 4K entries (16 Kbytes) to 64K entries (256 Kbytes).
**  . All four queues must be the same size and may be contiguous. 
**    Therefore, the total amount of local memory needed by the circular queues ranges from 64 Kbytes to 1 Mbytes. 
**    The Queue size is determined by the Queue Size field in the MU Configuration Register.
**  . There is one base address for all four queues. 
**    It is stored in the Queue Base Address Register (QBAR).
**    The starting addresses of each queue is based on the Queue Base Address and the Queue Size field. 
**    here shows an example of how the circular queues should be set up based on the
**    Intelligent I/O (I 2 O) Architecture Specification. 
**    Other ordering of the circular queues is possible.
**
**  				Queue                           Starting Address
**  				Inbound Free Queue              QBAR
**  				Inbound Post Queue              QBAR + Queue Size
**  				Outbound Post Queue             QBAR + 2 * Queue Size
**  				Outbound Free Queue             QBAR + 3 * Queue Size
**  ===================================================================================
**  Inbound Post Queue
**  ------------------
**  The Inbound Post Queue holds posted messages placed there by other processors for the Intel XScale core to process.
**  This queue is read from the queue tail by the Intel XScale core. It is written to the queue head by external PCI agents. 
**  The tail pointer is maintained by the Intel XScale core. The head pointer is maintained by the MU hardware.
**  For a PCI write transaction that accesses the Inbound Queue Port, 
**  the MU writes the data to the local memory location address in the Inbound Post Head Pointer Register.
**  When the data written to the Inbound Queue Port is written to local memory, the MU hardware increments the Inbound Post Head Pointer Register.
**  An Intel XScale core interrupt may be generated when the Inbound Post Queue is written. 
**  The Inbound Post Queue Interrupt bit in the Inbound Interrupt Status Register indicates the interrupt status.
**  The interrupt is cleared when the Inbound Post Queue Interrupt bit is cleared. 
**  The interrupt can be masked by the Inbound Interrupt Mask Register. 
**  Software must be aware of the state of the Inbound Post Queue Interrupt Mask bit to guarantee 
**  that the full condition is recognized by the core processor.
**  In addition, to guarantee that the queue does not get overwritten, 
**  software must process messages from the tail of the queue before incrementing the tail pointer and clearing this interrupt.
**  Once cleared, an interrupt is NOT generated when the head and tail pointers remain unequal (i.e. queue status is Not Empty). 
**  Only a new message posting the in the inbound queue generates a new interrupt. 
**  Therefore, when software leaves any unprocessed messages in the post queue when the interrupt is cleared, 
**  software must retain the information that the Inbound Post queue status.
**  From the time that the PCI write transaction is received until the data is written 
**  in local memory and the Inbound Post Head Pointer Register is incremented, 
**  any PCI transaction that attempts to access the Inbound Post Queue Port is signalled a Retry.
**  The Intel XScale core may read messages from the Inbound Post Queue 
**  by reading the data from the local memory location pointed to by the Inbound Post Tail Pointer Register. 
**  The Intel XScale core must then increment the Inbound Post Tail Pointer Register. 
**  When the Inbound Post Queue is full (head and tail pointers are equal and the head pointer was last updated by hardware), 
**  the hardware retries any PCI writes until a slot in the queue becomes available. 
**  A slot in the post queue becomes available by the Intel XScale core incrementing the tail pointer.
**  ===================================================================================
**  Inbound Free Queue
**  ------------------
**  The Inbound Free Queue holds free inbound messages placed there by the Intel XScale core for other processors to use.
**  This queue is read from the queue tail by external PCI agents. 
**  It is written to the queue head by the Intel XScale core. 
**  The tail pointer is maintained by the MU hardware.
**  The head pointer is maintained by the Intel XScale core.
**  For a PCI read transaction that accesses the Inbound Queue Port,
**  the MU attempts to read the data at the local memory address in the Inbound Free Tail Pointer. 
**  When the queue is not empty (head and tail pointers are not equal) 
**  or full (head and tail pointers are equal but the head pointer was last written by software), the data is returned.
**  When the queue is empty (head and tail pointers are equal and the head pointer was last updated by hardware), 
**  the value of -1 (FFFF.FFFFH) is  returned.
**  When the queue was not empty and the MU succeeded in returning the data at the tail, 
**  the MU hardware must increment the value in the Inbound Free Tail Pointer Register.
**  To reduce latency for the PCI read access, the MU implements a prefetch mechanism to anticipate accesses to the Inbound Free Queue. 
**  The MU hardware prefetches the data at the tail of the Inbound Free Queue and load it into an internal prefetch register. 
**  When the PCI read access occurs, the data is read directly from the prefetch register.
**  The prefetch mechanism loads a value of -1 (FFFF.FFFFH) into the prefetch register 
**  when the head and tail pointers are equal and the queue is empty. 
**  In order to update the prefetch register when messages are added to the queue and it becomes non-empty, 
**  the prefetch mechanism automatically starts a prefetch when the prefetch register contains FFFF.FFFFH 
**  and the Inbound Free Head Pointer Register is written.
**  The Intel XScale core needs to update the Inbound Free Head Pointer Register when it adds messages to the queue.
**  A prefetch must appear atomic from the perspective of the external PCI agent.
**  When a prefetch is started, any PCI transaction that attempts to access the Inbound Free Queue is signalled a Retry until the prefetch is completed.
**  The Intel XScale core may place messages in the Inbound Free Queue by writing the data to the
**  local memory location pointed to by the Inbound Free Head Pointer Register. 
**  The processor must then increment the Inbound Free Head Pointer Register.
**  ==================================================================================
**  Outbound Post Queue
**  -------------------
**  The Outbound Post Queue holds outbound posted messages placed there by the Intel XScale 
**  core for other processors to process. This queue is read from the queue tail by external PCI agents.
**  It is written to the queue head by the Intel XScale  core. The tail pointer is maintained by the
**  MU hardware. The head pointer is maintained by the Intel XScale  core.
**  For a PCI read transaction that accesses the Outbound Queue Port, the MU attempts to read the
**  data at the local memory address in the Outbound Post Tail Pointer Register. When the queue is not
**  empty (head and tail pointers are not equal) or full (head and tail pointers are equal but the head
**  pointer was last written by software), the data is returned. When the queue is empty (head and tail
**  pointers are equal and the head pointer was last updated by hardware), the value of -1
**  (FFFF.FFFFH) is returned. When the queue was not empty and the MU succeeded in returning the
**  data at the tail, the MU hardware must increment the value in the Outbound Post Tail Pointer
**  Register.
**  To reduce latency for the PCI read access, the MU implements a prefetch mechanism to anticipate
**  accesses to the Outbound Post Queue. The MU hardware prefetches the data at the tail of the
**  Outbound Post Queue and load it into an internal prefetch register. When the PCI read access
**  occurs, the data is read directly from the prefetch register.
**  The prefetch mechanism loads a value of -1 (FFFF.FFFFH) into the prefetch register when the head
**  and tail pointers are equal and the queue is empty. In order to update the prefetch register when
**  messages are added to the queue and it becomes non-empty, the prefetch mechanism automatically
**  starts a prefetch when the prefetch register contains FFFF.FFFFH and the Outbound Post Head
**  Pointer Register is written. The Intel XScale  core needs to update the Outbound Post Head
**  Pointer Register when it adds messages to the queue.
**  A prefetch must appear atomic from the perspective of the external PCI agent. When a prefetch is
**  started, any PCI transaction that attempts to access the Outbound Post Queue is signalled a Retry
**  until the prefetch is completed.
**  A PCI interrupt may be generated when data in the prefetch buffer is valid. When the prefetch
**  queue is clear, no interrupt is generated. The Outbound Post Queue Interrupt bit in the Outbound
**  Interrupt Status Register shall indicate the status of the prefetch buffer data and therefore the
**  interrupt status. The interrupt is cleared when any prefetched data has been read from the Outbound
**  Queue Port. The interrupt can be masked by the Outbound Interrupt Mask Register.
**  The Intel XScale  core may place messages in the Outbound Post Queue by writing the data to
**  the local memory address in the Outbound Post Head Pointer Register. The processor must then
**  increment the Outbound Post Head Pointer Register.
**  ==================================================
**  Outbound Free Queue
**  -----------------------
**  The Outbound Free Queue holds free messages placed there by other processors for the Intel
**  XScale  core to use. This queue is read from the queue tail by the Intel XScale  core. It is
**  written to the queue head by external PCI agents. The tail pointer is maintained by the Intel
**  XScale  core. The head pointer is maintained by the MU hardware.
**  For a PCI write transaction that accesses the Outbound Queue Port, the MU writes the data to the
**  local memory address in the Outbound Free Head Pointer Register. When the data written to the
**  Outbound Queue Port is written to local memory, the MU hardware increments the Outbound Free
**  Head Pointer Register.
**  When the head pointer and the tail pointer become equal and the queue is full, the MU may signal
**  an interrupt to the Intel XScale  core to register the queue full condition. This interrupt is
**  recorded in the Inbound Interrupt Status Register. The interrupt is cleared when the Outbound Free
**  Queue Full Interrupt bit is cleared and not by writing to the head or tail pointers. The interrupt can
**  be masked by the Inbound Interrupt Mask Register. Software must be aware of the state of the
**  Outbound Free Queue Interrupt Mask bit to guarantee that the full condition is recognized by the
**  core processor.
**  From the time that a PCI write transaction is received until the data is written in local memory and
**  the Outbound Free Head Pointer Register is incremented, any PCI transaction that attempts to
**  access the Outbound Free Queue Port is signalled a retry.
**  The Intel XScale  core may read messages from the Outbound Free Queue by reading the data
**  from the local memory address in the Outbound Free Tail Pointer Register. The processor must
**  then increment the Outbound Free Tail Pointer Register. When the Outbound Free Queue is full,
**  the hardware must retry any PCI writes until a slot in the queue becomes available.
**
**  ==================================================================================
**  Circular Queue Summary
**  ----------------------
**  ________________________________________________________________________________________________________________________________________________
** | Queue Name  |  PCI Port     |Generate PCI Interrupt |Generate Intel Xscale Core Interrupt|Head Pointer maintained by|Tail Pointer maintained by|
** |_____________|_______________|_______________________|____________________________________|__________________________|__________________________|
** |Inbound Post | Inbound Queue |                       |                                    |                          |                          |
** |    Queue    |     Port      |          NO           |      Yes, when queue is written    |         MU hardware      |     Intel XScale         |
** |_____________|_______________|_______________________|____________________________________|__________________________|__________________________|
** |Inbound Free | Inbound Queue |                       |                                    |                          |                          |
** |    Queue    |     Port      |          NO           |      NO                            |        Intel XScale      |      MU hardware         |
** |_____________|_______________|_______________________|____________________________________|__________________________|__________________________|
** ==================================================================================
**  Circular Queue Status Summary
**  ----------------------
**  ____________________________________________________________________________________________________
** |     Queue Name      |  Queue Status  | Head & Tail Pointer |         Last Pointer Update           |
** |_____________________|________________|_____________________|_______________________________________|
** | Inbound Post Queue  |      Empty     |       Equal         | Tail pointer last updated by software |
** |_____________________|________________|_____________________|_______________________________________|
** | Inbound Free Queue  |      Empty     |       Equal         | Head pointer last updated by hardware |
** |_____________________|________________|_____________________|_______________________________________|
**************************************************************************
*/

/*
**************************************************************************
**       Index Registers
**  ========================
**  . The Index Registers are a set of 1004 registers that when written by an external PCI agent can generate an interrupt to the Intel XScale core. 
**    These registers are for inbound messages only.
**    The interrupt is recorded in the Inbound Interrupt Status Register.
**    The storage for the Index Registers is allocated from the 80331 local memory. 
**    PCI write accesses to the Index Registers write the data to local memory. 
**    PCI read accesses to the Index Registers read the data from local memory. 
**  . The local memory used for the Index Registers ranges from Inbound ATU Translate Value Register + 050H 
**                                                           to Inbound ATU Translate Value Register + FFFH.
**  . The address of the first write access is stored in the Index Address Register. 
**    This register is written during the earliest write access and provides a means to determine which Index Register was written. 
**    Once updated by the MU, the Index Address Register is not updated until the Index Register 
**    Interrupt bit in the Inbound Interrupt Status Register is cleared. 
**  . When the interrupt is cleared, the Index Address Register is re-enabled and stores the address of the next Index Register write access.
**    Writes by the Intel XScale core to the local memory used by the Index Registers 
**    does not cause an interrupt and does not update the Index Address Register.
**  . The index registers can be accessed with Multi-DWORD reads and single QWORD aligned writes.
**************************************************************************
*/
/*
**************************************************************************
**    Messaging Unit Internal Bus Memory Map
**  =======================================
**  Internal Bus Address___Register Description (Name)____________________|_PCI Configuration Space Register Number_
**  FFFF E300H             reserved                                       |
**    ..                     ..                                           |
**  FFFF E30CH             reserved                                       |
**  FFFF E310H             Inbound Message Register 0                     | Available through
**  FFFF E314H             Inbound Message Register 1                     | ATU Inbound Translation Window
**  FFFF E318H             Outbound Message Register 0                    |
**  FFFF E31CH             Outbound Message Register 1                    | or
**  FFFF E320H             Inbound Doorbell Register                      |
**  FFFF E324H             Inbound Interrupt Status Register              | must translate PCI address to
**  FFFF E328H             Inbound Interrupt Mask Register                | the Intel Xscale Core
**  FFFF E32CH             Outbound Doorbell Register                     | Memory-Mapped Address
**  FFFF E330H             Outbound Interrupt Status Register             |
**  FFFF E334H             Outbound Interrupt Mask Register               |
**  ______________________________________________________________________|________________________________________
**  FFFF E338H             reserved                                       |
**  FFFF E33CH             reserved                                       |
**  FFFF E340H             reserved                                       |
**  FFFF E344H             reserved                                       |
**  FFFF E348H             reserved                                       |
**  FFFF E34CH             reserved                                       |
**  FFFF E350H             MU Configuration Register                      |
**  FFFF E354H             Queue Base Address Register                    |
**  FFFF E358H             reserved                                       |
**  FFFF E35CH             reserved                                       | must translate PCI address to
**  FFFF E360H             Inbound Free Head Pointer Register             | the Intel Xscale Core
**  FFFF E364H             Inbound Free Tail Pointer Register             | Memory-Mapped Address
**  FFFF E368H             Inbound Post Head pointer Register             |
**  FFFF E36CH             Inbound Post Tail Pointer Register             |
**  FFFF E370H             Outbound Free Head Pointer Register            |
**  FFFF E374H             Outbound Free Tail Pointer Register            |
**  FFFF E378H             Outbound Post Head pointer Register            |
**  FFFF E37CH             Outbound Post Tail Pointer Register            |
**  FFFF E380H             Index Address Register                         |
**  FFFF E384H             reserved                                       |
**   ..                       ..                                          |
**  FFFF E3FCH             reserved                                       |
**  ______________________________________________________________________|_______________________________________
**************************************************************************
*/
/*
**************************************************************************
**  MU Configuration Register - MUCR  FFFF.E350H
**
**  . The MU Configuration Register (MUCR) contains the Circular Queue Enable bit and the size of one Circular Queue.
**  . The Circular Queue Enable bit enables or disables the Circular Queues. 
**    The Circular Queues are disabled at reset to allow the software to initialize the head 
**    and tail pointer registers before any PCI accesses to the Queue Ports. 
**  . Each Circular Queue may range from 4 K entries (16 Kbytes) to 64 K entries (256 Kbytes) and there are four Circular Queues.
**  ------------------------------------------------------------------------
**  Bit       Default                       Description
**  31:06     000000H 00 2                  Reserved
**  05:01     00001 2                       Circular Queue Size - This field determines the size of each Circular Queue. 
**  					All four queues are the same size.
**  					¡E 00001 2 - 4K Entries (16 Kbytes)
**  					¡E 00010 2 - 8K Entries (32 Kbytes)
**  					¡E 00100 2 - 16K Entries (64 Kbytes)
**  					¡E 01000 2 - 32K Entries (128 Kbytes)
**  					¡E 10000 2 - 64K Entries (256 Kbytes)
**  00        0 2                       Circular Queue Enable - This bit enables or disables the Circular Queues. When clear the Circular
**  					Queues are disabled, however the MU accepts PCI accesses to the Circular Queue Ports but ignores
** 					the data for Writes and return FFFF.FFFFH for Reads. Interrupts are not generated to the core when
** 					disabled. When set, the Circular Queues are fully enabled.
**************************************************************************
*/
#define     ARCMSR_MU_CONFIGURATION_REG  	          0xFFFFE350        
#define     ARCMSR_MU_CIRCULAR_QUEUE_SIZE64K  	          0x0020    
#define     ARCMSR_MU_CIRCULAR_QUEUE_SIZE32K  	          0x0010
#define     ARCMSR_MU_CIRCULAR_QUEUE_SIZE16K  	          0x0008   
#define     ARCMSR_MU_CIRCULAR_QUEUE_SIZE8K  	          0x0004   
#define     ARCMSR_MU_CIRCULAR_QUEUE_SIZE4K  	          0x0002    
#define     ARCMSR_MU_CIRCULAR_QUEUE_ENABLE  	          0x0001        /*0:disable 1:enable*/
/*
**************************************************************************
**  Queue Base Address Register - QBAR
**
**  . The Queue Base Address Register (QBAR) contains the local memory address of the Circular Queues.
**    The base address is required to be located on a 1 Mbyte address boundary.
**  . All Circular Queue head and tail pointers are based on the QBAR. 
**    When the head and tail pointer registers are read, the Queue Base Address is returned in the upper 12 bits. 
**    Writing to the upper 12 bits of the head and tail pointer registers does not affect the Queue Base Address or Queue Base Address Register.
**  Warning: 
**         The QBAR must designate a range allocated to the 80331 DDR SDRAM interface 
**  ------------------------------------------------------------------------
**  Bit       Default                       Description
**  31:20     000H                          Queue Base Address - Local memory address of the circular queues.
**  19:00     00000H                        Reserved
**************************************************************************
*/
#define     ARCMSR_MU_QUEUE_BASE_ADDRESS_REG  	      0xFFFFE354   
/*
**************************************************************************
**  Inbound Free Head Pointer Register - IFHPR
**
**  . The Inbound Free Head Pointer Register (IFHPR) contains the local memory offset from 
**    the Queue Base Address of the head pointer for the Inbound Free Queue. 
**    The Head Pointer must be aligned on a DWORD address boundary.
**    When read, the Queue Base Address is provided in the upper 12 bits of the register. 
**    Writes to the upper 12 bits of the register are ignored. 
**    This register is maintained by software.
**  ------------------------------------------------------------------------
**  Bit       Default                       Description
**  31:20     000H                          Queue Base Address - Local memory address of the circular queues.
**  19:02     0000H 00 2                    Inbound Free Head Pointer - Local memory offset of the head pointer for the Inbound Free Queue.
**  01:00     00 2                          Reserved
**************************************************************************
*/
#define     ARCMSR_MU_INBOUND_FREE_HEAD_PTR_REG       0xFFFFE360   
/*
**************************************************************************
**  Inbound Free Tail Pointer Register - IFTPR
**
**  . The Inbound Free Tail Pointer Register (IFTPR) contains the local memory offset from the Queue
**    Base Address of the tail pointer for the Inbound Free Queue. The Tail Pointer must be aligned on a
**    DWORD address boundary. When read, the Queue Base Address is provided in the upper 12 bits
**    of the register. Writes to the upper 12 bits of the register are ignored.
**  ------------------------------------------------------------------------
**  Bit       Default                       Description
**  31:20     000H                          Queue Base Address - Local memory address of the circular queues.
**  19:02     0000H 00 2                    Inbound Free Tail Pointer - Local memory offset of the tail pointer for the Inbound Free Queue.
**  01:00     00 2                          Reserved
**************************************************************************
*/
#define     ARCMSR_MU_INBOUND_FREE_TAIL_PTR_REG       0xFFFFE364  
/*
**************************************************************************
**  Inbound Post Head Pointer Register - IPHPR
**
**  . The Inbound Post Head Pointer Register (IPHPR) contains the local memory offset from the Queue
**    Base Address of the head pointer for the Inbound Post Queue. The Head Pointer must be aligned on
**    a DWORD address boundary. When read, the Queue Base Address is provided in the upper 12 bits
**    of the register. Writes to the upper 12 bits of the register are ignored.
**  ------------------------------------------------------------------------
**  Bit       Default                       Description
**  31:20     000H                          Queue Base Address - Local memory address of the circular queues.
**  19:02     0000H 00 2                    Inbound Post Head Pointer - Local memory offset of the head pointer for the Inbound Post Queue.
**  01:00     00 2                          Reserved
**************************************************************************
*/
#define     ARCMSR_MU_INBOUND_POST_HEAD_PTR_REG       0xFFFFE368
/*
**************************************************************************
**  Inbound Post Tail Pointer Register - IPTPR
**
**  . The Inbound Post Tail Pointer Register (IPTPR) contains the local memory offset from the Queue
**    Base Address of the tail pointer for the Inbound Post Queue. The Tail Pointer must be aligned on a
**    DWORD address boundary. When read, the Queue Base Address is provided in the upper 12 bits
**    of the register. Writes to the upper 12 bits of the register are ignored.
**  ------------------------------------------------------------------------
**  Bit       Default                       Description
**  31:20     000H                          Queue Base Address - Local memory address of the circular queues.
**  19:02     0000H 00 2                    Inbound Post Tail Pointer - Local memory offset of the tail pointer for the Inbound Post Queue.
**  01:00     00 2                          Reserved
**************************************************************************
*/
#define     ARCMSR_MU_INBOUND_POST_TAIL_PTR_REG       0xFFFFE36C
/*
**************************************************************************
**  Index Address Register - IAR
**
**  . The Index Address Register (IAR) contains the offset of the least recently accessed Index Register.
**    It is written by the MU when the Index Registers are written by a PCI agent.
**    The register is not updated until the Index Interrupt bit in the Inbound Interrupt Status Register is cleared.
**  . The local memory address of the Index Register least recently accessed is computed 
**    by adding the Index Address Register to the Inbound ATU Translate Value Register.
**  ------------------------------------------------------------------------
**  Bit       Default                       Description
**  31:12     000000H                       Reserved
**  11:02     00H 00 2                      Index Address - is the local memory offset of the Index Register written (050H to FFCH)
**  01:00     00 2                          Reserved
**************************************************************************
*/
#define     ARCMSR_MU_LOCAL_MEMORY_INDEX_REG  	      0xFFFFE380    /*1004 dwords 0x0050....0x0FFC, 4016 bytes 0x0050...0x0FFF*/
/*
**********************************************************************************************************
**                                RS-232 Interface for Areca Raid Controller
**                    The low level command interface is exclusive with VT100 terminal
**  --------------------------------------------------------------------
**    1. Sequence of command execution
**  --------------------------------------------------------------------
**    	(A) Header : 3 bytes sequence (0x5E, 0x01, 0x61)
**    	(B) Command block : variable length of data including length, command code, data and checksum byte
**    	(C) Return data : variable length of data
**  --------------------------------------------------------------------  
**    2. Command block
**  --------------------------------------------------------------------
**    	(A) 1st byte : command block length (low byte)
**    	(B) 2nd byte : command block length (high byte)
**                note ..command block length shouldn't > 2040 bytes, length excludes these two bytes
**    	(C) 3rd byte : command code
**    	(D) 4th and following bytes : variable length data bytes depends on command code
**    	(E) last byte : checksum byte (sum of 1st byte until last data byte)
**  --------------------------------------------------------------------  
**    3. Command code and associated data
**  --------------------------------------------------------------------
**    	The following are command code defined in raid controller Command code 0x10--0x1? are used for system level management,
**    	no password checking is needed and should be implemented in separate well controlled utility and not for end user access.
**    	Command code 0x20--0x?? always check the password, password must be entered to enable these command.
**    	enum
**    	{
**    		GUI_SET_SERIAL=0x10,
**    		GUI_SET_VENDOR,
**    		GUI_SET_MODEL,
**    		GUI_IDENTIFY,
**    		GUI_CHECK_PASSWORD,
**    		GUI_LOGOUT,
**    		GUI_HTTP,
**    		GUI_SET_ETHERNET_ADDR,
**    		GUI_SET_LOGO,
**    		GUI_POLL_EVENT,
**    		GUI_GET_EVENT,
**    		GUI_GET_HW_MONITOR,
**
**    		//    GUI_QUICK_CREATE=0x20, (function removed)
**    		GUI_GET_INFO_R=0x20,
**    		GUI_GET_INFO_V,
**    		GUI_GET_INFO_P,
**    		GUI_GET_INFO_S,
**    		GUI_CLEAR_EVENT,
**
**    		GUI_MUTE_BEEPER=0x30,
**    		GUI_BEEPER_SETTING,
**    		GUI_SET_PASSWORD,
**    		GUI_HOST_INTERFACE_MODE,
**    		GUI_REBUILD_PRIORITY,
**    		GUI_MAX_ATA_MODE,
**    		GUI_RESET_CONTROLLER,
**    		GUI_COM_PORT_SETTING,
**    		GUI_NO_OPERATION,
**    		GUI_DHCP_IP,
**
**    		GUI_CREATE_PASS_THROUGH=0x40,
**    		GUI_MODIFY_PASS_THROUGH,
**    		GUI_DELETE_PASS_THROUGH,
**    		GUI_IDENTIFY_DEVICE,
**
**    		GUI_CREATE_RAIDSET=0x50,
**    		GUI_DELETE_RAIDSET,
**    		GUI_EXPAND_RAIDSET,
**    		GUI_ACTIVATE_RAIDSET,
**    		GUI_CREATE_HOT_SPARE,
**    		GUI_DELETE_HOT_SPARE,
**
**    		GUI_CREATE_VOLUME=0x60,
**    		GUI_MODIFY_VOLUME,
**    		GUI_DELETE_VOLUME,
**    		GUI_START_CHECK_VOLUME,
**    		GUI_STOP_CHECK_VOLUME
**    	};
**
**    Command description :
**
**    	GUI_SET_SERIAL : Set the controller serial#
**    		byte 0,1        : length
**    		byte 2          : command code 0x10
**    		byte 3          : password length (should be 0x0f)
**    		byte 4-0x13     : should be "ArEcATecHnoLogY"
**    		byte 0x14--0x23 : Serial number string (must be 16 bytes)
**      GUI_SET_VENDOR : Set vendor string for the controller
**    		byte 0,1        : length
**    		byte 2          : command code 0x11
**    		byte 3          : password length (should be 0x08)
**    		byte 4-0x13     : should be "ArEcAvAr"
**    		byte 0x14--0x3B : vendor string (must be 40 bytes)
**      GUI_SET_MODEL : Set the model name of the controller
**    		byte 0,1        : length
**    		byte 2          : command code 0x12
**    		byte 3          : password length (should be 0x08)
**    		byte 4-0x13     : should be "ArEcAvAr"
**    		byte 0x14--0x1B : model string (must be 8 bytes)
**      GUI_IDENTIFY : Identify device
**    		byte 0,1        : length
**    		byte 2          : command code 0x13
**    		                  return "Areca RAID Subsystem "
**      GUI_CHECK_PASSWORD : Verify password
**    		byte 0,1        : length
**    		byte 2          : command code 0x14
**    		byte 3          : password length
**    		byte 4-0x??     : user password to be checked
**      GUI_LOGOUT : Logout GUI (force password checking on next command)
**    		byte 0,1        : length
**    		byte 2          : command code 0x15
**      GUI_HTTP : HTTP interface (reserved for Http proxy service)(0x16)
**
**      GUI_SET_ETHERNET_ADDR : Set the ethernet MAC address
**    		byte 0,1        : length
**    		byte 2          : command code 0x17
**    		byte 3          : password length (should be 0x08)
**    		byte 4-0x13     : should be "ArEcAvAr"
**    		byte 0x14--0x19 : Ethernet MAC address (must be 6 bytes)
**      GUI_SET_LOGO : Set logo in HTTP
**    		byte 0,1        : length
**    		byte 2          : command code 0x18
**    		byte 3          : Page# (0/1/2/3) (0xff --> clear OEM logo)
**    		byte 4/5/6/7    : 0x55/0xaa/0xa5/0x5a
**    		byte 8          : TITLE.JPG data (each page must be 2000 bytes)
**    		                  note .... page0 1st 2 byte must be actual length of the JPG file
**      GUI_POLL_EVENT : Poll If Event Log Changed
**    		byte 0,1        : length
**    		byte 2          : command code 0x19
**      GUI_GET_EVENT : Read Event
**    		byte 0,1        : length
**    		byte 2          : command code 0x1a
**    		byte 3          : Event Page (0:1st page/1/2/3:last page)
**      GUI_GET_HW_MONITOR : Get HW monitor data
**    		byte 0,1        : length
**    		byte 2 			: command code 0x1b
**    		byte 3 			: # of FANs(example 2)
**    		byte 4 			: # of Voltage sensor(example 3)
**    		byte 5 			: # of temperature sensor(example 2)
**    		byte 6 			: # of power
**    		byte 7/8        : Fan#0 (RPM)
**    		byte 9/10       : Fan#1
**    		byte 11/12 		: Voltage#0 original value in *1000
**    		byte 13/14 		: Voltage#0 value
**    		byte 15/16 		: Voltage#1 org
**    		byte 17/18 		: Voltage#1
**    		byte 19/20 		: Voltage#2 org
**    		byte 21/22 		: Voltage#2
**    		byte 23 		: Temp#0
**    		byte 24 		: Temp#1
**    		byte 25 		: Power indicator (bit0 : power#0, bit1 : power#1)
**    		byte 26 		: UPS indicator
**      GUI_QUICK_CREATE : Quick create raid/volume set
**    	    byte 0,1        : length
**    	    byte 2          : command code 0x20
**    	    byte 3/4/5/6    : raw capacity
**    	    byte 7 			: raid level
**    	    byte 8 			: stripe size
**    	    byte 9 			: spare
**    	    byte 10/11/12/13: device mask (the devices to create raid/volume) 
**    		                  This function is removed, application like to implement quick create function 
**    		                  need to use GUI_CREATE_RAIDSET and GUI_CREATE_VOLUMESET function.
**      GUI_GET_INFO_R : Get Raid Set Information
**    		byte 0,1        : length
**    		byte 2          : command code 0x20
**    		byte 3          : raidset#
**
**    	typedef struct sGUI_RAIDSET
**    	{
**    		BYTE grsRaidSetName[16];
**    		DWORD grsCapacity;
**    		DWORD grsCapacityX;
**    		DWORD grsFailMask;
**    		BYTE grsDevArray[32];
**    		BYTE grsMemberDevices;
**    		BYTE grsNewMemberDevices;
**    		BYTE grsRaidState;
**    		BYTE grsVolumes;
**    		BYTE grsVolumeList[16];
**    		BYTE grsRes1;
**    		BYTE grsRes2;
**    		BYTE grsRes3;
**    		BYTE grsFreeSegments;
**    		DWORD grsRawStripes[8];
**    		DWORD grsRes4;
**    		DWORD grsRes5; //     Total to 128 bytes
**    		DWORD grsRes6; //     Total to 128 bytes
**    	} sGUI_RAIDSET, *pGUI_RAIDSET;
**      GUI_GET_INFO_V : Get Volume Set Information
**    		byte 0,1        : length
**    		byte 2          : command code 0x21
**    		byte 3          : volumeset#
**
**    	typedef struct sGUI_VOLUMESET
**    	{
**    		BYTE gvsVolumeName[16]; //     16
**    		DWORD gvsCapacity;
**    		DWORD gvsCapacityX;
**    		DWORD gvsFailMask;
**    		DWORD gvsStripeSize;
**    		DWORD gvsNewFailMask;
**    		DWORD gvsNewStripeSize;
**    		DWORD gvsVolumeStatus;
**    		DWORD gvsProgress; //     32
**    		sSCSI_ATTR gvsScsi; 
**    		BYTE gvsMemberDisks;
**    		BYTE gvsRaidLevel; //     8
**
**    		BYTE gvsNewMemberDisks;
**    		BYTE gvsNewRaidLevel;
**    		BYTE gvsRaidSetNumber;
**    		BYTE gvsRes0; //     4
**    		BYTE gvsRes1[4]; //     64 bytes
**    	} sGUI_VOLUMESET, *pGUI_VOLUMESET;
**
**      GUI_GET_INFO_P : Get Physical Drive Information
**    		byte 0,1        : length
**    		byte 2          : command code 0x22
**    		byte 3          : drive # (from 0 to max-channels - 1)
**
**    	typedef struct sGUI_PHY_DRV
**    	{
**    		BYTE gpdModelName[40];
**    		BYTE gpdSerialNumber[20];
**    		BYTE gpdFirmRev[8];
**    		DWORD gpdCapacity;
**    		DWORD gpdCapacityX; //     Reserved for expansion
**    		BYTE gpdDeviceState;
**    		BYTE gpdPioMode;
**    		BYTE gpdCurrentUdmaMode;
**    		BYTE gpdUdmaMode;
**    		BYTE gpdDriveSelect;
**    		BYTE gpdRaidNumber; //     0xff if not belongs to a raid set
**    		sSCSI_ATTR gpdScsi;
**    		BYTE gpdReserved[40]; //     Total to 128 bytes
**    	} sGUI_PHY_DRV, *pGUI_PHY_DRV;
**
**    	GUI_GET_INFO_S : Get System Information
**      	byte 0,1        : length
**      	byte 2          : command code 0x23
**
**    	typedef struct sCOM_ATTR
**    	{
**    		BYTE comBaudRate;
**    		BYTE comDataBits;
**    		BYTE comStopBits;
**    		BYTE comParity;
**    		BYTE comFlowControl;
**    	} sCOM_ATTR, *pCOM_ATTR;
**
**    	typedef struct sSYSTEM_INFO
**    	{
**    		BYTE gsiVendorName[40];
**    		BYTE gsiSerialNumber[16];
**    		BYTE gsiFirmVersion[16];
**    		BYTE gsiBootVersion[16];
**    		BYTE gsiMbVersion[16];
**    		BYTE gsiModelName[8];
**    		BYTE gsiLocalIp[4];
**    		BYTE gsiCurrentIp[4];
**    		DWORD gsiTimeTick;
**    		DWORD gsiCpuSpeed;
**    		DWORD gsiICache;
**    		DWORD gsiDCache;
**    		DWORD gsiScache;
**    		DWORD gsiMemorySize;
**    		DWORD gsiMemorySpeed;
**    		DWORD gsiEvents;
**    		BYTE gsiMacAddress[6];
**    		BYTE gsiDhcp;
**    		BYTE gsiBeeper;
**    		BYTE gsiChannelUsage;
**    		BYTE gsiMaxAtaMode;
**    		BYTE gsiSdramEcc; //     1:if ECC enabled
**    		BYTE gsiRebuildPriority;
**    		sCOM_ATTR gsiComA; //     5 bytes
**    		sCOM_ATTR gsiComB; //     5 bytes
**    		BYTE gsiIdeChannels;
**    		BYTE gsiScsiHostChannels;
**    		BYTE gsiIdeHostChannels;
**    		BYTE gsiMaxVolumeSet;
**    		BYTE gsiMaxRaidSet;
**    		BYTE gsiEtherPort; //     1:if ether net port supported
**    		BYTE gsiRaid6Engine; //     1:Raid6 engine supported
**    		BYTE gsiRes[75];
**    	} sSYSTEM_INFO, *pSYSTEM_INFO;
**
**    	GUI_CLEAR_EVENT : Clear System Event
**    		byte 0,1        : length
**    		byte 2          : command code 0x24
**
**      GUI_MUTE_BEEPER : Mute current beeper
**    		byte 0,1        : length
**    		byte 2          : command code 0x30
**
**      GUI_BEEPER_SETTING : Disable beeper
**    		byte 0,1        : length
**    		byte 2          : command code 0x31
**    		byte 3          : 0->disable, 1->enable
**
**      GUI_SET_PASSWORD : Change password
**    		byte 0,1        : length
**    		byte 2 			: command code 0x32
**    		byte 3 			: pass word length ( must <= 15 )
**    		byte 4 			: password (must be alpha-numerical)
**
**    	GUI_HOST_INTERFACE_MODE : Set host interface mode
**    		byte 0,1        : length
**    		byte 2 			: command code 0x33
**    		byte 3 			: 0->Independent, 1->cluster
**
**      GUI_REBUILD_PRIORITY : Set rebuild priority
**    		byte 0,1        : length
**    		byte 2 			: command code 0x34
**    		byte 3 			: 0/1/2/3 (low->high)
**
**      GUI_MAX_ATA_MODE : Set maximum ATA mode to be used
**    		byte 0,1        : length
**    		byte 2 			: command code 0x35
**    		byte 3 			: 0/1/2/3 (133/100/66/33)
**
**      GUI_RESET_CONTROLLER : Reset Controller
**    		byte 0,1        : length
**    		byte 2          : command code 0x36
**                            *Response with VT100 screen (discard it)
**
**      GUI_COM_PORT_SETTING : COM port setting
**    		byte 0,1        : length
**    		byte 2 			: command code 0x37
**    		byte 3 			: 0->COMA (term port), 1->COMB (debug port)
**    		byte 4 			: 0/1/2/3/4/5/6/7 (1200/2400/4800/9600/19200/38400/57600/115200)
**    		byte 5 			: data bit (0:7 bit, 1:8 bit : must be 8 bit)
**    		byte 6 			: stop bit (0:1, 1:2 stop bits)
**    		byte 7 			: parity (0:none, 1:off, 2:even)
**    		byte 8 			: flow control (0:none, 1:xon/xoff, 2:hardware => must use none)
**
**      GUI_NO_OPERATION : No operation
**    		byte 0,1        : length
**    		byte 2          : command code 0x38
**
**      GUI_DHCP_IP : Set DHCP option and local IP address
**    		byte 0,1        : length
**    		byte 2          : command code 0x39
**    		byte 3          : 0:dhcp disabled, 1:dhcp enabled
**    		byte 4/5/6/7    : IP address
**
**      GUI_CREATE_PASS_THROUGH : Create pass through disk
**    		byte 0,1        : length
**    		byte 2 			: command code 0x40
**    		byte 3 			: device #
**    		byte 4 			: scsi channel (0/1)
**    		byte 5 			: scsi id (0-->15)
**    		byte 6 			: scsi lun (0-->7)
**    		byte 7 			: tagged queue (1 : enabled)
**    		byte 8 			: cache mode (1 : enabled)
**    		byte 9 			: max speed (0/1/2/3/4, async/20/40/80/160 for scsi)
**    								    (0/1/2/3/4, 33/66/100/133/150 for ide  )
**
**      GUI_MODIFY_PASS_THROUGH : Modify pass through disk
**    		byte 0,1        : length
**    		byte 2 			: command code 0x41
**    		byte 3 			: device #
**    		byte 4 			: scsi channel (0/1)
**    		byte 5 			: scsi id (0-->15)
**    		byte 6 			: scsi lun (0-->7)
**    		byte 7 			: tagged queue (1 : enabled)
**    		byte 8 			: cache mode (1 : enabled)
**    		byte 9 			: max speed (0/1/2/3/4, async/20/40/80/160 for scsi)
**    							        (0/1/2/3/4, 33/66/100/133/150 for ide  )
**
**      GUI_DELETE_PASS_THROUGH : Delete pass through disk
**    		byte 0,1        : length
**    		byte 2          : command code 0x42
**    		byte 3          : device# to be deleted
**
**      GUI_IDENTIFY_DEVICE : Identify Device
**    		byte 0,1        : length
**    		byte 2          : command code 0x43
**    		byte 3          : Flash Method(0:flash selected, 1:flash not selected)
**    		byte 4/5/6/7    : IDE device mask to be flashed
**                           note .... no response data available
**
**    	GUI_CREATE_RAIDSET : Create Raid Set
**    		byte 0,1        : length
**    		byte 2          : command code 0x50
**    		byte 3/4/5/6    : device mask
**    		byte 7-22       : raidset name (if byte 7 == 0:use default)
**
**      GUI_DELETE_RAIDSET : Delete Raid Set
**    		byte 0,1        : length
**    		byte 2          : command code 0x51
**    		byte 3          : raidset#
**
**    	GUI_EXPAND_RAIDSET : Expand Raid Set 
**    		byte 0,1        : length
**    		byte 2          : command code 0x52
**    		byte 3          : raidset#
**    		byte 4/5/6/7    : device mask for expansion
**    		byte 8/9/10     : (8:0 no change, 1 change, 0xff:terminate, 9:new raid level,10:new stripe size 0/1/2/3/4/5->4/8/16/32/64/128K )
**    		byte 11/12/13   : repeat for each volume in the raidset ....
**
**      GUI_ACTIVATE_RAIDSET : Activate incomplete raid set 
**    		byte 0,1        : length
**    		byte 2          : command code 0x53
**    		byte 3          : raidset#
**
**      GUI_CREATE_HOT_SPARE : Create hot spare disk 
**    		byte 0,1        : length
**    		byte 2          : command code 0x54
**    		byte 3/4/5/6    : device mask for hot spare creation
**
**    	GUI_DELETE_HOT_SPARE : Delete hot spare disk 
**    		byte 0,1        : length
**    		byte 2          : command code 0x55
**    		byte 3/4/5/6    : device mask for hot spare deletion
**
**    	GUI_CREATE_VOLUME : Create volume set 
**    		byte 0,1        : length
**    		byte 2          : command code 0x60
**    		byte 3          : raidset#
**    		byte 4-19       : volume set name (if byte4 == 0, use default)
**    		byte 20-27      : volume capacity (blocks)
**    		byte 28 		: raid level
**    		byte 29 		: stripe size (0/1/2/3/4/5->4/8/16/32/64/128K)
**    		byte 30 		: channel
**    		byte 31 		: ID
**    		byte 32 		: LUN
**    		byte 33 		: 1 enable tag
**    		byte 34 		: 1 enable cache
**    		byte 35 		: speed (0/1/2/3/4->async/20/40/80/160 for scsi)
**    								(0/1/2/3/4->33/66/100/133/150 for IDE  )
**    		byte 36 		: 1 to select quick init
**
**    	GUI_MODIFY_VOLUME : Modify volume Set
**    		byte 0,1        : length
**    		byte 2          : command code 0x61
**    		byte 3          : volumeset#
**    		byte 4-19       : new volume set name (if byte4 == 0, not change)
**    		byte 20-27      : new volume capacity (reserved)
**    		byte 28 		: new raid level
**    		byte 29 		: new stripe size (0/1/2/3/4/5->4/8/16/32/64/128K)
**    		byte 30 		: new channel
**    		byte 31 		: new ID
**    		byte 32 		: new LUN
**    		byte 33 		: 1 enable tag
**    		byte 34 		: 1 enable cache
**    		byte 35 		: speed (0/1/2/3/4->async/20/40/80/160 for scsi)
**    								(0/1/2/3/4->33/66/100/133/150 for IDE  )
**
**    	GUI_DELETE_VOLUME : Delete volume set
**    		byte 0,1        : length
**    		byte 2          : command code 0x62
**    		byte 3          : volumeset#
**
**    	GUI_START_CHECK_VOLUME : Start volume consistency check
**    		byte 0,1        : length
**    		byte 2          : command code 0x63
**    		byte 3          : volumeset#
**
**    	GUI_STOP_CHECK_VOLUME : Stop volume consistency check
**    		byte 0,1        : length
**    		byte 2          : command code 0x64
** ---------------------------------------------------------------------   
**    4. Returned data
** ---------------------------------------------------------------------   
**    	(A) Header          : 3 bytes sequence (0x5E, 0x01, 0x61)
**    	(B) Length          : 2 bytes (low byte 1st, excludes length and checksum byte)
**    	(C) status or data  :
**           <1> If length == 1 ==> 1 byte status code
**    								#define GUI_OK                    0x41
**    								#define GUI_RAIDSET_NOT_NORMAL    0x42
**    								#define GUI_VOLUMESET_NOT_NORMAL  0x43
**    								#define GUI_NO_RAIDSET            0x44
**    								#define GUI_NO_VOLUMESET          0x45
**    								#define GUI_NO_PHYSICAL_DRIVE     0x46
**    								#define GUI_PARAMETER_ERROR       0x47
**    								#define GUI_UNSUPPORTED_COMMAND   0x48
**    								#define GUI_DISK_CONFIG_CHANGED   0x49
**    								#define GUI_INVALID_PASSWORD      0x4a
**    								#define GUI_NO_DISK_SPACE         0x4b
**    								#define GUI_CHECKSUM_ERROR        0x4c
**    								#define GUI_PASSWORD_REQUIRED     0x4d
**           <2> If length > 1 ==> data block returned from controller and the contents depends on the command code
**        (E) Checksum : checksum of length and status or data byte
**************************************************************************
*/
