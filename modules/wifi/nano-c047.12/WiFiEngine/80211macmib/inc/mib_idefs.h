/******************************************************************************

            Copyright (c) 2004 by Nanoradio AB 

This software is copyrighted by and is the sole property of Nanoradio AB.
All rights, title, ownership, or other interests in the software remain the
property of Nanoradio AB.  This software may only be used in accordance
with the corresponding license agreement.  Any unauthorized use, duplication,
transmission, distribution, or disclosure of this software is expressly forbidden.

This Copyright notice may not be removed or modified without prior written
consent of Nanoradio AB.

Nanoradio AB reserves the right to modify this software without notice.

Nanoradio AB 
Torshamnsgatan 39                       info@nanoradio.se
164 40 Kista                            http://www.nanoradio.se
SWEDEN

--------------------------------------------------------------------
$Workfile:   mib_idefs.h  $
$Revision: 1.9 $
--------------------------------------------------------------------

Module Description :
==================

This header file holds definitions for related to MIB database.


Revision History:
=================
 * Initial revision coefws.
******************************************************************************/
#ifndef MAC_MIB_IDEFS_H
#define MAC_MIB_IDEFS_H
#include "sysdef.h"

/******************************************************************************
M A C R O S   &   D E F I N E S
******************************************************************************/
#define MIB_OBJECT_SIZE_SIZE                  10
#define MIB_OBJECT_SIZE_OFFSET                0
#define MIB_OBJECT_SIZE_EVFUNC_OFFSET         22
#define MIB_OBJECT_SIZE_MASK           ((((uintptr_t)(1<<MIB_OBJECT_SIZE_SIZE)-1)) << MIB_OBJECT_SIZE_OFFSET)
#define MIB_OBJECT_SIZE_EVFUNC_MASK    (((uintptr_t)((1<<MIB_OBJECT_SIZE_SIZE)-1)) << MIB_OBJECT_SIZE_EVFUNC_OFFSET)

#define MIB_OBJECT_EV_FUNC_FUNC_SIZE          22
#define MIB_OBJECT_EV_FUNC_FUNC_OFFSET        0
#define MIB_OBJECT_EV_FUNC_FUNC_MASK          (((1<<MIB_OBJECT_EV_FUNC_FUNC_SIZE)-1)<<MIB_OBJECT_EV_FUNC_FUNC_OFFSET)

#define MIB_OBJECT_EV_FUNC_MEMREF_SIZE        22
#define MIB_OBJECT_EV_FUNC_MEMREF_OFFSET      0
#define MIB_OBJECT_EV_FUNC_MEMREF_MASK        (((1<<MIB_OBJECT_EV_FUNC_MEMREF_SIZE)-1)<<MIB_OBJECT_EV_FUNC_MEMREF_OFFSET)


#define MIB_COLUMNAR_FLAG_OFFSET       23

#define MIB_EV_FUNC_FLAG_OFFSET        22

#define MIB_OBJECT_SIZE_DESCRIPTION_SIZE   3
#define MIB_OBJECT_SIZE_DESCRIPTION_OFFSET 24
#define MIB_OBJECT_SIZE_DESCRIPTION_MASK   (((1<<MIB_OBJECT_SIZE_DESCRIPTION_SIZE)-1)<<MIB_OBJECT_SIZE_DESCRIPTION_OFFSET)

#define MIB_REFERENCE_TYPE_SIZE   3
#define MIB_REFERENCE_TYPE_OFFSET 27
#define MIB_REFERENCE_TYPE_MASK   (((1<<MIB_REFERENCE_TYPE_SIZE)-1)<<MIB_REFERENCE_TYPE_OFFSET)

#define MIB_ACCESS_READ_OFFSET   30
#define MIB_ACCESS_WRITE_OFFSET  31


#define MIB_CHECK_READ_PERMISSION(_object)\
        ((((uintptr_t)(_object)->storage_description) & ((uint32_t)1 << MIB_ACCESS_READ_OFFSET)) != 0)

#define MIB_CHECK_WRITE_PERMISSION(_object)\
        ((((uintptr_t)(_object)->storage_description) & ((uint32_t)1 << MIB_ACCESS_WRITE_OFFSET)) != 0)

#define MIB_CHECK_EV_FUNC(_object)\
        ((((uintptr_t)(_object)->storage_description) & ((uint32_t)1 << MIB_EV_FUNC_FLAG_OFFSET)) != 0)


#define MIB_TABLE_EV_POINTER_WIN_XP ,0


#define MIB_ENTRY_TABLE_REFERENCE(_table)\
        (char *)&(_table),\
        (char *)(sizeof(_table)/sizeof(mib_object_entry_t) |\
        (MIB_OBJECT_REFERENCE_TYPE_MIB_TABLE << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_FIXED_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET))

#define MIB_ENTRY_SUBTABLE_REFERENCE(_table)\
        (char *)(_table),\
        (char *)(sizeof(_table)/sizeof(mib_object_entry_t) |\
        (MIB_OBJECT_REFERENCE_TYPE_MIB_SUBTABLE << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_FIXED_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET) |\
        (1 << MIB_ACCESS_WRITE_OFFSET))

