/*
 * Copyright (C) 2016 Chen-Yu Tsai <wens@csie.org>
 *
 * This file is dual-licensed: you can use it either under the terms
 * of the GPL or the X11 license, at your option. Note that this dual
 * licensing only applies to this file, and not this project as a
 * whole.
 *
 *  a) This file is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of the
 *     License, or (at your option) any later version.
 *
 *     This file is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 * Or, alternatively,
 *
 *  b) Permission is hereby granted, free of charge, to any person
 *     obtaining a copy of this software and associated documentation
 *     files (the "Software"), to deal in the Software without
 *     restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or
 *     sell copies of the Software, and to permit persons to whom the
 *     Software is furnished to do so, subject to the following
 *     conditions:
 *
 *     The above copyright notice and this permission notice shall be
 *     included in all copies or substantial portions of the Software.
 *
 *     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *     EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *     OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *     NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *     HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *     WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *     FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *     OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _DT_BINDINGS_CLOCK_SUN9I_A80_DE_H_
#define _DT_BINDINGS_CLOCK_SUN9I_A80_DE_H_

#define CLK_FE0			0
#define CLK_FE1			1
#define CLK_FE2			2
#define CLK_IEP_DEU0		3
#define CLK_IEP_DEU1		4
#define CLK_BE0			5
#define CLK_BE1			6
#define CLK_BE2			7
#define CLK_IEP_DRC0		8
#define CLK_IEP_DRC1		9
#define CLK_MERGE		10

#define CLK_DRAM_FE0		11
#define CLK_DRAM_FE1		12
#define CLK_DRAM_FE2		13
#define CLK_DRAM_DEU0		14
#define CLK_DRAM_DEU1		15
#define CLK_DRAM_BE0		16
#define CLK_DRAM_BE1		17
#define CLK_DRAM_BE2		18
#define CLK_DRAM_DRC0		19
#define CLK_DRAM_DRC1		20

#define CLK_BUS_FE0		21
#define CLK_BUS_FE1		22
#define CLK_BUS_FE2		23
#define CLK_BUS_DEU0		24
#define CLK_BUS_DEU1		25
#define CLK_BUS_BE0		26
#define CLK_BUS_BE1		27
#define CLK_BUS_BE2		28
#define CLK_BUS_DRC0		29
#define CLK_BUS_DRC1		30

#endif /* _DT_BINDINGS_CLOCK_SUN9I_A80_DE_H_ */
