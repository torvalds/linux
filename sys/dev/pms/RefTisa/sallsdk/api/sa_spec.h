/******************************************************************************
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
******************************************************************************/
/*****************************************************************************/
/*! \file sa_spec.h
 *  \brief The file defines the constants defined by sas spec
 */

/*****************************************************************************/

#ifndef  __SA_SPEC_H__
#define __SA_SPEC_H__

/****************************************************************
 *            SAS Specification related defines                 *
 ****************************************************************/
#define SA_SAS_PROTOCOL_SMP                               0x00
#define SA_SAS_PROTOCOL_SSP                               0x01
#define SA_SAS_PROTOCOL_STP                               0x02

#define SA_OPENFRM_SIZE                                   (28)
#define SA_IDENTIFY_FRAME_SIZE                            (28)
//#define SAS_IDENTIFY_FRM_SIZE                             SA_IDENTIFY_FRAME_SIZE

#define SA_SAS_FRAME_TYPE_SSP_DATA                        0x01
#define SA_SAS_FRAME_TYPE_SSP_XRDY                        0x05
#define SA_SAS_FRAME_TYPE_SSP_CMD                         0x06
#define SA_SAS_FRAME_TYPE_SSP_RSP                         0x07
#define SA_SAS_FRAME_TYPE_SSP_TASK                        0x16
#define SA_SAS_FRAME_TYPE_SMP_REQ                         0x40
#define SA_SAS_FRAME_TYPE_SMP_RSP                         0x41

#define SA_SAS_CONNECTION_RATE_1_5G                       0x08
#define SA_SAS_CONNECTION_RATE_3_0G                       0x09
#define SA_SAS_CONNECTION_RATE_6_0G                       0x0A
#define SA_SAS_CONNECTION_RATE_12_0G                      0x0B

#define SA_SAS_DEV_TYPE_NO_DEVICE                         0x00
#define SA_SAS_DEV_TYPE_END_DEVICE                        0x01
#define SA_SAS_DEV_TYPE_EDGE_EXPANDER                     0x02
#define SA_SAS_DEV_TYPE_FANOUT_EXPANDER                   0x03

#define AGSA_DEV_TYPE_END_DEVICE                          (SA_SAS_DEV_TYPE_END_DEVICE << 4)
#define AGSA_DEV_TYPE_EDGE_EXPANDER                       (SA_SAS_DEV_TYPE_EDGE_EXPANDER << 4)
#define AGSA_DEV_TYPE_FAN_EXPANDER                        (SA_SAS_DEV_TYPE_FANOUT_EXPANDER << 4)

#define SA_SAS_SMP_REPORT_GENERAL                         0x00
#define SA_SAS_SMP_REPORT_MANUFACTURE_INFORMATION         0x01
#define SA_SAS_SMP_READ_GPIO_REGISTER                     0x02
#define SA_SAS_SMP_DISCOVER                               0x10
#define SA_SAS_SMP_REPORT_PHY_ERROR_LOG                   0x11
#define SA_SAS_SMP_REPORT_PHY_SATA                        0x12
#define SA_SAS_SMP_REPORT_ROUTING_INFORMATION             0x13
#define SA_SAS_SMP_WRITE_GPIO_REGISTER                    0x82
#define SA_SAS_SMP_CONFIGURE_ROUTING_INFORMATION          0x90
#define SA_SAS_SMP_PHY_CONTROL                            0x91
#define SA_SAS_SMP_PHY_TEST                               0x92

#define SA_SAS_SMP_FUNCTION_ACCEPTED                      0x00
#define SA_SAS_SMP_FUNCTION_UNKNOWN                       0x01
#define SA_SAS_SMP_FUNCTION_FAILED                        0x02
#define SA_SAS_SMP_INVALID_REQ_FRAME_LENGTH               0x03
#define SA_SAS_SMP_PHY_NOT_EXIST                          0x10

#define SA_SAS_ROUTING_DIRECT                             0x00
#define SA_SAS_ROUTING_SUBTRACTIVE                        0x01
#define SA_SAS_ROUTING_TABLE                              0x02

#define SA_SAS_PHYCTL_LINK_RESET                          0x01
#define SA_SAS_PHYCTL_HARD_RESET                          0x02
#define SA_SAS_PHYCTL_DISABLE                             0x03
#define SA_SAS_PHYCTL_CLEAR_ERROR_LOG                     0x05
#define SA_SAS_PHYCTL_CLEAR_AFFILIATION                   0x06
#define SA_SAS_PHYCTL_TRANSMIT_PS_SIGNAL                  0x07

#define SA_SSP_CMDIU_LEN_BYTES                            28
#define SA_SSP_TMIU_LEN_BYTES                             28


#define SASD_DEV_SATA_MASK                                0xF0
#define SASD_DEV_SAS_MASK                                 0x0F

#define SASD_DEV_SAS_END_DEVICE                           0x01 /* SAS end device type */
#define SASD_DEV_SAS_EDGE_EXPANDER                        0x02 /* SAS edge expander device type */
#define SASD_DEV_SAS_FAN_EXPANDER                         0x03 /* SAS fan out expander device type */

#define SASD_DEV_SATA_ATA_DEVICE                          0x10 /* SATA ATA device type */
#define SASD_DEV_SATA_ATAPI_DEVICE                        0x20 /* SATA ATAPI device type */
#define SASD_DEV_SATA_PM_DEVICE                           0x30 /* SATA PM device type */
#define SASD_DEV_SATA_SEMB_DEVICE                         0x40 /* SATA SEMB device type */
#define SASD_DEV_SATA_SEMB_WO_SEP_DEVICE                  0x50 /* SATA SEMB without SEP device type */

#define SASD_DEV_SATA_UNKNOWN_DEVICE                      0xFF /* SAS SATA unknown device type */


#define SASD_TASK_ATTR_SIMPLE                             0x0
#define SASD_TASK_ATTR_HEAD_OF_QUEUE                      0x1
#define SASD_TASK_ATTR_ORDERED                            0x2
#define SASD_TASK_ATTR_ACA                                0x4


