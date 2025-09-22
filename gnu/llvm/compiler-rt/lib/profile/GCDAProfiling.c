/*===- GCDAProfiling.c - Support library for GCDA file emission -----------===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
|*===----------------------------------------------------------------------===*|
|*
|* This file implements the call back routines for the gcov profiling
|* instrumentation pass. Link against this library when running code through
|* the -insert-gcov-profiling LLVM pass.
|*
|* We emit files in a corrupt version of GCOV's "gcda" file format. These files
|* are only close enough that LCOV will happily parse them. Anything that lcov
|* ignores is missing.
|*
|* TODO: gcov is multi-process safe by having each exit open the existing file
|* and append to it. We'd like to achieve that and be thread-safe too.
|*
\*===----------------------------------------------------------------------===*/

#if !defined(__Fuchsia__)

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "WindowsMMap.h"
#else
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "InstrProfiling.h"
#include "InstrProfilingUtil.h"

/* #define DEBUG_GCDAPROFILING */

enum {
  GCOV_DATA_MAGIC = 0x67636461, // "gcda"

  GCOV_TAG_FUNCTION = 0x01000000,
  GCOV_TAG_COUNTER_ARCS = 0x01a10000,
  // GCOV_TAG_OBJECT_SUMMARY superseded GCOV_TAG_PROGRAM_SUMMARY in GCC 9.
  GCOV_TAG_OBJECT_SUMMARY = 0xa1000000,
  GCOV_TAG_PROGRAM_SUMMARY = 0xa3000000,
};

/*
 * --- GCOV file format I/O primitives ---
 */

/*
 * The current file name we're outputting. Used primarily for error logging.
 */
static char *filename = NULL;

/*
 * The current file we're outputting.
 */
static FILE *output_file = NULL;

/*
 * Buffer that we write things into.
 */
#define WRITE_BUFFER_SIZE (128 * 1024)
static unsigned char *write_buffer = NULL;
static uint64_t cur_buffer_size = 0;
static uint64_t cur_pos = 0;
static uint64_t file_size = 0;
static int new_file = 0;
static int gcov_version;
#if defined(_WIN32)
static HANDLE mmap_handle = NULL;
#endif
static int fd = -1;

typedef void (*fn_ptr)(void);

typedef void* dynamic_object_id;
// The address of this variable identifies a given dynamic object.
static dynamic_object_id current_id;
#define CURRENT_ID (&current_id)

struct fn_node {
  dynamic_object_id id;
  fn_ptr fn;
  struct fn_node* next;
};

struct fn_list {
  struct fn_node *head, *tail;
};

/*
 * A list of functions to write out the data, shared between all dynamic objects.
 */
struct fn_list writeout_fn_list;

/*
 *  A list of reset functions, shared between all dynamic objects.
 */
struct fn_list reset_fn_list;

static void fn_list_insert(struct fn_list* list, fn_ptr fn) {
  struct fn_node* new_node = malloc(sizeof(struct fn_node));
  new_node->fn = fn;
  new_node->next = NULL;
  new_node->id = CURRENT_ID;

  if (!list->head) {
    list->head = list->tail = new_node;
  } else {
    list->tail->next = new_node;
    list->tail = new_node;
  }
}

static void fn_list_remove(struct fn_list* list) {
  struct fn_node* curr = list->head;
  struct fn_node* prev = NULL;
  struct fn_node* next = NULL;

  while (curr) {
    next = curr->next;

    if (curr->id == CURRENT_ID) {
      if (curr == list->head) {
        list->head = next;
      }

      if (curr == list->tail) {
        list->tail = prev;
      }

      if (prev) {
        prev->next = next;
      }

      free(curr);
    } else {
      prev = curr;
    }

    curr = next;
  }
}

static void resize_write_buffer(uint64_t size) {
  if (!new_file) return;
  size += cur_pos;
  if (size <= cur_buffer_size) return;
  size = (size - 1) / WRITE_BUFFER_SIZE + 1;
  size *= WRITE_BUFFER_SIZE;
  write_buffer = realloc(write_buffer, size);
  cur_buffer_size = size;
}

static void write_bytes(const char *s, size_t len) {
  resize_write_buffer(len);
  memcpy(&write_buffer[cur_pos], s, len);
  cur_pos += len;
}

static void write_32bit_value(uint32_t i) {
  write_bytes((char*)&i, 4);
}

static void write_64bit_value(uint64_t i) {
  // GCOV uses a lo-/hi-word format even on big-endian systems.
  // See also GCOVBuffer::readInt64 in LLVM.
  uint32_t lo = (uint32_t) i;
  uint32_t hi = (uint32_t) (i >> 32);
  write_32bit_value(lo);
  write_32bit_value(hi);
}

