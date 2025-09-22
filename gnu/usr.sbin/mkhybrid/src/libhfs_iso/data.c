/*
 * hfsutils - tools for reading and writing Macintosh HFS volumes
 * Copyright (C) 1996, 1997 Robert Leslie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

# include <string.h>
# include <time.h>

# include "internal.h"
# include "data.h"
# include "btree.h"

# define MUTDIFF  2082844800L

static
unsigned long tzdiff = -1;

unsigned char hfs_charorder[256] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,

  0x20, 0x22, 0x23, 0x28, 0x29, 0x2a, 0x2b, 0x2c,
  0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
  0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e,
  0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,

  0x47, 0x48, 0x58, 0x5a, 0x5e, 0x60, 0x67, 0x69,
  0x6b, 0x6d, 0x73, 0x75, 0x77, 0x79, 0x7b, 0x7f,
  0x8d, 0x8f, 0x91, 0x93, 0x96, 0x98, 0x9f, 0xa1,
  0xa3, 0xa5, 0xa8, 0xaa, 0xab, 0xac, 0xad, 0xae,

  0x54, 0x48, 0x58, 0x5a, 0x5e, 0x60, 0x67, 0x69,
  0x6b, 0x6d, 0x73, 0x75, 0x77, 0x79, 0x7b, 0x7f,
  0x8d, 0x8f, 0x91, 0x93, 0x96, 0x98, 0x9f, 0xa1,
  0xa3, 0xa5, 0xa8, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3,

  0x4c, 0x50, 0x5c, 0x62, 0x7d, 0x81, 0x9a, 0x55,
  0x4a, 0x56, 0x4c, 0x4e, 0x50, 0x5c, 0x62, 0x64,
  0x65, 0x66, 0x6f, 0x70, 0x71, 0x72, 0x7d, 0x89,
  0x8a, 0x8b, 0x81, 0x83, 0x9c, 0x9d, 0x9e, 0x9a,

  0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0x95,
  0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0x52, 0x85,
  0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8,
  0xc9, 0xca, 0xcb, 0x57, 0x8c, 0xcc, 0x52, 0x85,

  0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0x26,
  0x27, 0xd4, 0x20, 0x4a, 0x4e, 0x83, 0x87, 0x87,
  0xd5, 0xd6, 0x24, 0x25, 0x2d, 0x2e, 0xd7, 0xd8,
  0xa7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,

  0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
  0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
  0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
  0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

/*
 * NAME:	data->getb()
 * DESCRIPTION:	marshal 1 byte into local host format
 */
char d_getb(unsigned char *ptr)
{
  return (char) ptr[0];
}

/*
 * NAME:	data->getw()
 * DESCRIPTION:	marshal 2 bytes into local host format
 */
short d_getw(unsigned char *ptr)
{
  return (short)
    ((ptr[0] << 8) |
     (ptr[1] << 0));
}

/*
 * NAME:	data->getl()
 * DESCRIPTION:	marshal 4 bytes into local host format
 */
long d_getl(unsigned char *ptr)
{
  return (long)
    ((ptr[0] << 24) |
     (ptr[1] << 16) |
     (ptr[2] <<  8) |
     (ptr[3] <<  0));
}

/*
 * NAME:	data->putb()
 * DESCRIPTION:	marshal 1 byte out to Macintosh (big-endian) format
 */
void d_putb(unsigned char *ptr, char data)
{
  ptr[0] = (unsigned char) data;
}

/*
 * NAME:	data->putw()
 * DESCRIPTION:	marshal 2 bytes out to Macintosh (big-endian) format
 */
void d_putw(unsigned char *ptr, short data)
{
  ptr[0] = ((unsigned short) data & 0xff00) >> 8;
  ptr[1] = ((unsigned short) data & 0x00ff) >> 0;
}

/*
 * NAME:	data->putl()
 * DESCRIPTION:	marshal 4 bytes out to Macintosh (big-endian) format
 */
void d_putl(unsigned char *ptr, long data)
{
  ptr[0] = ((unsigned long) data & 0xff000000) >> 24;
  ptr[1] = ((unsigned long) data & 0x00ff0000) >> 16;
  ptr[2] = ((unsigned long) data & 0x0000ff00) >>  8;
  ptr[3] = ((unsigned long) data & 0x000000ff) >>  0;
}

