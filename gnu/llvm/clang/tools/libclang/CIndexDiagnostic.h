/*===-- CIndexDiagnostic.h - Diagnostics C Interface ------------*- C++ -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* Implements the diagnostic functions of the Clang C interface.              *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/
#ifndef LLVM_CLANG_TOOLS_LIBCLANG_CINDEXDIAGNOSTIC_H
#define LLVM_CLANG_TOOLS_LIBCLANG_CINDEXDIAGNOSTIC_H

#include "clang-c/Index.h"
#include <memory>
#include <vector>
#include <assert.h>

namespace clang {

class LangOptions;
class StoredDiagnostic;
class CXDiagnosticImpl;
  
class CXDiagnosticSetImpl {
  std::vector<std::unique_ptr<CXDiagnosticImpl>> Diagnostics;
  const bool IsExternallyManaged;
public:
  CXDiagnosticSetImpl(bool isManaged = false)
    : IsExternallyManaged(isManaged) {}

  virtual ~CXDiagnosticSetImpl();

  size_t getNumDiagnostics() const {
    return Diagnostics.size();
  }
  
  CXDiagnosticImpl *getDiagnostic(unsigned i) const {
    assert(i < getNumDiagnostics());
    return Diagnostics[i].get();
  }

  void appendDiagnostic(std::unique_ptr<CXDiagnosticImpl> D);

  bool empty() const {
    return Diagnostics.empty();
  }
  
  bool isExternallyManaged() const { return IsExternallyManaged; }
};

class CXDiagnosticImpl {
public:
  enum Kind { StoredDiagnosticKind, LoadedDiagnosticKind,
              CustomNoteDiagnosticKind };
  
  virtual ~CXDiagnosticImpl();
  
  /// Return the severity of the diagnostic.
  virtual CXDiagnosticSeverity getSeverity() const = 0;
  
  /// Return the location of the diagnostic.
  virtual CXSourceLocation getLocation() const = 0;

  /// Return the spelling of the diagnostic.
  virtual CXString getSpelling() const = 0;

  /// Return the text for the diagnostic option.
  virtual CXString getDiagnosticOption(CXString *Disable) const = 0;
  
  /// Return the category of the diagnostic.
  virtual unsigned getCategory() const = 0;

  /// Return the category string of the diagnostic.
  virtual CXString getCategoryText() const = 0;

  /// Return the number of source ranges for the diagnostic.
  virtual unsigned getNumRanges() const = 0;
  
  /// Return the source ranges for the diagnostic.
  virtual CXSourceRange getRange(unsigned Range) const = 0;

  /// Return the number of FixIts.
  virtual unsigned getNumFixIts() const = 0;

  /// Return the FixIt information (source range and inserted text).
  virtual CXString getFixIt(unsigned FixIt,
                            CXSourceRange *ReplacementRange) const = 0;

  Kind getKind() const { return K; }
  
  CXDiagnosticSetImpl &getChildDiagnostics() {
    return ChildDiags;
  }
  
protected:
  CXDiagnosticImpl(Kind k) : K(k) {}
  CXDiagnosticSetImpl ChildDiags;

  void append(std::unique_ptr<CXDiagnosticImpl> D) {
    ChildDiags.appendDiagnostic(std::move(D));
  }
  
private:
  Kind K;
};
  
/// The storage behind a CXDiagnostic
struct CXStoredDiagnostic : public CXDiagnosticImpl {
  const StoredDiagnostic &Diag;
  const LangOptions &LangOpts;
  
  CXStoredDiagnostic(const StoredDiagnostic &Diag,
                     const LangOptions &LangOpts)
    : CXDiagnosticImpl(StoredDiagnosticKind),
      Diag(Diag), LangOpts(LangOpts) { }

  ~CXStoredDiagnostic() override {}

  /// Return the severity of the diagnostic.
  CXDiagnosticSeverity getSeverity() const override;

  /// Return the location of the diagnostic.
  CXSourceLocation getLocation() const override;

  /// Return the spelling of the diagnostic.
  CXString getSpelling() const override;

  /// Return the text for the diagnostic option.
  CXString getDiagnosticOption(CXString *Disable) const override;

  /// Return the category of the diagnostic.
  unsigned getCategory() const override;

  /// Return the category string of the diagnostic.
  CXString getCategoryText() const override;

  /// Return the number of source ranges for the diagnostic.
  unsigned getNumRanges() const override;

  /// Return the source ranges for the diagnostic.
  CXSourceRange getRange(unsigned Range) const override;

  /// Return the number of FixIts.
  unsigned getNumFixIts() const override;

  /// Return the FixIt information (source range and inserted text).
  CXString getFixIt(unsigned FixIt,
                    CXSourceRange *ReplacementRange) const override;

  static bool classof(const CXDiagnosticImpl *D) {
    return D->getKind() == StoredDiagnosticKind;
  }
};

namespace cxdiag {
CXDiagnosticSetImpl *lazyCreateDiags(CXTranslationUnit TU,
                                     bool checkIfChanged = false);
} // end namespace cxdiag

} // end namespace clang

#endif
