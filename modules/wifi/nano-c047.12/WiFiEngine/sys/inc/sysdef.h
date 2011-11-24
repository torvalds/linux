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
#ifndef SYSDEF_H           
#define SYSDEF_H
#include "env_config_all.h"
#include "systypes.h"
#include "sys_cfg.h"


/*---------------------------------------------------------------------------*/
/*! \brief        MACRO that controls generation of wrapper functions.       */
/*! \description  Define the top node definition as a struct to please the   */
/*                compiler. The definition will be used by the registry      */
/*                compiler to identify the top node.                         */ 
/*---------------------------------------------------------------------------*/
#define GENERATE_WRAPPER_FUNCTIONS(coding)

/*---------------------------------------------------------------------------*/
/*! \brief MACRO replacement of clib functions. */
/*---------------------------------------------------------------------------*/

#ifndef WINCE
#ifndef WIFI_ENGINE
#define SPRINTF str_sprintf
#define VSPRINTF str_vsprintf
#define sprintf str_sprintf
#define vsprintf str_vsprintf


#else /* WIFI_ENGINE */

#define SPRINTF sprintf
#define VSPRINTF vsprintf

#endif /* WIFI_ENGINE */
#endif /* WINCE */

/*! Macro for casting a non-function object pointer to a function pointer
 */
#define FUNCPTR_CAST(type,pointer) (*((type *)&(pointer)))

/*---------------------------------------------------------------------------*/
/*! \brief Macros for far functions

Use these macros where far functions are needed in the CR16 world. */
/*---------------------------------------------------------------------------*/
#define MEMCHR(p, v, l) memchr(p, v, l)
#define MEMCMP(p1, p2, l) memcmp(p1, p2, l)
#define MEMCPY(d, s, l) (void)memcpy(d, s, l)
#define MEMMOVE(d, s, l) memmove(d, s, l)
#define MEMSET(p, v, l) (void)memset(p, v, l)
#define STRCAT(d, s) strcat(d, s)
#define STRCHR(s, c) strchr(s, c)
#define STRCMP(s1, s2) strcmp(s1, s2)
#define STRCPY(d, s) strcpy(d, s)
#define STRCSPN(d, s, l) strcspn(d, s, l)
#define STRLEN(s) strlen(s)
#define STRNCAT(d, s, l) strncat(d, s, l)
#define STRNCMP(s1, s2, l) strncmp(s1, s2, l)
#define STRNCPY(d, s, l) strncpy(d, s, l)
#define STRPBRK(s, set) strpbrk(s, set)
#define STRRCHR(s, c) strrchr(s, c)
#define STRSPN(s, set) strspn(s, set)
#define STRSTR(src, sub) strstr(src, sub)
#define STRTOK(s, set) strtok(s, set)

/*********************************/
/* Consistency checks of defines */
/*********************************/
#ifndef LITTLEENDIAN
#ifndef BIGENDIAN
#error You must define either LITTLEENDIAN or BIGENDIAN
#endif
#endif

/***********************/
/*  C O N S T A N T S  */
/***********************/
#ifdef __C51__
   #define TASK
#else
   #define TASK C_task
#endif

#ifndef NO
#define NO   0
#endif

#ifndef YES
#define YES  1
#endif

#ifndef SOME
#define SOME 2
#endif
#define BOOLEAN     uint8_t

#ifndef FALSE
#define FALSE       0
#endif

#ifndef TRUE
#define TRUE        1
#endif

#ifndef DUMMY
#define DUMMY       0
#endif

#ifndef UNUSED
/* TODO: This macro should be extended to used correct syntax
   depending on compiler. For example, GCC has special argument that
   is better to use. */
#define UNUSED(expr) do { (void)(expr); } while (0)
#endif


/* Packed field access macros.*/
/* Assumes data stored in memory is in little endian format.*/
#define PACKED_READ_1BYTE(_address) (*(uint8_t *)(_address))
#define PACKED_WRITE_1BYTE(_address, _value) (*(uint8_t *)(_address) = (_value))

#ifdef ARCHITECTURE_32BIT
 #define PACKED_READ_2BYTE(_address)\
    ( (_address) & 0x01 == 0 ? *(uint16_t *)(_address) : *(uint8_t *)(_address) + (*(uint8_t *)(_address+1) << 8))
 #define PACKED_WRITE_2BYTE(_address, _value)\
 {\
    uint16_t _tmpVal;\
    _tmpVal = _value;\
    if((_address) & 0x01 == 0)\
    {\
       *(uint16_t *)(_address) = (uint16_t)(_tmpVal);\
    }\
    else\
    {\
       *(uint8_t *)(_address)     = (uint8_t)(_tmpVal);\
       *(uint8_t *)((_address)+1) = (uint8_t)((_tmpVal)>>8);\
    }\
 }
 
 #define PACKED_READ_4BYTE(_address)\
    ( (_address) & 0x03 == 0 ? *(uint32_t *)(_address) : *(uint8_t *)(_address) + (*(uint8_t *)(_address+1) << 8) + (*(uint8_t *)(_address+2) << 16) + (*(uint8_t *)(_address+3) << 24 ) )

 #define PACKED_WRITE_4BYTE(_address, _value)\
 {\
   uint32_t _tmpVal;\
   _tmpVal = _value;\
   if(_address & 0x03 == 0)\
   {\
      *(uint32_t *)(_address) = (uint32_t)(_tmpVal);\
   }\
   else\
   {\
      *(uint8_t *)(_address)     = (uint8_t)(_tmpVal);\
      *(uint8_t *)((_address)+1) = (uint8_t)(_tmpVal>>8);\
      *(uint8_t *)((_address)+2) = (uint8_t)(_tmpVal>>16);\
      *(uint8_t *)((_address)+3) = (uint8_t)(_tmpVal>>24);\
   }\
 }                                                        
                                                        
#else
 #define PACKED_READ_2BYTE(_address) (*(uint16_t *)(_address))
 #define PACKED_READ_4BYTE(_address) (*(uint32_t *)(_address))
 #define PACKED_WRITE_2BYTE(_address, _value) (*(uint16_t *)(_address) = (_value))
 #define PACKED_WRITE_4BYTE(_address, _value) (*(uint32_t *)(_address) = (_value))
#endif

/****************************************/
/* Object identities in CPU             */
/****************************************/
typedef uint8_t SYSDEF_ObjectType;

typedef struct
{
   uint32_t   size;
   char     * ref;
}varstring_t;



/*****************/
/*  M A C R O S  */
/*****************/
#ifdef __C51__
   #define SYSDEF_MONITOR_PROTOTYPE
#else
   #define SYSDEF_MONITOR_PROTOTYPE /*monitor*/
#endif

extern ROM_STORAGE(char) product_version[];
extern uint32_t system_capabilities;
extern uint8_t hpi_ctrl_shadow;
           
           






#endif /* #ifndef SYSDEF_H */