/*****************************************************************************
** SAS TM Function definitions
*****************************************************************************/
#define SASD_SAS_ABORT_TASK                               0x01
#define SASD_SAS_ABORT_TASK_SET                           0x02
#define SASD_SAS_CLEAR_TASK_SET                           0x04
#define SASD_SAS_LOGICAL_UNIT_RESET                       0x08
#define SASD_SAS_CLEAR_ACA                                0x40
#define SASD_SAS_QUARY_TASK                               0x80

/****************************************************************
 *            SATA Specification related defines                *
 ****************************************************************/
#define SA_SATA_MAX_QUEUED_COMMANDS                       32
#define SA_SATA_MAX_PM_PORTS                              15

#define SA_SATA_FIS_TYPE_HOST_2_DEV                       0x27
#define SA_SATA_FIS_TYPE_DEV_2_HOST                       0x34
#define SA_SATA_FIS_TYPE_SET_DEVICE                       0xA1
#define SA_SATA_FIS_TYPE_DMA_ACTIVE                       0x39
#define SA_SATA_FIS_TYPE_FDMA_SETUP                       0x41
#define SA_SATA_FIS_TYPE_BIST                             0x58

#define SA_SATA_CMD_IDENTIFY_DEVICE                       0xEC
#define SA_SATA_CMD_EXEC_DEV_DIAG                         0x90

#define SA_SATA_CONTROL_SRST                              0x04

#define SA_SATA_H2DREG_LEN_BYTES                          20
#define SA_SATA_H2D_BIST_LEN_BYTES                        12
/****************************************************************
 *            SAS Specification related structures              *
 ****************************************************************/



/** \brief Structure for SATA BIST FIS
 *
 * The agsaFisBIST_t data structure describes a SATA FIS (Frame Information Structures)
 * for FIS type BIST (Built In Self Test) Activate Bidirectional.
 *
 * This data structure is one instance of the SATA request structure agsaSATAInitiatorRequest_t,
 * which is one instance of the generic request, issued to saSATAStart().
 */


#define SA_SATA_BIST_PATTERN_T_BIT  0x80
#define SA_SATA_BIST_PATTERN_A_BIT  0x40
#define SA_SATA_BIST_PATTERN_S_BIT  0x20
#define SA_SATA_BIST_PATTERN_L_BIT  0x10
#define SA_SATA_BIST_PATTERN_F_BIT  0x08
#define SA_SATA_BIST_PATTERN_P_BIT  0x04
#define SA_SATA_BIST_PATTERN_R_BIT  0x02
#define SA_SATA_BIST_PATTERN_V_BIT  0x01

/*
 * The first SATA DWORD types.
 */
typedef struct agsaFisBISTHeader_s
{
    bit8    fisType; /* fisType, set to 58h for BIST */
    bit8    pmPort;
    /* b7-b4  reserved */
    /* b3-b0  PM Port. device port address that the PM should deliver the FIS to */
    bit8   patternDefinition;
    /* b7 : T Far end transmit only mode */
    /* b6 : A ALIGN Bypass (Do not Transmit Align Primitives) (valid only in combination with T Bit) (optional behavior) */
    /* b5 : S Bypass Scrambling (valid only in combination with T Bit) (optional behavior) */
    /* b4 : L Far End Retimed Loopback. Transmitter shall insert additional ALIGNS) */
    /* b3 : F Far End Analog (AFE) Loopback (Optional) */
    /* b2 : P Primitive bit. (valid only in combination with the T Bit) (optional behavior) */
    /* b1 : R Reserved */
    /* b0 : V Vendor Specific Test Mode. Causes all other bits to be ignored */
    bit8   reserved5;       /* Reserved */
} agsaFisBISTHeader_t;


typedef struct agsaFisRegD2HHeader_s
{
    bit8    fisType; /* fisType, set to 34h for DeviceToHostReg */
    bit8    i_pmPort;
    /* b7     : reserved */
    /* b6     : I Interrupt bit */
    /* b5-b4  : reserved */
    /* b3-b0  : PM Port */
    bit8    status; /* Contains the contents to be placed in the Status(and Alternate status)
                       Register of the Shadow Command Block */
    bit8    error;  /* Contains the contents to be placed in the Error register of the Shadow Command Block */
} agsaFisRegD2HHeader_t;

typedef struct agsaFisSetDevBitsHeader_s
{
    bit8    fisType;    /* fisType, set to A1h for SetDeviceBit */
    bit8    n_i_pmPort;
    /* b7   : n Bit. Notification bit. If set device needs attention. */
    /* b6   : i Bit. Interrupt Bit */
    /* b5-b4: reserved2 */
    /* b3-b0: PM Port */
    bit8    statusHi_Lo;
    /* b7   : reserved */
    /* b6-b4: Status Hi. Contains the contents to be placed in bits 6, 5, and 4 of
       the Status register of the Shadow Command Block */
    /* b3   : Reserved */
    /* b2-b0: Status Lo  Contains the contents to be placed in bits 2,1, and 0 of the
       Status register of the Shadow Command Block */
    bit8    error;    /* Contains the contents to be placed in the Error register of
                         the Shadow Command Block */
} agsaFisSetDevBitsHeader_t;

typedef struct agsaFisRegH2DHeader_s
{
    bit8    fisType;      /* fisType, set to 27h for DeviceToHostReg */
    bit8    c_pmPort;
    /* b7   : C_bit This bit is set to one when the register transfer is
       due to an update of the Command register */
    /* b6-b4: reserved */
    /* b3-b0: PM Port */
    bit8    command;  /* Contains the contents of the Command register of
                         the Shadow Command Block */
    bit8    features; /* Contains the contents of the Features register of
                         the Shadow Command Block */
} agsaFisRegH2DHeader_t;

typedef struct agsaFisPioSetupHeader_s
{
    bit8    fisType;  /* set to 5F */
    bit8    i_d_pmPort;
    /* b7   : reserved */
    /* b6   : i bit. Interrupt bit */
    /* b5   : d bit. data transfer direction. set to 1 for device to host xfer */
    /* b4   : reserved */
    /* b3-b0: PM Port */
    bit8    status;
    bit8    error;
} agsaFisPioSetupHeader_t;

