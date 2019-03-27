/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
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


/**************************************************************************//**
 @File          fm_mac_ext.h

 @Description   FM MAC ...
*//***************************************************************************/
#ifndef __FM_MAC_EXT_H
#define __FM_MAC_EXT_H

#include "std_ext.h"
#include "enet_ext.h"


/**************************************************************************//**

 @Group         FM_grp Frame Manager API

 @Description   FM API functions, definitions and enums

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         FM_mac_grp FM MAC

 @Description   FM MAC API functions, definitions and enums

 @{
*//***************************************************************************/

#define FM_MAC_NO_PFC   0xff


/**************************************************************************//**
 @Description   FM MAC Exceptions
*//***************************************************************************/
typedef enum e_FmMacExceptions {
    e_FM_MAC_EX_10G_MDIO_SCAN_EVENTMDIO = 0                     /**< 10GEC MDIO scan event interrupt */
   ,e_FM_MAC_EX_10G_MDIO_CMD_CMPL                               /**< 10GEC MDIO command completion interrupt */
   ,e_FM_MAC_EX_10G_REM_FAULT                                   /**< 10GEC, mEMAC Remote fault interrupt */
   ,e_FM_MAC_EX_10G_LOC_FAULT                                   /**< 10GEC, mEMAC Local fault interrupt */
   ,e_FM_MAC_EX_10G_1TX_ECC_ER                                  /**< 10GEC, mEMAC Transmit frame ECC error interrupt */
   ,e_FM_MAC_EX_10G_TX_FIFO_UNFL                                /**< 10GEC, mEMAC Transmit FIFO underflow interrupt */
   ,e_FM_MAC_EX_10G_TX_FIFO_OVFL                                /**< 10GEC, mEMAC Transmit FIFO overflow interrupt */
   ,e_FM_MAC_EX_10G_TX_ER                                       /**< 10GEC Transmit frame error interrupt */
   ,e_FM_MAC_EX_10G_RX_FIFO_OVFL                                /**< 10GEC, mEMAC Receive FIFO overflow interrupt */
   ,e_FM_MAC_EX_10G_RX_ECC_ER                                   /**< 10GEC, mEMAC Receive frame ECC error interrupt */
   ,e_FM_MAC_EX_10G_RX_JAB_FRM                                  /**< 10GEC Receive jabber frame interrupt */
   ,e_FM_MAC_EX_10G_RX_OVRSZ_FRM                                /**< 10GEC Receive oversized frame interrupt */
   ,e_FM_MAC_EX_10G_RX_RUNT_FRM                                 /**< 10GEC Receive runt frame interrupt */
   ,e_FM_MAC_EX_10G_RX_FRAG_FRM                                 /**< 10GEC Receive fragment frame interrupt */
   ,e_FM_MAC_EX_10G_RX_LEN_ER                                   /**< 10GEC Receive payload length error interrupt */
   ,e_FM_MAC_EX_10G_RX_CRC_ER                                   /**< 10GEC Receive CRC error interrupt */
   ,e_FM_MAC_EX_10G_RX_ALIGN_ER                                 /**< 10GEC Receive alignment error interrupt */
   ,e_FM_MAC_EX_1G_BAB_RX                                       /**< dTSEC Babbling receive error */
   ,e_FM_MAC_EX_1G_RX_CTL                                       /**< dTSEC Receive control (pause frame) interrupt */
   ,e_FM_MAC_EX_1G_GRATEFUL_TX_STP_COMPLET                      /**< dTSEC Graceful transmit stop complete */
   ,e_FM_MAC_EX_1G_BAB_TX                                       /**< dTSEC Babbling transmit error */
   ,e_FM_MAC_EX_1G_TX_CTL                                       /**< dTSEC Transmit control (pause frame) interrupt */
   ,e_FM_MAC_EX_1G_TX_ERR                                       /**< dTSEC Transmit error */
   ,e_FM_MAC_EX_1G_LATE_COL                                     /**< dTSEC Late collision */
   ,e_FM_MAC_EX_1G_COL_RET_LMT                                  /**< dTSEC Collision retry limit */
   ,e_FM_MAC_EX_1G_TX_FIFO_UNDRN                                /**< dTSEC Transmit FIFO underrun */
   ,e_FM_MAC_EX_1G_MAG_PCKT                                     /**< dTSEC Magic Packet detection */
   ,e_FM_MAC_EX_1G_MII_MNG_RD_COMPLET                           /**< dTSEC MII management read completion */
   ,e_FM_MAC_EX_1G_MII_MNG_WR_COMPLET                           /**< dTSEC MII management write completion */
   ,e_FM_MAC_EX_1G_GRATEFUL_RX_STP_COMPLET                      /**< dTSEC Graceful receive stop complete */
   ,e_FM_MAC_EX_1G_TX_DATA_ERR                                  /**< dTSEC Internal data error on transmit */
   ,e_FM_MAC_EX_1G_RX_DATA_ERR                                  /**< dTSEC Internal data error on receive */
   ,e_FM_MAC_EX_1G_1588_TS_RX_ERR                               /**< dTSEC Time-Stamp Receive Error */
   ,e_FM_MAC_EX_1G_RX_MIB_CNT_OVFL                              /**< dTSEC MIB counter overflow */
   ,e_FM_MAC_EX_TS_FIFO_ECC_ERR                                 /**< mEMAC Time-stamp FIFO ECC error interrupt;
                                                                     not supported on T4240/B4860 rev1 chips */
   ,e_FM_MAC_EX_MAGIC_PACKET_INDICATION = e_FM_MAC_EX_1G_MAG_PCKT
                                                                /**< mEMAC Magic Packet Indication Interrupt */
} e_FmMacExceptions;

