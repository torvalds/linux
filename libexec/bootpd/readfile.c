/************************************************************************
          Copyright 1988, 1991 by Carnegie Mellon University

                          All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted, provided
that the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation, and that the name of Carnegie Mellon University not be used
in advertising or publicity pertaining to distribution of the software
without specific, written prior permission.

CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL CMU BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL
DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

 $FreeBSD$

************************************************************************/

/*
 * bootpd configuration file reading code.
 *
 * The routines in this file deal with reading, interpreting, and storing
 * the information found in the bootpd configuration file (usually
 * /etc/bootptab).
 */


#include <sys/errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <netinet/in.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <syslog.h>

#ifndef USE_BFUNCS
#include <memory.h>
/* Yes, memcpy is OK here (no overlapped copies). */
#define	bcopy(a,b,c)	memcpy(b,a,c)
#define	bzero(p,l)	memset(p,0,l)
#define	bcmp(a,b,c)	memcmp(a,b,c)
#endif

#include "bootp.h"
#include "hash.h"
#include "hwaddr.h"
#include "lookup.h"
#include "readfile.h"
#include "report.h"
#include "tzone.h"
#include "bootpd.h"

#define HASHTABLESIZE		257	/* Hash table size (prime) */

/* Non-standard hardware address type (see bootp.h) */
#define HTYPE_DIRECT	0

/* Error codes returned by eval_symbol: */
#define SUCCESS			  0
#define E_END_OF_ENTRY		(-1)
#define E_SYNTAX_ERROR		(-2)
#define E_UNKNOWN_SYMBOL	(-3)
#define E_BAD_IPADDR		(-4)
#define E_BAD_HWADDR		(-5)
#define E_BAD_LONGWORD		(-6)
#define E_BAD_HWATYPE		(-7)
#define E_BAD_PATHNAME		(-8)
#define E_BAD_VALUE 		(-9)

/* Tag idendities. */
#define SYM_NULL		  0
#define SYM_BOOTFILE		  1
#define SYM_COOKIE_SERVER	  2
#define SYM_DOMAIN_SERVER	  3
#define SYM_GATEWAY		  4
#define SYM_HWADDR		  5
#define SYM_HOMEDIR		  6
#define SYM_HTYPE		  7
#define SYM_IMPRESS_SERVER	  8
#define SYM_IPADDR		  9
#define SYM_LOG_SERVER		 10
#define SYM_LPR_SERVER		 11
#define SYM_NAME_SERVER		 12
#define SYM_RLP_SERVER		 13
#define SYM_SUBNET_MASK		 14
#define SYM_TIME_OFFSET		 15
#define SYM_TIME_SERVER		 16
#define SYM_VENDOR_MAGIC	 17
#define SYM_SIMILAR_ENTRY	 18
#define SYM_NAME_SWITCH		 19
#define SYM_BOOTSIZE		 20
#define SYM_BOOT_SERVER		 22
#define SYM_TFTPDIR		 23
#define SYM_DUMP_FILE		 24
#define SYM_DOMAIN_NAME          25
#define SYM_SWAP_SERVER          26
#define SYM_ROOT_PATH            27
#define SYM_EXTEN_FILE           28
#define SYM_REPLY_ADDR           29
#define SYM_NIS_DOMAIN           30	/* RFC 1533 */
#define SYM_NIS_SERVER           31	/* RFC 1533 */
#define SYM_NTP_SERVER           32	/* RFC 1533 */
#define SYM_EXEC_FILE		 33	/* YORK_EX_OPTION */
#define SYM_MSG_SIZE 		 34
#define SYM_MIN_WAIT		 35
/* XXX - Add new tags here */

#define OP_ADDITION		  1	/* Operations on tags */
#define OP_DELETION		  2
#define OP_BOOLEAN		  3

#define MAXINADDRS		 16	/* Max size of an IP address list */
#define MAXBUFLEN		256	/* Max temp buffer space */
#define MAXENTRYLEN	       2048	/* Max size of an entire entry */



/*
 * Structure used to map a configuration-file symbol (such as "ds") to a
 * unique integer.
 */

struct symbolmap {
	char *symbol;
	int symbolcode;
};


struct htypename {
	char *name;
	byte htype;
};


PRIVATE int nhosts;				/* Number of hosts (/w hw or IP address) */
PRIVATE int nentries;			/* Total number of entries */
PRIVATE int32 modtime = 0;		/* Last modification time of bootptab */
PRIVATE char *current_hostname;	/* Name of the current entry. */
PRIVATE char current_tagname[8];

/*
 * List of symbolic names used in the bootptab file.  The order and actual
 * values of the symbol codes (SYM_. . .) are unimportant, but they must
 * all be unique.
 */

PRIVATE struct symbolmap symbol_list[] = {
	{"bf", SYM_BOOTFILE},
	{"bs", SYM_BOOTSIZE},
	{"cs", SYM_COOKIE_SERVER},
	{"df", SYM_DUMP_FILE},
	{"dn", SYM_DOMAIN_NAME},
	{"ds", SYM_DOMAIN_SERVER},
	{"ef", SYM_EXTEN_FILE},
	{"ex", SYM_EXEC_FILE},		/* YORK_EX_OPTION */
	{"gw", SYM_GATEWAY},
	{"ha", SYM_HWADDR},
	{"hd", SYM_HOMEDIR},
	{"hn", SYM_NAME_SWITCH},
	{"ht", SYM_HTYPE},
	{"im", SYM_IMPRESS_SERVER},
	{"ip", SYM_IPADDR},
	{"lg", SYM_LOG_SERVER},
	{"lp", SYM_LPR_SERVER},
	{"ms", SYM_MSG_SIZE},
	{"mw", SYM_MIN_WAIT},
	{"ns", SYM_NAME_SERVER},
	{"nt", SYM_NTP_SERVER},
	{"ra", SYM_REPLY_ADDR},
	{"rl", SYM_RLP_SERVER},
	{"rp", SYM_ROOT_PATH},
	{"sa", SYM_BOOT_SERVER},
	{"sm", SYM_SUBNET_MASK},
	{"sw", SYM_SWAP_SERVER},
	{"tc", SYM_SIMILAR_ENTRY},
	{"td", SYM_TFTPDIR},
	{"to", SYM_TIME_OFFSET},
	{"ts", SYM_TIME_SERVER},
	{"vm", SYM_VENDOR_MAGIC},
	{"yd", SYM_NIS_DOMAIN},
	{"ys", SYM_NIS_SERVER},
	/* XXX - Add new tags here */
};


/*
 * List of symbolic names for hardware types.  Name translates into
 * hardware type code listed with it.  Names must begin with a letter
 * and must be all lowercase.  This is searched linearly, so put
 * commonly-used entries near the beginning.
 */

PRIVATE struct htypename htnamemap[] = {
	{"ethernet", HTYPE_ETHERNET},
	{"ethernet3", HTYPE_EXP_ETHERNET},
	{"ether", HTYPE_ETHERNET},
	{"ether3", HTYPE_EXP_ETHERNET},
	{"ieee802", HTYPE_IEEE802},
	{"tr", HTYPE_IEEE802},
	{"token-ring", HTYPE_IEEE802},
	{"pronet", HTYPE_PRONET},
	{"chaos", HTYPE_CHAOS},
	{"arcnet", HTYPE_ARCNET},
	{"ax.25", HTYPE_AX25},
	{"direct", HTYPE_DIRECT},
	{"serial", HTYPE_DIRECT},
	{"slip", HTYPE_DIRECT},
	{"ppp", HTYPE_DIRECT}
};



