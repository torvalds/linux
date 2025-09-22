//===--- rtsan_context.h - Realtime Sanitizer -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#pragma once

namespace __rtsan {

class Context {
public:
  Context();

  void RealtimePush();
  void RealtimePop();

  void BypassPush();
  void BypassPop();

  void ExpectNotRealtime(const char *intercepted_function_name);

private:
  bool InRealtimeContext() const;
  bool IsBypassed() const;
  void PrintDiagnostics(const char *intercepted_function_name);

  int realtime_depth{0};
  int bypass_depth{0};
};

Context &GetContextForThisThread();

} // namespace __rtsan
