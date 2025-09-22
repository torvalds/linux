//===---------------------AnsiTerminal.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_ANSITERMINAL_H
#define LLDB_UTILITY_ANSITERMINAL_H

#define ANSI_FG_COLOR_BLACK 30
#define ANSI_FG_COLOR_RED 31
#define ANSI_FG_COLOR_GREEN 32
#define ANSI_FG_COLOR_YELLOW 33
#define ANSI_FG_COLOR_BLUE 34
#define ANSI_FG_COLOR_PURPLE 35
#define ANSI_FG_COLOR_CYAN 36
#define ANSI_FG_COLOR_WHITE 37

#define ANSI_FG_COLOR_BRIGHT_BLACK 90
#define ANSI_FG_COLOR_BRIGHT_RED 91
#define ANSI_FG_COLOR_BRIGHT_GREEN 92
#define ANSI_FG_COLOR_BRIGHT_YELLOW 93
#define ANSI_FG_COLOR_BRIGHT_BLUE 94
#define ANSI_FG_COLOR_BRIGHT_PURPLE 95
#define ANSI_FG_COLOR_BRIGHT_CYAN 96
#define ANSI_FG_COLOR_BRIGHT_WHITE 97

#define ANSI_BG_COLOR_BLACK 40
#define ANSI_BG_COLOR_RED 41
#define ANSI_BG_COLOR_GREEN 42
#define ANSI_BG_COLOR_YELLOW 43
#define ANSI_BG_COLOR_BLUE 44
#define ANSI_BG_COLOR_PURPLE 45
#define ANSI_BG_COLOR_CYAN 46
#define ANSI_BG_COLOR_WHITE 47

#define ANSI_BG_COLOR_BRIGHT_BLACK 100
#define ANSI_BG_COLOR_BRIGHT_RED 101
#define ANSI_BG_COLOR_BRIGHT_GREEN 102
#define ANSI_BG_COLOR_BRIGHT_YELLOW 103
#define ANSI_BG_COLOR_BRIGHT_BLUE 104
#define ANSI_BG_COLOR_BRIGHT_PURPLE 105
#define ANSI_BG_COLOR_BRIGHT_CYAN 106
#define ANSI_BG_COLOR_BRIGHT_WHITE 107

#define ANSI_SPECIAL_FRAMED 51
#define ANSI_SPECIAL_ENCIRCLED 52

#define ANSI_CTRL_NORMAL 0
#define ANSI_CTRL_BOLD 1
#define ANSI_CTRL_FAINT 2
#define ANSI_CTRL_ITALIC 3
#define ANSI_CTRL_UNDERLINE 4
#define ANSI_CTRL_SLOW_BLINK 5
#define ANSI_CTRL_FAST_BLINK 6
#define ANSI_CTRL_IMAGE_NEGATIVE 7
#define ANSI_CTRL_CONCEAL 8
#define ANSI_CTRL_CROSSED_OUT 9

#define ANSI_ESC_START "\033["
#define ANSI_ESC_END "m"

#define ANSI_STR(s) #s
#define ANSI_DEF_STR(s) ANSI_STR(s)

#define ANSI_ESCAPE1(s) ANSI_ESC_START ANSI_DEF_STR(s) ANSI_ESC_END

#define ANSI_1_CTRL(ctrl1) "\033["##ctrl1 ANSI_ESC_END
#define ANSI_2_CTRL(ctrl1, ctrl2) "\033["##ctrl1 ";"##ctrl2 ANSI_ESC_END

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"

#include <string>