/*
 * Externals and forward declarations.
 */

extern boolean iplookcmp();
boolean nmcmp(hash_datum *, hash_datum *);

PRIVATE void
	adjust(char **);
PRIVATE void
	del_string(struct shared_string *);
PRIVATE void
	del_bindata(struct shared_bindata *);
PRIVATE void
	del_iplist(struct in_addr_list *);
PRIVATE void
	eat_whitespace(char **);
PRIVATE int
	eval_symbol(char **, struct host *);
PRIVATE void
	fill_defaults(struct host *, char **);
PRIVATE void
	free_host(hash_datum *);
PRIVATE struct in_addr_list *
	get_addresses(char **);
PRIVATE struct shared_string *
	get_shared_string(char **);
PRIVATE char *
	get_string(char **, char *, u_int *);
PRIVATE u_int32
	get_u_long(char **);
PRIVATE boolean
	goodname(char *);
PRIVATE boolean
	hwinscmp(hash_datum *, hash_datum *);
PRIVATE int
	interp_byte(char **, byte *);
PRIVATE void
	makelower(char *);
PRIVATE boolean
        nullcmp(hash_datum *, hash_datum *);
PRIVATE int
	process_entry(struct host *, char *);
PRIVATE int
	process_generic(char **, struct shared_bindata **, u_int);
PRIVATE byte *
	prs_haddr(char **, u_int);
PRIVATE int
	prs_inetaddr(char **, u_int32 *);
PRIVATE void
	read_entry(FILE *, char *, u_int *);
PRIVATE char *
	smalloc(u_int);


/*
 * Vendor magic cookies for CMU and RFC1048
 */
u_char vm_cmu[4] = VM_CMU;
u_char vm_rfc1048[4] = VM_RFC1048;

/*
 * Main hash tables
 */
hash_tbl *hwhashtable;
hash_tbl *iphashtable;
hash_tbl *nmhashtable;

/*
 * Allocate hash tables for hardware address, ip address, and hostname
 * (shared by bootpd and bootpef)
 */
void
rdtab_init()
{
	hwhashtable = hash_Init(HASHTABLESIZE);
	iphashtable = hash_Init(HASHTABLESIZE);
	nmhashtable = hash_Init(HASHTABLESIZE);
	if (!(hwhashtable && iphashtable && nmhashtable)) {
		report(LOG_ERR, "Unable to allocate hash tables.");
		exit(1);
	}
}


/*
 * Read bootptab database file.  Avoid rereading the file if the
 * write date hasn't changed since the last time we read it.
 */

void
readtab(force)
	int force;
{
	struct host *hp;
	FILE *fp;
	struct stat st;
	unsigned hashcode, buflen;
	static char buffer[MAXENTRYLEN];

	/*
	 * Check the last modification time.
	 */
	if (stat(bootptab, &st) < 0) {
		report(LOG_ERR, "stat on \"%s\": %s",
			   bootptab, get_errmsg());
		return;
	}
#ifdef DEBUG
	if (debug > 3) {
		char timestr[28];
		strcpy(timestr, ctime(&(st.st_mtime)));
		/* zap the newline */
		timestr[24] = '\0';
		report(LOG_INFO, "bootptab mtime: %s",
			   timestr);
	}
#endif
	if ((force == 0) &&
		(st.st_mtime == modtime) &&
		st.st_nlink) {
		/*
		 * hasn't been modified or deleted yet.
		 */
		return;
	}
	if (debug)
		report(LOG_INFO, "reading %s\"%s\"",
			   (modtime != 0L) ? "new " : "",
			   bootptab);

	/*
	 * Open bootptab file.
	 */
	if ((fp = fopen(bootptab, "r")) == NULL) {
		report(LOG_ERR, "error opening \"%s\": %s", bootptab, get_errmsg());
		return;
	}
	/*
	 * Record file modification time.
	 */
	if (fstat(fileno(fp), &st) < 0) {
		report(LOG_ERR, "fstat: %s", get_errmsg());
		fclose(fp);
		return;
	}
	modtime = st.st_mtime;

	/*
	 * Entirely erase all hash tables.
	 */
	hash_Reset(hwhashtable, free_host);
	hash_Reset(iphashtable, free_host);
	hash_Reset(nmhashtable, free_host);

	nhosts = 0;
	nentries = 0;
	while (TRUE) {
		buflen = sizeof(buffer);
		read_entry(fp, buffer, &buflen);
		if (buflen == 0) {		/* More entries? */
			break;
		}
		hp = (struct host *) smalloc(sizeof(struct host));
		bzero((char *) hp, sizeof(*hp));
		/* the link count it zero */

		/*
		 * Get individual info
		 */
		if (process_entry(hp, buffer) < 0) {
			hp->linkcount = 1;
			free_host((hash_datum *) hp);
			continue;
		}
		/*
		 * If this is not a dummy entry, and the IP or HW
		 * address is not yet set, try to get them here.
		 * Dummy entries have . as first char of name.
		 */
		if (goodname(hp->hostname->string)) {
			char *hn = hp->hostname->string;
			u_int32 value;
			if (hp->flags.iaddr == 0) {
				if (lookup_ipa(hn, &value)) {
					report(LOG_ERR, "can not get IP addr for %s", hn);
					report(LOG_ERR, "(dummy names should start with '.')");
				} else {
					hp->iaddr.s_addr = value;
					hp->flags.iaddr = TRUE;
				}
			}
			/* Set default subnet mask. */
			if (hp->flags.subnet_mask == 0) {
				if (lookup_netmask(hp->iaddr.s_addr, &value)) {
					report(LOG_ERR, "can not get netmask for %s", hn);
				} else {
					hp->subnet_mask.s_addr = value;
					hp->flags.subnet_mask = TRUE;
				}
			}
		}
		if (hp->flags.iaddr) {
			nhosts++;
		}
		/* Register by HW addr if known. */
		if (hp->flags.htype && hp->flags.haddr) {
			/* We will either insert it or free it. */
			hp->linkcount++;
			hashcode = hash_HashFunction(hp->haddr, haddrlength(hp->htype));
			if (hash_Insert(hwhashtable, hashcode, hwinscmp, hp, hp) < 0) {
				report(LOG_NOTICE, "duplicate %s address: %s",
					   netname(hp->htype),
					   haddrtoa(hp->haddr, haddrlength(hp->htype)));
				free_host((hash_datum *) hp);
				continue;
			}
		}
		/* Register by IP addr if known. */
		if (hp->flags.iaddr) {
			hashcode = hash_HashFunction((u_char *) & (hp->iaddr.s_addr), 4);
			if (hash_Insert(iphashtable, hashcode, nullcmp, hp, hp) < 0) {
				report(LOG_ERR,
					   "hash_Insert() failed on IP address insertion");
			} else {
				/* Just inserted the host struct in a new hash list. */
				hp->linkcount++;
			}
		}
		/* Register by Name (always known) */
		hashcode = hash_HashFunction((u_char *) hp->hostname->string,
									 strlen(hp->hostname->string));
		if (hash_Insert(nmhashtable, hashcode, nullcmp,
						hp->hostname->string, hp) < 0) {
			report(LOG_ERR,
				 "hash_Insert() failed on insertion of hostname: \"%s\"",
				   hp->hostname->string);
		} else {
			/* Just inserted the host struct in a new hash list. */
			hp->linkcount++;
		}

		nentries++;
	}

	fclose(fp);
	if (debug)
		report(LOG_INFO, "read %d entries (%d hosts) from \"%s\"",
			   nentries, nhosts, bootptab);
	return;
}



