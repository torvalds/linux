/*
 * sound/iwmem.h
 *
 * DRAM size encoding table for AMD Interwave chip.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 * Changes:
 * Bartlomiej Zolnierkiewicz	: added __initdata to mem_decode
 */


#define K 1024
#define M (1024*K)
static int mem_decode[][4] __initdata =
{
/*	Bank0	Bank1	Bank2	Bank3	Encoding bits	*/
	{256*K,	0,	0,	0},		/*  0 */
	{256*K,	256*K,	0,	0},		/*  1 */
	{256*K,	256*K,	256*K,	256*K},		/*  2 */
	{256*K,	1*M,	0,	0},		/*  3 */
	{256*K,	1*M,	1*M,	1*M},		/*  4 */
	{256*K,	256*K,	1*M,	0},		/*  5 */
	{256*K,	256*K,	1*M,	1*M},		/*  6 */
	{1*M,	0,	0,	0},		/*  7 */
	{1*M,	1*M,	0,	0},		/*  8 */
	{1*M,	1*M,	1*M,	1*M},		/*  9 */
	{4*M,	0,	0,	0},		/* 10 */
	{4*M,	4*M,	0,	0},		/* 11 */
	{4*M,	4*M,	4*M,	4*M}		/* 12 */
};
