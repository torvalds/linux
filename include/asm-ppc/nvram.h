/*
 * PreP compliant NVRAM access
 */

#ifdef __KERNEL__
#ifndef _PPC_NVRAM_H
#define _PPC_NVRAM_H

#define NVRAM_AS0  0x74
#define NVRAM_AS1  0x75
#define NVRAM_DATA 0x77


/* RTC Offsets */

#define MOTO_RTC_SECONDS		0x1FF9
#define MOTO_RTC_MINUTES		0x1FFA
#define MOTO_RTC_HOURS		0x1FFB
#define MOTO_RTC_DAY_OF_WEEK		0x1FFC
#define MOTO_RTC_DAY_OF_MONTH	0x1FFD
#define MOTO_RTC_MONTH		0x1FFE
#define MOTO_RTC_YEAR		0x1FFF
#define MOTO_RTC_CONTROLA            0x1FF8
#define MOTO_RTC_CONTROLB            0x1FF9

/* PowerMac specific nvram stuffs */

enum {
	pmac_nvram_OF,		/* Open Firmware partition */
	pmac_nvram_XPRAM,	/* MacOS XPRAM partition */
	pmac_nvram_NR		/* MacOS Name Registry partition */
};

/* Return partition offset in nvram */
extern int	pmac_get_partition(int partition);

/* Direct access to XPRAM on PowerMacs */
extern u8	pmac_xpram_read(int xpaddr);
extern void	pmac_xpram_write(int xpaddr, u8 data);

/* Synchronize NVRAM */
extern void	nvram_sync(void);

/* Normal access to NVRAM */
extern unsigned char nvram_read_byte(int i);
extern void nvram_write_byte(unsigned char c, int i);

/* Some offsets in XPRAM */
#define PMAC_XPRAM_MACHINE_LOC	0xe4
#define PMAC_XPRAM_SOUND_VOLUME	0x08

/* Machine location structure in PowerMac XPRAM */
struct pmac_machine_location {
	unsigned int	latitude;	/* 2+30 bit Fractional number */
	unsigned int	longitude;	/* 2+30 bit Fractional number */
	unsigned int	delta;		/* mix of GMT delta and DLS */
};

/*
 * /dev/nvram ioctls
 *
 * Note that PMAC_NVRAM_GET_OFFSET is still supported, but is
 * definitely obsolete. Do not use it if you can avoid it
 */

#define OBSOLETE_PMAC_NVRAM_GET_OFFSET \
				_IOWR('p', 0x40, int)

#define IOC_NVRAM_GET_OFFSET	_IOWR('p', 0x42, int)	/* Get NVRAM partition offset */
#define IOC_NVRAM_SYNC		_IO('p', 0x43)		/* Sync NVRAM image */

#endif
#endif /* __KERNEL__ */
