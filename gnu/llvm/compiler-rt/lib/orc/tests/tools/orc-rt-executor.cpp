//===- orc-rt-executor.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains the orc-rt-executor test tool. This is a "blank executable" that
// links the ORC runtime and can accept code from a JIT controller like lii or
// llvm-jitlink.
//
//===----------------------------------------------------------------------===//

#include <cstring>
#include <iostream>
#include <optional>
#include <string_view>

void printHelp(std::string_view ProgName, std::ostream &OS) {
  OS << "usage: " << ProgName << " [help] [<mode>] <program arguments>...\n"
     << "  <mode>                 -- specify how to listen for JIT'd program\n"
     << "    filedesc=<in>,<out>  -- read from <in> filedesc, write to out\n"
     << "    tcp=<host>:<port>    -- listen on the given host/port\n"
     << "  help                   -- print help and exit\n"
     << "\n"
     << " Notes:\n"
     << "   Program arguments will be made available to the JIT controller.\n"
     << "   When running a JIT'd program containing a main function the\n"
     << "   controller may choose to pass these on to main, however\n"
     << "   orc-rt-executor does not enforce this.\n";
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printHelp("orc-rt-executor", std::cerr);
    std::cerr << "error: insufficient arguments.\n";
    exit(1);
  }

  if (!strcmp(argv[1], "help")) {
    printHelp(argv[0], std::cerr);
    exit(0);
  }

  std::cerr << "error: One day I will be a real program, but I am not yet.\n";

  return 0;
}
