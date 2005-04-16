/*
 * SA-1101.h
 *
 * Copyright (c) Peter Danielsson 1999
 *
 * Definition of constants related to the sa1101
 * support chip for the sa1100
 *
 */


/* Be sure that virtual mapping is defined right */
#ifndef __ASM_ARCH_HARDWARE_H
#error You must include hardware.h not SA-1101.h
#endif

#ifndef SA1101_BASE
#error You must define SA-1101 physical base address
#endif

#ifndef LANGUAGE
# ifdef __ASSEMBLY__
#  define LANGUAGE Assembly
# else
#  define LANGUAGE C
# endif
#endif

/*
 * We have mapped the sa1101 depending on the value of SA1101_BASE.
 * It then appears from 0xf4000000.
 */

#define SA1101_p2v( x )         ((x) - SA1101_BASE + 0xf4000000)
#define SA1101_v2p( x )         ((x) - 0xf4000000  + SA1101_BASE)

#ifndef SA1101_p2v
#define SA1101_p2v(PhAdd)  (PhAdd)
#endif

#include <asm/arch/bitfield.h>

#define C               0
#define Assembly        1


/*
 * Memory map
 */

#define __SHMEM_CONTROL0	0x00000000
#define __SYSTEM_CONTROL1	0x00000400
#define __ARBITER		0x00020000
#define __SYSTEM_CONTROL2	0x00040000
#define __SYSTEM_CONTROL3	0x00060000
#define __PARALLEL_PORT		0x00080000
#define __VIDMEM_CONTROL	0x00100000
#define __UPDATE_FIFO		0x00120000
#define __SHMEM_CONTROL1	0x00140000
#define __INTERRUPT_CONTROL	0x00160000
#define __USB_CONTROL		0x00180000
#define __TRACK_INTERFACE	0x001a0000
#define __MOUSE_INTERFACE	0x001b0000
#define __KEYPAD_INTERFACE	0x001c0000
#define __PCMCIA_INTERFACE	0x001e0000
#define	__VGA_CONTROL		0x00200000
#define __GPIO_INTERFACE	0x00300000

/*
 * Macro that calculates real address for registers in the SA-1101
 */

#define _SA1101( x )    ((x) + SA1101_BASE)

/*
 * Interface and shared memory controller registers
 *
 * Registers
 *	SKCR		SA-1101 control register (read/write)
 *	SMCR		Shared Memory Controller Register
 *	SNPR		Snoop Register
 */

#define _SKCR		_SA1101( 0x00000000 ) /* SA-1101 Control Reg. */
#define _SMCR		_SA1101( 0x00140000 ) /* Shared Mem. Control Reg. */
#define _SNPR		_SA1101( 0x00140400 ) /* Snoop Reg. */

#if LANGUAGE == C
#define SKCR		(*((volatile Word *) SA1101_p2v (_SKCR)))
#define SMCR		(*((volatile Word *) SA1101_p2v (_SMCR)))
#define SNPR		(*((volatile Word *) SA1101_p2v (_SNPR)))

#define SKCR_PLLEn	  0x0001	  /* Enable On-Chip PLL */
#define SKCR_BCLKEn	  0x0002	  /* Enables BCLK */
#define SKCR_Sleep	  0x0004	  /* Sleep Mode */
#define SKCR_IRefEn	  0x0008	  /* DAC Iref input enable */
#define SKCR_VCOON	  0x0010	  /* VCO bias */
#define SKCR_ScanTestEn	  0x0020	  /* Enables scan test */
#define SKCR_ClockTestEn  0x0040	  /* Enables clock test */

#define SMCR_DCAC	  Fld(2,0)	  /* Number of column address bits */
#define SMCR_DRAC	  Fld(2,2)	  /* Number of row address bits */
#define SMCR_ArbiterBias  0x0008	  /* favor video or USB */
#define SMCR_TopVidMem	  Fld(4,5)	  /* Top 4 bits of vidmem addr. */

#define SMCR_ColAdrBits( x )		  /* col. addr bits 8..11 */ \
	(( (x) - 8 ) << FShft (SMCR_DCAC))
