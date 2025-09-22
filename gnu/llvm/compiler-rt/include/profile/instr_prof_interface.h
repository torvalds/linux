/*===---- instr_prof_interface.h - Instrumentation PGO User Program API ----===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 *
 * This header provides a public interface for fine-grained control of counter
 * reset and profile dumping. These interface functions can be directly called
 * in user programs.
 *
\*===---------------------------------------------------------------------===*/

#ifndef COMPILER_RT_INSTR_PROFILING
#define COMPILER_RT_INSTR_PROFILING

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __LLVM_INSTR_PROFILE_GENERATE
// Profile file reset and dump interfaces.
// When `-fprofile[-instr]-generate`/`-fcs-profile-generate` is in effect,
// clang defines __LLVM_INSTR_PROFILE_GENERATE to pick up the API calls.

/*!
 * \brief Set the filename for writing instrumentation data.
 *
 * Sets the filename to be used for subsequent calls to
 * \a __llvm_profile_write_file().
 *
 * \c Name is not copied, so it must remain valid.  Passing NULL resets the
 * filename logic to the default behaviour.
 *
 * Note: There may be multiple copies of the profile runtime (one for each
 * instrumented image/DSO). This API only modifies the filename within the
 * copy of the runtime available to the calling image.
 *
 * Warning: This is a no-op if continuous mode (\ref
 * __llvm_profile_is_continuous_mode_enabled) is on. The reason for this is
 * that in continuous mode, profile counters are mmap()'d to the profile at
 * program initialization time. Support for transferring the mmap'd profile
 * counts to a new file has not been implemented.
 */
void __llvm_profile_set_filename(const char *Name);

/*!
 * \brief Interface to set all PGO counters to zero for the current process.
 *
 */
void __llvm_profile_reset_counters(void);

/*!
 * \brief this is a wrapper interface to \c __llvm_profile_write_file.
 * After this interface is invoked, an already dumped flag will be set
 * so that profile won't be dumped again during program exit.
 * Invocation of interface __llvm_profile_reset_counters will clear
 * the flag. This interface is designed to be used to collect profile
 * data from user selected hot regions. The use model is
 *      __llvm_profile_reset_counters();
 *      ... hot region 1
 *      __llvm_profile_dump();
 *      .. some other code
 *      __llvm_profile_reset_counters();
 *      ... hot region 2
 *      __llvm_profile_dump();
 *
 *  It is expected that on-line profile merging is on with \c %m specifier
 *  used in profile filename . If merging is not turned on, user is expected
 *  to invoke __llvm_profile_set_filename to specify different profile names
 *  for different regions before dumping to avoid profile write clobbering.
 */
int __llvm_profile_dump(void);

// Interface to dump the current process' order file to disk.
int __llvm_orderfile_dump(void);

#else

#define __llvm_profile_set_filename(Name)
#define __llvm_profile_reset_counters()
#define __llvm_profile_dump() (0)
#define __llvm_orderfile_dump() (0)

#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
