/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Header file for cc2520 radio driver
 *
 * Copyright (C) 2014 Varka Bhadram <varkab@cdac.in>
 *                    Md.Jamal Mohiuddin <mjmohiuddin@cdac.in>
 *                    P Sowjanya <sowjanyap@cdac.in>
 */

#ifndef __CC2520_H
#define __CC2520_H

struct cc2520_platform_data {
	int fifo;
	int fifop;
	int cca;
	int sfd;
	int reset;
	int vreg;
};

#endif
