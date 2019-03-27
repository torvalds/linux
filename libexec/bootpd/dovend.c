/*
 * dovend.c : Inserts all but the first few vendor options.
 *
 * $FreeBSD$
 */

#include <sys/types.h>

#include <netinet/in.h>
#include <arpa/inet.h>			/* inet_ntoa */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#ifndef USE_BFUNCS
# include <memory.h>
/* Yes, memcpy is OK here (no overlapped copies). */
# define bcopy(a,b,c)    memcpy(b,a,c)
# define bzero(p,l)      memset(p,0,l)
# define bcmp(a,b,c)     memcmp(a,b,c)
# define index           strchr
#endif

#include "bootp.h"
#include "bootpd.h"
#include "report.h"
#include "dovend.h"

PRIVATE int insert_generic(struct shared_bindata *, byte **, int *);

/*
 * Insert the 2nd part of the options into an option buffer.
 * Return amount of space used.
 *
 * This inserts everything EXCEPT:
 *   magic cookie, subnet mask, gateway, bootsize, extension file
 * Those are handled separately (in bootpd.c) to allow this function
 * to be shared between bootpd and bootpef.
 *
 * When an "extension file" is in use, the options inserted by
 * this function go into the exten_file, not the bootp response.
 */

int
dovend_rfc1497(hp, buf, len)
	struct host *hp;
	byte *buf;
	int len;
{
	int bytesleft = len;
	byte *vp = buf;

	static const char noroom[] = "%s: No room for \"%s\" option";
#define	NEED(LEN, MSG) do                       \
		if (bytesleft < (LEN)) {         	    \
			report(LOG_NOTICE, noroom,          \
				   hp->hostname->string, MSG);  \
			return (vp - buf);                  \
		} while (0)

	/*
	 * Note that the following have already been inserted:
	 *   magic_cookie, subnet_mask, gateway, bootsize
	 *
	 * The remaining options are inserted in order of importance.
	 * (Of course the importance of each is a matter of opinion.)
	 * The option insertion order should probably be configurable.
	 *
	 * This is the order used in the NetBSD version.  Can anyone
	 * explain why the time_offset and swap_server are first?
	 * Also, why is the hostname so far down the list?  -gwr
	 */

	if (hp->flags.time_offset) {
		NEED(6, "to");
		*vp++ = TAG_TIME_OFFSET;/* -1 byte  */
		*vp++ = 4;				/* -1 byte  */
		insert_u_long(htonl(hp->time_offset), &vp);	/* -4 bytes */
		bytesleft -= 6;
	}
	/*
	 * swap server, root path, dump path
	 */
	if (hp->flags.swap_server) {
		NEED(6, "sw");
		/* There is just one SWAP_SERVER, so it is not an iplist. */
		*vp++ = TAG_SWAP_SERVER;/* -1 byte  */
		*vp++ = 4;				/* -1 byte  */
		insert_u_long(hp->swap_server.s_addr, &vp);	/* -4 bytes */
		bytesleft -= 6;			/* Fix real count */
	}
	if (hp->flags.root_path) {
		/*
		 * Check for room for root_path.  Add 2 to account for
		 * TAG_ROOT_PATH and length.
		 */
		len = strlen(hp->root_path->string);
		NEED((len + 2), "rp");
		*vp++ = TAG_ROOT_PATH;
		*vp++ = (byte) (len & 0xFF);
		bcopy(hp->root_path->string, vp, len);
		vp += len;
		bytesleft -= len + 2;
	}
	if (hp->flags.dump_file) {
		/*
		 * Check for room for dump_file.  Add 2 to account for
		 * TAG_DUMP_FILE and length.
		 */
		len = strlen(hp->dump_file->string);
		NEED((len + 2), "df");
		*vp++ = TAG_DUMP_FILE;
		*vp++ = (byte) (len & 0xFF);
		bcopy(hp->dump_file->string, vp, len);
		vp += len;
		bytesleft -= len + 2;
	}
	/*
	 * DNS server and domain
	 */
	if (hp->flags.domain_server) {
		if (insert_ip(TAG_DOMAIN_SERVER,
					  hp->domain_server,
					  &vp, &bytesleft))
			NEED(8, "ds");
	}
	if (hp->flags.domain_name) {
		/*
		 * Check for room for domain_name.  Add 2 to account for
		 * TAG_DOMAIN_NAME and length.
		 */
		len = strlen(hp->domain_name->string);
		NEED((len + 2), "dn");
		*vp++ = TAG_DOMAIN_NAME;
		*vp++ = (byte) (len & 0xFF);
		bcopy(hp->domain_name->string, vp, len);
		vp += len;
		bytesleft -= len + 2;
	}
	/*
	 * NIS (YP) server and domain
	 */
	if (hp->flags.nis_server) {
		if (insert_ip(TAG_NIS_SERVER,
					  hp->nis_server,
					  &vp, &bytesleft))
			NEED(8, "ds");
	}
	if (hp->flags.nis_domain) {
		/*
		 * Check for room for nis_domain.  Add 2 to account for
		 * TAG_NIS_DOMAIN and length.
		 */
		len = strlen(hp->nis_domain->string);
		NEED((len + 2), "dn");
		*vp++ = TAG_NIS_DOMAIN;
		*vp++ = (byte) (len & 0xFF);
		bcopy(hp->nis_domain->string, vp, len);
		vp += len;
		bytesleft -= len + 2;
	}
	/* IEN 116 name server */
	if (hp->flags.name_server) {
		if (insert_ip(TAG_NAME_SERVER,
					  hp->name_server,
					  &vp, &bytesleft))
			NEED(8, "ns");
	}
	if (hp->flags.rlp_server) {
		if (insert_ip(TAG_RLP_SERVER,
					  hp->rlp_server,
					  &vp, &bytesleft))
			NEED(8, "rl");
	}
	/* Time server (RFC 868) */
	if (hp->flags.time_server) {
		if (insert_ip(TAG_TIME_SERVER,
					  hp->time_server,
					  &vp, &bytesleft))
			NEED(8, "ts");
	}
	/* NTP (time) Server (RFC 1129) */
	if (hp->flags.ntp_server) {
		if (insert_ip(TAG_NTP_SERVER,
					  hp->ntp_server,
					  &vp, &bytesleft))
			NEED(8, "ts");
	}
	/*
	 * I wonder:  If the hostname were "promoted" into the BOOTP
	 * response part, might these "extension" files possibly be
	 * shared between several clients?
	 *
	 * Also, why not just use longer BOOTP packets with all the
	 * additional length used as option data.  This bootpd version
	 * already supports that feature by replying with the same
	 * packet length as the client request packet. -gwr
	 */
	if (hp->flags.name_switch && hp->flags.send_name) {
		/*
		 * Check for room for hostname.  Add 2 to account for
		 * TAG_HOST_NAME and length.
		 */
		len = strlen(hp->hostname->string);
#if 0
		/*
		 * XXX - Too much magic.  The user can always set the hostname
		 * to the short version in the bootptab file. -gwr
		 */
		if ((len + 2) > bytesleft) {
			/*
			 * Not enough room for full (domain-qualified) hostname, try
			 * stripping it down to just the first field (host).
			 */
			char *tmpstr = hp->hostname->string;
			len = 0;
			while (*tmpstr && (*tmpstr != '.')) {
				tmpstr++;
				len++;
			}
		}
#endif
		NEED((len + 2), "hn");
		*vp++ = TAG_HOST_NAME;
		*vp++ = (byte) (len & 0xFF);
		bcopy(hp->hostname->string, vp, len);
		vp += len;
		bytesleft -= len + 2;
	}
	/*
	 * The rest of these are less important, so they go last.
	 */
	if (hp->flags.lpr_server) {
		if (insert_ip(TAG_LPR_SERVER,
					  hp->lpr_server,
					  &vp, &bytesleft))
			NEED(8, "lp");
	}
	if (hp->flags.cookie_server) {
		if (insert_ip(TAG_COOKIE_SERVER,
					  hp->cookie_server,
					  &vp, &bytesleft))
			NEED(8, "cs");
	}
	if (hp->flags.log_server) {
		if (insert_ip(TAG_LOG_SERVER,
					  hp->log_server,
					  &vp, &bytesleft))
			NEED(8, "lg");
	}
	/*
	 * XXX - Add new tags here (to insert options)
	 */
	if (hp->flags.generic) {
		if (insert_generic(hp->generic, &vp, &bytesleft))
			NEED(64, "(generic)");
	}
	/*
	 * The end marker is inserted by the caller.
	 */
	return (vp - buf);
#undef	NEED
}								/* dovend_rfc1497 */



