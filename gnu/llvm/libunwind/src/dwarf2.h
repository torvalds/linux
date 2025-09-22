//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//


/*
   These constants were taken from version 3 of the DWARF standard,
   which is Copyright (c) 2005 Free Standards Group, and
   Copyright (c) 1992, 1993 UNIX International, Inc.
*/

#ifndef __DWARF2__
#define __DWARF2__

// DWARF unwind instructions
enum {
  DW_CFA_nop                 = 0x0,
  DW_CFA_set_loc             = 0x1,
  DW_CFA_advance_loc1        = 0x2,
  DW_CFA_advance_loc2        = 0x3,
  DW_CFA_advance_loc4        = 0x4,
  DW_CFA_offset_extended     = 0x5,
  DW_CFA_restore_extended    = 0x6,
  DW_CFA_undefined           = 0x7,
  DW_CFA_same_value          = 0x8,
  DW_CFA_register            = 0x9,
  DW_CFA_remember_state      = 0xA,
  DW_CFA_restore_state       = 0xB,
  DW_CFA_def_cfa             = 0xC,
  DW_CFA_def_cfa_register    = 0xD,
  DW_CFA_def_cfa_offset      = 0xE,
  DW_CFA_def_cfa_expression  = 0xF,
  DW_CFA_expression         = 0x10,
  DW_CFA_offset_extended_sf = 0x11,
  DW_CFA_def_cfa_sf         = 0x12,
  DW_CFA_def_cfa_offset_sf  = 0x13,
  DW_CFA_val_offset         = 0x14,
  DW_CFA_val_offset_sf      = 0x15,
  DW_CFA_val_expression     = 0x16,
  DW_CFA_advance_loc        = 0x40, // high 2 bits are 0x1, lower 6 bits are delta
  DW_CFA_offset             = 0x80, // high 2 bits are 0x2, lower 6 bits are register
  DW_CFA_restore            = 0xC0, // high 2 bits are 0x3, lower 6 bits are register

  // GNU extensions
  DW_CFA_GNU_window_save              = 0x2D,
  DW_CFA_GNU_args_size                = 0x2E,
  DW_CFA_GNU_negative_offset_extended = 0x2F,

  // AARCH64 extensions
  DW_CFA_AARCH64_negate_ra_state      = 0x2D
};


// FSF exception handling Pointer-Encoding constants
// Used in CFI augmentation by GCC
enum {
  DW_EH_PE_ptr       = 0x00,
  DW_EH_PE_uleb128   = 0x01,
  DW_EH_PE_udata2    = 0x02,
  DW_EH_PE_udata4    = 0x03,
  DW_EH_PE_udata8    = 0x04,
  DW_EH_PE_signed    = 0x08,
  DW_EH_PE_sleb128   = 0x09,
  DW_EH_PE_sdata2    = 0x0A,
  DW_EH_PE_sdata4    = 0x0B,
  DW_EH_PE_sdata8    = 0x0C,
  DW_EH_PE_absptr    = 0x00,
  DW_EH_PE_pcrel     = 0x10,
  DW_EH_PE_textrel   = 0x20,
  DW_EH_PE_datarel   = 0x30,
  DW_EH_PE_funcrel   = 0x40,
  DW_EH_PE_aligned   = 0x50,
  DW_EH_PE_indirect  = 0x80,
  DW_EH_PE_omit      = 0xFF
};


