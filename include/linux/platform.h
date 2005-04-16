/*
 * include/linux/platform.h - platform driver definitions
 *
 * Because of the prolific consumerism of the average American,
 * and the dominant marketing budgets of PC OEMs, we have been
 * blessed with frequent updates of the PC architecture. 
 *
 * While most of these calls are singular per architecture, they 
 * require an extra layer of abstraction on the x86 so the right
 * subsystem gets the right call. 
 *
 * Basically, this consolidates the power off and reboot callbacks 
 * into one structure, as well as adding power management hooks.
 *
 * When adding a platform driver, please make sure all callbacks are 
 * filled. There are defaults defined below that do nothing; use those
 * if you do not support that callback.
 */ 

#ifndef _PLATFORM_H_
#define _PLATFORM_H_
#ifdef __KERNEL__

#include <linux/types.h>

struct platform_t {
	char	* name;
	u32	suspend_states;
	void	(*reboot)(char * cmd);
	void	(*halt)(void);
	void	(*power_off)(void);
	int	(*suspend)(int state, int flags);
	void	(*idle)(void);
};

extern struct platform_t * platform;
extern void default_reboot(char * cmd);
extern void default_halt(void);
extern int default_suspend(int state, int flags);
extern void default_idle(void);

#endif /* __KERNEL__ */
#endif /* _PLATFORM_H */
