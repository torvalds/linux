//===-- sanitizer_common_interceptors_format.inc ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Scanf/printf implementation for use in *Sanitizer interceptors.
// Follows http://pubs.opengroup.org/onlinepubs/9699919799/functions/fscanf.html
// and http://pubs.opengroup.org/onlinepubs/9699919799/functions/fprintf.html
// with a few common GNU extensions.
//
//===----------------------------------------------------------------------===//

#include <stdarg.h>

static const char *parse_number(const char *p, int *out) {
  *out = internal_atoll(p);
  while (*p >= '0' && *p <= '9')
    ++p;
  return p;
}

static const char *maybe_parse_param_index(const char *p, int *out) {
  // n$
  if (*p >= '0' && *p <= '9') {
    int number;
    const char *q = parse_number(p, &number);
    CHECK(q);
    if (*q == '$') {
      *out = number;
      p = q + 1;
    }
  }

  // Otherwise, do not change p. This will be re-parsed later as the field
  // width.
  return p;
}

static bool char_is_one_of(char c, const char *s) {
  return !!internal_strchr(s, c);
}

static const char *maybe_parse_length_modifier(const char *p, char ll[2]) {
  if (char_is_one_of(*p, "jztLq")) {
    ll[0] = *p;
    ++p;
  } else if (*p == 'h') {
    ll[0] = 'h';
    ++p;
    if (*p == 'h') {
      ll[1] = 'h';
      ++p;
    }
  } else if (*p == 'l') {
    ll[0] = 'l';
    ++p;
    if (*p == 'l') {
      ll[1] = 'l';
      ++p;
    }
  }
  return p;
}

// Returns true if the character is an integer conversion specifier.
static bool format_is_integer_conv(char c) {
  return char_is_one_of(c, "diouxXn");
}

// Returns true if the character is an floating point conversion specifier.
static bool format_is_float_conv(char c) {
  return char_is_one_of(c, "aAeEfFgG");
}

// Returns string output character size for string-like conversions,
// or 0 if the conversion is invalid.
static int format_get_char_size(char convSpecifier,
                                const char lengthModifier[2]) {
  if (char_is_one_of(convSpecifier, "CS")) {
    return sizeof(wchar_t);
  }

  if (char_is_one_of(convSpecifier, "cs[")) {
    if (lengthModifier[0] == 'l' && lengthModifier[1] == '\0')
      return sizeof(wchar_t);
    else if (lengthModifier[0] == '\0')
      return sizeof(char);
  }

  return 0;
}

enum FormatStoreSize {
  // Store size not known in advance; can be calculated as wcslen() of the
  // destination buffer.
  FSS_WCSLEN = -2,
  // Store size not known in advance; can be calculated as strlen() of the
  // destination buffer.
  FSS_STRLEN = -1,
  // Invalid conversion specifier.
  FSS_INVALID = 0
};

// Returns the memory size of a format directive (if >0), or a value of
// FormatStoreSize.
static int format_get_value_size(char convSpecifier,
                                 const char lengthModifier[2],
                                 bool promote_float) {
  if (format_is_integer_conv(convSpecifier)) {
    switch (lengthModifier[0]) {
    case 'h':
      return lengthModifier[1] == 'h' ? sizeof(char) : sizeof(short);
    case 'l':
      return lengthModifier[1] == 'l' ? sizeof(long long) : sizeof(long);
    case 'q':
      return sizeof(long long);
    case 'L':
      return sizeof(long long);
    case 'j':
      return sizeof(INTMAX_T);
    case 'z':
      return sizeof(SIZE_T);
    case 't':
      return sizeof(PTRDIFF_T);
    case 0:
      return sizeof(int);
    default:
      return FSS_INVALID;
    }
  }

  if (format_is_float_conv(convSpecifier)) {
    switch (lengthModifier[0]) {
    case 'L':
    case 'q':
      return sizeof(long double);
    case 'l':
      return lengthModifier[1] == 'l' ? sizeof(long double)
                                           : sizeof(double);
    case 0:
      // Printf promotes floats to doubles but scanf does not
      return promote_float ? sizeof(double) : sizeof(float);
    default:
      return FSS_INVALID;
    }
  }

  if (convSpecifier == 'p') {
    if (lengthModifier[0] != 0)
      return FSS_INVALID;
    return sizeof(void *);
  }

  return FSS_INVALID;
}

