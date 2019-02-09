Notes on Filesystem Layout
--------------------------

These notes describe what mkcramfs generates.  Kernel requirements are
a bit looser, e.g. it doesn't care if the <file_data> items are
swapped around (though it does care that directory entries (inodes) in
a given directory are contiguous, as this is used by readdir).

All data is currently in host-endian format; neither mkcramfs nor the
kernel ever do swabbing.  (See section `Block Size' below.)

<filesystem>:
	<superblock>
	<directory_structure>
	<data>

<superblock>: struct cramfs_super (see cramfs_fs.h).

<directory_structure>:
	For each file:
		struct cramfs_inode (see cramfs_fs.h).
		Filename.  Not generally null-terminated, but it is
		 null-padded to a multiple of 4 bytes.

The order of inode traversal is described as "width-first" (not to be
confused with breadth-first); i.e. like depth-first but listing all of
a directory's entries before recursing down its subdirectories: the
same order as `ls -AUR' (but without the /^\..*:$/ directory header
lines); put another way, the same order as `find -type d -exec
ls -AU1 {} \;'.

Beginning in 2.4.7, directory entries are sorted.  This optimization
allows cramfs_lookup to return more quickly when a filename does not
exist, speeds up user-space directory sorts, etc.

<data>:
	One <file_data> for each file that's either a symlink or a
	 regular file of non-zero st_size.

<file_data>:
	nblocks * <block_pointer>
	 (where nblocks = (st_size - 1) / blksize + 1)
	nblocks * <block>
	padding to multiple of 4 bytes

The i'th <block_pointer> for a file stores the byte offset of the
*end* of the i'th <block> (i.e. one past the last byte, which is the
same as the start of the (i+1)'th <block> if there is one).  The first
<block> immediately follows the last <block_pointer> for the file.
<block_pointer>s are each 32 bits long.

When the CRAMFS_FLAG_EXT_BLOCK_POINTERS capability bit is set, each
<block_pointer>'s top bits may contain special flags as follows:

CRAMFS_BLK_FLAG_UNCOMPRESSED (bit 31):
	The block data is not compressed and should be copied verbatim.

CRAMFS_BLK_FLAG_DIRECT_PTR (bit 30):
	The <block_pointer> stores the actual block start offset and not
	its end, shifted right by 2 bits. The block must therefore be
	aligned to a 4-byte boundary. The block size is either blksize
	if CRAMFS_BLK_FLAG_UNCOMPRESSED is also specified, otherwise
	the compressed data length is included in the first 2 bytes of
	the block data. This is used to allow discontiguous data layout
	and specific data block alignments e.g. for XIP applications.


The order of <file_data>'s is a depth-first descent of the directory
tree, i.e. the same order as `find -size +0 \( -type f -o -type l \)
-print'.


<block>: The i'th <block> is the output of zlib's compress function
applied to the i'th blksize-sized chunk of the input data if the
corresponding CRAMFS_BLK_FLAG_UNCOMPRESSED <block_ptr> bit is not set,
otherwise it is the input data directly.
(For the last <block> of the file, the input may of course be smaller.)
Each <block> may be a different size.  (See <block_pointer> above.)

<block>s are merely byte-aligned, not generally u32-aligned.

When CRAMFS_BLK_FLAG_DIRECT_PTR is specified then the corresponding
<block> may be located anywhere and not necessarily contiguous with
the previous/next blocks. In that case it is minimally u32-aligned.
If CRAMFS_BLK_FLAG_UNCOMPRESSED is also specified then the size is always
blksize except for the last block which is limited by the file length.
If CRAMFS_BLK_FLAG_DIRECT_PTR is set and CRAMFS_BLK_FLAG_UNCOMPRESSED
is not set then the first 2 bytes of the block contains the size of the
remaining block data as this cannot be determined from the placement of
logically adjacent blocks.


Holes
-----

This kernel supports cramfs holes (i.e. [efficient representation of]
blocks in uncompressed data consisting entirely of NUL bytes), but by
default mkcramfs doesn't test for & create holes, since cramfs in
kernels up to at least 2.3.39 didn't support holes.  Run mkcramfs
with -z if you want it to create files that can have holes in them.


Tools
-----

The cramfs user-space tools, including mkcramfs and cramfsck, are
located at <http://sourceforge.net/projects/cramfs/>.


Future Development
==================

Block Size
----------

(Block size in cramfs refers to the size of input data that is
compressed at a time.  It's intended to be somewhere around
PAGE_SIZE for cramfs_readpage's convenience.)

The superblock ought to indicate the block size that the fs was
written for, since comments in <linux/pagemap.h> indicate that
PAGE_SIZE may grow in future (if I interpret the comment
correctly).

Currently, mkcramfs #define's PAGE_SIZE as 4096 and uses that
for blksize, whereas Linux-2.3.39 uses its PAGE_SIZE, which in
turn is defined as PAGE_SIZE (which can be as large as 32KB on arm).
This discrepancy is a bug, though it's not clear which should be
changed.

One option is to change mkcramfs to take its PAGE_SIZE from
<asm/page.h>.  Personally I don't like this option, but it does
require the least amount of change: just change `#define
PAGE_SIZE (4096)' to `#include <asm/page.h>'.  The disadvantage
is that the generated cramfs cannot always be shared between different
kernels, not even necessarily kernels of the same architecture if
PAGE_SIZE is subject to change between kernel versions
(currently possible with arm and ia64).

The remaining options try to make cramfs more sharable.

One part of that is addressing endianness.  The two options here are
`always use little-endian' (like ext2fs) or `writer chooses
endianness; kernel adapts at runtime'.  Little-endian wins because of
code simplicity and little CPU overhead even on big-endian machines.

The cost of swabbing is changing the code to use the le32_to_cpu
etc. macros as used by ext2fs.  We don't need to swab the compressed
data, only the superblock, inodes and block pointers.


The other part of making cramfs more sharable is choosing a block
size.  The options are:

  1. Always 4096 bytes.

  2. Writer chooses blocksize; kernel adapts but rejects blocksize >
     PAGE_SIZE.

  3. Writer chooses blocksize; kernel adapts even to blocksize >
     PAGE_SIZE.

It's easy enough to change the kernel to use a smaller value than
PAGE_SIZE: just make cramfs_readpage read multiple blocks.

The cost of option 1 is that kernels with a larger PAGE_SIZE
value don't get as good compression as they can.

The cost of option 2 relative to option 1 is that the code uses
variables instead of #define'd constants.  The gain is that people
with kernels having larger PAGE_SIZE can make use of that if
they don't mind their cramfs being inaccessible to kernels with
smaller PAGE_SIZE values.

Option 3 is easy to implement if we don't mind being CPU-inefficient:
e.g. get readpage to decompress to a buffer of size MAX_BLKSIZE (which
must be no larger than 32KB) and discard what it doesn't need.
Getting readpage to read into all the covered pages is harder.

The main advantage of option 3 over 1, 2, is better compression.  The
cost is greater complexity.  Probably not worth it, but I hope someone
will disagree.  (If it is implemented, then I'll re-use that code in
e2compr.)


Another cost of 2 and 3 over 1 is making mkcramfs use a different
block size, but that just means adding and parsing a -b option.


Inode Size
----------

Given that cramfs will probably be used for CDs etc. as well as just
silicon ROMs, it might make sense to expand the inode a little from
its current 12 bytes.  Inodes other than the root inode are followed
by filename, so the expansion doesn't even have to be a multiple of 4
bytes.
