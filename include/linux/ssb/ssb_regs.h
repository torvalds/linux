#ifndef LINUX_SSB_REGS_H_
#define LINUX_SSB_REGS_H_


/* SiliconBackplane Address Map.
 * All regions may not exist on all chips.
 */
#define SSB_SDRAM_BASE		0x00000000U	/* Physical SDRAM */
#define SSB_PCI_MEM		0x08000000U	/* Host Mode sb2pcitranslation0 (64 MB) */
#define SSB_PCI_CFG		0x0c000000U	/* Host Mode sb2pcitranslation1 (64 MB) */
#define	SSB_SDRAM_SWAPPED	0x10000000U	/* Byteswapped Physical SDRAM */
#define SSB_ENUM_BASE    	0x18000000U	/* Enumeration space base */
#define	SSB_ENUM_LIMIT		0x18010000U	/* Enumeration space limit */

#define	SSB_FLASH2		0x1c000000U	/* Flash Region 2 (region 1 shadowed here) */
#define	SSB_FLASH2_SZ		0x02000000U	/* Size of Flash Region 2 */

#define	SSB_EXTIF_BASE		0x1f000000U	/* External Interface region base address */
#define	SSB_FLASH1		0x1fc00000U	/* Flash Region 1 */
#define	SSB_FLASH1_SZ		0x00400000U	/* Size of Flash Region 1 */

#define SSB_PCI_DMA		0x40000000U	/* Client Mode sb2pcitranslation2 (1 GB) */
#define SSB_PCI_DMA_SZ		0x40000000U	/* Client Mode sb2pcitranslation2 size in bytes */
#define SSB_PCIE_DMA_L32	0x00000000U	/* PCIE Client Mode sb2pcitranslation2 (2 ZettaBytes), low 32 bits */
#define SSB_PCIE_DMA_H32	0x80000000U	/* PCIE Client Mode sb2pcitranslation2 (2 ZettaBytes), high 32 bits */
#define	SSB_EUART		(SSB_EXTIF_BASE + 0x00800000)
#define	SSB_LED			(SSB_EXTIF_BASE + 0x00900000)


/* Enumeration space constants */
#define SSB_CORE_SIZE		0x1000	/* Size of a core MMIO area */
#define SSB_MAX_NR_CORES	((SSB_ENUM_LIMIT - SSB_ENUM_BASE) / SSB_CORE_SIZE)


/* mips address */
#define	SSB_EJTAG		0xff200000	/* MIPS EJTAG space (2M) */


/* SSB PCI config space registers. */
#define SSB_PMCSR		0x44
#define  SSB_PE			0x100
#define	SSB_BAR0_WIN		0x80	/* Backplane address space 0 */
#define	SSB_BAR1_WIN		0x84	/* Backplane address space 1 */
#define	SSB_SPROMCTL		0x88	/* SPROM control */
#define  SSB_SPROMCTL_WE	0x10	/* SPROM write enable */
#define	SSB_BAR1_CONTROL	0x8c	/* Address space 1 burst control */
#define SSB_PCI_IRQS		0x90	/* PCI interrupts */
#define SSB_PCI_IRQMASK		0x94	/* PCI IRQ control and mask (pcirev >= 6 only) */
#define SSB_BACKPLANE_IRQS	0x98	/* Backplane Interrupts */
#define SSB_GPIO_IN		0xB0	/* GPIO Input (pcirev >= 3 only) */
#define SSB_GPIO_OUT		0xB4	/* GPIO Output (pcirev >= 3 only) */
#define SSB_GPIO_OUT_ENABLE	0xB8	/* GPIO Output Enable/Disable (pcirev >= 3 only) */
#define  SSB_GPIO_SCS		0x10	/* PCI config space bit 4 for 4306c0 slow clock source */
#define  SSB_GPIO_HWRAD		0x20	/* PCI config space GPIO 13 for hw radio disable */
#define  SSB_GPIO_XTAL		0x40	/* PCI config space GPIO 14 for Xtal powerup */
#define  SSB_GPIO_PLL		0x80	/* PCI config space GPIO 15 for PLL powerdown */


#define SSB_BAR0_MAX_RETRIES	50

