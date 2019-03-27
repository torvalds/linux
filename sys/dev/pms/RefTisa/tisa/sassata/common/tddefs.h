/*******************************************************************************
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
*following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided
*with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED 
*WARRANTIES,INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
*FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
*NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
*LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
*SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*
* $FreeBSD$
*
********************************************************************************/
/*******************************************************************************/
/** \file
 *
 * The file contains defines and data structures for SAS/SATA TD layer
 *
 */

#ifndef __TDDEFS_H__
#define __TDDEFS_H__



#ifndef agTRUE
#define agTRUE          1
#endif

#ifndef agFALSE
#define agFALSE         0
#endif

#ifndef agNULL
#define agNULL     ((void *)0)
#endif

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

#ifndef IN_OUT
#define IN_OUT
#endif

#ifndef os_bit8
#define os_bit8     bit8
#endif

#ifndef os_bit16
#define os_bit16    bit16
#endif

#ifndef os_bit32
#define os_bit32    bit32
#endif

#ifndef OFF
#define OFF     0
#endif

#ifndef ON
#define ON      1
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#endif

#define TD_OPERATION_INITIATOR    0x1
#define TD_OPERATION_TARGET       0x2

/* indices for mem_t structures */
#define DEK_MEM_INDEX_1             15
#define DEK_MEM_INDEX_2             16

/* some useful macros */
#ifndef AG_ALIGNSIZE
#define AG_ALIGNSIZE(count, alignment) (bit32) ( (bitptr)(count)+(bitptr)(alignment) )
#endif

#define DEFAULT_KEY_BUFFER_SIZE             64

/**< the default maximum number of phys */
#ifdef FPGA_CARD

#define TD_MAX_NUM_PHYS 2

#else
#define TD_MAX_NUM_PHYS 16
#define TD_MAX_CARD_NUM 20
#endif

#define TD_CARD_ID_FREE     0
#define TD_CARD_ID_ALLOC    1
#define TD_CARD_ID_LEN      128

/**< the maximum number of port context */
/* should be the number of phyical phys in chip + 1 */
#define TD_MAX_PORT_CONTEXT 16
/**< the maximum number of target device */
/* For Initiator and Target
   this is initial value for MaxTargets in the configuration(adj) file */
#define DEFAULT_MAX_DEV 256
/* the maximum number of interrupt coalesce context */
#define TD_MAX_INT_COALESCE 512

#if (defined(__FreeBSD__))
#define MAX_OUTSTANDING_IO_PER_LUN   64
#else
#define MAX_OUTSTANDING_IO_PER_LUN  254  //64
#endif

/* default values */
#define DEFAULT_MAX_ACTIVE_IOS  128
#define DEFAULT_NUM_REG_CLIENTS 256
#define DEFAULT_NUM_INBOUND_QUEUE 1
#define DEFAULT_NUM_OUTBOUND_QUEUE 1
#define DEFAULT_INBOUND_QUEUE_SIZE 512
#define DEFAULT_INBOUND_QUEUE_ELE_SIZE 128
#define DEFAULT_OUTBOUND_QUEUE_SIZE 512
#define DEFAULT_OUTBOUND_QUEUE_ELE_SIZE 128
#define DEFAULT_OUTBOUND_QUEUE_INTERRUPT_DELAY 0
#define DEFAULT_OUTBOUND_QUEUE_INTERRUPT_COUNT 1
#define DEFAULT_OUTBOUND_INTERRUPT_ENABLE 1
#define DEFAULT_INBOUND_QUEUE_PRIORITY         0
#define DEFAULT_QUEUE_OPTION         0
#define DEFAULT_FW_MAX_PORTS         8



/* SAS device type definition. SAS spec(r.7) p206  */
#define SAS_NO_DEVICE                    0
#define SAS_END_DEVICE                   1
#define SAS_EDGE_EXPANDER_DEVICE         2
#define SAS_FANOUT_EXPANDER_DEVICE       3

/* routing attributes */
#define SAS_ROUTING_DIRECT                             0x00
#define SAS_ROUTING_SUBTRACTIVE                        0x01
#define SAS_ROUTING_TABLE                              0x02

#define SAS_CONNECTION_RATE_1_5G                       0x08
#define SAS_CONNECTION_RATE_3_0G                       0x09
#define SAS_CONNECTION_RATE_6_0G                       0x0A
#define SAS_CONNECTION_RATE_12_0G                      0x0B

/**< defines the maximum number of expanders */
#define TD_MAX_EXPANDER_PHYS                         256
/**< the maximum number of expanders at TD */
#define TD_MAX_EXPANDER 128

/*****************************************************************************
** SCSI Operation Codes (first byte in CDB)
*****************************************************************************/


#define SCSIOPC_TEST_UNIT_READY     0x00
#define SCSIOPC_INQUIRY             0x12
#define SCSIOPC_MODE_SENSE_6        0x1A
#define SCSIOPC_MODE_SENSE_10       0x5A
#define SCSIOPC_MODE_SELECT_6       0x15
#define SCSIOPC_START_STOP_UNIT     0x1B
#define SCSIOPC_READ_CAPACITY_10    0x25
#define SCSIOPC_READ_CAPACITY_16    0x9E
#define SCSIOPC_READ_6              0x08
#define SCSIOPC_READ_10             0x28
#define SCSIOPC_READ_12             0xA8
#define SCSIOPC_READ_16             0x88
#define SCSIOPC_WRITE_6             0x0A
#define SCSIOPC_WRITE_10            0x2A
#define SCSIOPC_WRITE_12            0xAA
#define SCSIOPC_WRITE_16            0x8A
#define SCSIOPC_WRITE_VERIFY        0x2E
#define SCSIOPC_VERIFY_10           0x2F
#define SCSIOPC_VERIFY_12           0xAF
#define SCSIOPC_VERIFY_16           0x8F
#define SCSIOPC_REQUEST_SENSE       0x03
#define SCSIOPC_REPORT_LUN          0xA0
#define SCSIOPC_FORMAT_UNIT         0x04
#define SCSIOPC_SEND_DIAGNOSTIC     0x1D
#define SCSIOPC_WRITE_SAME_10       0x41
#define SCSIOPC_WRITE_SAME_16       0x93
#define SCSIOPC_READ_BUFFER         0x3C
#define SCSIOPC_WRITE_BUFFER        0x3B

#define SCSIOPC_GET_CONFIG          0x46
#define SCSIOPC_GET_EVENT_STATUS_NOTIFICATION        0x4a
#define SCSIOPC_REPORT_KEY          0xA4
#define SCSIOPC_SEND_KEY            0xA3
#define SCSIOPC_READ_DVD_STRUCTURE  0xAD
#define SCSIOPC_TOC                 0x43
#define SCSIOPC_PREVENT_ALLOW_MEDIUM_REMOVAL         0x1E
#define SCSIOPC_READ_VERIFY         0x42

#define SCSIOPC_LOG_SENSE           0x4D
#define SCSIOPC_LOG_SELECT          0x4C
#define SCSIOPC_MODE_SELECT_6       0x15
#define SCSIOPC_MODE_SELECT_10      0x55
#define SCSIOPC_SYNCHRONIZE_CACHE_10 0x35
#define SCSIOPC_SYNCHRONIZE_CACHE_16 0x91
#define SCSIOPC_WRITE_AND_VERIFY_10 0x2E
#define SCSIOPC_WRITE_AND_VERIFY_12 0xAE
#define SCSIOPC_WRITE_AND_VERIFY_16 0x8E
#define SCSIOPC_READ_MEDIA_SERIAL_NUMBER 0xAB
#define SCSIOPC_REASSIGN_BLOCKS     0x07





