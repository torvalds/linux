/******************************************************************************
 *
 * Name: acmacros.h - C macros for the entire subsystem.
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2006, R. Byron Moore
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef __ACMACROS_H__
#define __ACMACROS_H__

/*
 * Data manipulation macros
 */
#define ACPI_LOWORD(l)                  ((u16)(u32)(l))
#define ACPI_HIWORD(l)                  ((u16)((((u32)(l)) >> 16) & 0xFFFF))
#define ACPI_LOBYTE(l)                  ((u8)(u16)(l))
#define ACPI_HIBYTE(l)                  ((u8)((((u16)(l)) >> 8) & 0xFF))

#define ACPI_SET_BIT(target,bit)        ((target) |= (bit))
#define ACPI_CLEAR_BIT(target,bit)      ((target) &= ~(bit))
#define ACPI_MIN(a,b)                   (((a)<(b))?(a):(b))

/* Size calculation */

#define ACPI_ARRAY_LENGTH(x)            (sizeof(x) / sizeof((x)[0]))

#if ACPI_MACHINE_WIDTH == 16

/*
 * For 16-bit addresses, we have to assume that the upper 32 bits
 * (out of 64) are zero.
 */
#define ACPI_LODWORD(l)                 ((u32)(l))
#define ACPI_HIDWORD(l)                 ((u32)(0))

#define ACPI_GET_ADDRESS(a)             ((a).lo)
#define ACPI_STORE_ADDRESS(a,b)         {(a).hi=0;(a).lo=(u32)(b);}
#define ACPI_VALID_ADDRESS(a)           ((a).hi | (a).lo)

#else
#ifdef ACPI_NO_INTEGER64_SUPPORT
/*
 * acpi_integer is 32-bits, no 64-bit support on this platform
 */
#define ACPI_LODWORD(l)                 ((u32)(l))
#define ACPI_HIDWORD(l)                 ((u32)(0))

#define ACPI_GET_ADDRESS(a)             (a)
#define ACPI_STORE_ADDRESS(a,b)         ((a)=(b))
#define ACPI_VALID_ADDRESS(a)           (a)

#else

/*
 * Full 64-bit address/integer on both 32-bit and 64-bit platforms
 */
#define ACPI_LODWORD(l)                 ((u32)(u64)(l))
#define ACPI_HIDWORD(l)                 ((u32)(((*(struct uint64_struct *)(void *)(&l))).hi))

#define ACPI_GET_ADDRESS(a)             (a)
#define ACPI_STORE_ADDRESS(a,b)         ((a)=(acpi_physical_address)(b))
#define ACPI_VALID_ADDRESS(a)           (a)
#endif
#endif

/*
 * printf() format helpers
 */

/* Split 64-bit integer into two 32-bit values. Use with %8.8X%8.8X */

#define ACPI_FORMAT_UINT64(i)           ACPI_HIDWORD(i),ACPI_LODWORD(i)

/*
 * Extract data using a pointer.  Any more than a byte and we
 * get into potential aligment issues -- see the STORE macros below.
 * Use with care.
 */
#define ACPI_GET8(ptr)                  *ACPI_CAST_PTR (u8, ptr)
#define ACPI_GET16(ptr)                 *ACPI_CAST_PTR (u16, ptr)
#define ACPI_GET32(ptr)                 *ACPI_CAST_PTR (u32, ptr)
#define ACPI_GET64(ptr)                 *ACPI_CAST_PTR (u64, ptr)
#define ACPI_SET8(ptr)                  *ACPI_CAST_PTR (u8, ptr)
#define ACPI_SET16(ptr)                 *ACPI_CAST_PTR (u16, ptr)
#define ACPI_SET32(ptr)                 *ACPI_CAST_PTR (u32, ptr)
#define ACPI_SET64(ptr)                 *ACPI_CAST_PTR (u64, ptr)

/*
 * Pointer manipulation
 */
#define ACPI_CAST_PTR(t, p)             ((t *) (acpi_uintptr_t) (p))
#define ACPI_CAST_INDIRECT_PTR(t, p)    ((t **) (acpi_uintptr_t) (p))
#define ACPI_ADD_PTR(t,a,b)             ACPI_CAST_PTR (t, (ACPI_CAST_PTR (u8,(a)) + (acpi_native_uint)(b)))
#define ACPI_PTR_DIFF(a,b)              (acpi_native_uint) (ACPI_CAST_PTR (u8,(a)) - ACPI_CAST_PTR (u8,(b)))

/* Pointer/Integer type conversions */

#define ACPI_TO_POINTER(i)              ACPI_ADD_PTR (void,(void *) NULL,(acpi_native_uint) i)
#define ACPI_TO_INTEGER(p)              ACPI_PTR_DIFF (p,(void *) NULL)
#define ACPI_OFFSET(d,f)                (acpi_size) ACPI_PTR_DIFF (&(((d *)0)->f),(void *) NULL)