/* Silicon backplane configuration register definitions */
#define SSB_IPSFLAG		0x0F08
#define	 SSB_IPSFLAG_IRQ1	0x0000003F /* which sbflags get routed to mips interrupt 1 */
#define	 SSB_IPSFLAG_IRQ1_SHIFT	0
#define	 SSB_IPSFLAG_IRQ2	0x00003F00 /* which sbflags get routed to mips interrupt 2 */
#define	 SSB_IPSFLAG_IRQ2_SHIFT	8
#define	 SSB_IPSFLAG_IRQ3	0x003F0000 /* which sbflags get routed to mips interrupt 3 */
#define	 SSB_IPSFLAG_IRQ3_SHIFT	16
#define	 SSB_IPSFLAG_IRQ4	0x3F000000 /* which sbflags get routed to mips interrupt 4 */
#define	 SSB_IPSFLAG_IRQ4_SHIFT	24
#define SSB_TPSFLAG		0x0F18
#define  SSB_TPSFLAG_BPFLAG	0x0000003F /* Backplane flag # */
#define  SSB_TPSFLAG_ALWAYSIRQ	0x00000040 /* IRQ is always sent on the Backplane */
#define SSB_TMERRLOGA		0x0F48
#define SSB_TMERRLOG		0x0F50
#define SSB_ADMATCH3		0x0F60
#define SSB_ADMATCH2		0x0F68
#define SSB_ADMATCH1		0x0F70
#define SSB_IMSTATE		0x0F90     /* SB Initiator Agent State */
#define  SSB_IMSTATE_PC		0x0000000f /* Pipe Count */
#define  SSB_IMSTATE_AP_MASK	0x00000030 /* Arbitration Priority */
#define  SSB_IMSTATE_AP_BOTH	0x00000000 /* Use both timeslices and token */
#define  SSB_IMSTATE_AP_TS	0x00000010 /* Use timeslices only */
#define  SSB_IMSTATE_AP_TK	0x00000020 /* Use token only */
#define  SSB_IMSTATE_AP_RSV	0x00000030 /* Reserved */
#define  SSB_IMSTATE_IBE	0x00020000 /* In Band Error */
#define  SSB_IMSTATE_TO		0x00040000 /* Timeout */
#define  SSB_IMSTATE_BUSY	0x01800000 /* Busy (Backplane rev >= 2.3 only) */
#define  SSB_IMSTATE_REJECT	0x02000000 /* Reject (Backplane rev >= 2.3 only) */
#define SSB_INTVEC		0x0F94     /* SB Interrupt Mask */
#define  SSB_INTVEC_PCI		0x00000001 /* Enable interrupts for PCI */
#define  SSB_INTVEC_ENET0	0x00000002 /* Enable interrupts for enet 0 */
#define  SSB_INTVEC_ILINE20	0x00000004 /* Enable interrupts for iline20 */
#define  SSB_INTVEC_CODEC	0x00000008 /* Enable interrupts for v90 codec */
#define  SSB_INTVEC_USB		0x00000010 /* Enable interrupts for usb */
#define  SSB_INTVEC_EXTIF	0x00000020 /* Enable interrupts for external i/f */
#define  SSB_INTVEC_ENET1	0x00000040 /* Enable interrupts for enet 1 */
#define SSB_TMSLOW		0x0F98     /* SB Target State Low */
#define  SSB_TMSLOW_RESET	0x00000001 /* Reset */
#define  SSB_TMSLOW_REJECT	0x00000002 /* Reject (Standard Backplane) */
#define  SSB_TMSLOW_REJECT_23	0x00000004 /* Reject (Backplane rev 2.3) */
#define  SSB_TMSLOW_CLOCK	0x00010000 /* Clock Enable */
#define  SSB_TMSLOW_FGC		0x00020000 /* Force Gated Clocks On */
#define  SSB_TMSLOW_PE		0x40000000 /* Power Management Enable */
#define  SSB_TMSLOW_BE		0x80000000 /* BIST Enable */
#define SSB_TMSHIGH		0x0F9C     /* SB Target State High */
#define  SSB_TMSHIGH_SERR	0x00000001 /* S-error */
#define  SSB_TMSHIGH_INT	0x00000002 /* Interrupt */
#define  SSB_TMSHIGH_BUSY	0x00000004 /* Busy */
#define  SSB_TMSHIGH_TO		0x00000020 /* Timeout. Backplane rev >= 2.3 only */
#define  SSB_TMSHIGH_COREFL	0x1FFF0000 /* Core specific flags */
#define  SSB_TMSHIGH_COREFL_SHIFT	16
#define  SSB_TMSHIGH_DMA64	0x10000000 /* 64bit DMA supported */
#define  SSB_TMSHIGH_GCR	0x20000000 /* Gated Clock Request */
#define  SSB_TMSHIGH_BISTF	0x40000000 /* BIST Failed */
#define  SSB_TMSHIGH_BISTD	0x80000000 /* BIST Done */
#define SSB_BWA0		0x0FA0
#define SSB_IMCFGLO		0x0FA8
#define  SSB_IMCFGLO_SERTO	0x00000007 /* Service timeout */
#define  SSB_IMCFGLO_REQTO	0x00000070 /* Request timeout */
#define  SSB_IMCFGLO_REQTO_SHIFT	4
#define  SSB_IMCFGLO_CONNID	0x00FF0000 /* Connection ID */
#define  SSB_IMCFGLO_CONNID_SHIFT	16
#define SSB_IMCFGHI		0x0FAC
#define SSB_ADMATCH0		0x0FB0
#define SSB_TMCFGLO		0x0FB8
#define SSB_TMCFGHI		0x0FBC
#define SSB_BCONFIG		0x0FC0
#define SSB_BSTATE		0x0FC8
#define SSB_ACTCFG		0x0FD8
#define SSB_FLAGST		0x0FE8
#define SSB_IDLOW		0x0FF8
#define  SSB_IDLOW_CFGSP	0x00000003 /* Config Space */
#define  SSB_IDLOW_ADDRNGE	0x00000038 /* Address Ranges supported */
#define  SSB_IDLOW_ADDRNGE_SHIFT	3
#define  SSB_IDLOW_SYNC		0x00000040
#define  SSB_IDLOW_INITIATOR	0x00000080
#define  SSB_IDLOW_MIBL		0x00000F00 /* Minimum Backplane latency */
#define  SSB_IDLOW_MIBL_SHIFT	8
#define  SSB_IDLOW_MABL		0x0000F000 /* Maximum Backplane latency */
#define  SSB_IDLOW_MABL_SHIFT	12
#define  SSB_IDLOW_TIF		0x00010000 /* This Initiator is first */
#define  SSB_IDLOW_CCW		0x000C0000 /* Cycle counter width */
#define  SSB_IDLOW_CCW_SHIFT	18
#define  SSB_IDLOW_TPT		0x00F00000 /* Target ports */
#define  SSB_IDLOW_TPT_SHIFT	20
#define  SSB_IDLOW_INITP	0x0F000000 /* Initiator ports */
#define  SSB_IDLOW_INITP_SHIFT	24
#define  SSB_IDLOW_SSBREV	0xF0000000 /* Sonics Backplane Revision code */
#define  SSB_IDLOW_SSBREV_22	0x00000000 /* <= 2.2 */
#define  SSB_IDLOW_SSBREV_23	0x10000000 /* 2.3 */
#define  SSB_IDLOW_SSBREV_24	0x40000000 /* ?? Found in BCM4328 */
#define  SSB_IDLOW_SSBREV_25	0x50000000 /* ?? Not Found yet */
#define  SSB_IDLOW_SSBREV_26	0x60000000 /* ?? Found in some BCM4311/2 */
#define  SSB_IDLOW_SSBREV_27	0x70000000 /* ?? Found in some BCM4311/2 */
#define SSB_IDHIGH		0x0FFC     /* SB Identification High */
#define  SSB_IDHIGH_RCLO	0x0000000F /* Revision Code (low part) */
#define  SSB_IDHIGH_CC		0x00008FF0 /* Core Code */
#define  SSB_IDHIGH_CC_SHIFT	4
#define  SSB_IDHIGH_RCHI	0x00007000 /* Revision Code (high part) */
#define  SSB_IDHIGH_RCHI_SHIFT	8	   /* yes, shift 8 is right */
#define  SSB_IDHIGH_VC		0xFFFF0000 /* Vendor Code */
#define  SSB_IDHIGH_VC_SHIFT	16

