/* $Id: time.cpp $ */
/** @file
 * IPRT - Time.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_TIME
#include <iprt/time.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#include "internal/time.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The max year we possibly could implode. */
#define RTTIME_MAX_YEAR         (292 + 1970)
/** The min year we possibly could implode. */
#define RTTIME_MIN_YEAR         (-293 + 1970)

/** The max day supported by our time representation. (2262-04-11T23-47-16.854775807) */
#define RTTIME_MAX_DAY          (365*292+71 + 101-1)
/** The min day supported by our time representation. (1677-09-21T00-12-43.145224192) */
#define RTTIME_MIN_DAY          (365*-293-70 + 264-1)

/** The max nano second into the max day.             (2262-04-11T23-47-16.854775807) */
#define RTTIME_MAX_DAY_NANO     ( INT64_C(1000000000) * (23*3600 + 47*60 + 16) + 854775807 )
/** The min nano second into the min day.             (1677-09-21T00-12-43.145224192) */
#define RTTIME_MIN_DAY_NANO     ( INT64_C(1000000000) * (00*3600 + 12*60 + 43) + 145224192 )

/**
 * Asserts that a_pTime is normalized.
 */
#define RTTIME_ASSERT_NORMALIZED(a_pTime) \
    do \
    { \
        Assert(RT_ABS((a_pTime)->offUTC) <= 840); \
        Assert((a_pTime)->u32Nanosecond < 1000000000); \
        Assert((a_pTime)->u8Second < 60); \
        Assert((a_pTime)->u8Minute < 60); \
        Assert((a_pTime)->u8Hour < 24); \
        Assert((a_pTime)->u8Month >= 1 && (a_pTime)->u8Month <= 12); \
        Assert((a_pTime)->u8WeekDay < 7); \
        Assert((a_pTime)->u16YearDay >= 1); \
        Assert((a_pTime)->u16YearDay <= (rtTimeIsLeapYear((a_pTime)->i32Year) ? 366 : 365)); \
        Assert((a_pTime)->u8MonthDay >= 1 && (a_pTime)->u8MonthDay <= 31); \
    } while (0)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Days per month in a common year.
 */
