/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Robert N. M. Watson
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

/*-
 * Kernel text-dump support: write a series of text files to the dump
 * partition for later recovery, including captured DDB output, kernel
 * configuration, message buffer, and panic message.  This allows for a more
 * compact representation of critical debugging information than traditional
 * binary dumps, as well as allowing dump information to be used without
 * access to kernel symbols, source code, etc.
 *
 * Storage Layout
 * --------------
 *
 * Crash dumps are aligned to the end of the dump or swap partition in order
 * to minimize the chances of swap duing fsck eating into the dump.  However,
 * unlike a memory dump, we don't know the size of the textdump a priori, so
 * can't just write it out sequentially in order from a known starting point
 * calculated with respect to the end of the partition.  In order to address
 * this, we actually write out the textdump in reverse block order, allowing
 * us to directly align it to the end of the partition and then write out the
 * dump header and trailer before and after it once done.  savecore(8) must
 * know to reverse the order of the blocks in order to produce a readable
 * file.
 *
 * Data is written out in the ustar file format so that we can write data
 * incrementally as a stream without reference to previous files.
 *
 * TODO
 * ----
 *
 * - Allow subsystems to register to submit files for inclusion in the text
 *   dump in a generic way.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_config.h"

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/kerneldump.h>
#include <sys/msgbuf.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <ddb/ddb.h>
#include <ddb/db_lex.h>

static SYSCTL_NODE(_debug_ddb, OID_AUTO, textdump, CTLFLAG_RW, 0,
    "DDB textdump options");

/*
 * Don't touch the first SIZEOF_METADATA bytes on the dump device.  This is
 * to protect us from metadata and metadata from us.
 */
#define	SIZEOF_METADATA		(64*1024)

/*
 * Data is written out as a series of files in the ustar tar format.  ustar
 * is a simple streamed format consiting of a series of files prefixed with
 * headers, and all padded to 512-byte block boundaries, which maps
 * conveniently to our requirements.
 */
struct ustar_header {
	char	uh_filename[100];
	char	uh_mode[8];
	char	uh_tar_owner[8];
	char	uh_tar_group[8];
	char	uh_size[12];
	char	uh_mtime[12];
	char	uh_sum[8];
	char	uh_type;
	char	uh_linkfile[100];
	char	uh_ustar[6];
	char	uh_version[2];
	char	uh_owner[32];
	char	uh_group[32];
	char	uh_major[8];
	char	uh_minor[8];
	char	uh_filenameprefix[155];
	char	uh_zeropad[12];
} __packed;

/*
 * Various size assertions -- pretty much everything must be one block in
 * size.
 */
CTASSERT(sizeof(struct kerneldumpheader) == TEXTDUMP_BLOCKSIZE);
CTASSERT(sizeof(struct ustar_header) == TEXTDUMP_BLOCKSIZE);

/*
 * Is a textdump scheduled?  If so, the shutdown code will invoke our dumpsys
 * routine instead of the machine-dependent kernel dump routine.
 */
#ifdef TEXTDUMP_PREFERRED
int	textdump_pending = 1;
#else
int	textdump_pending = 0;
#endif
SYSCTL_INT(_debug_ddb_textdump, OID_AUTO, pending, CTLFLAG_RW,
    &textdump_pending, 0,
    "Perform textdump instead of regular kernel dump.");

/*
 * Various constants for tar headers and contents.
 */
#define	TAR_USER	"root"
#define	TAR_GROUP	"wheel"
#define	TAR_UID		"0"
#define	TAR_GID		"0"
#define	TAR_MODE	"0600"
#define	TAR_USTAR	"ustar"

#define	TAR_CONFIG_FILENAME	"config.txt"	/* Kernel configuration. */
#define	TAR_MSGBUF_FILENAME	"msgbuf.txt"	/* Kernel messsage buffer. */
#define	TAR_PANIC_FILENAME	"panic.txt"	/* Panic message. */
#define	TAR_VERSION_FILENAME	"version.txt"	/* Kernel version. */

/*
 * Configure which files will be dumped.
 */
#ifdef INCLUDE_CONFIG_FILE
static int textdump_do_config = 1;
SYSCTL_INT(_debug_ddb_textdump, OID_AUTO, do_config, CTLFLAG_RW,
    &textdump_do_config, 0, "Dump kernel configuration in textdump");
#endif

static int textdump_do_ddb = 1;
SYSCTL_INT(_debug_ddb_textdump, OID_AUTO, do_ddb, CTLFLAG_RW,
    &textdump_do_ddb, 0, "Dump DDB captured output in textdump");