#if ACPI_MACHINE_WIDTH == 16
#define ACPI_STORE_POINTER(d,s)         ACPI_MOVE_32_TO_32(d,s)
#define ACPI_PHYSADDR_TO_PTR(i)         (void *)(i)
#define ACPI_PTR_TO_PHYSADDR(i)         (u32) ACPI_CAST_PTR (u8,(i))
#else
#define ACPI_PHYSADDR_TO_PTR(i)         ACPI_TO_POINTER(i)
#define ACPI_PTR_TO_PHYSADDR(i)         ACPI_TO_INTEGER(i)
#endif

#ifndef ACPI_MISALIGNMENT_NOT_SUPPORTED
#define ACPI_COMPARE_NAME(a,b)          (*ACPI_CAST_PTR (u32,(a)) == *ACPI_CAST_PTR (u32,(b)))
#else
#define ACPI_COMPARE_NAME(a,b)          (!ACPI_STRNCMP (ACPI_CAST_PTR (char,(a)), ACPI_CAST_PTR (char,(b)), ACPI_NAME_SIZE))
#endif

/*
 * Macros for moving data around to/from buffers that are possibly unaligned.
 * If the hardware supports the transfer of unaligned data, just do the store.
 * Otherwise, we have to move one byte at a time.
 */
#ifdef ACPI_BIG_ENDIAN
/*
 * Macros for big-endian machines
 */

/* This macro sets a buffer index, starting from the end of the buffer */

#define ACPI_BUFFER_INDEX(buf_len,buf_offset,byte_gran) ((buf_len) - (((buf_offset)+1) * (byte_gran)))

/* These macros reverse the bytes during the move, converting little-endian to big endian */

	 /* Big Endian      <==        Little Endian */
	 /*  Hi...Lo                     Lo...Hi     */
/* 16-bit source, 16/32/64 destination */

#define ACPI_MOVE_16_TO_16(d,s)         {((  u8 *)(void *)(d))[0] = ((u8 *)(void *)(s))[1];\
			  ((  u8 *)(void *)(d))[1] = ((u8 *)(void *)(s))[0];}

#define ACPI_MOVE_16_TO_32(d,s)         {(*(u32 *)(void *)(d))=0;\
					  ((u8 *)(void *)(d))[2] = ((u8 *)(void *)(s))[1];\
					  ((u8 *)(void *)(d))[3] = ((u8 *)(void *)(s))[0];}

#define ACPI_MOVE_16_TO_64(d,s)         {(*(u64 *)(void *)(d))=0;\
							   ((u8 *)(void *)(d))[6] = ((u8 *)(void *)(s))[1];\
							   ((u8 *)(void *)(d))[7] = ((u8 *)(void *)(s))[0];}

/* 32-bit source, 16/32/64 destination */

#define ACPI_MOVE_32_TO_16(d,s)         ACPI_MOVE_16_TO_16(d,s)	/* Truncate to 16 */

#define ACPI_MOVE_32_TO_32(d,s)         {((  u8 *)(void *)(d))[0] = ((u8 *)(void *)(s))[3];\
									  ((  u8 *)(void *)(d))[1] = ((u8 *)(void *)(s))[2];\
									  ((  u8 *)(void *)(d))[2] = ((u8 *)(void *)(s))[1];\
									  ((  u8 *)(void *)(d))[3] = ((u8 *)(void *)(s))[0];}

#define ACPI_MOVE_32_TO_64(d,s)         {(*(u64 *)(void *)(d))=0;\
										   ((u8 *)(void *)(d))[4] = ((u8 *)(void *)(s))[3];\
										   ((u8 *)(void *)(d))[5] = ((u8 *)(void *)(s))[2];\
										   ((u8 *)(void *)(d))[6] = ((u8 *)(void *)(s))[1];\
										   ((u8 *)(void *)(d))[7] = ((u8 *)(void *)(s))[0];}

/* 64-bit source, 16/32/64 destination */

#define ACPI_MOVE_64_TO_16(d,s)         ACPI_MOVE_16_TO_16(d,s)	/* Truncate to 16 */

#define ACPI_MOVE_64_TO_32(d,s)         ACPI_MOVE_32_TO_32(d,s)	/* Truncate to 32 */

#define ACPI_MOVE_64_TO_64(d,s)         {((  u8 *)(void *)(d))[0] = ((u8 *)(void *)(s))[7];\
										 ((  u8 *)(void *)(d))[1] = ((u8 *)(void *)(s))[6];\
										 ((  u8 *)(void *)(d))[2] = ((u8 *)(void *)(s))[5];\
										 ((  u8 *)(void *)(d))[3] = ((u8 *)(void *)(s))[4];\
										 ((  u8 *)(void *)(d))[4] = ((u8 *)(void *)(s))[3];\
										 ((  u8 *)(void *)(d))[5] = ((u8 *)(void *)(s))[2];\
										 ((  u8 *)(void *)(d))[6] = ((u8 *)(void *)(s))[1];\
										 ((  u8 *)(void *)(d))[7] = ((u8 *)(void *)(s))[0];}
