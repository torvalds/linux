/*-
 * Copyright (c) 2018 Microsemi Corporation.
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

/* $FreeBSD$ */

#ifndef _PQI_DEFINES_H
#define _PQI_DEFINES_H

#define PQI_STATUS_FAILURE			-1
#define PQI_STATUS_TIMEOUT			-2
#define PQI_STATUS_QFULL			-3
#define PQI_STATUS_SUCCESS			0

#define PQISRC_CMD_TIMEOUT_CNT			1200000 /* 500usec * 1200000 = 5 min  */
#define PQI_CMND_COMPLETE_TMO			1000 /* in millisecond  */

#define	INVALID_ELEM				0xffff
#ifndef MIN
#define MIN(a,b)                                ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a,b)                                ((a) > (b) ? (a) : (b))
#endif

#define PQISRC_ROUNDUP(x, y)			(((x) + (y) - 1) / (y) * (y))
#define PQISRC_DIV_ROUND_UP(x, y)		(((x) + (y) - 1) / (y))

#define ALIGN_BOUNDARY(a, n)	{	\
		if (a % n)	\
			a = a + (n - a % n);	\
	}

/* Busy wait timeout on a condition */
#define	COND_BUSYWAIT(cond, timeout /* in millisecond */) { \
		if (!(cond)) { \
			while (timeout) { \
				OS_BUSYWAIT(1000); \
				if (cond) \
					break; \
				timeout--; \
			} \
		} \
	}

/* Wait timeout on a condition*/
#define	COND_WAIT(cond, timeout /* in millisecond */) { \
		if (!(cond)) { \
			while (timeout) { \
				OS_SLEEP(1000); \
				if (cond) \
					break; \
				timeout--; \
			} \
		} \
	}
	
#define FILL_QUEUE_ARRAY_ADDR(q,virt,dma) { 	\
			q->array_virt_addr = virt;	\
			q->array_dma_addr = dma;	\
		}

#define	true	1
#define false	0

enum INTR_TYPE {
	LOCK_INTR,		
	LOCK_SLEEP
};

#define LOCKNAME_SIZE		32

#define INTR_TYPE_NONE		0x0
#define INTR_TYPE_FIXED		0x1
#define INTR_TYPE_MSI		0x2
#define INTR_TYPE_MSIX		0x4
#define SIS_ENABLE_MSIX		0x40
#define SIS_ENABLE_INTX		0x80
#define PQISRC_LEGACY_INTX_MASK	0x1

#define DMA_TO_VIRT(mem)	((mem)->virt_addr)
#define DMA_PHYS_LOW(mem)	(((mem)->dma_addr)  & 0x00000000ffffffff)
#define DMA_PHYS_HIGH(mem)	((((mem)->dma_addr) & 0xffffffff00000000) >> 32)


typedef enum REQUEST_STATUS {
	REQUEST_SUCCESS = 0,
	REQUEST_PENDING = -1,
	REQUEST_FAILED = -2,
}REQUEST_STATUS_T;

typedef enum IO_PATH {
	AIO_PATH,
	RAID_PATH
}IO_PATH_T;

typedef enum device_type
{
	DISK_DEVICE,
	TAPE_DEVICE,
	ROM_DEVICE = 5,
	SES_DEVICE,
	CONTROLLER_DEVICE,
	MEDIUM_CHANGER_DEVICE,
	RAID_DEVICE = 0x0c,
	ENCLOSURE_DEVICE,
	ZBC_DEVICE = 0x14
} device_type_t;

typedef enum controller_state {
	PQI_UP_RUNNING,
	PQI_BUS_RESET,
}controller_state_t;


#define PQISRC_MAX_MSIX_SUPPORTED		64

/* SIS Specific */
#define PQISRC_INIT_STRUCT_REVISION		9
#define	PQISRC_SECTOR_SIZE			512
#define	PQISRC_BLK_SIZE				PQISRC_SECTOR_SIZE
#define	PQISRC_DEFAULT_DMA_ALIGN		4
#define	PQISRC_DMA_ALIGN_MASK			(PQISRC_DEFAULT_DMA_ALIGN - 1)
#define PQISRC_ERR_BUF_DMA_ALIGN		32
#define PQISRC_ERR_BUF_ELEM_SIZE		MAX(sizeof(raid_path_error_info_elem_t),sizeof(aio_path_error_info_elem_t))
#define	PQISRC_INIT_STRUCT_DMA_ALIGN		16

#define SIS_CMD_GET_ADAPTER_PROPERTIES		0x19
#define SIS_CMD_GET_COMM_PREFERRED_SETTINGS	0x26
#define SIS_CMD_GET_PQI_CAPABILITIES		0x3000
#define SIS_CMD_INIT_BASE_STRUCT_ADDRESS	0x1b

#define SIS_SUPPORT_EXT_OPT			0x00800000
#define SIS_SUPPORT_PQI				0x00000004
#define SIS_SUPPORT_PQI_RESET_QUIESCE		0x00000008

#define SIS_PQI_RESET_QUIESCE			0x1000000

#define SIS_STATUS_OK_TIMEOUT			120000	/* in milli sec, 5 sec */

#define SIS_CMD_COMPLETE_TIMEOUT   		30000  /* in milli sec, 30 secs */
#define SIS_POLL_START_WAIT_TIME		20000  /* in micro sec, 20 milli sec */
#define SIS_DB_BIT_CLEAR_TIMEOUT_CNT		120000	/* 500usec * 120000 = 60 sec */

#define SIS_ENABLE_TIMEOUT			3000
#define REENABLE_SIS				0x1
#define TRIGGER_NMI_SIS				0x800000
/*SIS Register status defines */

#define PQI_CTRL_KERNEL_UP_AND_RUNNING		0x80
#define PQI_CTRL_KERNEL_PANIC			0x100

#define SIS_CTL_TO_HOST_DB_DISABLE_ALL		0xFFFFFFFF
#define SIS_CTL_TO_HOST_DB_CLEAR		0x00001000 			
#define	SIS_CMD_SUBMIT				0x00000200  /* Bit 9 */
#define SIS_CMD_COMPLETE			0x00001000  /* Bit 12 */
#define SIS_CMD_STATUS_SUCCESS			0x1	

/* PQI specific */

