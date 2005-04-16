/*
 *  include/asm-s390x/extmem.h
 *
 *  definitions for external memory segment support
 *  Copyright (C) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 */

#ifndef _ASM_S390X_DCSS_H
#define _ASM_S390X_DCSS_H
#ifndef __ASSEMBLY__

/* possible values for segment type as returned by segment_info */
#define SEG_TYPE_SW 0
#define SEG_TYPE_EW 1
#define SEG_TYPE_SR 2
#define SEG_TYPE_ER 3
#define SEG_TYPE_SN 4
#define SEG_TYPE_EN 5
#define SEG_TYPE_SC 6
#define SEG_TYPE_EWEN 7

#define SEGMENT_SHARED 0
#define SEGMENT_EXCLUSIVE 1

extern int segment_load (char *name,int segtype,unsigned long *addr,unsigned long *length);
extern void segment_unload(char *name);
extern void segment_save(char *name);
extern int segment_type (char* name);
extern int segment_modify_shared (char *name, int do_nonshared);

#endif
#endif
