/*
 * zone.c -- zone parser
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <limits.h>
#include <stddef.h>
#if _WIN32
#  include <direct.h>
#else
#  include <unistd.h>
#endif

#include "zone.h"

typedef zone_parser_t parser_t; // convenience
typedef zone_file_t file_t;

#include "attributes.h"
#include "diagnostic.h"

#if _MSC_VER
# define strcasecmp(s1, s2) _stricmp(s1, s2)
# define strncasecmp(s1, s2, n) _strnicmp(s1, s2, n)
#else
#include <strings.h>
#endif

static const char not_a_file[] = "<string>";

#include "isadetection.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#if HAVE_HASWELL
extern int32_t zone_haswell_parse(parser_t *);
#endif

#if HAVE_WESTMERE
extern int32_t zone_westmere_parse(parser_t *);
#endif

extern int32_t zone_fallback_parse(parser_t *);

typedef struct kernel kernel_t;
struct kernel {
  const char *name;
  uint32_t instruction_set;
  int32_t (*parse)(parser_t *);
};

static const kernel_t kernels[] = {
#if HAVE_HASWELL
  { "haswell", AVX2, &zone_haswell_parse },
#endif
#if HAVE_WESTMERE
  { "westmere", SSE42|PCLMULQDQ, &zone_westmere_parse },
#endif
  { "fallback", DEFAULT, &zone_fallback_parse }
};

diagnostic_push()
msvc_diagnostic_ignored(4996)

static inline const kernel_t *
select_kernel(void)
{
  const char *preferred;
  const uint32_t supported = detect_supported_architectures();
  const size_t length = sizeof(kernels)/sizeof(kernels[0]);
  size_t count = 0;

  if ((preferred = getenv("ZONE_KERNEL"))) {
    for (; count < length; count++)
      if (strcasecmp(preferred, kernels[count].name) == 0)
        break;
    if (count == length)
      count = 0;
  }

  for (; count < length; count++)
    if ((kernels[count].instruction_set & supported) == (kernels[count].instruction_set))
      return &kernels[count];

  return &kernels[length - 1];
}

diagnostic_pop()

static int32_t parse(parser_t *parser, void *user_data)
{
  const kernel_t *kernel;

  kernel = select_kernel();
  assert(kernel);
  parser->user_data = user_data;
  return kernel->parse(parser);
}

diagnostic_push()
msvc_diagnostic_ignored(4996)

#if _WIN32
// The Win32 API offers PathIsRelative, but it requires linking with shlwapi.
// Rewriting a relative path is not too complex, unlike correct conversion of
// Windows paths in general (https://googleprojectzero.blogspot.com/2016/02/).
// Rooted paths, relative or not, unc and extended paths are never resolved
// relative to the includer.
nonnull_all
static int32_t resolve_path(const char *include, char **path)
{
  if ((*path = _fullpath(NULL, include, 0)))
    return 0;
  return (errno == ENOMEM) ? ZONE_OUT_OF_MEMORY : ZONE_NOT_A_FILE;
}
#else
nonnull_all
static int32_t resolve_path(const char *include, char **path)
{
  char *resolved;
  char buffer[PATH_MAX + 1];

  if (!(resolved = realpath(include, buffer)))
    return (errno == ENOMEM) ? ZONE_OUT_OF_MEMORY : ZONE_NOT_A_FILE;
  assert(resolved == buffer);
  size_t length = strlen(buffer);
  if (!(resolved = malloc(length + 1)))
    return ZONE_OUT_OF_MEMORY;
  memcpy(resolved, buffer, length + 1);
  *path = resolved;
  return 0;
}
#endif

nonnull((1))
static void close_file(
  parser_t *parser, file_t *file)
{
  assert((file->name == not_a_file) == (file->path == not_a_file));

  const bool is_string = file->name == not_a_file || file->path == not_a_file;

  assert(!is_string || file == &parser->first);
  assert(!is_string || file->handle == NULL);
  (void)parser;
#ifndef NDEBUG
  const bool is_stdin = file->name &&
                        file->name != not_a_file &&
                        strcmp(file->name, "-") == 0;
  assert(!is_stdin || (!file->handle || file->handle == stdin));
#endif
  if (file->buffer.data && !is_string)
    free(file->buffer.data);
  file->buffer.data = NULL;
  if (file->name && file->name != not_a_file)
    free((char *)file->name);
  file->name = NULL;
  if (file->path && file->path != not_a_file)
    free((char *)file->path);
  file->path = NULL;
  // stdin is not opened, it must not be closed
  if (file->handle && file->handle != stdin)
    (void)fclose(file->handle);
  file->handle = NULL;
}

nonnull_all
static void initialize_file(
  parser_t *parser, file_t *file)
{
  const size_t size = offsetof(file_t, fields.head);
  memset(file, 0, size);

  if (file == &parser->first) {
    file->includer = NULL;
    memcpy(file->origin.octets,
           parser->options.origin.octets,
           parser->options.origin.length);
    file->origin.length = parser->options.origin.length;
    file->last_class = parser->options.default_class;
    file->dollar_ttl = parser->options.default_ttl;
    file->last_ttl = parser->options.default_ttl;
    file->ttl = file->default_ttl = &file->last_ttl;
  } else {
    assert(parser->file);
    file->includer = parser->file;
    memcpy(&file->origin, &parser->file->origin, sizeof(file->origin));
    // Retain class and TTL values.
    file->last_class = parser->file->last_class;
    file->dollar_ttl = parser->file->dollar_ttl;
    file->last_ttl = parser->file->last_ttl;
    // RRs appearing after the $TTL directive that do not explicitly include
    // a TTL value, have their TTL set to the TTL in the $TTL directive. RRs
    // appearing before a $TTL directive use the last explicitly stated value.
    if (parser->file->default_ttl == &parser->file->last_ttl)
      file->ttl = file->default_ttl = &file->last_ttl;
    else
      file->ttl = file->default_ttl = &file->dollar_ttl;
  }

  file->line = 1;
  file->name = (char *)not_a_file;
  file->path = (char *)not_a_file;
  file->handle = NULL;
  file->buffer.data = NULL;
  file->start_of_line = true;
  file->end_of_file = 1;
  file->fields.tape[0] = NULL;
  file->fields.head = file->fields.tail = file->fields.tape;
  file->delimiters.tape[0] = NULL;
  file->delimiters.head = file->delimiters.tail = file->delimiters.tape;
  file->newlines.tape[0] = 0;
  file->newlines.head = file->newlines.tail = file->newlines.tape;
}

nonnull_all
static int32_t open_file(
  parser_t *parser, file_t *file, const char *include, size_t length)
{
  int32_t code;
  const size_t size = ZONE_WINDOW_SIZE + 1 + ZONE_BLOCK_SIZE;

  initialize_file(parser, file);

  file->path = NULL;
  if (!(file->name = malloc(length + 1)))
    return ZONE_OUT_OF_MEMORY;
  memcpy(file->name, include, length);
  file->name[length] = '\0';
  if (!(file->buffer.data = malloc(size)))
    return (void)close_file(parser, file), ZONE_OUT_OF_MEMORY;
  file->buffer.data[0] = '\0';
  file->buffer.size = ZONE_WINDOW_SIZE;
  file->end_of_file = 0;
  file->fields.tape[0] = &file->buffer.data[0];
  file->fields.tape[1] = &file->buffer.data[0];

  if(file == &parser->first && strcmp(file->name, "-") == 0) {
    if (!(file->path = malloc(2)))
      return (void)close_file(parser, file), ZONE_OUT_OF_MEMORY;
    file->path[0] = '-';
    file->path[1] = '\0';
  } else {
    // The file is resolved relative to the working directory. The absolute
    // path is used to protect against recusive includes. Not for opening the
    // file as file descriptors for pipes and sockets the entries will be
    // symoblic links whose content is the file type with the inode.
    // See NLnetLabs/nsd#380.
    if ((code = resolve_path(file->name, &file->path)))
      return (void)close_file(parser, file), code;
  }

  if(strcmp(file->path, "-") == 0) {
    file->handle = stdin;
    return 0;
  } else {
    if ((file->handle = fopen(file->name, "rb")))
      return 0;
  }

  switch (errno) {
    case ENOMEM:
      code = ZONE_OUT_OF_MEMORY;
      break;
    case EACCES:
      code = ZONE_NOT_PERMITTED;
      break;
    default:
      code = ZONE_NOT_A_FILE;
      break;
  }

  close_file(parser, file);
  return code;
}

diagnostic_pop()

diagnostic_push()
clang_diagnostic_ignored(missing-prototypes)

nonnull((1))
void zone_close_file(
  parser_t *parser, zone_file_t *file)
{
  if (!file)
    return;
  close_file(parser, file);
  free(file);
}

nonnull_all
int32_t zone_open_file(
  parser_t *parser, const char *path, size_t length, zone_file_t **file)
{
  int32_t code;

  if (!(*file = malloc(sizeof(**file))))
    return ZONE_OUT_OF_MEMORY;
  if ((code = open_file(parser, *file, path, length)) == 0)
    return 0;

  free(*file);

  const char *reason = NULL;
  switch (code) {
    case ZONE_OUT_OF_MEMORY: reason = "out of memory"; break;
    case ZONE_NOT_PERMITTED: reason = "access denied"; break;
    case ZONE_NOT_A_FILE:    reason = "no such file";  break;
  }

  assert(reason);
  zone_error(parser, "Cannot open %.*s, %s", (int)length, path, reason);
  return code;
}

nonnull_all
void zone_close(parser_t *parser)
{
  assert(parser);
  for (zone_file_t *file = parser->file, *includer; file; file = includer) {
    includer = file->includer;
    close_file(parser, file);
    if (file != &parser->first)
      free(file);
  }
}

nonnull((1,2,3))
static int32_t initialize_parser(
  zone_parser_t *parser,
  const zone_options_t *options,
  zone_buffers_t *buffers,
  void *user_data)
{
  if (!options->accept.callback)
    return ZONE_BAD_PARAMETER;
  if (!options->default_ttl)
    return ZONE_BAD_PARAMETER;
  if (!options->secondary && options->default_ttl > INT32_MAX)
    return ZONE_BAD_PARAMETER;
  if (!options->origin.octets || !options->origin.length)
    return ZONE_BAD_PARAMETER;

  const uint8_t *root = &options->origin.octets[options->origin.length - 1];
  if (root[0] != 0)
    return ZONE_BAD_PARAMETER;
  const uint8_t *label = &options->origin.octets[0];
  while (label < root) {
    if (root - label < label[0])
      return ZONE_BAD_PARAMETER;
    label += label[0] + 1;
  }

  if (label != root)
    return ZONE_BAD_PARAMETER;

  const size_t size = offsetof(parser_t, file);
  memset(parser, 0, size);
  parser->options = *options;
  parser->user_data = user_data;
  parser->file = &parser->first;
  parser->buffers.size = buffers->size;
  parser->buffers.owner.active = 0;
  parser->buffers.owner.blocks = buffers->owner;
  parser->buffers.rdata.active = 0;
  parser->buffers.rdata.blocks = buffers->rdata;
  parser->owner = &parser->buffers.owner.blocks[0];
  parser->owner->length = 0;
  parser->rdata = &parser->buffers.rdata.blocks[0];

  if (!parser->options.no_includes && !parser->options.include_limit)
    parser->options.include_limit = 10; // arbitrary, default in NSD

  return 0;
}

int32_t zone_open(
  zone_parser_t *parser,
  const zone_options_t *options,
  zone_buffers_t *buffers,
  const char *path,
  void *user_data)
{
  int32_t code;

  if ((code = initialize_parser(parser, options, buffers, user_data)) < 0)
    return code;
  if ((code = open_file(parser, &parser->first, path, strlen(path))) == 0)
    return 0;

  const char *reason = NULL;
  switch (code) {
    case ZONE_OUT_OF_MEMORY: reason = "out of memory"; break;
    case ZONE_NOT_PERMITTED: reason = "access denied"; break;
    case ZONE_NOT_A_FILE:    reason = "no such file";  break;
  }

  assert(reason);
  zone_error(parser, "Cannot open %s, %s", path, reason);
  return code;
}

diagnostic_pop()

int32_t zone_parse(
  zone_parser_t *parser,
  const zone_options_t *options,
  zone_buffers_t *buffers,
  const char *path,
  void *user_data)
{
  int32_t code;

  if ((code = zone_open(parser, options, buffers, path, user_data)) < 0)
    return code;
  code = parse(parser, user_data);
  zone_close(parser);
  return code;
}

int32_t zone_parse_string(
  parser_t *parser,
  const zone_options_t *options,
  zone_buffers_t *buffers,
  const char *string,
  size_t length,
  void *user_data)
{
  int32_t code;

  if ((code = initialize_parser(parser, options, buffers, user_data)) < 0)
    return code;
  if (!length || string[length] != '\0')
    return ZONE_BAD_PARAMETER;
  initialize_file(parser, parser->file);
  parser->file->buffer.data = (char *)string;
  parser->file->buffer.size = length;
  parser->file->buffer.length = length;
  parser->file->fields.tape[0] = &string[length];
  parser->file->fields.tape[1] = &string[length];
  assert(parser->file->end_of_file == 1);

  code = parse(parser, user_data);
  zone_close(parser);
  return code;
}

zone_nonnull((1,5))
static void print_message(
  zone_parser_t *parser,
  uint32_t priority,
  const char *file,
  size_t line,
  const char *message,
  void *user_data)
{
  (void)parser;
  (void)user_data;

  assert(parser->file);
  FILE *output = priority == ZONE_INFO ? stdout : stderr;

  if (file)
    fprintf(output, "%s:%zu: %s\n", file, line, message);
  else
    fprintf(output, "%s\n", message);
}

void zone_vlog(
  zone_parser_t *parser,
  uint32_t priority,
  const char *format,
  va_list arguments);

void zone_vlog(
  zone_parser_t *parser,
  uint32_t priority,
  const char *format,
  va_list arguments)
{
  char message[2048];
  int length;
  zone_log_t callback = print_message;

  if (!(priority & ~parser->options.log.mask))
    return;

  length = vsnprintf(message, sizeof(message), format, arguments);
  assert(length >= 0);
  if ((size_t)length >= sizeof(message))
    memcpy(message+(sizeof(message) - 4), "...", 3);
  if (parser->options.log.callback)
    callback = parser->options.log.callback;
  assert(parser->file);
  const char *file = parser->file->name;
  const size_t line = parser->file->line;
  callback(parser, priority, file, line, message, parser->user_data);
}

void zone_log(
  zone_parser_t *parser,
  uint32_t priority,
  const char *format,
  ...)
{
  va_list arguments;
  va_start(arguments, format);
  zone_vlog(parser, priority, format, arguments);
  va_end(arguments);
}
