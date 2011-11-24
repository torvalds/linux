/*
 *  Copyright (c) 2009 Atheros Communications, Inc., All Rights Reserved
 */

#ifndef	_AR6003REG_H
#define	_AR6003REG_H

// MAC PCU Registers
#define F2_STA_ID0          0x8000  // MAC station ID0 register - low 32 bits
#define F2_STA_ID1          0x8004  // MAC station ID1 register - upper 16 bits
#define F2_STA_ID1_SADH_MASK       0x0000FFFF // Mask for upper 16 bits of MAC addr
#define F2_STA_ID1_STA_AP          0x00010000 // Device is AP
#define F2_STA_ID1_AD_HOC          0x00020000 // Device is ad-hoc
#define F2_STA_ID1_PWR_SAV         0x00040000 // Power save reporting in self-generated frames
#define F2_STA_ID1_KSRCHDIS        0x00080000 // Key search disable
#define F2_STA_ID1_PCF		   0x00100000 // Observe PCF
#define F2_STA_ID1_USE_DEFANT      0x00200000 // Use default antenna
#define F2_STA_ID1_DEFANT_UPDATE   0x00400000 // Update default antenna w/ TX antenna
#define F2_STA_ID1_RTS_USE_DEF     0x00800000 // Use default antenna to send RTS
#define F2_STA_ID1_ACKCTS_6MB      0x01000000 // Use 6Mb/s rate for ACK & CTS

#define F2_BSS_ID0           0x8008  // MAC BSSID low 32 bits
#define F2_BSS_ID1           0x800C  // MAC BSSID upper 16 bits / AID
#define F2_BSS_ID1_U16_M     0x0000FFFF // Mask for upper 16 bits of BSSID
#define F2_BSS_ID1_AID_M     0xFFFF0000 // Mask for association ID
#define F2_BSS_ID1_AID_S     16         // Shift for association ID

#define F2_DEF_ANT           0x8038  //default antenna register

#define F2_RXDP              0x000C  // MAC receive queue descriptor pointer

#define F2_QCU_0             0x0001

#define F2_IMR_S0               0x00a4 // MAC Secondary interrupt mask register 0
#define F2_IMR_S0_QCU_TXOK_M    0x000003FF // Mask for TXOK (QCU 0-15)
#define F2_IMR_S0_QCU_TXDESC_M  0xFFFF0000 // Mask for TXDESC (QCU 0-15)
#define F2_IMR_S0_QCU_TXDESC_S  16		   // Shift for TXDESC (QCU 0-15)

#define F2_IMR               0x00a0  // MAC Primary interrupt mask register
#define F2_IMR_RXOK          0x00000001 // At least one frame received sans errors
#define F2_IMR_RXDESC        0x00000002 // Receive interrupt request
#define F2_IMR_RXERR         0x00000004 // Receive error interrupt
#define F2_IMR_RXNOPKT       0x00000008 // No frame received within timeout clock
#define F2_IMR_RXEOL         0x00000010 // Received descriptor empty interrupt
#define F2_IMR_RXORN         0x00000020 // Receive FIFO overrun interrupt
#define F2_IMR_TXOK          0x00000040 // Transmit okay interrupt
#define F2_IMR_TXDESC        0x00000080 // Transmit interrupt request
#define F2_IMR_TXERR         0x00000100 // Transmit error interrupt
#define F2_IMR_TXNOPKT       0x00000200 // No frame transmitted interrupt
#define F2_IMR_TXEOL         0x00000400 // Transmit descriptor empty interrupt
#define F2_IMR_TXURN         0x00000800 // Transmit FIFO underrun interrupt
#define F2_IMR_MIB           0x00001000 // MIB interrupt - see MIBC
#define F2_IMR_SWI           0x00002000 // Software interrupt
#define F2_IMR_RXPHY         0x00004000 // PHY receive error interrupt
#define F2_IMR_RXKCM         0x00008000 // Key-cache miss interrupt
#define F2_IMR_SWBA          0x00010000 // Software beacon alert interrupt
#define F2_IMR_BRSSI         0x00020000 // Beacon threshold interrupt
#define F2_IMR_BMISS         0x00040000 // Beacon missed interrupt
#define F2_IMR_HIUERR        0x00080000 // An unexpected bus error has occurred
#define F2_IMR_BNR           0x00100000 // BNR interrupt
#define F2_IMR_RXCHIRP       0x00200000 // BNR interrupt
#define F2_IMR_BCNMISC       0x00800000 // TIM interrupt
#define F2_IMR_GPIO          0x01000000 // GPIO Interrupt
#define F2_IMR_QCBROVF       0x02000000 // QCU CBR overflow interrupt
#define F2_IMR_QCBRURN       0x04000000 // QCU CBR underrun interrupt
#define F2_IMR_QTRIG         0x08000000 // QCU scheduling trigger interrupt
#define F2_IMR_RESV0         0xF0000000 // Reserved


