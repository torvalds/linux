#!/usr/bin/awk -f

#===-- generate_netbsd_syscalls.awk ----------------------------------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===------------------------------------------------------------------------===#
#
# This file is a generator of:
#  - include/sanitizer/netbsd_syscall_hooks.h
#  - lib/sanitizer_common/sanitizer_syscalls_netbsd.inc
#
# This script accepts on the input syscalls.master by default located in the
# /usr/src/sys/kern/syscalls.master path in the NetBSD distribution.
#
# This script shall be executed only on the newest NetBSD version.
# This script will emit compat code for the older releases.
#
# NetBSD minimal version supported 9.0.
# NetBSD current version supported 9.99.30.
#
#===------------------------------------------------------------------------===#

BEGIN {
  # hardcode the script name
  script_name = "generate_netbsd_syscalls.awk"
  outputh = "../include/sanitizer/netbsd_syscall_hooks.h"
  outputinc = "../lib/sanitizer_common/sanitizer_syscalls_netbsd.inc"

  # assert that we are in the directory with scripts
  in_utils = system("test -f " script_name " && exit 1 || exit 0")
  if (in_utils == 0) {
    usage()
  }

  # assert 1 argument passed
  if (ARGC != 2) {
    usage()
  }

  # assert argument is a valid file path to syscall.master
  if (system("test -f " ARGV[1]) != 0) {
    usage()
  }

  # sanity check that the path ends with "syscall.master"
  if (ARGV[1] !~ /syscalls\.master$/) {
    usage()
  }

  # accept overloading CLANGFORMAT from environment
  clangformat = "clang-format"
  if ("CLANGFORMAT" in ENVIRON) {
    clangformat = ENVIRON["CLANGFORMAT"]
  }

  # parsing specific symbols
  parsingheader=1

  parsedsyscalls=0

  # Hardcoded in algorithm
  SYS_MAXSYSARGS=8
}

# Parse the RCS ID from syscall.master
parsingheader == 1 && NR == 1 {
  if (match($0, /\$[^$]+\$/)) {
    # trim initial 'NetBSD: ' and trailing ' $'
    syscallmasterversion = substr($0, RSTART + 9, RLENGTH - 11)
  } else {
    # wrong file?
    usage()
  }
}

# skip the following lines
#  - empty
NF == 0 {
  next
}
#  - comment
$1 == ";" {
  next
}

# separator between the header and table with syscalls
$0 == "%%" {
  parsingheader = 0
  next
}

# preserve 'if/elif/else/endif' C preprocessor as-is
parsingheader == 0 && $0 ~ /^#/ {
  if (parsedsyscalls in ifelifelseendif) {
    ifelifelseendif[parsedsyscalls] = ifelifelseendif[parsedsyscalls] "\n" $0
  } else {
    ifelifelseendif[parsedsyscalls] = $0
  }
  next
}

