// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ERRC
#define _LIBCPP___ERRC

/*
    system_error synopsis

namespace std
{

enum class errc
{
    address_family_not_supported,       // EAFNOSUPPORT
    address_in_use,                     // EADDRINUSE
    address_not_available,              // EADDRNOTAVAIL
    already_connected,                  // EISCONN
    argument_list_too_long,             // E2BIG
    argument_out_of_domain,             // EDOM
    bad_address,                        // EFAULT
    bad_file_descriptor,                // EBADF
    bad_message,                        // EBADMSG
    broken_pipe,                        // EPIPE
    connection_aborted,                 // ECONNABORTED
    connection_already_in_progress,     // EALREADY
    connection_refused,                 // ECONNREFUSED
    connection_reset,                   // ECONNRESET
    cross_device_link,                  // EXDEV
    destination_address_required,       // EDESTADDRREQ
    device_or_resource_busy,            // EBUSY
    directory_not_empty,                // ENOTEMPTY
    executable_format_error,            // ENOEXEC
    file_exists,                        // EEXIST
    file_too_large,                     // EFBIG
    filename_too_long,                  // ENAMETOOLONG
    function_not_supported,             // ENOSYS
    host_unreachable,                   // EHOSTUNREACH
    identifier_removed,                 // EIDRM
    illegal_byte_sequence,              // EILSEQ
    inappropriate_io_control_operation, // ENOTTY
    interrupted,                        // EINTR
    invalid_argument,                   // EINVAL
    invalid_seek,                       // ESPIPE
    io_error,                           // EIO
    is_a_directory,                     // EISDIR
    message_size,                       // EMSGSIZE
    network_down,                       // ENETDOWN
    network_reset,                      // ENETRESET
    network_unreachable,                // ENETUNREACH
    no_buffer_space,                    // ENOBUFS
    no_child_process,                   // ECHILD
    no_link,                            // ENOLINK
    no_lock_available,                  // ENOLCK
    no_message_available,               // ENODATA         // deprecated
    no_message,                         // ENOMSG
    no_protocol_option,                 // ENOPROTOOPT
    no_space_on_device,                 // ENOSPC
    no_stream_resources,                // ENOSR           // deprecated
    no_such_device_or_address,          // ENXIO
    no_such_device,                     // ENODEV
    no_such_file_or_directory,          // ENOENT
    no_such_process,                    // ESRCH
    not_a_directory,                    // ENOTDIR
    not_a_socket,                       // ENOTSOCK
    not_a_stream,                       // ENOSTR          // deprecated
    not_connected,                      // ENOTCONN
    not_enough_memory,                  // ENOMEM
    not_supported,                      // ENOTSUP
    operation_canceled,                 // ECANCELED
    operation_in_progress,              // EINPROGRESS
    operation_not_permitted,            // EPERM
    operation_not_supported,            // EOPNOTSUPP
    operation_would_block,              // EWOULDBLOCK
    owner_dead,                         // EOWNERDEAD
    permission_denied,                  // EACCES
    protocol_error,                     // EPROTO
    protocol_not_supported,             // EPROTONOSUPPORT
    read_only_file_system,              // EROFS
    resource_deadlock_would_occur,      // EDEADLK
    resource_unavailable_try_again,     // EAGAIN
    result_out_of_range,                // ERANGE
    state_not_recoverable,              // ENOTRECOVERABLE
    stream_timeout,                     // ETIME           // deprecated
    text_file_busy,                     // ETXTBSY
    timed_out,                          // ETIMEDOUT
    too_many_files_open_in_system,      // ENFILE
    too_many_files_open,                // EMFILE
    too_many_links,                     // EMLINK
    too_many_symbolic_link_levels,      // ELOOP
    value_too_large,                    // EOVERFLOW
    wrong_protocol_type                 // EPROTOTYPE
};

*/

#include <__config>
#include <cerrno>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

// The method of pushing and popping the diagnostics fails for GCC.  GCC does
// not recognize the pragma's used to generate deprecated diagnostics for
// macros. So GCC does not need the pushing and popping.
//
// TODO Remove this when the deprecated constants are removed.
//
// Note based on the post-review comments in
// https://github.com/llvm/llvm-project/pull/80542 libc++ no longer deprecates
// the macros. Since C libraries may start to deprecate these POSIX macros the
// deprecation warning avoidance is kept.
#if defined(_LIBCPP_COMPILER_CLANG_BASED)
#  define _LIBCPP_SUPPRESS_DEPRECATED_ERRC_PUSH _LIBCPP_SUPPRESS_DEPRECATED_PUSH
#  define _LIBCPP_SUPPRESS_DEPRECATED_ERRC_POP _LIBCPP_SUPPRESS_DEPRECATED_POP
#else
#  define _LIBCPP_SUPPRESS_DEPRECATED_ERRC_PUSH
#  define _LIBCPP_SUPPRESS_DEPRECATED_ERRC_POP
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// Some error codes are not present on all platforms, so we provide equivalents
// for them:

