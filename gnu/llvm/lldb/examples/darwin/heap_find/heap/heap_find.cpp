//===-- heap_find.c ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file compiles into a dylib and can be used on darwin to find data that
// is contained in active malloc blocks. To use this make the project, then
// load the shared library in a debug session while you are stopped:
//
// (lldb) process load /path/to/libheap.dylib
//
// Now you can use the "find_pointer_in_heap" and "find_cstring_in_heap"
// functions in the expression parser.
//
// This will grep everything in all active allocation blocks and print and
// malloc blocks that contain the pointer 0x112233000000:
//
// (lldb) expression find_pointer_in_heap (0x112233000000)
//
// This will grep everything in all active allocation blocks and print and
// malloc blocks that contain the C string "hello" (as a substring, no
// NULL termination included):
//
// (lldb) expression find_cstring_in_heap ("hello")
//
// The results will be printed to the STDOUT of the inferior program. The
// return value of the "find_pointer_in_heap" function is the number of
// pointer references that were found. A quick example shows
//
// (lldb) expr find_pointer_in_heap(0x0000000104000410)
// (uint32_t) $5 = 0x00000002
// 0x104000740: 0x0000000104000410 found in malloc block 0x104000730 + 16
// (malloc_size = 48)
// 0x100820060: 0x0000000104000410 found in malloc block 0x100820000 + 96
// (malloc_size = 4096)
//
// From the above output we see that 0x104000410 was found in the malloc block
// at 0x104000730 and 0x100820000. If we want to see what these blocks are, we
// can display the memory for this block using the "address" ("A" for short)
// format. The address format shows pointers, and if those pointers point to
// objects that have symbols or know data contents, it will display information
// about the pointers:
//
// (lldb) memory read --format address --count 1 0x104000730
// 0x104000730: 0x0000000100002460 (void *)0x0000000100002488: MyString
//
// We can see that the first block is a "MyString" object that contains our
// pointer value at offset 16.
//
// Looking at the next pointers, are a bit more tricky:
// (lldb) memory read -fA 0x100820000 -c1
// 0x100820000: 0x4f545541a1a1a1a1
// (lldb) memory read 0x100820000
// 0x100820000: a1 a1 a1 a1 41 55 54 4f 52 45 4c 45 41 53 45 21 ....AUTORELEASE!
// 0x100820010: 78 00 82 00 01 00 00 00 60 f9 e8 75 ff 7f 00 00 x.......`..u....
//
// This is an objective C auto release pool object that contains our pointer.
// C++ classes will show up if they are virtual as something like:
// (lldb) memory read --format address --count 1 0x104008000
// 0x104008000: 0x109008000 vtable for lldb_private::Process
//
// This is a clue that the 0x104008000 is a "lldb_private::Process *".
//===----------------------------------------------------------------------===//
// C includes
#include <assert.h>
#include <ctype.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <malloc/malloc.h>
#include <objc/objc-runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// C++ includes
#include <vector>

// Redefine private types from "/usr/local/include/stack_logging.h"
typedef struct {
  uint32_t type_flags;
  uint64_t stack_identifier;
  uint64_t argument;
  mach_vm_address_t address;
} mach_stack_logging_record_t;

// Redefine private defines from "/usr/local/include/stack_logging.h"
#define stack_logging_type_free 0
#define stack_logging_type_generic 1
#define stack_logging_type_alloc 2
#define stack_logging_type_dealloc 4
// This bit is made up by this code
#define stack_logging_type_vm_region 8

// Redefine private function prototypes from
// "/usr/local/include/stack_logging.h"
extern "C" kern_return_t __mach_stack_logging_set_file_path(task_t task,
                                                            char *file_path);

extern "C" kern_return_t
__mach_stack_logging_get_frames(task_t task, mach_vm_address_t address,
                                mach_vm_address_t *stack_frames_buffer,
                                uint32_t max_stack_frames, uint32_t *count);