/*
 * Read an entire host entry from the file pointed to by "fp" and insert it
 * into the memory pointed to by "buffer".  Leading whitespace and comments
 * starting with "#" are ignored (removed).  Backslashes (\) always quote
 * the next character except that newlines preceded by a backslash cause
 * line-continuation onto the next line.  The entry is terminated by a
 * newline character which is not preceded by a backslash.  Sequences
 * surrounded by double quotes are taken literally (including newlines, but
 * not backslashes).
 *
 * The "bufsiz" parameter points to an unsigned int which specifies the
 * maximum permitted buffer size.  Upon return, this value will be replaced
 * with the actual length of the entry (not including the null terminator).
 *
 * This code is a little scary. . . .  I don't like using gotos in C
 * either, but I first wrote this as an FSM diagram and gotos seemed like
 * the easiest way to implement it.  Maybe later I'll clean it up.
 */

PRIVATE void
read_entry(fp, buffer, bufsiz)
	FILE *fp;
	char *buffer;
	unsigned *bufsiz;
{
	int c, length;

	length = 0;

	/*
	 * Eat whitespace, blank lines, and comment lines.
	 */
  top:
	c = fgetc(fp);
	if (c < 0) {
		goto done;				/* Exit if end-of-file */
	}
	if (isspace(c)) {
		goto top;				/* Skip over whitespace */
	}
	if (c == '#') {
		while (TRUE) {			/* Eat comments after # */
			c = fgetc(fp);
			if (c < 0) {
				goto done;		/* Exit if end-of-file */
			}
			if (c == '\n') {
				goto top;		/* Try to read the next line */
			}
		}
	}
	ungetc(c, fp);				/* Other character, push it back to reprocess it */


	/*
	 * Now we're actually reading a data entry.  Get each character and
	 * assemble it into the data buffer, processing special characters like
	 * double quotes (") and backslashes (\).
	 */

  mainloop:
	c = fgetc(fp);
	switch (c) {
	case EOF:
	case '\n':
		goto done;				/* Exit on EOF or newline */
	case '\\':
		c = fgetc(fp);			/* Backslash, read a new character */
		if (c < 0) {
			goto done;			/* Exit on EOF */
		}
		*buffer++ = c;			/* Store the literal character */
		length++;
		if (length < *bufsiz - 1) {
			goto mainloop;
		} else {
			goto done;
		}
	case '"':
		*buffer++ = '"';		/* Store double-quote */
		length++;
		if (length >= *bufsiz - 1) {
			goto done;
		}
		while (TRUE) {			/* Special quote processing loop */
			c = fgetc(fp);
			switch (c) {
			case EOF:
				goto done;		/* Exit on EOF . . . */
			case '"':
				*buffer++ = '"';/* Store matching quote */
				length++;
				if (length < *bufsiz - 1) {
					goto mainloop;	/* And continue main loop */
				} else {
					goto done;
				}
			case '\\':
				if ((c = fgetc(fp)) < 0) {	/* Backslash */
					goto done;	/* EOF. . . .*/
				}
				/* FALLTHROUGH */
			default:
				*buffer++ = c;	/* Other character, store it */
				length++;
				if (length >= *bufsiz - 1) {
					goto done;
				}
			}
		}
	case ':':
		*buffer++ = c;			/* Store colons */
		length++;
		if (length >= *bufsiz - 1) {
			goto done;
		}
		do {					/* But remove whitespace after them */
			c = fgetc(fp);
			if ((c < 0) || (c == '\n')) {
				goto done;
			}
		} while (isspace(c));	/* Skip whitespace */

		if (c == '\\') {		/* Backslash quotes next character */
			c = fgetc(fp);
			if (c < 0) {
				goto done;
			}
			if (c == '\n') {
				goto top;		/* Backslash-newline continuation */
			}
		}
		/* FALLTHROUGH if "other" character */
	default:
		*buffer++ = c;			/* Store other characters */
		length++;
		if (length >= *bufsiz - 1) {
			goto done;
		}
	}
	goto mainloop;				/* Keep going */

  done:
	*buffer = '\0';				/* Terminate string */
	*bufsiz = length;			/* Tell the caller its length */
}



/*
 * Parse out all the various tags and parameters in the host entry pointed
 * to by "src".  Stuff all the data into the appropriate fields of the
 * host structure pointed to by "host".  If there is any problem with the
 * entry, an error message is reported via report(), no further processing
 * is done, and -1 is returned.  Successful calls return 0.
 *
 * (Some errors probably shouldn't be so completely fatal. . . .)
 */

PRIVATE int
process_entry(host, src)
	struct host *host;
	char *src;
{
	int retval;
	char *msg;

	if (!host || *src == '\0') {
		return -1;
	}
	host->hostname = get_shared_string(&src);
#if 0
	/* Be more liberal for the benefit of dummy tag names. */
	if (!goodname(host->hostname->string)) {
		report(LOG_ERR, "bad hostname: \"%s\"", host->hostname->string);
		del_string(host->hostname);
		return -1;
	}
#endif
	current_hostname = host->hostname->string;
	adjust(&src);
	while (TRUE) {
		retval = eval_symbol(&src, host);
		if (retval == SUCCESS) {
			adjust(&src);
			continue;
		}
		if (retval == E_END_OF_ENTRY) {
			/* The default subnet mask is set in readtab() */
			return 0;
		}
		/* Some kind of error. */
		switch (retval) {
		case E_SYNTAX_ERROR:
			msg = "bad syntax";
			break;
		case E_UNKNOWN_SYMBOL:
			msg = "unknown symbol";
			break;
		case E_BAD_IPADDR:
			msg = "bad INET address";
			break;
		case E_BAD_HWADDR:
			msg = "bad hardware address";
			break;
		case E_BAD_LONGWORD:
			msg = "bad longword value";
			break;
		case E_BAD_HWATYPE:
			msg = "bad HW address type";
			break;
		case E_BAD_PATHNAME:
			msg = "bad pathname (need leading '/')";
			break;
		case E_BAD_VALUE:
			msg = "bad value";
			break;
		default:
			msg = "unknown error";
			break;
		}						/* switch */
		report(LOG_ERR, "in entry named \"%s\", symbol \"%s\": %s",
			   current_hostname, current_tagname, msg);
		return -1;
	}
}


/*
 * Macros for use in the function below:
 */

/* Parse one INET address stored directly in MEMBER. */
#define PARSE_IA1(MEMBER) do \
{ \
	if (optype == OP_BOOLEAN) \
		return E_SYNTAX_ERROR; \
	hp->flags.MEMBER = FALSE; \
	if (optype == OP_ADDITION) { \
		if (prs_inetaddr(symbol, &value) < 0) \
			return E_BAD_IPADDR; \
		hp->MEMBER.s_addr = value; \
		hp->flags.MEMBER = TRUE; \
	} \
} while (0)

