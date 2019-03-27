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
/*! \file sampidefs.h
 *  \brief The file defines the constants used by SAS/SATA LL layer
 *
 */

/*******************************************************************************/

#ifndef  __SAMPIDEFS_H__

#define __SAMPIDEFS_H__

/* for Request Opcode of IOMB */
#define OPC_INB_ECHO                          0x001   /*  */

#define OPC_INB_PHYSTART                      0x004   /*  */
#define OPC_INB_PHYSTOP                       0x005   /*  */
#define OPC_INB_SSPINIIOSTART                 0x006   /*  */
#define OPC_INB_SSPINITMSTART                 0x007   /*  */
#define OPC_INB_SSPINIEXTIOSTART              0x008   /*  V reserved */
#define OPC_INB_DEV_HANDLE_ACCEPT             0x009   /*  */
#define OPC_INB_SSPTGTIOSTART                 0x00a   /*  */
#define OPC_INB_SSPTGTRSPSTART                0x00b   /*  */
#define OPC_INB_SSP_ABORT                     0x00f   /*  */
#define OPC_INB_DEREG_DEV_HANDLE              0x010   /* 16 */
#define OPC_INB_GET_DEV_HANDLE                0x011   /* 17 */
#define OPC_INB_SMP_REQUEST                   0x012   /* 18 */

#define OPC_INB_SMP_ABORT                     0x014   /* 20 */

#define OPC_INB_SPC_REG_DEV                   0x016   /* 22 V reserved */
#define OPC_INB_SATA_HOST_OPSTART             0x017   /* 23 */
#define OPC_INB_SATA_ABORT                    0x018   /* 24 */
#define OPC_INB_LOCAL_PHY_CONTROL             0x019   /* 25 */
#define OPC_INB_SPC_GET_DEV_INFO              0x01a   /* 26 V reserved */

#define OPC_INB_FW_FLASH_UPDATE               0x020   /* 32 */

#define OPC_INB_GPIO                          0x022    /* 34 */
#define OPC_INB_SAS_DIAG_MODE_START_END       0x023    /* 35 */
#define OPC_INB_SAS_DIAG_EXECUTE              0x024    /* 36 */
#define OPC_INB_SPC_SAS_HW_EVENT_ACK          0x025    /* 37 V reserved */
#define OPC_INB_GET_TIME_STAMP                0x026    /* 38 */
#define OPC_INB_PORT_CONTROL                  0x027    /* 39 */
#define OPC_INB_GET_NVMD_DATA                 0x028    /* 40 */
#define OPC_INB_SET_NVMD_DATA                 0x029    /* 41 */
#define OPC_INB_SET_DEVICE_STATE              0x02a    /* 42 */
#define OPC_INB_GET_DEVICE_STATE              0x02b    /* 43 */
#define OPC_INB_SET_DEV_INFO                  0x02c    /* 44 */
#define OPC_INB_SAS_RE_INITIALIZE             0x02d    /* 45 V reserved */
#define OPC_INB_SGPIO                         0x02e    /* 46 */
#define OPC_INB_PCIE_DIAG_EXECUTE             0x02f    /* 47 */

#define OPC_INB_SET_CONTROLLER_CONFIG         0x030    /* 48 */
#define OPC_INB_GET_CONTROLLER_CONFIG         0x031    /* 49 */

#define OPC_INB_REG_DEV                       0x032    /* 50 SPCV */
#define OPC_INB_SAS_HW_EVENT_ACK              0x033    /* 51 SPCV */
#define OPC_INB_GET_DEV_INFO                  0x034    /* 52 SPCV */
#define OPC_INB_GET_PHY_PROFILE               0x035    /* 53 SPCV */
#define OPC_INB_FLASH_OP_EXT                  0x036    /* 54 SPCV */
#define OPC_INB_SET_PHY_PROFILE               0x037    /* 55 SPCV */
#define OPC_INB_GET_DFE_DATA                  0x038    /* 56 SPCV */
#define OPC_INB_GET_VHIST_CAP                 0x039    /* 57 SPCV12g */


#define OPC_INB_KEK_MANAGEMENT                0x100    /* 256 SPCV */
#define OPC_INB_DEK_MANAGEMENT                0x101    /* 257 SPCV */
#define OPC_INB_SSP_DIF_ENC_OPSTART           0x102    /* 258 SPCV */
#define OPC_INB_SATA_DIF_ENC_OPSTART          0x103    /* 259 SPCV */
#define OPC_INB_OPR_MGMT                      0x104    /* 260 SPCV */
#define OPC_INB_ENC_TEST_EXECUTE              0x105    /* 261 SPCV */
#define OPC_INB_SET_OPERATOR                  0x106    /* 262 SPCV */
#define OPC_INB_GET_OPERATOR                  0x107    /* 263 SPCV */
#define OPC_INB_DIF_ENC_OFFLOAD_CMD           0x110    /* 272 SPCV */

#define OPC_INB_FW_PROFILE                    0x888    /* 2184 SPCV */

/* for Response Opcode of IOMB */
#define OPC_OUB_ECHO                          0x001    /* 1 */

#define OPC_OUB_SPC_HW_EVENT                  0x004    /*  4 V reserved Now OPC_OUB_HW_EVENT */
#define OPC_OUB_SSP_COMP                      0x005    /* 5 */
#define OPC_OUB_SMP_COMP                      0x006    /* 6 */
#define OPC_OUB_LOCAL_PHY_CNTRL               0x007    /* 7 */

#define OPC_OUB_SPC_DEV_REGIST                0x00a    /* 10 V reserved Now OPC_OUB_DEV_REGIST */
#define OPC_OUB_DEREG_DEV                     0x00b    /* 11 */
#define OPC_OUB_GET_DEV_HANDLE                0x00c    /* 12 */
#define OPC_OUB_SATA_COMP                     0x00d    /* 13 */
#define OPC_OUB_SATA_EVENT                    0x00e    /* 14 */
#define OPC_OUB_SSP_EVENT                     0x00f    /* 15 */

#define OPC_OUB_SPC_DEV_HANDLE_ARRIV          0x010    /* 16 V reserved Now OPC_OUB_DEV_HANDLE_ARRIV */

#define OPC_OUB_SSP_RECV_EVENT                0x012    /* 18 */
#define OPC_OUB_SPC_DEV_INFO                  0x013    /* 19 V reserved Now OPC_OUB_DEV_INFO*/
#define OPC_OUB_FW_FLASH_UPDATE               0x014    /* 20 */

#define OPC_OUB_GPIO_RESPONSE                 0x016    /* 22 */
#define OPC_OUB_GPIO_EVENT                    0x017    /* 23 */
#define OPC_OUB_GENERAL_EVENT                 0x018    /* 24 */

#define OPC_OUB_SSP_ABORT_RSP                 0x01a    /* 26 */
#define OPC_OUB_SATA_ABORT_RSP                0x01b    /* 27 */
#define OPC_OUB_SAS_DIAG_MODE_START_END       0x01c    /* 28 */
#define OPC_OUB_SAS_DIAG_EXECUTE              0x01d    /* 29 */
#define OPC_OUB_GET_TIME_STAMP                0x01e    /* 30 */
#define OPC_OUB_SPC_SAS_HW_EVENT_ACK          0x01f    /* 31 V reserved Now OPC_OUB_SAS_HW_EVENT_ACK*/
#define OPC_OUB_PORT_CONTROL                  0x020    /* 32 */
#define OPC_OUB_SKIP_ENTRY                    0x021    /* 33 */
#define OPC_OUB_SMP_ABORT_RSP                 0x022    /* 34 */
#define OPC_OUB_GET_NVMD_DATA                 0x023    /* 35 */
#define OPC_OUB_SET_NVMD_DATA                 0x024    /* 36 */
#define OPC_OUB_DEVICE_HANDLE_REMOVAL         0x025    /* 37 */
#define OPC_OUB_SET_DEVICE_STATE              0x026    /* 38 */
#define OPC_OUB_GET_DEVICE_STATE              0x027    /* 39 */
#define OPC_OUB_SET_DEV_INFO                  0x028    /* 40 */
#define OPC_OUB_SAS_RE_INITIALIZE             0x029    /* 41 V reserved not replaced */

#define OPC_OUB_HW_EVENT                      0x700    /* 1792 SPCV Was OPC_OUB_SPC_HW_EVENT*/
#define OPC_OUB_DEV_HANDLE_ARRIV              0x720    /* 1824 SPCV Was OPC_OUB_SPC_DEV_HANDLE_ARRIV*/

#define OPC_OUB_PHY_START_RESPONSE            0x804    /* 2052 SPCV */
#define OPC_OUB_PHY_STOP_RESPONSE             0x805    /* 2053 SPCV */
#define OPC_OUB_SGPIO_RESPONSE                0x82E    /* 2094 SPCV */
#define OPC_OUB_PCIE_DIAG_EXECUTE             0x82F    /* 2095 SPCV */

#define OPC_OUB_SET_CONTROLLER_CONFIG         0x830    /* 2096 SPCV */
#define OPC_OUB_GET_CONTROLLER_CONFIG         0x831    /* 2097 SPCV */
#define OPC_OUB_DEV_REGIST                    0x832    /* 2098 SPCV */
#define OPC_OUB_SAS_HW_EVENT_ACK              0x833    /* 2099 SPCV */
#define OPC_OUB_DEV_INFO                      0x834    /* 2100 SPCV */
#define OPC_OUB_GET_PHY_PROFILE_RSP           0x835    /* 2101 SPCV */
#define OPC_OUB_FLASH_OP_EXT_RSP              0x836    /* 2102 SPCV */
#define OPC_OUB_SET_PHY_PROFILE_RSP           0x837    /* 2103 SPCV */
#define OPC_OUB_GET_DFE_DATA_RSP              0x838    /* 2104 SPCV */
#define OPC_OUB_GET_VIST_CAP_RSP              0x839    /* Can be 2104 for SPCV12g  */