/* defines */
#define PQISRC_PQI_REG_OFFSET				0x4000
#define	PQISRC_MAX_OUTSTANDING_REQ			4096
#define	PQISRC_MAX_ADMIN_IB_QUEUE_ELEM_NUM		16
#define	PQISRC_MAX_ADMIN_OB_QUEUE_ELEM_NUM		16



#define PQI_MIN_OP_IB_QUEUE_ID				1
#define PQI_OP_EVENT_QUEUE_ID				1
#define PQI_MIN_OP_OB_QUEUE_ID				2

#define	PQISRC_MAX_SUPPORTED_OP_IB_Q		128
#define PQISRC_MAX_SUPPORTED_OP_RAID_IB_Q	(PQISRC_MAX_SUPPORTED_OP_IB_Q / 2)
#define PQISRC_MAX_SUPPORTED_OP_AIO_IB_Q	(PQISRC_MAX_SUPPORTED_OP_RAID_IB_Q)
#define	PQISRC_MAX_OP_IB_QUEUE_ELEM_NUM		(PQISRC_MAX_OUTSTANDING_REQ / PQISRC_MAX_SUPPORTED_OP_IB_Q)	
#define	PQISRC_MAX_OP_OB_QUEUE_ELEM_NUM		PQISRC_MAX_OUTSTANDING_REQ	
#define	PQISRC_MIN_OP_OB_QUEUE_ELEM_NUM		2
#define	PQISRC_MAX_SUPPORTED_OP_OB_Q		64
#define PQISRC_OP_MAX_IBQ_ELEM_SIZE		8 /* 8 * 16  = 128 bytes */	
#define PQISRC_OP_MIN_IBQ_ELEM_SIZE		2 /* 2 * 16  = 32 bytes */
#define PQISRC_OP_OBQ_ELEM_SIZE			1 /* 16 bytes */
#define PQISRC_ADMIN_IBQ_ELEM_SIZE		2 /* 2 * 16  = 32 bytes */
#define PQISRC_INTR_COALSC_GRAN			0
#define PQISRC_PROTO_BIT_MASK			0
#define PQISRC_SGL_SUPPORTED_BIT_MASK		0

#define PQISRC_NUM_EVENT_Q_ELEM			32
#define PQISRC_EVENT_Q_ELEM_SIZE		32 

/* PQI Registers state status */

#define PQI_RESET_ACTION_RESET			0x1
#define PQI_RESET_ACTION_COMPLETED		0x2
#define PQI_RESET_TYPE_NO_RESET			0x0
#define PQI_RESET_TYPE_SOFT_RESET		0x1
#define PQI_RESET_TYPE_FIRM_RESET		0x2
#define PQI_RESET_TYPE_HARD_RESET		0x3

#define PQI_RESET_POLL_INTERVAL 		100000 /*100 msec*/

enum pqisrc_ctrl_mode{
	CTRL_SIS_MODE = 0,
	CTRL_PQI_MODE
};

/* PQI device performing internal initialization (e.g., POST). */
#define PQI_DEV_STATE_POWER_ON_AND_RESET	0x0  
/* Upon entry to this state PQI device initialization begins. */
#define PQI_DEV_STATE_PQI_STATUS_AVAILABLE	0x1  
/* PQI device Standard registers are available to the driver. */
#define PQI_DEV_STATE_ALL_REGISTERS_READY	0x2  
/* PQI device is initialized and ready to process any PCI transactions. */
#define PQI_DEV_STATE_ADMIN_QUEUE_PAIR_READY	0x3 
/* The PQI Device Error register indicates the error. */
#define PQI_DEV_STATE_ERROR			0x4  

#define PQI_DEV_STATE_AT_INIT			( PQI_DEV_STATE_PQI_STATUS_AVAILABLE | \
						  PQI_DEV_STATE_ALL_REGISTERS_READY | \
						  PQI_DEV_STATE_ADMIN_QUEUE_PAIR_READY )		

#define PQISRC_PQI_DEVICE_SIGNATURE		"PQI DREG"
#define	PQI_ADMINQ_ELEM_ARRAY_ALIGN		64
#define	PQI_ADMINQ_CI_PI_ALIGN			64
#define	PQI_OPQ_ELEM_ARRAY_ALIGN		64
#define	PQI_OPQ_CI_PI_ALIGN				4
#define	PQI_ADDR_ALIGN_MASK_64			0x3F /* lsb 6 bits */
#define	PQI_ADDR_ALIGN_MASK_4			0x3  /* lsb 2 bits */

#define	PQISRC_PQIMODE_READY_TIMEOUT   		(30 * 1000 ) /* 30 secs */
#define	PQISRC_MODE_READY_POLL_INTERVAL		1000 /* 1 msec */

#define PRINT_PQI_SIGNATURE(sign)		{ int i = 0; \
						  char si[9]; \
						  for(i=0;i<8;i++) \
							si[i] = *((char *)&(sign)+i); \
						  si[i] = '\0'; \
						  DBG_INFO("Signature is %s",si); \
						}
#define PQI_CONF_TABLE_MAX_LEN		((uint16_t)~0)
#define PQI_CONF_TABLE_SIGNATURE	"CFGTABLE"

/* PQI configuration table section IDs */
#define PQI_CONF_TABLE_SECTION_GENERAL_INFO		0
#define PQI_CONF_TABLE_SECTION_FIRMWARE_FEATURES	1
#define PQI_CONF_TABLE_SECTION_FIRMWARE_ERRATA		2
#define PQI_CONF_TABLE_SECTION_DEBUG			3
#define PQI_CONF_TABLE_SECTION_HEARTBEAT		4

#define CTRLR_HEARTBEAT_CNT(softs)		LE_64(PCI_MEM_GET64(softs, softs->heartbeat_counter_abs_addr, softs->heartbeat_counter_off))
#define	PQI_NEW_HEARTBEAT_MECHANISM(softs)	1

 /* pqi-2r00a table 36 */
#define PQI_ADMIN_QUEUE_MSIX_DISABLE		(0x80000000)   
#define PQI_ADMIN_QUEUE_MSIX_ENABLE		(0 << 31)

#define	PQI_ADMIN_QUEUE_CONF_FUNC_CREATE_Q_PAIR	0x01
#define	PQI_ADMIN_QUEUE_CONF_FUNC_DEL_Q_PAIR	0x02
#define	PQI_ADMIN_QUEUE_CONF_FUNC_STATUS_IDLE	0x00
#define PQISRC_ADMIN_QUEUE_CREATE_TIMEOUT	1000  /* in miLLI sec, 1 sec, 100 ms is standard */
#define PQISRC_ADMIN_QUEUE_DELETE_TIMEOUT	100  /* 100 ms is standard */
#define	PQISRC_ADMIN_CMD_RESP_TIMEOUT		3000 /* 3 sec  */
#define PQISRC_RAIDPATH_CMD_TIMEOUT		30000 /* 30 sec */