/* Parse a list of INET addresses pointed to by MEMBER */
#define PARSE_IAL(MEMBER) do \
{ \
	if (optype == OP_BOOLEAN) \
		return E_SYNTAX_ERROR; \
	if (hp->flags.MEMBER) { \
		hp->flags.MEMBER = FALSE; \
		assert(hp->MEMBER); \
		del_iplist(hp->MEMBER); \
		hp->MEMBER = NULL; \
	} \
	if (optype == OP_ADDITION) { \
		hp->MEMBER = get_addresses(symbol); \
		if (hp->MEMBER == NULL) \
			return E_SYNTAX_ERROR; \
		hp->flags.MEMBER = TRUE; \
	} \
} while (0)

/* Parse a shared string pointed to by MEMBER */
#define PARSE_STR(MEMBER) do \
{ \
	if (optype == OP_BOOLEAN) \
		return E_SYNTAX_ERROR; \
	if (hp->flags.MEMBER) { \
		hp->flags.MEMBER = FALSE; \
		assert(hp->MEMBER); \
		del_string(hp->MEMBER); \
		hp->MEMBER = NULL; \
	} \
	if (optype == OP_ADDITION) { \
		hp->MEMBER = get_shared_string(symbol); \
		if (hp->MEMBER == NULL) \
			return E_SYNTAX_ERROR; \
		hp->flags.MEMBER = TRUE; \
	} \
} while (0)

/* Parse an unsigned integer value for MEMBER */
#define PARSE_UINT(MEMBER) do \
{ \
	if (optype == OP_BOOLEAN) \
		return E_SYNTAX_ERROR; \
	hp->flags.MEMBER = FALSE; \
	if (optype == OP_ADDITION) { \
		value = get_u_long(symbol); \
		hp->MEMBER = value; \
		hp->flags.MEMBER = TRUE; \
	} \
} while (0)

/*
 * Evaluate the two-character tag symbol pointed to by "symbol" and place
 * the data in the structure pointed to by "hp".  The pointer pointed to
 * by "symbol" is updated to point past the source string (but may not
 * point to the next tag entry).
 *
 * Obviously, this need a few more comments. . . .
 */
PRIVATE int
eval_symbol(symbol, hp)
	char **symbol;
	struct host *hp;
{
	char tmpstr[MAXSTRINGLEN];
	byte *tmphaddr;
	struct symbolmap *symbolptr;
	u_int32 value;
	int32 timeoff;
	int i, numsymbols;
	unsigned len;
	int optype;					/* Indicates boolean, addition, or deletion */

	eat_whitespace(symbol);

	/* Make sure this is set before returning. */
	current_tagname[0] = (*symbol)[0];
	current_tagname[1] = (*symbol)[1];
	current_tagname[2] = 0;

	if ((*symbol)[0] == '\0') {
		return E_END_OF_ENTRY;
	}
	if ((*symbol)[0] == ':') {
		return SUCCESS;
	}
	if ((*symbol)[0] == 'T') {	/* generic symbol */
		(*symbol)++;
		value = get_u_long(symbol);
		snprintf(current_tagname, sizeof(current_tagname),
			"T%d", (int)value);
		eat_whitespace(symbol);
		if ((*symbol)[0] != '=') {
			return E_SYNTAX_ERROR;
		}
		(*symbol)++;
		if (!(hp->generic)) {
			hp->generic = (struct shared_bindata *)
				smalloc(sizeof(struct shared_bindata));
		}
		if (process_generic(symbol, &(hp->generic), (byte) (value & 0xFF)))
			return E_SYNTAX_ERROR;
		hp->flags.generic = TRUE;
		return SUCCESS;
	}
	/*
	 * Determine the type of operation to be done on this symbol
	 */
	switch ((*symbol)[2]) {
	case '=':
		optype = OP_ADDITION;
		break;
	case '@':
		optype = OP_DELETION;
		break;
	case ':':
	case '\0':
		optype = OP_BOOLEAN;
		break;
	default:
		return E_SYNTAX_ERROR;
	}

	symbolptr = symbol_list;
	numsymbols = sizeof(symbol_list) / sizeof(struct symbolmap);
	for (i = 0; i < numsymbols; i++) {
		if (((symbolptr->symbol)[0] == (*symbol)[0]) &&
			((symbolptr->symbol)[1] == (*symbol)[1])) {
			break;
		}
		symbolptr++;
	}
	if (i >= numsymbols) {
		return E_UNKNOWN_SYMBOL;
	}
	/*
	 * Skip past the = or @ character (to point to the data) if this
	 * isn't a boolean operation.  For boolean operations, just skip
	 * over the two-character tag symbol (and nothing else. . . .).
	 */
	(*symbol) += (optype == OP_BOOLEAN) ? 2 : 3;

	eat_whitespace(symbol);

