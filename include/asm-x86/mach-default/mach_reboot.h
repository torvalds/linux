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

	/* old method, works on most machines */
	for (i = 0; i < 10; i++) {
		kb_wait();
		udelay(50);
		outb(0xfe, 0x64);	/* pulse reset low */
		udelay(50);
	}

	/* New method: sets the "System flag" which, when set, indicates
	 * successful completion of the keyboard controller self-test (Basic
	 * Assurance Test, BAT).  This is needed for some machines with no
	 * keyboard plugged in.  This read-modify-write sequence sets only the
	 * system flag
	 */
	for (i = 0; i < 10; i++) {
		int cmd;

		outb(0x20, 0x64);	/* read Controller Command Byte */
		udelay(50);
		kb_wait();
		udelay(50);
		cmd = inb(0x60);
		udelay(50);
		kb_wait();
		udelay(50);
		outb(0x60, 0x64);	/* write Controller Command Byte */
		udelay(50);
		kb_wait();
		udelay(50);
		outb(cmd | 0x14, 0x60); /* set "System flag" and "Keyboard Disabled" */
		udelay(50);
		kb_wait();
		udelay(50);
		outb(0xfe, 0x64);	/* pulse reset low */
		udelay(50);
	}
}

#endif /* !_MACH_REBOOT_H */