#define OPC_OUB_FW_PROFILE                    0x888    /* 2184 */

#define OPC_OUB_KEK_MANAGEMENT                0x900    /* 2304 SPCV */
#define OPC_OUB_DEK_MANAGEMENT                0x901    /* 2305 SPCV */
#define OPC_OUB_COMBINED_SSP_COMP             0x902    /* 2306 SPCV */
#define OPC_OUB_COMBINED_SATA_COMP            0x903    /* 2307 SPCV */
#define OPC_OUB_OPR_MGMT                      0x904    /* 2308 SPCV */
#define OPC_OUB_ENC_TEST_EXECUTE              0x905    /* 2309 SPCV */
#define OPC_OUB_SET_OPERATOR                  0x906    /* 2310 SPCV */
#define OPC_OUB_GET_OPERATOR                  0x907    /* 2311 SPCV */
#define OPC_OUB_DIF_ENC_OFFLOAD_RSP           0x910    /* 2320 SPCV */

/* Definitions for encryption key management */
#define KEK_MGMT_SUBOP_INVALIDATE                0x1
#define KEK_MGMT_SUBOP_UPDATE                    0x2
#define KEK_MGMT_SUBOP_KEYCARDINVALIDATE         0x3
#define KEK_MGMT_SUBOP_KEYCARDUPDATE             0x4

#define DEK_MGMT_SUBOP_INVALIDATE                0x1
#define DEK_MGMT_SUBOP_UPDATE                    0x2

/***************************************************
 *           typedef for IOMB structure
 ***************************************************/
/** \brief the data structure of Echo Command
 *
 * use to describe MPI Echo Command (64 bytes)
 *
 */
typedef struct agsaEchoCmd_s {
  bit32           tag;
  bit32           payload[14];
} agsaEchoCmd_t;

/** \brief the data structure of PHY Start Command
 *
 * use to describe MPI PHY Start Command (64 bytes)
 *
 */
typedef struct agsaPhyStartCmd_s {
  bit32             tag;
  bit32             SscdAseSHLmMlrPhyId;
  agsaSASIdentify_t sasIdentify;
  bit32             analogSetupIdx;
  bit32             SAWT_DAWT;
  bit32             reserved[5];
} agsaPhyStartCmd_t;

#define SPINHOLD_DISABLE   (0x00 << 14)
#define SPINHOLD_ENABLE    (0x01 << 14)
#define LINKMODE_SAS       (0x01 << 12)
#define LINKMODE_DSATA     (0x02 << 12)
#define LINKMODE_AUTO      (0x03 << 12)
#define LINKRATE_15        (0x01 << 8)
#define LINKRATE_30        (0x02 << 8)
#define LINKRATE_60        (0x04 << 8)
#define LINKRATE_12        (0x08 << 8)

/** \brief the data structure of PHY Stop Command
 *
 * use to describe MPI PHY Start Command (64 bytes)
 *
 */
typedef struct agsaPhyStopCmd_s {
  bit32             tag;
  bit32             phyId;
  bit32             reserved[13];
} agsaPhyStopCmd_t;

/** \brief the data structure of SSP INI IO Start Command
 *
 * use to describe MPI SSP INI IO Start Command (64 bytes)
 *
 */
typedef struct agsaSSPIniIOStartCmd_s {
  bit32                tag;
  bit32                deviceId;
  bit32                dataLen;
  bit32                dirMTlr;
  agsaSSPCmdInfoUnit_t SSPInfoUnit;
  bit32                AddrLow0;
  bit32                AddrHi0;
  bit32                Len0;
  bit32                E0;
} agsaSSPIniIOStartCmd_t;

/** \brief the data structure of SSP INI TM Start Command
 *
 * use to describe MPI SSP INI TM Start Command (64 bytes)
 *
 */
typedef struct agsaSSPIniTMStartCmd_s {
  bit32                tag;
  bit32                deviceId;
  bit32                relatedTag;
  bit32                TMfunction;
  bit8                 lun[8];
  bit32                dsAdsMReport;
  bit32                reserved[8];
} agsaSSPIniTMStartCmd_t;

/** \brief the data structure of SSP INI Extended IO Start Command
 *
 * use to describe MPI SSP INI Extended CDB Start Command (96 bytes to support 32 CDB)
 *
 */
typedef struct agsaSSPIniExtIOStartCmd_s {
  bit32                tag;
  bit32                deviceId;
  bit32                dataLen;
  bit32                SSPIuLendirMTlr;
  bit8                 SSPIu[1];
  /* variable lengh */
  /*  bit32            AddrLow0; */
  /*  bit32            AddrHi0;  */
  /*  bit32            Len0;     */
  /*  bit32            E0;       */
} agsaSSPIniExtIOStartCmd_t;

typedef struct agsaSSPIniEncryptIOStartCmd_s
{
  bit32                tag;                  /* 1 */
  bit32                deviceId;             /* 2 */
  bit32                dataLen;              /* 3 */
  bit32                dirMTlr;              /* 4 */
  bit32                sspiu_0_3_indcdbalL;  /* 5 */
  bit32                sspiu_4_7_indcdbalH;  /* 6 */
  bit32                sspiu_8_11;           /* 7 */
  bit32                sspiu_12_15;          /* 8 */
  bit32                sspiu_16_19;          /* 9 */
  bit32                sspiu_19_23;          /* 10 */
  bit32                sspiu_24_27;          /* 11 */
  bit32                epl_descL;            /* 12 */
  bit32                dpl_descL;            /* 13 */
  bit32                edpl_descH;           /* 14 */
  bit32                DIF_flags;            /* 15 */
  bit32                udt;                  /* 16 0x10 */
  bit32                udtReplacementLo;     /* 17 */
  bit32                udtReplacementHi;     /* 18 */
  bit32                DIF_seed;             /* 19 */
  bit32                encryptFlagsLo;       /* 20 0x14 */
  bit32                encryptFlagsHi;       /* 21 */
  bit32                keyTag_W0;            /* 22 */
  bit32                keyTag_W1;            /* 23 */
  bit32                tweakVal_W0;          /* 24 0x18 */
  bit32                tweakVal_W1;          /* 25 */
  bit32                tweakVal_W2;          /* 26 */
  bit32                tweakVal_W3;          /* 27 */
  bit32                AddrLow0;             /* 28 0x1C */
  bit32                AddrHi0;              /* 29 */
  bit32                Len0;                 /* 30 */
  bit32                E0;                   /* 31 */
} agsaSSPIniEncryptIOStartCmd_t;

/** \brief the data structure of SSP Abort Command
 *
 * use to describe MPI SSP Abort Command (64 bytes)
 *
 */
typedef struct agsaSSPAbortCmd_s {
  bit32             tag;
  bit32             deviceId;
  bit32             HTagAbort;
  bit32             abortAll;
  bit32             reserved[11];
} agsaSSPAbortCmd_t;

/** \brief the data structure of Register Device Command
 *
 * use to describe MPI DEVICE REGISTER Command (64 bytes)
 *
 */
typedef struct agsaRegDevCmd_s {
  bit32             tag;
  bit32             phyIdportId;
  bit32             dTypeLRateAwtHa;
  bit32             ITNexusTimeOut;
  bit32             sasAddrHi;
  bit32             sasAddrLo;
  bit32             DeviceId;
  bit32             reserved[8];
} agsaRegDevCmd_t;

/** \brief the data structure of Deregister Device Handle Command
 *
 * use to describe MPI DEREGISTER DEVIDE HANDLE Command (64 bytes)
 *
 */
typedef struct agsaDeregDevHandleCmd_s {
  bit32             tag;
  bit32             deviceId;
  bit32             portId;
  bit32             reserved[12];
} agsaDeregDevHandleCmd_t;

/** \brief the data structure of Get Device Handle Command
 *
 * use to describe MPI GET DEVIDE HANDLE Command (64 bytes)
 *
 */
typedef struct agsaGetDevHandleCmd_s {
  bit32             tag;
  bit32             DevADevTMaxDIDportId;
  bit32             skipCount;
  bit32             reserved[12];
} agsaGetDevHandleCmd_t;

/** \brief the data structure of SMP Request Command
 *
 * use to describe MPI SMP REQUEST Command (64 bytes)
 *
 */

typedef struct agsaSMPCmd_s {
  bit32                tag;
  bit32                deviceId;
  bit32                IR_IP_OV_res_phyId_DPdLen_res;
                                               /* Bits [0]  - IR */
                                               /* Bits [1] - IP */
                                               /* Bits [15:2] - Reserved */
                                               /* Bits [23:16] - Len */
                                               /* Bits [31:24] - Reserved */
  bit32                SMPCmd[12];
} agsaSMPCmd_t;