// DWARF expressions
enum {
  DW_OP_addr               = 0x03, // constant address (size target specific)
  DW_OP_deref              = 0x06,
  DW_OP_const1u            = 0x08, // 1-byte constant
  DW_OP_const1s            = 0x09, // 1-byte constant
  DW_OP_const2u            = 0x0A, // 2-byte constant
  DW_OP_const2s            = 0x0B, // 2-byte constant
  DW_OP_const4u            = 0x0C, // 4-byte constant
  DW_OP_const4s            = 0x0D, // 4-byte constant
  DW_OP_const8u            = 0x0E, // 8-byte constant
  DW_OP_const8s            = 0x0F, // 8-byte constant
  DW_OP_constu             = 0x10, // ULEB128 constant
  DW_OP_consts             = 0x11, // SLEB128 constant
  DW_OP_dup                = 0x12,
  DW_OP_drop               = 0x13,
  DW_OP_over               = 0x14,
  DW_OP_pick               = 0x15, // 1-byte stack index
  DW_OP_swap               = 0x16,
  DW_OP_rot                = 0x17,
  DW_OP_xderef             = 0x18,
  DW_OP_abs                = 0x19,
  DW_OP_and                = 0x1A,
  DW_OP_div                = 0x1B,
  DW_OP_minus              = 0x1C,
  DW_OP_mod                = 0x1D,
  DW_OP_mul                = 0x1E,
  DW_OP_neg                = 0x1F,
  DW_OP_not                = 0x20,
  DW_OP_or                 = 0x21,
  DW_OP_plus               = 0x22,
  DW_OP_plus_uconst        = 0x23, // ULEB128 addend
  DW_OP_shl                = 0x24,
  DW_OP_shr                = 0x25,
  DW_OP_shra               = 0x26,
  DW_OP_xor                = 0x27,
  DW_OP_skip               = 0x2F, // signed 2-byte constant
  DW_OP_bra                = 0x28, // signed 2-byte constant
  DW_OP_eq                 = 0x29,
  DW_OP_ge                 = 0x2A,
  DW_OP_gt                 = 0x2B,
  DW_OP_le                 = 0x2C,
  DW_OP_lt                 = 0x2D,
  DW_OP_ne                 = 0x2E,
  DW_OP_lit0               = 0x30, // Literal 0
  DW_OP_lit1               = 0x31, // Literal 1
  DW_OP_lit2               = 0x32, // Literal 2
  DW_OP_lit3               = 0x33, // Literal 3
  DW_OP_lit4               = 0x34, // Literal 4
  DW_OP_lit5               = 0x35, // Literal 5
  DW_OP_lit6               = 0x36, // Literal 6
  DW_OP_lit7               = 0x37, // Literal 7
  DW_OP_lit8               = 0x38, // Literal 8
  DW_OP_lit9               = 0x39, // Literal 9
  DW_OP_lit10              = 0x3A, // Literal 10
  DW_OP_lit11              = 0x3B, // Literal 11
  DW_OP_lit12              = 0x3C, // Literal 12
  DW_OP_lit13              = 0x3D, // Literal 13
  DW_OP_lit14              = 0x3E, // Literal 14
  DW_OP_lit15              = 0x3F, // Literal 15
  DW_OP_lit16              = 0x40, // Literal 16
  DW_OP_lit17              = 0x41, // Literal 17
  DW_OP_lit18              = 0x42, // Literal 18
  DW_OP_lit19              = 0x43, // Literal 19
  DW_OP_lit20              = 0x44, // Literal 20
  DW_OP_lit21              = 0x45, // Literal 21
  DW_OP_lit22              = 0x46, // Literal 22
  DW_OP_lit23              = 0x47, // Literal 23
  DW_OP_lit24              = 0x48, // Literal 24
  DW_OP_lit25              = 0x49, // Literal 25
  DW_OP_lit26              = 0x4A, // Literal 26
  DW_OP_lit27              = 0x4B, // Literal 27
  DW_OP_lit28              = 0x4C, // Literal 28
  DW_OP_lit29              = 0x4D, // Literal 29
  DW_OP_lit30              = 0x4E, // Literal 30
  DW_OP_lit31              = 0x4F, // Literal 31
  DW_OP_reg0               = 0x50, // Contents of reg0
  DW_OP_reg1               = 0x51, // Contents of reg1
  DW_OP_reg2               = 0x52, // Contents of reg2
  DW_OP_reg3               = 0x53, // Contents of reg3
  DW_OP_reg4               = 0x54, // Contents of reg4
  DW_OP_reg5               = 0x55, // Contents of reg5
  DW_OP_reg6               = 0x56, // Contents of reg6
  DW_OP_reg7               = 0x57, // Contents of reg7
  DW_OP_reg8               = 0x58, // Contents of reg8
  DW_OP_reg9               = 0x59, // Contents of reg9
  DW_OP_reg10              = 0x5A, // Contents of reg10
  DW_OP_reg11              = 0x5B, // Contents of reg11
  DW_OP_reg12              = 0x5C, // Contents of reg12
  DW_OP_reg13              = 0x5D, // Contents of reg13
  DW_OP_reg14              = 0x5E, // Contents of reg14
  DW_OP_reg15              = 0x5F, // Contents of reg15
  DW_OP_reg16              = 0x60, // Contents of reg16
  DW_OP_reg17              = 0x61, // Contents of reg17
  DW_OP_reg18              = 0x62, // Contents of reg18
  DW_OP_reg19              = 0x63, // Contents of reg19
  DW_OP_reg20              = 0x64, // Contents of reg20
  DW_OP_reg21              = 0x65, // Contents of reg21
  DW_OP_reg22              = 0x66, // Contents of reg22
  DW_OP_reg23              = 0x67, // Contents of reg23
  DW_OP_reg24              = 0x68, // Contents of reg24
  DW_OP_reg25              = 0x69, // Contents of reg25
  DW_OP_reg26              = 0x6A, // Contents of reg26
  DW_OP_reg27              = 0x6B, // Contents of reg27
  DW_OP_reg28              = 0x6C, // Contents of reg28
  DW_OP_reg29              = 0x6D, // Contents of reg29
  DW_OP_reg30              = 0x6E, // Contents of reg30
  DW_OP_reg31              = 0x6F, // Contents of reg31
  DW_OP_breg0              = 0x70, // base register 0 + SLEB128 offset
  DW_OP_breg1              = 0x71, // base register 1 + SLEB128 offset
  DW_OP_breg2              = 0x72, // base register 2 + SLEB128 offset
  DW_OP_breg3              = 0x73, // base register 3 + SLEB128 offset
  DW_OP_breg4              = 0x74, // base register 4 + SLEB128 offset
  DW_OP_breg5              = 0x75, // base register 5 + SLEB128 offset
  DW_OP_breg6              = 0x76, // base register 6 + SLEB128 offset
  DW_OP_breg7              = 0x77, // base register 7 + SLEB128 offset
  DW_OP_breg8              = 0x78, // base register 8 + SLEB128 offset
  DW_OP_breg9              = 0x79, // base register 9 + SLEB128 offset
  DW_OP_breg10             = 0x7A, // base register 10 + SLEB128 offset
  DW_OP_breg11             = 0x7B, // base register 11 + SLEB128 offset
  DW_OP_breg12             = 0x7C, // base register 12 + SLEB128 offset
  DW_OP_breg13             = 0x7D, // base register 13 + SLEB128 offset
  DW_OP_breg14             = 0x7E, // base register 14 + SLEB128 offset
  DW_OP_breg15             = 0x7F, // base register 15 + SLEB128 offset
  DW_OP_breg16             = 0x80, // base register 16 + SLEB128 offset
  DW_OP_breg17             = 0x81, // base register 17 + SLEB128 offset
  DW_OP_breg18             = 0x82, // base register 18 + SLEB128 offset
  DW_OP_breg19             = 0x83, // base register 19 + SLEB128 offset
  DW_OP_breg20             = 0x84, // base register 20 + SLEB128 offset
  DW_OP_breg21             = 0x85, // base register 21 + SLEB128 offset
  DW_OP_breg22             = 0x86, // base register 22 + SLEB128 offset
  DW_OP_breg23             = 0x87, // base register 23 + SLEB128 offset
  DW_OP_breg24             = 0x88, // base register 24 + SLEB128 offset
  DW_OP_breg25             = 0x89, // base register 25 + SLEB128 offset
  DW_OP_breg26             = 0x8A, // base register 26 + SLEB128 offset
  DW_OP_breg27             = 0x8B, // base register 27 + SLEB128 offset
  DW_OP_breg28             = 0x8C, // base register 28 + SLEB128 offset
  DW_OP_breg29             = 0x8D, // base register 29 + SLEB128 offset
  DW_OP_breg30             = 0x8E, // base register 30 + SLEB128 offset
  DW_OP_breg31             = 0x8F, // base register 31 + SLEB128 offset
  DW_OP_regx               = 0x90, // ULEB128 register
  DW_OP_fbreg              = 0x91, // SLEB128 offset
  DW_OP_bregx              = 0x92, // ULEB128 register followed by SLEB128 offset
  DW_OP_piece              = 0x93, // ULEB128 size of piece addressed
  DW_OP_deref_size         = 0x94, // 1-byte size of data retrieved
  DW_OP_xderef_size        = 0x95, // 1-byte size of data retrieved
  DW_OP_nop                = 0x96,
  DW_OP_push_object_addres = 0x97,
  DW_OP_call2              = 0x98, // 2-byte offset of DIE
  DW_OP_call4              = 0x99, // 4-byte offset of DIE
  DW_OP_call_ref           = 0x9A, // 4- or 8-byte offset of DIE
  DW_OP_lo_user            = 0xE0,
  DW_OP_APPLE_uninit       = 0xF0,
  DW_OP_hi_user            = 0xFF
};


#endif
