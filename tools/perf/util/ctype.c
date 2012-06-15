/*
 * Sane locale-independent, ASCII ctype.
 *
 * No surprises, and works with signed and unsigned chars.
 */
#include "util.h"

enum {
	S = GIT_SPACE,
	A = GIT_ALPHA,
	D = GIT_DIGIT,
	G = GIT_GLOB_SPECIAL,	/* *, ?, [, \\ */
	R = GIT_REGEX_SPECIAL,	/* $, (, ), +, ., ^, {, | * */
	P = GIT_PRINT_EXTRA,	/* printable - alpha - digit - glob - regex */

	PS = GIT_SPACE | GIT_PRINT_EXTRA,
};

unsigned char sane_ctype[256] = {
/*	0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F			    */

	0, 0, 0, 0, 0, 0, 0, 0, 0, S, S, 0, 0, S, 0, 0,		/*   0.. 15 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		/*  16.. 31 */
	PS,P, P, P, R, P, P, P, R, R, G, R, P, P, R, P,		/*  32.. 47 */
	D, D, D, D, D, D, D, D, D, D, P, P, P, P, P, G,		/*  48.. 63 */
	P, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,		/*  64.. 79 */
	A, A, A, A, A, A, A, A, A, A, A, G, G, P, R, P,		/*  80.. 95 */
	P, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,		/*  96..111 */
	A, A, A, A, A, A, A, A, A, A, A, R, R, P, P, 0,		/* 112..127 */
	/* Nothing in the 128.. range */
};

const char *graph_line =
	"_____________________________________________________________________"
	"_____________________________________________________________________";
const char *graph_dotted_line =
	"---------------------------------------------------------------------"
	"---------------------------------------------------------------------"
	"---------------------------------------------------------------------";