#else
/*
 * Macros for little-endian machines
 */

/* This macro sets a buffer index, starting from the beginning of the buffer */

#define ACPI_BUFFER_INDEX(buf_len,buf_offset,byte_gran) (buf_offset)

#ifndef ACPI_MISALIGNMENT_NOT_SUPPORTED

/* The hardware supports unaligned transfers, just do the little-endian move */

#if ACPI_MACHINE_WIDTH == 16

/* No 64-bit integers */
/* 16-bit source, 16/32/64 destination */

#define ACPI_MOVE_16_TO_16(d,s)         *(u16 *)(void *)(d) = *(u16 *)(void *)(s)
#define ACPI_MOVE_16_TO_32(d,s)         *(u32 *)(void *)(d) = *(u16 *)(void *)(s)
#define ACPI_MOVE_16_TO_64(d,s)         ACPI_MOVE_16_TO_32(d,s)

/* 32-bit source, 16/32/64 destination */

#define ACPI_MOVE_32_TO_16(d,s)         ACPI_MOVE_16_TO_16(d,s)	/* Truncate to 16 */
#define ACPI_MOVE_32_TO_32(d,s)         *(u32 *)(void *)(d) = *(u32 *)(void *)(s)
#define ACPI_MOVE_32_TO_64(d,s)         ACPI_MOVE_32_TO_32(d,s)

/* 64-bit source, 16/32/64 destination */

#define ACPI_MOVE_64_TO_16(d,s)         ACPI_MOVE_16_TO_16(d,s)	/* Truncate to 16 */
#define ACPI_MOVE_64_TO_32(d,s)         ACPI_MOVE_32_TO_32(d,s)	/* Truncate to 32 */
#define ACPI_MOVE_64_TO_64(d,s)         ACPI_MOVE_32_TO_32(d,s)

#else
/* 16-bit source, 16/32/64 destination */

#define ACPI_MOVE_16_TO_16(d,s)         *(u16 *)(void *)(d) = *(u16 *)(void *)(s)
#define ACPI_MOVE_16_TO_32(d,s)         *(u32 *)(void *)(d) = *(u16 *)(void *)(s)
#define ACPI_MOVE_16_TO_64(d,s)         *(u64 *)(void *)(d) = *(u16 *)(void *)(s)

/* 32-bit source, 16/32/64 destination */

#define ACPI_MOVE_32_TO_16(d,s)         ACPI_MOVE_16_TO_16(d,s)	/* Truncate to 16 */
#define ACPI_MOVE_32_TO_32(d,s)         *(u32 *)(void *)(d) = *(u32 *)(void *)(s)
#define ACPI_MOVE_32_TO_64(d,s)         *(u64 *)(void *)(d) = *(u32 *)(void *)(s)

/* 64-bit source, 16/32/64 destination */

#define ACPI_MOVE_64_TO_16(d,s)         ACPI_MOVE_16_TO_16(d,s)	/* Truncate to 16 */
#define ACPI_MOVE_64_TO_32(d,s)         ACPI_MOVE_32_TO_32(d,s)	/* Truncate to 32 */
#define ACPI_MOVE_64_TO_64(d,s)         *(u64 *)(void *)(d) = *(u64 *)(void *)(s)
#endif

#else
/*
 * The hardware does not support unaligned transfers.  We must move the
 * data one byte at a time.  These macros work whether the source or
 * the destination (or both) is/are unaligned.  (Little-endian move)
 */

/* 16-bit source, 16/32/64 destination */

#define ACPI_MOVE_16_TO_16(d,s)         {((  u8 *)(void *)(d))[0] = ((u8 *)(void *)(s))[0];\
										 ((  u8 *)(void *)(d))[1] = ((u8 *)(void *)(s))[1];}

#define ACPI_MOVE_16_TO_32(d,s)         {(*(u32 *)(void *)(d)) = 0; ACPI_MOVE_16_TO_16(d,s);}
#define ACPI_MOVE_16_TO_64(d,s)         {(*(u64 *)(void *)(d)) = 0; ACPI_MOVE_16_TO_16(d,s);}

/* 32-bit source, 16/32/64 destination */

#define ACPI_MOVE_32_TO_16(d,s)         ACPI_MOVE_16_TO_16(d,s)	/* Truncate to 16 */

#define ACPI_MOVE_32_TO_32(d,s)         {((  u8 *)(void *)(d))[0] = ((u8 *)(void *)(s))[0];\
										 ((  u8 *)(void *)(d))[1] = ((u8 *)(void *)(s))[1];\
										 ((  u8 *)(void *)(d))[2] = ((u8 *)(void *)(s))[2];\
										 ((  u8 *)(void *)(d))[3] = ((u8 *)(void *)(s))[3];}

