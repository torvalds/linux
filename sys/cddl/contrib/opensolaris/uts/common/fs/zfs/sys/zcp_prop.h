/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#ifndef _SYS_ZCP_PROP_H
#define	_SYS_ZCP_PROP_H

#ifdef	__cplusplus
extern "C" {
#endif

int zcp_load_get_lib(lua_State *state);
boolean_t prop_valid_for_ds(dsl_dataset_t *ds, zfs_prop_t zfs_prop);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ZCP_PROP_H */