extern "C" kern_return_t __mach_stack_logging_enumerate_records(
    task_t task, mach_vm_address_t address,
    void enumerator(mach_stack_logging_record_t, void *), void *context);

extern "C" kern_return_t __mach_stack_logging_frames_for_uniqued_stack(
    task_t task, uint64_t stack_identifier,
    mach_vm_address_t *stack_frames_buffer, uint32_t max_stack_frames,
    uint32_t *count);

extern "C" void *gdb_class_getClass(void *objc_class);

static void range_info_callback(task_t task, void *baton, unsigned type,
                                uint64_t ptr_addr, uint64_t ptr_size);

// Redefine private global variables prototypes from
// "/usr/local/include/stack_logging.h"

extern "C" int stack_logging_enable_logging;

// Local defines
#define MAX_FRAMES 1024

// Local Typedefs and Types
typedef void range_callback_t(task_t task, void *baton, unsigned type,
                              uint64_t ptr_addr, uint64_t ptr_size);
typedef void zone_callback_t(void *info, const malloc_zone_t *zone);
typedef int (*comare_function_t)(const void *, const void *);
struct range_callback_info_t {
  zone_callback_t *zone_callback;
  range_callback_t *range_callback;
  void *baton;
  int check_vm_regions;
};

enum data_type_t {
  eDataTypeAddress,
  eDataTypeContainsData,
  eDataTypeObjC,
  eDataTypeHeapInfo
};

struct aligned_data_t {
  const uint8_t *buffer;
  uint32_t size;
  uint32_t align;
};

struct objc_data_t {
  void *match_isa; // Set to NULL for all objective C objects
  bool match_superclasses;
};

struct range_contains_data_callback_info_t {
  data_type_t type;
  const void *lookup_addr;
  union {
    uintptr_t addr;
    aligned_data_t data;
    objc_data_t objc;
  };
  uint32_t match_count;
  bool done;
  bool unique;
};

struct malloc_match {
  void *addr;
  intptr_t size;
  intptr_t offset;
  uintptr_t type;
};

struct malloc_stack_entry {
  const void *address;
  uint64_t argument;
  uint32_t type_flags;
  uint32_t num_frames;
  mach_vm_address_t frames[MAX_FRAMES];
};

struct malloc_block_contents {
  union {
    Class isa;
    void *pointers[2];
  };
};

static int compare_void_ptr(const void *a, const void *b) {
  Class a_ptr = *(Class *)a;
  Class b_ptr = *(Class *)b;
  if (a_ptr < b_ptr)
    return -1;
  if (a_ptr > b_ptr)
    return +1;
  return 0;
}

class MatchResults {
  enum { k_max_entries = 8 * 1024 };

public:
  MatchResults() : m_size(0) {}

  void clear() {
    m_size = 0;
    bzero(&m_entries, sizeof(m_entries));
  }

  bool empty() const { return m_size == 0; }

  void push_back(const malloc_match &m, bool unique = false) {
    if (unique) {
      // Don't add the entry if there is already a match for this address
      for (uint32_t i = 0; i < m_size; ++i) {
        if (((uint8_t *)m_entries[i].addr + m_entries[i].offset) ==
            ((uint8_t *)m.addr + m.offset))
          return; // Duplicate entry
      }
    }
    if (m_size < k_max_entries - 1) {
      m_entries[m_size] = m;
      m_size++;
    }
  }

  malloc_match *data() {
    // If empty, return NULL
    if (empty())
      return NULL;
    // In not empty, terminate and return the result
    malloc_match terminator_entry = {NULL, 0, 0, 0};
    // We always leave room for an empty entry at the end
    m_entries[m_size] = terminator_entry;
    return m_entries;
  }

protected:
  malloc_match m_entries[k_max_entries];
  uint32_t m_size;
};

