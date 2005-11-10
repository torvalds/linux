#ifndef __PPC_XMON_H
#define __PPC_XMON_H
#ifdef __KERNEL__

struct pt_regs;

extern int xmon(struct pt_regs *excp);
extern void xmon_printf(const char *fmt, ...);
extern void xmon_init(int);

#endif
#endif
