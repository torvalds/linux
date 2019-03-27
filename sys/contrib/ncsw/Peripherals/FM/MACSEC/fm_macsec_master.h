/*
 * Copyright 2008-2015 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/******************************************************************************
 @File          fm_macsec_master.h

 @Description   FM MACSEC internal structures and definitions.
*//***************************************************************************/
#ifndef __FM_MACSEC_MASTER_H
#define __FM_MACSEC_MASTER_H

#include "error_ext.h"
#include "std_ext.h"

#include "fm_macsec.h"


#define MACSEC_ICV_SIZE             16
#define MACSEC_SECTAG_SIZE          16
#define MACSEC_SCI_SIZE             8
#define MACSEC_FCS_SIZE             4

/**************************************************************************//**
 @Description       Exceptions
*//***************************************************************************/

#define FM_MACSEC_EX_TX_SC_0       0x80000000
#define FM_MACSEC_EX_TX_SC(sc)     (FM_MACSEC_EX_TX_SC_0 >> (sc))
#define FM_MACSEC_EX_ECC           0x00000001

#define GET_EXCEPTION_FLAG(bitMask, exception, id)  switch (exception){     \
    case e_FM_MACSEC_EX_TX_SC:                                              \
        bitMask = FM_MACSEC_EX_TX_SC(id); break;                            \
    case e_FM_MACSEC_EX_ECC:                                                \
        bitMask = FM_MACSEC_EX_ECC; break;                                  \
    default: bitMask = 0;break;}

#define FM_MACSEC_USER_EX_SINGLE_BIT_ECC            0x80000000
#define FM_MACSEC_USER_EX_MULTI_BIT_ECC             0x40000000

#define GET_USER_EXCEPTION_FLAG(bitMask, exception)     switch (exception){ \
    case e_FM_MACSEC_EX_SINGLE_BIT_ECC:                                     \
        bitMask = FM_MACSEC_USER_EX_SINGLE_BIT_ECC; break;                  \
    case e_FM_MACSEC_EX_MULTI_BIT_ECC:                                      \
        bitMask = FM_MACSEC_USER_EX_MULTI_BIT_ECC; break;                   \
    default: bitMask = 0;break;}

/**************************************************************************//**
 @Description       Events
*//***************************************************************************/

#define FM_MACSEC_EV_TX_SC_0_NEXT_PN                  0x80000000
#define FM_MACSEC_EV_TX_SC_NEXT_PN(sc)                (FM_MACSEC_EV_TX_SC_0_NEXT_PN >> (sc))

#define GET_EVENT_FLAG(bitMask, event, id)      switch (event){     \
    case e_FM_MACSEC_EV_TX_SC_NEXT_PN:                              \
        bitMask = FM_MACSEC_EV_TX_SC_NEXT_PN(id); break;            \
    default: bitMask = 0;break;}

/**************************************************************************//**
 @Description       Defaults
*//***************************************************************************/
#define DEFAULT_userExceptions              (FM_MACSEC_USER_EX_SINGLE_BIT_ECC     |\
                                            FM_MACSEC_USER_EX_MULTI_BIT_ECC)

#define DEFAULT_exceptions                  (FM_MACSEC_EX_TX_SC(0)     |\
                                            FM_MACSEC_EX_TX_SC(1)      |\
                                            FM_MACSEC_EX_TX_SC(2)      |\
                                            FM_MACSEC_EX_TX_SC(3)      |\
                                            FM_MACSEC_EX_TX_SC(4)      |\
                                            FM_MACSEC_EX_TX_SC(5)      |\
                                            FM_MACSEC_EX_TX_SC(6)      |\
                                            FM_MACSEC_EX_TX_SC(7)      |\
                                            FM_MACSEC_EX_TX_SC(8)      |\
                                            FM_MACSEC_EX_TX_SC(9)      |\
                                            FM_MACSEC_EX_TX_SC(10)     |\
                                            FM_MACSEC_EX_TX_SC(11)     |\
                                            FM_MACSEC_EX_TX_SC(12)     |\
                                            FM_MACSEC_EX_TX_SC(13)     |\
                                            FM_MACSEC_EX_TX_SC(14)     |\
                                            FM_MACSEC_EX_TX_SC(15)     |\
                                            FM_MACSEC_EX_ECC          )