#define ACPI_MOVE_32_TO_64(d,s)         {(*(u64 *)(void *)(d)) = 0; ACPI_MOVE_32_TO_32(d,s);}

/* 64-bit source, 16/32/64 destination */

#define ACPI_MOVE_64_TO_16(d,s)         ACPI_MOVE_16_TO_16(d,s)	/* Truncate to 16 */
#define ACPI_MOVE_64_TO_32(d,s)         ACPI_MOVE_32_TO_32(d,s)	/* Truncate to 32 */
#define ACPI_MOVE_64_TO_64(d,s)         {((  u8 *)(void *)(d))[0] = ((u8 *)(void *)(s))[0];\
										 ((  u8 *)(void *)(d))[1] = ((u8 *)(void *)(s))[1];\
										 ((  u8 *)(void *)(d))[2] = ((u8 *)(void *)(s))[2];\
										 ((  u8 *)(void *)(d))[3] = ((u8 *)(void *)(s))[3];\
										 ((  u8 *)(void *)(d))[4] = ((u8 *)(void *)(s))[4];\
										 ((  u8 *)(void *)(d))[5] = ((u8 *)(void *)(s))[5];\
										 ((  u8 *)(void *)(d))[6] = ((u8 *)(void *)(s))[6];\
										 ((  u8 *)(void *)(d))[7] = ((u8 *)(void *)(s))[7];}
#endif
#endif

/* Macros based on machine integer width */

#if ACPI_MACHINE_WIDTH == 16
#define ACPI_MOVE_SIZE_TO_16(d,s)       ACPI_MOVE_16_TO_16(d,s)

#elif ACPI_MACHINE_WIDTH == 32
#define ACPI_MOVE_SIZE_TO_16(d,s)       ACPI_MOVE_32_TO_16(d,s)

#elif ACPI_MACHINE_WIDTH == 64
#define ACPI_MOVE_SIZE_TO_16(d,s)       ACPI_MOVE_64_TO_16(d,s)

#else
#error unknown ACPI_MACHINE_WIDTH
#endif

/*
 * Fast power-of-two math macros for non-optimized compilers
 */
#define _ACPI_DIV(value,power_of2)      ((u32) ((value) >> (power_of2)))
#define _ACPI_MUL(value,power_of2)      ((u32) ((value) << (power_of2)))
#define _ACPI_MOD(value,divisor)        ((u32) ((value) & ((divisor) -1)))

#define ACPI_DIV_2(a)                   _ACPI_DIV(a,1)
#define ACPI_MUL_2(a)                   _ACPI_MUL(a,1)
#define ACPI_MOD_2(a)                   _ACPI_MOD(a,2)

#define ACPI_DIV_4(a)                   _ACPI_DIV(a,2)
#define ACPI_MUL_4(a)                   _ACPI_MUL(a,2)
#define ACPI_MOD_4(a)                   _ACPI_MOD(a,4)

#define ACPI_DIV_8(a)                   _ACPI_DIV(a,3)
#define ACPI_MUL_8(a)                   _ACPI_MUL(a,3)
#define ACPI_MOD_8(a)                   _ACPI_MOD(a,8)

#define ACPI_DIV_16(a)                  _ACPI_DIV(a,4)
#define ACPI_MUL_16(a)                  _ACPI_MUL(a,4)
#define ACPI_MOD_16(a)                  _ACPI_MOD(a,16)

#define ACPI_DIV_32(a)                  _ACPI_DIV(a,5)
#define ACPI_MUL_32(a)                  _ACPI_MUL(a,5)
#define ACPI_MOD_32(a)                  _ACPI_MOD(a,32)

/*
 * Rounding macros (Power of two boundaries only)
 */
#define ACPI_ROUND_DOWN(value,boundary)     (((acpi_native_uint)(value)) & \
												(~(((acpi_native_uint) boundary)-1)))

#define ACPI_ROUND_UP(value,boundary)       ((((acpi_native_uint)(value)) + \
												(((acpi_native_uint) boundary)-1)) & \
												(~(((acpi_native_uint) boundary)-1)))

/* Note: sizeof(acpi_native_uint) evaluates to either 2, 4, or 8 */

#define ACPI_ROUND_DOWN_to_32_bIT(a)        ACPI_ROUND_DOWN(a,4)
#define ACPI_ROUND_DOWN_to_64_bIT(a)        ACPI_ROUND_DOWN(a,8)
#define ACPI_ROUND_DOWN_TO_NATIVE_WORD(a)   ACPI_ROUND_DOWN(a,sizeof(acpi_native_uint))

#define ACPI_ROUND_UP_to_32_bIT(a)          ACPI_ROUND_UP(a,4)
#define ACPI_ROUND_UP_to_64_bIT(a)          ACPI_ROUND_UP(a,8)
#define ACPI_ROUND_UP_TO_NATIVE_WORD(a)     ACPI_ROUND_UP(a,sizeof(acpi_native_uint))