typedef union agsaFisHeader_s
{
    agsaFisBISTHeader_t       Bist;
    agsaFisRegD2HHeader_t     D2H;
    agsaFisRegH2DHeader_t     H2D;
    agsaFisSetDevBitsHeader_t SetDevBits;
    agsaFisPioSetupHeader_t   PioSetup;
} agsaFisHeader_t;


typedef struct agsaFisBISTData_s
{
    bit8    data[8]; /* BIST data */
} agsaFisBISTData_t;


typedef struct agsaFisBIST_s
{
    agsaFisBISTHeader_t   h;
    agsaFisBISTData_t     d;
} agsaFisBIST_t;

/** \brief Structure for SATA Device to Host Register FIS
 *
 * The agsaFisRegDeviceToHost_t data structure describes a SATA FIS (Frame Information
 * Structures) for FIS type Register Device to Host.
 *
 * This structure is used only as inbound data (device to host) to describe device to
 * host response.
 */

#define SA_SATA_RD2H_I_BIT  0x40

typedef struct agsaFisRegD2HData_s
{
    bit8    lbaLow;     /* Contains the contents to be placed in the LBA Low register
                           of the Shadow Command Block */
    bit8    lbaMid;     /* Contains the contents to be placed in the LBA Mid register
                           of the Shadow Command Block */

    bit8    lbaHigh;    /* Contains the contents to be placed in the LBA High register
                           of the Shadow Command Block */
    bit8    device;     /* Contains the contents to be placed in the Device register of the Shadow Command Block */

    bit8    lbaLowExp;  /* Contains the contents of the expanded address field
                           of the Shadow Command Block */
    bit8    lbaMidExp;  /* Contains the contents of the expanded address field
                           of the Shadow Command Block */
    bit8    lbaHighExp; /* Contains the contents of the expanded address field
                           of the Shadow Command Block */
    bit8    reserved4;  /** reserved */

    bit8    sectorCount; /* Contains the contents to be placed in the Sector
                            Count register of the Shadow Command Block */
    bit8    sectorCountExp;  /* Contains the contents of the expanded address
                                field of the Shadow Command Block */
    bit8    reserved6;  /* Reserved */
    bit8    reserved5;  /* Reserved */
    bit32   reserved7; /* Reserved */
} agsaFisRegD2HData_t;


typedef struct agsaFisRegDeviceToHost_s
{
    agsaFisRegD2HHeader_t     h;
    agsaFisRegD2HData_t       d;
} agsaFisRegDeviceToHost_t;



/** \brief Structure for SATA Host to Device Register FIS
 *
 * The agsaFisRegHostToDevice_t data structure describes a SATA FIS
 * (Frame Information Structures) for FIS type Register Host to Device.

 * This data structure is one instance of the SATA request structure
 * agsaSATAInitiatorRequest_t, which is one instance of the generic request,
 * issued to saSATAStart().
 */
typedef struct agsaFisRegH2DData_s
{
    bit8    lbaLow;       /* Contains the contents of the LBA Low register of the Shadow Command Block */
    bit8    lbaMid;       /* Contains the contents of the LBA Mid register of the Shadow Command Block */
    bit8    lbaHigh;      /* Contains the contents of the LBA High register of the Shadow Command Block */
    bit8    device;       /* Contains the contents of the Device register of the Shadow Command Block */

    bit8    lbaLowExp;    /* Contains the contents of the expanded address field of the
                             Shadow Command Block */
    bit8    lbaMidExp;    /* Contains the contents of the expanded address field of the
                             Shadow Command Block */
    bit8    lbaHighExp;   /* Contains the contents of the expanded address field of the
                             Shadow Command Block */
    bit8    featuresExp;  /* Contains the contents of the expanded address field of the
                             Shadow Command Block */

    bit8    sectorCount;    /* Contains the contents of the Sector Count register of the
                               Shadow Command Block */
    bit8    sectorCountExp; /* Contains the contents of the expanded address field of
                               the Shadow Command Block */
    bit8    reserved4;    /* Reserved */
    bit8    control;      /* Contains the contents of the Device Control register of the
                             Shadow Command Block */
    bit32   reserved5;      /* Reserved */
} agsaFisRegH2DData_t;

typedef struct agsaFisRegHostToDevice_s
{
    agsaFisRegH2DHeader_t   h;
    agsaFisRegH2DData_t     d;
} agsaFisRegHostToDevice_t;


/** \brief Structure for SATA SetDeviceBit FIS
 *
 * The agsaFisSetDevBits_t data structure describes a SATA FIS (Frame Information Structures)
 * for FIS type Set Device Bits - Device to Host.
 *
 * This structure is used only as inbound data (device to host) to describe device to host
 * response.
 */
typedef struct agsaFisSetDevBitsData_s
{
    bit32   reserved6; /* Reserved */
} agsaFisSetDevBitsData_t;


typedef struct agsaFisSetDevBits_s
{
    agsaFisSetDevBitsHeader_t   h;
    agsaFisSetDevBitsData_t     d;
} agsaFisSetDevBits_t;


/** \brief union data structure specifies a FIS from host software
 *
 * union data structure specifies a FIS from host software
 */
typedef union agsaSATAHostFis_u
{
    agsaFisRegHostToDevice_t    fisRegHostToDev; /* Structure containing the FIS request
                                                    for Register - Host to Device */
    agsaFisBIST_t               fisBIST; /* Structure containing the FIS request for BIST */
} agsaSATAHostFis_t;

/** \brief
 *
 * This structure is used
 *
 */
typedef struct agsaFisPioSetupData_s
{
    bit8    lbaLow;       /* Contains the contents of the LBA Low register of the Shadow Command Block */
    bit8    lbaMid;       /* Contains the contents of the LBA Mid register of the Shadow Command Block */
    bit8    lbaHigh;      /* Contains the contents of the LBA High register of the Shadow Command Block */
    bit8    device;       /* Contains the contents of the Device register of the Shadow Command Block */

    bit8    lbaLowExp;    /* Contains the contents of the expanded address field of the
                             Shadow Command Block */
    bit8    lbaMidExp;    /* Contains the contents of the expanded address field of the
                             Shadow Command Block */
    bit8    lbaHighExp;   /* Contains the contents of the expanded address field of the
                             Shadow Command Block */
    bit8    reserved1;    /* reserved */

    bit8    sectorCount;    /* Contains the contents of the Sector Count register of the
                               Shadow Command Block */
    bit8    sectorCountExp; /* Contains the contents of the expanded address field of
                               the Shadow Command Block */
    bit8    reserved2;    /* Reserved */
    bit8    e_status;     /* Contains the new value of Status Reg of the Command block
                             at the conclusion of the subsequent Data FIS */
    bit8    reserved4[2];    /* Reserved */
    bit8    transferCount[2]; /* the number of bytes to be xfered in the subsequent Data FiS */
} agsaFisPioSetupData_t;