/**************************************************************************//**
 @Description   TM MAC statistics level
*//***************************************************************************/
typedef enum e_FmMacStatisticsLevel {
    e_FM_MAC_NONE_STATISTICS = 0,       /**< No statistics */
    e_FM_MAC_PARTIAL_STATISTICS,        /**< Only error counters are available; Optimized for performance */
    e_FM_MAC_FULL_STATISTICS            /**< All counters available; Not optimized for performance */
} e_FmMacStatisticsLevel;


#if (DPAA_VERSION >= 11)
/**************************************************************************//**
 @Description   Priority Flow Control Parameters
*//***************************************************************************/
typedef struct t_FmMacPfcParams {
    bool        pfcEnable;                                      /**< Enable/Disable PFC */

    uint16_t    pauseQuanta[FM_MAX_NUM_OF_PFC_PRIORITIES];      /**< Pause Quanta per priority to be sent in a pause frame. Each quanta represents a 512 bit-times*/

    uint16_t    pauseThresholdQuanta[FM_MAX_NUM_OF_PFC_PRIORITIES];/**< Pause threshold per priority, when timer passes this threshold time a PFC frames is sent again if the port is still congested or BM pool in depletion*/


} t_FmMacPfcParams;
#endif /* (DPAA_VERSION >= 11) */

/**************************************************************************//**
 @Function      t_FmMacExceptionCallback

 @Description   Fm Mac Exception Callback from FM MAC to the user

 @Param[in]     h_App             - Handle to the upper layer handler

 @Param[in]     exceptions        - The exception that occurred

 @Return        void.
*//***************************************************************************/
typedef void (t_FmMacExceptionCallback)(t_Handle h_App, e_FmMacExceptions exceptions);


