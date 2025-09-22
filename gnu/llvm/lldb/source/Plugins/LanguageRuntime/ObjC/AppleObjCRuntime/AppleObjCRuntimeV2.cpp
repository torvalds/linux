//===-- AppleObjCRuntimeV2.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/DebuggerEvents.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionValueBoolean.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrameRecognizer.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"
#include "lldb/lldb-enumerations.h"

#include "AppleObjCClassDescriptorV2.h"
#include "AppleObjCDeclVendor.h"
#include "AppleObjCRuntimeV2.h"
#include "AppleObjCTrampolineHandler.h"
#include "AppleObjCTypeEncodingParser.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/ScopeExit.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace lldb;
using namespace lldb_private;

char AppleObjCRuntimeV2::ID = 0;

static const char *g_get_dynamic_class_info_name =
    "__lldb_apple_objc_v2_get_dynamic_class_info";

static const char *g_get_dynamic_class_info_body = R"(

extern "C"
{
    size_t strlen(const char *);
    char *strncpy (char * s1, const char * s2, size_t n);
    int printf(const char * format, ...);
}
#define DEBUG_PRINTF(fmt, ...) if (should_log) printf(fmt, ## __VA_ARGS__)

typedef struct _NXMapTable {
    void *prototype;
    unsigned num_classes;
    unsigned num_buckets_minus_one;
    void *buckets;
} NXMapTable;

#define NX_MAPNOTAKEY   ((void *)(-1))

typedef struct BucketInfo
{
    const char *name_ptr;
    Class isa;
} BucketInfo;

struct ClassInfo
{
    Class isa;
    uint32_t hash;
} __attribute__((__packed__));

uint32_t
__lldb_apple_objc_v2_get_dynamic_class_info (void *gdb_objc_realized_classes_ptr,
                                             void *class_infos_ptr,
                                             uint32_t class_infos_byte_size,
                                             uint32_t should_log)
{
    DEBUG_PRINTF ("gdb_objc_realized_classes_ptr = %p\n", gdb_objc_realized_classes_ptr);
    DEBUG_PRINTF ("class_infos_ptr = %p\n", class_infos_ptr);
    DEBUG_PRINTF ("class_infos_byte_size = %u\n", class_infos_byte_size);
    const NXMapTable *grc = (const NXMapTable *)gdb_objc_realized_classes_ptr;
    if (grc)
    {
        const unsigned num_classes = grc->num_classes;
        DEBUG_PRINTF ("num_classes = %u\n", grc->num_classes);
        if (class_infos_ptr)
        {
            const unsigned num_buckets_minus_one = grc->num_buckets_minus_one;
            DEBUG_PRINTF ("num_buckets_minus_one = %u\n", num_buckets_minus_one);

            const size_t max_class_infos = class_infos_byte_size/sizeof(ClassInfo);
            DEBUG_PRINTF ("max_class_infos = %u\n", max_class_infos);

            ClassInfo *class_infos = (ClassInfo *)class_infos_ptr;
            BucketInfo *buckets = (BucketInfo *)grc->buckets;

            uint32_t idx = 0;
            for (unsigned i=0; i<=num_buckets_minus_one; ++i)
            {
                if (buckets[i].name_ptr != NX_MAPNOTAKEY)
                {
                    if (idx < max_class_infos)
                    {
                        const char *s = buckets[i].name_ptr;
                        uint32_t h = 5381;
                        for (unsigned char c = *s; c; c = *++s)
                            h = ((h << 5) + h) + c;
                        class_infos[idx].hash = h;
                        class_infos[idx].isa = buckets[i].isa;
                        DEBUG_PRINTF ("[%u] isa = %8p %s\n", idx, class_infos[idx].isa, buckets[i].name_ptr);
                    }
                    ++idx;
                }
            }
            if (idx < max_class_infos)
            {
                class_infos[idx].isa = NULL;
                class_infos[idx].hash = 0;
            }
        }
        return num_classes;
    }
    return 0;
}

)";

static const char *g_get_dynamic_class_info2_name =
    "__lldb_apple_objc_v2_get_dynamic_class_info2";

static const char *g_get_dynamic_class_info2_body = R"(

extern "C" {
    int printf(const char * format, ...);
    void free(void *ptr);
    Class* objc_copyRealizedClassList_nolock(unsigned int *outCount);
    const char* objc_debug_class_getNameRaw(Class cls);
}

#define DEBUG_PRINTF(fmt, ...) if (should_log) printf(fmt, ## __VA_ARGS__)

struct ClassInfo
{
    Class isa;
    uint32_t hash;
} __attribute__((__packed__));

uint32_t
__lldb_apple_objc_v2_get_dynamic_class_info2(void *gdb_objc_realized_classes_ptr,
                                             void *class_infos_ptr,
                                             uint32_t class_infos_byte_size,
                                             uint32_t should_log)
{
    DEBUG_PRINTF ("class_infos_ptr = %p\n", class_infos_ptr);
    DEBUG_PRINTF ("class_infos_byte_size = %u\n", class_infos_byte_size);

    const size_t max_class_infos = class_infos_byte_size/sizeof(ClassInfo);
    DEBUG_PRINTF ("max_class_infos = %u\n", max_class_infos);

    ClassInfo *class_infos = (ClassInfo *)class_infos_ptr;

    uint32_t count = 0;
    Class* realized_class_list = objc_copyRealizedClassList_nolock(&count);
    DEBUG_PRINTF ("count = %u\n", count);

    uint32_t idx = 0;
    for (uint32_t i=0; i<count; ++i)
    {
        if (idx < max_class_infos)
        {
            Class isa = realized_class_list[i];
            const char *name_ptr = objc_debug_class_getNameRaw(isa);
            if (!name_ptr)
                continue;
            const char *s = name_ptr;
            uint32_t h = 5381;
            for (unsigned char c = *s; c; c = *++s)
                h = ((h << 5) + h) + c;
            class_infos[idx].hash = h;
            class_infos[idx].isa = isa;
            DEBUG_PRINTF ("[%u] isa = %8p %s\n", idx, class_infos[idx].isa, name_ptr);
        }
        idx++;
    }

    if (idx < max_class_infos)
    {
        class_infos[idx].isa = NULL;
        class_infos[idx].hash = 0;
    }

    free(realized_class_list);
    return count;
}
)";

static const char *g_get_dynamic_class_info3_name =
    "__lldb_apple_objc_v2_get_dynamic_class_info3";

static const char *g_get_dynamic_class_info3_body = R"(

extern "C" {
    int printf(const char * format, ...);
    void free(void *ptr);
    size_t objc_getRealizedClassList_trylock(Class *buffer, size_t len);
    const char* objc_debug_class_getNameRaw(Class cls);
    const char* class_getName(Class cls);
}

#define DEBUG_PRINTF(fmt, ...) if (should_log) printf(fmt, ## __VA_ARGS__)

struct ClassInfo
{
    Class isa;
    uint32_t hash;
} __attribute__((__packed__));

uint32_t
__lldb_apple_objc_v2_get_dynamic_class_info3(void *gdb_objc_realized_classes_ptr,
                                             void *class_infos_ptr,
                                             uint32_t class_infos_byte_size,
                                             void *class_buffer,
                                             uint32_t class_buffer_len,
                                             uint32_t should_log)
{
    DEBUG_PRINTF ("class_infos_ptr = %p\n", class_infos_ptr);
    DEBUG_PRINTF ("class_infos_byte_size = %u\n", class_infos_byte_size);

    const size_t max_class_infos = class_infos_byte_size/sizeof(ClassInfo);
    DEBUG_PRINTF ("max_class_infos = %u\n", max_class_infos);

    ClassInfo *class_infos = (ClassInfo *)class_infos_ptr;

    Class *realized_class_list = (Class*)class_buffer;

    uint32_t count = objc_getRealizedClassList_trylock(realized_class_list,
                                                       class_buffer_len);
    DEBUG_PRINTF ("count = %u\n", count);

    uint32_t idx = 0;
    for (uint32_t i=0; i<count; ++i)
    {
        if (idx < max_class_infos)
        {
            Class isa = realized_class_list[i];
            const char *name_ptr = objc_debug_class_getNameRaw(isa);
            if (!name_ptr) {
               class_getName(isa); // Realize name of lazy classes.
               name_ptr = objc_debug_class_getNameRaw(isa);
            }
            if (!name_ptr)
                continue;
            const char *s = name_ptr;
            uint32_t h = 5381;
            for (unsigned char c = *s; c; c = *++s)
                h = ((h << 5) + h) + c;
            class_infos[idx].hash = h;
            class_infos[idx].isa = isa;
            DEBUG_PRINTF ("[%u] isa = %8p %s\n", idx, class_infos[idx].isa, name_ptr);
        }
        idx++;
    }

    if (idx < max_class_infos)
    {
        class_infos[idx].isa = NULL;
        class_infos[idx].hash = 0;
    }

    return count;
}
)";

// We'll substitute in class_getName or class_getNameRaw depending
// on which is present.
static const char *g_shared_cache_class_name_funcptr = R"(
extern "C"
{
    const char *%s(void *objc_class);
    const char *(*class_name_lookup_func)(void *) = %s;
}
)";

static const char *g_get_shared_cache_class_info_name =
    "__lldb_apple_objc_v2_get_shared_cache_class_info";

static const char *g_get_shared_cache_class_info_body = R"(

extern "C"
{
    size_t strlen(const char *);
    char *strncpy (char * s1, const char * s2, size_t n);
    int printf(const char * format, ...);
}

#define DEBUG_PRINTF(fmt, ...) if (should_log) printf(fmt, ## __VA_ARGS__)


struct objc_classheader_t {
    int32_t clsOffset;
    int32_t hiOffset;
};

struct objc_classheader_v16_t {
    uint64_t isDuplicate       : 1,
             objectCacheOffset : 47, // Offset from the shared cache base
             dylibObjCIndex    : 16;
};

struct objc_clsopt_t {
    uint32_t capacity;
    uint32_t occupied;
    uint32_t shift;
    uint32_t mask;
    uint32_t zero;
    uint32_t unused;
    uint64_t salt;
    uint32_t scramble[256];
    uint8_t tab[0]; // tab[mask+1]
    //  uint8_t checkbytes[capacity];
    //  int32_t offset[capacity];
    //  objc_classheader_t clsOffsets[capacity];
    //  uint32_t duplicateCount;
    //  objc_classheader_t duplicateOffsets[duplicateCount];
};

struct objc_clsopt_v16_t {
   uint32_t version;
   uint32_t capacity;
   uint32_t occupied;
   uint32_t shift;
   uint32_t mask;
   uint32_t zero;
   uint64_t salt;
   uint32_t scramble[256];
   uint8_t  tab[0]; // tab[mask+1]
   //  uint8_t checkbytes[capacity];
   //  int32_t offset[capacity];
   //  objc_classheader_t clsOffsets[capacity];
   //  uint32_t duplicateCount;
   //  objc_classheader_t duplicateOffsets[duplicateCount];
};

struct objc_opt_t {
    uint32_t version;
    int32_t selopt_offset;
    int32_t headeropt_offset;
    int32_t clsopt_offset;
};

struct objc_opt_v14_t {
    uint32_t version;
    uint32_t flags;
    int32_t selopt_offset;
    int32_t headeropt_offset;
    int32_t clsopt_offset;
};

struct objc_opt_v16_t {
    uint32_t version;
    uint32_t flags;
    int32_t selopt_offset;
    int32_t headeropt_ro_offset;
    int32_t unused_clsopt_offset;
    int32_t unused_protocolopt_offset;
    int32_t headeropt_rw_offset;
    int32_t unused_protocolopt2_offset;
    int32_t largeSharedCachesClassOffset;
    int32_t largeSharedCachesProtocolOffset;
    uint64_t relativeMethodSelectorBaseAddressCacheOffset;
};

struct ClassInfo
{
    Class isa;
    uint32_t hash;
}  __attribute__((__packed__));

