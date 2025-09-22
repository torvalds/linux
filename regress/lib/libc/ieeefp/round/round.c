/*	$OpenBSD: round.c,v 1.3 2003/07/31 21:48:03 deraadt Exp $	*/
/*	$NetBSD: round.c,v 1.1 1995/04/26 00:27:28 jtc Exp $	*/

/*
 * Written by J.T. Conklin, Apr 18, 1995
 * Public domain.
 */

#include <assert.h>
#include <stdlib.h>
#include <ieeefp.h>
#include <float.h>

int
main(int argc, char *argv[])
{
	/*
	 * This test would be better if it actually performed some
	 * calculations to verify the selected rounding mode.  But
	 * this is probably acceptable since the fp{get,set}round
	 * functions usually just get or set the processors fpu
	 * control word.
	 */

	assert(fpgetround() == FP_RN);
	assert(FLT_ROUNDS == 1);

	assert(fpsetround(FP_RP) == FP_RN);
	assert(fpgetround() == FP_RP);
	assert(FLT_ROUNDS == 2);

	assert(fpsetround(FP_RM) == FP_RP);
	assert(fpgetround() == FP_RM);
	assert(FLT_ROUNDS == 3);

	assert(fpsetround(FP_RZ) == FP_RM);
	assert(fpgetround() == FP_RZ);
	assert(FLT_ROUNDS == 0);

	assert(fpsetround(FP_RN) == FP_RZ);
	assert(fpgetround() == FP_RN);
	assert(FLT_ROUNDS == 1);

	exit(0);
}