#define ACPI_ROUND_BITS_UP_TO_BYTES(a)      ACPI_DIV_8((a) + 7)
#define ACPI_ROUND_BITS_DOWN_TO_BYTES(a)    ACPI_DIV_8((a))

#define ACPI_ROUND_UP_TO_1K(a)              (((a) + 1023) >> 10)

/* Generic (non-power-of-two) rounding */

#define ACPI_ROUND_UP_TO(value,boundary)    (((value) + ((boundary)-1)) / (boundary))

#define ACPI_IS_MISALIGNED(value)           (((acpi_native_uint)value) & (sizeof(acpi_native_uint)-1))

/*
 * Bitmask creation
 * Bit positions start at zero.
 * MASK_BITS_ABOVE creates a mask starting AT the position and above
 * MASK_BITS_BELOW creates a mask starting one bit BELOW the position
 */
#define ACPI_MASK_BITS_ABOVE(position)      (~((ACPI_INTEGER_MAX) << ((u32) (position))))
#define ACPI_MASK_BITS_BELOW(position)      ((ACPI_INTEGER_MAX) << ((u32) (position)))

#define ACPI_IS_OCTAL_DIGIT(d)              (((char)(d) >= '0') && ((char)(d) <= '7'))

/* Bitfields within ACPI registers */

#define ACPI_REGISTER_PREPARE_BITS(val, pos, mask)      ((val << pos) & mask)
#define ACPI_REGISTER_INSERT_VALUE(reg, pos, mask, val)  reg = (reg & (~(mask))) | ACPI_REGISTER_PREPARE_BITS(val, pos, mask)

/* Generate a UUID */

#define ACPI_INIT_UUID(a,b,c,d0,d1,d2,d3,d4,d5,d6,d7) \
	(a) & 0xFF, ((a) >> 8) & 0xFF, ((a) >> 16) & 0xFF, ((a) >> 24) & 0xFF, \
	(b) & 0xFF, ((b) >> 8) & 0xFF, \
	(c) & 0xFF, ((c) >> 8) & 0xFF, \
	(d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7)

/*
 * An struct acpi_namespace_node * can appear in some contexts,
 * where a pointer to an union acpi_operand_object    can also
 * appear.  This macro is used to distinguish them.
 *
 * The "Descriptor" field is the first field in both structures.
 */
#define ACPI_GET_DESCRIPTOR_TYPE(d)     (((union acpi_descriptor *)(void *)(d))->common.descriptor_type)
#define ACPI_SET_DESCRIPTOR_TYPE(d,t)   (((union acpi_descriptor *)(void *)(d))->common.descriptor_type = t)

/* Macro to test the object type */

#define ACPI_GET_OBJECT_TYPE(d)         (((union acpi_operand_object *)(void *)(d))->common.type)

/* Macro to check the table flags for SINGLE or MULTIPLE tables are allowed */

#define ACPI_IS_SINGLE_TABLE(x)         (((x) & 0x01) == ACPI_TABLE_SINGLE ? 1 : 0)

/*
 * Macros for the master AML opcode table
 */
#if defined(ACPI_DISASSEMBLER) || defined (ACPI_DEBUG_OUTPUT)
#define ACPI_OP(name,Pargs,Iargs,obj_type,class,type,flags)    {name,(u32)(Pargs),(u32)(Iargs),(u32)(flags),obj_type,class,type}
#else
#define ACPI_OP(name,Pargs,Iargs,obj_type,class,type,flags)    {(u32)(Pargs),(u32)(Iargs),(u32)(flags),obj_type,class,type}
#endif

#ifdef ACPI_DISASSEMBLER
#define ACPI_DISASM_ONLY_MEMBERS(a)     a;
#else
#define ACPI_DISASM_ONLY_MEMBERS(a)
#endif

#define ARG_TYPE_WIDTH                  5
#define ARG_1(x)                        ((u32)(x))
#define ARG_2(x)                        ((u32)(x) << (1 * ARG_TYPE_WIDTH))
#define ARG_3(x)                        ((u32)(x) << (2 * ARG_TYPE_WIDTH))
#define ARG_4(x)                        ((u32)(x) << (3 * ARG_TYPE_WIDTH))
#define ARG_5(x)                        ((u32)(x) << (4 * ARG_TYPE_WIDTH))
#define ARG_6(x)                        ((u32)(x) << (5 * ARG_TYPE_WIDTH))