/*
 * Insert a tag value, a length value, and a list of IP addresses into the
 * memory buffer indirectly pointed to by "dest".  "tag" is the RFC1048 tag
 * number to use, "iplist" is a pointer to a list of IP addresses
 * (struct in_addr_list), and "bytesleft" points to an integer which
 * indicates the size of the "dest" buffer.
 *
 * Return zero if everything fits.
 *
 * This is used to fill the vendor-specific area of a bootp packet in
 * conformance to RFC1048.
 */

int
insert_ip(tag, iplist, dest, bytesleft)
	byte tag;
	struct in_addr_list *iplist;
	byte **dest;
	int *bytesleft;
{
	struct in_addr *addrptr;
	unsigned addrcount = 1;
	byte *d;

	if (iplist == NULL)
		return (0);

	if (*bytesleft >= 6) {
		d = *dest;				/* Save pointer for later */
		**dest = tag;
		(*dest) += 2;
		(*bytesleft) -= 2;		/* Account for tag and length */
		addrptr = iplist->addr;
		addrcount = iplist->addrcount;
		while ((*bytesleft >= 4) && (addrcount > 0)) {
			insert_u_long(addrptr->s_addr, dest);
			addrptr++;
			addrcount--;
			(*bytesleft) -= 4;	/* Four bytes per address */
		}
		d[1] = (byte) ((*dest - d - 2) & 0xFF);
	}
	return (addrcount);
}