/*
 * NAME:	data->fetchb()
 * DESCRIPTION:	incrementally retrieve a byte of data
 */
void d_fetchb(unsigned char **ptr, char *dest)
{
  *dest = d_getb(*ptr);
  *ptr += 1;
}

/*
 * NAME:	data->fetchw()
 * DESCRIPTION:	incrementally retrieve a word of data
 */
void d_fetchw(unsigned char **ptr, short *dest)
{
  *dest = d_getw(*ptr);
  *ptr += 2;
}

/*
 * NAME:	data->fetchl()
 * DESCRIPTION:	incrementally retrieve a long word of data
 */
void d_fetchl(unsigned char **ptr, long *dest)
{
  *dest = d_getl(*ptr);
  *ptr += 4;
}

/*
 * NAME:	data->fetchs()
 * DESCRIPTION:	incrementally retrieve a string
 */
void d_fetchs(unsigned char **ptr, char *dest, int size)
{
  int len;
  char blen;

  d_fetchb(ptr, &blen);
  len = blen;

  if (len > 0 && len < size)
    memcpy(dest, *ptr, len);
  else
    len = 0;

  dest[len] = 0;

  *ptr += size - 1;
}

/*
 * NAME:	data->storeb()
 * DESCRIPTION:	incrementally store a byte of data
 */
void d_storeb(unsigned char **ptr, char data)
{
  d_putb(*ptr, data);
  *ptr += 1;
}

/*
 * NAME:	data->storew()
 * DESCRIPTION:	incrementally store a word of data
 */
void d_storew(unsigned char **ptr, short data)
{
  d_putw(*ptr, data);
  *ptr += 2;
}

/*
 * NAME:	data->storel()
 * DESCRIPTION:	incrementally store a long word of data
 */
void d_storel(unsigned char **ptr, long data)
{
  d_putl(*ptr, data);
  *ptr += 4;
}

/*
 * NAME:	data->stores()
 * DESCRIPTION:	incrementally store a string
 */
void d_stores(unsigned char **ptr, char *src, int size)
{
  int len;

  len = strlen(src);
  if (len > --size)
    len = 0;

  d_storeb(ptr, (unsigned char) len);

  memcpy(*ptr, src, len);
  memset(*ptr + len, 0, size - len);

  *ptr += size;
}

/*
 * NAME:	calctzdiff()
 * DESCRIPTION:	calculate the timezone difference between local time and UTC
 */
static
void calctzdiff(void)
{
  time_t t;
  int isdst;
  struct tm tm, *tmp;

  time(&t);
  isdst = localtime(&t)->tm_isdst;

  tmp = gmtime(&t);
  if (tmp)
    {
      tm = *tmp;
      tm.tm_isdst = isdst;

      tzdiff = t - mktime(&tm);
    }
  else
    tzdiff = 0;
}

/*
 * NAME:	data->tomtime()
 * DESCRIPTION:	convert UNIX time to Macintosh time
 */
unsigned long d_tomtime(unsigned long secs)
{
  time_t utime = secs;

  if (tzdiff == -1)
    calctzdiff();

  return utime + tzdiff + MUTDIFF;
}

/*
 * NAME:	data->toutime()
 * DESCRIPTION:	convert Macintosh time to UNIX time
 */
unsigned long d_toutime(unsigned long secs)
{
  time_t utime = secs;

  if (tzdiff == -1)
    calctzdiff();

  return utime - MUTDIFF - tzdiff;
}

/*
 * NAME:	data->relstring()
 * DESCRIPTION:	compare two strings as per MacOS for HFS
 */
int d_relstring(char *str1, char *str2)
{
  int diff;

  while (*str1 && *str2)
    {
      diff = hfs_charorder[(unsigned char) *str1] -
	     hfs_charorder[(unsigned char) *str2];

      if (diff)
	return diff;

      ++str1, ++str2;
    }

  if (! *str1 && *str2)
    return -1;
  else if (*str1 && ! *str2)
    return 1;

  return 0;
}