/* SPROM shadow area. If not otherwise noted, fields are
 * two bytes wide. Note that the SPROM can _only_ be read
 * in two-byte quantities.
 */
#define SSB_SPROMSIZE_WORDS		64
#define SSB_SPROMSIZE_BYTES		(SSB_SPROMSIZE_WORDS * sizeof(u16))
#define SSB_SPROMSIZE_WORDS_R123	64
#define SSB_SPROMSIZE_WORDS_R4		220
#define SSB_SPROMSIZE_BYTES_R123	(SSB_SPROMSIZE_WORDS_R123 * sizeof(u16))
#define SSB_SPROMSIZE_BYTES_R4		(SSB_SPROMSIZE_WORDS_R4 * sizeof(u16))
#define SSB_SPROM_BASE1			0x1000
#define SSB_SPROM_BASE31		0x0800
#define SSB_SPROM_REVISION		0x007E
#define  SSB_SPROM_REVISION_REV		0x00FF	/* SPROM Revision number */
#define  SSB_SPROM_REVISION_CRC		0xFF00	/* SPROM CRC8 value */
#define  SSB_SPROM_REVISION_CRC_SHIFT	8

/* SPROM Revision 1 */
#define SSB_SPROM1_SPID			0x0004	/* Subsystem Product ID for PCI */
#define SSB_SPROM1_SVID			0x0006	/* Subsystem Vendor ID for PCI */
#define SSB_SPROM1_PID			0x0008	/* Product ID for PCI */
#define SSB_SPROM1_IL0MAC		0x0048	/* 6 bytes MAC address for 802.11b/g */
#define SSB_SPROM1_ET0MAC		0x004E	/* 6 bytes MAC address for Ethernet */
#define SSB_SPROM1_ET1MAC		0x0054	/* 6 bytes MAC address for 802.11a */
#define SSB_SPROM1_ETHPHY		0x005A	/* Ethernet PHY settings */
#define  SSB_SPROM1_ETHPHY_ET0A		0x001F	/* MII Address for enet0 */
#define  SSB_SPROM1_ETHPHY_ET1A		0x03E0	/* MII Address for enet1 */
#define  SSB_SPROM1_ETHPHY_ET1A_SHIFT	5
#define  SSB_SPROM1_ETHPHY_ET0M		(1<<14)	/* MDIO for enet0 */
#define  SSB_SPROM1_ETHPHY_ET1M		(1<<15)	/* MDIO for enet1 */
#define SSB_SPROM1_BINF			0x005C	/* Board info */
#define  SSB_SPROM1_BINF_BREV		0x00FF	/* Board Revision */
#define  SSB_SPROM1_BINF_CCODE		0x0F00	/* Country Code */
#define  SSB_SPROM1_BINF_CCODE_SHIFT	8
#define  SSB_SPROM1_BINF_ANTBG		0x3000	/* Available B-PHY and G-PHY antennas */
#define  SSB_SPROM1_BINF_ANTBG_SHIFT	12
#define  SSB_SPROM1_BINF_ANTA		0xC000	/* Available A-PHY antennas */
#define  SSB_SPROM1_BINF_ANTA_SHIFT	14
#define SSB_SPROM1_PA0B0		0x005E
#define SSB_SPROM1_PA0B1		0x0060
#define SSB_SPROM1_PA0B2		0x0062
#define SSB_SPROM1_GPIOA		0x0064	/* General Purpose IO pins 0 and 1 */
#define  SSB_SPROM1_GPIOA_P0		0x00FF	/* Pin 0 */
#define  SSB_SPROM1_GPIOA_P1		0xFF00	/* Pin 1 */
#define  SSB_SPROM1_GPIOA_P1_SHIFT	8
#define SSB_SPROM1_GPIOB		0x0066	/* General Purpuse IO pins 2 and 3 */
#define  SSB_SPROM1_GPIOB_P2		0x00FF	/* Pin 2 */
#define  SSB_SPROM1_GPIOB_P3		0xFF00	/* Pin 3 */
#define  SSB_SPROM1_GPIOB_P3_SHIFT	8
#define SSB_SPROM1_MAXPWR		0x0068	/* Power Amplifier Max Power */
#define  SSB_SPROM1_MAXPWR_BG		0x00FF	/* B-PHY and G-PHY (in dBm Q5.2) */
#define  SSB_SPROM1_MAXPWR_A		0xFF00	/* A-PHY (in dBm Q5.2) */
#define  SSB_SPROM1_MAXPWR_A_SHIFT	8
#define SSB_SPROM1_PA1B0		0x006A
#define SSB_SPROM1_PA1B1		0x006C
#define SSB_SPROM1_PA1B2		0x006E
#define SSB_SPROM1_ITSSI		0x0070	/* Idle TSSI Target */
#define  SSB_SPROM1_ITSSI_BG		0x00FF	/* B-PHY and G-PHY*/
#define  SSB_SPROM1_ITSSI_A		0xFF00	/* A-PHY */
#define  SSB_SPROM1_ITSSI_A_SHIFT	8
#define SSB_SPROM1_BFLLO		0x0072	/* Boardflags (low 16 bits) */
#define SSB_SPROM1_AGAIN		0x0074	/* Antenna Gain (in dBm Q5.2) */
#define  SSB_SPROM1_AGAIN_BG		0x00FF	/* B-PHY and G-PHY */
#define  SSB_SPROM1_AGAIN_BG_SHIFT	0
#define  SSB_SPROM1_AGAIN_A		0xFF00	/* A-PHY */
#define  SSB_SPROM1_AGAIN_A_SHIFT	8
#define SSB_SPROM1_CCODE		0x0076

