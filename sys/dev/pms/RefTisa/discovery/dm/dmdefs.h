/*******************************************************************************
**
* Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
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
**
* $FreeBSD$
*
********************************************************************************/
#ifndef __DMDEFS_H__
#define __DMDEFS_H__

#include <dev/pms/RefTisa/tisa/sassata/common/ossa.h>

#define DIRECT_SMP
//#undef DIRECT_SMP

/* the index for memory requirement, must be continious */
#define DM_ROOT_MEM_INDEX                          0                       /**< the index of dm root memory */
#define DM_PORT_MEM_INDEX                          1                       /**< the index of port context memory */
#define DM_DEVICE_MEM_INDEX                        2                       /**< the index of Device descriptors memory */
#define DM_EXPANDER_MEM_INDEX                      3                       /**< the index of Expander device descriptors memory */
#define DM_SMP_MEM_INDEX                           4                       /**< the index of SMP command descriptors memory */
#define DM_INDIRECT_SMP_MEM_INDEX                  5                       /**< the index of Indirect SMP command descriptors memory */



#define DM_MAX_NUM_PHYS                         16
#define DM_MAX_EXPANDER_PHYS                    256
#define DM_MAX_DEV                              2048
#define DM_MAX_EXPANDER_DEV                     32
#define DM_MAX_PORT_CONTEXT                     16
#define DM_MAX_SMP                              32
#define DM_MAX_INDIRECT_SMP                     DM_MAX_SMP

#define DM_USECS_PER_TICK                       1000000                   /**< defines the heart beat of the LL layer 10ms */

/*
*  FIS type 
*/
#define PIO_SETUP_DEV_TO_HOST_FIS   0x5F
#define REG_DEV_TO_HOST_FIS         0x34 
#define SET_DEV_BITS_FIS            0xA1

#define DEFAULT_KEY_BUFFER_SIZE     64
 
enum dm_locks_e
{
  DM_PORT_LOCK = 0,
  DM_DEVICE_LOCK,
  DM_EXPANDER_LOCK,
  DM_TIMER_LOCK,
  DM_SMP_LOCK,
  DM_MAX_LOCKS
};
/* default SMP timeout: 0xFFFF is the Maximum Allowed */
#define DEFAULT_SMP_TIMEOUT       0xFFFF

/* SMP direct payload size limit: IOMB direct payload size = 48 */
#define SMP_DIRECT_PAYLOAD_LIMIT 44

#define SMP_INDIRECT_PAYLOAD	512

/* SMP maximum payload size allowed by SAS spec withtout CRC 4 bytes */
#define SMP_MAXIMUM_PAYLOAD      1024

/*! \def MIN(a,b)
* \brief MIN macro
*
* use to find MIN of two values
*/
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

/*! \def MAX(a,b)
* \brief MAX macro
*
* use to find MAX of two values
*/
#ifndef MAX
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#endif

#ifndef agNULL
#define agNULL     ((void *)0)
#endif

/* for debugging print */
#if defined(DM_DEBUG)

/*
* for debugging purposes.  
*/
extern bit32 gDMDebugLevel;

#define DM_DBG0(format) tddmLogDebugString(gDMDebugLevel, 0, format)
#define DM_DBG1(format) tddmLogDebugString(gDMDebugLevel, 1, format)
#define DM_DBG2(format) tddmLogDebugString(gDMDebugLevel, 2, format)
#define DM_DBG3(format) tddmLogDebugString(gDMDebugLevel, 3, format)
#define DM_DBG4(format) tddmLogDebugString(gDMDebugLevel, 4, format)
#define DM_DBG5(format) tddmLogDebugString(gDMDebugLevel, 5, format)
#define DM_DBG6(format) tddmLogDebugString(gDMDebugLevel, 6, format)


#else

#define DM_DBG0(format)
#define DM_DBG1(format)
#define DM_DBG2(format)
#define DM_DBG3(format)
#define DM_DBG4(format)
#define DM_DBG5(format)
#define DM_DBG6(format)

#endif /* DM_DEBUG */

//#define DM_ASSERT OS_ASSERT
//#define tddmLogDebugString TIDEBUG_MSG

/* discovery related state */
#define DM_DSTATE_NOT_STARTED                 0 
#define DM_DSTATE_STARTED                     1
#define DM_DSTATE_COMPLETED                   2
#define DM_DSTATE_COMPLETED_WITH_FAILURE      3

/* SAS/SATA discovery status */
#define DISCOVERY_NOT_START                       0                       /**< status indicates discovery not started */
#define DISCOVERY_UP_STREAM                       1                       /**< status indicates discover upstream */
#define DISCOVERY_DOWN_STREAM                     2                       /**< status indicates discover downstream */
#define DISCOVERY_CONFIG_ROUTING                  3                       /**< status indicates discovery config routing table */
#define DISCOVERY_SAS_DONE                        4                       /**< status indicates discovery done */
#define DISCOVERY_REPORT_PHY_SATA                 5                       /**< status indicates discovery report phy sata */

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
#define SMP_DISCOVER_LIST                          0x20


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