#define ARGI_LIST1(a)                   (ARG_1(a))
#define ARGI_LIST2(a,b)                 (ARG_1(b)|ARG_2(a))
#define ARGI_LIST3(a,b,c)               (ARG_1(c)|ARG_2(b)|ARG_3(a))
#define ARGI_LIST4(a,b,c,d)             (ARG_1(d)|ARG_2(c)|ARG_3(b)|ARG_4(a))
#define ARGI_LIST5(a,b,c,d,e)           (ARG_1(e)|ARG_2(d)|ARG_3(c)|ARG_4(b)|ARG_5(a))
#define ARGI_LIST6(a,b,c,d,e,f)         (ARG_1(f)|ARG_2(e)|ARG_3(d)|ARG_4(c)|ARG_5(b)|ARG_6(a))

#define ARGP_LIST1(a)                   (ARG_1(a))
#define ARGP_LIST2(a,b)                 (ARG_1(a)|ARG_2(b))
#define ARGP_LIST3(a,b,c)               (ARG_1(a)|ARG_2(b)|ARG_3(c))
#define ARGP_LIST4(a,b,c,d)             (ARG_1(a)|ARG_2(b)|ARG_3(c)|ARG_4(d))
#define ARGP_LIST5(a,b,c,d,e)           (ARG_1(a)|ARG_2(b)|ARG_3(c)|ARG_4(d)|ARG_5(e))
#define ARGP_LIST6(a,b,c,d,e,f)         (ARG_1(a)|ARG_2(b)|ARG_3(c)|ARG_4(d)|ARG_5(e)|ARG_6(f))

#define GET_CURRENT_ARG_TYPE(list)      (list & ((u32) 0x1F))
#define INCREMENT_ARG_LIST(list)        (list >>= ((u32) ARG_TYPE_WIDTH))

#if defined (ACPI_DEBUG_OUTPUT) || !defined (ACPI_NO_ERROR_MESSAGES)
/*
 * Module name is include in both debug and non-debug versions primarily for
 * error messages. The __FILE__ macro is not very useful for this, because it
 * often includes the entire pathname to the module
 */
#define ACPI_MODULE_NAME(name)          static char ACPI_UNUSED_VAR *_acpi_module_name = name;
#else
#define ACPI_MODULE_NAME(name)
#endif

/*
 * Ascii error messages can be configured out
 */
#ifndef ACPI_NO_ERROR_MESSAGES
#define AE_INFO                         _acpi_module_name, __LINE__

/*
 * Error reporting. Callers module and line number are inserted by AE_INFO,
 * the plist contains a set of parens to allow variable-length lists.
 * These macros are used for both the debug and non-debug versions of the code.
 */
#define ACPI_INFO(plist)                acpi_ut_info plist
#define ACPI_WARNING(plist)             acpi_ut_warning plist
#define ACPI_EXCEPTION(plist)           acpi_ut_exception plist
#define ACPI_ERROR(plist)               acpi_ut_error plist
#define ACPI_ERROR_NAMESPACE(s,e)       acpi_ns_report_error (AE_INFO, s, e);
#define ACPI_ERROR_METHOD(s,n,p,e)      acpi_ns_report_method_error (AE_INFO, s, n, p, e);

#else

/* No error messages */

#define ACPI_INFO(plist)
#define ACPI_WARNING(plist)
#define ACPI_EXCEPTION(plist)
#define ACPI_ERROR(plist)
#define ACPI_ERROR_NAMESPACE(s,e)
#define ACPI_ERROR_METHOD(s,n,p,e)
#endif

/*
 * Debug macros that are conditionally compiled
 */
#ifdef ACPI_DEBUG_OUTPUT

/*
 * Common parameters used for debug output functions:
 * line number, function name, module(file) name, component ID
 */
#define ACPI_DEBUG_PARAMETERS           __LINE__, ACPI_GET_FUNCTION_NAME, _acpi_module_name, _COMPONENT

/*
 * Function entry tracing
 */

/*
 * If ACPI_GET_FUNCTION_NAME was not defined in the compiler-dependent header,
 * define it now. This is the case where there the compiler does not support
 * a __FUNCTION__ macro or equivalent. We save the function name on the
 * local stack.
 */
#ifndef ACPI_GET_FUNCTION_NAME
#define ACPI_GET_FUNCTION_NAME          _acpi_function_name
/*
 * The Name parameter should be the procedure name as a quoted string.
 * This is declared as a local string ("MyFunctionName") so that it can
 * be also used by the function exit macros below.
 * Note: (const char) is used to be compatible with the debug interfaces
 * and macros such as __FUNCTION__.
 */
#define ACPI_FUNCTION_NAME(name)        const char *_acpi_function_name = #name;

#else
/* Compiler supports __FUNCTION__ (or equivalent) -- Ignore this macro */

#define ACPI_FUNCTION_NAME(name)
#endif

#define ACPI_FUNCTION_TRACE(a)          ACPI_FUNCTION_NAME(a) \
			  acpi_ut_trace(ACPI_DEBUG_PARAMETERS)
#define ACPI_FUNCTION_TRACE_PTR(a,b)    ACPI_FUNCTION_NAME(a) \
					   acpi_ut_trace_ptr(ACPI_DEBUG_PARAMETERS,(void *)b)