#define REPORT_PQI_DEV_CAP_DATA_BUF_SIZE   	sizeof(pqi_dev_cap_t)
#define REPORT_MANUFACTURER_INFO_DATA_BUF_SIZE	0x80   /* Data buffer size specified in bytes 0-1 of data buffer.  128 bytes. */
/* PQI IUs */
/* Admin IU request length not including header. */
#define	PQI_STANDARD_IU_LENGTH			0x003C  /* 60 bytes. */
#define PQI_IU_TYPE_GENERAL_ADMIN_REQUEST	0x60
#define PQI_IU_TYPE_GENERAL_ADMIN_RESPONSE	0xe0

/* PQI / Vendor specific IU */
#define	PQI_FUNCTION_REPORT_DEV_CAP				0x00
#define PQI_REQUEST_IU_TASK_MANAGEMENT				0x13
#define PQI_IU_TYPE_RAID_PATH_IO_REQUEST			0x14
#define PQI_IU_TYPE_AIO_PATH_IO_REQUEST				0x15
#define PQI_REQUEST_IU_GENERAL_ADMIN				0x60
#define PQI_REQUEST_IU_REPORT_VENDOR_EVENT_CONFIG		0x72
#define PQI_REQUEST_IU_SET_VENDOR_EVENT_CONFIG			0x73
#define PQI_RESPONSE_IU_GENERAL_MANAGEMENT			0x81
#define PQI_RESPONSE_IU_TASK_MANAGEMENT				0x93
#define PQI_RESPONSE_IU_GENERAL_ADMIN				0xe0

#define PQI_RESPONSE_IU_RAID_PATH_IO_SUCCESS			0xf0
#define PQI_RESPONSE_IU_AIO_PATH_IO_SUCCESS			0xf1
#define PQI_RESPONSE_IU_RAID_PATH_IO_ERROR			0xf2
#define PQI_RESPONSE_IU_AIO_PATH_IO_ERROR			0xf3
#define PQI_RESPONSE_IU_AIO_PATH_IS_OFF				0xf4
#define PQI_REQUEST_IU_ACKNOWLEDGE_VENDOR_EVENT			0xf6
#define PQI_REQUEST_HEADER_LENGTH				4
#define PQI_FUNCTION_CREATE_OPERATIONAL_IQ			0x10
#define PQI_FUNCTION_CREATE_OPERATIONAL_OQ			0x11
#define PQI_FUNCTION_DELETE_OPERATIONAL_IQ			0x12
#define PQI_FUNCTION_DELETE_OPERATIONAL_OQ			0x13
#define PQI_FUNCTION_CHANGE_OPERATIONAL_IQ_PROP			0x14
#define PQI_CHANGE_OP_IQ_PROP_ASSIGN_AIO			1

#define PQI_DEFAULT_IB_QUEUE					0
/* Interface macros */

#define GET_FW_STATUS(softs) \
        (PCI_MEM_GET32(softs, &softs->ioa_reg->scratchpad3_fw_status, LEGACY_SIS_OMR))

#define SIS_IS_KERNEL_PANIC(softs) \
	(GET_FW_STATUS(softs) & PQI_CTRL_KERNEL_PANIC)

#define SIS_IS_KERNEL_UP(softs) \
	(GET_FW_STATUS(softs) & PQI_CTRL_KERNEL_UP_AND_RUNNING)

#define PQI_GET_CTRL_MODE(softs) \
	(PCI_MEM_GET32(softs, &softs->ioa_reg->scratchpad0, LEGACY_SIS_SCR0))

#define PQI_SAVE_CTRL_MODE(softs, mode) \
	PCI_MEM_PUT32(softs, &softs->ioa_reg->scratchpad0, LEGACY_SIS_SCR0, mode)

#define PQISRC_MAX_TARGETID			1024		
#define PQISRC_MAX_TARGETLUN			64		

/* Vendor specific IU Type for Event config Cmds */
#define PQI_REQUEST_IU_REPORT_EVENT_CONFIG		0x72
#define PQI_REQUEST_IU_SET_EVENT_CONFIG			0x73
#define PQI_REQUEST_IU_ACKNOWLEDGE_VENDOR_EVENT		0xf6
#define PQI_RESPONSE_IU_GENERAL_MANAGEMENT		0x81
#define	PQI_MANAGEMENT_CMD_RESP_TIMEOUT			3000 
#define	PQISRC_EVENT_ACK_RESP_TIMEOUT			1000


/* Supported Event types by controller */
#define PQI_NUM_SUPPORTED_EVENTS		7

#define PQI_EVENT_TYPE_HOTPLUG			0x1
#define PQI_EVENT_TYPE_HARDWARE			0x2
#define PQI_EVENT_TYPE_PHYSICAL_DEVICE		0x4
#define PQI_EVENT_TYPE_LOGICAL_DEVICE		0x5
#define PQI_EVENT_TYPE_AIO_STATE_CHANGE		0xfd
#define PQI_EVENT_TYPE_AIO_CONFIG_CHANGE	0xfe
#define PQI_EVENT_TYPE_HEARTBEAT		0xff

/* for indexing into the pending_events[] field of struct pqisrc_softstate */
#define PQI_EVENT_HEARTBEAT		0
#define PQI_EVENT_HOTPLUG		1
#define PQI_EVENT_HARDWARE		2
#define PQI_EVENT_PHYSICAL_DEVICE	3
#define PQI_EVENT_LOGICAL_DEVICE	4
#define PQI_EVENT_AIO_STATE_CHANGE	5
#define PQI_EVENT_AIO_CONFIG_CHANGE	6

#define PQI_MAX_HEARTBEAT_REQUESTS	5


/* Device flags */
#define	PQISRC_DFLAG_VALID			(1 << 0)
#define	PQISRC_DFLAG_CONFIGURING		(1 << 1)

#define MAX_EMBEDDED_SG_IN_FIRST_IU		4
#define MAX_EMBEDDED_SG_IN_IU			8
#define SG_FLAG_LAST				0x40000000
#define SG_FLAG_CHAIN				0x80000000

