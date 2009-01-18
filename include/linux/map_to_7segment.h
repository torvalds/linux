/*
 * Copyright (c) 2005 Henk Vergonet <Henk.Vergonet@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef MAP_TO_7SEGMENT_H
#define MAP_TO_7SEGMENT_H

/* This file provides translation primitives and tables for the conversion
 * of (ASCII) characters to a 7-segments notation.
 *
 * The 7 segment's wikipedia notation below is used as standard.
 * See: http://en.wikipedia.org/wiki/Seven_segment_display
 *
 * Notation:	+-a-+
 *		f   b
 *		+-g-+
 *		e   c
 *		+-d-+
 *
 * Usage:
 *
 *   Register a map variable, and fill it with a character set:
 *	static SEG7_DEFAULT_MAP(map_seg7);
 *
 *
 *   Then use for conversion:
 *	seg7 = map_to_seg7(&map_seg7, some_char);
 *	...
 *
 * In device drivers it is recommended, if required, to make the char map
 * accessible via the sysfs interface using the following scheme:
 *
 * static ssize_t show_map(struct device *dev, char *buf) {
 *	memcpy(buf, &map_seg7, sizeof(map_seg7));
 *	return sizeof(map_seg7);
 * }
 * static ssize_t store_map(struct device *dev, const char *buf, size_t cnt) {
 *	if(cnt != sizeof(map_seg7))
 *		return -EINVAL;
 *	memcpy(&map_seg7, buf, cnt);
 *	return cnt;
 * }
 * static DEVICE_ATTR(map_seg7, PERMS_RW, show_map, store_map);
 *
 * History:
 * 2005-05-31	RFC linux-kernel@vger.kernel.org
 */
#include <linux/errno.h>


#define BIT_SEG7_A		0
#define BIT_SEG7_B		1
#define BIT_SEG7_C		2
#define BIT_SEG7_D		3
#define BIT_SEG7_E		4
#define BIT_SEG7_F		5
#define BIT_SEG7_G		6
#define BIT_SEG7_RESERVED	7

struct seg7_conversion_map {
	unsigned char	table[128];
};

static __inline__ int map_to_seg7(struct seg7_conversion_map *map, int c)
{
	return c >= 0 && c < sizeof(map->table) ? map->table[c] : -EINVAL;
}

#define SEG7_CONVERSION_MAP(_name, _map)	\
	struct seg7_conversion_map _name = { .table = { _map } }

/*
 * It is recommended to use a facility that allows user space to redefine
 * custom character sets for LCD devices. Please use a sysfs interface
 * as described above.
 */
#define MAP_TO_SEG7_SYSFS_FILE	"map_seg7"

/*******************************************************************************
 * ASCII conversion table
 ******************************************************************************/

#define _SEG7(l,a,b,c,d,e,f,g)	\
      (	a<<BIT_SEG7_A |	b<<BIT_SEG7_B |	c<<BIT_SEG7_C |	d<<BIT_SEG7_D |	\
	e<<BIT_SEG7_E |	f<<BIT_SEG7_F |	g<<BIT_SEG7_G )

#define _MAP_0_32_ASCII_SEG7_NON_PRINTABLE	\
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,

#define _MAP_33_47_ASCII_SEG7_SYMBOL		\
 _SEG7('!',0,0,0,0,1,1,0), _SEG7('"',0,1,0,0,0,1,0), _SEG7('#',0,1,1,0,1,1,0),\
 _SEG7('$',1,0,1,1,0,1,1), _SEG7('%',0,0,1,0,0,1,0), _SEG7('&',1,0,1,1,1,1,1),\
 _SEG7('\'',0,0,0,0,0,1,0),_SEG7('(',1,0,0,1,1,1,0), _SEG7(')',1,1,1,1,0,0,0),\
 _SEG7('*',0,1,1,0,1,1,1), _SEG7('+',0,1,1,0,0,0,1), _SEG7(',',0,0,0,0,1,0,0),\
 _SEG7('-',0,0,0,0,0,0,1), _SEG7('.',0,0,0,0,1,0,0), _SEG7('/',0,1,0,0,1,0,1),

#define _MAP_48_57_ASCII_SEG7_NUMERIC		\
 _SEG7('0',1,1,1,1,1,1,0), _SEG7('1',0,1,1,0,0,0,0), _SEG7('2',1,1,0,1,1,0,1),\
 _SEG7('3',1,1,1,1,0,0,1), _SEG7('4',0,1,1,0,0,1,1), _SEG7('5',1,0,1,1,0,1,1),\
 _SEG7('6',1,0,1,1,1,1,1), _SEG7('7',1,1,1,0,0,0,0), _SEG7('8',1,1,1,1,1,1,1),\
 _SEG7('9',1,1,1,1,0,1,1),

#define _MAP_58_64_ASCII_SEG7_SYMBOL		\
 _SEG7(':',0,0,0,1,0,0,1), _SEG7(';',0,0,0,1,0,0,1), _SEG7('<',1,0,0,0,0,1,1),\
 _SEG7('=',0,0,0,1,0,0,1), _SEG7('>',1,1,0,0,0,0,1), _SEG7('?',1,1,1,0,0,1,0),\
 _SEG7('@',1,1,0,1,1,1,1),