uint32_t
__lldb_apple_objc_v2_get_shared_cache_class_info (void *objc_opt_ro_ptr,
                                                  void *shared_cache_base_ptr,
                                                  void *class_infos_ptr,
                                                  uint64_t *relative_selector_offset,
                                                  uint32_t class_infos_byte_size,
                                                  uint32_t should_log)
{
    *relative_selector_offset = 0;
    uint32_t idx = 0;
    DEBUG_PRINTF ("objc_opt_ro_ptr = %p\n", objc_opt_ro_ptr);
    DEBUG_PRINTF ("shared_cache_base_ptr = %p\n", shared_cache_base_ptr);
    DEBUG_PRINTF ("class_infos_ptr = %p\n", class_infos_ptr);
    DEBUG_PRINTF ("class_infos_byte_size = %u (%llu class infos)\n", class_infos_byte_size, (uint64_t)(class_infos_byte_size/sizeof(ClassInfo)));
    if (objc_opt_ro_ptr)
    {
        const objc_opt_t *objc_opt = (objc_opt_t *)objc_opt_ro_ptr;
        const objc_opt_v14_t* objc_opt_v14 = (objc_opt_v14_t*)objc_opt_ro_ptr;
        const objc_opt_v16_t* objc_opt_v16 = (objc_opt_v16_t*)objc_opt_ro_ptr;
        if (objc_opt->version >= 16)
        {
            *relative_selector_offset = objc_opt_v16->relativeMethodSelectorBaseAddressCacheOffset;
            DEBUG_PRINTF ("objc_opt->version = %u\n", objc_opt_v16->version);
            DEBUG_PRINTF ("objc_opt->flags = %u\n", objc_opt_v16->flags);
            DEBUG_PRINTF ("objc_opt->selopt_offset = %d\n", objc_opt_v16->selopt_offset);
            DEBUG_PRINTF ("objc_opt->headeropt_ro_offset = %d\n", objc_opt_v16->headeropt_ro_offset);
            DEBUG_PRINTF ("objc_opt->relativeMethodSelectorBaseAddressCacheOffset = %d\n", *relative_selector_offset);
        }
        else if (objc_opt->version >= 14)
        {
            DEBUG_PRINTF ("objc_opt->version = %u\n", objc_opt_v14->version);
            DEBUG_PRINTF ("objc_opt->flags = %u\n", objc_opt_v14->flags);
            DEBUG_PRINTF ("objc_opt->selopt_offset = %d\n", objc_opt_v14->selopt_offset);
            DEBUG_PRINTF ("objc_opt->headeropt_offset = %d\n", objc_opt_v14->headeropt_offset);
            DEBUG_PRINTF ("objc_opt->clsopt_offset = %d\n", objc_opt_v14->clsopt_offset);
        }
        else
        {
            DEBUG_PRINTF ("objc_opt->version = %u\n", objc_opt->version);
            DEBUG_PRINTF ("objc_opt->selopt_offset = %d\n", objc_opt->selopt_offset);
            DEBUG_PRINTF ("objc_opt->headeropt_offset = %d\n", objc_opt->headeropt_offset);
            DEBUG_PRINTF ("objc_opt->clsopt_offset = %d\n", objc_opt->clsopt_offset);
        }

        if (objc_opt->version == 16)
        {
            const objc_clsopt_v16_t* clsopt = (const objc_clsopt_v16_t*)((uint8_t *)objc_opt + objc_opt_v16->largeSharedCachesClassOffset);
            const size_t max_class_infos = class_infos_byte_size/sizeof(ClassInfo);

            DEBUG_PRINTF("max_class_infos = %llu\n", (uint64_t)max_class_infos);

            ClassInfo *class_infos = (ClassInfo *)class_infos_ptr;

            const uint8_t *checkbytes = &clsopt->tab[clsopt->mask+1];
            const int32_t *offsets = (const int32_t *)(checkbytes + clsopt->capacity);
            const objc_classheader_v16_t *classOffsets = (const objc_classheader_v16_t *)(offsets + clsopt->capacity);

            DEBUG_PRINTF ("clsopt->capacity = %u\n", clsopt->capacity);
            DEBUG_PRINTF ("clsopt->mask = 0x%8.8x\n", clsopt->mask);
            DEBUG_PRINTF ("classOffsets = %p\n", classOffsets);

            for (uint32_t i=0; i<clsopt->capacity; ++i)
            {
                const uint64_t objectCacheOffset = classOffsets[i].objectCacheOffset;
                DEBUG_PRINTF("objectCacheOffset[%u] = %u\n", i, objectCacheOffset);

                if (classOffsets[i].isDuplicate) {
                    DEBUG_PRINTF("isDuplicate = true\n");
                    continue; // duplicate
                }

                if (objectCacheOffset == 0) {
                    DEBUG_PRINTF("objectCacheOffset == invalidEntryOffset\n");
                    continue; // invalid offset
                }

                if (class_infos && idx < max_class_infos)
                {
                    class_infos[idx].isa = (Class)((uint8_t *)shared_cache_base_ptr + objectCacheOffset);

                    // Lookup the class name.
                    const char *name = class_name_lookup_func(class_infos[idx].isa);
                    DEBUG_PRINTF("[%u] isa = %8p %s\n", idx, class_infos[idx].isa, name);

                    // Hash the class name so we don't have to read it.
                    const char *s = name;
                    uint32_t h = 5381;
                    for (unsigned char c = *s; c; c = *++s)
                    {
                        // class_getName demangles swift names and the hash must
                        // be calculated on the mangled name.  hash==0 means lldb
                        // will fetch the mangled name and compute the hash in
                        // ParseClassInfoArray.
                        if (c == '.')
                        {
                            h = 0;
                            break;
                        }
                        h = ((h << 5) + h) + c;
                    }
                    class_infos[idx].hash = h;
                }
                else
                {
                    DEBUG_PRINTF("not(class_infos && idx < max_class_infos)\n");
                }
                ++idx;
            }

            const uint32_t *duplicate_count_ptr = (uint32_t *)&classOffsets[clsopt->capacity];
            const uint32_t duplicate_count = *duplicate_count_ptr;
            const objc_classheader_v16_t *duplicateClassOffsets = (const objc_classheader_v16_t *)(&duplicate_count_ptr[1]);

            DEBUG_PRINTF ("duplicate_count = %u\n", duplicate_count);
            DEBUG_PRINTF ("duplicateClassOffsets = %p\n", duplicateClassOffsets);

            for (uint32_t i=0; i<duplicate_count; ++i)
            {
                const uint64_t objectCacheOffset = classOffsets[i].objectCacheOffset;
                DEBUG_PRINTF("objectCacheOffset[%u] = %u\n", i, objectCacheOffset);

                if (classOffsets[i].isDuplicate) {
                    DEBUG_PRINTF("isDuplicate = true\n");
                    continue; // duplicate
                }

                if (objectCacheOffset == 0) {
                    DEBUG_PRINTF("objectCacheOffset == invalidEntryOffset\n");
                    continue; // invalid offset
                }

                if (class_infos && idx < max_class_infos)
                {
                    class_infos[idx].isa = (Class)((uint8_t *)shared_cache_base_ptr + objectCacheOffset);

                    // Lookup the class name.
                    const char *name = class_name_lookup_func(class_infos[idx].isa);
                    DEBUG_PRINTF("[%u] isa = %8p %s\n", idx, class_infos[idx].isa, name);

                    // Hash the class name so we don't have to read it.
                    const char *s = name;
                    uint32_t h = 5381;
                    for (unsigned char c = *s; c; c = *++s)
                    {
                        // class_getName demangles swift names and the hash must
                        // be calculated on the mangled name.  hash==0 means lldb
                        // will fetch the mangled name and compute the hash in
                        // ParseClassInfoArray.
                        if (c == '.')
                        {
                            h = 0;
                            break;
                        }
                        h = ((h << 5) + h) + c;
                    }
                    class_infos[idx].hash = h;
                }
            }
        }
        else if (objc_opt->version >= 12 && objc_opt->version <= 15)
        {
            const objc_clsopt_t* clsopt = NULL;
            if (objc_opt->version >= 14)
                clsopt = (const objc_clsopt_t*)((uint8_t *)objc_opt_v14 + objc_opt_v14->clsopt_offset);
            else
                clsopt = (const objc_clsopt_t*)((uint8_t *)objc_opt + objc_opt->clsopt_offset);
            const size_t max_class_infos = class_infos_byte_size/sizeof(ClassInfo);
            DEBUG_PRINTF("max_class_infos = %llu\n", (uint64_t)max_class_infos);
            ClassInfo *class_infos = (ClassInfo *)class_infos_ptr;
            int32_t invalidEntryOffset = 0;
            // this is safe to do because the version field order is invariant
            if (objc_opt->version == 12)
                invalidEntryOffset = 16;
            const uint8_t *checkbytes = &clsopt->tab[clsopt->mask+1];
            const int32_t *offsets = (const int32_t *)(checkbytes + clsopt->capacity);
            const objc_classheader_t *classOffsets = (const objc_classheader_t *)(offsets + clsopt->capacity);
            DEBUG_PRINTF ("clsopt->capacity = %u\n", clsopt->capacity);
            DEBUG_PRINTF ("clsopt->mask = 0x%8.8x\n", clsopt->mask);
            DEBUG_PRINTF ("classOffsets = %p\n", classOffsets);
            DEBUG_PRINTF("invalidEntryOffset = %d\n", invalidEntryOffset);
            for (uint32_t i=0; i<clsopt->capacity; ++i)
            {
                const int32_t clsOffset = classOffsets[i].clsOffset;
                DEBUG_PRINTF("clsOffset[%u] = %u\n", i, clsOffset);
                if (clsOffset & 1)
                {
                    DEBUG_PRINTF("clsOffset & 1\n");
                    continue; // duplicate
                }
                else if (clsOffset == invalidEntryOffset)
                {
                    DEBUG_PRINTF("clsOffset == invalidEntryOffset\n");
                    continue; // invalid offset
                }

                if (class_infos && idx < max_class_infos)
                {
                    class_infos[idx].isa = (Class)((uint8_t *)clsopt + clsOffset);
                    const char *name = class_name_lookup_func (class_infos[idx].isa);
                    DEBUG_PRINTF ("[%u] isa = %8p %s\n", idx, class_infos[idx].isa, name);
                    // Hash the class name so we don't have to read it
                    const char *s = name;
                    uint32_t h = 5381;
                    for (unsigned char c = *s; c; c = *++s)
                    {
                        // class_getName demangles swift names and the hash must
                        // be calculated on the mangled name.  hash==0 means lldb
                        // will fetch the mangled name and compute the hash in
                        // ParseClassInfoArray.
                        if (c == '.')
                        {
                            h = 0;
                            break;
                        }
                        h = ((h << 5) + h) + c;
                    }
                    class_infos[idx].hash = h;
                }
                else
                {
                    DEBUG_PRINTF("not(class_infos && idx < max_class_infos)\n");
                }
                ++idx;
            }

            const uint32_t *duplicate_count_ptr = (uint32_t *)&classOffsets[clsopt->capacity];
            const uint32_t duplicate_count = *duplicate_count_ptr;
            const objc_classheader_t *duplicateClassOffsets = (const objc_classheader_t *)(&duplicate_count_ptr[1]);
            DEBUG_PRINTF ("duplicate_count = %u\n", duplicate_count);
            DEBUG_PRINTF ("duplicateClassOffsets = %p\n", duplicateClassOffsets);
            for (uint32_t i=0; i<duplicate_count; ++i)
            {
                const int32_t clsOffset = duplicateClassOffsets[i].clsOffset;
                if (clsOffset & 1)
                    continue; // duplicate
                else if (clsOffset == invalidEntryOffset)
                    continue; // invalid offset

                if (class_infos && idx < max_class_infos)
                {
                    class_infos[idx].isa = (Class)((uint8_t *)clsopt + clsOffset);
                    const char *name = class_name_lookup_func (class_infos[idx].isa);
                    DEBUG_PRINTF ("[%u] isa = %8p %s\n", idx, class_infos[idx].isa, name);
                    // Hash the class name so we don't have to read it
                    const char *s = name;
                    uint32_t h = 5381;
                    for (unsigned char c = *s; c; c = *++s)
                    {
                        // class_getName demangles swift names and the hash must
                        // be calculated on the mangled name.  hash==0 means lldb
                        // will fetch the mangled name and compute the hash in
                        // ParseClassInfoArray.
                        if (c == '.')
                        {
                            h = 0;
                            break;
                        }
                        h = ((h << 5) + h) + c;
                    }
                    class_infos[idx].hash = h;
                }
                ++idx;
            }
        }
        DEBUG_PRINTF ("%u class_infos\n", idx);
        DEBUG_PRINTF ("done\n");
    }
    return idx;
}


)";

static uint64_t
ExtractRuntimeGlobalSymbol(Process *process, ConstString name,
                           const ModuleSP &module_sp, Status &error,
                           bool read_value = true, uint8_t byte_size = 0,
                           uint64_t default_value = LLDB_INVALID_ADDRESS,
                           SymbolType sym_type = lldb::eSymbolTypeData) {
  if (!process) {
    error.SetErrorString("no process");
    return default_value;
  }

  if (!module_sp) {
    error.SetErrorString("no module");
    return default_value;
  }

  if (!byte_size)
    byte_size = process->GetAddressByteSize();
  const Symbol *symbol =
      module_sp->FindFirstSymbolWithNameAndType(name, lldb::eSymbolTypeData);

  if (!symbol || !symbol->ValueIsAddress()) {
    error.SetErrorString("no symbol");
    return default_value;
  }

  lldb::addr_t symbol_load_addr =
      symbol->GetAddressRef().GetLoadAddress(&process->GetTarget());
  if (symbol_load_addr == LLDB_INVALID_ADDRESS) {
    error.SetErrorString("symbol address invalid");
    return default_value;
  }

  if (read_value)
    return process->ReadUnsignedIntegerFromMemory(symbol_load_addr, byte_size,
                                                  default_value, error);
  return symbol_load_addr;
}

static void RegisterObjCExceptionRecognizer(Process *process);

AppleObjCRuntimeV2::AppleObjCRuntimeV2(Process *process,
                                       const ModuleSP &objc_module_sp)
    : AppleObjCRuntime(process), m_objc_module_sp(objc_module_sp),
      m_dynamic_class_info_extractor(*this),
      m_shared_cache_class_info_extractor(*this), m_decl_vendor_up(),
      m_tagged_pointer_obfuscator(LLDB_INVALID_ADDRESS),
      m_isa_hash_table_ptr(LLDB_INVALID_ADDRESS),
      m_relative_selector_base(LLDB_INVALID_ADDRESS), m_hash_signature(),
      m_has_object_getClass(false), m_has_objc_copyRealizedClassList(false),
      m_has_objc_getRealizedClassList_trylock(false), m_loaded_objc_opt(false),
      m_non_pointer_isa_cache_up(),
      m_tagged_pointer_vendor_up(
          TaggedPointerVendorV2::CreateInstance(*this, objc_module_sp)),
      m_encoding_to_type_sp(), m_CFBoolean_values(),
      m_realized_class_generation_count(0) {
  static const ConstString g_gdb_object_getClass("gdb_object_getClass");
  m_has_object_getClass = HasSymbol(g_gdb_object_getClass);
  static const ConstString g_objc_copyRealizedClassList(
      "_ZL33objc_copyRealizedClassList_nolockPj");
  static const ConstString g_objc_getRealizedClassList_trylock(
      "_objc_getRealizedClassList_trylock");
  m_has_objc_copyRealizedClassList = HasSymbol(g_objc_copyRealizedClassList);
  m_has_objc_getRealizedClassList_trylock =
      HasSymbol(g_objc_getRealizedClassList_trylock);
  WarnIfNoExpandedSharedCache();
  RegisterObjCExceptionRecognizer(process);
}

LanguageRuntime *
AppleObjCRuntimeV2::GetPreferredLanguageRuntime(ValueObject &in_value) {
  if (auto process_sp = in_value.GetProcessSP()) {
    assert(process_sp.get() == m_process);
    if (auto descriptor_sp = GetNonKVOClassDescriptor(in_value)) {
      LanguageType impl_lang = descriptor_sp->GetImplementationLanguage();
      if (impl_lang != eLanguageTypeUnknown)
        return process_sp->GetLanguageRuntime(impl_lang);
    }
  }
  return nullptr;
}

bool AppleObjCRuntimeV2::GetDynamicTypeAndAddress(
    ValueObject &in_value, lldb::DynamicValueType use_dynamic,
    TypeAndOrName &class_type_or_name, Address &address,
    Value::ValueType &value_type) {
  // We should never get here with a null process...
  assert(m_process != nullptr);

  // The Runtime is attached to a particular process, you shouldn't pass in a
  // value from another process. Note, however, the process might be NULL (e.g.
  // if the value was made with SBTarget::EvaluateExpression...) in which case
  // it is sufficient if the target's match:

  Process *process = in_value.GetProcessSP().get();
  if (process)
    assert(process == m_process);
  else
    assert(in_value.GetTargetSP().get() == m_process->CalculateTarget().get());

  class_type_or_name.Clear();
  value_type = Value::ValueType::Scalar;

  // Make sure we can have a dynamic value before starting...
  if (CouldHaveDynamicValue(in_value)) {
    // First job, pull out the address at 0 offset from the object  That will
    // be the ISA pointer.
    ClassDescriptorSP objc_class_sp(GetNonKVOClassDescriptor(in_value));
    if (objc_class_sp) {
      const addr_t object_ptr = in_value.GetPointerValue();
      address.SetRawAddress(object_ptr);

      ConstString class_name(objc_class_sp->GetClassName());
      class_type_or_name.SetName(class_name);
      TypeSP type_sp(objc_class_sp->GetType());
      if (type_sp)
        class_type_or_name.SetTypeSP(type_sp);
      else {
        type_sp = LookupInCompleteClassCache(class_name);
        if (type_sp) {
          objc_class_sp->SetType(type_sp);
          class_type_or_name.SetTypeSP(type_sp);
        } else {
          // try to go for a CompilerType at least
          if (auto *vendor = GetDeclVendor()) {
            auto types = vendor->FindTypes(class_name, /*max_matches*/ 1);
            if (!types.empty())
              class_type_or_name.SetCompilerType(types.front());
          }
        }
      }
    }
  }
  return !class_type_or_name.IsEmpty();
}

// Static Functions
LanguageRuntime *AppleObjCRuntimeV2::CreateInstance(Process *process,
                                                    LanguageType language) {
  // FIXME: This should be a MacOS or iOS process, and we need to look for the
  // OBJC section to make
  // sure we aren't using the V1 runtime.
  if (language == eLanguageTypeObjC) {
    ModuleSP objc_module_sp;

    if (AppleObjCRuntime::GetObjCVersion(process, objc_module_sp) ==
        ObjCRuntimeVersions::eAppleObjC_V2)
      return new AppleObjCRuntimeV2(process, objc_module_sp);
    return nullptr;
  }
  return nullptr;
}

static constexpr OptionDefinition g_objc_classtable_dump_options[] = {
    {LLDB_OPT_SET_ALL,
     false,
     "verbose",
     'v',
     OptionParser::eNoArgument,
     nullptr,
     {},
     0,
     eArgTypeNone,
     "Print ivar and method information in detail"}};

class CommandObjectObjC_ClassTable_Dump : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options(), m_verbose(false, false) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;
      switch (short_option) {
      case 'v':
        m_verbose.SetCurrentValue(true);
        m_verbose.SetOptionWasSet();
        break;

      default:
        error.SetErrorStringWithFormat("unrecognized short option '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_verbose.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_objc_classtable_dump_options);
    }

    OptionValueBoolean m_verbose;
  };

  CommandObjectObjC_ClassTable_Dump(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "dump",
                            "Dump information on Objective-C classes "
                            "known to the current process.",
                            "language objc class-table dump",
                            eCommandRequiresProcess |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused),
        m_options() {
    AddSimpleArgumentList(eArgTypeRegularExpression, eArgRepeatOptional);
  }

  ~CommandObjectObjC_ClassTable_Dump() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    std::unique_ptr<RegularExpression> regex_up;
    switch (command.GetArgumentCount()) {
    case 0:
      break;
    case 1: {
      regex_up =
          std::make_unique<RegularExpression>(command.GetArgumentAtIndex(0));
      if (!regex_up->IsValid()) {
        result.AppendError(
            "invalid argument - please provide a valid regular expression");
        result.SetStatus(lldb::eReturnStatusFailed);
        return;
      }
      break;
    }
    default: {
      result.AppendError("please provide 0 or 1 arguments");
      result.SetStatus(lldb::eReturnStatusFailed);
      return;
    }
    }

    Process *process = m_exe_ctx.GetProcessPtr();
    ObjCLanguageRuntime *objc_runtime = ObjCLanguageRuntime::Get(*process);
    if (objc_runtime) {
      auto iterators_pair = objc_runtime->GetDescriptorIteratorPair();
      auto iterator = iterators_pair.first;
      auto &std_out = result.GetOutputStream();
      for (; iterator != iterators_pair.second; iterator++) {
        if (iterator->second) {
          const char *class_name =
              iterator->second->GetClassName().AsCString("<unknown>");
          if (regex_up && class_name &&
              !regex_up->Execute(llvm::StringRef(class_name)))
            continue;
          std_out.Printf("isa = 0x%" PRIx64, iterator->first);
          std_out.Printf(" name = %s", class_name);
          std_out.Printf(" instance size = %" PRIu64,
                         iterator->second->GetInstanceSize());
          std_out.Printf(" num ivars = %" PRIuPTR,
                         (uintptr_t)iterator->second->GetNumIVars());
          if (auto superclass = iterator->second->GetSuperclass()) {
            std_out.Printf(" superclass = %s",
                           superclass->GetClassName().AsCString("<unknown>"));
          }
          std_out.Printf("\n");
          if (m_options.m_verbose) {
            for (size_t i = 0; i < iterator->second->GetNumIVars(); i++) {
              auto ivar = iterator->second->GetIVarAtIndex(i);
              std_out.Printf(
                  "  ivar name = %s type = %s size = %" PRIu64
                  " offset = %" PRId32 "\n",
                  ivar.m_name.AsCString("<unknown>"),
                  ivar.m_type.GetDisplayTypeName().AsCString("<unknown>"),
                  ivar.m_size, ivar.m_offset);
            }

            iterator->second->Describe(
                nullptr,
                [&std_out](const char *name, const char *type) -> bool {
                  std_out.Printf("  instance method name = %s type = %s\n",
                                 name, type);
                  return false;
                },
                [&std_out](const char *name, const char *type) -> bool {
                  std_out.Printf("  class method name = %s type = %s\n", name,
                                 type);
                  return false;
                },
                nullptr);
          }
        } else {
          if (regex_up && !regex_up->Execute(llvm::StringRef()))
            continue;
          std_out.Printf("isa = 0x%" PRIx64 " has no associated class.\n",
                         iterator->first);
        }
      }
      result.SetStatus(lldb::eReturnStatusSuccessFinishResult);
      return;
    }
    result.AppendError("current process has no Objective-C runtime loaded");
    result.SetStatus(lldb::eReturnStatusFailed);
  }

  CommandOptions m_options;
};