typedef struct agsaFisPioSetup_s
{
    agsaFisPioSetupHeader_t h;
    agsaFisPioSetupData_t   d;
} agsaFisPioSetup_t;



/** \brief describe SAS IDENTIFY address frame
 *
 * describe SAS IDENTIFY address frame, the CRC field is not included in the structure
 *
 */
typedef struct agsaSASIdentify_s
{
    bit8  deviceType_addressFrameType;
    /* b7   : reserved */
    /* b6-4 : device type */
    /* b3-0 : address frame type */
    bit8  reason;  /* reserved */
    /* b7-4 : reserved */
    /* b3-0 : reason SAS2 */
    bit8  initiator_ssp_stp_smp;
    /* b8-4 : reserved */
    /* b3   : SSP initiator port */
    /* b2   : STP initiator port */
    /* b1   : SMP initiator port */
    /* b0   : reserved */
    bit8  target_ssp_stp_smp;
    /* b8-4 : reserved */
    /* b3   : SSP target port */
    /* b2   : STP target port */
    /* b1   : SMP target port */
    /* b0   : reserved */
    bit8  deviceName[8];            /* reserved */

    bit8  sasAddressHi[4];          /* BE SAS address Lo */
    bit8  sasAddressLo[4];          /* BE SAS address Hi */

    bit8  phyIdentifier;            /* phy identifier of the phy transmitting the IDENTIFY address frame */
    bit8  zpsds_breakReplyCap;
    /* b7-3 : reserved */
    /* b2   : Inside ZPSDS Persistent */
    /* b1   : Requested Inside ZPSDS */
    /* b0   : Break Reply Capable */
    bit8  reserved3[6];             /* reserved */
} agsaSASIdentify_t;

#define SA_IDFRM_GET_SAS_ADDRESSLO(identFrame)                  \
    DMA_BEBIT32_TO_BIT32(*(bit32 *)(identFrame)->sasAddressLo)

#define SA_IDFRM_GET_SAS_ADDRESSHI(identFrame)                  \
    DMA_BEBIT32_TO_BIT32(*(bit32 *)(identFrame)->sasAddressHi)

#define SA_IDFRM_GET_DEVICETTYPE(identFrame)                    \
    (((identFrame)->deviceType_addressFrameType & 0x70) >> 4)

#define SA_IDFRM_PUT_SAS_ADDRESSLO(identFrame, src32)                   \
    ((*(bit32 *)((identFrame)->sasAddressLo)) = BIT32_TO_DMA_BEBIT32(src32))

#define SA_IDFRM_PUT_SAS_ADDRESSHI(identFrame, src32)                   \
    ((*(bit32 *)((identFrame)->sasAddressHi)) = BIT32_TO_DMA_BEBIT32(src32))

#define SA_IDFRM_SSP_BIT         0x8   /* SSP Initiator port */
#define SA_IDFRM_STP_BIT         0x4   /* STP Initiator port */
#define SA_IDFRM_SMP_BIT         0x2   /* SMP Initiator port */
#define SA_IDFRM_SATA_BIT        0x1   /* SATA device, valid in the discovery response only */


#define SA_IDFRM_IS_SSP_INITIATOR(identFrame)                           \
    (((identFrame)->initiator_ssp_stp_smp & SA_IDFRM_SSP_BIT) == SA_IDFRM_SSP_BIT)

#define SA_IDFRM_IS_STP_INITIATOR(identFrame)                           \
    (((identFrame)->initiator_ssp_stp_smp & SA_IDFRM_STP_BIT) == SA_IDFRM_STP_BIT)

#define SA_IDFRM_IS_SMP_INITIATOR(identFrame)                           \
    (((identFrame)->initiator_ssp_stp_smp & SA_IDFRM_SMP_BIT) == SA_IDFRM_SMP_BIT)

#define SA_IDFRM_IS_SSP_TARGET(identFrame)                              \
    (((identFrame)->target_ssp_stp_smp & SA_IDFRM_SSP_BIT) == SA_IDFRM_SSP_BIT)

#define SA_IDFRM_IS_STP_TARGET(identFrame)                              \
    (((identFrame)->target_ssp_stp_smp & SA_IDFRM_STP_BIT) == SA_IDFRM_STP_BIT)

#define SA_IDFRM_IS_SMP_TARGET(identFrame)                              \
    (((identFrame)->target_ssp_stp_smp & SA_IDFRM_SMP_BIT) == SA_IDFRM_SMP_BIT)

#define SA_IDFRM_IS_SATA_DEVICE(identFrame)                             \
    (((identFrame)->target_ssp_stp_smp & SA_IDFRM_SATA_BIT) == SA_IDFRM_SATA_BIT)

/** \brief data structure provides the identify data of the SATA device
 *
 * data structure provides the identify data of the SATA device
 *
 */
