/*
 *   fs/cifs/cache.c - CIFS filesystem cache index structure definitions
 *
 *   Copyright (c) 2010 Novell, Inc.
 *   Authors(s): Suresh Jayaraman (sjayaraman@suse.de>
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "fscache.h"
#include "cifs_debug.h"

/*
 * CIFS filesystem definition for FS-Cache
 */
struct fscache_netfs cifs_fscache_netfs = {
	.name = "cifs",
	.version = 0,
};

/*
 * Register CIFS for caching with FS-Cache
 */
int cifs_fscache_register(void)
{
	return fscache_register_netfs(&cifs_fscache_netfs);
}

/*
 * Unregister CIFS for caching
 */
void cifs_fscache_unregister(void)
{
	fscache_unregister_netfs(&cifs_fscache_netfs);
}

/*
 * Key layout of CIFS server cache index object
 */
struct cifs_server_key {
	uint16_t	family;		/* address family */
	uint16_t	port;		/* IP port */
	union {
		struct in_addr	ipv4_addr;
		struct in6_addr	ipv6_addr;
	} addr[0];
};

/*
 * Server object keyed by {IPaddress,port,family} tuple
 */
static uint16_t cifs_server_get_key(const void *cookie_netfs_data,
				   void *buffer, uint16_t maxbuf)
{
	const struct TCP_Server_Info *server = cookie_netfs_data;
	const struct sockaddr *sa = (struct sockaddr *) &server->addr.sockAddr;
	struct cifs_server_key *key = buffer;
	uint16_t key_len = sizeof(struct cifs_server_key);

	memset(key, 0, key_len);

	/*
	 * Should not be a problem as sin_family/sin6_family overlays
	 * sa_family field
	 */
	switch (sa->sa_family) {
	case AF_INET:
		key->family = server->addr.sockAddr.sin_family;
		key->port = server->addr.sockAddr.sin_port;
		key->addr[0].ipv4_addr = server->addr.sockAddr.sin_addr;
		key_len += sizeof(key->addr[0].ipv4_addr);
		break;

	case AF_INET6:
		key->family = server->addr.sockAddr6.sin6_family;
		key->port = server->addr.sockAddr6.sin6_port;
		key->addr[0].ipv6_addr = server->addr.sockAddr6.sin6_addr;
		key_len += sizeof(key->addr[0].ipv6_addr);
		break;

	default:
		cERROR(1, "CIFS: Unknown network family '%d'", sa->sa_family);
		key_len = 0;
		break;
	}

	return key_len;
}

/*
 * Server object for FS-Cache
 */
const struct fscache_cookie_def cifs_fscache_server_index_def = {
	.name = "CIFS.server",
	.type = FSCACHE_COOKIE_TYPE_INDEX,
	.get_key = cifs_server_get_key,
};
