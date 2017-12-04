/* SPDX-License-Identifier: GPL-2.0 */

#ifndef TRIDENTFB_DEBUG
#define TRIDENTFB_DEBUG 0
#endif

#if TRIDENTFB_DEBUG
#define debug(f, a...)	printk("%s:" f,  __func__ , ## a);
#else
#define debug(f, a...)
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
#define TGUI9440	0x9440
#define TGUI9660	0x9660
#define PROVIDIA9685	0x9685
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

/* these defines are for 'lcd' variable */
#define LCD_STRETCH	0
#define LCD_CENTER	1
#define LCD_BIOS	2

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

/* 3x4 */
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

/* Graphics Engine */
#define STATUS	0x2120
#define OLDCMD	0x2124
#define DRAWFL	0x2128
#define OLDCLR	0x212C
#define OLDDST	0x2138
#define OLDSRC	0x213C
#define OLDDIM	0x2140
#define CMD	0x2144
#define ROP	0x2148
#define COLOR	0x2160
#define BGCOLOR	0x2164
#define SRC1	0x2100
#define SRC2	0x2104
#define DST1	0x2108
#define DST2	0x210C

#define ROP_S	0xCC
#define ROP_P	0xF0
#define ROP_X	0x66