/**************************************************************************//**
 @Description   TM MAC statistics rfc3635
*//***************************************************************************/
typedef struct t_FmMacStatistics {
/* RMON */
    uint64_t  eStatPkts64;             /**< r-10G tr-DT 64 byte frame counter */
    uint64_t  eStatPkts65to127;        /**< r-10G 65 to 127 byte frame counter */
    uint64_t  eStatPkts128to255;       /**< r-10G 128 to 255 byte frame counter */
    uint64_t  eStatPkts256to511;       /**< r-10G 256 to 511 byte frame counter */
    uint64_t  eStatPkts512to1023;      /**< r-10G 512 to 1023 byte frame counter */
    uint64_t  eStatPkts1024to1518;     /**< r-10G 1024 to 1518 byte frame counter */
    uint64_t  eStatPkts1519to1522;     /**< r-10G 1519 to 1522 byte good frame count */
/* */
    uint64_t  eStatFragments;          /**< Total number of packets that were less than 64 octets long with a wrong CRC.*/
    uint64_t  eStatJabbers;            /**< Total number of packets longer than valid maximum length octets */
    uint64_t  eStatsDropEvents;        /**< number of dropped packets due to internal errors of the MAC Client (during receive). */
    uint64_t  eStatCRCAlignErrors;     /**< Incremented when frames of correct length but with CRC error are received.*/
    uint64_t  eStatUndersizePkts;      /**< Incremented for frames under 64 bytes with a valid FCS and otherwise well formed;
                                            This count does not include range length errors */
    uint64_t  eStatOversizePkts;       /**< Incremented for frames which exceed 1518 (non VLAN) or 1522 (VLAN) and contains
                                            a valid FCS and otherwise well formed */
/* Pause */
    uint64_t  teStatPause;             /**< Pause MAC Control received */
    uint64_t  reStatPause;             /**< Pause MAC Control sent */
/* MIB II */
    uint64_t  ifInOctets;              /**< Total number of byte received. */
    uint64_t  ifInPkts;                /**< Total number of packets received.*/
    uint64_t  ifInUcastPkts;           /**< Total number of unicast frame received;
                                            NOTE: this counter is not supported on dTSEC MAC */
    uint64_t  ifInMcastPkts;           /**< Total number of multicast frame received*/
    uint64_t  ifInBcastPkts;           /**< Total number of broadcast frame received */
    uint64_t  ifInDiscards;            /**< Frames received, but discarded due to problems within the MAC RX. */
    uint64_t  ifInErrors;              /**< Number of frames received with error:
                                               - FIFO Overflow Error
                                               - CRC Error
                                               - Frame Too Long Error
                                               - Alignment Error
                                               - The dedicated Error Code (0xfe, not a code error) was received */
    uint64_t  ifOutOctets;             /**< Total number of byte sent. */
    uint64_t  ifOutPkts;               /**< Total number of packets sent .*/
    uint64_t  ifOutUcastPkts;          /**< Total number of unicast frame sent;
                                            NOTE: this counter is not supported on dTSEC MAC */
    uint64_t  ifOutMcastPkts;          /**< Total number of multicast frame sent */
    uint64_t  ifOutBcastPkts;          /**< Total number of multicast frame sent */
    uint64_t  ifOutDiscards;           /**< Frames received, but discarded due to problems within the MAC TX N/A!.*/
    uint64_t  ifOutErrors;             /**< Number of frames transmitted with error:
                                               - FIFO Overflow Error
                                               - FIFO Underflow Error
                                               - Other */
} t_FmMacStatistics;