class CommandObjectMultiwordObjC_TaggedPointer_Info
    : public CommandObjectParsed {
public:
  CommandObjectMultiwordObjC_TaggedPointer_Info(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "info", "Dump information on a tagged pointer.",
            "language objc tagged-pointer info",
            eCommandRequiresProcess | eCommandProcessMustBeLaunched |
                eCommandProcessMustBePaused) {
    AddSimpleArgumentList(eArgTypeAddress, eArgRepeatPlus);
  }

  ~CommandObjectMultiwordObjC_TaggedPointer_Info() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    if (command.GetArgumentCount() == 0) {
      result.AppendError("this command requires arguments");
      result.SetStatus(lldb::eReturnStatusFailed);
      return;
    }

    Process *process = m_exe_ctx.GetProcessPtr();
    ExecutionContext exe_ctx(process);

    ObjCLanguageRuntime *objc_runtime = ObjCLanguageRuntime::Get(*process);
    if (!objc_runtime) {
      result.AppendError("current process has no Objective-C runtime loaded");
      result.SetStatus(lldb::eReturnStatusFailed);
      return;
    }

    ObjCLanguageRuntime::TaggedPointerVendor *tagged_ptr_vendor =
        objc_runtime->GetTaggedPointerVendor();
    if (!tagged_ptr_vendor) {
      result.AppendError("current process has no tagged pointer support");
      result.SetStatus(lldb::eReturnStatusFailed);
      return;
    }

    for (size_t i = 0; i < command.GetArgumentCount(); i++) {
      const char *arg_str = command.GetArgumentAtIndex(i);
      if (!arg_str)
        continue;

      Status error;
      lldb::addr_t arg_addr = OptionArgParser::ToAddress(
          &exe_ctx, arg_str, LLDB_INVALID_ADDRESS, &error);
      if (arg_addr == 0 || arg_addr == LLDB_INVALID_ADDRESS || error.Fail()) {
        result.AppendErrorWithFormatv(
            "could not convert '{0}' to a valid address\n", arg_str);
        result.SetStatus(lldb::eReturnStatusFailed);
        return;
      }

      if (!tagged_ptr_vendor->IsPossibleTaggedPointer(arg_addr)) {
        result.GetOutputStream().Format("{0:x16} is not tagged\n", arg_addr);
        continue;
      }

      auto descriptor_sp = tagged_ptr_vendor->GetClassDescriptor(arg_addr);
      if (!descriptor_sp) {
        result.AppendErrorWithFormatv(
            "could not get class descriptor for {0:x16}\n", arg_addr);
        result.SetStatus(lldb::eReturnStatusFailed);
        return;
      }

      uint64_t info_bits = 0;
      uint64_t value_bits = 0;
      uint64_t payload = 0;
      if (descriptor_sp->GetTaggedPointerInfo(&info_bits, &value_bits,
                                              &payload)) {
        result.GetOutputStream().Format(
            "{0:x} is tagged\n"
            "\tpayload = {1:x16}\n"
            "\tvalue = {2:x16}\n"
            "\tinfo bits = {3:x16}\n"
            "\tclass = {4}\n",
            arg_addr, payload, value_bits, info_bits,
            descriptor_sp->GetClassName().AsCString("<unknown>"));
      } else {
        result.GetOutputStream().Format("{0:x16} is not tagged\n", arg_addr);
      }
    }

    result.SetStatus(lldb::eReturnStatusSuccessFinishResult);
  }
};

class CommandObjectMultiwordObjC_ClassTable : public CommandObjectMultiword {
public:
  CommandObjectMultiwordObjC_ClassTable(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "class-table",
            "Commands for operating on the Objective-C class table.",
            "class-table <subcommand> [<subcommand-options>]") {
    LoadSubCommand(
        "dump",
        CommandObjectSP(new CommandObjectObjC_ClassTable_Dump(interpreter)));
  }

  ~CommandObjectMultiwordObjC_ClassTable() override = default;
};

class CommandObjectMultiwordObjC_TaggedPointer : public CommandObjectMultiword {
public:
  CommandObjectMultiwordObjC_TaggedPointer(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "tagged-pointer",
            "Commands for operating on Objective-C tagged pointers.",
            "class-table <subcommand> [<subcommand-options>]") {
    LoadSubCommand(
        "info",
        CommandObjectSP(
            new CommandObjectMultiwordObjC_TaggedPointer_Info(interpreter)));
  }

  ~CommandObjectMultiwordObjC_TaggedPointer() override = default;
};

class CommandObjectMultiwordObjC : public CommandObjectMultiword {
public:
  CommandObjectMultiwordObjC(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "objc",
            "Commands for operating on the Objective-C language runtime.",
            "objc <subcommand> [<subcommand-options>]") {
    LoadSubCommand("class-table",
                   CommandObjectSP(
                       new CommandObjectMultiwordObjC_ClassTable(interpreter)));
    LoadSubCommand("tagged-pointer",
                   CommandObjectSP(new CommandObjectMultiwordObjC_TaggedPointer(
                       interpreter)));
  }

  ~CommandObjectMultiwordObjC() override = default;
};

void AppleObjCRuntimeV2::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "Apple Objective-C Language Runtime - Version 2",
      CreateInstance,
      [](CommandInterpreter &interpreter) -> lldb::CommandObjectSP {
        return CommandObjectSP(new CommandObjectMultiwordObjC(interpreter));
      },
      GetBreakpointExceptionPrecondition);
}

void AppleObjCRuntimeV2::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

BreakpointResolverSP
AppleObjCRuntimeV2::CreateExceptionResolver(const BreakpointSP &bkpt,
                                            bool catch_bp, bool throw_bp) {
  BreakpointResolverSP resolver_sp;

  if (throw_bp)
    resolver_sp = std::make_shared<BreakpointResolverName>(
        bkpt, std::get<1>(GetExceptionThrowLocation()).AsCString(),
        eFunctionNameTypeBase, eLanguageTypeUnknown, Breakpoint::Exact, 0,
        eLazyBoolNo);
  // FIXME: We don't do catch breakpoints for ObjC yet.
  // Should there be some way for the runtime to specify what it can do in this
  // regard?
  return resolver_sp;
}

llvm::Expected<std::unique_ptr<UtilityFunction>>
AppleObjCRuntimeV2::CreateObjectChecker(std::string name,
                                        ExecutionContext &exe_ctx) {
  char check_function_code[2048];

  int len = 0;
  if (m_has_object_getClass) {
    len = ::snprintf(check_function_code, sizeof(check_function_code), R"(
                     extern "C" void *gdb_object_getClass(void *);
                     extern "C" int printf(const char *format, ...);
                     extern "C" void
                     %s(void *$__lldb_arg_obj, void *$__lldb_arg_selector) {
                       if ($__lldb_arg_obj == (void *)0)
                         return; // nil is ok
                       if (!gdb_object_getClass($__lldb_arg_obj)) {
                         *((volatile int *)0) = 'ocgc';
                       } else if ($__lldb_arg_selector != (void *)0) {
                         signed char $responds = (signed char)
                             [(id)$__lldb_arg_obj respondsToSelector:
                                 (void *) $__lldb_arg_selector];
                         if ($responds == (signed char) 0)
                           *((volatile int *)0) = 'ocgc';
                       }
                     })",
                     name.c_str());
  } else {
    len = ::snprintf(check_function_code, sizeof(check_function_code), R"(
                     extern "C" void *gdb_class_getClass(void *);
                     extern "C" int printf(const char *format, ...);
                     extern "C" void
                     %s(void *$__lldb_arg_obj, void *$__lldb_arg_selector) {
                       if ($__lldb_arg_obj == (void *)0)
                         return; // nil is ok
                       void **$isa_ptr = (void **)$__lldb_arg_obj;
                       if (*$isa_ptr == (void *)0 ||
                           !gdb_class_getClass(*$isa_ptr))
                         *((volatile int *)0) = 'ocgc';
                       else if ($__lldb_arg_selector != (void *)0) {
                         signed char $responds = (signed char)
                             [(id)$__lldb_arg_obj respondsToSelector:
                                 (void *) $__lldb_arg_selector];
                         if ($responds == (signed char) 0)
                           *((volatile int *)0) = 'ocgc';
                       }
                     })",
                     name.c_str());
  }

  assert(len < (int)sizeof(check_function_code));
  UNUSED_IF_ASSERT_DISABLED(len);

  return GetTargetRef().CreateUtilityFunction(check_function_code, name,
                                              eLanguageTypeC, exe_ctx);
}

size_t AppleObjCRuntimeV2::GetByteOffsetForIvar(CompilerType &parent_ast_type,
                                                const char *ivar_name) {
  uint32_t ivar_offset = LLDB_INVALID_IVAR_OFFSET;

  ConstString class_name = parent_ast_type.GetTypeName();
  if (!class_name.IsEmpty() && ivar_name && ivar_name[0]) {
    // Make the objective C V2 mangled name for the ivar offset from the class
    // name and ivar name
    std::string buffer("OBJC_IVAR_$_");
    buffer.append(class_name.AsCString());
    buffer.push_back('.');
    buffer.append(ivar_name);
    ConstString ivar_const_str(buffer.c_str());

    // Try to get the ivar offset address from the symbol table first using the
    // name we created above
    SymbolContextList sc_list;
    Target &target = m_process->GetTarget();
    target.GetImages().FindSymbolsWithNameAndType(ivar_const_str,
                                                  eSymbolTypeObjCIVar, sc_list);

    addr_t ivar_offset_address = LLDB_INVALID_ADDRESS;

    Status error;
    SymbolContext ivar_offset_symbol;
    if (sc_list.GetSize() == 1 &&
        sc_list.GetContextAtIndex(0, ivar_offset_symbol)) {
      if (ivar_offset_symbol.symbol)
        ivar_offset_address =
            ivar_offset_symbol.symbol->GetLoadAddress(&target);
    }

    // If we didn't get the ivar offset address from the symbol table, fall
    // back to getting it from the runtime
    if (ivar_offset_address == LLDB_INVALID_ADDRESS)
      ivar_offset_address = LookupRuntimeSymbol(ivar_const_str);

    if (ivar_offset_address != LLDB_INVALID_ADDRESS)
      ivar_offset = m_process->ReadUnsignedIntegerFromMemory(
          ivar_offset_address, 4, LLDB_INVALID_IVAR_OFFSET, error);
  }
  return ivar_offset;
}

// tagged pointers are special not-a-real-pointer values that contain both type
// and value information this routine attempts to check with as little
// computational effort as possible whether something could possibly be a
// tagged pointer - false positives are possible but false negatives shouldn't
bool AppleObjCRuntimeV2::IsTaggedPointer(addr_t ptr) {
  if (!m_tagged_pointer_vendor_up)
    return false;
  return m_tagged_pointer_vendor_up->IsPossibleTaggedPointer(ptr);
}

class RemoteNXMapTable {
public:
  RemoteNXMapTable() : m_end_iterator(*this, -1) {}

  void Dump() {
    printf("RemoteNXMapTable.m_load_addr = 0x%" PRIx64 "\n", m_load_addr);
    printf("RemoteNXMapTable.m_count = %u\n", m_count);
    printf("RemoteNXMapTable.m_num_buckets_minus_one = %u\n",
           m_num_buckets_minus_one);
    printf("RemoteNXMapTable.m_buckets_ptr = 0x%" PRIX64 "\n", m_buckets_ptr);
  }

  bool ParseHeader(Process *process, lldb::addr_t load_addr) {
    m_process = process;
    m_load_addr = load_addr;
    m_map_pair_size = m_process->GetAddressByteSize() * 2;
    m_invalid_key =
        m_process->GetAddressByteSize() == 8 ? UINT64_MAX : UINT32_MAX;
    Status err;

    // This currently holds true for all platforms we support, but we might
    // need to change this to use get the actually byte size of "unsigned" from
    // the target AST...
    const uint32_t unsigned_byte_size = sizeof(uint32_t);
    // Skip the prototype as we don't need it (const struct
    // +NXMapTablePrototype *prototype)

    bool success = true;
    if (load_addr == LLDB_INVALID_ADDRESS)
      success = false;
    else {
      lldb::addr_t cursor = load_addr + m_process->GetAddressByteSize();

      // unsigned count;
      m_count = m_process->ReadUnsignedIntegerFromMemory(
          cursor, unsigned_byte_size, 0, err);
      if (m_count) {
        cursor += unsigned_byte_size;

        // unsigned nbBucketsMinusOne;
        m_num_buckets_minus_one = m_process->ReadUnsignedIntegerFromMemory(
            cursor, unsigned_byte_size, 0, err);
        cursor += unsigned_byte_size;

        // void *buckets;
        m_buckets_ptr = m_process->ReadPointerFromMemory(cursor, err);

        success = m_count > 0 && m_buckets_ptr != LLDB_INVALID_ADDRESS;
      }
    }

    if (!success) {
      m_count = 0;
      m_num_buckets_minus_one = 0;
      m_buckets_ptr = LLDB_INVALID_ADDRESS;
    }
    return success;
  }

  // const_iterator mimics NXMapState and its code comes from NXInitMapState
  // and NXNextMapState.
  typedef std::pair<ConstString, ObjCLanguageRuntime::ObjCISA> element;

  friend class const_iterator;
  class const_iterator {
  public:
    const_iterator(RemoteNXMapTable &parent, int index)
        : m_parent(parent), m_index(index) {
      AdvanceToValidIndex();
    }

    const_iterator(const const_iterator &rhs)
        : m_parent(rhs.m_parent), m_index(rhs.m_index) {
      // AdvanceToValidIndex() has been called by rhs already.
    }

    const_iterator &operator=(const const_iterator &rhs) {
      // AdvanceToValidIndex() has been called by rhs already.
      assert(&m_parent == &rhs.m_parent);
      m_index = rhs.m_index;
      return *this;
    }

    bool operator==(const const_iterator &rhs) const {
      if (&m_parent != &rhs.m_parent)
        return false;
      if (m_index != rhs.m_index)
        return false;

      return true;
    }

    bool operator!=(const const_iterator &rhs) const {
      return !(operator==(rhs));
    }

    const_iterator &operator++() {
      AdvanceToValidIndex();
      return *this;
    }

    element operator*() const {
      if (m_index == -1) {
        // TODO find a way to make this an error, but not an assert
        return element();
      }

      lldb::addr_t pairs_ptr = m_parent.m_buckets_ptr;
      size_t map_pair_size = m_parent.m_map_pair_size;
      lldb::addr_t pair_ptr = pairs_ptr + (m_index * map_pair_size);

      Status err;

      lldb::addr_t key =
          m_parent.m_process->ReadPointerFromMemory(pair_ptr, err);
      if (!err.Success())
        return element();
      lldb::addr_t value = m_parent.m_process->ReadPointerFromMemory(
          pair_ptr + m_parent.m_process->GetAddressByteSize(), err);
      if (!err.Success())
        return element();

      std::string key_string;

      m_parent.m_process->ReadCStringFromMemory(key, key_string, err);
      if (!err.Success())
        return element();

      return element(ConstString(key_string.c_str()),
                     (ObjCLanguageRuntime::ObjCISA)value);
    }

