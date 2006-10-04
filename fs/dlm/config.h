/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __CONFIG_DOT_H__
#define __CONFIG_DOT_H__

#define DLM_MAX_ADDR_COUNT 3

struct dlm_config_info {
	int tcp_port;
	int buffer_size;
	int rsbtbl_size;
	int lkbtbl_size;
	int dirtbl_size;
	int recover_timer;
	int toss_secs;
	int scan_secs;
};

extern struct dlm_config_info dlm_config;

int dlm_config_init(void);
void dlm_config_exit(void);
int dlm_node_weight(char *lsname, int nodeid);
int dlm_nodeid_list(char *lsname, int **ids_out);
int dlm_nodeid_to_addr(int nodeid, struct sockaddr_storage *addr);
int dlm_addr_to_nodeid(struct sockaddr_storage *addr, int *nodeid);
int dlm_our_nodeid(void);
int dlm_our_addr(struct sockaddr_storage *addr, int num);

#endif				/* __CONFIG_DOT_H__ */

