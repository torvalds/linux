#ifdef CONFIG_X86_MCE
extern void mcheck_init(struct cpuinfo_x86 *c);
#else
#define mcheck_init(c) do {} while(0)
#endif

extern int mce_disabled;

extern void stop_mce(void);
extern void restart_mce(void);