/*****************************************************************************
** SCSI GENERIC 6 BYTE CDB
*****************************************************************************/
typedef struct CBD6_s {
  bit8  opcode;
  bit8  rsv; /* not 100% correct */
  bit8  lba[2]; /* not 100% correct */
  bit8  len;
  bit8  control;
} CDB6_t;



/*****************************************************************************
** SCSI GENERIC 10 BYTE CDB
*****************************************************************************/
typedef struct CBD10_s {
  bit8  opcode;
  bit8  rsv_service;
  bit8  lba[4];
  bit8  rsv;
  bit8  len[2];
  bit8  control;
} CDB10_t;

/*****************************************************************************
** SCSI GENERIC 12 BYTE CDB
*****************************************************************************/
typedef struct CBD12_s {
  bit8  opcode;
  bit8  rsv_service;
  bit8  lba[4];
  bit8  len[4];
  bit8  rsv;
  bit8  control;
} CDB12_t;


/*****************************************************************************
** SCSI GENERIC 16 BYTE CDB
*****************************************************************************/
typedef struct CBD16_s {
  bit8  opcode;
  bit8  rsv_service;
  bit8  lba[4];
  bit8  add_cdb[4];
  bit8  len[4];
  bit8  rsv;
  bit8  control;
} CDB16_t;

#define BLOCK_BYTE_LENGTH             512

/*****************************************************************************
** SCSI STATUS BYTES
*****************************************************************************/

#define SCSI_STATUS_GOOD               0x00
#define SCSI_STATUS_CHECK_CONDITION    0x02
#define SCSI_STATUS_BUSY               0x08
#define SCSI_STATUS_COMMAND_TERMINATED 0x22
#define SCSI_STATUS_TASK_SET_FULL      0x28

/*****************************************************************************
** SAS TM Function data present see SAS spec p311 Table 109 (Revision 7)
*****************************************************************************/
#define NO_DATA            0
#define RESPONSE_DATA      1
#define SENSE_DATA         2

/* 4 bytes, SAS spec p312 Table 110 (Revision 7) */
#define RESPONSE_DATA_LEN  4

#define SAS_CMND 0
#define SAS_TM   1

/* SMP frame type */
#define SMP_REQUEST        0x40
#define SMP_RESPONSE       0x41

#define SMP_INITIATOR     0x01
#define SMP_TARGET        0x02

/* default SMP timeout: 0xFFFF is the Maximum Allowed */
#define DEFAULT_SMP_TIMEOUT       0xFFFF

/* SMP direct payload size limit: IOMB direct payload size = 48 */
#define SMP_DIRECT_PAYLOAD_LIMIT 44

/* SMP function */
#define SMP_REPORT_GENERAL                         0x00
#define SMP_REPORT_MANUFACTURE_INFORMATION         0x01
#define SMP_READ_GPIO_REGISTER                     0x02
#define SMP_DISCOVER                               0x10
#define SMP_REPORT_PHY_ERROR_LOG                   0x11
#define SMP_REPORT_PHY_SATA                        0x12
#define SMP_REPORT_ROUTING_INFORMATION             0x13
#define SMP_WRITE_GPIO_REGISTER                    0x82
#define SMP_CONFIGURE_ROUTING_INFORMATION          0x90
#define SMP_PHY_CONTROL                            0x91
#define SMP_PHY_TEST_FUNCTION                      0x92
#define SMP_PMC_SPECIFIC                           0xC0


/* SMP function results */
#define SMP_FUNCTION_ACCEPTED                      0x00
#define UNKNOWN_SMP_FUNCTION                       0x01
#define SMP_FUNCTION_FAILED                        0x02
#define INVALID_REQUEST_FRAME_LENGTH               0x03
#define INVALID_EXPANDER_CHANGE_COUNT              0x04
#define SMP_FN_BUSY                                0x05
#define INCOMPLETE_DESCRIPTOR_LIST                 0x06
#define PHY_DOES_NOT_EXIST                         0x10
#define INDEX_DOES_NOT_EXIST                       0x11
#define PHY_DOES_NOT_SUPPORT_SATA                  0x12
#define UNKNOWN_PHY_OPERATION                      0x13
#define UNKNOWN_PHY_TEST_FUNCTION                  0x14
#define PHY_TEST_FUNCTION_IN_PROGRESS              0x15
#define PHY_VACANT                                 0x16
#define UNKNOWN_PHY_EVENT_SOURCE                   0x17
#define UNKNOWN_DESCRIPTOT_TYPE                    0x18
#define UNKNOWN_PHY_FILETER                        0x19
#define AFFILIATION_VIOLATION                      0x1A
#define SMP_ZONE_VIOLATION                         0x20
#define NO_MANAGEMENT_ACCESS_RIGHTS                0x21
#define UNKNOWN_ENABLE_DISABLE_ZONING_VALUE        0x22
#define ZONE_LOCK_VIOLATION                        0x23
#define NOT_ACTIVATED                              0x24
#define ZONE_GROUP_OUT_OF_RANGE                    0x25
#define NO_PHYSICAL_PRESENCE                       0x26
#define SAVING_NOT_SUPPORTED                       0x27
#define SOURCE_ZONE_GROUP_DOES_NOT_EXIST           0x28
#define DISABLED_PASSWORD_NOT_SUPPORTED            0x29

/* SMP PHY CONTROL OPERATION */
#define SMP_PHY_CONTROL_NOP                        0x00
#define SMP_PHY_CONTROL_LINK_RESET                 0x01
#define SMP_PHY_CONTROL_HARD_RESET                 0x02
#define SMP_PHY_CONTROL_DISABLE                    0x03
#define SMP_PHY_CONTROL_CLEAR_ERROR_LOG            0x05
#define SMP_PHY_CONTROL_CLEAR_AFFILIATION          0x06
#define SMP_PHY_CONTROL_XMIT_SATA_PS_SIGNAL        0x07


#define IT_NEXUS_TIMEOUT    0x7D0 /* 2000 ms; old value was 0xFFFF */

#define PORT_RECOVERY_TIMEOUT  ((IT_NEXUS_TIMEOUT/100) + 30)   /* 5000 ms; in 100ms; should be large than IT_NEXUS_TIMEOUT */

#define STP_IDLE_TIME           5 /* 5 us; the defaulf of the controller */

#define SET_ESGL_EXTEND(val) \
 ((val) = (val) | 0x80000000)

#define CLEAR_ESGL_EXTEND(val) \
 ((val) = (val) & 0x7FFFFFFF)

#define DEVINFO_GET_SAS_ADDRESSLO(devInfo) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(devInfo)->sasAddressLo)

#define DEVINFO_GET_SAS_ADDRESSHI(devInfo) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(devInfo)->sasAddressHi)

/* this macro is based on SAS spec, not sTSDK 0xC0 */
#define DEVINFO_GET_DEVICETTYPE(devInfo) \
  (((devInfo)->devType_S_Rate & 0xC0) >> 6)

#define DEVINFO_GET_LINKRATE(devInfo) \
  ((devInfo)->devType_S_Rate & 0x0F)

#define DEVINFO_GET_EXT_MCN(devInfo) \
  (((devInfo)->ext & 0x7800) >> 11)


#define DEVINFO_PUT_SMPTO(devInfo, smpto) \
  ((devInfo)->smpTimeout) = smpto

