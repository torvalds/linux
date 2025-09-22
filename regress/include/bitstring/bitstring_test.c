/* $OpenBSD: bitstring_test.c,v 1.7 2024/08/26 12:15:40 bluhm Exp $	 */
/* $NetBSD: bitstring_test.c,v 1.4 1995/04/29 05:44:35 cgd Exp $	 */

/*
 * this is a simple program to test bitstring.h
 * inspect the output, you should notice problems
 * choose the ATT or BSD flavor
 */
// #define ATT /*-*/
#define BSD			/*-*/

/*
 * include the following define if you want the program to link. this
 * corrects a misspeling in bitstring.h
 */
#define _bitstr_size bitstr_size

#include <stdio.h>
#include <stdlib.h>

/* #ifdef NOTSOGOOD */
#include "bitstring.h"
/* #else */
/* #include "gbitstring.h" */
/* #endif */

int             TEST_LENGTH;
#define DECL_TEST_LENGTH	37	/* a mostly random number */

static void
clearbits(bitstr_t *b, int n)
{
	register int    i = bitstr_size(n);

	while (i--)
		*(b + i) = 0;
}

static void
printbits(bitstr_t *b, int n)
{
	register int    i, k;
	int             jc, js;

	bit_ffc(b, n, &jc);
	bit_ffs(b, n, &js);
	(void) printf("%3d %3d ", jc, js);
	for (i = 0; i < n; i++) {
		(void) putchar((bit_test(b, i) ? '1' : '0'));
	}
	(void) putchar('\n');
}