struct ScanfDirective {
  int argIdx; // argument index, or -1 if not specified ("%n$")
  int fieldWidth;
  const char *begin;
  const char *end;
  bool suppressed; // suppress assignment ("*")
  bool allocate;   // allocate space ("m")
  char lengthModifier[2];
  char convSpecifier;
  bool maybeGnuMalloc;
};

// Parse scanf format string. If a valid directive in encountered, it is
// returned in dir. This function returns the pointer to the first
// unprocessed character, or 0 in case of error.
// In case of the end-of-string, a pointer to the closing \0 is returned.
static const char *scanf_parse_next(const char *p, bool allowGnuMalloc,
                                    ScanfDirective *dir) {
  internal_memset(dir, 0, sizeof(*dir));
  dir->argIdx = -1;

  while (*p) {
    if (*p != '%') {
      ++p;
      continue;
    }
    dir->begin = p;
    ++p;
    // %%
    if (*p == '%') {
      ++p;
      continue;
    }
    if (*p == '\0') {
      return nullptr;
    }
    // %n$
    p = maybe_parse_param_index(p, &dir->argIdx);
    CHECK(p);
    // *
    if (*p == '*') {
      dir->suppressed = true;
      ++p;
    }
    // Field width
    if (*p >= '0' && *p <= '9') {
      p = parse_number(p, &dir->fieldWidth);
      CHECK(p);
      if (dir->fieldWidth <= 0)  // Width if at all must be non-zero
        return nullptr;
    }
    // m
    if (*p == 'm') {
      dir->allocate = true;
      ++p;
    }
    // Length modifier.
    p = maybe_parse_length_modifier(p, dir->lengthModifier);
    // Conversion specifier.
    dir->convSpecifier = *p++;
    // Consume %[...] expression.
    if (dir->convSpecifier == '[') {
      if (*p == '^')
        ++p;
      if (*p == ']')
        ++p;
      while (*p && *p != ']')
        ++p;
      if (*p == 0)
        return nullptr; // unexpected end of string
                        // Consume the closing ']'.
      ++p;
    }
    // This is unfortunately ambiguous between old GNU extension
    // of %as, %aS and %a[...] and newer POSIX %a followed by
    // letters s, S or [.
    if (allowGnuMalloc && dir->convSpecifier == 'a' &&
        !dir->lengthModifier[0]) {
      if (*p == 's' || *p == 'S') {
        dir->maybeGnuMalloc = true;
        ++p;
      } else if (*p == '[') {
        // Watch for %a[h-j%d], if % appears in the
        // [...] range, then we need to give up, we don't know
        // if scanf will parse it as POSIX %a [h-j %d ] or
        // GNU allocation of string with range dh-j plus %.
        const char *q = p + 1;
        if (*q == '^')
          ++q;
        if (*q == ']')
          ++q;
        while (*q && *q != ']' && *q != '%')
          ++q;
        if (*q == 0 || *q == '%')
          return nullptr;
        p = q + 1; // Consume the closing ']'.
        dir->maybeGnuMalloc = true;
      }
    }
    dir->end = p;
    break;
  }
  return p;
}

static int scanf_get_value_size(ScanfDirective *dir) {
  if (dir->allocate) {
    if (!char_is_one_of(dir->convSpecifier, "cCsS["))
      return FSS_INVALID;
    return sizeof(char *);
  }

  if (dir->maybeGnuMalloc) {
    if (dir->convSpecifier != 'a' || dir->lengthModifier[0])
      return FSS_INVALID;
    // This is ambiguous, so check the smaller size of char * (if it is
    // a GNU extension of %as, %aS or %a[...]) and float (if it is
    // POSIX %a followed by s, S or [ letters).
    return sizeof(char *) < sizeof(float) ? sizeof(char *) : sizeof(float);
  }

  if (char_is_one_of(dir->convSpecifier, "cCsS[")) {
    bool needsTerminator = char_is_one_of(dir->convSpecifier, "sS[");
    unsigned charSize =
        format_get_char_size(dir->convSpecifier, dir->lengthModifier);
    if (charSize == 0)
      return FSS_INVALID;
    if (dir->fieldWidth == 0) {
      if (!needsTerminator)
        return charSize;
      return (charSize == sizeof(char)) ? FSS_STRLEN : FSS_WCSLEN;
    }
    return (dir->fieldWidth + needsTerminator) * charSize;
  }

  return format_get_value_size(dir->convSpecifier, dir->lengthModifier, false);
}