#define ACPI_FUNCTION_TRACE_U32(a,b)    ACPI_FUNCTION_NAME(a) \
							 acpi_ut_trace_u32(ACPI_DEBUG_PARAMETERS,(u32)b)
#define ACPI_FUNCTION_TRACE_STR(a,b)    ACPI_FUNCTION_NAME(a) \
									  acpi_ut_trace_str(ACPI_DEBUG_PARAMETERS,(char *)b)

#define ACPI_FUNCTION_ENTRY()           acpi_ut_track_stack_ptr()

/*
 * Function exit tracing.
 * WARNING: These macros include a return statement.  This is usually considered
 * bad form, but having a separate exit macro is very ugly and difficult to maintain.
 * One of the FUNCTION_TRACE macros above must be used in conjunction with these macros
 * so that "_AcpiFunctionName" is defined.
 *
 * Note: the DO_WHILE0 macro is used to prevent some compilers from complaining
 * about these constructs.
 */
#ifdef ACPI_USE_DO_WHILE_0
#define ACPI_DO_WHILE0(a)               do a while(0)
#else
#define ACPI_DO_WHILE0(a)               a
#endif

#define return_VOID                     ACPI_DO_WHILE0 ({ \
											acpi_ut_exit (ACPI_DEBUG_PARAMETERS); \
											return;})
/*
 * There are two versions of most of the return macros. The default version is
 * safer, since it avoids side-effects by guaranteeing that the argument will
 * not be evaluated twice.
 *
 * A less-safe version of the macros is provided for optional use if the
 * compiler uses excessive CPU stack (for example, this may happen in the
 * debug case if code optimzation is disabled.)
 */
#ifndef ACPI_SIMPLE_RETURN_MACROS

#define return_ACPI_STATUS(s)           ACPI_DO_WHILE0 ({ \
											register acpi_status _s = (s); \
											acpi_ut_status_exit (ACPI_DEBUG_PARAMETERS, _s); \
											return (_s); })
#define return_PTR(s)                   ACPI_DO_WHILE0 ({ \
											register void *_s = (void *) (s); \
											acpi_ut_ptr_exit (ACPI_DEBUG_PARAMETERS, (u8 *) _s); \
											return (_s); })
#define return_VALUE(s)                 ACPI_DO_WHILE0 ({ \
											register acpi_integer _s = (s); \
											acpi_ut_value_exit (ACPI_DEBUG_PARAMETERS, _s); \
											return (_s); })
#define return_UINT8(s)                 ACPI_DO_WHILE0 ({ \
											register u8 _s = (u8) (s); \
											acpi_ut_value_exit (ACPI_DEBUG_PARAMETERS, (acpi_integer) _s); \
											return (_s); })
#define return_UINT32(s)                ACPI_DO_WHILE0 ({ \
											register u32 _s = (u32) (s); \
											acpi_ut_value_exit (ACPI_DEBUG_PARAMETERS, (acpi_integer) _s); \
											return (_s); })
#else				/* Use original less-safe macros */

#define return_ACPI_STATUS(s)           ACPI_DO_WHILE0 ({ \
											acpi_ut_status_exit (ACPI_DEBUG_PARAMETERS, (s)); \
											return((s)); })
#define return_PTR(s)                   ACPI_DO_WHILE0 ({ \
											acpi_ut_ptr_exit (ACPI_DEBUG_PARAMETERS, (u8 *) (s)); \
											return((s)); })
#define return_VALUE(s)                 ACPI_DO_WHILE0 ({ \
											acpi_ut_value_exit (ACPI_DEBUG_PARAMETERS, (acpi_integer) (s)); \
											return((s)); })
#define return_UINT8(s)                 return_VALUE(s)
#define return_UINT32(s)                return_VALUE(s)

#endif				/* ACPI_SIMPLE_RETURN_MACROS */

/* Conditional execution */

#define ACPI_DEBUG_EXEC(a)              a
#define ACPI_NORMAL_EXEC(a)

#define ACPI_DEBUG_DEFINE(a)            a;
#define ACPI_DEBUG_ONLY_MEMBERS(a)      a;
#define _VERBOSE_STRUCTURES

/* Stack and buffer dumping */

#define ACPI_DUMP_STACK_ENTRY(a)        acpi_ex_dump_operand((a),0)
#define ACPI_DUMP_OPERANDS(a,b,c,d,e)   acpi_ex_dump_operands(a,b,c,d,e,_acpi_module_name,__LINE__)

#define ACPI_DUMP_ENTRY(a,b)            acpi_ns_dump_entry (a,b)
#define ACPI_DUMP_PATHNAME(a,b,c,d)     acpi_ns_dump_pathname(a,b,c,d)
#define ACPI_DUMP_RESOURCE_LIST(a)      acpi_rs_dump_resource_list(a)
#define ACPI_DUMP_BUFFER(a,b)           acpi_ut_dump_buffer((u8 *)a,b,DB_BYTE_DISPLAY,_COMPONENT)

