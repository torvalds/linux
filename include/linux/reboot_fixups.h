#ifndef _LINUX_REBOOT_FIXUPS_H
#define _LINUX_REBOOT_FIXUPS_H

#ifdef CONFIG_X86_REBOOTFIXUPS
extern void mach_reboot_fixups(void);
#else
#define mach_reboot_fixups() ((void)(0))
#endif

#endif /* _LINUX_REBOOT_FIXUPS_H */
