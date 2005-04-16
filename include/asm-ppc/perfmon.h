#ifndef __PERFMON_H
#define __PERFMON_H

extern void (*perf_irq)(struct pt_regs *);

int request_perfmon_irq(void (*handler)(struct pt_regs *));
void free_perfmon_irq(void);

#ifdef CONFIG_FSL_BOOKE
void init_pmc_stop(int ctr);
void set_pmc_event(int ctr, int event);
void set_pmc_user_kernel(int ctr, int user, int kernel);
void set_pmc_marked(int ctr, int mark0, int mark1);
void pmc_start_ctr(int ctr, int enable);
void pmc_start_ctrs(int enable);
void pmc_stop_ctrs(void);
void dump_pmcs(void);

extern struct op_ppc32_model op_model_fsl_booke;
#endif

#endif /* __PERFMON_H */