static uint32_t read_32bit_value(void) {
  uint32_t val;

  if (new_file)
    return (uint32_t)-1;

  val = *(uint32_t*)&write_buffer[cur_pos];
  cur_pos += 4;
  return val;
}

static uint64_t read_64bit_value(void) {
  // GCOV uses a lo-/hi-word format even on big-endian systems.
  // See also GCOVBuffer::readInt64 in LLVM.
  uint32_t lo = read_32bit_value();
  uint32_t hi = read_32bit_value();
  return ((uint64_t)hi << 32) | ((uint64_t)lo);
}

static char *mangle_filename(const char *orig_filename) {
  char *new_filename;
  size_t prefix_len;
  int prefix_strip;
  const char *prefix = lprofGetPathPrefix(&prefix_strip, &prefix_len);

  if (prefix == NULL)
    return strdup(orig_filename);

  new_filename = malloc(prefix_len + 1 + strlen(orig_filename) + 1);
  lprofApplyPathPrefix(new_filename, orig_filename, prefix, prefix_len,
                       prefix_strip);

  return new_filename;
}

static int map_file(void) {
  fseek(output_file, 0L, SEEK_END);
  file_size = ftell(output_file);

  /* A size of 0 means the file has been created just now (possibly by another
   * process in lock-after-open race condition). No need to mmap. */
  if (file_size == 0)
    return -1;

#if defined(_WIN32)
  HANDLE mmap_fd;
  if (fd == -1)
    mmap_fd = INVALID_HANDLE_VALUE;
  else
    mmap_fd = (HANDLE)_get_osfhandle(fd);

  mmap_handle = CreateFileMapping(mmap_fd, NULL, PAGE_READWRITE, DWORD_HI(file_size), DWORD_LO(file_size), NULL);
  if (mmap_handle == NULL) {
    fprintf(stderr, "profiling: %s: cannot create file mapping: %lu\n",
            filename, GetLastError());
    return -1;
  }

  write_buffer = MapViewOfFile(mmap_handle, FILE_MAP_WRITE, 0, 0, file_size);
  if (write_buffer == NULL) {
    fprintf(stderr, "profiling: %s: cannot map: %lu\n", filename,
            GetLastError());
    CloseHandle(mmap_handle);
    return -1;
  }
#else
  write_buffer = mmap(0, file_size, PROT_READ | PROT_WRITE,
                      MAP_FILE | MAP_SHARED, fd, 0);
  if (write_buffer == (void *)-1) {
    int errnum = errno;
    fprintf(stderr, "profiling: %s: cannot map: %s\n", filename,
            strerror(errnum));
    return -1;
  }
#endif

  return 0;
}

static void unmap_file(void) {
#if defined(_WIN32)
  if (!UnmapViewOfFile(write_buffer)) {
    fprintf(stderr, "profiling: %s: cannot unmap mapped view: %lu\n", filename,
            GetLastError());
  }

  if (!CloseHandle(mmap_handle)) {
    fprintf(stderr, "profiling: %s: cannot close file mapping handle: %lu\n",
            filename, GetLastError());
  }

  mmap_handle = NULL;
#else
  if (munmap(write_buffer, file_size) == -1) {
    int errnum = errno;
    fprintf(stderr, "profiling: %s: cannot munmap: %s\n", filename,
            strerror(errnum));
  }
#endif

  write_buffer = NULL;
  file_size = 0;
}

/*
 * --- LLVM line counter API ---
 */

/* A file in this case is a translation unit. Each .o file built with line
 * profiling enabled will emit to a different file. Only one file may be
 * started at a time.
 */
COMPILER_RT_VISIBILITY
void llvm_gcda_start_file(const char *orig_filename, uint32_t version,
                          uint32_t checksum) {
  const char *mode = "r+b";
  filename = mangle_filename(orig_filename);

  /* Try just opening the file. */
  fd = open(filename, O_RDWR | O_BINARY);

  if (fd == -1) {
    /* Try creating the file. */
    fd = open(filename, O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0644);
    if (fd != -1) {
      mode = "w+b";
    } else {
      /* Try creating the directories first then opening the file. */
      __llvm_profile_recursive_mkdir(filename);
      fd = open(filename, O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0644);
      if (fd != -1) {
        mode = "w+b";
      } else {
        /* Another process may have created the file just now.
         * Try opening it without O_CREAT and O_EXCL. */
        fd = open(filename, O_RDWR | O_BINARY);
        if (fd == -1) {
          /* Bah! It's hopeless. */
          int errnum = errno;
          fprintf(stderr, "profiling: %s: cannot open: %s\n", filename,
                  strerror(errnum));
          return;
        }
      }
    }
  }

  /* Try to flock the file to serialize concurrent processes writing out to the
   * same GCDA. This can fail if the filesystem doesn't support it, but in that
   * case we'll just carry on with the old racy behaviour and hope for the best.
   */
  lprofLockFd(fd);
  output_file = fdopen(fd, mode);

  /* Initialize the write buffer. */
  new_file = 0;
  write_buffer = NULL;
  cur_buffer_size = 0;
  cur_pos = 0;

  if (map_file() == -1) {
    /* The file has been created just now (file_size == 0) or mmap failed
     * unexpectedly. In the latter case, try to recover by clobbering. */
    new_file = 1;
    write_buffer = NULL;
    resize_write_buffer(WRITE_BUFFER_SIZE);
    memset(write_buffer, 0, WRITE_BUFFER_SIZE);
  }

  /* gcda file, version, stamp checksum. */
  {
    uint8_t c3 = version >> 24;
    uint8_t c2 = (version >> 16) & 255;
    uint8_t c1 = (version >> 8) & 255;
    gcov_version = c3 >= 'A' ? (c3 - 'A') * 100 + (c2 - '0') * 10 + c1 - '0'
                             : (c3 - '0') * 10 + c1 - '0';
  }
  write_32bit_value(GCOV_DATA_MAGIC);
  write_32bit_value(version);
  write_32bit_value(checksum);

#ifdef DEBUG_GCDAPROFILING
  fprintf(stderr, "llvmgcda: [%s]\n", orig_filename);
#endif
}