  private:
    void AdvanceToValidIndex() {
      if (m_index == -1)
        return;

      const lldb::addr_t pairs_ptr = m_parent.m_buckets_ptr;
      const size_t map_pair_size = m_parent.m_map_pair_size;
      const lldb::addr_t invalid_key = m_parent.m_invalid_key;
      Status err;

      while (m_index--) {
        lldb::addr_t pair_ptr = pairs_ptr + (m_index * map_pair_size);
        lldb::addr_t key =
            m_parent.m_process->ReadPointerFromMemory(pair_ptr, err);

        if (!err.Success()) {
          m_index = -1;
          return;
        }

        if (key != invalid_key)
          return;
      }
    }
    RemoteNXMapTable &m_parent;
    int m_index;
  };

  const_iterator begin() {
    return const_iterator(*this, m_num_buckets_minus_one + 1);
  }

  const_iterator end() { return m_end_iterator; }

  uint32_t GetCount() const { return m_count; }

  uint32_t GetBucketCount() const { return m_num_buckets_minus_one; }

  lldb::addr_t GetBucketDataPointer() const { return m_buckets_ptr; }

  lldb::addr_t GetTableLoadAddress() const { return m_load_addr; }

private:
  // contents of _NXMapTable struct
  uint32_t m_count = 0;
  uint32_t m_num_buckets_minus_one = 0;
  lldb::addr_t m_buckets_ptr = LLDB_INVALID_ADDRESS;
  lldb_private::Process *m_process = nullptr;
  const_iterator m_end_iterator;
  lldb::addr_t m_load_addr = LLDB_INVALID_ADDRESS;
  size_t m_map_pair_size = 0;
  lldb::addr_t m_invalid_key = 0;
};

AppleObjCRuntimeV2::HashTableSignature::HashTableSignature() = default;

void AppleObjCRuntimeV2::HashTableSignature::UpdateSignature(
    const RemoteNXMapTable &hash_table) {
  m_count = hash_table.GetCount();
  m_num_buckets = hash_table.GetBucketCount();
  m_buckets_ptr = hash_table.GetBucketDataPointer();
}

bool AppleObjCRuntimeV2::HashTableSignature::NeedsUpdate(
    Process *process, AppleObjCRuntimeV2 *runtime,
    RemoteNXMapTable &hash_table) {
  if (!hash_table.ParseHeader(process, runtime->GetISAHashTablePointer())) {
    return false; // Failed to parse the header, no need to update anything
  }

  // Check with out current signature and return true if the count, number of
  // buckets or the hash table address changes.
  if (m_count == hash_table.GetCount() &&
      m_num_buckets == hash_table.GetBucketCount() &&
      m_buckets_ptr == hash_table.GetBucketDataPointer()) {
    // Hash table hasn't changed
    return false;
  }
  // Hash table data has changed, we need to update
  return true;
}

ObjCLanguageRuntime::ClassDescriptorSP
AppleObjCRuntimeV2::GetClassDescriptorFromISA(ObjCISA isa) {
  ObjCLanguageRuntime::ClassDescriptorSP class_descriptor_sp;
  if (auto *non_pointer_isa_cache = GetNonPointerIsaCache())
    class_descriptor_sp = non_pointer_isa_cache->GetClassDescriptor(isa);
  if (!class_descriptor_sp)
    class_descriptor_sp = ObjCLanguageRuntime::GetClassDescriptorFromISA(isa);
  return class_descriptor_sp;
}

ObjCLanguageRuntime::ClassDescriptorSP
AppleObjCRuntimeV2::GetClassDescriptor(ValueObject &valobj) {
  ClassDescriptorSP objc_class_sp;
  if (valobj.IsBaseClass()) {
    ValueObject *parent = valobj.GetParent();
    // if I am my own parent, bail out of here fast..
    if (parent && parent != &valobj) {
      ClassDescriptorSP parent_descriptor_sp = GetClassDescriptor(*parent);
      if (parent_descriptor_sp)
        return parent_descriptor_sp->GetSuperclass();
    }
    return nullptr;
  }
  // if we get an invalid VO (which might still happen when playing around with
  // pointers returned by the expression parser, don't consider this a valid
  // ObjC object)
  if (!valobj.GetCompilerType().IsValid())
    return objc_class_sp;
  addr_t isa_pointer = valobj.GetPointerValue();

  // tagged pointer
  if (IsTaggedPointer(isa_pointer))
    return m_tagged_pointer_vendor_up->GetClassDescriptor(isa_pointer);
  ExecutionContext exe_ctx(valobj.GetExecutionContextRef());

  Process *process = exe_ctx.GetProcessPtr();
  if (!process)
    return objc_class_sp;

  Status error;
  ObjCISA isa = process->ReadPointerFromMemory(isa_pointer, error);
  if (isa == LLDB_INVALID_ADDRESS)
    return objc_class_sp;

  objc_class_sp = GetClassDescriptorFromISA(isa);
  if (!objc_class_sp) {
    if (ABISP abi_sp = process->GetABI())
      isa = abi_sp->FixCodeAddress(isa);
    objc_class_sp = GetClassDescriptorFromISA(isa);
  }

  if (isa && !objc_class_sp) {
    Log *log = GetLog(LLDBLog::Process | LLDBLog::Types);
    LLDB_LOGF(log,
              "0x%" PRIx64 ": AppleObjCRuntimeV2::GetClassDescriptor() ISA was "
              "not in class descriptor cache 0x%" PRIx64,
              isa_pointer, isa);
  }
  return objc_class_sp;
}

lldb::addr_t AppleObjCRuntimeV2::GetTaggedPointerObfuscator() {
  if (m_tagged_pointer_obfuscator != LLDB_INVALID_ADDRESS)
    return m_tagged_pointer_obfuscator;

  Process *process = GetProcess();
  ModuleSP objc_module_sp(GetObjCModule());

  if (!objc_module_sp)
    return LLDB_INVALID_ADDRESS;

  static ConstString g_gdb_objc_obfuscator(
      "objc_debug_taggedpointer_obfuscator");

  const Symbol *symbol = objc_module_sp->FindFirstSymbolWithNameAndType(
      g_gdb_objc_obfuscator, lldb::eSymbolTypeAny);
  if (symbol) {
    lldb::addr_t g_gdb_obj_obfuscator_ptr =
        symbol->GetLoadAddress(&process->GetTarget());

    if (g_gdb_obj_obfuscator_ptr != LLDB_INVALID_ADDRESS) {
      Status error;
      m_tagged_pointer_obfuscator =
          process->ReadPointerFromMemory(g_gdb_obj_obfuscator_ptr, error);
    }
  }
  // If we don't have a correct value at this point, there must be no
  // obfuscation.
  if (m_tagged_pointer_obfuscator == LLDB_INVALID_ADDRESS)
    m_tagged_pointer_obfuscator = 0;

  return m_tagged_pointer_obfuscator;
}

lldb::addr_t AppleObjCRuntimeV2::GetISAHashTablePointer() {
  if (m_isa_hash_table_ptr == LLDB_INVALID_ADDRESS) {
    Process *process = GetProcess();

    ModuleSP objc_module_sp(GetObjCModule());

    if (!objc_module_sp)
      return LLDB_INVALID_ADDRESS;

    static ConstString g_gdb_objc_realized_classes("gdb_objc_realized_classes");

    const Symbol *symbol = objc_module_sp->FindFirstSymbolWithNameAndType(
        g_gdb_objc_realized_classes, lldb::eSymbolTypeAny);
    if (symbol) {
      lldb::addr_t gdb_objc_realized_classes_ptr =
          symbol->GetLoadAddress(&process->GetTarget());

      if (gdb_objc_realized_classes_ptr != LLDB_INVALID_ADDRESS) {
        Status error;
        m_isa_hash_table_ptr = process->ReadPointerFromMemory(
            gdb_objc_realized_classes_ptr, error);
      }
    }
  }
  return m_isa_hash_table_ptr;
}

std::unique_ptr<AppleObjCRuntimeV2::SharedCacheImageHeaders>
AppleObjCRuntimeV2::SharedCacheImageHeaders::CreateSharedCacheImageHeaders(
    AppleObjCRuntimeV2 &runtime) {
  Log *log = GetLog(LLDBLog::Process | LLDBLog::Types);
  Process *process = runtime.GetProcess();
  ModuleSP objc_module_sp(runtime.GetObjCModule());
  if (!objc_module_sp || !process)
    return nullptr;

  const Symbol *symbol = objc_module_sp->FindFirstSymbolWithNameAndType(
      ConstString("objc_debug_headerInfoRWs"), lldb::eSymbolTypeAny);
  if (!symbol) {
    LLDB_LOG(log, "Symbol 'objc_debug_headerInfoRWs' unavailable. Some "
                  "information concerning the shared cache may be unavailable");
    return nullptr;
  }

  lldb::addr_t objc_debug_headerInfoRWs_addr =
      symbol->GetLoadAddress(&process->GetTarget());
  if (objc_debug_headerInfoRWs_addr == LLDB_INVALID_ADDRESS) {
    LLDB_LOG(log, "Symbol 'objc_debug_headerInfoRWs' was found but we were "
                  "unable to get its load address");
    return nullptr;
  }

  Status error;
  lldb::addr_t objc_debug_headerInfoRWs_ptr =
      process->ReadPointerFromMemory(objc_debug_headerInfoRWs_addr, error);
  if (error.Fail()) {
    LLDB_LOG(log,
             "Failed to read address of 'objc_debug_headerInfoRWs' at {0:x}",
             objc_debug_headerInfoRWs_addr);
    return nullptr;
  }

  const size_t metadata_size =
      sizeof(uint32_t) + sizeof(uint32_t); // count + entsize
  DataBufferHeap metadata_buffer(metadata_size, '\0');
  process->ReadMemory(objc_debug_headerInfoRWs_ptr, metadata_buffer.GetBytes(),
                      metadata_size, error);
  if (error.Fail()) {
    LLDB_LOG(log,
             "Unable to read metadata for 'objc_debug_headerInfoRWs' at {0:x}",
             objc_debug_headerInfoRWs_ptr);
    return nullptr;
  }

  DataExtractor metadata_extractor(metadata_buffer.GetBytes(), metadata_size,
                                   process->GetByteOrder(),
                                   process->GetAddressByteSize());
  lldb::offset_t cursor = 0;
  uint32_t count = metadata_extractor.GetU32_unchecked(&cursor);
  uint32_t entsize = metadata_extractor.GetU32_unchecked(&cursor);
  if (count == 0 || entsize == 0) {
    LLDB_LOG(log,
             "'objc_debug_headerInfoRWs' had count {0} with entsize {1}. These "
             "should both be non-zero.",
             count, entsize);
    return nullptr;
  }

  std::unique_ptr<SharedCacheImageHeaders> shared_cache_image_headers(
      new SharedCacheImageHeaders(runtime, objc_debug_headerInfoRWs_ptr, count,
                                  entsize));
  if (auto Err = shared_cache_image_headers->UpdateIfNeeded()) {
    LLDB_LOG_ERROR(log, std::move(Err),
                   "Failed to update SharedCacheImageHeaders: {0}");
    return nullptr;
  }

  return shared_cache_image_headers;
}

llvm::Error AppleObjCRuntimeV2::SharedCacheImageHeaders::UpdateIfNeeded() {
  if (!m_needs_update)
    return llvm::Error::success();

  Process *process = m_runtime.GetProcess();
  constexpr lldb::addr_t metadata_size =
      sizeof(uint32_t) + sizeof(uint32_t); // count + entsize

  Status error;
  const lldb::addr_t first_header_addr = m_headerInfoRWs_ptr + metadata_size;
  DataBufferHeap header_buffer(m_entsize, '\0');
  lldb::offset_t cursor = 0;
  for (uint32_t i = 0; i < m_count; i++) {
    const lldb::addr_t header_addr = first_header_addr + (i * m_entsize);
    process->ReadMemory(header_addr, header_buffer.GetBytes(), m_entsize,
                        error);
    if (error.Fail())
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Failed to read memory from inferior when "
                                     "populating SharedCacheImageHeaders");

    DataExtractor header_extractor(header_buffer.GetBytes(), m_entsize,
                                   process->GetByteOrder(),
                                   process->GetAddressByteSize());
    cursor = 0;
    bool is_loaded = false;
    if (m_entsize == 4) {
      uint32_t header = header_extractor.GetU32_unchecked(&cursor);
      if (header & 1)
        is_loaded = true;
    } else {
      uint64_t header = header_extractor.GetU64_unchecked(&cursor);
      if (header & 1)
        is_loaded = true;
    }

    if (is_loaded)
      m_loaded_images.set(i);
    else
      m_loaded_images.reset(i);
  }
  m_needs_update = false;
  m_version++;
  return llvm::Error::success();
}

bool AppleObjCRuntimeV2::SharedCacheImageHeaders::IsImageLoaded(
    uint16_t image_index) {
  if (image_index >= m_count)
    return false;
  if (auto Err = UpdateIfNeeded()) {
    Log *log = GetLog(LLDBLog::Process | LLDBLog::Types);
    LLDB_LOG_ERROR(log, std::move(Err),
                   "Failed to update SharedCacheImageHeaders: {0}");
  }
  return m_loaded_images.test(image_index);
}

uint64_t AppleObjCRuntimeV2::SharedCacheImageHeaders::GetVersion() {
  if (auto Err = UpdateIfNeeded()) {
    Log *log = GetLog(LLDBLog::Process | LLDBLog::Types);
    LLDB_LOG_ERROR(log, std::move(Err),
                   "Failed to update SharedCacheImageHeaders: {0}");
  }
  return m_version;
}

std::unique_ptr<UtilityFunction>
AppleObjCRuntimeV2::DynamicClassInfoExtractor::GetClassInfoUtilityFunctionImpl(
    ExecutionContext &exe_ctx, Helper helper, std::string code,
    std::string name) {
  Log *log = GetLog(LLDBLog::Process | LLDBLog::Types);

  LLDB_LOG(log, "Creating utility function {0}", name);

  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(exe_ctx.GetTargetRef());
  if (!scratch_ts_sp)
    return {};

  auto utility_fn_or_error = exe_ctx.GetTargetRef().CreateUtilityFunction(
      std::move(code), std::move(name), eLanguageTypeC, exe_ctx);
  if (!utility_fn_or_error) {
    LLDB_LOG_ERROR(
        log, utility_fn_or_error.takeError(),
        "Failed to get utility function for dynamic info extractor: {0}");
    return {};
  }

  // Make some types for our arguments.
  CompilerType clang_uint32_t_type =
      scratch_ts_sp->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 32);
  CompilerType clang_void_pointer_type =
      scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();

  // Make the runner function for our implementation utility function.
  ValueList arguments;
  Value value;
  value.SetValueType(Value::ValueType::Scalar);
  value.SetCompilerType(clang_void_pointer_type);
  arguments.PushValue(value);
  arguments.PushValue(value);
  value.SetValueType(Value::ValueType::Scalar);
  value.SetCompilerType(clang_uint32_t_type);
  arguments.PushValue(value);

  // objc_getRealizedClassList_trylock takes an additional buffer and length.
  if (helper == Helper::objc_getRealizedClassList_trylock) {
    value.SetCompilerType(clang_void_pointer_type);
    arguments.PushValue(value);
    value.SetCompilerType(clang_uint32_t_type);
    arguments.PushValue(value);
  }

  arguments.PushValue(value);

  std::unique_ptr<UtilityFunction> utility_fn = std::move(*utility_fn_or_error);

  Status error;
  utility_fn->MakeFunctionCaller(clang_uint32_t_type, arguments,
                                 exe_ctx.GetThreadSP(), error);

  if (error.Fail()) {
    LLDB_LOG(log,
             "Failed to make function caller for implementation lookup: {0}.",
             error.AsCString());
    return {};
  }

  return utility_fn;
}