typedef struct agsaSMPCmd_V_s {
  bit32                tag;                    /* 1 */
  bit32                deviceId;               /* 2 */
  bit32                IR_IP_OV_res_phyId_DPdLen_res;/* 3 */
                                               /* Bits [0]  - IR */
                                               /* Bits [1] - IP */
                                               /* Bits [15:2] - Reserved */
                                               /* Bits [23:16] - Len */
                                               /* Bits [31:24] - Reserved */
  bit32                SMPHDR;                 /* 4 */
  bit32                SMP3_0;                 /* 5 */
  bit32                SMP7_4;                 /* 6 */
  bit32                SMP11_8;                /* 7 */
  bit32                IndirL_SMPRF15_12;      /* 8 */
  bit32                IndirH_or_SMPRF19_16;   /* 9 */
  bit32                IndirLen_or_SMPRF23_20; /* 10 */
  bit32                R_or_SMPRF27_24;        /* 11 */
  bit32                ISRAL_or_SMPRF31_28;    /* 12 */
  bit32                ISRAH_or_SMPRF35_32;    /* 13 */
  bit32                ISRL_or_SMPRF39_36;     /* 14 */
  bit32                R_or_SMPRF43_40;        /* 15 */
} agsaSMPCmd_V_t;

/** \brief the data structure of SMP Abort Command
 *
 * use to describe MPI SMP Abort Command (64 bytes)
 *
 */
typedef struct agsaSMPAbortCmd_s {
  bit32             tag;
  bit32             deviceId;
  bit32             HTagAbort;
  bit32             Scp;
  bit32             reserved[11];
} agsaSMPAbortCmd_t;

/** \brief the data structure of SATA Start Command
 *
 * use to describe MPI SATA Start Command (64 bytes)
 *
 */
typedef struct agsaSATAStartCmd_s {
  bit32                    tag;              /* 1 */
  bit32                    deviceId;         /* 2 */
  bit32                    dataLen;          /* 3 */
  bit32                    optNCQTagataProt; /* 4 */
  agsaFisRegHostToDevice_t sataFis;          /* 5 6 7 8 9 */
  bit32                    reserved1;        /* 10 */
  bit32                    reserved2;        /* 11 */
  bit32                    AddrLow0;         /* 12 */
  bit32                    AddrHi0;          /* 13 */
  bit32                    Len0;             /* 14 */
  bit32                    E0;               /* 15 */
  bit32                    ATAPICDB[4];     /* 16-19 */
} agsaSATAStartCmd_t;

typedef struct agsaSATAEncryptStartCmd_s
{
  bit32                tag;                  /* 1 */
  bit32                IniDeviceId;          /* 2 */
  bit32                dataLen;              /* 3 */
  bit32                optNCQTagataProt;     /* 4 */
  agsaFisRegHostToDevice_t sataFis;          /* 5 6 7 8 9 */
  bit32                reserved1;            /* 10 */
  bit32                Res_EPL_DESCL;        /* 11 */
  bit32                resSKIPBYTES;         /* 12 */
  bit32                Res_DPL_DESCL_NDPLR;  /* 13 DIF per LA Address lo if DPLE is 1 */
  bit32                Res_EDPL_DESCH;       /* 14 DIF per LA Address hi if DPLE is 1 */
  bit32                DIF_flags;            /* 15 */
  bit32                udt;                  /* 16 */
  bit32                udtReplacementLo;     /* 17 */
  bit32                udtReplacementHi;     /* 18 */
  bit32                DIF_seed;             /* 19 */
  bit32                encryptFlagsLo;       /* 20 */
  bit32                encryptFlagsHi;       /* 21 */
  bit32                keyTagLo;             /* 22 */
  bit32                keyTagHi;             /* 23 */
  bit32                tweakVal_W0;          /* 24 */
  bit32                tweakVal_W1;          /* 25 */
  bit32                tweakVal_W2;          /* 26 */
  bit32                tweakVal_W3;          /* 27 */
  bit32                AddrLow0;             /* 28 */
  bit32                AddrHi0;              /* 29 */
  bit32                Len0;                 /* 30 */
  bit32                E0;                   /* 31 */
} agsaSATAEncryptStartCmd_t;

/** \brief the data structure of SATA Abort Command
 *
 * use to describe MPI SATA Abort Command (64 bytes)
 *
 */
typedef struct agsaSATAAbortCmd_s {
  bit32             tag;
  bit32             deviceId;
  bit32             HTagAbort;
  bit32             abortAll;
  bit32             reserved[11];
} agsaSATAAbortCmd_t;

/** \brief the data structure of Local PHY Control Command
 *
 * use to describe MPI LOCAL PHY CONTROL Command (64 bytes)
 *
 */
typedef struct agsaLocalPhyCntrlCmd_s {
  bit32             tag;
  bit32             phyOpPhyId;
  bit32             reserved1[14];
} agsaLocalPhyCntrlCmd_t;

/** \brief the data structure of Get Device Info Command
 *
 * use to describe MPI GET DEVIDE INFO Command (64 bytes)
 *
 */
typedef struct agsaGetDevInfoCmd_s {
  bit32             tag;
  bit32             DeviceId;
  bit32             reserved[13];
} agsaGetDevInfoCmd_t;

/** \brief the data structure of HW Reset Command
 *
 * use to describe MPI HW Reset Command (64 bytes)
 *
 */
typedef struct agsaHWResetCmd_s {
  bit32           option;
  bit32           reserved[14];
} agsaHWResetCmd_t;

/** \brief the data structure of Firmware download
 *
 * use to describe MPI FW DOWNLOAD Command (64 bytes)
 */
typedef struct agsaFwFlashUpdate_s {
  bit32             tag;
  bit32             curImageOffset;
  bit32             curImageLen;
  bit32             totalImageLen;
  bit32             reserved0[7];
  bit32             SGLAL;
  bit32             SGLAH;
  bit32             Len;
  bit32             extReserved;
} agsaFwFlashUpdate_t;


/** \brief the data structure EXT Flash Op
 *
 * use to describe Extented Flash Operation Command (128 bytes)
 */
typedef struct agsaFwFlashOpExt_s {
  bit32             tag;
  bit32             Command;
  bit32             PartOffset;
  bit32             DataLength;
  bit32             Reserved0[7];
  bit32             SGLAL;
  bit32             SGLAH;
  bit32             Len;
  bit32             E_sgl;
  bit32             Reserved[15];
} agsaFwFlashOpExt_t;

/** \brief the data structure EXT Flash Op
 *
 * use to describe Extented Flash Operation Command (64 bytes)
 */
typedef struct agsaFwFlashOpExtRsp_s {
  bit32             tag;
  bit32             Command;
  bit32             Status;
  bit32             Epart_Size;
  bit32             EpartSectSize;
  bit32             Reserved[10];
} agsaFwFlashOpExtRsp_t;


#define FWFLASH_IOMB_RESERVED_LEN 0x07

#ifdef SPC_ENABLE_PROFILE
typedef struct agsaFwProfileIOMB_s {
  bit32             tag;
  bit32             tcid_processor_cmd;
  bit32             codeStartAdd;
  bit32             codeEndAdd;
  bit32             reserved0[7];
  bit32             SGLAL;
  bit32             SGLAH;
  bit32             Len;
  bit32             extReserved;
} agsaFwProfileIOMB_t;
#define FWPROFILE_IOMB_RESERVED_LEN 0x07
#endif
/** \brief the data structure of GPIO Commannd
 *
 * use to describe MPI GPIO Command (64 bytes)
 */
typedef struct agsaGPIOCmd_s {
  bit32             tag;
  bit32             eOBIDGeGsGrGw;
  bit32             GpioWrMsk;
  bit32             GpioWrVal;
  bit32             GpioIe;
  bit32             OT11_0;
  bit32             OT19_12; /* reserved for SPCv controller */
  bit32             GPIEVChange;
  bit32             GPIEVRise;
  bit32             GPIEVFall;
  bit32             reserved[5];
} agsaGPIOCmd_t;


#define GPIO_GW_BIT 0x1
#define GPIO_GR_BIT 0x2
#define GPIO_GS_BIT 0x4
#define GPIO_GE_BIT 0x8

/** \brief the data structure of SAS Diagnostic Start/End Command
 *
 * use to describe MPI SAS Diagnostic Start/End Command (64 bytes)
 */
typedef struct agsaSASDiagStartEndCmd_s {
  bit32             tag;
  bit32             OperationPhyId;
  bit32             reserved[13];
} agsaSASDiagStartEndCmd_t;

/** \brief the data structure of SAS Diagnostic Execute Command
 *
 * use to describe MPI SAS Diagnostic Execute Command for SPCv (128 bytes)
 */
typedef struct agsaSASDiagExecuteCmd_s {
  bit32             tag;             /* 1 */
  bit32             CmdTypeDescPhyId;/* 2 */
  bit32             Pat1Pat2;        /* 3 */
  bit32             Threshold;       /* 4 */
  bit32             CodePatErrMsk;   /* 5 */
  bit32             Pmon;            /* 6 */
  bit32             PERF1CTL;        /* 7 */
  bit32             THRSHLD1;        /* 8 */
  bit32             reserved[23];     /* 9 31 */
} agsaSASDiagExecuteCmd_t;


/** \brief the data structure of SAS Diagnostic Execute Command
 *
 * use to describe MPI SAS Diagnostic Execute Command for SPC (64 bytes)
 */
typedef struct agsa_SPC_SASDiagExecuteCmd_s {
  bit32             tag;             /* 1 */
  bit32             CmdTypeDescPhyId;/* 2 */
  bit32             Pat1Pat2;        /* 3 */
  bit32             Threshold;       /* 4 */
  bit32             CodePatErrMsk;   /* 5 */
  bit32             Pmon;            /* 6 */
  bit32             PERF1CTL;        /* 7 */
  bit32             reserved[8];     /* 8 15 */
} agsa_SPC_SASDiagExecuteCmd_t;
#define SAS_DIAG_PARAM_BYTES 24