COMPILER_RT_VISIBILITY
void llvm_gcda_emit_function(uint32_t ident, uint32_t func_checksum,
                             uint32_t cfg_checksum) {
  uint32_t len = 2;
  int use_extra_checksum = gcov_version >= 47;

  if (use_extra_checksum)
    len++;
#ifdef DEBUG_GCDAPROFILING
  fprintf(stderr, "llvmgcda: function id=0x%08x\n", ident);
#endif
  if (!output_file) return;

  /* function tag */
  write_32bit_value(GCOV_TAG_FUNCTION);
  write_32bit_value(len);
  write_32bit_value(ident);
  write_32bit_value(func_checksum);
  if (use_extra_checksum)
    write_32bit_value(cfg_checksum);
}

COMPILER_RT_VISIBILITY
void llvm_gcda_emit_arcs(uint32_t num_counters, uint64_t *counters) {
  uint32_t i;
  uint64_t *old_ctrs = NULL;
  uint32_t val = 0;
  uint64_t save_cur_pos = cur_pos;

  if (!output_file) return;

  val = read_32bit_value();

  if (val != (uint32_t)-1) {
    /* There are counters present in the file. Merge them. */
    if (val != GCOV_TAG_COUNTER_ARCS) {
      fprintf(stderr, "profiling: %s: cannot merge previous GCDA file: "
                      "corrupt arc tag (0x%08x)\n",
              filename, val);
      return;
    }

    val = read_32bit_value();
    if (val == (uint32_t)-1 || val / 2 != num_counters) {
      fprintf(stderr, "profiling: %s: cannot merge previous GCDA file: "
                      "mismatched number of counters (%d)\n",
              filename, val);
      return;
    }

    old_ctrs = malloc(sizeof(uint64_t) * num_counters);
    for (i = 0; i < num_counters; ++i)
      old_ctrs[i] = read_64bit_value();
  }

  cur_pos = save_cur_pos;

  /* Counter #1 (arcs) tag */
  write_32bit_value(GCOV_TAG_COUNTER_ARCS);
  write_32bit_value(num_counters * 2);
  for (i = 0; i < num_counters; ++i) {
    counters[i] += (old_ctrs ? old_ctrs[i] : 0);
    write_64bit_value(counters[i]);
  }

  free(old_ctrs);

#ifdef DEBUG_GCDAPROFILING
  fprintf(stderr, "llvmgcda:   %u arcs\n", num_counters);
  for (i = 0; i < num_counters; ++i)
    fprintf(stderr, "llvmgcda:   %llu\n", (unsigned long long)counters[i]);
#endif
}