static int textdump_do_msgbuf = 1;
SYSCTL_INT(_debug_ddb_textdump, OID_AUTO, do_msgbuf, CTLFLAG_RW,
    &textdump_do_msgbuf, 0, "Dump kernel message buffer in textdump");

static int textdump_do_panic = 1;
SYSCTL_INT(_debug_ddb_textdump, OID_AUTO, do_panic, CTLFLAG_RW,
    &textdump_do_panic, 0, "Dump kernel panic message in textdump");

static int textdump_do_version = 1;
SYSCTL_INT(_debug_ddb_textdump, OID_AUTO, do_version, CTLFLAG_RW,
    &textdump_do_version, 0, "Dump kernel version string in textdump");

/*
 * State related to incremental writing of blocks to disk.
 */
static off_t textdump_offset;		/* Offset of next sequential write. */
static int textdump_error;		/* Carried write error, if any. */

/*
 * Statically allocate space to prepare block-sized headers and data.
 */
char textdump_block_buffer[TEXTDUMP_BLOCKSIZE];
static struct kerneldumpheader kdh;

/*
 * Calculate and fill in the checksum for a ustar header.
 */
static void
ustar_checksum(struct ustar_header *uhp)
{
	u_int sum;
	int i;

	for (i = 0; i < sizeof(uhp->uh_sum); i++)
		uhp->uh_sum[i] = ' ';
	sum = 0;
	for (i = 0; i < sizeof(*uhp); i++)
		sum += ((u_char *)uhp)[i];
	snprintf(uhp->uh_sum, sizeof(uhp->uh_sum), "%6o", sum);
}

/*
 * Each file in the tarball has a block-sized header with its name and other,
 * largely hard-coded, properties.
 */
void
textdump_mkustar(char *block_buffer, const char *filename, u_int size)
{
	struct ustar_header *uhp;

#ifdef TEXTDUMP_VERBOSE
	if (textdump_error == 0)
		printf("textdump: creating '%s'.\n", filename);
#endif
	uhp = (struct ustar_header *)block_buffer;
	bzero(uhp, sizeof(*uhp));
	strlcpy(uhp->uh_filename, filename, sizeof(uhp->uh_filename));
	strlcpy(uhp->uh_mode, TAR_MODE, sizeof(uhp->uh_mode));
	snprintf(uhp->uh_size, sizeof(uhp->uh_size), "%o", size);
	strlcpy(uhp->uh_tar_owner, TAR_UID, sizeof(uhp->uh_tar_owner));
	strlcpy(uhp->uh_tar_group, TAR_GID, sizeof(uhp->uh_tar_group));
	strlcpy(uhp->uh_owner, TAR_USER, sizeof(uhp->uh_owner));
	strlcpy(uhp->uh_group, TAR_GROUP, sizeof(uhp->uh_group));
	snprintf(uhp->uh_mtime, sizeof(uhp->uh_mtime), "%lo",
	    (unsigned long)time_second);
	uhp->uh_type = 0;
	strlcpy(uhp->uh_ustar, TAR_USTAR, sizeof(uhp->uh_ustar));
	ustar_checksum(uhp);
}

/*
 * textdump_writeblock() writes TEXTDUMP_BLOCKSIZE-sized blocks of data to
 * the space between di->mediaoffset and di->mediaoffset + di->mediasize.  It
 * accepts an offset relative to di->mediaoffset.  If we're carrying any
 * error from previous I/O, return that error and don't continue to try to
 * write.  Most writers ignore the error and forge ahead on the basis that
 * there's not much you can do.
 */
static int
textdump_writeblock(struct dumperinfo *di, off_t offset, char *buffer)
{

	if (textdump_error)
		return (textdump_error);
	if (offset + TEXTDUMP_BLOCKSIZE > di->mediasize)
		return (EIO);
	if (offset < SIZEOF_METADATA)
		return (ENOSPC);
	textdump_error = dump_write(di, buffer, 0, offset + di->mediaoffset,
	    TEXTDUMP_BLOCKSIZE);
	if (textdump_error)
		printf("textdump_writeblock: offset %jd, error %d\n", (intmax_t)offset,
		    textdump_error);
	return (textdump_error);
}

/*
 * Interfaces to save and restore the dump offset, so that printers can go
 * back to rewrite a header if required, while avoiding their knowing about
 * the global layout of the blocks.
 *
 * If we ever want to support writing textdumps to tape or other
 * stream-oriented target, we'll need to remove this.
 */
void
textdump_saveoff(off_t *offsetp)
{

	*offsetp = textdump_offset;
}

void
textdump_restoreoff(off_t offset)
{

	textdump_offset = offset;
}

/*
 * Interface to write the "next block" relative to the current offset; since
 * we write backwards from the end of the partition, we subtract, but there's
 * no reason for the caller to know this.
 */