#define SMCR_RowAdrBits( x )		  /* row addr bits 9..12 */\
	(( (x) - 9 ) << FShft (SMCR_DRAC)

#define SNPR_VFBstart	  Fld(12,0)	/* Video frame buffer addr */
#define SNPR_VFBsize	  Fld(11,12)	/* Video frame buffer size */
#define SNPR_WholeBank	  (1 << 23)	/* Whole bank bit */
#define SNPR_BankSelect	  Fld(2,27)	/* Bank select */
#define SNPR_SnoopEn	  (1 << 31)	/* Enable snoop operation */

#define SNPR_Set_VFBsize( x )   /* set frame buffer size (in kb) */ \
	( (x) << FShft (SNPR_VFBsize))
#define SNPR_Select_Bank(x)     /* select bank 0 or 1 */  \
	(( (x) + 1 ) << FShft (SNPR_BankSelect ))

#endif /* LANGUAGE == C */

/*
 * Video Memory Controller
 *
 * Registers
 *    VMCCR	Configuration register
 *    VMCAR	VMC address register
 *    VMCDR	VMC data register
 *
 */

#define _VMCCR		_SA1101( 0x00100000 )	/* Configuration register */
#define _VMCAR		_SA1101( 0x00101000 )	/* VMC address register */
#define _VMCDR		_SA1101( 0x00101400 )	/* VMC data register */

#if LANGUAGE == C
#define VMCCR		(*((volatile Word *) SA1101_p2v (_VMCCR)))
#define VMCAR		(*((volatile Word *) SA1101_p2v (_VMCAR)))
#define VMCDR		(*((volatile Word *) SA1101_p2v (_VMCDR)))

#define VMCCR_RefreshEn	    0x0000	  /* Enable memory refresh */
#define VMCCR_Config	    0x0001	  /* DRAM size */
#define VMCCR_RefPeriod	    Fld(2,3)	  /* Refresh period */
#define VMCCR_StaleDataWait Fld(4,5)	  /* Stale FIFO data timeout counter */
#define VMCCR_SleepState    (1<<9)	  /* State of interface pins in sleep*/
#define VMCCR_RefTest	    (1<<10)	  /* refresh test */
#define VMCCR_RefLow	    Fld(6,11)	  /* refresh low counter */
#define VMCCR_RefHigh	    Fld(7,17)	  /* refresh high counter */
#define VMCCR_SDTCTest	    Fld(7,24)	  /* stale data timeout counter */
#define VMCCR_ForceSelfRef  (1<<31)	  /* Force self refresh */

#endif LANGUAGE == C


/* Update FIFO
 *
 * Registers
 *    UFCR	Update FIFO Control Register
 *    UFSR	Update FIFO Status Register
 *    UFLVLR	update FIFO level register
 *    UFDR	update FIFO data register
 */

#define _UFCR	_SA1101(0x00120000)   /* Update FIFO Control Reg. */
#define _UFSR	_SA1101(0x00120400)   /* Update FIFO Status Reg. */	
#define _UFLVLR	_SA1101(0x00120800)   /* Update FIFO level reg. */
#define _UFDR	_SA1101(0x00120c00)   /* Update FIFO data reg. */

#if LANGUAGE == C

#define UFCR 	(*((volatile Word *) SA1101_p2v (_UFCR)))
#define UFSR	(*((volatile Word *) SA1101_p2v (_UFSR)))
#define UFLVLR	(*((volatile Word *) SA1101_p2v (_UFLVLR))) 
#define UFDR	(*((volatile Word *) SA1101_p2v (_UFDR)))


#define UFCR_FifoThreshhold	Fld(7,0)	/* Level for FifoGTn flag */

#define UFSR_FifoGTnFlag	0x01		/* FifoGTn flag */#define UFSR_FifoEmpty		0x80		/* FIFO is empty */

#endif /* LANGUAGE == C */

/* System Controller
 *
 * Registers
 *    SKPCR	Power Control Register
 *    SKCDR	Clock Divider Register
 *    DACDR1	DAC1 Data register
 *    DACDR2	DAC2 Data register
 */

#define _SKPCR		_SA1101(0x00000400)
#define _SKCDR		_SA1101(0x00040000)
#define _DACDR1		_SA1101(0x00060000)
#define _DACDR2		_SA1101(0x00060400)

#if LANGUAGE == C
#define SKPCR 	(*((volatile Word *) SA1101_p2v (_SKPCR)))
#define SKCDR	(*((volatile Word *) SA1101_p2v (_SKCDR)))
#define DACDR1	(*((volatile Word *) SA1101_p2v (_DACDR1)))
#define DACDR2	(*((volatile Word *) SA1101_p2v (_DACDR2)))

#define SKPCR_UCLKEn	     0x01    /* USB Enable */
#define SKPCR_PCLKEn	     0x02    /* PS/2 Enable */
#define SKPCR_ICLKEn	     0x04    /* Interrupt Controller Enable */
#define SKPCR_VCLKEn	     0x08    /* Video Controller Enable */
#define SKPCR_PICLKEn	     0x10    /* parallel port Enable */
#define SKPCR_DCLKEn	     0x20    /* DACs Enable */
#define SKPCR_nKPADEn	     0x40    /* Multiplexer */

#define SKCDR_PLLMul	     Fld(7,0)	/* PLL Multiplier */
#define SKCDR_VCLKEn	     Fld(2,7)	/* Video controller clock divider */
#define SKDCR_BCLKEn	     (1<<9)	/* BCLK Divider */
#define SKDCR_UTESTCLKEn     (1<<10)	/* Route USB clock during test mode */
#define SKDCR_DivRValue	     Fld(6,11)	/* Input clock divider for PLL */
#define SKDCR_DivNValue	     Fld(5,17)	/* Output clock divider for PLL */
#define SKDCR_PLLRSH	     Fld(3,22)	/* PLL bandwidth control */
#define SKDCR_ChargePump     (1<<25)	/* Charge pump control */
#define SKDCR_ClkTestMode    (1<<26)	/* Clock output test mode */
#define SKDCR_ClkTestEn	     (1<<27)	/* Test clock generator */
#define SKDCR_ClkJitterCntl  Fld(3,28)	/* video clock jitter compensation */

#define DACDR_DACCount	     Fld(8,0)	/* Count value */
#define DACDR1_DACCount	     DACDR_DACCount
#define DACDR2_DACCount	     DACDR_DACCount

#endif /* LANGUAGE == C */

/*
 * Parallel Port Interface
 *
 * Registers
 *    IEEE_Config	IEEE mode selection and programmable attributes
 *    IEEE_Control	Controls the states of IEEE port control outputs
 *    IEEE_Data		Forward transfer data register
 *    IEEE_Addr		Forward transfer address register
 *    IEEE_Status	Port IO signal status register
 *    IEEE_IntStatus	Port interrupts status register
 *    IEEE_FifoLevels   Rx and Tx FIFO interrupt generation levels
 *    IEEE_InitTime	Forward timeout counter initial value
 *    IEEE_TimerStatus	Forward timeout counter current value
 *    IEEE_FifoReset	Reset forward transfer FIFO
 *    IEEE_ReloadValue	Counter reload value
 *    IEEE_TestControl	Control testmode
 *    IEEE_TestDataIn	Test data register
 *    IEEE_TestDataInEn	Enable test data
 *    IEEE_TestCtrlIn	Test control signals
 *    IEEE_TestCtrlInEn	Enable test control signals
 *    IEEE_TestDataStat	Current data bus value
 *
 */

/*
 * The control registers are defined as offsets from a base address 
 */
 
#define _IEEE( x ) _SA1101( (x) + __PARALLEL_PORT )

#define _IEEE_Config	    _IEEE( 0x0000 )
#define _IEEE_Control	    _IEEE( 0x0400 )
#define _IEEE_Data	    _IEEE( 0x4000 )
#define _IEEE_Addr	    _IEEE( 0x0800 )
#define _IEEE_Status	    _IEEE( 0x0c00 )
#define _IEEE_IntStatus	    _IEEE( 0x1000 )
#define _IEEE_FifoLevels    _IEEE( 0x1400 )
#define _IEEE_InitTime	    _IEEE( 0x1800 )
#define _IEEE_TimerStatus   _IEEE( 0x1c00 )
#define _IEEE_FifoReset	    _IEEE( 0x2000 )
#define _IEEE_ReloadValue   _IEEE( 0x3c00 )
#define _IEEE_TestControl   _IEEE( 0x2400 )
#define _IEEE_TestDataIn    _IEEE( 0x2800 )
#define _IEEE_TestDataInEn  _IEEE( 0x2c00 )
#define _IEEE_TestCtrlIn    _IEEE( 0x3000 )
#define _IEEE_TestCtrlInEn  _IEEE( 0x3400 )
#define _IEEE_TestDataStat  _IEEE( 0x3800 )
 

#if LANGUAGE == C
#define IEEE_Config	    (*((volatile Word *) SA1101_p2v (_IEEE_Config)))
#define IEEE_Control	    (*((volatile Word *) SA1101_p2v (_IEEE_Control)))
#define IEEE_Data	    (*((volatile Word *) SA1101_p2v (_IEEE_Data)))
#define IEEE_Addr	    (*((volatile Word *) SA1101_p2v (_IEEE_Addr)))
#define IEEE_Status	    (*((volatile Word *) SA1101_p2v (_IEEE_Status)))
#define IEEE_IntStatus	    (*((volatile Word *) SA1101_p2v (_IEEE_IntStatus)))
#define IEEE_FifoLevels	    (*((volatile Word *) SA1101_p2v (_IEEE_FifoLevels)))
#define IEEE_InitTime	    (*((volatile Word *) SA1101_p2v (_IEEE_InitTime)))
#define IEEE_TimerStatus    (*((volatile Word *) SA1101_p2v (_IEEE_TimerStatus)))
#define IEEE_FifoReset	    (*((volatile Word *) SA1101_p2v (_IEEE_FifoReset)))
#define IEEE_ReloadValue    (*((volatile Word *) SA1101_p2v (_IEEE_ReloadValue)))
#define IEEE_TestControl    (*((volatile Word *) SA1101_p2v (_IEEE_TestControl)))
#define IEEE_TestDataIn     (*((volatile Word *) SA1101_p2v (_IEEE_TestDataIn)))
#define IEEE_TestDataInEn   (*((volatile Word *) SA1101_p2v (_IEEE_TestDataInEn)))
#define IEEE_TestCtrlIn     (*((volatile Word *) SA1101_p2v (_IEEE_TestCtrlIn)))
#define IEEE_TestCtrlInEn   (*((volatile Word *) SA1101_p2v (_IEEE_TestCtrlInEn)))
#define IEEE_TestDataStat   (*((volatile Word *) SA1101_p2v (_IEEE_TestDataStat)))


#define IEEE_Config_M	    Fld(3,0)	 /* Mode select */
#define IEEE_Config_D	    0x04	 /* FIFO access enable */
#define IEEE_Config_B	    0x08	 /* 9-bit word enable */
#define IEEE_Config_T	    0x10	 /* Data transfer enable */
#define IEEE_Config_A	    0x20	 /* Data transfer direction */
#define IEEE_Config_E	    0x40	 /* Timer enable */
#define IEEE_Control_A	    0x08	 /* AutoFd output */
#define IEEE_Control_E	    0x04	 /* Selectin output */
#define IEEE_Control_T	    0x02	 /* Strobe output */
#define IEEE_Control_I	    0x01	 /* Port init output */
#define IEEE_Data_C	    (1<<31)	 /* Byte count */
#define IEEE_Data_Db	    Fld(9,16)	 /* Data byte 2 */
#define IEEE_Data_Da	    Fld(9,0)	 /* Data byte 1 */
#define IEEE_Addr_A	    Fld(8,0)	 /* forward address transfer byte */
#define IEEE_Status_A	    0x0100	 /* nAutoFd port output status */
#define IEEE_Status_E	    0x0080	 /* nSelectIn port output status */
#define IEEE_Status_T	    0x0040	 /* nStrobe port output status */
#define IEEE_Status_I	    0x0020	 /* nInit port output status */
#define IEEE_Status_B	    0x0010	 /* Busy port inout status */
#define IEEE_Status_S	    0x0008	 /* Select port input status */
#define IEEE_Status_K	    0x0004	 /* nAck port input status */
#define IEEE_Status_F	    0x0002	 /* nFault port input status */
#define IEEE_Status_R	    0x0001	 /* pError port input status */

#define IEEE_IntStatus_IntReqDat	 0x0100
#define IEEE_IntStatus_IntReqEmp	 0x0080
#define IEEE_IntStatus_IntReqInt	 0x0040
#define IEEE_IntStatus_IntReqRav	 0x0020
#define IEEE_IntStatus_IntReqTim	 0x0010
#define IEEE_IntStatus_RevAddrComp	 0x0008
#define IEEE_IntStatus_RevDataComp	 0x0004
#define IEEE_IntStatus_FwdAddrComp	 0x0002
#define IEEE_IntStatus_FwdDataComp	 0x0001
#define IEEE_FifoLevels_RevFifoLevel	 2
#define IEEE_FifoLevels_FwdFifoLevel	 1
#define IEEE_InitTime_TimValInit	 Fld(22,0)
#define IEEE_TimerStatus_TimValStat	 Fld(22,0)
#define IEEE_ReloadValue_Reload		 Fld(4,0)

#define IEEE_TestControl_RegClk		 0x04
#define IEEE_TestControl_ClockSelect	 Fld(2,1)
#define IEEE_TestControl_TimerTestModeEn 0x01
#define IEEE_TestCtrlIn_PError		 0x10
#define IEEE_TestCtrlIn_nFault		 0x08
#define IEEE_TestCtrlIn_nAck		 0x04
#define IEEE_TestCtrlIn_PSel		 0x02
#define IEEE_TestCtrlIn_Busy		 0x01

#endif /* LANGUAGE == C */

/*
 * VGA Controller
 *
 * Registers
 *    VideoControl	Video Control Register
 *    VgaTiming0	VGA Timing Register 0
 *    VgaTiming1	VGA Timing Register 1
 *    VgaTiming2	VGA Timing Register 2
 *    VgaTiming3	VGA Timing Register 3
 *    VgaBorder		VGA Border Color Register
 *    VgaDBAR		VGADMA Base Address Register
 *    VgaDCAR		VGADMA Channel Current Address Register
 *    VgaStatus		VGA Status Register
 *    VgaInterruptMask	VGA Interrupt Mask Register
 *    VgaPalette	VGA Palette Registers
 *    DacControl	DAC Control Register
 *    VgaTest		VGA Controller Test Register
 */

#define _VGA( x )	_SA1101( ( x ) + __VGA_CONTROL )

#define _VideoControl	    _VGA( 0x0000 )
#define _VgaTiming0	    _VGA( 0x0400 )
#define _VgaTiming1	    _VGA( 0x0800 )
#define _VgaTiming2	    _VGA( 0x0c00 )
#define _VgaTiming3	    _VGA( 0x1000 )
#define _VgaBorder	    _VGA( 0x1400 )
#define _VgaDBAR	    _VGA( 0x1800 )
#define _VgaDCAR	    _VGA( 0x1c00 )
#define _VgaStatus	    _VGA( 0x2000 )
#define _VgaInterruptMask   _VGA( 0x2400 )
#define _VgaPalette	    _VGA( 0x40000 )
#define _DacControl	    _VGA( 0x3000 )
#define _VgaTest	    _VGA( 0x2c00 )

#if (LANGUAGE == C)
#define VideoControl   (*((volatile Word *) SA1101_p2v (_VideoControl)))
#define VgaTiming0     (*((volatile Word *) SA1101_p2v (_VgaTiming0)))
#define VgaTiming1     (*((volatile Word *) SA1101_p2v (_VgaTiming1)))
#define VgaTiming2     (*((volatile Word *) SA1101_p2v (_VgaTiming2)))
#define VgaTiming3     (*((volatile Word *) SA1101_p2v (_VgaTiming3)))
#define VgaBorder      (*((volatile Word *) SA1101_p2v (_VgaBorder)))
#define VgaDBAR	       (*((volatile Word *) SA1101_p2v (_VgaDBAR)))
#define VgaDCAR	       (*((volatile Word *) SA1101_p2v (_VgaDCAR)))
#define VgaStatus      (*((volatile Word *) SA1101_p2v (_VgaStatus)))
#define VgaInterruptMask (*((volatile Word *) SA1101_p2v (_VgaInterruptMask)))
#define VgaPalette     (*((volatile Word *) SA1101_p2v (_VgaPalette)))
#define DacControl     (*((volatile Word *) SA1101_p2v (_DacControl))
#define VgaTest        (*((volatile Word *) SA1101_p2v (_VgaTest)))

#define VideoControl_VgaEn    0x00000000
#define VideoControl_BGR      0x00000001
#define VideoControl_VCompVal Fld(2,2)
#define VideoControl_VgaReq   Fld(4,4)
#define VideoControl_VBurstL  Fld(4,8)
#define VideoControl_VMode    (1<<12)
#define VideoControl_PalRead  (1<<13)

#define VgaTiming0_PPL	      Fld(6,2)
#define VgaTiming0_HSW	      Fld(8,8)
#define VgaTiming0_HFP	      Fld(8,16)
#define VgaTiming0_HBP	      Fld(8,24)

#define VgaTiming1_LPS	      Fld(10,0)
#define VgaTiming1_VSW	      Fld(6,10)
#define VgaTiming1_VFP	      Fld(8,16)
#define VgaTiming1_VBP	      Fld(8,24)

#define VgaTiming2_IVS	      0x01
#define VgaTiming2_IHS	      0x02
#define VgaTiming2_CVS	      0x04
#define VgaTiming2_CHS	      0x08

#define VgaTiming3_HBS	      Fld(8,0)
#define VgaTiming3_HBE	      Fld(8,8)
#define VgaTiming3_VBS	      Fld(8,16)
#define VgaTiming3_VBE	      Fld(8,24)

#define VgaBorder_BCOL	      Fld(24,0)

#define VgaStatus_VFUF	      0x01
#define VgaStatus_VNext	      0x02
#define VgaStatus_VComp	      0x04

#define VgaInterruptMask_VFUFMask   0x00
#define VgaInterruptMask_VNextMask  0x01
#define VgaInterruptMask_VCompMask  0x02

#define VgaPalette_R	      Fld(8,0)
#define VgaPalette_G	      Fld(8,8)
#define VgaPalette_B	      Fld(8,16)

#define DacControl_DACON      0x0001
#define DacControl_COMPON     0x0002
#define DacControl_PEDON      0x0004
#define DacControl_RTrim      Fld(5,4)
#define DacControl_GTrim      Fld(5,9)
#define DacControl_BTrim      Fld(5,14)

#define VgaTest_TDAC	      0x00
#define VgaTest_Datatest      Fld(4,1)
#define VgaTest_DACTESTDAC    0x10
#define VgaTest_DACTESTOUT    Fld(3,5)

#endif /* LANGUAGE == C */

/*
 * USB Host Interface Controller
 *
 * Registers
 *    Revision
 *    Control
 *    CommandStatus
 *    InterruptStatus
 *    InterruptEnable
 *    HCCA
 *    PeriodCurrentED
 *    ControlHeadED
 *    BulkHeadED
 *    BulkCurrentED
 *    DoneHead
 *    FmInterval
 *    FmRemaining
 *    FmNumber
 *    PeriodicStart
 *    LSThreshold
 *    RhDescriptorA
 *    RhDescriptorB
 *    RhStatus
 *    RhPortStatus
 *    USBStatus
 *    USBReset
 *    USTAR
 *    USWER
 *    USRFR
 *    USNFR
 *    USTCSR
 *    USSR
 *    
 */

#define _USB( x )	_SA1101( ( x ) + __USB_CONTROL )


#define _Revision	  _USB( 0x0000 )
#define _Control	  _USB( 0x0888 )
#define _CommandStatus	  _USB( 0x0c00 )
#define _InterruptStatus  _USB( 0x1000 )
#define _InterruptEnable  _USB( 0x1400 )
#define _HCCA		  _USB( 0x1800 )
#define _PeriodCurrentED  _USB( 0x1c00 )
#define _ControlHeadED	  _USB( 0x2000 )
#define _BulkHeadED	  _USB( 0x2800 )
#define _BulkCurrentED	  _USB( 0x2c00 )
#define _DoneHead	  _USB( 0x3000 )
#define _FmInterval	  _USB( 0x3400 )
#define _FmRemaining	  _USB( 0x3800 )
#define _FmNumber	  _USB( 0x3c00 )
#define _PeriodicStart	  _USB( 0x4000 )
#define _LSThreshold	  _USB( 0x4400 )
#define _RhDescriptorA	  _USB( 0x4800 )
#define _RhDescriptorB	  _USB( 0x4c00 )
#define _RhStatus	  _USB( 0x5000 )
#define _RhPortStatus	  _USB( 0x5400 )
#define _USBStatus	  _USB( 0x11800 )
#define _USBReset	  _USB( 0x11c00 )

#define _USTAR		  _USB( 0x10400 )
#define _USWER		  _USB( 0x10800 )
#define _USRFR		  _USB( 0x10c00 )
#define _USNFR		  _USB( 0x11000 )
#define _USTCSR		  _USB( 0x11400 )
#define _USSR		  _USB( 0x11800 )


#if (LANGUAGE == C)

#define Revision	(*((volatile Word *) SA1101_p2v (_Revision)))
#define Control		(*((volatile Word *) SA1101_p2v (_Control)))
#define CommandStatus	(*((volatile Word *) SA1101_p2v (_CommandStatus)))
#define InterruptStatus	(*((volatile Word *) SA1101_p2v (_InterruptStatus)))
#define InterruptEnable	(*((volatile Word *) SA1101_p2v (_InterruptEnable)))
#define HCCA		(*((volatile Word *) SA1101_p2v (_HCCA)))
#define PeriodCurrentED	(*((volatile Word *) SA1101_p2v (_PeriodCurrentED)))
#define ControlHeadED	(*((volatile Word *) SA1101_p2v (_ControlHeadED)))
#define BulkHeadED	(*((volatile Word *) SA1101_p2v (_BulkHeadED)))
#define BulkCurrentED	(*((volatile Word *) SA1101_p2v (_BulkCurrentED)))
#define DoneHead	(*((volatile Word *) SA1101_p2v (_DoneHead)))
#define FmInterval	(*((volatile Word *) SA1101_p2v (_FmInterval)))
#define FmRemaining	(*((volatile Word *) SA1101_p2v (_FmRemaining)))
#define FmNumber	(*((volatile Word *) SA1101_p2v (_FmNumber)))
#define PeriodicStart	(*((volatile Word *) SA1101_p2v (_PeriodicStart)))
#define LSThreshold	(*((volatile Word *) SA1101_p2v (_LSThreshold)))
#define RhDescriptorA	(*((volatile Word *) SA1101_p2v (_RhDescriptorA)))
#define RhDescriptorB	(*((volatile Word *) SA1101_p2v (_RhDescriptorB)))
#define RhStatus	(*((volatile Word *) SA1101_p2v (_RhStatus)))
#define RhPortStatus	(*((volatile Word *) SA1101_p2v (_RhPortStatus)))
#define USBStatus	(*((volatile Word *) SA1101_p2v (_USBStatus)))
#define USBReset	(*((volatile Word *) SA1101_p2v (_USBReset)))
#define USTAR		(*((volatile Word *) SA1101_p2v (_USTAR)))
#define USWER		(*((volatile Word *) SA1101_p2v (_USWER)))
#define USRFR		(*((volatile Word *) SA1101_p2v (_USRFR)))
#define USNFR		(*((volatile Word *) SA1101_p2v (_USNFR)))
#define USTCSR		(*((volatile Word *) SA1101_p2v (_USTCSR)))
#define USSR		(*((volatile Word *) SA1101_p2v (_USSR)))


#define USBStatus_IrqHciRmtWkp	     (1<<7)
#define USBStatus_IrqHciBuffAcc	     (1<<8)
#define USBStatus_nIrqHciM	     (1<<9)
#define USBStatus_nHciMFClr	     (1<<10)

#define USBReset_ForceIfReset	     0x01
#define USBReset_ForceHcReset	     0x02
#define USBReset_ClkGenReset	     0x04

#define USTCR_RdBstCntrl	     Fld(3,0)
#define USTCR_ByteEnable	     Fld(4,3)
#define USTCR_WriteEn		     (1<<7)
#define USTCR_FifoCir		     (1<<8)
#define USTCR_TestXferSel	     (1<<9)
#define USTCR_FifoCirAtEnd	     (1<<10)
#define USTCR_nSimScaleDownClk	     (1<<11)

#define USSR_nAppMDEmpty	     0x01
#define USSR_nAppMDFirst	     0x02
#define USSR_nAppMDLast		     0x04
#define USSR_nAppMDFull		     0x08
#define USSR_nAppMAFull		     0x10
#define USSR_XferReq		     0x20
#define USSR_XferEnd		     0x40

#endif /* LANGUAGE == C */


/*
 * Interrupt Controller
 *
 * Registers
 *    INTTEST0		Test register 0
 *    INTTEST1		Test register 1
 *    INTENABLE0	Interrupt Enable register 0
 *    INTENABLE1	Interrupt Enable register 1
 *    INTPOL0		Interrupt Polarity selection 0
 *    INTPOL1		Interrupt Polarity selection 1
 *    INTTSTSEL		Interrupt source selection
 *    INTSTATCLR0	Interrupt Status 0
 *    INTSTATCLR1	Interrupt Status 1
 *    INTSET0		Interrupt Set 0
 *    INTSET1		Interrupt Set 1
 */

#define _INT( x )	_SA1101( ( x ) + __INTERRUPT_CONTROL)

#define _INTTEST0	_INT( 0x1000 )
#define _INTTEST1	_INT( 0x1400 )
#define _INTENABLE0	_INT( 0x2000 )
#define _INTENABLE1	_INT( 0x2400 )
#define _INTPOL0	_INT( 0x3000 )
#define _INTPOL1	_INT( 0x3400 )
#define _INTTSTSEL     	_INT( 0x5000 )
#define _INTSTATCLR0	_INT( 0x6000 )
#define _INTSTATCLR1	_INT( 0x6400 )
#define _INTSET0	_INT( 0x7000 )
#define _INTSET1	_INT( 0x7400 )

#if ( LANGUAGE == C )
#define INTTEST0	(*((volatile Word *) SA1101_p2v (_INTTEST0)))
#define INTTEST1	(*((volatile Word *) SA1101_p2v (_INTTEST1)))
#define INTENABLE0	(*((volatile Word *) SA1101_p2v (_INTENABLE0)))
#define INTENABLE1	(*((volatile Word *) SA1101_p2v (_INTENABLE1)))
#define INTPOL0		(*((volatile Word *) SA1101_p2v (_INTPOL0)))
#define INTPOL1		(*((volatile Word *) SA1101_p2v (_INTPOL1)))
#define INTTSTSEL	(*((volatile Word *) SA1101_p2v (_INTTSTSEL)))
#define INTSTATCLR0	(*((volatile Word *) SA1101_p2v (_INTSTATCLR0)))
#define INTSTATCLR1	(*((volatile Word *) SA1101_p2v (_INTSTATCLR1)))
#define INTSET0		(*((volatile Word *) SA1101_p2v (_INTSET0)))
#define INTSET1		(*((volatile Word *) SA1101_p2v (_INTSET1)))

#endif /* LANGUAGE == C */

/*
 * PS/2 Trackpad and Mouse Interfaces
 *
 * Registers   (prefix kbd applies to trackpad interface, mse to mouse)
 *    KBDCR		Control Register
 *    KBDSTAT		Status Register
 *    KBDDATA		Transmit/Receive Data register
 *    KBDCLKDIV		Clock Division Register
 *    KBDPRECNT		Clock Precount Register
 *    KBDTEST1		Test register 1
 *    KBDTEST2		Test register 2
 *    KBDTEST3		Test register 3
 *    KBDTEST4		Test register 4
 *    MSECR	
 *    MSESTAT
 *    MSEDATA
 *    MSECLKDIV
 *    MSEPRECNT
 *    MSETEST1
 *    MSETEST2
 *    MSETEST3
 *    MSETEST4
 *     
 */

#define _KBD( x )	_SA1101( ( x ) + __TRACK_INTERFACE )
#define _MSE( x )	_SA1101( ( x ) + __MOUSE_INTERFACE )

#define _KBDCR		_KBD( 0x0000 )
#define _KBDSTAT	_KBD( 0x0400 )
#define _KBDDATA	_KBD( 0x0800 )
#define _KBDCLKDIV	_KBD( 0x0c00 )
#define _KBDPRECNT	_KBD( 0x1000 )
#define	_KBDTEST1	_KBD( 0x2000 )
#define _KBDTEST2	_KBD( 0x2400 )
#define _KBDTEST3	_KBD( 0x2800 )
#define _KBDTEST4	_KBD( 0x2c00 )
#define _MSECR		_MSE( 0x0000 )
#define _MSESTAT	_MSE( 0x0400 )
#define _MSEDATA	_MSE( 0x0800 )
#define _MSECLKDIV	_MSE( 0x0c00 )
#define _MSEPRECNT	_MSE( 0x1000 )
#define	_MSETEST1	_MSE( 0x2000 )
#define _MSETEST2	_MSE( 0x2400 )
#define _MSETEST3	_MSE( 0x2800 )
#define _MSETEST4	_MSE( 0x2c00 )

#if ( LANGUAGE == C )

#define KBDCR	    (*((volatile Word *) SA1101_p2v (_KBDCR)))
#define KBDSTAT	    (*((volatile Word *) SA1101_p2v (_KBDSTAT)))
#define KBDDATA	    (*((volatile Word *) SA1101_p2v (_KBDDATA)))
#define KBDCLKDIV   (*((volatile Word *) SA1101_p2v (_KBDCLKDIV)))
#define KBDPRECNT   (*((volatile Word *) SA1101_p2v (_KBDPRECNT)))
#define KBDTEST1    (*((volatile Word *) SA1101_p2v (_KBDTEST1)))
#define KBDTEST2    (*((volatile Word *) SA1101_p2v (_KBDTEST2)))
#define KBDTEST3    (*((volatile Word *) SA1101_p2v (_KBDTEST3)))
#define KBDTEST4    (*((volatile Word *) SA1101_p2v (_KBDTEST4)))
#define MSECR	    (*((volatile Word *) SA1101_p2v (_MSECR)))
#define MSESTAT	    (*((volatile Word *) SA1101_p2v (_MSESTAT)))
#define MSEDATA	    (*((volatile Word *) SA1101_p2v (_MSEDATA)))
#define MSECLKDIV   (*((volatile Word *) SA1101_p2v (_MSECLKDIV)))
#define MSEPRECNT   (*((volatile Word *) SA1101_p2v (_MSEPRECNT)))
#define MSETEST1    (*((volatile Word *) SA1101_p2v (_MSETEST1)))
#define MSETEST2    (*((volatile Word *) SA1101_p2v (_MSETEST2)))
#define MSETEST3    (*((volatile Word *) SA1101_p2v (_MSETEST3)))
#define MSETEST4    (*((volatile Word *) SA1101_p2v (_MSETEST4)))


#define KBDCR_ENA		 0x08
#define KBDCR_FKD		 0x02
#define KBDCR_FKC		 0x01

#define KBDSTAT_TXE		 0x80
#define KBDSTAT_TXB		 0x40
#define KBDSTAT_RXF		 0x20
#define KBDSTAT_RXB		 0x10
#define KBDSTAT_ENA		 0x08
#define KBDSTAT_RXP		 0x04
#define KBDSTAT_KBD		 0x02
#define KBDSTAT_KBC		 0x01

#define KBDCLKDIV_DivVal	 Fld(4,0)

#define MSECR_ENA		 0x08
#define MSECR_FKD		 0x02
#define MSECR_FKC		 0x01

#define MSESTAT_TXE		 0x80
#define MSESTAT_TXB		 0x40
#define MSESTAT_RXF		 0x20
#define MSESTAT_RXB		 0x10
#define MSESTAT_ENA		 0x08
#define MSESTAT_RXP		 0x04	
#define MSESTAT_MSD		 0x02
#define MSESTAT_MSC		 0x01

#define MSECLKDIV_DivVal	 Fld(4,0)

#define KBDTEST1_CD		 0x80
#define KBDTEST1_RC1		 0x40
#define KBDTEST1_MC		 0x20
#define KBDTEST1_C		 Fld(2,3)
#define KBDTEST1_T2		 0x40
#define KBDTEST1_T1		 0x20
#define KBDTEST1_T0		 0x10
#define KBDTEST2_TICBnRES	 0x08
#define KBDTEST2_RKC		 0x04
#define KBDTEST2_RKD		 0x02
#define KBDTEST2_SEL		 0x01
#define KBDTEST3_ms_16		 0x80
#define KBDTEST3_us_64		 0x40
#define KBDTEST3_us_16		 0x20
#define KBDTEST3_DIV8		 0x10
#define KBDTEST3_DIn		 0x08
#define KBDTEST3_CIn		 0x04
#define KBDTEST3_KD		 0x02
#define KBDTEST3_KC		 0x01
#define KBDTEST4_BC12		 0x80
#define KBDTEST4_BC11		 0x40
#define KBDTEST4_TRES		 0x20
#define KBDTEST4_CLKOE		 0x10
#define KBDTEST4_CRES		 0x08
#define KBDTEST4_RXB		 0x04
#define KBDTEST4_TXB		 0x02
#define KBDTEST4_SRX		 0x01

#define MSETEST1_CD		 0x80
#define MSETEST1_RC1		 0x40
#define MSETEST1_MC		 0x20
#define MSETEST1_C		 Fld(2,3)
#define MSETEST1_T2		 0x40
#define MSETEST1_T1		 0x20
#define MSETEST1_T0		 0x10
#define MSETEST2_TICBnRES	 0x08
#define MSETEST2_RKC		 0x04
#define MSETEST2_RKD		 0x02
#define MSETEST2_SEL		 0x01
#define MSETEST3_ms_16		 0x80
#define MSETEST3_us_64		 0x40
#define MSETEST3_us_16		 0x20
#define MSETEST3_DIV8		 0x10
#define MSETEST3_DIn		 0x08
#define MSETEST3_CIn		 0x04
#define MSETEST3_KD		 0x02
#define MSETEST3_KC		 0x01
#define MSETEST4_BC12		 0x80
#define MSETEST4_BC11		 0x40
#define MSETEST4_TRES		 0x20
#define MSETEST4_CLKOE		 0x10
#define MSETEST4_CRES		 0x08
#define MSETEST4_RXB		 0x04
#define MSETEST4_TXB		 0x02
#define MSETEST4_SRX		 0x01

#endif  /* LANGUAGE == C */


/*
 * General-Purpose I/O Interface
 *
 * Registers
 *    PADWR	Port A Data Write Register
 *    PBDWR	Port B Data Write Register
 *    PADRR	Port A Data Read Register
 *    PBDRR	Port B Data Read Register
 *    PADDR	Port A Data Direction Register
 *    PBDDR	Port B Data Direction Register
 *    PASSR	Port A Sleep State Register
 *    PBSSR	Port B Sleep State Register
 *
 */

#define _PIO( x )      _SA1101( ( x ) + __GPIO_INTERFACE )

#define _PADWR	       _PIO( 0x0000 )
#define _PBDWR	       _PIO( 0x0400 )
#define _PADRR	       _PIO( 0x0000 )
#define _PBDRR	       _PIO( 0x0400 )
#define _PADDR	       _PIO( 0x0800 )
#define _PBDDR	       _PIO( 0x0c00 )
#define _PASSR	       _PIO( 0x1000 )
#define _PBSSR	       _PIO( 0x1400 )


#if ( LANGUAGE == C )


#define PADWR	    (*((volatile Word *) SA1101_p2v (_PADWR)))
#define PBDWR	    (*((volatile Word *) SA1101_p2v (_PBDWR)))
#define PADRR	    (*((volatile Word *) SA1101_p2v (_PADRR)))
#define PBDRR	    (*((volatile Word *) SA1101_p2v (_PBDRR)))
#define PADDR	    (*((volatile Word *) SA1101_p2v (_PADDR)))
#define PBDDR	    (*((volatile Word *) SA1101_p2v (_PBDDR)))
#define PASSR	    (*((volatile Word *) SA1101_p2v (_PASSR)))
#define PBSSR	    (*((volatile Word *) SA1101_p2v (_PBSSR)))

#endif



/*
 * Keypad Interface
 *
 * Registers
 *    PXDWR
 *    PXDRR
 *    PYDWR
 *    PYDRR
 *
 */

#define _KEYPAD( x )	_SA1101( ( x ) + __KEYPAD_INTERFACE ) 

#define _PXDWR	   _KEYPAD( 0x0000 )
#define _PXDRR	   _KEYPAD( 0x0000 )
#define _PYDWR	   _KEYPAD( 0x0400 )
#define _PYDRR	   _KEYPAD( 0x0400 )

#if ( LANGUAGE == C )


#define PXDWR	    (*((volatile Word *) SA1101_p2v (_PXDWR)))
#define PXDRR	    (*((volatile Word *) SA1101_p2v (_PXDRR)))
#define PYDWR	    (*((volatile Word *) SA1101_p2v (_PYDWR)))
#define PYDRR	    (*((volatile Word *) SA1101_p2v (_PYDRR)))

#endif



/*
 * PCMCIA Interface
 *
 * Registers
 *    PCSR	Status Register
 *    PCCR	Control Register
 *    PCSSR	Sleep State Register
 *
 */

#define _CARD( x )	_SA1101( ( x ) + __PCMCIA_INTERFACE )

#define _PCSR	   _CARD( 0x0000 )
#define _PCCR	   _CARD( 0x0400 )
#define _PCSSR	   _CARD( 0x0800 )

#if ( LANGUAGE == C )
#define PCSR    (*((volatile Word *) SA1101_p2v (_PCSR)))
#define PCCR	(*((volatile Word *) SA1101_p2v (_PCCR)))
#define PCSSR	(*((volatile Word *) SA1101_p2v (_PCSSR)))

#define PCSR_S0_ready		0x0001
#define PCSR_S1_ready		0x0002
#define PCSR_S0_detected	0x0004
#define PCSR_S1_detected	0x0008
#define PCSR_S0_VS1		0x0010
#define PCSR_S0_VS2		0x0020
#define PCSR_S1_VS1		0x0040
#define PCSR_S1_VS2		0x0080
#define PCSR_S0_WP		0x0100
#define PCSR_S1_WP		0x0200
#define PCSR_S0_BVD1_nSTSCHG	0x0400
#define PCSR_S0_BVD2_nSPKR	0x0800
#define PCSR_S1_BVD1_nSTSCHG	0x1000
#define PCSR_S1_BVD2_nSPKR	0x2000

#define PCCR_S0_VPP0		0x0001
#define PCCR_S0_VPP1		0x0002
#define PCCR_S0_VCC0		0x0004
#define PCCR_S0_VCC1		0x0008
#define PCCR_S1_VPP0		0x0010
#define PCCR_S1_VPP1		0x0020
#define PCCR_S1_VCC0		0x0040
#define PCCR_S1_VCC1		0x0080
#define PCCR_S0_reset		0x0100
#define PCCR_S1_reset		0x0200
#define PCCR_S0_float		0x0400
#define PCCR_S1_float		0x0800

#define PCSSR_S0_VCC0		0x0001
#define PCSSR_S0_VCC1		0x0002
#define PCSSR_S0_VPP0		0x0004
#define PCSSR_S0_VPP1		0x0008
#define PCSSR_S0_control	0x0010
#define PCSSR_S1_VCC0		0x0020
#define PCSSR_S1_VCC1		0x0040
#define PCSSR_S1_VPP0		0x0080
#define PCSSR_S1_VPP1		0x0100
#define PCSSR_S1_control	0x0200

#endif

#undef C
#undef Assembly
