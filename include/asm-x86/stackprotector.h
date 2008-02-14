#ifndef _ASM_STACKPROTECTOR_H
#define _ASM_STACKPROTECTOR_H 1

/*
 * Initialize the stackprotector canary value.
 *
 * NOTE: this must only be called from functions that never return,
 * and it must always be inlined.
 */
static __always_inline void boot_init_stack_canary(void)
{
	/*
	 * If we're the non-boot CPU, nothing set the PDA stack
	 * canary up for us - and if we are the boot CPU we have
	 * a 0 stack canary. This is a good place for updating
	 * it, as we wont ever return from this function (so the
	 * invalid canaries already on the stack wont ever
	 * trigger):
	 */
	current->stack_canary = get_random_int();
	write_pda(stack_canary, current->stack_canary);
}

#endif