class MallocStackLoggingEntries {
  enum { k_max_entries = 128 };

public:
  MallocStackLoggingEntries() : m_size(0) {}

  void clear() { m_size = 0; }

  bool empty() const { return m_size == 0; }

  malloc_stack_entry *next() {
    if (m_size < k_max_entries - 1) {
      malloc_stack_entry *result = m_entries + m_size;
      ++m_size;
      return result;
    }
    return NULL; // Out of entries...
  }

  malloc_stack_entry *data() {
    // If empty, return NULL
    if (empty())
      return NULL;
    // In not empty, terminate and return the result
    m_entries[m_size].address = NULL;
    m_entries[m_size].argument = 0;
    m_entries[m_size].type_flags = 0;
    m_entries[m_size].num_frames = 0;
    return m_entries;
  }

protected:
  malloc_stack_entry m_entries[k_max_entries];
  uint32_t m_size;
};

// A safe way to allocate memory and keep it from interfering with the
// malloc enumerators.
void *safe_malloc(size_t n_bytes) {
  if (n_bytes > 0) {
    const int k_page_size = getpagesize();
    const mach_vm_size_t vm_size =
        ((n_bytes + k_page_size - 1) / k_page_size) * k_page_size;
    vm_address_t address = 0;
    kern_return_t kerr = vm_allocate(mach_task_self(), &address, vm_size, true);
    if (kerr == KERN_SUCCESS)
      return (void *)address;
  }
  return NULL;
}

// ObjCClasses
class ObjCClasses {
public:
  ObjCClasses() : m_objc_class_ptrs(NULL), m_size(0) {}

  bool Update() {
    // TODO: find out if class list has changed and update if needed
    if (m_objc_class_ptrs == NULL) {
      m_size = objc_getClassList(NULL, 0);
      if (m_size > 0) {
        // Allocate the class pointers
        m_objc_class_ptrs = (Class *)safe_malloc(m_size * sizeof(Class));
        m_size = objc_getClassList(m_objc_class_ptrs, m_size);
        // Sort Class pointers for quick lookup
        ::qsort(m_objc_class_ptrs, m_size, sizeof(Class), compare_void_ptr);
      } else
        return false;
    }
    return true;
  }

  uint32_t FindClassIndex(Class isa) {
    Class *matching_class = (Class *)bsearch(&isa, m_objc_class_ptrs, m_size,
                                             sizeof(Class), compare_void_ptr);
    if (matching_class) {
      uint32_t idx = matching_class - m_objc_class_ptrs;
      return idx;
    }
    return UINT32_MAX;
  }

  Class GetClassAtIndex(uint32_t idx) const {
    if (idx < m_size)
      return m_objc_class_ptrs[idx];
    return NULL;
  }
  uint32_t GetSize() const { return m_size; }

private:
  Class *m_objc_class_ptrs;
  uint32_t m_size;
};

// Local global variables
MatchResults g_matches;
MallocStackLoggingEntries g_malloc_stack_history;
ObjCClasses g_objc_classes;

// ObjCClassInfo

enum HeapInfoSortType { eSortTypeNone, eSortTypeBytes, eSortTypeCount };

class ObjCClassInfo {
public:
  ObjCClassInfo() : m_entries(NULL), m_size(0), m_sort_type(eSortTypeNone) {}

  void Update(const ObjCClasses &objc_classes) {
    m_size = objc_classes.GetSize();
    m_entries = (Entry *)safe_malloc(m_size * sizeof(Entry));
    m_sort_type = eSortTypeNone;
    Reset();
  }

  bool AddInstance(uint32_t idx, uint64_t ptr_size) {
    if (m_size == 0)
      Update(g_objc_classes);
    // Update the totals for the classes
    if (idx < m_size) {
      m_entries[idx].bytes += ptr_size;
      ++m_entries[idx].count;
      return true;
    }
    return false;
  }