/** \brief the data structure of SSP TGT IO Start Command
 *
 * use to describe MPI SSP TGT IO Start Command (64 bytes)
 *
 */
typedef struct agsaSSPTgtIOStartCmd_s {
  bit32              tag;              /*  1 */
  bit32              deviceId;         /*  2 */
  bit32              dataLen;          /*  3 */
  bit32              dataOffset;       /*  4 */
  bit32              INITagAgrDir;     /*  5 */
  bit32              reserved;         /*  6 */
  bit32              DIF_flags;        /*  7 */
  bit32              udt;              /*  8 */
  bit32              udtReplacementLo; /*  9 */
  bit32              udtReplacementHi; /* 10 */
  bit32              DIF_seed;         /* 11 */
  bit32              AddrLow0;         /* 12 */
  bit32              AddrHi0;          /* 13 */
  bit32              Len0;             /* 14 */
  bit32              E0;               /* 15 */
} agsaSSPTgtIOStartCmd_t;

/** \brief the data structure of SSP TGT Response Start Command
 *
 * use to describe MPI SSP TGT Response Start Command (64 bytes)
 *
 */
typedef struct agsaSSPTgtRspStartCmd_s {
  bit32                    tag;
  bit32                    deviceId;
  bit32                    RspLen;
  bit32                    INITag_IP_AN;
  bit32                    reserved[7];
  bit32                    AddrLow0;
  bit32                    AddrHi0;
  bit32                    Len0;
  bit32                    E0;
} agsaSSPTgtRspStartCmd_t;

/** \brief the data structure of Device Handle Accept Command
 *
 * use to describe MPI Device Handle Accept Command (64 bytes)
 *
 */
typedef struct agsaDevHandleAcceptCmd_s {
  bit32                    tag;
  bit32                    Ctag;
  bit32                    deviceId;
  bit32                    DevA_MCN_R_R_HA_ITNT;
  bit32                    reserved[11];
} agsaDevHandleAcceptCmd_t;

/** \brief the data structure of SAS HW Event Ack Command
 *
 * use to describe MPI SAS HW Event Ack Command (64 bytes)
 *
 */
typedef struct agsaSASHwEventAckCmd_s {
  bit32                    tag;
  bit32                    sEaPhyIdPortId;
  bit32                    Param0;
  bit32                    Param1;
  bit32                    reserved[11];
} agsaSASHwEventAckCmd_t;

/** \brief the data structure of Get Time Stamp Command
 *
 * use to describe MPI Get Time Stamp Command (64 bytes)
 *
 */
typedef struct agsaGetTimeStampCmd_s {
  bit32                    tag;
  bit32                    reserved[14];
} agsaGetTimeStampCmd_t;

/** \brief the data structure of Port Control Command
 *
 * use to describe MPI Port Control Command (64 bytes)
 *
 */
typedef struct agsaPortControlCmd_s {
  bit32                    tag;
  bit32                    portOPPortId;
  bit32                    Param0;
  bit32                    Param1;
  bit32                    reserved[11];
} agsaPortControlCmd_t;

/** \brief the data structure of Set NVM Data Command
 *
 * use to describe MPI Set NVM Data Command (64 bytes)
 *
 */
typedef struct agNVMIndirect_s {
  bit32           signature;
  bit32           reserved[7];
  bit32           ISglAL;
  bit32           ISglAH;
  bit32           ILen;
  bit32           reserved1;
} agNVMIndirect_t;

typedef union agsaSetNVMData_s {
  bit32           NVMData[12];
  agNVMIndirect_t indirectData;
} agsaSetNVMData_t;

typedef struct agsaSetNVMDataCmd_s {
  bit32            tag;
  bit32            LEN_IR_VPDD;
  bit32            VPDOffset;
  agsaSetNVMData_t Data;
} agsaSetNVMDataCmd_t;

/** \brief the data structure of Get NVM Data Command
 *
 * use to describe MPI Get NVM Data Command (64 bytes)
 *
 */
typedef struct agsaGetNVMDataCmd_s {
  bit32           tag;
  bit32           LEN_IR_VPDD;
  bit32           VPDOffset;
  bit32           reserved[8];
  bit32           respAddrLo;
  bit32           respAddrHi;
  bit32           respLen;
  bit32           reserved1;
} agsaGetNVMDataCmd_t;

#define TWI_DEVICE 0x0
#define C_SEEPROM  0x1
#define VPD_FLASH  0x4
#define AAP1_RDUMP 0x5
#define IOP_RDUMP  0x6
#define EXPAN_ROM  0x7

#define DIRECT_MODE   0x0
#define INDIRECT_MODE 0x1

#define IRMode     0x80000000
#define IPMode     0x80000000
#define NVMD_TYPE  0x0000000F
#define NVMD_STAT  0x0000FFFF
#define NVMD_LEN   0xFF000000

#define TWI_DEVICE 0x0
#define SEEPROM    0x1

/** \brief the data structure of Set Device State Command
 *
 * use to describe MPI Set Device State Command (64 bytes)
 *
 */
typedef struct agsaSetDeviceStateCmd_s {
  bit32           tag;
  bit32           deviceId;
  bit32           NDS;
  bit32           reserved[12];
} agsaSetDeviceStateCmd_t;

#define DS_OPERATIONAL     0x01
#define DS_IN_RECOVERY     0x03
#define DS_IN_ERROR        0x04
#define DS_NON_OPERATIONAL 0x07

/** \brief the data structure of Get Device State Command
 *
 * use to describe MPI Get Device State Command (64 bytes)
 *
 */
typedef struct agsaGetDeviceStateCmd_s {
  bit32           tag;
  bit32           deviceId;
  bit32           reserved[13];
} agsaGetDeviceStateCmd_t;

/** \brief the data structure of Set Device Info Command
 *
 * use to describe MPI OPC_INB_SET_DEV_INFO (0x02c) Command (64 bytes)
 *
 */
typedef struct agsaSetDevInfoCmd_s {
  bit32             tag;
  bit32             deviceId;
  bit32             SA_SR_SI;
  bit32             DEVA_MCN_R_ITNT;
  bit32             reserved[11];
} agsaSetDevInfoCmd_t;

#define SET_DEV_INFO_V_DW3_MASK    0x0000003F
#define SET_DEV_INFO_V_DW4_MASK    0xFF07FFFF
#define SET_DEV_INFO_SPC_DW3_MASK  0x7
#define SET_DEV_INFO_SPC_DW4_MASK  0x003FFFF

#define SET_DEV_INFO_V_DW3_SM_SHIFT 3
#define SET_DEV_INFO_V_DW3_SA_SHIFT 2
#define SET_DEV_INFO_V_DW3_SR_SHIFT 1
#define SET_DEV_INFO_V_DW3_SI_SHIFT 0

#define SET_DEV_INFO_V_DW4_MCN_SHIFT     24
#define SET_DEV_INFO_V_DW4_AWT_SHIFT     17
#define SET_DEV_INFO_V_DW4_RETRY_SHIFT   16
#define SET_DEV_INFO_V_DW4_ITNEXUS_SHIFT  0

/** \brief the data structure of SAS Re_Initialize Command
 *
 * use to describe MPI SAS RE_INITIALIZE Command (64 bytes)
 *
 */
typedef struct agsaSasReInitializeCmd_s {
  bit32             tag;
  bit32             setFlags;
  bit32             MaxPorts;
  bit32             openRejReCmdData;
  bit32             sataHOLTMO;
  bit32             reserved[10];
} agsaSasReInitializeCmd_t;


/** \brief the data structure of SGPIO Command
 *
 * use to describe MPI serial GPIO Command (64 bytes)
 *
 */
typedef struct agsaSGpioCmd_s {
  bit32             tag;
  bit32             regIndexRegTypeFunctionFrameType;
  bit32             regCount;
  bit32             writeData[OSSA_SGPIO_MAX_WRITE_DATA_COUNT];
} agsaSGpioCmd_t;

/** \brief the data structure of PCIE Diagnostic Command
 *
 * use to describe MPI PCIE Diagnostic Command for SPCv (128 bytes)
 *
 */
typedef struct agsaPCIeDiagExecuteCmd_s {
  bit32    tag;           /* 1 */
  bit32    CmdTypeDesc;   /* 2 */
  bit32    UUM_EDA;       /* 3 */
  bit32    UDTR1_UDT0;    /* 4 */
  bit32    UDT5_UDT2;     /* 5 */
  bit32    UDTR5_UDTR2;   /* 6 */
  bit32    Res_IOS;       /* 7 */
  bit32    rdAddrLower;   /* 8 */
  bit32    rdAddrUpper;   /* 9 */
  bit32    wrAddrLower;   /* 10 */
  bit32    wrAddrUpper;   /* 11 */
  bit32    len;           /* 12 */
  bit32    pattern;       /* 13 */
  bit32    reserved2[2];  /* 14 15 */
  bit32    reserved3[16]; /* 15 31 */
} agsaPCIeDiagExecuteCmd_t;


/** \brief the data structure of PCI Diagnostic Command for SPC
 *
 * use to describe MPI PCI Diagnostic Command for SPC (64 bytes)
 *
 */
typedef struct agsa_SPC_PCIDiagExecuteCmd_s {
  bit32    tag;
  bit32    CmdTypeDesc;
  bit32    reserved1[5];
  bit32    rdAddrLower;
  bit32    rdAddrUpper;
  bit32    wrAddrLower;
  bit32    wrAddrUpper;
  bit32    len;
  bit32    pattern;
  bit32    reserved2[2];
} agsa_SPC_PCIDiagExecuteCmd_t;