UtilityFunction *
AppleObjCRuntimeV2::DynamicClassInfoExtractor::GetClassInfoUtilityFunction(
    ExecutionContext &exe_ctx, Helper helper) {
  switch (helper) {
  case gdb_objc_realized_classes: {
    if (!m_gdb_objc_realized_classes_helper.utility_function)
      m_gdb_objc_realized_classes_helper.utility_function =
          GetClassInfoUtilityFunctionImpl(exe_ctx, helper,
                                          g_get_dynamic_class_info_body,
                                          g_get_dynamic_class_info_name);
    return m_gdb_objc_realized_classes_helper.utility_function.get();
  }
  case objc_copyRealizedClassList: {
    if (!m_objc_copyRealizedClassList_helper.utility_function)
      m_objc_copyRealizedClassList_helper.utility_function =
          GetClassInfoUtilityFunctionImpl(exe_ctx, helper,
                                          g_get_dynamic_class_info2_body,
                                          g_get_dynamic_class_info2_name);
    return m_objc_copyRealizedClassList_helper.utility_function.get();
  }
  case objc_getRealizedClassList_trylock: {
    if (!m_objc_getRealizedClassList_trylock_helper.utility_function)
      m_objc_getRealizedClassList_trylock_helper.utility_function =
          GetClassInfoUtilityFunctionImpl(exe_ctx, helper,
                                          g_get_dynamic_class_info3_body,
                                          g_get_dynamic_class_info3_name);
    return m_objc_getRealizedClassList_trylock_helper.utility_function.get();
  }
  }
  llvm_unreachable("Unexpected helper");
}

lldb::addr_t &
AppleObjCRuntimeV2::DynamicClassInfoExtractor::GetClassInfoArgs(Helper helper) {
  switch (helper) {
  case gdb_objc_realized_classes:
    return m_gdb_objc_realized_classes_helper.args;
  case objc_copyRealizedClassList:
    return m_objc_copyRealizedClassList_helper.args;
  case objc_getRealizedClassList_trylock:
    return m_objc_getRealizedClassList_trylock_helper.args;
  }
  llvm_unreachable("Unexpected helper");
}

AppleObjCRuntimeV2::DynamicClassInfoExtractor::Helper
AppleObjCRuntimeV2::DynamicClassInfoExtractor::ComputeHelper(
    ExecutionContext &exe_ctx) const {
  if (!m_runtime.m_has_objc_copyRealizedClassList &&
      !m_runtime.m_has_objc_getRealizedClassList_trylock)
    return DynamicClassInfoExtractor::gdb_objc_realized_classes;

  if (Process *process = m_runtime.GetProcess()) {
    if (DynamicLoader *loader = process->GetDynamicLoader()) {
      if (loader->IsFullyInitialized()) {
        switch (exe_ctx.GetTargetRef().GetDynamicClassInfoHelper()) {
        case eDynamicClassInfoHelperAuto:
          [[fallthrough]];
        case eDynamicClassInfoHelperGetRealizedClassList:
          if (m_runtime.m_has_objc_getRealizedClassList_trylock)
            return DynamicClassInfoExtractor::objc_getRealizedClassList_trylock;
          [[fallthrough]];
        case eDynamicClassInfoHelperCopyRealizedClassList:
          if (m_runtime.m_has_objc_copyRealizedClassList)
            return DynamicClassInfoExtractor::objc_copyRealizedClassList;
          [[fallthrough]];
        case eDynamicClassInfoHelperRealizedClassesStruct:
          return DynamicClassInfoExtractor::gdb_objc_realized_classes;
        }
      }
    }
  }

  return DynamicClassInfoExtractor::gdb_objc_realized_classes;
}

std::unique_ptr<UtilityFunction>
AppleObjCRuntimeV2::SharedCacheClassInfoExtractor::
    GetClassInfoUtilityFunctionImpl(ExecutionContext &exe_ctx) {
  Log *log = GetLog(LLDBLog::Process | LLDBLog::Types);

  LLDB_LOG(log, "Creating utility function {0}",
           g_get_shared_cache_class_info_name);

  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(exe_ctx.GetTargetRef());
  if (!scratch_ts_sp)
    return {};

  // If the inferior objc.dylib has the class_getNameRaw function, use that in
  // our jitted expression.  Else fall back to the old class_getName.
  static ConstString g_class_getName_symbol_name("class_getName");
  static ConstString g_class_getNameRaw_symbol_name(
      "objc_debug_class_getNameRaw");

  ConstString class_name_getter_function_name =
      m_runtime.HasSymbol(g_class_getNameRaw_symbol_name)
          ? g_class_getNameRaw_symbol_name
          : g_class_getName_symbol_name;

  // Substitute in the correct class_getName / class_getNameRaw function name,
  // concatenate the two parts of our expression text.  The format string has
  // two %s's, so provide the name twice.
  std::string shared_class_expression;
  llvm::raw_string_ostream(shared_class_expression)
      << llvm::format(g_shared_cache_class_name_funcptr,
                      class_name_getter_function_name.AsCString(),
                      class_name_getter_function_name.AsCString());

  shared_class_expression += g_get_shared_cache_class_info_body;

  auto utility_fn_or_error = exe_ctx.GetTargetRef().CreateUtilityFunction(
      std::move(shared_class_expression), g_get_shared_cache_class_info_name,
      eLanguageTypeC, exe_ctx);

  if (!utility_fn_or_error) {
    LLDB_LOG_ERROR(
        log, utility_fn_or_error.takeError(),
        "Failed to get utility function for shared class info extractor: {0}");
    return nullptr;
  }

  // Make some types for our arguments.
  CompilerType clang_uint32_t_type =
      scratch_ts_sp->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 32);
  CompilerType clang_void_pointer_type =
      scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();
  CompilerType clang_uint64_t_pointer_type =
      scratch_ts_sp->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 64)
          .GetPointerType();

  // Next make the function caller for our implementation utility function.
  ValueList arguments;
  Value value;
  value.SetValueType(Value::ValueType::Scalar);
  value.SetCompilerType(clang_void_pointer_type);
  arguments.PushValue(value);
  arguments.PushValue(value);
  arguments.PushValue(value);

  value.SetValueType(Value::ValueType::Scalar);
  value.SetCompilerType(clang_uint64_t_pointer_type);
  arguments.PushValue(value);

  value.SetValueType(Value::ValueType::Scalar);
  value.SetCompilerType(clang_uint32_t_type);
  arguments.PushValue(value);
  arguments.PushValue(value);

  std::unique_ptr<UtilityFunction> utility_fn = std::move(*utility_fn_or_error);

  Status error;
  utility_fn->MakeFunctionCaller(clang_uint32_t_type, arguments,
                                 exe_ctx.GetThreadSP(), error);

  if (error.Fail()) {
    LLDB_LOG(log,
             "Failed to make function caller for implementation lookup: {0}.",
             error.AsCString());
    return {};
  }

  return utility_fn;
}

UtilityFunction *
AppleObjCRuntimeV2::SharedCacheClassInfoExtractor::GetClassInfoUtilityFunction(
    ExecutionContext &exe_ctx) {
  if (!m_utility_function)
    m_utility_function = GetClassInfoUtilityFunctionImpl(exe_ctx);
  return m_utility_function.get();
}

AppleObjCRuntimeV2::DescriptorMapUpdateResult
AppleObjCRuntimeV2::DynamicClassInfoExtractor::UpdateISAToDescriptorMap(
    RemoteNXMapTable &hash_table) {
  Process *process = m_runtime.GetProcess();
  if (process == nullptr)
    return DescriptorMapUpdateResult::Fail();

  uint32_t num_class_infos = 0;

  Log *log = GetLog(LLDBLog::Process | LLDBLog::Types);

  ExecutionContext exe_ctx;

  ThreadSP thread_sp = process->GetThreadList().GetExpressionExecutionThread();

  if (!thread_sp)
    return DescriptorMapUpdateResult::Fail();

  if (!thread_sp->SafeToCallFunctions())
    return DescriptorMapUpdateResult::Retry();

  thread_sp->CalculateExecutionContext(exe_ctx);
  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(process->GetTarget());

  if (!scratch_ts_sp)
    return DescriptorMapUpdateResult::Fail();

  Address function_address;

  const uint32_t addr_size = process->GetAddressByteSize();

  Status err;

  // Compute which helper we're going to use for this update.
  const DynamicClassInfoExtractor::Helper helper = ComputeHelper(exe_ctx);

  // Read the total number of classes from the hash table
  const uint32_t num_classes =
      helper == DynamicClassInfoExtractor::gdb_objc_realized_classes
          ? hash_table.GetCount()
          : m_runtime.m_realized_class_generation_count;
  if (num_classes == 0) {
    LLDB_LOGF(log, "No dynamic classes found.");
    return DescriptorMapUpdateResult::Success(0);
  }

  UtilityFunction *get_class_info_code =
      GetClassInfoUtilityFunction(exe_ctx, helper);
  if (!get_class_info_code) {
    // The callee will have already logged a useful error message.
    return DescriptorMapUpdateResult::Fail();
  }

  FunctionCaller *get_class_info_function =
      get_class_info_code->GetFunctionCaller();

  if (!get_class_info_function) {
    LLDB_LOGF(log, "Failed to get implementation lookup function caller.");
    return DescriptorMapUpdateResult::Fail();
  }

  ValueList arguments = get_class_info_function->GetArgumentValues();

  DiagnosticManager diagnostics;

  const uint32_t class_info_byte_size = addr_size + 4;
  const uint32_t class_infos_byte_size = num_classes * class_info_byte_size;
  lldb::addr_t class_infos_addr = process->AllocateMemory(
      class_infos_byte_size, ePermissionsReadable | ePermissionsWritable, err);

  if (class_infos_addr == LLDB_INVALID_ADDRESS) {
    LLDB_LOGF(log,
              "unable to allocate %" PRIu32
              " bytes in process for shared cache read",
              class_infos_byte_size);
    return DescriptorMapUpdateResult::Fail();
  }

  auto deallocate_class_infos = llvm::make_scope_exit([&] {
    // Deallocate the memory we allocated for the ClassInfo array
    if (class_infos_addr != LLDB_INVALID_ADDRESS)
      process->DeallocateMemory(class_infos_addr);
  });

  lldb::addr_t class_buffer_addr = LLDB_INVALID_ADDRESS;
  const uint32_t class_byte_size = addr_size;
  const uint32_t class_buffer_len = num_classes;
  const uint32_t class_buffer_byte_size = class_buffer_len * class_byte_size;
  if (helper == Helper::objc_getRealizedClassList_trylock) {
    class_buffer_addr = process->AllocateMemory(
        class_buffer_byte_size, ePermissionsReadable | ePermissionsWritable,
        err);
    if (class_buffer_addr == LLDB_INVALID_ADDRESS) {
      LLDB_LOGF(log,
                "unable to allocate %" PRIu32
                " bytes in process for shared cache read",
                class_buffer_byte_size);
      return DescriptorMapUpdateResult::Fail();
    }
  }

  auto deallocate_class_buffer = llvm::make_scope_exit([&] {
    // Deallocate the memory we allocated for the Class array
    if (class_buffer_addr != LLDB_INVALID_ADDRESS)
      process->DeallocateMemory(class_buffer_addr);
  });

  std::lock_guard<std::mutex> guard(m_mutex);

  // Fill in our function argument values
  uint32_t index = 0;
  arguments.GetValueAtIndex(index++)->GetScalar() =
      hash_table.GetTableLoadAddress();
  arguments.GetValueAtIndex(index++)->GetScalar() = class_infos_addr;
  arguments.GetValueAtIndex(index++)->GetScalar() = class_infos_byte_size;

  if (class_buffer_addr != LLDB_INVALID_ADDRESS) {
    arguments.GetValueAtIndex(index++)->GetScalar() = class_buffer_addr;
    arguments.GetValueAtIndex(index++)->GetScalar() = class_buffer_byte_size;
  }

  // Only dump the runtime classes from the expression evaluation if the log is
  // verbose:
  Log *type_log = GetLog(LLDBLog::Types);
  bool dump_log = type_log && type_log->GetVerbose();

  arguments.GetValueAtIndex(index++)->GetScalar() = dump_log ? 1 : 0;

  bool success = false;

  diagnostics.Clear();

  // Write our function arguments into the process so we can run our function
  if (get_class_info_function->WriteFunctionArguments(
          exe_ctx, GetClassInfoArgs(helper), arguments, diagnostics)) {
    EvaluateExpressionOptions options;
    options.SetUnwindOnError(true);
    options.SetTryAllThreads(false);
    options.SetStopOthers(true);
    options.SetIgnoreBreakpoints(true);
    options.SetTimeout(process->GetUtilityExpressionTimeout());
    options.SetIsForUtilityExpr(true);

    CompilerType clang_uint32_t_type =
        scratch_ts_sp->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 32);

    Value return_value;
    return_value.SetValueType(Value::ValueType::Scalar);
    return_value.SetCompilerType(clang_uint32_t_type);
    return_value.GetScalar() = 0;

    diagnostics.Clear();

    // Run the function
    ExpressionResults results = get_class_info_function->ExecuteFunction(
        exe_ctx, &GetClassInfoArgs(helper), options, diagnostics, return_value);

    if (results == eExpressionCompleted) {
      // The result is the number of ClassInfo structures that were filled in
      num_class_infos = return_value.GetScalar().ULong();
      LLDB_LOG(log, "Discovered {0} Objective-C classes", num_class_infos);
      if (num_class_infos > 0) {
        // Read the ClassInfo structures
        DataBufferHeap buffer(num_class_infos * class_info_byte_size, 0);
        if (process->ReadMemory(class_infos_addr, buffer.GetBytes(),
                                buffer.GetByteSize(),
                                err) == buffer.GetByteSize()) {
          DataExtractor class_infos_data(buffer.GetBytes(),
                                         buffer.GetByteSize(),
                                         process->GetByteOrder(), addr_size);
          m_runtime.ParseClassInfoArray(class_infos_data, num_class_infos);
        }
      }
      success = true;
    } else {
      if (log) {
        LLDB_LOGF(log, "Error evaluating our find class name function.");
        diagnostics.Dump(log);
      }
    }
  } else {
    if (log) {
      LLDB_LOGF(log, "Error writing function arguments.");
      diagnostics.Dump(log);
    }
  }

  return DescriptorMapUpdateResult(success, false, num_class_infos);
}

uint32_t AppleObjCRuntimeV2::ParseClassInfoArray(const DataExtractor &data,
                                                 uint32_t num_class_infos) {
  // Parses an array of "num_class_infos" packed ClassInfo structures:
  //
  //    struct ClassInfo
  //    {
  //        Class isa;
  //        uint32_t hash;
  //    } __attribute__((__packed__));

  Log *log = GetLog(LLDBLog::Types);
  bool should_log = log && log->GetVerbose();

  uint32_t num_parsed = 0;

  // Iterate through all ClassInfo structures
  lldb::offset_t offset = 0;
  for (uint32_t i = 0; i < num_class_infos; ++i) {
    ObjCISA isa = data.GetAddress(&offset);

    if (isa == 0) {
      if (should_log)
        LLDB_LOGF(
            log, "AppleObjCRuntimeV2 found NULL isa, ignoring this class info");
      continue;
    }
    // Check if we already know about this ISA, if we do, the info will never
    // change, so we can just skip it.
    if (ISAIsCached(isa)) {
      if (should_log)
        LLDB_LOGF(log,
                  "AppleObjCRuntimeV2 found cached isa=0x%" PRIx64
                  ", ignoring this class info",
                  isa);
      offset += 4;
    } else {
      // Read the 32 bit hash for the class name
      const uint32_t name_hash = data.GetU32(&offset);
      ClassDescriptorSP descriptor_sp(
          new ClassDescriptorV2(*this, isa, nullptr));

      // The code in g_get_shared_cache_class_info_body sets the value of the
      // hash to 0 to signal a demangled symbol. We use class_getName() in that
      // code to find the class name, but this returns a demangled name for
      // Swift symbols. For those symbols, recompute the hash here by reading
      // their name from the runtime.
      if (name_hash)
        AddClass(isa, descriptor_sp, name_hash);
      else
        AddClass(isa, descriptor_sp,
                 descriptor_sp->GetClassName().AsCString(nullptr));
      num_parsed++;
      if (should_log)
        LLDB_LOGF(log,
                  "AppleObjCRuntimeV2 added isa=0x%" PRIx64
                  ", hash=0x%8.8x, name=%s",
                  isa, name_hash,
                  descriptor_sp->GetClassName().AsCString("<unknown>"));
    }
  }
  if (should_log)
    LLDB_LOGF(log, "AppleObjCRuntimeV2 parsed %" PRIu32 " class infos",
              num_parsed);
  return num_parsed;
}

bool AppleObjCRuntimeV2::HasSymbol(ConstString Name) {
  if (!m_objc_module_sp)
    return false;
  if (const Symbol *symbol = m_objc_module_sp->FindFirstSymbolWithNameAndType(
          Name, lldb::eSymbolTypeCode)) {
    if (symbol->ValueIsAddress() || symbol->GetAddressRef().IsValid())
      return true;
  }
  return false;
}