#define DEVINFO_PUT_ITNEXUSTO(devInfo, itnexusto) \
  ((devInfo)->it_NexusTimeout) = itnexusto

#define DEVINFO_PUT_FBS(devInfo, fbs) \
  ((devInfo)->firstBurstSize) = fbs

#define DEVINFO_PUT_FLAG(devInfo, tlr) \
  ((devInfo)->flag) = tlr

#define DEVINFO_PUT_DEV_S_RATE(devInfo, dev_s_rate) \
  ((devInfo)->devType_S_Rate) = dev_s_rate

#define DEVINFO_PUT_SAS_ADDRESSLO(devInfo, src32) \
  *(bit32 *)((devInfo)->sasAddressLo) = BIT32_TO_DMA_BEBIT32(src32)

#define DEVINFO_PUT_SAS_ADDRESSHI(devInfo, src32) \
  *(bit32 *)((devInfo)->sasAddressHi) = BIT32_TO_DMA_BEBIT32(src32)

#define DEVICE_SSP_BIT         0x8   /* SSP Initiator port */
#define DEVICE_STP_BIT         0x4   /* STP Initiator port */
#define DEVICE_SMP_BIT         0x2   /* SMP Initiator port */
#define DEVICE_SATA_BIT        0x1   /* SATA device, valid in the discovery response only */

#define DEVICE_IS_SSP_INITIATOR(DeviceData) \
  (((DeviceData)->initiator_ssp_stp_smp & DEVICE_SSP_BIT) == DEVICE_SSP_BIT)

#define DEVICE_IS_STP_INITIATOR(DeviceData) \
  (((DeviceData)->initiator_ssp_stp_smp & DEVICE_STP_BIT) == DEVICE_STP_BIT)

#define DEVICE_IS_SMP_INITIATOR(DeviceData) \
  (((DeviceData)->initiator_ssp_stp_smp & DEVICE_SMP_BIT) == DEVICE_SMP_BIT)

#define DEVICE_IS_SSP_TARGET(DeviceData) \
  (((DeviceData)->target_ssp_stp_smp & DEVICE_SSP_BIT) == DEVICE_SSP_BIT)

#define DEVICE_IS_STP_TARGET(DeviceData) \
  (((DeviceData)->target_ssp_stp_smp & DEVICE_STP_BIT) == DEVICE_STP_BIT)

#define DEVICE_IS_SMP_TARGET(DeviceData) \
  (((DeviceData)->target_ssp_stp_smp & DEVICE_SMP_BIT) == DEVICE_SMP_BIT)

#define DEVICE_IS_SATA_DEVICE(DeviceData) \
  (((DeviceData)->target_ssp_stp_smp & DEVICE_SATA_BIT) == DEVICE_SATA_BIT)




/* Negotiated Phyical Link Rate
#define Phy_ENABLED_UNKNOWN
*/
/* old SMP header definition */
typedef struct tdssSMPFrameHeader_s
{
    bit8   smpFrameType;      /* The first byte of SMP frame represents the SMP FRAME TYPE */
    bit8   smpFunction;       /* The second byte of the SMP frame represents the SMP FUNCTION */
    bit8   smpFunctionResult; /* The third byte of SMP frame represents FUNCTION RESULT of the SMP response. */
    bit8   smpReserved;       /* reserved */
} tdssSMPFrameHeader_t;

/****************************************************************
 *            report general request
 ****************************************************************/
#ifdef FOR_COMPLETENESS
typedef struct smpReqReportGeneral_s
{
  /* nothing. some compiler disallowed structure with no member */
} smpReqReportGeneral_t;
#endif

/****************************************************************
 *            report general response
 ****************************************************************/
#define REPORT_GENERAL_CONFIGURING_BIT     0x2
#define REPORT_GENERAL_CONFIGURABLE_BIT    0x1

typedef struct smpRespReportGeneral_s
{
  bit8   expanderChangeCount16[2];
  bit8   expanderRouteIndexes16[2];
  bit8   reserved1;
  bit8   numOfPhys;
  bit8   configuring_configurable;
    /* B7-2 : reserved */
    /* B1   : configuring */
    /* B0   : configurable */
  bit8   reserved4[17];
} smpRespReportGeneral_t;

#define REPORT_GENERAL_IS_CONFIGURING(pResp) \
  (((pResp)->configuring_configurable & REPORT_GENERAL_CONFIGURING_BIT) == \
      REPORT_GENERAL_CONFIGURING_BIT)

#define REPORT_GENERAL_IS_CONFIGURABLE(pResp) \
  (((pResp)->configuring_configurable & REPORT_GENERAL_CONFIGURABLE_BIT) == \
      REPORT_GENERAL_CONFIGURABLE_BIT)

#define REPORT_GENERAL_GET_ROUTEINDEXES(pResp) \
  DMA_BEBIT16_TO_BIT16(*(bit16 *)((pResp)->expanderRouteIndexes16))


/****************************************************************
 *            report manufacturer info response
 ****************************************************************/
typedef struct smpRespReportManufactureInfo_s
{
  bit8    reserved1[8];
  bit8    vendorIdentification[8];
  bit8    productIdentification[16];
  bit8    productRevisionLevel[4];
  bit8    vendorSpecific[20];
} smpRespReportManufactureInfo_t;

/****************************************************************
 *           discover request
 ****************************************************************/
typedef struct smpReqDiscover_s
{
  bit32   reserved1;
  bit8    reserved2;
  bit8    phyIdentifier;
  bit8    ignored;
  bit8    reserved3;
} smpReqDiscover_t;

/****************************************************************
 *           discover response
 ****************************************************************/
typedef struct smpRespDiscover_s
{
  bit8   reserved1[4];
  bit8   reserved2;
  bit8   phyIdentifier;
  bit8   reserved3[2];
  bit8   attachedDeviceType;
    /* B7   : reserved */
    /* B6-4 : attachedDeviceType */
    /* B3-0 : reserved */
  bit8   negotiatedPhyLinkRate;
    /* B7-4 : reserved */
    /* B3-0 : negotiatedPhyLinkRate */
  bit8   attached_Ssp_Stp_Smp_Sata_Initiator;
    /* B7-4 : reserved */
    /* B3   : attachedSspInitiator */
    /* B2   : attachedStpInitiator */
    /* B1   : attachedSmpInitiator */
    /* B0   : attachedSataHost */
  bit8   attached_SataPS_Ssp_Stp_Smp_Sata_Target;
    /* B7   : attachedSataPortSelector */
    /* B6-4 : reserved */
    /* B3   : attachedSspTarget */
    /* B2   : attachedStpTarget */
    /* B1   : attachedSmpTarget */
    /* B0   : attachedSatadevice */
  bit8   sasAddressHi[4];
  bit8   sasAddressLo[4];
  bit8   attachedSasAddressHi[4];
  bit8   attachedSasAddressLo[4];
  bit8   attachedPhyIdentifier;
  bit8   reserved9[7];
  bit8   programmedAndHardware_MinPhyLinkRate;
    /* B7-4 : programmedMinPhyLinkRate */
    /* B3-0 : hardwareMinPhyLinkRate */
  bit8   programmedAndHardware_MaxPhyLinkRate;
    /* B7-4 : programmedMaxPhyLinkRate */
    /* B3-0 : hardwareMaxPhyLinkRate */
  bit8   phyChangeCount;
  bit8   virtualPhy_partialPathwayTimeout;
    /* B7   : virtualPhy*/
    /* B6-4 : reserved */
    /* B3-0 : partialPathwayTimeout */
  bit8   routingAttribute;
    /* B7-4 : reserved */
    /* B3-0 : routingAttribute */
  bit8   reserved13[5];
  bit8   vendorSpecific[2];
} smpRespDiscover_t;