#define _MAP_65_90_ASCII_SEG7_ALPHA_UPPR	\
 _SEG7('A',1,1,1,0,1,1,1), _SEG7('B',1,1,1,1,1,1,1), _SEG7('C',1,0,0,1,1,1,0),\
 _SEG7('D',1,1,1,1,1,1,0), _SEG7('E',1,0,0,1,1,1,1), _SEG7('F',1,0,0,0,1,1,1),\
 _SEG7('G',1,1,1,1,0,1,1), _SEG7('H',0,1,1,0,1,1,1), _SEG7('I',0,1,1,0,0,0,0),\
 _SEG7('J',0,1,1,1,0,0,0), _SEG7('K',0,1,1,0,1,1,1), _SEG7('L',0,0,0,1,1,1,0),\
 _SEG7('M',1,1,1,0,1,1,0), _SEG7('N',1,1,1,0,1,1,0), _SEG7('O',1,1,1,1,1,1,0),\
 _SEG7('P',1,1,0,0,1,1,1), _SEG7('Q',1,1,1,1,1,1,0), _SEG7('R',1,1,1,0,1,1,1),\
 _SEG7('S',1,0,1,1,0,1,1), _SEG7('T',0,0,0,1,1,1,1), _SEG7('U',0,1,1,1,1,1,0),\
 _SEG7('V',0,1,1,1,1,1,0), _SEG7('W',0,1,1,1,1,1,1), _SEG7('X',0,1,1,0,1,1,1),\
 _SEG7('Y',0,1,1,0,0,1,1), _SEG7('Z',1,1,0,1,1,0,1),

#define _MAP_91_96_ASCII_SEG7_SYMBOL		\
 _SEG7('[',1,0,0,1,1,1,0), _SEG7('\\',0,0,1,0,0,1,1),_SEG7(']',1,1,1,1,0,0,0),\
 _SEG7('^',1,1,0,0,0,1,0), _SEG7('_',0,0,0,1,0,0,0), _SEG7('`',0,1,0,0,0,0,0),

#define _MAP_97_122_ASCII_SEG7_ALPHA_LOWER	\
 _SEG7('A',1,1,1,0,1,1,1), _SEG7('b',0,0,1,1,1,1,1), _SEG7('c',0,0,0,1,1,0,1),\
 _SEG7('d',0,1,1,1,1,0,1), _SEG7('E',1,0,0,1,1,1,1), _SEG7('F',1,0,0,0,1,1,1),\
 _SEG7('G',1,1,1,1,0,1,1), _SEG7('h',0,0,1,0,1,1,1), _SEG7('i',0,0,1,0,0,0,0),\
 _SEG7('j',0,0,1,1,0,0,0), _SEG7('k',0,0,1,0,1,1,1), _SEG7('L',0,0,0,1,1,1,0),\
 _SEG7('M',1,1,1,0,1,1,0), _SEG7('n',0,0,1,0,1,0,1), _SEG7('o',0,0,1,1,1,0,1),\
 _SEG7('P',1,1,0,0,1,1,1), _SEG7('q',1,1,1,0,0,1,1), _SEG7('r',0,0,0,0,1,0,1),\
 _SEG7('S',1,0,1,1,0,1,1), _SEG7('T',0,0,0,1,1,1,1), _SEG7('u',0,0,1,1,1,0,0),\
 _SEG7('v',0,0,1,1,1,0,0), _SEG7('W',0,1,1,1,1,1,1), _SEG7('X',0,1,1,0,1,1,1),\
 _SEG7('y',0,1,1,1,0,1,1), _SEG7('Z',1,1,0,1,1,0,1),

#define _MAP_123_126_ASCII_SEG7_SYMBOL		\
 _SEG7('{',1,0,0,1,1,1,0), _SEG7('|',0,0,0,0,1,1,0), _SEG7('}',1,1,1,1,0,0,0),\
 _SEG7('~',1,0,0,0,0,0,0),

/* Maps */

/* This set tries to map as close as possible to the visible characteristics
 * of the ASCII symbol, lowercase and uppercase letters may differ in
 * presentation on the display.
 */
#define MAP_ASCII7SEG_ALPHANUM			\
	_MAP_0_32_ASCII_SEG7_NON_PRINTABLE	\
	_MAP_33_47_ASCII_SEG7_SYMBOL		\
	_MAP_48_57_ASCII_SEG7_NUMERIC		\
	_MAP_58_64_ASCII_SEG7_SYMBOL		\
	_MAP_65_90_ASCII_SEG7_ALPHA_UPPR	\
	_MAP_91_96_ASCII_SEG7_SYMBOL		\
	_MAP_97_122_ASCII_SEG7_ALPHA_LOWER	\
	_MAP_123_126_ASCII_SEG7_SYMBOL

/* This set tries to map as close as possible to the symbolic characteristics
 * of the ASCII character for maximum discrimination.
 * For now this means all alpha chars are in lower case representations.
 * (This for example facilitates the use of hex numbers with uppercase input.)
 */
#define MAP_ASCII7SEG_ALPHANUM_LC			\
	_MAP_0_32_ASCII_SEG7_NON_PRINTABLE	\
	_MAP_33_47_ASCII_SEG7_SYMBOL		\
	_MAP_48_57_ASCII_SEG7_NUMERIC		\
	_MAP_58_64_ASCII_SEG7_SYMBOL		\
	_MAP_97_122_ASCII_SEG7_ALPHA_LOWER	\
	_MAP_91_96_ASCII_SEG7_SYMBOL		\
	_MAP_97_122_ASCII_SEG7_ALPHA_LOWER	\
	_MAP_123_126_ASCII_SEG7_SYMBOL

#define SEG7_DEFAULT_MAP(_name)		\
	SEG7_CONVERSION_MAP(_name,MAP_ASCII7SEG_ALPHANUM)

#endif	/* MAP_TO_7SEGMENT_H */