// enum class errc
//
// LWG3869 deprecates the UNIX STREAMS macros and enum values.
// This makes the code clumbersome:
// - the enum value is deprecated and should show a diagnostic,
// - the macro is deprecated and should _not_ show a diagnostic in this
//   context, and
// - the macro is not always available.
// This leads to the odd pushing and popping of the deprecated
// diagnostic.
_LIBCPP_DECLARE_STRONG_ENUM(errc){
    address_family_not_supported       = EAFNOSUPPORT,
    address_in_use                     = EADDRINUSE,
    address_not_available              = EADDRNOTAVAIL,
    already_connected                  = EISCONN,
    argument_list_too_long             = E2BIG,
    argument_out_of_domain             = EDOM,
    bad_address                        = EFAULT,
    bad_file_descriptor                = EBADF,
    bad_message                        = EBADMSG,
    broken_pipe                        = EPIPE,
    connection_aborted                 = ECONNABORTED,
    connection_already_in_progress     = EALREADY,
    connection_refused                 = ECONNREFUSED,
    connection_reset                   = ECONNRESET,
    cross_device_link                  = EXDEV,
    destination_address_required       = EDESTADDRREQ,
    device_or_resource_busy            = EBUSY,
    directory_not_empty                = ENOTEMPTY,
    executable_format_error            = ENOEXEC,
    file_exists                        = EEXIST,
    file_too_large                     = EFBIG,
    filename_too_long                  = ENAMETOOLONG,
    function_not_supported             = ENOSYS,
    host_unreachable                   = EHOSTUNREACH,
    identifier_removed                 = EIDRM,
    illegal_byte_sequence              = EILSEQ,
    inappropriate_io_control_operation = ENOTTY,
    interrupted                        = EINTR,
    invalid_argument                   = EINVAL,
    invalid_seek                       = ESPIPE,
    io_error                           = EIO,
    is_a_directory                     = EISDIR,
    message_size                       = EMSGSIZE,
    network_down                       = ENETDOWN,
    network_reset                      = ENETRESET,
    network_unreachable                = ENETUNREACH,
    no_buffer_space                    = ENOBUFS,
    no_child_process                   = ECHILD,
    no_link                            = ENOLINK,
    no_lock_available                  = ENOLCK,
    // clang-format off
    no_message_available _LIBCPP_DEPRECATED =
    _LIBCPP_SUPPRESS_DEPRECATED_ERRC_PUSH
#ifdef ENODATA
                                              ENODATA
#else
                                              ENOMSG
#endif
    _LIBCPP_SUPPRESS_DEPRECATED_ERRC_POP
    ,
    // clang-format on
    no_message         = ENOMSG,
    no_protocol_option = ENOPROTOOPT,
    no_space_on_device = ENOSPC,
    // clang-format off
    no_stream_resources _LIBCPP_DEPRECATED =
    _LIBCPP_SUPPRESS_DEPRECATED_ERRC_PUSH
#ifdef ENOSR
                                              ENOSR
#else
                                              ENOMEM
#endif
    _LIBCPP_SUPPRESS_DEPRECATED_ERRC_POP
    ,
    // clang-format on
    no_such_device_or_address = ENXIO,
    no_such_device            = ENODEV,
    no_such_file_or_directory = ENOENT,
    no_such_process           = ESRCH,
    not_a_directory           = ENOTDIR,
    not_a_socket              = ENOTSOCK,
    // clang-format off
    not_a_stream _LIBCPP_DEPRECATED =
    _LIBCPP_SUPPRESS_DEPRECATED_ERRC_PUSH
#ifdef ENOSTR
                                      ENOSTR
#else
                                      EINVAL
#endif
    _LIBCPP_SUPPRESS_DEPRECATED_ERRC_POP
    ,
    // clang-format on
    not_connected                  = ENOTCONN,
    not_enough_memory              = ENOMEM,
    not_supported                  = ENOTSUP,
    operation_canceled             = ECANCELED,
    operation_in_progress          = EINPROGRESS,
    operation_not_permitted        = EPERM,
    operation_not_supported        = EOPNOTSUPP,
    operation_would_block          = EWOULDBLOCK,
    owner_dead                     = EOWNERDEAD,
    permission_denied              = EACCES,
    protocol_error                 = EPROTO,
    protocol_not_supported         = EPROTONOSUPPORT,
    read_only_file_system          = EROFS,
    resource_deadlock_would_occur  = EDEADLK,
    resource_unavailable_try_again = EAGAIN,
    result_out_of_range            = ERANGE,
    state_not_recoverable          = ENOTRECOVERABLE,
    // clang-format off
    stream_timeout _LIBCPP_DEPRECATED =
    _LIBCPP_SUPPRESS_DEPRECATED_ERRC_PUSH
#ifdef ETIME
                                        ETIME
#else
                                        ETIMEDOUT
#endif
    _LIBCPP_SUPPRESS_DEPRECATED_ERRC_POP
    ,
    // clang-format on
    text_file_busy                = ETXTBSY,
    timed_out                     = ETIMEDOUT,
    too_many_files_open_in_system = ENFILE,
    too_many_files_open           = EMFILE,
    too_many_links                = EMLINK,
    too_many_symbolic_link_levels = ELOOP,
    value_too_large               = EOVERFLOW,
    wrong_protocol_type           = EPROTOTYPE};
_LIBCPP_DECLARE_STRONG_ENUM_EPILOG(errc)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ERRC
