/* SCTP kernel reference Implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * 
 * This file is part of the SCTP kernel reference Implementation
 * 
 * Support for memory object debugging.  This allows one to monitor the
 * object allocations/deallocations for types instrumented for this 
 * via the proc fs. 
 * 
 * The SCTP reference implementation is free software; 
 * you can redistribute it and/or modify it under the terms of 
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * The SCTP reference implementation is distributed in the hope that it 
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.  
 * 
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 * 
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by: 
 *    Jon Grimm             <jgrimm@us.ibm.com>
 * 
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/kernel.h>
#include <net/sctp/sctp.h>

/*
 * Global counters to count raw object allocation counts.
 * To add new counters, choose a unique suffix for the variable
 * name as the helper macros key off this suffix to make
 * life easier for the programmer.
 */

SCTP_DBG_OBJCNT(sock);
SCTP_DBG_OBJCNT(ep);
SCTP_DBG_OBJCNT(transport);
SCTP_DBG_OBJCNT(assoc);
SCTP_DBG_OBJCNT(bind_addr);
SCTP_DBG_OBJCNT(bind_bucket);
SCTP_DBG_OBJCNT(chunk);
SCTP_DBG_OBJCNT(addr);
SCTP_DBG_OBJCNT(ssnmap);
SCTP_DBG_OBJCNT(datamsg);

/* An array to make it easy to pretty print the debug information
 * to the proc fs.
 */
static sctp_dbg_objcnt_entry_t sctp_dbg_objcnt[] = {
	SCTP_DBG_OBJCNT_ENTRY(sock),
	SCTP_DBG_OBJCNT_ENTRY(ep),
	SCTP_DBG_OBJCNT_ENTRY(assoc),
	SCTP_DBG_OBJCNT_ENTRY(transport),
	SCTP_DBG_OBJCNT_ENTRY(chunk),
	SCTP_DBG_OBJCNT_ENTRY(bind_addr),
	SCTP_DBG_OBJCNT_ENTRY(bind_bucket),
	SCTP_DBG_OBJCNT_ENTRY(addr),
	SCTP_DBG_OBJCNT_ENTRY(ssnmap),
	SCTP_DBG_OBJCNT_ENTRY(datamsg),
};

/* Callback from procfs to read out objcount information.
 * Walk through the entries in the sctp_dbg_objcnt array, dumping
 * the raw object counts for each monitored type.
 *
 * This code was modified from similar code in route.c
 */
static int sctp_dbg_objcnt_read(char *buffer, char **start, off_t offset,
				int length, int *eof, void *data)
{
	int len = 0;
	off_t pos = 0;
	int entries;
	int i;
	char temp[128];

	/* How many entries? */
	entries = ARRAY_SIZE(sctp_dbg_objcnt);

	/* Walk the entries and print out the debug information
	 * for proc fs.
	 */
	for (i = 0; i < entries; i++) {
		pos += 128;

		/* Skip ahead. */
		if (pos <= offset) {
			len = 0;
			continue;
		}
		/* Print out each entry. */
		sprintf(temp, "%s: %d",
			sctp_dbg_objcnt[i].label,
			atomic_read(sctp_dbg_objcnt[i].counter));

		sprintf(buffer + len, "%-127s\n", temp);
		len += 128;
		if (pos >= offset+length)
			goto done;
	}

done:
	*start = buffer + len - (pos - offset);
	len = pos - offset;
	if (len > length)
		len = length;

  	return len;
}

/* Initialize the objcount in the proc filesystem.  */
void sctp_dbg_objcnt_init(void)
{
	create_proc_read_entry("sctp_dbg_objcnt", 0, proc_net_sctp,
			       sctp_dbg_objcnt_read, NULL);
}

/* Cleanup the objcount entry in the proc filesystem.  */
void sctp_dbg_objcnt_exit(void)
{
	remove_proc_entry("sctp_dbg_objcnt", proc_net_sctp);
}


