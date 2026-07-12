/*
 * NEGATIVE TEST CASE -- xdrgen must REJECT this specification.
 *
 * RFC 5531 assigns only unsigned constants to program, version, and
 * procedure numbers (Section 12.3). This spec gives a procedure a
 * negative number, which the front end must reject.
 *
 * Expected diagnostic:
 *   negative procedure number -5 in version 'BADVERS'
 *
 * The tests directory has no automated runner; exercise by hand:
 *   ./xdrgen definitions tests/bad-procedure-number-negative.x   (must fail)
 */

program BADPROG {
	version BADVERS {
		void BADPROC_NULL(void) = 0;
		void BADPROC_FOO(void)  = -5;
	} = 1;
} = 100000;
