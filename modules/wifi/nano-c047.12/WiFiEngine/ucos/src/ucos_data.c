/**************************************************************************/
/* Copyright Nanoradio AB 2004                                            */
/*                                                                        */
/* Module Name   : ucos.c                                                 */
/* Revision      : PA1                                                    */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/*------------------------------------------------------------------------*/
/*                                                                        */
/* Module Description :                                                   */
/* ==================                                                     */
/*                                                                        */
/* This module bla, bla bla                                               */ 
 /*                                                                        */
/* Restrictions/special remarks:                                          */
/* ============================                                           */
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
#include "sysdef.h"
#include "ucos_idefs.h"

/**********************************************/
/* T E M P O R A Y  T E S T V A R I A B L E S */
/**********************************************/



/*******************************************************************************
C O N S T A N T S / M A C R O S
*******************************************************************************/

/*********************************************/
/* E X T E R N A L   D E C L A R A T I O N S */
/*********************************************/



/*********************************************/
/* C O N S T A N T   D E C L A R A T I O N S */
/*********************************************/






/***********************************/
/* L O C A L     T Y P E D E F ' S */
/***********************************/







/*******************************************************/
/*  L O C A L   F U N C T I O N   P R O T O T Y P E S  */
/*******************************************************/

/***********************************/
/* M O D U L E   V A R I A B L E S */
/***********************************/

/****************************/
/* Dynamic buffer variables */
/****************************/

#ifndef SMALL_UCOS
#if UCOS_NUM_SMALL_XBUFFERS != 0
SmallXBufferType     smallxBufferPool[UCOS_NUM_SMALL_XBUFFERS];
SmallXBufferType  *  nextFreeSmallxBuffer;
SmallXBufferType  *  smallxBufferPoolStart;
SmallXBufferType  *  smallxBufferPoolEnd;
#endif

#if UCOS_NUM_LARGE_XBUFFERS != 0
LargeXBufferType     largexBufferPool[UCOS_NUM_LARGE_XBUFFERS];
LargeXBufferType  *  nextFreeLargexBuffer;
LargeXBufferType  *  largexBufferPoolStart;
LargeXBufferType  *  largexBufferPoolEnd;
#endif
#endif /* SMALL_UCOS */


/***********************/
/* Scheduler variables */
/***********************/
ucos_msg_fifo_t       ucos_msg_fifo[SYSCFG_UCOS_OBJECT_PRIO_NUM_LEVELS];
ucos_msg_queue_t      ucos_msg_queue[SYSCFG_UCOS_OBJECT_PRIO_NUM_LEVELS];


/*****************************************************/
/*         C O D E   S T A R T S   H E R E           */
/*****************************************************/

/******************************** END OF FILE *********************************/

