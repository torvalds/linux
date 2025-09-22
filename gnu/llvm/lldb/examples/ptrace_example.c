//===-- ptrace_example.c --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <asm/ptrace.h>
#include <linux/elf.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

// The demo program shows how to do basic ptrace operations without lldb
// or lldb-server. For the purposes of experimentation or reporting bugs
// in kernels.
//
// It is AArch64 Linux specific, adapt as needed.
//
// Expected output:
// Before breakpoint
// After breakpoint

void inferior() {
  if (ptrace(PTRACE_TRACEME, 0, 0, 0) < 0) {
    perror("ptrace");
    return;
  }

  printf("Before breakpoint\n");

  // Go into debugger. Instruction replaced with nop later.
  // We write 2 instuctions because POKETEXT works with
  // 64 bit values and we don't want to overwrite the
  // call to printf accidentally.
  asm volatile("BRK #0 \n nop");

  printf("After breakpoint\n");
}

void debugger(pid_t child) {
  int wait_status;
  // Wait until it hits the breakpoint.
  wait(&wait_status);

  while (WIFSTOPPED(wait_status)) {
    if (WIFEXITED(wait_status)) {
      printf("inferior exited normally\n");
      return;
    }

    // Read general purpose registers to find the PC value.
    struct user_pt_regs regs;
    struct iovec io;
    io.iov_base = &regs;
    io.iov_len = sizeof(regs);
    if (ptrace(PTRACE_GETREGSET, child, NT_PRSTATUS, &io) < 0) {
      printf("getregset failed\n");
      return;
    }

    // Replace brk #0 / nop with nop / nop by writing to memory
    // at the current PC.
    uint64_t replace = 0xd503201fd503201f;
    if (ptrace(PTRACE_POKETEXT, child, regs.pc, replace) < 0) {
      printf("replacing bkpt failed\n");
      return;
    }

    // Single step over where the brk was.
    if (ptrace(PTRACE_SINGLESTEP, child, 0, 0) < 0) {
      perror("ptrace");
      return;
    }

    // Wait for single step to be done.
    wait(&wait_status);

    // Run to completion.
    if (ptrace(PTRACE_CONT, child, 0, 0) < 0) {
      perror("ptrace");
      return;
    }

    // Wait to see that the inferior exited.
    wait(&wait_status);
  }
}

int main() {
  pid_t child = fork();

  if (child == 0)
    inferior();
  else if (child > 0)
    debugger(child);
  else
    return -1;

  return 0;
}