/**************************************************************************//**
 @Group         FM_mac_init_grp FM MAC Initialization Unit

 @Description   FM MAC Initialization Unit

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   FM MAC config input
*//***************************************************************************/
typedef struct t_FmMacParams {
    uintptr_t                   baseAddr;           /**< Base of memory mapped FM MAC registers */
    t_EnetAddr                  addr;               /**< MAC address of device; First octet is sent first */
    uint8_t                     macId;              /**< MAC ID;
                                                         numbering of dTSEC and 1G-mEMAC:
                                                            0 - FM_MAX_NUM_OF_1G_MACS;
                                                         numbering of 10G-MAC (TGEC) and 10G-mEMAC:
                                                            0 - FM_MAX_NUM_OF_10G_MACS */
    e_EnetMode                  enetMode;           /**< Ethernet operation mode (MAC-PHY interface and speed);
                                                         Note that the speed should indicate the maximum rate that
                                                         this MAC should support rather than the actual speed;
                                                         i.e. user should use the FM_MAC_AdjustLink() routine to
                                                         provide accurate speed;
                                                         In case of mEMAC RGMII mode, the MAC is configured to RGMII
                                                         automatic mode, where actual speed/duplex mode information
                                                         is provided by PHY automatically in-band; FM_MAC_AdjustLink()
                                                         function should be used to switch to manual RGMII speed/duplex mode
                                                         configuration if RGMII PHY doesn't support in-band status signaling;
                                                         In addition, in mEMAC, in case where user is using the higher MACs
                                                         (i.e. the MACs that should support 10G), user should pass here
                                                         speed=10000 even if the interface is not allowing that (e.g. SGMII). */
    t_Handle                    h_Fm;               /**< A handle to the FM object this port related to */
    int                         mdioIrq;            /**< MDIO exceptions interrupt source - not valid for all
                                                         MACs; MUST be set to 'NO_IRQ' for MACs that don't have
                                                         mdio-irq, or for polling */
    t_FmMacExceptionCallback    *f_Event;           /**< MDIO Events Callback Routine         */
    t_FmMacExceptionCallback    *f_Exception;       /**< Exception Callback Routine         */
    t_Handle                    h_App;              /**< A handle to an application layer object; This handle will
                                                         be passed by the driver upon calling the above callbacks */
} t_FmMacParams;


/**************************************************************************//**
 @Function      FM_MAC_Config

 @Description   Creates descriptor for the FM MAC module.

                The routine returns a handle (descriptor) to the FM MAC object.
                This descriptor must be passed as first parameter to all other
                FM MAC function calls.

                No actual initialization or configuration of FM MAC hardware is
                done by this routine.

 @Param[in]     p_FmMacParam   - Pointer to data structure of parameters

 @Retval        Handle to FM MAC object, or NULL for Failure.
*//***************************************************************************/
t_Handle FM_MAC_Config(t_FmMacParams *p_FmMacParam);

/**************************************************************************//**
 @Function      FM_MAC_Init

 @Description   Initializes the FM MAC module

 @Param[in]     h_FmMac - FM module descriptor

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error  FM_MAC_Init(t_Handle h_FmMac);

/**************************************************************************//**
 @Function      FM_Free

 @Description   Frees all resources that were assigned to FM MAC module.

                Calling this routine invalidates the descriptor.

 @Param[in]     h_FmMac - FM module descriptor

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error  FM_MAC_Free(t_Handle h_FmMac);


/**************************************************************************//**
 @Group         FM_mac_advanced_init_grp    FM MAC Advanced Configuration Unit

 @Description   Configuration functions used to change default values.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      FM_MAC_ConfigResetOnInit

 @Description   Tell the driver whether to reset the FM MAC before initialization or
                not. It changes the default configuration [DEFAULT_resetOnInit].

 @Param[in]     h_FmMac    A handle to a FM MAC Module.
 @Param[in]     enable     When TRUE, FM will be reset before any initialization.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Config() and before FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_ConfigResetOnInit(t_Handle h_FmMac, bool enable);

/**************************************************************************//**
 @Function      FM_MAC_ConfigLoopback

 @Description   Enable/Disable internal loopback mode

 @Param[in]     h_FmMac    A handle to a FM MAC Module.
 @Param[in]     enable     TRUE to enable or FALSE to disable.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Config() and before FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_ConfigLoopback(t_Handle h_FmMac, bool enable);

/**************************************************************************//**
 @Function      FM_MAC_ConfigMaxFrameLength

 @Description   Setup maximum Rx Frame Length (in 1G MAC, effects also Tx)

 @Param[in]     h_FmMac    A handle to a FM MAC Module.
 @Param[in]     newVal     MAX Frame length

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Config() and before FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_ConfigMaxFrameLength(t_Handle h_FmMac, uint16_t newVal);

/**************************************************************************//**
 @Function      FM_MAC_ConfigWan

 @Description   ENABLE WAN mode in 10G-MAC

 @Param[in]     h_FmMac    A handle to a FM MAC Module.
 @Param[in]     enable     TRUE to enable or FALSE to disable.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Config() and before FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_ConfigWan(t_Handle h_FmMac, bool enable);

/**************************************************************************//**
 @Function      FM_MAC_ConfigPadAndCrc

 @Description   Config PAD and CRC mode

 @Param[in]     h_FmMac    A handle to a FM MAC Module.
 @Param[in]     enable     TRUE to enable or FALSE to disable.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Config() and before FM_MAC_Init().
                Not supported on 10G-MAC (i.e. CRC & PAD are added automatically
                by HW); on mEMAC, this routine supports only PAD (i.e. CRC is
                added automatically by HW).
*//***************************************************************************/
t_Error FM_MAC_ConfigPadAndCrc(t_Handle h_FmMac, bool enable);