COMPILER_RT_VISIBILITY
void llvm_gcda_summary_info(void) {
  uint32_t runs = 1;
  static uint32_t run_counted = 0; // We only want to increase the run count once.
  uint32_t val = 0;
  uint64_t save_cur_pos = cur_pos;

  if (!output_file) return;

  val = read_32bit_value();

  if (val != (uint32_t)-1) {
    /* There are counters present in the file. Merge them. */
    uint32_t gcov_tag =
        gcov_version >= 90 ? GCOV_TAG_OBJECT_SUMMARY : GCOV_TAG_PROGRAM_SUMMARY;
    if (val != gcov_tag) {
      fprintf(stderr,
              "profiling: %s: cannot merge previous run count: "
              "corrupt object tag (0x%08x)\n",
              filename, val);
      return;
    }

    val = read_32bit_value(); /* length */
    uint32_t prev_runs;
    if (gcov_version < 90) {
      read_32bit_value();
      read_32bit_value();
      prev_runs = read_32bit_value();
    } else {
      prev_runs = read_32bit_value();
      read_32bit_value();
    }
    for (uint32_t i = gcov_version < 90 ? 3 : 2; i < val; ++i)
      read_32bit_value();
    /* Add previous run count to new counter, if not already counted before. */
    runs = run_counted ? prev_runs : prev_runs + 1;
  }

  cur_pos = save_cur_pos;

  if (gcov_version >= 90) {
    write_32bit_value(GCOV_TAG_OBJECT_SUMMARY);
    write_32bit_value(2);
    write_32bit_value(runs);
    write_32bit_value(0); // sum_max
  } else {
    // Before gcov 4.8 (r190952), GCOV_TAG_SUMMARY_LENGTH was 9. r190952 set
    // GCOV_TAG_SUMMARY_LENGTH to 22. We simply use the smallest length which
    // can make gcov read "Runs:".
    write_32bit_value(GCOV_TAG_PROGRAM_SUMMARY);
    write_32bit_value(3);
    write_32bit_value(0);
    write_32bit_value(0);
    write_32bit_value(runs);
  }

  run_counted = 1;

#ifdef DEBUG_GCDAPROFILING
  fprintf(stderr, "llvmgcda:   %u runs\n", runs);
#endif
}

COMPILER_RT_VISIBILITY
void llvm_gcda_end_file(void) {
  /* Write out EOF record. */
  if (output_file) {
    write_bytes("\0\0\0\0\0\0\0\0", 8);

    if (new_file) {
      fwrite(write_buffer, cur_pos, 1, output_file);
      free(write_buffer);
    } else {
      unmap_file();
    }

    fflush(output_file);
    lprofUnlockFd(fd);
    fclose(output_file);
    output_file = NULL;
    write_buffer = NULL;
  }
  free(filename);

#ifdef DEBUG_GCDAPROFILING
  fprintf(stderr, "llvmgcda: -----\n");
#endif
}

COMPILER_RT_VISIBILITY
void llvm_register_writeout_function(fn_ptr fn) {
  fn_list_insert(&writeout_fn_list, fn);
}

COMPILER_RT_VISIBILITY
void llvm_writeout_files(void) {
  struct fn_node *curr = writeout_fn_list.head;

  while (curr) {
    if (curr->id == CURRENT_ID) {
      curr->fn();
    }
    curr = curr->next;
  }
}

#ifndef _WIN32
// __attribute__((destructor)) and destructors whose priorities are greater than
// 100 run before this function and can thus be tracked. The priority is
// compatible with GCC 7 onwards.
#if __GNUC__ >= 9
#pragma GCC diagnostic ignored "-Wprio-ctor-dtor"
#endif
__attribute__((destructor(100)))
#endif
static void llvm_writeout_and_clear(void) {
  llvm_writeout_files();
  fn_list_remove(&writeout_fn_list);
}

COMPILER_RT_VISIBILITY
void llvm_register_reset_function(fn_ptr fn) {
  fn_list_insert(&reset_fn_list, fn);
}

COMPILER_RT_VISIBILITY
void llvm_delete_reset_function_list(void) { fn_list_remove(&reset_fn_list); }

COMPILER_RT_VISIBILITY
void llvm_reset_counters(void) {
  struct fn_node *curr = reset_fn_list.head;

  while (curr) {
    if (curr->id == CURRENT_ID) {
      curr->fn();
    }
    curr = curr->next;
  }
}

#if !defined(_WIN32)
COMPILER_RT_VISIBILITY
pid_t __gcov_fork() {
  pid_t parent_pid = getpid();
  pid_t pid = fork();

  if (pid == 0) {
    pid_t child_pid = getpid();
    if (child_pid != parent_pid) {
      // The pid changed so we've a fork (one could have its own fork function)
      // Just reset the counters for this child process
      // threads.
      llvm_reset_counters();
    }
  }
  return pid;
}
#endif

COMPILER_RT_VISIBILITY
void llvm_gcov_init(fn_ptr wfn, fn_ptr rfn) {
  static int atexit_ran = 0;

  if (wfn)
    llvm_register_writeout_function(wfn);

  if (rfn)
    llvm_register_reset_function(rfn);

  if (atexit_ran == 0) {
    atexit_ran = 1;

    /* Make sure we write out the data and delete the data structures. */
    atexit(llvm_delete_reset_function_list);
#ifdef _WIN32
    atexit(llvm_writeout_and_clear);
#endif
  }
}

void __gcov_dump(void) {
  for (struct fn_node *f = writeout_fn_list.head; f; f = f->next)
    f->fn();
}

void __gcov_reset(void) {
  for (struct fn_node *f = reset_fn_list.head; f; f = f->next)
    f->fn();
}

#endif
