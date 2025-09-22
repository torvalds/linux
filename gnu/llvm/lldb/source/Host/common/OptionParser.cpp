//===-- source/Host/common/OptionParser.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/OptionParser.h"
#include "lldb/Host/HostGetOpt.h"
#include "lldb/Utility/OptionDefinition.h"
#include "lldb/lldb-private-types.h"

#include <vector>

using namespace lldb_private;

void OptionParser::Prepare(std::unique_lock<std::mutex> &lock) {
  static std::mutex g_mutex;
  lock = std::unique_lock<std::mutex>(g_mutex);
#ifdef __GLIBC__
  optind = 0;
#else
  optreset = 1;
  optind = 1;
#endif
}

void OptionParser::EnableError(bool error) { opterr = error ? 1 : 0; }

int OptionParser::Parse(llvm::MutableArrayRef<char *> argv,
                        llvm::StringRef optstring, const Option *longopts,
                        int *longindex) {
  std::vector<option> opts;
  while (longopts->definition != nullptr) {
    option opt;
    opt.flag = longopts->flag;
    opt.val = longopts->val;
    opt.name = longopts->definition->long_option;
    opt.has_arg = longopts->definition->option_has_arg;
    opts.push_back(opt);
    ++longopts;
  }
  opts.push_back(option());
  std::string opt_cstr = std::string(optstring);
  return getopt_long_only(argv.size() - 1, argv.data(), opt_cstr.c_str(),
                          &opts[0], longindex);
}

char *OptionParser::GetOptionArgument() { return optarg; }

int OptionParser::GetOptionIndex() { return optind; }

int OptionParser::GetOptionErrorCause() { return optopt; }

std::string OptionParser::GetShortOptionString(struct option *long_options) {
  std::string s;
  int i = 0;
  bool done = false;
  while (!done) {
    if (long_options[i].name == nullptr && long_options[i].has_arg == 0 &&
        long_options[i].flag == nullptr && long_options[i].val == 0) {
      done = true;
    } else {
      if (long_options[i].flag == nullptr && isalpha(long_options[i].val)) {
        s.append(1, (char)long_options[i].val);
        switch (long_options[i].has_arg) {
        default:
        case no_argument:
          break;

        case optional_argument:
          s.append(2, ':');
          break;
        case required_argument:
          s.append(1, ':');
          break;
        }
      }
      ++i;
    }
  }
  return s;
}