#define DEFAULT_events                      (FM_MACSEC_EV_TX_SC_NEXT_PN(0)   |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(1)    |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(2)    |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(3)    |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(4)    |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(5)    |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(6)    |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(7)    |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(8)    |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(9)    |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(10)   |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(11)   |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(12)   |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(13)   |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(14)   |\
                                            FM_MACSEC_EV_TX_SC_NEXT_PN(15)   )

#define DEFAULT_unknownSciFrameTreatment                e_FM_MACSEC_UNKNOWN_SCI_FRAME_TREATMENT_DISCARD_BOTH
#define DEFAULT_invalidTagsFrameTreatment               FALSE
#define DEFAULT_encryptWithNoChangedTextFrameTreatment  FALSE
#define DEFAULT_untagFrameTreatment                     e_FM_MACSEC_UNTAG_FRAME_TREATMENT_DELIVER_UNCONTROLLED_DISCARD_CONTROLLED
#define DEFAULT_changedTextWithNoEncryptFrameTreatment  FALSE
#define DEFAULT_onlyScbIsSetFrameTreatment              FALSE
#define DEFAULT_keysUnreadable                          FALSE
#define DEFAULT_normalMode                              TRUE
#define DEFAULT_sc0ReservedForPTP                       FALSE
#define DEFAULT_initNextPn                              1
#define DEFAULT_pnExhThr                                0xffffffff
#define DEFAULT_sectagOverhead                          (MACSEC_ICV_SIZE + MACSEC_SECTAG_SIZE)
#define DEFAULT_mflSubtract                             MACSEC_FCS_SIZE


/**************************************************************************//**
 @Description       Memory Mapped Registers
*//***************************************************************************/

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */

