#ifndef ASM_X86__REBOOT_H
#define ASM_X86__REBOOT_H

struct pt_regs;

struct machine_ops {
	void (*restart)(char *cmd);
	void (*halt)(void);
	void (*power_off)(void);
	void (*shutdown)(void);
	void (*crash_shutdown)(struct pt_regs *);
	void (*emergency_restart)(void);
};

extern struct machine_ops machine_ops;

void native_machine_crash_shutdown(struct pt_regs *regs);
void native_machine_shutdown(void);
void machine_real_restart(const unsigned char *code, int length);

#endif /* ASM_X86__REBOOT_H */
