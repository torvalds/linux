//===-- lldb-types.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_LLDB_TYPES_H
#define LLDB_LLDB_TYPES_H

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"

#include <cstdint>

// All host systems must define:
//  lldb::rwlock_t          The type representing a read/write lock on the host
//  lldb::process_t         The type representing a process on the host
//  lldb::thread_t          The native thread type for spawned threads on the
//                          host
//  lldb::file_t            The type representing a file on the host
//  lldb::socket_t          The type representing a socket on the host
//  lldb::thread_arg_t      The type of the one and only thread creation
//                          argument for the host system
//  lldb::thread_result_t   The type that gets returned when a thread finishes
//  lldb::thread_func_t     The function prototype used to spawn a thread on the
//                          host system.
//  lldb::pipe_t            The type representing a pipe on the host
//
// Additionally, lldb defines a few macros based on these definitions:
//  LLDB_INVALID_PROCESS      The value of an invalid lldb::process_t
//  LLDB_INVALID_HOST_THREAD  The value of an invalid lldb::thread_t
//  LLDB_INVALID_PIPE         The value of an invalid lldb::pipe_t

#ifdef _WIN32

#include <process.h>

namespace lldb {
typedef void *rwlock_t;
typedef void *process_t;                          // Process type is HANDLE
typedef void *thread_t;                           // Host thread type
typedef void *file_t;                             // Host file type
typedef unsigned int __w64 socket_t;              // Host socket type
typedef void *thread_arg_t;                       // Host thread argument type
typedef unsigned thread_result_t;                 // Host thread result type
typedef thread_result_t (*thread_func_t)(void *); // Host thread function type
typedef void *pipe_t;                             // Host pipe type is HANDLE

#else

#include <pthread.h>

namespace lldb {
typedef pthread_rwlock_t rwlock_t;
typedef uint64_t process_t;             // Process type is just a pid.
typedef pthread_t thread_t;             // Host thread type
typedef int file_t;                     // Host file type
typedef int socket_t;                   // Host socket type
typedef void *thread_arg_t;             // Host thread argument type
typedef void *thread_result_t;          // Host thread result type
typedef void *(*thread_func_t)(void *); // Host thread function type
typedef int pipe_t;                     // Host pipe type

#endif // _WIN32

#define LLDB_INVALID_PROCESS ((lldb::process_t)-1)
#define LLDB_INVALID_HOST_THREAD ((lldb::thread_t)NULL)
#define LLDB_INVALID_PIPE ((lldb::pipe_t)-1)
#define LLDB_INVALID_CALLBACK_TOKEN ((lldb::callback_token_t) - 1)

typedef void (*LogOutputCallback)(const char *, void *baton);
typedef bool (*CommandOverrideCallback)(void *baton, const char **argv);
typedef bool (*ExpressionCancelCallback)(ExpressionEvaluationPhase phase,
                                         void *baton);

typedef void *ScriptObjectPtr;

typedef uint64_t addr_t;
typedef int32_t callback_token_t;
typedef uint64_t user_id_t;
typedef uint64_t pid_t;
typedef uint64_t tid_t;
typedef uint64_t offset_t;
typedef int32_t break_id_t;
typedef int32_t watch_id_t;
typedef uint32_t wp_resource_id_t;
typedef void *opaque_compiler_type_t;
typedef uint64_t queue_id_t;
typedef uint32_t cpu_id_t; // CPU core id

} // namespace lldb

#endif // LLDB_LLDB_TYPES_H