/**************************************************************************//**
 @Function      FM_MAC_ConfigHalfDuplex

 @Description   Config Half Duplex Mode

 @Param[in]     h_FmMac    A handle to a FM MAC Module.
 @Param[in]     enable     TRUE to enable or FALSE to disable.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Config() and before FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_ConfigHalfDuplex(t_Handle h_FmMac, bool enable);

/**************************************************************************//**
 @Function      FM_MAC_ConfigTbiPhyAddr

 @Description   Configures the address of internal TBI PHY.

 @Param[in]     h_FmMac    A handle to a FM MAC Module.
 @Param[in]     newVal     TBI PHY address (1-31).

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Config() and before FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_ConfigTbiPhyAddr(t_Handle h_FmMac, uint8_t newVal);

/**************************************************************************//**
 @Function      FM_MAC_ConfigLengthCheck

 @Description   Configure the frame length checking.

 @Param[in]     h_FmMac    A handle to a FM MAC Module.
 @Param[in]     enable     TRUE to enable or FALSE to disable.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Config() and before FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_ConfigLengthCheck(t_Handle h_FmMac, bool enable);

/**************************************************************************//**
 @Function      FM_MAC_ConfigException

 @Description   Change Exception selection from default

 @Param[in]     h_FmMac         A handle to a FM MAC Module.
 @Param[in]     ex              Type of the desired exceptions
 @Param[in]     enable          TRUE to enable the specified exception, FALSE to disable it.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Config() and before FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_ConfigException(t_Handle h_FmMac, e_FmMacExceptions ex, bool enable);

#ifdef FM_TX_ECC_FRMS_ERRATA_10GMAC_A004
t_Error FM_MAC_ConfigSkipFman11Workaround (t_Handle h_FmMac);
#endif /* FM_TX_ECC_FRMS_ERRATA_10GMAC_A004 */
/** @} */ /* end of FM_mac_advanced_init_grp group */
/** @} */ /* end of FM_mac_init_grp group */