typedef _Packed struct
{
    /* MACsec configuration */
    volatile uint32_t   cfg;            /**< MACsec configuration */
    volatile uint32_t   et;             /**< MACsec EtherType */
    volatile uint8_t    res1[56];       /**< reserved */
    volatile uint32_t   mfl;            /**< Maximum Frame Length */
    volatile uint32_t   tpnet;          /**< TX Packet Number exhaustion threshold */
    volatile uint8_t    res2[56];       /**< reserved */
    volatile uint32_t   rxsca;          /**< RX SC access select */
    volatile uint8_t    res3[60];       /**< reserved */
    volatile uint32_t   txsca;          /**< TX SC access select */
    volatile uint8_t    res4[60];       /**< reserved */

    /* RX configuration, status and statistic */
    volatile uint32_t   rxsci1h;        /**< RX Secure Channel Identifier first half */
    volatile uint32_t   rxsci2h;        /**< RX Secure Channel Identifier second half */
    volatile uint8_t    res5[8];        /**< reserved */
    volatile uint32_t   ifio1hs;        /**< ifInOctets first half Statistic */
    volatile uint32_t   ifio2hs;        /**< ifInOctets second half Statistic */
    volatile uint32_t   ifiups;         /**< ifInUcastPkts Statistic */
    volatile uint8_t    res6[4];        /**< reserved */
    volatile uint32_t   ifimps;         /**< ifInMulticastPkts Statistic */
    volatile uint32_t   ifibps;         /**< ifInBroadcastPkts Statistic */
    volatile uint32_t   rxsccfg;        /**< RX Secure Channel configuration */
    volatile uint32_t   rpw;            /**< replayWindow */
    volatile uint8_t    res7[16];       /**< reserved */
    volatile uint32_t   inov1hs;        /**< InOctetsValidated first half Statistic */
    volatile uint32_t   inov2hs;        /**< InOctetsValidated second half Statistic */
    volatile uint32_t   inod1hs;        /**< InOctetsDecrypted first half Statistic */
    volatile uint32_t   inod2hs;        /**< InOctetsDecrypted second half Statistic */
    volatile uint32_t   rxscipus;       /**< RX Secure Channel InPktsUnchecked Statistic */
    volatile uint32_t   rxscipds;       /**< RX Secure Channel InPktsDelayed Statistic */
    volatile uint32_t   rxscipls;       /**< RX Secure Channel InPktsLate Statistic */
    volatile uint8_t    res8[4];        /**< reserved */
    volatile uint32_t   rxaninuss[MAX_NUM_OF_SA_PER_SC];   /**< RX AN 0-3 InNotUsingSA Statistic */
    volatile uint32_t   rxanipuss[MAX_NUM_OF_SA_PER_SC];   /**< RX AN 0-3 InPktsUnusedSA Statistic */
    _Packed struct
    {
        volatile uint32_t   rxsacs;     /**< RX Security Association configuration and status */
        volatile uint32_t   rxsanpn;    /**< RX Security Association nextPN */
        volatile uint32_t   rxsalpn;    /**< RX Security Association lowestPN */
        volatile uint32_t   rxsaipos;   /**< RX Security Association InPktsOK Statistic */
        volatile uint32_t   rxsak[4];   /**< RX Security Association key (128 bit) */
        volatile uint32_t   rxsah[4];   /**< RX Security Association hash (128 bit) */
        volatile uint32_t   rxsaipis;   /**< RX Security Association InPktsInvalid Statistic */
        volatile uint32_t   rxsaipnvs;  /**< RX Security Association InPktsNotValid Statistic */
        volatile uint8_t    res9[8];    /**< reserved */
    } _PackedType fmMacsecRxScSa[NUM_OF_SA_PER_RX_SC];

    /* TX configuration, status and statistic */
    volatile uint32_t   txsci1h;        /**< TX Secure Channel Identifier first half */
    volatile uint32_t   txsci2h;        /**< TX Secure Channel Identifier second half */
    volatile uint8_t    res10[8];        /**< reserved */
    volatile uint32_t   ifoo1hs;        /**< ifOutOctets first half Statistic */
    volatile uint32_t   ifoo2hs;        /**< ifOutOctets second half Statistic */
    volatile uint32_t   ifoups;         /**< ifOutUcastPkts Statistic */
    volatile uint32_t   opus;           /**< OutPktsUntagged Statistic */
    volatile uint32_t   ifomps;         /**< ifOutMulticastPkts Statistic */
    volatile uint32_t   ifobps;         /**< ifOutBroadcastPkts Statistic */
    volatile uint32_t   txsccfg;        /**< TX Secure Channel configuration */
    volatile uint32_t   optls;          /**< OutPktsTooLong Statistic */
    volatile uint8_t    res11[16];      /**< reserved */
    volatile uint32_t   oop1hs;         /**< OutOctetsProtected first half Statistic */
    volatile uint32_t   oop2hs;         /**< OutOctetsProtected second half Statistic */
    volatile uint32_t   ooe1hs;         /**< OutOctetsEncrypted first half Statistic */
    volatile uint32_t   ooe2hs;         /**< OutOctetsEncrypted second half Statistic */
    volatile uint8_t    res12[48];      /**< reserved */
    _Packed struct
    {
        volatile uint32_t   txsacs;     /**< TX Security Association configuration and status */
        volatile uint32_t   txsanpn;    /**< TX Security Association nextPN */
        volatile uint32_t   txsaopps;   /**< TX Security Association OutPktsProtected Statistic */
        volatile uint32_t   txsaopes;   /**< TX Security Association OutPktsEncrypted Statistic */
        volatile uint32_t   txsak[4];   /**< TX Security Association key (128 bit) */
        volatile uint32_t   txsah[4];   /**< TX Security Association hash (128 bit) */
        volatile uint8_t    res13[16];  /**< reserved */
    } _PackedType fmMacsecTxScSa[NUM_OF_SA_PER_TX_SC];
    volatile uint8_t    res14[248];     /**< reserved */

    /* Global configuration and status */
    volatile uint32_t   ip_rev1;        /**< MACsec IP Block Revision 1 register */
    volatile uint32_t   ip_rev2;        /**< MACsec IP Block Revision 2 register */
    volatile uint32_t   evr;            /**< MACsec Event Register */
    volatile uint32_t   ever;           /**< MACsec Event Enable Register */
    volatile uint32_t   evfr;           /**< MACsec Event Force Register */
    volatile uint32_t   err;            /**< MACsec Error Register */
    volatile uint32_t   erer;           /**< MACsec Error Enable Register */
    volatile uint32_t   erfr;           /**< MACsec Error Force Register */
    volatile uint8_t    res15[40];      /**< reserved */
    volatile uint32_t   meec;           /**< MACsec Memory ECC Error Capture Register */
    volatile uint32_t   idle;           /**< MACsec Idle status Register */
    volatile uint8_t    res16[184];     /**< reserved */
    /* DEBUG */
    volatile uint32_t   rxec;           /**< MACsec RX error capture Register */
    volatile uint8_t    res17[28];      /**< reserved */
    volatile uint32_t   txec;           /**< MACsec TX error capture Register */
    volatile uint8_t    res18[220];     /**< reserved */

    /* Macsec Rx global statistic */
    volatile uint32_t   ifiocp1hs;      /**< ifInOctetsCp first half Statistic */
    volatile uint32_t   ifiocp2hs;      /**< ifInOctetsCp second half Statistic */
    volatile uint32_t   ifiupcps;       /**< ifInUcastPktsCp Statistic */
    volatile uint8_t    res19[4];       /**< reserved */
    volatile uint32_t   ifioup1hs;      /**< ifInOctetsUp first half Statistic */
    volatile uint32_t   ifioup2hs;      /**< ifInOctetsUp second half Statistic */
    volatile uint32_t   ifiupups;       /**< ifInUcastPktsUp Statistic */
    volatile uint8_t    res20[4];       /**< reserved */
    volatile uint32_t   ifimpcps;       /**< ifInMulticastPktsCp Statistic */
    volatile uint32_t   ifibpcps;       /**< ifInBroadcastPktsCp Statistic */
    volatile uint32_t   ifimpups;       /**< ifInMulticastPktsUp Statistic */
    volatile uint32_t   ifibpups;       /**< ifInBroadcastPktsUp Statistic */
    volatile uint32_t   ipwts;          /**< InPktsWithoutTag Statistic */
    volatile uint32_t   ipkays;         /**< InPktsKaY Statistic */
    volatile uint32_t   ipbts;          /**< InPktsBadTag Statistic */
    volatile uint32_t   ipsnfs;         /**< InPktsSCINotFound Statistic */
    volatile uint32_t   ipuecs;         /**< InPktsUnsupportedEC Statistic */
    volatile uint32_t   ipescbs;        /**< InPktsEponSingleCopyBroadcast Statistic */
    volatile uint32_t   iptls;          /**< InPktsTooLong Statistic */
    volatile uint8_t    res21[52];      /**< reserved */

    /* Macsec Tx global statistic */
    volatile uint32_t   opds;           /**< OutPktsDiscarded Statistic */
#if (DPAA_VERSION >= 11)
    volatile uint8_t    res22[124];     /**< reserved */
    _Packed struct
    {
        volatile uint32_t   rxsak[8];   /**< RX Security Association key (128/256 bit) */
        volatile uint8_t    res23[32];  /**< reserved */
    } _PackedType rxScSaKey[NUM_OF_SA_PER_RX_SC];
    _Packed struct
    {
        volatile uint32_t   txsak[8];   /**< TX Security Association key (128/256 bit) */
        volatile uint8_t    res24[32];  /**< reserved */
    } _PackedType txScSaKey[NUM_OF_SA_PER_TX_SC];
#endif /* (DPAA_VERSION >= 11) */
} _PackedType t_FmMacsecRegs;

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */


