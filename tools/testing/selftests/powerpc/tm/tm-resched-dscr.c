/* Test context switching to see if the DSCR SPR is correctly preserved
 * when within a transaction.
 *
 * Note: We assume that the DSCR has been left at the default value (0)
 * for all CPUs.
 *
 * Method:
 *
 * Set a value into the DSCR.
 *
 * Start a transaction, and suspend it (*).
 *
 * Hard loop checking to see if the transaction has become doomed.
 *
 * Now that we *may* have been preempted, record the DSCR and TEXASR SPRS.
 *
 * If the abort was because of a context switch, check the DSCR value.
 * Otherwise, try again.
 *
 * (*) If the transaction is not suspended we can't see the problem because
 * the transaction abort handler will restore the DSCR to it's checkpointed
 * value before we regain control.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <asm/tm.h>

#define TBEGIN          ".long 0x7C00051D ;"
#define TEND            ".long 0x7C00055D ;"
#define TCHECK          ".long 0x7C00059C ;"
#define TSUSPEND        ".long 0x7C0005DD ;"
#define TRESUME         ".long 0x7C2005DD ;"
#define SPRN_TEXASR     0x82
#define SPRN_DSCR       0x03

int main(void) {
	uint64_t rv, dscr1 = 1, dscr2, texasr;

	printf("Check DSCR TM context switch: ");
	fflush(stdout);
	for (;;) {
		rv = 1;
		asm __volatile__ (
			/* set a known value into the DSCR */
			"ld      3, %[dscr1];"
			"mtspr   %[sprn_dscr], 3;"

			/* start and suspend a transaction */
			TBEGIN
			"beq     1f;"
			TSUSPEND

			/* hard loop until the transaction becomes doomed */
			"2: ;"
			TCHECK
			"bc      4, 0, 2b;"

			/* record DSCR and TEXASR */
			"mfspr   3, %[sprn_dscr];"
			"std     3, %[dscr2];"
			"mfspr   3, %[sprn_texasr];"
			"std     3, %[texasr];"

			TRESUME
			TEND
			"li      %[rv], 0;"
			"1: ;"
			: [rv]"=r"(rv), [dscr2]"=m"(dscr2), [texasr]"=m"(texasr)
			: [dscr1]"m"(dscr1)
			, [sprn_dscr]"i"(SPRN_DSCR), [sprn_texasr]"i"(SPRN_TEXASR)
			: "memory", "r3"
		);
		assert(rv); /* make sure the transaction aborted */
		if ((texasr >> 56) != TM_CAUSE_RESCHED) {
			putchar('.');
			fflush(stdout);
			continue;
		}
		if (dscr2 != dscr1) {
			printf(" FAIL\n");
			exit(EXIT_FAILURE);
		} else {
			printf(" OK\n");
			exit(EXIT_SUCCESS);
		}
	}
}