  void Reset() {
    m_sort_type = eSortTypeNone;
    for (uint32_t i = 0; i < m_size; ++i) {
      // In case we sort the entries after gathering the data, we will
      // want to know the index into the m_objc_class_ptrs[] array.
      m_entries[i].idx = i;
      m_entries[i].bytes = 0;
      m_entries[i].count = 0;
    }
  }
  void SortByTotalBytes(const ObjCClasses &objc_classes, bool print) {
    if (m_sort_type != eSortTypeBytes && m_size > 0) {
      ::qsort(m_entries, m_size, sizeof(Entry),
              (comare_function_t)compare_bytes);
      m_sort_type = eSortTypeBytes;
    }
    if (print && m_size > 0) {
      puts("Objective-C objects by total bytes:");
      puts("Total Bytes Class Name");
      puts("----------- "
           "-----------------------------------------------------------------");
      for (uint32_t i = 0; i < m_size && m_entries[i].bytes > 0; ++i) {
        printf("%11llu %s\n", m_entries[i].bytes,
               class_getName(objc_classes.GetClassAtIndex(m_entries[i].idx)));
      }
    }
  }
  void SortByTotalCount(const ObjCClasses &objc_classes, bool print) {
    if (m_sort_type != eSortTypeCount && m_size > 0) {
      ::qsort(m_entries, m_size, sizeof(Entry),
              (comare_function_t)compare_count);
      m_sort_type = eSortTypeCount;
    }
    if (print && m_size > 0) {
      puts("Objective-C objects by total count:");
      puts("Count    Class Name");
      puts("-------- "
           "-----------------------------------------------------------------");
      for (uint32_t i = 0; i < m_size && m_entries[i].count > 0; ++i) {
        printf("%8u %s\n", m_entries[i].count,
               class_getName(objc_classes.GetClassAtIndex(m_entries[i].idx)));
      }
    }
  }

private:
  struct Entry {
    uint32_t idx;   // Index into the m_objc_class_ptrs[] array
    uint32_t count; // Number of object instances that were found
    uint64_t bytes; // Total number of bytes for each objc class
  };

  static int compare_bytes(const Entry *a, const Entry *b) {
    // Reverse the comparison to most bytes entries end up at top of list
    if (a->bytes > b->bytes)
      return -1;
    if (a->bytes < b->bytes)
      return +1;
    return 0;
  }

  static int compare_count(const Entry *a, const Entry *b) {
    // Reverse the comparison to most count entries end up at top of list
    if (a->count > b->count)
      return -1;
    if (a->count < b->count)
      return +1;
    return 0;
  }

  Entry *m_entries;
  uint32_t m_size;
  HeapInfoSortType m_sort_type;
};

ObjCClassInfo g_objc_class_snapshot;

// task_peek
//
// Reads memory from this tasks address space. This callback is needed
// by the code that iterates through all of the malloc blocks to read
// the memory in this process.
static kern_return_t task_peek(task_t task, vm_address_t remote_address,
                               vm_size_t size, void **local_memory) {
  *local_memory = (void *)remote_address;
  return KERN_SUCCESS;
}