#define DISCRSP_SSP_BIT    0x08
#define DISCRSP_STP_BIT    0x04
#define DISCRSP_SMP_BIT    0x02
#define DISCRSP_SATA_BIT   0x01

#define DISCRSP_SATA_PS_BIT   0x80

#define DISCRSP_GET_ATTACHED_DEVTYPE(pResp) \
  (((pResp)->attachedDeviceType & 0x70) >> 4)
#define DISCRSP_GET_LINKRATE(pResp) \
  ((bit8)((pResp)->negotiatedPhyLinkRate & 0x0F))

#define DISCRSP_IS_SSP_INITIATOR(pResp) \
  (((pResp)->attached_Ssp_Stp_Smp_Sata_Initiator & DISCRSP_SSP_BIT) == DISCRSP_SSP_BIT)
#define DISCRSP_IS_STP_INITIATOR(pResp) \
  (((pResp)->attached_Ssp_Stp_Smp_Sata_Initiator & DISCRSP_STP_BIT) == DISCRSP_STP_BIT)
#define DISCRSP_IS_SMP_INITIATOR(pResp) \
  (((pResp)->attached_Ssp_Stp_Smp_Sata_Initiator & DISCRSP_SMP_BIT) == DISCRSP_SMP_BIT)
#define DISCRSP_IS_SATA_HOST(pResp) \
  (((pResp)->attached_Ssp_Stp_Smp_Sata_Initiator & DISCRSP_SATA_BIT) == DISCRSP_SATA_BIT)

#define DISCRSP_IS_SSP_TARGET(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & DISCRSP_SSP_BIT) == DISCRSP_SSP_BIT)
#define DISCRSP_IS_STP_TARGET(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & DISCRSP_STP_BIT) == DISCRSP_STP_BIT)
#define DISCRSP_IS_SMP_TARGET(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & DISCRSP_SMP_BIT) == DISCRSP_SMP_BIT)
#define DISCRSP_IS_SATA_DEVICE(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & DISCRSP_SATA_BIT) == DISCRSP_SATA_BIT)
#define DISCRSP_IS_SATA_PORTSELECTOR(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & DISCRSP_SATA_PS_BIT) == DISCRSP_SATA_PS_BIT)

#define DISCRSP_GET_SAS_ADDRESSHI(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->sasAddressHi)
#define DISCRSP_GET_SAS_ADDRESSLO(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->sasAddressLo)

#define DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->attachedSasAddressHi)
#define DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->attachedSasAddressLo)

#define DISCRSP_VIRTUALPHY_BIT 0x80
#define DISCRSP_IS_VIRTUALPHY(pResp) \
  (((pResp)->virtualPhy_partialPathwayTimeout & DISCRSP_VIRTUALPHY_BIT) == DISCRSP_VIRTUALPHY_BIT)

#define DISCRSP_GET_ROUTINGATTRIB(pResp) \
 ((bit8)((pResp)->routingAttribute & 0x0F))

/****************************************************************
 *            report route table request
 ****************************************************************/
typedef struct smpReqReportRouteTable_s
{
  bit8   reserved1[2];
  bit8   expanderRouteIndex16[20];
  bit8   reserved2;
  bit8   phyIdentifier;
  bit8   reserved3[2];
} smpReqReportRouteTable_t;

/****************************************************************
 *            report route response
 ****************************************************************/
typedef struct smpRespReportRouteTable_s
{
  bit8   reserved1[2];
  bit8   expanderRouteIndex16[2];
  bit8   reserved2;
  bit8   phyIdentifier;
  bit8   reserved3[2];
  bit8   disabled;
    /* B7   : expander route entry disabled */
    /* B6-0 : reserved */
  bit8   reserved5[3];
  bit8   routedSasAddressHi32[4];
  bit8   routedSasAddressLo32[4];
  bit8   reserved6[16];
} smpRespReportRouteTable_t;

/****************************************************************
 *            configure route information request
 ****************************************************************/
typedef struct smpReqConfigureRouteInformation_s
{
  bit8   reserved1[2];
  bit8   expanderRouteIndex[2];
  bit8   reserved2;
  bit8   phyIdentifier;
  bit8   reserved3[2];
  bit8   disabledBit_reserved4;
  bit8   reserved5[3];
  bit8   routedSasAddressHi[4];
  bit8   routedSasAddressLo[4];
  bit8   reserved6[16];
} smpReqConfigureRouteInformation_t;

/****************************************************************
 *            configure route response
 ****************************************************************/
#ifdef FOR_COMPLETENESS
typedef struct smpRespConfigureRouteInformation_s
{
  /* nothing. some compiler disallowed structure with no member */
} smpRespConfigureRouteInformation_t;
#endif

/****************************************************************
 *            report Phy Sata request
 ****************************************************************/
typedef struct smpReqReportPhySata_s
{
  bit8   reserved1[4];
  bit8   reserved2;
  bit8   phyIdentifier;
  bit8   reserved3[2];
} smpReqReportPhySata_t;

/****************************************************************
 *            report Phy Sata response
 ****************************************************************/
typedef struct smpRespReportPhySata_s
{
  bit8   reserved1[4];
  bit8   reserved2;
  bit8   phyIdentifier;
  bit8   reserved3;
  bit8   affiliations_sup_valid;
    /* b7-2 : reserved */
    /* b1   : Affiliations supported */
    /* b0   : Affiliation valid */
  bit8   reserved5[4];
  bit8   stpSasAddressHi[4];
  bit8   stpSasAddressLo[4];
  bit8   regDevToHostFis[20];
  bit8   reserved6[4];
  bit8   affiliatedStpInitiatorSasAddressHi[4];
  bit8   affiliatedStpInitiatorSasAddressLo[4];
} smpRespReportPhySata_t;


/****************************************************************
 *            Phy Control request
 ****************************************************************/
typedef struct smpReqPhyControl_s
{
  bit8   reserved1[4];
  bit8   reserved2;
  bit8   phyIdentifier;
  bit8   phyOperation;
  bit8   updatePartialPathwayTOValue;
    /* b7-1 : reserved */
    /* b0   : update partial pathway timeout value */
  bit8   reserved3[20];
  bit8   programmedMinPhysicalLinkRate;
    /* b7-4 : programmed Minimum Physical Link Rate*/
    /* b3-0 : reserved */
  bit8   programmedMaxPhysicalLinkRate;
    /* b7-4 : programmed Maximum Physical Link Rate*/
    /* b3-0 : reserved */
  bit8   reserved4[2];
  bit8   partialPathwayTOValue;
    /* b7-4 : reserved */
    /* b3-0 : partial Pathway TO Value */
  bit8   reserved5[3];
} smpReqPhyControl_t;

/****************************************************************
 *            Phy Control response
 ****************************************************************/
#ifdef FOR_COMPLETENESS
typedef struct smpRespPhyControl_s
{
  /* nothing. some compiler disallowed structure with no member */
} smpRespPhyControl_t;
#endif


/*****************************************************************************
** SCSI SENSE KEY VALUES
*****************************************************************************/