/**************************************************************************//**
 @Group         FM_mac_runtime_control_grp FM MAC Runtime Control Unit

 @Description   FM MAC Runtime control unit API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      FM_MAC_Enable

 @Description   Enable the MAC

 @Param[in]     h_FmMac    A handle to a FM MAC Module.
 @Param[in]     mode       Mode of operation (RX, TX, Both)

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_Enable(t_Handle h_FmMac,  e_CommMode mode);

/**************************************************************************//**
 @Function      FM_MAC_Disable

 @Description   DISABLE the MAC

 @Param[in]     h_FmMac    A handle to a FM MAC Module.
 @Param[in]     mode       Define what part to Disable (RX,  TX or BOTH)

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_Disable(t_Handle h_FmMac, e_CommMode mode);

/**************************************************************************//**
 @Function      FM_MAC_Resume

 @Description   Re-init the MAC after suspend

 @Param[in]     h_FmMac    A handle to a FM MAC Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_Resume(t_Handle h_FmMac);

/**************************************************************************//**
 @Function      FM_MAC_Enable1588TimeStamp

 @Description   Enables the TSU operation.

 @Param[in]     h_Fm   - Handle to the PTP as returned from the FM_MAC_PtpConfig.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_Enable1588TimeStamp(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FM_MAC_Disable1588TimeStamp

 @Description   Disables the TSU operation.

 @Param[in]     h_Fm   - Handle to the PTP as returned from the FM_MAC_PtpConfig.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_Disable1588TimeStamp(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FM_MAC_SetTxAutoPauseFrames

 @Description   Enable/Disable transmission of Pause-Frames.
                The routine changes the default configuration [DEFAULT_TX_PAUSE_TIME].

 @Param[in]     h_FmMac       -  A handle to a FM MAC Module.
 @Param[in]     pauseTime     -  Pause quanta value used with transmitted pause frames.
                                 Each quanta represents a 512 bit-times; Note that '0'
                                 as an input here will be used as disabling the
                                 transmission of the pause-frames.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_SetTxAutoPauseFrames(t_Handle h_FmMac,
                                    uint16_t pauseTime);

 /**************************************************************************//**
 @Function      FM_MAC_SetTxPauseFrames

 @Description   Enable/Disable transmission of Pause-Frames.
                The routine changes the default configuration:
                pause-time - [DEFAULT_TX_PAUSE_TIME]
                threshold-time - [0]

 @Param[in]     h_FmMac       -  A handle to a FM MAC Module.
 @Param[in]     priority      -  the PFC class of service; use 'FM_MAC_NO_PFC'
                                 to indicate legacy pause support (i.e. no PFC).
 @Param[in]     pauseTime     -  Pause quanta value used with transmitted pause frames.
                                 Each quanta represents a 512 bit-times;
                                 Note that '0' as an input here will be used as disabling the
                                 transmission of the pause-frames.
 @Param[in]     threshTime    -  Pause Threshold equanta value used by the MAC to retransmit pause frame.
                                 if the situation causing a pause frame to be sent didn't finish when the timer
                                 reached the threshold quanta, the MAC will retransmit the pause frame.
                                 Each quanta represents a 512 bit-times.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Init().
                In order for PFC to work properly the user must configure
                TNUM-aging in the tx-port it is recommended that pre-fetch and
                rate limit in the tx port should be disabled;
                PFC is supported only on new mEMAC; i.e. in MACs that don't have
                PFC support (10G-MAC and dTSEC), user should use 'FM_MAC_NO_PFC'
                in the 'priority' field.
*//***************************************************************************/
t_Error FM_MAC_SetTxPauseFrames(t_Handle h_FmMac,
                                uint8_t  priority,
                                uint16_t pauseTime,
                                uint16_t threshTime);