typedef struct agsaSATAIdentifyData_s
{
  bit16   rm_ataDevice;
    /* b15-b9 :  */
    /* b8     :  ataDevice */
    /* b7-b1  : */
    /* b0     :  removableMedia */
  bit16   word1_9[9];                    /**< word 1 to 9 of identify device information */
  bit8    serialNumber[20];              /**< word 10 to 19 of identify device information, 20 ASCII chars */
  bit16   word20_22[3];                  /**< word 20 to 22 of identify device information */
  bit8    firmwareVersion[8];            /**< word 23 to 26 of identify device information, 4 ASCII chars */
  bit8    modelNumber[40];               /**< word 27 to 46 of identify device information, 40 ASCII chars */
  bit16   word47_48[2];                  /**< word 47 to 48 of identify device information, 40 ASCII chars */
  bit16   dma_lba_iod_ios_stimer;
    /* b15-b14:word49_bit14_15 */
    /* b13    : standbyTimerSupported */
    /* b12    : word49_bit12 */
    /* b11    : IORDYSupported */
    /* b10     : IORDYDisabled */
    /* b9     : lbaSupported */
    /* b8     : dmaSupported */
    /* b7-b0  : retired */
  bit16   word50_52[3];                  /**< word 50 to 52 of identify device information, 40 ASCII chars */
  bit16   valid_w88_w70;
    /* b15-3  : word53_bit3_15 */
    /* b2     : validWord88  */
    /* b1     : validWord70_64  */
    /* b0     : word53_bit0  */
  bit16   word54_59[6];                  /**< word54-59 of identify device information  */
  bit16   numOfUserAddressableSectorsLo; /**< word60 of identify device information  */
  bit16   numOfUserAddressableSectorsHi; /**< word61 of identify device information  */
  bit16   word62_74[13];                 /**< word62-74 of identify device information  */
  bit16   queueDepth;
    /* b15-5  : word75_bit5_15 */
    /* b4-0   : queueDepth */
  bit16   sataCapabilities;
    /* b15-b11: word76_bit11_15  */
    /* b10    : phyEventCountersSupport */
    /* b9     : hostInitPowerMangment */
    /* b8     : nativeCommandQueuing */
    /* b7-b3  : word76_bit4_7 */
    /* b2     : sataGen2Supported (3.0 Gbps) */
    /* b1     : sataGen1Supported (1.5 Gbps) */
    /* b0      :word76_bit0 */
  bit16   word77;                        /**< word77 of identify device information */
    /* b15-b6 : word77 bit6_15, Reserved */
    /* b5     : DMA Setup Auto-Activate support */
    /* b4     : NCQ streaming support */
    /* b3-b1  : coded value indicating current negotiated SATA signal speed */
    /* b0     : shall be zero */
  bit16   sataFeaturesSupported;
    /* b15-b7 : word78_bit7_15 */
    /* b6     : softSettingPreserveSupported */
    /* b5     : word78_bit5 */
    /* b4     : inOrderDataDeliverySupported */
    /* b3     : devInitPowerManagementSupported */
    /* b2     : autoActiveDMASupported */
    /* b1     : nonZeroBufOffsetSupported */
    /* b0     : word78_bit0  */
  bit16   sataFeaturesEnabled;
    /* b15-7  : word79_bit7_15  */
    /* b6     : softSettingPreserveEnabled */
    /* b5     : word79_bit5  */
    /* b4     : inOrderDataDeliveryEnabled */
    /* b3     : devInitPowerManagementEnabled */
    /* b2     : autoActiveDMAEnabled */
    /* b1     : nonZeroBufOffsetEnabled */
    /* b0     : word79_bit0 */
  bit16   majorVersionNumber;
    /* b15    : word80_bit15 */
    /* b14    : supportATA_ATAPI14 */
    /* b13    : supportATA_ATAPI13 */
    /* b12    : supportATA_ATAPI12 */
    /* b11    : supportATA_ATAPI11 */
    /* b10    : supportATA_ATAPI10 */
    /* b9     : supportATA_ATAPI9  */
    /* b8     : supportATA_ATAPI8  */
    /* b7     : supportATA_ATAPI7  */
    /* b6     : supportATA_ATAPI6  */
    /* b5     : supportATA_ATAPI5  */
    /* b4     : supportATA_ATAPI4 */
    /* b3     : supportATA3 */
    /* b2-0   : word80_bit0_2 */
  bit16   minorVersionNumber;            /**< word81 of identify device information */
  bit16   commandSetSupported;
    /* b15    : word82_bit15 */
    /* b14    : NOPSupported */
    /* b13    : READ_BUFFERSupported */
    /* b12    : WRITE_BUFFERSupported */
    /* b11    : word82_bit11 */
    /* b10    : hostProtectedAreaSupported */
    /* b9     : DEVICE_RESETSupported */
    /* b8     : SERVICEInterruptSupported */
    /* b7     : releaseInterruptSupported */
    /* b6     : lookAheadSupported */
    /* b5     : writeCacheSupported */
    /* b4     : word82_bit4 */
    /* b3     : mandPowerManagmentSupported */
    /* b2     : removableMediaSupported */
    /* b1     : securityModeSupported */
    /* b0     : SMARTSupported */
  bit16   commandSetSupported1;
    /* b15-b14: word83_bit14_15  */
    /* b13    : FLUSH_CACHE_EXTSupported  */
    /* b12    : mandatoryFLUSH_CACHESupported */
    /* b11    : devConfOverlaySupported */
    /* b10    : address48BitsSupported */
    /* b9     : autoAcousticManageSupported */
    /* b8     : SET_MAX_SecurityExtSupported */
    /* b7     : word83_bit7 */
    /* b6     : SET_FEATUREReqSpinupSupported */
    /* b5     : powerUpInStandyBySupported */
    /* b4     : removableMediaStNotifSupported */
    /* b3     : advanPowerManagmentSupported */
    /* b2     : CFASupported */
    /* b1     : DMAQueuedSupported */
    /* b0     : DOWNLOAD_MICROCODESupported */
  bit16   commandSetFeatureSupportedExt;
    /* b15-b13: word84_bit13_15 */
    /* b12    : timeLimitRWContSupported */
    /* b11    : timeLimitRWSupported */
    /* b10    : writeURGBitSupported */
    /* b9     : readURGBitSupported */
    /* b8     : wwwNameSupported */
    /* b7     : WRITE_DMAQ_FUA_EXTSupported */
    /* b6     : WRITE_FUA_EXTSupported */
    /* b5     : generalPurposeLogSupported */
    /* b4     : streamingSupported  */
    /* b3     : mediaCardPassThroughSupported */
    /* b2     : mediaSerialNoSupported */
    /* b1     : SMARTSelfRestSupported */
    /* b0     : SMARTErrorLogSupported */
  bit16   commandSetFeatureEnabled;
    /* b15    : word85_bit15 */
    /* b14    : NOPEnabled */
    /* b13    : READ_BUFFEREnabled  */
    /* b12    : WRITE_BUFFEREnabled */
    /* b11    : word85_bit11 */
    /* b10    : hostProtectedAreaEnabled  */
    /* b9     : DEVICE_RESETEnabled */
    /* b8     : SERVICEInterruptEnabled */
    /* b7     : releaseInterruptEnabled */
    /* b6     : lookAheadEnabled */
    /* b5     : writeCacheEnabled */
    /* b4     : word85_bit4 */
    /* b3     : mandPowerManagmentEnabled */
    /* b2     : removableMediaEnabled */
    /* b1     : securityModeEnabled */
    /* b0     : SMARTEnabled */
  bit16   commandSetFeatureEnabled1;
    /* b15-b14: word86_bit14_15 */
    /* b13    : FLUSH_CACHE_EXTEnabled */
    /* b12    : mandatoryFLUSH_CACHEEnabled */
    /* b11    : devConfOverlayEnabled */
    /* b10    : address48BitsEnabled */
    /* b9     : autoAcousticManageEnabled */
    /* b8     : SET_MAX_SecurityExtEnabled */
    /* b7     : word86_bit7 */
    /* b6     : SET_FEATUREReqSpinupEnabled */
    /* b5     : powerUpInStandyByEnabled  */
    /* b4     : removableMediaStNotifEnabled */
    /* b3     : advanPowerManagmentEnabled */
    /* b2     : CFAEnabled */
    /* b1     : DMAQueuedEnabled */
    /* b0     : DOWNLOAD_MICROCODEEnabled */
  bit16   commandSetFeatureDefault;
    /* b15-b13: word87_bit13_15 */
    /* b12    : timeLimitRWContEnabled */
    /* b11    : timeLimitRWEnabled */
    /* b10    : writeURGBitEnabled */
    /* b9     : readURGBitEnabled */
    /* b8     : wwwNameEnabled */
    /* b7     : WRITE_DMAQ_FUA_EXTEnabled */
    /* b6     : WRITE_FUA_EXTEnabled */
    /* b5     : generalPurposeLogEnabled */
    /* b4     : streamingEnabled */
    /* b3     : mediaCardPassThroughEnabled */
    /* b2     : mediaSerialNoEnabled */
    /* b1     : SMARTSelfRestEnabled */
    /* b0     : SMARTErrorLogEnabled */
  bit16   ultraDMAModes;
    /* b15    : word88_bit15 */
    /* b14    : ultraDMAMode6Selected */
    /* b13    : ultraDMAMode5Selected */
    /* b12    : ultraDMAMode4Selected */
    /* b11    : ultraDMAMode3Selected */
    /* b10    : ultraDMAMode2Selected */
    /* b9     : ultraDMAMode1Selected */
    /* b8     : ultraDMAMode0Selected */
    /* b7     : word88_bit7  */
    /* b6     : ultraDMAMode6Supported */
    /* b5     : ultraDMAMode5Supported */
    /* b4     : ultraDMAMode4Supported */
    /* b3     : ultraDMAMode3Supported */
    /* b2     : ultraDMAMode2Supported */
    /* b1     : ultraDMAMode1Supported */
    /* b0     : ultraDMAMode0Supported */
  bit16   timeToSecurityErase;
  bit16   timeToEnhhancedSecurityErase;
  bit16   currentAPMValue;
  bit16   masterPasswordRevCode;
  bit16   hardwareResetResult;
    /* b15-b14: word93_bit15_14 */
    /* b13    : deviceDetectedCBLIBbelow Vil */
    /* b12-b8 : device1 HardwareResetResult */
    /* b7-b0  : device0 HardwareResetResult */
  bit16   currentAutoAccousticManagementValue;
    /* b15-b8 : Vendor recommended value */
    /* b7-b0  : current value */
  bit16   word95_99[5];                  /**< word85-99 of identify device information  */
  bit16   maxLBA0_15;                    /**< word100 of identify device information  */
  bit16   maxLBA16_31;                   /**< word101 of identify device information  */
  bit16   maxLBA32_47;                   /**< word102 of identify device information  */
  bit16   maxLBA48_63;                   /**< word103 of identify device information  */
  bit16   word104_107[4];                /**< word104-107 of identify device information  */
  bit16   namingAuthority;
    /* b15-b12: NAA_bit0_3  */
    /* b11-b0 : IEEE_OUI_bit12_23*/
  bit16   namingAuthority1;
    /* b15-b4 : IEEE_OUI_bit0_11 */
    /* b3-b0  : uniqueID_bit32_35 */
  bit16   uniqueID_bit16_31;                      /**< word110 of identify device information  */
  bit16   uniqueID_bit0_15;                       /**< word111 of identify device information  */
  bit16   word112_126[15];
  bit16   removableMediaStatusNotificationFeature;
    /* b15-b2 : word127_b16_2 */
    /* b1-b0  : supported set see ATAPI6 spec */
  bit16   securityStatus;
    /* b15-b9 : word128_b15_9 */
    /* b8     : securityLevel */
    /* b7-b6  : word128_b7_6 */
    /* b5     : enhancedSecurityEraseSupported */
    /* b4     : securityCountExpired */
    /* b3     : securityFrozen */
    /* b2     : securityLocked */
    /* b1     : securityEnabled */
    /* b0     : securitySupported */
  bit16   vendorSpecific[31];
  bit16   cfaPowerMode1;
    /* b15    : word 160 supported */
    /* b14    : word160_b14 */
    /* b13    : cfaPowerRequired */
    /* b12    : cfaPowerModeDisabled */
    /* b11-b0 : maxCurrentInMa */
  bit16   word161_175[15];
  bit16   currentMediaSerialNumber[30];
  bit16   word206_254[49];              /**< word206-254 of identify device information  */
  bit16   integrityWord;
    /* b15-b8 : cheksum */
    /* b7-b0  : signature */
} agsaSATAIdentifyData_t;




