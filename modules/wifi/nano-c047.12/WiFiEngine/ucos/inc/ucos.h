/**************************************************************************/
/* Copyright Nanoradio AB 2004                                            */
/*                                                                        */
/* Module Name      : ucos.h                                               */
/* Revision         : PA1                                                 */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/*------------------------------------------------------------------------*/
/*                                                                        */
/* Module Description :                                                   */
/* ==================                                                     */
/*                                                                        */
/* This module contains definitions for UCOS block functions.              */
/*                                                                        */
/*                                                                        */
/* Restrictions:                                                          */
/* =============                                                          */
/* N/A                                                                    */
/*                                                                        */
/*                                                                        */
/* Revision History:                                                      */
/* ====================================================================   */
/*                                                                        */
/* Rev/Date/By                                                            */
/* Purpose     : Module created                                           */
/* Solution    : N/A                                                      */
/*                                                                        */
/* --------------------------------------------------------------------   */
/**************************************************************************/


#ifndef UCOS_H       
#define UCOS_H
#include "ucos_defs.h"


/*********************/
/* C O N S T A N T S */
/*********************/


/*********************/
/* T Y P E D E F ' S */
/*********************/


/************************************/
/* GLOBAL VARIABLES                 */
/************************************/
extern uint16_t NR_UCOS_OBJECTS;

/*************************/
/* P R O T O T Y P E ' S */
/*************************/
ucos_rc_t      ucos_init(void);
/*Partial re-init after sleep mode*/
void           ucos_re_init(void);
void           ucos_startup(void);
void           ucos_executive(ucos_mode_t mode);
void           ucos_register_object(SYSDEF_ObjectType          object,
                                    syscfg_ucos_object_prio_t  prio,
                                    ucos_object_entry_t        entry,
                                    char                     * name);
void           ucos_send_msg(ucos_msg_id_t     msg,
                             SYSDEF_ObjectType dest,
                             ucos_msg_param_t  param);
void           ucos_kill_msg(SYSDEF_ObjectType dest,
                             ucos_msg_id_t     msg);
void           ucos_get_timer(SYSDEF_ObjectType, ucos_timeout_t, ucos_timer_id_t);
void           ucos_kill_timer(SYSDEF_ObjectType, ucos_timer_id_t);
void           ucos_timer_timeout(uintptr_t trans_id);
void          *UCOS_intMalloc( uint16_t reqSize );
void           UCOS_intFree( void *ptr);
ucos_rc_t      UCOS_xfree(UCOS_XBufType);
#ifndef UCOS_XMALLOC_DEBUG
UCOS_XBufType  UCOS_xmalloc(uint16_t);
#else
UCOS_XBufType  UCOS_xmalloc_dbg(char* file, int line, uint16_t reqSize);
#define        UCOS_xmalloc(_reqSize) UCOS_xmalloc_dbg(__FILE__, __LINE__, (_reqSize))
#endif
void           ucos_idle_enable(void);
void           ucos_idle_disable(void);

#endif