#define IN_PQI_RESET(softs)			(softs->ctlr_state & PQI_BUS_RESET)
#define DEV_GONE(dev)				(!dev || (dev->invalid == true))
#define IS_AIO_PATH(dev)				(dev->aio_enabled)
#define IS_RAID_PATH(dev)				(!dev->aio_enabled)

#define DEV_RESET(dvp)                          (dvp->reset_in_progress)

/* SOP data direction flags */
#define SOP_DATA_DIR_NONE			0x00
#define SOP_DATA_DIR_FROM_DEVICE		0x01
#define SOP_DATA_DIR_TO_DEVICE			0x02
#define SOP_DATA_DIR_BIDIRECTIONAL		0x03
#define SOP_PARTIAL_DATA_BUFFER			0x04

#define PQISRC_DMA_VALID			(1 << 0)
#define PQISRC_CMD_NO_INTR			(1 << 1)

#define SOP_TASK_ATTRIBUTE_SIMPLE		0
#define SOP_TASK_ATTRIBUTE_HEAD_OF_QUEUE	1
#define SOP_TASK_ATTRIBUTE_ORDERED		2
#define SOP_TASK_ATTRIBUTE_ACA			4

#define SOP_TASK_MANAGEMENT_FUNCTION_COMPLETE           0x0
#define SOP_TASK_MANAGEMENT_FUNCTION_REJECTED           0x4
#define SOP_TASK_MANAGEMENT_FUNCTION_FAILED		0x5
#define SOP_TASK_MANAGEMENT_FUNCTION_SUCCEEDED          0x8
#define SOP_TASK_MANAGEMENT_FUNCTION_ABORT_TASK		0x01
#define SOP_TASK_MANAGEMENT_FUNCTION_ABORT_TASK_SET	0x02
#define SOP_TASK_MANAGEMENT_LUN_RESET			0x8


/* Additional CDB bytes  */
#define PQI_ADDITIONAL_CDB_BYTES_0		0	/* 16 byte CDB */
#define PQI_ADDITIONAL_CDB_BYTES_4		1	/* 20 byte CDB */
#define PQI_ADDITIONAL_CDB_BYTES_8		2	/* 24 byte CDB */
#define PQI_ADDITIONAL_CDB_BYTES_12		3	/* 28 byte CDB */
#define PQI_ADDITIONAL_CDB_BYTES_16		4	/* 32 byte CDB */

#define PQI_PROTOCOL_SOP			0x0

#define PQI_AIO_STATUS_GOOD			0x0
#define PQI_AIO_STATUS_CHECK_CONDITION		0x2
#define PQI_AIO_STATUS_CONDITION_MET		0x4
#define PQI_AIO_STATUS_DEVICE_BUSY		0x8
#define PQI_AIO_STATUS_INT_GOOD			0x10
#define PQI_AIO_STATUS_INT_COND_MET		0x14
#define PQI_AIO_STATUS_RESERV_CONFLICT		0x18
#define PQI_AIO_STATUS_CMD_TERMINATED		0x22
#define PQI_AIO_STATUS_QUEUE_FULL		0x28
#define PQI_AIO_STATUS_TASK_ABORTED		0x40
#define PQI_AIO_STATUS_UNDERRUN			0x51
#define PQI_AIO_STATUS_OVERRUN			0x75
/* Status when Target Failure */
#define PQI_AIO_STATUS_IO_ERROR			0x1
#define PQI_AIO_STATUS_IO_ABORTED		0x2
#define PQI_AIO_STATUS_IO_NO_DEVICE		0x3
#define PQI_AIO_STATUS_INVALID_DEVICE		0x4
#define PQI_AIO_STATUS_AIO_PATH_DISABLED	0xe

/* Service Response */
#define PQI_AIO_SERV_RESPONSE_COMPLETE			0
#define PQI_AIO_SERV_RESPONSE_FAILURE			1
#define PQI_AIO_SERV_RESPONSE_TMF_COMPLETE		2
#define PQI_AIO_SERV_RESPONSE_TMF_SUCCEEDED		3
#define PQI_AIO_SERV_RESPONSE_TMF_REJECTED		4
#define PQI_AIO_SERV_RESPONSE_TMF_INCORRECT_LUN		5

#define PQI_TMF_WAIT_DELAY				10000000	/* 10 seconds */

#define PQI_RAID_STATUS_GOOD				PQI_AIO_STATUS_GOOD
#define PQI_RAID_STATUS_CHECK_CONDITION			PQI_AIO_STATUS_CHECK_CONDITION
#define PQI_RAID_STATUS_CONDITION_MET			PQI_AIO_STATUS_CONDITION_MET
#define PQI_RAID_STATUS_DEVICE_BUSY			PQI_AIO_STATUS_DEVICE_BUSY
#define PQI_RAID_STATUS_INT_GOOD			PQI_AIO_STATUS_INT_GOOD	
#define PQI_RAID_STATUS_INT_COND_MET			PQI_AIO_STATUS_INT_COND_MET
#define PQI_RAID_STATUS_RESERV_CONFLICT			PQI_AIO_STATUS_RESERV_CONFLICT
#define PQI_RAID_STATUS_CMD_TERMINATED			PQI_AIO_STATUS_CMD_TERMINATED
#define PQI_RAID_STATUS_QUEUE_FULL			PQI_AIO_STATUS_QUEUE_FULL
#define PQI_RAID_STATUS_TASK_ABORTED			PQI_AIO_STATUS_TASK_ABORTED
#define PQI_RAID_STATUS_UNDERRUN			PQI_AIO_STATUS_UNDERRUN	
#define PQI_RAID_STATUS_OVERRUN				PQI_AIO_STATUS_OVERRUN

/* VPD inquiry pages */
#define SCSI_VPD_SUPPORTED_PAGES	0x0	/* standard page */
#define SCSI_VPD_DEVICE_ID		0x83	/* standard page */
#define SA_VPD_PHYS_DEVICE_ID		0xc0	/* vendor-specific page */
#define SA_VPD_LV_DEVICE_GEOMETRY	0xc1	/* vendor-specific page */
#define SA_VPD_LV_IOACCEL_STATUS	0xc2	/* vendor-specific page */
#define SA_VPD_LV_STATUS		0xc3	/* vendor-specific page */

#define VPD_PAGE			(1 << 8)