#define MIB_ENTRY_COLUMNAR_TABLE_REFERENCE(_table)\
        (char *)&(_table),\
        (char *)(sizeof(_table)/sizeof(mib_object_entry_t) |\
        (1 << MIB_COLUMNAR_FLAG_OFFSET) |\
        (MIB_OBJECT_REFERENCE_TYPE_MIB_TABLE << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_FIXED_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET))

#define MIB_ENTRY_REFERENCE_MEMORY_FIXED_SIZE_RW(_memref, _size)\
        (char *)(uintptr_t)(_memref),\
        (char *)((_size) |\
        (MIB_OBJECT_REFERENCE_TYPE_MEMORY << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_FIXED_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET) |\
        (1 << MIB_ACCESS_WRITE_OFFSET))

#define MIB_ENTRY_REFERENCE_MEMORY_EXACT_SIZE_RW(_memref, _size)\
        (char *)(uintptr_t)(_memref),\
        (char *)((_size) |\
        (MIB_OBJECT_REFERENCE_TYPE_MEMORY << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_EXACT_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET) |\
        (1 << MIB_ACCESS_WRITE_OFFSET))




#if defined (WIN_SIMULATION)

#define MIB_ENTRY_REFERENCE_MEMORY_EV_FUNC_EXACT_SIZE_RW(_memref, _func, _size)\
        ((char *)(uintptr_t)(_memref)),\
        (char *)((_size) |\
        (MIB_OBJECT_REFERENCE_TYPE_MEMORY << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_EXACT_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET) |\
        (1 << MIB_EV_FUNC_FLAG_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET)  |\
        (1 << MIB_ACCESS_WRITE_OFFSET)),\
        ((char *)(uintptr_t)(_func))


#define MIB_ENTRY_REFERENCE_MEMORY_EV_FUNC_FIXED_SIZE_RW(_memref, _func, _size)\
        ((char *)(uintptr_t)(_memref)),\
        (char *)((_size) |\
        (MIB_OBJECT_REFERENCE_TYPE_MEMORY << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_FIXED_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET) |\
        (1 << MIB_EV_FUNC_FLAG_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET)  |\
        (1 << MIB_ACCESS_WRITE_OFFSET)),\
        ((char *)(uintptr_t)(_func))

#else

#define MIB_ENTRY_REFERENCE_MEMORY_EV_FUNC_EXACT_SIZE_RW(_memref, _func, _size)\
        ((char *)(uintptr_t)(_memref)) + ((_size) << MIB_OBJECT_SIZE_EVFUNC_OFFSET)\
        ,\
        ((char *)(uintptr_t)(_func))  +\
        ((MIB_OBJECT_REFERENCE_TYPE_MEMORY << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_EXACT_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET) |\
        (1 << MIB_EV_FUNC_FLAG_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET)  |\
        (1 << MIB_ACCESS_WRITE_OFFSET) | 1)


#define MIB_ENTRY_REFERENCE_MEMORY_EV_FUNC_FIXED_SIZE_RW(_memref, _func, _size)\
        ((char *)(uintptr_t)(_memref)) + ((_size) << MIB_OBJECT_SIZE_EVFUNC_OFFSET)\
        ,\
        ((char *)(uintptr_t)(_func))  +\
        ((MIB_OBJECT_REFERENCE_TYPE_MEMORY << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_FIXED_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET) |\
        (1 << MIB_EV_FUNC_FLAG_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET)  |\
        (1 << MIB_ACCESS_WRITE_OFFSET) | 1) 

#endif



#define MIB_ENTRY_REFERENCE_MEMORY_FIXED_SIZE_RO(_memref, _size)\
        (char *)(uintptr_t)(_memref),\
        (char *)((_size) |\
        (MIB_OBJECT_REFERENCE_TYPE_MEMORY << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_FIXED_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET))

#define MIB_ENTRY_REFERENCE_MEMORY_NULL_TERMINATED_STRING_RO(_memref, _max_size)\
        (char *)(uintptr_t)(_memref),\
        (char *)((_max_size) |\
        (MIB_OBJECT_REFERENCE_TYPE_MEMORY << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_NULL_TERMINATED_STRING << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET))

#define MIB_ENTRY_REFERENCE_FUNCTION_FIXED_SIZE_RW(_func, _size)\
        (char *)(uintptr_t)(_func),\
        (char *)((_size) |\
        (MIB_OBJECT_REFERENCE_TYPE_FUNCTION << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_FIXED_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET) |\
        (1 << MIB_ACCESS_WRITE_OFFSET))

#define MIB_ENTRY_REFERENCE_FUNCTION_EXACT_SIZE_RW(_func, _size)\
        (char *)(uintptr_t)(_func),\
        (char *)((_size) |\
        (MIB_OBJECT_REFERENCE_TYPE_FUNCTION << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_EXACT_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET) |\
        (1 << MIB_ACCESS_WRITE_OFFSET))

