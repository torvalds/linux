//===- DIASupport.h - Common header includes for DIA ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Common defines and header includes for all LLVMDebugInfoPDBDIA.  The
// definitions here configure the necessary #defines and include system headers
// in the proper order for using DIA.
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIASUPPORT_H
#define LLVM_DEBUGINFO_PDB_DIA_DIASUPPORT_H

// Require at least Vista
#define NTDDI_VERSION NTDDI_VISTA
#define _WIN32_WINNT _WIN32_WINNT_VISTA
#define WINVER _WIN32_WINNT_VISTA
#ifndef NOMINMAX
#define NOMINMAX
#endif

// atlbase.h has to come before windows.h
#include <atlbase.h>
#include <windows.h>

// DIA headers must come after windows headers.
#include <cvconst.h>
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#endif
#include <dia2.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include <diacreate.h>

#endif // LLVM_DEBUGINFO_PDB_DIA_DIASUPPORT_H