/* logical volume states */
#define SA_LV_OK					0x0
#define SA_LV_NOT_AVAILABLE				0xb
#define SA_LV_UNDERGOING_ERASE				0xf
#define SA_LV_UNDERGOING_RPI				0x12
#define SA_LV_PENDING_RPI				0x13
#define SA_LV_ENCRYPTED_NO_KEY				0x14
#define SA_LV_PLAINTEXT_IN_ENCRYPT_ONLY_CONTROLLER	0x15
#define SA_LV_UNDERGOING_ENCRYPTION			0x16
#define SA_LV_UNDERGOING_ENCRYPTION_REKEYING		0x17
#define SA_LV_ENCRYPTED_IN_NON_ENCRYPTED_CONTROLLER	0x18
#define SA_LV_PENDING_ENCRYPTION			0x19
#define SA_LV_PENDING_ENCRYPTION_REKEYING		0x1a
#define SA_LV_STATUS_VPD_UNSUPPORTED			0xff

/*
 * assume worst case: SATA queue depth of 31 minus 4 internal firmware commands
 */
#define PQI_PHYSICAL_DISK_DEFAULT_MAX_QUEUE_DEPTH	27

/* 0 = no limit */
#define PQI_LOGICAL_DISK_DEFAULT_MAX_QUEUE_DEPTH	0



#define RAID_CTLR_LUNID		"\0\0\0\0\0\0\0\0"

#define SA_CACHE_FLUSH		0x1
#define SA_INQUIRY		0x12
#define SA_REPORT_LOG		0xc2	/* Report Logical LUNs */
#define SA_REPORT_PHYS		0xc3	/* Report Physical LUNs */
#define SA_CISS_READ		0xc0
#define SA_GET_RAID_MAP		0xc8

#define SA_REPORT_LOG_EXTENDED		0x1
#define SA_REPORT_PHYS_EXTENDED		0x2

#define SA_CACHE_FLUSH_BUF_LEN		4

#define REPORT_LUN_DEV_FLAG_AIO_ENABLED 		0x8
#define PQI_MAX_TRANSFER_SIZE				(4 * 1024U * 1024U)
#define RAID_MAP_MAX_ENTRIES				1024
#define RAID_MAP_ENCRYPTION_ENABLED			0x1
#define PQI_PHYSICAL_DISK_DEFAULT_MAX_QUEUE_DEPTH	27

#define ASC_LUN_NOT_READY				0x4
#define ASCQ_LUN_NOT_READY_FORMAT_IN_PROGRESS		0x4
#define ASCQ_LUN_NOT_READY_INITIALIZING_CMD_REQ		0x2


#define OBDR_SIG_OFFSET		43
#define OBDR_TAPE_SIG		"$DR-10"
#define OBDR_SIG_LEN		(sizeof(OBDR_TAPE_SIG) - 1)
#define OBDR_TAPE_INQ_SIZE	(OBDR_SIG_OFFSET + OBDR_SIG_LEN)


#define IOACCEL_STATUS_BYTE	4
#define OFFLOAD_CONFIGURED_BIT	0x1
#define OFFLOAD_ENABLED_BIT	0x2

#define PQI_RAID_DATA_IN_OUT_GOOD		0x0
#define PQI_RAID_DATA_IN_OUT_UNDERFLOW		0x1
#define PQI_RAID_DATA_IN_OUT_UNSOLICITED_ABORT	0xf3
#define PQI_RAID_DATA_IN_OUT_ABORTED		0xf4

#define PQI_PHYSICAL_DEVICE_BUS		0
#define PQI_RAID_VOLUME_BUS		1
#define PQI_HBA_BUS			2
#define PQI_EXTERNAL_RAID_VOLUME_BUS	3
#define PQI_MAX_BUS			PQI_EXTERNAL_RAID_VOLUME_BUS

#define TEST_UNIT_READY		0x00
#define SCSI_VPD_HEADER_LENGTH	64


#define PQI_MAX_MULTILUN	256
#define PQI_MAX_LOGICALS	64
#define PQI_MAX_PHYSICALS	1024
#define	PQI_MAX_DEVICES		(PQI_MAX_LOGICALS + PQI_MAX_PHYSICALS + 1) /* 1 for controller device entry */
#define PQI_MAX_EXT_TARGETS	32

#define PQI_CTLR_INDEX		(PQI_MAX_DEVICES - 1)
#define PQI_PD_INDEX(t)		(t + PQI_MAX_LOGICALS)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define MAX_TARGET_DEVICES 1024

#define PQI_NO_MEM	2

typedef enum pqisrc_device_status {
	DEVICE_NOT_FOUND,
	DEVICE_CHANGED,
	DEVICE_UNCHANGED,
} device_status_t;

#define SA_RAID_0			0
#define SA_RAID_4			1
#define SA_RAID_1			2	/* also used for RAID 10 */
#define SA_RAID_5			3	/* also used for RAID 50 */
#define SA_RAID_51			4
#define SA_RAID_6			5	/* also used for RAID 60 */
#define SA_RAID_ADM			6	/* also used for RAID 1+0 ADM */
#define SA_RAID_MAX			SA_RAID_ADM
#define SA_RAID_UNKNOWN			0xff

/* BMIC commands */
#define BMIC_IDENTIFY_CONTROLLER		0x11
#define BMIC_IDENTIFY_PHYSICAL_DEVICE		0x15
#define BMIC_READ				0x26
#define BMIC_WRITE				0x27
#define BMIC_SENSE_CONTROLLER_PARAMETERS	0x64
#define BMIC_SENSE_SUBSYSTEM_INFORMATION	0x66
#define BMIC_CACHE_FLUSH			0xc2
#define BMIC_FLASH_FIRMWARE			0xf7
#define BMIC_WRITE_HOST_WELLNESS		0xa5


#define MASKED_DEVICE(lunid)			((lunid)[3] & 0xC0)
#define BMIC_GET_LEVEL_2_BUS(lunid)		((lunid)[7] & 0x3F)
#define BMIC_GET_LEVEL_TWO_TARGET(lunid)	((lunid)[6])
#define BMIC_GET_DRIVE_NUMBER(lunid)		\
	(((BMIC_GET_LEVEL_2_BUS((lunid)) - 1) << 8) +	\
	BMIC_GET_LEVEL_TWO_TARGET((lunid)))
#define NON_DISK_PHYS_DEV(rle)			\
	(((reportlun_ext_entry_t *)(rle))->device_flags & 0x1)