int
main(int argc, char *argv[])
{
	int             i, j, k, *p;
	bitstr_t       *bs;
	bitstr_t        bit_decl(bss, DECL_TEST_LENGTH);

	if (argc > 1)
		TEST_LENGTH = atoi(argv[1]);
	else
		TEST_LENGTH = DECL_TEST_LENGTH;

	if (TEST_LENGTH < 4) {
		fprintf(stderr,
			"TEST_LENGTH must be at least 4, but it is %d\n",
			TEST_LENGTH);
		exit(1);
	}
	(void) printf("Testing with TEST_LENGTH = %d\n\n", TEST_LENGTH);

	(void) printf("test _bit_byte, _bit_mask, and bitstr_size\n");
	(void) printf("  i   _bit_byte(i)   _bit_mask(i) bitstr_size(i)\n");
	for (i = 0; i < TEST_LENGTH; i++) {
		(void) printf("%3d%15d%15d%15d\n",
			      i, _bit_byte(i), _bit_mask(i), bitstr_size(i));
	}

	bs = bit_alloc(TEST_LENGTH);
	clearbits(bs, TEST_LENGTH);
	(void) printf("\ntest bit_alloc, clearbits, bit_ffc, bit_ffs\n");
	(void) printf("be:   0  -1 ");
	for (i = 0; i < TEST_LENGTH; i++)
		(void) putchar('0');
	(void) printf("\nis: ");
	printbits(bs, TEST_LENGTH);

	(void) printf("\ntest bit_set\n");
	for (i = 0; i < TEST_LENGTH; i += 3) {
		bit_set(bs, i);
	}
	(void) printf("be:   1   0 ");
	for (i = 0; i < TEST_LENGTH; i++)
		(void) putchar("100"[i % 3]);
	(void) printf("\nis: ");
	printbits(bs, TEST_LENGTH);

	(void) printf("\ntest bit_clear\n");
	for (i = 0; i < TEST_LENGTH; i += 6) {
		bit_clear(bs, i);
	}
	(void) printf("be:   0   3 ");
	for (i = 0; i < TEST_LENGTH; i++)
		(void) putchar("000100"[i % 6]);
	(void) printf("\nis: ");
	printbits(bs, TEST_LENGTH);

	(void) printf("\ntest bit_test using previous bitstring\n");
	(void) printf("  i    bit_test(i)\n");
	for (i = 0; i < TEST_LENGTH; i++) {
		(void) printf("%3d%15d\n",
			      i, bit_test(bs, i));
	}

	clearbits(bs, TEST_LENGTH);
	(void) printf("\ntest clearbits\n");
	(void) printf("be:   0  -1 ");
	for (i = 0; i < TEST_LENGTH; i++)
		(void) putchar('0');
	(void) printf("\nis: ");
	printbits(bs, TEST_LENGTH);

	(void) printf("\ntest bit_nset and bit_nclear\n");
	bit_nset(bs, 1, TEST_LENGTH - 2);
	(void) printf("be:   0   1 0");
	for (i = 0; i < TEST_LENGTH - 2; i++)
		(void) putchar('1');
	(void) printf("0\nis: ");
	printbits(bs, TEST_LENGTH);

	bit_nclear(bs, 2, TEST_LENGTH - 3);
	(void) printf("be:   0   1 01");
	for (i = 0; i < TEST_LENGTH - 4; i++)
		(void) putchar('0');
	(void) printf("10\nis: ");
	printbits(bs, TEST_LENGTH);

	bit_nclear(bs, 0, TEST_LENGTH - 1);
	(void) printf("be:   0  -1 ");
	for (i = 0; i < TEST_LENGTH; i++)
		(void) putchar('0');
	(void) printf("\nis: ");
	printbits(bs, TEST_LENGTH);
	bit_nset(bs, 0, TEST_LENGTH - 2);
	(void) printf("be: %3d   0 ", TEST_LENGTH - 1);
	for (i = 0; i < TEST_LENGTH - 1; i++)
		(void) putchar('1');
	putchar('0');
	(void) printf("\nis: ");
	printbits(bs, TEST_LENGTH);
	bit_nclear(bs, 0, TEST_LENGTH - 1);
	(void) printf("be:   0  -1 ");
	for (i = 0; i < TEST_LENGTH; i++)
		(void) putchar('0');
	(void) printf("\nis: ");
	printbits(bs, TEST_LENGTH);

	(void) printf("\n");
	(void) printf("first 1 bit should move right 1 position each line\n");
	for (i = 0; i < TEST_LENGTH; i++) {
		bit_nclear(bs, 0, TEST_LENGTH - 1);
		bit_nset(bs, i, TEST_LENGTH - 1);
		(void) printf("%3d ", i);
		printbits(bs, TEST_LENGTH);
	}

	(void) printf("\n");
	(void) printf("first 0 bit should move right 1 position each line\n");
	for (i = 0; i < TEST_LENGTH; i++) {
		bit_nset(bs, 0, TEST_LENGTH - 1);
		bit_nclear(bs, i, TEST_LENGTH - 1);
		(void) printf("%3d ", i);
		printbits(bs, TEST_LENGTH);
	}

	(void) printf("\n");
	(void) printf("first 0 bit should move left 1 position each line\n");
	for (i = 0; i < TEST_LENGTH; i++) {
		bit_nclear(bs, 0, TEST_LENGTH - 1);
		bit_nset(bs, 0, TEST_LENGTH - 1 - i);
		(void) printf("%3d ", i);
		printbits(bs, TEST_LENGTH);
	}

	(void) printf("\n");
	(void) printf("first 1 bit should move left 1 position each line\n");
	for (i = 0; i < TEST_LENGTH; i++) {
		bit_nset(bs, 0, TEST_LENGTH - 1);
		bit_nclear(bs, 0, TEST_LENGTH - 1 - i);
		(void) printf("%3d ", i);
		printbits(bs, TEST_LENGTH);
	}

	(void) printf("\n");
	(void) printf("0 bit should move right 1 position each line\n");
	for (i = 0; i < TEST_LENGTH; i++) {
		bit_nset(bs, 0, TEST_LENGTH - 1);
		bit_nclear(bs, i, i);
		(void) printf("%3d ", i);
		printbits(bs, TEST_LENGTH);
	}

	(void) printf("\n");
	(void) printf("1 bit should move right 1 position each line\n");
	for (i = 0; i < TEST_LENGTH; i++) {
		bit_nclear(bs, 0, TEST_LENGTH - 1);
		bit_nset(bs, i, i);
		(void) printf("%3d ", i);
		printbits(bs, TEST_LENGTH);
	}

	(void) printf("\n");
	(void) printf("CHI square test\n");
	for (i = 0; i < TEST_LENGTH; i++) {
		bit_nclear(bs, 0, TEST_LENGTH - 1);
		bit_nset(bs, i, i);
		bit_nset(bs, TEST_LENGTH - 1 - i, TEST_LENGTH - 1 - i);
		(void) printf("%3d ", i);
		printbits(bs, TEST_LENGTH);
	}

	(void) printf("\n");
	(void) printf("macros should evaluate arguments only once\n");
	i = j = 0;
	_bit_byte(i++);
	(void) printf("_bit_byte(%d) -> %d\n", j++, i);
	_bit_mask(i++);
	(void) printf("_bit_mask(%d) -> %d\n", j++, i);
	bitstr_size(i++);
	(void) printf("bitstr_size(%d) -> %d\n", j++, i);
	free(bit_alloc(i++));
	(void) printf("bit_alloc(%d) -> %d\n", j++, i);
	{ bitstr_t bit_decl(bd, i++); }
	(void) printf("bit_alloc(%d) -> %d\n", j++, i);
	bit_test(bs, i++);
	(void) printf("bit_test(%d) -> %d\n", j++, i);
	bit_set(bs, i++);
	(void) printf("bit_set(%d) -> %d\n", j++, i);
	bit_clear(bs, i++);
	(void) printf("bit_clear(%d) -> %d\n", j++, i);
	i %= TEST_LENGTH; j %= TEST_LENGTH;
	bit_nclear(bs, i++, i++);
	(void) printf("bit_nclear(%d, %d) -> %d\n", j, j + 1, i);
	j += 2;
	bit_nset(bs, i++, i++);
	(void) printf("bit_nset(%d, %d) -> %d\n", j, j + 1, i);
	j += 2;
	p = &k;
	bit_ffc(bs, i++, p++);
	(void) printf("bit_ffc(%d, %ld) -> %d\n", j++, --p - &k, i);
	bit_ffs(bs, i++, p++);
	(void) printf("bit_ffs(%d, %ld) -> %d\n", j++, --p - &k, i);

	(void) free(bs);
	return (0);
}