/** \brief the data structure of GET DFE Data Command
 *
 * use to describe GET DFE Data Command for SPCv (128 bytes)
 *
 */
typedef struct agsaGetDDEFDataCmd_s {
  bit32    tag;           /* 1 */
  bit32    reserved_In_Ln;/* 2 */
  bit32    MCNT;          /* 3 */
  bit32    reserved1[3];  /* 4 - 6 */
  bit32    Buf_AddrL;     /* 7 */
  bit32    Buf_AddrH;     /* 8 */
  bit32    Buf_Len;       /* 9 */
  bit32    E_reserved;    /* 10 */
  bit32    reserved2[21]; /* 11 - 31 */
} agsaGetDDEFDataCmd_t;


/***********************************************
 * outbound IOMBs
 ***********************************************/
/** \brief the data structure of Echo Response
 *
 * use to describe MPI Echo Response (64 bytes)
 *
 */
typedef struct agsaEchoRsp_s {
  bit32           tag;
  bit32           payload[14];
} agsaEchoRsp_t;

/** \brief the data structure of HW Event from Outbound
 *
 * use to describe MPI HW Event (64 bytes)
 *
 */
typedef struct agsaHWEvent_SPC_OUB_s {
  bit32             LRStatusEventPhyIdPortId;
  bit32             EVParam;
  bit32             NpipPortState;
  agsaSASIdentify_t sasIdentify;
  agsaFisRegDeviceToHost_t sataFis;
} agsaHWEvent_SPC_OUB_t;

#define PHY_ID_BITS    0x000000F0
#define LINK_RATE_MASK 0xF0000000
#define STATUS_BITS    0x0F000000
#define HW_EVENT_BITS  0x00FFFF00

typedef struct agsaHWEvent_Phy_OUB_s {
  bit32             tag;
  bit32             Status;
  bit32             ReservedPhyId;
} agsaHWEvent_Phy_OUB_t;

/** \brief the data structure of HW Event from Outbound
 *
 * use to describe MPI HW Event (64 bytes)
 *
 */
typedef struct agsaHWEvent_V_OUB_s {
  bit32             LRStatEventPortId;
  bit32             EVParam;
  bit32             RsvPhyIdNpipRsvPortState;
  agsaSASIdentify_t sasIdentify;
  agsaFisRegDeviceToHost_t sataFis;
} agsaHWEvent_V_OUB_t;

#define PHY_ID_V_BITS  0x00FF0000
#define NIPP_V_BITS    0x0000FF00



/** \brief the data structure of SSP Completion Response
 *
 * use to describe MPI SSP Completion Response (1024 bytes)
 *
 */
typedef struct agsaSSPCompletionRsp_s {
  bit32                     tag;
  bit32                     status;
  bit32                     param;
  bit32                     SSPTag;
  agsaSSPResponseInfoUnit_t SSPrsp;
  bit32                     respData;
  bit32                     senseData[5];
  bit32                     respData1[239];
} agsaSSPCompletionRsp_t;


/** \brief the data structure of SSP Completion DIF Response
 *
 * use to describe MPI SSP Completion DIF Response (1024 bytes)
 *
 */
typedef struct agsaSSPCompletionDifRsp_s {
  bit32 tag;
  bit32 status;
  bit32 param;
  bit32 SSPTag;
  bit32 Device_Id;
  bit32 UpperLBA;
  bit32 LowerLBA;
  bit32 sasAddressHi;
  bit32 sasAddressLo;
  bit32 ExpectedCRCUDT01;
  bit32 ExpectedUDT2345;
  bit32 ActualCRCUDT01;
  bit32 ActualUDT2345;
  bit32 DIFErrDevID;
  bit32 ErrBoffsetEDataLen;
  bit32 EDATA_FRM;

} agsaSSPCompletionDifRsp_t;


/* SSPTag bit fields Bits [31:16] */
#define SSP_RESCV_BIT       0x00010000  /* Bits [16] */
#define SSP_RESCV_PAD       0x00060000  /* Bits [18:17] */
#define SSP_RESCV_PAD_SHIFT 17
#define SSP_AGR_S_BIT       (1 << 19)   /* Bits [19] */

/** \brief the data structure of SMP Completion Response
 *
 * use to describe MPI SMP Completion Response (1024 bytes)
 *
 */
typedef struct agsaSMPCompletionRsp_s {
  bit32                     tag;
  bit32                     status;
  bit32                     param;
  bit32                     SMPrsp[252];
} agsaSMPCompletionRsp_t;

/** \brief the data structure of Deregister Device Response
 *
 * use to describe MPI Deregister Device Response (64 bytes)
 *
 */
typedef struct agsaDeregDevHandleRsp_s {
  bit32                     tag;
  bit32                     status;
  bit32                     deviceId;
  bit32                     reserved[12];
} agsaDeregDevHandleRsp_t;

/** \brief the data structure of Get Device Handle Response
 *
 * use to describe MPI Get Device Handle Response (64 bytes)
 *
 */
typedef struct agsaGetDevHandleRsp_s {
  bit32                     tag;
  bit32                     DeviceIdcPortId;
  bit32                     deviceId[13];
} agsaGetDevHandleRsp_t;

#define DEVICE_IDC_BITS 0x00FFFF00
#define DEVICE_ID_BITS  0x00000FFF

/** \brief the data structure of Local Phy Control Response
 *
 * use to describe MPI Local Phy Control Response (64 bytes)
 *
 */
typedef struct agsaLocalPhyCntrlRsp_s {
  bit32                     tag;
  bit32                     phyOpId;
  bit32                     status;
  bit32                     reserved[12];
} agsaLocalPhyCntrlRsp_t;

#define LOCAL_PHY_OP_BITS 0x0000FF00
#define LOCAL_PHY_PHYID   0x000000FF

/** \brief the data structure of DEVICE_REGISTRATION Response
 *
 * use to describe device registration response (64 bytes)
 *
 */
typedef struct agsaDeviceRegistrationRsp_s {
  bit32             tag;
  bit32             status;
  bit32             deviceId;
  bit32             reserved[12];
} agsaDeviceRegistrationRsp_t;


#define FAILURE_OUT_OF_RESOURCE             0x01 /* The device registration failed because the SPC 8x6G is running out of device handle resources. The parameter DEVICE_ID is not used. */
#define FAILURE_DEVICE_ALREADY_REGISTERED   0x02 /* The device registration failed because the SPC 8x6G detected an existing device handle with a similar SAS address. The parameter DEVICE_ID contains the existing  DEVICE _ID assigned to the SAS device. */
#define FAILURE_INVALID_PHY_ID              0x03 /* Only for directly-attached SATA registration. The device registration failed because the SPC 8x6G detected an invalid (out-of-range) PHY ID. */
#define FAILURE_PHY_ID_ALREADY_REGISTERED   0x04 /* Only for directly-attached SATA registration. The device registration failed because the SPC 8x6G detected an already -registered PHY ID for a directly attached SATA drive. */
#define FAILURE_PORT_ID_OUT_OF_RANGE        0x05 /* PORT_ID specified in the REGISTER_DEVICE Command is out-of range (0-7).  */
#define FAILURE_PORT_NOT_VALID_STATE        0x06 /* The PORT_ID specified in the REGISTER_DEVICE Command is not in PORT_VALID state. */
#define FAILURE_DEVICE_TYPE_NOT_VALID       0x07 /* The device type, specified in the ‘S field in the REGISTER_DEVICE Command is not valid. */

#define MPI_ERR_DEVICE_HANDLE_UNAVAILABLE   0x1020 /* The device registration failed because the SPCv controller is running out of device handle resources. The parameter DEVICE_ID is not used. */
#define MPI_ERR_DEVICE_ALREADY_REGISTERED   0x1021 /* The device registration failed because the SPCv controller detected an existing device handle with the same SAS address. The parameter DEVICE_ID contains the existing DEVICE _ID assigned to the SAS device. */
#define MPI_ERR_DEVICE_TYPE_NOT_VALID       0x1022 /* The device type, specified in the ‘S field in the REGISTER_DEVICE_HANDLE Command (page 274) is not valid. */
#define MPI_ERR_PORT_INVALID_PORT_ID        0x1041 /* specified in the REGISTER_DEVICE_HANDLE Command (page 274) is invalid. i.e Out of supported range  */
#define MPI_ERR_PORT_STATE_NOT_VALID        0x1042 /* The PORT_ID specified in the REGISTER_DEVICE_HANDLE Command (page 274) is not in PORT_VALID state.  */
#define MPI_ERR_PORT_STATE_NOT_IN_USE       0x1043
#define MPI_ERR_PORT_OP_NOT_SUPPORTED       0x1044
#define MPI_ERR_PORT_SMP_PHY_WIDTH_EXCEED   0x1045
#define MPI_ERR_PORT_NOT_IN_CORRECT_STATE   0x1047 /*MPI_ERR_DEVICE_ACCEPT_PENDING*/


#define MPI_ERR_PHY_ID_INVALID              0x1061 /* Only for directly-attached SATA registration. The device registration failed because the SPCv controller detected an invalid (out-of-range) PHY ID. */
#define MPI_ERR_PHY_ID_ALREADY_REGISTERED   0x1062 /* Only for directly-attached SATA registration. The device registration failed because the SPCv controller detected an alreadyregistered PHY ID for a directly-attached SATA drive. */




/** \brief the data structure of SATA Completion Response
 *
 * use to describe MPI SATA Completion Response (64 bytes)
 *
 */