#define NO_TIMEOUT		((unsigned long) -1)

#define BMIC_DEVICE_TYPE_SATA	0x1

/* No of IO slots required for internal requests */
#define PQI_RESERVED_IO_SLOTS_SYNC_REQUESTS	3
#define PQI_RESERVED_IO_SLOTS_TMF		1
#define PQI_RESERVED_IO_SLOTS_CNT		(PQI_NUM_SUPPORTED_EVENTS + \
						PQI_RESERVED_IO_SLOTS_TMF + \
						PQI_RESERVED_IO_SLOTS_SYNC_REQUESTS)

static inline uint16_t GET_LE16(const uint8_t *p)
{
	return p[0] | p[1] << 8;
}

static inline uint32_t GET_LE32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

static inline uint64_t GET_LE64(const uint8_t *p)
{
	return (((uint64_t)GET_LE32(p + 4) << 32) |
		GET_LE32(p));
}

static inline uint16_t GET_BE16(const uint8_t *p)
{
        return p[0] << 8 | p[1];
}

static inline uint32_t GET_BE32(const uint8_t *p)
{
        return p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static inline uint64_t GET_BE64(const uint8_t *p)
{
        return (((uint64_t)GET_BE32(p) << 32) |
               GET_BE32(p + 4));
}

static inline void PUT_BE16(uint16_t val, uint8_t *p)
{
        *p++ = val >> 8;
        *p++ = val;
}

static inline void PUT_BE32(uint32_t val, uint8_t *p)
{
        PUT_BE16(val >> 16, p);
        PUT_BE16(val, p + 2);
}

static inline void PUT_BE64(uint64_t val, uint8_t *p)
{
        PUT_BE32(val >> 32, p);
        PUT_BE32(val, p + 4);
}


#define OS_FREEBSD
#define SIS_POLL_WAIT

#define OS_ATTRIBUTE_PACKED         __attribute__((__packed__))
#define OS_ATTRIBUTE_ALIGNED(n)     __attribute__((aligned(n)))


/* Management Interface */
#define CCISS_IOC_MAGIC		'C'
#define SMARTPQI_IOCTL_BASE     'M'
#define CCISS_GETDRIVVER       _IOWR(SMARTPQI_IOCTL_BASE, 0, driver_info)
#define CCISS_GETPCIINFO       _IOWR(SMARTPQI_IOCTL_BASE, 1, pqi_pci_info_t)
#define SMARTPQI_PASS_THRU     _IOWR(SMARTPQI_IOCTL_BASE, 2, IOCTL_Command_struct)
#define CCISS_PASSTHRU         _IOWR('C', 210, IOCTL_Command_struct)
#define CCISS_REGNEWD          _IO(CCISS_IOC_MAGIC, 14)

/*IOCTL  pci_info structure */
typedef struct pqi_pci_info
{
       unsigned char   bus;
       unsigned char   dev_fn;
       unsigned short  domain;
       uint32_t        board_id;
       uint32_t        chip_id;
}pqi_pci_info_t;

typedef struct _driver_info
{
	unsigned char 	major_version;
	unsigned char 	minor_version;
	unsigned char 	release_version;
	unsigned long 	build_revision;
	unsigned long 	max_targets;
	unsigned long 	max_io;
	unsigned long 	max_transfer_length;
}driver_info, *pdriver_info;

typedef uint8_t *passthru_buf_type_t;


#define PQISRC_DRIVER_MAJOR		1
#define PQISRC_DRIVER_MINOR		0
#define PQISRC_DRIVER_RELEASE		3
#define PQISRC_DRIVER_REVISION		239

#define STR(s)                          # s
#define PQISRC_VERSION(a, b, c, d)      STR(a.b.c-d)
#define PQISRC_DRIVER_VERSION           PQISRC_VERSION(PQISRC_DRIVER_MAJOR, \
                                        PQISRC_DRIVER_MINOR, \
                                        PQISRC_DRIVER_RELEASE, \
                                        PQISRC_DRIVER_REVISION)
	
/* End Management interface */

#ifdef ASSERT
#undef ASSERT
#endif

#define ASSERT(cond) {\
        	if (!(cond)) { \
			printf("Assertion failed at file %s line %d\n",__FILE__,__LINE__);	\
		}	\
		}


#define PQI_MAX_MSIX            64      /* vectors */
#define PQI_MSI_CTX_SIZE        sizeof(pqi_intr_ctx)+1
#define IS_POLLING_REQUIRED(softs)	if (cold) {\
					pqisrc_process_event_intr_src(softs, 0);\
					pqisrc_process_response_queue(softs, 1);\
				}

#define OS_GET_TASK_ATTR(rcb)		os_get_task_attr(rcb)
#define OS_FW_HEARTBEAT_TIMER_INTERVAL (5)

typedef struct PCI_ACC_HANDLE {
        bus_space_tag_t         pqi_btag;
        bus_space_handle_t      pqi_bhandle;
} PCI_ACC_HANDLE_T;

/*
 * Legacy SIS Register definitions for the Adaptec PMC SRC/SRCv/smartraid adapters.
 */
/* accessible via BAR0 */
#define LEGACY_SIS_IOAR		0x18	/* IOA->host interrupt register */
#define LEGACY_SIS_IDBR		0x20	/* inbound doorbell register */
#define LEGACY_SIS_IISR		0x24	/* inbound interrupt status register */
#define LEGACY_SIS_OIMR		0x34	/* outbound interrupt mask register */
#define LEGACY_SIS_ODBR_R	0x9c	/* outbound doorbell register read */
#define LEGACY_SIS_ODBR_C	0xa0	/* outbound doorbell register clear */

#define LEGACY_SIS_SCR0		0xb0	/* scratchpad 0 */
#define LEGACY_SIS_OMR		0xbc	/* outbound message register */
#define LEGACY_SIS_IQUE64_L	0xc0	/* inbound queue address 64-bit (low) */
#define LEGACY_SIS_IQUE64_H	0xc4	/* inbound queue address 64-bit (high)*/
#define LEGACY_SIS_ODBR_MSI	0xc8	/* MSI register for sync./AIF */
#define LEGACY_SIS_IQN_L	0xd0	/* inbound queue native mode (low) */
#define LEGACY_SIS_IQN_H	0xd4	/* inbound queue native mode (high)*/
#define LEGACY_SIS_MAILBOX	0x7fc60	/* mailbox (20 bytes) */
#define LEGACY_SIS_SRCV_MAILBOX	0x1000	/* mailbox (20 bytes) */

