/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2009 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __LOWCOMMS_DOT_H__
#define __LOWCOMMS_DOT_H__

int dlm_lowcomms_start(void);
void dlm_lowcomms_stop(void);
int dlm_lowcomms_close(int nodeid);
void *dlm_lowcomms_get_buffer(int nodeid, int len, gfp_t allocation, char **ppc);
void dlm_lowcomms_commit_buffer(void *mh);
int dlm_lowcomms_connect_node(int nodeid);

#endif				/* __LOWCOMMS_DOT_H__ */