AppleObjCRuntimeV2::DescriptorMapUpdateResult
AppleObjCRuntimeV2::SharedCacheClassInfoExtractor::UpdateISAToDescriptorMap() {
  Process *process = m_runtime.GetProcess();
  if (process == nullptr)
    return DescriptorMapUpdateResult::Fail();

  Log *log = GetLog(LLDBLog::Process | LLDBLog::Types);

  ExecutionContext exe_ctx;

  ThreadSP thread_sp = process->GetThreadList().GetExpressionExecutionThread();

  if (!thread_sp)
    return DescriptorMapUpdateResult::Fail();

  if (!thread_sp->SafeToCallFunctions())
    return DescriptorMapUpdateResult::Retry();

  thread_sp->CalculateExecutionContext(exe_ctx);
  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(process->GetTarget());

  if (!scratch_ts_sp)
    return DescriptorMapUpdateResult::Fail();

  Address function_address;

  const uint32_t addr_size = process->GetAddressByteSize();

  Status err;

  uint32_t num_class_infos = 0;

  const lldb::addr_t objc_opt_ptr = m_runtime.GetSharedCacheReadOnlyAddress();
  const lldb::addr_t shared_cache_base_addr =
      m_runtime.GetSharedCacheBaseAddress();

  if (objc_opt_ptr == LLDB_INVALID_ADDRESS ||
      shared_cache_base_addr == LLDB_INVALID_ADDRESS)
    return DescriptorMapUpdateResult::Fail();

  // The number of entries to pre-allocate room for.
  // Each entry is (addrsize + 4) bytes
  // FIXME: It is not sustainable to continue incrementing this value every time
  // the shared cache grows. This is because it requires allocating memory in
  // the inferior process and some inferior processes have small memory limits.
  const uint32_t max_num_classes = 212992;

  UtilityFunction *get_class_info_code = GetClassInfoUtilityFunction(exe_ctx);
  if (!get_class_info_code) {
    // The callee will have already logged a useful error message.
    return DescriptorMapUpdateResult::Fail();
  }

  FunctionCaller *get_shared_cache_class_info_function =
      get_class_info_code->GetFunctionCaller();

  if (!get_shared_cache_class_info_function) {
    LLDB_LOGF(log, "Failed to get implementation lookup function caller.");
    return DescriptorMapUpdateResult::Fail();
  }

  ValueList arguments =
      get_shared_cache_class_info_function->GetArgumentValues();

  DiagnosticManager diagnostics;

  const uint32_t class_info_byte_size = addr_size + 4;
  const uint32_t class_infos_byte_size = max_num_classes * class_info_byte_size;
  lldb::addr_t class_infos_addr = process->AllocateMemory(
      class_infos_byte_size, ePermissionsReadable | ePermissionsWritable, err);
  const uint32_t relative_selector_offset_addr_size = 64;
  lldb::addr_t relative_selector_offset_addr =
      process->AllocateMemory(relative_selector_offset_addr_size,
                              ePermissionsReadable | ePermissionsWritable, err);

  if (class_infos_addr == LLDB_INVALID_ADDRESS) {
    LLDB_LOGF(log,
              "unable to allocate %" PRIu32
              " bytes in process for shared cache read",
              class_infos_byte_size);
    return DescriptorMapUpdateResult::Fail();
  }

  std::lock_guard<std::mutex> guard(m_mutex);

  // Fill in our function argument values
  arguments.GetValueAtIndex(0)->GetScalar() = objc_opt_ptr;
  arguments.GetValueAtIndex(1)->GetScalar() = shared_cache_base_addr;
  arguments.GetValueAtIndex(2)->GetScalar() = class_infos_addr;
  arguments.GetValueAtIndex(3)->GetScalar() = relative_selector_offset_addr;
  arguments.GetValueAtIndex(4)->GetScalar() = class_infos_byte_size;
  // Only dump the runtime classes from the expression evaluation if the log is
  // verbose:
  Log *type_log = GetLog(LLDBLog::Types);
  bool dump_log = type_log && type_log->GetVerbose();

  arguments.GetValueAtIndex(5)->GetScalar() = dump_log ? 1 : 0;

  bool success = false;

  diagnostics.Clear();

  // Write our function arguments into the process so we can run our function
  if (get_shared_cache_class_info_function->WriteFunctionArguments(
          exe_ctx, m_args, arguments, diagnostics)) {
    EvaluateExpressionOptions options;
    options.SetUnwindOnError(true);
    options.SetTryAllThreads(false);
    options.SetStopOthers(true);
    options.SetIgnoreBreakpoints(true);
    options.SetTimeout(process->GetUtilityExpressionTimeout());
    options.SetIsForUtilityExpr(true);

    CompilerType clang_uint32_t_type =
        scratch_ts_sp->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 32);

    Value return_value;
    return_value.SetValueType(Value::ValueType::Scalar);
    return_value.SetCompilerType(clang_uint32_t_type);
    return_value.GetScalar() = 0;

    diagnostics.Clear();

    // Run the function
    ExpressionResults results =
        get_shared_cache_class_info_function->ExecuteFunction(
            exe_ctx, &m_args, options, diagnostics, return_value);

    if (results == eExpressionCompleted) {
      // The result is the number of ClassInfo structures that were filled in
      num_class_infos = return_value.GetScalar().ULong();
      LLDB_LOG(log, "Discovered {0} Objective-C classes in the shared cache",
               num_class_infos);
      // Assert if there were more classes than we pre-allocated
      // room for.
      assert(num_class_infos <= max_num_classes);
      if (num_class_infos > 0) {
        if (num_class_infos > max_num_classes) {
          num_class_infos = max_num_classes;

          success = false;
        } else {
          success = true;
        }

        // Read the relative selector offset.
        DataBufferHeap relative_selector_offset_buffer(64, 0);
        if (process->ReadMemory(relative_selector_offset_addr,
                                relative_selector_offset_buffer.GetBytes(),
                                relative_selector_offset_buffer.GetByteSize(),
                                err) ==
            relative_selector_offset_buffer.GetByteSize()) {
          DataExtractor relative_selector_offset_data(
              relative_selector_offset_buffer.GetBytes(),
              relative_selector_offset_buffer.GetByteSize(),
              process->GetByteOrder(), addr_size);
          lldb::offset_t offset = 0;
          uint64_t relative_selector_offset =
              relative_selector_offset_data.GetU64(&offset);
          if (relative_selector_offset > 0) {
            // The offset is relative to the objc_opt struct.
            m_runtime.SetRelativeSelectorBaseAddr(objc_opt_ptr +
                                                  relative_selector_offset);
          }
        }

        // Read the ClassInfo structures
        DataBufferHeap class_infos_buffer(
            num_class_infos * class_info_byte_size, 0);
        if (process->ReadMemory(class_infos_addr, class_infos_buffer.GetBytes(),
                                class_infos_buffer.GetByteSize(),
                                err) == class_infos_buffer.GetByteSize()) {
          DataExtractor class_infos_data(class_infos_buffer.GetBytes(),
                                         class_infos_buffer.GetByteSize(),
                                         process->GetByteOrder(), addr_size);

          m_runtime.ParseClassInfoArray(class_infos_data, num_class_infos);
        }
      } else {
        success = true;
      }
    } else {
      if (log) {
        LLDB_LOGF(log, "Error evaluating our find class name function.");
        diagnostics.Dump(log);
      }
    }
  } else {
    if (log) {
      LLDB_LOGF(log, "Error writing function arguments.");
      diagnostics.Dump(log);
    }
  }

  // Deallocate the memory we allocated for the ClassInfo array
  process->DeallocateMemory(class_infos_addr);

  return DescriptorMapUpdateResult(success, false, num_class_infos);
}

lldb::addr_t AppleObjCRuntimeV2::GetSharedCacheReadOnlyAddress() {
  Process *process = GetProcess();

  if (process) {
    ModuleSP objc_module_sp(GetObjCModule());

    if (objc_module_sp) {
      ObjectFile *objc_object = objc_module_sp->GetObjectFile();

      if (objc_object) {
        SectionList *section_list = objc_module_sp->GetSectionList();

        if (section_list) {
          SectionSP text_segment_sp(
              section_list->FindSectionByName(ConstString("__TEXT")));

          if (text_segment_sp) {
            SectionSP objc_opt_section_sp(
                text_segment_sp->GetChildren().FindSectionByName(
                    ConstString("__objc_opt_ro")));

            if (objc_opt_section_sp) {
              return objc_opt_section_sp->GetLoadBaseAddress(
                  &process->GetTarget());
            }
          }
        }
      }
    }
  }
  return LLDB_INVALID_ADDRESS;
}

lldb::addr_t AppleObjCRuntimeV2::GetSharedCacheBaseAddress() {
  StructuredData::ObjectSP info = m_process->GetSharedCacheInfo();
  if (!info)
    return LLDB_INVALID_ADDRESS;

  StructuredData::Dictionary *info_dict = info->GetAsDictionary();
  if (!info_dict)
    return LLDB_INVALID_ADDRESS;

  StructuredData::ObjectSP value =
      info_dict->GetValueForKey("shared_cache_base_address");
  if (!value)
    return LLDB_INVALID_ADDRESS;

  return value->GetUnsignedIntegerValue(LLDB_INVALID_ADDRESS);
}

void AppleObjCRuntimeV2::UpdateISAToDescriptorMapIfNeeded() {
  LLDB_SCOPED_TIMER();

  Log *log = GetLog(LLDBLog::Process | LLDBLog::Types);

  // Else we need to check with our process to see when the map was updated.
  Process *process = GetProcess();

  if (process) {
    RemoteNXMapTable hash_table;

    // Update the process stop ID that indicates the last time we updated the
    // map, whether it was successful or not.
    m_isa_to_descriptor_stop_id = process->GetStopID();

    // Ask the runtime is the realized class generation count changed. Unlike
    // the hash table, this accounts for lazily named classes.
    const bool class_count_changed = RealizedClassGenerationCountChanged();

    if (!m_hash_signature.NeedsUpdate(process, this, hash_table) &&
        !class_count_changed)
      return;

    m_hash_signature.UpdateSignature(hash_table);

    // Grab the dynamically loaded Objective-C classes from memory.
    DescriptorMapUpdateResult dynamic_update_result =
        m_dynamic_class_info_extractor.UpdateISAToDescriptorMap(hash_table);

    // Now get the objc classes that are baked into the Objective-C runtime in
    // the shared cache, but only once per process as this data never changes
    if (!m_loaded_objc_opt) {
      // it is legitimately possible for the shared cache to be empty - in that
      // case, the dynamic hash table will contain all the class information we
      // need; the situation we're trying to detect is one where we aren't
      // seeing class information from the runtime - in order to detect that
      // vs. just the shared cache being empty or sparsely populated, we set an
      // arbitrary (very low) threshold for the number of classes that we want
      // to see in a "good" scenario - anything below that is suspicious
      // (Foundation alone has thousands of classes)
      const uint32_t num_classes_to_warn_at = 500;

      DescriptorMapUpdateResult shared_cache_update_result =
          m_shared_cache_class_info_extractor.UpdateISAToDescriptorMap();

      LLDB_LOGF(log,
                "attempted to read objc class data - results: "
                "[dynamic_update]: ran: %s, retry: %s, count: %" PRIu32
                " [shared_cache_update]: ran: %s, retry: %s, count: %" PRIu32,
                dynamic_update_result.m_update_ran ? "yes" : "no",
                dynamic_update_result.m_retry_update ? "yes" : "no",
                dynamic_update_result.m_num_found,
                shared_cache_update_result.m_update_ran ? "yes" : "no",
                shared_cache_update_result.m_retry_update ? "yes" : "no",
                shared_cache_update_result.m_num_found);

      // warn if:
      // - we could not run either expression
      // - we found fewer than num_classes_to_warn_at classes total
      if (dynamic_update_result.m_retry_update ||
          shared_cache_update_result.m_retry_update)
        WarnIfNoClassesCached(SharedCacheWarningReason::eExpressionUnableToRun);
      else if ((!shared_cache_update_result.m_update_ran) ||
               (!dynamic_update_result.m_update_ran))
        WarnIfNoClassesCached(
            SharedCacheWarningReason::eExpressionExecutionFailure);
      else if (dynamic_update_result.m_num_found +
                   shared_cache_update_result.m_num_found <
               num_classes_to_warn_at)
        WarnIfNoClassesCached(SharedCacheWarningReason::eNotEnoughClassesRead);
      else
        m_loaded_objc_opt = true;
    }
  } else {
    m_isa_to_descriptor_stop_id = UINT32_MAX;
  }
}

bool AppleObjCRuntimeV2::RealizedClassGenerationCountChanged() {
  Process *process = GetProcess();
  if (!process)
    return false;

  Status error;
  uint64_t objc_debug_realized_class_generation_count =
      ExtractRuntimeGlobalSymbol(
          process, ConstString("objc_debug_realized_class_generation_count"),
          GetObjCModule(), error);
  if (error.Fail())
    return false;

  if (m_realized_class_generation_count ==
      objc_debug_realized_class_generation_count)
    return false;

  Log *log = GetLog(LLDBLog::Process | LLDBLog::Types);
  LLDB_LOG(log,
           "objc_debug_realized_class_generation_count changed from {0} to {1}",
           m_realized_class_generation_count,
           objc_debug_realized_class_generation_count);

  m_realized_class_generation_count =
      objc_debug_realized_class_generation_count;

  return true;
}

static bool DoesProcessHaveSharedCache(Process &process) {
  PlatformSP platform_sp = process.GetTarget().GetPlatform();
  if (!platform_sp)
    return true; // this should not happen

  llvm::StringRef platform_plugin_name_sr = platform_sp->GetPluginName();
  if (platform_plugin_name_sr.ends_with("-simulator"))
    return false;

  return true;
}

void AppleObjCRuntimeV2::WarnIfNoClassesCached(
    SharedCacheWarningReason reason) {
  if (GetProcess() && !DoesProcessHaveSharedCache(*GetProcess())) {
    // Simulators do not have the objc_opt_ro class table so don't actually
    // complain to the user
    return;
  }

  Debugger &debugger(GetProcess()->GetTarget().GetDebugger());
  switch (reason) {
  case SharedCacheWarningReason::eNotEnoughClassesRead:
    Debugger::ReportWarning("could not find Objective-C class data in "
                            "the process. This may reduce the quality of type "
                            "information available.\n",
                            debugger.GetID(), &m_no_classes_cached_warning);
    break;
  case SharedCacheWarningReason::eExpressionExecutionFailure:
    Debugger::ReportWarning(
        "could not execute support code to read "
        "Objective-C class data in the process. This may "
        "reduce the quality of type information available.\n",
        debugger.GetID(), &m_no_classes_cached_warning);
    break;
  case SharedCacheWarningReason::eExpressionUnableToRun:
    Debugger::ReportWarning(
        "could not execute support code to read Objective-C class data because "
        "it's not yet safe to do so, and will be retried later.\n",
        debugger.GetID(), nullptr);
    break;
  }
}

void AppleObjCRuntimeV2::WarnIfNoExpandedSharedCache() {
  if (!m_objc_module_sp)
    return;

  ObjectFile *object_file = m_objc_module_sp->GetObjectFile();
  if (!object_file)
    return;

  if (!object_file->IsInMemory())
    return;

  Target &target = GetProcess()->GetTarget();
  Debugger &debugger = target.GetDebugger();

  std::string buffer;
  llvm::raw_string_ostream os(buffer);

  os << "libobjc.A.dylib is being read from process memory. This "
        "indicates that LLDB could not ";
  if (PlatformSP platform_sp = target.GetPlatform()) {
    if (platform_sp->IsHost()) {
      os << "read from the host's in-memory shared cache";
    } else {
      os << "find the on-disk shared cache for this device";
    }
  } else {
    os << "read from the shared cache";
  }
  os << ". This will likely reduce debugging performance.\n";

  Debugger::ReportWarning(os.str(), debugger.GetID(),
                          &m_no_expanded_cache_warning);
}

DeclVendor *AppleObjCRuntimeV2::GetDeclVendor() {
  if (!m_decl_vendor_up)
    m_decl_vendor_up = std::make_unique<AppleObjCDeclVendor>(*this);

  return m_decl_vendor_up.get();
}