#define SCSI_SNSKEY_NO_SENSE           0x00
#define SCSI_SNSKEY_RECOVERED_ERROR    0x01
#define SCSI_SNSKEY_NOT_READY          0x02
#define SCSI_SNSKEY_MEDIUM_ERROR       0x03
#define SCSI_SNSKEY_HARDWARE_ERROR     0x04
#define SCSI_SNSKEY_ILLEGAL_REQUEST    0x05
#define SCSI_SNSKEY_UNIT_ATTENTION     0x06
#define SCSI_SNSKEY_DATA_PROTECT       0x07
#define SCSI_SNSKEY_ABORTED_COMMAND    0x0B
#define SCSI_SNSKEY_MISCOMPARE         0x0E

/*****************************************************************************
** SCSI Additional Sense Codes and Qualifiers combo two-bytes
*****************************************************************************/

#define SCSI_SNSCODE_NO_ADDITIONAL_INFO                         0x0000
#define SCSI_SNSCODE_LUN_CRC_ERROR_DETECTED                     0x0803
#define SCSI_SNSCODE_INVALID_COMMAND                            0x2000
#define SCSI_SNSCODE_LOGICAL_BLOCK_OUT                          0x2100
#define SCSI_SNSCODE_INVALID_FIELD_IN_CDB                       0x2400
#define SCSI_SNSCODE_LOGICAL_NOT_SUPPORTED                      0x2500
#define SCSI_SNSCODE_POWERON_RESET                              0x2900
#define SCSI_SNSCODE_EVERLAPPED_CMDS                            0x4e00
#define SCSI_SNSCODE_INTERNAL_TARGET_FAILURE                    0x4400
#define SCSI_SNSCODE_MEDIUM_NOT_PRESENT                         0x3a00
#define SCSI_SNSCODE_UNRECOVERED_READ_ERROR                     0x1100
#define SCSI_SNSCODE_RECORD_NOT_FOUND                           0x1401
#define SCSI_SNSCODE_NOT_READY_TO_READY_CHANGE                  0x2800
#define SCSI_SNSCODE_OPERATOR_MEDIUM_REMOVAL_REQUEST            0x5a01
#define SCSI_SNSCODE_INFORMATION_UNIT_CRC_ERROR                 0x4703
#define SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_FORMAT_IN_PROGRESS  0x0404
#define SCSI_SNSCODE_HARDWARE_IMPENDING_FAILURE                 0x5d10
#define SCSI_SNSCODE_LOW_POWER_CONDITION_ON                     0x5e00
#define SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_INIT_REQUIRED       0x0402
#define SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST               0x2600
#define SCSI_SNSCODE_ATA_DEVICE_FAILED_SET_FEATURES             0x4471
#define SCSI_SNSCODE_ATA_DEVICE_FEATURE_NOT_ENABLED             0x670B
#define SCSI_SNSCODE_LOGICAL_UNIT_FAILED_SELF_TEST              0x3E03
#define SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR                     0x2C00
#define SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE         0x2100
#define SCSI_SNSCODE_LOGICAL_UNIT_FAILURE                       0x3E01
#define SCSI_SNSCODE_MEDIA_LOAD_OR_EJECT_FAILED                 0x5300
#define SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_INITIALIZING_COMMAND_REQUIRED 0x0402
#define SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE          0x0400
#define SCSI_SNSCODE_LOGICAL_UNIT_DOES_NOT_RESPOND_TO_SELECTION           0x0500
#define SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN         0x4000
#define SCSI_SNSCODE_COMMANDS_CLEARED_BY_ANOTHER_INITIATOR      0x2F00
#define SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED       0x0C02
/*****************************************************************************
** SCSI Additional Sense Codes and Qualifiers saparate bytes
*****************************************************************************/

#define SCSI_ASC_NOTREADY_INIT_CMD_REQ    0x04
#define SCSI_ASCQ_NOTREADY_INIT_CMD_REQ   0x02


/*****************************************************************************
** Inquiry command fields and response sizes
*****************************************************************************/
#define SCSIOP_INQUIRY_CMDDT        0x02
#define SCSIOP_INQUIRY_EVPD         0x01
#define STANDARD_INQUIRY_SIZE       36
#define SATA_PAGE83_INQUIRY_WWN_SIZE       16      /* SAT, revision8, Table81, p78, 12 + 4 */
#define SATA_PAGE83_INQUIRY_NO_WWN_SIZE    76      /* SAT, revision8, Table81, p78, 72 + 4 */
#define SATA_PAGE89_INQUIRY_SIZE    572     /* SAT, revision8, Table87, p84 */
#define SATA_PAGE0_INQUIRY_SIZE     8       /* SPC-4, 7.6.9   Table331, p345 */
#define SATA_PAGE80_INQUIRY_SIZE    24     /* SAT, revision8, Table79, p77 */


/* not sure here */
/* define byte swap macro */
#define AGSA_FLIP_2_BYTES(_x) ((bit16)(((((bit16)(_x))&0x00FF)<<8)|  \
                                     ((((bit16)(_x))&0xFF00)>>8)))

#define AGSA_FLIP_4_BYTES(_x) ((bit32)(((((bit32)(_x))&0x000000FF)<<24)|  \
                                     ((((bit32)(_x))&0x0000FF00)<<8)|   \
                                     ((((bit32)(_x))&0x00FF0000)>>8)|   \
                                     ((((bit32)(_x))&0xFF000000)>>24)))


/*********************************************************************
** BUFFER CONVERTION MACROS
*********************************************************************/

/*********************************************************************
* CPU buffer access macro                                            *
*                                                                    *
*/

#define OSSA_OFFSET_OF(STRUCT_TYPE, FEILD)              \
        (bitptr)&(((STRUCT_TYPE *)0)->FEILD)


#if defined(SA_CPU_LITTLE_ENDIAN)

#define OSSA_WRITE_LE_16(AGROOT, DMA_ADDR, OFFSET, VALUE16)     \
        (*((bit16 *)(((bit8 *)DMA_ADDR)+(OFFSET)))) = (bit16)(VALUE16);

#define OSSA_WRITE_LE_32(AGROOT, DMA_ADDR, OFFSET, VALUE32)     \
        (*((bit32 *)(((bit8 *)DMA_ADDR)+(OFFSET)))) = (bit32)(VALUE32);

#define OSSA_READ_LE_16(AGROOT, ADDR16, DMA_ADDR, OFFSET)       \
        (*((bit16 *)ADDR16)) = (*((bit16 *)(((bit8 *)DMA_ADDR)+(OFFSET))))

#define OSSA_READ_LE_32(AGROOT, ADDR32, DMA_ADDR, OFFSET)       \
        (*((bit32 *)ADDR32)) = (*((bit32 *)(((bit8 *)DMA_ADDR)+(OFFSET))))

#define OSSA_WRITE_BE_16(AGROOT, DMA_ADDR, OFFSET, VALUE16)     \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))))   = (bit8)((((bit16)VALUE16)>>8)&0xFF);  \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))) = (bit8)(((bit16)VALUE16)&0xFF);

#define OSSA_WRITE_BE_32(AGROOT, DMA_ADDR, OFFSET, VALUE32)     \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))))   = (bit8)((((bit32)VALUE32)>>24)&0xFF); \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))) = (bit8)((((bit32)VALUE32)>>16)&0xFF); \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+2))) = (bit8)((((bit32)VALUE32)>>8)&0xFF);  \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+3))) = (bit8)(((bit32)VALUE32)&0xFF);

#define OSSA_READ_BE_16(AGROOT, ADDR16, DMA_ADDR, OFFSET)       \
        (*(bit8 *)(((bit8 *)ADDR16)+1)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))));   \
        (*(bit8 *)(((bit8 *)ADDR16)))   = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1)));