// Interrupt status registers (read-and-clear access, secondary shadow copies)
#define F2_ISR_RAC           0x00c0 // MAC Primary interrupt status register,

#define F2_ISR               0x0080 // MAC Primary interrupt status register,
#define F2_ISR_RXOK          0x00000001 // At least one frame received sans errors
#define F2_ISR_RXDESC        0x00000002 // Receive interrupt request
#define F2_ISR_RXERR         0x00000004 // Receive error interrupt
#define F2_ISR_RXNOPKT       0x00000008 // No frame received within timeout clock
#define F2_ISR_RXEOL         0x00000010 // Received descriptor empty interrupt
#define F2_ISR_RXORN         0x00000020 // Receive FIFO overrun interrupt
#define F2_ISR_TXOK          0x00000040 // Transmit okay interrupt
#define F2_ISR_TXDESC        0x00000080 // Transmit interrupt request
#define F2_ISR_TXERR         0x00000100 // Transmit error interrupt
#define F2_ISR_TXNOPKT       0x00000200 // No frame transmitted interrupt
#define F2_ISR_TXEOL         0x00000400 // Transmit descriptor empty interrupt
#define F2_ISR_TXURN         0x00000800 // Transmit FIFO underrun interrupt
#define F2_ISR_MIB           0x00001000 // MIB interrupt - see MIBC
#define F2_ISR_SWI           0x00002000 // Software interrupt
#define F2_ISR_RXPHY         0x00004000 // PHY receive error interrupt
#define F2_ISR_RXKCM         0x00008000 // Key-cache miss interrupt
#define F2_ISR_SWBA          0x00010000 // Software beacon alert interrupt
#define F2_ISR_BRSSI         0x00020000 // Beacon threshold interrupt
#define F2_ISR_BMISS         0x00040000 // Beacon missed interrupt
#define F2_ISR_HIUERR        0x00080000 // An unexpected bus error has occurred
#define F2_ISR_BNR           0x00100000 // Beacon not ready interrupt
#define F2_ISR_RXCHIRP       0x00200000 // Beacon not ready interrupt
#define F2_ISR_BCNMISC       0x00800000 // TIM interrupt
#define F2_ISR_GPIO          0x01000000 // GPIO Interrupt
#define F2_ISR_QCBROVF       0x02000000 // QCU CBR overflow interrupt
#define F2_ISR_QCBRURN       0x04000000 // QCU CBR underrun interrupt
#define F2_ISR_QTRIG         0x08000000 // QCU scheduling trigger interrupt
#define F2_ISR_RESV0         0xF0000000 // Reserved

#define F2_ISR_S0_S          0x00c4 // MAC Secondary interrupt status register 0,
                                    // shadow copy
#define F2_ISR_S1_S          0x00c8 // MAC Secondary interrupt status register 1,
                                    // shadow copy
#define F2_ISR_S2_S          0x00cc // MAC Secondary interrupt status register 2,
                                    // shadow copy
#define F2_ISR_S3_S          0x00d0 // MAC Secondary interrupt status register 3,
                                    // shadow copy
#define F2_ISR_S4_S          0x00d4 // MAC Secondary interrupt status register 4,

#define F2_IER               0x0024  // MAC Interrupt enable register
#define F2_IER_ENABLE        0x00000001 // Global interrupt enable
#define F2_IER_DISABLE       0x00000000 // Global interrupt disable

#define F2_Q0_TXDP           0x0800 // MAC Transmit Queue descriptor pointer

#define F2_Q0_STS            0x0a00 // MAC Miscellaneous QCU status
#define F2_Q_STS_PEND_FR_CNT_M          0x00000003 // Mask for Pending Frame Count

// TBD?? bit 17 - 25 are added to Venus
#define F2_RX_FILTER         0x8024  // MAC receive filter register
#define F2_RX_FILTER_MASK    0x0001FFFF
#define F2_RX_FILTER_ALL     0x00000000 // Disallow all frames
#define F2_RX_UCAST          0x00000001 // Allow unicast frames
#define F2_RX_MCAST          0x00000002 // Allow multicast frames
#define F2_RX_BCAST          0x00000004 // Allow broadcast frames
#define F2_RX_CONTROL        0x00000008 // Allow control frames
#define F2_RX_BEACON         0x00000010 // Allow beacon frames
#define F2_RX_PROM           0x00000020 // Promiscuous mode, all packets

#if defined(VENUS_EMULATION)

