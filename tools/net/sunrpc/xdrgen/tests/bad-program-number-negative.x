/*
 * NEGATIVE TEST CASE -- xdrgen must REJECT this specification.
 *
 * RFC 5531 assigns only unsigned constants to program, version, and
 * procedure numbers (Section 12.3). This spec gives the program a
 * negative number, which the front end must reject.
 *
 * Expected diagnostic:
 *   negative program number -100000 in program 'BADPROG'
 *
 * The tests directory has no automated runner; exercise by hand:
 *   ./xdrgen definitions tests/bad-program-number-negative.x   (must fail)
 */

program BADPROG {
	version BADVERS {
		void BADPROC_NULL(void) = 0;
	} = 1;
} = -100000;