/* SPROM Revision 2 (inherits from rev 1) */
#define SSB_SPROM2_BFLHI		0x0038	/* Boardflags (high 16 bits) */
#define SSB_SPROM2_MAXP_A		0x003A	/* A-PHY Max Power */
#define  SSB_SPROM2_MAXP_A_HI		0x00FF	/* Max Power High */
#define  SSB_SPROM2_MAXP_A_LO		0xFF00	/* Max Power Low */
#define  SSB_SPROM2_MAXP_A_LO_SHIFT	8
#define SSB_SPROM2_PA1LOB0		0x003C	/* A-PHY PowerAmplifier Low Settings */
#define SSB_SPROM2_PA1LOB1		0x003E	/* A-PHY PowerAmplifier Low Settings */
#define SSB_SPROM2_PA1LOB2		0x0040	/* A-PHY PowerAmplifier Low Settings */
#define SSB_SPROM2_PA1HIB0		0x0042	/* A-PHY PowerAmplifier High Settings */
#define SSB_SPROM2_PA1HIB1		0x0044	/* A-PHY PowerAmplifier High Settings */
#define SSB_SPROM2_PA1HIB2		0x0046	/* A-PHY PowerAmplifier High Settings */
#define SSB_SPROM2_OPO			0x0078	/* OFDM Power Offset from CCK Level */
#define  SSB_SPROM2_OPO_VALUE		0x00FF
#define  SSB_SPROM2_OPO_UNUSED		0xFF00
#define SSB_SPROM2_CCODE		0x007C	/* Two char Country Code */

/* SPROM Revision 3 (inherits most data from rev 2) */
#define SSB_SPROM3_OFDMAPO		0x002C	/* A-PHY OFDM Mid Power Offset (4 bytes, BigEndian) */
#define SSB_SPROM3_OFDMALPO		0x0030	/* A-PHY OFDM Low Power Offset (4 bytes, BigEndian) */
#define SSB_SPROM3_OFDMAHPO		0x0034	/* A-PHY OFDM High Power Offset (4 bytes, BigEndian) */
#define SSB_SPROM3_GPIOLDC		0x0042	/* GPIO LED Powersave Duty Cycle (4 bytes, BigEndian) */
#define  SSB_SPROM3_GPIOLDC_OFF		0x0000FF00	/* Off Count */
#define  SSB_SPROM3_GPIOLDC_OFF_SHIFT	8
#define  SSB_SPROM3_GPIOLDC_ON		0x00FF0000	/* On Count */
#define  SSB_SPROM3_GPIOLDC_ON_SHIFT	16
#define SSB_SPROM3_IL0MAC		0x004A	/* 6 bytes MAC address for 802.11b/g */
#define SSB_SPROM3_CCKPO		0x0078	/* CCK Power Offset */
#define  SSB_SPROM3_CCKPO_1M		0x000F	/* 1M Rate PO */
#define  SSB_SPROM3_CCKPO_2M		0x00F0	/* 2M Rate PO */
#define  SSB_SPROM3_CCKPO_2M_SHIFT	4
#define  SSB_SPROM3_CCKPO_55M		0x0F00	/* 5.5M Rate PO */
#define  SSB_SPROM3_CCKPO_55M_SHIFT	8
#define  SSB_SPROM3_CCKPO_11M		0xF000	/* 11M Rate PO */
#define  SSB_SPROM3_CCKPO_11M_SHIFT	12
#define  SSB_SPROM3_OFDMGPO		0x107A	/* G-PHY OFDM Power Offset (4 bytes, BigEndian) */

