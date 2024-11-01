/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */

#ifndef DT_BINDINGS_DDR_H
#define DT_BINDINGS_DDR_H

/*
 * DDR3 SDRAM Standard Speed Bins include tCK, tRCD, tRP, tRAS and tRC for
 * each corresponding bin.
 */

/* DDR3-800 (5-5-5) */
#define DDR3_800D	0
/* DDR3-800 (6-6-6) */
#define DDR3_800E	1
/* DDR3-1066 (6-6-6) */
#define DDR3_1066E	2
/* DDR3-1066 (7-7-7) */
#define DDR3_1066F	3
/* DDR3-1066 (8-8-8) */
#define DDR3_1066G	4
/* DDR3-1333 (7-7-7) */
#define DDR3_1333F	5
/* DDR3-1333 (8-8-8) */
#define DDR3_1333G	6
/* DDR3-1333 (9-9-9) */
#define DDR3_1333H	7
/* DDR3-1333 (10-10-10) */
#define DDR3_1333J 	8
/* DDR3-1600 (8-8-8) */
#define DDR3_1600G	9
/* DDR3-1600 (9-9-9) */
#define DDR3_1600H	10
/* DDR3-1600 (10-10-10) */
#define DDR3_1600J	11
/* DDR3-1600 (11-11-11) */
#define DDR3_1600K	12
/* DDR3-1600 (10-10-10) */
#define DDR3_1866J	13
/* DDR3-1866 (11-11-11) */
#define DDR3_1866K	14
/* DDR3-1866 (12-12-12) */
#define DDR3_1866L	15
/* DDR3-1866 (13-13-13) */
#define DDR3_1866M	16
/* DDR3-2133 (11-11-11) */
#define DDR3_2133K	17
/* DDR3-2133 (12-12-12) */
#define DDR3_2133L	18
/* DDR3-2133 (13-13-13) */
#define DDR3_2133M	19
/* DDR3-2133 (14-14-14) */
#define DDR3_2133N	20
/* DDR3 ATF default */
#define DDR3_DEFAULT	21

#endif
