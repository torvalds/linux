/*
 * Windows port of asd command line utility
 *
 * Copyright 2002, Broadcom Corporation
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied or
 * duplicated in any form, in whole or in part, without the prior written
 * permission of Broadcom Corporation.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h> 

#include "wfa_debug.h"
#include "wfa_sock.h"
#include "wfa_main.h"
#include "wfa_types.h"
#include "wfa_tlv.h"
#include "wfa_tg.h"
#include "wfa_cmds.h"
#include "wfa_miscs.h"
#include "wfa_ca.h"
#include "wfa_agtctrl.h"
#include "wfa_agt.h"
#include "wfa_rsp.h"
#include "wfa_wmmps.h"

void
get_rwl_exe_path(char *rwl_exe_path, int exe_path_len)
{
	char *trafficPath;
	trafficPath = malloc(WFA_BUFF_1K);
	if(GetCurrentDirectory(WFA_BUFF_128,trafficPath) == 0) {
		DPRINT_ERR(WFA_ERR, "Failed to get the Current path\n");
		free(trafficPath);
		exit(1);
	}
	_snprintf(rwl_exe_path, exe_path_len, " \"%s/wl\" ", trafficPath);
	free(trafficPath);
}

int
error_check(int unused_err)
{
	DPRINT_ERR(WFA_ERR, "Doing error check %d\n", unused_err);
	if (WSAGetLastError() == WSAEINTR)
		return TRUE;
	else
		return FALSE;
}

FILE*
asd_cmd_exec(char *trafficPath)
{
	FILE *fp;
	if((fp = asd_Config(trafficPath,TEMP_FILE_PATH)) == NULL){
		DPRINT_ERR(WFA_ERR, "Command Execution Failed\n");
		free(trafficPath);
	}
	return fp;
}

void
file_cleanup(FILE *fp)
{
	Cleanup_File(fp);
}

int
interface_validation(char *interfac)
{
	return isIpV4Addr(interfac);
}