static const uint8_t g_acDaysInMonths[12] =
{
  /*Jan Feb Mar Arp May Jun Jul Aug Sep Oct Nov Dec */
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/**
 * Days per month in a leap year.
 */
static const uint8_t g_acDaysInMonthsLeap[12] =
{
  /*Jan Feb Mar Arp May Jun Jul Aug Sep Oct Nov Dec */
    31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/**
 * The day of year for each month in a common year.
 */
static const uint16_t g_aiDayOfYear[12 + 1] =
{
    1,                                  /* Jan */
    1+31,                               /* Feb */
    1+31+28,                            /* Mar */
    1+31+28+31,                         /* Apr */
    1+31+28+31+30,                      /* May */
    1+31+28+31+30+31,                   /* Jun */
    1+31+28+31+30+31+30,                /* Jul */
    1+31+28+31+30+31+30+31,             /* Aug */
    1+31+28+31+30+31+30+31+31,          /* Sep */
    1+31+28+31+30+31+30+31+31+30,       /* Oct */
    1+31+28+31+30+31+30+31+31+30+31,    /* Nov */
    1+31+28+31+30+31+30+31+31+30+31+30, /* Dec */
    1+31+28+31+30+31+30+31+31+30+31+30+31
};

/**
 * The day of year for each month in a leap year.
 */
static const uint16_t g_aiDayOfYearLeap[12 + 1] =
{
    1,                                  /* Jan */
    1+31,                               /* Feb */
    1+31+29,                            /* Mar */
    1+31+29+31,                         /* Apr */
    1+31+29+31+30,                      /* May */
    1+31+29+31+30+31,                   /* Jun */
    1+31+29+31+30+31+30,                /* Jul */
    1+31+29+31+30+31+30+31,             /* Aug */
    1+31+29+31+30+31+30+31+31,          /* Sep */
    1+31+29+31+30+31+30+31+31+30,       /* Oct */
    1+31+29+31+30+31+30+31+31+30+31,    /* Nov */
    1+31+29+31+30+31+30+31+31+30+31+30, /* Dec */
    1+31+29+31+30+31+30+31+31+30+31+30+31
};

/** The index of 1970 in g_aoffYear */
#define OFF_YEAR_IDX_EPOCH  300
/** The year of the first index. */
#define OFF_YEAR_IDX_0_YEAR 1670

/**
 * The number of days the 1st of January a year is offseted from 1970-01-01.
 */
static const int32_t g_aoffYear[] =
{
/*1670:*/ 365*-300+-72, 365*-299+-72, 365*-298+-72, 365*-297+-71, 365*-296+-71, 365*-295+-71, 365*-294+-71, 365*-293+-70, 365*-292+-70, 365*-291+-70,
/*1680:*/ 365*-290+-70, 365*-289+-69, 365*-288+-69, 365*-287+-69, 365*-286+-69, 365*-285+-68, 365*-284+-68, 365*-283+-68, 365*-282+-68, 365*-281+-67,
/*1690:*/ 365*-280+-67, 365*-279+-67, 365*-278+-67, 365*-277+-66, 365*-276+-66, 365*-275+-66, 365*-274+-66, 365*-273+-65, 365*-272+-65, 365*-271+-65,
/*1700:*/ 365*-270+-65, 365*-269+-65, 365*-268+-65, 365*-267+-65, 365*-266+-65, 365*-265+-64, 365*-264+-64, 365*-263+-64, 365*-262+-64, 365*-261+-63,
/*1710:*/ 365*-260+-63, 365*-259+-63, 365*-258+-63, 365*-257+-62, 365*-256+-62, 365*-255+-62, 365*-254+-62, 365*-253+-61, 365*-252+-61, 365*-251+-61,
/*1720:*/ 365*-250+-61, 365*-249+-60, 365*-248+-60, 365*-247+-60, 365*-246+-60, 365*-245+-59, 365*-244+-59, 365*-243+-59, 365*-242+-59, 365*-241+-58,
/*1730:*/ 365*-240+-58, 365*-239+-58, 365*-238+-58, 365*-237+-57, 365*-236+-57, 365*-235+-57, 365*-234+-57, 365*-233+-56, 365*-232+-56, 365*-231+-56,
/*1740:*/ 365*-230+-56, 365*-229+-55, 365*-228+-55, 365*-227+-55, 365*-226+-55, 365*-225+-54, 365*-224+-54, 365*-223+-54, 365*-222+-54, 365*-221+-53,
/*1750:*/ 365*-220+-53, 365*-219+-53, 365*-218+-53, 365*-217+-52, 365*-216+-52, 365*-215+-52, 365*-214+-52, 365*-213+-51, 365*-212+-51, 365*-211+-51,
/*1760:*/ 365*-210+-51, 365*-209+-50, 365*-208+-50, 365*-207+-50, 365*-206+-50, 365*-205+-49, 365*-204+-49, 365*-203+-49, 365*-202+-49, 365*-201+-48,
/*1770:*/ 365*-200+-48, 365*-199+-48, 365*-198+-48, 365*-197+-47, 365*-196+-47, 365*-195+-47, 365*-194+-47, 365*-193+-46, 365*-192+-46, 365*-191+-46,
/*1780:*/ 365*-190+-46, 365*-189+-45, 365*-188+-45, 365*-187+-45, 365*-186+-45, 365*-185+-44, 365*-184+-44, 365*-183+-44, 365*-182+-44, 365*-181+-43,
/*1790:*/ 365*-180+-43, 365*-179+-43, 365*-178+-43, 365*-177+-42, 365*-176+-42, 365*-175+-42, 365*-174+-42, 365*-173+-41, 365*-172+-41, 365*-171+-41,
/*1800:*/ 365*-170+-41, 365*-169+-41, 365*-168+-41, 365*-167+-41, 365*-166+-41, 365*-165+-40, 365*-164+-40, 365*-163+-40, 365*-162+-40, 365*-161+-39,
/*1810:*/ 365*-160+-39, 365*-159+-39, 365*-158+-39, 365*-157+-38, 365*-156+-38, 365*-155+-38, 365*-154+-38, 365*-153+-37, 365*-152+-37, 365*-151+-37,
/*1820:*/ 365*-150+-37, 365*-149+-36, 365*-148+-36, 365*-147+-36, 365*-146+-36, 365*-145+-35, 365*-144+-35, 365*-143+-35, 365*-142+-35, 365*-141+-34,
/*1830:*/ 365*-140+-34, 365*-139+-34, 365*-138+-34, 365*-137+-33, 365*-136+-33, 365*-135+-33, 365*-134+-33, 365*-133+-32, 365*-132+-32, 365*-131+-32,
/*1840:*/ 365*-130+-32, 365*-129+-31, 365*-128+-31, 365*-127+-31, 365*-126+-31, 365*-125+-30, 365*-124+-30, 365*-123+-30, 365*-122+-30, 365*-121+-29,
/*1850:*/ 365*-120+-29, 365*-119+-29, 365*-118+-29, 365*-117+-28, 365*-116+-28, 365*-115+-28, 365*-114+-28, 365*-113+-27, 365*-112+-27, 365*-111+-27,
/*1860:*/ 365*-110+-27, 365*-109+-26, 365*-108+-26, 365*-107+-26, 365*-106+-26, 365*-105+-25, 365*-104+-25, 365*-103+-25, 365*-102+-25, 365*-101+-24,
/*1870:*/ 365*-100+-24, 365* -99+-24, 365* -98+-24, 365* -97+-23, 365* -96+-23, 365* -95+-23, 365* -94+-23, 365* -93+-22, 365* -92+-22, 365* -91+-22,
/*1880:*/ 365* -90+-22, 365* -89+-21, 365* -88+-21, 365* -87+-21, 365* -86+-21, 365* -85+-20, 365* -84+-20, 365* -83+-20, 365* -82+-20, 365* -81+-19,
/*1890:*/ 365* -80+-19, 365* -79+-19, 365* -78+-19, 365* -77+-18, 365* -76+-18, 365* -75+-18, 365* -74+-18, 365* -73+-17, 365* -72+-17, 365* -71+-17,
/*1900:*/ 365* -70+-17, 365* -69+-17, 365* -68+-17, 365* -67+-17, 365* -66+-17, 365* -65+-16, 365* -64+-16, 365* -63+-16, 365* -62+-16, 365* -61+-15,
/*1910:*/ 365* -60+-15, 365* -59+-15, 365* -58+-15, 365* -57+-14, 365* -56+-14, 365* -55+-14, 365* -54+-14, 365* -53+-13, 365* -52+-13, 365* -51+-13,
/*1920:*/ 365* -50+-13, 365* -49+-12, 365* -48+-12, 365* -47+-12, 365* -46+-12, 365* -45+-11, 365* -44+-11, 365* -43+-11, 365* -42+-11, 365* -41+-10,
/*1930:*/ 365* -40+-10, 365* -39+-10, 365* -38+-10, 365* -37+-9 , 365* -36+-9 , 365* -35+-9 , 365* -34+-9 , 365* -33+-8 , 365* -32+-8 , 365* -31+-8 ,
/*1940:*/ 365* -30+-8 , 365* -29+-7 , 365* -28+-7 , 365* -27+-7 , 365* -26+-7 , 365* -25+-6 , 365* -24+-6 , 365* -23+-6 , 365* -22+-6 , 365* -21+-5 ,
/*1950:*/ 365* -20+-5 , 365* -19+-5 , 365* -18+-5 , 365* -17+-4 , 365* -16+-4 , 365* -15+-4 , 365* -14+-4 , 365* -13+-3 , 365* -12+-3 , 365* -11+-3 ,
/*1960:*/ 365* -10+-3 , 365*  -9+-2 , 365*  -8+-2 , 365*  -7+-2 , 365*  -6+-2 , 365*  -5+-1 , 365*  -4+-1 , 365*  -3+-1 , 365*  -2+-1 , 365*  -1+0  ,
/*1970:*/ 365*   0+0  , 365*   1+0  , 365*   2+0  , 365*   3+1  , 365*   4+1  , 365*   5+1  , 365*   6+1  , 365*   7+2  , 365*   8+2  , 365*   9+2  ,
/*1980:*/ 365*  10+2  , 365*  11+3  , 365*  12+3  , 365*  13+3  , 365*  14+3  , 365*  15+4  , 365*  16+4  , 365*  17+4  , 365*  18+4  , 365*  19+5  ,
/*1990:*/ 365*  20+5  , 365*  21+5  , 365*  22+5  , 365*  23+6  , 365*  24+6  , 365*  25+6  , 365*  26+6  , 365*  27+7  , 365*  28+7  , 365*  29+7  ,
/*2000:*/ 365*  30+7  , 365*  31+8  , 365*  32+8  , 365*  33+8  , 365*  34+8  , 365*  35+9  , 365*  36+9  , 365*  37+9  , 365*  38+9  , 365*  39+10 ,
/*2010:*/ 365*  40+10 , 365*  41+10 , 365*  42+10 , 365*  43+11 , 365*  44+11 , 365*  45+11 , 365*  46+11 , 365*  47+12 , 365*  48+12 , 365*  49+12 ,
/*2020:*/ 365*  50+12 , 365*  51+13 , 365*  52+13 , 365*  53+13 , 365*  54+13 , 365*  55+14 , 365*  56+14 , 365*  57+14 , 365*  58+14 , 365*  59+15 ,
/*2030:*/ 365*  60+15 , 365*  61+15 , 365*  62+15 , 365*  63+16 , 365*  64+16 , 365*  65+16 , 365*  66+16 , 365*  67+17 , 365*  68+17 , 365*  69+17 ,
/*2040:*/ 365*  70+17 , 365*  71+18 , 365*  72+18 , 365*  73+18 , 365*  74+18 , 365*  75+19 , 365*  76+19 , 365*  77+19 , 365*  78+19 , 365*  79+20 ,
/*2050:*/ 365*  80+20 , 365*  81+20 , 365*  82+20 , 365*  83+21 , 365*  84+21 , 365*  85+21 , 365*  86+21 , 365*  87+22 , 365*  88+22 , 365*  89+22 ,
/*2060:*/ 365*  90+22 , 365*  91+23 , 365*  92+23 , 365*  93+23 , 365*  94+23 , 365*  95+24 , 365*  96+24 , 365*  97+24 , 365*  98+24 , 365*  99+25 ,
/*2070:*/ 365* 100+25 , 365* 101+25 , 365* 102+25 , 365* 103+26 , 365* 104+26 , 365* 105+26 , 365* 106+26 , 365* 107+27 , 365* 108+27 , 365* 109+27 ,
/*2080:*/ 365* 110+27 , 365* 111+28 , 365* 112+28 , 365* 113+28 , 365* 114+28 , 365* 115+29 , 365* 116+29 , 365* 117+29 , 365* 118+29 , 365* 119+30 ,
/*2090:*/ 365* 120+30 , 365* 121+30 , 365* 122+30 , 365* 123+31 , 365* 124+31 , 365* 125+31 , 365* 126+31 , 365* 127+32 , 365* 128+32 , 365* 129+32 ,
/*2100:*/ 365* 130+32 , 365* 131+32 , 365* 132+32 , 365* 133+32 , 365* 134+32 , 365* 135+33 , 365* 136+33 , 365* 137+33 , 365* 138+33 , 365* 139+34 ,
/*2110:*/ 365* 140+34 , 365* 141+34 , 365* 142+34 , 365* 143+35 , 365* 144+35 , 365* 145+35 , 365* 146+35 , 365* 147+36 , 365* 148+36 , 365* 149+36 ,
/*2120:*/ 365* 150+36 , 365* 151+37 , 365* 152+37 , 365* 153+37 , 365* 154+37 , 365* 155+38 , 365* 156+38 , 365* 157+38 , 365* 158+38 , 365* 159+39 ,
/*2130:*/ 365* 160+39 , 365* 161+39 , 365* 162+39 , 365* 163+40 , 365* 164+40 , 365* 165+40 , 365* 166+40 , 365* 167+41 , 365* 168+41 , 365* 169+41 ,
/*2140:*/ 365* 170+41 , 365* 171+42 , 365* 172+42 , 365* 173+42 , 365* 174+42 , 365* 175+43 , 365* 176+43 , 365* 177+43 , 365* 178+43 , 365* 179+44 ,
/*2150:*/ 365* 180+44 , 365* 181+44 , 365* 182+44 , 365* 183+45 , 365* 184+45 , 365* 185+45 , 365* 186+45 , 365* 187+46 , 365* 188+46 , 365* 189+46 ,
/*2160:*/ 365* 190+46 , 365* 191+47 , 365* 192+47 , 365* 193+47 , 365* 194+47 , 365* 195+48 , 365* 196+48 , 365* 197+48 , 365* 198+48 , 365* 199+49 ,
/*2170:*/ 365* 200+49 , 365* 201+49 , 365* 202+49 , 365* 203+50 , 365* 204+50 , 365* 205+50 , 365* 206+50 , 365* 207+51 , 365* 208+51 , 365* 209+51 ,
/*2180:*/ 365* 210+51 , 365* 211+52 , 365* 212+52 , 365* 213+52 , 365* 214+52 , 365* 215+53 , 365* 216+53 , 365* 217+53 , 365* 218+53 , 365* 219+54 ,
/*2190:*/ 365* 220+54 , 365* 221+54 , 365* 222+54 , 365* 223+55 , 365* 224+55 , 365* 225+55 , 365* 226+55 , 365* 227+56 , 365* 228+56 , 365* 229+56 ,
/*2200:*/ 365* 230+56 , 365* 231+56 , 365* 232+56 , 365* 233+56 , 365* 234+56 , 365* 235+57 , 365* 236+57 , 365* 237+57 , 365* 238+57 , 365* 239+58 ,
/*2210:*/ 365* 240+58 , 365* 241+58 , 365* 242+58 , 365* 243+59 , 365* 244+59 , 365* 245+59 , 365* 246+59 , 365* 247+60 , 365* 248+60 , 365* 249+60 ,
/*2220:*/ 365* 250+60 , 365* 251+61 , 365* 252+61 , 365* 253+61 , 365* 254+61 , 365* 255+62 , 365* 256+62 , 365* 257+62 , 365* 258+62 , 365* 259+63 ,
/*2230:*/ 365* 260+63 , 365* 261+63 , 365* 262+63 , 365* 263+64 , 365* 264+64 , 365* 265+64 , 365* 266+64 , 365* 267+65 , 365* 268+65 , 365* 269+65 ,
/*2240:*/ 365* 270+65 , 365* 271+66 , 365* 272+66 , 365* 273+66 , 365* 274+66 , 365* 275+67 , 365* 276+67 , 365* 277+67 , 365* 278+67 , 365* 279+68 ,
/*2250:*/ 365* 280+68 , 365* 281+68 , 365* 282+68 , 365* 283+69 , 365* 284+69 , 365* 285+69 , 365* 286+69 , 365* 287+70 , 365* 288+70 , 365* 289+70 ,
/*2260:*/ 365* 290+70 , 365* 291+71 , 365* 292+71 , 365* 293+71 , 365* 294+71 , 365* 295+72 , 365* 296+72 , 365* 297+72 , 365* 298+72 , 365* 299+73
};

/* generator code:
#include <stdio.h>
bool isLeapYear(int iYear)
{
    return iYear % 4 == 0 && (iYear % 100 != 0 || iYear % 400 == 0);
}
void printYear(int iYear, int iLeap)
{
    if (!(iYear % 10))
        printf("\n/" "*%d:*" "/", iYear + 1970);
    printf(" 365*%4d+%-3d,", iYear, iLeap);
}
int main()
{
    int iYear = 0;
    int iLeap = 0;
    while (iYear > -300)
        iLeap -= isLeapYear(1970 + --iYear);
    while (iYear < 300)
    {
        printYear(iYear, iLeap);
        iLeap += isLeapYear(1970 + iYear++);
    }
    printf("\n");
    return 0;
}
*/

/** RFC-1123 week day names. */
static const char * const g_apszWeekDays[7] =
{
    "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};
/** RFC-1123 month of the year names. */
static const char * const g_apszMonths[1+12] =
{
    "000", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};


/**
 * Checks if a year is a leap year or not.
 *
 * @returns true if it's a leap year.
 * @returns false if it's a common year.
 * @param   i32Year     The year in question.
 */
DECLINLINE(bool) rtTimeIsLeapYear(int32_t i32Year)
{
    return i32Year % 4 == 0
        && (    i32Year % 100 != 0
            ||  i32Year % 400 == 0);
}


/**
 * Checks if a year is a leap year or not.
 *
 * @returns true if it's a leap year.
 * @returns false if it's a common year.
 * @param   i32Year     The year in question.
 */
RTDECL(bool) RTTimeIsLeapYear(int32_t i32Year)
{
    return rtTimeIsLeapYear(i32Year);
}
RT_EXPORT_SYMBOL(RTTimeIsLeapYear);


/**
 * Explodes a time spec (UTC).
 *
 * @returns pTime.
 * @param   pTime       Where to store the exploded time.
 * @param   pTimeSpec   The time spec to exploded.
 */
RTDECL(PRTTIME) RTTimeExplode(PRTTIME pTime, PCRTTIMESPEC pTimeSpec)
{
    int64_t         i64Div;
    int32_t         i32Div;
    int32_t         i32Rem;
    unsigned        iYear;
    const uint16_t *paiDayOfYear;
    int             iMonth;

    AssertMsg(VALID_PTR(pTime), ("%p\n", pTime));
    AssertMsg(VALID_PTR(pTimeSpec), ("%p\n", pTime));

    /*
     * The simple stuff first.
     */
    pTime->fFlags = RTTIME_FLAGS_TYPE_UTC;
    i64Div = pTimeSpec->i64NanosecondsRelativeToUnixEpoch;
    i32Rem = (int32_t)(i64Div % 1000000000);
    i64Div /= 1000000000;
    if (i32Rem < 0)
    {
        i32Rem += 1000000000;
        i64Div--;
    }
    pTime->u32Nanosecond = i32Rem;

    /* second */
    i32Rem = (int32_t)(i64Div % 60);
    i64Div /= 60;
    if (i32Rem < 0)
    {
        i32Rem += 60;
        i64Div--;
    }
    pTime->u8Second      = i32Rem;

    /* minute */
    i32Div = (int32_t)i64Div;   /* 60,000,000,000 > 33bit, so 31bit suffices. */
    i32Rem = i32Div % 60;
    i32Div /= 60;
    if (i32Rem < 0)
    {
        i32Rem += 60;
        i32Div--;
    }
    pTime->u8Minute      = i32Rem;

    /* hour */
    i32Rem = i32Div % 24;
    i32Div /= 24;                       /* days relative to 1970-01-01 */
    if (i32Rem < 0)
    {
        i32Rem += 24;
        i32Div--;
    }
    pTime->u8Hour        = i32Rem;

    /* weekday - 1970-01-01 was a Thursday (3) */
    pTime->u8WeekDay     = ((int)(i32Div % 7) + 3 + 7) % 7;

    /*
     * We've now got a number of days relative to 1970-01-01.
     * To get the correct year number we have to mess with leap years. Fortunately,
     * the representation we've got only supports a few hundred years, so we can
     * generate a table and perform a simple two way search from the modulus 365 derived.
     */
    iYear = OFF_YEAR_IDX_EPOCH + i32Div / 365;
    while (g_aoffYear[iYear + 1] <= i32Div)
        iYear++;
    while (g_aoffYear[iYear] > i32Div)
        iYear--;
    pTime->i32Year       = iYear + OFF_YEAR_IDX_0_YEAR;
    i32Div -= g_aoffYear[iYear];
    pTime->u16YearDay    = i32Div + 1;

    /*
     * Figuring out the month is done in a manner similar to the year, only here we
     * ensure that the index is matching or too small.
     */
    if (rtTimeIsLeapYear(pTime->i32Year))
    {
        pTime->fFlags   |= RTTIME_FLAGS_LEAP_YEAR;
        paiDayOfYear = &g_aiDayOfYearLeap[0];
    }
    else
    {
        pTime->fFlags   |= RTTIME_FLAGS_COMMON_YEAR;
        paiDayOfYear = &g_aiDayOfYear[0];
    }
    iMonth = i32Div / 32;
    i32Div++;
    while (paiDayOfYear[iMonth + 1] <= i32Div)
        iMonth++;
    pTime->u8Month       = iMonth + 1;
    i32Div -= paiDayOfYear[iMonth];
    pTime->u8MonthDay    = i32Div + 1;

    /* This is for UTC timespecs, so, no offset. */
    pTime->offUTC        = 0;

    return pTime;
}
RT_EXPORT_SYMBOL(RTTimeExplode);


/**
 * Implodes exploded time to a time spec (UTC).
 *
 * @returns pTime on success.
 * @returns NULL if the pTime data is invalid.
 * @param   pTimeSpec   Where to store the imploded UTC time.
 *                      If pTime specifies a time which outside the range, maximum or
 *                      minimum values will be returned.
 * @param   pTime       Pointer to the exploded time to implode.
 *                      The fields u8Month, u8WeekDay and u8MonthDay are not used,
 *                      and all the other fields are expected to be within their
 *                      bounds. Use RTTimeNormalize() or RTTimeLocalNormalize() to
 *                      calculate u16YearDay and normalize the ranges of the fields.
 */
RTDECL(PRTTIMESPEC) RTTimeImplode(PRTTIMESPEC pTimeSpec, PCRTTIME pTime)
{
    int32_t     i32Days;
    uint32_t    u32Secs;
    int64_t     i64Nanos;

    /*
     * Validate input.
     */
    AssertReturn(VALID_PTR(pTimeSpec), NULL);
    AssertReturn(VALID_PTR(pTime), NULL);
    AssertReturn(pTime->u32Nanosecond < 1000000000, NULL);
    AssertReturn(pTime->u8Second < 60, NULL);
    AssertReturn(pTime->u8Minute < 60, NULL);
    AssertReturn(pTime->u8Hour < 24, NULL);
    AssertReturn(pTime->u16YearDay >= 1, NULL);
    AssertReturn(pTime->u16YearDay <= (rtTimeIsLeapYear(pTime->i32Year) ? 366 : 365), NULL);
    AssertMsgReturn(pTime->i32Year <= RTTIME_MAX_YEAR && pTime->i32Year >= RTTIME_MIN_YEAR, ("%RI32\n", pTime->i32Year), NULL);
    Assert(pTime->offUTC >= -840 && pTime->offUTC <= 840);

    /*
     * Do the conversion to nanoseconds.
     */
    i32Days  = g_aoffYear[pTime->i32Year - OFF_YEAR_IDX_0_YEAR]
             + pTime->u16YearDay - 1;
    AssertMsgReturn(i32Days <= RTTIME_MAX_DAY && i32Days >= RTTIME_MIN_DAY, ("%RI32\n", i32Days), NULL);

    u32Secs  = pTime->u8Second
             + pTime->u8Minute * 60
             + pTime->u8Hour   * 3600;
    i64Nanos = (uint64_t)pTime->u32Nanosecond
             + u32Secs * UINT64_C(1000000000);
    AssertMsgReturn(i32Days != RTTIME_MAX_DAY || i64Nanos <= RTTIME_MAX_DAY_NANO, ("%RI64\n", i64Nanos), NULL);
    AssertMsgReturn(i32Days != RTTIME_MIN_DAY || i64Nanos >= RTTIME_MIN_DAY_NANO, ("%RI64\n", i64Nanos), NULL);

    i64Nanos += i32Days * UINT64_C(86400000000000);
    if ((pTime->fFlags & RTTIME_FLAGS_TYPE_MASK) == RTTIME_FLAGS_TYPE_LOCAL)
        i64Nanos -= pTime->offUTC * RT_NS_1MIN;

    pTimeSpec->i64NanosecondsRelativeToUnixEpoch = i64Nanos;
    return pTimeSpec;
}
RT_EXPORT_SYMBOL(RTTimeImplode);


/**
 * Internal worker for RTTimeNormalize and RTTimeLocalNormalize.
 */
static PRTTIME rtTimeNormalizeInternal(PRTTIME pTime)
{
    unsigned    uSecond;
    unsigned    uMinute;
    unsigned    uHour;
    bool        fLeapYear;

    /*
     * Fix the YearDay and Month/MonthDay.
     */
    fLeapYear = rtTimeIsLeapYear(pTime->i32Year);
    if (!pTime->u16YearDay)
    {
        /*
         * The Month+MonthDay must present, overflow adjust them and calc the year day.
         */
        AssertMsgReturn(    pTime->u8Month
                        &&  pTime->u8MonthDay,
                        ("date=%d-%d-%d\n", pTime->i32Year, pTime->u8Month, pTime->u8MonthDay),
                        NULL);
        while (pTime->u8Month > 12)
        {
            pTime->u8Month -= 12;
            pTime->i32Year++;
            fLeapYear = rtTimeIsLeapYear(pTime->i32Year);
            pTime->fFlags &= ~(RTTIME_FLAGS_COMMON_YEAR | RTTIME_FLAGS_LEAP_YEAR);
        }

        for (;;)
        {
            unsigned cDaysInMonth = fLeapYear
                                  ? g_acDaysInMonthsLeap[pTime->u8Month - 1]
                                  : g_acDaysInMonths[pTime->u8Month - 1];
            if (pTime->u8MonthDay <= cDaysInMonth)
                break;
            pTime->u8MonthDay -= cDaysInMonth;
            if (pTime->u8Month != 12)
                pTime->u8Month++;
            else
            {
                pTime->u8Month = 1;
                pTime->i32Year++;
                fLeapYear = rtTimeIsLeapYear(pTime->i32Year);
                pTime->fFlags &= ~(RTTIME_FLAGS_COMMON_YEAR | RTTIME_FLAGS_LEAP_YEAR);
            }
        }

        pTime->u16YearDay  = pTime->u8MonthDay - 1
                           + (fLeapYear
                              ? g_aiDayOfYearLeap[pTime->u8Month - 1]
                              : g_aiDayOfYear[pTime->u8Month - 1]);
    }
    else
    {
        /*
         * Are both YearDay and Month/MonthDay valid?
         * Check that they don't overflow and match, if not use YearDay (simpler).
         */
        bool fRecalc = true;
        if (    pTime->u8Month
            &&  pTime->u8MonthDay)
        {
            do
            {
                uint16_t u16YearDay;

                /* If you change one, zero the other to make clear what you mean. */
                AssertBreak(pTime->u8Month <= 12);
                AssertBreak(pTime->u8MonthDay <= (fLeapYear
                                                  ? g_acDaysInMonthsLeap[pTime->u8Month - 1]
                                                  : g_acDaysInMonths[pTime->u8Month - 1]));
                u16YearDay = pTime->u8MonthDay - 1
                           + (fLeapYear
                              ? g_aiDayOfYearLeap[pTime->u8Month - 1]
                              : g_aiDayOfYear[pTime->u8Month - 1]);
                AssertBreak(u16YearDay == pTime->u16YearDay);
                fRecalc = false;
            } while (0);
        }
        if (fRecalc)
        {
            const uint16_t *paiDayOfYear;

            /* overflow adjust YearDay */
            while (pTime->u16YearDay > (fLeapYear ? 366 : 365))
            {
                pTime->u16YearDay -= fLeapYear ? 366 : 365;
                pTime->i32Year++;
                fLeapYear = rtTimeIsLeapYear(pTime->i32Year);
                pTime->fFlags &= ~(RTTIME_FLAGS_COMMON_YEAR | RTTIME_FLAGS_LEAP_YEAR);
            }

            /* calc Month and MonthDay */
            paiDayOfYear = fLeapYear
                         ? &g_aiDayOfYearLeap[0]
                         : &g_aiDayOfYear[0];
            pTime->u8Month = 1;
            while (pTime->u16YearDay >= paiDayOfYear[pTime->u8Month])
                pTime->u8Month++;
            Assert(pTime->u8Month >= 1 && pTime->u8Month <= 12);
            pTime->u8MonthDay = pTime->u16YearDay - paiDayOfYear[pTime->u8Month - 1] + 1;
        }
    }

    /*
     * Fixup time overflows.
     * Use unsigned int values internally to avoid overflows.
     */
    uSecond = pTime->u8Second;
    uMinute = pTime->u8Minute;
    uHour   = pTime->u8Hour;

    while (pTime->u32Nanosecond >= 1000000000)
    {
        pTime->u32Nanosecond -= 1000000000;
        uSecond++;
    }

    while (uSecond >= 60)
    {
        uSecond -= 60;
        uMinute++;
    }

    while (uMinute >= 60)
    {
        uMinute -= 60;
        uHour++;
    }

    while (uHour >= 24)
    {
        uHour -= 24;

        /* This is really a RTTimeIncDay kind of thing... */
        if (pTime->u16YearDay + 1 != (fLeapYear ? g_aiDayOfYearLeap[pTime->u8Month] : g_aiDayOfYear[pTime->u8Month]))
        {
            pTime->u16YearDay++;
            pTime->u8MonthDay++;
        }
        else if (pTime->u8Month != 12)
        {
            pTime->u16YearDay++;
            pTime->u8Month++;
            pTime->u8MonthDay = 1;
        }
        else
        {
            pTime->i32Year++;
            fLeapYear = rtTimeIsLeapYear(pTime->i32Year);
            pTime->fFlags &= ~(RTTIME_FLAGS_COMMON_YEAR | RTTIME_FLAGS_LEAP_YEAR);
            pTime->u16YearDay = 1;
            pTime->u8Month = 1;
            pTime->u8MonthDay = 1;
        }
    }

    pTime->u8Second = uSecond;
    pTime->u8Minute = uMinute;
    pTime->u8Hour = uHour;

    /*
     * Correct the leap year flag.
     * Assert if it's wrong, but ignore if unset.
     */
    if (fLeapYear)
    {
        Assert(!(pTime->fFlags & RTTIME_FLAGS_COMMON_YEAR));
        pTime->fFlags &= ~RTTIME_FLAGS_COMMON_YEAR;
        pTime->fFlags |= RTTIME_FLAGS_LEAP_YEAR;
    }
    else
    {
        Assert(!(pTime->fFlags & RTTIME_FLAGS_LEAP_YEAR));
        pTime->fFlags &= ~RTTIME_FLAGS_LEAP_YEAR;
        pTime->fFlags |= RTTIME_FLAGS_COMMON_YEAR;
    }


    /*
     * Calc week day.
     *
     * 1970-01-01 was a Thursday (3), so find the number of days relative to
     * that point. We use the table when possible and a slow+stupid+brute-force
     * algorithm for points outside it. Feel free to optimize the latter by
     * using some clever formula.
     */
    if (    pTime->i32Year >= OFF_YEAR_IDX_0_YEAR
        &&  pTime->i32Year <  OFF_YEAR_IDX_0_YEAR + (int32_t)RT_ELEMENTS(g_aoffYear))
    {
        int32_t offDays = g_aoffYear[pTime->i32Year - OFF_YEAR_IDX_0_YEAR]
                        + pTime->u16YearDay -1;
        pTime->u8WeekDay = ((offDays % 7) + 3 + 7) % 7;
    }
    else
    {
        int32_t i32Year = pTime->i32Year;
        if (i32Year >= 1970)
        {
            uint64_t offDays = pTime->u16YearDay - 1;
            while (--i32Year >= 1970)
                offDays += rtTimeIsLeapYear(i32Year) ? 366 : 365;
            pTime->u8WeekDay = (uint8_t)((offDays + 3) % 7);
        }
        else
        {
            int64_t offDays = (fLeapYear ? -366 - 1 : -365 - 1) + pTime->u16YearDay;
            while (++i32Year < 1970)
                offDays -= rtTimeIsLeapYear(i32Year) ? 366 : 365;
            pTime->u8WeekDay = ((int)(offDays % 7) + 3 + 7) % 7;
        }
    }
    return pTime;
}


/**
 * Normalizes the fields of a time structure.
 *
 * It is possible to calculate year-day from month/day and vice
 * versa. If you adjust any of these, make sure to zero the
 * other so you make it clear which of the fields to use. If
 * it's ambiguous, the year-day field is used (and you get
 * assertions in debug builds).
 *
 * All the time fields and the year-day or month/day fields will
 * be adjusted for overflows. (Since all fields are unsigned, there
 * is no underflows.) It is possible to exploit this for simple
 * date math, though the recommended way of doing that to implode
 * the time into a timespec and do the math on that.
 *
 * @returns pTime on success.
 * @returns NULL if the data is invalid.
 *
 * @param   pTime       The time structure to normalize.
 *
 * @remarks This function doesn't work with local time, only with UTC time.
 */
RTDECL(PRTTIME) RTTimeNormalize(PRTTIME pTime)
{
    /*
     * Validate that we've got the minimum of stuff handy.
     */
    AssertReturn(VALID_PTR(pTime), NULL);
    AssertMsgReturn(!(pTime->fFlags & ~RTTIME_FLAGS_MASK), ("%#x\n", pTime->fFlags), NULL);
    AssertMsgReturn((pTime->fFlags & RTTIME_FLAGS_TYPE_MASK) != RTTIME_FLAGS_TYPE_LOCAL, ("Use RTTimeLocalNormalize!\n"), NULL);
    AssertMsgReturn(pTime->offUTC == 0, ("%d; Use RTTimeLocalNormalize!\n", pTime->offUTC), NULL);

    pTime = rtTimeNormalizeInternal(pTime);
    if (pTime)
        pTime->fFlags |= RTTIME_FLAGS_TYPE_UTC;
    return pTime;
}
RT_EXPORT_SYMBOL(RTTimeNormalize);


/**
 * Normalizes the fields of a time structure, assuming local time.
 *
 * It is possible to calculate year-day from month/day and vice
 * versa. If you adjust any of these, make sure to zero the
 * other so you make it clear which of the fields to use. If
 * it's ambiguous, the year-day field is used (and you get
 * assertions in debug builds).
 *
 * All the time fields and the year-day or month/day fields will
 * be adjusted for overflows. (Since all fields are unsigned, there
 * is no underflows.) It is possible to exploit this for simple
 * date math, though the recommended way of doing that to implode
 * the time into a timespec and do the math on that.
 *
 * @returns pTime on success.
 * @returns NULL if the data is invalid.
 *
 * @param   pTime       The time structure to normalize.
 *
 * @remarks This function doesn't work with UTC time, only with local time.
 */
RTDECL(PRTTIME) RTTimeLocalNormalize(PRTTIME pTime)
{
    /*
     * Validate that we've got the minimum of stuff handy.
     */
    AssertReturn(VALID_PTR(pTime), NULL);
    AssertMsgReturn(!(pTime->fFlags & ~RTTIME_FLAGS_MASK), ("%#x\n", pTime->fFlags), NULL);
    AssertMsgReturn((pTime->fFlags & RTTIME_FLAGS_TYPE_MASK) != RTTIME_FLAGS_TYPE_UTC, ("Use RTTimeNormalize!\n"), NULL);

    pTime = rtTimeNormalizeInternal(pTime);
    if (pTime)
        pTime->fFlags |= RTTIME_FLAGS_TYPE_LOCAL;
    return pTime;
}
RT_EXPORT_SYMBOL(RTTimeLocalNormalize);


/**
 * Converts a time spec to a ISO date string.
 *
 * @returns psz on success.
 * @returns NULL on buffer underflow.
 * @param   pTime       The time. Caller should've normalized this.
 * @param   psz         Where to store the string.
 * @param   cb          The size of the buffer.
 */
RTDECL(char *) RTTimeToString(PCRTTIME pTime, char *psz, size_t cb)
{
    size_t cch;

    /* (Default to UTC if not specified) */
    if (    (pTime->fFlags & RTTIME_FLAGS_TYPE_MASK) == RTTIME_FLAGS_TYPE_LOCAL
        &&  pTime->offUTC)
    {
        int32_t  offUTC = pTime->offUTC;
        Assert(offUTC <= 840 && offUTC >= -840);
        char     chSign;
        if (offUTC >= 0)
            chSign = '+';
        else
        {
            chSign = '-';
            offUTC = -offUTC;
        }
        uint32_t offUTCHour   = (uint32_t)offUTC / 60;
        uint32_t offUTCMinute = (uint32_t)offUTC % 60;
        cch = RTStrPrintf(psz, cb,
                          "%RI32-%02u-%02uT%02u:%02u:%02u.%09RU32%c%02d%:02d",
                          pTime->i32Year, pTime->u8Month, pTime->u8MonthDay,
                          pTime->u8Hour, pTime->u8Minute, pTime->u8Second, pTime->u32Nanosecond,
                          chSign, offUTCHour, offUTCMinute);
        if (    cch <= 15
            ||  psz[cch - 6] != chSign)
            return NULL;
    }
    else
    {
        cch = RTStrPrintf(psz, cb, "%RI32-%02u-%02uT%02u:%02u:%02u.%09RU32Z",
                          pTime->i32Year, pTime->u8Month, pTime->u8MonthDay,
                          pTime->u8Hour, pTime->u8Minute, pTime->u8Second, pTime->u32Nanosecond);
        if (    cch <= 15
            ||  psz[cch - 1] != 'Z')
            return NULL;
    }
    return psz;
}
RT_EXPORT_SYMBOL(RTTimeToString);


/**
 * Converts a time spec to a ISO date string, extended version.
 *
 * @returns Output string length on success (positive), VERR_BUFFER_OVERFLOW
 *          (negative) or VERR_OUT_OF_RANGE (negative) on failure.
 * @param   pTime           The time. Caller should've normalized this.
 * @param   psz             Where to store the string.
 * @param   cb              The size of the buffer.
 * @param   cFractionDigits Number of digits in the fraction.  Max is 9.
 */
RTDECL(ssize_t) RTTimeToStringEx(PCRTTIME pTime, char *psz, size_t cb, unsigned cFractionDigits)
{
    size_t cch;

    /* Format the fraction. */
    char szFraction[16];
    if (!cFractionDigits)
        szFraction[0] = '\0';
    else
    {
        AssertReturn(cFractionDigits <= 9, VERR_OUT_OF_RANGE);
        Assert(pTime->u32Nanosecond <= 999999999);
        RTStrPrintf(szFraction, sizeof(szFraction), ".%09RU32", pTime->u32Nanosecond);
        szFraction[cFractionDigits + 1] = '\0';
    }

    /* (Default to UTC if not specified) */
    if (    (pTime->fFlags & RTTIME_FLAGS_TYPE_MASK) == RTTIME_FLAGS_TYPE_LOCAL
        &&  pTime->offUTC)
    {
        int32_t  offUTC = pTime->offUTC;
        Assert(offUTC <= 840 && offUTC >= -840);
        char     chSign;
        if (offUTC >= 0)
            chSign = '+';
        else
        {
            chSign = '-';
            offUTC = -offUTC;
        }
        uint32_t offUTCHour   = (uint32_t)offUTC / 60;
        uint32_t offUTCMinute = (uint32_t)offUTC % 60;

        /* Examples: 2018-09-07T16:12:00+02:00  2018-09-07T16:12:00.123456789+02:00 */
        cch = RTStrPrintf(psz, cb,
                          "%04RI32-%02u-%02uT%02u:%02u:%02u%s%c%02d%:02d",
                          pTime->i32Year, pTime->u8Month, pTime->u8MonthDay,
                          pTime->u8Hour, pTime->u8Minute, pTime->u8Second, szFraction,
                          chSign, offUTCHour, offUTCMinute);
        if (   cch >= 24
            && psz[cch - 6] == chSign)
            return cch;
    }
    else
    {
        /* Examples: 2018-09-07T16:12:00Z  2018-09-07T16:12:00.123456789Z */
        cch = RTStrPrintf(psz, cb, "%04RI32-%02u-%02uT%02u:%02u:%02u%sZ",
                          pTime->i32Year, pTime->u8Month, pTime->u8MonthDay,
                          pTime->u8Hour, pTime->u8Minute, pTime->u8Second, szFraction);
        if (   cch >= 19
            && psz[cch - 1] == 'Z')
            return cch;
    }
    return VERR_BUFFER_OVERFLOW;
}
RT_EXPORT_SYMBOL(RTTimeToStringEx);


/**
 * Converts a time spec to a ISO date string.
 *
 * @returns psz on success.
 * @returns NULL on buffer underflow.
 * @param   pTime       The time spec.
 * @param   psz         Where to store the string.
 * @param   cb          The size of the buffer.
 */
RTDECL(char *) RTTimeSpecToString(PCRTTIMESPEC pTime, char *psz, size_t cb)
{
    RTTIME Time;
    return RTTimeToString(RTTimeExplode(&Time, pTime), psz, cb);
}
RT_EXPORT_SYMBOL(RTTimeSpecToString);



/**
 * Attempts to convert an ISO date string to a time structure.
 *
 * We're a little forgiving with zero padding, unspecified parts, and leading
 * and trailing spaces.
 *
 * @retval  pTime on success,
 * @retval  NULL on failure.
 * @param   pTime       Where to store the time on success.
 * @param   pszString   The ISO date string to convert.
 */
RTDECL(PRTTIME) RTTimeFromString(PRTTIME pTime, const char *pszString)
{
    /* Ignore leading spaces. */
    while (RT_C_IS_SPACE(*pszString))
        pszString++;

    /*
     * Init non date & time parts.
     */
    pTime->fFlags = RTTIME_FLAGS_TYPE_LOCAL;
    pTime->offUTC = 0;

    /*
     * The date part.
     */

    /* Year */
    int rc = RTStrToInt32Ex(pszString, (char **)&pszString, 10, &pTime->i32Year);
    if (rc != VWRN_TRAILING_CHARS)
        return NULL;

    bool const fLeapYear = rtTimeIsLeapYear(pTime->i32Year);
    if (fLeapYear)
        pTime->fFlags |= RTTIME_FLAGS_LEAP_YEAR;

    if (*pszString++ != '-')
        return NULL;

    /* Month of the year. */
    rc = RTStrToUInt8Ex(pszString, (char **)&pszString, 10, &pTime->u8Month);
    if (rc != VWRN_TRAILING_CHARS)
        return NULL;
    if (pTime->u8Month == 0 || pTime->u8Month > 12)
        return NULL;
    if (*pszString++ != '-')
        return NULL;

    /* Day of month.*/
    rc = RTStrToUInt8Ex(pszString, (char **)&pszString, 10, &pTime->u8MonthDay);
    if (rc != VWRN_TRAILING_CHARS && rc != VINF_SUCCESS)
        return NULL;
    unsigned const cDaysInMonth = fLeapYear
                                ? g_acDaysInMonthsLeap[pTime->u8Month - 1]
                                : g_acDaysInMonths[pTime->u8Month - 1];
    if (pTime->u8MonthDay == 0 || pTime->u8MonthDay > cDaysInMonth)
        return NULL;

    /* Calculate year day. */
    pTime->u16YearDay = pTime->u8MonthDay - 1
                      + (fLeapYear
                         ? g_aiDayOfYearLeap[pTime->u8Month - 1]
                         : g_aiDayOfYear[pTime->u8Month - 1]);

    pTime->u8WeekDay = UINT8_MAX; /* later */

    /*
     * The time part.
     */
    if (*pszString++ != 'T')
        return NULL;

    /* Hour. */
    rc = RTStrToUInt8Ex(pszString, (char **)&pszString, 10, &pTime->u8Hour);
    if (rc != VWRN_TRAILING_CHARS)
        return NULL;
    if (pTime->u8Hour > 23)
        return NULL;
    if (*pszString++ != ':')
        return NULL;

    /* Minute. */
    rc = RTStrToUInt8Ex(pszString, (char **)&pszString, 10, &pTime->u8Minute);
    if (rc != VWRN_TRAILING_CHARS)
        return NULL;
    if (pTime->u8Minute > 59)
        return NULL;
    if (*pszString++ != ':')
        return NULL;

    /* Second. */
    rc = RTStrToUInt8Ex(pszString, (char **)&pszString, 10, &pTime->u8Second);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS && rc != VWRN_TRAILING_SPACES)
        return NULL;
    if (pTime->u8Second > 59)
        return NULL;

    /* We generally put a 9 digit fraction here, but it's entirely optional. */
    if (*pszString == '.')
    {
        const char * const pszStart = ++pszString;
        rc = RTStrToUInt32Ex(pszString, (char **)&pszString, 10, &pTime->u32Nanosecond);
        if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS && rc != VWRN_TRAILING_SPACES)
            return NULL;
        if (pTime->u32Nanosecond >= 1000000000)
            return NULL;
        switch (pszString - pszStart)
        {
            case 1: pTime->u32Nanosecond *= 100000000; break;
            case 2: pTime->u32Nanosecond *= 10000000; break;
            case 3: pTime->u32Nanosecond *= 1000000; break;
            case 4: pTime->u32Nanosecond *= 100000; break;
            case 5: pTime->u32Nanosecond *= 10000; break;
            case 6: pTime->u32Nanosecond *= 1000; break;
            case 7: pTime->u32Nanosecond *= 100; break;
            case 8: pTime->u32Nanosecond *= 10; break;
            case 9: break;
            default:
                return NULL;
        }
        if (pTime->u32Nanosecond >= 1000000000)
            return NULL;
    }
    else
        pTime->u32Nanosecond = 0;

    /*
     * Time zone.
     */
    if (*pszString == 'Z')
    {
        pszString++;
        pTime->fFlags &= ~RTTIME_FLAGS_TYPE_MASK;
        pTime->fFlags |= RTTIME_FLAGS_TYPE_UTC;
        pTime->offUTC = 0;
    }
    else if (   *pszString == '+'
             || *pszString == '-')
    {
        int8_t cUtcHours = 0;
        rc = RTStrToInt8Ex(pszString, (char **)&pszString, 10, &cUtcHours);
        if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS && rc != VWRN_TRAILING_SPACES)
            return NULL;
        uint8_t cUtcMin = 0;
        if (*pszString == ':')
        {
            rc = RTStrToUInt8Ex(pszString + 1, (char **)&pszString, 10, &cUtcMin);
            if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES)
                return NULL;
        }
        else if (*pszString && !RT_C_IS_BLANK(*pszString))
            return NULL;
        if (cUtcHours >= 0)
            pTime->offUTC = cUtcHours * 60 + cUtcMin;
        else
            pTime->offUTC = cUtcHours * 60 - cUtcMin;
        if (RT_ABS(pTime->offUTC) > 840)
            return NULL;
    }
    /* else: No time zone given, local with offUTC = 0. */

    /*
     * The rest of the string should be blanks.
     */
    char ch;
    while ((ch = *pszString++) != '\0')
        if (!RT_C_IS_BLANK(ch))
            return NULL;

    /* Calc week day. */
    rtTimeNormalizeInternal(pTime);
    return pTime;
}
RT_EXPORT_SYMBOL(RTTimeFromString);