/*
 * Master debug print macros
 * Print iff:
 *    1) Debug print for the current component is enabled
 *    2) Debug error level or trace level for the print statement is enabled
 */
#define ACPI_DEBUG_PRINT(plist)         acpi_ut_debug_print plist
#define ACPI_DEBUG_PRINT_RAW(plist)     acpi_ut_debug_print_raw plist

#else
/*
 * This is the non-debug case -- make everything go away,
 * leaving no executable debug code!
 */
#define ACPI_DEBUG_EXEC(a)
#define ACPI_NORMAL_EXEC(a)             a;

#define ACPI_DEBUG_DEFINE(a)
#define ACPI_DEBUG_ONLY_MEMBERS(a)
#define ACPI_FUNCTION_NAME(a)
#define ACPI_FUNCTION_TRACE(a)
#define ACPI_FUNCTION_TRACE_PTR(a,b)
#define ACPI_FUNCTION_TRACE_U32(a,b)
#define ACPI_FUNCTION_TRACE_STR(a,b)
#define ACPI_FUNCTION_EXIT
#define ACPI_FUNCTION_STATUS_EXIT(s)
#define ACPI_FUNCTION_VALUE_EXIT(s)
#define ACPI_FUNCTION_ENTRY()
#define ACPI_DUMP_STACK_ENTRY(a)
#define ACPI_DUMP_OPERANDS(a,b,c,d,e)
#define ACPI_DUMP_ENTRY(a,b)
#define ACPI_DUMP_TABLES(a,b)
#define ACPI_DUMP_PATHNAME(a,b,c,d)
#define ACPI_DUMP_RESOURCE_LIST(a)
#define ACPI_DUMP_BUFFER(a,b)
#define ACPI_DEBUG_PRINT(pl)
#define ACPI_DEBUG_PRINT_RAW(pl)

#define return_VOID                     return
#define return_ACPI_STATUS(s)           return(s)
#define return_VALUE(s)                 return(s)
#define return_UINT8(s)                 return(s)
#define return_UINT32(s)                return(s)
#define return_PTR(s)                   return(s)

#endif

/*
 * Some code only gets executed when the debugger is built in.
 * Note that this is entirely independent of whether the
 * DEBUG_PRINT stuff (set by ACPI_DEBUG_OUTPUT) is on, or not.
 */
#ifdef ACPI_DEBUGGER
#define ACPI_DEBUGGER_EXEC(a)           a
#else
#define ACPI_DEBUGGER_EXEC(a)
#endif

/*
 * For 16-bit code, we want to shrink some things even though
 * we are using ACPI_DEBUG_OUTPUT to get the debug output
 */
#if ACPI_MACHINE_WIDTH == 16
#undef ACPI_DEBUG_ONLY_MEMBERS
#undef _VERBOSE_STRUCTURES
#define ACPI_DEBUG_ONLY_MEMBERS(a)
#endif

#ifdef ACPI_DEBUG_OUTPUT
/*
 * 1) Set name to blanks
 * 2) Copy the object name
 */
#define ACPI_ADD_OBJECT_NAME(a,b)       ACPI_MEMSET (a->common.name, ' ', sizeof (a->common.name));\
										ACPI_STRNCPY (a->common.name, acpi_gbl_ns_type_names[b], sizeof (a->common.name))
#else

#define ACPI_ADD_OBJECT_NAME(a,b)
#endif

/*
 * Memory allocation tracking (DEBUG ONLY)
 */
#ifndef ACPI_DBG_TRACK_ALLOCATIONS

/* Memory allocation */

#define ACPI_ALLOCATE(a)            acpi_ut_allocate((acpi_size)(a),_COMPONENT,_acpi_module_name,__LINE__)
#define ACPI_ALLOCATE_ZEROED(a)     acpi_ut_allocate_zeroed((acpi_size)(a), _COMPONENT,_acpi_module_name,__LINE__)
#define ACPI_FREE(a)                acpi_os_free(a)
#define ACPI_MEM_TRACKING(a)

#else

/* Memory allocation */

#define ACPI_ALLOCATE(a)            acpi_ut_allocate_and_track((acpi_size)(a),_COMPONENT,_acpi_module_name,__LINE__)
#define ACPI_ALLOCATE_ZEROED(a)     acpi_ut_allocate_zeroed_and_track((acpi_size)(a), _COMPONENT,_acpi_module_name,__LINE__)
#define ACPI_FREE(a)                acpi_ut_free_and_track(a,_COMPONENT,_acpi_module_name,__LINE__)
#define ACPI_MEM_TRACKING(a)        a

#endif				/* ACPI_DBG_TRACK_ALLOCATIONS */

#endif				/* ACMACROS_H */
