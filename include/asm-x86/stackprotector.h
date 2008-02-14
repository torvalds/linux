#ifndef _ASM_STACKPROTECTOR_H
#define _ASM_STACKPROTECTOR_H 1

#include <asm/tsc.h>

/*
 * Initialize the stackprotector canary value.
 *
 * NOTE: this must only be called from functions that never return,
 * and it must always be inlined.
 */
static __always_inline void boot_init_stack_canary(void)
{
	u64 canary;
	u64 tsc;

	/*
	 * If we're the non-boot CPU, nothing set the PDA stack
	 * canary up for us - and if we are the boot CPU we have
	 * a 0 stack canary. This is a good place for updating
	 * it, as we wont ever return from this function (so the
	 * invalid canaries already on the stack wont ever
	 * trigger).
	 *
	 * We both use the random pool and the current TSC as a source
	 * of randomness. The TSC only matters for very early init,
	 * there it already has some randomness on most systems. Later
	 * on during the bootup the random pool has true entropy too.
	 */
	get_random_bytes(&canary, sizeof(canary));
	tsc = __native_read_tsc();
	canary += tsc + (tsc << 32UL);

	current->stack_canary = canary;
	write_pda(stack_canary, canary);
}

#endif