typedef struct agsaSATACompletionRsp_s {
  bit32                     tag;
  bit32                     status;
  bit32                     param;
  bit32                     FSATArsp;
  bit32                     respData[11];
} agsaSATACompletionRsp_t;

/** \brief the data structure of SATA Event Response
 *
 * use to describe MPI SATA Event Response (64 bytes)
 *
 */
typedef struct agsaSATAEventRsp_s {
  bit32                     tag;
  bit32                     event;
  bit32                     portId;
  bit32                     deviceId;
  bit32                     reserved[11];
} agsaSATAEventRsp_t;

/** \brief the data structure of SSP Event Response
 *
 * use to describe MPI SSP Event Response (64 bytes)
 *
 */
typedef struct agsaSSPEventRsp_s {
  bit32                     tag;
  bit32                     event;
  bit32                     portId;
  bit32                     deviceId;
  bit32                     SSPTag;
  bit32                     EVT_PARAM0_or_LBAH;
  bit32                     EVT_PARAM1_or_LBAL;
  bit32                     SAS_ADDRH;
  bit32                     SAS_ADDRL;
  bit32                     UDT1_E_UDT0_E_CRC_E;
  bit32                     UDT5_E_UDT4_E_UDT3_E_UDT2_E;
  bit32                     UDT1_A_UDT0_A_CRC_A;
  bit32                     UDT5_A_UDT4_A_UDT3_A_UDT2_A;
  bit32                     HW_DEVID_Reserved_DIF_ERR;
  bit32                     EDATA_LEN_ERR_BOFF;
  bit32                     EDATA_FRM;
} agsaSSPEventRsp_t;

#define SSPTAG_BITS 0x0000FFFF

/** \brief the data structure of Get Device Info Response
 *
 * use to describe MPI Get Device Info Response (64 bytes)
 *
 */
typedef struct agsaGetDevInfoRspSpc_s {
  bit32           tag;
  bit32           status;
  bit32           deviceId;
  bit32           dTypeSrateSMPTOArPortID;
  bit32           FirstBurstSizeITNexusTimeOut;
  bit8            sasAddrHi[4];
  bit8            sasAddrLow[4];
  bit32           reserved[8];
} agsaGetDevInfoRsp_t;

#define SMPTO_BITS     0xFFFF
#define NEXUSTO_BITS   0xFFFF
#define FIRST_BURST    0xFFFF
#define FLAG_BITS      0x3
#define LINK_RATE_BITS 0xFF
#define DEV_TYPE_BITS  0x30000000

/** \brief the data structure of Get Device Info Response V
 *
 * use to describe MPI Get Device Info Response (64 bytes)
 *
 */
typedef struct agsaGetDevInfoRspV_s {
  bit32           tag;
  bit32           status;
  bit32           deviceId;
  bit32           ARSrateSMPTimeOutPortID;
  bit32           IRMcnITNexusTimeOut;
  bit8            sasAddrHi[4];
  bit8            sasAddrLow[4];
  bit32           reserved[8];
} agsaGetDevInfoRspV_t;

#define SMPTO_VBITS     0xFFFF
#define NEXUSTO_VBITS   0xFFFF
#define FIRST_BURST_MCN 0xF
#define FLAG_VBITS      0x3
#define LINK_RATE_VBITS 0xFF
#define DEV_TYPE_VBITS  0x10000000


/** \brief the data structure of Get Phy Profile Command IOMB V
 *
 */
typedef struct agsaGetPhyProfileCmd_V_s {
  bit32           tag;
  bit32           Reserved_Ppc_SOP_PHYID;
  bit32           reserved[29];
} agsaGetPhyProfileCmd_V_t;


/** \brief the data structure of Get Phy Profile Response IOMB V
 *
 */
typedef struct agsaGetPhyProfileRspV_s {
  bit32           tag;
  bit32           status;
  bit32           Reserved_Ppc_SOP_PHYID;
  bit32           PageSpecificArea[12];
} agsaGetPhyProfileRspV_t;

/** \brief the data structure of Set Phy Profile Command IOMB V
 *
 */
typedef struct agsaSetPhyProfileCmd_V_s {
  bit32           tag;
  bit32           Reserved_Ppc_SOP_PHYID;
  bit32           PageSpecificArea[29];
} agsaSetPhyProfileCmd_V_t;

/** \brief the data structure of GetVis Command IOMB V
 *  OPC_OUB_GET_VIST_CAP_RSP
 */
typedef struct agsaGetVHistCap_V_s {
  bit32           tag;
  bit32           Channel;
  bit32           NumBitLo;
  bit32           NumBitHi;
  bit32           reserved0;
  bit32           reserved1;
  bit32           PcieAddrLo;
  bit32           PcieAddrHi;
  bit32           ByteCount;
  bit32           reserved2[22];
} agsaGetVHistCap_V_t;

/** \brief the data structure of Set Phy Profile Response IOMB V
 *
 */
typedef struct agsaSetPhyProfileRspV_s {
  bit32           tag;
  bit32           status;
  bit32           Reserved_Ppc_PHYID;
  bit32           PageSpecificArea[12];
} agsaSetPhyProfileRspV_t;

typedef struct agsaGetPhyInfoV_s {
  bit32           tag;
  bit32           Reserved_SOP_PHYID;
  bit32           reserved[28];
} agsaGetPhyInfoV_t;


#define SPC_GET_SAS_PHY_ERR_COUNTERS      1
#define SPC_GET_SAS_PHY_ERR_COUNTERS_CLR  2
#define SPC_GET_SAS_PHY_BW_COUNTERS       3


/** \brief the data structure of FW_FLASH_UPDATE Response
 *
 * use to describe MPI FW_FLASH_UPDATE Response (64 bytes)
 *
 */
typedef struct agsaFwFlashUpdateRsp_s {
  bit32             tag;
  bit32             status;
  bit32             reserved[13];
} agsaFwFlashUpdateRsp_t;

#ifdef SPC_ENABLE_PROFILE
typedef struct agsaFwProfileRsp_s {
  bit32             tag;
  bit32             status;
  bit32             len;
  bit32             reserved[12];
} agsaFwProfileRsp_t;
#endif
/** \brief the data structure of GPIO Response
 *
 * use to describe MPI GPIO Response (64 bytes)
 */
typedef struct agsaGPIORsp_s {
  bit32             tag;
  bit32             reserved[2];
  bit32             GpioRdVal;
  bit32             GpioIe;
  bit32             OT11_0;
  bit32             OT19_12;
  bit32             GPIEVChange;
  bit32             GPIEVRise;
  bit32             GPIEVFall;
  bit32             reserved1[5];
} agsaGPIORsp_t;

/** \brief the data structure of GPIO Event
 *
 * use to describe MPI GPIO Event Response (64 bytes)
 */
typedef struct agsaGPIOEvent_s {
  bit32             GpioEvent;
  bit32             reserved[14];
} agsaGPIOEvent_t;

/** \brief the data structure of GENERAL_EVENT Response
 *
 * use to describe MPI GENERNAL_EVENT Notification (64 bytes)
 *
 */
typedef struct agsaGenernalEventRsp_s {
  bit32             status;
  bit32             inboundIOMB[14];
} agsaGenernalEventRsp_t;

/** \brief the data structure of SSP_ABORT Response
 *
 * use to describe MPI SSP_ABORT (64 bytes)
 *
 */
typedef struct agsaSSPAbortRsp_s {
  bit32             tag;
  bit32             status;
  bit32             scp;
  bit32             reserved[12];
} agsaSSPAbortRsp_t;

/** \brief the data structure of SATA_ABORT Response
 *
 * use to describe MPI SATA_ABORT (64 bytes)
 *
 */
typedef struct agsaSATAAbortRsp_s {
  bit32             tag;
  bit32             status;
  bit32             scp;
  bit32             reserved[12];
} agsaSATAAbortRsp_t;

/** \brief the data structure of SAS Diagnostic Start/End Response
 *
 * use to describe MPI SAS Diagnostic Start/End Response (64 bytes)
 *
 */
typedef struct agsaSASDiagStartEndRsp_s {
  bit32             tag;
  bit32             Status;
  bit32             reserved[13];
} agsaSASDiagStartEndRsp_t;

/** \brief the data structure of SAS Diagnostic Execute Response
 *
 * use to describe MPI SAS Diagnostic Execute Response (64 bytes)
 *
 */
typedef struct agsaSASDiagExecuteRsp_s {
  bit32             tag;
  bit32             CmdTypeDescPhyId;
  bit32             Status;
  bit32             ReportData;
  bit32             reserved[11];
} agsaSASDiagExecuteRsp_t;

/** \brief the data structure of General Event Notification Response
 *
 * use to describe MPI General Event Notification Response (64 bytes)
 *
 */
typedef struct agsaGeneralEventRsp_s {
  bit32             status;
  bit32             inbIOMBpayload[14];
} agsaGeneralEventRsp_t;

#define GENERAL_EVENT_PAYLOAD 14
#define OPCODE_BITS           0x00000fff