/** \brief data structure describes an SSP Command INFORMATION UNIT
 *
 * data structure describes an SSP Command INFORMATION UNIT used for SSP command and is part of
 * the SSP frame.
 *
 * Currently, only CDB up to 16 bytes is supported. Additional CDB length is supported to 0 bytes..
 *
 */
typedef struct agsaSSPCmdInfoUnit_s
{
    bit8    lun[8];                /* SCSI Logical Unit Number */
    bit8    reserved1;             /* reserved */
    bit8    efb_tp_taskAttribute;
    /* B7   : enabledFirstBurst */
    /* B6-3 : taskPriority */
    /* B2-0 : taskAttribute */
    bit8    reserved2;             /* reserved */
    bit8    additionalCdbLen;
    /* B7-2 : additionalCdbLen */
    /* B1-0 : reserved */
    bit8    cdb[16];      /* The SCSI CDB up to 16 bytes length */
} agsaSSPCmdInfoUnit_t;

#define SA_SSPCMD_GET_TASKATTRIB(pCmd) ((pCmd)->efb_tp_taskAttribute & 0x7)


/** \brief structure describes an SSP Response INFORMATION UNIT
 *
 * data structure describes an SSP Response INFORMATION UNIT used for SSP response to Command IU
 * or Task IU and is part of the SSP frame
 *
 */