/**
 * Attempts to convert an ISO date string to a time structure.
 *
 * We're a little forgiving with zero padding, unspecified parts, and leading
 * and trailing spaces.
 *
 * @retval  pTime on success,
 * @retval  NULL on failure.
 * @param   pTime       The time spec.
 * @param   pszString   The ISO date string to convert.
 */
RTDECL(PRTTIMESPEC) RTTimeSpecFromString(PRTTIMESPEC pTime, const char *pszString)
{
    RTTIME Time;
    if (RTTimeFromString(&Time, pszString))
        return RTTimeImplode(pTime, &Time);
    return NULL;
}
RT_EXPORT_SYMBOL(RTTimeSpecFromString);


/**
 * Formats the given time on a RTC-2822 compliant format.
 *
 * @returns Output string length on success (positive), VERR_BUFFER_OVERFLOW
 *          (negative) on failure.
 * @param   pTime       The time. Caller should've normalized this.
 * @param   psz         Where to store the string.
 * @param   cb          The size of the buffer.
 */
RTDECL(ssize_t) RTTimeToRfc2822(PRTTIME pTime, char *psz, size_t cb, uint32_t fFlags)
{
    Assert(pTime->u8Month > 0 && pTime->u8Month <= 12);
    Assert(pTime->u8WeekDay < 7);
    Assert(!(fFlags & ~RTTIME_RFC2822_F_GMT));

    /* (Default to UTC if not specified) */
    if (   (pTime->fFlags & RTTIME_FLAGS_TYPE_MASK) == RTTIME_FLAGS_TYPE_LOCAL
        && pTime->offUTC)
    {
        Assert(!(fFlags & RTTIME_RFC2822_F_GMT) /* don't call with local time. duh! */ );

        /* Calc the UTC offset part. */
        int32_t offUtc = pTime->offUTC;
        Assert(offUtc <= 840 && offUtc >= -840);
        char     chSign;
        if (offUtc >= 0)
            chSign = '+';
        else
        {
            chSign = '-';
            offUtc = -offUtc;
        }
        uint32_t offUtcHour   = (uint32_t)offUtc / 60;
        uint32_t offUtcMinute = (uint32_t)offUtc % 60;

        /* Example:                       "Mon, 31 Aug 2018 00:00:00 +0200" */
        size_t cch = RTStrPrintf(psz, cb, "%s, %u %s %04RI32 %02u:%02u:%02u %c%02u%02u", g_apszWeekDays[pTime->u8WeekDay],
                                 pTime->u8MonthDay, g_apszMonths[pTime->u8Month], pTime->i32Year,
                                 pTime->u8Hour, pTime->u8Minute, pTime->u8Second, chSign, offUtcHour, offUtcMinute);
        if (   cch >= 27
            && psz[cch - 5] == chSign)
            return cch;
    }
    else if (fFlags & RTTIME_RFC2822_F_GMT)
    {
        /* Example:                       "Mon, 1 Jan 1971 23:55:59 GMT" */
        size_t cch = RTStrPrintf(psz, cb, "%s, %u %s %04RI32 %02u:%02u:%02u GMT", g_apszWeekDays[pTime->u8WeekDay],
                                 pTime->u8MonthDay, g_apszMonths[pTime->u8Month], pTime->i32Year,
                                 pTime->u8Hour, pTime->u8Minute, pTime->u8Second);
        if (   cch >= 27
            && psz[cch - 1] == 'T')
            return cch;
    }
    else
    {
        /* Example:                       "Mon, 1 Jan 1971 00:00:00 -0000" */
        size_t cch = RTStrPrintf(psz, cb, "%s, %u %s %04RI32 %02u:%02u:%02u -0000", g_apszWeekDays[pTime->u8WeekDay],
                                 pTime->u8MonthDay, g_apszMonths[pTime->u8Month], pTime->i32Year,
                                 pTime->u8Hour, pTime->u8Minute, pTime->u8Second);
        if (   cch >= 27
            && psz[cch - 5] == '-')
            return cch;
    }
    return VERR_BUFFER_OVERFLOW;
}
RT_EXPORT_SYMBOL(RTTimeToRfc2822);