static const void foreach_zone_in_this_process(range_callback_info_t *info) {
  if (info == NULL || info->zone_callback == NULL)
    return;

  vm_address_t *zones = NULL;
  unsigned int num_zones = 0;

  kern_return_t err = malloc_get_all_zones(0, task_peek, &zones, &num_zones);
  if (KERN_SUCCESS == err) {
    for (unsigned int i = 0; i < num_zones; ++i) {
      info->zone_callback(info, (const malloc_zone_t *)zones[i]);
    }
  }

  if (info->check_vm_regions) {
#if defined(VM_REGION_SUBMAP_SHORT_INFO_COUNT_64)
    typedef vm_region_submap_short_info_data_64_t RegionInfo;
    enum { kRegionInfoSize = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64 };
#else
    typedef vm_region_submap_info_data_64_t RegionInfo;
    enum { kRegionInfoSize = VM_REGION_SUBMAP_INFO_COUNT_64 };
#endif
    task_t task = mach_task_self();
    mach_vm_address_t vm_region_base_addr;
    mach_vm_size_t vm_region_size;
    natural_t vm_region_depth;
    RegionInfo vm_region_info;

    ((range_contains_data_callback_info_t *)info->baton)->unique = true;

    for (vm_region_base_addr = 0, vm_region_size = 1; vm_region_size != 0;
         vm_region_base_addr += vm_region_size) {
      mach_msg_type_number_t vm_region_info_size = kRegionInfoSize;
      const kern_return_t err = mach_vm_region_recurse(
          task, &vm_region_base_addr, &vm_region_size, &vm_region_depth,
          (vm_region_recurse_info_t)&vm_region_info, &vm_region_info_size);
      if (err)
        break;
      // Check all read + write regions. This will cover the thread stacks
      // and any regions of memory that aren't covered by the heap
      if (vm_region_info.protection & VM_PROT_WRITE &&
          vm_region_info.protection & VM_PROT_READ) {
        // printf ("checking vm_region: [0x%16.16llx - 0x%16.16llx)\n",
        // (uint64_t)vm_region_base_addr, (uint64_t)vm_region_base_addr +
        // vm_region_size);
        range_info_callback(task, info->baton, stack_logging_type_vm_region,
                            vm_region_base_addr, vm_region_size);
      }
    }
  }
}

// dump_malloc_block_callback
//
// A simple callback that will dump each malloc block and all available
// info from the enumeration callback perspective.
static void dump_malloc_block_callback(task_t task, void *baton, unsigned type,
                                       uint64_t ptr_addr, uint64_t ptr_size) {
  printf("task = 0x%4.4x: baton = %p, type = %u, ptr_addr = 0x%llx + 0x%llu\n",
         task, baton, type, ptr_addr, ptr_size);
}

static void ranges_callback(task_t task, void *baton, unsigned type,
                            vm_range_t *ptrs, unsigned count) {
  range_callback_info_t *info = (range_callback_info_t *)baton;
  while (count--) {
    info->range_callback(task, info->baton, type, ptrs->address, ptrs->size);
    ptrs++;
  }
}

static void enumerate_range_in_zone(void *baton, const malloc_zone_t *zone) {
  range_callback_info_t *info = (range_callback_info_t *)baton;

  if (zone && zone->introspect)
    zone->introspect->enumerator(
        mach_task_self(), info, MALLOC_PTR_IN_USE_RANGE_TYPE,
        (vm_address_t)zone, task_peek, ranges_callback);
}