#define MIB_ENTRY_REFERENCE_FUNCTION_IMPLICIT_SIZE_RW(_func, _max_size)\
        (char *)(uintptr_t)(_func),\
        (char *)((uint32_t)(_max_size) |\
        (MIB_OBJECT_REFERENCE_TYPE_FUNCTION << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_IMPLICIT_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET) |\
        (1 << MIB_ACCESS_WRITE_OFFSET))

#define MIB_ENTRY_REFERENCE_IMMEDIATE(_val, _size)\
        (char *)(uintptr_t)(_val),\
        (char *)((_size) |\
        (MIB_OBJECT_REFERENCE_TYPE_IMMEDIATE << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_FIXED_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET))


#define MIB_ENTRY_NOT_SUPPORTED\
        (char *)(DUMMY),\
        (char *)((MIB_OBJECT_REFERENCE_TYPE_UNSPECIFIED << MIB_REFERENCE_TYPE_OFFSET))


#define MIB_ENTRY_REFERENCE_IE_RO(_memref)\
        (char *)(uintptr_t)(_memref),\
        (char *)((MIB_OBJECT_REFERENCE_TYPE_IE << MIB_REFERENCE_TYPE_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET))


#define MIB_ENTRY_REFERENCE_IE_IMPLICIT_SIZE_RW(_memref, _max_size)\
        (char *)(uintptr_t)(_memref),\
        (char *)((_max_size) |\
        (MIB_OBJECT_REFERENCE_TYPE_IE << MIB_REFERENCE_TYPE_OFFSET) |\
        (MIB_OBJECT_SIZE_DESCRIPTION_IMPLICIT_SIZE << MIB_OBJECT_SIZE_DESCRIPTION_OFFSET) |\
        (1 << MIB_ACCESS_READ_OFFSET) |\
        (1 << MIB_ACCESS_WRITE_OFFSET))





        
/******************************************************************************
C O N S T A N T S
******************************************************************************/


/******************************************************************************
T Y P E D E F ' S
******************************************************************************/

/*
   Object reference descriptor bit encoding

                i rrr aaaaaaaaaaaaaaaaaaaa      
                ^ ^   ^
                | |   |
                | |    ------------- address reference     (bit 0-19)
                |  ----------------- reference type        (bit 20-22)
                 ------------------- indexed reference     (bit 23)
 


   Object storage descriptor bit encoding

    type "unspecified"

          tttuuuuuuuuuuuuuuuuuuuuuuuuuuuuu
          ^  ^      
          |  |      
          |   ---------------------- unspecifed      (bit 0-28)
           ------------------------- type            (bit 29-31)

    type "fixed size"

          tttuuooooooooooooossssssssssssss
          ^    ^            ^
          |    |            |
          |    |             ------- size            (bit 0-13)
          |     -------------------- indexed offset  (bit 14 - 27)
           ------------------------- type            (bit 29-31)

     type "information element"

          tttuuooooooooooooommmmmmmmmmmmmm
          ^    ^            ^
          |    |            |
          |    |             ------- max size        (bit 0-13)
          |     -------------------- indexed offset  (bit 14 - 27)
           ------------------------- type            (bit 29-31)

     type "null terminated string"

          tttuuooooooooooooommmmmmmmmmmmmm
          ^    ^            ^
          |    |            |
          |    |             ------- max size        (bit 0-13)
          |     -------------------- indexed offset  (bit 14 - 27)
           ------------------------- type            (bit 29-31)

*/
typedef enum
{
   MIB_OBJECT_REFERENCE_TYPE_UNSPECIFIED,
   MIB_OBJECT_REFERENCE_TYPE_MIB_TABLE,
   MIB_OBJECT_REFERENCE_TYPE_MIB_OBJECT,
   MIB_OBJECT_REFERENCE_TYPE_FUNCTION,
   MIB_OBJECT_REFERENCE_TYPE_MEMORY,
   MIB_OBJECT_REFERENCE_TYPE_IMMEDIATE,
   MIB_OBJECT_REFERENCE_TYPE_IE,
   MIB_OBJECT_REFERENCE_TYPE_MIB_SUBTABLE,
   MIB_OBJECT_REFERENCE_TYPE_NUM_TYPES
}mib_object_reference_type_t;

typedef enum
{
   MIB_OBJECT_SIZE_DESCRIPTION_UNSPECIFIED,
   MIB_OBJECT_SIZE_DESCRIPTION_IMPLICIT_SIZE,
   MIB_OBJECT_SIZE_DESCRIPTION_FIXED_SIZE,
   MIB_OBJECT_SIZE_DESCRIPTION_EXACT_SIZE,
   MIB_OBJECT_SIZE_DESCRIPTION_NULL_TERMINATED_STRING,
   MIB_OBJECT_SIZE_DESCRIPTION_NUM_TYPES
}mib_object_size_description_t;

#endif /* #ifndef MAC_MIB_IDEFS_H */
/* E N D  O F  F I L E *******************************************************/

