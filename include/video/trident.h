
#ifndef TRIDENTFB_DEBUG
#define TRIDENTFB_DEBUG 0
#endif

#if TRIDENTFB_DEBUG
#define debug(f,a...)	printk("%s:" f,  __FUNCTION__ , ## a);mdelay(1000);
#else
#define debug(f,a...)
#endif

#define output(f, a...) pr_info("tridentfb: " f, ## a)

#define Kb	(1024)
#define Mb	(Kb*Kb)

/* PCI IDS of supported cards temporarily here */

#define CYBER9320	0x9320
#define CYBER9388	0x9388
#define CYBER9382	0x9382		/* the real PCI id for this is 9660 */
#define CYBER9385	0x9385		/* ditto */		
#define CYBER9397	0x9397
#define CYBER9397DVD	0x939A
#define CYBER9520	0x9520
#define CYBER9525DVD	0x9525
#define TGUI9660	0x9660
#define IMAGE975	0x9750
#define IMAGE985	0x9850
#define BLADE3D		0x9880
#define CYBERBLADEE4	0x9540
#define CYBERBLADEi7	0x8400
#define CYBERBLADEi7D	0x8420
#define CYBERBLADEi1	0x8500
#define CYBERBLADEi1D	0x8520
#define CYBERBLADEAi1	0x8600
#define CYBERBLADEAi1D	0x8620
#define CYBERBLADEXPAi1 0x8820
#define CYBERBLADEXPm8  0x9910
#define CYBERBLADEXPm16 0x9930

/* acceleration families */
#define IMAGE	0
#define BLADE	1
#define XP	2

#define is_image(id)	
#define is_xp(id)	((id == CYBERBLADEXPAi1) ||\
			 (id == CYBERBLADEXPm8) ||\
			 (id == CYBERBLADEXPm16)) 

#define is_blade(id)	((id == BLADE3D) ||\
			 (id == CYBERBLADEE4) ||\
			 (id == CYBERBLADEi7) ||\
			 (id == CYBERBLADEi7D) ||\
			 (id == CYBERBLADEi1) ||\
			 (id == CYBERBLADEi1D) ||\
			 (id ==	CYBERBLADEAi1) ||\
			 (id ==	CYBERBLADEAi1D))

/* these defines are for 'lcd' variable */
#define LCD_STRETCH	0
#define LCD_CENTER	1
#define LCD_BIOS	2

/* display types */
#define DISPLAY_CRT	0
#define DISPLAY_FP	1

#define flatpanel (displaytype == DISPLAY_FP)

/* General Registers */
#define SPR	0x1F		/* Software Programming Register (videoram) */

/* 3C4 */
#define RevisionID 0x09
#define OldOrNew 0x0B	
#define ConfPort1 0x0C
#define ConfPort2 0x0C
#define NewMode2 0x0D
#define NewMode1 0x0E
#define Protection 0x11
#define MCLKLow 0x16
#define MCLKHigh 0x17
#define ClockLow 0x18
#define ClockHigh 0x19
#define SSetup 0x20
#define SKey 0x37
#define SPKey 0x57

/* 0x3x4 */
#define CRTHTotal	0x00
#define CRTHDispEnd	0x01
#define CRTHBlankStart	0x02
#define CRTHBlankEnd	0x03
#define CRTHSyncStart	0x04
#define CRTHSyncEnd	0x05

#define CRTVTotal	0x06
#define CRTVDispEnd	0x12
#define CRTVBlankStart	0x15
#define CRTVBlankEnd	0x16
#define CRTVSyncStart	0x10
#define CRTVSyncEnd	0x11

#define CRTOverflow	0x07
#define CRTPRowScan	0x08
#define CRTMaxScanLine	0x09
#define CRTModeControl	0x17
#define CRTLineCompare	0x18

/* 3x4 */
#define StartAddrHigh 0x0C
#define StartAddrLow 0x0D
#define Offset 0x13
#define Underline 0x14
#define CRTCMode 0x17
#define CRTCModuleTest 0x1E
#define FIFOControl 0x20
#define LinearAddReg 0x21
#define DRAMTiming 0x23
#define New32 0x23
#define RAMDACTiming 0x25
#define CRTHiOrd 0x27
#define AddColReg 0x29
#define InterfaceSel 0x2A
#define HorizOverflow 0x2B
#define GETest 0x2D
#define Performance 0x2F
#define GraphEngReg 0x36
#define I2C 0x37
#define PixelBusReg 0x38
#define PCIReg 0x39
#define DRAMControl 0x3A
#define MiscContReg 0x3C
#define CursorXLow 0x40
#define CursorXHigh 0x41
#define CursorYLow 0x42
#define CursorYHigh 0x43
#define CursorLocLow 0x44
#define CursorLocHigh 0x45
#define CursorXOffset 0x46
#define CursorYOffset 0x47
#define CursorFG1 0x48
#define CursorFG2 0x49
#define CursorFG3 0x4A
#define CursorFG4 0x4B
#define CursorBG1 0x4C
#define CursorBG2 0x4D
#define CursorBG3 0x4E
#define CursorBG4 0x4F
#define CursorControl 0x50
#define PCIRetry 0x55
#define PreEndControl 0x56
#define PreEndFetch 0x57
#define PCIMaster 0x60
#define Enhancement0 0x62
#define NewEDO 0x64
#define TVinterface 0xC0
#define TVMode 0xC1
#define ClockControl 0xCF


/* 3CE */
#define MiscExtFunc 0x0F
#define PowerStatus 0x23
#define MiscIntContReg 0x2F
#define CyberControl 0x30
#define CyberEnhance 0x31
#define FPConfig     0x33
#define VertStretch  0x52
#define HorStretch   0x53
#define BiosMode     0x5c
#define BiosReg      0x5d

