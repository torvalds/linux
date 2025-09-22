//===-- GenealogySPI.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_GENEALOGYSPI_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_GENEALOGYSPI_H

#include <xpc/xpc.h>

typedef void *os_activity_process_list_t;
typedef void *os_activity_list_t;
typedef void *os_trace_message_list_t;
typedef struct os_activity_watch_s *os_activity_watch_t;
typedef uint64_t os_activity_t;

struct os_activity_breadcrumb_s {
  uint32_t breadcrumb_id;
  uint64_t activity_id;
  uint64_t timestamp;
  const char *name;
};

typedef struct os_activity_breadcrumb_s *os_activity_breadcrumb_t;

typedef struct os_trace_message_s {
  uint64_t trace_id;
  uint64_t thread;
  uint64_t timestamp;
  uint32_t offset;
  xpc_object_t __unsafe_unretained payload;
  const uint8_t *image_uuid;
  const char *image_path;
  const char *format;
  const void *buffer;
  size_t bufferLen;
} * os_trace_message_t;

typedef struct os_activity_process_s {
  os_activity_process_list_t child_procs;
  os_trace_message_list_t messages;
  os_activity_list_t activities;
  void *breadcrumbs;
  uint64_t proc_id;
  const uint8_t *image_uuid;
  const char *image_path;
  pid_t pid;
} * os_activity_process_t;

typedef struct os_activity_entry_s {
  uint64_t activity_start;
  os_activity_t activity_id;
  os_activity_t parent_id;
  const char *activity_name;
  const char *reason;
  os_trace_message_list_t messages;
} * os_activity_entry_t;

enum {
  OS_ACTIVITY_DIAGNOSTIC_DEFAULT = 0x00000000,
  OS_ACTIVITY_DIAGNOSTIC_PROCESS_ONLY = 0x00000001,
  OS_ACTIVITY_DIAGNOSTIC_SKIP_DECODE = 0x00000002,
  OS_ACTIVITY_DIAGNOSTIC_FLATTENED = 0x00000004,
  OS_ACTIVITY_DIAGNOSTIC_ALL_ACTIVITIES = 0x00000008,
  OS_ACTIVITY_DIAGNOSTIC_MAX = 0x0000000f
};
typedef uint32_t os_activity_diagnostic_flag_t;

enum {
  OS_ACTIVITY_WATCH_DEFAULT = 0x00000000,
  OS_ACTIVITY_WATCH_PROCESS_ONLY = 0x00000001,
  OS_ACTIVITY_WATCH_SKIP_DECODE = 0x00000002,
  OS_ACTIVITY_WATCH_PAYLOAD = 0x00000004,
  OS_ACTIVITY_WATCH_ERRORS = 0x00000008,
  OS_ACTIVITY_WATCH_FAULTS = 0x00000010,
  OS_ACTIVITY_WATCH_MAX = 0x0000001f
};
typedef uint32_t os_activity_watch_flag_t;

// Return values from os_trace_get_type()
#define OS_TRACE_TYPE_RELEASE (1u << 0)
#define OS_TRACE_TYPE_DEBUG (1u << 1)
#define OS_TRACE_TYPE_ERROR ((1u << 6) | (1u << 0))
#define OS_TRACE_TYPE_FAULT ((1u << 7) | (1u << 6) | (1u << 0))

typedef void (^os_activity_watch_block_t)(os_activity_watch_t watch,
                                          os_activity_process_t process_info,
                                          bool canceled);
typedef void (^os_diagnostic_block_t)(os_activity_process_list_t processes,
                                      int error);

#endif
