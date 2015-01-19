#ifndef LOADER_H
#define LOADER_H
#include "logo.h"

#define  INVALID_INFO   		0xffffffff
#define  LOGO_DBG_ENABLE	0x10000001
#define  LOGO_PROGRESS_ENABLE	0x10000002
#define  LOGO_LOADED		0x10000003
#define  LOGO_NEEDSCALER		0x10000004
#define  PLATFORM_DEVICE_OSD		"apollofb"
#define  PLATFORM_DEVICE_VID		"amstream"

#define  	PARA_FIRST_GROUP_START   	1
#define  	PARA_SECOND_GROUP_START 	5
#define	    PARA_THIRD_GROUP_START	18+7
#define    PARA_FOURTH_GROUP_START  21+7
#define    PARA_FIFTH_GROUP_START  	22+7
#define    PARA_SIXTH_GROUP_START	23+7
#define    PARA_END					23+7

typedef  struct {
	char *name;
	int   info;	
	u32   prev_idx;
	u32   next_idx;
	u32   cur_group_start;
	u32   cur_group_end;
}para_info_pair_t;



#endif
