// SPDX-License-Identifier: GPL-2.0-only
/*****************************************************************************
 *
 * Copyright (C) 2008 Cedric Bregardis <cedric.bregardis@free.fr> and
 * Jean-Christian Hassler <jhassler@free.fr>
 * Copyright 1998 Emagic Soft- und Hardware GmbH
 * Copyright 2002 Martijn Sipkema
 *
 * This file is part of the Audiowerk2 ALSA driver
 *
 *****************************************************************************/

#define TSL_WS0		(1UL << 31)
#define	TSL_WS1		(1UL << 30)
#define	TSL_WS2		(1UL << 29)
#define TSL_WS3		(1UL << 28)
#define TSL_WS4		(1UL << 27)
#define	TSL_DIS_A1	(1UL << 24)
#define TSL_SDW_A1	(1UL << 23)
#define TSL_SIB_A1	(1UL << 22)
#define TSL_SF_A1	(1UL << 21)
#define	TSL_LF_A1	(1UL << 20)
#define TSL_BSEL_A1	(1UL << 17)
#define TSL_DOD_A1	(1UL << 15)
#define TSL_LOW_A1	(1UL << 14)
#define TSL_DIS_A2	(1UL << 11)
#define TSL_SDW_A2	(1UL << 10)
#define TSL_SIB_A2	(1UL << 9)
#define TSL_SF_A2	(1UL << 8)
#define TSL_LF_A2	(1UL << 7)
#define TSL_BSEL_A2	(1UL << 4)
#define TSL_DOD_A2	(1UL << 2)
#define TSL_LOW_A2	(1UL << 1)
#define TSL_EOS		(1UL << 0)

    /* Audiowerk8 hardware setup: */
    /*      WS0, SD4, TSL1  - Analog/ digital in */
    /*      WS1, SD0, TSL1  - Analog out #1, digital out */
    /*      WS2, SD2, TSL1  - Analog out #2 */
    /*      WS3, SD1, TSL2  - Analog out #3 */
    /*      WS4, SD3, TSL2  - Analog out #4 */

    /* Audiowerk8 timing: */
    /*      Timeslot:     | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | ... */

    /*      A1_INPUT: */
    /*      SD4:          <_ADC-L_>-------<_ADC-R_>-------< */
    /*      WS0:          _______________/---------------\_ */

    /*      A1_OUTPUT: */
    /*      SD0:          <_1-L___>-------<_1-R___>-------< */
    /*      WS1:          _______________/---------------\_ */
    /*      SD2:          >-------<_2-L___>-------<_2-R___> */
    /*      WS2:          -------\_______________/--------- */

    /*      A2_OUTPUT: */
    /*      SD1:          <_3-L___>-------<_3-R___>-------< */
    /*      WS3:          _______________/---------------\_ */
    /*      SD3:          >-------<_4-L___>-------<_4-R___> */
    /*      WS4:          -------\_______________/--------- */

static const int tsl1[8] = {
	1 * TSL_SDW_A1 | 3 * TSL_BSEL_A1 |
	0 * TSL_DIS_A1 | 0 * TSL_DOD_A1 | TSL_LF_A1,

	1 * TSL_SDW_A1 | 2 * TSL_BSEL_A1 |
	0 * TSL_DIS_A1 | 0 * TSL_DOD_A1,

	0 * TSL_SDW_A1 | 3 * TSL_BSEL_A1 |
	0 * TSL_DIS_A1 | 0 * TSL_DOD_A1,

	0 * TSL_SDW_A1 | 2 * TSL_BSEL_A1 |
	0 * TSL_DIS_A1 | 0 * TSL_DOD_A1,

	1 * TSL_SDW_A1 | 1 * TSL_BSEL_A1 |
	0 * TSL_DIS_A1 | 0 * TSL_DOD_A1 | TSL_WS1 | TSL_WS0,

	1 * TSL_SDW_A1 | 0 * TSL_BSEL_A1 |
	0 * TSL_DIS_A1 | 0 * TSL_DOD_A1 | TSL_WS1 | TSL_WS0,

	0 * TSL_SDW_A1 | 1 * TSL_BSEL_A1 |
	0 * TSL_DIS_A1 | 0 * TSL_DOD_A1 | TSL_WS1 | TSL_WS0,

	0 * TSL_SDW_A1 | 0 * TSL_BSEL_A1 | 0 * TSL_DIS_A1 |
	0 * TSL_DOD_A1 | TSL_WS1 | TSL_WS0 | TSL_SF_A1 | TSL_EOS,
};

static const int tsl2[8] = {
	0 * TSL_SDW_A2 | 3 * TSL_BSEL_A2 | 2 * TSL_DOD_A2 | TSL_LF_A2,
	0 * TSL_SDW_A2 | 2 * TSL_BSEL_A2 | 2 * TSL_DOD_A2,
	0 * TSL_SDW_A2 | 3 * TSL_BSEL_A2 | 2 * TSL_DOD_A2,
	0 * TSL_SDW_A2 | 2 * TSL_BSEL_A2 | 2 * TSL_DOD_A2,
	0 * TSL_SDW_A2 | 1 * TSL_BSEL_A2 | 2 * TSL_DOD_A2 | TSL_WS2,
	0 * TSL_SDW_A2 | 0 * TSL_BSEL_A2 | 2 * TSL_DOD_A2 | TSL_WS2,
	0 * TSL_SDW_A2 | 1 * TSL_BSEL_A2 | 2 * TSL_DOD_A2 | TSL_WS2,
	0 * TSL_SDW_A2 | 0 * TSL_BSEL_A2 | 2 * TSL_DOD_A2 | TSL_WS2 | TSL_EOS
};
