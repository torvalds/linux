//===-- Uio.h ---------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Host_linux_Uio_h_
#define liblldb_Host_linux_Uio_h_

#include "lldb/Host/Config.h"
#include <sys/uio.h>

// We shall provide our own implementation of process_vm_readv if it is not
// present
#if !HAVE_PROCESS_VM_READV
ssize_t process_vm_readv(::pid_t pid, const struct iovec *local_iov,
                         unsigned long liovcnt, const struct iovec *remote_iov,
                         unsigned long riovcnt, unsigned long flags);
#endif

#endif // liblldb_Host_linux_Uio_h_
