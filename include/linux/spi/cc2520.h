/* Header file for cc2520 radio driver
 *
 * Copyright (C) 2014 Varka Bhadram <varkab@cdac.in>
 *                    Md.Jamal Mohiuddin <mjmohiuddin@cdac.in>
 *                    P Sowjanya <sowjanyap@cdac.in>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
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
	bool amplified;
};

#endif
