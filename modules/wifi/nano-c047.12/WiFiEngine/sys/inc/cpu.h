/**************************************************************************/
/* Copyright Nanoradio AB 2004                                            */
/*                                                                        */
/* Module Name   : sysdef.h                                               */
/* Revision      : PA1                                                    */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/*------------------------------------------------------------------------*/
/*                                                                        */
/* Module Description :                                                   */
/* ==================                                                     */
/*                                                                        */
/* This module contains global system definitions for the LCS system.     */
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
/* Purpose         : DSP and TDMA switch of INT0/INT1                     */
/* Solution        : Yes                                                  */
/*                                                                        */
/* Rev/Date/By                                                            */
/* Purpose         : Module created                                       */
/* Solution        : N/A                                                  */
/*                                                                        */
/* --------------------------------------------------------------------   */
/**************************************************************************/
#ifndef CPU_H           

#define CPU_H
#include "sysdef.h"
#ifdef __C51__
   #include "at89x52.h"
#elif defined(__arm)
   /* Empty */
#elif !defined(_WIN32)
   #include "iom103.h"
#endif

#endif /* #ifndef CPU_H */











