//===-- dfsan_interface.h -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of DataFlowSanitizer.
//
// Public interface header.
//===----------------------------------------------------------------------===//
#ifndef DFSAN_INTERFACE_H
#define DFSAN_INTERFACE_H

#include <sanitizer/common_interface_defs.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t dfsan_label;
typedef uint32_t dfsan_origin;

/// Signature of the callback argument to dfsan_set_write_callback().
typedef void(SANITIZER_CDECL *dfsan_write_callback_t)(int fd, const void *buf,
                                                      size_t count);

/// Signature of the callback argument to dfsan_set_conditional_callback().
typedef void(SANITIZER_CDECL *dfsan_conditional_callback_t)(
    dfsan_label label, dfsan_origin origin);

/// Signature of the callback argument to dfsan_set_reaches_function_callback().
/// The description is intended to hold the name of the variable.
typedef void(SANITIZER_CDECL *dfsan_reaches_function_callback_t)(
    dfsan_label label, dfsan_origin origin, const char *file, unsigned int line,
    const char *function);

/// Computes the union of \c l1 and \c l2, resulting in a union label.
dfsan_label SANITIZER_CDECL dfsan_union(dfsan_label l1, dfsan_label l2);

/// Sets the label for each address in [addr,addr+size) to \c label.
void SANITIZER_CDECL dfsan_set_label(dfsan_label label, void *addr,
                                     size_t size);

/// Sets the label for each address in [addr,addr+size) to the union of the
/// current label for that address and \c label.
void SANITIZER_CDECL dfsan_add_label(dfsan_label label, void *addr,
                                     size_t size);

/// Retrieves the label associated with the given data.
///
/// The type of 'data' is arbitrary.  The function accepts a value of any type,
/// which can be truncated or extended (implicitly or explicitly) as necessary.
/// The truncation/extension operations will preserve the label of the original
/// value.
dfsan_label SANITIZER_CDECL dfsan_get_label(long data);

/// Retrieves the immediate origin associated with the given data. The returned
/// origin may point to another origin.
///
/// The type of 'data' is arbitrary.
dfsan_origin SANITIZER_CDECL dfsan_get_origin(long data);

/// Retrieves the label associated with the data at the given address.
dfsan_label SANITIZER_CDECL dfsan_read_label(const void *addr, size_t size);

/// Return the origin associated with the first taint byte in the size bytes
/// from the address addr.
dfsan_origin SANITIZER_CDECL dfsan_read_origin_of_first_taint(const void *addr,
                                                              size_t size);

/// Returns whether the given label contains the label elem.
int SANITIZER_CDECL dfsan_has_label(dfsan_label label, dfsan_label elem);

/// Flushes the DFSan shadow, i.e. forgets about all labels currently associated
/// with the application memory.  Use this call to start over the taint tracking
/// within the same process.
///
/// Note: If another thread is working with tainted data during the flush, that
/// taint could still be written to shadow after the flush.
void SANITIZER_CDECL dfsan_flush(void);

/// Sets a callback to be invoked on calls to write().  The callback is invoked
/// before the write is done.  The write is not guaranteed to succeed when the
/// callback executes.  Pass in NULL to remove any callback.
void SANITIZER_CDECL
dfsan_set_write_callback(dfsan_write_callback_t labeled_write_callback);

/// Sets a callback to be invoked on any conditional expressions which have a
/// taint label set. This can be used to find where tainted data influences
/// the behavior of the program.
/// These callbacks will only be added when -dfsan-conditional-callbacks=true.
void SANITIZER_CDECL
dfsan_set_conditional_callback(dfsan_conditional_callback_t callback);

/// Conditional expressions occur during signal handlers.
/// Making callbacks that handle signals well is tricky, so when
/// -dfsan-conditional-callbacks=true, conditional expressions used in signal
/// handlers will add the labels they see into a global (bitwise-or together).
/// This function returns all label bits seen in signal handler conditions.
dfsan_label SANITIZER_CDECL dfsan_get_labels_in_signal_conditional();

/// Sets a callback to be invoked when tainted data reaches a function.
/// This could occur at function entry, or at a load instruction.
/// These callbacks will only be added if -dfsan-reaches-function-callbacks=1.
void SANITIZER_CDECL
dfsan_set_reaches_function_callback(dfsan_reaches_function_callback_t callback);

/// Making callbacks that handle signals well is tricky, so when
/// -dfsan-reaches-function-callbacks=true, functions reached in signal
/// handlers will add the labels they see into a global (bitwise-or together).
/// This function returns all label bits seen during signal handlers.
dfsan_label SANITIZER_CDECL dfsan_get_labels_in_signal_reaches_function();