// Common part of *scanf interceptors.
// Process format string and va_list, and report all store ranges.
// Stops when "consuming" n_inputs input items.
static void scanf_common(void *ctx, int n_inputs, bool allowGnuMalloc,
                         const char *format, va_list aq) {
  CHECK_GT(n_inputs, 0);
  const char *p = format;

  COMMON_INTERCEPTOR_READ_RANGE(ctx, format, internal_strlen(format) + 1);

  while (*p) {
    ScanfDirective dir;
    p = scanf_parse_next(p, allowGnuMalloc, &dir);
    if (!p)
      break;
    if (dir.convSpecifier == 0) {
      // This can only happen at the end of the format string.
      CHECK_EQ(*p, 0);
      break;
    }
    // Here the directive is valid. Do what it says.
    if (dir.argIdx != -1) {
      // Unsupported.
      break;
    }
    if (dir.suppressed)
      continue;
    int size = scanf_get_value_size(&dir);
    if (size == FSS_INVALID) {
      Report("%s: WARNING: unexpected format specifier in scanf interceptor: %.*s\n",
             SanitizerToolName, static_cast<int>(dir.end - dir.begin), dir.begin);
      break;
    }
    void *argp = va_arg(aq, void *);
    if (dir.convSpecifier != 'n')
      --n_inputs;
    if (n_inputs < 0)
      break;
    if (size == FSS_STRLEN) {
      size = internal_strlen((const char *)argp) + 1;
    } else if (size == FSS_WCSLEN) {
      // FIXME: actually use wcslen() to calculate it.
      size = 0;
    }
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, argp, size);
    // For %mc/%mC/%ms/%m[/%mS, write the allocated output buffer as well.
    if (dir.allocate) {
      if (char *buf = *(char **)argp) {
        if (dir.convSpecifier == 'c')
          size = 1;
        else if (dir.convSpecifier == 'C')
          size = sizeof(wchar_t);
        else if (dir.convSpecifier == 'S')
          size = (internal_wcslen((wchar_t *)buf) + 1) * sizeof(wchar_t);
        else  // 's' or '['
          size = internal_strlen(buf) + 1;
        COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, size);
      }
    }
  }
}

#if SANITIZER_INTERCEPT_PRINTF

struct PrintfDirective {
  int fieldWidth;
  int fieldPrecision;
  int argIdx; // width argument index, or -1 if not specified ("%*n$")
  int precisionIdx; // precision argument index, or -1 if not specified (".*n$")
  const char *begin;
  const char *end;
  bool starredWidth;
  bool starredPrecision;
  char lengthModifier[2];
  char convSpecifier;
};

static const char *maybe_parse_number(const char *p, int *out) {
  if (*p >= '0' && *p <= '9')
    p = parse_number(p, out);
  return p;
}

static const char *maybe_parse_number_or_star(const char *p, int *out,
                                              bool *star) {
  if (*p == '*') {
    *star = true;
    ++p;
  } else {
    *star = false;
    p = maybe_parse_number(p, out);
  }
  return p;
}

// Parse printf format string. Same as scanf_parse_next.
static const char *printf_parse_next(const char *p, PrintfDirective *dir) {
  internal_memset(dir, 0, sizeof(*dir));
  dir->argIdx = -1;
  dir->precisionIdx = -1;

  while (*p) {
    if (*p != '%') {
      ++p;
      continue;
    }
    dir->begin = p;
    ++p;
    // %%
    if (*p == '%') {
      ++p;
      continue;
    }
    if (*p == '\0') {
      return nullptr;
    }
    // %n$
    p = maybe_parse_param_index(p, &dir->precisionIdx);
    CHECK(p);
    // Flags
    while (char_is_one_of(*p, "'-+ #0")) {
      ++p;
    }
    // Field width
    p = maybe_parse_number_or_star(p, &dir->fieldWidth,
                                   &dir->starredWidth);
    if (!p)
      return nullptr;
    // Precision
    if (*p == '.') {
      ++p;
      // Actual precision is optional (surprise!)
      p = maybe_parse_number_or_star(p, &dir->fieldPrecision,
                                     &dir->starredPrecision);
      if (!p)
        return nullptr;
      // m$
      if (dir->starredPrecision) {
        p = maybe_parse_param_index(p, &dir->precisionIdx);
        CHECK(p);
      }
    }
    // Length modifier.
    p = maybe_parse_length_modifier(p, dir->lengthModifier);
    // Conversion specifier.
    dir->convSpecifier = *p++;
    dir->end = p;
    break;
  }
  return p;
}