/**
 * Attempts to convert an RFC-2822 date string to a time structure.
 *
 * We're a little forgiving with zero padding, unspecified parts, and leading
 * and trailing spaces.
 *
 * @retval  pTime on success,
 * @retval  NULL on failure.
 * @param   pTime       Where to store the time on success.
 * @param   pszString   The ISO date string to convert.
 */
RTDECL(PRTTIME) RTTimeFromRfc2822(PRTTIME pTime, const char *pszString)
{
    /*
     * Fri, 31 Aug 2018 00:00:00 +0200
     * Mon, 3 Sep 2018 00:00:00 GMT
     * Mon, 3 Sep 2018 00:00:00 -0000
     * 3 Sep 2018 00:00:00 -0000 (?)
     * 3 Sep 2018 00:00:00 GMT   (?)
     *
     */

    /* Ignore leading spaces. */
    while (RT_C_IS_SPACE(*pszString))
        pszString++;

    /*
     * Init non date & time parts.
     */
    pTime->fFlags = RTTIME_FLAGS_TYPE_LOCAL;
    pTime->offUTC = 0;

    /*
     * The date part.
     */

    /* Optional day of week: */
    if (RT_C_IS_ALPHA(pszString[0]) && pszString[1] != '\0')
    {
        uint32_t uWeekDay = RT_MAKE_U32_FROM_U8(RT_C_TO_LOWER(pszString[0]), RT_C_TO_LOWER(pszString[1]),
                                                RT_C_TO_LOWER(pszString[2]), 0);
        if (     uWeekDay == RT_MAKE_U32_FROM_U8('m', 'o', 'n', 0))     pTime->u8WeekDay = 0;
        else if (uWeekDay == RT_MAKE_U32_FROM_U8('t', 'u', 'e', 0))     pTime->u8WeekDay = 1;
        else if (uWeekDay == RT_MAKE_U32_FROM_U8('w', 'e', 'd', 0))     pTime->u8WeekDay = 2;
        else if (uWeekDay == RT_MAKE_U32_FROM_U8('t', 'h', 'u', 0))     pTime->u8WeekDay = 3;
        else if (uWeekDay == RT_MAKE_U32_FROM_U8('f', 'r', 'i', 0))     pTime->u8WeekDay = 4;
        else if (uWeekDay == RT_MAKE_U32_FROM_U8('s', 'a', 't', 0))     pTime->u8WeekDay = 5;
        else if (uWeekDay == RT_MAKE_U32_FROM_U8('s', 'u', 'n', 0))     pTime->u8WeekDay = 6;
        else
            return NULL;
        pszString += 3;
        while (RT_C_IS_ALPHA(*pszString))
            pszString++;
        if (*pszString == ',')
            pszString++;
        while (RT_C_IS_SPACE(*pszString))
            pszString++;
        if (!RT_C_IS_DIGIT(pszString[0]))
            return NULL;
    }
    else if (RT_C_IS_DIGIT(pszString[0]))
        pTime->u8WeekDay = UINT8_MAX;
    else
        return NULL;

    /* Day of month.*/
    int rc = RTStrToUInt8Ex(pszString, (char **)&pszString, 10, &pTime->u8MonthDay);
    if (rc != VWRN_TRAILING_CHARS && rc != VINF_SUCCESS)
        return NULL;
    while (RT_C_IS_SPACE(*pszString))
        pszString++;

    /* Month of the year. */
    if (pszString[0] == '\0' || pszString[1] == '\0' || pszString[2] == '\0')
        return NULL;
    uint32_t uMonth = RT_MAKE_U32_FROM_U8(RT_C_TO_LOWER(pszString[0]), RT_C_TO_LOWER(pszString[1]),
                                          RT_C_TO_LOWER(pszString[2]), 0);
    if (     uMonth == RT_MAKE_U32_FROM_U8('j', 'a', 'n', 0))     pTime->u8Month = 1;
    else if (uMonth == RT_MAKE_U32_FROM_U8('f', 'e', 'b', 0))     pTime->u8Month = 2;
    else if (uMonth == RT_MAKE_U32_FROM_U8('m', 'a', 'r', 0))     pTime->u8Month = 3;
    else if (uMonth == RT_MAKE_U32_FROM_U8('a', 'p', 'r', 0))     pTime->u8Month = 4;
    else if (uMonth == RT_MAKE_U32_FROM_U8('m', 'a', 'y', 0))     pTime->u8Month = 5;
    else if (uMonth == RT_MAKE_U32_FROM_U8('j', 'u', 'n', 0))     pTime->u8Month = 6;
    else if (uMonth == RT_MAKE_U32_FROM_U8('j', 'u', 'l', 0))     pTime->u8Month = 7;
    else if (uMonth == RT_MAKE_U32_FROM_U8('a', 'u', 'g', 0))     pTime->u8Month = 8;
    else if (uMonth == RT_MAKE_U32_FROM_U8('s', 'e', 'p', 0))     pTime->u8Month = 9;
    else if (uMonth == RT_MAKE_U32_FROM_U8('o', 'c', 't', 0))     pTime->u8Month = 10;
    else if (uMonth == RT_MAKE_U32_FROM_U8('n', 'o', 'v', 0))     pTime->u8Month = 11;
    else if (uMonth == RT_MAKE_U32_FROM_U8('d', 'e', 'c', 0))     pTime->u8Month = 12;
    else
        return NULL;
    pszString += 3;
    while (RT_C_IS_ALPHA(*pszString))
        pszString++;
    while (RT_C_IS_SPACE(*pszString))
        pszString++;

    /* Year */
    const char * const pszStartYear = pszString;
    rc = RTStrToInt32Ex(pszString, (char **)&pszString, 10, &pTime->i32Year);
    if (rc != VWRN_TRAILING_CHARS)
        return NULL;
    if (pszString - pszStartYear >= 4 )
    { /* likely */ }
    else if (pszString - pszStartYear == 3)
        pTime->i32Year += 1900;
    else if (pszString - pszStartYear == 2)
        pTime->i32Year += pTime->i32Year >= 50 ? 1900 : 2000;
    else
        return NULL;

    bool const fLeapYear = rtTimeIsLeapYear(pTime->i32Year);
    if (fLeapYear)
        pTime->fFlags |= RTTIME_FLAGS_LEAP_YEAR;

    while (RT_C_IS_SPACE(*pszString))
        pszString++;


    /* Calculate year day. */
    unsigned const cDaysInMonth = fLeapYear
                                ? g_acDaysInMonthsLeap[pTime->u8Month - 1]
                                : g_acDaysInMonths[pTime->u8Month - 1];
    if (pTime->u8MonthDay == 0 || pTime->u8MonthDay > cDaysInMonth)
        return NULL;

    pTime->u16YearDay = pTime->u8MonthDay - 1
                      + (fLeapYear
                         ? g_aiDayOfYearLeap[pTime->u8Month - 1]
                         : g_aiDayOfYear[pTime->u8Month - 1]);

    /*
     * The time part.
     */
    /* Hour. */
    rc = RTStrToUInt8Ex(pszString, (char **)&pszString, 10, &pTime->u8Hour);
    if (rc != VWRN_TRAILING_CHARS)
        return NULL;
    if (pTime->u8Hour > 23)
        return NULL;
    if (*pszString++ != ':')
        return NULL;

    /* Minute. */
    rc = RTStrToUInt8Ex(pszString, (char **)&pszString, 10, &pTime->u8Minute);
    if (rc != VWRN_TRAILING_CHARS)
        return NULL;
    if (pTime->u8Minute > 59)
        return NULL;
    if (*pszString++ != ':')
        return NULL;

    /* Second. */
    rc = RTStrToUInt8Ex(pszString, (char **)&pszString, 10, &pTime->u8Second);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS && rc != VWRN_TRAILING_SPACES)
        return NULL;
    if (pTime->u8Second > 59)
        return NULL;

    /* Non-standard fraction.  Handy for testing, though. */
    if (*pszString == '.')
    {
        const char * const pszStart = ++pszString;
        rc = RTStrToUInt32Ex(pszString, (char **)&pszString, 10, &pTime->u32Nanosecond);
        if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS && rc != VWRN_TRAILING_SPACES)
            return NULL;
        if (pTime->u32Nanosecond >= 1000000000)
            return NULL;
        switch (pszString - pszStart)
        {
            case 1: pTime->u32Nanosecond *= 100000000; break;
            case 2: pTime->u32Nanosecond *= 10000000; break;
            case 3: pTime->u32Nanosecond *= 1000000; break;
            case 4: pTime->u32Nanosecond *= 100000; break;
            case 5: pTime->u32Nanosecond *= 10000; break;
            case 6: pTime->u32Nanosecond *= 1000; break;
            case 7: pTime->u32Nanosecond *= 100; break;
            case 8: pTime->u32Nanosecond *= 10; break;
            case 9: break;
            default:
                return NULL;
        }
        if (pTime->u32Nanosecond >= 1000000000)
            return NULL;
    }
    else
        pTime->u32Nanosecond = 0;
    while (RT_C_IS_SPACE(*pszString))
        pszString++;

    /*
     * Time zone.
     */
    if (   *pszString == '+'
        || *pszString == '-')
    {
        if (   !RT_C_IS_DIGIT(pszString[1])
            || !RT_C_IS_DIGIT(pszString[2]))
            return NULL;
        int8_t cUtcHours = (pszString[1] - '0') * 10 + (pszString[2] - '0');
        char   chSign    = *pszString;
        if (chSign == '-')
            cUtcHours = -cUtcHours;
        pszString += 3;

        uint8_t cUtcMin = 0;
        if (RT_C_IS_DIGIT(pszString[0]))
        {
            rc = RTStrToUInt8Ex(pszString, (char **)&pszString, 10, &cUtcMin);
            if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES)
                return NULL;
        }
        else if (*pszString && !RT_C_IS_BLANK(*pszString))
            return NULL;
        if (cUtcHours >= 0)
            pTime->offUTC = cUtcHours * 60 + cUtcMin;
        else
            pTime->offUTC = cUtcHours * 60 - cUtcMin;
        if (RT_ABS(pTime->offUTC) > 840)
            return NULL;

        /* -0000: GMT isn't necessarily the local time zone, so change flags from local to UTC. */
        if (pTime->offUTC == 0 && chSign == '-')
        {
            pTime->fFlags &= ~RTTIME_FLAGS_TYPE_MASK;
            pTime->fFlags |= RTTIME_FLAGS_TYPE_UTC;
        }
    }
    else if (RT_C_IS_ALPHA(*pszString))
    {
        uint32_t uTimeZone = RT_MAKE_U32_FROM_U8(RT_C_TO_LOWER(pszString[0]), RT_C_TO_LOWER(pszString[1]),
                                                 RT_C_TO_LOWER(pszString[2]), 0);
        if (uTimeZone == RT_MAKE_U32_FROM_U8('g', 'm', 't', 0))
        {
            pTime->fFlags &= ~RTTIME_FLAGS_TYPE_MASK;
            pTime->fFlags |= RTTIME_FLAGS_TYPE_UTC;
            pTime->offUTC = 0;
            pszString += 3;
        }
        else if ((uint16_t)uTimeZone == RT_MAKE_U16('u', 't'))
        {
            pTime->fFlags &= ~RTTIME_FLAGS_TYPE_MASK;
            pTime->fFlags |= RTTIME_FLAGS_TYPE_UTC;
            pTime->offUTC = 0;
            pszString += 2;
        }
        else
        {
            static const struct { uint32_t uTimeZone; int32_t offUtc; } s_aLegacyTimeZones[] =
            {
                { RT_MAKE_U32_FROM_U8('e', 'd', 't', 0), -4*60 },
                { RT_MAKE_U32_FROM_U8('e', 's', 't', 0), -5*60 },
                { RT_MAKE_U32_FROM_U8('c', 'd', 't', 0), -5*60 },
                { RT_MAKE_U32_FROM_U8('c', 's', 't', 0), -6*60 },
                { RT_MAKE_U32_FROM_U8('m', 'd', 't', 0), -6*60 },
                { RT_MAKE_U32_FROM_U8('m', 's', 't', 0), -7*60 },
                { RT_MAKE_U32_FROM_U8('p', 'd', 't', 0), -7*60 },
                { RT_MAKE_U32_FROM_U8('p', 's', 't', 0), -8*60 },
            };
            size_t i = RT_ELEMENTS(s_aLegacyTimeZones);
            while (i-- > 0)
                if (s_aLegacyTimeZones[i].uTimeZone == uTimeZone)
                {
                    pTime->fFlags &= ~RTTIME_FLAGS_TYPE_MASK;
                    pTime->fFlags |= RTTIME_FLAGS_TYPE_LOCAL;
                    pTime->offUTC = s_aLegacyTimeZones[i].offUtc;
                    pszString += 3;
                    break;
                }
        }

    }
    /* else: No time zone given, local with offUTC = 0. */

    /*
     * The rest of the string should be blanks.
     */
    char ch;
    while ((ch = *pszString++) != '\0')
        if (!RT_C_IS_BLANK(ch))
            return NULL;

    rtTimeNormalizeInternal(pTime);
    return pTime;
}
RT_EXPORT_SYMBOL(RTTimeFromRfc2822);


