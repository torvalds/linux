/*
 * Copyright (c) 2005 Richard Purdie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/suspend.h>

struct pxa_cpu_pm_fns {
	int	save_size;
	void	(*save)(unsigned long *);
	void	(*restore)(unsigned long *);
	int	(*valid)(suspend_state_t state);
	void	(*enter)(suspend_state_t state);
};

extern struct pxa_cpu_pm_fns *pxa_cpu_pm_fns;

/* sleep.S */
extern void pxa25x_cpu_suspend(unsigned int);
extern void pxa27x_cpu_suspend(unsigned int);
extern void pxa_cpu_resume(void);

extern int pxa_pm_enter(suspend_state_t state);