/// Interceptor hooks.
/// Whenever a dfsan's custom function is called the corresponding
/// hook is called it non-zero. The hooks should be defined by the user.
/// The primary use case is taint-guided fuzzing, where the fuzzer
/// needs to see the parameters of the function and the labels.
/// FIXME: implement more hooks.
void SANITIZER_CDECL dfsan_weak_hook_memcmp(void *caller_pc, const void *s1,
                                            const void *s2, size_t n,
                                            dfsan_label s1_label,
                                            dfsan_label s2_label,
                                            dfsan_label n_label);
void SANITIZER_CDECL dfsan_weak_hook_strncmp(void *caller_pc, const char *s1,
                                             const char *s2, size_t n,
                                             dfsan_label s1_label,
                                             dfsan_label s2_label,
                                             dfsan_label n_label);

/// Prints the origin trace of the label at the address addr to stderr. It also
/// prints description at the beginning of the trace. If origin tracking is not
/// on, or the address is not labeled, it prints nothing.
void SANITIZER_CDECL dfsan_print_origin_trace(const void *addr,
                                              const char *description);
/// As above, but use an origin id from dfsan_get_origin() instead of address.
/// Does not include header line with taint label and address information.
void SANITIZER_CDECL dfsan_print_origin_id_trace(dfsan_origin origin);

/// Prints the origin trace of the label at the address \p addr to a
/// pre-allocated output buffer. If origin tracking is not on, or the address is
/// not labeled, it prints nothing.
///
/// Typical usage:
/// \code
///   char kDescription[] = "...";
///   char buf[1024];
///   dfsan_sprint_origin_trace(&tainted_var, kDescription, buf, sizeof(buf));
/// \endcode
///
/// Typical usage that handles truncation:
/// \code
///   char buf[1024];
///   int len = dfsan_sprint_origin_trace(&var, nullptr, buf, sizeof(buf));
///
///   if (len < sizeof(buf)) {
///     ProcessOriginTrace(buf);
///   } else {
///     char *tmpbuf = new char[len + 1];
///     dfsan_sprint_origin_trace(&var, nullptr, tmpbuf, len + 1);
///     ProcessOriginTrace(tmpbuf);
///     delete[] tmpbuf;
///   }
/// \endcode
///
/// \param addr The tainted memory address whose origin we are printing.
/// \param description A description printed at the beginning of the trace.
/// \param [out] out_buf The output buffer to write the results to.
/// \param out_buf_size The size of \p out_buf.
///
/// \returns The number of symbols that should have been written to \p out_buf
/// (not including trailing null byte '\0'). Thus, the string is truncated iff
/// return value is not less than \p out_buf_size.
size_t SANITIZER_CDECL dfsan_sprint_origin_trace(const void *addr,
                                                 const char *description,
                                                 char *out_buf,
                                                 size_t out_buf_size);
/// As above, but use an origin id from dfsan_get_origin() instead of address.
/// Does not include header line with taint label and address information.
size_t SANITIZER_CDECL dfsan_sprint_origin_id_trace(dfsan_origin origin,
                                                    char *out_buf,
                                                    size_t out_buf_size);

/// Prints the stack trace leading to this call to a pre-allocated output
/// buffer.
///
/// For usage examples, see dfsan_sprint_origin_trace.
///
/// \param [out] out_buf The output buffer to write the results to.
/// \param out_buf_size The size of \p out_buf.
///
/// \returns The number of symbols that should have been written to \p out_buf
/// (not including trailing null byte '\0'). Thus, the string is truncated iff
/// return value is not less than \p out_buf_size.
size_t SANITIZER_CDECL dfsan_sprint_stack_trace(char *out_buf,
                                                size_t out_buf_size);

/// Retrieves the very first origin associated with the data at the given
/// address.
dfsan_origin SANITIZER_CDECL dfsan_get_init_origin(const void *addr);

/// Returns the value of -dfsan-track-origins.
/// * 0: do not track origins.
/// * 1: track origins at memory store operations.
/// * 2: track origins at memory load and store operations.
int SANITIZER_CDECL dfsan_get_track_origins(void);
#ifdef __cplusplus
} // extern "C"

template <typename T> void dfsan_set_label(dfsan_label label, T &data) {
  dfsan_set_label(label, (void *)&data, sizeof(T));
}

#endif

#endif // DFSAN_INTERFACE_H
