/*
 * NEGATIVE TEST CASE -- xdrgen must REJECT this specification.
 *
 * RFC 5531 encodes program, version, and procedure numbers as unsigned
 * 32-bit integers (Section 9). This spec gives the program a number one
 * past the 32-bit maximum, which the front end must reject.
 *
 * Expected diagnostic:
 *   program number 4294967296 in program 'BADPROG' exceeds 4294967295
 *
 * The tests directory has no automated runner; exercise by hand:
 *   ./xdrgen definitions tests/bad-program-number-too-large.x   (must fail)
 */

program BADPROG {
	version BADVERS {
		void BADPROC_NULL(void) = 0;
	} = 1;
} = 4294967296;
