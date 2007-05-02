#ifndef _ASM_REBOOT_H
#define _ASM_REBOOT_H

struct pt_regs;

struct machine_ops
{
	void (*restart)(char *cmd);
	void (*halt)(void);
	void (*power_off)(void);
	void (*shutdown)(void);
	void (*crash_shutdown)(struct pt_regs *);
	void (*emergency_restart)(void);
};

extern struct machine_ops machine_ops;

void machine_real_restart(unsigned char *code, int length);

#endif	/* _ASM_REBOOT_H */