static void range_info_callback(task_t task, void *baton, unsigned type,
                                uint64_t ptr_addr, uint64_t ptr_size) {
  const uint64_t end_addr = ptr_addr + ptr_size;

  range_contains_data_callback_info_t *info =
      (range_contains_data_callback_info_t *)baton;
  switch (info->type) {
  case eDataTypeAddress:
    // Check if the current malloc block contains an address specified by
    // "info->addr"
    if (ptr_addr <= info->addr && info->addr < end_addr) {
      ++info->match_count;
      malloc_match match = {(void *)ptr_addr, ptr_size, info->addr - ptr_addr,
                            type};
      g_matches.push_back(match, info->unique);
    }
    break;

  case eDataTypeContainsData:
    // Check if the current malloc block contains data specified in "info->data"
    {
      const uint32_t size = info->data.size;
      if (size < ptr_size) // Make sure this block can contain this data
      {
        uint8_t *ptr_data = NULL;
        if (task_peek(task, ptr_addr, ptr_size, (void **)&ptr_data) ==
            KERN_SUCCESS) {
          const void *buffer = info->data.buffer;
          assert(ptr_data);
          const uint32_t align = info->data.align;
          for (uint64_t addr = ptr_addr;
               addr < end_addr && ((end_addr - addr) >= size);
               addr += align, ptr_data += align) {
            if (memcmp(buffer, ptr_data, size) == 0) {
              ++info->match_count;
              malloc_match match = {(void *)ptr_addr, ptr_size, addr - ptr_addr,
                                    type};
              g_matches.push_back(match, info->unique);
            }
          }
        } else {
          printf("0x%llx: error: couldn't read %llu bytes\n", ptr_addr,
                 ptr_size);
        }
      }
    }
    break;

  case eDataTypeObjC:
    // Check if the current malloc block contains an objective C object
    // of any sort where the first pointer in the object is an OBJC class
    // pointer (an isa)
    {
      malloc_block_contents *block_contents = NULL;
      if (task_peek(task, ptr_addr, sizeof(void *), (void **)&block_contents) ==
          KERN_SUCCESS) {
        // We assume that g_objc_classes is up to date
        // that the class list was verified to have some classes in it
        // before calling this function
        const uint32_t objc_class_idx =
            g_objc_classes.FindClassIndex(block_contents->isa);
        if (objc_class_idx != UINT32_MAX) {
          bool match = false;
          if (info->objc.match_isa == 0) {
            // Match any objective C object
            match = true;
          } else {
            // Only match exact isa values in the current class or
            // optionally in the super classes
            if (info->objc.match_isa == block_contents->isa)
              match = true;
            else if (info->objc.match_superclasses) {
              Class super = class_getSuperclass(block_contents->isa);
              while (super) {
                match = super == info->objc.match_isa;
                if (match)
                  break;
                super = class_getSuperclass(super);
              }
            }
          }
          if (match) {
            // printf (" success\n");
            ++info->match_count;
            malloc_match match = {(void *)ptr_addr, ptr_size, 0, type};
            g_matches.push_back(match, info->unique);
          } else {
            // printf (" error: wrong class: %s\n", dl_info.dli_sname);
          }
        } else {
          // printf ("\terror: symbol not objc class: %s\n", dl_info.dli_sname);
          return;
        }
      }
    }
    break;

  case eDataTypeHeapInfo:
    // Check if the current malloc block contains an objective C object
    // of any sort where the first pointer in the object is an OBJC class
    // pointer (an isa)
    {
      malloc_block_contents *block_contents = NULL;
      if (task_peek(task, ptr_addr, sizeof(void *), (void **)&block_contents) ==
          KERN_SUCCESS) {
        // We assume that g_objc_classes is up to date
        // that the class list was verified to have some classes in it
        // before calling this function
        const uint32_t objc_class_idx =
            g_objc_classes.FindClassIndex(block_contents->isa);
        if (objc_class_idx != UINT32_MAX) {
          // This is an objective C object
          g_objc_class_snapshot.AddInstance(objc_class_idx, ptr_size);
        } else {
          // Classify other heap info
        }
      }
    }
    break;
  }
}

static void
get_stack_for_address_enumerator(mach_stack_logging_record_t stack_record,
                                 void *task_ptr) {
  malloc_stack_entry *stack_entry = g_malloc_stack_history.next();
  if (stack_entry) {
    stack_entry->address = (void *)stack_record.address;
    stack_entry->type_flags = stack_record.type_flags;
    stack_entry->argument = stack_record.argument;
    stack_entry->num_frames = 0;
    stack_entry->frames[0] = 0;
    kern_return_t err = __mach_stack_logging_frames_for_uniqued_stack(
        *(task_t *)task_ptr, stack_record.stack_identifier, stack_entry->frames,
        MAX_FRAMES, &stack_entry->num_frames);
    // Terminate the frames with zero if there is room
    if (stack_entry->num_frames < MAX_FRAMES)
      stack_entry->frames[stack_entry->num_frames] = 0;
  }
}