	/* The cases below are in order by symbolcode value. */
	switch (symbolptr->symbolcode) {

	case SYM_BOOTFILE:
		PARSE_STR(bootfile);
		break;

	case SYM_COOKIE_SERVER:
		PARSE_IAL(cookie_server);
		break;

	case SYM_DOMAIN_SERVER:
		PARSE_IAL(domain_server);
		break;

	case SYM_GATEWAY:
		PARSE_IAL(gateway);
		break;

	case SYM_HWADDR:
		if (optype == OP_BOOLEAN)
			return E_SYNTAX_ERROR;
		hp->flags.haddr = FALSE;
		if (optype == OP_ADDITION) {
			/* Default the HW type to Ethernet */
			if (hp->flags.htype == 0) {
				hp->flags.htype = TRUE;
				hp->htype = HTYPE_ETHERNET;
			}
			tmphaddr = prs_haddr(symbol, hp->htype);
			if (!tmphaddr)
				return E_BAD_HWADDR;
			bcopy(tmphaddr, hp->haddr, haddrlength(hp->htype));
			hp->flags.haddr = TRUE;
		}
		break;

	case SYM_HOMEDIR:
		PARSE_STR(homedir);
		break;

	case SYM_HTYPE:
		if (optype == OP_BOOLEAN)
			return E_SYNTAX_ERROR;
		hp->flags.htype = FALSE;
		if (optype == OP_ADDITION) {
			value = 0L;			/* Assume an illegal value */
			eat_whitespace(symbol);
			if (isdigit(**symbol)) {
				value = get_u_long(symbol);
			} else {
				len = sizeof(tmpstr);
				(void) get_string(symbol, tmpstr, &len);
				makelower(tmpstr);
				numsymbols = sizeof(htnamemap) /
					sizeof(struct htypename);
				for (i = 0; i < numsymbols; i++) {
					if (!strcmp(htnamemap[i].name, tmpstr)) {
						break;
					}
				}
				if (i < numsymbols) {
					value = htnamemap[i].htype;
				}
			}
			if (value >= hwinfocnt) {
				return E_BAD_HWATYPE;
			}
			hp->htype = (byte) (value & 0xFF);
			hp->flags.htype = TRUE;
		}
		break;

	case SYM_IMPRESS_SERVER:
		PARSE_IAL(impress_server);
		break;

	case SYM_IPADDR:
		PARSE_IA1(iaddr);
		break;

	case SYM_LOG_SERVER:
		PARSE_IAL(log_server);
		break;

	case SYM_LPR_SERVER:
		PARSE_IAL(lpr_server);
		break;

	case SYM_NAME_SERVER:
		PARSE_IAL(name_server);
		break;

	case SYM_RLP_SERVER:
		PARSE_IAL(rlp_server);
		break;

	case SYM_SUBNET_MASK:
		PARSE_IA1(subnet_mask);
		break;

	case SYM_TIME_OFFSET:
		if (optype == OP_BOOLEAN)
			return E_SYNTAX_ERROR;
		hp->flags.time_offset = FALSE;
		if (optype == OP_ADDITION) {
			len = sizeof(tmpstr);
			(void) get_string(symbol, tmpstr, &len);
			if (!strncmp(tmpstr, "auto", 4)) {
				hp->time_offset = secondswest;
			} else {
				if (sscanf(tmpstr, "%d", (int*)&timeoff) != 1)
					return E_BAD_LONGWORD;
				hp->time_offset = timeoff;
			}
			hp->flags.time_offset = TRUE;
		}
		break;

	case SYM_TIME_SERVER:
		PARSE_IAL(time_server);
		break;

	case SYM_VENDOR_MAGIC:
		if (optype == OP_BOOLEAN)
			return E_SYNTAX_ERROR;
		hp->flags.vm_cookie = FALSE;
		if (optype == OP_ADDITION) {
			if (strncmp(*symbol, "auto", 4)) {
				/* The string is not "auto" */
				if (!strncmp(*symbol, "rfc", 3)) {
					bcopy(vm_rfc1048, hp->vm_cookie, 4);
				} else if (!strncmp(*symbol, "cmu", 3)) {
					bcopy(vm_cmu, hp->vm_cookie, 4);
				} else {
					if (!isdigit(**symbol))
						return E_BAD_IPADDR;
					if (prs_inetaddr(symbol, &value) < 0)
						return E_BAD_IPADDR;
					bcopy(&value, hp->vm_cookie, 4);
				}
				hp->flags.vm_cookie = TRUE;
			}
		}
		break;

	case SYM_SIMILAR_ENTRY:
		switch (optype) {
		case OP_ADDITION:
			fill_defaults(hp, symbol);
			break;
		default:
			return E_SYNTAX_ERROR;
		}
		break;

	case SYM_NAME_SWITCH:
		switch (optype) {
		case OP_ADDITION:
			return E_SYNTAX_ERROR;
		case OP_DELETION:
			hp->flags.send_name = FALSE;
			hp->flags.name_switch = FALSE;
			break;
		case OP_BOOLEAN:
			hp->flags.send_name = TRUE;
			hp->flags.name_switch = TRUE;
			break;
		}
		break;

	case SYM_BOOTSIZE:
		switch (optype) {
		case OP_ADDITION:
			if (!strncmp(*symbol, "auto", 4)) {
				hp->flags.bootsize = TRUE;
				hp->flags.bootsize_auto = TRUE;
			} else {
				hp->bootsize = (unsigned int) get_u_long(symbol);
				hp->flags.bootsize = TRUE;
				hp->flags.bootsize_auto = FALSE;
			}
			break;
		case OP_DELETION:
			hp->flags.bootsize = FALSE;
			break;
		case OP_BOOLEAN:
			hp->flags.bootsize = TRUE;
			hp->flags.bootsize_auto = TRUE;
			break;
		}
		break;

	case SYM_BOOT_SERVER:
		PARSE_IA1(bootserver);
		break;

	case SYM_TFTPDIR:
		PARSE_STR(tftpdir);
		if ((hp->tftpdir != NULL) &&
			(hp->tftpdir->string[0] != '/'))
			return E_BAD_PATHNAME;
		break;

	case SYM_DUMP_FILE:
		PARSE_STR(dump_file);
		break;

	case SYM_DOMAIN_NAME:
		PARSE_STR(domain_name);
		break;

	case SYM_SWAP_SERVER:
		PARSE_IA1(swap_server);
		break;

	case SYM_ROOT_PATH:
		PARSE_STR(root_path);
		break;

	case SYM_EXTEN_FILE:
		PARSE_STR(exten_file);
		break;

	case SYM_REPLY_ADDR:
		PARSE_IA1(reply_addr);
		break;

	case SYM_NIS_DOMAIN:
		PARSE_STR(nis_domain);
		break;

	case SYM_NIS_SERVER:
		PARSE_IAL(nis_server);
		break;

	case SYM_NTP_SERVER:
		PARSE_IAL(ntp_server);
		break;

#ifdef	YORK_EX_OPTION
	case SYM_EXEC_FILE:
		PARSE_STR(exec_file);
		break;
#endif

	case SYM_MSG_SIZE:
		PARSE_UINT(msg_size);
		if (hp->msg_size < BP_MINPKTSZ ||
			hp->msg_size > MAX_MSG_SIZE)
			return E_BAD_VALUE;
		break;

	case SYM_MIN_WAIT:
		PARSE_UINT(min_wait);
		break;

		/* XXX - Add new tags here */

	default:
		return E_UNKNOWN_SYMBOL;

	}							/* switch symbolcode */

	return SUCCESS;
}
#undef	PARSE_IA1
#undef	PARSE_IAL
#undef	PARSE_STR




/*
 * Read a string from the buffer indirectly pointed to through "src" and
 * move it into the buffer pointed to by "dest".  A pointer to the maximum
 * allowable length of the string (including null-terminator) is passed as
 * "length".  The actual length of the string which was read is returned in
 * the unsigned integer pointed to by "length".  This value is the same as
 * that which would be returned by applying the strlen() function on the
 * destination string (i.e the terminating null is not counted as a
 * character).  Trailing whitespace is removed from the string.  For
 * convenience, the function returns the new value of "dest".
 *
 * The string is read until the maximum number of characters, an unquoted
 * colon (:), or a null character is read.  The return string in "dest" is
 * null-terminated.
 */

PRIVATE char *
get_string(src, dest, length)
	char **src, *dest;
	unsigned *length;
{
	int n, len, quoteflag;

	quoteflag = FALSE;
	n = 0;
	len = *length - 1;
	while ((n < len) && (**src)) {
		if (!quoteflag && (**src == ':')) {
			break;
		}
		if (**src == '"') {
			(*src)++;
			quoteflag = !quoteflag;
			continue;
		}
		if (**src == '\\') {
			(*src)++;
			if (!**src) {
				break;
			}
		}
		*dest++ = *(*src)++;
		n++;
	}

	/*
	 * Remove that troublesome trailing whitespace. . .
	 */
	while ((n > 0) && isspace(dest[-1])) {
		dest--;
		n--;
	}

	*dest = '\0';
	*length = n;
	return dest;
}



/*
 * Read the string indirectly pointed to by "src", update the caller's
 * pointer, and return a pointer to a malloc'ed shared_string structure
 * containing the string.
 *
 * The string is read using the same rules as get_string() above.
 */

PRIVATE struct shared_string *
get_shared_string(src)
	char **src;
{
	char retstring[MAXSTRINGLEN];
	struct shared_string *s;
	unsigned length;

	length = sizeof(retstring);
	(void) get_string(src, retstring, &length);

	s = (struct shared_string *) smalloc(sizeof(struct shared_string)
										 + length);
	s->linkcount = 1;
	strcpy(s->string, retstring);

	return s;
}



/*
 * Load RFC1048 generic information directly into a memory buffer.
 *
 * "src" indirectly points to the ASCII representation of the generic data.
 * "dest" points to a string structure which is updated to point to a new
 * string with the new data appended to the old string.  The old string is
 * freed.
 *
 * The given tag value is inserted with the new data.
 *
 * The data may be represented as either a stream of hexadecimal numbers
 * representing bytes (any or all bytes may optionally start with '0x' and
 * be separated with periods ".") or as a quoted string of ASCII
 * characters (the quotes are required).
 */