#define DM_VEN_DEV_SPC                               0x80010000
#define DM_VEN_DEV_ADAPSPC                           0x80810000
#define DM_VEN_DEV_SPCv                              0x80080000
#define DM_VEN_DEV_SPCve                             0x80090000
#define DM_VEN_DEV_SPCvplus                          0x80180000
#define DM_VEN_DEV_SPCveplus                         0x80190000
#define DM_VEN_DEV_ADAPvplus                         0x80880000
#define DM_VEN_DEV_ADAPveplus                        0x80890000

#define DMIsSPC(agr)           (DM_VEN_DEV_SPC        == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC */
#define DMIsSPCADAP(agr)       (DM_VEN_DEV_SPC        == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC */
#define DMIsSPCv(agr)          (DM_VEN_DEV_SPCv       == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPCv */
#define DMIsSPCve(agr)         (DM_VEN_DEV_SPCve      == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPCve */
#define DMIsSPCvplus(agr)      (DM_VEN_DEV_SPCvplus   == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPCv+ */
#define DMIsSPCveplus(agr)     (DM_VEN_DEV_SPCveplus  == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPCve+ */
#define DMIsSPCADAPvplus(agr)  (DM_VEN_DEV_ADAPvplus  == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPCv+ */
#define DMIsSPCADAPveplus(agr) (DM_VEN_DEV_ADAPveplus == (ossaHwRegReadConfig32(agr,0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPCve+ */

/****************************************************************
 *            SAS 1.1 Spec
 ****************************************************************/
/* SMP header definition */
typedef struct dmSMPFrameHeader_s
{
    bit8   smpFrameType;      /* The first byte of SMP frame represents the SMP FRAME TYPE */ 
    bit8   smpFunction;       /* The second byte of the SMP frame represents the SMP FUNCTION */
    bit8   smpFunctionResult; /* The third byte of SMP frame represents FUNCTION RESULT of the SMP response. */
    bit8   smpReserved;       /* reserved */
} dmSMPFrameHeader_t;

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
#define REPORT_GENERAL_LONG_RESPONSE_BIT   0x80

