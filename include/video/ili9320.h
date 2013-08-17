/* include/video/ili9320.c
 *
 * ILI9320 LCD controller configuration control.
 *
 * Copyright 2007 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define ILI9320_REG(x)	(x)

#define ILI9320_INDEX			ILI9320_REG(0x00)

#define ILI9320_OSCILATION		ILI9320_REG(0x00)
#define ILI9320_DRIVER			ILI9320_REG(0x01)
#define ILI9320_DRIVEWAVE		ILI9320_REG(0x02)
#define ILI9320_ENTRYMODE		ILI9320_REG(0x03)
#define ILI9320_RESIZING		ILI9320_REG(0x04)
#define ILI9320_DISPLAY1		ILI9320_REG(0x07)
#define ILI9320_DISPLAY2		ILI9320_REG(0x08)
#define ILI9320_DISPLAY3		ILI9320_REG(0x09)
#define ILI9320_DISPLAY4		ILI9320_REG(0x0A)
#define ILI9320_RGB_IF1			ILI9320_REG(0x0C)
#define ILI9320_FRAMEMAKER		ILI9320_REG(0x0D)
#define ILI9320_RGB_IF2			ILI9320_REG(0x0F)

#define ILI9320_POWER1			ILI9320_REG(0x10)
#define ILI9320_POWER2			ILI9320_REG(0x11)
#define ILI9320_POWER3			ILI9320_REG(0x12)
#define ILI9320_POWER4			ILI9320_REG(0x13)
#define ILI9320_GRAM_HORIZ_ADDR		ILI9320_REG(0x20)
#define ILI9320_GRAM_VERT_ADD		ILI9320_REG(0x21)
#define ILI9320_POWER7			ILI9320_REG(0x29)
#define ILI9320_FRAME_RATE_COLOUR	ILI9320_REG(0x2B)

#define ILI9320_GAMMA1			ILI9320_REG(0x30)
#define ILI9320_GAMMA2			ILI9320_REG(0x31)
#define ILI9320_GAMMA3			ILI9320_REG(0x32)
#define ILI9320_GAMMA4			ILI9320_REG(0x35)
#define ILI9320_GAMMA5			ILI9320_REG(0x36)
#define ILI9320_GAMMA6			ILI9320_REG(0x37)
#define ILI9320_GAMMA7			ILI9320_REG(0x38)
#define ILI9320_GAMMA8			ILI9320_REG(0x39)
#define ILI9320_GAMMA9			ILI9320_REG(0x3C)
#define ILI9320_GAMMA10			ILI9320_REG(0x3D)

#define ILI9320_HORIZ_START		ILI9320_REG(0x50)
#define ILI9320_HORIZ_END		ILI9320_REG(0x51)
#define ILI9320_VERT_START		ILI9320_REG(0x52)
#define ILI9320_VERT_END		ILI9320_REG(0x53)

#define ILI9320_DRIVER2			ILI9320_REG(0x60)
#define ILI9320_BASE_IMAGE		ILI9320_REG(0x61)
#define ILI9320_VERT_SCROLL		ILI9320_REG(0x6a)

#define ILI9320_PARTIAL1_POSITION	ILI9320_REG(0x80)
#define ILI9320_PARTIAL1_START		ILI9320_REG(0x81)
#define ILI9320_PARTIAL1_END		ILI9320_REG(0x82)
#define ILI9320_PARTIAL2_POSITION	ILI9320_REG(0x83)
#define ILI9320_PARTIAL2_START		ILI9320_REG(0x84)
#define ILI9320_PARTIAL2_END		ILI9320_REG(0x85)

#define ILI9320_INTERFACE1		ILI9320_REG(0x90)
#define ILI9320_INTERFACE2		ILI9320_REG(0x92)
#define ILI9320_INTERFACE3		ILI9320_REG(0x93)
#define ILI9320_INTERFACE4		ILI9320_REG(0x95)
#define ILI9320_INTERFACE5		ILI9320_REG(0x97)
#define ILI9320_INTERFACE6		ILI9320_REG(0x98)

/* Register contents definitions. */

#define ILI9320_OSCILATION_OSC		(1 << 0)

#define ILI9320_DRIVER_SS		(1 << 8)
#define ILI9320_DRIVER_SM		(1 << 10)

#define ILI9320_DRIVEWAVE_EOR		(1 << 8)
#define ILI9320_DRIVEWAVE_BC		(1 << 9)
#define ILI9320_DRIVEWAVE_MUSTSET	(1 << 10)