/*
Table 171 GENERAL_EVENT Notification Status Field Codes
Value Name Description
*/
#define GEN_EVENT_IOMB_V_BIT_NOT_SET             0x01 /* INBOUND_ Inbound IOMB is received with the V bit in the IOMB header not set. */
#define GEN_EVENT_INBOUND_IOMB_OPC_NOT_SUPPORTED 0x02 /* Inbound IOMB is received with an unsupported OPC. */
#define GEN_EVENT_IOMB_INVALID_OBID              0x03 /* INBOUND Inbound IOMB is received with an invalid OBID. */
#define GEN_EVENT_DS_IN_NON_OPERATIONAL          0x39 /* DEVICE_HANDLE_ACCEPT command failed due to the device being in DS_NON_OPERATIONAL state. */
#define GEN_EVENT_DS_IN_RECOVERY                 0x3A /* DEVICE_HANDLE_ACCEPT command failed due to device being in DS_IN_RECOVERY state. */
#define GEN_EVENT_DS_INVALID                     0x49 /* DEVICE_HANDLE_ACCEPT command failed due to device being in DS_INVALID state. */

#define GEN_EVENT_IO_XFER_READ_COMPL_ERR         0x50 /* Indicates the PCIe Read Request to fetch one or more inbound IOMBs received
                                                        a failed completion response. The first and second Dwords of the
                                                        INBOUND IOMB field ( Dwords 2 and 3) contains information to identifying
                                                        the location in the inbound queue where the error occurred.
                                                        Dword 2 bits[15:0] contains the inbound queue number.
                                                        Dword 2 bits[31:16] specifies how many consecutive IOMBs were affected
                                                        by the failed DMA.
                                                        Dword 3 specifies the Consumer Index [CI] of the inbound queue where
                                                        the DMA operation failed.*/

/** \brief the data structure of SSP Request Received Notification
 *
 * use to describe MPI SSP Request Received Notification ( 1024 bytes)
 *
 */
typedef struct agsaSSPReqReceivedNotify_s {
  bit32             deviceId;
  bit32             iniTagSSPIul;
  bit32             frameTypeHssa;
  bit32             TlrHdsa;
  bit32             SSPIu[251];
} agsaSSPReqReceivedNotify_t;

#define SSPIUL_BITS  0x0000FFFF
#define INITTAG_BITS 0x0000FFFF
#define FRAME_TYPE   0x000000FF
#define TLR_BITS     0x00000300

/** \brief the data structure of Device Handle Arrived Notification
 *
 * use to describe MPI Device Handle Arrived Notification ( 64 bytes)
 *
 */
typedef struct agsaDeviceHandleArrivedNotify_s {
  bit32             CTag;
  bit32             HostAssignedIdFwdDeviceId;
  bit32             ProtConrPortId;
  bit8              sasAddrHi[4];
  bit8              sasAddrLow[4];
  bit32             reserved[10];

} agsaDeviceHandleArrivedNotify_t;


#define Conrate_V_MASK 0x0000F000
#define Conrate_V_SHIFT 12
#define Conrate_SPC_MASK  0x0000F000
#define Conrate_SPC_SHIFT 4

#define Protocol_SPC_MASK 0x00000700
#define Protocol_SPC_SHIFT 8
#define Protocol_SPC_MASK 0x00000700
#define Protocol_SPC_SHIFT 8

#define PortId_V_MASK   0xFF
#define PortId_SPC_MASK 0x0F

#define PROTOCOL_BITS        0x00000700
#define PROTOCOL_SHIFT       8

#define SHIFT_REG_64K_MASK   0xffff0000
#define SHIFT_REG_BIT_SHIFT  8
#define SPC_GSM_SM_OFFSET    0x400000
#define SPCV_GSM_SM_OFFSET   0x0

/** \brief the data structure of Get Time Stamp Response
 *
 * use to describe MPI Get TIme Stamp Response ( 64 bytes)
 *
 */
typedef struct agsaGetTimeStampRsp_s {
  bit32             tag;
  bit32             timeStampLower;
  bit32             timeStampUpper;
  bit32             reserved[12];
} agsaGetTimeStampRsp_t;

/** \brief the data structure of SAS HW Event Ack Response
 *
 * use to describe SAS HW Event Ack Response ( 64 bytes)
 *
 */
typedef struct agsaSASHwEventAckRsp_s {
  bit32             tag;
  bit32             status;
  bit32             reserved[13];
} agsaSASHwEventAckRsp_t;

/** \brief the data structure of Port Control Response
 *
 * use to describe Port Control Response ( 64 bytes)
 *
 */
typedef struct agsaPortControlRsp_s {
  bit32             tag;
  bit32             portOPPortId;
  bit32             status;
  bit32             rsvdPortState;
  bit32             reserved[11];
} agsaPortControlRsp_t;

/** \brief the data structure of SMP Abort Response
 *
 * use to describe SMP Abort Response ( 64 bytes)
 *
 */
typedef struct agsaSMPAbortRsp_s {
  bit32             tag;
  bit32             status;
  bit32             scp;
  bit32             reserved[12];
} agsaSMPAbortRsp_t;

/** \brief the data structure of Get NVMD Data Response
 *
 * use to describe MPI Get NVMD Data Response (64 bytes)
 *
 */
typedef struct agsaGetNVMDataRsp_s {
  bit32           tag;
  bit32           iRTdaBnDpsAsNvm;
  bit32           DlenStatus;
  bit32           NVMData[12];
} agsaGetNVMDataRsp_t;

/** \brief the data structure of Set NVMD Data Response
 *
 * use to describe MPI Set NVMD Data Response (64 bytes)
 *
 */
typedef struct agsaSetNVMDataRsp_s {
  bit32           tag;
  bit32           iPTdaBnDpsAsNvm;
  bit32           status;
  bit32           reserved[12];
} agsaSetNVMDataRsp_t;

/** \brief the data structure of Device Handle Removal
 *
 * use to describe MPI Device Handle Removel Notification (64 bytes)
 *
 */
typedef struct agsaDeviceHandleRemoval_s {
  bit32           portId;
  bit32           deviceId;
  bit32           reserved[13];
} agsaDeviceHandleRemoval_t;

/** \brief the data structure of Set Device State Response
 *
 * use to describe MPI Set Device State Response (64 bytes)
 *
 */
typedef struct agsaSetDeviceStateRsp_s {
  bit32           tag;
  bit32           status;
  bit32           deviceId;
  bit32           pds_nds;
  bit32           reserved[11];
} agsaSetDeviceStateRsp_t;

#define NDS_BITS 0x0F
#define PDS_BITS 0xF0

/** \brief the data structure of Get Device State Response
 *
 * use to describe MPI Get Device State Response (64 bytes)
 *
 */
typedef struct agsaGetDeviceStateRsp_s {
  bit32           tag;
  bit32           status;
  bit32           deviceId;
  bit32           ds;
  bit32           reserved[11];
} agsaGetDeviceStateRsp_t;

/** \brief the data structure of Set Device Info Response
 *
 * use to describe MPI Set Device Info Response (64 bytes)
 *
 */
typedef struct agsaSetDeviceInfoRsp_s {
  bit32           tag;
  bit32           status;
  bit32           deviceId;
  bit32           SA_SR_SI;
  bit32           A_R_ITNT;
  bit32           reserved[10];
} agsaSetDeviceInfoRsp_t;

/** \brief the data structure of SAS Re_Initialize Response
 *
 * use to describe MPI SAS RE_INITIALIZE Response (64 bytes)
 *
 */
typedef struct agsaSasReInitializeRsp_s {
  bit32             tag;
  bit32             status;
  bit32             setFlags;
  bit32             MaxPorts;
  bit32             openRejReCmdData;
  bit32             sataHOLTMO;
  bit32             reserved[9];
} agsaSasReInitializeRsp_t;

/** \brief the data structure of SGPIO Response
 *
 * use to describe MPI serial GPIO Response IOMB (64 bytes)
 *
 */
typedef struct agsaSGpioRsp_s {
  bit32             tag;
  bit32             resultFunctionFrameType;
  bit32             readData[OSSA_SGPIO_MAX_READ_DATA_COUNT];
} agsaSGpioRsp_t;


/** \brief the data structure of PCIe diag response
 *
 * use to describe PCIe diag response IOMB (64 bytes)
 *
 */

typedef struct agsaPCIeDiagExecuteRsp_s {
  bit32    tag;               /* 1 */
  bit32    CmdTypeDesc;       /* 2 */
  bit32    Status;            /* 3 */
  bit32    reservedDW4;       /* 4 */
  bit32    reservedDW5;       /* 5 */
  bit32    ERR_BLKH;          /* 6 */
  bit32    ERR_BLKL;          /* 7 */
  bit32    DWord8;            /* 8 */
  bit32    DWord9;            /* 9 */
  bit32    DWord10;           /* 10 */
  bit32    DWord11;           /* 11 */
  bit32    DIF_ERR;           /* 12 */
  bit32    reservedDW13;      /* 13 */
  bit32    reservedDW14;      /* 14 */
  bit32    reservedDW15;      /* 15 */
} agsaPCIeDiagExecuteRsp_t;

/** \brief the data structure of PCI diag response
 *
 * use to describe PCI diag response IOMB  for SPC (64 bytes)
 *
 */

typedef struct agsa_SPC_PCIeDiagExecuteRsp_s {
  bit32    tag;               /* 1 */
  bit32    CmdTypeDesc;       /* 2 */
  bit32    Status;            /* 3 */
  bit32    reserved[12];      /* 4 15 */
} agsa_SPC_PCIeDiagExecuteRsp_t;

/** \brief the data structure of GET DFE Data Response
 *
 * use to describe GET DFE Data Response for SPCv (64 bytes)
 *
 */
typedef struct agsaGetDDEFDataRsp_s {
  bit32    tag;           /* 1 */
  bit32    status;        /* 2 */
  bit32    reserved_In_Ln;/* 3 */
  bit32    MCNT;          /* 4 */
  bit32    NBT;           /* 5 */
  bit32    reserved[10];  /* 6 - 15 */
} agsaGetDDEFDataRsp_t;