int
textdump_writenextblock(struct dumperinfo *di, char *buffer)
{
	int error;

	error = textdump_writeblock(di, textdump_offset, buffer);
	textdump_offset -= TEXTDUMP_BLOCKSIZE;
	return (error);
}

#ifdef INCLUDE_CONFIG_FILE
extern char kernconfstring[];

/*
 * Dump kernel configuration.
 */
static void
textdump_dump_config(struct dumperinfo *di)
{
	u_int count, fullblocks, len;

	len = strlen(kernconfstring);
	textdump_mkustar(textdump_block_buffer, TAR_CONFIG_FILENAME, len);
	(void)textdump_writenextblock(di, textdump_block_buffer);

	/*
	 * Write out all full blocks directly from the string, and handle any
	 * left-over bits by copying it to out to the local buffer and
	 * zero-padding it.
	 */
	fullblocks = len / TEXTDUMP_BLOCKSIZE;
	for (count = 0; count < fullblocks; count++)
		(void)textdump_writenextblock(di, kernconfstring + count *
		    TEXTDUMP_BLOCKSIZE);
	if (len % TEXTDUMP_BLOCKSIZE != 0) {
		bzero(textdump_block_buffer, TEXTDUMP_BLOCKSIZE);
		bcopy(kernconfstring + count * TEXTDUMP_BLOCKSIZE,
		    textdump_block_buffer, len % TEXTDUMP_BLOCKSIZE);
		(void)textdump_writenextblock(di, textdump_block_buffer);
	}
}
#endif /* INCLUDE_CONFIG_FILE */

/*
 * Dump kernel message buffer.
 */
static void
textdump_dump_msgbuf(struct dumperinfo *di)
{
	off_t end_offset, tarhdr_offset;
	u_int i, len, offset, seq, total_len;
	char buf[16];

	/*
	 * Write out a dummy tar header to advance the offset; we'll rewrite
	 * it later once we know the true size.
	 */
	textdump_saveoff(&tarhdr_offset);
	textdump_mkustar(textdump_block_buffer, TAR_MSGBUF_FILENAME, 0);
	(void)textdump_writenextblock(di, textdump_block_buffer);

	/*
	 * Copy out the data in small chunks, but don't copy nuls that may be
	 * present if the message buffer has not yet completely filled at
	 * least once.
	 */
	total_len = 0;
	offset = 0;
	msgbuf_peekbytes(msgbufp, NULL, 0, &seq);
	while ((len = msgbuf_peekbytes(msgbufp, buf, sizeof(buf), &seq)) > 0) {
		for (i = 0; i < len; i++) {
			if (buf[i] == '\0')
				continue;
			textdump_block_buffer[offset] = buf[i];
			offset++;
			if (offset != sizeof(textdump_block_buffer))
				continue;
			(void)textdump_writenextblock(di,
			    textdump_block_buffer);
			total_len += offset;
			offset = 0;
		}
	}
	total_len += offset;	/* Without the zero-padding. */
	if (offset != 0) {
		bzero(textdump_block_buffer + offset,
		    sizeof(textdump_block_buffer) - offset);
		(void)textdump_writenextblock(di, textdump_block_buffer);
	}

	/*
	 * Rewrite tar header to reflect how much was actually written.
	 */
	textdump_saveoff(&end_offset);
	textdump_restoreoff(tarhdr_offset);
	textdump_mkustar(textdump_block_buffer, TAR_MSGBUF_FILENAME,
	    total_len);
	(void)textdump_writenextblock(di, textdump_block_buffer);
	textdump_restoreoff(end_offset);
}

static void
textdump_dump_panic(struct dumperinfo *di)
{
	u_int len;

	/*
	 * Write out tar header -- we store up to one block of panic message.
	 */
	len = min(strlen(panicstr), TEXTDUMP_BLOCKSIZE);
	textdump_mkustar(textdump_block_buffer, TAR_PANIC_FILENAME, len);
	(void)textdump_writenextblock(di, textdump_block_buffer);

	/*
	 * Zero-pad the panic string and write out block.
	 */
	bzero(textdump_block_buffer, sizeof(textdump_block_buffer));
	bcopy(panicstr, textdump_block_buffer, len);
	(void)textdump_writenextblock(di, textdump_block_buffer);
}

static void
textdump_dump_version(struct dumperinfo *di)
{
	u_int len;

	/*
	 * Write out tar header -- at most one block of version information.
	 */
	len = min(strlen(version), TEXTDUMP_BLOCKSIZE);
	textdump_mkustar(textdump_block_buffer, TAR_VERSION_FILENAME, len);
	(void)textdump_writenextblock(di, textdump_block_buffer);

	/*
	 * Zero pad the version string and write out block.
	 */
	bzero(textdump_block_buffer, sizeof(textdump_block_buffer));
	bcopy(version, textdump_block_buffer, len);
	(void)textdump_writenextblock(di, textdump_block_buffer);
}