#define ILI9320_ENTRYMODE_AM		(1 << 3)
#define ILI9320_ENTRYMODE_ID(x)		((x) << 4)
#define ILI9320_ENTRYMODE_ORG		(1 << 7)
#define ILI9320_ENTRYMODE_HWM		(1 << 8)
#define ILI9320_ENTRYMODE_BGR		(1 << 12)
#define ILI9320_ENTRYMODE_DFM		(1 << 14)
#define ILI9320_ENTRYMODE_TRI		(1 << 15)


#define ILI9320_RESIZING_RSZ(x)		((x) << 0)
#define ILI9320_RESIZING_RCH(x)		((x) << 4)
#define ILI9320_RESIZING_RCV(x)		((x) << 8)


#define ILI9320_DISPLAY1_D(x)		((x) << 0)
#define ILI9320_DISPLAY1_CL		(1 << 3)
#define ILI9320_DISPLAY1_DTE		(1 << 4)
#define ILI9320_DISPLAY1_GON		(1 << 5)
#define ILI9320_DISPLAY1_BASEE		(1 << 8)
#define ILI9320_DISPLAY1_PTDE(x)	((x) << 12)


#define ILI9320_DISPLAY2_BP(x)		((x) << 0)
#define ILI9320_DISPLAY2_FP(x)		((x) << 8)


#define ILI9320_RGBIF1_RIM_RGB18	(0 << 0)
#define ILI9320_RGBIF1_RIM_RGB16	(1 << 0)
#define ILI9320_RGBIF1_RIM_RGB6		(2 << 0)

#define ILI9320_RGBIF1_CLK_INT		(0 << 4)
#define ILI9320_RGBIF1_CLK_RGBIF	(1 << 4)
#define ILI9320_RGBIF1_CLK_VSYNC	(2 << 4)

#define ILI9320_RGBIF1_RM		(1 << 8)

#define ILI9320_RGBIF1_ENC_FRAMES(x)	(((x) - 1)<< 13)

#define ILI9320_RGBIF2_DPL		(1 << 0)
#define ILI9320_RGBIF2_EPL		(1 << 1)
#define ILI9320_RGBIF2_HSPL		(1 << 3)
#define ILI9320_RGBIF2_VSPL		(1 << 4)


#define ILI9320_POWER1_SLP		(1 << 1)
#define ILI9320_POWER1_DSTB		(1 << 2)
#define ILI9320_POWER1_AP(x)		((x) << 4)
#define ILI9320_POWER1_APE		(1 << 7)
#define ILI9320_POWER1_BT(x)		((x) << 8)
#define ILI9320_POWER1_SAP		(1 << 12)


#define ILI9320_POWER2_VC(x)		((x) << 0)
#define ILI9320_POWER2_DC0(x)		((x) << 4)
#define ILI9320_POWER2_DC1(x)		((x) << 8)


#define ILI9320_POWER3_VRH(x)		((x) << 0)
#define ILI9320_POWER3_PON		(1 << 4)
#define ILI9320_POWER3_VCMR		(1 << 8)


#define ILI9320_POWER4_VREOUT(x)	((x) << 8)


#define ILI9320_DRIVER2_SCNL(x)		((x) << 0)
#define ILI9320_DRIVER2_NL(x)		((x) << 8)
#define ILI9320_DRIVER2_GS		(1 << 15)


#define ILI9320_BASEIMAGE_REV		(1 << 0)
#define ILI9320_BASEIMAGE_VLE		(1 << 1)
#define ILI9320_BASEIMAGE_NDL		(1 << 2)


#define ILI9320_INTERFACE4_RTNE(x)	(x)
#define ILI9320_INTERFACE4_DIVE(x)	((x) << 8)

/* SPI interface definitions */

#define ILI9320_SPI_IDCODE		(0x70)
#define ILI9320_SPI_ID(x)		((x) << 2)
#define ILI9320_SPI_READ		(0x01)
#define ILI9320_SPI_WRITE		(0x00)
#define ILI9320_SPI_DATA		(0x02)
#define ILI9320_SPI_INDEX		(0x00)

/* platform data to pass configuration from lcd */

enum ili9320_suspend {
	ILI9320_SUSPEND_OFF,
	ILI9320_SUSPEND_DEEP,
};

struct ili9320_platdata {
	unsigned short	hsize;
	unsigned short	vsize;

	enum ili9320_suspend suspend;

	/* set the reset line, 0 = reset asserted, 1 = normal */
	void		(*reset)(unsigned int val);

	unsigned short	entry_mode;
	unsigned short	display2;
	unsigned short	display3;
	unsigned short	display4;
	unsigned short	rgb_if1;
	unsigned short	rgb_if2;
	unsigned short	interface2;
	unsigned short	interface3;
	unsigned short	interface4;
	unsigned short	interface5;
	unsigned short	interface6;
};