/* SPROM Revision 4 */
#define SSB_SPROM4_BFLLO		0x0044	/* Boardflags (low 16 bits) */
#define SSB_SPROM4_BFLHI		0x0046  /* Board Flags Hi */
#define SSB_SPROM4_BFL2LO		0x0048	/* Board flags 2 (low 16 bits) */
#define SSB_SPROM4_BFL2HI		0x004A	/* Board flags 2 Hi */
#define SSB_SPROM4_IL0MAC		0x004C	/* 6 byte MAC address for a/b/g/n */
#define SSB_SPROM4_CCODE		0x0052	/* Country Code (2 bytes) */
#define SSB_SPROM4_GPIOA		0x0056	/* Gen. Purpose IO # 0 and 1 */
#define  SSB_SPROM4_GPIOA_P0		0x00FF	/* Pin 0 */
#define  SSB_SPROM4_GPIOA_P1		0xFF00	/* Pin 1 */
#define  SSB_SPROM4_GPIOA_P1_SHIFT	8
#define SSB_SPROM4_GPIOB		0x0058	/* Gen. Purpose IO # 2 and 3 */
#define  SSB_SPROM4_GPIOB_P2		0x00FF	/* Pin 2 */
#define  SSB_SPROM4_GPIOB_P3		0xFF00	/* Pin 3 */
#define  SSB_SPROM4_GPIOB_P3_SHIFT	8
#define SSB_SPROM4_ETHPHY		0x005A	/* Ethernet PHY settings ?? */
#define  SSB_SPROM4_ETHPHY_ET0A		0x001F	/* MII Address for enet0 */
#define  SSB_SPROM4_ETHPHY_ET1A		0x03E0	/* MII Address for enet1 */
#define  SSB_SPROM4_ETHPHY_ET1A_SHIFT	5
#define  SSB_SPROM4_ETHPHY_ET0M		(1<<14)	/* MDIO for enet0 */
#define  SSB_SPROM4_ETHPHY_ET1M		(1<<15)	/* MDIO for enet1 */
#define SSB_SPROM4_ANTAVAIL		0x005D  /* Antenna available bitfields */
#define  SSB_SPROM4_ANTAVAIL_A		0x00FF	/* A-PHY bitfield */
#define  SSB_SPROM4_ANTAVAIL_A_SHIFT	0
#define  SSB_SPROM4_ANTAVAIL_BG		0xFF00	/* B-PHY and G-PHY bitfield */
#define  SSB_SPROM4_ANTAVAIL_BG_SHIFT	8
#define SSB_SPROM4_AGAIN01		0x005E	/* Antenna Gain (in dBm Q5.2) */
#define  SSB_SPROM4_AGAIN0		0x00FF	/* Antenna 0 */
#define  SSB_SPROM4_AGAIN0_SHIFT	0
#define  SSB_SPROM4_AGAIN1		0xFF00	/* Antenna 1 */
#define  SSB_SPROM4_AGAIN1_SHIFT	8
#define SSB_SPROM4_AGAIN23		0x0060
#define  SSB_SPROM4_AGAIN2		0x00FF	/* Antenna 2 */
#define  SSB_SPROM4_AGAIN2_SHIFT	0
#define  SSB_SPROM4_AGAIN3		0xFF00	/* Antenna 3 */
#define  SSB_SPROM4_AGAIN3_SHIFT	8
#define SSB_SPROM4_TXPID2G01		0x0062 	/* TX Power Index 2GHz */
#define  SSB_SPROM4_TXPID2G0		0x00FF
#define  SSB_SPROM4_TXPID2G0_SHIFT	0
#define  SSB_SPROM4_TXPID2G1		0xFF00
#define  SSB_SPROM4_TXPID2G1_SHIFT	8
#define SSB_SPROM4_TXPID2G23		0x0064 	/* TX Power Index 2GHz */
#define  SSB_SPROM4_TXPID2G2		0x00FF
#define  SSB_SPROM4_TXPID2G2_SHIFT	0
#define  SSB_SPROM4_TXPID2G3		0xFF00
#define  SSB_SPROM4_TXPID2G3_SHIFT	8
#define SSB_SPROM4_TXPID5G01		0x0066 	/* TX Power Index 5GHz middle subband */
#define  SSB_SPROM4_TXPID5G0		0x00FF
#define  SSB_SPROM4_TXPID5G0_SHIFT	0
#define  SSB_SPROM4_TXPID5G1		0xFF00
#define  SSB_SPROM4_TXPID5G1_SHIFT	8
#define SSB_SPROM4_TXPID5G23		0x0068 	/* TX Power Index 5GHz middle subband */
#define  SSB_SPROM4_TXPID5G2		0x00FF
#define  SSB_SPROM4_TXPID5G2_SHIFT	0
#define  SSB_SPROM4_TXPID5G3		0xFF00
#define  SSB_SPROM4_TXPID5G3_SHIFT	8
#define SSB_SPROM4_TXPID5GL01		0x006A 	/* TX Power Index 5GHz low subband */
#define  SSB_SPROM4_TXPID5GL0		0x00FF
#define  SSB_SPROM4_TXPID5GL0_SHIFT	0
#define  SSB_SPROM4_TXPID5GL1		0xFF00
#define  SSB_SPROM4_TXPID5GL1_SHIFT	8
#define SSB_SPROM4_TXPID5GL23		0x006C 	/* TX Power Index 5GHz low subband */
#define  SSB_SPROM4_TXPID5GL2		0x00FF
#define  SSB_SPROM4_TXPID5GL2_SHIFT	0
#define  SSB_SPROM4_TXPID5GL3		0xFF00
#define  SSB_SPROM4_TXPID5GL3_SHIFT	8
#define SSB_SPROM4_TXPID5GH01		0x006E 	/* TX Power Index 5GHz high subband */
#define  SSB_SPROM4_TXPID5GH0		0x00FF
#define  SSB_SPROM4_TXPID5GH0_SHIFT	0
#define  SSB_SPROM4_TXPID5GH1		0xFF00
#define  SSB_SPROM4_TXPID5GH1_SHIFT	8
#define SSB_SPROM4_TXPID5GH23		0x0070 	/* TX Power Index 5GHz high subband */
#define  SSB_SPROM4_TXPID5GH2		0x00FF
#define  SSB_SPROM4_TXPID5GH2_SHIFT	0
#define  SSB_SPROM4_TXPID5GH3		0xFF00
#define  SSB_SPROM4_TXPID5GH3_SHIFT	8
#define SSB_SPROM4_MAXP_BG		0x0080  /* Max Power BG in path 1 */
#define  SSB_SPROM4_MAXP_BG_MASK	0x00FF  /* Mask for Max Power BG */
#define  SSB_SPROM4_ITSSI_BG		0xFF00	/* Mask for path 1 itssi_bg */
#define  SSB_SPROM4_ITSSI_BG_SHIFT	8
#define SSB_SPROM4_MAXP_A		0x008A  /* Max Power A in path 1 */
#define  SSB_SPROM4_MAXP_A_MASK		0x00FF  /* Mask for Max Power A */
#define  SSB_SPROM4_ITSSI_A		0xFF00	/* Mask for path 1 itssi_a */
#define  SSB_SPROM4_ITSSI_A_SHIFT	8
#define SSB_SPROM4_PA0B0		0x0082	/* The paXbY locations are */
#define SSB_SPROM4_PA0B1		0x0084	/*   only guesses */
#define SSB_SPROM4_PA0B2		0x0086
#define SSB_SPROM4_PA1B0		0x008E
#define SSB_SPROM4_PA1B1		0x0090
#define SSB_SPROM4_PA1B2		0x0092