/** \brief the data structure of GET Vis Data Response
 *
 * use to describe GET Vis Data Response for SPCv (64 bytes)
 *
 */
typedef struct agsaGetVHistCapRsp_s {
  bit32    tag;           /* 1 */
  bit32    status;        /* 2 */
  bit32    channel;       /* 3 */
  bit32    BistLo;        /* 4 */
  bit32    BistHi;        /* 5 */
  bit32    BytesXfered;   /* 6 */
  bit32    PciLo;         /* 7 */
  bit32    PciHi;         /* 8 */
  bit32    PciBytecount;  /* 9 */
  bit32    reserved[5];  /* 10 - 15 */
} agsaGetVHistCapRsp_t;

typedef struct agsaSetControllerConfigCmd_s {
  bit32             tag;
  bit32             pageCode;
  bit32             configPage[13];     /* Page code specific fields */
} agsaSetControllerConfigCmd_t;


typedef struct agsaSetControllerConfigRsp_s {
  bit32             tag;
  bit32             status;
  bit32             errorQualifierPage;
  bit32             reserved[12];
} agsaSetControllerConfigRsp_t;

typedef struct agsaGetControllerConfigCmd_s {
  bit32             tag;
  bit32             pageCode;
  bit32             INT_VEC_MSK0;
  bit32             INT_VEC_MSK1;
  bit32             reserved[11];
} agsaGetControllerConfigCmd_t;

typedef struct agsaGetControllerConfigRsp_s {
  bit32             tag;
  bit32             status;
  bit32             errorQualifier;
  bit32             configPage[12];     /* Page code specific fields */
} agsaGetControllerConfigRsp_t;

typedef struct agsaDekManagementCmd_s {
  bit32             tag;
  bit32             KEKIDX_Reserved_TBLS_DSOP;
  bit32             dekIndex;
  bit32             tableAddrLo;
  bit32             tableAddrHi;
  bit32             tableEntries;
  bit32             Reserved_DBF_TBL_SIZE;
} agsaDekManagementCmd_t;

typedef struct agsaDekManagementRsp_s {
  bit32             tag;
  bit32             status;
  bit32             flags;
  bit32             dekIndex;
  bit32             errorQualifier;
  bit32             reserved[12];
} agsaDekManagementRsp_t;

typedef struct agsaKekManagementCmd_s {
  bit32             tag;
  bit32             NEWKIDX_CURKIDX_KBF_Reserved_SKNV_KSOP;
  bit32             reserved;
  bit32             kekBlob[12];
} agsaKekManagementCmd_t;

typedef struct agsaKekManagementRsp_s {
  bit32             tag;
  bit32             status;
  bit32             flags;
  bit32             errorQualifier;
  bit32             reserved[12];
} agsaKekManagementRsp_t;


typedef struct agsaCoalSspComplCxt_s {
    bit32            tag;
    bit16            SSPTag;
    bit16            reserved;
} agsaCoalSspComplCxt_t;

/** \brief the data structure of SSP Completion Response
 *
 * use to describe MPI SSP Completion Response (1024 bytes)
 *
 */
typedef struct agsaSSPCoalescedCompletionRsp_s {
  bit32                     coalescedCount;
  agsaCoalSspComplCxt_t     sspComplCxt[1]; /* Open ended array */
} agsaSSPCoalescedCompletionRsp_t;


/** \brief the data structure of SATA Completion Response
 *
 * use to describe MPI SATA Completion Response (1024 bytes)
 *
 */
typedef struct agsaCoalStpComplCxt_s {
    bit32            tag;
    bit16            reserved;
} agsaCoalStpComplCxt_t;

typedef struct agsaSATACoalescedCompletionRsp_s {
  bit32                     coalescedCount;
  agsaCoalStpComplCxt_t     stpComplCxt[1]; /* Open ended array */
} agsaSATACoalescedCompletionRsp_t;


/** \brief the data structure of Operator Mangement Command
 *
 * use to describe OPR_MGMT  Command (128 bytes)
 *
 */
typedef struct  agsaOperatorMangmentCmd_s{
  bit32                tag;               /* 1 */
  bit32                OPRIDX_AUTIDX_R_KBF_PKT_OMO;/* 2 */
  bit8                 IDString_Role[32];    /*  3 10 */
#ifndef HAILEAH_HOST_6G_COMPITIBILITY_FLAG
  agsaEncryptKekBlob_t Kblob;            /* 11 22 */
#endif
  bit32                reserved[8];      /* 23 31 */
} agsaOperatorMangmentCmd_t;


/*
 *
 * use to describe OPR_MGMT Response (64 bytes)
 *
 */
typedef struct agsaOperatorMangmentRsp_s {
  bit32            tag;                    /* 1 */
  bit32            status;                 /* 2 */
  bit32            OPRIDX_AUTIDX_R_OMO;    /* 3 */
  bit32            errorQualifier;         /* 4 */
  bit32            reserved[10];           /* 5 15 */
} agsaOperatorMangmenRsp_t;

/** \brief the data structure of Set Operator Command
 *
 * use to describe Set Operator  Command (64 bytes)
 *
 */
typedef struct  agsaSetOperatorCmd_s{
  bit32                tag;               /* 1 */
  bit32                OPRIDX_PIN_ACS;    /* 2 */
  bit32                cert[10];          /* 3 12 */
  bit32                reserved[3];       /* 13 15 */
} agsaSetOperatorCmd_t;

/*
 *
 * use to describe Set Operator Response (64 bytes)
 *
 */
typedef struct agsaSetOperatorRsp_s {
  bit32            tag;                    /* 1 */
  bit32            status;                 /* 2 */
  bit32            ERR_QLFR_OPRIDX_PIN_ACS;/* 3 */
  bit32            reserved[12];           /* 4 15 */
} agsaSetOperatorRsp_t;

/** \brief the data structure of Get Operator Command
 *
 * use to describe Get Operator Command (64 bytes)
 *
 */
typedef struct  agsaGetOperatorCmd_s{
  bit32                tag;               /* 1 */
  bit32                option;            /* 2 */
  bit32                OprBufAddrLo;      /* 3 */
  bit32                OprBufAddrHi;      /* 4*/
  bit32                reserved[11];      /*5 15*/
} agsaGetOperatorCmd_t;

/*
 *
 * use to describe Get Operator Response (64 bytes)
 *
 */
typedef struct agsaGetOperatorRsp_s {
  bit32            tag;                    /* 1 */
  bit32            status;                 /* 2 */
  bit32            Num_Option;             /* 3 */
  bit32            IDString[8];            /* 4 11*/
  bit32            reserved[4];            /* 12 15*/
} agsaGetOperatorRsp_t;

/*
 *
 * use to start Encryption BIST (128 bytes)
 * 0x105
 */
typedef struct agsaEncryptBist_s {
  bit32 tag;               /* 1 */
  bit32 r_subop;           /* 2 */
  bit32 testDiscption[28]; /* 3 31 */
} agsaEncryptBist_t;

/*
 *
 * use to describe Encryption BIST Response (64 bytes)
 * 0x905
 */

typedef struct agsaEncryptBistRsp_s {
  bit32 tag;             /* 1 */
  bit32 status;          /* 2 */
  bit32 subop;           /* 3 */
  bit32 testResults[11]; /* 4 15 */
} agsaEncryptBistRsp_t;

/** \brief the data structure of DifEncOffload Command
 *
 * use to describe Set DifEncOffload Command (128 bytes)
 *
 */
typedef struct  agsaDifEncOffloadCmd_s{
  bit32                tag;                      /* 1 */
  bit32                option;                   /* 2 */
  bit32                reserved[2];              /* 3-4 */
  bit32                Src_Data_Len;             /* 5 */
  bit32                Dst_Data_Len;             /* 6 */
  bit32                flags;                    /* 7 */
  bit32                UDTR01UDT01;              /* 8 */
  bit32                UDT2345;                  /* 9 */
  bit32                UDTR2345;                 /* 10 */
  bit32                DPLR0SecCnt_IOSeed;       /* 11 */
  bit32                DPL_Addr_Lo;              /* 12 */
  bit32                DPL_Addr_Hi;              /* 13 */
  bit32                KeyIndex_CMode_KTS_ENT_R; /* 14 */
  bit32                EPLR0SecCnt_KS_ENSS;      /* 15 */
  bit32                keyTag_W0;                /* 16 */
  bit32                keyTag_W1;                /* 17 */
  bit32                tweakVal_W0;              /* 18 */
  bit32                tweakVal_W1;              /* 19 */
  bit32                tweakVal_W2;              /* 20 */
  bit32                tweakVal_W3;              /* 21 */
  bit32                EPL_Addr_Lo;              /* 22 */
  bit32                EPL_Addr_Hi;              /* 23 */
  agsaSgl_t            SrcSgl;                   /* 24-27 */
  agsaSgl_t            DstSgl;                   /* 28-31 */
} agsaDifEncOffloadCmd_t;

/*
 *
 * use to describe DIF/Encryption Offload Response (32 bytes)
 * 0x910
 */
typedef struct agsaDifEncOffloadRspV_s {
  bit32                 tag;
  bit32                 status;
  bit32                 ExpectedCRCUDT01;
  bit32                 ExpectedUDT2345;
  bit32                 ActualCRCUDT01;
  bit32                 ActualUDT2345;
  bit32                 DIFErr;
  bit32                 ErrBoffset;
} agsaDifEncOffloadRspV_t;

#endif  /*__SAMPIDEFS_H__ */
