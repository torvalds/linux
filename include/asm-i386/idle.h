#ifndef _ASM_I386_IDLE_H
#define _ASM_I386_IDLE_H 1

#define IDLE_START 1
#define IDLE_END 2

struct notifier_block;
void idle_notifier_register(struct notifier_block *n);
void idle_notifier_unregister(struct notifier_block *n);

void exit_idle(void);
void enter_idle(void);

#endif