/* SPROM Revision 5 (inherits most data from rev 4) */
#define SSB_SPROM5_CCODE		0x0044	/* Country Code (2 bytes) */
#define SSB_SPROM5_BFLLO		0x004A	/* Boardflags (low 16 bits) */
#define SSB_SPROM5_BFLHI		0x004C  /* Board Flags Hi */
#define SSB_SPROM5_BFL2LO		0x004E	/* Board flags 2 (low 16 bits) */
#define SSB_SPROM5_BFL2HI		0x0050	/* Board flags 2 Hi */
#define SSB_SPROM5_IL0MAC		0x0052	/* 6 byte MAC address for a/b/g/n */
#define SSB_SPROM5_GPIOA		0x0076	/* Gen. Purpose IO # 0 and 1 */
#define  SSB_SPROM5_GPIOA_P0		0x00FF	/* Pin 0 */
#define  SSB_SPROM5_GPIOA_P1		0xFF00	/* Pin 1 */
#define  SSB_SPROM5_GPIOA_P1_SHIFT	8
#define SSB_SPROM5_GPIOB		0x0078	/* Gen. Purpose IO # 2 and 3 */
#define  SSB_SPROM5_GPIOB_P2		0x00FF	/* Pin 2 */
#define  SSB_SPROM5_GPIOB_P3		0xFF00	/* Pin 3 */
#define  SSB_SPROM5_GPIOB_P3_SHIFT	8

/* SPROM Revision 8 */
#define SSB_SPROM8_BOARDREV		0x0082	/* Board revision */
#define SSB_SPROM8_BFLLO		0x0084	/* Board flags (bits 0-15) */
#define SSB_SPROM8_BFLHI		0x0086	/* Board flags (bits 16-31) */
#define SSB_SPROM8_BFL2LO		0x0088	/* Board flags (bits 32-47) */
#define SSB_SPROM8_BFL2HI		0x008A	/* Board flags (bits 48-63) */
#define SSB_SPROM8_IL0MAC		0x008C	/* 6 byte MAC address */
#define SSB_SPROM8_CCODE		0x0092	/* 2 byte country code */
#define SSB_SPROM8_GPIOA		0x0096	/*Gen. Purpose IO # 0 and 1 */
#define  SSB_SPROM8_GPIOA_P0		0x00FF	/* Pin 0 */
#define  SSB_SPROM8_GPIOA_P1		0xFF00	/* Pin 1 */
#define  SSB_SPROM8_GPIOA_P1_SHIFT	8
#define SSB_SPROM8_GPIOB		0x0098	/* Gen. Purpose IO # 2 and 3 */
#define  SSB_SPROM8_GPIOB_P2		0x00FF	/* Pin 2 */
#define  SSB_SPROM8_GPIOB_P3		0xFF00	/* Pin 3 */
#define  SSB_SPROM8_GPIOB_P3_SHIFT	8
#define SSB_SPROM8_ANTAVAIL		0x009C  /* Antenna available bitfields*/
#define  SSB_SPROM8_ANTAVAIL_A		0xFF00	/* A-PHY bitfield */
#define  SSB_SPROM8_ANTAVAIL_A_SHIFT	8
#define  SSB_SPROM8_ANTAVAIL_BG		0x00FF	/* B-PHY and G-PHY bitfield */
#define  SSB_SPROM8_ANTAVAIL_BG_SHIFT	0
#define SSB_SPROM8_AGAIN01		0x009E	/* Antenna Gain (in dBm Q5.2) */
#define  SSB_SPROM8_AGAIN0		0x00FF	/* Antenna 0 */
#define  SSB_SPROM8_AGAIN0_SHIFT	0
#define  SSB_SPROM8_AGAIN1		0xFF00	/* Antenna 1 */
#define  SSB_SPROM8_AGAIN1_SHIFT	8
#define SSB_SPROM8_AGAIN23		0x00A0
#define  SSB_SPROM8_AGAIN2		0x00FF	/* Antenna 2 */
#define  SSB_SPROM8_AGAIN2_SHIFT	0
#define  SSB_SPROM8_AGAIN3		0xFF00	/* Antenna 3 */
#define  SSB_SPROM8_AGAIN3_SHIFT	8
#define SSB_SPROM8_RSSIPARM2G		0x00A4	/* RSSI params for 2GHz */
#define  SSB_SPROM8_RSSISMF2G		0x000F
#define  SSB_SPROM8_RSSISMC2G		0x00F0
#define  SSB_SPROM8_RSSISMC2G_SHIFT	4
#define  SSB_SPROM8_RSSISAV2G		0x0700
#define  SSB_SPROM8_RSSISAV2G_SHIFT	8
#define  SSB_SPROM8_BXA2G		0x1800
#define  SSB_SPROM8_BXA2G_SHIFT		11
#define SSB_SPROM8_RSSIPARM5G		0x00A6	/* RSSI params for 5GHz */
#define  SSB_SPROM8_RSSISMF5G		0x000F
#define  SSB_SPROM8_RSSISMC5G		0x00F0
#define  SSB_SPROM8_RSSISMC5G_SHIFT	4
#define  SSB_SPROM8_RSSISAV5G		0x0700
#define  SSB_SPROM8_RSSISAV5G_SHIFT	8
#define  SSB_SPROM8_BXA5G		0x1800
#define  SSB_SPROM8_BXA5G_SHIFT		11
#define SSB_SPROM8_TRI25G		0x00A8	/* TX isolation 2.4&5.3GHz */
#define  SSB_SPROM8_TRI2G		0x00FF	/* TX isolation 2.4GHz */
#define  SSB_SPROM8_TRI5G		0xFF00	/* TX isolation 5.3GHz */
#define  SSB_SPROM8_TRI5G_SHIFT		8
#define SSB_SPROM8_TRI5GHL		0x00AA	/* TX isolation 5.2/5.8GHz */
#define  SSB_SPROM8_TRI5GL		0x00FF	/* TX isolation 5.2GHz */
#define  SSB_SPROM8_TRI5GH		0xFF00	/* TX isolation 5.8GHz */
#define  SSB_SPROM8_TRI5GH_SHIFT	8
#define SSB_SPROM8_RXPO			0x00AC  /* RX power offsets */
#define  SSB_SPROM8_RXPO2G		0x00FF	/* 2GHz RX power offset */
#define  SSB_SPROM8_RXPO5G		0xFF00	/* 5GHz RX power offset */
#define  SSB_SPROM8_RXPO5G_SHIFT	8
#define SSB_SPROM8_FEM2G		0x00AE
#define SSB_SPROM8_FEM5G		0x00B0
#define  SSB_SROM8_FEM_TSSIPOS		0x0001
#define  SSB_SROM8_FEM_TSSIPOS_SHIFT	0
#define  SSB_SROM8_FEM_EXTPA_GAIN	0x0006
#define  SSB_SROM8_FEM_EXTPA_GAIN_SHIFT	1
#define  SSB_SROM8_FEM_PDET_RANGE	0x00F8
#define  SSB_SROM8_FEM_PDET_RANGE_SHIFT	3
#define  SSB_SROM8_FEM_TR_ISO		0x0700
#define  SSB_SROM8_FEM_TR_ISO_SHIFT	8
#define  SSB_SROM8_FEM_ANTSWLUT		0xF800
#define  SSB_SROM8_FEM_ANTSWLUT_SHIFT	11
#define SSB_SPROM8_THERMAL		0x00B2
#define SSB_SPROM8_MPWR_RAWTS		0x00B4
#define SSB_SPROM8_TS_SLP_OPT_CORRX	0x00B6
#define SSB_SPROM8_FOC_HWIQ_IQSWP	0x00B8
#define SSB_SPROM8_PHYCAL_TEMPDELTA	0x00BA

