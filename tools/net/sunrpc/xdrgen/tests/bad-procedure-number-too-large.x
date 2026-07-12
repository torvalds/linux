/*
 * NEGATIVE TEST CASE -- xdrgen must REJECT this specification.
 *
 * RFC 5531 encodes program, version, and procedure numbers as unsigned
 * 32-bit integers (Section 9). This spec gives a procedure a number one
 * past the 32-bit maximum, which the front end must reject.
 *
 * Expected diagnostic:
 *   procedure number 4294967296 in version 'BADVERS' exceeds 4294967295
 *
 * The tests directory has no automated runner; exercise by hand:
 *   ./xdrgen definitions tests/bad-procedure-number-too-large.x   (must fail)
 */

program BADPROG {
	version BADVERS {
		void BADPROC_NULL(void) = 0;
		void BADPROC_FOO(void)  = 4294967296;
	} = 1;
} = 100000;