PRIVATE int
process_generic(src, dest, tagvalue)
	char **src;
	struct shared_bindata **dest;
	u_int tagvalue;
{
	byte tmpbuf[MAXBUFLEN];
	byte *str;
	struct shared_bindata *bdata;
	u_int newlength, oldlength;

	str = tmpbuf;
	*str++ = (tagvalue & 0xFF);	/* Store tag value */
	str++;						/* Skip over length field */
	if ((*src)[0] == '"') {		/* ASCII data */
		newlength = sizeof(tmpbuf) - 2;	/* Set maximum allowed length */
		(void) get_string(src, (char *) str, &newlength);
		newlength++;			/* null terminator */
	} else {					/* Numeric data */
		newlength = 0;
		while (newlength < sizeof(tmpbuf) - 2) {
			if (interp_byte(src, str++) < 0)
				break;
			newlength++;
			if (**src == '.') {
				(*src)++;
			}
		}
	}
	if ((*src)[0] != ':')
		return -1;

	tmpbuf[1] = (newlength & 0xFF);
	oldlength = ((*dest)->length);
	bdata = (struct shared_bindata *) smalloc(sizeof(struct shared_bindata)
											+ oldlength + newlength + 1);
	if (oldlength > 0) {
		bcopy((*dest)->data, bdata->data, oldlength);
	}
	bcopy(tmpbuf, bdata->data + oldlength, newlength + 2);
	bdata->length = oldlength + newlength + 2;
	bdata->linkcount = 1;
	if (*dest) {
		del_bindata(*dest);
	}
	*dest = bdata;
	return 0;
}



/*
 * Verify that the given string makes sense as a hostname (according to
 * Appendix 1, page 29 of RFC882).
 *
 * Return TRUE for good names, FALSE otherwise.
 */

PRIVATE boolean
goodname(hostname)
	char *hostname;
{
	do {
		if (!isalpha(*hostname++)) {	/* First character must be a letter */
			return FALSE;
		}
		while (isalnum(*hostname) ||
			   (*hostname == '-') ||
			   (*hostname == '_') )
		{
			hostname++;			/* Alphanumeric or a hyphen */
		}
		if (!isalnum(hostname[-1])) {	/* Last must be alphanumeric */
			return FALSE;
		}
		if (*hostname == '\0') {/* Done? */
			return TRUE;
		}
	} while (*hostname++ == '.');	/* Dot, loop for next label */

	return FALSE;				/* If it's not a dot, lose */
}



/*
 * Null compare function -- always returns FALSE so an element is always
 * inserted into a hash table (i.e. there is never a collision with an
 * existing element).
 */

PRIVATE boolean
nullcmp(d1, d2)
	hash_datum *d1, *d2;
{
	return FALSE;
}


/*
 * Function for comparing a string with the hostname field of a host
 * structure.
 */

boolean
nmcmp(d1, d2)
	hash_datum *d1, *d2;
{
	char *name = (char *) d1;	/* XXX - OK? */
	struct host *hp = (struct host *) d2;

	return !strcmp(name, hp->hostname->string);
}


/*
 * Compare function to determine whether two hardware addresses are
 * equivalent.  Returns TRUE if "host1" and "host2" are equivalent, FALSE
 * otherwise.
 *
 * If the hardware addresses of "host1" and "host2" are identical, but
 * they are on different IP subnets, this function returns FALSE.
 *
 * This function is used when inserting elements into the hardware address
 * hash table.
 */

PRIVATE boolean
hwinscmp(d1, d2)
	hash_datum *d1, *d2;
{
	struct host *host1 = (struct host *) d1;
	struct host *host2 = (struct host *) d2;

	if (host1->htype != host2->htype) {
		return FALSE;
	}
	if (bcmp(host1->haddr, host2->haddr, haddrlength(host1->htype))) {
		return FALSE;
	}
	/* XXX - Is the subnet_mask field set yet? */
	if ((host1->subnet_mask.s_addr) == (host2->subnet_mask.s_addr)) {
		if (((host1->iaddr.s_addr) & (host1->subnet_mask.s_addr)) !=
			((host2->iaddr.s_addr) & (host2->subnet_mask.s_addr)))
		{
			return FALSE;
		}
	}
	return TRUE;
}


/*
 * Macros for use in the function below:
 */

#define DUP_COPY(MEMBER) do \
{ \
	if (!hp->flags.MEMBER) { \
		if ((hp->flags.MEMBER = hp2->flags.MEMBER) != 0) { \
			hp->MEMBER = hp2->MEMBER; \
		} \
	} \
} while (0)

#define DUP_LINK(MEMBER) do \
{ \
	if (!hp->flags.MEMBER) { \
		if ((hp->flags.MEMBER = hp2->flags.MEMBER) != 0) { \
			assert(hp2->MEMBER); \
			hp->MEMBER = hp2->MEMBER; \
			(hp->MEMBER->linkcount)++; \
		} \
	} \
} while (0)

/*
 * Process the "similar entry" symbol.
 *
 * The host specified as the value of the "tc" symbol is used as a template
 * for the current host entry.  Symbol values not explicitly set in the
 * current host entry are inferred from the template entry.
 */
PRIVATE void
fill_defaults(hp, src)
	struct host *hp;
	char **src;
{
	unsigned int tlen, hashcode;
	struct host *hp2;
	char tstring[MAXSTRINGLEN];

	tlen = sizeof(tstring);
	(void) get_string(src, tstring, &tlen);
	hashcode = hash_HashFunction((u_char *) tstring, tlen);
	hp2 = (struct host *) hash_Lookup(nmhashtable, hashcode, nmcmp, tstring);

	if (hp2 == NULL) {
		report(LOG_ERR, "can't find tc=\"%s\"", tstring);
		return;
	}
	DUP_LINK(bootfile);
	DUP_LINK(cookie_server);
	DUP_LINK(domain_server);
	DUP_LINK(gateway);
	/* haddr not copied */
	DUP_LINK(homedir);
	DUP_COPY(htype);

	DUP_LINK(impress_server);
	/* iaddr not copied */
	DUP_LINK(log_server);
	DUP_LINK(lpr_server);
	DUP_LINK(name_server);
	DUP_LINK(rlp_server);

	DUP_COPY(subnet_mask);
	DUP_COPY(time_offset);
	DUP_LINK(time_server);

	if (!hp->flags.vm_cookie) {
		if ((hp->flags.vm_cookie = hp2->flags.vm_cookie)) {
			bcopy(hp2->vm_cookie, hp->vm_cookie, 4);
		}
	}
	if (!hp->flags.name_switch) {
		if ((hp->flags.name_switch = hp2->flags.name_switch)) {
			hp->flags.send_name = hp2->flags.send_name;
		}
	}
	if (!hp->flags.bootsize) {
		if ((hp->flags.bootsize = hp2->flags.bootsize)) {
			hp->flags.bootsize_auto = hp2->flags.bootsize_auto;
			hp->bootsize = hp2->bootsize;
		}
	}
	DUP_COPY(bootserver);

	DUP_LINK(tftpdir);
	DUP_LINK(dump_file);
	DUP_LINK(domain_name);

	DUP_COPY(swap_server);
	DUP_LINK(root_path);
	DUP_LINK(exten_file);

	DUP_COPY(reply_addr);

	DUP_LINK(nis_domain);
	DUP_LINK(nis_server);
	DUP_LINK(ntp_server);

#ifdef	YORK_EX_OPTION
	DUP_LINK(exec_file);
#endif

	DUP_COPY(msg_size);
	DUP_COPY(min_wait);

	/* XXX - Add new tags here */

	DUP_LINK(generic);

}
#undef	DUP_COPY
#undef	DUP_LINK



