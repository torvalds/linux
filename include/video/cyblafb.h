
#ifndef CYBLAFB_DEBUG
#define CYBLAFB_DEBUG 0
#endif

#if CYBLAFB_DEBUG
#define debug(f,a...)	printk("%s:" f,  __func__ , ## a);
#else
#define debug(f,a...)
#endif

#define output(f, a...) printk("cyblafb: " f, ## a)

#define Kb	(1024)
#define Mb	(Kb*Kb)

/* PCI IDS of supported cards temporarily here */

#define CYBERBLADEi1	0x8500

/* these defines are for 'lcd' variable */
#define LCD_STRETCH	0
#define LCD_CENTER	1
#define LCD_BIOS	2

/* display types */
#define DISPLAY_CRT	0
#define DISPLAY_FP	1

#define ROP_S	0xCC

#define point(x,y) ((y)<<16|(x))

//
// Attribute Regs, ARxx, 3c0/3c1
//
#define AR00	0x00
#define AR01	0x01
#define AR02	0x02
#define AR03	0x03
#define AR04	0x04
#define AR05	0x05
#define AR06	0x06
#define AR07	0x07
#define AR08	0x08
#define AR09	0x09
#define AR0A	0x0A
#define AR0B	0x0B
#define AR0C	0x0C
#define AR0D	0x0D
#define AR0E	0x0E
#define AR0F	0x0F
#define AR10	0x10
#define AR12	0x12
#define AR13	0x13

//
// Sequencer Regs, SRxx, 3c4/3c5
//
#define SR00	0x00
#define SR01	0x01
#define SR02	0x02
#define SR03	0x03
#define SR04	0x04
#define SR0D	0x0D
#define SR0E	0x0E
#define SR11	0x11
#define SR18	0x18
#define SR19	0x19

//
//
//
#define CR00	0x00
#define CR01	0x01
#define CR02	0x02
#define CR03	0x03
#define CR04	0x04
#define CR05	0x05
#define CR06	0x06
#define CR07	0x07
#define CR08	0x08
#define CR09	0x09
#define CR0A	0x0A
#define CR0B	0x0B
#define CR0C	0x0C
#define CR0D	0x0D
#define CR0E	0x0E
#define CR0F	0x0F
#define CR10	0x10
#define CR11	0x11
#define CR12	0x12
#define CR13	0x13
#define CR14	0x14
#define CR15	0x15
#define CR16	0x16
#define CR17	0x17
#define CR18	0x18
#define CR19	0x19
#define CR1A	0x1A
#define CR1B	0x1B
#define CR1C	0x1C
#define CR1D	0x1D
#define CR1E	0x1E
#define CR1F	0x1F
#define CR20	0x20
#define CR21	0x21
#define CR27	0x27
#define CR29	0x29
#define CR2A	0x2A
#define CR2B	0x2B
#define CR2D	0x2D
#define CR2F	0x2F
#define CR36	0x36
#define CR38	0x38
#define CR39	0x39
#define CR3A	0x3A
#define CR55	0x55
#define CR56	0x56
#define CR57	0x57
#define CR58	0x58

//
//
//

#define GR00	0x01
#define GR01	0x01
#define GR02	0x02
#define GR03	0x03
#define GR04	0x04
#define GR05	0x05
#define GR06	0x06
#define GR07	0x07
#define GR08	0x08
#define GR0F	0x0F
#define GR20	0x20
#define GR23	0x23
#define GR2F	0x2F
#define GR30	0x30
#define GR31	0x31
#define GR33	0x33
#define GR52	0x52
#define GR53	0x53
#define GR5D	0x5d


//
// Graphics Engine
//
#define GEBase	0x2100		// could be mapped elsewhere if we like it
#define GE00	(GEBase+0x00)	// source 1, p 111
#define GE04	(GEBase+0x04)	// source 2, p 111
#define GE08	(GEBase+0x08)	// destination 1, p 111
#define GE0C	(GEBase+0x0C)	// destination 2, p 112
#define GE10	(GEBase+0x10)	// right view base & enable, p 112
#define GE13	(GEBase+0x13)	// left view base & enable, p 112
#define GE18	(GEBase+0x18)	// block write start address, p 112
#define GE1C	(GEBase+0x1C)	// block write end address, p 112
#define GE20	(GEBase+0x20)	// engine status, p 113
#define GE24	(GEBase+0x24)	// reset all GE pointers
#define GE44	(GEBase+0x44)	// command register, p 126
#define GE48	(GEBase+0x48)	// raster operation, p 127
#define GE60	(GEBase+0x60)	// foreground color, p 128
#define GE64	(GEBase+0x64)	// background color, p 128
#define GE6C	(GEBase+0x6C)	// Pattern and Style, p 129, ok
#define GE9C	(GEBase+0x9C)	// pixel engine data port, p 125
#define GEB8	(GEBase+0xB8)	// Destination Stride / Buffer Base 0, p 133
#define GEBC	(GEBase+0xBC)	// Destination Stride / Buffer Base 1, p 133
#define GEC0	(GEBase+0xC0)	// Destination Stride / Buffer Base 2, p 133
#define GEC4	(GEBase+0xC4)	// Destination Stride / Buffer Base 3, p 133
#define GEC8	(GEBase+0xC8)	// Source Stride / Buffer Base 0, p 133
#define GECC	(GEBase+0xCC)	// Source Stride / Buffer Base 1, p 133
#define GED0	(GEBase+0xD0)	// Source Stride / Buffer Base 2, p 133
#define GED4	(GEBase+0xD4)	// Source Stride / Buffer Base 3, p 133
