
/* JEDEC Flash Interface.
 * This is an older type of interface for self programming flash. It is
 * commonly use in older AMD chips and is obsolete compared with CFI.
 * It is called JEDEC because the JEDEC association distributes the ID codes
 * for the chips.
 *
 * See the AMD flash databook for information on how to operate the interface.
 *
 * $Id: jedec.h,v 1.4 2005/11/07 11:14:54 gleixner Exp $
 */

#ifndef __LINUX_MTD_JEDEC_H__
#define __LINUX_MTD_JEDEC_H__

#include <linux/types.h>

#define MAX_JEDEC_CHIPS 16

// Listing of all supported chips and their information
struct JEDECTable
{
   __u16 jedec;
   char *name;
   unsigned long size;
   unsigned long sectorsize;
   __u32 capabilities;
};

// JEDEC being 0 is the end of the chip array
struct jedec_flash_chip
{
   __u16 jedec;
   unsigned long size;
   unsigned long sectorsize;

   // *(__u8*)(base + (adder << addrshift)) = data << datashift
   // Address size = size << addrshift
   unsigned long base;           // Byte 0 of the flash, will be unaligned
   unsigned int datashift;       // Useful for 32bit/16bit accesses
   unsigned int addrshift;
   unsigned long offset;         // linerized start. base==offset for unbanked, uninterleaved flash

   __u32 capabilities;

   // These markers are filled in by the flash_chip_scan function
   unsigned long start;
   unsigned long length;
};

struct jedec_private
{
   unsigned long size;         // Total size of all the devices

   /* Bank handling. If sum(bank_fill) == size then this is linear flash.
      Otherwise the mapping has holes in it. bank_fill may be used to
      find the holes, but in the common symetric case
      bank_fill[0] == bank_fill[*], thus addresses may be computed
      mathmatically. bank_fill must be powers of two */
   unsigned is_banked;
   unsigned long bank_fill[MAX_JEDEC_CHIPS];

   struct jedec_flash_chip chips[MAX_JEDEC_CHIPS];
};

#endif
