#ifndef __PPC_XMON_H
#define __PPC_XMON_H
#ifdef __KERNEL__

struct pt_regs;

extern void xmon(struct pt_regs *excp);
extern void xmon_printf(const char *fmt, ...);
extern void xmon_map_scc(void);
extern int xmon_bpt(struct pt_regs *regs);
extern int xmon_sstep(struct pt_regs *regs);
extern int xmon_iabr_match(struct pt_regs *regs);
extern int xmon_dabr_match(struct pt_regs *regs);
extern void (*xmon_fault_handler)(struct pt_regs *regs);

#endif
#endif