#define LEGACY_SIS_ODR_SHIFT 	12	/* outbound doorbell shift */
#define LEGACY_SIS_IDR_SHIFT 	9	/* inbound doorbell shift */


/*
 * PQI Register definitions for the smartraid adapters
 */
/* accessible via BAR0 */
#define PQI_SIGNATURE                  0x4000
#define PQI_ADMINQ_CONFIG              0x4008
#define PQI_ADMINQ_CAP                 0x4010
#define PQI_LEGACY_INTR_STATUS         0x4018  
#define PQI_LEGACY_INTR_MASK_SET       0x401C
#define PQI_LEGACY_INTR_MASK_CLR       0x4020
#define PQI_DEV_STATUS                 0x4040
#define PQI_ADMIN_IBQ_PI_OFFSET        0x4048
#define PQI_ADMIN_OBQ_CI_OFFSET        0x4050
#define PQI_ADMIN_IBQ_ELEM_ARRAY_ADDR  0x4058
#define PQI_ADMIN_OBQ_ELEM_ARRAY_ADDR  0x4060
#define PQI_ADMIN_IBQ_CI_ADDR          0x4068
#define PQI_ADMIN_OBQ_PI_ADDR          0x4070
#define PQI_ADMINQ_PARAM               0x4078
#define PQI_DEV_ERR                    0x4080
#define PQI_DEV_ERR_DETAILS            0x4088
#define PQI_DEV_RESET                  0x4090
#define PQI_POWER_ACTION               0x4094

/* Busy wait micro seconds */
#define OS_BUSYWAIT(x) DELAY(x)
#define OS_SLEEP(timeout)	\
	DELAY(timeout);
	
#define OS_HOST_WELLNESS_TIMEOUT	(24 * 3600)


#define LE_16(x) htole16(x)
#define LE_32(x) htole32(x)
#define LE_64(x) htole64(x)
#define BE_16(x) htobe16(x)
#define BE_32(x) htobe32(x)
#define BE_64(x) htobe64(x)

#define PQI_HWIF_SRCV           0
#define PQI_HWIF_UNKNOWN        -1


#define SMART_STATE_SUSPEND     	(1<<0)
#define SMART_STATE_UNUSED0     	(1<<1)
#define SMART_STATE_INTERRUPTS_ON       (1<<2)
#define SMART_STATE_AIF_SLEEPER 	(1<<3)
#define SMART_STATE_RESET               (1<<4)

#define PQI_FLAG_BUSY 			(1<<0)
#define PQI_MSI_ENABLED 		(1<<1)
#define PQI_SIM_REGISTERED 		(1<<2)
#define PQI_MTX_INIT	 		(1<<3)


#define PQI_CMD_MAPPED 			(1<<2)

/* Interrupt context to get oq_id */
typedef struct pqi_intr_ctx {
        int 	 oq_id;
        device_t pqi_dev;
}pqi_intr_ctx_t;

typedef uint8_t os_dev_info_t;

typedef struct OS_SPECIFIC {
	device_t                pqi_dev; 	
	struct resource		*pqi_regs_res0; /* reg. if. window */
	int			pqi_regs_rid0;		/* resource ID */
	bus_dma_tag_t		pqi_parent_dmat;	/* parent DMA tag */
	bus_dma_tag_t           pqi_buffer_dmat;

	/* controller hardware interface */
	int			pqi_hwif;	
	struct resource         *pqi_irq[PQI_MAX_MSIX];  /* interrupt */
	int                     pqi_irq_rid[PQI_MAX_MSIX];
	void                    *intrcookie[PQI_MAX_MSIX];
	bool                    intr_registered[PQI_MAX_MSIX];
	bool			msi_enabled;            /* MSI/MSI-X enabled */
	pqi_intr_ctx_t		*msi_ctx;
	int			oq_id;
	int			pqi_state;
	uint32_t		pqi_flags;
	struct mtx              cam_lock;
	struct mtx              map_lock;
	int                     mtx_init;
	int                     sim_registered;
	struct cam_devq         *devq;
	struct cam_sim          *sim;
	struct cam_path         *path;
	struct task		event_task;
	struct cdev             *cdev;
	struct callout_handle   wellness_periodic;	/* periodic event handling */
	struct callout_handle   heartbeat_timeout_id;	/* heart beat event handling */
	eventhandler_tag        eh;
} OS_SPECIFIC_T;

typedef bus_addr_t dma_addr_t; 

/* Atomic */
typedef volatile uint64_t OS_ATOMIC64_T;
#define OS_ATOMIC64_SET(_softs, target, val)	atomic_set_long(&(_softs)->target, val)
#define OS_ATOMIC64_READ(_softs, target)	atomic_load_acq_64(&(_softs)->target)
#define OS_ATOMIC64_INC(_softs, target)		atomic_add_64(&(_softs)->target, 1)

/* Register access macros */
#define PCI_MEM_GET32( _softs, _absaddr, _offset ) \
    bus_space_read_4(_softs->pci_mem_handle.pqi_btag, \
        _softs->pci_mem_handle.pqi_bhandle, _offset)

#define PCI_MEM_GET64( _softs, _absaddr, _offset ) \
    bus_space_read_8(_softs->pci_mem_handle.pqi_btag, \
        _softs->pci_mem_handle.pqi_bhandle, _offset)

#define PCI_MEM_PUT32( _softs, _absaddr, _offset, _val ) \
    bus_space_write_4(_softs->pci_mem_handle.pqi_btag, \
        _softs->pci_mem_handle.pqi_bhandle, _offset, _val)

#define PCI_MEM_PUT64( _softs, _absaddr, _offset, _val ) \
    bus_space_write_8(_softs->pci_mem_handle.pqi_btag, \
        _softs->pci_mem_handle.pqi_bhandle, _offset, _val)

#define PCI_MEM_GET_BUF(_softs, _absaddr, _offset, buf, size) \
	bus_space_read_region_1(_softs->pci_mem_handle.pqi_btag,\
	_softs->pci_mem_handle.pqi_bhandle, _offset, buf, size)
    
/* Lock */
typedef struct mtx OS_LOCK_T;
typedef struct sema OS_SEMA_LOCK_T;

#define	OS_PQILOCK_T OS_LOCK_T