/* There are 4 blocks with power info sharing the same layout */
#define SSB_SROM8_PWR_INFO_CORE0	0x00C0
#define SSB_SROM8_PWR_INFO_CORE1	0x00E0
#define SSB_SROM8_PWR_INFO_CORE2	0x0100
#define SSB_SROM8_PWR_INFO_CORE3	0x0120

#define SSB_SROM8_2G_MAXP_ITSSI		0x00
#define  SSB_SPROM8_2G_MAXP		0x00FF
#define  SSB_SPROM8_2G_ITSSI		0xFF00
#define  SSB_SPROM8_2G_ITSSI_SHIFT	8
#define SSB_SROM8_2G_PA_0		0x02	/* 2GHz power amp settings */
#define SSB_SROM8_2G_PA_1		0x04
#define SSB_SROM8_2G_PA_2		0x06
#define SSB_SROM8_5G_MAXP_ITSSI		0x08	/* 5GHz ITSSI and 5.3GHz Max Power */
#define  SSB_SPROM8_5G_MAXP		0x00FF
#define  SSB_SPROM8_5G_ITSSI		0xFF00
#define  SSB_SPROM8_5G_ITSSI_SHIFT	8
#define SSB_SPROM8_5GHL_MAXP		0x0A	/* 5.2GHz and 5.8GHz Max Power */
#define  SSB_SPROM8_5GH_MAXP		0x00FF
#define  SSB_SPROM8_5GL_MAXP		0xFF00
#define  SSB_SPROM8_5GL_MAXP_SHIFT	8
#define SSB_SROM8_5G_PA_0		0x0C	/* 5.3GHz power amp settings */
#define SSB_SROM8_5G_PA_1		0x0E
#define SSB_SROM8_5G_PA_2		0x10
#define SSB_SROM8_5GL_PA_0		0x12	/* 5.2GHz power amp settings */
#define SSB_SROM8_5GL_PA_1		0x14
#define SSB_SROM8_5GL_PA_2		0x16
#define SSB_SROM8_5GH_PA_0		0x18	/* 5.8GHz power amp settings */
#define SSB_SROM8_5GH_PA_1		0x1A
#define SSB_SROM8_5GH_PA_2		0x1C

/* TODO: Make it deprecated */
#define SSB_SPROM8_MAXP_BG		0x00C0  /* Max Power 2GHz in path 1 */
#define  SSB_SPROM8_MAXP_BG_MASK	0x00FF  /* Mask for Max Power 2GHz */
#define  SSB_SPROM8_ITSSI_BG		0xFF00	/* Mask for path 1 itssi_bg */
#define  SSB_SPROM8_ITSSI_BG_SHIFT	8
#define SSB_SPROM8_PA0B0		0x00C2	/* 2GHz power amp settings */
#define SSB_SPROM8_PA0B1		0x00C4
#define SSB_SPROM8_PA0B2		0x00C6
#define SSB_SPROM8_MAXP_A		0x00C8  /* Max Power 5.3GHz */
#define  SSB_SPROM8_MAXP_A_MASK		0x00FF  /* Mask for Max Power 5.3GHz */
#define  SSB_SPROM8_ITSSI_A		0xFF00	/* Mask for path 1 itssi_a */
#define  SSB_SPROM8_ITSSI_A_SHIFT	8
#define SSB_SPROM8_MAXP_AHL		0x00CA  /* Max Power 5.2/5.8GHz */
#define  SSB_SPROM8_MAXP_AH_MASK	0x00FF  /* Mask for Max Power 5.8GHz */
#define  SSB_SPROM8_MAXP_AL_MASK	0xFF00  /* Mask for Max Power 5.2GHz */
#define  SSB_SPROM8_MAXP_AL_SHIFT	8
#define SSB_SPROM8_PA1B0		0x00CC	/* 5.3GHz power amp settings */
#define SSB_SPROM8_PA1B1		0x00CE
#define SSB_SPROM8_PA1B2		0x00D0
#define SSB_SPROM8_PA1LOB0		0x00D2	/* 5.2GHz power amp settings */
#define SSB_SPROM8_PA1LOB1		0x00D4
#define SSB_SPROM8_PA1LOB2		0x00D6
#define SSB_SPROM8_PA1HIB0		0x00D8	/* 5.8GHz power amp settings */
#define SSB_SPROM8_PA1HIB1		0x00DA
#define SSB_SPROM8_PA1HIB2		0x00DC

