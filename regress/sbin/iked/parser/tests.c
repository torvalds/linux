/* 	$OpenBSD: tests.c,v 1.1 2017/05/29 20:59:28 markus Exp $ */
/*
 * Regress test for iked payload parser
 *
 * Placed in the public domain
 */

#include "test_helper.h"

void parser_fuzz_tests(void);

void
tests(void)
{
	parser_fuzz_tests();
}