#define OSSA_READ_BE_32(AGROOT, ADDR32, DMA_ADDR, OFFSET)       \
        (*(bit8 *)(((bit8 *)ADDR32)+3)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))));   \
        (*(bit8 *)(((bit8 *)ADDR32)+2)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))); \
        (*(bit8 *)(((bit8 *)ADDR32)+1)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+2))); \
        (*(bit8 *)(((bit8 *)ADDR32)))   = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+3)));

#define OSSA_WRITE_BYTE_STRING(AGROOT, DEST_ADDR, SRC_ADDR, LEN)                        \
        si_memcpy(DEST_ADDR, SRC_ADDR, LEN);


#elif defined(SA_CPU_BIG_ENDIAN)

#define OSSA_WRITE_LE_16(AGROOT, DMA_ADDR, OFFSET, VALUE16)     \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))) = (bit8)((((bit16)VALUE16)>>8)&0xFF);   \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))))   = (bit8)(((bit16)VALUE16)&0xFF);

#define OSSA_WRITE_LE_32(AGROOT, DMA_ADDR, OFFSET, VALUE32)     \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+3))) = (bit8)((((bit32)VALUE32)>>24)&0xFF);  \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+2))) = (bit8)((((bit32)VALUE32)>>16)&0xFF);  \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))) = (bit8)((((bit32)VALUE32)>>8)&0xFF);   \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))))   = (bit8)(((bit32)VALUE32)&0xFF);

#define OSSA_READ_LE_16(AGROOT, ADDR16, DMA_ADDR, OFFSET)       \
        (*(bit8 *)(((bit8 *)ADDR16)+1)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))));   \
        (*(bit8 *)(((bit8 *)ADDR16)))   = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1)));

#define OSSA_READ_LE_32(AGROOT, ADDR32, DMA_ADDR, OFFSET)       \
        (*((bit8 *)(((bit8 *)ADDR32)+3))) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))));   \
        (*((bit8 *)(((bit8 *)ADDR32)+2))) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))); \
        (*((bit8 *)(((bit8 *)ADDR32)+1))) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+2))); \
        (*((bit8 *)(((bit8 *)ADDR32))))   = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+3)));

#define OSSA_WRITE_BE_16(AGROOT, DMA_ADDR, OFFSET, VALUE16)         \
        (*((bit16 *)(((bit8 *)DMA_ADDR)+(OFFSET)))) = (bit16)(VALUE16);

#define OSSA_WRITE_BE_32(AGROOT, DMA_ADDR, OFFSET, VALUE32)         \
        (*((bit32 *)(((bit8 *)DMA_ADDR)+(OFFSET)))) = (bit32)(VALUE32);

#define OSSA_READ_BE_16(AGROOT, ADDR16, DMA_ADDR, OFFSET)           \
        (*((bit16 *)ADDR16)) = (*((bit16 *)(((bit8 *)DMA_ADDR)+(OFFSET))));

#define OSSA_READ_BE_32(AGROOT, ADDR32, DMA_ADDR, OFFSET)           \
        (*((bit32 *)ADDR32)) = (*((bit32 *)(((bit8 *)DMA_ADDR)+(OFFSET))));

#define OSSA_WRITE_BYTE_STRING(AGROOT, DEST_ADDR, SRC_ADDR, LEN)    \
        si_memcpy(DEST_ADDR, SRC_ADDR, LEN);

#else

#error (Host CPU endianess undefined!!)

#endif


#if defined(SA_CPU_LITTLE_ENDIAN)

#ifndef LEBIT16_TO_BIT16
#define LEBIT16_TO_BIT16(_x)   (_x)
#endif

#ifndef BIT16_TO_LEBIT16
#define BIT16_TO_LEBIT16(_x)   (_x)
#endif

#ifndef BIT16_TO_BEBIT16
#define BIT16_TO_BEBIT16(_x)   AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef BEBIT16_TO_BIT16
#define BEBIT16_TO_BIT16(_x)   AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef LEBIT32_TO_BIT32
#define LEBIT32_TO_BIT32(_x)   (_x)
#endif

#ifndef BIT32_TO_LEBIT32
#define BIT32_TO_LEBIT32(_x)   (_x)
#endif


#ifndef BEBIT32_TO_BIT32
#define BEBIT32_TO_BIT32(_x)   AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef BIT32_TO_BEBIT32
#define BIT32_TO_BEBIT32(_x)   AGSA_FLIP_4_BYTES(_x)
#endif

#elif defined(SA_CPU_BIG_ENDIAN)

#ifndef LEBIT16_TO_BIT16
#define LEBIT16_TO_BIT16(_x)   AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef BIT16_TO_LEBIT16
#define BIT16_TO_LEBIT16(_x)   AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef BIT16_TO_BEBIT16
#define BIT16_TO_BEBIT16(_x)   (_x)
#endif

#ifndef BEBIT16_TO_BIT16
#define BEBIT16_TO_BIT16(_x)   (_x)
#endif

#ifndef LEBIT32_TO_BIT32
#define LEBIT32_TO_BIT32(_x)   AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef BIT32_TO_LEBIT32
#define BIT32_TO_LEBIT32(_x)   AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef BEBIT32_TO_BIT32
#define BEBIT32_TO_BIT32(_x)   (_x)
#endif

#ifndef BIT32_TO_BEBIT32
#define BIT32_TO_BEBIT32(_x)   (_x)
#endif

#else

#error No definition of SA_CPU_BIG_ENDIAN or SA_CPU_LITTLE_ENDIAN

#endif


#define TargetUnknown   0
#define TargetRead      1
#define TargetWrite     2


#define CDB_GRP_MASK    0xE0   /* 1110 0000 */
#define CDB_6BYTE       0x00
#define CDB_10BYTE1     0x20
#define CDB_10BYTE2     0x40
#define CDB_12BYTE      0xA0
#define CDB_16BYTE      0x80

/* ATA device type */
#define SATA_ATA_DEVICE                           0x01                       /**< ATA ATA device type */
#define SATA_ATAPI_DEVICE                         0x02                       /**< ATA ATAPI device type */
#define SATA_PM_DEVICE                            0x03                       /**< ATA PM device type */
#define SATA_SEMB_DEVICE                          0x04                       /**< ATA SEMB device type */
#define SATA_SEMB_WO_SEP_DEVICE                   0x05                       /**< ATA SEMB without SEP device type */
#define UNKNOWN_DEVICE                            0xFF

/****************************************************************
 *            SATA Specification related defines                *
 ****************************************************************/
#define SATA_MAX_QUEUED_COMMANDS                      32
#define SATA_MAX_PM_PORTS                             15


/* PMC IOCTL signature */
#define PMC_IOCTL_SIGNATURE   0x1234



/*
 *  FIS type
 */
#define PIO_SETUP_DEV_TO_HOST_FIS   0x5F
#define REG_DEV_TO_HOST_FIS         0x34
#define SET_DEV_BITS_FIS            0xA1

#define TD_ASSERT OS_ASSERT

#ifdef TD_DISCOVER
#define TDSA_DISCOVERY_OPTION_FULL_START 0
#define TDSA_DISCOVERY_OPTION_INCREMENTAL_START 1
#define TDSA_DISCOVERY_OPTION_ABORT 2

#define TDSA_DISCOVERY_TYPE_SAS 0
#define TDSA_DISCOVERY_TYPE_SATA 1


