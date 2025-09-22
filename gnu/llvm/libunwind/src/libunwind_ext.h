//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//  Extensions to libunwind API.
//
//===----------------------------------------------------------------------===//

#ifndef __LIBUNWIND_EXT__
#define __LIBUNWIND_EXT__

#include "config.h"
#include <libunwind.h>
#include <unwind.h>

#define UNW_STEP_SUCCESS 1
#define UNW_STEP_END     0

#ifdef __cplusplus
extern "C" {
#endif

extern int __unw_getcontext(unw_context_t *);
extern int __unw_init_local(unw_cursor_t *, unw_context_t *);
extern int __unw_step(unw_cursor_t *);
extern int __unw_get_reg(unw_cursor_t *, unw_regnum_t, unw_word_t *);
extern int __unw_get_fpreg(unw_cursor_t *, unw_regnum_t, unw_fpreg_t *);
extern int __unw_set_reg(unw_cursor_t *, unw_regnum_t, unw_word_t);
extern int __unw_set_fpreg(unw_cursor_t *, unw_regnum_t, unw_fpreg_t);
extern int __unw_resume(unw_cursor_t *);

#ifdef __arm__
/* Save VFP registers in FSTMX format (instead of FSTMD). */
extern void __unw_save_vfp_as_X(unw_cursor_t *);
#endif

extern const char *__unw_regname(unw_cursor_t *, unw_regnum_t);
extern int __unw_get_proc_info(unw_cursor_t *, unw_proc_info_t *);
extern int __unw_is_fpreg(unw_cursor_t *, unw_regnum_t);
extern int __unw_is_signal_frame(unw_cursor_t *);
extern int __unw_get_proc_name(unw_cursor_t *, char *, size_t, unw_word_t *);

#if defined(_AIX)
extern uintptr_t __unw_get_data_rel_base(unw_cursor_t *);
#endif

// SPI
extern void __unw_iterate_dwarf_unwind_cache(void (*func)(
    unw_word_t ip_start, unw_word_t ip_end, unw_word_t fde, unw_word_t mh));

// IPI
extern void __unw_add_dynamic_fde(unw_word_t fde);
extern void __unw_remove_dynamic_fde(unw_word_t fde);

extern void __unw_add_dynamic_eh_frame_section(unw_word_t eh_frame_start);
extern void __unw_remove_dynamic_eh_frame_section(unw_word_t eh_frame_start);

#ifdef __APPLE__

// Holds a description of the object-format-header (if any) and unwind info
// sections for a given address:
//
// * dso_base should point to a header for the JIT'd object containing the
//   given address. The header's type should match the format type that
//   libunwind was compiled for (so a mach_header or mach_header_64 on Darwin).
//   A value of zero indicates that no such header exists.
//
// * dwarf_section and dwarf_section_length hold the address range of a DWARF
//   eh-frame section associated with the given address, if any. If the
//   dwarf_section_length field is zero it indicates that no such section
//   exists (and in this case dwarf_section should also be set to zero).
//
// * compact_unwind_section and compact_unwind_section_length hold the address
//   range of a compact-unwind info section associated with the given address,
//   if any. If the compact_unwind_section_length field is zero it indicates
//   that no such section exists (and in this case compact_unwind_section
//   should also be set to zero).
//
// See the unw_find_dynamic_unwind_sections type below for more details.
struct unw_dynamic_unwind_sections {
  unw_word_t dso_base;
  unw_word_t dwarf_section;
  size_t     dwarf_section_length;
  unw_word_t compact_unwind_section;
  size_t     compact_unwind_section_length;
};

// Typedef for unwind-info lookup callbacks. Functions of this type can be
// registered and deregistered using __unw_add_find_dynamic_unwind_sections
// and __unw_remove_find_dynamic_unwind_sections respectively.
//
// An unwind-info lookup callback should return 1 to indicate that it found
// unwind-info for the given address, or 0 to indicate that it did not find
// unwind-info for the given address. If found, the callback should populate
// some or all of the fields of the info argument (which is guaranteed to be
// non-null with all fields zero-initialized):
typedef int (*unw_find_dynamic_unwind_sections)(
    unw_word_t addr, struct unw_dynamic_unwind_sections *info);

// Register a dynamic unwind-info lookup callback. If libunwind does not find
// unwind info for a given frame in the executable program or normal dynamic
// shared objects then it will call all registered dynamic lookup functions
// in registration order until either one of them returns true, or the end
// of the list is reached. This lookup will happen before libunwind searches
// any eh-frames registered via __register_frame or
// __unw_add_dynamic_eh_frame_section.
//
// Returns UNW_ESUCCESS for successful registrations. If the given callback
// has already been registered then UNW_EINVAL will be returned. If all
// available callback entries are in use then UNW_ENOMEM will be returned.
extern int __unw_add_find_dynamic_unwind_sections(
    unw_find_dynamic_unwind_sections find_dynamic_unwind_sections);

// Deregister a dynacim unwind-info lookup callback.
//
// Returns UNW_ESUCCESS for successful deregistrations. If the given callback
// has already been registered then UNW_EINVAL will be returned.
extern int __unw_remove_find_dynamic_unwind_sections(
    unw_find_dynamic_unwind_sections find_dynamic_unwind_sections);

#endif

#if defined(_LIBUNWIND_ARM_EHABI)
extern const uint32_t* decode_eht_entry(const uint32_t*, size_t*, size_t*);
extern _Unwind_Reason_Code _Unwind_VRS_Interpret(_Unwind_Context *context,
                                                 const uint32_t *data,
                                                 size_t offset, size_t len);
#endif

#ifdef __cplusplus
}
#endif

#endif // __LIBUNWIND_EXT__