/**************************************************************************//**
 @Function      FM_MAC_SetRxIgnorePauseFrames

 @Description   Enable/Disable ignoring of Pause-Frames.

 @Param[in]     h_FmMac    - A handle to a FM MAC Module.
 @Param[in]     en         - boolean indicates whether to ignore the incoming pause
                             frames or not.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_SetRxIgnorePauseFrames(t_Handle h_FmMac, bool en);

/**************************************************************************//**
 @Function      FM_MAC_SetWakeOnLan

 @Description   Enable/Disable Wake On Lan support

 @Param[in]     h_FmMac    - A handle to a FM MAC Module.
 @Param[in]     en         - boolean indicates whether to enable Wake On Lan
                             support or not.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_SetWakeOnLan(t_Handle h_FmMac, bool en);

/**************************************************************************//**
 @Function      FM_MAC_ResetCounters

 @Description   reset all statistics counters

 @Param[in]     h_FmMac    - A handle to a FM MAC Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_ResetCounters(t_Handle h_FmMac);

/**************************************************************************//**
 @Function      FM_MAC_SetException

 @Description   Enable/Disable a specific Exception

 @Param[in]     h_FmMac        - A handle to a FM MAC Module.
 @Param[in]     ex             - Type of the desired exceptions
 @Param[in]     enable         - TRUE to enable the specified exception, FALSE to disable it.


 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_SetException(t_Handle h_FmMac, e_FmMacExceptions ex, bool enable);

/**************************************************************************//**
 @Function      FM_MAC_SetStatistics

 @Description   Define Statistics level.
                Where applicable, the routine also enables the MIB counters
                overflow interrupt in order to keep counters accurate
                and account for overflows.
                This routine is relevant only for dTSEC.

 @Param[in]     h_FmMac         - A handle to a FM MAC Module.
 @Param[in]     statisticsLevel - Full statistics level provides all standard counters but may
                                  reduce performance. Partial statistics provides only special
                                  event counters (errors etc.). If selected, regular counters (such as
                                  byte/packet) will be invalid and will return -1.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_SetStatistics(t_Handle h_FmMac, e_FmMacStatisticsLevel statisticsLevel);

/**************************************************************************//**
 @Function      FM_MAC_GetStatistics

 @Description   get all statistics counters

 @Param[in]     h_FmMac       -  A handle to a FM MAC Module.
 @Param[in]     p_Statistics  -  Structure with statistics

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
t_Error FM_MAC_GetStatistics(t_Handle h_FmMac, t_FmMacStatistics *p_Statistics);

/**************************************************************************//**
 @Function      FM_MAC_ModifyMacAddr

 @Description   Replace the main MAC Address

 @Param[in]     h_FmMac     -   A handle to a FM Module.
 @Param[in]     p_EnetAddr  -   Ethernet Mac address

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only after FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_ModifyMacAddr(t_Handle h_FmMac, t_EnetAddr *p_EnetAddr);

/**************************************************************************//**
 @Function      FM_MAC_AddHashMacAddr

 @Description   Add an Address to the hash table. This is for filter purpose only.

 @Param[in]     h_FmMac     -   A handle to a FM Module.
 @Param[in]     p_EnetAddr  -   Ethernet Mac address

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Init(). It is a filter only address.
 @Cautions      Some address need to be filterd out in upper FM blocks.
*//***************************************************************************/
t_Error FM_MAC_AddHashMacAddr(t_Handle h_FmMac, t_EnetAddr *p_EnetAddr);

