//===- TemplateInstCallback.h - Template Instantiation Callback - C++ --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file defines the TemplateInstantiationCallback class, which is the
// base class for callbacks that will be notified at template instantiations.
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_TEMPLATEINSTCALLBACK_H
#define LLVM_CLANG_SEMA_TEMPLATEINSTCALLBACK_H

#include "clang/Sema/Sema.h"

namespace clang {

/// This is a base class for callbacks that will be notified at every
/// template instantiation.
class TemplateInstantiationCallback {
public:
  virtual ~TemplateInstantiationCallback() = default;

  /// Called before doing AST-parsing.
  virtual void initialize(const Sema &TheSema) = 0;

  /// Called after AST-parsing is completed.
  virtual void finalize(const Sema &TheSema) = 0;

  /// Called when instantiation of a template just began.
  virtual void atTemplateBegin(const Sema &TheSema,
                               const Sema::CodeSynthesisContext &Inst) = 0;

  /// Called when instantiation of a template is just about to end.
  virtual void atTemplateEnd(const Sema &TheSema,
                             const Sema::CodeSynthesisContext &Inst) = 0;
};

template <class TemplateInstantiationCallbackPtrs>
void initialize(TemplateInstantiationCallbackPtrs &Callbacks,
                const Sema &TheSema) {
  for (auto &C : Callbacks) {
    if (C)
      C->initialize(TheSema);
  }
}

template <class TemplateInstantiationCallbackPtrs>
void finalize(TemplateInstantiationCallbackPtrs &Callbacks,
              const Sema &TheSema) {
  for (auto &C : Callbacks) {
    if (C)
      C->finalize(TheSema);
  }
}

template <class TemplateInstantiationCallbackPtrs>
void atTemplateBegin(TemplateInstantiationCallbackPtrs &Callbacks,
                     const Sema &TheSema,
                     const Sema::CodeSynthesisContext &Inst) {
  for (auto &C : Callbacks) {
    if (C)
      C->atTemplateBegin(TheSema, Inst);
  }
}

template <class TemplateInstantiationCallbackPtrs>
void atTemplateEnd(TemplateInstantiationCallbackPtrs &Callbacks,
                   const Sema &TheSema,
                   const Sema::CodeSynthesisContext &Inst) {
  for (auto &C : Callbacks) {
    if (C)
      C->atTemplateEnd(TheSema, Inst);
  }
}

} // namespace clang

#endif
