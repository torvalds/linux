/* Header file for the ARM EABI unwinder
   Copyright (C) 2003, 2004, 2005, 2006  Free Software Foundation, Inc.
   Contributed by Paul Brook

   This file is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   In addition to the permissions in the GNU General Public License, the
   Free Software Foundation gives you unlimited permission to link the
   compiled version of this file into combinations with other programs,
   and to distribute those combinations without any restriction coming
   from the use of this file.  (The General Public License restrictions
   do apply in other respects; for example, they cover modification of
   the file, and distribution when not linked into a combine
   executable.)

   This file is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* Language-independent unwinder header public defines.  This contains both
   ABI defined objects, and GNU support routines.  */

#ifndef UNWIND_ARM_H
#define UNWIND_ARM_H

#define __ARM_EABI_UNWINDER__ 1

#ifdef __cplusplus
extern "C" {
#endif
  typedef unsigned _Unwind_Word __attribute__((__mode__(__word__)));
  typedef signed _Unwind_Sword __attribute__((__mode__(__word__)));
  typedef unsigned _Unwind_Ptr __attribute__((__mode__(__pointer__)));
  typedef unsigned _Unwind_Internal_Ptr __attribute__((__mode__(__pointer__)));
  typedef _Unwind_Word _uw;
  typedef unsigned _uw64 __attribute__((mode(__DI__)));
  typedef unsigned _uw16 __attribute__((mode(__HI__)));
  typedef unsigned _uw8 __attribute__((mode(__QI__)));

  typedef enum
    {
      _URC_OK = 0,       /* operation completed successfully */
      _URC_FOREIGN_EXCEPTION_CAUGHT = 1,
      _URC_END_OF_STACK = 5,
      _URC_HANDLER_FOUND = 6,
      _URC_INSTALL_CONTEXT = 7,
      _URC_CONTINUE_UNWIND = 8,
      _URC_FAILURE = 9   /* unspecified failure of some kind */
    }
  _Unwind_Reason_Code;

  typedef enum
    {
      _US_VIRTUAL_UNWIND_FRAME = 0,
      _US_UNWIND_FRAME_STARTING = 1,
      _US_UNWIND_FRAME_RESUME = 2,
      _US_ACTION_MASK = 3,
      _US_FORCE_UNWIND = 8,
      _US_END_OF_STACK = 16
    }
  _Unwind_State;

  /* Provided only for for compatibility with existing code.  */
  typedef int _Unwind_Action;
#define _UA_SEARCH_PHASE	1
#define _UA_CLEANUP_PHASE	2
#define _UA_HANDLER_FRAME	4
#define _UA_FORCE_UNWIND	8
#define _UA_END_OF_STACK	16
#define _URC_NO_REASON 	_URC_OK

  typedef struct _Unwind_Control_Block _Unwind_Control_Block;
  typedef struct _Unwind_Context _Unwind_Context;
  typedef _uw _Unwind_EHT_Header;


  /* UCB: */

  struct _Unwind_Control_Block
    {
      char exception_class[8];
      void (*exception_cleanup)(_Unwind_Reason_Code, _Unwind_Control_Block *);
      /* Unwinder cache, private fields for the unwinder's use */
      struct
	{
	  _uw reserved1;  /* Forced unwind stop fn, 0 if not forced */
	  _uw reserved2;  /* Personality routine address */
	  _uw reserved3;  /* Saved callsite address */
	  _uw reserved4;  /* Forced unwind stop arg */
	  _uw reserved5;
	}
      unwinder_cache;
      /* Propagation barrier cache (valid after phase 1): */
      struct
	{
	  _uw sp;
	  _uw bitpattern[5];
	}
      barrier_cache;
      /* Cleanup cache (preserved over cleanup): */
      struct
	{
	  _uw bitpattern[4];
	}
      cleanup_cache;
      /* Pr cache (for pr's benefit): */
      struct
	{
	  _uw fnstart;			/* function start address */
	  _Unwind_EHT_Header *ehtp;	/* pointer to EHT entry header word */
	  _uw additional;		/* additional data */
	  _uw reserved1;
	}
      pr_cache;
      long long int :0;	/* Force alignment to 8-byte boundary */
    };

  /* Virtual Register Set*/

  typedef enum
    {
      _UVRSC_CORE = 0,      /* integer register */
      _UVRSC_VFP = 1,       /* vfp */
      _UVRSC_FPA = 2,       /* fpa */
      _UVRSC_WMMXD = 3,     /* Intel WMMX data register */
      _UVRSC_WMMXC = 4      /* Intel WMMX control register */
    }
  _Unwind_VRS_RegClass;

  typedef enum
    {
      _UVRSD_UINT32 = 0,
      _UVRSD_VFPX = 1,
      _UVRSD_FPAX = 2,
      _UVRSD_UINT64 = 3,
      _UVRSD_FLOAT = 4,
      _UVRSD_DOUBLE = 5
    }
  _Unwind_VRS_DataRepresentation;

  typedef enum
    {
      _UVRSR_OK = 0,
      _UVRSR_NOT_IMPLEMENTED = 1,
      _UVRSR_FAILED = 2
    }
  _Unwind_VRS_Result;

  /* Frame unwinding state.  */
  typedef struct
    {
      /* The current word (bytes packed msb first).  */
      _uw data;
      /* Pointer to the next word of data.  */
      _uw *next;
      /* The number of bytes left in this word.  */
      _uw8 bytes_left;
      /* The number of words pointed to by ptr.  */
      _uw8 words_left;
    }
  __gnu_unwind_state;

  typedef _Unwind_Reason_Code (*personality_routine) (_Unwind_State,
      _Unwind_Control_Block *, _Unwind_Context *);

  _Unwind_VRS_Result _Unwind_VRS_Set(_Unwind_Context *, _Unwind_VRS_RegClass,
                                     _uw, _Unwind_VRS_DataRepresentation,
                                     void *);

  _Unwind_VRS_Result _Unwind_VRS_Get(_Unwind_Context *, _Unwind_VRS_RegClass,
                                     _uw, _Unwind_VRS_DataRepresentation,
                                     void *);

  _Unwind_VRS_Result _Unwind_VRS_Pop(_Unwind_Context *, _Unwind_VRS_RegClass,
                                     _uw, _Unwind_VRS_DataRepresentation);


  /* Support functions for the PR.  */
#define _Unwind_Exception _Unwind_Control_Block
  typedef char _Unwind_Exception_Class[8];

  void * _Unwind_GetLanguageSpecificData (_Unwind_Context *);
  _Unwind_Ptr _Unwind_GetRegionStart (_Unwind_Context *);

  /* These two should never be used.  */
  _Unwind_Ptr _Unwind_GetDataRelBase (_Unwind_Context *);
  _Unwind_Ptr _Unwind_GetTextRelBase (_Unwind_Context *);

  /* Interface functions: */
  _Unwind_Reason_Code _Unwind_RaiseException(_Unwind_Control_Block *ucbp);
  void __attribute__((noreturn)) _Unwind_Resume(_Unwind_Control_Block *ucbp);
  _Unwind_Reason_Code _Unwind_Resume_or_Rethrow (_Unwind_Control_Block *ucbp);

  typedef _Unwind_Reason_Code (*_Unwind_Stop_Fn)
       (int, _Unwind_Action, _Unwind_Exception_Class,
	_Unwind_Control_Block *, struct _Unwind_Context *, void *);
  _Unwind_Reason_Code _Unwind_ForcedUnwind (_Unwind_Control_Block *,
					    _Unwind_Stop_Fn, void *);
  _Unwind_Word _Unwind_GetCFA (struct _Unwind_Context *);
  void _Unwind_Complete(_Unwind_Control_Block *ucbp);
  void _Unwind_DeleteException (_Unwind_Exception *);

  _Unwind_Reason_Code __gnu_unwind_frame (_Unwind_Control_Block *,
					  _Unwind_Context *);
  _Unwind_Reason_Code __gnu_unwind_execute (_Unwind_Context *,
					    __gnu_unwind_state *);

  /* Decode an R_ARM_TARGET2 relocation.  */
  static inline _Unwind_Word
  _Unwind_decode_target2 (_Unwind_Word ptr)
    {
      _Unwind_Word tmp;

      tmp = *(_Unwind_Word *) ptr;
      /* Zero values are always NULL.  */
      if (!tmp)
	return 0;

#if defined(linux) || defined(__NetBSD__) || defined(__OpenBSD__)
      /* Pc-relative indirect.  */
      tmp += ptr;
      tmp = *(_Unwind_Word *) tmp;
#elif defined(__symbian__)
      /* Absolute pointer.  Nothing more to do.  */
#else
      /* Pc-relative pointer.  */
      tmp += ptr;
#endif
      return tmp;
    }

  static inline _Unwind_Word
  _Unwind_GetGR (_Unwind_Context *context, int regno)
    {
      _uw val;
      _Unwind_VRS_Get (context, _UVRSC_CORE, regno, _UVRSD_UINT32, &val);
      return val;
    }

  /* Return the address of the instruction, not the actual IP value.  */
#define _Unwind_GetIP(context) \
  (_Unwind_GetGR (context, 15) & ~(_Unwind_Word)1)

#define _Unwind_GetIPInfo(context, ip_before_insn) \
  (*ip_before_insn = 0, _Unwind_GetGR (context, 15) & ~(_Unwind_Word)1)

  static inline void
  _Unwind_SetGR (_Unwind_Context *context, int regno, _Unwind_Word val)
    {
      _Unwind_VRS_Set (context, _UVRSC_CORE, regno, _UVRSD_UINT32, &val);
    }

  /* The dwarf unwinder doesn't understand arm/thumb state.  We assume the
     landing pad uses the same instruction set as the call site.  */
#define _Unwind_SetIP(context, val) \
  _Unwind_SetGR (context, 15, val | (_Unwind_GetGR (context, 15) & 1))

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif /* defined UNWIND_ARM_H */