/**************************************************************************//**
 @Function      FM_MAC_RemoveHashMacAddr

 @Description   Delete an Address to the hash table. This is for filter purpose only.

 @Param[in]     h_FmMac     -   A handle to a FM Module.
 @Param[in]     p_EnetAddr  -   Ethernet Mac address

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_RemoveHashMacAddr(t_Handle h_FmMac, t_EnetAddr *p_EnetAddr);

/**************************************************************************//**
 @Function      FM_MAC_AddExactMatchMacAddr

 @Description   Add a unicast or multicast mac address for exact-match filtering
                (8 on dTSEC, 2 for 10G-MAC)

 @Param[in]     h_FmMac     -   A handle to a FM Module.
 @Param[in]     p_EnetAddr  -   MAC Address to ADD

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only after FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_AddExactMatchMacAddr(t_Handle h_FmMac, t_EnetAddr *p_EnetAddr);

/**************************************************************************//**
 @Function      FM_MAC_RemovelExactMatchMacAddr

 @Description   Remove a uni cast or multi cast mac address.

 @Param[in]     h_FmMac     -   A handle to a FM Module.
 @Param[in]     p_EnetAddr  -   MAC Address to remove

 @Return        E_OK on success; Error code otherwise..

 @Cautions      Allowed only after FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_RemovelExactMatchMacAddr(t_Handle h_FmMac, t_EnetAddr *p_EnetAddr);

/**************************************************************************//**
 @Function      FM_MAC_SetPromiscuous

 @Description   Enable/Disable MAC Promiscuous mode for ALL mac addresses.

 @Param[in]     h_FmMac    - A handle to a FM MAC Module.
 @Param[in]     enable     - TRUE to enable or FALSE to disable.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only after FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_SetPromiscuous(t_Handle h_FmMac, bool enable);

/**************************************************************************//**
 @Function      FM_MAC_AdjustLink

 @Description   Adjusts the Ethernet link with new speed/duplex setup.
                This routine is relevant for dTSEC and mEMAC.
                In case of mEMAC, this routine is also used for manual
                re-configuration of RGMII speed and duplex mode for
                RGMII PHYs not supporting in-band status information
                to MAC.

 @Param[in]     h_FmMac     - A handle to a FM Module.
 @Param[in]     speed       - Ethernet speed.
 @Param[in]     fullDuplex  - TRUE for full-duplex mode;
                              FALSE for half-duplex mode.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_MAC_AdjustLink(t_Handle h_FmMac, e_EnetSpeed speed, bool fullDuplex);

/**************************************************************************//**
 @Function      FM_MAC_RestartAutoneg

 @Description   Restarts the auto-negotiation process.
                When auto-negotiation process is invoked under traffic the
                auto-negotiation process between the internal SGMII PHY and the
                external PHY does not always complete successfully. Calling this
                function will restart the auto-negotiation process that will end
                successfully. It is recommended to call this function after issuing
                auto-negotiation restart command to the Eth Phy.
                This routine is relevant only for dTSEC.

 @Param[in]     h_FmMac     - A handle to a FM Module.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_MAC_RestartAutoneg(t_Handle h_FmMac);

/**************************************************************************//**
 @Function      FM_MAC_GetId

 @Description   Return the MAC ID

 @Param[in]     h_FmMac     -   A handle to a FM Module.
 @Param[out]    p_MacId     -   MAC ID of device

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only after FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_GetId(t_Handle h_FmMac, uint32_t *p_MacId);

/**************************************************************************//**
 @Function      FM_MAC_GetVesrion

 @Description   Return Mac HW chip version

 @Param[in]     h_FmMac      -   A handle to a FM Module.
 @Param[out]    p_MacVresion -   Mac version as defined by the chip

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only after FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_GetVesrion(t_Handle h_FmMac, uint32_t *p_MacVresion);

/**************************************************************************//**
 @Function      FM_MAC_MII_WritePhyReg

 @Description   Write data into Phy Register

 @Param[in]     h_FmMac     -   A handle to a FM Module.
 @Param[in]     phyAddr     -   Phy Address on the MII bus
 @Param[in]     reg         -   Register Number.
 @Param[in]     data        -   Data to write.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only after FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_MII_WritePhyReg(t_Handle h_FmMac, uint8_t phyAddr, uint8_t reg, uint16_t data);

/**************************************************************************//**
 @Function      FM_MAC_MII_ReadPhyReg

 @Description   Read data from Phy Register

 @Param[in]     h_FmMac     -   A handle to a FM Module.
 @Param[in]     phyAddr     -   Phy Address on the MII bus
 @Param[in]     reg         -   Register Number.
 @Param[out]    p_Data      -   Data from PHY.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only after FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_MII_ReadPhyReg(t_Handle h_FmMac,  uint8_t phyAddr, uint8_t reg, uint16_t *p_Data);

#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
/**************************************************************************//**
 @Function      FM_MAC_DumpRegs

 @Description   Dump internal registers

 @Param[in]     h_FmMac     -   A handle to a FM Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only after FM_MAC_Init().
*//***************************************************************************/
t_Error FM_MAC_DumpRegs(t_Handle h_FmMac);
#endif /* (defined(DEBUG_ERRORS) && ... */

/** @} */ /* end of FM_mac_runtime_control_grp group */
/** @} */ /* end of FM_mac_grp group */
/** @} */ /* end of FM_grp group */


#endif /* __FM_MAC_EXT_H */