typedef struct agsaSSPResponseInfoUnit_s
{
    bit8    reserved1[10];      /* reserved */

    bit8    dataPres;           /* which data is present */
    /* B7-2 : reserved */
    /* B1-0 : data Present */
    bit8    status;             /* SCSI status as define by SAM-3 */
    bit8    reserved4[4];       /* reserved */
    bit8    senseDataLen[4];    /* SCSI Sense Data length */
    bit8    responsedataLen[4]; /* Response data length */
    /* Follow by Response Data if any */
    /* Follow by Sense Data if any */
} agsaSSPResponseInfoUnit_t;


typedef struct agsaSSPFrameFormat_s
{
    bit8    frameType;             /* frame type */
    bit8    hdsa[3];               /* Hashed destination SAS Address */
    bit8    reserved1;
    bit8    hssa[3];               /* Hashed source SAS Address */
    bit8    reserved2;
    bit8    reserved3;
    bit8    tlr_rdf;
    /* B7-5 : reserved */
    /* B4-3 : TLR control*/
    /* B2   : Retry Data Frames */
    /* B1   : Retransmit */
    /* B0   : Changing Data Pointer */
    bit8    fill_bytes;
    /* B7-2 : reserved */
    /* B1-0 : Number of Fill bytes*/
    bit8    reserved5;
    bit8    reserved6[3];
    bit8    tag[2];               /* CMD or TM tag */
    bit8    tptt[2];              /* target port transfer tag */
    bit8    dataOffset[4];        /* data offset */
    /* Follow by IU  */
} agsaSSPFrameFormat_t;


typedef struct agsaSSPOpenFrame_s
{
    bit8    frameType;             /* frame type */
    /* B7   : Initiator Port */
    /* B6-4 : Protocol */
    /* B3-0 : Address Frame Type */
    bit8    feat_connrate;
    /* B7-4 : features */
    /* B3-0 : connection rate */
    bit8    initiatorConnTag[2];    /* Initiator connection tag */
    bit8    dstSasAddr[8];          /* Destination SAS Address */
    bit8    srcSasAddr[8];          /* Source SAS Address */
    bit8    zoneSrcGroup;           /* Zone source group */
    bit8    pathwayBlockCount;      /* pathway block count */
    bit8    arbWaitTime[2];         /* Arbitration Wait Time */
    bit8    moreCompatFeat[4];      /* More Compatibility Features */
    /* Follow by CRC  */
} agsaSSPOpenFrame_t;

#define SA_SSPRESP_GET_SENSEDATALEN(pSSPResp)                   \
    DMA_BEBIT32_TO_BIT32(*(bit32*)(pSSPResp)->senseDataLen)

#define SA_SSPRESP_GET_RESPONSEDATALEN(pSSPResp)                \
    DMA_BEBIT32_TO_BIT32(*(bit32*)(pSSPResp)->responsedataLen)

#define SA_SSPRESP_GET_DATAPRES(pSSPResp) ((pSSPResp)->dataPres & 0x3)

/** \brief structure describes a SAS SSP Task Management command request
 *
 * The agsaSSPScsiTaskMgntReq_t data structure describes a SAS SSP Task Management command request sent by the
 * initiator or received by the target.
 *
 * The response to Task Management is specified by agsaSSPResponseInfoUnit_t.
 *
 * This data structure is one instance of the generic request issued to saSSPStart() and is passed
 * as an agsaSASRequestBody_t
 *
 */
typedef struct agsaSSPScsiTaskMgntReq_s
{
    bit8    lun[8];               /* SCSI Logical Unit Number */
    bit16   reserved1;            /* reserved */
    bit8    taskMgntFunction;     /* task management function code */
    bit8    reserved2;            /* reserved */
    bit16   tagOfTaskToBeManaged; /* Tag/context of task to be managed */
    bit16   reserved3;            /* reserved */
    bit32   reserved4[3];         /* reserved */
    bit32   tmOption;             /* Not part of SSP TMF IU */
    /* B7-2 : reserved */
    /* B1   : DS_OPTION */
    /* B0   : ADS_OPTION */
} agsaSSPScsiTaskMgntReq_t;


/** \brief data structure describes the first four bytes of the SMP frame.
 *
 * The agsaSMPFrameHeader_t data structure describes the first four bytes of the SMP frame.
 *
 *
 */

typedef struct agsaSMPFrameHeader_s
{
    bit8   smpFrameType;      /* The first byte of SMP frame represents the SMP FRAME TYPE */
    bit8   smpFunction;       /* The second byte of the SMP frame represents the SMP FUNCTION */
    bit8   smpFunctionResult; /* The third byte of SMP frame represents FUNCTION RESULT of the SMP response. */
    bit8   smpReserved;       /* reserved */
} agsaSMPFrameHeader_t;

/****************************************************************
 *            report general response
 ****************************************************************/
#define SA_REPORT_GENERAL_CONFIGURING_BIT     0x2
#define SA_REPORT_GENERAL_CONFIGURABLE_BIT    0x1

typedef struct agsaSmpRespReportGeneral_s
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
} agsaSmpRespReportGeneral_t;

#define SA_REPORT_GENERAL_IS_CONFIGURING(pResp) \
  (((pResp)->configuring_configurable & SA_REPORT_GENERAL_CONFIGURING_BIT) == \
      SA_REPORT_GENERAL_CONFIGURING_BIT)