# parsing of syscall definitions
parsingheader == 0 && $1 ~ /^[0-9]+$/ {
  # first join multiple lines into single one
  while (sub(/\\$/, "")) {
    getline line
    $0 = $0 "" line
  }

  # Skip unwanted syscalls
  skip=0
  if ($0 ~ /OBSOL/ || $0 ~ /EXCL/ || $0 ~ /UNIMPL/) {
    skip=1
  }

  # Compose the syscall name
  #  - compat?
  compat=""
  if (match($0, /COMPAT_[0-9]+/)) {
    compat = tolower(substr($0, RSTART, RLENGTH))
  }
  # - alias name?
  alias=""
  if ($(NF) != "}" && !skip) {
    alias = alias "" $(NF)
  }
  # - compat version?
  compatver=""
  if (match($0, /\|[0-9]+\|/)) {
    compatver = tolower(substr($0, RSTART + 1, RLENGTH - 2))
  }
  # - basename?
  basename=""
  if (skip) {
    basename = $1
  } else {
    if (match($0, /\|[_a-z0-9]+\(/)) {
      basename = tolower(substr($0, RSTART + 1, RLENGTH - 2))
    }
  }

  syscallname=""

  if (skip) {
    syscallname= syscallname "$"
  }

  if (length(compat) > 0) {
    syscallname = syscallname "" compat "_"
  }
  if (length(alias) > 0) {
    syscallname = syscallname "" alias
  } else {
    if (length(compatver) > 0) {
      syscallname = syscallname "__" basename "" compatver
    } else {
      syscallname = syscallname "" basename
    }
  }

  # Store the syscallname
  syscalls[parsedsyscalls]=syscallname

  # Extract syscall arguments
  if (match($0, /\([^)]+\)/)) {
    args = substr($0, RSTART + 1, RLENGTH - 2)

    if (args == "void") {
      syscallargs[parsedsyscalls] = "void"
      syscallfullargs[parsedsyscalls] = "void"
    } else {
      # Normalize 'type * argument' to 'type *argument'
      gsub("\\*[ \t]+", "*", args)

      n = split(args, a, ",")

      # Handle the first argument
      match(a[1], /[*_a-z0-9\[\]]+$/)
      syscallfullargs[parsedsyscalls] = substr(a[1], RSTART) "_"

      gsub(".+[ *]", "", a[1])
      syscallargs[parsedsyscalls] = a[1]

      # Handle the rest of arguments
      for (i = 2; i <= n; i++) {
        match(a[i], /[*_a-zA-Z0-9\[\]]+$/)
        fs = substr(a[i], RSTART)
        if (fs ~ /\[/) {
          sub(/\[/, "_[", fs)
        } else {
          fs = fs "_"
        }
        syscallfullargs[parsedsyscalls] = syscallfullargs[parsedsyscalls] "$" fs
	gsub(".+[ *]", "", a[i])
        syscallargs[parsedsyscalls] = syscallargs[parsedsyscalls] "$" a[i]
      }

      # Handle array arguments for syscall(2) and __syscall(2)
      nargs = "arg0$arg1$arg2$arg3$arg4$arg5$arg6$arg7"
      gsub(/args\[SYS_MAXSYSARGS\]/, nargs, syscallargs[parsedsyscalls])
    }
  }

  parsedsyscalls++

  # Done with this line
  next
}


END {
  # empty file?
  if (NR < 1 && !abnormal_exit) {
    usage()
  }

  # Handle abnormal exit
  if (abnormal_exit) {
    exit(abnormal_exit)
  }

  # Generate sanitizer_syscalls_netbsd.inc

  # open pipe
  cmd = clangformat " > " outputh

  pcmd("//===-- netbsd_syscall_hooks.h --------------------------------------------===//")
  pcmd("//")
  pcmd("// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.")
  pcmd("// See https://llvm.org/LICENSE.txt for license information.")
  pcmd("// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception")
  pcmd("//")
  pcmd("//===----------------------------------------------------------------------===//")
  pcmd("//")
  pcmd("// This file is a part of public sanitizer interface.")
  pcmd("//")
  pcmd("// System call handlers.")
  pcmd("//")
  pcmd("// Interface methods declared in this header implement pre- and post- syscall")
  pcmd("// actions for the active sanitizer.")
  pcmd("// Usage:")
  pcmd("//   __sanitizer_syscall_pre_getfoo(...args...);")
  pcmd("//   long long res = syscall(SYS_getfoo, ...args...);")
  pcmd("//   __sanitizer_syscall_post_getfoo(res, ...args...);")
  pcmd("//")
  pcmd("// DO NOT EDIT! THIS FILE HAS BEEN GENERATED!")
  pcmd("//")
  pcmd("// Generated with: " script_name)
  pcmd("// Generated date: " strftime("%F"))
  pcmd("// Generated from: " syscallmasterversion)
  pcmd("//")
  pcmd("//===----------------------------------------------------------------------===//")
  pcmd("#ifndef SANITIZER_NETBSD_SYSCALL_HOOKS_H")
  pcmd("#define SANITIZER_NETBSD_SYSCALL_HOOKS_H")
  pcmd("")

  for (i = 0; i < parsedsyscalls; i++) {

    if (i in ifelifelseendif) {
      pcmd(ifelifelseendif[i])
    }

    sn = syscalls[i]

    if (sn ~ /^\$/) {
      pcmd("/* syscall " substr(sn,2) " has been skipped */")
      continue
    }

    inargs = ""

    if (syscallargs[i] != "void") {
      inargs = syscallargs[i]
      gsub(/\$/, ", ", inargs)
    }

    outargs = ""

    if (syscallargs[i] != "void") {
      outargs = "(long long)(" syscallargs[i] ")"
      gsub(/\$/, "), (long long)(", outargs)
    }

    pcmd("#define __sanitizer_syscall_pre_" sn "(" inargs ") \\")
    pcmd("  __sanitizer_syscall_pre_impl_" sn "(" outargs ")")

    if (inargs == "") {
      inargs = "res"
    } else {
      inargs = "res, " inargs
    }

    if (outargs == "") {
      outargs = "res"
    } else {
      outargs = "res, " outargs
    }

    pcmd("#define __sanitizer_syscall_post_" sn "(" inargs ") \\")
    pcmd("  __sanitizer_syscall_post_impl_" sn "(" outargs ")")
  }

  pcmd("")
  pcmd("/* Compat with older releases */")
  pcmd("#define __sanitizer_syscall_pre_getvfsstat __sanitizer_syscall_pre_compat_90_getvfsstat")
  pcmd("#define __sanitizer_syscall_post_getvfsstat __sanitizer_syscall_post_compat_90_getvfsstat")
  pcmd("")
  pcmd("#define __sanitizer_syscall_pre_statvfs1 __sanitizer_syscall_pre_compat_90_statvfs1")
  pcmd("#define __sanitizer_syscall_post_statvfs1 __sanitizer_syscall_post_compat_90_statvfs1")
  pcmd("")
  pcmd("#define __sanitizer_syscall_pre_fstatvfs1 __sanitizer_syscall_pre_compat_90_fstatvfs1")
  pcmd("#define __sanitizer_syscall_post_fstatvfs1 __sanitizer_syscall_post_compat_90_fstatvfs1")
  pcmd("")
  pcmd("#define __sanitizer_syscall_pre___fhstatvfs140 __sanitizer_syscall_pre_compat_90_fhstatvfs1")
  pcmd("#define __sanitizer_syscall_post___fhstatvfs140 __sanitizer_syscall_post_compat_90_fhstatvfs1")

  pcmd("")
  pcmd("#ifdef __cplusplus")
  pcmd("extern \"C\" {")
  pcmd("#endif")
  pcmd("")
  pcmd("// Private declarations. Do not call directly from user code. Use macros above.")
  pcmd("")
  pcmd("// DO NOT EDIT! THIS FILE HAS BEEN GENERATED!")
  pcmd("")

  for (i = 0; i < parsedsyscalls; i++) {

    if (i in ifelifelseendif) {
      pcmd(ifelifelseendif[i])
    }

    sn = syscalls[i]

    if (sn ~ /^\$/) {
      pcmd("/* syscall " substr(sn,2) " has been skipped */")
      continue
    }

    preargs = syscallargs[i]

    if (preargs != "void") {
      preargs = "long long " preargs
      gsub(/\$/, ", long long ", preargs)
    }

    if (preargs == "void") {
      postargs = "long long res"
    } else {
      postargs = "long long res, " preargs
    }

    pcmd("void __sanitizer_syscall_pre_impl_" sn "(" preargs ");")
    pcmd("void __sanitizer_syscall_post_impl_" sn "(" postargs ");")
  }

  pcmd("")
  pcmd("#ifdef __cplusplus")
  pcmd("} // extern \"C\"")
  pcmd("#endif")

  pcmd("")
  pcmd("// DO NOT EDIT! THIS FILE HAS BEEN GENERATED!")
  pcmd("")

  pcmd("#endif  // SANITIZER_NETBSD_SYSCALL_HOOKS_H")

  close(cmd)

  # Generate sanitizer_syscalls_netbsd.inc

  # open pipe
  cmd = clangformat " > " outputinc

  pcmd("//===-- sanitizer_syscalls_netbsd.inc ---------------------------*- C++ -*-===//")
  pcmd("//")
  pcmd("// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.")
  pcmd("// See https://llvm.org/LICENSE.txt for license information.")
  pcmd("// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception")
  pcmd("//")
  pcmd("//===----------------------------------------------------------------------===//")
  pcmd("//")
  pcmd("// Common syscalls handlers for tools like AddressSanitizer,")
  pcmd("// ThreadSanitizer, MemorySanitizer, etc.")
  pcmd("//")
  pcmd("// This file should be included into the tool's interceptor file,")
  pcmd("// which has to define it's own macros:")
  pcmd("//   COMMON_SYSCALL_PRE_READ_RANGE")
  pcmd("//          Called in prehook for regions that will be read by the kernel and")
  pcmd("//          must be initialized.")
  pcmd("//   COMMON_SYSCALL_PRE_WRITE_RANGE")
  pcmd("//          Called in prehook for regions that will be written to by the kernel")
  pcmd("//          and must be addressable. The actual write range may be smaller than")
  pcmd("//          reported in the prehook. See POST_WRITE_RANGE.")
  pcmd("//   COMMON_SYSCALL_POST_READ_RANGE")
  pcmd("//          Called in posthook for regions that were read by the kernel. Does")
  pcmd("//          not make much sense.")
  pcmd("//   COMMON_SYSCALL_POST_WRITE_RANGE")
  pcmd("//          Called in posthook for regions that were written to by the kernel")
  pcmd("//          and are now initialized.")
  pcmd("//   COMMON_SYSCALL_ACQUIRE(addr)")
  pcmd("//          Acquire memory visibility from addr.")
  pcmd("//   COMMON_SYSCALL_RELEASE(addr)")
  pcmd("//          Release memory visibility to addr.")
  pcmd("//   COMMON_SYSCALL_FD_CLOSE(fd)")
  pcmd("//          Called before closing file descriptor fd.")
  pcmd("//   COMMON_SYSCALL_FD_ACQUIRE(fd)")
  pcmd("//          Acquire memory visibility from fd.")
  pcmd("//   COMMON_SYSCALL_FD_RELEASE(fd)")
  pcmd("//          Release memory visibility to fd.")
  pcmd("//   COMMON_SYSCALL_PRE_FORK()")
  pcmd("//          Called before fork syscall.")
  pcmd("//   COMMON_SYSCALL_POST_FORK(long long res)")
  pcmd("//          Called after fork syscall.")
  pcmd("//")
  pcmd("// DO NOT EDIT! THIS FILE HAS BEEN GENERATED!")
  pcmd("//")
  pcmd("// Generated with: " script_name)
  pcmd("// Generated date: " strftime("%F"))
  pcmd("// Generated from: " syscallmasterversion)
  pcmd("//")
  pcmd("//===----------------------------------------------------------------------===//")
  pcmd("")
  pcmd("#include \"sanitizer_platform.h\"")
  pcmd("#if SANITIZER_NETBSD")
  pcmd("")
  pcmd("#include \"sanitizer_libc.h\"")
  pcmd("")
  pcmd("#define PRE_SYSCALL(name)                                                      \\")
  pcmd("  SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_syscall_pre_impl_##name")
  pcmd("#define PRE_READ(p, s) COMMON_SYSCALL_PRE_READ_RANGE(p, s)")
  pcmd("#define PRE_WRITE(p, s) COMMON_SYSCALL_PRE_WRITE_RANGE(p, s)")
  pcmd("")
  pcmd("#define POST_SYSCALL(name)                                                     \\")
  pcmd("  SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_syscall_post_impl_##name")
  pcmd("#define POST_READ(p, s) COMMON_SYSCALL_POST_READ_RANGE(p, s)")
  pcmd("#define POST_WRITE(p, s) COMMON_SYSCALL_POST_WRITE_RANGE(p, s)")
  pcmd("")
  pcmd("#ifndef COMMON_SYSCALL_ACQUIRE")
  pcmd("# define COMMON_SYSCALL_ACQUIRE(addr) ((void)(addr))")
  pcmd("#endif")
  pcmd("")
  pcmd("#ifndef COMMON_SYSCALL_RELEASE")
  pcmd("# define COMMON_SYSCALL_RELEASE(addr) ((void)(addr))")
  pcmd("#endif")
  pcmd("")
  pcmd("#ifndef COMMON_SYSCALL_FD_CLOSE")
  pcmd("# define COMMON_SYSCALL_FD_CLOSE(fd) ((void)(fd))")
  pcmd("#endif")
  pcmd("")
  pcmd("#ifndef COMMON_SYSCALL_FD_ACQUIRE")
  pcmd("# define COMMON_SYSCALL_FD_ACQUIRE(fd) ((void)(fd))")
  pcmd("#endif")
  pcmd("")
  pcmd("#ifndef COMMON_SYSCALL_FD_RELEASE")
  pcmd("# define COMMON_SYSCALL_FD_RELEASE(fd) ((void)(fd))")
  pcmd("#endif")
  pcmd("")
  pcmd("#ifndef COMMON_SYSCALL_PRE_FORK")
  pcmd("# define COMMON_SYSCALL_PRE_FORK() {}")
  pcmd("#endif")
  pcmd("")
  pcmd("#ifndef COMMON_SYSCALL_POST_FORK")
  pcmd("# define COMMON_SYSCALL_POST_FORK(res) {}")
  pcmd("#endif")
  pcmd("")
  pcmd("// FIXME: do some kind of PRE_READ for all syscall arguments (int(s) and such).")
  pcmd("")
  pcmd("extern \"C\" {")
  pcmd("#define SYS_MAXSYSARGS " SYS_MAXSYSARGS)

  for (i = 0; i < parsedsyscalls; i++) {

    if (i in ifelifelseendif) {
      pcmd(ifelifelseendif[i])
    }

    sn = syscalls[i]

    if (sn ~ /^\$/) {
      pcmd("/* syscall " substr(sn,2) " has been skipped */")
      continue
    }

    preargs = syscallfullargs[i]

    if (preargs != "void") {
      preargs = "long long " preargs
      gsub(/\$/, ", long long ", preargs)
      gsub(/long long \*/, "void *", preargs)
    }

    if (preargs == "void") {
      postargs = "long long res"
    } else {
      postargs = "long long res, " preargs
    }

    pcmd("PRE_SYSCALL(" sn ")(" preargs ")")
    pcmd("{")
    syscall_body(sn, "pre")
    pcmd("}")

    pcmd("POST_SYSCALL(" sn ")(" postargs ")")
    pcmd("{")
    syscall_body(sn, "post")
    pcmd("}")
  }

  pcmd("#undef SYS_MAXSYSARGS")
  pcmd("}  // extern \"C\"")
  pcmd("")
  pcmd("#undef PRE_SYSCALL")
  pcmd("#undef PRE_READ")
  pcmd("#undef PRE_WRITE")
  pcmd("#undef POST_SYSCALL")
  pcmd("#undef POST_READ")
  pcmd("#undef POST_WRITE")
  pcmd("")
  pcmd("#endif  // SANITIZER_NETBSD")

  close(cmd)

  # Hack for preprocessed code
  system("sed -i 's,^ \\([^ ]\\),  \\1,' " outputinc)
}

function usage()
{
  print "Usage: " script_name " syscalls.master"
  abnormal_exit = 1
  exit 1
}

function pcmd(string)
{
  print string | cmd
}

function syscall_body(syscall, mode)
{
  # Hardcode sanitizing rules here
  # These syscalls don't change often so they are hand coded
  if (syscall == "syscall") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "exit") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "fork") {
    if (mode == "pre") {
      pcmd("COMMON_SYSCALL_PRE_FORK();")
    } else {
      pcmd("COMMON_SYSCALL_POST_FORK(res);")
    }
  } else if (syscall == "read") {
    if (mode == "pre") {
      pcmd("if (buf_) {")
      pcmd("  PRE_WRITE(buf_, nbyte_);")
      pcmd("}")
    } else {
      pcmd("if (res > 0) {")
      pcmd("  POST_WRITE(buf_, res);")
      pcmd("}")
    }
  } else if (syscall == "write") {
    if (mode == "pre") {
      pcmd("if (buf_) {")
      pcmd("  PRE_READ(buf_, nbyte_);")
      pcmd("}")
    } else {
      pcmd("if (res > 0) {")
      pcmd("  POST_READ(buf_, res);")
      pcmd("}")
    }
  } else if (syscall == "open") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res > 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "close") {
    if (mode == "pre") {
      pcmd("COMMON_SYSCALL_FD_CLOSE((int)fd_);")
    } else {
      pcmd("/* Nothing to do */")
    }
  } else if (syscall == "compat_50_wait4") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_ocreat") {
    pcmd("/* TODO */")
  } else if (syscall == "link") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("const char *link = (const char *)link_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
      pcmd("if (link) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(link) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  const char *link = (const char *)link_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("  if (link) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(link) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "unlink") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "chdir") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "fchdir") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_50_mknod") {
    pcmd("/* TODO */")
  } else if (syscall == "chmod") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "chown") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "break") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_20_getfsstat") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_olseek") {
    pcmd("/* TODO */")
  } else if (syscall == "getpid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_40_mount") {
    pcmd("/* TODO */")
  } else if (syscall == "unmount") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "setuid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "getuid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "geteuid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "ptrace") {
    if (mode == "pre") {
      pcmd("if (req_ == ptrace_pt_io) {")
      pcmd("  struct __sanitizer_ptrace_io_desc *addr = (struct __sanitizer_ptrace_io_desc *)addr_;")
      pcmd("  PRE_READ(addr, struct_ptrace_ptrace_io_desc_struct_sz);")
      pcmd("  if (addr->piod_op == ptrace_piod_write_d || addr->piod_op == ptrace_piod_write_i) {")
      pcmd("    PRE_READ(addr->piod_addr, addr->piod_len);")
      pcmd("  }")
      pcmd("  if (addr->piod_op == ptrace_piod_read_d || addr->piod_op == ptrace_piod_read_i || addr->piod_op == ptrace_piod_read_auxv) {")
      pcmd("    PRE_WRITE(addr->piod_addr, addr->piod_len);")
      pcmd("  }")
      pcmd("} else if (req_ == ptrace_pt_lwpinfo) {")
      pcmd("  struct __sanitizer_ptrace_lwpinfo *addr = (struct __sanitizer_ptrace_lwpinfo *)addr_;")
      pcmd("  PRE_READ(&addr->pl_lwpid, sizeof(__sanitizer_lwpid_t));")
      pcmd("  PRE_WRITE(addr, struct_ptrace_ptrace_lwpinfo_struct_sz);")
      pcmd("} else if (req_ == ptrace_pt_set_event_mask) {")
      pcmd("  PRE_READ(addr_, struct_ptrace_ptrace_event_struct_sz);")
      pcmd("} else if (req_ == ptrace_pt_get_event_mask) {")
      pcmd("  PRE_WRITE(addr_, struct_ptrace_ptrace_event_struct_sz);")
      pcmd("} else if (req_ == ptrace_pt_set_siginfo) {")
      pcmd("  PRE_READ(addr_, struct_ptrace_ptrace_siginfo_struct_sz);")
      pcmd("} else if (req_ == ptrace_pt_get_siginfo) {")
      pcmd("  PRE_WRITE(addr_, struct_ptrace_ptrace_siginfo_struct_sz);")
      pcmd("} else if (req_ == ptrace_pt_lwpstatus) {")
      pcmd("  struct __sanitizer_ptrace_lwpstatus *addr = (struct __sanitizer_ptrace_lwpstatus *)addr_;")
      pcmd("  PRE_READ(&addr->pl_lwpid, sizeof(__sanitizer_lwpid_t));")
      pcmd("  PRE_WRITE(addr, struct_ptrace_ptrace_lwpstatus_struct_sz);")
      pcmd("} else if (req_ == ptrace_pt_lwpnext) {")
      pcmd("  struct __sanitizer_ptrace_lwpstatus *addr = (struct __sanitizer_ptrace_lwpstatus *)addr_;")
      pcmd("  PRE_READ(&addr->pl_lwpid, sizeof(__sanitizer_lwpid_t));")
      pcmd("  PRE_WRITE(addr, struct_ptrace_ptrace_lwpstatus_struct_sz);")
      pcmd("} else if (req_ == ptrace_pt_setregs) {")
      pcmd("  PRE_READ(addr_, struct_ptrace_reg_struct_sz);")
      pcmd("} else if (req_ == ptrace_pt_getregs) {")
      pcmd("  PRE_WRITE(addr_, struct_ptrace_reg_struct_sz);")
      pcmd("} else if (req_ == ptrace_pt_setfpregs) {")
      pcmd("  PRE_READ(addr_, struct_ptrace_fpreg_struct_sz);")
      pcmd("} else if (req_ == ptrace_pt_getfpregs) {")
      pcmd("  PRE_WRITE(addr_, struct_ptrace_fpreg_struct_sz);")
      pcmd("} else if (req_ == ptrace_pt_setdbregs) {")
      pcmd("  PRE_READ(addr_, struct_ptrace_dbreg_struct_sz);")
      pcmd("} else if (req_ == ptrace_pt_getdbregs) {")
      pcmd("  PRE_WRITE(addr_, struct_ptrace_dbreg_struct_sz);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  if (req_ == ptrace_pt_io) {")
      pcmd("    struct __sanitizer_ptrace_io_desc *addr = (struct __sanitizer_ptrace_io_desc *)addr_;")
      pcmd("    POST_READ(addr, struct_ptrace_ptrace_io_desc_struct_sz);")
      pcmd("    if (addr->piod_op == ptrace_piod_write_d || addr->piod_op == ptrace_piod_write_i) {")
      pcmd("      POST_READ(addr->piod_addr, addr->piod_len);")
      pcmd("    }")
      pcmd("    if (addr->piod_op == ptrace_piod_read_d || addr->piod_op == ptrace_piod_read_i || addr->piod_op == ptrace_piod_read_auxv) {")
      pcmd("      POST_WRITE(addr->piod_addr, addr->piod_len);")
      pcmd("    }")
      pcmd("  } else if (req_ == ptrace_pt_lwpinfo) {")
      pcmd("    struct __sanitizer_ptrace_lwpinfo *addr = (struct __sanitizer_ptrace_lwpinfo *)addr_;")
      pcmd("    POST_READ(&addr->pl_lwpid, sizeof(__sanitizer_lwpid_t));")
      pcmd("    POST_WRITE(addr, struct_ptrace_ptrace_lwpinfo_struct_sz);")
      pcmd("  } else if (req_ == ptrace_pt_set_event_mask) {")
      pcmd("    POST_READ(addr_, struct_ptrace_ptrace_event_struct_sz);")
      pcmd("  } else if (req_ == ptrace_pt_get_event_mask) {")
      pcmd("    POST_WRITE(addr_, struct_ptrace_ptrace_event_struct_sz);")
      pcmd("  } else if (req_ == ptrace_pt_set_siginfo) {")
      pcmd("    POST_READ(addr_, struct_ptrace_ptrace_siginfo_struct_sz);")
      pcmd("  } else if (req_ == ptrace_pt_get_siginfo) {")
      pcmd("    POST_WRITE(addr_, struct_ptrace_ptrace_siginfo_struct_sz);")
      pcmd("  } else if (req_ == ptrace_pt_lwpstatus) {")
      pcmd("    struct __sanitizer_ptrace_lwpstatus *addr = (struct __sanitizer_ptrace_lwpstatus *)addr_;")
      pcmd("    POST_READ(&addr->pl_lwpid, sizeof(__sanitizer_lwpid_t));")
      pcmd("    POST_WRITE(addr, struct_ptrace_ptrace_lwpstatus_struct_sz);")
      pcmd("  } else if (req_ == ptrace_pt_lwpnext) {")
      pcmd("    struct __sanitizer_ptrace_lwpstatus *addr = (struct __sanitizer_ptrace_lwpstatus *)addr_;")
      pcmd("    POST_READ(&addr->pl_lwpid, sizeof(__sanitizer_lwpid_t));")
      pcmd("    POST_WRITE(addr, struct_ptrace_ptrace_lwpstatus_struct_sz);")
      pcmd("  } else if (req_ == ptrace_pt_setregs) {")
      pcmd("    POST_READ(addr_, struct_ptrace_reg_struct_sz);")
      pcmd("  } else if (req_ == ptrace_pt_getregs) {")
      pcmd("    POST_WRITE(addr_, struct_ptrace_reg_struct_sz);")
      pcmd("  } else if (req_ == ptrace_pt_setfpregs) {")
      pcmd("    POST_READ(addr_, struct_ptrace_fpreg_struct_sz);")
      pcmd("  } else if (req_ == ptrace_pt_getfpregs) {")
      pcmd("    POST_WRITE(addr_, struct_ptrace_fpreg_struct_sz);")
      pcmd("  } else if (req_ == ptrace_pt_setdbregs) {")
      pcmd("    POST_READ(addr_, struct_ptrace_dbreg_struct_sz);")
      pcmd("  } else if (req_ == ptrace_pt_getdbregs) {")
      pcmd("    POST_WRITE(addr_, struct_ptrace_dbreg_struct_sz);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "recvmsg") {
    if (mode == "pre") {
      pcmd("PRE_WRITE(msg_, sizeof(__sanitizer_msghdr));")
    } else {
      pcmd("if (res > 0) {")
      pcmd("  POST_WRITE(msg_, sizeof(__sanitizer_msghdr));")
      pcmd("}")
    }
  } else if (syscall == "sendmsg") {
    if (mode == "pre") {
      pcmd("PRE_READ(msg_, sizeof(__sanitizer_msghdr));")
    } else {
      pcmd("if (res > 0) {")
      pcmd("  POST_READ(msg_, sizeof(__sanitizer_msghdr));")
      pcmd("}")
    }
  } else if (syscall == "recvfrom") {
    if (mode == "pre") {
      pcmd("PRE_WRITE(buf_, len_);")
      pcmd("PRE_WRITE(from_, struct_sockaddr_sz);")
      pcmd("PRE_WRITE(fromlenaddr_, sizeof(__sanitizer_socklen_t));")
    } else {
      pcmd("if (res >= 0) {")
      pcmd("  POST_WRITE(buf_, res);")
      pcmd("  POST_WRITE(from_, struct_sockaddr_sz);")
      pcmd("  POST_WRITE(fromlenaddr_, sizeof(__sanitizer_socklen_t));")
      pcmd("}")
    }
  } else if (syscall == "accept") {
    if (mode == "pre") {
      pcmd("PRE_WRITE(name_, struct_sockaddr_sz);")
      pcmd("PRE_WRITE(anamelen_, sizeof(__sanitizer_socklen_t));")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  POST_WRITE(name_, struct_sockaddr_sz);")
      pcmd("  POST_WRITE(anamelen_, sizeof(__sanitizer_socklen_t));")
      pcmd("}")
    }
  } else if (syscall == "getpeername") {
    if (mode == "pre") {
      pcmd("PRE_WRITE(asa_, struct_sockaddr_sz);")
      pcmd("PRE_WRITE(alen_, sizeof(__sanitizer_socklen_t));")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  POST_WRITE(asa_, struct_sockaddr_sz);")
      pcmd("  POST_WRITE(alen_, sizeof(__sanitizer_socklen_t));")
      pcmd("}")
    }
  } else if (syscall == "getsockname") {
    if (mode == "pre") {
      pcmd("PRE_WRITE(asa_, struct_sockaddr_sz);")
      pcmd("PRE_WRITE(alen_, sizeof(__sanitizer_socklen_t));")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  POST_WRITE(asa_, struct_sockaddr_sz);")
      pcmd("  POST_WRITE(alen_, sizeof(__sanitizer_socklen_t));")
      pcmd("}")
    }
  } else if (syscall == "access") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "chflags") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "fchflags") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "sync") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "kill") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_43_stat43") {
    pcmd("/* TODO */")
  } else if (syscall == "getppid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_43_lstat43") {
    pcmd("/* TODO */")
  } else if (syscall == "dup") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "pipe") {
    pcmd("/* pipe returns two descriptors through two returned values */")
  } else if (syscall == "getegid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "profil") {
    if (mode == "pre") {
      pcmd("if (samples_) {")
      pcmd("  PRE_WRITE(samples_, size_);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  if (samples_) {")
      pcmd("    POST_WRITE(samples_, size_);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "ktrace") {
    if (mode == "pre") {
      pcmd("const char *fname = (const char *)fname_;")
      pcmd("if (fname) {")
      pcmd("  PRE_READ(fname, __sanitizer::internal_strlen(fname) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *fname = (const char *)fname_;")
      pcmd("if (res == 0) {")
      pcmd("  if (fname) {")
      pcmd("    POST_READ(fname, __sanitizer::internal_strlen(fname) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "compat_13_sigaction13") {
    pcmd("/* TODO */")
  } else if (syscall == "getgid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_13_sigprocmask13") {
    pcmd("/* TODO */")
  } else if (syscall == "__getlogin") {
    if (mode == "pre") {
      pcmd("if (namebuf_) {")
      pcmd("  PRE_WRITE(namebuf_, namelen_);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  if (namebuf_) {")
      pcmd("    POST_WRITE(namebuf_, namelen_);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "__setlogin") {
    if (mode == "pre") {
      pcmd("const char *namebuf = (const char *)namebuf_;")
      pcmd("if (namebuf) {")
      pcmd("  PRE_READ(namebuf, __sanitizer::internal_strlen(namebuf) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *namebuf = (const char *)namebuf_;")
      pcmd("  if (namebuf) {")
      pcmd("    POST_READ(namebuf, __sanitizer::internal_strlen(namebuf) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "acct") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "compat_13_sigpending13") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_13_sigaltstack13") {
    pcmd("/* TODO */")
  } else if (syscall == "ioctl") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_12_oreboot") {
    pcmd("/* TODO */")
  } else if (syscall == "revoke") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "symlink") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("const char *link = (const char *)link_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
      pcmd("if (link) {")
      pcmd("  PRE_READ(link, __sanitizer::internal_strlen(link) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  const char *link = (const char *)link_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("  if (link) {")
      pcmd("    POST_READ(link, __sanitizer::internal_strlen(link) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "readlink") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
      pcmd("if (buf_) {")
      pcmd("  PRE_WRITE(buf_, count_);")
      pcmd("}")
    } else {
      pcmd("if (res > 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("  if (buf_) {")
      pcmd("    PRE_WRITE(buf_, res);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "execve") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("char **argp = (char **)argp_;")
      pcmd("char **envp = (char **)envp_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
      pcmd("if (argp && argp[0]) {")
      pcmd("  char *a = argp[0];")
      pcmd("  while (a++) {")
      pcmd("    PRE_READ(a, __sanitizer::internal_strlen(a) + 1);")
      pcmd("  }")
      pcmd("}")
      pcmd("if (envp && envp[0]) {")
      pcmd("  char *e = envp[0];")
      pcmd("  while (e++) {")
      pcmd("    PRE_READ(e, __sanitizer::internal_strlen(e) + 1);")
      pcmd("  }")
      pcmd("}")
    } else {
      pcmd("/* If we are here, something went wrong */")
      pcmd("const char *path = (const char *)path_;")
      pcmd("char **argp = (char **)argp_;")
      pcmd("char **envp = (char **)envp_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
      pcmd("if (argp && argp[0]) {")
      pcmd("  char *a = argp[0];")
      pcmd("  while (a++) {")
      pcmd("    POST_READ(a, __sanitizer::internal_strlen(a) + 1);")
      pcmd("  }")
      pcmd("}")
      pcmd("if (envp && envp[0]) {")
      pcmd("  char *e = envp[0];")
      pcmd("  while (e++) {")
      pcmd("    POST_READ(e, __sanitizer::internal_strlen(e) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "umask") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "chroot") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "compat_43_fstat43") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_ogetkerninfo") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_ogetpagesize") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_12_msync") {
    pcmd("/* TODO */")
  } else if (syscall == "vfork") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_43_ommap") {
    pcmd("/* TODO */")
  } else if (syscall == "vadvise") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "munmap") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "mprotect") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "madvise") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "mincore") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "getgroups") {
    if (mode == "pre") {
      pcmd("unsigned int *gidset = (unsigned int *)gidset_;")
      pcmd("if (gidset) {")
      pcmd("  PRE_WRITE(gidset, sizeof(*gidset) * gidsetsize_);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  unsigned int *gidset = (unsigned int *)gidset_;")
      pcmd("  if (gidset) {")
      pcmd("    POST_WRITE(gidset, sizeof(*gidset) * gidsetsize_);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "setgroups") {
    if (mode == "pre") {
      pcmd("unsigned int *gidset = (unsigned int *)gidset_;")
      pcmd("if (gidset) {")
      pcmd("  PRE_READ(gidset, sizeof(*gidset) * gidsetsize_);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  unsigned int *gidset = (unsigned int *)gidset_;")
      pcmd("  if (gidset) {")
      pcmd("    POST_READ(gidset, sizeof(*gidset) * gidsetsize_);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "getpgrp") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "setpgid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_50_setitimer") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_owait") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_12_oswapon") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50_getitimer") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_ogethostname") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_osethostname") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_ogetdtablesize") {
    pcmd("/* TODO */")
  } else if (syscall == "dup2") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "getrandom") {
    pcmd("/* TODO */")
  } else if (syscall == "fcntl") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_50_select") {
    pcmd("/* TODO */")
  } else if (syscall == "fsync") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "setpriority") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_30_socket") {
    pcmd("/* TODO */")
  } else if (syscall == "connect") {
    if (mode == "pre") {
      pcmd("PRE_READ(name_, namelen_);")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  POST_READ(name_, namelen_);")
      pcmd("}")
    }
  } else if (syscall == "compat_43_oaccept") {
    pcmd("/* TODO */")
  } else if (syscall == "getpriority") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_43_osend") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_orecv") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_13_sigreturn13") {
    pcmd("/* TODO */")
  } else if (syscall == "bind") {
    if (mode == "pre") {
      pcmd("PRE_READ(name_, namelen_);")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  PRE_READ(name_, namelen_);")
      pcmd("}")
    }
  } else if (syscall == "setsockopt") {
    if (mode == "pre") {
      pcmd("if (val_) {")
      pcmd("  PRE_READ(val_, valsize_);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  if (val_) {")
      pcmd("    POST_READ(val_, valsize_);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "listen") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_43_osigvec") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_osigblock") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_osigsetmask") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_13_sigsuspend13") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_osigstack") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_orecvmsg") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_osendmsg") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50_gettimeofday") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50_getrusage") {
    pcmd("/* TODO */")
  } else if (syscall == "getsockopt") {
    pcmd("/* TODO */")
  } else if (syscall == "readv") {
    if (mode == "pre") {
      pcmd("struct __sanitizer_iovec *iovp = (struct __sanitizer_iovec *)iovp_;")
      pcmd("int i;")
      pcmd("if (iovp) {")
      pcmd("  PRE_READ(iovp, sizeof(struct __sanitizer_iovec) * iovcnt_);")
      pcmd("  for (i = 0; i < iovcnt_; i++) {")
      pcmd("    PRE_WRITE(iovp[i].iov_base, iovp[i].iov_len);")
      pcmd("  }")
      pcmd("}")
    } else {
      pcmd("struct __sanitizer_iovec *iovp = (struct __sanitizer_iovec *)iovp_;")
      pcmd("int i;")
      pcmd("uptr m, n = res;")
      pcmd("if (res > 0) {")
      pcmd("  if (iovp) {")
      pcmd("    POST_READ(iovp, sizeof(struct __sanitizer_iovec) * iovcnt_);")
      pcmd("    for (i = 0; i < iovcnt_ && n > 0; i++) {")
      pcmd("      m = n > iovp[i].iov_len ? iovp[i].iov_len : n;")
      pcmd("      POST_WRITE(iovp[i].iov_base, m);")
      pcmd("      n -= m;")
      pcmd("    }")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "writev") {
    if (mode == "pre") {
      pcmd("struct __sanitizer_iovec *iovp = (struct __sanitizer_iovec *)iovp_;")
      pcmd("int i;")
      pcmd("if (iovp) {")
      pcmd("  PRE_READ(iovp, sizeof(struct __sanitizer_iovec) * iovcnt_);")
      pcmd("  for (i = 0; i < iovcnt_; i++) {")
      pcmd("    PRE_READ(iovp[i].iov_base, iovp[i].iov_len);")
      pcmd("  }")
      pcmd("}")
    } else {
      pcmd("struct __sanitizer_iovec *iovp = (struct __sanitizer_iovec *)iovp_;")
      pcmd("int i;")
      pcmd("uptr m, n = res;")
      pcmd("if (res > 0) {")
      pcmd("  if (iovp) {")
      pcmd("    POST_READ(iovp, sizeof(struct __sanitizer_iovec) * iovcnt_);")
      pcmd("    for (i = 0; i < iovcnt_ && n > 0; i++) {")
      pcmd("      m = n > iovp[i].iov_len ? iovp[i].iov_len : n;")
      pcmd("      POST_READ(iovp[i].iov_base, m);")
      pcmd("      n -= m;")
      pcmd("    }")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "compat_50_settimeofday") {
    pcmd("/* TODO */")
  } else if (syscall == "fchown") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "fchmod") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_43_orecvfrom") {
    pcmd("/* TODO */")
  } else if (syscall == "setreuid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "setregid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "rename") {
    if (mode == "pre") {
      pcmd("const char *from = (const char *)from_;")
      pcmd("const char *to = (const char *)to_;")
      pcmd("if (from) {")
      pcmd("  PRE_READ(from, __sanitizer::internal_strlen(from) + 1);")
      pcmd("}")
      pcmd("if (to) {")
      pcmd("  PRE_READ(to, __sanitizer::internal_strlen(to) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *from = (const char *)from_;")
      pcmd("  const char *to = (const char *)to_;")
      pcmd("  if (from) {")
      pcmd("    POST_READ(from, __sanitizer::internal_strlen(from) + 1);")
      pcmd("  }")
      pcmd("  if (to) {")
      pcmd("    POST_READ(to, __sanitizer::internal_strlen(to) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "compat_43_otruncate") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_oftruncate") {
    pcmd("/* TODO */")
  } else if (syscall == "flock") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "mkfifo") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "sendto") {
    if (mode == "pre") {
      pcmd("PRE_READ(buf_, len_);")
      pcmd("PRE_READ(to_, tolen_);")
    } else {
      pcmd("if (res >= 0) {")
      pcmd("  POST_READ(buf_, len_);")
      pcmd("  POST_READ(to_, tolen_);")
      pcmd("}")
    }
  } else if (syscall == "shutdown") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "socketpair") {
    if (mode == "pre") {
      pcmd("PRE_WRITE(rsv_, 2 * sizeof(int));")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  POST_WRITE(rsv_, 2 * sizeof(int));")
      pcmd("}")
    }
  } else if (syscall == "mkdir") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "rmdir") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "compat_50_utimes") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50_adjtime") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_ogetpeername") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_ogethostid") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_osethostid") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_ogetrlimit") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_osetrlimit") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_okillpg") {
    pcmd("/* TODO */")
  } else if (syscall == "setsid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_50_quotactl") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_oquota") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_43_ogetsockname") {
    pcmd("/* TODO */")
  } else if (syscall == "nfssvc") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_43_ogetdirentries") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_20_statfs") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_20_fstatfs") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_30_getfh") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_09_ogetdomainname") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_09_osetdomainname") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_09_ouname") {
    pcmd("/* TODO */")
  } else if (syscall == "sysarch") {
    pcmd("/* TODO */")
  } else if (syscall == "__futex") {
    pcmd("/* TODO */")
  } else if (syscall == "__futex_set_robust_list") {
    pcmd("/* TODO */")
  } else if (syscall == "__futex_get_robust_list") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_10_osemsys") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_10_omsgsys") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_10_oshmsys") {
    pcmd("/* TODO */")
  } else if (syscall == "pread") {
    if (mode == "pre") {
      pcmd("if (buf_) {")
      pcmd("  PRE_WRITE(buf_, nbyte_);")
      pcmd("}")
    } else {
      pcmd("if (res > 0) {")
      pcmd("  POST_WRITE(buf_, res);")
      pcmd("}")
    }
  } else if (syscall == "pwrite") {
    if (mode == "pre") {
      pcmd("if (buf_) {")
      pcmd("  PRE_READ(buf_, nbyte_);")
      pcmd("}")
    } else {
      pcmd("if (res > 0) {")
      pcmd("  POST_READ(buf_, res);")
      pcmd("}")
    }
  } else if (syscall == "compat_30_ntp_gettime") {
    pcmd("/* TODO */")
  } else if (syscall == "ntp_adjtime") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "setgid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "setegid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "seteuid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "lfs_bmapv") {
    pcmd("/* TODO */")
  } else if (syscall == "lfs_markv") {
    pcmd("/* TODO */")
  } else if (syscall == "lfs_segclean") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50_lfs_segwait") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_12_stat12") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_12_fstat12") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_12_lstat12") {
    pcmd("/* TODO */")
  } else if (syscall == "pathconf") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res != -1) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "getsockopt2") {
    pcmd("/* TODO */")
  } else if (syscall == "fpathconf") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "getrlimit") {
    if (mode == "pre") {
      pcmd("PRE_WRITE(rlp_, struct_rlimit_sz);")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  POST_WRITE(rlp_, struct_rlimit_sz);")
      pcmd("}")
    }
  } else if (syscall == "setrlimit") {
    if (mode == "pre") {
      pcmd("PRE_READ(rlp_, struct_rlimit_sz);")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  POST_READ(rlp_, struct_rlimit_sz);")
      pcmd("}")
    }
  } else if (syscall == "compat_12_getdirentries") {
    pcmd("/* TODO */")
  } else if (syscall == "mmap") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__syscall") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "lseek") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "truncate") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "ftruncate") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__sysctl") {
    if (mode == "pre") {
      pcmd("const int *name = (const int *)name_;")
      pcmd("if (name) {")
      pcmd("  PRE_READ(name, namelen_ * sizeof(*name));")
      pcmd("}")
      pcmd("if (newv_) {")
      pcmd("  PRE_READ(name, newlen_);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const int *name = (const int *)name_;")
      pcmd("  if (name) {")
      pcmd("    POST_READ(name, namelen_ * sizeof(*name));")
      pcmd("  }")
      pcmd("  if (newv_) {")
      pcmd("    POST_READ(name, newlen_);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "mlock") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "munlock") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "undelete") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  const char *path = (const char *)path_;")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "compat_50_futimes") {
    pcmd("/* TODO */")
  } else if (syscall == "getpgid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "reboot") {
    if (mode == "pre") {
      pcmd("const char *bootstr = (const char *)bootstr_;")
      pcmd("if (bootstr) {")
      pcmd("  PRE_READ(bootstr, __sanitizer::internal_strlen(bootstr) + 1);")
      pcmd("}")
    } else {
      pcmd("/* This call should never return */")
      pcmd("const char *bootstr = (const char *)bootstr_;")
      pcmd("if (bootstr) {")
      pcmd("  POST_READ(bootstr, __sanitizer::internal_strlen(bootstr) + 1);")
      pcmd("}")
    }
  } else if (syscall == "poll") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "afssys") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_14___semctl") {
    pcmd("/* TODO */")
  } else if (syscall == "semget") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "semop") {
    if (mode == "pre") {
      pcmd("if (sops_) {")
      pcmd("  PRE_READ(sops_, nsops_ * struct_sembuf_sz);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  if (sops_) {")
      pcmd("    POST_READ(sops_, nsops_ * struct_sembuf_sz);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "semconfig") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_14_msgctl") {
    pcmd("/* TODO */")
  } else if (syscall == "msgget") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "msgsnd") {
    if (mode == "pre") {
      pcmd("if (msgp_) {")
      pcmd("  PRE_READ(msgp_, msgsz_);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  if (msgp_) {")
      pcmd("    POST_READ(msgp_, msgsz_);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "msgrcv") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "shmat") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_14_shmctl") {
    pcmd("/* TODO */")
  } else if (syscall == "shmdt") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "shmget") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_50_clock_gettime") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50_clock_settime") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50_clock_getres") {
    pcmd("/* TODO */")
  } else if (syscall == "timer_create") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "timer_delete") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_50_timer_settime") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50_timer_gettime") {
    pcmd("/* TODO */")
  } else if (syscall == "timer_getoverrun") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_50_nanosleep") {
    pcmd("/* TODO */")
  } else if (syscall == "fdatasync") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "mlockall") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "munlockall") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_50___sigtimedwait") {
    pcmd("/* TODO */")
  } else if (syscall == "sigqueueinfo") {
    if (mode == "pre") {
      pcmd("if (info_) {")
      pcmd("  PRE_READ(info_, siginfo_t_sz);")
      pcmd("}")
    }
  } else if (syscall == "modctl") {
    pcmd("/* TODO */")
  } else if (syscall == "_ksem_init") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_ksem_open") {
    if (mode == "pre") {
      pcmd("const char *name = (const char *)name_;")
      pcmd("if (name) {")
      pcmd("  PRE_READ(name, __sanitizer::internal_strlen(name) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *name = (const char *)name_;")
      pcmd("if (name) {")
      pcmd("  POST_READ(name, __sanitizer::internal_strlen(name) + 1);")
      pcmd("}")
    }
  } else if (syscall == "_ksem_unlink") {
    if (mode == "pre") {
      pcmd("const char *name = (const char *)name_;")
      pcmd("if (name) {")
      pcmd("  PRE_READ(name, __sanitizer::internal_strlen(name) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *name = (const char *)name_;")
      pcmd("if (name) {")
      pcmd("  POST_READ(name, __sanitizer::internal_strlen(name) + 1);")
      pcmd("}")
    }
  } else if (syscall == "_ksem_close") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_ksem_post") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_ksem_wait") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_ksem_trywait") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_ksem_getvalue") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_ksem_destroy") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_ksem_timedwait") {
    if (mode == "pre") {
      pcmd("if (abstime_) {")
      pcmd("  PRE_READ(abstime_, struct_timespec_sz);")
      pcmd("}")
    }
  } else if (syscall == "mq_open") {
    if (mode == "pre") {
      pcmd("const char *name = (const char *)name_;")
      pcmd("if (name) {")
      pcmd("  PRE_READ(name, __sanitizer::internal_strlen(name) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *name = (const char *)name_;")
      pcmd("if (name) {")
      pcmd("  POST_READ(name, __sanitizer::internal_strlen(name) + 1);")
      pcmd("}")
    }
  } else if (syscall == "mq_close") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "mq_unlink") {
    if (mode == "pre") {
      pcmd("const char *name = (const char *)name_;")
      pcmd("if (name) {")
      pcmd("  PRE_READ(name, __sanitizer::internal_strlen(name) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *name = (const char *)name_;")
      pcmd("if (name) {")
      pcmd("  POST_READ(name, __sanitizer::internal_strlen(name) + 1);")
      pcmd("}")
    }
  } else if (syscall == "mq_getattr") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "mq_setattr") {
    if (mode == "pre") {
      pcmd("if (mqstat_) {")
      pcmd("  PRE_READ(mqstat_, struct_mq_attr_sz);")
      pcmd("}")
    }
  } else if (syscall == "mq_notify") {
    if (mode == "pre") {
      pcmd("if (notification_) {")
      pcmd("  PRE_READ(notification_, struct_sigevent_sz);")
      pcmd("}")
    }
  } else if (syscall == "mq_send") {
    if (mode == "pre") {
      pcmd("if (msg_ptr_) {")
      pcmd("  PRE_READ(msg_ptr_, msg_len_);")
      pcmd("}")
    }
  } else if (syscall == "mq_receive") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_50_mq_timedsend") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50_mq_timedreceive") {
    pcmd("/* TODO */")
  } else if (syscall == "__posix_rename") {
    if (mode == "pre") {
      pcmd("const char *from = (const char *)from_;")
      pcmd("const char *to = (const char *)to_;")
      pcmd("if (from_) {")
      pcmd("  PRE_READ(from, __sanitizer::internal_strlen(from) + 1);")
      pcmd("}")
      pcmd("if (to) {")
      pcmd("  PRE_READ(to, __sanitizer::internal_strlen(to) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *from = (const char *)from_;")
      pcmd("const char *to = (const char *)to_;")
      pcmd("if (from) {")
      pcmd("  POST_READ(from, __sanitizer::internal_strlen(from) + 1);")
      pcmd("}")
      pcmd("if (to) {")
      pcmd("  POST_READ(to, __sanitizer::internal_strlen(to) + 1);")
      pcmd("}")
    }
  } else if (syscall == "swapctl") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_30_getdents") {
    pcmd("/* TODO */")
  } else if (syscall == "minherit") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "lchmod") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "lchown") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "compat_50_lutimes") {
    pcmd("/* TODO */")
  } else if (syscall == "__msync13") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_30___stat13") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_30___fstat13") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_30___lstat13") {
    pcmd("/* TODO */")
  } else if (syscall == "__sigaltstack14") {
    if (mode == "pre") {
      pcmd("if (nss_) {")
      pcmd("  PRE_READ(nss_, struct_sigaltstack_sz);")
      pcmd("}")
      pcmd("if (oss_) {")
      pcmd("  PRE_READ(oss_, struct_sigaltstack_sz);")
      pcmd("}")
    }
  } else if (syscall == "__vfork14") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__posix_chown") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "__posix_fchown") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__posix_lchown") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "getsid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__clone") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "fktrace") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "preadv") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "pwritev") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_16___sigaction14") {
    pcmd("/* TODO */")
  } else if (syscall == "__sigpending14") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__sigprocmask14") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__sigsuspend14") {
    pcmd("if (set_) {")
    pcmd("  PRE_READ(set_, sizeof(__sanitizer_sigset_t));")
    pcmd("}")
  } else if (syscall == "compat_16___sigreturn14") {
    pcmd("/* TODO */")
  } else if (syscall == "__getcwd") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "fchroot") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_30_fhopen") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_30_fhstat") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_20_fhstatfs") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50_____semctl13") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50___msgctl13") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50___shmctl13") {
    pcmd("/* TODO */")
  } else if (syscall == "lchflags") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "issetugid") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "utrace") {
    if (mode == "pre") {
      pcmd("const char *label = (const char *)label_;")
      pcmd("if (label) {")
      pcmd("  PRE_READ(label, __sanitizer::internal_strlen(label) + 1);")
      pcmd("}")
      pcmd("if (addr_) {")
      pcmd("  PRE_READ(addr_, len_);")
      pcmd("}")
    } else {
      pcmd("const char *label = (const char *)label_;")
      pcmd("if (label) {")
      pcmd("  POST_READ(label, __sanitizer::internal_strlen(label) + 1);")
      pcmd("}")
      pcmd("if (addr_) {")
      pcmd("  POST_READ(addr_, len_);")
      pcmd("}")
    }
  } else if (syscall == "getcontext") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "setcontext") {
    if (mode == "pre") {
      pcmd("if (ucp_) {")
      pcmd("  PRE_READ(ucp_, ucontext_t_sz);")
      pcmd("}")
    }
  } else if (syscall == "_lwp_create") {
    if (mode == "pre") {
      pcmd("if (ucp_) {")
      pcmd("  PRE_READ(ucp_, ucontext_t_sz);")
      pcmd("}")
    }
  } else if (syscall == "_lwp_exit") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_lwp_self") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_lwp_wait") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_lwp_suspend") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_lwp_continue") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_lwp_wakeup") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_lwp_getprivate") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_lwp_setprivate") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_lwp_kill") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_lwp_detach") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_50__lwp_park") {
    pcmd("/* TODO */")
  } else if (syscall == "_lwp_unpark") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_lwp_unpark_all") {
    if (mode == "pre") {
      pcmd("if (targets_) {")
      pcmd("  PRE_READ(targets_, ntargets_ * sizeof(__sanitizer_lwpid_t));")
      pcmd("}")
    }
  } else if (syscall == "_lwp_setname") {
    if (mode == "pre") {
      pcmd("const char *name = (const char *)name_;")
      pcmd("if (name) {")
      pcmd("  PRE_READ(name, __sanitizer::internal_strlen(name) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *name = (const char *)name_;")
      pcmd("if (name) {")
      pcmd("  POST_READ(name, __sanitizer::internal_strlen(name) + 1);")
      pcmd("}")
    }
  } else if (syscall == "_lwp_getname") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_lwp_ctl") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_60_sa_register") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_60_sa_stacks") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_60_sa_enable") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_60_sa_setconcurrency") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_60_sa_yield") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_60_sa_preempt") {
    pcmd("/* TODO */")
  } else if (syscall == "__sigaction_sigtramp") {
    pcmd("if (nsa_) {")
    pcmd("  PRE_READ(nsa_, sizeof(__sanitizer_sigaction));")
    pcmd("}")
  } else if (syscall == "rasctl") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "kqueue") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_50_kevent") {
    pcmd("/* TODO */")
  } else if (syscall == "_sched_setparam") {
    pcmd("if (params_) {")
    pcmd("  PRE_READ(params_, struct_sched_param_sz);")
    pcmd("}")
  } else if (syscall == "_sched_getparam") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_sched_setaffinity") {
    pcmd("if (cpuset_) {")
    pcmd("  PRE_READ(cpuset_, size_);")
    pcmd("}")
  } else if (syscall == "_sched_getaffinity") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "sched_yield") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_sched_protect") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "fsync_range") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "uuidgen") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_90_getvfsstat") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_90_statvfs1") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "compat_90_fstatvfs1") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_30_fhstatvfs1") {
    pcmd("/* TODO */")
  } else if (syscall == "extattrctl") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "extattr_set_file") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "extattr_get_file") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "extattr_delete_file") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "extattr_set_fd") {
    pcmd("/* TODO */")
  } else if (syscall == "extattr_get_fd") {
    pcmd("/* TODO */")
  } else if (syscall == "extattr_delete_fd") {
    pcmd("/* TODO */")
  } else if (syscall == "extattr_set_link") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "extattr_get_link") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "extattr_delete_link") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "extattr_list_fd") {
    pcmd("/* TODO */")
  } else if (syscall == "extattr_list_file") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "extattr_list_link") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "compat_50_pselect") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50_pollts") {
    pcmd("/* TODO */")
  } else if (syscall == "setxattr") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "lsetxattr") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "fsetxattr") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "getxattr") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "lgetxattr") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "fgetxattr") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "listxattr") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "llistxattr") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "flistxattr") {
    pcmd("/* TODO */")
  } else if (syscall == "removexattr") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "lremovexattr") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "fremovexattr") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50___stat30") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50___fstat30") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50___lstat30") {
    pcmd("/* TODO */")
  } else if (syscall == "__getdents30") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "posix_fadvise") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "compat_30___fhstat30") {
    pcmd("/* TODO */")
  } else if (syscall == "compat_50___ntp_gettime30") {
    pcmd("/* TODO */")
  } else if (syscall == "__socket30") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__getfh30") {
    if (mode == "pre") {
      pcmd("const char *fname = (const char *)fname_;")
      pcmd("if (fname) {")
      pcmd("  PRE_READ(fname, __sanitizer::internal_strlen(fname) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *fname = (const char *)fname_;")
      pcmd("if (res == 0) {")
      pcmd("  if (fname) {")
      pcmd("    POST_READ(fname, __sanitizer::internal_strlen(fname) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "__fhopen40") {
    if (mode == "pre") {
      pcmd("if (fhp_) {")
      pcmd("  PRE_READ(fhp_, fh_size_);")
      pcmd("}")
    }
  } else if (syscall == "compat_90_fhstatvfs1") {
    if (mode == "pre") {
      pcmd("if (fhp_) {")
      pcmd("  PRE_READ(fhp_, fh_size_);")
      pcmd("}")
    }
  } else if (syscall == "compat_50___fhstat40") {
    if (mode == "pre") {
      pcmd("if (fhp_) {")
      pcmd("  PRE_READ(fhp_, fh_size_);")
      pcmd("}")
    }
  } else if (syscall == "aio_cancel") {
    if (mode == "pre") {
      pcmd("if (aiocbp_) {")
      pcmd("  PRE_READ(aiocbp_, sizeof(struct __sanitizer_aiocb));")
      pcmd("}")
    }
  } else if (syscall == "aio_error") {
    if (mode == "pre") {
      pcmd("if (aiocbp_) {")
      pcmd("  PRE_READ(aiocbp_, sizeof(struct __sanitizer_aiocb));")
      pcmd("}")
    }
  } else if (syscall == "aio_fsync") {
    if (mode == "pre") {
      pcmd("if (aiocbp_) {")
      pcmd("  PRE_READ(aiocbp_, sizeof(struct __sanitizer_aiocb));")
      pcmd("}")
    }
  } else if (syscall == "aio_read") {
    if (mode == "pre") {
      pcmd("if (aiocbp_) {")
      pcmd("  PRE_READ(aiocbp_, sizeof(struct __sanitizer_aiocb));")
      pcmd("}")
    }
  } else if (syscall == "aio_return") {
    if (mode == "pre") {
      pcmd("if (aiocbp_) {")
      pcmd("  PRE_READ(aiocbp_, sizeof(struct __sanitizer_aiocb));")
      pcmd("}")
    }
  } else if (syscall == "compat_50_aio_suspend") {
    pcmd("/* TODO */")
  } else if (syscall == "aio_write") {
    if (mode == "pre") {
      pcmd("if (aiocbp_) {")
      pcmd("  PRE_READ(aiocbp_, sizeof(struct __sanitizer_aiocb));")
      pcmd("}")
    }
  } else if (syscall == "lio_listio") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__mount50") {
    if (mode == "pre") {
      pcmd("const char *type = (const char *)type_;")
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (type) {")
      pcmd("  PRE_READ(type, __sanitizer::internal_strlen(type) + 1);")
      pcmd("}")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
      pcmd("if (data_) {")
      pcmd("  PRE_READ(data_, data_len_);")
      pcmd("}")
    } else {
      pcmd("const char *type = (const char *)type_;")
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (type) {")
      pcmd("  POST_READ(type, __sanitizer::internal_strlen(type) + 1);")
      pcmd("}")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
      pcmd("if (data_) {")
      pcmd("  POST_READ(data_, data_len_);")
      pcmd("}")
    }
  } else if (syscall == "mremap") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "pset_create") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "pset_destroy") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "pset_assign") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "_pset_bind") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__posix_fadvise50") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__select50") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__gettimeofday50") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__settimeofday50") {
    if (mode == "pre") {
      pcmd("if (tv_) {")
      pcmd("  PRE_READ(tv_, timeval_sz);")
      pcmd("}")
      pcmd("if (tzp_) {")
      pcmd("  PRE_READ(tzp_, struct_timezone_sz);")
      pcmd("}")
    }
  } else if (syscall == "__utimes50") {
    if (mode == "pre") {
      pcmd("struct __sanitizer_timespec **tptr = (struct __sanitizer_timespec **)tptr_;")
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
      pcmd("if (tptr) {")
      pcmd("  PRE_READ(tptr[0], struct_timespec_sz);")
      pcmd("  PRE_READ(tptr[1], struct_timespec_sz);")
      pcmd("}")
    }
  } else if (syscall == "__adjtime50") {
    if (mode == "pre") {
      pcmd("if (delta_) {")
      pcmd("  PRE_READ(delta_, timeval_sz);")
      pcmd("}")
    }
  } else if (syscall == "__lfs_segwait50") {
    pcmd("/* TODO */")
  } else if (syscall == "__futimes50") {
    if (mode == "pre") {
      pcmd("struct __sanitizer_timespec **tptr = (struct __sanitizer_timespec **)tptr_;")
      pcmd("if (tptr) {")
      pcmd("  PRE_READ(tptr[0], struct_timespec_sz);")
      pcmd("  PRE_READ(tptr[1], struct_timespec_sz);")
      pcmd("}")
    }
  } else if (syscall == "__lutimes50") {
    if (mode == "pre") {
      pcmd("struct __sanitizer_timespec **tptr = (struct __sanitizer_timespec **)tptr_;")
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
      pcmd("if (tptr) {")
      pcmd("  PRE_READ(tptr[0], struct_timespec_sz);")
      pcmd("  PRE_READ(tptr[1], struct_timespec_sz);")
      pcmd("}")
    } else {
      pcmd("struct __sanitizer_timespec **tptr = (struct __sanitizer_timespec **)tptr_;")
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
      pcmd("if (tptr) {")
      pcmd("  POST_READ(tptr[0], struct_timespec_sz);")
      pcmd("  POST_READ(tptr[1], struct_timespec_sz);")
      pcmd("}")
    }
  } else if (syscall == "__setitimer50") {
    if (mode == "pre") {
      pcmd("struct __sanitizer_itimerval *itv = (struct __sanitizer_itimerval *)itv_;")
      pcmd("if (itv) {")
      pcmd("  PRE_READ(&itv->it_interval.tv_sec, sizeof(__sanitizer_time_t));")
      pcmd("  PRE_READ(&itv->it_interval.tv_usec, sizeof(__sanitizer_suseconds_t));")
      pcmd("  PRE_READ(&itv->it_value.tv_sec, sizeof(__sanitizer_time_t));")
      pcmd("  PRE_READ(&itv->it_value.tv_usec, sizeof(__sanitizer_suseconds_t));")
      pcmd("}")
    }
  } else if (syscall == "__getitimer50") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__clock_gettime50") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__clock_settime50") {
    if (mode == "pre") {
      pcmd("if (tp_) {")
      pcmd("  PRE_READ(tp_, struct_timespec_sz);")
      pcmd("}")
    }
  } else if (syscall == "__clock_getres50") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__nanosleep50") {
    if (mode == "pre") {
      pcmd("if (rqtp_) {")
      pcmd("  PRE_READ(rqtp_, struct_timespec_sz);")
      pcmd("}")
    }
  } else if (syscall == "____sigtimedwait50") {
    if (mode == "pre") {
      pcmd("if (set_) {")
      pcmd("  PRE_READ(set_, sizeof(__sanitizer_sigset_t));")
      pcmd("}")
      pcmd("if (timeout_) {")
      pcmd("  PRE_READ(timeout_, struct_timespec_sz);")
      pcmd("}")
    }
  } else if (syscall == "__mq_timedsend50") {
    if (mode == "pre") {
      pcmd("if (msg_ptr_) {")
      pcmd("  PRE_READ(msg_ptr_, msg_len_);")
      pcmd("}")
      pcmd("if (abs_timeout_) {")
      pcmd("  PRE_READ(abs_timeout_, struct_timespec_sz);")
      pcmd("}")
    }
  } else if (syscall == "__mq_timedreceive50") {
    if (mode == "pre") {
      pcmd("if (msg_ptr_) {")
      pcmd("  PRE_READ(msg_ptr_, msg_len_);")
      pcmd("}")
      pcmd("if (abs_timeout_) {")
      pcmd("  PRE_READ(abs_timeout_, struct_timespec_sz);")
      pcmd("}")
    }
  } else if (syscall == "compat_60__lwp_park") {
    pcmd("/* TODO */")
  } else if (syscall == "__kevent50") {
    if (mode == "pre") {
      pcmd("if (changelist_) {")
      pcmd("  PRE_READ(changelist_, nchanges_ * struct_kevent_sz);")
      pcmd("}")
      pcmd("if (timeout_) {")
      pcmd("  PRE_READ(timeout_, struct_timespec_sz);")
      pcmd("}")
    }
  } else if (syscall == "__pselect50") {
    if (mode == "pre") {
      pcmd("if (ts_) {")
      pcmd("  PRE_READ(ts_, struct_timespec_sz);")
      pcmd("}")
      pcmd("if (mask_) {")
      pcmd("  PRE_READ(mask_, sizeof(struct __sanitizer_sigset_t));")
      pcmd("}")
    }
  } else if (syscall == "__pollts50") {
    if (mode == "pre") {
      pcmd("if (ts_) {")
      pcmd("  PRE_READ(ts_, struct_timespec_sz);")
      pcmd("}")
      pcmd("if (mask_) {")
      pcmd("  PRE_READ(mask_, sizeof(struct __sanitizer_sigset_t));")
      pcmd("}")
    }
  } else if (syscall == "__aio_suspend50") {
    if (mode == "pre") {
      pcmd("int i;")
      pcmd("const struct aiocb * const *list = (const struct aiocb * const *)list_;")
      pcmd("if (list) {")
      pcmd("  for (i = 0; i < nent_; i++) {")
      pcmd("    if (list[i]) {")
      pcmd("      PRE_READ(list[i], sizeof(struct __sanitizer_aiocb));")
      pcmd("    }")
      pcmd("  }")
      pcmd("}")
      pcmd("if (timeout_) {")
      pcmd("  PRE_READ(timeout_, struct_timespec_sz);")
      pcmd("}")
    }
  } else if (syscall == "__stat50") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (res == 0) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "__fstat50") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__lstat50") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (res == 0) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "____semctl50") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__shmctl50") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__msgctl50") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__getrusage50") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__timer_settime50") {
    if (mode == "pre") {
      pcmd("struct __sanitizer_itimerval *value = (struct __sanitizer_itimerval *)value_;")
      pcmd("if (value) {")
      pcmd("  PRE_READ(&value->it_interval.tv_sec, sizeof(__sanitizer_time_t));")
      pcmd("  PRE_READ(&value->it_interval.tv_usec, sizeof(__sanitizer_suseconds_t));")
      pcmd("  PRE_READ(&value->it_value.tv_sec, sizeof(__sanitizer_time_t));")
      pcmd("  PRE_READ(&value->it_value.tv_usec, sizeof(__sanitizer_suseconds_t));")
      pcmd("}")
    } else {
      pcmd("struct __sanitizer_itimerval *value = (struct __sanitizer_itimerval *)value_;")
      pcmd("if (res == 0) {")
      pcmd("  if (value) {")
      pcmd("    POST_READ(&value->it_interval.tv_sec, sizeof(__sanitizer_time_t));")
      pcmd("    POST_READ(&value->it_interval.tv_usec, sizeof(__sanitizer_suseconds_t));")
      pcmd("    POST_READ(&value->it_value.tv_sec, sizeof(__sanitizer_time_t));")
      pcmd("    POST_READ(&value->it_value.tv_usec, sizeof(__sanitizer_suseconds_t));")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "__timer_gettime50") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__ntp_gettime50") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__wait450") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__mknod50") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (res == 0) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "__fhstat50") {
    if (mode == "pre") {
      pcmd("if (fhp_) {")
      pcmd("  PRE_READ(fhp_, fh_size_);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  if (fhp_) {")
      pcmd("    POST_READ(fhp_, fh_size_);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "pipe2") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "dup3") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "kqueue1") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "paccept") {
    if (mode == "pre") {
      pcmd("if (mask_) {")
      pcmd("  PRE_READ(mask_, sizeof(__sanitizer_sigset_t));")
      pcmd("}")
    } else {
      pcmd("if (res >= 0) {")
      pcmd("  if (mask_) {")
      pcmd("    PRE_READ(mask_, sizeof(__sanitizer_sigset_t));")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "linkat") {
    if (mode == "pre") {
      pcmd("const char *name1 = (const char *)name1_;")
      pcmd("const char *name2 = (const char *)name2_;")
      pcmd("if (name1) {")
      pcmd("  PRE_READ(name1, __sanitizer::internal_strlen(name1) + 1);")
      pcmd("}")
      pcmd("if (name2) {")
      pcmd("  PRE_READ(name2, __sanitizer::internal_strlen(name2) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *name1 = (const char *)name1_;")
      pcmd("const char *name2 = (const char *)name2_;")
      pcmd("if (res == 0) {")
      pcmd("  if (name1) {")
      pcmd("    POST_READ(name1, __sanitizer::internal_strlen(name1) + 1);")
      pcmd("  }")
      pcmd("  if (name2) {")
      pcmd("    POST_READ(name2, __sanitizer::internal_strlen(name2) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "renameat") {
    if (mode == "pre") {
      pcmd("const char *from = (const char *)from_;")
      pcmd("const char *to = (const char *)to_;")
      pcmd("if (from) {")
      pcmd("  PRE_READ(from, __sanitizer::internal_strlen(from) + 1);")
      pcmd("}")
      pcmd("if (to) {")
      pcmd("  PRE_READ(to, __sanitizer::internal_strlen(to) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *from = (const char *)from_;")
      pcmd("const char *to = (const char *)to_;")
      pcmd("if (res == 0) {")
      pcmd("  if (from) {")
      pcmd("    POST_READ(from, __sanitizer::internal_strlen(from) + 1);")
      pcmd("  }")
      pcmd("  if (to) {")
      pcmd("    POST_READ(to, __sanitizer::internal_strlen(to) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "mkfifoat") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (res == 0) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "mknodat") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (res == 0) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "mkdirat") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (res == 0) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "faccessat") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (res == 0) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "fchmodat") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (res == 0) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "fchownat") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (res == 0) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "fexecve") {
    pcmd("/* TODO */")
  } else if (syscall == "fstatat") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "utimensat") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
      pcmd("if (tptr_) {")
      pcmd("  PRE_READ(tptr_, struct_timespec_sz);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (res > 0) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("  if (tptr_) {")
      pcmd("    POST_READ(tptr_, struct_timespec_sz);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "openat") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (res > 0) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "readlinkat") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (res > 0) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "symlinkat") {
    if (mode == "pre") {
      pcmd("const char *path1 = (const char *)path1_;")
      pcmd("const char *path2 = (const char *)path2_;")
      pcmd("if (path1) {")
      pcmd("  PRE_READ(path1, __sanitizer::internal_strlen(path1) + 1);")
      pcmd("}")
      pcmd("if (path2) {")
      pcmd("  PRE_READ(path2, __sanitizer::internal_strlen(path2) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path1 = (const char *)path1_;")
      pcmd("const char *path2 = (const char *)path2_;")
      pcmd("if (res == 0) {")
      pcmd("  if (path1) {")
      pcmd("    POST_READ(path1, __sanitizer::internal_strlen(path1) + 1);")
      pcmd("  }")
      pcmd("  if (path2) {")
      pcmd("    POST_READ(path2, __sanitizer::internal_strlen(path2) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "unlinkat") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (res == 0) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "futimens") {
    if (mode == "pre") {
      pcmd("struct __sanitizer_timespec **tptr = (struct __sanitizer_timespec **)tptr_;")
      pcmd("if (tptr) {")
      pcmd("  PRE_READ(tptr[0], struct_timespec_sz);")
      pcmd("  PRE_READ(tptr[1], struct_timespec_sz);")
      pcmd("}")
    } else {
      pcmd("struct __sanitizer_timespec **tptr = (struct __sanitizer_timespec **)tptr_;")
      pcmd("if (res == 0) {")
      pcmd("  if (tptr) {")
      pcmd("    POST_READ(tptr[0], struct_timespec_sz);")
      pcmd("    POST_READ(tptr[1], struct_timespec_sz);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "__quotactl") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (res == 0) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "posix_spawn") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (pid_) {")
      pcmd("  if (path) {")
      pcmd("    POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "recvmmsg") {
    if (mode == "pre") {
      pcmd("if (timeout_) {")
      pcmd("  PRE_READ(timeout_, struct_timespec_sz);")
      pcmd("}")
    } else {
      pcmd("if (res >= 0) {")
      pcmd("  if (timeout_) {")
      pcmd("    POST_READ(timeout_, struct_timespec_sz);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "sendmmsg") {
    if (mode == "pre") {
      pcmd("struct __sanitizer_mmsghdr *mmsg = (struct __sanitizer_mmsghdr *)mmsg_;")
      pcmd("if (mmsg) {")
      pcmd("  PRE_READ(mmsg, sizeof(struct __sanitizer_mmsghdr) * (vlen_ > 1024 ? 1024 : vlen_));")
      pcmd("}")
    } else {
      pcmd("struct __sanitizer_mmsghdr *mmsg = (struct __sanitizer_mmsghdr *)mmsg_;")
      pcmd("if (res >= 0) {")
      pcmd("  if (mmsg) {")
      pcmd("    POST_READ(mmsg, sizeof(struct __sanitizer_mmsghdr) * (vlen_ > 1024 ? 1024 : vlen_));")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "clock_nanosleep") {
    if (mode == "pre") {
      pcmd("if (rqtp_) {")
      pcmd("  PRE_READ(rqtp_, struct_timespec_sz);")
      pcmd("}")
    } else {
      pcmd("if (rqtp_) {")
      pcmd("  POST_READ(rqtp_, struct_timespec_sz);")
      pcmd("}")
    }
  } else if (syscall == "___lwp_park60") {
    if (mode == "pre") {
      pcmd("if (ts_) {")
      pcmd("  PRE_READ(ts_, struct_timespec_sz);")
      pcmd("}")
    } else {
      pcmd("if (res == 0) {")
      pcmd("  if (ts_) {")
      pcmd("    POST_READ(ts_, struct_timespec_sz);")
      pcmd("  }")
      pcmd("}")
    }
  } else if (syscall == "posix_fallocate") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "fdiscard") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "wait6") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "clock_getcpuclockid2") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__getvfsstat90") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__statvfs190") {
    if (mode == "pre") {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  PRE_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    } else {
      pcmd("const char *path = (const char *)path_;")
      pcmd("if (path) {")
      pcmd("  POST_READ(path, __sanitizer::internal_strlen(path) + 1);")
      pcmd("}")
    }
  } else if (syscall == "__fstatvfs190") {
    pcmd("/* Nothing to do */")
  } else if (syscall == "__fhstatvfs190") {
    if (mode == "pre") {
      pcmd("if (fhp_) {")
      pcmd("  PRE_READ(fhp_, fh_size_);")
      pcmd("}")
    }
  } else if (syscall == "__acl_get_link") {
    pcmd("/* TODO */")
  } else if (syscall == "__acl_set_link") {
    pcmd("/* TODO */")
  } else if (syscall == "__acl_delete_link") {
    pcmd("/* TODO */")
  } else if (syscall == "__acl_aclcheck_link") {
    pcmd("/* TODO */")
  } else if (syscall == "__acl_get_file") {
    pcmd("/* TODO */")
  } else if (syscall == "__acl_set_file") {
    pcmd("/* TODO */")
  } else if (syscall == "__acl_get_fd") {
    pcmd("/* TODO */")
  } else if (syscall == "__acl_set_fd") {
    pcmd("/* TODO */")
  } else if (syscall == "__acl_delete_file") {
    pcmd("/* TODO */")
  } else if (syscall == "__acl_delete_fd") {
    pcmd("/* TODO */")
  } else if (syscall == "__acl_aclcheck_file") {
    pcmd("/* TODO */")
  } else if (syscall == "__acl_aclcheck_fd") {
    pcmd("/* TODO */")
  } else if (syscall == "lpathconf") {
    pcmd("/* TODO */")
  } else {
    print "Unrecognized syscall: " syscall
    abnormal_exit = 1
    exit 1
  }
}
