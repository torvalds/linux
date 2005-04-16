/*
 *  arch/i386/mach-generic/mach_reboot.h
 *
 *  Machine specific reboot functions for generic.
 *  Split out from reboot.c by Osamu Tomita <tomita@cinet.co.jp>
 */
#ifndef _MACH_REBOOT_H
#define _MACH_REBOOT_H

static inline void kb_wait(void)
{
	int i;

	for (i = 0; i < 0x10000; i++)
		if ((inb_p(0x64) & 0x02) == 0)
			break;
}

static inline void mach_reboot(void)
{
	int i;
	for (i = 0; i < 100; i++) {
		kb_wait();
		udelay(50);
		outb(0xfe, 0x64);         /* pulse reset low */
		udelay(50);
	}
}

#endif /* !_MACH_REBOOT_H */
