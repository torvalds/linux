/*
 * Linux port of asd command line utility
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
#include "wfa_main.h"
#include "wfa_types.h"
#include "wfa_tlv.h"
#include "wfa_tg.h"
#include "wfa_cmds.h"
#include "wfa_miscs.h"
#include "wfa_sock.h"
#include "wfa_ca.h"
#include "wfa_agtctrl.h"
#include "wfa_agt.h"
#include "wfa_rsp.h"
#include "wfa_wmmps.h"

void
get_rwl_exe_path(char *rwl_exe_path, int exe_path_len)
{
	strncpy(rwl_exe_path,  "./wl", exe_path_len);
}

int
error_check(int errno_defined)
{
	if (errno == EINTR)
		return TRUE;
	else
		return FALSE;
}

FILE*
asd_cmd_exec(char * trafficPath)
{
	/* Execute the command through "wl" on the DUT,
	 * read the response into trafficPath and return the response
	 */
	FILE *fp;
	strncat(trafficPath,TEMP_FILE_PATH, strlen(TEMP_FILE_PATH));
	system(trafficPath);
	if ((fp = fopen(TEMP_FILE_PATH, "r+")) == NULL) {
		DPRINT_ERR(WFA_ERR, "failed to open temp_file_path\n");
		free(trafficPath);
	}
	return fp;
}
void
file_cleanup(FILE *fp)
{
	char *trafficPath;
	trafficPath = malloc(WFA_BUFF_1K);
	fclose(fp);
	strcpy(trafficPath,"rm -f ");
	strncat(trafficPath,TEMP_FILE_PATH, strlen(TEMP_FILE_PATH));
	exec_process(trafficPath);
	free(trafficPath);
}

int
interface_validation(char *interfac)
{
	return isString(interfac);
}
