struct pt_regs;
struct sigaction;
asmlinkage long xtensa_execve(char*, char**, char**, struct pt_regs*);
asmlinkage long xtensa_clone(unsigned long, unsigned long, struct pt_regs*);
asmlinkage long xtensa_pipe(int __user *);
asmlinkage long xtensa_mmap2(unsigned long, unsigned long, unsigned long,
    			     unsigned long, unsigned long, unsigned long);
asmlinkage long xtensa_ptrace(long, long, long, long);
asmlinkage long xtensa_sigreturn(struct pt_regs*);
asmlinkage long xtensa_rt_sigreturn(struct pt_regs*);
asmlinkage long xtensa_sigsuspend(struct pt_regs*);
asmlinkage long xtensa_rt_sigsuspend(struct pt_regs*);
asmlinkage long xtensa_sigaction(int, const struct old_sigaction*,
				 struct old_sigaction*);
asmlinkage long xtensa_sigaltstack(struct pt_regs *regs);
asmlinkage long sys_rt_sigaction(int,
				 const struct sigaction __user *,
				 struct sigaction __user *,
				 size_t);
asmlinkage long xtensa_shmat(int shmid, char __user *shmaddr, int shmflg);