/*
 * Insert generic data into a bootp packet.  The data is assumed to already
 * be in RFC1048 format.  It is inserted using a first-fit algorithm which
 * attempts to insert as many tags as possible.  Tags and data which are
 * too large to fit are skipped; any remaining tags are tried until they
 * have all been exhausted.
 * Return zero if everything fits.
 */

static int
insert_generic(gendata, buff, bytesleft)
	struct shared_bindata *gendata;
	byte **buff;
	int *bytesleft;
{
	byte *srcptr;
	int length, numbytes;
	int skipped = 0;

	if (gendata == NULL)
		return (0);

	srcptr = gendata->data;
	length = gendata->length;
	while ((length > 0) && (*bytesleft > 0)) {
		switch (*srcptr) {
		case TAG_END:
			length = 0;			/* Force an exit on next iteration */
			break;
		case TAG_PAD:
			*(*buff)++ = *srcptr++;
			(*bytesleft)--;
			length--;
			break;
		default:
			numbytes = srcptr[1] + 2;
			if (*bytesleft < numbytes)
				skipped += numbytes;
			else {
				bcopy(srcptr, *buff, numbytes);
				(*buff) += numbytes;
				(*bytesleft) -= numbytes;
			}
			srcptr += numbytes;
			length -= numbytes;
			break;
		}
	} /* while */
	return (skipped);
}

/*
 * Insert the unsigned long "value" into memory starting at the byte
 * pointed to by the byte pointer (*dest).  (*dest) is updated to
 * point to the next available byte.
 *
 * Since it is desirable to internally store network addresses in network
 * byte order (in struct in_addr's), this routine expects longs to be
 * passed in network byte order.
 *
 * However, due to the nature of the main algorithm, the long must be in
 * host byte order, thus necessitating the use of ntohl() first.
 */

void
insert_u_long(value, dest)
	u_int32 value;
	byte **dest;
{
	byte *temp;
	int n;

	value = ntohl(value);		/* Must use host byte order here */
	temp = (*dest += 4);
	for (n = 4; n > 0; n--) {
		*--temp = (byte) (value & 0xFF);
		value >>= 8;
	}
	/* Final result is network byte order */
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
