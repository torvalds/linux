/*  devfs (Device FileSystem) utilities.

    Copyright (C) 1999-2002  Richard Gooch

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Richard Gooch may be reached by email at  rgooch@atnf.csiro.au
    The postal address is:
      Richard Gooch, c/o ATNF, P. O. Box 76, Epping, N.S.W., 2121, Australia.

    ChangeLog

    19991031   Richard Gooch <rgooch@atnf.csiro.au>
               Created.
    19991103   Richard Gooch <rgooch@atnf.csiro.au>
               Created <_devfs_convert_name> and supported SCSI and IDE CD-ROMs
    20000203   Richard Gooch <rgooch@atnf.csiro.au>
               Changed operations pointer type to void *.
    20000621   Richard Gooch <rgooch@atnf.csiro.au>
               Changed interface to <devfs_register_series>.
    20000622   Richard Gooch <rgooch@atnf.csiro.au>
               Took account of interface change to <devfs_mk_symlink>.
               Took account of interface change to <devfs_mk_dir>.
    20010519   Richard Gooch <rgooch@atnf.csiro.au>
               Documentation cleanup.
    20010709   Richard Gooch <rgooch@atnf.csiro.au>
               Created <devfs_*alloc_major> and <devfs_*alloc_devnum>.
    20010710   Richard Gooch <rgooch@atnf.csiro.au>
               Created <devfs_*alloc_unique_number>.
    20010730   Richard Gooch <rgooch@atnf.csiro.au>
               Documentation typo fix.
    20010806   Richard Gooch <rgooch@atnf.csiro.au>
               Made <block_semaphore> and <char_semaphore> private.
    20010813   Richard Gooch <rgooch@atnf.csiro.au>
               Fixed bug in <devfs_alloc_unique_number>: limited to 128 numbers
    20010818   Richard Gooch <rgooch@atnf.csiro.au>
               Updated major masks up to Linus' "no new majors" proclamation.
	       Block: were 126 now 122 free, char: were 26 now 19 free.
    20020324   Richard Gooch <rgooch@atnf.csiro.au>
               Fixed bug in <devfs_alloc_unique_number>: was clearing beyond
	       bitfield.
    20020326   Richard Gooch <rgooch@atnf.csiro.au>
               Fixed bitfield data type for <devfs_*alloc_devnum>.
               Made major bitfield type and initialiser 64 bit safe.
    20020413   Richard Gooch <rgooch@atnf.csiro.au>
               Fixed shift warning on 64 bit machines.
    20020428   Richard Gooch <rgooch@atnf.csiro.au>
               Copied and used macro for error messages from fs/devfs/base.c 
    20021013   Richard Gooch <rgooch@atnf.csiro.au>
               Documentation fix.
    20030101   Adam J. Richter <adam@yggdrasil.com>
               Eliminate DEVFS_SPECIAL_{CHR,BLK}.  Use mode_t instead.
    20030106   Christoph Hellwig <hch@infradead.org>
               Rewrite devfs_{,de}alloc_devnum to look like C code.
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/bitops.h>

int devfs_register_tape(const char *name)
{
	char tname[32], dest[64];
	static unsigned int tape_counter;
	unsigned int n = tape_counter++;

	sprintf(dest, "../%s", name);
	sprintf(tname, "tapes/tape%u", n);
	devfs_mk_symlink(tname, dest);

	return n;
}

EXPORT_SYMBOL(devfs_register_tape);

void devfs_unregister_tape(int num)
{
	if (num >= 0)
		devfs_remove("tapes/tape%u", num);
}

EXPORT_SYMBOL(devfs_unregister_tape);
