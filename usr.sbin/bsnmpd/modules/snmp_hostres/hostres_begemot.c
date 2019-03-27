/*-
 * Copyright (c) 2005-2006.
 *	Hartmut Brandt.
 *	All rights reserved.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
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
 *
 * $FreeBSD$
 */

#include <stdlib.h>

#include "hostres_snmp.h"
#include "hostres_oid.h"
#include "hostres_tree.h"

int
op_begemot(struct snmp_context *ctx, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{

	switch (op) {

	  case SNMP_OP_GET:
		switch (value->var.subs[sub - 1]) {

		  case LEAF_begemotHrStorageUpdate:
			value->v.uint32 = storage_tbl_refresh;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrFSUpdate:
			value->v.uint32 = fs_tbl_refresh;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrDiskStorageUpdate:
			value->v.uint32 = disk_storage_tbl_refresh;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrNetworkUpdate:
			value->v.uint32 = network_tbl_refresh;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrSWInstalledUpdate:
			value->v.uint32 = swins_tbl_refresh;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrSWRunUpdate:
			value->v.uint32 = swrun_tbl_refresh;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrPkgDir:
			return (string_get(value, pkg_dir, -1));
		}
		abort();

	  case SNMP_OP_GETNEXT:
		abort();

	  case SNMP_OP_SET:
		switch (value->var.subs[sub - 1]) {

		  case LEAF_begemotHrStorageUpdate:
			ctx->scratch->int1 = storage_tbl_refresh;
			storage_tbl_refresh = value->v.uint32;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrFSUpdate:
			ctx->scratch->int1 = fs_tbl_refresh;
			fs_tbl_refresh = value->v.uint32;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrDiskStorageUpdate:
			ctx->scratch->int1 = disk_storage_tbl_refresh;
			disk_storage_tbl_refresh = value->v.uint32;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrNetworkUpdate:
			ctx->scratch->int1 = network_tbl_refresh;
			network_tbl_refresh = value->v.uint32;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrSWInstalledUpdate:
			ctx->scratch->int1 = swins_tbl_refresh;
			swins_tbl_refresh = value->v.uint32;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrSWRunUpdate:
			ctx->scratch->int1 = swrun_tbl_refresh;
			swrun_tbl_refresh = value->v.uint32;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrPkgDir:
			return (string_save(value, ctx, -1, &pkg_dir));
		}
		abort();

	  case SNMP_OP_COMMIT:
		switch (value->var.subs[sub - 1]) {

		  case LEAF_begemotHrStorageUpdate:
		  case LEAF_begemotHrFSUpdate:
		  case LEAF_begemotHrDiskStorageUpdate:
		  case LEAF_begemotHrNetworkUpdate:
		  case LEAF_begemotHrSWInstalledUpdate:
		  case LEAF_begemotHrSWRunUpdate:
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrPkgDir:
			string_commit(ctx);
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_ROLLBACK:
		switch (value->var.subs[sub - 1]) {

		  case LEAF_begemotHrStorageUpdate:
			storage_tbl_refresh = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrFSUpdate:
			fs_tbl_refresh = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrDiskStorageUpdate:
			disk_storage_tbl_refresh = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrNetworkUpdate:
			network_tbl_refresh = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrSWInstalledUpdate:
			swins_tbl_refresh = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrSWRunUpdate:
			swrun_tbl_refresh = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotHrPkgDir:
			string_rollback(ctx, &pkg_dir);
			return (SNMP_ERR_NOERROR);
		}
		abort();
	}

	abort();
}