#define F2_BEACON           0x8024  // MAC beacon control value/mode bits
#define F2_BEACON_PERIOD_MASK 0x0000FFFF  // Beacon period mask in TU/msec
#define F2_BEACON_TIM_MASK    0x007F0000  // Mask for byte offset of TIM start
#define F2_BEACON_TIM_S       16          // Shift for byte offset of TIM start
#define F2_BEACON_EN          0x00800000  // beacon enable
#define F2_BEACON_RESET_TSF   0x01000000  // Clears TSF to 0

#define F2_PCICFG           0x4010  // PCI configuration register
#define F2_PCICFG_SLEEP_CLK_SEL         0x00000002 // select between 40MHz normal or 32KHz sleep clock
#define F2_PCICFG_EEAE       0x00000001 // enable software access to EEPROM
#define F2_PCICFG_CLKRUNEN   0x00000004 // enable PCI CLKRUN function
#define F2_PCICFG_LED_PEND   0x00000020 // LED0&1 provide pending status
#define F2_PCICFG_LED_ACT    0x00000040 // LED0&1 provide activity status
#define F2_PCICFG_SL_INTEN   0x00000800 // enable interrupt line assertion when asleep
#define F2_PCICFG_BCTL           0x00001000 // LED blink rate ctrl (0 = bytes/s, 1 = frames/s)
#define F2_PCICFG_SPWR_DN    0x00010000 // mask for sleep/awake indication
#define F2_PCICFG_LED_MODE_M            0x000E0000 // Mask for LED mode select
#define F2_PCICFG_LED_BLINK_THRESHOLD_M         0x00700000 // Mask for LED blink threshold select
#define F2_PCICFG_LED_SLOW_BLINK_MODE           0x00800000 // LED slowest blink rate mode
#define F2_PCICFG_SLEEP_CLK_RATE_INDICATION     0x03000000 // LED slowest blink rate mode
#define F2_PCICFG_RESV2                         0xFF000000 // Reserved

#define F2_TSF_L32          0x804c  // MAC local clock lower 32 bits
#define F2_TSF_U32          0x8050  // MAC local clock upper 32 bits
 
#endif

#define F2_DIAG_SW           0x8030  // MAC PCU control register
#define F2_DIAG_ENCRYPT_DIS  0x00000008 // disable encryption
#define F2_DIAG_RX_DIS       0x00000020 // disable receive
#define F2_DIAG_CHAN_INFO    0x00000100 // dump channel info
#define F2_DUAL_CHAIN_CHAN_INFO    0x01000000 // dump channel info
#define F2_DIAG_FORCE_RX_ABORT     0x02000000 // force rx abort 

#define F2_CR                0x0008  // MAC Control Register - only write values of 1 have effect
#define F2_CR_RXE            0x00000004 // Receive enable
#define F2_CR_RXD            0x00000020 // Receive disable
#define F2_CR_SWI            0x00000040 // One-shot software interrupt

#define F2_Q_TXE             0x0840 // MAC Transmit Queue enable
#define F2_Q_TXE_M           0x000003FF // Mask for TXE (QCU 0-15)

#define F2_D0_QCUMASK        0x1000 // MAC QCU Mask

#define F2_D0_LCL_IFS        0x1040 // MAC DCU-specific IFS settings
#define F2_D_LCL_IFS_AIFS_M  0x0FF00000 // Mask for AIFS
#define F2_D_LCL_IFS_AIFS_S  20         // Shift for AIFS
#define F2_D_LCL_IFS_CWMIN_M 0x000003FF // Mask for CW_MIN
#define F2_D_LCL_IFS_CWMAX_M 0x000FFC00 // Mask for CW_MAX
#define F2_D_LCL_IFS_CWMIN_S 0 // shift for CW_MIN
#define F2_D_LCL_IFS_CWMAX_S 10 // shift for CW_MAX

#define F2_D0_RETRY_LIMIT    0x1080 // MAC Retry limits

#define F2_Q0_MISC           0x09c0 // MAC Miscellaneous QCU settings

#define F2_D_GBL_IFS_SIFS    0x1030 // MAC DCU-global IFS settings: SIFS duration
#define F2_D_GBL_IFS_EIFS    0x10b0 // MAC DCU-global IFS settings: EIFS duration

#define F2_D_FPCTL           0x1230 //Frame prefetch

#define PHY_CHIP_ID          0x9818  // PHY chip revision ID

#define PHY_RX_DELAY         0x9914  // PHY analog_power_on_time, in 100ns increments
#define PHY_RX_DELAY_M       0x00003FFF // Mask for delay from active assertion (wake up)

#define F2_TIME_OUT          0x8014  // MAC ACK & CTS time-out

#define F2_Q_TXD             0x0880 // MAC Transmit Queue disable

#define F2_RPGTO             0x0050  // MAC receive frame gap timeout
#define F2_RPGTO_MASK        0x000003FF // Mask for receive frame gap timeout

