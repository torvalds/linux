/** @file
  This file contains AML code definition in the latest ACPI spec.

  Copyright (c) 2011, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef _ACPI_AML_H_
#define _ACPI_AML_H_

//
// ACPI AML definition
//

//
// Primary OpCode
//
#define AML_ZERO_OP                  0x00
#define AML_ONE_OP                   0x01
#define AML_ALIAS_OP                 0x06
#define AML_NAME_OP                  0x08
#define AML_BYTE_PREFIX              0x0a
#define AML_WORD_PREFIX              0x0b
#define AML_DWORD_PREFIX             0x0c
#define AML_STRING_PREFIX            0x0d
#define AML_QWORD_PREFIX             0x0e
#define AML_SCOPE_OP                 0x10
#define AML_BUFFER_OP                0x11
#define AML_PACKAGE_OP               0x12
#define AML_VAR_PACKAGE_OP           0x13
#define AML_METHOD_OP                0x14
#define AML_DUAL_NAME_PREFIX         0x2e
#define AML_MULTI_NAME_PREFIX        0x2f
#define AML_NAME_CHAR_A              0x41
#define AML_NAME_CHAR_B              0x42
#define AML_NAME_CHAR_C              0x43
#define AML_NAME_CHAR_D              0x44
#define AML_NAME_CHAR_E              0x45
#define AML_NAME_CHAR_F              0x46
#define AML_NAME_CHAR_G              0x47
#define AML_NAME_CHAR_H              0x48
#define AML_NAME_CHAR_I              0x49
#define AML_NAME_CHAR_J              0x4a
#define AML_NAME_CHAR_K              0x4b
#define AML_NAME_CHAR_L              0x4c
#define AML_NAME_CHAR_M              0x4d
#define AML_NAME_CHAR_N              0x4e
#define AML_NAME_CHAR_O              0x4f
#define AML_NAME_CHAR_P              0x50
#define AML_NAME_CHAR_Q              0x51
#define AML_NAME_CHAR_R              0x52
#define AML_NAME_CHAR_S              0x53
#define AML_NAME_CHAR_T              0x54
#define AML_NAME_CHAR_U              0x55
#define AML_NAME_CHAR_V              0x56
#define AML_NAME_CHAR_W              0x57
#define AML_NAME_CHAR_X              0x58
#define AML_NAME_CHAR_Y              0x59
#define AML_NAME_CHAR_Z              0x5a
#define AML_ROOT_CHAR                0x5c
#define AML_PARENT_PREFIX_CHAR       0x5e
#define AML_NAME_CHAR__              0x5f
#define AML_LOCAL0                   0x60
#define AML_LOCAL1                   0x61
#define AML_LOCAL2                   0x62
#define AML_LOCAL3                   0x63
#define AML_LOCAL4                   0x64
#define AML_LOCAL5                   0x65
#define AML_LOCAL6                   0x66
#define AML_LOCAL7                   0x67
#define AML_ARG0                     0x68
#define AML_ARG1                     0x69
#define AML_ARG2                     0x6a
#define AML_ARG3                     0x6b
#define AML_ARG4                     0x6c
#define AML_ARG5                     0x6d
#define AML_ARG6                     0x6e
#define AML_STORE_OP                 0x70
#define AML_REF_OF_OP                0x71
#define AML_ADD_OP                   0x72
#define AML_CONCAT_OP                0x73
#define AML_SUBTRACT_OP              0x74
#define AML_INCREMENT_OP             0x75
#define AML_DECREMENT_OP             0x76
#define AML_MULTIPLY_OP              0x77
#define AML_DIVIDE_OP                0x78
#define AML_SHIFT_LEFT_OP            0x79
#define AML_SHIFT_RIGHT_OP           0x7a
#define AML_AND_OP                   0x7b
#define AML_NAND_OP                  0x7c
#define AML_OR_OP                    0x7d
#define AML_NOR_OP                   0x7e
#define AML_XOR_OP                   0x7f
#define AML_NOT_OP                   0x80
#define AML_FIND_SET_LEFT_BIT_OP     0x81
#define AML_FIND_SET_RIGHT_BIT_OP    0x82
#define AML_DEREF_OF_OP              0x83
#define AML_CONCAT_RES_OP            0x84
#define AML_MOD_OP                   0x85
#define AML_NOTIFY_OP                0x86
#define AML_SIZE_OF_OP               0x87
#define AML_INDEX_OP                 0x88
#define AML_MATCH_OP                 0x89
#define AML_CREATE_DWORD_FIELD_OP    0x8a
#define AML_CREATE_WORD_FIELD_OP     0x8b
#define AML_CREATE_BYTE_FIELD_OP     0x8c
#define AML_CREATE_BIT_FIELD_OP      0x8d
#define AML_OBJECT_TYPE_OP           0x8e
#define AML_CREATE_QWORD_FIELD_OP    0x8f
#define AML_LAND_OP                  0x90
#define AML_LOR_OP                   0x91
#define AML_LNOT_OP                  0x92
#define AML_LEQUAL_OP                0x93
#define AML_LGREATER_OP              0x94
#define AML_LLESS_OP                 0x95
#define AML_TO_BUFFER_OP             0x96
#define AML_TO_DEC_STRING_OP         0x97
#define AML_TO_HEX_STRING_OP         0x98
#define AML_TO_INTEGER_OP            0x99
#define AML_TO_STRING_OP             0x9c
#define AML_COPY_OBJECT_OP           0x9d
#define AML_MID_OP                   0x9e
#define AML_CONTINUE_OP              0x9f
#define AML_IF_OP                    0xa0
#define AML_ELSE_OP                  0xa1
#define AML_WHILE_OP                 0xa2
#define AML_NOOP_OP                  0xa3
#define AML_RETURN_OP                0xa4
#define AML_BREAK_OP                 0xa5
#define AML_BREAK_POINT_OP           0xcc
#define AML_ONES_OP                  0xff

//
// Extended OpCode
//
#define AML_EXT_OP                   0x5b

#define AML_EXT_MUTEX_OP             0x01
#define AML_EXT_EVENT_OP             0x02
#define AML_EXT_COND_REF_OF_OP       0x12
#define AML_EXT_CREATE_FIELD_OP      0x13
#define AML_EXT_LOAD_TABLE_OP        0x1f
#define AML_EXT_LOAD_OP              0x20
#define AML_EXT_STALL_OP             0x21
#define AML_EXT_SLEEP_OP             0x22
#define AML_EXT_ACQUIRE_OP           0x23
#define AML_EXT_SIGNAL_OP            0x24
#define AML_EXT_WAIT_OP              0x25
#define AML_EXT_RESET_OP             0x26
#define AML_EXT_RELEASE_OP           0x27
#define AML_EXT_FROM_BCD_OP          0x28
#define AML_EXT_TO_BCD_OP            0x29
#define AML_EXT_UNLOAD_OP            0x2a
#define AML_EXT_REVISION_OP          0x30
#define AML_EXT_DEBUG_OP             0x31
#define AML_EXT_FATAL_OP             0x32
#define AML_EXT_TIMER_OP             0x33
#define AML_EXT_REGION_OP            0x80
#define AML_EXT_FIELD_OP             0x81
#define AML_EXT_DEVICE_OP            0x82
#define AML_EXT_PROCESSOR_OP         0x83
#define AML_EXT_POWER_RES_OP         0x84
#define AML_EXT_THERMAL_ZONE_OP      0x85
#define AML_EXT_INDEX_FIELD_OP       0x86
#define AML_EXT_BANK_FIELD_OP        0x87
#define AML_EXT_DATA_REGION_OP       0x88

#endif