malloc_stack_entry *get_stack_history_for_address(const void *addr,
                                                  int history) {
  if (!stack_logging_enable_logging)
    return NULL;
  g_malloc_stack_history.clear();
  kern_return_t err;
  task_t task = mach_task_self();
  if (history) {
    err = __mach_stack_logging_enumerate_records(
        task, (mach_vm_address_t)addr, get_stack_for_address_enumerator, &task);
  } else {
    malloc_stack_entry *stack_entry = g_malloc_stack_history.next();
    if (stack_entry) {
      stack_entry->address = addr;
      stack_entry->type_flags = stack_logging_type_alloc;
      stack_entry->argument = 0;
      stack_entry->num_frames = 0;
      stack_entry->frames[0] = 0;
      err = __mach_stack_logging_get_frames(task, (mach_vm_address_t)addr,
                                            stack_entry->frames, MAX_FRAMES,
                                            &stack_entry->num_frames);
      if (err == 0 && stack_entry->num_frames > 0) {
        // Terminate the frames with zero if there is room
        if (stack_entry->num_frames < MAX_FRAMES)
          stack_entry->frames[stack_entry->num_frames] = 0;
      } else {
        g_malloc_stack_history.clear();
      }
    }
  }
  // Return data if there is any
  return g_malloc_stack_history.data();
}

// find_pointer_in_heap
//
// Finds a pointer value inside one or more currently valid malloc
// blocks.
malloc_match *find_pointer_in_heap(const void *addr, int check_vm_regions) {
  g_matches.clear();
  // Setup "info" to look for a malloc block that contains data
  // that is the pointer
  if (addr) {
    range_contains_data_callback_info_t data_info;
    data_info.type = eDataTypeContainsData; // Check each block for data
    data_info.data.buffer =
        (uint8_t *)&addr; // What data? The pointer value passed in
    data_info.data.size =
        sizeof(addr); // How many bytes? The byte size of a pointer
    data_info.data.align = sizeof(addr); // Align to a pointer byte size
    data_info.match_count = 0;           // Initialize the match count to zero
    data_info.done = false;   // Set done to false so searching doesn't stop
    data_info.unique = false; // Set to true when iterating on the vm_regions
    range_callback_info_t info = {enumerate_range_in_zone, range_info_callback,
                                  &data_info, check_vm_regions};
    foreach_zone_in_this_process(&info);
  }
  return g_matches.data();
}

// find_pointer_in_memory
//
// Finds a pointer value inside one or more currently valid malloc
// blocks.
malloc_match *find_pointer_in_memory(uint64_t memory_addr, uint64_t memory_size,
                                     const void *addr) {
  g_matches.clear();
  // Setup "info" to look for a malloc block that contains data
  // that is the pointer
  range_contains_data_callback_info_t data_info;
  data_info.type = eDataTypeContainsData; // Check each block for data
  data_info.data.buffer =
      (uint8_t *)&addr; // What data? The pointer value passed in
  data_info.data.size =
      sizeof(addr); // How many bytes? The byte size of a pointer
  data_info.data.align = sizeof(addr); // Align to a pointer byte size
  data_info.match_count = 0;           // Initialize the match count to zero
  data_info.done = false;   // Set done to false so searching doesn't stop
  data_info.unique = false; // Set to true when iterating on the vm_regions
  range_info_callback(mach_task_self(), &data_info, stack_logging_type_generic,
                      memory_addr, memory_size);
  return g_matches.data();
}

// find_objc_objects_in_memory
//
// Find all instances of ObjC classes 'c', or all ObjC classes if 'c' is
// NULL. If 'c' is non NULL, then also check objects to see if they
// inherit from 'c'
malloc_match *find_objc_objects_in_memory(void *isa, int check_vm_regions) {
  g_matches.clear();
  if (g_objc_classes.Update()) {
    // Setup "info" to look for a malloc block that contains data
    // that is the pointer
    range_contains_data_callback_info_t data_info;
    data_info.type = eDataTypeObjC; // Check each block for data
    data_info.objc.match_isa = isa;
    data_info.objc.match_superclasses = true;
    data_info.match_count = 0; // Initialize the match count to zero
    data_info.done = false;    // Set done to false so searching doesn't stop
    data_info.unique = false;  // Set to true when iterating on the vm_regions
    range_callback_info_t info = {enumerate_range_in_zone, range_info_callback,
                                  &data_info, check_vm_regions};
    foreach_zone_in_this_process(&info);
  }
  return g_matches.data();
}

