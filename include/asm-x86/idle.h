#ifndef _ASM_X86_64_IDLE_H
#define _ASM_X86_64_IDLE_H 1

#define IDLE_START 1
#define IDLE_END 2

struct notifier_block;
void idle_notifier_register(struct notifier_block *n);

void enter_idle(void);
void exit_idle(void);

#endif