namespace lldb_private {

namespace ansi {

inline std::string FormatAnsiTerminalCodes(llvm::StringRef format,
                                           bool do_color = true) {
  // Convert "${ansi.XXX}" tokens to ansi values or clear them if do_color is
  // false.
  // clang-format off
  static const struct {
    const char *name;
    const char *value;
  } g_color_tokens[] = {
#define _TO_STR2(_val) #_val
#define _TO_STR(_val) _TO_STR2(_val)
      {"fg.black}",         ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_BLACK) ANSI_ESC_END},
      {"fg.red}",           ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_RED) ANSI_ESC_END},
      {"fg.green}",         ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_GREEN) ANSI_ESC_END},
      {"fg.yellow}",        ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_YELLOW) ANSI_ESC_END},
      {"fg.blue}",          ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_BLUE) ANSI_ESC_END},
      {"fg.purple}",        ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_PURPLE) ANSI_ESC_END},
      {"fg.cyan}",          ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_CYAN) ANSI_ESC_END},
      {"fg.white}",         ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_WHITE) ANSI_ESC_END},
      {"fg.bright.black}",  ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_BRIGHT_BLACK) ANSI_ESC_END},
      {"fg.bright.red}",    ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_BRIGHT_RED) ANSI_ESC_END},
      {"fg.bright.green}",  ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_BRIGHT_GREEN) ANSI_ESC_END},
      {"fg.bright.yellow}", ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_BRIGHT_YELLOW) ANSI_ESC_END},
      {"fg.bright.blue}",   ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_BRIGHT_BLUE) ANSI_ESC_END},
      {"fg.bright.purple}", ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_BRIGHT_PURPLE) ANSI_ESC_END},
      {"fg.bright.cyan}",   ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_BRIGHT_CYAN) ANSI_ESC_END},
      {"fg.bright.white}",  ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_BRIGHT_WHITE) ANSI_ESC_END},
      {"bg.black}",         ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_BLACK) ANSI_ESC_END},
      {"bg.red}",           ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_RED) ANSI_ESC_END},
      {"bg.green}",         ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_GREEN) ANSI_ESC_END},
      {"bg.yellow}",        ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_YELLOW) ANSI_ESC_END},
      {"bg.blue}",          ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_BLUE) ANSI_ESC_END},
      {"bg.purple}",        ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_PURPLE) ANSI_ESC_END},
      {"bg.cyan}",          ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_CYAN) ANSI_ESC_END},
      {"bg.white}",         ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_WHITE) ANSI_ESC_END},
      {"bg.bright.black}",  ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_BRIGHT_BLACK) ANSI_ESC_END},
      {"bg.bright.red}",    ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_BRIGHT_RED) ANSI_ESC_END},
      {"bg.bright.green}",  ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_BRIGHT_GREEN) ANSI_ESC_END},
      {"bg.bright.yellow}", ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_BRIGHT_YELLOW) ANSI_ESC_END},
      {"bg.bright.blue}",   ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_BRIGHT_BLUE) ANSI_ESC_END},
      {"bg.bright.purple}", ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_BRIGHT_PURPLE) ANSI_ESC_END},
      {"bg.bright.cyan}",   ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_BRIGHT_CYAN) ANSI_ESC_END},
      {"bg.bright.white}",  ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_BRIGHT_WHITE) ANSI_ESC_END},
      {"normal}",           ANSI_ESC_START _TO_STR(ANSI_CTRL_NORMAL) ANSI_ESC_END},
      {"bold}",             ANSI_ESC_START _TO_STR(ANSI_CTRL_BOLD) ANSI_ESC_END},
      {"faint}",            ANSI_ESC_START _TO_STR(ANSI_CTRL_FAINT) ANSI_ESC_END},
      {"italic}",           ANSI_ESC_START _TO_STR(ANSI_CTRL_ITALIC) ANSI_ESC_END},
      {"underline}",        ANSI_ESC_START _TO_STR(ANSI_CTRL_UNDERLINE) ANSI_ESC_END},
      {"slow-blink}",       ANSI_ESC_START _TO_STR(ANSI_CTRL_SLOW_BLINK) ANSI_ESC_END},
      {"fast-blink}",       ANSI_ESC_START _TO_STR(ANSI_CTRL_FAST_BLINK) ANSI_ESC_END},
      {"negative}",         ANSI_ESC_START _TO_STR(ANSI_CTRL_IMAGE_NEGATIVE) ANSI_ESC_END},
      {"conceal}",          ANSI_ESC_START _TO_STR(ANSI_CTRL_CONCEAL) ANSI_ESC_END},
      {"crossed-out}",      ANSI_ESC_START _TO_STR(ANSI_CTRL_CROSSED_OUT) ANSI_ESC_END},
#undef _TO_STR
#undef _TO_STR2
  };
  // clang-format on
  auto codes = llvm::ArrayRef(g_color_tokens);

  static const char tok_hdr[] = "${ansi.";

  std::string fmt;
  while (!format.empty()) {
    llvm::StringRef left, right;
    std::tie(left, right) = format.split(tok_hdr);

    fmt += left;

    if (left == format && right.empty()) {
      // The header was not found.  Just exit.
      break;
    }

    bool found_code = false;
    for (const auto &code : codes) {
      if (!right.consume_front(code.name))
        continue;

      if (do_color)
        fmt.append(code.value);
      found_code = true;
      break;
    }
    format = right;
    // If we haven't found a valid replacement value, we just copy the string
    // to the result without any modifications.
    if (!found_code)
      fmt.append(tok_hdr);
  }
  return fmt;
}
}
} // namespace lldb_private

#endif