// get_heap_info
//
// Gather information for all allocations on the heap and report
// statistics.

void get_heap_info(int sort_type) {
  if (g_objc_classes.Update()) {
    // Reset all stats
    g_objc_class_snapshot.Reset();
    // Setup "info" to look for a malloc block that contains data
    // that is the pointer
    range_contains_data_callback_info_t data_info;
    data_info.type = eDataTypeHeapInfo; // Check each block for data
    data_info.match_count = 0;          // Initialize the match count to zero
    data_info.done = false;   // Set done to false so searching doesn't stop
    data_info.unique = false; // Set to true when iterating on the vm_regions
    const int check_vm_regions = false;
    range_callback_info_t info = {enumerate_range_in_zone, range_info_callback,
                                  &data_info, check_vm_regions};
    foreach_zone_in_this_process(&info);

    // Sort and print byte total bytes
    switch (sort_type) {
    case eSortTypeNone:
    default:
    case eSortTypeBytes:
      g_objc_class_snapshot.SortByTotalBytes(g_objc_classes, true);
      break;

    case eSortTypeCount:
      g_objc_class_snapshot.SortByTotalCount(g_objc_classes, true);
      break;
    }
  } else {
    printf("error: no objective C classes\n");
  }
}

// find_cstring_in_heap
//
// Finds a C string inside one or more currently valid malloc blocks.
malloc_match *find_cstring_in_heap(const char *s, int check_vm_regions) {
  g_matches.clear();
  if (s == NULL || s[0] == '\0') {
    printf("error: invalid argument (empty cstring)\n");
    return NULL;
  }
  // Setup "info" to look for a malloc block that contains data
  // that is the C string passed in aligned on a 1 byte boundary
  range_contains_data_callback_info_t data_info;
  data_info.type = eDataTypeContainsData; // Check each block for data
  data_info.data.buffer = (uint8_t *)s;   // What data? The C string passed in
  data_info.data.size = strlen(s); // How many bytes? The length of the C string
  data_info.data.align =
      1; // Data doesn't need to be aligned, so set the alignment to 1
  data_info.match_count = 0; // Initialize the match count to zero
  data_info.done = false;    // Set done to false so searching doesn't stop
  data_info.unique = false;  // Set to true when iterating on the vm_regions
  range_callback_info_t info = {enumerate_range_in_zone, range_info_callback,
                                &data_info, check_vm_regions};
  foreach_zone_in_this_process(&info);
  return g_matches.data();
}

// find_block_for_address
//
// Find the malloc block that whose address range contains "addr".
malloc_match *find_block_for_address(const void *addr, int check_vm_regions) {
  g_matches.clear();
  // Setup "info" to look for a malloc block that contains data
  // that is the C string passed in aligned on a 1 byte boundary
  range_contains_data_callback_info_t data_info;
  data_info.type = eDataTypeAddress; // Check each block to see if the block
                                     // contains the address passed in
  data_info.addr = (uintptr_t)addr;  // What data? The C string passed in
  data_info.match_count = 0;         // Initialize the match count to zero
  data_info.done = false;   // Set done to false so searching doesn't stop
  data_info.unique = false; // Set to true when iterating on the vm_regions
  range_callback_info_t info = {enumerate_range_in_zone, range_info_callback,
                                &data_info, check_vm_regions};
  foreach_zone_in_this_process(&info);
  return g_matches.data();
}
