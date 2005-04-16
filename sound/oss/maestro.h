/*
 *	Registers for the ESS PCI cards
 */
 
/*
 *	Memory access
 */
 
#define ESS_MEM_DATA		0x00
#define	ESS_MEM_INDEX		0x02

/*
 *	AC-97 Codec port. Delay 1uS after each write. This is used to
 *	talk AC-97 (see intel.com). Write data then register.
 */
 
#define ESS_AC97_INDEX		0x30		/* byte wide */
#define ESS_AC97_DATA		0x32

/* 
 *	Reading is a bit different. You write register|0x80 to ubdex
 *	delay 1uS poll the low bit of index, when it clears read the
 *	data value.
 */

/*
 *	Control port. Not yet fully understood
 *	The value 0xC090 gets loaded to it then 0x0000 and 0x2800
 *	to the data port. Then after 4uS the value 0x300 is written
 */
 
#define RING_BUS_CTRL_L		0x34
#define RING_BUS_CTRL_H		0x36

/*
 *	This is also used during setup. The value 0x17 is written to it
 */
 
#define ESS_SETUP_18		0x18

/*
 *	And this one gets 0x000b
 */
 
#define ESS_SETUP_A2		0xA2

/*
 *	And this 0x0000
 */
 
#define ESS_SETUP_A4		0xA4
#define ESS_SETUP_A6		0xA6

/*
 *	Stuff to do with Harpo - the wave stuff
 */
 
#define ESS_WAVETABLE_SIZE	0x14
#define 	ESS_WAVETABLE_2M	0xA180

