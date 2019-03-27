# $FreeBSD$

.include <bsd.init.mk>

LIB=		clang_rt.stats-${CRTARCH}

SRCS+=		sanitizer_common/sanitizer_allocator.cc
SRCS+=		sanitizer_common/sanitizer_allocator_checks.cc
SRCS+=		sanitizer_common/sanitizer_allocator_report.cc
SRCS+=		sanitizer_common/sanitizer_common.cc
SRCS+=		sanitizer_common/sanitizer_common_libcdep.cc
SRCS+=		sanitizer_common/sanitizer_deadlock_detector1.cc
SRCS+=		sanitizer_common/sanitizer_deadlock_detector2.cc
SRCS+=		sanitizer_common/sanitizer_errno.cc
SRCS+=		sanitizer_common/sanitizer_file.cc
SRCS+=		sanitizer_common/sanitizer_flag_parser.cc
SRCS+=		sanitizer_common/sanitizer_flags.cc
SRCS+=		sanitizer_common/sanitizer_libc.cc
SRCS+=		sanitizer_common/sanitizer_libignore.cc
SRCS+=		sanitizer_common/sanitizer_linux.cc
SRCS+=		sanitizer_common/sanitizer_linux_libcdep.cc
SRCS+=		sanitizer_common/sanitizer_persistent_allocator.cc
SRCS+=		sanitizer_common/sanitizer_platform_limits_freebsd.cc
SRCS+=		sanitizer_common/sanitizer_platform_limits_linux.cc
SRCS+=		sanitizer_common/sanitizer_platform_limits_posix.cc
SRCS+=		sanitizer_common/sanitizer_posix.cc
SRCS+=		sanitizer_common/sanitizer_posix_libcdep.cc
SRCS+=		sanitizer_common/sanitizer_printf.cc
SRCS+=		sanitizer_common/sanitizer_procmaps_bsd.cc
SRCS+=		sanitizer_common/sanitizer_procmaps_common.cc
SRCS+=		sanitizer_common/sanitizer_stackdepot.cc
SRCS+=		sanitizer_common/sanitizer_stacktrace.cc
SRCS+=		sanitizer_common/sanitizer_stacktrace_libcdep.cc
SRCS+=		sanitizer_common/sanitizer_stacktrace_printer.cc
SRCS+=		sanitizer_common/sanitizer_suppressions.cc
SRCS+=		sanitizer_common/sanitizer_symbolizer.cc
SRCS+=		sanitizer_common/sanitizer_symbolizer_libbacktrace.cc
SRCS+=		sanitizer_common/sanitizer_symbolizer_libcdep.cc
SRCS+=		sanitizer_common/sanitizer_symbolizer_posix_libcdep.cc
SRCS+=		sanitizer_common/sanitizer_symbolizer_report.cc
SRCS+=		sanitizer_common/sanitizer_termination.cc
SRCS+=		sanitizer_common/sanitizer_thread_registry.cc
SRCS+=		sanitizer_common/sanitizer_tls_get_addr.cc
SRCS+=		sanitizer_common/sanitizer_type_traits.cc
SRCS+=		sanitizer_common/sanitizer_unwind_linux_libcdep.cc
SRCS+=		stats/stats.cc

.include <bsd.lib.mk>
