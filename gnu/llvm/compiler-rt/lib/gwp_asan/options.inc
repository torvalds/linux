//===-- options.inc ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef GWP_ASAN_OPTION
#error "Define GWP_ASAN_OPTION prior to including this file!"
#endif

#ifndef GWP_ASAN_DEFAULT_ENABLED
#define GWP_ASAN_DEFAULT_ENABLED true
#endif

#ifndef GWP_ASAN_STRINGIFY
#define GWP_ASAN_STRINGIFY(S) GWP_ASAN_STRINGIFY_(S)
#define GWP_ASAN_STRINGIFY_(S) #S
#endif

GWP_ASAN_OPTION(bool, Enabled, GWP_ASAN_DEFAULT_ENABLED,
                "Is GWP-ASan enabled? Defaults to " GWP_ASAN_STRINGIFY(
                    GWP_ASAN_DEFAULT_ENABLED) ".")

GWP_ASAN_OPTION(int, MaxSimultaneousAllocations, 16,
                "Number of simultaneously-guarded allocations available in the "
                "pool. Defaults to 16.")

GWP_ASAN_OPTION(int, SampleRate, 5000,
                "The probability (1 / SampleRate) that an allocation is "
                "selected for GWP-ASan sampling. Default is 5000. Sample rates "
                "up to (2^30 - 1) are supported.")

// Developer note - This option is not actually processed by GWP-ASan itself. It
// is included here so that a user can specify whether they want signal handlers
// or not. The supporting allocator should inspect this value to see whether
// signal handlers need to be installed, and then use
// crash_handler::installSignalHandlers() in order to install the handlers. Note
// that in order to support signal handlers, you will need to link against the
// optional crash_handler component.
GWP_ASAN_OPTION(
    bool, InstallSignalHandlers, true,
    "Install GWP-ASan signal handlers for SIGSEGV during dynamic loading. This "
    "allows better error reports by providing stack traces for allocation and "
    "deallocation when reporting a memory error. GWP-ASan's signal handler "
    "will forward the signal to any previously-installed handler, and user "
    "programs that install further signal handlers should make sure they do "
    "the same. Note, if the previously installed SIGSEGV handler is SIG_IGN, "
    "we terminate the process after dumping the error report.")

GWP_ASAN_OPTION(
    bool, Recoverable, false,
    "Install GWP-ASan's signal handler in recoverable mode. This means that "
    "upon GWP-ASan detecting an error, it'll print the error report, but *not* "
    "crash. Only one crash per sampled allocation will ever be recorded, and "
    "if a sampled allocation does actually cause a crash, it'll permanently "
    "occupy a slot in the pool. The recoverable mode also means that "
    "previously-installed signal handlers will only be triggered for "
    "non-GWP-ASan errors, as all GWP-ASan errors won't be forwarded.")

GWP_ASAN_OPTION(bool, InstallForkHandlers, true,
                "Install GWP-ASan atfork handlers to acquire internal locks "
                "before fork and release them after.")

GWP_ASAN_OPTION(bool, help, false, "Print a summary of the available options.")

// =============================================================================
// ==== WARNING
// =============================================================================
// If you are adding flags to GWP-ASan, please note that GWP-ASan flag strings
// may be parsed by trusted system components (on Android, GWP-ASan flag strings
// are parsed by libc during the dynamic loader). This means that GWP-ASan
// should never feature flags like log paths on disk, because this can lead to
// arbitrary file write and thus privilege escalation. For an example, see the
// setuid ASan log_path exploits: https://www.exploit-db.com/exploits/46241.
//
// Please place all new flags above this warning, so that the warning always
// stays at the bottom.