#define DISCOVERY_TIMER_VALUE (2 * 1000 * 1000)       /* 2 seconds */
#define DISCOVERY_RETRIES     3
#define CONFIGURE_ROUTE_TIMER_VALUE (1 * 1000 * 1000)       /* 1 seconds */
#define DEVICE_REGISTRATION_TIMER_VALUE (2 * 1000 * 1000)       /* 2 seconds */
#define SMP_RETRIES     5
#define SMP_BUSY_TIMER_VALUE (1 * 1000 * 1000)       /* 1 second */
#define SMP_BUSY_RETRIES     5
#define SATA_ID_DEVICE_DATA_TIMER_VALUE (3 * 1000 * 1000)       /* 3 second */
#define SATA_ID_DEVICE_DATA_RETRIES     3
#define BC_TIMER_VALUE (5 * 1000 * 1000 )      /* 5 second */
#define SMP_TIMER_VALUE (10 * 1000 * 1000)       /* 10 second */

#endif
#define STP_DEVICE_TYPE 0     /* SATA behind expander 00*/
#define SAS_DEVICE_TYPE 1     /* SSP or SMP 01 */
#define SATA_DEVICE_TYPE 2    /* direct SATA 10 */

#define ATAPI_DEVICE_FLAG 0x200000   /* ATAPI device flag*/

#define TD_INTERNAL_TM_RESET 0xFF

/* in terms of Kbytes*/
#define HOST_EVENT_LOG_SIZE  128
#define DEFAULT_EVENT_LOG_OPTION 3

/* Device state */
#define SAT_DEV_STATE_NORMAL                  0  /* Normal */
#define SAT_DEV_STATE_IN_RECOVERY             1  /* SAT in recovery mode */
#define SAT_DEV_STATE_FORMAT_IN_PROGRESS      2  /* Format unit in progress */
#define SAT_DEV_STATE_SMART_THRESHOLD         3  /* SMART Threshold Exceeded Condition*/
#define SAT_DEV_STATE_LOW_POWER               4  /* Low Power State*/

#define TD_GET_PHY_ID(input) (input & 0x0F)
#define TD_GET_PHY_NUMS(input) ((input & 0xF0) >> 4)
#define TD_GET_LINK_RATE(input) ((input & 0xFF00) >> 8)
#define TD_GET_PORT_STATE(input) ((input & 0xF0000) >> 16)
#define TD_GET_PHY_STATUS(input) ((input & 0xFF00) >> 8)
#define TD_GET_RESET_STATUS(input) ((input & 0xFF00) >> 8)

#define TD_MAX_NUM_NOTIFY_SPINUP 20

#define SPC_VPD_SIGNATURE     0xFEDCBA98

#define TD_GET_FRAME_TYPE(input)    (input & 0xFF)
#define TD_GET_TLR(input)           ((input & 0x300) >> 8)

/* PORT RESET TMO is in 100ms */
#define SAS_PORT_RESET_TMO          3 /* 300 ms */
#define SATA_PORT_RESET_TMO         80 /* 8000 ms = 8 sec */
#define SAS_12G_PORT_RESET_TMO      8 /* 800 ms */

/* task attribute based on sTSDK API */
#define TD_TASK_SIMPLE         0x0       /* Simple        */
#define TD_TASK_ORDERED        0x2       /* Ordered       */
#define TD_TASK_HEAD_OF_QUEUE  0x1       /* Head of Queue */
#define TD_TASK_ACA            0x4       /* ACA           */

/* compiler flag for direct smp */
#define DIRECT_SMP
//#undef DIRECT_SMP

#define CONFIGURE_FW_MAX_PORTS 0x20000000

#define NO_ACK  0xFFFF

#define OPEN_RETRY_RETRIES  10

#ifdef AGTIAPI_CTL
/* scsi command/page */
#define MODE_SELECT          0x15
#define PAGE_FORMAT          0x10
#define DR_MODE_PG_SZ        16
#define DR_MODE_PG_CODE      0x02
#define DR_MODE_PG_LENGTH    0x0e
#endif /* AGTIAPI_CTL */

enum td_locks_e
{
  /* for tdsaAllShared->FreeDeviceList, tdsaAllShared->MainDeviceList,
    oneDeviceData->MainLink, oneDeviceData->FreeLink */
  TD_DEVICE_LOCK,
  /* for tdsaAllShared->FreePortContextList, tdsaAllShared->MainPortContextList,
    onePortContext->MainLink, onePortContext->FreeLink */
  TD_PORT_LOCK,
  /* for onePortContext->discovery.discoveringExpanderList,
    onePortContext->discovery.UpdiscoveringExpanderList,
    tdsaAllShared->freeExpanderList */
  TD_DISC_LOCK,
  /* for onePortContext->discovery.DiscoverySMPTimer,
   oneDeviceData->SATAIDDeviceTimer, discovery->discoveryTimer,
   discovery->SMPBusyTimer, discovery->BCTimer,
   discovery->deviceRegistrationTimer, discovery->configureRouteTimer,
   tdsaAllShared->itdsaIni->timerlist, tdsaAllShared->timerlist */
  TD_TIMER_LOCK,
#ifdef INITIATOR_DRIVER
  /* for     tdsaAllShared->pEsglAllInfo->freelist
    tdsaAllShared->pEsglAllInfo->NumFreeEsglPages
    tdsaAllShared->pEsglPageInfo->tdlist */
  TD_ESGL_LOCK,
  /* for satIOContext->pSatDevData->satVerifyState,
    satIOContext->pSatDevData->satSectorDone,
    satIOContext->pSatDevData->satPendingNCQIO,
    satIOContext->pSatDevData->satPendingIO,
    satIOContext->pSatDevData->satPendingNONNCQIO,
    satIOContext->pSatDevData->satFreeIntIoLinkList,
    satIOContext->pSatDevData->satActiveIntIoLinkList,
    satIOContext->pSatDevData->freeSATAFDMATagBitmap,
    satIOContext->satIoContextLink,
    oneDeviceData->satDevData.satIoLinkList */
  TD_SATA_LOCK,
#ifdef TD_INT_COALESCE
  /* for tdsaIntCoalCxt->FreeLink, tdsaIntCoalCxt->MainLink,
    tdsaIntCoalCxtHead->FreeLink, tdsaIntCoalCxtHead->MainLink */
  TD_INTCOAL_LOCK,
#endif
#endif
#ifdef TARGET_DRIVER
  /* for tdsaAllShared->ttdsaTgt->ttdsaXchgData.xchgFreeList,
    tdsaAllShared->ttdsaTgt->ttdsaXchgData.xchgBusyList */
  TD_TGT_LOCK,
#endif
  TD_MAX_LOCKS
};

#define TD_GET_SAS_ADDRESSLO(sasAddressLo)                  \
    DMA_BEBIT32_TO_BIT32(*(bit32 *)sasAddressLo)

#define TD_GET_SAS_ADDRESSHI(sasAddressHi)                  \
    DMA_BEBIT32_TO_BIT32(*(bit32 *)sasAddressHi)

#define TD_XFER_RDY_PRIORTY_DEVICE_FLAG (1 << 22)


#ifdef FDS_DM
/* bit32 -> bit8 array[4] */
#define PORTINFO_PUT_SAS_LOCAL_ADDRESSLO(portInfo, src32) \
  *(bit32 *)((portInfo)->sasLocalAddressLo) = BIT32_TO_DMA_BEBIT32(src32)

#define PORTINFO_PUT_SAS_LOCAL_ADDRESSHI(portInfo, src32) \
  *(bit32 *)((portInfo)->sasLocalAddressHi) = BIT32_TO_DMA_BEBIT32(src32)