/*
 * This function adjusts the caller's pointer to point just past the
 * first-encountered colon.  If it runs into a null character, it leaves
 * the pointer pointing to it.
 */

PRIVATE void
adjust(s)
	char **s;
{
	char *t;

	t = *s;
	while (*t && (*t != ':')) {
		t++;
	}
	if (*t) {
		t++;
	}
	*s = t;
}




/*
 * This function adjusts the caller's pointer to point to the first
 * non-whitespace character.  If it runs into a null character, it leaves
 * the pointer pointing to it.
 */

PRIVATE void
eat_whitespace(s)
	char **s;
{
	char *t;

	t = *s;
	while (*t && isspace(*t)) {
		t++;
	}
	*s = t;
}



/*
 * This function converts the given string to all lowercase.
 */

PRIVATE void
makelower(s)
	char *s;
{
	while (*s) {
		if (isupper(*s)) {
			*s = tolower(*s);
		}
		s++;
	}
}



/*
 *
 *	N O T E :
 *
 *	In many of the functions which follow, a parameter such as "src" or
 *	"symbol" is passed as a pointer to a pointer to something.  This is
 *	done for the purpose of letting the called function update the
 *	caller's copy of the parameter (i.e. to effect call-by-reference
 *	parameter passing).  The value of the actual parameter is only used
 *	to locate the real parameter of interest and then update this indirect
 *	parameter.
 *
 *	I'm sure somebody out there won't like this. . . .
 *  (Yea, because it usually makes code slower... -gwr)
 *
 */



/*
 * "src" points to a character pointer which points to an ASCII string of
 * whitespace-separated IP addresses.  A pointer to an in_addr_list
 * structure containing the list of addresses is returned.  NULL is
 * returned if no addresses were found at all.  The pointer pointed to by
 * "src" is updated to point to the first non-address (illegal) character.
 */

PRIVATE struct in_addr_list *
get_addresses(src)
	char **src;
{
	struct in_addr tmpaddrlist[MAXINADDRS];
	struct in_addr *address1, *address2;
	struct in_addr_list *result;
	unsigned addrcount, totalsize;

	address1 = tmpaddrlist;
	for (addrcount = 0; addrcount < MAXINADDRS; addrcount++) {
		while (isspace(**src) || (**src == ',')) {
			(*src)++;
		}
		if (!**src) {			/* Quit if nothing more */
			break;
		}
		if (prs_inetaddr(src, &(address1->s_addr)) < 0) {
			break;
		}
		address1++;				/* Point to next address slot */
	}
	if (addrcount < 1) {
		result = NULL;
	} else {
		totalsize = sizeof(struct in_addr_list)
		+			(addrcount - 1) * sizeof(struct in_addr);
		result = (struct in_addr_list *) smalloc(totalsize);
		result->linkcount = 1;
		result->addrcount = addrcount;
		address1 = tmpaddrlist;
		address2 = result->addr;
		for (; addrcount > 0; addrcount--) {
			address2->s_addr = address1->s_addr;
			address1++;
			address2++;
		}
	}
	return result;
}



/*
 * prs_inetaddr(src, result)
 *
 * "src" is a value-result parameter; the pointer it points to is updated
 * to point to the next data position.   "result" points to an unsigned long
 * in which an address is returned.
 *
 * This function parses the IP address string in ASCII "dot notation" pointed
 * to by (*src) and places the result (in network byte order) in the unsigned
 * long pointed to by "result".  For malformed addresses, -1 is returned,
 * (*src) points to the first illegal character, and the unsigned long pointed
 * to by "result" is unchanged.  Successful calls return 0.
 */

PRIVATE int
prs_inetaddr(src, result)
	char **src;
	u_int32 *result;
{
	char tmpstr[MAXSTRINGLEN];
	u_int32 value;
	u_int32 parts[4], *pp;
	int n;
	char *s, *t;

	/* Leading alpha char causes IP addr lookup. */
	if (isalpha(**src)) {
		/* Lookup IP address. */
		s = *src;
		t = tmpstr;
		while ((isalnum(*s) || (*s == '.') ||
				(*s == '-') || (*s == '_') ) &&
			   (t < &tmpstr[MAXSTRINGLEN - 1]) )
			*t++ = *s++;
		*t = '\0';
		*src = s;

		n = lookup_ipa(tmpstr, result);
		if (n < 0)
			report(LOG_ERR, "can not get IP addr for %s", tmpstr);
		return n;
	}

	/*
	 * Parse an address in Internet format:
	 *	a.b.c.d
	 *	a.b.c	(with c treated as 16-bits)
	 *	a.b	(with b treated as 24 bits)
	 */
	pp = parts;
  loop:
	/* If it's not a digit, return error. */
	if (!isdigit(**src))
		return -1;
	*pp++ = get_u_long(src);
	if (**src == '.') {
		if (pp < (parts + 4)) {
			(*src)++;
			goto loop;
		}
		return (-1);
	}
#if 0
	/* This is handled by the caller. */
	if (**src && !(isspace(**src) || (**src == ':'))) {
		return (-1);
	}
#endif

	/*
	 * Construct the address according to
	 * the number of parts specified.
	 */
	n = pp - parts;
	switch (n) {
	case 1:					/* a -- 32 bits */
		value = parts[0];
		break;
	case 2:					/* a.b -- 8.24 bits */
		value = (parts[0] << 24) | (parts[1] & 0xFFFFFF);
		break;
	case 3:					/* a.b.c -- 8.8.16 bits */
		value = (parts[0] << 24) | ((parts[1] & 0xFF) << 16) |
			(parts[2] & 0xFFFF);
		break;
	case 4:					/* a.b.c.d -- 8.8.8.8 bits */
		value = (parts[0] << 24) | ((parts[1] & 0xFF) << 16) |
			((parts[2] & 0xFF) << 8) | (parts[3] & 0xFF);
		break;
	default:
		return (-1);
	}
	*result = htonl(value);
	return (0);
}



/*
 * "src" points to a pointer which in turn points to a hexadecimal ASCII
 * string.  This string is interpreted as a hardware address and returned
 * as a pointer to the actual hardware address, represented as an array of
 * bytes.
 *
 * The ASCII string must have the proper number of digits for the specified
 * hardware type (e.g. twelve digits for a 48-bit Ethernet address).
 * Two-digit sequences (bytes) may be separated with periods (.)  and/or
 * prefixed with '0x' for readability, but this is not required.
 *
 * For bad addresses, the pointer which "src" points to is updated to point
 * to the start of the first two-digit sequence which was bad, and the
 * function returns a NULL pointer.
 */