/**************************************************************************//**
 @Description       General defines
*//***************************************************************************/

#define SCI_HIGH_MASK   0xffffffff00000000LL
#define SCI_LOW_MASK    0x00000000ffffffffLL

#define LONG_SHIFT      32

#define GET_SCI_FIRST_HALF(sci)     (uint32_t)((macsecSCI_t)((macsecSCI_t)(sci) & SCI_HIGH_MASK) >> LONG_SHIFT)
#define GET_SCI_SECOND_HALF(sci)    (uint32_t)((macsecSCI_t)(sci) & SCI_LOW_MASK)

/**************************************************************************//**
 @Description       Configuration defines
*//***************************************************************************/

/* masks */
#define CFG_UECT                        0x00000800
#define CFG_ESCBT                       0x00000400
#define CFG_USFT                        0x00000300
#define CFG_ITT                         0x00000080
#define CFG_KFT                         0x00000040
#define CFG_UFT                         0x00000030
#define CFG_KSS                         0x00000004
#define CFG_BYPN                        0x00000002
#define CFG_S0I                         0x00000001

#define ET_TYPE                         0x0000ffff

#define MFL_MAX_LEN                     0x0000ffff

#define RXSCA_SC_SEL                    0x0000000f

#define TXSCA_SC_SEL                    0x0000000f