/* bit32 -> bit8 array[4] */
#define PORTINFO_PUT_SAS_REMOTE_ADDRESSLO(portInfo, src32) \
  *(bit32 *)((portInfo)->sasRemoteAddressLo) = BIT32_TO_DMA_BEBIT32(src32)
#define PORTINFO_PUT_SAS_REMOTE_ADDRESSHI(portInfo, src32) \
  *(bit32 *)((portInfo)->sasRemoteAddressHi) = BIT32_TO_DMA_BEBIT32(src32)
#endif /* FDS_DM */

#ifdef FDS_SM
/* this applies to ID data and all other SATA IOs */
#define SM_RETRIES 10
#endif

#define TI_TIROOT_TO_tdsaRoot(t_r)        (((tdsaRoot_t *)((tiRoot_t *)t_r)->tdData) )

#define TI_TIROOT_TO_tdsaAllShared(t_r1)  (tdsaContext_t *)&(t_r1->tdsaAllShared)

#define TI_TIROOT_TO_agroot(t_r2)  (agsaRoot_t *)&((t_r2)->agRootNonInt)


#define TI_TIROOT_TO_AGROOT(t_root) (TI_TIROOT_TO_agroot(TI_TIROOT_TO_tdsaAllShared(TI_TIROOT_TO_tdsaRoot(t_root)) ))

#define TI_VEN_DEV_SPC                            0x80010000
#define TI_VEN_DEV_SPCADAP                        0x80810000
#define TI_VEN_DEV_SPCv                           0x80080000
#define TI_VEN_DEV_SPCve                          0x80090000
#define TI_VEN_DEV_SPCvplus                       0x80180000
#define TI_VEN_DEV_SPCveplus                      0x80190000
#define TI_VEN_DEV_SPCADAPvplus                   0x80880000
#define TI_VEN_DEV_SPCADAPveplus                  0x80890000

#define TI_VEN_DEV_SPC12Gv                        0x80700000
#define TI_VEN_DEV_SPC12Gve                       0x80710000
#define TI_VEN_DEV_SPC12Gvplus                    0x80720000
#define TI_VEN_DEV_SPC12Gveplus                   0x80730000
#define TI_VEN_DEV_9015                           0x90150000
#define TI_VEN_DEV_SPC12ADP                       0x80740000 /* 8 ports KBP added*/
#define TI_VEN_DEV_SPC12ADPP                      0x80760000 /* 16 ports  */
#define TI_VEN_DEV_SPC12SATA                      0x80060000 /* SATA HBA */
#define TI_VEN_DEV_9060                           0x90600000

#define tIsSPC(agr)           (TI_VEN_DEV_SPC           == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC */
#define tIsSPCHIL(agr)        (TI_VEN_DEV_SPCADAP       == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC */
#define tIsSPCv(agr)          (TI_VEN_DEV_SPCv          == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPCv */
#define tIsSPCve(agr)         (TI_VEN_DEV_SPCve         == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPCve */
#define tIsSPCvplus(agr)      (TI_VEN_DEV_SPCvplus      == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPCv+ */
#define tIsSPCveplus(agr)     (TI_VEN_DEV_SPCveplus     == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPCve+ */
#define tIsSPCADAPvplus(agr)  (TI_VEN_DEV_SPCADAPvplus  == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPCv+ */
#define tIsSPCADAPveplus(agr) (TI_VEN_DEV_SPCADAPveplus == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPCve+ */

#define tIsSPC12Gv(agr)       (TI_VEN_DEV_SPC12Gv       == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC12Gv */
#define tIsSPC12Gve(agr)      (TI_VEN_DEV_SPC12Gve      == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC12Gve */
#define tIsSPC12Gvplus(agr)   (TI_VEN_DEV_SPC12Gvplus   == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC12Gv+ */
#define tIsSPC12Gveplus(agr)  (TI_VEN_DEV_SPC12Gveplus  == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC12Gve+ */
#define tIsSPC9015(agr)       (TI_VEN_DEV_9015          == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC12Gve+ */
#define tIsSPC9060(agr)       (TI_VEN_DEV_9060          == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC12Gve+ */
#define tIsSPC12ADP(agr)      (TI_VEN_DEV_SPC12ADP      == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0)
#define tIsSPC12ADPP(agr)     (TI_VEN_DEV_SPC12ADPP     == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0)
#define tIsSPC12SATA(agr)     (TI_VEN_DEV_SPC12SATA     == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0)

#define tiIS_SPC(agr) (( tIsSPC((agr))    == 1) ? 1 : \
                       ( tIsSPCHIL((agr)) == 1) ? 1 : 0 )

#define tiIS_HIL(agr) ((tIsSPCHIL ((agr))       == 1) ? 1 : \
                       (tIsSPCADAPvplus((agr))  == 1) ? 1 : \
                       (tIsSPCADAPveplus((agr)) == 1) ? 1 : 0 )

#define tiIS_SPC6V(agr) ((tIsSPCv((agr))          == 1) ? 1 : \
                         (tIsSPCve((agr))         == 1) ? 1 : \
                         (tIsSPCvplus((agr))      == 1) ? 1 : \
                         (tIsSPCveplus((agr))     == 1) ? 1 : \
                         (tIsSPCADAPvplus((agr))  == 1) ? 1 : \
                         (tIsSPCADAPveplus((agr)) == 1) ? 1 : 0 )

#define tIsSPCV12G(agr)   ((tIsSPC12Gv(agr) == 1)     ? 1 : \
                           (tIsSPC12Gve(agr) == 1)    ? 1 : \
                           (tIsSPC12Gvplus(agr)== 1)  ? 1 : \
                           (tIsSPC12Gveplus(agr)== 1) ? 1 : \
                           (tIsSPC9015(agr)== 1)      ? 1 : \
                           (tIsSPC12ADP(agr)== 1)     ? 1 : \
                           (tIsSPC12ADPP(agr)== 1)    ? 1 : \
                           (tIsSPC12SATA(agr)   == 1) ? 1 : \
                           (tIsSPC9060(agr)     == 1) ? 1 : 0)

#define tiIS_8PHY(agr) ((tIsSPCv((agr))     == 1) ? 1 : \
                        (tIsSPCve((agr))    == 1) ? 1 : \
                        (tIsSPC12Gv((agr))  == 1) ? 1 : \
                        (tIsSPC12Gve((agr)) == 1) ? 1 : \
                        (tIsSPC12ADP(agr)   == 1) ? 1 : 0 )

#define tiIS_16PHY(agr) ((tIsSPCvplus((agr))      == 1) ? 1 : \
                         (tIsSPCveplus((agr))     == 1) ? 1 : \
                         (tIsSPCADAPvplus((agr))  == 1) ? 1 : \
                         (tIsSPCADAPveplus((agr)) == 1) ? 1 : \
                         (tIsSPC12ADPP(agr)       == 1) ? 1 : \
                         (tIsSPC12SATA(agr)       == 1) ? 1 : 0 )

#define tiIS_SPC_ENC(agr)((tIsSPCve((agr))         == 1) ? 1 : \
                          (tIsSPCveplus((agr))     == 1) ? 1 : \
                          (tIsSPCADAPveplus((agr)) == 1) ? 1 : 0 )

#define tIsSPCV12or6G(agr)  ((tiIS_SPC6V(agr) == 1) ? 1 : \
                             (tIsSPCV12G(agr) == 1) ? 1 :  0)

#endif /* __TDDEFS_H__ */