typedef struct smpRespReportGeneral_s
{
  bit8   expanderChangeCount16[2];
  bit8   expanderRouteIndexes16[2];
  bit8   reserved1; /* byte 9; has LONG Response for SAS 2 at bit 8 */
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

#define REPORT_GENERAL_IS_LONG_RESPONSE(pResp) \
  (((pResp)->reserved1 & REPORT_GENERAL_LONG_RESPONSE_BIT) == \
      REPORT_GENERAL_LONG_RESPONSE_BIT)
            
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
  bit8   attachedDeviceType; /* byte 12 */
    /* B7   : reserved */
    /* B6-4 : attachedDeviceType */
    /* B3-0 : reserved */
  bit8   negotiatedPhyLinkRate; /* byte 11 */
    /* B7-4 : reserved */
    /* B3-0 : negotiatedPhyLinkRate */
  bit8   attached_Ssp_Stp_Smp_Sata_Initiator; /* byte 14 */
    /* B7-4 : reserved */
    /* B3   : attachedSspInitiator */
    /* B2   : attachedStpInitiator */
    /* B1   : attachedSmpInitiator */
    /* B0   : attachedSataHost */
  bit8   attached_SataPS_Ssp_Stp_Smp_Sata_Target; /* byte 15 */
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
  bit8   virtualPhy_partialPathwayTimeout; /* byte 43 */
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
  ((pResp)->negotiatedPhyLinkRate & 0x0F)

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

/* bit8 array[4] -> bit32 */
#define DISCRSP_GET_SAS_ADDRESSHI(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->sasAddressHi)
#define DISCRSP_GET_SAS_ADDRESSLO(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->sasAddressLo)

/* bit8 array[4] -> bit32 */
#define DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->attachedSasAddressHi)
#define DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->attachedSasAddressLo)

#define DISCRSP_VIRTUALPHY_BIT 0x80
#define DISCRSP_IS_VIRTUALPHY(pResp) \
  (((pResp)->virtualPhy_partialPathwayTimeout & DISCRSP_VIRTUALPHY_BIT) == DISCRSP_VIRTUALPHY_BIT)

#define DISCRSP_GET_ROUTINGATTRIB(pResp) \
  ((pResp)->routingAttribute & 0x0F)

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


/****************************************************************
 *            SAS 2 Rev 14c Spec
 ****************************************************************/
/* SMP header definition */
typedef struct tdssSMPFrameHeader2_s
{
    bit8   smpFrameType;      /* The first byte of SMP frame represents the SMP FRAME TYPE */ 
    bit8   smpFunction;       /* The second byte of the SMP frame represents the SMP FUNCTION */
    bit8   smpAllocLenFuncResult; /* The third byte of SMP frame represents ALLOCATED RESPONSE LENGTH of SMP request or FUNCTION RESULT of the SMP response. */
    bit8   smpReqResLen;       /* The third byte of SMP frame represents REQUEST LENGTH of SMP request or RESPONSE LENGTH of the SMP response. */
} tdssSMPFrameHeader2_t;

/****************************************************************
 *            report general request
 ****************************************************************/
#ifdef FOR_COMPLETENESS
typedef struct smpReqReportGeneral2_s 
{
  /* nothing. some compiler disallowed structure with no member */
} smpReqReportGeneral2_t; 
#endif

/****************************************************************
 *            report general response
 ****************************************************************/
#define REPORT_GENERAL_TABLE_TO_TABLE_SUPPORTED_BIT   0x80
#define REPORT_GENERAL_CONFIGURES_OTHERS_BIT          0x04

typedef struct smpRespReportGeneral2_s
{
  bit8   expanderChangeCount16[2]; /* byte 4-5 */
  bit8   expanderRouteIndexes16[2]; /* byte 6-7 */
  bit8   LongResponse; /* byte 8 */
  /* B7: LongResponse */
  /* B6-0: Reserved */ 
  bit8   numOfPhys; /* byte 9 */
  bit8   byte10; 
    /* B7   : TABLE TO TABLE SUPPORTED */
    /* B6   : ZONE CONFIGURING */
    /* B5   : SELF CONFIGURING */
    /* B4   : STP CONTINUE AWT */
    /* B3   : OPEN REJECT RETRY SUPPORTED */
    /* B2   : CONFIGURES OTHERS */
    /* B1   : CONFIGURING */
    /* B0   : EXTERNALLY CONFIGURABLE ROUTE TABLE  */
  bit8   reserved1; /* byte11 */
  bit8   EnclosureLogicalID[8];
  bit8   reserved2[8]; /* upto byte27; Spec 1.1 */
  bit8   reserved3[2];
  bit8   STPBusInactivityTimeLimit[2];
  bit8   STPMaxConnectTimeLimit[2]; /* byte33 */
  bit8   STPSMPI_TNexusLossTime[2]; /* byte35 */
  bit8   byte36;
    /* B7-6 : NUMBER OF ZONE GROUPS */
    /* B5   : RESERVED */
    /* B4   : ZONE LOCKED */
    /* B3   : PHYSICAL PRESENCE SUPPORTED */
    /* B2   : PHYSICAL PRESENCE ASSERTED */
    /* B1   : ZONING SUPPORTED */
    /* B0   : ZONING ENABLED */
  bit8   byte37;
    /* B7-5 : RESERVED */
    /* B4   : SAVING */
    /* B3   : SAVING ZONE MANAGER PASSWORD SUPPORTED */
    /* B2   : SAVING ZONE PHY INFORMATION SUPPORTED   */
    /* B1   : SAVING ZONE PERMISSION TABLE SUPPORTED */
    /* B0   : SAVING ZONING ENABLED SUPPORTED */
  bit8   MaxNumOfRoutedSASAddr[2]; /* byte39 */
  bit8   ActiveZoneManagerSASAddr[8]; /* byte47 */
  bit8   ZoneLockInactivityTimeLimit[2]; /* byte49 */  
  bit8   reserved4[2];
  bit8   reserved5; /* byte52 */
  bit8   FirstEnclosureConnectorElementIdx; /* byte53 */  
  bit8   NumOfEnclosureConnectorElementIdxs; /* byte54 */  
  bit8   reserved6; /* byte55 */
  bit8   ReducedFunctionality;
  /* B7: ReducedFunctionality */
  /* B6-0: Reserved */
  bit8   TimeToReducedFunctionality;
  bit8   InitialTimeToReducedFunctionality;
  bit8   MaxReducedFunctionalityTime; /* byte59 */
  bit8   LastSelfConfigurationStatusDescIdx[2];
  bit8   MaxNumOfStoredSelfConfigurationStatusDesc[2];
  bit8   LastPhyEventListDescIdx[2];
  bit8   MaxNumbOfStoredPhyEventListDesc[2]; /* byte67 */
  bit8   STPRejectToOpenLimit[2]; /* byte69 */
  bit8   reserved7[2]; /* byte71 */
    
} smpRespReportGeneral2_t;

#define SAS2_REPORT_GENERAL_GET_ROUTEINDEXES(pResp) \
  DMA_BEBIT16_TO_BIT16(*(bit16 *)((pResp)->expanderRouteIndexes16))

#define SAS2_REPORT_GENERAL_IS_CONFIGURING(pResp) \
  (((pResp)->byte10 & REPORT_GENERAL_CONFIGURING_BIT) == \
      REPORT_GENERAL_CONFIGURING_BIT)

#define SAS2_REPORT_GENERAL_IS_CONFIGURABLE(pResp) \
  (((pResp)->byte10 & REPORT_GENERAL_CONFIGURABLE_BIT) == \
      REPORT_GENERAL_CONFIGURABLE_BIT)

#define SAS2_REPORT_GENERAL_IS_TABLE_TO_TABLE_SUPPORTED(pResp) \
  (((pResp)->byte10 & REPORT_GENERAL_TABLE_TO_TABLE_SUPPORTED_BIT) == \
      REPORT_GENERAL_TABLE_TO_TABLE_SUPPORTED_BIT)

#define SAS2_REPORT_GENERAL_IS_CONFIGURES_OTHERS(pResp) \
  (((pResp)->byte10 & REPORT_GENERAL_CONFIGURES_OTHERS_BIT) == \
      REPORT_GENERAL_CONFIGURES_OTHERS_BIT)

/****************************************************************
 *            report manufacturer info request
 ****************************************************************/
#ifdef FOR_COMPLETENESS
typedef struct smpReqReportManufactureInfo2_s 
{
  /* nothing. some compiler disallowed structure with no member */
} smpReqReportManufactureInfo2_t; 
#endif

/****************************************************************
 *            report manufacturer info response
 ****************************************************************/
typedef struct smpRespReportManufactureInfo2_s 
{
  bit16   ExpanderChangeCount; /* byte 4-5 */
  bit8    reserved1[2]; /* byte 6-7 */
  bit8    SAS11Format; /* byte 8 */
    /* B7-1 : RESERVED */
    /* B0   : SAS-1.1 Format */
  bit8    reserved2[3]; /* byte 9-11 */
  bit8    vendorIdentification[8]; /* byte 12-19 */
  bit8    productIdentification[16]; /* byte 20-35 */
  bit8    productRevisionLevel[4]; /* byte 36-39 */
  bit8    componentVendorID[8]; /* byte 40-47 */
  bit8    componentID[2]; /* byte 48-49 */
  bit8    componentRevisionLevel; /* byte 50 */
  bit8    reserved3; /* byte 51 */
  bit8    vendorSpecific[8]; /* byte 52-59 */
} smpRespReportManufactureInfo2_t;

/****************************************************************
 *           discover request
 ****************************************************************/
typedef struct smpReqDiscover2_s 
{
  bit32   reserved1; /* byte 4 - 7 */
  bit8    IgnoreZoneGroup; /* byte 8 */
  bit8    phyIdentifier; /* byte 9 */
  bit16   reserved2;  /* byte 10 - 11*/
} smpReqDiscover2_t;

/****************************************************************
 *           discover response
 ****************************************************************/
typedef struct smpRespDiscover2_s
{
  bit16  ExpanderChangeCount; /* byte 4 - 5 */
  bit8   reserved1[3]; /* byte 6 - 8 */
  bit8   phyIdentifier; /* byte 9 */
  bit8   reserved2[2]; /* byte 10 - 11 */
  bit8   attachedDeviceTypeReason; /* byte 12 */
    /* B7   : RESERVED */
    /* B6-4 : Attached Device Type */
    /* B3-0 : Attached Reason */
  bit8   NegotiatedLogicalLinkRate; /* byte 13 */
    /* B7-4 : RESERVED */
    /* B3-0 : Negotiated Logical Link Rate */
  bit8   attached_Ssp_Stp_Smp_Sata_Initiator; /* byte 14 */
    /* B7-4 : reserved */
    /* B3   : attached SSP Initiator */
    /* B2   : attached STP Initiator */
    /* B1   : attached SMP Initiator */
    /* B0   : attached SATA Host */
  bit8   attached_SataPS_Ssp_Stp_Smp_Sata_Target; /* byte 15 */
    /* B7   : attached SATA Port Selector */
    /* B6-4 : reserved */
    /* B3   : attached SSP Target */
    /* B2   : attached STP Target */
    /* B1   : attached SMP Target */
    /* B0   : attached SATA device */
  bit8   sasAddressHi[4]; /* byte 16 - 19 */
  bit8   sasAddressLo[4]; /* byte 20 - 23 */
  bit8   attachedSasAddressHi[4]; /* byte 24 - 27 */
  bit8   attachedSasAddressLo[4]; /* byte 28 - 31 */
  bit8   attachedPhyIdentifier; /* byte 32 */
  bit8   byte33; /* byte 33 */
    /* B7-3   : reserved */
    /* B2   : attached Inside ZPSDS Persistent */
    /* B1   : attached Requested Inside ZPSDS */
    /* B0   : attached Break Reply Capable */
  bit8   reserved3[6]; /* byte 34 - 39; for indentify address frame related fields */
  bit8   programmedAndHardware_MinPhyLinkRate; /* byte 40 */
    /* B7-4 : programmedMinPhyLinkRate */
    /* B3-0 : hardwareMinPhyLinkRate */
  bit8   programmedAndHardware_MaxPhyLinkRate; /* byte 41 */
    /* B7-4 : programmedMaxPhyLinkRate */
    /* B3-0 : hardwareMaxPhyLinkRate */
  bit8   phyChangeCount;  /* byte 42 */
  bit8   virtualPhy_partialPathwayTimeout; /* byte 43 */
    /* B7   : virtualPhy*/
    /* B6-4 : reserved */
    /* B3-0 : partialPathwayTimeout */
  bit8   routingAttribute; /* byte 44 */
    /* B7-4 : reserved */
    /* B3-0 : routingAttribute */
  bit8   ConnectorType; /* byte 45 */
    /* B7   : reserved */
    /* B6-0 : Connector Type */
  bit8   ConnectorElementIndex; /* byte 46 */
  bit8   ConnectorPhysicalLink; /* byte 47 */
  bit8   reserved4[2]; /* byte 48 - 49 */
  bit8   vendorSpecific[2]; /* byte 50 - 51*/
  bit8   AttachedDeviceName[8]; /* byte 52 - 59*/
  bit8   byte60; /* byte 60 */
    /* B7   : reserved */
    /* B6   : Requested Inside ZPSDS Changed By Expander */
    /* B5   : Inside ZPSDS Persistent */
    /* B4   : Requested Inside ZPSDS */
    /* B3   : reserved */
    /* B2   : Zone Group Persistent */
    /* B1   : Inside ZPSDS */
    /* B0   : Zoning Enabled */
  bit8   reserved5[2]; /* byte 61 - 62; zoning-related fields */
  bit8   ZoneGroup; /* byte 63 */
  bit8   SelfCongfiguringStatus; /* byte 64 */
  bit8   SelfCongfigurationLevelsCompleted; /* byte 65 */
  bit8   reserved6[2]; /* byte 66 - 67; self configuration related fields */
  bit8   SelfConfigurationSASAddressHi[4]; /* byte 68 - 71 */
  bit8   SelfConfigurationSASAddressLo[4]; /* byte 72 - 75 */
  bit8   ProgrammedphyCapabilities[4]; /* byte 76 - 79 */
  bit8   CurrentphyCapabilities[4]; /* byte 80 - 83 */
  bit8   AttachedphyCapabilities[4]; /* byte 84 - 87 */
  bit8   reserved7[6]; /* byte 88 - 93 */
  bit8   ReasonNegotiatedPhysicalLinkRate; /* byte 94 */
  bit8   NegotiatedSSCHWMuxingSupported; /* byte 95 */
    /* B7-2 : reserved */
    /* B1   : Negotiated SSC */
    /* B0   : HW Muxing Supported */
  bit8   byte96; /* byte 96 */
    /* B7-6 : reserved */
    /* B5   : Default Inside ZPSDS Persistent */
    /* B4   : Default Requested Inside ZPSDS */
    /* B3   : reserved */
    /* B2   : Default Zone Group Persistent */
    /* B1   : reserved */
    /* B0   : Default Zoning Enabled */
  bit8   reserved8; /* byte 97 */
  bit8   reserved9; /* byte 98 */
  bit8   DefaultZoneGroup; /* byte 99 */
  bit8   byte100; /* byte 100 */
    /* B7-6 : reserved */
    /* B5   : Saved Inside ZPSDS Persistent */
    /* B4   : Saved Requested Inside ZPSDS */
    /* B3   : reserved */
    /* B2   : Saved Zone Group Persistent */
    /* B1   : reserved */
    /* B0   : Saved Zoning Enabled */
  bit8   reserved10; /* byte 101 */
  bit8   reserved11; /* byte 102 */
  bit8   SavedZoneGroup; /* byte 103 */
  bit8   byte104; /* byte 104 */
    /* B7-6 : reserved */
    /* B5   : Shadow Inside ZPSDS Persistent */
    /* B4   : Shadow Requested Inside ZPSDS */
    /* B3   : reserved */
    /* B2   : Shadow Zone Group Persistent */
    /* B1-0 : reserved */
  bit8   reserved12; /* byte 105 */
  bit8   reserved13; /* byte 106 */
  bit8   ShadowZoneGroup; /* byte 107 */
  bit8   DeviceSlotNumber; /* byte 108 */
  bit8   GroupNumber; /* byte 109 */
  bit16  PathToEnclosure; /* byte 110 - 111 */
   
} smpRespDiscover2_t;

#define SAS2_DISCRSP_SSP_BIT    0x08
#define SAS2_DISCRSP_STP_BIT    0x04
#define SAS2_DISCRSP_SMP_BIT    0x02
#define SAS2_DISCRSP_SATA_BIT   0x01

#define SAS2_DISCRSP_SATA_PS_BIT   0x80

#define SAS2_MUXING_SUPPORTED   0x01

#define SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pResp) \
  (((pResp)->attachedDeviceTypeReason & 0x70) >> 4)
#define SAS2_DISCRSP_GET_LINKRATE(pResp) \
  ((pResp)->ReasonNegotiatedPhysicalLinkRate & 0x0F)
#define SAS2_DISCRSP_GET_LOGICAL_LINKRATE(pResp) \
  ((pResp)->NegotiatedLogicalLinkRate & 0x0F)

#define SAS2_DISCRSP_IS_SSP_INITIATOR(pResp) \
  (((pResp)->attached_Ssp_Stp_Smp_Sata_Initiator & DISCRSP_SSP_BIT) == DISCRSP_SSP_BIT)
#define SAS2_DISCRSP_IS_STP_INITIATOR(pResp) \
  (((pResp)->attached_Ssp_Stp_Smp_Sata_Initiator & DISCRSP_STP_BIT) == DISCRSP_STP_BIT)
#define SAS2_DISCRSP_IS_SMP_INITIATOR(pResp) \
  (((pResp)->attached_Ssp_Stp_Smp_Sata_Initiator & DISCRSP_SMP_BIT) == DISCRSP_SMP_BIT)
#define SAS2_DISCRSP_IS_SATA_HOST(pResp) \
  (((pResp)->attached_Ssp_Stp_Smp_Sata_Initiator & DISCRSP_SATA_BIT) == DISCRSP_SATA_BIT)

#define SAS2_DISCRSP_IS_SSP_TARGET(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & DISCRSP_SSP_BIT) == DISCRSP_SSP_BIT)
#define SAS2_DISCRSP_IS_STP_TARGET(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & DISCRSP_STP_BIT) == DISCRSP_STP_BIT)
#define SAS2_DISCRSP_IS_SMP_TARGET(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & DISCRSP_SMP_BIT) == DISCRSP_SMP_BIT)
#define SAS2_DISCRSP_IS_SATA_DEVICE(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & DISCRSP_SATA_BIT) == DISCRSP_SATA_BIT)
#define SAS2_DISCRSP_IS_SATA_PORTSELECTOR(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & DISCRSP_SATA_PS_BIT) == DISCRSP_SATA_PS_BIT)

#define SAS2_DISCRSP_GET_SAS_ADDRESSHI(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->sasAddressHi)
#define SAS2_DISCRSP_GET_SAS_ADDRESSLO(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->sasAddressLo)

#define SAS2_DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->attachedSasAddressHi)
#define SAS2_DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->attachedSasAddressLo)

#define SAS2_DISCRSP_VIRTUALPHY_BIT 0x80
#define SAS2_DISCRSP_IS_VIRTUALPHY(pResp) \
  (((pResp)->virtualPhy_partialPathwayTimeout & DISCRSP_VIRTUALPHY_BIT) == DISCRSP_VIRTUALPHY_BIT)

#define SAS2_DISCRSP_GET_ROUTINGATTRIB(pResp) \
  ((pResp)->routingAttribute & 0x0F)

#define SAS2_DISCRSP_IS_MUXING_SUPPORTED(pResp) \
  (((pResp)->NegotiatedSSCHWMuxingSupported & SAS2_MUXING_SUPPORTED) == SAS2_MUXING_SUPPORTED)
  
/****************************************************************
 *           discover list request
 ****************************************************************/
typedef struct smpReqDiscoverList2_s 
{
  bit32   reserved1; /* byte 4 - 7 */
  bit8    StartingPhyID; /* byte 8 */
  bit8    MaxNumDiscoverDesc; /* byte 9 */
  bit8    byte10;  /* byte 10 */
    /* B7   : Ignore Zone Group */
    /* B6-4 : Reserved */
    /* B3-0 : Phy Filter */
  bit8    byte11;  /* byte 11 */
    /* B7-4 : Reserved */
    /* B6-4 : Descriptor Type */
  bit32   reserved2; /* byte 12 - 15 */
  bit8    VendorSpecific[12]; /* byte 16 - 27 */
} smpReqDiscoverList2_t;



/****************************************************************
 *           discover list response
 ****************************************************************/
typedef struct smpRespDiscoverList2_s 
{
  bit16   ExpanderChangeCount; /* byte 4 - 5 */
  bit16   reserved1; /* byte 6 - 7 */
  bit8    StartingPhyID; /* byte 8 */
  bit8    MaxNumDiscoverDesc; /* byte 9 */
  bit8    byte10;  /* byte 10 */
    /* B7-4 : Reserved */
    /* B3-0 : Phy Filter */
  bit8    byte11;  /* byte 11 */
    /* B7-4 : Reserved */
    /* B6-4 : Descriptor Type */
  bit8    DescLen;  /* byte 12 */
  bit8    reserved2; /* byte 13 */
  bit16   reserved3; /* byte 14 - 15 */
  bit8    byte16; /* byte 16 */
    /* B7   : Zoning Supported */
    /* B6   : Zoning Enabled */
    /* B5-4 : Reserved */
    /* B3   : Self Configuring */
    /* B2   : Zone Configuring */
    /* B1   : Configuring */
    /* B0   : Externally Configurable Route Table */
  bit8    reserved4; /* byte 17 */
  bit16   LastDescIdx; /* byte 18 - 19 */
  bit16   LastPhyDescIdx; /* byte 20 - 21 */
  bit8    reserved5[10]; /* byte 22 - 31 */
  bit8    VendorSpecific[16]; /* byte 32 - 47 */
} smpRespDiscoverList2_t;



/****************************************************************
 *            report route table request
 ****************************************************************/
typedef struct smpReqReportRouteTable2_s
{
  bit8   reserved1[2]; /* byte 4 - 5 */
  bit8   expanderRouteIndex16[20]; /* byte 6- 7 */
  bit8   reserved2; /* byte 8 */
  bit8   phyIdentifier; /* byte 9 */
  bit8   reserved3[2]; /* byte 10 -11  */
} smpReqReportRouteTable2_t;

/****************************************************************
 *            report route response
 ****************************************************************/
typedef struct smpRespReportRouteTable2_s 
{
  bit16  expanderChangeCount; /* byte 4 - 5 */
  bit16  expanderRouteIndex; /* byte 6 - 7 */
  bit8   reserved1; /* byte 8 */
  bit8   phyIdentifier; /* byte 9 */
  bit8   reserved2[2]; /* byte 10 - 11 */
  bit8   disabledBit_reserved3; /* byte 12 */
    /* B7   : Expander Route Entry Disabled */
    /* B6-0 : reserved */
  bit8   reserved4[3]; /* byte 13-15 */
  bit8   routedSasAddressHi[4]; /* byte 16-19 */
  bit8   routedSasAddressLo[4]; /* byte 20-23 */
  bit8   reserved5[16]; /* byte 24-39 */
} smpRespReportRouteTable2_t;

/****************************************************************
 *            configure route information request
 ****************************************************************/
typedef struct smpReqConfigureRouteInformation2_s
{
  bit16  expectedExpanderChangeCount; /* byte 4-5 */
  bit16  expanderRouteIndex; /* byte 6-7 */
  bit8   reserved1; /* byte 8 */
  bit8   phyIdentifier; /* byte 9 */
  bit8   reserved2[2]; /* byte 10-11 */
  bit8   disabledBit_reserved3; /* byte 12 */
    /* B7   : Expander Route Entry Disabled */
    /* B6-0 : reserved */
  bit8   reserved4[3]; /* byte 13-15 */
  bit8   routedSasAddressHi[4]; /* byte 16-19 */
  bit8   routedSasAddressLo[4]; /* byte 20-23 */
  bit8   reserved5[16]; /* byte 24-39 */
} smpReqConfigureRouteInformation2_t;

/****************************************************************
 *            configure route response
 ****************************************************************/
#ifdef FOR_COMPLETENESS
typedef struct smpRespConfigureRouteInformation2_s 
{
  /* nothing. some compiler disallowed structure with no member */
} smpRespConfigureRouteInformation2_t;
#endif

/****************************************************************
 *            report Phy Sata request
 ****************************************************************/
typedef struct smpReqReportPhySata2_s
{
  bit8   reserved1[5]; /* byte 4-8 */
  bit8   phyIdentifier; /* byte 9 */
  bit8   AffiliationContext; /* byte 10 */
  bit8   reserved2; /* byte 11 */
} smpReqReportPhySata2_t;

/****************************************************************
 *            report Phy Sata response
 ****************************************************************/
typedef struct smpRespReportPhySata2_s 
{
  bit16  expanderChangeCount; /* byte 4-5 */
  bit8   reserved1[3]; /* byte 6-8 */
  bit8   phyIdentifier; /* byte 9 */
  bit8   reserved2; /* byte 10 */
  bit8   byte11; /* byte 11 */
    /* b7-3 : reserved */
    /* b2   : STP I_T Nexus Loss Occurred */
    /* b1   : Affiliations supported */
    /* b0   : Affiliation valid */
  bit8   reserved3[4]; /* byte 12-15 */
  bit8   stpSasAddressHi[4]; /* byte 16-19 */
  bit8   stpSasAddressLo[4]; /* byte 20-23 */
  bit8   regDevToHostFis[20]; /* byte 24-43 */
  bit8   reserved4[4]; /* byte 44-47 */
  bit8   affiliatedStpInitiatorSasAddressHi[4]; /* byte 48-51 */
  bit8   affiliatedStpInitiatorSasAddressLo[4]; /* byte 52-55 */
  bit8   STPITNexusLossSASAddressHi[4]; /* byte 56-59 */
  bit8   STPITNexusLossSASAddressLo[4]; /* byte 60-63 */
  bit8   reserved5; /* byte 64 */
  bit8   AffiliationContext; /* byte 65 */
  bit8   CurrentAffiliationContexts; /* byte 66 */
  bit8   MaxAffiliationContexts; /* byte 67 */
  
} smpRespReportPhySata2_t;

/****************************************************************
 *            Phy Control request
 ****************************************************************/
typedef struct smpReqPhyControl2_s
{
  bit16  expectedExpanderChangeCount; /* byte 4-5 */
  bit8   reserved1[3]; /* byte 6-8 */
  bit8   phyIdentifier; /* byte 9 */
  bit8   phyOperation; /* byte 10 */
  bit8   updatePartialPathwayTOValue; /* byte 11 */
    /* b7-1 : reserved */
    /* b0   : update partial pathway timeout value */
  bit8   reserved2[12]; /* byte 12-23 */
  bit8   AttachedDeviceName[8]; /* byte 24-31 */
  bit8   programmedMinPhysicalLinkRate; /* byte 32 */
    /* b7-4 : programmed Minimum Physical Link Rate*/
    /* b3-0 : reserved */
  bit8   programmedMaxPhysicalLinkRate; /* byte 33 */
    /* b7-4 : programmed Maximum Physical Link Rate*/
    /* b3-0 : reserved */
  bit8   reserved3[2]; /* byte 34-35 */
  bit8   partialPathwayTOValue; /* byte 36 */
    /* b7-4 : reserved */
    /* b3-0 : partial Pathway TO Value */
  bit8   reserved4[3]; /* byte 37-39 */
  
} smpReqPhyControl2_t;

/****************************************************************
 *            Phy Control response
 ****************************************************************/
#ifdef FOR_COMPLETENESS
typedef struct smpRespPhyControl2_s 
{
  /* nothing. some compiler disallowed structure with no member */
} smpRespPhyControl2_t;
#endif

#define SMP_REQUEST        0x40
#define SMP_RESPONSE       0x41

/* bit8 array[4] -> bit32 */
#define DM_GET_SAS_ADDRESSLO(sasAddressLo)                  \
    DMA_BEBIT32_TO_BIT32(*(bit32 *)sasAddressLo)

#define DM_GET_SAS_ADDRESSHI(sasAddressHi)                  \
    DMA_BEBIT32_TO_BIT32(*(bit32 *)sasAddressHi)


#define DM_GET_LINK_RATE(input) (input & 0x0F)

#define DM_SAS_CONNECTION_RATE_1_5G                       0x08
#define DM_SAS_CONNECTION_RATE_3_0G                       0x09
#define DM_SAS_CONNECTION_RATE_6_0G                       0x0A
#define DM_SAS_CONNECTION_RATE_12_0G                      0x0B

#define DISCOVERY_CONFIGURING_TIMER_VALUE (3 * 1000 * 1000)       /* 3 seconds */
#define DISCOVERY_RETRIES                  3
#define CONFIGURE_ROUTE_TIMER_VALUE       (1 * 1000 * 1000)       /* 1 seconds */
#define DEVICE_REGISTRATION_TIMER_VALUE   (2 * 1000 * 1000)       /* 2 seconds */
#define SMP_RETRIES                        5
#define SMP_BUSY_TIMER_VALUE              (1 * 1000 * 1000)       /* 1 second */
#define SMP_BUSY_RETRIES                   5
#define SATA_ID_DEVICE_DATA_TIMER_VALUE   (3 * 1000 * 1000)       /* 3 second */
#define SATA_ID_DEVICE_DATA_RETRIES        3
#define BC_TIMER_VALUE                    (5 * 1000 * 1000)       /* 5 second */
#define SMP_TIMER_VALUE                   (30 * 1000 * 1000)       /* 30 second */

#define STP_DEVICE_TYPE 0     /* SATA behind expander 00*/
#define SAS_DEVICE_TYPE 1     /* SSP or SMP 01 */
#define SATA_DEVICE_TYPE 2    /* direct SATA 10 */
#define ATAPI_DEVICE_FLAG 0x200000   /* ATAPI device flag*/


/* ATA device type */
#define SATA_ATA_DEVICE                           0x01                       /**< ATA ATA device type */
#define SATA_ATAPI_DEVICE                         0x02                       /**< ATA ATAPI device type */
#define SATA_PM_DEVICE                            0x03                       /**< ATA PM device type */
#define SATA_SEMB_DEVICE                          0x04                       /**< ATA SEMB device type */
#define SATA_SEMB_WO_SEP_DEVICE                   0x05                       /**< ATA SEMB without SEP device type */
#define UNKNOWN_DEVICE                            0xFF


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

#define IT_NEXUS_TIMEOUT    0x7D0 /* 2000 ms; old value was 0xFFFF */

/* bit8 array[4] -> bit32 */
#define DEVINFO_GET_SAS_ADDRESSLO(devInfo) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(devInfo)->sasAddressLo)

#define DEVINFO_GET_SAS_ADDRESSHI(devInfo) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(devInfo)->sasAddressHi)

/* this macro is based on SAS spec, not sTSDK 0xC0 */
#define DEVINFO_GET_DEVICETTYPE(devInfo) \
  (((devInfo)->devType_S_Rate & 0xC0) >> 6)

#define DEVINFO_GET_LINKRATE(devInfo) \
  ((devInfo)->devType_S_Rate & 0x0F)

/**< target device type */
#define DM_DEFAULT_DEVICE 0
#define DM_SAS_DEVICE 1
#define DM_SATA_DEVICE 2

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

/* bit8 array[4] -> bit32 */
#define DEVINFO_GET_SAS_ADDRESSLO(devInfo) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(devInfo)->sasAddressLo)

#define DEVINFO_GET_SAS_ADDRESSHI(devInfo) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(devInfo)->sasAddressHi)

/* this macro is based on SAS spec, not sTSDK 0xC0 */
#define DEVINFO_GET_DEVICETTYPE(devInfo) \
  (((devInfo)->devType_S_Rate & 0xC0) >> 6)

#define DEVINFO_GET_LINKRATE(devInfo) \
  ((devInfo)->devType_S_Rate & 0x0F)


#define DEVINFO_GET_EXT_SMP(devInfo) \
  (((devInfo)->ext & 0x100) >> 8)

#define DEVINFO_GET_EXT_EXPANDER_TYPE(devInfo) \
  (((devInfo)->ext & 0x600) >> 9)

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

/* bit32 -> bit8 array[4] */
#define DEVINFO_PUT_SAS_ADDRESSLO(devInfo, src32) \
  *(bit32 *)((devInfo)->sasAddressLo) = BIT32_TO_DMA_BEBIT32(src32)

#define DEVINFO_PUT_SAS_ADDRESSHI(devInfo, src32) \
  *(bit32 *)((devInfo)->sasAddressHi) = BIT32_TO_DMA_BEBIT32(src32)

#define DEVINFO_PUT_INITIATOR_SSP_STP_SMP(devInfo, ini_ssp_stp_smp) \
  ((devInfo)->initiator_ssp_stp_smp) = ini_ssp_stp_smp

#define DEVINFO_PUT_TARGET_SSP_STP_SMP(devInfo, tgt_ssp_stp_smp) \
  ((devInfo)->target_ssp_stp_smp) = tgt_ssp_stp_smp

#define DEVINFO_PUT_EXT(devInfo, extension) \
  ((devInfo)->ext) = extension

#endif /* __DMDEFS_H__ */

