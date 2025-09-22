//===- TableGenBackends.h - Declarations for Clang TableGen Backends ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations for all of the Clang TableGen
// backends. A "TableGen backend" is just a function. See
// "$LLVM_ROOT/utils/TableGen/TableGenBackends.h" for more info.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_UTILS_TABLEGEN_TABLEGENBACKENDS_H
#define LLVM_CLANG_UTILS_TABLEGEN_TABLEGENBACKENDS_H

#include <string>

namespace llvm {
class raw_ostream;
class RecordKeeper;
} // namespace llvm

namespace clang {

void EmitClangDeclContext(llvm::RecordKeeper &RK, llvm::raw_ostream &OS);
/**
  @param PriorizeIfSubclassOf These classes should be prioritized in the output.
  This is useful to force enum generation/jump tables/lookup tables to be more
  compact in both size and surrounding code in hot functions. An example use is
  in Decl for classes that inherit from DeclContext, for functions like
  castFromDeclContext.
  */
void EmitClangASTNodes(llvm::RecordKeeper &RK, llvm::raw_ostream &OS,
                       const std::string &N, const std::string &S,
                       std::string_view PriorizeIfSubclassOf = "");
void EmitClangBasicReader(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangBasicWriter(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangTypeNodes(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangTypeReader(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangTypeWriter(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrParserStringSwitches(llvm::RecordKeeper &Records,
                                       llvm::raw_ostream &OS);
void EmitClangAttrSubjectMatchRulesParserStringSwitches(
    llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrClass(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrImpl(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrList(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrSubjectMatchRuleList(llvm::RecordKeeper &Records,
                                       llvm::raw_ostream &OS);
void EmitClangAttrPCHRead(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangAttrPCHWrite(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangRegularKeywordAttributeInfo(llvm::RecordKeeper &Records,
                                          llvm::raw_ostream &OS);
void EmitClangAttrHasAttrImpl(llvm::RecordKeeper &Records,
                              llvm::raw_ostream &OS);
void EmitClangAttrSpellingListIndex(llvm::RecordKeeper &Records,
                                    llvm::raw_ostream &OS);
void EmitClangAttrASTVisitor(llvm::RecordKeeper &Records,
                             llvm::raw_ostream &OS);
void EmitClangAttrTemplateInstantiate(llvm::RecordKeeper &Records,
                                      llvm::raw_ostream &OS);
void EmitClangAttrParsedAttrList(llvm::RecordKeeper &Records,
                                 llvm::raw_ostream &OS);
void EmitClangAttrParsedAttrImpl(llvm::RecordKeeper &Records,
                                 llvm::raw_ostream &OS);
void EmitClangAttrParsedAttrKinds(llvm::RecordKeeper &Records,
                                  llvm::raw_ostream &OS);
void EmitClangAttrTextNodeDump(llvm::RecordKeeper &Records,
                               llvm::raw_ostream &OS);
void EmitClangAttrNodeTraverse(llvm::RecordKeeper &Records,
                               llvm::raw_ostream &OS);
void EmitClangAttrDocTable(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitClangBuiltins(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitClangDiagsDefs(llvm::RecordKeeper &Records, llvm::raw_ostream &OS,
                        const std::string &Component);
void EmitClangDiagGroups(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangDiagsIndexName(llvm::RecordKeeper &Records,
                             llvm::raw_ostream &OS);

void EmitClangSACheckers(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitClangCommentHTMLTags(llvm::RecordKeeper &Records,
                              llvm::raw_ostream &OS);
void EmitClangCommentHTMLTagsProperties(llvm::RecordKeeper &Records,
                                        llvm::raw_ostream &OS);
void EmitClangCommentHTMLNamedCharacterReferences(llvm::RecordKeeper &Records,
                                                  llvm::raw_ostream &OS);

void EmitClangCommentCommandInfo(llvm::RecordKeeper &Records,
                                 llvm::raw_ostream &OS);
void EmitClangCommentCommandList(llvm::RecordKeeper &Records,
                                 llvm::raw_ostream &OS);
void EmitClangOpcodes(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitClangSyntaxNodeList(llvm::RecordKeeper &Records,
                             llvm::raw_ostream &OS);
void EmitClangSyntaxNodeClasses(llvm::RecordKeeper &Records,
                                llvm::raw_ostream &OS);

void EmitNeon(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitFP16(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitBF16(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitNeonSema(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitVectorTypes(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitNeonTest(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitSveHeader(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitSveBuiltins(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitSveBuiltinCG(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitSveTypeFlags(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitSveRangeChecks(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitSveStreamingAttrs(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitSmeHeader(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitSmeBuiltins(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitSmeBuiltinCG(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitSmeRangeChecks(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitSmeStreamingAttrs(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitSmeBuiltinZAState(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitMveHeader(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitMveBuiltinDef(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitMveBuiltinSema(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitMveBuiltinCG(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitMveBuiltinAliases(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitRVVHeader(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitRVVBuiltins(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitRVVBuiltinCG(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitRVVBuiltinSema(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitCdeHeader(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitCdeBuiltinDef(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitCdeBuiltinSema(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitCdeBuiltinCG(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitCdeBuiltinAliases(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitClangAttrDocs(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangDiagDocs(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);
void EmitClangOptDocs(llvm::RecordKeeper &Records, llvm::raw_ostream &OS);

void EmitClangOpenCLBuiltins(llvm::RecordKeeper &Records,
                             llvm::raw_ostream &OS);
void EmitClangOpenCLBuiltinHeader(llvm::RecordKeeper &Records,
                                  llvm::raw_ostream &OS);
void EmitClangOpenCLBuiltinTests(llvm::RecordKeeper &Records,
                                 llvm::raw_ostream &OS);

void EmitClangDataCollectors(llvm::RecordKeeper &Records,
                             llvm::raw_ostream &OS);

void EmitTestPragmaAttributeSupportedAttributes(llvm::RecordKeeper &Records,
                                                llvm::raw_ostream &OS);

} // end namespace clang

#endif
