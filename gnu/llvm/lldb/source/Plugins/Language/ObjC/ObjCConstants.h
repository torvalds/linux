//===-- ObjCConstants.h------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_OBJCCONSTANTS_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_OBJCCONSTANTS_H

// Objective-C Type Encoding
#define _C_ID '@'
#define _C_CLASS '#'
#define _C_SEL ':'
#define _C_CHR 'c'
#define _C_UCHR 'C'
#define _C_SHT 's'
#define _C_USHT 'S'
#define _C_INT 'i'
#define _C_UINT 'I'
#define _C_LNG 'l'
#define _C_ULNG 'L'
#define _C_LNG_LNG 'q'
#define _C_ULNG_LNG 'Q'
#define _C_FLT 'f'
#define _C_DBL 'd'
#define _C_BFLD 'b'
#define _C_BOOL 'B'
#define _C_VOID 'v'
#define _C_UNDEF '?'
#define _C_PTR '^'
#define _C_CHARPTR '*'
#define _C_ATOM '%'
#define _C_ARY_B '['
#define _C_ARY_E ']'
#define _C_UNION_B '('
#define _C_UNION_E ')'
#define _C_STRUCT_B '{'
#define _C_STRUCT_E '}'
#define _C_VECTOR '!'
#define _C_CONST 'r'

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_OBJCCONSTANTS_H