#define SA_REPORT_GENERAL_IS_CONFIGURABLE(pResp) \
  (((pResp)->configuring_configurable & SA_REPORT_GENERAL_CONFIGURABLE_BIT) == \
      SA_REPORT_GENERAL_CONFIGURABLE_BIT)

#define SA_REPORT_GENERAL_GET_ROUTEINDEXES(pResp) \
  DMA_BEBIT16_TO_BIT16(*(bit16 *)((pResp)->expanderRouteIndexes16))

/****************************************************************
 *            report manufacturer info response
 ****************************************************************/
typedef struct agsaSmpRespReportManufactureInfo_s
{
  bit8    reserved1[8];
  bit8    vendorIdentification[8];
  bit8    productIdentification[16];
  bit8    productRevisionLevel[4];
  bit8    vendorSpecific[20];
} agsaSmpRespReportManufactureInfo_t;

/****************************************************************
 *           discover request
 ****************************************************************/
typedef struct agsaSmpReqDiscover_s
{
  bit32   reserved1;
  bit8    reserved2;
  bit8    phyIdentifier;
  bit8    ignored;
  bit8    reserved3;
} agsaSmpReqDiscover_t;

/****************************************************************
 *           discover response
 ****************************************************************/
typedef struct agsaSmpRespDiscover_s
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
} agsaSmpRespDiscover_t;

#define SA_DISCRSP_SSP_BIT    0x08
#define SA_DISCRSP_STP_BIT    0x04
#define SA_DISCRSP_SMP_BIT    0x02
#define SA_DISCRSP_SATA_BIT   0x01

#define SA_DISCRSP_SATA_PS_BIT   0x80

#define SA_DISCRSP_GET_ATTACHED_DEVTYPE(pResp) \
  (((pResp)->attachedDeviceType & 0x70) >> 4)
#define SA_DISCRSP_GET_LINKRATE(pResp) \
  ((pResp)->negotiatedPhyLinkRate & 0x0F)

#define SA_DISCRSP_IS_SSP_INITIATOR(pResp) \
  (((pResp)->attached_Ssp_Stp_Smp_Sata_Initiator & SA_DISCRSP_SSP_BIT) == SA_DISCRSP_SSP_BIT)
#define SA_DISCRSP_IS_STP_INITIATOR(pResp) \
  (((pResp)->attached_Ssp_Stp_Smp_Sata_Initiator & SA_DISCRSP_STP_BIT) == SA_DISCRSP_STP_BIT)
#define SA_DISCRSP_IS_SMP_INITIATOR(pResp) \
  (((pResp)->attached_Ssp_Stp_Smp_Sata_Initiator & SA_DISCRSP_SMP_BIT) == SA_DISCRSP_SMP_BIT)
#define SA_DISCRSP_IS_SATA_HOST(pResp) \
  (((pResp)->attached_Ssp_Stp_Smp_Sata_Initiator & SA_DISCRSP_SATA_BIT) == SA_DISCRSP_SATA_BIT)

#define SA_DISCRSP_IS_SSP_TARGET(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & SA_DISCRSP_SSP_BIT) == SA_DISCRSP_SSP_BIT)
#define SA_DISCRSP_IS_STP_TARGET(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & SA_DISCRSP_STP_BIT) == SA_DISCRSP_STP_BIT)
#define SA_DISCRSP_IS_SMP_TARGET(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & SA_DISCRSP_SMP_BIT) == SA_DISCRSP_SMP_BIT)
#define SA_DISCRSP_IS_SATA_DEVICE(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & SA_DISCRSP_SATA_BIT) == SA_DISCRSP_SATA_BIT)
#define SA_DISCRSP_IS_SATA_PORTSELECTOR(pResp) \
  (((pResp)->attached_SataPS_Ssp_Stp_Smp_Sata_Target & SA_DISCRSP_SATA_PS_BIT) == SA_DISCRSP_SATA_PS_BIT)

#define SA_DISCRSP_GET_SAS_ADDRESSHI(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->sasAddressHi)
#define SA_DISCRSP_GET_SAS_ADDRESSLO(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->sasAddressLo)

#define SA_DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->attachedSasAddressHi)
#define SA_DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pResp) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(pResp)->attachedSasAddressLo)

#define SA_DISCRSP_VIRTUALPHY_BIT 0x80
#define SA_DISCRSP_IS_VIRTUALPHY(pResp) \
  (((pResp)->virtualPhy_partialPathwayTimeout & SA_DISCRSP_VIRTUALPHY_BIT) == SA_DISCRSP_VIRTUALPHY_BIT)

#define SA_DISCRSP_GET_ROUTINGATTRIB(pResp) \
  ((pResp)->routingAttribute & 0x0F)

/****************************************************************
 *            report route table request
 ****************************************************************/
typedef struct agsaSmpReqReportRouteTable_s
{
  bit8   reserved1[2];
  bit8   expanderRouteIndex16[20];
  bit8   reserved2;
  bit8   phyIdentifier;
  bit8   reserved3[2];
} agsaSmpReqReportRouteTable_t;

/****************************************************************
 *            report route response
 ****************************************************************/
typedef struct agsaSmpRespReportRouteTable_s
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
} agsaSmpRespReportRouteTable_t;

/****************************************************************
 *            configure route information request
 ****************************************************************/
typedef struct agsaSmpReqConfigureRouteInformation_s
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
} agsaSmpReqConfigureRouteInformation_t;

/****************************************************************
 *            report Phy Sata request
 ****************************************************************/
typedef struct agsaSmpReqReportPhySata_s
{
  bit8   reserved1[4];
  bit8   reserved2;
  bit8   phyIdentifier;
  bit8   reserved3[2];
} agsaSmpReqReportPhySata_t;

/****************************************************************
 *            report Phy Sata response
 ****************************************************************/
typedef struct agsaSmpRespReportPhySata_s
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
} agsaSmpRespReportPhySata_t;

/****************************************************************
 *            Phy Control request
 ****************************************************************/
typedef struct agsaSmpReqPhyControl_s
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
} agsaSmpReqPhyControl_t;




#endif  /*__SASPEC_H__ */