/*
 * Commit text dump to disk.
 */
void
textdump_dumpsys(struct dumperinfo *di)
{
	struct kerneldumpcrypto *kdc;
	off_t dumplen, trailer_offset;

	if (di->blocksize != TEXTDUMP_BLOCKSIZE) {
		printf("Dump partition block size (%ju) not textdump "
		    "block size (%ju)", (uintmax_t)di->blocksize,
		    (uintmax_t)TEXTDUMP_BLOCKSIZE);
		return;
	}

	/*
	 * We don't know a priori how large the dump will be, but we do know
	 * that we need to reserve space for metadata and that we need two
	 * dump headers.  Also leave room for one ustar header and one block
	 * of data.
	 */
	if (di->mediasize < SIZEOF_METADATA + 2 * sizeof(kdh)) {
		printf("Insufficient space on dump partition for minimal textdump.\n");
		return;
	}
	textdump_error = 0;

	/*
	 * Disable EKCD because we don't provide encrypted textdumps.
	 */
	kdc = di->kdcrypto;
	di->kdcrypto = NULL;

	/*
	 * Position the start of the dump so that we'll write the kernel dump
	 * trailer immediately before the end of the partition, and then work
	 * our way back.  We will rewrite this header later to reflect the
	 * true size if things go well.
	 */
	textdump_offset = di->mediasize - sizeof(kdh);
	textdump_saveoff(&trailer_offset);
	dump_init_header(di, &kdh, TEXTDUMPMAGIC, KERNELDUMP_TEXT_VERSION, 0);
	(void)textdump_writenextblock(di, (char *)&kdh);

	/*
	 * Write a series of files in ustar format.
	 */
	if (textdump_do_ddb)
		db_capture_dump(di);
#ifdef INCLUDE_CONFIG_FILE
	if (textdump_do_config)
		textdump_dump_config(di);
#endif
	if (textdump_do_msgbuf)
		textdump_dump_msgbuf(di);
	if (textdump_do_panic && panicstr != NULL)
		textdump_dump_panic(di);
	if (textdump_do_version)
		textdump_dump_version(di);

	/*
	 * Now that we know the true size, we can write out the header, then
	 * seek back to the end and rewrite the trailer with the correct
	 * size.
	 */
	dumplen = trailer_offset - (textdump_offset + TEXTDUMP_BLOCKSIZE);
	dump_init_header(di, &kdh, TEXTDUMPMAGIC, KERNELDUMP_TEXT_VERSION,
	    dumplen);
	(void)textdump_writenextblock(di, (char *)&kdh);
	textdump_restoreoff(trailer_offset);
	(void)textdump_writenextblock(di, (char *)&kdh);

	/*
	 * Terminate the dump, report any errors, and clear the pending flag.
	 */
	if (textdump_error == 0)
		(void)dump_write(di, NULL, 0, 0, 0);
	if (textdump_error == ENOSPC)
		printf("Textdump: Insufficient space on dump partition\n");
	else if (textdump_error != 0)
		printf("Textdump: Error %d writing dump\n", textdump_error);
	else
		printf("Textdump complete.\n");
	textdump_pending = 0;

	/*
	 * Restore EKCD status.
	 */
	di->kdcrypto = kdc;
}

/*-
 * DDB(4) command to manage textdumps:
 *
 * textdump set        - request a textdump
 * textdump status     - print DDB output textdump status
 * textdump unset      - clear textdump request
 */
static void
db_textdump_usage(void)
{

	db_printf("textdump [unset|set|status|dump]\n");
}

void
db_textdump_cmd(db_expr_t addr, bool have_addr, db_expr_t count, char *modif)
{
	int t;

	t = db_read_token();
	if (t != tIDENT) {
		db_textdump_usage();
		return;
	}
	if (db_read_token() != tEOL) {
		db_textdump_usage();
		return;
	}
	if (strcmp(db_tok_string, "set") == 0) {
		textdump_pending = 1;
		db_printf("textdump set\n");
	} else if (strcmp(db_tok_string, "status") == 0) {
		if (textdump_pending)
			db_printf("textdump is set\n");
		else
			db_printf("textdump is not set\n");
	} else if (strcmp(db_tok_string, "unset") == 0) {
		textdump_pending = 0;
		db_printf("textdump unset\n");
	} else if (strcmp(db_tok_string, "dump") == 0) {
		textdump_pending = 1;
		doadump(true);
	} else {
		db_textdump_usage();
	}
}
