/*===-- MemEntryDef.inc - MemProf profiling runtime macros -*- C++ -*-======== *\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/
/*
 * This file defines the macros for memprof profiling data structures.
 * Eg. usage to define the memprof meminfoblock struct:
 *
 * struct MemInfoBlock {
 * #define MIBEntryDef(NameTag, Name, Type) Type Name;
 * #include MIBEntryDef.inc
 * #undef MIBEntryDef
 * };
 *
 * This file has two identical copies. The primary copy lives in LLVM and
 * the other one sits in compiler-rt/include/profile directory. To make changes
 * in this file, first modify the primary copy and copy it over to compiler-rt.
 * Testing of any change in this file can start only after the two copies are
 * synced up.
 *
\*===----------------------------------------------------------------------===*/
#ifndef MIBEntryDef
#define MIBEntryDef(NameTag, Name, Type)
#endif

MIBEntryDef(AllocCount = 1, AllocCount, uint32_t)
MIBEntryDef(TotalAccessCount = 2, TotalAccessCount, uint64_t)
MIBEntryDef(MinAccessCount = 3, MinAccessCount, uint64_t)
MIBEntryDef(MaxAccessCount = 4, MaxAccessCount, uint64_t)
MIBEntryDef(TotalSize = 5, TotalSize, uint64_t)
MIBEntryDef(MinSize = 6, MinSize, uint32_t)
MIBEntryDef(MaxSize = 7, MaxSize, uint32_t)
MIBEntryDef(AllocTimestamp = 8, AllocTimestamp, uint32_t)
MIBEntryDef(DeallocTimestamp = 9, DeallocTimestamp, uint32_t)
MIBEntryDef(TotalLifetime = 10, TotalLifetime, uint64_t)
MIBEntryDef(MinLifetime = 11, MinLifetime, uint32_t)
MIBEntryDef(MaxLifetime = 12, MaxLifetime, uint32_t)
MIBEntryDef(AllocCpuId = 13, AllocCpuId, uint32_t)
MIBEntryDef(DeallocCpuId = 14, DeallocCpuId, uint32_t)
MIBEntryDef(NumMigratedCpu = 15, NumMigratedCpu, uint32_t)
MIBEntryDef(NumLifetimeOverlaps = 16, NumLifetimeOverlaps, uint32_t)
MIBEntryDef(NumSameAllocCpu = 17, NumSameAllocCpu, uint32_t)
MIBEntryDef(NumSameDeallocCpu = 18, NumSameDeallocCpu, uint32_t)
MIBEntryDef(DataTypeId = 19, DataTypeId, uint64_t)
MIBEntryDef(TotalAccessDensity = 20, TotalAccessDensity, uint64_t)
MIBEntryDef(MinAccessDensity = 21, MinAccessDensity, uint32_t)
MIBEntryDef(MaxAccessDensity = 22, MaxAccessDensity, uint32_t)
MIBEntryDef(TotalLifetimeAccessDensity = 23, TotalLifetimeAccessDensity, uint64_t)
MIBEntryDef(MinLifetimeAccessDensity = 24, MinLifetimeAccessDensity, uint32_t)
MIBEntryDef(MaxLifetimeAccessDensity = 25, MaxLifetimeAccessDensity, uint32_t)
MIBEntryDef(AccessHistogramSize = 26, AccessHistogramSize, uint32_t)
MIBEntryDef(AccessHistogram = 27, AccessHistogram, uintptr_t)