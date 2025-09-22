#define	FILE_VERSION_MAJOR	3
#define	patchlevel		22

/*
 * Patchlevel file for Ian Darwin's MAGIC command.
 * $Id: patchlevel.h,v 1.1 2000/10/10 20:40:37 beck Exp $
 *
 * $Log: patchlevel.h,v $
 * Revision 1.1  2000/10/10 20:40:37  beck
 * mkhybrid 1.12b5.1
 *
 * Note, this normally makes a "mkisofs" which is the same binary, mkisofs
 * from here needs to be tested out prior to replacing our existing mkisofs
 * with this. (currently just installs mkhybrid)
 *
 * Revision 1.22  1997/01/15 17:23:24  christos
 * - add support for elf core files: find the program name under SVR4 [Ken Pizzini]
 * - print strings only up to the first carriage return [various]
 * - freebsd international ascii support [J Wunsch]
 * - magic fixes and additions [Guy Harris]
 * - 64 bit fixes [Larry Schwimmer]
 * - support for both utime and utimes, but don't restore file access times
 *   by default [various]
 * - \xXX only takes 2 hex digits, not 3.
 * - re-implement support for core files [Guy Harris]
 *
 * Revision 1.21  1996/10/05 18:15:29  christos
 * Segregate elf stuff and conditionally enable it with -DBUILTIN_ELF
 * More magic fixes
 *
 * Revision 1.20  1996/06/22  22:15:52  christos
 * - support relative offsets of the form >&
 * - fix bug with truncating magic strings that contain \n
 * - file -f - did not read from stdin as documented
 * - support elf file parsing using our own elf support.
 * - as always magdir fixes and additions.
 *
 * Revision 1.19  1995/10/27  23:14:46  christos
 * Ability to parse colon separated list of magic files
 * New LEGAL.NOTICE
 * Various magic file changes
 *
 * Revision 1.18  1995/05/20  22:09:21  christos
 * Passed incorrect argument to eatsize().
 * Use %ld and %lx where appropriate.
 * Remove unused variables
 * ELF support for both big and little endian
 * Fixes for small files again.
 *
 * Revision 1.17  1995/04/28  17:29:13  christos
 * - Incorrect nroff detection fix from der Mouse
 * - Lost and incorrect magic entries.
 * - Added ELF stripped binary detection [in C; ugh]
 * - Look for $MAGIC to find the magic file.
 * - Eat trailing size specifications from numbers i.e. ignore 10L
 * - More fixes for very short files
 *
 * Revision 1.16  1995/03/25  22:06:45  christos
 * - use strtoul() where it exists.
 * - fix sign-extend bug
 * - try to detect tar archives before nroff files, otherwise
 *   tar files where the first file starts with a . will not work
 *
 * Revision 1.15  1995/01/21  21:03:35  christos
 * Added CSECTION for the file man page
 * Added version flag -v
 * Fixed bug with -f input flag (from iorio@violet.berkeley.edu)
 * Lots of magic fixes and reorganization...
 *
 * Revision 1.14  1994/05/03  17:58:23  christos
 * changes from mycroft@gnu.ai.mit.edu (Charles Hannum) for unsigned
 *
 * Revision 1.13  1994/01/21  01:27:01  christos
 * Fixed null termination bug from Don Seeley at BSDI in ascmagic.c
 *
 * Revision 1.12  1993/10/27  20:59:05  christos
 * Changed -z flag to understand gzip format too.
 * Moved builtin compression detection to a table, and move
 * the compress magic entry out of the source.
 * Made printing of numbers unsigned, and added the mask to it.
 * Changed the buffer size to 8k, because gzip will refuse to
 * unzip just a few bytes.
 *
 * Revision 1.11  1993/09/24  18:49:06  christos
 * Fixed small bug in softmagic.c introduced by
 * copying the data to be examined out of the input
 * buffer. Changed the Makefile to use sed to create
 * the correct man pages.
 *
 * Revision 1.10  1993/09/23  21:56:23  christos
 * Passed purify. Fixed indirections. Fixed byte order printing.
 * Fixed segmentation faults caused by referencing past the end
 * of the magic buffer. Fixed bus errors caused by referencing
 * unaligned shorts or longs.
 *
 * Revision 1.9  1993/03/24  14:23:40  ian
 * Batch of minor changes from several contributors.
 *
 * Revision 1.8  93/02/19  15:01:26  ian
 * Numerous changes from Guy Harris too numerous to mention but including
 * byte-order independance, fixing "old-style masking", etc. etc. A bugfix
 * for broken symlinks from martin@@d255s004.zfe.siemens.de.
 * 
 * Revision 1.7  93/01/05  14:57:27  ian
 * Couple of nits picked by Christos (again, thanks).
 * 
 * Revision 1.6  93/01/05  13:51:09  ian
 * Lotsa work on the Magic directory.
 * 
 * Revision 1.5  92/09/14  14:54:51  ian
 * Fix a tiny null-pointer bug in previous fix for tar archive + uncompress.
 * 
 */