#define F2_RPCNT             0x0054 // MAC receive frame count limit
#define F2_RPCNT_MASK        0x0000001F // Mask for receive frame count limit

#define F2_TST_ADDAC         0x8034  // TST_ADDAC
#define F2_TST_ADDAC_CONT_TX 0x00000001 // cont_tx

// PHY registers
#define PHY_BASE_CHAIN0      0x9800  // PHY registers base address for chain0
#define PHY_BASE             PHY_BASE_CHAIN0      
#define PHY_BASE_FORCE_AGC_CLEAR   0x10000000

#define PHY_FRAME_CONTROL              0x9804  // PHY frame control register
#define PHY_FC_TURBO_MODE              0x00000001 // Set turbo mode bits
#define PHY_FC_TURBO_SHORT             0x00000002 // Set short symbols to turbo mode setting
#define PHY_FC_DYN_20_40               0x00000004 
#define PHY_FC_DYN_20_40_PRI_ONLY      0x00000008 
#define PHY_FC_DYN_20_40_PRI_CHN       0x00000010 
#define PHY_FC_DYN_20_40_EXT_CHN       0x00000020 
#define PHY_FC_DYN_HT_ENABLE           0x00000040 
#define PHY_FC_ALLOW_SHORT_GI          0x00000080 
#define PHY_FC_CHAINS_USE_WALSH        0x00000100 
#define PHY_FC_SINGLE_HT_LTF1          0x00000200 
#define PHY_FC_GF_ENABLE               0x00000400 
#define PHY_FC_BYPASS_DAC_FIFO         0x00000800 

#define PHY_AGC_CONTROL      0x9860  // PHY chip calibration and noise floor setting
#define PHY_AGC_CONTROL_CAL  0x00000001 // Perform PHY chip internal calibration
#define PHY_AGC_CONTROL_NF   0x00000002 // Perform PHY chip noise-floor calculation

#define PHY_ACTIVE           0x981C  // PHY activation register
#define PHY_ACTIVE_EN        0x00000001 // Activate PHY chips
#define PHY_ACTIVE_DIS       0x00000000 // Deactivate PHY chips

#define PHY_TIMING5                      0x9924     // PHY TIMING5
#define PHY_TIMING5_RSSI_THR1A           0x007f0000
#define PHY_TIMING5_ENABLE_RSSI_THR1A    0x00008000
#define PHY_TIMING5_CYCPWR_THR3          0x00007f00
#define PHY_TIMING5_CYCPWR_THR1          0x000000fe
#define PHY_TIMING5_ENABLE_CYCPWR_THR1   0x00000001

#define PHY_ADC_PARALLEL_CNTL 0x982c     /* adc parallel control register */
#define PWD_ADC_CL_MASK       0xffff7fff /* mask: clears bb_off_pwdAdc bit */

#define PHY_FRAME_CONTROL1   0x9944  // rest of old PHY frame control register
#define PHY_CHAN_INFO        0x99DC  // 5416 CHAN_INFO register
#define PHY_CHAN_INFO_GAIN0  0x9CFC  // 5416 CHAN_INFO register

#define PHY_CCA                 0x9864
#define PHY_CCA_MINCCA_PWR      0x1ff00000
#define PHY_CCA_THRESH62        0x000ff000
#define PHY_CCA_THRESH62_S      12
#define PHY_CCA_COUNT_MAXC      0x00000e00
#define PHY_CCA_MAXCCA_PWR      0x000001ff

#define PHY_RFBUS_REQ                 0x997C
#define PHY_RFBUS_REQ_REQUEST         0x00000001
#define PHY_RFBUS_GNT                 0x9C20
#define PHY_RFBUS_GNT_GRANT           0x00000001

#define TPCRG1_REG                    0xa258
#define BB_FORCE_DAC_GAIN_SET         0x00000001
#define BB_NUM_PD_GAIN_CLEAR          0xffff3fff
#define BB_PD_GAIN_SETTING1_CLEAR     0xfffcffff
#define BB_PD_GAIN_SETTING2_CLEAR     0xfff3ffff
#define BB_PD_GAIN_SETTING3_CLEAR     0xffcfffff
#define BB_NUM_PD_GAIN_SHIFT          14
#define BB_PD_GAIN_SETTING1_SHIFT     16
#define BB_PD_GAIN_SETTING2_SHIFT     18
#define BB_PD_GAIN_SETTING3_SHIFT     20

#define TPCRG5_REG                    0xa26c
#define BB_PD_GAIN_OVERLAP_MASK       0x0000000f

#if defined(VENUS_EMULATION)
#define TPCRG7_REG 0xa274
#else
#define TPCRG7_REG 0xa7f8
#endif
#define BB_FORCE_TX_GAIN_SET 0x00000001


#endif /* _AR6003REG_H */

