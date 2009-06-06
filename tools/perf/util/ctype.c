/*
 * Sane locale-independent, ASCII ctype.
 *
 * No surprises, and works with signed and unsigned chars.
 */
#include "cache.h"

enum {
	S = GIT_SPACE,
	A = GIT_ALPHA,
	D = GIT_DIGIT,
	G = GIT_GLOB_SPECIAL,	/* *, ?, [, \\ */
	R = GIT_REGEX_SPECIAL,	/* $, (, ), +, ., ^, {, | * */
};

unsigned char sane_ctype[256] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, S, S, 0, 0, S, 0, 0,		/*   0.. 15 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		/*  16.. 31 */
	S, 0, 0, 0, R, 0, 0, 0, R, R, G, R, 0, 0, R, 0,		/*  32.. 47 */
	D, D, D, D, D, D, D, D, D, D, 0, 0, 0, 0, 0, G,		/*  48.. 63 */
	0, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,		/*  64.. 79 */
	A, A, A, A, A, A, A, A, A, A, A, G, G, 0, R, 0,		/*  80.. 95 */
	0, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,		/*  96..111 */
	A, A, A, A, A, A, A, A, A, A, A, R, R, 0, 0, 0,		/* 112..127 */
	/* Nothing in the 128.. range */
};