#define OS_ACQUIRE_SPINLOCK(_lock) mtx_lock_spin(_lock)
#define OS_RELEASE_SPINLOCK(_lock) mtx_unlock_spin(_lock)

#define OS_INIT_PQILOCK(_softs,_lock,_lockname) os_init_spinlock(_softs,_lock,_lockname)
#define OS_UNINIT_PQILOCK(_lock) os_uninit_spinlock(_lock)

#define PQI_LOCK(_lock) OS_ACQUIRE_SPINLOCK(_lock)
#define PQI_UNLOCK(_lock) OS_RELEASE_SPINLOCK(_lock)

#define OS_GET_CDBP(rcb)	((rcb->cm_ccb->ccb_h.flags & CAM_CDB_POINTER) ? rcb->cm_ccb->csio.cdb_io.cdb_ptr : rcb->cm_ccb->csio.cdb_io.cdb_bytes)
#define GET_SCSI_BUFFLEN(rcb)	(rcb->cm_ccb->csio.dxfer_len)

#define OS_GET_IO_QINDEX(softs,rcb)	curcpu % softs->num_op_obq
#define OS_GET_IO_RESP_QID(softs,rcb)	(softs->op_ob_q[(OS_GET_IO_QINDEX(softs,rcb))].q_id)
#define OS_GET_IO_REQ_QINDEX(softs,rcb)	OS_GET_IO_QINDEX(softs,rcb)
#define OS_GET_TMF_RESP_QID		OS_GET_IO_RESP_QID
#define OS_GET_TMF_REQ_QINDEX		OS_GET_IO_REQ_QINDEX

/* check request type */
#define is_internal_req(rcb)	(!(rcb)->cm_ccb)

/* sg elements addr, len, flags */
#define OS_GET_IO_SG_COUNT(rcb)		rcb->nseg
#define OS_GET_IO_SG_ADDR(rcb,i)	rcb->sgt[i].addr
#define OS_GET_IO_SG_LEN(rcb,i)		rcb->sgt[i].len

/* scsi commands used in pqilib for RAID bypass*/
#define SCMD_READ_6	READ_6
#define SCMD_WRITE_6	WRITE_6
#define SCMD_READ_10	READ_10
#define SCMD_WRITE_10	WRITE_10
#define SCMD_READ_12	READ_12
#define SCMD_WRITE_12	WRITE_12
#define SCMD_READ_16	READ_16
#define SCMD_WRITE_16	WRITE_16

/* Debug facility */

#define PQISRC_LOG_LEVEL  0x60

static int logging_level  = PQISRC_LOG_LEVEL;

#define	PQISRC_FLAGS_MASK		0x0000ffff
#define	PQISRC_FLAGS_INIT 		0x00000001
#define	PQISRC_FLAGS_INFO 		0x00000002
#define	PQISRC_FLAGS_FUNC		0x00000004
#define	PQISRC_FLAGS_TRACEIO		0x00000008
#define	PQISRC_FLAGS_DISC		0x00000010
#define	PQISRC_FLAGS_WARN		0x00000020
#define	PQISRC_FLAGS_ERROR		0x00000040


#define	DBG_INIT(fmt,args...)						\
		do {							\
			if (logging_level & PQISRC_FLAGS_INIT) { 	\
				printf("[INIT]:[ %s ] [ %d ]"fmt,__func__,__LINE__,##args);			\
			}						\
		}while(0);

#define	DBG_INFO(fmt,args...)						\
		do {							\
			if (logging_level & PQISRC_FLAGS_INFO) { 	\
				printf("[INFO]:[ %s ] [ %d ]"fmt,__func__,__LINE__,##args);			\
			}						\
		}while(0);

#define	DBG_FUNC(fmt,args...)						\
		do {							\
			if (logging_level & PQISRC_FLAGS_FUNC) { 	\
				printf("[FUNC]:[ %s ] [ %d ]"fmt,__func__,__LINE__,##args);			\
			}						\
		}while(0);

#define	DBG_TRACEIO(fmt,args...)					\
		do {							\
			if (logging_level & PQISRC_FLAGS_TRACEIO) { 	\
				printf("[TRACEIO]:[ %s ] [ %d ]"fmt,__func__,__LINE__,##args);			\
			}						\
		}while(0);

#define	DBG_DISC(fmt,args...)						\
		do {							\
			if (logging_level & PQISRC_FLAGS_DISC) { 	\
				printf("[DISC]:[ %s ] [ %d ]"fmt,__func__,__LINE__,##args);			\
			}						\
		}while(0);

#define	DBG_WARN(fmt,args...)						\
		do {							\
			if (logging_level & PQISRC_FLAGS_WARN) { 	\
				printf("[WARN]:[%u:%u.%u][CPU %d][%s][%d]:"fmt,softs->bus_id,softs->device_id,softs->func_id,curcpu,__func__,__LINE__,##args);\
			}						\
		}while(0);

#define	DBG_ERR(fmt,args...)						\
		do {							\
			if (logging_level & PQISRC_FLAGS_ERROR) { 	\
				printf("[ERROR]::[%u:%u.%u][CPU %d][%s][%d]:"fmt,softs->bus_id,softs->device_id,softs->func_id,curcpu,__func__,__LINE__,##args); \
			}						\
		}while(0);
#define	DBG_IO(fmt,args...)						\
		do {							\
			if (logging_level & PQISRC_FLAGS_TRACEIO) { 	\
				printf("[IO]:[ %s ] [ %d ]"fmt,__func__,__LINE__,##args);			\
			}						\
		}while(0);

#define	DBG_ERR_BTL(device,fmt,args...)						\
		do {							\
			if (logging_level & PQISRC_FLAGS_ERROR) { 	\
				printf("[ERROR]::[%u:%u.%u][%u,%u,%u][CPU %d][%s][%d]:"fmt, softs->bus_id, softs->device_id, softs->func_id, device->bus, device->target, device->lun,curcpu,__func__,__LINE__,##args); \
			}						\
		}while(0);

#define	DBG_WARN_BTL(device,fmt,args...)						\
		do {							\
			if (logging_level & PQISRC_FLAGS_WARN) { 	\
				printf("[WARN]:[%u:%u.%u][%u,%u,%u][CPU %d][%s][%d]:"fmt, softs->bus_id, softs->device_id, softs->func_id, device->bus, device->target, device->lun,curcpu,__func__,__LINE__,##args);\
			}						\
		}while(0);

#endif // _PQI_DEFINES_H
