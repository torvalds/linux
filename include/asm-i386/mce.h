#ifdef CONFIG_X86_MCE
extern void mcheck_init(struct cpuinfo_x86 *c);
#else
#define mcheck_init(c) do {} while(0)
#endif