lldb::addr_t AppleObjCRuntimeV2::LookupRuntimeSymbol(ConstString name) {
  lldb::addr_t ret = LLDB_INVALID_ADDRESS;

  const char *name_cstr = name.AsCString();

  if (name_cstr) {
    llvm::StringRef name_strref(name_cstr);

    llvm::StringRef ivar_prefix("OBJC_IVAR_$_");
    llvm::StringRef class_prefix("OBJC_CLASS_$_");

    if (name_strref.starts_with(ivar_prefix)) {
      llvm::StringRef ivar_skipped_prefix =
          name_strref.substr(ivar_prefix.size());
      std::pair<llvm::StringRef, llvm::StringRef> class_and_ivar =
          ivar_skipped_prefix.split('.');

      if (!class_and_ivar.first.empty() && !class_and_ivar.second.empty()) {
        const ConstString class_name_cs(class_and_ivar.first);
        ClassDescriptorSP descriptor =
            ObjCLanguageRuntime::GetClassDescriptorFromClassName(class_name_cs);

        if (descriptor) {
          const ConstString ivar_name_cs(class_and_ivar.second);
          const char *ivar_name_cstr = ivar_name_cs.AsCString();

          auto ivar_func = [&ret,
                            ivar_name_cstr](const char *name, const char *type,
                                            lldb::addr_t offset_addr,
                                            uint64_t size) -> lldb::addr_t {
            if (!strcmp(name, ivar_name_cstr)) {
              ret = offset_addr;
              return true;
            }
            return false;
          };

          descriptor->Describe(
              std::function<void(ObjCISA)>(nullptr),
              std::function<bool(const char *, const char *)>(nullptr),
              std::function<bool(const char *, const char *)>(nullptr),
              ivar_func);
        }
      }
    } else if (name_strref.starts_with(class_prefix)) {
      llvm::StringRef class_skipped_prefix =
          name_strref.substr(class_prefix.size());
      const ConstString class_name_cs(class_skipped_prefix);
      ClassDescriptorSP descriptor =
          GetClassDescriptorFromClassName(class_name_cs);

      if (descriptor)
        ret = descriptor->GetISA();
    }
  }

  return ret;
}

AppleObjCRuntimeV2::NonPointerISACache *
AppleObjCRuntimeV2::NonPointerISACache::CreateInstance(
    AppleObjCRuntimeV2 &runtime, const lldb::ModuleSP &objc_module_sp) {
  Process *process(runtime.GetProcess());

  Status error;

  Log *log = GetLog(LLDBLog::Types);

  auto objc_debug_isa_magic_mask = ExtractRuntimeGlobalSymbol(
      process, ConstString("objc_debug_isa_magic_mask"), objc_module_sp, error);
  if (error.Fail())
    return nullptr;

  auto objc_debug_isa_magic_value = ExtractRuntimeGlobalSymbol(
      process, ConstString("objc_debug_isa_magic_value"), objc_module_sp,
      error);
  if (error.Fail())
    return nullptr;

  auto objc_debug_isa_class_mask = ExtractRuntimeGlobalSymbol(
      process, ConstString("objc_debug_isa_class_mask"), objc_module_sp, error);
  if (error.Fail())
    return nullptr;

  if (log)
    log->PutCString("AOCRT::NPI: Found all the non-indexed ISA masks");

  bool foundError = false;
  auto objc_debug_indexed_isa_magic_mask = ExtractRuntimeGlobalSymbol(
      process, ConstString("objc_debug_indexed_isa_magic_mask"), objc_module_sp,
      error);
  foundError |= error.Fail();

  auto objc_debug_indexed_isa_magic_value = ExtractRuntimeGlobalSymbol(
      process, ConstString("objc_debug_indexed_isa_magic_value"),
      objc_module_sp, error);
  foundError |= error.Fail();

  auto objc_debug_indexed_isa_index_mask = ExtractRuntimeGlobalSymbol(
      process, ConstString("objc_debug_indexed_isa_index_mask"), objc_module_sp,
      error);
  foundError |= error.Fail();

  auto objc_debug_indexed_isa_index_shift = ExtractRuntimeGlobalSymbol(
      process, ConstString("objc_debug_indexed_isa_index_shift"),
      objc_module_sp, error);
  foundError |= error.Fail();

  auto objc_indexed_classes =
      ExtractRuntimeGlobalSymbol(process, ConstString("objc_indexed_classes"),
                                 objc_module_sp, error, false);
  foundError |= error.Fail();

  if (log)
    log->PutCString("AOCRT::NPI: Found all the indexed ISA masks");

  // we might want to have some rules to outlaw these other values (e.g if the
  // mask is zero but the value is non-zero, ...)

  return new NonPointerISACache(
      runtime, objc_module_sp, objc_debug_isa_class_mask,
      objc_debug_isa_magic_mask, objc_debug_isa_magic_value,
      objc_debug_indexed_isa_magic_mask, objc_debug_indexed_isa_magic_value,
      objc_debug_indexed_isa_index_mask, objc_debug_indexed_isa_index_shift,
      foundError ? 0 : objc_indexed_classes);
}

AppleObjCRuntimeV2::TaggedPointerVendorV2 *
AppleObjCRuntimeV2::TaggedPointerVendorV2::CreateInstance(
    AppleObjCRuntimeV2 &runtime, const lldb::ModuleSP &objc_module_sp) {
  Process *process(runtime.GetProcess());

  Status error;

  auto objc_debug_taggedpointer_mask = ExtractRuntimeGlobalSymbol(
      process, ConstString("objc_debug_taggedpointer_mask"), objc_module_sp,
      error);
  if (error.Fail())
    return new TaggedPointerVendorLegacy(runtime);

  auto objc_debug_taggedpointer_slot_shift = ExtractRuntimeGlobalSymbol(
      process, ConstString("objc_debug_taggedpointer_slot_shift"),
      objc_module_sp, error, true, 4);
  if (error.Fail())
    return new TaggedPointerVendorLegacy(runtime);

  auto objc_debug_taggedpointer_slot_mask = ExtractRuntimeGlobalSymbol(
      process, ConstString("objc_debug_taggedpointer_slot_mask"),
      objc_module_sp, error, true, 4);
  if (error.Fail())
    return new TaggedPointerVendorLegacy(runtime);

  auto objc_debug_taggedpointer_payload_lshift = ExtractRuntimeGlobalSymbol(
      process, ConstString("objc_debug_taggedpointer_payload_lshift"),
      objc_module_sp, error, true, 4);
  if (error.Fail())
    return new TaggedPointerVendorLegacy(runtime);

  auto objc_debug_taggedpointer_payload_rshift = ExtractRuntimeGlobalSymbol(
      process, ConstString("objc_debug_taggedpointer_payload_rshift"),
      objc_module_sp, error, true, 4);
  if (error.Fail())
    return new TaggedPointerVendorLegacy(runtime);

  auto objc_debug_taggedpointer_classes = ExtractRuntimeGlobalSymbol(
      process, ConstString("objc_debug_taggedpointer_classes"), objc_module_sp,
      error, false);
  if (error.Fail())
    return new TaggedPointerVendorLegacy(runtime);

  // try to detect the "extended tagged pointer" variables - if any are
  // missing, use the non-extended vendor
  do {
    auto objc_debug_taggedpointer_ext_mask = ExtractRuntimeGlobalSymbol(
        process, ConstString("objc_debug_taggedpointer_ext_mask"),
        objc_module_sp, error);
    if (error.Fail())
      break;

    auto objc_debug_taggedpointer_ext_slot_shift = ExtractRuntimeGlobalSymbol(
        process, ConstString("objc_debug_taggedpointer_ext_slot_shift"),
        objc_module_sp, error, true, 4);
    if (error.Fail())
      break;

    auto objc_debug_taggedpointer_ext_slot_mask = ExtractRuntimeGlobalSymbol(
        process, ConstString("objc_debug_taggedpointer_ext_slot_mask"),
        objc_module_sp, error, true, 4);
    if (error.Fail())
      break;

    auto objc_debug_taggedpointer_ext_classes = ExtractRuntimeGlobalSymbol(
        process, ConstString("objc_debug_taggedpointer_ext_classes"),
        objc_module_sp, error, false);
    if (error.Fail())
      break;

    auto objc_debug_taggedpointer_ext_payload_lshift =
        ExtractRuntimeGlobalSymbol(
            process, ConstString("objc_debug_taggedpointer_ext_payload_lshift"),
            objc_module_sp, error, true, 4);
    if (error.Fail())
      break;

    auto objc_debug_taggedpointer_ext_payload_rshift =
        ExtractRuntimeGlobalSymbol(
            process, ConstString("objc_debug_taggedpointer_ext_payload_rshift"),
            objc_module_sp, error, true, 4);
    if (error.Fail())
      break;

    return new TaggedPointerVendorExtended(
        runtime, objc_debug_taggedpointer_mask,
        objc_debug_taggedpointer_ext_mask, objc_debug_taggedpointer_slot_shift,
        objc_debug_taggedpointer_ext_slot_shift,
        objc_debug_taggedpointer_slot_mask,
        objc_debug_taggedpointer_ext_slot_mask,
        objc_debug_taggedpointer_payload_lshift,
        objc_debug_taggedpointer_payload_rshift,
        objc_debug_taggedpointer_ext_payload_lshift,
        objc_debug_taggedpointer_ext_payload_rshift,
        objc_debug_taggedpointer_classes, objc_debug_taggedpointer_ext_classes);
  } while (false);

  // we might want to have some rules to outlaw these values (e.g if the
  // table's address is zero)

  return new TaggedPointerVendorRuntimeAssisted(
      runtime, objc_debug_taggedpointer_mask,
      objc_debug_taggedpointer_slot_shift, objc_debug_taggedpointer_slot_mask,
      objc_debug_taggedpointer_payload_lshift,
      objc_debug_taggedpointer_payload_rshift,
      objc_debug_taggedpointer_classes);
}

bool AppleObjCRuntimeV2::TaggedPointerVendorLegacy::IsPossibleTaggedPointer(
    lldb::addr_t ptr) {
  return (ptr & 1);
}

ObjCLanguageRuntime::ClassDescriptorSP
AppleObjCRuntimeV2::TaggedPointerVendorLegacy::GetClassDescriptor(
    lldb::addr_t ptr) {
  if (!IsPossibleTaggedPointer(ptr))
    return ObjCLanguageRuntime::ClassDescriptorSP();

  uint32_t foundation_version = m_runtime.GetFoundationVersion();

  if (foundation_version == LLDB_INVALID_MODULE_VERSION)
    return ObjCLanguageRuntime::ClassDescriptorSP();

  uint64_t class_bits = (ptr & 0xE) >> 1;
  ConstString name;

  static ConstString g_NSAtom("NSAtom");
  static ConstString g_NSNumber("NSNumber");
  static ConstString g_NSDateTS("NSDateTS");
  static ConstString g_NSManagedObject("NSManagedObject");
  static ConstString g_NSDate("NSDate");

  if (foundation_version >= 900) {
    switch (class_bits) {
    case 0:
      name = g_NSAtom;
      break;
    case 3:
      name = g_NSNumber;
      break;
    case 4:
      name = g_NSDateTS;
      break;
    case 5:
      name = g_NSManagedObject;
      break;
    case 6:
      name = g_NSDate;
      break;
    default:
      return ObjCLanguageRuntime::ClassDescriptorSP();
    }
  } else {
    switch (class_bits) {
    case 1:
      name = g_NSNumber;
      break;
    case 5:
      name = g_NSManagedObject;
      break;
    case 6:
      name = g_NSDate;
      break;
    case 7:
      name = g_NSDateTS;
      break;
    default:
      return ObjCLanguageRuntime::ClassDescriptorSP();
    }
  }

  lldb::addr_t unobfuscated = ptr ^ m_runtime.GetTaggedPointerObfuscator();
  return ClassDescriptorSP(new ClassDescriptorV2Tagged(name, unobfuscated));
}

AppleObjCRuntimeV2::TaggedPointerVendorRuntimeAssisted::
    TaggedPointerVendorRuntimeAssisted(
        AppleObjCRuntimeV2 &runtime, uint64_t objc_debug_taggedpointer_mask,
        uint32_t objc_debug_taggedpointer_slot_shift,
        uint32_t objc_debug_taggedpointer_slot_mask,
        uint32_t objc_debug_taggedpointer_payload_lshift,
        uint32_t objc_debug_taggedpointer_payload_rshift,
        lldb::addr_t objc_debug_taggedpointer_classes)
    : TaggedPointerVendorV2(runtime), m_cache(),
      m_objc_debug_taggedpointer_mask(objc_debug_taggedpointer_mask),
      m_objc_debug_taggedpointer_slot_shift(
          objc_debug_taggedpointer_slot_shift),
      m_objc_debug_taggedpointer_slot_mask(objc_debug_taggedpointer_slot_mask),
      m_objc_debug_taggedpointer_payload_lshift(
          objc_debug_taggedpointer_payload_lshift),
      m_objc_debug_taggedpointer_payload_rshift(
          objc_debug_taggedpointer_payload_rshift),
      m_objc_debug_taggedpointer_classes(objc_debug_taggedpointer_classes) {}

bool AppleObjCRuntimeV2::TaggedPointerVendorRuntimeAssisted::
    IsPossibleTaggedPointer(lldb::addr_t ptr) {
  return (ptr & m_objc_debug_taggedpointer_mask) != 0;
}

ObjCLanguageRuntime::ClassDescriptorSP
AppleObjCRuntimeV2::TaggedPointerVendorRuntimeAssisted::GetClassDescriptor(
    lldb::addr_t ptr) {
  ClassDescriptorSP actual_class_descriptor_sp;
  uint64_t unobfuscated = (ptr) ^ m_runtime.GetTaggedPointerObfuscator();

  if (!IsPossibleTaggedPointer(unobfuscated))
    return ObjCLanguageRuntime::ClassDescriptorSP();

  uintptr_t slot = (ptr >> m_objc_debug_taggedpointer_slot_shift) &
                   m_objc_debug_taggedpointer_slot_mask;

  CacheIterator iterator = m_cache.find(slot), end = m_cache.end();
  if (iterator != end) {
    actual_class_descriptor_sp = iterator->second;
  } else {
    Process *process(m_runtime.GetProcess());
    uintptr_t slot_ptr = slot * process->GetAddressByteSize() +
                         m_objc_debug_taggedpointer_classes;
    Status error;
    uintptr_t slot_data = process->ReadPointerFromMemory(slot_ptr, error);
    if (error.Fail() || slot_data == 0 ||
        slot_data == uintptr_t(LLDB_INVALID_ADDRESS))
      return nullptr;
    actual_class_descriptor_sp =
        m_runtime.GetClassDescriptorFromISA((ObjCISA)slot_data);
    if (!actual_class_descriptor_sp) {
      if (ABISP abi_sp = process->GetABI()) {
        ObjCISA fixed_isa = abi_sp->FixCodeAddress((ObjCISA)slot_data);
        actual_class_descriptor_sp =
            m_runtime.GetClassDescriptorFromISA(fixed_isa);
      }
    }
    if (!actual_class_descriptor_sp)
      return ObjCLanguageRuntime::ClassDescriptorSP();
    m_cache[slot] = actual_class_descriptor_sp;
  }

  uint64_t data_payload =
      ((unobfuscated << m_objc_debug_taggedpointer_payload_lshift) >>
       m_objc_debug_taggedpointer_payload_rshift);
  int64_t data_payload_signed =
      ((int64_t)(unobfuscated << m_objc_debug_taggedpointer_payload_lshift) >>
       m_objc_debug_taggedpointer_payload_rshift);
  return ClassDescriptorSP(new ClassDescriptorV2Tagged(
      actual_class_descriptor_sp, data_payload, data_payload_signed));
}

AppleObjCRuntimeV2::TaggedPointerVendorExtended::TaggedPointerVendorExtended(
    AppleObjCRuntimeV2 &runtime, uint64_t objc_debug_taggedpointer_mask,
    uint64_t objc_debug_taggedpointer_ext_mask,
    uint32_t objc_debug_taggedpointer_slot_shift,
    uint32_t objc_debug_taggedpointer_ext_slot_shift,
    uint32_t objc_debug_taggedpointer_slot_mask,
    uint32_t objc_debug_taggedpointer_ext_slot_mask,
    uint32_t objc_debug_taggedpointer_payload_lshift,
    uint32_t objc_debug_taggedpointer_payload_rshift,
    uint32_t objc_debug_taggedpointer_ext_payload_lshift,
    uint32_t objc_debug_taggedpointer_ext_payload_rshift,
    lldb::addr_t objc_debug_taggedpointer_classes,
    lldb::addr_t objc_debug_taggedpointer_ext_classes)
    : TaggedPointerVendorRuntimeAssisted(
          runtime, objc_debug_taggedpointer_mask,
          objc_debug_taggedpointer_slot_shift,
          objc_debug_taggedpointer_slot_mask,
          objc_debug_taggedpointer_payload_lshift,
          objc_debug_taggedpointer_payload_rshift,
          objc_debug_taggedpointer_classes),
      m_ext_cache(),
      m_objc_debug_taggedpointer_ext_mask(objc_debug_taggedpointer_ext_mask),
      m_objc_debug_taggedpointer_ext_slot_shift(
          objc_debug_taggedpointer_ext_slot_shift),
      m_objc_debug_taggedpointer_ext_slot_mask(
          objc_debug_taggedpointer_ext_slot_mask),
      m_objc_debug_taggedpointer_ext_payload_lshift(
          objc_debug_taggedpointer_ext_payload_lshift),
      m_objc_debug_taggedpointer_ext_payload_rshift(
          objc_debug_taggedpointer_ext_payload_rshift),
      m_objc_debug_taggedpointer_ext_classes(
          objc_debug_taggedpointer_ext_classes) {}