/**
 * Adds one day to @a pTime.
 *
 * ASSUMES it is zulu time so DST can be ignored.
 */
static PRTTIME rtTimeAdd1Day(PRTTIME pTime)
{
    Assert(!pTime->offUTC);
    rtTimeNormalizeInternal(pTime);
    pTime->u8MonthDay += 1;
    pTime->u16YearDay = 0;
    return rtTimeNormalizeInternal(pTime);
}


/**
 * Subtracts one day from @a pTime.
 *
 * ASSUMES it is zulu time so DST can be ignored.
 */
static PRTTIME rtTimeSub1Day(PRTTIME pTime)
{
    Assert(!pTime->offUTC);
    rtTimeNormalizeInternal(pTime);
    if (pTime->u16YearDay > 1)
    {
        pTime->u16YearDay -= 1;
        pTime->u8Month     = 0;
        pTime->u8MonthDay  = 0;
    }
    else
    {
        pTime->i32Year    -= 1;
        pTime->u16YearDay  = rtTimeIsLeapYear(pTime->i32Year) ? 366 : 365;
        pTime->u8MonthDay  = 31;
        pTime->u8Month     = 12;
        pTime->fFlags     &= ~(RTTIME_FLAGS_COMMON_YEAR | RTTIME_FLAGS_LEAP_YEAR);
    }
    return rtTimeNormalizeInternal(pTime);
}


/**
 * Adds a signed number of minutes to @a pTime.
 *
 * ASSUMES it is zulu time so DST can be ignored.
 *
 * @param   pTime       The time structure to work on.
 * @param   cAddend     Number of minutes to add.
 *                      ASSUMES the value isn't all that high!
 */
static PRTTIME rtTimeAddMinutes(PRTTIME pTime, int32_t cAddend)
{
    Assert(RT_ABS(cAddend) < 31 * 24 * 60);

    /*
     * Work on minutes of the day.
     */
    int32_t const   cMinutesInDay = 24 * 60;
    int32_t         iDayMinute    = (unsigned)pTime->u8Hour * 60 + pTime->u8Minute;
    iDayMinute += cAddend;

    while (iDayMinute >= cMinutesInDay)
    {
        rtTimeAdd1Day(pTime);
        iDayMinute -= cMinutesInDay;
    }

    while (iDayMinute < 0)
    {
        rtTimeSub1Day(pTime);
        iDayMinute += cMinutesInDay;
    }

    pTime->u8Hour   = iDayMinute / 60;
    pTime->u8Minute = iDayMinute % 60;

    return pTime;
}


/**
 * Converts @a pTime to zulu time (UTC) if needed.
 *
 * @returns pTime.
 * @param   pTime       What to convert (in/out).
 */
static PRTTIME rtTimeConvertToZulu(PRTTIME pTime)
{
    RTTIME_ASSERT_NORMALIZED(pTime);
    if ((pTime->fFlags & RTTIME_FLAGS_TYPE_MASK) != RTTIME_FLAGS_TYPE_UTC)
    {
        int32_t offUTC = pTime->offUTC;
        pTime->offUTC  = 0;
        pTime->fFlags &= ~RTTIME_FLAGS_TYPE_MASK;
        pTime->fFlags |= RTTIME_FLAGS_TYPE_UTC;
        if (offUTC != 0)
            rtTimeAddMinutes(pTime, -offUTC);
    }
    return pTime;
}


/**
 * Converts a time structure to UTC, relying on UTC offset information if it contains local time.
 *
 * @returns pTime on success.
 * @returns NULL if the data is invalid.
 * @param   pTime       The time structure to convert.
 */
RTDECL(PRTTIME) RTTimeConvertToZulu(PRTTIME pTime)
{
    /*
     * Validate that we've got the minimum of stuff handy.
     */
    AssertReturn(VALID_PTR(pTime), NULL);
    AssertMsgReturn(!(pTime->fFlags & ~RTTIME_FLAGS_MASK), ("%#x\n", pTime->fFlags), NULL);

    return rtTimeConvertToZulu(rtTimeNormalizeInternal(pTime));
}
RT_EXPORT_SYMBOL(RTTimeConvertToZulu);


/**
 * Compares two normalized time structures.
 *
 * @retval  0 if equal.
 * @retval  -1 if @a pLeft is earlier than @a pRight.
 * @retval  1 if @a pRight is earlier than @a pLeft.
 *
 * @param   pLeft       The left side time.  NULL is accepted.
 * @param   pRight      The right side time.  NULL is accepted.
 *
 * @note    A NULL time is considered smaller than anything else.  If both are
 *          NULL, they are considered equal.
 */
RTDECL(int) RTTimeCompare(PCRTTIME pLeft, PCRTTIME pRight)
{
#ifdef RT_STRICT
    if (pLeft)
        RTTIME_ASSERT_NORMALIZED(pLeft);
    if (pRight)
        RTTIME_ASSERT_NORMALIZED(pRight);
#endif

    int iRet;
    if (pLeft)
    {
        if (pRight)
        {
            /*
             * Only work with normalized zulu time.
             */
            RTTIME TmpLeft;
            if (   pLeft->offUTC     != 0
                || pLeft->u16YearDay == 0
                || pLeft->u16YearDay >  366
                || pLeft->u8Hour     >= 60
                || pLeft->u8Minute   >= 60
                || pLeft->u8Second   >= 60)
            {
                TmpLeft = *pLeft;
                pLeft = rtTimeConvertToZulu(rtTimeNormalizeInternal(&TmpLeft));
            }

            RTTIME TmpRight;
            if (   pRight->offUTC     != 0
                || pRight->u16YearDay == 0
                || pRight->u16YearDay >  366
                || pRight->u8Hour     >= 60
                || pRight->u8Minute   >= 60
                || pRight->u8Second   >= 60)
            {
                TmpRight = *pRight;
                pRight = rtTimeConvertToZulu(rtTimeNormalizeInternal(&TmpRight));
            }

            /*
             * Do the comparison.
             */
            if (       pLeft->i32Year       != pRight->i32Year)
                iRet = pLeft->i32Year       <  pRight->i32Year       ? -1 : 1;
            else if (  pLeft->u16YearDay    != pRight->u16YearDay)
                iRet = pLeft->u16YearDay    <  pRight->u16YearDay    ? -1 : 1;
            else if (  pLeft->u8Hour        != pRight->u8Hour)
                iRet = pLeft->u8Hour        <  pRight->u8Hour        ? -1 : 1;
            else if (  pLeft->u8Minute      != pRight->u8Minute)
                iRet = pLeft->u8Minute      <  pRight->u8Minute      ? -1 : 1;
            else if (  pLeft->u8Second      != pRight->u8Second)
                iRet = pLeft->u8Second      <  pRight->u8Second      ? -1 : 1;
            else if (  pLeft->u32Nanosecond != pRight->u32Nanosecond)
                iRet = pLeft->u32Nanosecond <  pRight->u32Nanosecond ? -1 : 1;
            else
                iRet = 0;
        }
        else
            iRet = 1;
    }
    else
        iRet = pRight ? -1 : 0;
    return iRet;
}
RT_EXPORT_SYMBOL(RTTimeCompare);