PRIVATE byte *
prs_haddr(src, htype)
	char **src;
	u_int htype;
{
	static byte haddr[MAXHADDRLEN];
	byte *hap;
	char tmpstr[MAXSTRINGLEN];
	u_int tmplen;
	unsigned hal;
	char *p;

	hal = haddrlength(htype);	/* Get length of this address type */
	if (hal <= 0) {
		report(LOG_ERR, "Invalid addr type for HW addr parse");
		return NULL;
	}
	tmplen = sizeof(tmpstr);
	get_string(src, tmpstr, &tmplen);
	p = tmpstr;

	/* If it's a valid host name, try to lookup the HW address. */
	if (goodname(p)) {
		/* Lookup Hardware Address for hostname. */
		if ((hap = lookup_hwa(p, htype)) != NULL)
			return hap; /* success */
		report(LOG_ERR, "Add 0x prefix if hex value starts with A-F");
		/* OK, assume it must be numeric. */
	}

	hap = haddr;
	while (hap < haddr + hal) {
		if ((*p == '.') || (*p == ':'))
			p++;
		if (interp_byte(&p, hap++) < 0) {
			return NULL;
		}
	}
	return haddr;
}



/*
 * "src" is a pointer to a character pointer which in turn points to a
 * hexadecimal ASCII representation of a byte.  This byte is read, the
 * character pointer is updated, and the result is deposited into the
 * byte pointed to by "retbyte".
 *
 * The usual '0x' notation is allowed but not required.  The number must be
 * a two digit hexadecimal number.  If the number is invalid, "src" and
 * "retbyte" are left untouched and -1 is returned as the function value.
 * Successful calls return 0.
 */

PRIVATE int
interp_byte(src, retbyte)
	char **src;
	byte *retbyte;
{
	int v;

	if ((*src)[0] == '0' &&
		((*src)[1] == 'x' ||
		 (*src)[1] == 'X')) {
		(*src) += 2;			/* allow 0x for hex, but don't require it */
	}
	if (!isxdigit((*src)[0]) || !isxdigit((*src)[1])) {
		return -1;
	}
	if (sscanf(*src, "%2x", &v) != 1) {
		return -1;
	}
	(*src) += 2;
	*retbyte = (byte) (v & 0xFF);
	return 0;
}



/*
 * The parameter "src" points to a character pointer which points to an
 * ASCII string representation of an unsigned number.  The number is
 * returned as an unsigned long and the character pointer is updated to
 * point to the first illegal character.
 */

PRIVATE u_int32
get_u_long(src)
	char **src;
{
	u_int32 value, base;
	char c;

	/*
	 * Collect number up to first illegal character.  Values are specified
	 * as for C:  0x=hex, 0=octal, other=decimal.
	 */
	value = 0;
	base = 10;
	if (**src == '0') {
		base = 8;
		(*src)++;
	}
	if (**src == 'x' || **src == 'X') {
		base = 16;
		(*src)++;
	}
	while ((c = **src)) {
		if (isdigit(c)) {
			value = (value * base) + (c - '0');
			(*src)++;
			continue;
		}
		if (base == 16 && isxdigit(c)) {
			value = (value << 4) + ((c & ~32) + 10 - 'A');
			(*src)++;
			continue;
		}
		break;
	}
	return value;
}



/*
 * Routines for deletion of data associated with the main data structure.
 */


/*
 * Frees the entire host data structure given.  Does nothing if the passed
 * pointer is NULL.
 */

PRIVATE void
free_host(hmp)
	hash_datum *hmp;
{
	struct host *hostptr = (struct host *) hmp;
	if (hostptr == NULL)
		return;
	assert(hostptr->linkcount > 0);
	if (--(hostptr->linkcount))
		return;					/* Still has references */
	del_iplist(hostptr->cookie_server);
	del_iplist(hostptr->domain_server);
	del_iplist(hostptr->gateway);
	del_iplist(hostptr->impress_server);
	del_iplist(hostptr->log_server);
	del_iplist(hostptr->lpr_server);
	del_iplist(hostptr->name_server);
	del_iplist(hostptr->rlp_server);
	del_iplist(hostptr->time_server);
	del_iplist(hostptr->nis_server);
	del_iplist(hostptr->ntp_server);

	/*
	 * XXX - Add new tags here
	 * (if the value is an IP list)
	 */

	del_string(hostptr->hostname);
	del_string(hostptr->homedir);
	del_string(hostptr->bootfile);
	del_string(hostptr->tftpdir);
	del_string(hostptr->root_path);
	del_string(hostptr->domain_name);
	del_string(hostptr->dump_file);
	del_string(hostptr->exten_file);
	del_string(hostptr->nis_domain);

#ifdef	YORK_EX_OPTION
	del_string(hostptr->exec_file);
#endif

	/*
	 * XXX - Add new tags here
	 * (if it is a shared string)
	 */

	del_bindata(hostptr->generic);
	free((char *) hostptr);
}



/*
 * Decrements the linkcount on the given IP address data structure.  If the
 * linkcount goes to zero, the memory associated with the data is freed.
 */

PRIVATE void
del_iplist(iplist)
	struct in_addr_list *iplist;
{
	if (iplist) {
		if (!(--(iplist->linkcount))) {
			free((char *) iplist);
		}
	}
}



/*
 * Decrements the linkcount on a string data structure.  If the count
 * goes to zero, the memory associated with the string is freed.  Does
 * nothing if the passed pointer is NULL.
 */

PRIVATE void
del_string(stringptr)
	struct shared_string *stringptr;
{
	if (stringptr) {
		if (!(--(stringptr->linkcount))) {
			free((char *) stringptr);
		}
	}
}



/*
 * Decrements the linkcount on a shared_bindata data structure.  If the
 * count goes to zero, the memory associated with the data is freed.  Does
 * nothing if the passed pointer is NULL.
 */

PRIVATE void
del_bindata(dataptr)
	struct shared_bindata *dataptr;
{
	if (dataptr) {
		if (!(--(dataptr->linkcount))) {
			free((char *) dataptr);
		}
	}
}




/* smalloc()  --  safe malloc()
 *
 * Always returns a valid pointer (if it returns at all).  The allocated
 * memory is initialized to all zeros.  If malloc() returns an error, a
 * message is printed using the report() function and the program aborts
 * with a status of 1.
 */

PRIVATE char *
smalloc(nbytes)
	unsigned nbytes;
{
	char *retvalue;

	retvalue = malloc(nbytes);
	if (!retvalue) {
		report(LOG_ERR, "malloc() failure -- exiting");
		exit(1);
	}
	bzero(retvalue, nbytes);
	return retvalue;
}


/*
 * Compare function to determine whether two hardware addresses are
 * equivalent.  Returns TRUE if "host1" and "host2" are equivalent, FALSE
 * otherwise.
 *
 * This function is used when retrieving elements from the hardware address
 * hash table.
 */

boolean
hwlookcmp(d1, d2)
	hash_datum *d1, *d2;
{
	struct host *host1 = (struct host *) d1;
	struct host *host2 = (struct host *) d2;

	if (host1->htype != host2->htype) {
		return FALSE;
	}
	if (bcmp(host1->haddr, host2->haddr, haddrlength(host1->htype))) {
		return FALSE;
	}
	return TRUE;
}


/*
 * Compare function for doing IP address hash table lookup.
 */

boolean
iplookcmp(d1, d2)
	hash_datum *d1, *d2;
{
	struct host *host1 = (struct host *) d1;
	struct host *host2 = (struct host *) d2;

	return (host1->iaddr.s_addr == host2->iaddr.s_addr);
}

/*
 * Local Variables:
 * tab-width: 4
 * c-indent-level: 4
 * c-argdecl-indent: 4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: -4
 * c-label-offset: -4
 * c-brace-offset: 0
 * End:
 */