bool AppleObjCRuntimeV2::TaggedPointerVendorExtended::
    IsPossibleExtendedTaggedPointer(lldb::addr_t ptr) {
  if (!IsPossibleTaggedPointer(ptr))
    return false;

  if (m_objc_debug_taggedpointer_ext_mask == 0)
    return false;

  return ((ptr & m_objc_debug_taggedpointer_ext_mask) ==
          m_objc_debug_taggedpointer_ext_mask);
}

ObjCLanguageRuntime::ClassDescriptorSP
AppleObjCRuntimeV2::TaggedPointerVendorExtended::GetClassDescriptor(
    lldb::addr_t ptr) {
  ClassDescriptorSP actual_class_descriptor_sp;
  uint64_t unobfuscated = (ptr) ^ m_runtime.GetTaggedPointerObfuscator();

  if (!IsPossibleTaggedPointer(unobfuscated))
    return ObjCLanguageRuntime::ClassDescriptorSP();

  if (!IsPossibleExtendedTaggedPointer(unobfuscated))
    return this->TaggedPointerVendorRuntimeAssisted::GetClassDescriptor(ptr);

  uintptr_t slot = (ptr >> m_objc_debug_taggedpointer_ext_slot_shift) &
                   m_objc_debug_taggedpointer_ext_slot_mask;

  CacheIterator iterator = m_ext_cache.find(slot), end = m_ext_cache.end();
  if (iterator != end) {
    actual_class_descriptor_sp = iterator->second;
  } else {
    Process *process(m_runtime.GetProcess());
    uintptr_t slot_ptr = slot * process->GetAddressByteSize() +
                         m_objc_debug_taggedpointer_ext_classes;
    Status error;
    uintptr_t slot_data = process->ReadPointerFromMemory(slot_ptr, error);
    if (error.Fail() || slot_data == 0 ||
        slot_data == uintptr_t(LLDB_INVALID_ADDRESS))
      return nullptr;
    actual_class_descriptor_sp =
        m_runtime.GetClassDescriptorFromISA((ObjCISA)slot_data);
    if (!actual_class_descriptor_sp)
      return ObjCLanguageRuntime::ClassDescriptorSP();
    m_ext_cache[slot] = actual_class_descriptor_sp;
  }

  uint64_t data_payload = (((uint64_t)unobfuscated
                            << m_objc_debug_taggedpointer_ext_payload_lshift) >>
                           m_objc_debug_taggedpointer_ext_payload_rshift);
  int64_t data_payload_signed =
      ((int64_t)((uint64_t)unobfuscated
                 << m_objc_debug_taggedpointer_ext_payload_lshift) >>
       m_objc_debug_taggedpointer_ext_payload_rshift);

  return ClassDescriptorSP(new ClassDescriptorV2Tagged(
      actual_class_descriptor_sp, data_payload, data_payload_signed));
}

AppleObjCRuntimeV2::NonPointerISACache::NonPointerISACache(
    AppleObjCRuntimeV2 &runtime, const ModuleSP &objc_module_sp,
    uint64_t objc_debug_isa_class_mask, uint64_t objc_debug_isa_magic_mask,
    uint64_t objc_debug_isa_magic_value,
    uint64_t objc_debug_indexed_isa_magic_mask,
    uint64_t objc_debug_indexed_isa_magic_value,
    uint64_t objc_debug_indexed_isa_index_mask,
    uint64_t objc_debug_indexed_isa_index_shift,
    lldb::addr_t objc_indexed_classes)
    : m_runtime(runtime), m_cache(), m_objc_module_wp(objc_module_sp),
      m_objc_debug_isa_class_mask(objc_debug_isa_class_mask),
      m_objc_debug_isa_magic_mask(objc_debug_isa_magic_mask),
      m_objc_debug_isa_magic_value(objc_debug_isa_magic_value),
      m_objc_debug_indexed_isa_magic_mask(objc_debug_indexed_isa_magic_mask),
      m_objc_debug_indexed_isa_magic_value(objc_debug_indexed_isa_magic_value),
      m_objc_debug_indexed_isa_index_mask(objc_debug_indexed_isa_index_mask),
      m_objc_debug_indexed_isa_index_shift(objc_debug_indexed_isa_index_shift),
      m_objc_indexed_classes(objc_indexed_classes), m_indexed_isa_cache() {}

ObjCLanguageRuntime::ClassDescriptorSP
AppleObjCRuntimeV2::NonPointerISACache::GetClassDescriptor(ObjCISA isa) {
  ObjCISA real_isa = 0;
  if (!EvaluateNonPointerISA(isa, real_isa))
    return ObjCLanguageRuntime::ClassDescriptorSP();
  auto cache_iter = m_cache.find(real_isa);
  if (cache_iter != m_cache.end())
    return cache_iter->second;
  auto descriptor_sp =
      m_runtime.ObjCLanguageRuntime::GetClassDescriptorFromISA(real_isa);
  if (descriptor_sp) // cache only positive matches since the table might grow
    m_cache[real_isa] = descriptor_sp;
  return descriptor_sp;
}

bool AppleObjCRuntimeV2::NonPointerISACache::EvaluateNonPointerISA(
    ObjCISA isa, ObjCISA &ret_isa) {
  Log *log = GetLog(LLDBLog::Types);

  LLDB_LOGF(log, "AOCRT::NPI Evaluate(isa = 0x%" PRIx64 ")", (uint64_t)isa);

  if ((isa & ~m_objc_debug_isa_class_mask) == 0)
    return false;

  // If all of the indexed ISA variables are set, then its possible that this
  // ISA is indexed, and we should first try to get its value using the index.
  // Note, we check these variables first as the ObjC runtime will set at least
  // one of their values to 0 if they aren't needed.
  if (m_objc_debug_indexed_isa_magic_mask &&
      m_objc_debug_indexed_isa_magic_value &&
      m_objc_debug_indexed_isa_index_mask &&
      m_objc_debug_indexed_isa_index_shift && m_objc_indexed_classes) {
    if ((isa & ~m_objc_debug_indexed_isa_index_mask) == 0)
      return false;

    if ((isa & m_objc_debug_indexed_isa_magic_mask) ==
        m_objc_debug_indexed_isa_magic_value) {
      // Magic bits are correct, so try extract the index.
      uintptr_t index = (isa & m_objc_debug_indexed_isa_index_mask) >>
                        m_objc_debug_indexed_isa_index_shift;
      // If the index is out of bounds of the length of the array then check if
      // the array has been updated.  If that is the case then we should try
      // read the count again, and update the cache if the count has been
      // updated.
      if (index > m_indexed_isa_cache.size()) {
        LLDB_LOGF(log,
                  "AOCRT::NPI (index = %" PRIu64
                  ") exceeds cache (size = %" PRIu64 ")",
                  (uint64_t)index, (uint64_t)m_indexed_isa_cache.size());

        Process *process(m_runtime.GetProcess());

        ModuleSP objc_module_sp(m_objc_module_wp.lock());
        if (!objc_module_sp)
          return false;

        Status error;
        auto objc_indexed_classes_count = ExtractRuntimeGlobalSymbol(
            process, ConstString("objc_indexed_classes_count"), objc_module_sp,
            error);
        if (error.Fail())
          return false;

        LLDB_LOGF(log, "AOCRT::NPI (new class count = %" PRIu64 ")",
                  (uint64_t)objc_indexed_classes_count);

        if (objc_indexed_classes_count > m_indexed_isa_cache.size()) {
          // Read the class entries we don't have.  We should just read all of
          // them instead of just the one we need as then we can cache those we
          // may need later.
          auto num_new_classes =
              objc_indexed_classes_count - m_indexed_isa_cache.size();
          const uint32_t addr_size = process->GetAddressByteSize();
          DataBufferHeap buffer(num_new_classes * addr_size, 0);

          lldb::addr_t last_read_class =
              m_objc_indexed_classes + (m_indexed_isa_cache.size() * addr_size);
          size_t bytes_read = process->ReadMemory(
              last_read_class, buffer.GetBytes(), buffer.GetByteSize(), error);
          if (error.Fail() || bytes_read != buffer.GetByteSize())
            return false;

          LLDB_LOGF(log, "AOCRT::NPI (read new classes count = %" PRIu64 ")",
                    (uint64_t)num_new_classes);

          // Append the new entries to the existing cache.
          DataExtractor data(buffer.GetBytes(), buffer.GetByteSize(),
                             process->GetByteOrder(),
                             process->GetAddressByteSize());

          lldb::offset_t offset = 0;
          for (unsigned i = 0; i != num_new_classes; ++i)
            m_indexed_isa_cache.push_back(data.GetAddress(&offset));
        }
      }

      // If the index is still out of range then this isn't a pointer.
      if (index > m_indexed_isa_cache.size())
        return false;

      LLDB_LOGF(log, "AOCRT::NPI Evaluate(ret_isa = 0x%" PRIx64 ")",
                (uint64_t)m_indexed_isa_cache[index]);

      ret_isa = m_indexed_isa_cache[index];
      return (ret_isa != 0); // this is a pointer so 0 is not a valid value
    }

    return false;
  }

  // Definitely not an indexed ISA, so try to use a mask to extract the pointer
  // from the ISA.
  if ((isa & m_objc_debug_isa_magic_mask) == m_objc_debug_isa_magic_value) {
    ret_isa = isa & m_objc_debug_isa_class_mask;
    return (ret_isa != 0); // this is a pointer so 0 is not a valid value
  }
  return false;
}

ObjCLanguageRuntime::EncodingToTypeSP AppleObjCRuntimeV2::GetEncodingToType() {
  if (!m_encoding_to_type_sp)
    m_encoding_to_type_sp =
        std::make_shared<AppleObjCTypeEncodingParser>(*this);
  return m_encoding_to_type_sp;
}

lldb_private::AppleObjCRuntime::ObjCISA
AppleObjCRuntimeV2::GetPointerISA(ObjCISA isa) {
  ObjCISA ret = isa;

  if (auto *non_pointer_isa_cache = GetNonPointerIsaCache())
    non_pointer_isa_cache->EvaluateNonPointerISA(isa, ret);

  return ret;
}

bool AppleObjCRuntimeV2::GetCFBooleanValuesIfNeeded() {
  if (m_CFBoolean_values)
    return true;

  static ConstString g_dunder_kCFBooleanFalse("__kCFBooleanFalse");
  static ConstString g_dunder_kCFBooleanTrue("__kCFBooleanTrue");
  static ConstString g_kCFBooleanFalse("kCFBooleanFalse");
  static ConstString g_kCFBooleanTrue("kCFBooleanTrue");

  std::function<lldb::addr_t(ConstString, ConstString)> get_symbol =
      [this](ConstString sym, ConstString real_sym) -> lldb::addr_t {
    SymbolContextList sc_list;
    GetProcess()->GetTarget().GetImages().FindSymbolsWithNameAndType(
        sym, lldb::eSymbolTypeData, sc_list);
    if (sc_list.GetSize() == 1) {
      SymbolContext sc;
      sc_list.GetContextAtIndex(0, sc);
      if (sc.symbol)
        return sc.symbol->GetLoadAddress(&GetProcess()->GetTarget());
    }
    GetProcess()->GetTarget().GetImages().FindSymbolsWithNameAndType(
        real_sym, lldb::eSymbolTypeData, sc_list);
    if (sc_list.GetSize() != 1)
      return LLDB_INVALID_ADDRESS;

    SymbolContext sc;
    sc_list.GetContextAtIndex(0, sc);
    if (!sc.symbol)
      return LLDB_INVALID_ADDRESS;

    lldb::addr_t addr = sc.symbol->GetLoadAddress(&GetProcess()->GetTarget());
    Status error;
    addr = GetProcess()->ReadPointerFromMemory(addr, error);
    if (error.Fail())
      return LLDB_INVALID_ADDRESS;
    return addr;
  };

  lldb::addr_t false_addr = get_symbol(g_dunder_kCFBooleanFalse, g_kCFBooleanFalse);
  lldb::addr_t true_addr = get_symbol(g_dunder_kCFBooleanTrue, g_kCFBooleanTrue);

  return (m_CFBoolean_values = {false_addr, true_addr}).operator bool();
}

void AppleObjCRuntimeV2::GetValuesForGlobalCFBooleans(lldb::addr_t &cf_true,
                                                      lldb::addr_t &cf_false) {
  if (GetCFBooleanValuesIfNeeded()) {
    cf_true = m_CFBoolean_values->second;
    cf_false = m_CFBoolean_values->first;
  } else
    this->AppleObjCRuntime::GetValuesForGlobalCFBooleans(cf_true, cf_false);
}

void AppleObjCRuntimeV2::ModulesDidLoad(const ModuleList &module_list) {
  AppleObjCRuntime::ModulesDidLoad(module_list);
  if (HasReadObjCLibrary() && m_shared_cache_image_headers_up)
    m_shared_cache_image_headers_up->SetNeedsUpdate();
}

bool AppleObjCRuntimeV2::IsSharedCacheImageLoaded(uint16_t image_index) {
  if (!m_shared_cache_image_headers_up) {
    m_shared_cache_image_headers_up =
        SharedCacheImageHeaders::CreateSharedCacheImageHeaders(*this);
  }
  if (m_shared_cache_image_headers_up)
    return m_shared_cache_image_headers_up->IsImageLoaded(image_index);

  return false;
}

std::optional<uint64_t> AppleObjCRuntimeV2::GetSharedCacheImageHeaderVersion() {
  if (!m_shared_cache_image_headers_up) {
    m_shared_cache_image_headers_up =
        SharedCacheImageHeaders::CreateSharedCacheImageHeaders(*this);
  }
  if (m_shared_cache_image_headers_up)
    return m_shared_cache_image_headers_up->GetVersion();

  return std::nullopt;
}

#pragma mark Frame recognizers

class ObjCExceptionRecognizedStackFrame : public RecognizedStackFrame {
public:
  ObjCExceptionRecognizedStackFrame(StackFrameSP frame_sp) {
    ThreadSP thread_sp = frame_sp->GetThread();
    ProcessSP process_sp = thread_sp->GetProcess();

    const lldb::ABISP &abi = process_sp->GetABI();
    if (!abi)
      return;

    TypeSystemClangSP scratch_ts_sp =
        ScratchTypeSystemClang::GetForTarget(process_sp->GetTarget());
    if (!scratch_ts_sp)
      return;
    CompilerType voidstar =
        scratch_ts_sp->GetBasicType(lldb::eBasicTypeVoid).GetPointerType();

    ValueList args;
    Value input_value;
    input_value.SetCompilerType(voidstar);
    args.PushValue(input_value);

    if (!abi->GetArgumentValues(*thread_sp, args))
      return;

    addr_t exception_addr = args.GetValueAtIndex(0)->GetScalar().ULongLong();

    Value value(exception_addr);
    value.SetCompilerType(voidstar);
    exception = ValueObjectConstResult::Create(frame_sp.get(), value,
                                               ConstString("exception"));
    exception = ValueObjectRecognizerSynthesizedValue::Create(
        *exception, eValueTypeVariableArgument);
    exception = exception->GetDynamicValue(eDynamicDontRunTarget);

    m_arguments = ValueObjectListSP(new ValueObjectList());
    m_arguments->Append(exception);

    m_stop_desc = "hit Objective-C exception";
  }

  ValueObjectSP exception;

  lldb::ValueObjectSP GetExceptionObject() override { return exception; }
};

class ObjCExceptionThrowFrameRecognizer : public StackFrameRecognizer {
  lldb::RecognizedStackFrameSP
  RecognizeFrame(lldb::StackFrameSP frame) override {
    return lldb::RecognizedStackFrameSP(
        new ObjCExceptionRecognizedStackFrame(frame));
  };
  std::string GetName() override {
    return "ObjC Exception Throw StackFrame Recognizer";
  }
};

static void RegisterObjCExceptionRecognizer(Process *process) {
  FileSpec module;
  ConstString function;
  std::tie(module, function) = AppleObjCRuntime::GetExceptionThrowLocation();
  std::vector<ConstString> symbols = {function};

  process->GetTarget().GetFrameRecognizerManager().AddRecognizer(
      StackFrameRecognizerSP(new ObjCExceptionThrowFrameRecognizer()),
      module.GetFilename(), symbols,
      /*first_instruction_only*/ true);
}