#define IP_REV_1_IP_ID                  0xffff0000
#define IP_REV_1_IP_MJ                  0x0000ff00
#define IP_REV_1_IP_MM                  0x000000ff

#define IP_REV_2_IP_INT                 0x00ff0000
#define IP_REV_2_IP_ERR                 0x0000ff00
#define IP_REV_2_IP_CFG                 0x000000ff

#define MECC_CAP                        0x80000000
#define MECC_CET                        0x40000000
#define MECC_SERCNT                     0x00ff0000
#define MECC_MEMADDR                    0x000001ff

/* shifts */
#define CFG_UECT_SHIFT                  (31-20)
#define CFG_ESCBT_SHIFT                 (31-21)
#define CFG_USFT_SHIFT                  (31-23)
#define CFG_ITT_SHIFT                   (31-24)
#define CFG_KFT_SHIFT                   (31-25)
#define CFG_UFT_SHIFT                   (31-27)
#define CFG_KSS_SHIFT                   (31-29)
#define CFG_BYPN_SHIFT                  (31-30)
#define CFG_S0I_SHIFT                   (31-31)

#define IP_REV_1_IP_ID_SHIFT            (31-15)
#define IP_REV_1_IP_MJ_SHIFT            (31-23)
#define IP_REV_1_IP_MM_SHIFT            (31-31)

#define IP_REV_2_IP_INT_SHIFT           (31-15)
#define IP_REV_2_IP_ERR_SHIFT           (31-23)
#define IP_REV_2_IP_CFG_SHIFT           (31-31)

#define MECC_CAP_SHIFT                  (31-0)
#define MECC_CET_SHIFT                  (31-1)
#define MECC_SERCNT_SHIFT               (31-15)
#define MECC_MEMADDR_SHIFT              (31-31)

/**************************************************************************//**
 @Description       RX SC defines
*//***************************************************************************/

/* masks */
#define RX_SCCFG_SCI_EN_MASK            0x00000800
#define RX_SCCFG_RP_MASK                0x00000400
#define RX_SCCFG_VF_MASK                0x00000300
#define RX_SCCFG_CO_MASK                0x0000003f

/* shifts */
#define RX_SCCFG_SCI_EN_SHIFT           (31-20)
#define RX_SCCFG_RP_SHIFT               (31-21)
#define RX_SCCFG_VF_SHIFT               (31-23)
#define RX_SCCFG_CO_SHIFT               (31-31)
#define RX_SCCFG_CS_SHIFT               (31-7)

/**************************************************************************//**
 @Description       RX SA defines
*//***************************************************************************/

