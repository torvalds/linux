//===-- AppleObjCTypeEncodingParser.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCTYPEENCODINGPARSER_H
#define LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCTYPEENCODINGPARSER_H

#include "Plugins/Language/ObjC/ObjCConstants.h"
#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"
#include "lldb/lldb-private.h"

#include "clang/AST/ASTContext.h"

namespace lldb_private {
class StringLexer;
class AppleObjCTypeEncodingParser : public ObjCLanguageRuntime::EncodingToType {
public:
  AppleObjCTypeEncodingParser(ObjCLanguageRuntime &runtime);
  ~AppleObjCTypeEncodingParser() override = default;

  CompilerType RealizeType(TypeSystemClang &ast_ctx, const char *name,
                           bool for_expression) override;

private:
  struct StructElement {
    std::string name;
    clang::QualType type;
    uint32_t bitfield = 0;

    StructElement();
    ~StructElement() = default;
  };

  clang::QualType BuildType(TypeSystemClang &clang_ast_ctx, StringLexer &type,
                            bool for_expression,
                            uint32_t *bitfield_bit_size = nullptr);

  clang::QualType BuildStruct(TypeSystemClang &ast_ctx, StringLexer &type,
                              bool for_expression);

  clang::QualType BuildAggregate(TypeSystemClang &clang_ast_ctx,
                                 StringLexer &type, bool for_expression,
                                 char opener, char closer, uint32_t kind);

  clang::QualType BuildUnion(TypeSystemClang &ast_ctx, StringLexer &type,
                             bool for_expression);

  clang::QualType BuildArray(TypeSystemClang &ast_ctx, StringLexer &type,
                             bool for_expression);

  std::string ReadStructName(StringLexer &type);

  StructElement ReadStructElement(TypeSystemClang &ast_ctx, StringLexer &type,
                                  bool for_expression);

  clang::QualType BuildObjCObjectPointerType(TypeSystemClang &clang_ast_ctx,
                                             StringLexer &type,
                                             bool for_expression);

  uint32_t ReadNumber(StringLexer &type);

  std::string ReadQuotedString(StringLexer &type);

  ObjCLanguageRuntime &m_runtime;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCTYPEENCODINGPARSER_H
