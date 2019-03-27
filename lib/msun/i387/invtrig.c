/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 David Schultz <das@FreeBSD.ORG>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdint.h>

#define STRUCT_DECLS
#include "invtrig.h"

/*
 * asinl() and acosl()
 */
const LONGDOUBLE
pS0 = { 0xaaaaaaaaaaaaaaa8ULL, 0x3ffcU }, /*  1.66666666666666666631e-01L */
pS1 = { 0xd5271b6699b48bfaULL, 0xbffdU }, /* -4.16313987993683104320e-01L */
pS2 = { 0xbcf67ca9e9f669cfULL, 0x3ffdU }, /*  3.69068046323246813704e-01L */
pS3 = { 0x8b7baa3d15f9830dULL, 0xbffcU }, /* -1.36213932016738603108e-01L */
pS4 = { 0x92154b093a3bff1cULL, 0x3ff9U }, /*  1.78324189708471965733e-02L */
pS5 = { 0xe5dd76401964508cULL, 0xbff2U }, /* -2.19216428382605211588e-04L */
pS6 = { 0xee69c5b0fdb76951ULL, 0xbfedU }, /* -7.10526623669075243183e-06L */
qS1 = { 0xbcaa2159c01436a0ULL, 0xc000U }, /* -2.94788392796209867269e+00L */
qS2 = { 0xd17a73d1e1564c29ULL, 0x4000U }, /*  3.27309890266528636716e+00L */
qS3 = { 0xd767e411c9cf4c2cULL, 0xbfffU }, /* -1.68285799854822427013e+00L */
qS4 = { 0xc809c0dfb9b0d0b7ULL, 0x3ffdU }, /*  3.90699412641738801874e-01L */
qS5 = { 0x80c3a2197c8ced57ULL, 0xbffaU }; /* -3.14365703596053263322e-02L */

/*
 * atanl()
 */
const LONGDOUBLE atanhi[] = {
	{ 0xed63382b0dda7b45ULL, 0x3ffdU }, /*  4.63647609000806116202e-01L */
	{ 0xc90fdaa22168c235ULL, 0x3ffeU }, /*  7.85398163397448309628e-01L */
	{ 0xfb985e940fb4d900ULL, 0x3ffeU }, /*  9.82793723247329067960e-01L */
	{ 0xc90fdaa22168c235ULL, 0x3fffU }, /*  1.57079632679489661926e+00L */
};

const LONGDOUBLE atanlo[] = {
	{ 0xdfc88bd978751a07ULL, 0x3fbcU }, /*  1.18469937025062860669e-20L */
	{ 0xece675d1fc8f8cbbULL, 0xbfbcU }, /* -1.25413940316708300586e-20L */
	{ 0xf10f5e197793c283ULL, 0x3fbdU }, /*  2.55232234165405176172e-20L */
	{ 0xece675d1fc8f8cbbULL, 0xbfbdU }, /* -2.50827880633416601173e-20L */
};

const LONGDOUBLE aT[] = {
	{ 0xaaaaaaaaaaaaaa9fULL, 0x3ffdU }, /*  3.33333333333333333017e-01L */
	{ 0xcccccccccccc62bcULL, 0xbffcU }, /* -1.99999999999999632011e-01L */
	{ 0x9249249248b81e3fULL, 0x3ffcU }, /*  1.42857142857046531280e-01L */
	{ 0xe38e38e3316f3de5ULL, 0xbffbU }, /* -1.11111111100562372733e-01L */
	{ 0xba2e8b8dc280726aULL, 0x3ffbU }, /*  9.09090902935647302252e-02L */
	{ 0x9d89d5b4c6847ec4ULL, 0xbffbU }, /* -7.69230552476207730353e-02L */
	{ 0x8888461d3099c677ULL, 0x3ffbU }, /*  6.66661718042406260546e-02L */
	{ 0xf0e8ee0f5328dc29ULL, 0xbffaU }, /* -5.88158892835030888692e-02L */
	{ 0xd73ea84d24bae54aULL, 0x3ffaU }, /*  5.25499891539726639379e-02L */
	{ 0xc08fa381dcd9213aULL, 0xbffaU }, /* -4.70119845393155721494e-02L */
	{ 0xa54a26f4095f2a3aULL, 0x3ffaU }, /*  4.03539201366454414072e-02L */
	{ 0xeea2d8d059ef3ad6ULL, 0xbff9U }, /* -2.91303858419364158725e-02L */
	{ 0xcc82292ab894b051ULL, 0x3ff8U }, /*  1.24822046299269234080e-02L */
};

const LONGDOUBLE
pi_lo = { 0xece675d1fc8f8cbbULL, 0xbfbeU }; /* -5.01655761266833202345e-20L */
