//===-- sanitizer_suppressions.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Suppression parsing/matching code.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_suppressions.h"

#include "sanitizer_allocator_internal.h"
#include "sanitizer_common.h"
#include "sanitizer_flags.h"
#include "sanitizer_file.h"
#include "sanitizer_libc.h"
#include "sanitizer_placement_new.h"

namespace __sanitizer {

SuppressionContext::SuppressionContext(const char *suppression_types[],
                                       int suppression_types_num)
    : suppression_types_(suppression_types),
      suppression_types_num_(suppression_types_num),
      can_parse_(true) {
  CHECK_LE(suppression_types_num_, kMaxSuppressionTypes);
  internal_memset(has_suppression_type_, 0, suppression_types_num_);
}

#if !SANITIZER_FUCHSIA
static bool GetPathAssumingFileIsRelativeToExec(const char *file_path,
                                                /*out*/char *new_file_path,
                                                uptr new_file_path_size) {
  InternalMmapVector<char> exec(kMaxPathLength);
  if (ReadBinaryNameCached(exec.data(), exec.size())) {
    const char *file_name_pos = StripModuleName(exec.data());
    uptr path_to_exec_len = file_name_pos - exec.data();
    internal_strncat(new_file_path, exec.data(),
                     Min(path_to_exec_len, new_file_path_size - 1));
    internal_strncat(new_file_path, file_path,
                     new_file_path_size - internal_strlen(new_file_path) - 1);
    return true;
  }
  return false;
}

static const char *FindFile(const char *file_path,
                            /*out*/char *new_file_path,
                            uptr new_file_path_size) {
  // If we cannot find the file, check if its location is relative to
  // the location of the executable.
  if (!FileExists(file_path) && !IsAbsolutePath(file_path) &&
      GetPathAssumingFileIsRelativeToExec(file_path, new_file_path,
                                          new_file_path_size)) {
    return new_file_path;
  }
  return file_path;
}
#else
static const char *FindFile(const char *file_path, char *, uptr) {
  return file_path;
}
#endif

void SuppressionContext::ParseFromFile(const char *filename) {
  if (filename[0] == '\0')
    return;

  InternalMmapVector<char> new_file_path(kMaxPathLength);
  filename = FindFile(filename, new_file_path.data(), new_file_path.size());

  // Read the file.
  VPrintf(1, "%s: reading suppressions file at %s\n",
          SanitizerToolName, filename);
  char *file_contents;
  uptr buffer_size;
  uptr contents_size;
  if (!ReadFileToBuffer(filename, &file_contents, &buffer_size,
                        &contents_size)) {
    Printf("%s: failed to read suppressions file '%s'\n", SanitizerToolName,
           filename);
    Die();
  }

  Parse(file_contents);
  UnmapOrDie(file_contents, buffer_size);
}

bool SuppressionContext::Match(const char *str, const char *type,
                               Suppression **s) {
  can_parse_ = false;
  if (!HasSuppressionType(type))
    return false;
  for (uptr i = 0; i < suppressions_.size(); i++) {
    Suppression &cur = suppressions_[i];
    if (0 == internal_strcmp(cur.type, type) && TemplateMatch(cur.templ, str)) {
      *s = &cur;
      return true;
    }
  }
  return false;
}

static const char *StripPrefix(const char *str, const char *prefix) {
  while (*str && *str == *prefix) {
    str++;
    prefix++;
  }
  if (!*prefix)
    return str;
  return 0;
}

void SuppressionContext::Parse(const char *str) {
  // Context must not mutate once Match has been called.
  CHECK(can_parse_);
  const char *line = str;
  while (line) {
    while (line[0] == ' ' || line[0] == '\t')
      line++;
    const char *end = internal_strchr(line, '\n');
    if (end == 0)
      end = line + internal_strlen(line);
    if (line != end && line[0] != '#') {
      const char *end2 = end;
      while (line != end2 &&
             (end2[-1] == ' ' || end2[-1] == '\t' || end2[-1] == '\r'))
        end2--;
      int type;
      for (type = 0; type < suppression_types_num_; type++) {
        const char *next_char = StripPrefix(line, suppression_types_[type]);
        if (next_char && *next_char == ':') {
          line = ++next_char;
          break;
        }
      }
      if (type == suppression_types_num_) {
        Printf("%s: failed to parse suppressions.\n", SanitizerToolName);
        Printf("Supported suppression types are:\n");
        for (type = 0; type < suppression_types_num_; type++)
          Printf("- %s\n", suppression_types_[type]);
        Die();
      }
      Suppression s;
      s.type = suppression_types_[type];
      s.templ = (char*)InternalAlloc(end2 - line + 1);
      internal_memcpy(s.templ, line, end2 - line);
      s.templ[end2 - line] = 0;
      suppressions_.push_back(s);
      has_suppression_type_[type] = true;
    }
    if (end[0] == 0)
      break;
    line = end + 1;
  }
}

uptr SuppressionContext::SuppressionCount() const {
  return suppressions_.size();
}

bool SuppressionContext::HasSuppressionType(const char *type) const {
  for (int i = 0; i < suppression_types_num_; i++) {
    if (0 == internal_strcmp(type, suppression_types_[i]))
      return has_suppression_type_[i];
  }
  return false;
}

const Suppression *SuppressionContext::SuppressionAt(uptr i) const {
  CHECK_LT(i, suppressions_.size());
  return &suppressions_[i];
}

void SuppressionContext::GetMatched(
    InternalMmapVector<Suppression *> *matched) {
  for (uptr i = 0; i < suppressions_.size(); i++)
    if (atomic_load_relaxed(&suppressions_[i].hit_count))
      matched->push_back(&suppressions_[i]);
}

}  // namespace __sanitizer