static int printf_get_value_size(PrintfDirective *dir) {
  if (char_is_one_of(dir->convSpecifier, "cCsS")) {
    unsigned charSize =
        format_get_char_size(dir->convSpecifier, dir->lengthModifier);
    if (charSize == 0)
      return FSS_INVALID;
    if (char_is_one_of(dir->convSpecifier, "sS")) {
      return (charSize == sizeof(char)) ? FSS_STRLEN : FSS_WCSLEN;
    }
    return charSize;
  }

  return format_get_value_size(dir->convSpecifier, dir->lengthModifier, true);
}

#define SKIP_SCALAR_ARG(aq, convSpecifier, size)                   \
  do {                                                             \
    if (format_is_float_conv(convSpecifier)) {                     \
      switch (size) {                                              \
      case 8:                                                      \
        va_arg(*aq, double);                                       \
        break;                                                     \
      case 12:                                                     \
        va_arg(*aq, long double);                                  \
        break;                                                     \
      case 16:                                                     \
        va_arg(*aq, long double);                                  \
        break;                                                     \
      default:                                                     \
        Report("WARNING: unexpected floating-point arg size"       \
               " in printf interceptor: %zu\n", static_cast<uptr>(size));             \
        return;                                                    \
      }                                                            \
    } else {                                                       \
      switch (size) {                                              \
      case 1:                                                      \
      case 2:                                                      \
      case 4:                                                      \
        va_arg(*aq, u32);                                          \
        break;                                                     \
      case 8:                                                      \
        va_arg(*aq, u64);                                          \
        break;                                                     \
      default:                                                     \
        Report("WARNING: unexpected arg size"                      \
               " in printf interceptor: %zu\n", static_cast<uptr>(size));             \
        return;                                                    \
      }                                                            \
    }                                                              \
  } while (0)

// Common part of *printf interceptors.
// Process format string and va_list, and report all load ranges.
static void printf_common(void *ctx, const char *format, va_list aq) {
  COMMON_INTERCEPTOR_READ_RANGE(ctx, format, internal_strlen(format) + 1);

  const char *p = format;

  while (*p) {
    PrintfDirective dir;
    p = printf_parse_next(p, &dir);
    if (!p)
      break;
    if (dir.convSpecifier == 0) {
      // This can only happen at the end of the format string.
      CHECK_EQ(*p, 0);
      break;
    }
    // Here the directive is valid. Do what it says.
    if (dir.argIdx != -1 || dir.precisionIdx != -1) {
      // Unsupported.
      break;
    }
    if (dir.starredWidth) {
      // Dynamic width
      SKIP_SCALAR_ARG(&aq, 'd', sizeof(int));
    }
    if (dir.starredPrecision) {
      // Dynamic precision
      SKIP_SCALAR_ARG(&aq, 'd', sizeof(int));
    }
    // %m does not require an argument: strlen(errno).
    if (dir.convSpecifier == 'm')
      continue;
    int size = printf_get_value_size(&dir);
    if (size == FSS_INVALID) {
      static int ReportedOnce;
      if (!ReportedOnce++)
        Report(
            "%s: WARNING: unexpected format specifier in printf "
            "interceptor: %.*s (reported once per process)\n",
            SanitizerToolName, static_cast<int>(dir.end - dir.begin), dir.begin);
      break;
    }
    if (dir.convSpecifier == 'n') {
      void *argp = va_arg(aq, void *);
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, argp, size);
      continue;
    } else if (size == FSS_STRLEN) {
      if (void *argp = va_arg(aq, void *)) {
        uptr len;
        if (dir.starredPrecision) {
          // FIXME: properly support starred precision for strings.
          len = 0;
        } else if (dir.fieldPrecision > 0) {
          // Won't read more than "precision" symbols.
          len = internal_strnlen((const char *)argp, dir.fieldPrecision);
          if (len < (uptr)dir.fieldPrecision)
            len++;
        } else {
          // Whole string will be accessed.
          len = internal_strlen((const char *)argp) + 1;
        }
        COMMON_INTERCEPTOR_READ_RANGE(ctx, argp, len);
      }
    } else if (size == FSS_WCSLEN) {
      if (void *argp = va_arg(aq, void *)) {
        // FIXME: Properly support wide-character strings (via wcsrtombs).
        COMMON_INTERCEPTOR_READ_RANGE(ctx, argp, 0);
      }
    } else {
      // Skip non-pointer args
      SKIP_SCALAR_ARG(&aq, dir.convSpecifier, size);
    }
  }
}

#endif // SANITIZER_INTERCEPT_PRINTF