#define SSB_SPROM8_CCK2GPO		0x0140	/* CCK power offset */
#define SSB_SPROM8_OFDM2GPO		0x0142	/* 2.4GHz OFDM power offset */
#define SSB_SPROM8_OFDM5GPO		0x0146	/* 5.3GHz OFDM power offset */
#define SSB_SPROM8_OFDM5GLPO		0x014A	/* 5.2GHz OFDM power offset */
#define SSB_SPROM8_OFDM5GHPO		0x014E	/* 5.8GHz OFDM power offset */

/* Values for boardflags_lo read from SPROM */
#define SSB_BFL_BTCOEXIST		0x0001	/* implements Bluetooth coexistance */
#define SSB_BFL_PACTRL			0x0002	/* GPIO 9 controlling the PA */
#define SSB_BFL_AIRLINEMODE		0x0004	/* implements GPIO 13 radio disable indication */
#define SSB_BFL_RSSI			0x0008	/* software calculates nrssi slope. */
#define SSB_BFL_ENETSPI			0x0010	/* has ephy roboswitch spi */
#define SSB_BFL_XTAL_NOSLOW		0x0020	/* no slow clock available */
#define SSB_BFL_CCKHIPWR		0x0040	/* can do high power CCK transmission */
#define SSB_BFL_ENETADM			0x0080	/* has ADMtek switch */
#define SSB_BFL_ENETVLAN		0x0100	/* can do vlan */
#define SSB_BFL_AFTERBURNER		0x0200	/* supports Afterburner mode */
#define SSB_BFL_NOPCI			0x0400	/* board leaves PCI floating */
#define SSB_BFL_FEM			0x0800	/* supports the Front End Module */
#define SSB_BFL_EXTLNA			0x1000	/* has an external LNA */
#define SSB_BFL_HGPA			0x2000	/* had high gain PA */
#define SSB_BFL_BTCMOD			0x4000	/* BFL_BTCOEXIST is given in alternate GPIOs */
#define SSB_BFL_ALTIQ			0x8000	/* alternate I/Q settings */

/* Values for boardflags_hi read from SPROM */
#define SSB_BFH_NOPA			0x0001	/* has no PA */
#define SSB_BFH_RSSIINV			0x0002	/* RSSI uses positive slope (not TSSI) */
#define SSB_BFH_PAREF			0x0004	/* uses the PARef LDO */
#define SSB_BFH_3TSWITCH		0x0008	/* uses a triple throw switch shared with bluetooth */
#define SSB_BFH_PHASESHIFT		0x0010	/* can support phase shifter */
#define SSB_BFH_BUCKBOOST		0x0020	/* has buck/booster */
#define SSB_BFH_FEM_BT			0x0040	/* has FEM and switch to share antenna with bluetooth */

/* Values for boardflags2_lo read from SPROM */
#define SSB_BFL2_RXBB_INT_REG_DIS	0x0001	/* external RX BB regulator present */
#define SSB_BFL2_APLL_WAR		0x0002	/* alternative A-band PLL settings implemented */
#define SSB_BFL2_TXPWRCTRL_EN 		0x0004	/* permits enabling TX Power Control */
#define SSB_BFL2_2X4_DIV		0x0008	/* 2x4 diversity switch */
#define SSB_BFL2_5G_PWRGAIN		0x0010	/* supports 5G band power gain */
#define SSB_BFL2_PCIEWAR_OVR		0x0020	/* overrides ASPM and Clkreq settings */
#define SSB_BFL2_CAESERS_BRD		0x0040	/* is Caesers board (unused) */
#define SSB_BFL2_BTC3WIRE		0x0080	/* used 3-wire bluetooth coexist */
#define SSB_BFL2_SKWRKFEM_BRD		0x0100	/* 4321mcm93 uses Skyworks FEM */
#define SSB_BFL2_SPUR_WAR		0x0200	/* has a workaround for clock-harmonic spurs */
#define SSB_BFL2_GPLL_WAR		0x0400	/* altenative G-band PLL settings implemented */

/* Values for SSB_SPROM1_BINF_CCODE */
enum {
	SSB_SPROM1CCODE_WORLD = 0,
	SSB_SPROM1CCODE_THAILAND,
	SSB_SPROM1CCODE_ISRAEL,
	SSB_SPROM1CCODE_JORDAN,
	SSB_SPROM1CCODE_CHINA,
	SSB_SPROM1CCODE_JAPAN,
	SSB_SPROM1CCODE_USA_CANADA_ANZ,
	SSB_SPROM1CCODE_EUROPE,
	SSB_SPROM1CCODE_USA_LOW,
	SSB_SPROM1CCODE_JAPAN_HIGH,
	SSB_SPROM1CCODE_ALL,
	SSB_SPROM1CCODE_NONE,
};

/* Address-Match values and masks (SSB_ADMATCHxxx) */
#define SSB_ADM_TYPE			0x00000003	/* Address type */
#define  SSB_ADM_TYPE0			0
#define  SSB_ADM_TYPE1			1
#define  SSB_ADM_TYPE2			2
#define SSB_ADM_AD64			0x00000004
#define SSB_ADM_SZ0			0x000000F8	/* Type0 size */
#define SSB_ADM_SZ0_SHIFT		3
#define SSB_ADM_SZ1			0x000001F8	/* Type1 size */
#define SSB_ADM_SZ1_SHIFT		3
#define SSB_ADM_SZ2			0x000001F8	/* Type2 size */
#define SSB_ADM_SZ2_SHIFT		3
#define SSB_ADM_EN			0x00000400	/* Enable */
#define SSB_ADM_NEG			0x00000800	/* Negative decode */
#define SSB_ADM_BASE0			0xFFFFFF00	/* Type0 base address */
#define SSB_ADM_BASE0_SHIFT		8
#define SSB_ADM_BASE1			0xFFFFF000	/* Type1 base address for the core */
#define SSB_ADM_BASE1_SHIFT		12
#define SSB_ADM_BASE2			0xFFFF0000	/* Type2 base address for the core */
#define SSB_ADM_BASE2_SHIFT		16


#endif /* LINUX_SSB_REGS_H_ */