/* masks */
#define RX_SACFG_ACTIVE                 0x80000000
#define RX_SACFG_AN_MASK                0x00000006
#define RX_SACFG_EN_MASK                0x00000001

/* shifts */
#define RX_SACFG_AN_SHIFT               (31-30)
#define RX_SACFG_EN_SHIFT               (31-31)

/**************************************************************************//**
 @Description       TX SC defines
*//***************************************************************************/

/* masks */
#define TX_SCCFG_AN_MASK                0x000c0000
#define TX_SCCFG_ASA_MASK               0x00020000
#define TX_SCCFG_SCE_MASK               0x00010000
#define TX_SCCFG_CO_MASK                0x00003f00
#define TX_SCCFG_CE_MASK                0x00000010
#define TX_SCCFG_PF_MASK                0x00000008
#define TX_SCCFG_AIS_MASK               0x00000004
#define TX_SCCFG_UES_MASK               0x00000002
#define TX_SCCFG_USCB_MASK              0x00000001

/* shifts */
#define TX_SCCFG_AN_SHIFT               (31-13)
#define TX_SCCFG_ASA_SHIFT              (31-14)
#define TX_SCCFG_SCE_SHIFT              (31-15)
#define TX_SCCFG_CO_SHIFT               (31-23)
#define TX_SCCFG_CE_SHIFT               (31-27)
#define TX_SCCFG_PF_SHIFT               (31-28)
#define TX_SCCFG_AIS_SHIFT              (31-29)
#define TX_SCCFG_UES_SHIFT              (31-30)
#define TX_SCCFG_USCB_SHIFT             (31-31)
#define TX_SCCFG_CS_SHIFT               (31-7)

/**************************************************************************//**
 @Description       TX SA defines
*//***************************************************************************/

/* masks */
#define TX_SACFG_ACTIVE                 0x80000000


typedef struct
{
    void        (*f_Isr) (t_Handle h_Arg, uint32_t id);
    t_Handle    h_SrcHandle;
} t_FmMacsecIntrSrc;

typedef struct
{
    e_FmMacsecUnknownSciFrameTreatment  unknownSciTreatMode;
    bool                                invalidTagsDeliverUncontrolled;
    bool                                changedTextWithNoEncryptDeliverUncontrolled;
    bool                                onlyScbIsSetDeliverUncontrolled;
    bool                                encryptWithNoChangedTextDiscardUncontrolled;
    e_FmMacsecUntagFrameTreatment       untagTreatMode;
    uint32_t                            pnExhThr;
    bool                                keysUnreadable;
    bool                                byPassMode;
    bool                                reservedSc0;
    uint32_t                            sectagOverhead;
    uint32_t                            mflSubtract;
} t_FmMacsecDriverParam;

typedef struct
{
    t_FmMacsecControllerDriver      fmMacsecControllerDriver;
    t_Handle                        h_Fm;
    t_FmMacsecRegs                  *p_FmMacsecRegs;
    t_Handle                        h_FmMac;            /**< A handle to the FM MAC object  related to */
    char                            fmMacsecModuleName[MODULE_NAME_SIZE];
    t_FmMacsecIntrSrc               intrMng[NUM_OF_INTER_MODULE_EVENTS];
    uint32_t                        events;
    uint32_t                        exceptions;
    uint32_t                        userExceptions;
    t_FmMacsecExceptionsCallback    *f_Exception;       /**< Exception Callback Routine         */
    t_Handle                        h_App;              /**< A handle to an application layer object; This handle will
                                                             be passed by the driver upon calling the above callbacks */
    bool                            rxScTable[NUM_OF_RX_SC];
    uint32_t                        numRxScAvailable;
    bool                            txScTable[NUM_OF_TX_SC];
    uint32_t                        numTxScAvailable;
    t_Handle                        rxScSpinLock;
    t_Handle                        txScSpinLock;
    t_FmMacsecDriverParam           *p_FmMacsecDriverParam;
} t_FmMacsec;


#endif /* __FM_MACSEC_MASTER_H */
