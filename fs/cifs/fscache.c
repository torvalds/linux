/*
 *   fs/cifs/fscache.c - CIFS filesystem cache interface
 *
 *   Copyright (c) 2010 Novell, Inc.
 *   Author(s): Suresh Jayaraman (sjayaraman@suse.de>
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
#include "cifsglob.h"
#include "cifs_debug.h"

void cifs_fscache_get_client_cookie(struct TCP_Server_Info *server)
{
	server->fscache =
		fscache_acquire_cookie(cifs_fscache_netfs.primary_index,
				&cifs_fscache_server_index_def, server);
	cFYI(1, "CIFS: get client cookie (0x%p/0x%p)", server,
				server->fscache);
}

void cifs_fscache_release_client_cookie(struct TCP_Server_Info *server)
{
	cFYI(1, "CIFS: release client cookie (0x%p/0x%p)", server,
				server->fscache);
	fscache_relinquish_cookie(server->fscache, 0);
	server->fscache = NULL;
}

void cifs_fscache_get_super_cookie(struct cifsTconInfo *tcon)
{
	struct TCP_Server_Info *server = tcon->ses->server;

	tcon->fscache =
		fscache_acquire_cookie(server->fscache,
				&cifs_fscache_super_index_def, tcon);
	cFYI(1, "CIFS: get superblock cookie (0x%p/0x%p)",
				server->fscache, tcon->fscache);
}

void cifs_fscache_release_super_cookie(struct cifsTconInfo *tcon)
{
	cFYI(1, "CIFS: releasing superblock cookie (0x%p)", tcon->fscache);
	fscache_relinquish_cookie(tcon->fscache, 0);
	tcon->fscache = NULL;
}
