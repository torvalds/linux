//===--- Format.cpp - Format C++ code -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements functions declared in Format.h. This will be
/// split into separate files as we go.
///
//===----------------------------------------------------------------------===//

#include "clang/Format/Format.h"
#include "DefinitionBlockSeparator.h"
#include "IntegerLiteralSeparatorFixer.h"
#include "NamespaceEndCommentsFixer.h"
#include "ObjCPropertyAttributeOrderFixer.h"
#include "QualifierAlignmentFixer.h"
#include "SortJavaScriptImports.h"
#include "UnwrappedLineFormatter.h"
#include "UsingDeclarationsSorter.h"
#include "clang/Tooling/Inclusions/HeaderIncludes.h"
#include "llvm/ADT/Sequence.h"

#define DEBUG_TYPE "format-formatter"

using clang::format::FormatStyle;

LLVM_YAML_IS_SEQUENCE_VECTOR(FormatStyle::RawStringFormat)

namespace llvm {
namespace yaml {
template <>
struct ScalarEnumerationTraits<FormatStyle::BreakBeforeNoexceptSpecifierStyle> {
  static void
  enumeration(IO &IO, FormatStyle::BreakBeforeNoexceptSpecifierStyle &Value) {
    IO.enumCase(Value, "Never", FormatStyle::BBNSS_Never);
    IO.enumCase(Value, "OnlyWithParen", FormatStyle::BBNSS_OnlyWithParen);
    IO.enumCase(Value, "Always", FormatStyle::BBNSS_Always);
  }
};

template <> struct MappingTraits<FormatStyle::AlignConsecutiveStyle> {
  static void enumInput(IO &IO, FormatStyle::AlignConsecutiveStyle &Value) {
    IO.enumCase(Value, "None",
                FormatStyle::AlignConsecutiveStyle(
                    {/*Enabled=*/false, /*AcrossEmptyLines=*/false,
                     /*AcrossComments=*/false, /*AlignCompound=*/false,
                     /*AlignFunctionPointers=*/false, /*PadOperators=*/true}));
    IO.enumCase(Value, "Consecutive",
                FormatStyle::AlignConsecutiveStyle(
                    {/*Enabled=*/true, /*AcrossEmptyLines=*/false,
                     /*AcrossComments=*/false, /*AlignCompound=*/false,
                     /*AlignFunctionPointers=*/false, /*PadOperators=*/true}));
    IO.enumCase(Value, "AcrossEmptyLines",
                FormatStyle::AlignConsecutiveStyle(
                    {/*Enabled=*/true, /*AcrossEmptyLines=*/true,
                     /*AcrossComments=*/false, /*AlignCompound=*/false,
                     /*AlignFunctionPointers=*/false, /*PadOperators=*/true}));
    IO.enumCase(Value, "AcrossComments",
                FormatStyle::AlignConsecutiveStyle(
                    {/*Enabled=*/true, /*AcrossEmptyLines=*/false,
                     /*AcrossComments=*/true, /*AlignCompound=*/false,
                     /*AlignFunctionPointers=*/false, /*PadOperators=*/true}));
    IO.enumCase(Value, "AcrossEmptyLinesAndComments",
                FormatStyle::AlignConsecutiveStyle(
                    {/*Enabled=*/true, /*AcrossEmptyLines=*/true,
                     /*AcrossComments=*/true, /*AlignCompound=*/false,
                     /*AlignFunctionPointers=*/false, /*PadOperators=*/true}));

    // For backward compatibility.
    IO.enumCase(Value, "true",
                FormatStyle::AlignConsecutiveStyle(
                    {/*Enabled=*/true, /*AcrossEmptyLines=*/false,
                     /*AcrossComments=*/false, /*AlignCompound=*/false,
                     /*AlignFunctionPointers=*/false, /*PadOperators=*/true}));
    IO.enumCase(Value, "false",
                FormatStyle::AlignConsecutiveStyle(
                    {/*Enabled=*/false, /*AcrossEmptyLines=*/false,
                     /*AcrossComments=*/false, /*AlignCompound=*/false,
                     /*AlignFunctionPointers=*/false, /*PadOperators=*/true}));
  }

  static void mapping(IO &IO, FormatStyle::AlignConsecutiveStyle &Value) {
    IO.mapOptional("Enabled", Value.Enabled);
    IO.mapOptional("AcrossEmptyLines", Value.AcrossEmptyLines);
    IO.mapOptional("AcrossComments", Value.AcrossComments);
    IO.mapOptional("AlignCompound", Value.AlignCompound);
    IO.mapOptional("AlignFunctionPointers", Value.AlignFunctionPointers);
    IO.mapOptional("PadOperators", Value.PadOperators);
  }
};

template <>
struct MappingTraits<FormatStyle::ShortCaseStatementsAlignmentStyle> {
  static void mapping(IO &IO,
                      FormatStyle::ShortCaseStatementsAlignmentStyle &Value) {
    IO.mapOptional("Enabled", Value.Enabled);
    IO.mapOptional("AcrossEmptyLines", Value.AcrossEmptyLines);
    IO.mapOptional("AcrossComments", Value.AcrossComments);
    IO.mapOptional("AlignCaseArrows", Value.AlignCaseArrows);
    IO.mapOptional("AlignCaseColons", Value.AlignCaseColons);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::AttributeBreakingStyle> {
  static void enumeration(IO &IO, FormatStyle::AttributeBreakingStyle &Value) {
    IO.enumCase(Value, "Always", FormatStyle::ABS_Always);
    IO.enumCase(Value, "Leave", FormatStyle::ABS_Leave);
    IO.enumCase(Value, "Never", FormatStyle::ABS_Never);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::ArrayInitializerAlignmentStyle> {
  static void enumeration(IO &IO,
                          FormatStyle::ArrayInitializerAlignmentStyle &Value) {
    IO.enumCase(Value, "None", FormatStyle::AIAS_None);
    IO.enumCase(Value, "Left", FormatStyle::AIAS_Left);
    IO.enumCase(Value, "Right", FormatStyle::AIAS_Right);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::BinaryOperatorStyle> {
  static void enumeration(IO &IO, FormatStyle::BinaryOperatorStyle &Value) {
    IO.enumCase(Value, "All", FormatStyle::BOS_All);
    IO.enumCase(Value, "true", FormatStyle::BOS_All);
    IO.enumCase(Value, "None", FormatStyle::BOS_None);
    IO.enumCase(Value, "false", FormatStyle::BOS_None);
    IO.enumCase(Value, "NonAssignment", FormatStyle::BOS_NonAssignment);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::BinPackStyle> {
  static void enumeration(IO &IO, FormatStyle::BinPackStyle &Value) {
    IO.enumCase(Value, "Auto", FormatStyle::BPS_Auto);
    IO.enumCase(Value, "Always", FormatStyle::BPS_Always);
    IO.enumCase(Value, "Never", FormatStyle::BPS_Never);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::BitFieldColonSpacingStyle> {
  static void enumeration(IO &IO,
                          FormatStyle::BitFieldColonSpacingStyle &Value) {
    IO.enumCase(Value, "Both", FormatStyle::BFCS_Both);
    IO.enumCase(Value, "None", FormatStyle::BFCS_None);
    IO.enumCase(Value, "Before", FormatStyle::BFCS_Before);
    IO.enumCase(Value, "After", FormatStyle::BFCS_After);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::BraceBreakingStyle> {
  static void enumeration(IO &IO, FormatStyle::BraceBreakingStyle &Value) {
    IO.enumCase(Value, "Attach", FormatStyle::BS_Attach);
    IO.enumCase(Value, "Linux", FormatStyle::BS_Linux);
    IO.enumCase(Value, "Mozilla", FormatStyle::BS_Mozilla);
    IO.enumCase(Value, "Stroustrup", FormatStyle::BS_Stroustrup);
    IO.enumCase(Value, "Allman", FormatStyle::BS_Allman);
    IO.enumCase(Value, "Whitesmiths", FormatStyle::BS_Whitesmiths);
    IO.enumCase(Value, "GNU", FormatStyle::BS_GNU);
    IO.enumCase(Value, "WebKit", FormatStyle::BS_WebKit);
    IO.enumCase(Value, "Custom", FormatStyle::BS_Custom);
  }
};

template <> struct MappingTraits<FormatStyle::BraceWrappingFlags> {
  static void mapping(IO &IO, FormatStyle::BraceWrappingFlags &Wrapping) {
    IO.mapOptional("AfterCaseLabel", Wrapping.AfterCaseLabel);
    IO.mapOptional("AfterClass", Wrapping.AfterClass);
    IO.mapOptional("AfterControlStatement", Wrapping.AfterControlStatement);
    IO.mapOptional("AfterEnum", Wrapping.AfterEnum);
    IO.mapOptional("AfterExternBlock", Wrapping.AfterExternBlock);
    IO.mapOptional("AfterFunction", Wrapping.AfterFunction);
    IO.mapOptional("AfterNamespace", Wrapping.AfterNamespace);
    IO.mapOptional("AfterObjCDeclaration", Wrapping.AfterObjCDeclaration);
    IO.mapOptional("AfterStruct", Wrapping.AfterStruct);
    IO.mapOptional("AfterUnion", Wrapping.AfterUnion);
    IO.mapOptional("BeforeCatch", Wrapping.BeforeCatch);
    IO.mapOptional("BeforeElse", Wrapping.BeforeElse);
    IO.mapOptional("BeforeLambdaBody", Wrapping.BeforeLambdaBody);
    IO.mapOptional("BeforeWhile", Wrapping.BeforeWhile);
    IO.mapOptional("IndentBraces", Wrapping.IndentBraces);
    IO.mapOptional("SplitEmptyFunction", Wrapping.SplitEmptyFunction);
    IO.mapOptional("SplitEmptyRecord", Wrapping.SplitEmptyRecord);
    IO.mapOptional("SplitEmptyNamespace", Wrapping.SplitEmptyNamespace);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::BracketAlignmentStyle> {
  static void enumeration(IO &IO, FormatStyle::BracketAlignmentStyle &Value) {
    IO.enumCase(Value, "Align", FormatStyle::BAS_Align);
    IO.enumCase(Value, "DontAlign", FormatStyle::BAS_DontAlign);
    IO.enumCase(Value, "AlwaysBreak", FormatStyle::BAS_AlwaysBreak);
    IO.enumCase(Value, "BlockIndent", FormatStyle::BAS_BlockIndent);

    // For backward compatibility.
    IO.enumCase(Value, "true", FormatStyle::BAS_Align);
    IO.enumCase(Value, "false", FormatStyle::BAS_DontAlign);
  }
};

template <>
struct ScalarEnumerationTraits<
    FormatStyle::BraceWrappingAfterControlStatementStyle> {
  static void
  enumeration(IO &IO,
              FormatStyle::BraceWrappingAfterControlStatementStyle &Value) {
    IO.enumCase(Value, "Never", FormatStyle::BWACS_Never);
    IO.enumCase(Value, "MultiLine", FormatStyle::BWACS_MultiLine);
    IO.enumCase(Value, "Always", FormatStyle::BWACS_Always);

    // For backward compatibility.
    IO.enumCase(Value, "false", FormatStyle::BWACS_Never);
    IO.enumCase(Value, "true", FormatStyle::BWACS_Always);
  }
};

template <>
struct ScalarEnumerationTraits<
    FormatStyle::BreakBeforeConceptDeclarationsStyle> {
  static void
  enumeration(IO &IO, FormatStyle::BreakBeforeConceptDeclarationsStyle &Value) {
    IO.enumCase(Value, "Never", FormatStyle::BBCDS_Never);
    IO.enumCase(Value, "Allowed", FormatStyle::BBCDS_Allowed);
    IO.enumCase(Value, "Always", FormatStyle::BBCDS_Always);

    // For backward compatibility.
    IO.enumCase(Value, "true", FormatStyle::BBCDS_Always);
    IO.enumCase(Value, "false", FormatStyle::BBCDS_Allowed);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::BreakBeforeInlineASMColonStyle> {
  static void enumeration(IO &IO,
                          FormatStyle::BreakBeforeInlineASMColonStyle &Value) {
    IO.enumCase(Value, "Never", FormatStyle::BBIAS_Never);
    IO.enumCase(Value, "OnlyMultiline", FormatStyle::BBIAS_OnlyMultiline);
    IO.enumCase(Value, "Always", FormatStyle::BBIAS_Always);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::BreakConstructorInitializersStyle> {
  static void
  enumeration(IO &IO, FormatStyle::BreakConstructorInitializersStyle &Value) {
    IO.enumCase(Value, "BeforeColon", FormatStyle::BCIS_BeforeColon);
    IO.enumCase(Value, "BeforeComma", FormatStyle::BCIS_BeforeComma);
    IO.enumCase(Value, "AfterColon", FormatStyle::BCIS_AfterColon);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::BreakInheritanceListStyle> {
  static void enumeration(IO &IO,
                          FormatStyle::BreakInheritanceListStyle &Value) {
    IO.enumCase(Value, "BeforeColon", FormatStyle::BILS_BeforeColon);
    IO.enumCase(Value, "BeforeComma", FormatStyle::BILS_BeforeComma);
    IO.enumCase(Value, "AfterColon", FormatStyle::BILS_AfterColon);
    IO.enumCase(Value, "AfterComma", FormatStyle::BILS_AfterComma);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::BreakTemplateDeclarationsStyle> {
  static void enumeration(IO &IO,
                          FormatStyle::BreakTemplateDeclarationsStyle &Value) {
    IO.enumCase(Value, "Leave", FormatStyle::BTDS_Leave);
    IO.enumCase(Value, "No", FormatStyle::BTDS_No);
    IO.enumCase(Value, "MultiLine", FormatStyle::BTDS_MultiLine);
    IO.enumCase(Value, "Yes", FormatStyle::BTDS_Yes);

    // For backward compatibility.
    IO.enumCase(Value, "false", FormatStyle::BTDS_MultiLine);
    IO.enumCase(Value, "true", FormatStyle::BTDS_Yes);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::DAGArgStyle> {
  static void enumeration(IO &IO, FormatStyle::DAGArgStyle &Value) {
    IO.enumCase(Value, "DontBreak", FormatStyle::DAS_DontBreak);
    IO.enumCase(Value, "BreakElements", FormatStyle::DAS_BreakElements);
    IO.enumCase(Value, "BreakAll", FormatStyle::DAS_BreakAll);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::DefinitionReturnTypeBreakingStyle> {
  static void
  enumeration(IO &IO, FormatStyle::DefinitionReturnTypeBreakingStyle &Value) {
    IO.enumCase(Value, "None", FormatStyle::DRTBS_None);
    IO.enumCase(Value, "All", FormatStyle::DRTBS_All);
    IO.enumCase(Value, "TopLevel", FormatStyle::DRTBS_TopLevel);

    // For backward compatibility.
    IO.enumCase(Value, "false", FormatStyle::DRTBS_None);
    IO.enumCase(Value, "true", FormatStyle::DRTBS_All);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::EscapedNewlineAlignmentStyle> {
  static void enumeration(IO &IO,
                          FormatStyle::EscapedNewlineAlignmentStyle &Value) {
    IO.enumCase(Value, "DontAlign", FormatStyle::ENAS_DontAlign);
    IO.enumCase(Value, "Left", FormatStyle::ENAS_Left);
    IO.enumCase(Value, "LeftWithLastLine", FormatStyle::ENAS_LeftWithLastLine);
    IO.enumCase(Value, "Right", FormatStyle::ENAS_Right);

    // For backward compatibility.
    IO.enumCase(Value, "true", FormatStyle::ENAS_Left);
    IO.enumCase(Value, "false", FormatStyle::ENAS_Right);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::EmptyLineAfterAccessModifierStyle> {
  static void
  enumeration(IO &IO, FormatStyle::EmptyLineAfterAccessModifierStyle &Value) {
    IO.enumCase(Value, "Never", FormatStyle::ELAAMS_Never);
    IO.enumCase(Value, "Leave", FormatStyle::ELAAMS_Leave);
    IO.enumCase(Value, "Always", FormatStyle::ELAAMS_Always);
  }
};

template <>
struct ScalarEnumerationTraits<
    FormatStyle::EmptyLineBeforeAccessModifierStyle> {
  static void
  enumeration(IO &IO, FormatStyle::EmptyLineBeforeAccessModifierStyle &Value) {
    IO.enumCase(Value, "Never", FormatStyle::ELBAMS_Never);
    IO.enumCase(Value, "Leave", FormatStyle::ELBAMS_Leave);
    IO.enumCase(Value, "LogicalBlock", FormatStyle::ELBAMS_LogicalBlock);
    IO.enumCase(Value, "Always", FormatStyle::ELBAMS_Always);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::IndentExternBlockStyle> {
  static void enumeration(IO &IO, FormatStyle::IndentExternBlockStyle &Value) {
    IO.enumCase(Value, "AfterExternBlock", FormatStyle::IEBS_AfterExternBlock);
    IO.enumCase(Value, "Indent", FormatStyle::IEBS_Indent);
    IO.enumCase(Value, "NoIndent", FormatStyle::IEBS_NoIndent);
    IO.enumCase(Value, "true", FormatStyle::IEBS_Indent);
    IO.enumCase(Value, "false", FormatStyle::IEBS_NoIndent);
  }
};

template <> struct MappingTraits<FormatStyle::IntegerLiteralSeparatorStyle> {
  static void mapping(IO &IO, FormatStyle::IntegerLiteralSeparatorStyle &Base) {
    IO.mapOptional("Binary", Base.Binary);
    IO.mapOptional("BinaryMinDigits", Base.BinaryMinDigits);
    IO.mapOptional("Decimal", Base.Decimal);
    IO.mapOptional("DecimalMinDigits", Base.DecimalMinDigits);
    IO.mapOptional("Hex", Base.Hex);
    IO.mapOptional("HexMinDigits", Base.HexMinDigits);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::JavaScriptQuoteStyle> {
  static void enumeration(IO &IO, FormatStyle::JavaScriptQuoteStyle &Value) {
    IO.enumCase(Value, "Leave", FormatStyle::JSQS_Leave);
    IO.enumCase(Value, "Single", FormatStyle::JSQS_Single);
    IO.enumCase(Value, "Double", FormatStyle::JSQS_Double);
  }
};

template <> struct MappingTraits<FormatStyle::KeepEmptyLinesStyle> {
  static void mapping(IO &IO, FormatStyle::KeepEmptyLinesStyle &Value) {
    IO.mapOptional("AtEndOfFile", Value.AtEndOfFile);
    IO.mapOptional("AtStartOfBlock", Value.AtStartOfBlock);
    IO.mapOptional("AtStartOfFile", Value.AtStartOfFile);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::LanguageKind> {
  static void enumeration(IO &IO, FormatStyle::LanguageKind &Value) {
    IO.enumCase(Value, "Cpp", FormatStyle::LK_Cpp);
    IO.enumCase(Value, "Java", FormatStyle::LK_Java);
    IO.enumCase(Value, "JavaScript", FormatStyle::LK_JavaScript);
    IO.enumCase(Value, "ObjC", FormatStyle::LK_ObjC);
    IO.enumCase(Value, "Proto", FormatStyle::LK_Proto);
    IO.enumCase(Value, "TableGen", FormatStyle::LK_TableGen);
    IO.enumCase(Value, "TextProto", FormatStyle::LK_TextProto);
    IO.enumCase(Value, "CSharp", FormatStyle::LK_CSharp);
    IO.enumCase(Value, "Json", FormatStyle::LK_Json);
    IO.enumCase(Value, "Verilog", FormatStyle::LK_Verilog);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::LanguageStandard> {
  static void enumeration(IO &IO, FormatStyle::LanguageStandard &Value) {
    IO.enumCase(Value, "c++03", FormatStyle::LS_Cpp03);
    IO.enumCase(Value, "C++03", FormatStyle::LS_Cpp03); // Legacy alias
    IO.enumCase(Value, "Cpp03", FormatStyle::LS_Cpp03); // Legacy alias

    IO.enumCase(Value, "c++11", FormatStyle::LS_Cpp11);
    IO.enumCase(Value, "C++11", FormatStyle::LS_Cpp11); // Legacy alias

    IO.enumCase(Value, "c++14", FormatStyle::LS_Cpp14);
    IO.enumCase(Value, "c++17", FormatStyle::LS_Cpp17);
    IO.enumCase(Value, "c++20", FormatStyle::LS_Cpp20);

    IO.enumCase(Value, "Latest", FormatStyle::LS_Latest);
    IO.enumCase(Value, "Cpp11", FormatStyle::LS_Latest); // Legacy alias
    IO.enumCase(Value, "Auto", FormatStyle::LS_Auto);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::LambdaBodyIndentationKind> {
  static void enumeration(IO &IO,
                          FormatStyle::LambdaBodyIndentationKind &Value) {
    IO.enumCase(Value, "Signature", FormatStyle::LBI_Signature);
    IO.enumCase(Value, "OuterScope", FormatStyle::LBI_OuterScope);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::LineEndingStyle> {
  static void enumeration(IO &IO, FormatStyle::LineEndingStyle &Value) {
    IO.enumCase(Value, "LF", FormatStyle::LE_LF);
    IO.enumCase(Value, "CRLF", FormatStyle::LE_CRLF);
    IO.enumCase(Value, "DeriveLF", FormatStyle::LE_DeriveLF);
    IO.enumCase(Value, "DeriveCRLF", FormatStyle::LE_DeriveCRLF);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::NamespaceIndentationKind> {
  static void enumeration(IO &IO,
                          FormatStyle::NamespaceIndentationKind &Value) {
    IO.enumCase(Value, "None", FormatStyle::NI_None);
    IO.enumCase(Value, "Inner", FormatStyle::NI_Inner);
    IO.enumCase(Value, "All", FormatStyle::NI_All);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::OperandAlignmentStyle> {
  static void enumeration(IO &IO, FormatStyle::OperandAlignmentStyle &Value) {
    IO.enumCase(Value, "DontAlign", FormatStyle::OAS_DontAlign);
    IO.enumCase(Value, "Align", FormatStyle::OAS_Align);
    IO.enumCase(Value, "AlignAfterOperator",
                FormatStyle::OAS_AlignAfterOperator);

    // For backward compatibility.
    IO.enumCase(Value, "true", FormatStyle::OAS_Align);
    IO.enumCase(Value, "false", FormatStyle::OAS_DontAlign);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::PackConstructorInitializersStyle> {
  static void
  enumeration(IO &IO, FormatStyle::PackConstructorInitializersStyle &Value) {
    IO.enumCase(Value, "Never", FormatStyle::PCIS_Never);
    IO.enumCase(Value, "BinPack", FormatStyle::PCIS_BinPack);
    IO.enumCase(Value, "CurrentLine", FormatStyle::PCIS_CurrentLine);
    IO.enumCase(Value, "NextLine", FormatStyle::PCIS_NextLine);
    IO.enumCase(Value, "NextLineOnly", FormatStyle::PCIS_NextLineOnly);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::PointerAlignmentStyle> {
  static void enumeration(IO &IO, FormatStyle::PointerAlignmentStyle &Value) {
    IO.enumCase(Value, "Middle", FormatStyle::PAS_Middle);
    IO.enumCase(Value, "Left", FormatStyle::PAS_Left);
    IO.enumCase(Value, "Right", FormatStyle::PAS_Right);

    // For backward compatibility.
    IO.enumCase(Value, "true", FormatStyle::PAS_Left);
    IO.enumCase(Value, "false", FormatStyle::PAS_Right);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::PPDirectiveIndentStyle> {
  static void enumeration(IO &IO, FormatStyle::PPDirectiveIndentStyle &Value) {
    IO.enumCase(Value, "None", FormatStyle::PPDIS_None);
    IO.enumCase(Value, "AfterHash", FormatStyle::PPDIS_AfterHash);
    IO.enumCase(Value, "BeforeHash", FormatStyle::PPDIS_BeforeHash);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::QualifierAlignmentStyle> {
  static void enumeration(IO &IO, FormatStyle::QualifierAlignmentStyle &Value) {
    IO.enumCase(Value, "Leave", FormatStyle::QAS_Leave);
    IO.enumCase(Value, "Left", FormatStyle::QAS_Left);
    IO.enumCase(Value, "Right", FormatStyle::QAS_Right);
    IO.enumCase(Value, "Custom", FormatStyle::QAS_Custom);
  }
};

template <> struct MappingTraits<FormatStyle::RawStringFormat> {
  static void mapping(IO &IO, FormatStyle::RawStringFormat &Format) {
    IO.mapOptional("Language", Format.Language);
    IO.mapOptional("Delimiters", Format.Delimiters);
    IO.mapOptional("EnclosingFunctions", Format.EnclosingFunctions);
    IO.mapOptional("CanonicalDelimiter", Format.CanonicalDelimiter);
    IO.mapOptional("BasedOnStyle", Format.BasedOnStyle);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::ReferenceAlignmentStyle> {
  static void enumeration(IO &IO, FormatStyle::ReferenceAlignmentStyle &Value) {
    IO.enumCase(Value, "Pointer", FormatStyle::RAS_Pointer);
    IO.enumCase(Value, "Middle", FormatStyle::RAS_Middle);
    IO.enumCase(Value, "Left", FormatStyle::RAS_Left);
    IO.enumCase(Value, "Right", FormatStyle::RAS_Right);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::RemoveParenthesesStyle> {
  static void enumeration(IO &IO, FormatStyle::RemoveParenthesesStyle &Value) {
    IO.enumCase(Value, "Leave", FormatStyle::RPS_Leave);
    IO.enumCase(Value, "MultipleParentheses",
                FormatStyle::RPS_MultipleParentheses);
    IO.enumCase(Value, "ReturnStatement", FormatStyle::RPS_ReturnStatement);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::RequiresClausePositionStyle> {
  static void enumeration(IO &IO,
                          FormatStyle::RequiresClausePositionStyle &Value) {
    IO.enumCase(Value, "OwnLine", FormatStyle::RCPS_OwnLine);
    IO.enumCase(Value, "WithPreceding", FormatStyle::RCPS_WithPreceding);
    IO.enumCase(Value, "WithFollowing", FormatStyle::RCPS_WithFollowing);
    IO.enumCase(Value, "SingleLine", FormatStyle::RCPS_SingleLine);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::RequiresExpressionIndentationKind> {
  static void
  enumeration(IO &IO, FormatStyle::RequiresExpressionIndentationKind &Value) {
    IO.enumCase(Value, "Keyword", FormatStyle::REI_Keyword);
    IO.enumCase(Value, "OuterScope", FormatStyle::REI_OuterScope);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::ReturnTypeBreakingStyle> {
  static void enumeration(IO &IO, FormatStyle::ReturnTypeBreakingStyle &Value) {
    IO.enumCase(Value, "None", FormatStyle::RTBS_None);
    IO.enumCase(Value, "Automatic", FormatStyle::RTBS_Automatic);
    IO.enumCase(Value, "ExceptShortType", FormatStyle::RTBS_ExceptShortType);
    IO.enumCase(Value, "All", FormatStyle::RTBS_All);
    IO.enumCase(Value, "TopLevel", FormatStyle::RTBS_TopLevel);
    IO.enumCase(Value, "TopLevelDefinitions",
                FormatStyle::RTBS_TopLevelDefinitions);
    IO.enumCase(Value, "AllDefinitions", FormatStyle::RTBS_AllDefinitions);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::SeparateDefinitionStyle> {
  static void enumeration(IO &IO, FormatStyle::SeparateDefinitionStyle &Value) {
    IO.enumCase(Value, "Leave", FormatStyle::SDS_Leave);
    IO.enumCase(Value, "Always", FormatStyle::SDS_Always);
    IO.enumCase(Value, "Never", FormatStyle::SDS_Never);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::ShortBlockStyle> {
  static void enumeration(IO &IO, FormatStyle::ShortBlockStyle &Value) {
    IO.enumCase(Value, "Never", FormatStyle::SBS_Never);
    IO.enumCase(Value, "false", FormatStyle::SBS_Never);
    IO.enumCase(Value, "Always", FormatStyle::SBS_Always);
    IO.enumCase(Value, "true", FormatStyle::SBS_Always);
    IO.enumCase(Value, "Empty", FormatStyle::SBS_Empty);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::ShortFunctionStyle> {
  static void enumeration(IO &IO, FormatStyle::ShortFunctionStyle &Value) {
    IO.enumCase(Value, "None", FormatStyle::SFS_None);
    IO.enumCase(Value, "false", FormatStyle::SFS_None);
    IO.enumCase(Value, "All", FormatStyle::SFS_All);
    IO.enumCase(Value, "true", FormatStyle::SFS_All);
    IO.enumCase(Value, "Inline", FormatStyle::SFS_Inline);
    IO.enumCase(Value, "InlineOnly", FormatStyle::SFS_InlineOnly);
    IO.enumCase(Value, "Empty", FormatStyle::SFS_Empty);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::ShortIfStyle> {
  static void enumeration(IO &IO, FormatStyle::ShortIfStyle &Value) {
    IO.enumCase(Value, "Never", FormatStyle::SIS_Never);
    IO.enumCase(Value, "WithoutElse", FormatStyle::SIS_WithoutElse);
    IO.enumCase(Value, "OnlyFirstIf", FormatStyle::SIS_OnlyFirstIf);
    IO.enumCase(Value, "AllIfsAndElse", FormatStyle::SIS_AllIfsAndElse);

    // For backward compatibility.
    IO.enumCase(Value, "Always", FormatStyle::SIS_OnlyFirstIf);
    IO.enumCase(Value, "false", FormatStyle::SIS_Never);
    IO.enumCase(Value, "true", FormatStyle::SIS_WithoutElse);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::ShortLambdaStyle> {
  static void enumeration(IO &IO, FormatStyle::ShortLambdaStyle &Value) {
    IO.enumCase(Value, "None", FormatStyle::SLS_None);
    IO.enumCase(Value, "false", FormatStyle::SLS_None);
    IO.enumCase(Value, "Empty", FormatStyle::SLS_Empty);
    IO.enumCase(Value, "Inline", FormatStyle::SLS_Inline);
    IO.enumCase(Value, "All", FormatStyle::SLS_All);
    IO.enumCase(Value, "true", FormatStyle::SLS_All);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::SortIncludesOptions> {
  static void enumeration(IO &IO, FormatStyle::SortIncludesOptions &Value) {
    IO.enumCase(Value, "Never", FormatStyle::SI_Never);
    IO.enumCase(Value, "CaseInsensitive", FormatStyle::SI_CaseInsensitive);
    IO.enumCase(Value, "CaseSensitive", FormatStyle::SI_CaseSensitive);

    // For backward compatibility.
    IO.enumCase(Value, "false", FormatStyle::SI_Never);
    IO.enumCase(Value, "true", FormatStyle::SI_CaseSensitive);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::SortJavaStaticImportOptions> {
  static void enumeration(IO &IO,
                          FormatStyle::SortJavaStaticImportOptions &Value) {
    IO.enumCase(Value, "Before", FormatStyle::SJSIO_Before);
    IO.enumCase(Value, "After", FormatStyle::SJSIO_After);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::SortUsingDeclarationsOptions> {
  static void enumeration(IO &IO,
                          FormatStyle::SortUsingDeclarationsOptions &Value) {
    IO.enumCase(Value, "Never", FormatStyle::SUD_Never);
    IO.enumCase(Value, "Lexicographic", FormatStyle::SUD_Lexicographic);
    IO.enumCase(Value, "LexicographicNumeric",
                FormatStyle::SUD_LexicographicNumeric);

    // For backward compatibility.
    IO.enumCase(Value, "false", FormatStyle::SUD_Never);
    IO.enumCase(Value, "true", FormatStyle::SUD_LexicographicNumeric);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::SpaceAroundPointerQualifiersStyle> {
  static void
  enumeration(IO &IO, FormatStyle::SpaceAroundPointerQualifiersStyle &Value) {
    IO.enumCase(Value, "Default", FormatStyle::SAPQ_Default);
    IO.enumCase(Value, "Before", FormatStyle::SAPQ_Before);
    IO.enumCase(Value, "After", FormatStyle::SAPQ_After);
    IO.enumCase(Value, "Both", FormatStyle::SAPQ_Both);
  }
};

template <> struct MappingTraits<FormatStyle::SpaceBeforeParensCustom> {
  static void mapping(IO &IO, FormatStyle::SpaceBeforeParensCustom &Spacing) {
    IO.mapOptional("AfterControlStatements", Spacing.AfterControlStatements);
    IO.mapOptional("AfterForeachMacros", Spacing.AfterForeachMacros);
    IO.mapOptional("AfterFunctionDefinitionName",
                   Spacing.AfterFunctionDefinitionName);
    IO.mapOptional("AfterFunctionDeclarationName",
                   Spacing.AfterFunctionDeclarationName);
    IO.mapOptional("AfterIfMacros", Spacing.AfterIfMacros);
    IO.mapOptional("AfterOverloadedOperator", Spacing.AfterOverloadedOperator);
    IO.mapOptional("AfterPlacementOperator", Spacing.AfterPlacementOperator);
    IO.mapOptional("AfterRequiresInClause", Spacing.AfterRequiresInClause);
    IO.mapOptional("AfterRequiresInExpression",
                   Spacing.AfterRequiresInExpression);
    IO.mapOptional("BeforeNonEmptyParentheses",
                   Spacing.BeforeNonEmptyParentheses);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::SpaceBeforeParensStyle> {
  static void enumeration(IO &IO, FormatStyle::SpaceBeforeParensStyle &Value) {
    IO.enumCase(Value, "Never", FormatStyle::SBPO_Never);
    IO.enumCase(Value, "ControlStatements",
                FormatStyle::SBPO_ControlStatements);
    IO.enumCase(Value, "ControlStatementsExceptControlMacros",
                FormatStyle::SBPO_ControlStatementsExceptControlMacros);
    IO.enumCase(Value, "NonEmptyParentheses",
                FormatStyle::SBPO_NonEmptyParentheses);
    IO.enumCase(Value, "Always", FormatStyle::SBPO_Always);
    IO.enumCase(Value, "Custom", FormatStyle::SBPO_Custom);

    // For backward compatibility.
    IO.enumCase(Value, "false", FormatStyle::SBPO_Never);
    IO.enumCase(Value, "true", FormatStyle::SBPO_ControlStatements);
    IO.enumCase(Value, "ControlStatementsExceptForEachMacros",
                FormatStyle::SBPO_ControlStatementsExceptControlMacros);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::SpacesInAnglesStyle> {
  static void enumeration(IO &IO, FormatStyle::SpacesInAnglesStyle &Value) {
    IO.enumCase(Value, "Never", FormatStyle::SIAS_Never);
    IO.enumCase(Value, "Always", FormatStyle::SIAS_Always);
    IO.enumCase(Value, "Leave", FormatStyle::SIAS_Leave);

    // For backward compatibility.
    IO.enumCase(Value, "false", FormatStyle::SIAS_Never);
    IO.enumCase(Value, "true", FormatStyle::SIAS_Always);
  }
};

template <> struct MappingTraits<FormatStyle::SpacesInLineComment> {
  static void mapping(IO &IO, FormatStyle::SpacesInLineComment &Space) {
    // Transform the maximum to signed, to parse "-1" correctly
    int signedMaximum = static_cast<int>(Space.Maximum);
    IO.mapOptional("Minimum", Space.Minimum);
    IO.mapOptional("Maximum", signedMaximum);
    Space.Maximum = static_cast<unsigned>(signedMaximum);

    if (Space.Maximum != -1u)
      Space.Minimum = std::min(Space.Minimum, Space.Maximum);
  }
};

template <> struct MappingTraits<FormatStyle::SpacesInParensCustom> {
  static void mapping(IO &IO, FormatStyle::SpacesInParensCustom &Spaces) {
    IO.mapOptional("ExceptDoubleParentheses", Spaces.ExceptDoubleParentheses);
    IO.mapOptional("InCStyleCasts", Spaces.InCStyleCasts);
    IO.mapOptional("InConditionalStatements", Spaces.InConditionalStatements);
    IO.mapOptional("InEmptyParentheses", Spaces.InEmptyParentheses);
    IO.mapOptional("Other", Spaces.Other);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::SpacesInParensStyle> {
  static void enumeration(IO &IO, FormatStyle::SpacesInParensStyle &Value) {
    IO.enumCase(Value, "Never", FormatStyle::SIPO_Never);
    IO.enumCase(Value, "Custom", FormatStyle::SIPO_Custom);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::TrailingCommaStyle> {
  static void enumeration(IO &IO, FormatStyle::TrailingCommaStyle &Value) {
    IO.enumCase(Value, "None", FormatStyle::TCS_None);
    IO.enumCase(Value, "Wrapped", FormatStyle::TCS_Wrapped);
  }
};

template <>
struct ScalarEnumerationTraits<FormatStyle::TrailingCommentsAlignmentKinds> {
  static void enumeration(IO &IO,
                          FormatStyle::TrailingCommentsAlignmentKinds &Value) {
    IO.enumCase(Value, "Leave", FormatStyle::TCAS_Leave);
    IO.enumCase(Value, "Always", FormatStyle::TCAS_Always);
    IO.enumCase(Value, "Never", FormatStyle::TCAS_Never);
  }
};

template <> struct MappingTraits<FormatStyle::TrailingCommentsAlignmentStyle> {
  static void enumInput(IO &IO,
                        FormatStyle::TrailingCommentsAlignmentStyle &Value) {
    IO.enumCase(Value, "Leave",
                FormatStyle::TrailingCommentsAlignmentStyle(
                    {FormatStyle::TCAS_Leave, 0}));

    IO.enumCase(Value, "Always",
                FormatStyle::TrailingCommentsAlignmentStyle(
                    {FormatStyle::TCAS_Always, 0}));

    IO.enumCase(Value, "Never",
                FormatStyle::TrailingCommentsAlignmentStyle(
                    {FormatStyle::TCAS_Never, 0}));

    // For backwards compatibility
    IO.enumCase(Value, "true",
                FormatStyle::TrailingCommentsAlignmentStyle(
                    {FormatStyle::TCAS_Always, 0}));
    IO.enumCase(Value, "false",
                FormatStyle::TrailingCommentsAlignmentStyle(
                    {FormatStyle::TCAS_Never, 0}));
  }

  static void mapping(IO &IO,
                      FormatStyle::TrailingCommentsAlignmentStyle &Value) {
    IO.mapOptional("Kind", Value.Kind);
    IO.mapOptional("OverEmptyLines", Value.OverEmptyLines);
  }
};

template <> struct ScalarEnumerationTraits<FormatStyle::UseTabStyle> {
  static void enumeration(IO &IO, FormatStyle::UseTabStyle &Value) {
    IO.enumCase(Value, "Never", FormatStyle::UT_Never);
    IO.enumCase(Value, "false", FormatStyle::UT_Never);
    IO.enumCase(Value, "Always", FormatStyle::UT_Always);
    IO.enumCase(Value, "true", FormatStyle::UT_Always);
    IO.enumCase(Value, "ForIndentation", FormatStyle::UT_ForIndentation);
    IO.enumCase(Value, "ForContinuationAndIndentation",
                FormatStyle::UT_ForContinuationAndIndentation);
    IO.enumCase(Value, "AlignWithSpaces", FormatStyle::UT_AlignWithSpaces);
  }
};

template <> struct MappingTraits<FormatStyle> {
  static void mapping(IO &IO, FormatStyle &Style) {
    // When reading, read the language first, we need it for getPredefinedStyle.
    IO.mapOptional("Language", Style.Language);

    StringRef BasedOnStyle;
    if (IO.outputting()) {
      StringRef Styles[] = {"LLVM",   "Google", "Chromium",  "Mozilla",
                            "WebKit", "GNU",    "Microsoft", "clang-format"};
      for (StringRef StyleName : Styles) {
        FormatStyle PredefinedStyle;
        if (getPredefinedStyle(StyleName, Style.Language, &PredefinedStyle) &&
            Style == PredefinedStyle) {
          BasedOnStyle = StyleName;
          break;
        }
      }
    } else {
      IO.mapOptional("BasedOnStyle", BasedOnStyle);
      if (!BasedOnStyle.empty()) {
        FormatStyle::LanguageKind OldLanguage = Style.Language;
        FormatStyle::LanguageKind Language =
            ((FormatStyle *)IO.getContext())->Language;
        if (!getPredefinedStyle(BasedOnStyle, Language, &Style)) {
          IO.setError(Twine("Unknown value for BasedOnStyle: ", BasedOnStyle));
          return;
        }
        Style.Language = OldLanguage;
      }
    }

    // Initialize some variables used in the parsing. The using logic is at the
    // end.

    // For backward compatibility:
    // The default value of ConstructorInitializerAllOnOneLineOrOnePerLine was
    // false unless BasedOnStyle was Google or Chromium whereas that of
    // AllowAllConstructorInitializersOnNextLine was always true, so the
    // equivalent default value of PackConstructorInitializers is PCIS_NextLine
    // for Google/Chromium or PCIS_BinPack otherwise. If the deprecated options
    // had a non-default value while PackConstructorInitializers has a default
    // value, set the latter to an equivalent non-default value if needed.
    const bool IsGoogleOrChromium = BasedOnStyle.equals_insensitive("google") ||
                                    BasedOnStyle.equals_insensitive("chromium");
    bool OnCurrentLine = IsGoogleOrChromium;
    bool OnNextLine = true;

    bool BreakBeforeInheritanceComma = false;
    bool BreakConstructorInitializersBeforeComma = false;

    bool DeriveLineEnding = true;
    bool UseCRLF = false;

    bool SpaceInEmptyParentheses = false;
    bool SpacesInConditionalStatement = false;
    bool SpacesInCStyleCastParentheses = false;
    bool SpacesInParentheses = false;

    // For backward compatibility.
    if (!IO.outputting()) {
      IO.mapOptional("AlignEscapedNewlinesLeft", Style.AlignEscapedNewlines);
      IO.mapOptional("AllowAllConstructorInitializersOnNextLine", OnNextLine);
      IO.mapOptional("AlwaysBreakAfterReturnType", Style.BreakAfterReturnType);
      IO.mapOptional("AlwaysBreakTemplateDeclarations",
                     Style.BreakTemplateDeclarations);
      IO.mapOptional("BreakBeforeInheritanceComma",
                     BreakBeforeInheritanceComma);
      IO.mapOptional("BreakConstructorInitializersBeforeComma",
                     BreakConstructorInitializersBeforeComma);
      IO.mapOptional("ConstructorInitializerAllOnOneLineOrOnePerLine",
                     OnCurrentLine);
      IO.mapOptional("DeriveLineEnding", DeriveLineEnding);
      IO.mapOptional("DerivePointerBinding", Style.DerivePointerAlignment);
      IO.mapOptional("KeepEmptyLinesAtEOF", Style.KeepEmptyLines.AtEndOfFile);
      IO.mapOptional("KeepEmptyLinesAtTheStartOfBlocks",
                     Style.KeepEmptyLines.AtStartOfBlock);
      IO.mapOptional("IndentFunctionDeclarationAfterType",
                     Style.IndentWrappedFunctionNames);
      IO.mapOptional("IndentRequires", Style.IndentRequiresClause);
      IO.mapOptional("PointerBindsToType", Style.PointerAlignment);
      IO.mapOptional("SpaceAfterControlStatementKeyword",
                     Style.SpaceBeforeParens);
      IO.mapOptional("SpaceInEmptyParentheses", SpaceInEmptyParentheses);
      IO.mapOptional("SpacesInConditionalStatement",
                     SpacesInConditionalStatement);
      IO.mapOptional("SpacesInCStyleCastParentheses",
                     SpacesInCStyleCastParentheses);
      IO.mapOptional("SpacesInParentheses", SpacesInParentheses);
      IO.mapOptional("UseCRLF", UseCRLF);
    }

    IO.mapOptional("AccessModifierOffset", Style.AccessModifierOffset);
    IO.mapOptional("AlignAfterOpenBracket", Style.AlignAfterOpenBracket);
    IO.mapOptional("AlignArrayOfStructures", Style.AlignArrayOfStructures);
    IO.mapOptional("AlignConsecutiveAssignments",
                   Style.AlignConsecutiveAssignments);
    IO.mapOptional("AlignConsecutiveBitFields",
                   Style.AlignConsecutiveBitFields);
    IO.mapOptional("AlignConsecutiveDeclarations",
                   Style.AlignConsecutiveDeclarations);
    IO.mapOptional("AlignConsecutiveMacros", Style.AlignConsecutiveMacros);
    IO.mapOptional("AlignConsecutiveShortCaseStatements",
                   Style.AlignConsecutiveShortCaseStatements);
    IO.mapOptional("AlignConsecutiveTableGenBreakingDAGArgColons",
                   Style.AlignConsecutiveTableGenBreakingDAGArgColons);
    IO.mapOptional("AlignConsecutiveTableGenCondOperatorColons",
                   Style.AlignConsecutiveTableGenCondOperatorColons);
    IO.mapOptional("AlignConsecutiveTableGenDefinitionColons",
                   Style.AlignConsecutiveTableGenDefinitionColons);
    IO.mapOptional("AlignEscapedNewlines", Style.AlignEscapedNewlines);
    IO.mapOptional("AlignOperands", Style.AlignOperands);
    IO.mapOptional("AlignTrailingComments", Style.AlignTrailingComments);
    IO.mapOptional("AllowAllArgumentsOnNextLine",
                   Style.AllowAllArgumentsOnNextLine);
    IO.mapOptional("AllowAllParametersOfDeclarationOnNextLine",
                   Style.AllowAllParametersOfDeclarationOnNextLine);
    IO.mapOptional("AllowBreakBeforeNoexceptSpecifier",
                   Style.AllowBreakBeforeNoexceptSpecifier);
    IO.mapOptional("AllowShortBlocksOnASingleLine",
                   Style.AllowShortBlocksOnASingleLine);
    IO.mapOptional("AllowShortCaseExpressionOnASingleLine",
                   Style.AllowShortCaseExpressionOnASingleLine);
    IO.mapOptional("AllowShortCaseLabelsOnASingleLine",
                   Style.AllowShortCaseLabelsOnASingleLine);
    IO.mapOptional("AllowShortCompoundRequirementOnASingleLine",
                   Style.AllowShortCompoundRequirementOnASingleLine);
    IO.mapOptional("AllowShortEnumsOnASingleLine",
                   Style.AllowShortEnumsOnASingleLine);
    IO.mapOptional("AllowShortFunctionsOnASingleLine",
                   Style.AllowShortFunctionsOnASingleLine);
    IO.mapOptional("AllowShortIfStatementsOnASingleLine",
                   Style.AllowShortIfStatementsOnASingleLine);
    IO.mapOptional("AllowShortLambdasOnASingleLine",
                   Style.AllowShortLambdasOnASingleLine);
    IO.mapOptional("AllowShortLoopsOnASingleLine",
                   Style.AllowShortLoopsOnASingleLine);
    IO.mapOptional("AlwaysBreakAfterDefinitionReturnType",
                   Style.AlwaysBreakAfterDefinitionReturnType);
    IO.mapOptional("AlwaysBreakBeforeMultilineStrings",
                   Style.AlwaysBreakBeforeMultilineStrings);
    IO.mapOptional("AttributeMacros", Style.AttributeMacros);
    IO.mapOptional("BinPackArguments", Style.BinPackArguments);
    IO.mapOptional("BinPackParameters", Style.BinPackParameters);
    IO.mapOptional("BitFieldColonSpacing", Style.BitFieldColonSpacing);
    IO.mapOptional("BracedInitializerIndentWidth",
                   Style.BracedInitializerIndentWidth);
    IO.mapOptional("BraceWrapping", Style.BraceWrapping);
    IO.mapOptional("BreakAdjacentStringLiterals",
                   Style.BreakAdjacentStringLiterals);
    IO.mapOptional("BreakAfterAttributes", Style.BreakAfterAttributes);
    IO.mapOptional("BreakAfterJavaFieldAnnotations",
                   Style.BreakAfterJavaFieldAnnotations);
    IO.mapOptional("BreakAfterReturnType", Style.BreakAfterReturnType);
    IO.mapOptional("BreakArrays", Style.BreakArrays);
    IO.mapOptional("BreakBeforeBinaryOperators",
                   Style.BreakBeforeBinaryOperators);
    IO.mapOptional("BreakBeforeConceptDeclarations",
                   Style.BreakBeforeConceptDeclarations);
    IO.mapOptional("BreakBeforeBraces", Style.BreakBeforeBraces);
    IO.mapOptional("BreakBeforeInlineASMColon",
                   Style.BreakBeforeInlineASMColon);
    IO.mapOptional("BreakBeforeTernaryOperators",
                   Style.BreakBeforeTernaryOperators);
    IO.mapOptional("BreakConstructorInitializers",
                   Style.BreakConstructorInitializers);
    IO.mapOptional("BreakFunctionDefinitionParameters",
                   Style.BreakFunctionDefinitionParameters);
    IO.mapOptional("BreakInheritanceList", Style.BreakInheritanceList);
    IO.mapOptional("BreakStringLiterals", Style.BreakStringLiterals);
    IO.mapOptional("BreakTemplateDeclarations",
                   Style.BreakTemplateDeclarations);
    IO.mapOptional("ColumnLimit", Style.ColumnLimit);
    IO.mapOptional("CommentPragmas", Style.CommentPragmas);
    IO.mapOptional("CompactNamespaces", Style.CompactNamespaces);
    IO.mapOptional("ConstructorInitializerIndentWidth",
                   Style.ConstructorInitializerIndentWidth);
    IO.mapOptional("ContinuationIndentWidth", Style.ContinuationIndentWidth);
    IO.mapOptional("Cpp11BracedListStyle", Style.Cpp11BracedListStyle);
    IO.mapOptional("DerivePointerAlignment", Style.DerivePointerAlignment);
    IO.mapOptional("DisableFormat", Style.DisableFormat);
    IO.mapOptional("EmptyLineAfterAccessModifier",
                   Style.EmptyLineAfterAccessModifier);
    IO.mapOptional("EmptyLineBeforeAccessModifier",
                   Style.EmptyLineBeforeAccessModifier);
    IO.mapOptional("ExperimentalAutoDetectBinPacking",
                   Style.ExperimentalAutoDetectBinPacking);
    IO.mapOptional("FixNamespaceComments", Style.FixNamespaceComments);
    IO.mapOptional("ForEachMacros", Style.ForEachMacros);
    IO.mapOptional("IfMacros", Style.IfMacros);
    IO.mapOptional("IncludeBlocks", Style.IncludeStyle.IncludeBlocks);
    IO.mapOptional("IncludeCategories", Style.IncludeStyle.IncludeCategories);
    IO.mapOptional("IncludeIsMainRegex", Style.IncludeStyle.IncludeIsMainRegex);
    IO.mapOptional("IncludeIsMainSourceRegex",
                   Style.IncludeStyle.IncludeIsMainSourceRegex);
    IO.mapOptional("IndentAccessModifiers", Style.IndentAccessModifiers);
    IO.mapOptional("IndentCaseBlocks", Style.IndentCaseBlocks);
    IO.mapOptional("IndentCaseLabels", Style.IndentCaseLabels);
    IO.mapOptional("IndentExternBlock", Style.IndentExternBlock);
    IO.mapOptional("IndentGotoLabels", Style.IndentGotoLabels);
    IO.mapOptional("IndentPPDirectives", Style.IndentPPDirectives);
    IO.mapOptional("IndentRequiresClause", Style.IndentRequiresClause);
    IO.mapOptional("IndentWidth", Style.IndentWidth);
    IO.mapOptional("IndentWrappedFunctionNames",
                   Style.IndentWrappedFunctionNames);
    IO.mapOptional("InsertBraces", Style.InsertBraces);
    IO.mapOptional("InsertNewlineAtEOF", Style.InsertNewlineAtEOF);
    IO.mapOptional("InsertTrailingCommas", Style.InsertTrailingCommas);
    IO.mapOptional("IntegerLiteralSeparator", Style.IntegerLiteralSeparator);
    IO.mapOptional("JavaImportGroups", Style.JavaImportGroups);
    IO.mapOptional("JavaScriptQuotes", Style.JavaScriptQuotes);
    IO.mapOptional("JavaScriptWrapImports", Style.JavaScriptWrapImports);
    IO.mapOptional("KeepEmptyLines", Style.KeepEmptyLines);
    IO.mapOptional("LambdaBodyIndentation", Style.LambdaBodyIndentation);
    IO.mapOptional("LineEnding", Style.LineEnding);
    IO.mapOptional("MacroBlockBegin", Style.MacroBlockBegin);
    IO.mapOptional("MacroBlockEnd", Style.MacroBlockEnd);
    IO.mapOptional("Macros", Style.Macros);
    IO.mapOptional("MainIncludeChar", Style.IncludeStyle.MainIncludeChar);
    IO.mapOptional("MaxEmptyLinesToKeep", Style.MaxEmptyLinesToKeep);
    IO.mapOptional("NamespaceIndentation", Style.NamespaceIndentation);
    IO.mapOptional("NamespaceMacros", Style.NamespaceMacros);
    IO.mapOptional("ObjCBinPackProtocolList", Style.ObjCBinPackProtocolList);
    IO.mapOptional("ObjCBlockIndentWidth", Style.ObjCBlockIndentWidth);
    IO.mapOptional("ObjCBreakBeforeNestedBlockParam",
                   Style.ObjCBreakBeforeNestedBlockParam);
    IO.mapOptional("ObjCPropertyAttributeOrder",
                   Style.ObjCPropertyAttributeOrder);
    IO.mapOptional("ObjCSpaceAfterProperty", Style.ObjCSpaceAfterProperty);
    IO.mapOptional("ObjCSpaceBeforeProtocolList",
                   Style.ObjCSpaceBeforeProtocolList);
    IO.mapOptional("PackConstructorInitializers",
                   Style.PackConstructorInitializers);
    IO.mapOptional("PenaltyBreakAssignment", Style.PenaltyBreakAssignment);
    IO.mapOptional("PenaltyBreakBeforeFirstCallParameter",
                   Style.PenaltyBreakBeforeFirstCallParameter);
    IO.mapOptional("PenaltyBreakComment", Style.PenaltyBreakComment);
    IO.mapOptional("PenaltyBreakFirstLessLess",
                   Style.PenaltyBreakFirstLessLess);
    IO.mapOptional("PenaltyBreakOpenParenthesis",
                   Style.PenaltyBreakOpenParenthesis);
    IO.mapOptional("PenaltyBreakScopeResolution",
                   Style.PenaltyBreakScopeResolution);
    IO.mapOptional("PenaltyBreakString", Style.PenaltyBreakString);
    IO.mapOptional("PenaltyBreakTemplateDeclaration",
                   Style.PenaltyBreakTemplateDeclaration);
    IO.mapOptional("PenaltyExcessCharacter", Style.PenaltyExcessCharacter);
    IO.mapOptional("PenaltyIndentedWhitespace",
                   Style.PenaltyIndentedWhitespace);
    IO.mapOptional("PenaltyReturnTypeOnItsOwnLine",
                   Style.PenaltyReturnTypeOnItsOwnLine);
    IO.mapOptional("PointerAlignment", Style.PointerAlignment);
    IO.mapOptional("PPIndentWidth", Style.PPIndentWidth);
    IO.mapOptional("QualifierAlignment", Style.QualifierAlignment);
    // Default Order for Left/Right based Qualifier alignment.
    if (Style.QualifierAlignment == FormatStyle::QAS_Right)
      Style.QualifierOrder = {"type", "const", "volatile"};
    else if (Style.QualifierAlignment == FormatStyle::QAS_Left)
      Style.QualifierOrder = {"const", "volatile", "type"};
    else if (Style.QualifierAlignment == FormatStyle::QAS_Custom)
      IO.mapOptional("QualifierOrder", Style.QualifierOrder);
    IO.mapOptional("RawStringFormats", Style.RawStringFormats);
    IO.mapOptional("ReferenceAlignment", Style.ReferenceAlignment);
    IO.mapOptional("ReflowComments", Style.ReflowComments);
    IO.mapOptional("RemoveBracesLLVM", Style.RemoveBracesLLVM);
    IO.mapOptional("RemoveParentheses", Style.RemoveParentheses);
    IO.mapOptional("RemoveSemicolon", Style.RemoveSemicolon);
    IO.mapOptional("RequiresClausePosition", Style.RequiresClausePosition);
    IO.mapOptional("RequiresExpressionIndentation",
                   Style.RequiresExpressionIndentation);
    IO.mapOptional("SeparateDefinitionBlocks", Style.SeparateDefinitionBlocks);
    IO.mapOptional("ShortNamespaceLines", Style.ShortNamespaceLines);
    IO.mapOptional("SkipMacroDefinitionBody", Style.SkipMacroDefinitionBody);
    IO.mapOptional("SortIncludes", Style.SortIncludes);
    IO.mapOptional("SortJavaStaticImport", Style.SortJavaStaticImport);
    IO.mapOptional("SortUsingDeclarations", Style.SortUsingDeclarations);
    IO.mapOptional("SpaceAfterCStyleCast", Style.SpaceAfterCStyleCast);
    IO.mapOptional("SpaceAfterLogicalNot", Style.SpaceAfterLogicalNot);
    IO.mapOptional("SpaceAfterTemplateKeyword",
                   Style.SpaceAfterTemplateKeyword);
    IO.mapOptional("SpaceAroundPointerQualifiers",
                   Style.SpaceAroundPointerQualifiers);
    IO.mapOptional("SpaceBeforeAssignmentOperators",
                   Style.SpaceBeforeAssignmentOperators);
    IO.mapOptional("SpaceBeforeCaseColon", Style.SpaceBeforeCaseColon);
    IO.mapOptional("SpaceBeforeCpp11BracedList",
                   Style.SpaceBeforeCpp11BracedList);
    IO.mapOptional("SpaceBeforeCtorInitializerColon",
                   Style.SpaceBeforeCtorInitializerColon);
    IO.mapOptional("SpaceBeforeInheritanceColon",
                   Style.SpaceBeforeInheritanceColon);
    IO.mapOptional("SpaceBeforeJsonColon", Style.SpaceBeforeJsonColon);
    IO.mapOptional("SpaceBeforeParens", Style.SpaceBeforeParens);
    IO.mapOptional("SpaceBeforeParensOptions", Style.SpaceBeforeParensOptions);
    IO.mapOptional("SpaceBeforeRangeBasedForLoopColon",
                   Style.SpaceBeforeRangeBasedForLoopColon);
    IO.mapOptional("SpaceBeforeSquareBrackets",
                   Style.SpaceBeforeSquareBrackets);
    IO.mapOptional("SpaceInEmptyBlock", Style.SpaceInEmptyBlock);
    IO.mapOptional("SpacesBeforeTrailingComments",
                   Style.SpacesBeforeTrailingComments);
    IO.mapOptional("SpacesInAngles", Style.SpacesInAngles);
    IO.mapOptional("SpacesInContainerLiterals",
                   Style.SpacesInContainerLiterals);
    IO.mapOptional("SpacesInLineCommentPrefix",
                   Style.SpacesInLineCommentPrefix);
    IO.mapOptional("SpacesInParens", Style.SpacesInParens);
    IO.mapOptional("SpacesInParensOptions", Style.SpacesInParensOptions);
    IO.mapOptional("SpacesInSquareBrackets", Style.SpacesInSquareBrackets);
    IO.mapOptional("Standard", Style.Standard);
    IO.mapOptional("StatementAttributeLikeMacros",
                   Style.StatementAttributeLikeMacros);
    IO.mapOptional("StatementMacros", Style.StatementMacros);
    IO.mapOptional("TableGenBreakingDAGArgOperators",
                   Style.TableGenBreakingDAGArgOperators);
    IO.mapOptional("TableGenBreakInsideDAGArg",
                   Style.TableGenBreakInsideDAGArg);
    IO.mapOptional("TabWidth", Style.TabWidth);
    IO.mapOptional("TypeNames", Style.TypeNames);
    IO.mapOptional("TypenameMacros", Style.TypenameMacros);
    IO.mapOptional("UseTab", Style.UseTab);
    IO.mapOptional("VerilogBreakBetweenInstancePorts",
                   Style.VerilogBreakBetweenInstancePorts);
    IO.mapOptional("WhitespaceSensitiveMacros",
                   Style.WhitespaceSensitiveMacros);

    // If AlwaysBreakAfterDefinitionReturnType was specified but
    // BreakAfterReturnType was not, initialize the latter from the former for
    // backwards compatibility.
    if (Style.AlwaysBreakAfterDefinitionReturnType != FormatStyle::DRTBS_None &&
        Style.BreakAfterReturnType == FormatStyle::RTBS_None) {
      if (Style.AlwaysBreakAfterDefinitionReturnType ==
          FormatStyle::DRTBS_All) {
        Style.BreakAfterReturnType = FormatStyle::RTBS_AllDefinitions;
      } else if (Style.AlwaysBreakAfterDefinitionReturnType ==
                 FormatStyle::DRTBS_TopLevel) {
        Style.BreakAfterReturnType = FormatStyle::RTBS_TopLevelDefinitions;
      }
    }

    // If BreakBeforeInheritanceComma was specified but BreakInheritance was
    // not, initialize the latter from the former for backwards compatibility.
    if (BreakBeforeInheritanceComma &&
        Style.BreakInheritanceList == FormatStyle::BILS_BeforeColon) {
      Style.BreakInheritanceList = FormatStyle::BILS_BeforeComma;
    }

    // If BreakConstructorInitializersBeforeComma was specified but
    // BreakConstructorInitializers was not, initialize the latter from the
    // former for backwards compatibility.
    if (BreakConstructorInitializersBeforeComma &&
        Style.BreakConstructorInitializers == FormatStyle::BCIS_BeforeColon) {
      Style.BreakConstructorInitializers = FormatStyle::BCIS_BeforeComma;
    }

    if (!IsGoogleOrChromium) {
      if (Style.PackConstructorInitializers == FormatStyle::PCIS_BinPack &&
          OnCurrentLine) {
        Style.PackConstructorInitializers = OnNextLine
                                                ? FormatStyle::PCIS_NextLine
                                                : FormatStyle::PCIS_CurrentLine;
      }
    } else if (Style.PackConstructorInitializers ==
               FormatStyle::PCIS_NextLine) {
      if (!OnCurrentLine)
        Style.PackConstructorInitializers = FormatStyle::PCIS_BinPack;
      else if (!OnNextLine)
        Style.PackConstructorInitializers = FormatStyle::PCIS_CurrentLine;
    }

    if (Style.LineEnding == FormatStyle::LE_DeriveLF) {
      if (!DeriveLineEnding)
        Style.LineEnding = UseCRLF ? FormatStyle::LE_CRLF : FormatStyle::LE_LF;
      else if (UseCRLF)
        Style.LineEnding = FormatStyle::LE_DeriveCRLF;
    }

    if (Style.SpacesInParens != FormatStyle::SIPO_Custom &&
        (SpacesInParentheses || SpaceInEmptyParentheses ||
         SpacesInConditionalStatement || SpacesInCStyleCastParentheses)) {
      if (SpacesInParentheses) {
        // For backward compatibility.
        Style.SpacesInParensOptions.ExceptDoubleParentheses = false;
        Style.SpacesInParensOptions.InConditionalStatements = true;
        Style.SpacesInParensOptions.InCStyleCasts =
            SpacesInCStyleCastParentheses;
        Style.SpacesInParensOptions.InEmptyParentheses =
            SpaceInEmptyParentheses;
        Style.SpacesInParensOptions.Other = true;
      } else {
        Style.SpacesInParensOptions = {};
        Style.SpacesInParensOptions.InConditionalStatements =
            SpacesInConditionalStatement;
        Style.SpacesInParensOptions.InCStyleCasts =
            SpacesInCStyleCastParentheses;
        Style.SpacesInParensOptions.InEmptyParentheses =
            SpaceInEmptyParentheses;
      }
      Style.SpacesInParens = FormatStyle::SIPO_Custom;
    }
  }
};

// Allows to read vector<FormatStyle> while keeping default values.
// IO.getContext() should contain a pointer to the FormatStyle structure, that
// will be used to get default values for missing keys.
// If the first element has no Language specified, it will be treated as the
// default one for the following elements.
template <> struct DocumentListTraits<std::vector<FormatStyle>> {
  static size_t size(IO &IO, std::vector<FormatStyle> &Seq) {
    return Seq.size();
  }
  static FormatStyle &element(IO &IO, std::vector<FormatStyle> &Seq,
                              size_t Index) {
    if (Index >= Seq.size()) {
      assert(Index == Seq.size());
      FormatStyle Template;
      if (!Seq.empty() && Seq[0].Language == FormatStyle::LK_None) {
        Template = Seq[0];
      } else {
        Template = *((const FormatStyle *)IO.getContext());
        Template.Language = FormatStyle::LK_None;
      }
      Seq.resize(Index + 1, Template);
    }
    return Seq[Index];
  }
};
} // namespace yaml
} // namespace llvm

namespace clang {
namespace format {

const std::error_category &getParseCategory() {
  static const ParseErrorCategory C{};
  return C;
}
std::error_code make_error_code(ParseError e) {
  return std::error_code(static_cast<int>(e), getParseCategory());
}

inline llvm::Error make_string_error(const Twine &Message) {
  return llvm::make_error<llvm::StringError>(Message,
                                             llvm::inconvertibleErrorCode());
}

const char *ParseErrorCategory::name() const noexcept {
  return "clang-format.parse_error";
}

std::string ParseErrorCategory::message(int EV) const {
  switch (static_cast<ParseError>(EV)) {
  case ParseError::Success:
    return "Success";
  case ParseError::Error:
    return "Invalid argument";
  case ParseError::Unsuitable:
    return "Unsuitable";
  case ParseError::BinPackTrailingCommaConflict:
    return "trailing comma insertion cannot be used with bin packing";
  case ParseError::InvalidQualifierSpecified:
    return "Invalid qualifier specified in QualifierOrder";
  case ParseError::DuplicateQualifierSpecified:
    return "Duplicate qualifier specified in QualifierOrder";
  case ParseError::MissingQualifierType:
    return "Missing type in QualifierOrder";
  case ParseError::MissingQualifierOrder:
    return "Missing QualifierOrder";
  }
  llvm_unreachable("unexpected parse error");
}

static void expandPresetsBraceWrapping(FormatStyle &Expanded) {
  if (Expanded.BreakBeforeBraces == FormatStyle::BS_Custom)
    return;
  Expanded.BraceWrapping = {/*AfterCaseLabel=*/false,
                            /*AfterClass=*/false,
                            /*AfterControlStatement=*/FormatStyle::BWACS_Never,
                            /*AfterEnum=*/false,
                            /*AfterFunction=*/false,
                            /*AfterNamespace=*/false,
                            /*AfterObjCDeclaration=*/false,
                            /*AfterStruct=*/false,
                            /*AfterUnion=*/false,
                            /*AfterExternBlock=*/false,
                            /*BeforeCatch=*/false,
                            /*BeforeElse=*/false,
                            /*BeforeLambdaBody=*/false,
                            /*BeforeWhile=*/false,
                            /*IndentBraces=*/false,
                            /*SplitEmptyFunction=*/true,
                            /*SplitEmptyRecord=*/true,
                            /*SplitEmptyNamespace=*/true};
  switch (Expanded.BreakBeforeBraces) {
  case FormatStyle::BS_Linux:
    Expanded.BraceWrapping.AfterClass = true;
    Expanded.BraceWrapping.AfterFunction = true;
    Expanded.BraceWrapping.AfterNamespace = true;
    break;
  case FormatStyle::BS_Mozilla:
    Expanded.BraceWrapping.AfterClass = true;
    Expanded.BraceWrapping.AfterEnum = true;
    Expanded.BraceWrapping.AfterFunction = true;
    Expanded.BraceWrapping.AfterStruct = true;
    Expanded.BraceWrapping.AfterUnion = true;
    Expanded.BraceWrapping.AfterExternBlock = true;
    Expanded.BraceWrapping.SplitEmptyFunction = true;
    Expanded.BraceWrapping.SplitEmptyRecord = false;
    break;
  case FormatStyle::BS_Stroustrup:
    Expanded.BraceWrapping.AfterFunction = true;
    Expanded.BraceWrapping.BeforeCatch = true;
    Expanded.BraceWrapping.BeforeElse = true;
    break;
  case FormatStyle::BS_Allman:
    Expanded.BraceWrapping.AfterCaseLabel = true;
    Expanded.BraceWrapping.AfterClass = true;
    Expanded.BraceWrapping.AfterControlStatement = FormatStyle::BWACS_Always;
    Expanded.BraceWrapping.AfterEnum = true;
    Expanded.BraceWrapping.AfterFunction = true;
    Expanded.BraceWrapping.AfterNamespace = true;
    Expanded.BraceWrapping.AfterObjCDeclaration = true;
    Expanded.BraceWrapping.AfterStruct = true;
    Expanded.BraceWrapping.AfterUnion = true;
    Expanded.BraceWrapping.AfterExternBlock = true;
    Expanded.BraceWrapping.BeforeCatch = true;
    Expanded.BraceWrapping.BeforeElse = true;
    Expanded.BraceWrapping.BeforeLambdaBody = true;
    break;
  case FormatStyle::BS_Whitesmiths:
    Expanded.BraceWrapping.AfterCaseLabel = true;
    Expanded.BraceWrapping.AfterClass = true;
    Expanded.BraceWrapping.AfterControlStatement = FormatStyle::BWACS_Always;
    Expanded.BraceWrapping.AfterEnum = true;
    Expanded.BraceWrapping.AfterFunction = true;
    Expanded.BraceWrapping.AfterNamespace = true;
    Expanded.BraceWrapping.AfterObjCDeclaration = true;
    Expanded.BraceWrapping.AfterStruct = true;
    Expanded.BraceWrapping.AfterExternBlock = true;
    Expanded.BraceWrapping.BeforeCatch = true;
    Expanded.BraceWrapping.BeforeElse = true;
    Expanded.BraceWrapping.BeforeLambdaBody = true;
    break;
  case FormatStyle::BS_GNU:
    Expanded.BraceWrapping = {
        /*AfterCaseLabel=*/true,
        /*AfterClass=*/true,
        /*AfterControlStatement=*/FormatStyle::BWACS_Always,
        /*AfterEnum=*/true,
        /*AfterFunction=*/true,
        /*AfterNamespace=*/true,
        /*AfterObjCDeclaration=*/true,
        /*AfterStruct=*/true,
        /*AfterUnion=*/true,
        /*AfterExternBlock=*/true,
        /*BeforeCatch=*/true,
        /*BeforeElse=*/true,
        /*BeforeLambdaBody=*/false,
        /*BeforeWhile=*/true,
        /*IndentBraces=*/true,
        /*SplitEmptyFunction=*/true,
        /*SplitEmptyRecord=*/true,
        /*SplitEmptyNamespace=*/true};
    break;
  case FormatStyle::BS_WebKit:
    Expanded.BraceWrapping.AfterFunction = true;
    break;
  default:
    break;
  }
}

static void expandPresetsSpaceBeforeParens(FormatStyle &Expanded) {
  if (Expanded.SpaceBeforeParens == FormatStyle::SBPO_Custom)
    return;
  // Reset all flags
  Expanded.SpaceBeforeParensOptions = {};
  Expanded.SpaceBeforeParensOptions.AfterPlacementOperator = true;

  switch (Expanded.SpaceBeforeParens) {
  case FormatStyle::SBPO_ControlStatements:
    Expanded.SpaceBeforeParensOptions.AfterControlStatements = true;
    Expanded.SpaceBeforeParensOptions.AfterForeachMacros = true;
    Expanded.SpaceBeforeParensOptions.AfterIfMacros = true;
    break;
  case FormatStyle::SBPO_ControlStatementsExceptControlMacros:
    Expanded.SpaceBeforeParensOptions.AfterControlStatements = true;
    break;
  case FormatStyle::SBPO_NonEmptyParentheses:
    Expanded.SpaceBeforeParensOptions.BeforeNonEmptyParentheses = true;
    break;
  default:
    break;
  }
}

static void expandPresetsSpacesInParens(FormatStyle &Expanded) {
  if (Expanded.SpacesInParens == FormatStyle::SIPO_Custom)
    return;
  assert(Expanded.SpacesInParens == FormatStyle::SIPO_Never);
  // Reset all flags
  Expanded.SpacesInParensOptions = {};
}

FormatStyle getLLVMStyle(FormatStyle::LanguageKind Language) {
  FormatStyle LLVMStyle;
  LLVMStyle.AccessModifierOffset = -2;
  LLVMStyle.AlignAfterOpenBracket = FormatStyle::BAS_Align;
  LLVMStyle.AlignArrayOfStructures = FormatStyle::AIAS_None;
  LLVMStyle.AlignConsecutiveAssignments = {};
  LLVMStyle.AlignConsecutiveAssignments.AcrossComments = false;
  LLVMStyle.AlignConsecutiveAssignments.AcrossEmptyLines = false;
  LLVMStyle.AlignConsecutiveAssignments.AlignCompound = false;
  LLVMStyle.AlignConsecutiveAssignments.AlignFunctionPointers = false;
  LLVMStyle.AlignConsecutiveAssignments.Enabled = false;
  LLVMStyle.AlignConsecutiveAssignments.PadOperators = true;
  LLVMStyle.AlignConsecutiveBitFields = {};
  LLVMStyle.AlignConsecutiveDeclarations = {};
  LLVMStyle.AlignConsecutiveMacros = {};
  LLVMStyle.AlignConsecutiveShortCaseStatements = {};
  LLVMStyle.AlignConsecutiveTableGenBreakingDAGArgColons = {};
  LLVMStyle.AlignConsecutiveTableGenCondOperatorColons = {};
  LLVMStyle.AlignConsecutiveTableGenDefinitionColons = {};
  LLVMStyle.AlignEscapedNewlines = FormatStyle::ENAS_Right;
  LLVMStyle.AlignOperands = FormatStyle::OAS_Align;
  LLVMStyle.AlignTrailingComments = {};
  LLVMStyle.AlignTrailingComments.Kind = FormatStyle::TCAS_Always;
  LLVMStyle.AlignTrailingComments.OverEmptyLines = 0;
  LLVMStyle.AllowAllArgumentsOnNextLine = true;
  LLVMStyle.AllowAllParametersOfDeclarationOnNextLine = true;
  LLVMStyle.AllowBreakBeforeNoexceptSpecifier = FormatStyle::BBNSS_Never;
  LLVMStyle.AllowShortBlocksOnASingleLine = FormatStyle::SBS_Never;
  LLVMStyle.AllowShortCaseExpressionOnASingleLine = true;
  LLVMStyle.AllowShortCaseLabelsOnASingleLine = false;
  LLVMStyle.AllowShortCompoundRequirementOnASingleLine = true;
  LLVMStyle.AllowShortEnumsOnASingleLine = true;
  LLVMStyle.AllowShortFunctionsOnASingleLine = FormatStyle::SFS_All;
  LLVMStyle.AllowShortIfStatementsOnASingleLine = FormatStyle::SIS_Never;
  LLVMStyle.AllowShortLambdasOnASingleLine = FormatStyle::SLS_All;
  LLVMStyle.AllowShortLoopsOnASingleLine = false;
  LLVMStyle.AlwaysBreakAfterDefinitionReturnType = FormatStyle::DRTBS_None;
  LLVMStyle.AlwaysBreakBeforeMultilineStrings = false;
  LLVMStyle.AttributeMacros.push_back("__capability");
  LLVMStyle.BinPackArguments = true;
  LLVMStyle.BinPackParameters = true;
  LLVMStyle.BitFieldColonSpacing = FormatStyle::BFCS_Both;
  LLVMStyle.BracedInitializerIndentWidth = std::nullopt;
  LLVMStyle.BraceWrapping = {/*AfterCaseLabel=*/false,
                             /*AfterClass=*/false,
                             /*AfterControlStatement=*/FormatStyle::BWACS_Never,
                             /*AfterEnum=*/false,
                             /*AfterFunction=*/false,
                             /*AfterNamespace=*/false,
                             /*AfterObjCDeclaration=*/false,
                             /*AfterStruct=*/false,
                             /*AfterUnion=*/false,
                             /*AfterExternBlock=*/false,
                             /*BeforeCatch=*/false,
                             /*BeforeElse=*/false,
                             /*BeforeLambdaBody=*/false,
                             /*BeforeWhile=*/false,
                             /*IndentBraces=*/false,
                             /*SplitEmptyFunction=*/true,
                             /*SplitEmptyRecord=*/true,
                             /*SplitEmptyNamespace=*/true};
  LLVMStyle.BreakAdjacentStringLiterals = true;
  LLVMStyle.BreakAfterAttributes = FormatStyle::ABS_Leave;
  LLVMStyle.BreakAfterJavaFieldAnnotations = false;
  LLVMStyle.BreakAfterReturnType = FormatStyle::RTBS_None;
  LLVMStyle.BreakArrays = true;
  LLVMStyle.BreakBeforeBinaryOperators = FormatStyle::BOS_None;
  LLVMStyle.BreakBeforeBraces = FormatStyle::BS_Attach;
  LLVMStyle.BreakBeforeConceptDeclarations = FormatStyle::BBCDS_Always;
  LLVMStyle.BreakBeforeInlineASMColon = FormatStyle::BBIAS_OnlyMultiline;
  LLVMStyle.BreakBeforeTernaryOperators = true;
  LLVMStyle.BreakConstructorInitializers = FormatStyle::BCIS_BeforeColon;
  LLVMStyle.BreakFunctionDefinitionParameters = false;
  LLVMStyle.BreakInheritanceList = FormatStyle::BILS_BeforeColon;
  LLVMStyle.BreakStringLiterals = true;
  LLVMStyle.BreakTemplateDeclarations = FormatStyle::BTDS_MultiLine;
  LLVMStyle.ColumnLimit = 80;
  LLVMStyle.CommentPragmas = "^ IWYU pragma:";
  LLVMStyle.CompactNamespaces = false;
  LLVMStyle.ConstructorInitializerIndentWidth = 4;
  LLVMStyle.ContinuationIndentWidth = 4;
  LLVMStyle.Cpp11BracedListStyle = true;
  LLVMStyle.DerivePointerAlignment = false;
  LLVMStyle.DisableFormat = false;
  LLVMStyle.EmptyLineAfterAccessModifier = FormatStyle::ELAAMS_Never;
  LLVMStyle.EmptyLineBeforeAccessModifier = FormatStyle::ELBAMS_LogicalBlock;
  LLVMStyle.ExperimentalAutoDetectBinPacking = false;
  LLVMStyle.FixNamespaceComments = true;
  LLVMStyle.ForEachMacros.push_back("foreach");
  LLVMStyle.ForEachMacros.push_back("Q_FOREACH");
  LLVMStyle.ForEachMacros.push_back("BOOST_FOREACH");
  LLVMStyle.IfMacros.push_back("KJ_IF_MAYBE");
  LLVMStyle.IncludeStyle.IncludeBlocks = tooling::IncludeStyle::IBS_Preserve;
  LLVMStyle.IncludeStyle.IncludeCategories = {
      {"^\"(llvm|llvm-c|clang|clang-c)/", 2, 0, false},
      {"^(<|\"(gtest|gmock|isl|json)/)", 3, 0, false},
      {".*", 1, 0, false}};
  LLVMStyle.IncludeStyle.IncludeIsMainRegex = "(Test)?$";
  LLVMStyle.IncludeStyle.MainIncludeChar = tooling::IncludeStyle::MICD_Quote;
  LLVMStyle.IndentAccessModifiers = false;
  LLVMStyle.IndentCaseBlocks = false;
  LLVMStyle.IndentCaseLabels = false;
  LLVMStyle.IndentExternBlock = FormatStyle::IEBS_AfterExternBlock;
  LLVMStyle.IndentGotoLabels = true;
  LLVMStyle.IndentPPDirectives = FormatStyle::PPDIS_None;
  LLVMStyle.IndentRequiresClause = true;
  LLVMStyle.IndentWidth = 2;
  LLVMStyle.IndentWrappedFunctionNames = false;
  LLVMStyle.InheritsParentConfig = false;
  LLVMStyle.InsertBraces = false;
  LLVMStyle.InsertNewlineAtEOF = false;
  LLVMStyle.InsertTrailingCommas = FormatStyle::TCS_None;
  LLVMStyle.IntegerLiteralSeparator = {
      /*Binary=*/0,  /*BinaryMinDigits=*/0,
      /*Decimal=*/0, /*DecimalMinDigits=*/0,
      /*Hex=*/0,     /*HexMinDigits=*/0};
  LLVMStyle.JavaScriptQuotes = FormatStyle::JSQS_Leave;
  LLVMStyle.JavaScriptWrapImports = true;
  LLVMStyle.KeepEmptyLines = {
      /*AtEndOfFile=*/false,
      /*AtStartOfBlock=*/true,
      /*AtStartOfFile=*/true,
  };
  LLVMStyle.LambdaBodyIndentation = FormatStyle::LBI_Signature;
  LLVMStyle.Language = Language;
  LLVMStyle.LineEnding = FormatStyle::LE_DeriveLF;
  LLVMStyle.MaxEmptyLinesToKeep = 1;
  LLVMStyle.NamespaceIndentation = FormatStyle::NI_None;
  LLVMStyle.ObjCBinPackProtocolList = FormatStyle::BPS_Auto;
  LLVMStyle.ObjCBlockIndentWidth = 2;
  LLVMStyle.ObjCBreakBeforeNestedBlockParam = true;
  LLVMStyle.ObjCSpaceAfterProperty = false;
  LLVMStyle.ObjCSpaceBeforeProtocolList = true;
  LLVMStyle.PackConstructorInitializers = FormatStyle::PCIS_BinPack;
  LLVMStyle.PointerAlignment = FormatStyle::PAS_Right;
  LLVMStyle.PPIndentWidth = -1;
  LLVMStyle.QualifierAlignment = FormatStyle::QAS_Leave;
  LLVMStyle.ReferenceAlignment = FormatStyle::RAS_Pointer;
  LLVMStyle.ReflowComments = true;
  LLVMStyle.RemoveBracesLLVM = false;
  LLVMStyle.RemoveParentheses = FormatStyle::RPS_Leave;
  LLVMStyle.RemoveSemicolon = false;
  LLVMStyle.RequiresClausePosition = FormatStyle::RCPS_OwnLine;
  LLVMStyle.RequiresExpressionIndentation = FormatStyle::REI_OuterScope;
  LLVMStyle.SeparateDefinitionBlocks = FormatStyle::SDS_Leave;
  LLVMStyle.ShortNamespaceLines = 1;
  LLVMStyle.SkipMacroDefinitionBody = false;
  LLVMStyle.SortIncludes = FormatStyle::SI_CaseSensitive;
  LLVMStyle.SortJavaStaticImport = FormatStyle::SJSIO_Before;
  LLVMStyle.SortUsingDeclarations = FormatStyle::SUD_LexicographicNumeric;
  LLVMStyle.SpaceAfterCStyleCast = false;
  LLVMStyle.SpaceAfterLogicalNot = false;
  LLVMStyle.SpaceAfterTemplateKeyword = true;
  LLVMStyle.SpaceAroundPointerQualifiers = FormatStyle::SAPQ_Default;
  LLVMStyle.SpaceBeforeAssignmentOperators = true;
  LLVMStyle.SpaceBeforeCaseColon = false;
  LLVMStyle.SpaceBeforeCpp11BracedList = false;
  LLVMStyle.SpaceBeforeCtorInitializerColon = true;
  LLVMStyle.SpaceBeforeInheritanceColon = true;
  LLVMStyle.SpaceBeforeJsonColon = false;
  LLVMStyle.SpaceBeforeParens = FormatStyle::SBPO_ControlStatements;
  LLVMStyle.SpaceBeforeParensOptions = {};
  LLVMStyle.SpaceBeforeParensOptions.AfterControlStatements = true;
  LLVMStyle.SpaceBeforeParensOptions.AfterForeachMacros = true;
  LLVMStyle.SpaceBeforeParensOptions.AfterIfMacros = true;
  LLVMStyle.SpaceBeforeRangeBasedForLoopColon = true;
  LLVMStyle.SpaceBeforeSquareBrackets = false;
  LLVMStyle.SpaceInEmptyBlock = false;
  LLVMStyle.SpacesBeforeTrailingComments = 1;
  LLVMStyle.SpacesInAngles = FormatStyle::SIAS_Never;
  LLVMStyle.SpacesInContainerLiterals = true;
  LLVMStyle.SpacesInLineCommentPrefix = {/*Minimum=*/1, /*Maximum=*/-1u};
  LLVMStyle.SpacesInParens = FormatStyle::SIPO_Never;
  LLVMStyle.SpacesInSquareBrackets = false;
  LLVMStyle.Standard = FormatStyle::LS_Latest;
  LLVMStyle.StatementAttributeLikeMacros.push_back("Q_EMIT");
  LLVMStyle.StatementMacros.push_back("Q_UNUSED");
  LLVMStyle.StatementMacros.push_back("QT_REQUIRE_VERSION");
  LLVMStyle.TableGenBreakingDAGArgOperators = {};
  LLVMStyle.TableGenBreakInsideDAGArg = FormatStyle::DAS_DontBreak;
  LLVMStyle.TabWidth = 8;
  LLVMStyle.UseTab = FormatStyle::UT_Never;
  LLVMStyle.VerilogBreakBetweenInstancePorts = true;
  LLVMStyle.WhitespaceSensitiveMacros.push_back("BOOST_PP_STRINGIZE");
  LLVMStyle.WhitespaceSensitiveMacros.push_back("CF_SWIFT_NAME");
  LLVMStyle.WhitespaceSensitiveMacros.push_back("NS_SWIFT_NAME");
  LLVMStyle.WhitespaceSensitiveMacros.push_back("PP_STRINGIZE");
  LLVMStyle.WhitespaceSensitiveMacros.push_back("STRINGIZE");

  LLVMStyle.PenaltyBreakAssignment = prec::Assignment;
  LLVMStyle.PenaltyBreakBeforeFirstCallParameter = 19;
  LLVMStyle.PenaltyBreakComment = 300;
  LLVMStyle.PenaltyBreakFirstLessLess = 120;
  LLVMStyle.PenaltyBreakOpenParenthesis = 0;
  LLVMStyle.PenaltyBreakScopeResolution = 500;
  LLVMStyle.PenaltyBreakString = 1000;
  LLVMStyle.PenaltyBreakTemplateDeclaration = prec::Relational;
  LLVMStyle.PenaltyExcessCharacter = 1'000'000;
  LLVMStyle.PenaltyIndentedWhitespace = 0;
  LLVMStyle.PenaltyReturnTypeOnItsOwnLine = 60;

  // Defaults that differ when not C++.
  switch (Language) {
  case FormatStyle::LK_TableGen:
    LLVMStyle.SpacesInContainerLiterals = false;
    break;
  case FormatStyle::LK_Json:
    LLVMStyle.ColumnLimit = 0;
    break;
  case FormatStyle::LK_Verilog:
    LLVMStyle.IndentCaseLabels = true;
    LLVMStyle.SpacesInContainerLiterals = false;
    break;
  default:
    break;
  }

  return LLVMStyle;
}

FormatStyle getGoogleStyle(FormatStyle::LanguageKind Language) {
  if (Language == FormatStyle::LK_TextProto) {
    FormatStyle GoogleStyle = getGoogleStyle(FormatStyle::LK_Proto);
    GoogleStyle.Language = FormatStyle::LK_TextProto;

    return GoogleStyle;
  }

  FormatStyle GoogleStyle = getLLVMStyle(Language);

  GoogleStyle.AccessModifierOffset = -1;
  GoogleStyle.AlignEscapedNewlines = FormatStyle::ENAS_Left;
  GoogleStyle.AllowShortIfStatementsOnASingleLine =
      FormatStyle::SIS_WithoutElse;
  GoogleStyle.AllowShortLoopsOnASingleLine = true;
  GoogleStyle.AlwaysBreakBeforeMultilineStrings = true;
  GoogleStyle.BreakTemplateDeclarations = FormatStyle::BTDS_Yes;
  GoogleStyle.DerivePointerAlignment = true;
  GoogleStyle.IncludeStyle.IncludeBlocks = tooling::IncludeStyle::IBS_Regroup;
  GoogleStyle.IncludeStyle.IncludeCategories = {{"^<ext/.*\\.h>", 2, 0, false},
                                                {"^<.*\\.h>", 1, 0, false},
                                                {"^<.*", 2, 0, false},
                                                {".*", 3, 0, false}};
  GoogleStyle.IncludeStyle.IncludeIsMainRegex = "([-_](test|unittest))?$";
  GoogleStyle.IndentCaseLabels = true;
  GoogleStyle.KeepEmptyLines.AtStartOfBlock = false;
  GoogleStyle.ObjCBinPackProtocolList = FormatStyle::BPS_Never;
  GoogleStyle.ObjCSpaceAfterProperty = false;
  GoogleStyle.ObjCSpaceBeforeProtocolList = true;
  GoogleStyle.PackConstructorInitializers = FormatStyle::PCIS_NextLine;
  GoogleStyle.PointerAlignment = FormatStyle::PAS_Left;
  GoogleStyle.RawStringFormats = {
      {
          FormatStyle::LK_Cpp,
          /*Delimiters=*/
          {
              "cc",
              "CC",
              "cpp",
              "Cpp",
              "CPP",
              "c++",
              "C++",
          },
          /*EnclosingFunctionNames=*/
          {},
          /*CanonicalDelimiter=*/"",
          /*BasedOnStyle=*/"google",
      },
      {
          FormatStyle::LK_TextProto,
          /*Delimiters=*/
          {
              "pb",
              "PB",
              "proto",
              "PROTO",
          },
          /*EnclosingFunctionNames=*/
          {
              "EqualsProto",
              "EquivToProto",
              "PARSE_PARTIAL_TEXT_PROTO",
              "PARSE_TEST_PROTO",
              "PARSE_TEXT_PROTO",
              "ParseTextOrDie",
              "ParseTextProtoOrDie",
              "ParseTestProto",
              "ParsePartialTestProto",
          },
          /*CanonicalDelimiter=*/"pb",
          /*BasedOnStyle=*/"google",
      },
  };

  GoogleStyle.SpacesBeforeTrailingComments = 2;
  GoogleStyle.Standard = FormatStyle::LS_Auto;

  GoogleStyle.PenaltyBreakBeforeFirstCallParameter = 1;
  GoogleStyle.PenaltyReturnTypeOnItsOwnLine = 200;

  if (Language == FormatStyle::LK_Java) {
    GoogleStyle.AlignAfterOpenBracket = FormatStyle::BAS_DontAlign;
    GoogleStyle.AlignOperands = FormatStyle::OAS_DontAlign;
    GoogleStyle.AlignTrailingComments = {};
    GoogleStyle.AlignTrailingComments.Kind = FormatStyle::TCAS_Never;
    GoogleStyle.AllowShortFunctionsOnASingleLine = FormatStyle::SFS_Empty;
    GoogleStyle.AllowShortIfStatementsOnASingleLine = FormatStyle::SIS_Never;
    GoogleStyle.AlwaysBreakBeforeMultilineStrings = false;
    GoogleStyle.BreakBeforeBinaryOperators = FormatStyle::BOS_NonAssignment;
    GoogleStyle.ColumnLimit = 100;
    GoogleStyle.SpaceAfterCStyleCast = true;
    GoogleStyle.SpacesBeforeTrailingComments = 1;
  } else if (Language == FormatStyle::LK_JavaScript) {
    GoogleStyle.AlignAfterOpenBracket = FormatStyle::BAS_AlwaysBreak;
    GoogleStyle.AlignOperands = FormatStyle::OAS_DontAlign;
    GoogleStyle.AllowShortFunctionsOnASingleLine = FormatStyle::SFS_Empty;
    // TODO: still under discussion whether to switch to SLS_All.
    GoogleStyle.AllowShortLambdasOnASingleLine = FormatStyle::SLS_Empty;
    GoogleStyle.AlwaysBreakBeforeMultilineStrings = false;
    GoogleStyle.BreakBeforeTernaryOperators = false;
    // taze:, triple slash directives (`/// <...`), tslint:, and @see, which is
    // commonly followed by overlong URLs.
    GoogleStyle.CommentPragmas = "(taze:|^/[ \t]*<|tslint:|@see)";
    // TODO: enable once decided, in particular re disabling bin packing.
    // https://google.github.io/styleguide/jsguide.html#features-arrays-trailing-comma
    // GoogleStyle.InsertTrailingCommas = FormatStyle::TCS_Wrapped;
    GoogleStyle.JavaScriptQuotes = FormatStyle::JSQS_Single;
    GoogleStyle.JavaScriptWrapImports = false;
    GoogleStyle.MaxEmptyLinesToKeep = 3;
    GoogleStyle.NamespaceIndentation = FormatStyle::NI_All;
    GoogleStyle.SpacesInContainerLiterals = false;
  } else if (Language == FormatStyle::LK_Proto) {
    GoogleStyle.AllowShortFunctionsOnASingleLine = FormatStyle::SFS_Empty;
    GoogleStyle.AlwaysBreakBeforeMultilineStrings = false;
    // This affects protocol buffer options specifications and text protos.
    // Text protos are currently mostly formatted inside C++ raw string literals
    // and often the current breaking behavior of string literals is not
    // beneficial there. Investigate turning this on once proper string reflow
    // has been implemented.
    GoogleStyle.BreakStringLiterals = false;
    GoogleStyle.Cpp11BracedListStyle = false;
    GoogleStyle.SpacesInContainerLiterals = false;
  } else if (Language == FormatStyle::LK_ObjC) {
    GoogleStyle.AlwaysBreakBeforeMultilineStrings = false;
    GoogleStyle.ColumnLimit = 100;
    // "Regroup" doesn't work well for ObjC yet (main header heuristic,
    // relationship between ObjC standard library headers and other heades,
    // #imports, etc.)
    GoogleStyle.IncludeStyle.IncludeBlocks =
        tooling::IncludeStyle::IBS_Preserve;
  } else if (Language == FormatStyle::LK_CSharp) {
    GoogleStyle.AllowShortFunctionsOnASingleLine = FormatStyle::SFS_Empty;
    GoogleStyle.AllowShortIfStatementsOnASingleLine = FormatStyle::SIS_Never;
    GoogleStyle.BreakStringLiterals = false;
    GoogleStyle.ColumnLimit = 100;
    GoogleStyle.NamespaceIndentation = FormatStyle::NI_All;
  }

  return GoogleStyle;
}

FormatStyle getChromiumStyle(FormatStyle::LanguageKind Language) {
  FormatStyle ChromiumStyle = getGoogleStyle(Language);

  // Disable include reordering across blocks in Chromium code.
  // - clang-format tries to detect that foo.h is the "main" header for
  //   foo.cc and foo_unittest.cc via IncludeIsMainRegex. However, Chromium
  //   uses many other suffices (_win.cc, _mac.mm, _posix.cc, _browsertest.cc,
  //   _private.cc, _impl.cc etc) in different permutations
  //   (_win_browsertest.cc) so disable this until IncludeIsMainRegex has a
  //   better default for Chromium code.
  // - The default for .cc and .mm files is different (r357695) for Google style
  //   for the same reason. The plan is to unify this again once the main
  //   header detection works for Google's ObjC code, but this hasn't happened
  //   yet. Since Chromium has some ObjC code, switching Chromium is blocked
  //   on that.
  // - Finally, "If include reordering is harmful, put things in different
  //   blocks to prevent it" has been a recommendation for a long time that
  //   people are used to. We'll need a dev education push to change this to
  //   "If include reordering is harmful, put things in a different block and
  //   _prepend that with a comment_ to prevent it" before changing behavior.
  ChromiumStyle.IncludeStyle.IncludeBlocks =
      tooling::IncludeStyle::IBS_Preserve;

  if (Language == FormatStyle::LK_Java) {
    ChromiumStyle.AllowShortIfStatementsOnASingleLine =
        FormatStyle::SIS_WithoutElse;
    ChromiumStyle.BreakAfterJavaFieldAnnotations = true;
    ChromiumStyle.ContinuationIndentWidth = 8;
    ChromiumStyle.IndentWidth = 4;
    // See styleguide for import groups:
    // https://chromium.googlesource.com/chromium/src/+/refs/heads/main/styleguide/java/java.md#Import-Order
    ChromiumStyle.JavaImportGroups = {
        "android",
        "androidx",
        "com",
        "dalvik",
        "junit",
        "org",
        "com.google.android.apps.chrome",
        "org.chromium",
        "java",
        "javax",
    };
    ChromiumStyle.SortIncludes = FormatStyle::SI_CaseSensitive;
  } else if (Language == FormatStyle::LK_JavaScript) {
    ChromiumStyle.AllowShortIfStatementsOnASingleLine = FormatStyle::SIS_Never;
    ChromiumStyle.AllowShortLoopsOnASingleLine = false;
  } else {
    ChromiumStyle.AllowAllParametersOfDeclarationOnNextLine = false;
    ChromiumStyle.AllowShortFunctionsOnASingleLine = FormatStyle::SFS_Inline;
    ChromiumStyle.AllowShortIfStatementsOnASingleLine = FormatStyle::SIS_Never;
    ChromiumStyle.AllowShortLoopsOnASingleLine = false;
    ChromiumStyle.BinPackParameters = false;
    ChromiumStyle.DerivePointerAlignment = false;
    if (Language == FormatStyle::LK_ObjC)
      ChromiumStyle.ColumnLimit = 80;
  }
  return ChromiumStyle;
}

FormatStyle getMozillaStyle() {
  FormatStyle MozillaStyle = getLLVMStyle();
  MozillaStyle.AllowAllParametersOfDeclarationOnNextLine = false;
  MozillaStyle.AllowShortFunctionsOnASingleLine = FormatStyle::SFS_Inline;
  MozillaStyle.AlwaysBreakAfterDefinitionReturnType =
      FormatStyle::DRTBS_TopLevel;
  MozillaStyle.BinPackArguments = false;
  MozillaStyle.BinPackParameters = false;
  MozillaStyle.BreakAfterReturnType = FormatStyle::RTBS_TopLevel;
  MozillaStyle.BreakBeforeBraces = FormatStyle::BS_Mozilla;
  MozillaStyle.BreakConstructorInitializers = FormatStyle::BCIS_BeforeComma;
  MozillaStyle.BreakInheritanceList = FormatStyle::BILS_BeforeComma;
  MozillaStyle.BreakTemplateDeclarations = FormatStyle::BTDS_Yes;
  MozillaStyle.ConstructorInitializerIndentWidth = 2;
  MozillaStyle.ContinuationIndentWidth = 2;
  MozillaStyle.Cpp11BracedListStyle = false;
  MozillaStyle.FixNamespaceComments = false;
  MozillaStyle.IndentCaseLabels = true;
  MozillaStyle.ObjCSpaceAfterProperty = true;
  MozillaStyle.ObjCSpaceBeforeProtocolList = false;
  MozillaStyle.PenaltyReturnTypeOnItsOwnLine = 200;
  MozillaStyle.PointerAlignment = FormatStyle::PAS_Left;
  MozillaStyle.SpaceAfterTemplateKeyword = false;
  return MozillaStyle;
}

FormatStyle getWebKitStyle() {
  FormatStyle Style = getLLVMStyle();
  Style.AccessModifierOffset = -4;
  Style.AlignAfterOpenBracket = FormatStyle::BAS_DontAlign;
  Style.AlignOperands = FormatStyle::OAS_DontAlign;
  Style.AlignTrailingComments = {};
  Style.AlignTrailingComments.Kind = FormatStyle::TCAS_Never;
  Style.AllowShortBlocksOnASingleLine = FormatStyle::SBS_Empty;
  Style.BreakBeforeBinaryOperators = FormatStyle::BOS_All;
  Style.BreakBeforeBraces = FormatStyle::BS_WebKit;
  Style.BreakConstructorInitializers = FormatStyle::BCIS_BeforeComma;
  Style.ColumnLimit = 0;
  Style.Cpp11BracedListStyle = false;
  Style.FixNamespaceComments = false;
  Style.IndentWidth = 4;
  Style.NamespaceIndentation = FormatStyle::NI_Inner;
  Style.ObjCBlockIndentWidth = 4;
  Style.ObjCSpaceAfterProperty = true;
  Style.PointerAlignment = FormatStyle::PAS_Left;
  Style.SpaceBeforeCpp11BracedList = true;
  Style.SpaceInEmptyBlock = true;
  return Style;
}

FormatStyle getGNUStyle() {
  FormatStyle Style = getLLVMStyle();
  Style.AlwaysBreakAfterDefinitionReturnType = FormatStyle::DRTBS_All;
  Style.BreakAfterReturnType = FormatStyle::RTBS_AllDefinitions;
  Style.BreakBeforeBinaryOperators = FormatStyle::BOS_All;
  Style.BreakBeforeBraces = FormatStyle::BS_GNU;
  Style.BreakBeforeTernaryOperators = true;
  Style.ColumnLimit = 79;
  Style.Cpp11BracedListStyle = false;
  Style.FixNamespaceComments = false;
  Style.SpaceBeforeParens = FormatStyle::SBPO_Always;
  Style.Standard = FormatStyle::LS_Cpp03;
  return Style;
}

FormatStyle getMicrosoftStyle(FormatStyle::LanguageKind Language) {
  FormatStyle Style = getLLVMStyle(Language);
  Style.ColumnLimit = 120;
  Style.TabWidth = 4;
  Style.IndentWidth = 4;
  Style.UseTab = FormatStyle::UT_Never;
  Style.BreakBeforeBraces = FormatStyle::BS_Custom;
  Style.BraceWrapping.AfterClass = true;
  Style.BraceWrapping.AfterControlStatement = FormatStyle::BWACS_Always;
  Style.BraceWrapping.AfterEnum = true;
  Style.BraceWrapping.AfterFunction = true;
  Style.BraceWrapping.AfterNamespace = true;
  Style.BraceWrapping.AfterObjCDeclaration = true;
  Style.BraceWrapping.AfterStruct = true;
  Style.BraceWrapping.AfterExternBlock = true;
  Style.BraceWrapping.BeforeCatch = true;
  Style.BraceWrapping.BeforeElse = true;
  Style.BraceWrapping.BeforeWhile = false;
  Style.PenaltyReturnTypeOnItsOwnLine = 1000;
  Style.AllowShortEnumsOnASingleLine = false;
  Style.AllowShortFunctionsOnASingleLine = FormatStyle::SFS_None;
  Style.AllowShortCaseLabelsOnASingleLine = false;
  Style.AllowShortIfStatementsOnASingleLine = FormatStyle::SIS_Never;
  Style.AllowShortLoopsOnASingleLine = false;
  Style.AlwaysBreakAfterDefinitionReturnType = FormatStyle::DRTBS_None;
  Style.BreakAfterReturnType = FormatStyle::RTBS_None;
  return Style;
}

FormatStyle getClangFormatStyle() {
  FormatStyle Style = getLLVMStyle();
  Style.InsertBraces = true;
  Style.InsertNewlineAtEOF = true;
  Style.IntegerLiteralSeparator.Decimal = 3;
  Style.IntegerLiteralSeparator.DecimalMinDigits = 5;
  Style.LineEnding = FormatStyle::LE_LF;
  Style.RemoveBracesLLVM = true;
  Style.RemoveParentheses = FormatStyle::RPS_ReturnStatement;
  Style.RemoveSemicolon = true;
  return Style;
}

FormatStyle getNoStyle() {
  FormatStyle NoStyle = getLLVMStyle();
  NoStyle.DisableFormat = true;
  NoStyle.SortIncludes = FormatStyle::SI_Never;
  NoStyle.SortUsingDeclarations = FormatStyle::SUD_Never;
  return NoStyle;
}

bool getPredefinedStyle(StringRef Name, FormatStyle::LanguageKind Language,
                        FormatStyle *Style) {
  if (Name.equals_insensitive("llvm"))
    *Style = getLLVMStyle(Language);
  else if (Name.equals_insensitive("chromium"))
    *Style = getChromiumStyle(Language);
  else if (Name.equals_insensitive("mozilla"))
    *Style = getMozillaStyle();
  else if (Name.equals_insensitive("google"))
    *Style = getGoogleStyle(Language);
  else if (Name.equals_insensitive("webkit"))
    *Style = getWebKitStyle();
  else if (Name.equals_insensitive("gnu"))
    *Style = getGNUStyle();
  else if (Name.equals_insensitive("microsoft"))
    *Style = getMicrosoftStyle(Language);
  else if (Name.equals_insensitive("clang-format"))
    *Style = getClangFormatStyle();
  else if (Name.equals_insensitive("none"))
    *Style = getNoStyle();
  else if (Name.equals_insensitive("inheritparentconfig"))
    Style->InheritsParentConfig = true;
  else
    return false;

  Style->Language = Language;
  return true;
}

ParseError validateQualifierOrder(FormatStyle *Style) {
  // If its empty then it means don't do anything.
  if (Style->QualifierOrder.empty())
    return ParseError::MissingQualifierOrder;

  // Ensure the list contains only currently valid qualifiers.
  for (const auto &Qualifier : Style->QualifierOrder) {
    if (Qualifier == "type")
      continue;
    auto token =
        LeftRightQualifierAlignmentFixer::getTokenFromQualifier(Qualifier);
    if (token == tok::identifier)
      return ParseError::InvalidQualifierSpecified;
  }

  // Ensure the list is unique (no duplicates).
  std::set<std::string> UniqueQualifiers(Style->QualifierOrder.begin(),
                                         Style->QualifierOrder.end());
  if (Style->QualifierOrder.size() != UniqueQualifiers.size()) {
    LLVM_DEBUG(llvm::dbgs()
               << "Duplicate Qualifiers " << Style->QualifierOrder.size()
               << " vs " << UniqueQualifiers.size() << "\n");
    return ParseError::DuplicateQualifierSpecified;
  }

  // Ensure the list has 'type' in it.
  if (!llvm::is_contained(Style->QualifierOrder, "type"))
    return ParseError::MissingQualifierType;

  return ParseError::Success;
}

std::error_code parseConfiguration(llvm::MemoryBufferRef Config,
                                   FormatStyle *Style, bool AllowUnknownOptions,
                                   llvm::SourceMgr::DiagHandlerTy DiagHandler,
                                   void *DiagHandlerCtxt) {
  assert(Style);
  FormatStyle::LanguageKind Language = Style->Language;
  assert(Language != FormatStyle::LK_None);
  if (Config.getBuffer().trim().empty())
    return make_error_code(ParseError::Success);
  Style->StyleSet.Clear();
  std::vector<FormatStyle> Styles;
  llvm::yaml::Input Input(Config, /*Ctxt=*/nullptr, DiagHandler,
                          DiagHandlerCtxt);
  // DocumentListTraits<vector<FormatStyle>> uses the context to get default
  // values for the fields, keys for which are missing from the configuration.
  // Mapping also uses the context to get the language to find the correct
  // base style.
  Input.setContext(Style);
  Input.setAllowUnknownKeys(AllowUnknownOptions);
  Input >> Styles;
  if (Input.error())
    return Input.error();

  for (unsigned i = 0; i < Styles.size(); ++i) {
    // Ensures that only the first configuration can skip the Language option.
    if (Styles[i].Language == FormatStyle::LK_None && i != 0)
      return make_error_code(ParseError::Error);
    // Ensure that each language is configured at most once.
    for (unsigned j = 0; j < i; ++j) {
      if (Styles[i].Language == Styles[j].Language) {
        LLVM_DEBUG(llvm::dbgs()
                   << "Duplicate languages in the config file on positions "
                   << j << " and " << i << "\n");
        return make_error_code(ParseError::Error);
      }
    }
  }
  // Look for a suitable configuration starting from the end, so we can
  // find the configuration for the specific language first, and the default
  // configuration (which can only be at slot 0) after it.
  FormatStyle::FormatStyleSet StyleSet;
  bool LanguageFound = false;
  for (const FormatStyle &Style : llvm::reverse(Styles)) {
    if (Style.Language != FormatStyle::LK_None)
      StyleSet.Add(Style);
    if (Style.Language == Language)
      LanguageFound = true;
  }
  if (!LanguageFound) {
    if (Styles.empty() || Styles[0].Language != FormatStyle::LK_None)
      return make_error_code(ParseError::Unsuitable);
    FormatStyle DefaultStyle = Styles[0];
    DefaultStyle.Language = Language;
    StyleSet.Add(std::move(DefaultStyle));
  }
  *Style = *StyleSet.Get(Language);
  if (Style->InsertTrailingCommas != FormatStyle::TCS_None &&
      Style->BinPackArguments) {
    // See comment on FormatStyle::TSC_Wrapped.
    return make_error_code(ParseError::BinPackTrailingCommaConflict);
  }
  if (Style->QualifierAlignment != FormatStyle::QAS_Leave)
    return make_error_code(validateQualifierOrder(Style));
  return make_error_code(ParseError::Success);
}

std::string configurationAsText(const FormatStyle &Style) {
  std::string Text;
  llvm::raw_string_ostream Stream(Text);
  llvm::yaml::Output Output(Stream);
  // We use the same mapping method for input and output, so we need a non-const
  // reference here.
  FormatStyle NonConstStyle = Style;
  expandPresetsBraceWrapping(NonConstStyle);
  expandPresetsSpaceBeforeParens(NonConstStyle);
  expandPresetsSpacesInParens(NonConstStyle);
  Output << NonConstStyle;

  return Stream.str();
}

std::optional<FormatStyle>
FormatStyle::FormatStyleSet::Get(FormatStyle::LanguageKind Language) const {
  if (!Styles)
    return std::nullopt;
  auto It = Styles->find(Language);
  if (It == Styles->end())
    return std::nullopt;
  FormatStyle Style = It->second;
  Style.StyleSet = *this;
  return Style;
}

void FormatStyle::FormatStyleSet::Add(FormatStyle Style) {
  assert(Style.Language != LK_None &&
         "Cannot add a style for LK_None to a StyleSet");
  assert(
      !Style.StyleSet.Styles &&
      "Cannot add a style associated with an existing StyleSet to a StyleSet");
  if (!Styles)
    Styles = std::make_shared<MapType>();
  (*Styles)[Style.Language] = std::move(Style);
}

void FormatStyle::FormatStyleSet::Clear() { Styles.reset(); }

std::optional<FormatStyle>
FormatStyle::GetLanguageStyle(FormatStyle::LanguageKind Language) const {
  return StyleSet.Get(Language);
}

namespace {

class ParensRemover : public TokenAnalyzer {
public:
  ParensRemover(const Environment &Env, const FormatStyle &Style)
      : TokenAnalyzer(Env, Style) {}

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override {
    AffectedRangeMgr.computeAffectedLines(AnnotatedLines);
    tooling::Replacements Result;
    removeParens(AnnotatedLines, Result);
    return {Result, 0};
  }

private:
  void removeParens(SmallVectorImpl<AnnotatedLine *> &Lines,
                    tooling::Replacements &Result) {
    const auto &SourceMgr = Env.getSourceManager();
    for (auto *Line : Lines) {
      removeParens(Line->Children, Result);
      if (!Line->Affected)
        continue;
      for (const auto *Token = Line->First; Token && !Token->Finalized;
           Token = Token->Next) {
        if (!Token->Optional || !Token->isOneOf(tok::l_paren, tok::r_paren))
          continue;
        auto *Next = Token->Next;
        assert(Next && Next->isNot(tok::eof));
        SourceLocation Start;
        if (Next->NewlinesBefore == 0) {
          Start = Token->Tok.getLocation();
          Next->WhitespaceRange = Token->WhitespaceRange;
        } else {
          Start = Token->WhitespaceRange.getBegin();
        }
        const auto &Range =
            CharSourceRange::getCharRange(Start, Token->Tok.getEndLoc());
        cantFail(Result.add(tooling::Replacement(SourceMgr, Range, " ")));
      }
    }
  }
};

class BracesInserter : public TokenAnalyzer {
public:
  BracesInserter(const Environment &Env, const FormatStyle &Style)
      : TokenAnalyzer(Env, Style) {}

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override {
    AffectedRangeMgr.computeAffectedLines(AnnotatedLines);
    tooling::Replacements Result;
    insertBraces(AnnotatedLines, Result);
    return {Result, 0};
  }

private:
  void insertBraces(SmallVectorImpl<AnnotatedLine *> &Lines,
                    tooling::Replacements &Result) {
    const auto &SourceMgr = Env.getSourceManager();
    int OpeningBraceSurplus = 0;
    for (AnnotatedLine *Line : Lines) {
      insertBraces(Line->Children, Result);
      if (!Line->Affected && OpeningBraceSurplus == 0)
        continue;
      for (FormatToken *Token = Line->First; Token && !Token->Finalized;
           Token = Token->Next) {
        int BraceCount = Token->BraceCount;
        if (BraceCount == 0)
          continue;
        std::string Brace;
        if (BraceCount < 0) {
          assert(BraceCount == -1);
          if (!Line->Affected)
            break;
          Brace = Token->is(tok::comment) ? "\n{" : "{";
          ++OpeningBraceSurplus;
        } else {
          if (OpeningBraceSurplus == 0)
            break;
          if (OpeningBraceSurplus < BraceCount)
            BraceCount = OpeningBraceSurplus;
          Brace = '\n' + std::string(BraceCount, '}');
          OpeningBraceSurplus -= BraceCount;
        }
        Token->BraceCount = 0;
        const auto Start = Token->Tok.getEndLoc();
        cantFail(Result.add(tooling::Replacement(SourceMgr, Start, 0, Brace)));
      }
    }
    assert(OpeningBraceSurplus == 0);
  }
};

class BracesRemover : public TokenAnalyzer {
public:
  BracesRemover(const Environment &Env, const FormatStyle &Style)
      : TokenAnalyzer(Env, Style) {}

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override {
    AffectedRangeMgr.computeAffectedLines(AnnotatedLines);
    tooling::Replacements Result;
    removeBraces(AnnotatedLines, Result);
    return {Result, 0};
  }

private:
  void removeBraces(SmallVectorImpl<AnnotatedLine *> &Lines,
                    tooling::Replacements &Result) {
    const auto &SourceMgr = Env.getSourceManager();
    const auto End = Lines.end();
    for (auto I = Lines.begin(); I != End; ++I) {
      const auto Line = *I;
      removeBraces(Line->Children, Result);
      if (!Line->Affected)
        continue;
      const auto NextLine = I + 1 == End ? nullptr : I[1];
      for (auto Token = Line->First; Token && !Token->Finalized;
           Token = Token->Next) {
        if (!Token->Optional)
          continue;
        if (!Token->isOneOf(tok::l_brace, tok::r_brace))
          continue;
        auto Next = Token->Next;
        assert(Next || Token == Line->Last);
        if (!Next && NextLine)
          Next = NextLine->First;
        SourceLocation Start;
        if (Next && Next->NewlinesBefore == 0 && Next->isNot(tok::eof)) {
          Start = Token->Tok.getLocation();
          Next->WhitespaceRange = Token->WhitespaceRange;
        } else {
          Start = Token->WhitespaceRange.getBegin();
        }
        const auto Range =
            CharSourceRange::getCharRange(Start, Token->Tok.getEndLoc());
        cantFail(Result.add(tooling::Replacement(SourceMgr, Range, "")));
      }
    }
  }
};

class SemiRemover : public TokenAnalyzer {
public:
  SemiRemover(const Environment &Env, const FormatStyle &Style)
      : TokenAnalyzer(Env, Style) {}

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override {
    AffectedRangeMgr.computeAffectedLines(AnnotatedLines);
    tooling::Replacements Result;
    removeSemi(Annotator, AnnotatedLines, Result);
    return {Result, 0};
  }

private:
  void removeSemi(TokenAnnotator &Annotator,
                  SmallVectorImpl<AnnotatedLine *> &Lines,
                  tooling::Replacements &Result) {
    auto PrecededByFunctionRBrace = [](const FormatToken &Tok) {
      const auto *Prev = Tok.Previous;
      if (!Prev || Prev->isNot(tok::r_brace))
        return false;
      const auto *LBrace = Prev->MatchingParen;
      return LBrace && LBrace->is(TT_FunctionLBrace);
    };
    const auto &SourceMgr = Env.getSourceManager();
    const auto End = Lines.end();
    for (auto I = Lines.begin(); I != End; ++I) {
      const auto Line = *I;
      removeSemi(Annotator, Line->Children, Result);
      if (!Line->Affected)
        continue;
      Annotator.calculateFormattingInformation(*Line);
      const auto NextLine = I + 1 == End ? nullptr : I[1];
      for (auto Token = Line->First; Token && !Token->Finalized;
           Token = Token->Next) {
        if (Token->isNot(tok::semi) ||
            (!Token->Optional && !PrecededByFunctionRBrace(*Token))) {
          continue;
        }
        auto Next = Token->Next;
        assert(Next || Token == Line->Last);
        if (!Next && NextLine)
          Next = NextLine->First;
        SourceLocation Start;
        if (Next && Next->NewlinesBefore == 0 && Next->isNot(tok::eof)) {
          Start = Token->Tok.getLocation();
          Next->WhitespaceRange = Token->WhitespaceRange;
        } else {
          Start = Token->WhitespaceRange.getBegin();
        }
        const auto Range =
            CharSourceRange::getCharRange(Start, Token->Tok.getEndLoc());
        cantFail(Result.add(tooling::Replacement(SourceMgr, Range, "")));
      }
    }
  }
};

class JavaScriptRequoter : public TokenAnalyzer {
public:
  JavaScriptRequoter(const Environment &Env, const FormatStyle &Style)
      : TokenAnalyzer(Env, Style) {}

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override {
    AffectedRangeMgr.computeAffectedLines(AnnotatedLines);
    tooling::Replacements Result;
    requoteJSStringLiteral(AnnotatedLines, Result);
    return {Result, 0};
  }

private:
  // Replaces double/single-quoted string literal as appropriate, re-escaping
  // the contents in the process.
  void requoteJSStringLiteral(SmallVectorImpl<AnnotatedLine *> &Lines,
                              tooling::Replacements &Result) {
    for (AnnotatedLine *Line : Lines) {
      requoteJSStringLiteral(Line->Children, Result);
      if (!Line->Affected)
        continue;
      for (FormatToken *FormatTok = Line->First; FormatTok;
           FormatTok = FormatTok->Next) {
        StringRef Input = FormatTok->TokenText;
        if (FormatTok->Finalized || !FormatTok->isStringLiteral() ||
            // NB: testing for not starting with a double quote to avoid
            // breaking `template strings`.
            (Style.JavaScriptQuotes == FormatStyle::JSQS_Single &&
             !Input.starts_with("\"")) ||
            (Style.JavaScriptQuotes == FormatStyle::JSQS_Double &&
             !Input.starts_with("\'"))) {
          continue;
        }

        // Change start and end quote.
        bool IsSingle = Style.JavaScriptQuotes == FormatStyle::JSQS_Single;
        SourceLocation Start = FormatTok->Tok.getLocation();
        auto Replace = [&](SourceLocation Start, unsigned Length,
                           StringRef ReplacementText) {
          auto Err = Result.add(tooling::Replacement(
              Env.getSourceManager(), Start, Length, ReplacementText));
          // FIXME: handle error. For now, print error message and skip the
          // replacement for release version.
          if (Err) {
            llvm::errs() << toString(std::move(Err)) << "\n";
            assert(false);
          }
        };
        Replace(Start, 1, IsSingle ? "'" : "\"");
        Replace(FormatTok->Tok.getEndLoc().getLocWithOffset(-1), 1,
                IsSingle ? "'" : "\"");

        // Escape internal quotes.
        bool Escaped = false;
        for (size_t i = 1; i < Input.size() - 1; i++) {
          switch (Input[i]) {
          case '\\':
            if (!Escaped && i + 1 < Input.size() &&
                ((IsSingle && Input[i + 1] == '"') ||
                 (!IsSingle && Input[i + 1] == '\''))) {
              // Remove this \, it's escaping a " or ' that no longer needs
              // escaping
              Replace(Start.getLocWithOffset(i), 1, "");
              continue;
            }
            Escaped = !Escaped;
            break;
          case '\"':
          case '\'':
            if (!Escaped && IsSingle == (Input[i] == '\'')) {
              // Escape the quote.
              Replace(Start.getLocWithOffset(i), 0, "\\");
            }
            Escaped = false;
            break;
          default:
            Escaped = false;
            break;
          }
        }
      }
    }
  }
};

class Formatter : public TokenAnalyzer {
public:
  Formatter(const Environment &Env, const FormatStyle &Style,
            FormattingAttemptStatus *Status)
      : TokenAnalyzer(Env, Style), Status(Status) {}

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override {
    tooling::Replacements Result;
    deriveLocalStyle(AnnotatedLines);
    AffectedRangeMgr.computeAffectedLines(AnnotatedLines);
    for (AnnotatedLine *Line : AnnotatedLines)
      Annotator.calculateFormattingInformation(*Line);
    Annotator.setCommentLineLevels(AnnotatedLines);

    WhitespaceManager Whitespaces(
        Env.getSourceManager(), Style,
        Style.LineEnding > FormatStyle::LE_CRLF
            ? WhitespaceManager::inputUsesCRLF(
                  Env.getSourceManager().getBufferData(Env.getFileID()),
                  Style.LineEnding == FormatStyle::LE_DeriveCRLF)
            : Style.LineEnding == FormatStyle::LE_CRLF);
    ContinuationIndenter Indenter(Style, Tokens.getKeywords(),
                                  Env.getSourceManager(), Whitespaces, Encoding,
                                  BinPackInconclusiveFunctions);
    unsigned Penalty =
        UnwrappedLineFormatter(&Indenter, &Whitespaces, Style,
                               Tokens.getKeywords(), Env.getSourceManager(),
                               Status)
            .format(AnnotatedLines, /*DryRun=*/false,
                    /*AdditionalIndent=*/0,
                    /*FixBadIndentation=*/false,
                    /*FirstStartColumn=*/Env.getFirstStartColumn(),
                    /*NextStartColumn=*/Env.getNextStartColumn(),
                    /*LastStartColumn=*/Env.getLastStartColumn());
    for (const auto &R : Whitespaces.generateReplacements())
      if (Result.add(R))
        return std::make_pair(Result, 0);
    return std::make_pair(Result, Penalty);
  }

private:
  bool
  hasCpp03IncompatibleFormat(const SmallVectorImpl<AnnotatedLine *> &Lines) {
    for (const AnnotatedLine *Line : Lines) {
      if (hasCpp03IncompatibleFormat(Line->Children))
        return true;
      for (FormatToken *Tok = Line->First->Next; Tok; Tok = Tok->Next) {
        if (!Tok->hasWhitespaceBefore()) {
          if (Tok->is(tok::coloncolon) && Tok->Previous->is(TT_TemplateOpener))
            return true;
          if (Tok->is(TT_TemplateCloser) &&
              Tok->Previous->is(TT_TemplateCloser)) {
            return true;
          }
        }
      }
    }
    return false;
  }

  int countVariableAlignments(const SmallVectorImpl<AnnotatedLine *> &Lines) {
    int AlignmentDiff = 0;
    for (const AnnotatedLine *Line : Lines) {
      AlignmentDiff += countVariableAlignments(Line->Children);
      for (FormatToken *Tok = Line->First; Tok && Tok->Next; Tok = Tok->Next) {
        if (Tok->isNot(TT_PointerOrReference))
          continue;
        // Don't treat space in `void foo() &&` as evidence.
        if (const auto *Prev = Tok->getPreviousNonComment()) {
          if (Prev->is(tok::r_paren) && Prev->MatchingParen) {
            if (const auto *Func =
                    Prev->MatchingParen->getPreviousNonComment()) {
              if (Func->isOneOf(TT_FunctionDeclarationName, TT_StartOfName,
                                TT_OverloadedOperator)) {
                continue;
              }
            }
          }
        }
        bool SpaceBefore = Tok->hasWhitespaceBefore();
        bool SpaceAfter = Tok->Next->hasWhitespaceBefore();
        if (SpaceBefore && !SpaceAfter)
          ++AlignmentDiff;
        if (!SpaceBefore && SpaceAfter)
          --AlignmentDiff;
      }
    }
    return AlignmentDiff;
  }

  void
  deriveLocalStyle(const SmallVectorImpl<AnnotatedLine *> &AnnotatedLines) {
    bool HasBinPackedFunction = false;
    bool HasOnePerLineFunction = false;
    for (AnnotatedLine *Line : AnnotatedLines) {
      if (!Line->First->Next)
        continue;
      FormatToken *Tok = Line->First->Next;
      while (Tok->Next) {
        if (Tok->is(PPK_BinPacked))
          HasBinPackedFunction = true;
        if (Tok->is(PPK_OnePerLine))
          HasOnePerLineFunction = true;

        Tok = Tok->Next;
      }
    }
    if (Style.DerivePointerAlignment) {
      const auto NetRightCount = countVariableAlignments(AnnotatedLines);
      if (NetRightCount > 0)
        Style.PointerAlignment = FormatStyle::PAS_Right;
      else if (NetRightCount < 0)
        Style.PointerAlignment = FormatStyle::PAS_Left;
      Style.ReferenceAlignment = FormatStyle::RAS_Pointer;
    }
    if (Style.Standard == FormatStyle::LS_Auto) {
      Style.Standard = hasCpp03IncompatibleFormat(AnnotatedLines)
                           ? FormatStyle::LS_Latest
                           : FormatStyle::LS_Cpp03;
    }
    BinPackInconclusiveFunctions =
        HasBinPackedFunction || !HasOnePerLineFunction;
  }

  bool BinPackInconclusiveFunctions;
  FormattingAttemptStatus *Status;
};

/// TrailingCommaInserter inserts trailing commas into container literals.
/// E.g.:
///     const x = [
///       1,
///     ];
/// TrailingCommaInserter runs after formatting. To avoid causing a required
/// reformatting (and thus reflow), it never inserts a comma that'd exceed the
/// ColumnLimit.
///
/// Because trailing commas disable binpacking of arrays, TrailingCommaInserter
/// is conceptually incompatible with bin packing.
class TrailingCommaInserter : public TokenAnalyzer {
public:
  TrailingCommaInserter(const Environment &Env, const FormatStyle &Style)
      : TokenAnalyzer(Env, Style) {}

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override {
    AffectedRangeMgr.computeAffectedLines(AnnotatedLines);
    tooling::Replacements Result;
    insertTrailingCommas(AnnotatedLines, Result);
    return {Result, 0};
  }

private:
  /// Inserts trailing commas in [] and {} initializers if they wrap over
  /// multiple lines.
  void insertTrailingCommas(SmallVectorImpl<AnnotatedLine *> &Lines,
                            tooling::Replacements &Result) {
    for (AnnotatedLine *Line : Lines) {
      insertTrailingCommas(Line->Children, Result);
      if (!Line->Affected)
        continue;
      for (FormatToken *FormatTok = Line->First; FormatTok;
           FormatTok = FormatTok->Next) {
        if (FormatTok->NewlinesBefore == 0)
          continue;
        FormatToken *Matching = FormatTok->MatchingParen;
        if (!Matching || !FormatTok->getPreviousNonComment())
          continue;
        if (!(FormatTok->is(tok::r_square) &&
              Matching->is(TT_ArrayInitializerLSquare)) &&
            !(FormatTok->is(tok::r_brace) && Matching->is(TT_DictLiteral))) {
          continue;
        }
        FormatToken *Prev = FormatTok->getPreviousNonComment();
        if (Prev->is(tok::comma) || Prev->is(tok::semi))
          continue;
        // getEndLoc is not reliably set during re-lexing, use text length
        // instead.
        SourceLocation Start =
            Prev->Tok.getLocation().getLocWithOffset(Prev->TokenText.size());
        // If inserting a comma would push the code over the column limit, skip
        // this location - it'd introduce an unstable formatting due to the
        // required reflow.
        unsigned ColumnNumber =
            Env.getSourceManager().getSpellingColumnNumber(Start);
        if (ColumnNumber > Style.ColumnLimit)
          continue;
        // Comma insertions cannot conflict with each other, and this pass has a
        // clean set of Replacements, so the operation below cannot fail.
        cantFail(Result.add(
            tooling::Replacement(Env.getSourceManager(), Start, 0, ",")));
      }
    }
  }
};

// This class clean up the erroneous/redundant code around the given ranges in
// file.
class Cleaner : public TokenAnalyzer {
public:
  Cleaner(const Environment &Env, const FormatStyle &Style)
      : TokenAnalyzer(Env, Style),
        DeletedTokens(FormatTokenLess(Env.getSourceManager())) {}

  // FIXME: eliminate unused parameters.
  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override {
    // FIXME: in the current implementation the granularity of affected range
    // is an annotated line. However, this is not sufficient. Furthermore,
    // redundant code introduced by replacements does not necessarily
    // intercept with ranges of replacements that result in the redundancy.
    // To determine if some redundant code is actually introduced by
    // replacements(e.g. deletions), we need to come up with a more
    // sophisticated way of computing affected ranges.
    AffectedRangeMgr.computeAffectedLines(AnnotatedLines);

    checkEmptyNamespace(AnnotatedLines);

    for (auto *Line : AnnotatedLines)
      cleanupLine(Line);

    return {generateFixes(), 0};
  }

private:
  void cleanupLine(AnnotatedLine *Line) {
    for (auto *Child : Line->Children)
      cleanupLine(Child);

    if (Line->Affected) {
      cleanupRight(Line->First, tok::comma, tok::comma);
      cleanupRight(Line->First, TT_CtorInitializerColon, tok::comma);
      cleanupRight(Line->First, tok::l_paren, tok::comma);
      cleanupLeft(Line->First, tok::comma, tok::r_paren);
      cleanupLeft(Line->First, TT_CtorInitializerComma, tok::l_brace);
      cleanupLeft(Line->First, TT_CtorInitializerColon, tok::l_brace);
      cleanupLeft(Line->First, TT_CtorInitializerColon, tok::equal);
    }
  }

  bool containsOnlyComments(const AnnotatedLine &Line) {
    for (FormatToken *Tok = Line.First; Tok; Tok = Tok->Next)
      if (Tok->isNot(tok::comment))
        return false;
    return true;
  }

  // Iterate through all lines and remove any empty (nested) namespaces.
  void checkEmptyNamespace(SmallVectorImpl<AnnotatedLine *> &AnnotatedLines) {
    std::set<unsigned> DeletedLines;
    for (unsigned i = 0, e = AnnotatedLines.size(); i != e; ++i) {
      auto &Line = *AnnotatedLines[i];
      if (Line.startsWithNamespace())
        checkEmptyNamespace(AnnotatedLines, i, i, DeletedLines);
    }

    for (auto Line : DeletedLines) {
      FormatToken *Tok = AnnotatedLines[Line]->First;
      while (Tok) {
        deleteToken(Tok);
        Tok = Tok->Next;
      }
    }
  }

  // The function checks if the namespace, which starts from \p CurrentLine, and
  // its nested namespaces are empty and delete them if they are empty. It also
  // sets \p NewLine to the last line checked.
  // Returns true if the current namespace is empty.
  bool checkEmptyNamespace(SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
                           unsigned CurrentLine, unsigned &NewLine,
                           std::set<unsigned> &DeletedLines) {
    unsigned InitLine = CurrentLine, End = AnnotatedLines.size();
    if (Style.BraceWrapping.AfterNamespace) {
      // If the left brace is in a new line, we should consume it first so that
      // it does not make the namespace non-empty.
      // FIXME: error handling if there is no left brace.
      if (!AnnotatedLines[++CurrentLine]->startsWith(tok::l_brace)) {
        NewLine = CurrentLine;
        return false;
      }
    } else if (!AnnotatedLines[CurrentLine]->endsWith(tok::l_brace)) {
      return false;
    }
    while (++CurrentLine < End) {
      if (AnnotatedLines[CurrentLine]->startsWith(tok::r_brace))
        break;

      if (AnnotatedLines[CurrentLine]->startsWithNamespace()) {
        if (!checkEmptyNamespace(AnnotatedLines, CurrentLine, NewLine,
                                 DeletedLines)) {
          return false;
        }
        CurrentLine = NewLine;
        continue;
      }

      if (containsOnlyComments(*AnnotatedLines[CurrentLine]))
        continue;

      // If there is anything other than comments or nested namespaces in the
      // current namespace, the namespace cannot be empty.
      NewLine = CurrentLine;
      return false;
    }

    NewLine = CurrentLine;
    if (CurrentLine >= End)
      return false;

    // Check if the empty namespace is actually affected by changed ranges.
    if (!AffectedRangeMgr.affectsCharSourceRange(CharSourceRange::getCharRange(
            AnnotatedLines[InitLine]->First->Tok.getLocation(),
            AnnotatedLines[CurrentLine]->Last->Tok.getEndLoc()))) {
      return false;
    }

    for (unsigned i = InitLine; i <= CurrentLine; ++i)
      DeletedLines.insert(i);

    return true;
  }

  // Checks pairs {start, start->next},..., {end->previous, end} and deletes one
  // of the token in the pair if the left token has \p LK token kind and the
  // right token has \p RK token kind. If \p DeleteLeft is true, the left token
  // is deleted on match; otherwise, the right token is deleted.
  template <typename LeftKind, typename RightKind>
  void cleanupPair(FormatToken *Start, LeftKind LK, RightKind RK,
                   bool DeleteLeft) {
    auto NextNotDeleted = [this](const FormatToken &Tok) -> FormatToken * {
      for (auto *Res = Tok.Next; Res; Res = Res->Next) {
        if (Res->isNot(tok::comment) &&
            DeletedTokens.find(Res) == DeletedTokens.end()) {
          return Res;
        }
      }
      return nullptr;
    };
    for (auto *Left = Start; Left;) {
      auto *Right = NextNotDeleted(*Left);
      if (!Right)
        break;
      if (Left->is(LK) && Right->is(RK)) {
        deleteToken(DeleteLeft ? Left : Right);
        for (auto *Tok = Left->Next; Tok && Tok != Right; Tok = Tok->Next)
          deleteToken(Tok);
        // If the right token is deleted, we should keep the left token
        // unchanged and pair it with the new right token.
        if (!DeleteLeft)
          continue;
      }
      Left = Right;
    }
  }

  template <typename LeftKind, typename RightKind>
  void cleanupLeft(FormatToken *Start, LeftKind LK, RightKind RK) {
    cleanupPair(Start, LK, RK, /*DeleteLeft=*/true);
  }

  template <typename LeftKind, typename RightKind>
  void cleanupRight(FormatToken *Start, LeftKind LK, RightKind RK) {
    cleanupPair(Start, LK, RK, /*DeleteLeft=*/false);
  }

  // Delete the given token.
  inline void deleteToken(FormatToken *Tok) {
    if (Tok)
      DeletedTokens.insert(Tok);
  }

  tooling::Replacements generateFixes() {
    tooling::Replacements Fixes;
    SmallVector<FormatToken *> Tokens;
    std::copy(DeletedTokens.begin(), DeletedTokens.end(),
              std::back_inserter(Tokens));

    // Merge multiple continuous token deletions into one big deletion so that
    // the number of replacements can be reduced. This makes computing affected
    // ranges more efficient when we run reformat on the changed code.
    unsigned Idx = 0;
    while (Idx < Tokens.size()) {
      unsigned St = Idx, End = Idx;
      while ((End + 1) < Tokens.size() && Tokens[End]->Next == Tokens[End + 1])
        ++End;
      auto SR = CharSourceRange::getCharRange(Tokens[St]->Tok.getLocation(),
                                              Tokens[End]->Tok.getEndLoc());
      auto Err =
          Fixes.add(tooling::Replacement(Env.getSourceManager(), SR, ""));
      // FIXME: better error handling. for now just print error message and skip
      // for the release version.
      if (Err) {
        llvm::errs() << toString(std::move(Err)) << "\n";
        assert(false && "Fixes must not conflict!");
      }
      Idx = End + 1;
    }

    return Fixes;
  }

  // Class for less-than inequality comparason for the set `RedundantTokens`.
  // We store tokens in the order they appear in the translation unit so that
  // we do not need to sort them in `generateFixes()`.
  struct FormatTokenLess {
    FormatTokenLess(const SourceManager &SM) : SM(SM) {}

    bool operator()(const FormatToken *LHS, const FormatToken *RHS) const {
      return SM.isBeforeInTranslationUnit(LHS->Tok.getLocation(),
                                          RHS->Tok.getLocation());
    }
    const SourceManager &SM;
  };

  // Tokens to be deleted.
  std::set<FormatToken *, FormatTokenLess> DeletedTokens;
};

class ObjCHeaderStyleGuesser : public TokenAnalyzer {
public:
  ObjCHeaderStyleGuesser(const Environment &Env, const FormatStyle &Style)
      : TokenAnalyzer(Env, Style), IsObjC(false) {}

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override {
    assert(Style.Language == FormatStyle::LK_Cpp);
    IsObjC = guessIsObjC(Env.getSourceManager(), AnnotatedLines,
                         Tokens.getKeywords());
    tooling::Replacements Result;
    return {Result, 0};
  }

  bool isObjC() { return IsObjC; }

private:
  static bool
  guessIsObjC(const SourceManager &SourceManager,
              const SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
              const AdditionalKeywords &Keywords) {
    // Keep this array sorted, since we are binary searching over it.
    static constexpr llvm::StringLiteral FoundationIdentifiers[] = {
        "CGFloat",
        "CGPoint",
        "CGPointMake",
        "CGPointZero",
        "CGRect",
        "CGRectEdge",
        "CGRectInfinite",
        "CGRectMake",
        "CGRectNull",
        "CGRectZero",
        "CGSize",
        "CGSizeMake",
        "CGVector",
        "CGVectorMake",
        "FOUNDATION_EXPORT", // This is an alias for FOUNDATION_EXTERN.
        "FOUNDATION_EXTERN",
        "NSAffineTransform",
        "NSArray",
        "NSAttributedString",
        "NSBlockOperation",
        "NSBundle",
        "NSCache",
        "NSCalendar",
        "NSCharacterSet",
        "NSCountedSet",
        "NSData",
        "NSDataDetector",
        "NSDecimal",
        "NSDecimalNumber",
        "NSDictionary",
        "NSEdgeInsets",
        "NSError",
        "NSErrorDomain",
        "NSHashTable",
        "NSIndexPath",
        "NSIndexSet",
        "NSInteger",
        "NSInvocationOperation",
        "NSLocale",
        "NSMapTable",
        "NSMutableArray",
        "NSMutableAttributedString",
        "NSMutableCharacterSet",
        "NSMutableData",
        "NSMutableDictionary",
        "NSMutableIndexSet",
        "NSMutableOrderedSet",
        "NSMutableSet",
        "NSMutableString",
        "NSNumber",
        "NSNumberFormatter",
        "NSObject",
        "NSOperation",
        "NSOperationQueue",
        "NSOperationQueuePriority",
        "NSOrderedSet",
        "NSPoint",
        "NSPointerArray",
        "NSQualityOfService",
        "NSRange",
        "NSRect",
        "NSRegularExpression",
        "NSSet",
        "NSSize",
        "NSString",
        "NSTimeZone",
        "NSUInteger",
        "NSURL",
        "NSURLComponents",
        "NSURLQueryItem",
        "NSUUID",
        "NSValue",
        "NS_ASSUME_NONNULL_BEGIN",
        "UIImage",
        "UIView",
    };

    for (auto *Line : AnnotatedLines) {
      if (Line->First && (Line->First->TokenText.starts_with("#") ||
                          Line->First->TokenText == "__pragma" ||
                          Line->First->TokenText == "_Pragma")) {
        continue;
      }
      for (const FormatToken *FormatTok = Line->First; FormatTok;
           FormatTok = FormatTok->Next) {
        if ((FormatTok->Previous && FormatTok->Previous->is(tok::at) &&
             (FormatTok->Tok.getObjCKeywordID() != tok::objc_not_keyword ||
              FormatTok->isOneOf(tok::numeric_constant, tok::l_square,
                                 tok::l_brace))) ||
            (FormatTok->Tok.isAnyIdentifier() &&
             std::binary_search(std::begin(FoundationIdentifiers),
                                std::end(FoundationIdentifiers),
                                FormatTok->TokenText)) ||
            FormatTok->is(TT_ObjCStringLiteral) ||
            FormatTok->isOneOf(Keywords.kw_NS_CLOSED_ENUM, Keywords.kw_NS_ENUM,
                               Keywords.kw_NS_ERROR_ENUM,
                               Keywords.kw_NS_OPTIONS, TT_ObjCBlockLBrace,
                               TT_ObjCBlockLParen, TT_ObjCDecl, TT_ObjCForIn,
                               TT_ObjCMethodExpr, TT_ObjCMethodSpecifier,
                               TT_ObjCProperty)) {
          LLVM_DEBUG(llvm::dbgs()
                     << "Detected ObjC at location "
                     << FormatTok->Tok.getLocation().printToString(
                            SourceManager)
                     << " token: " << FormatTok->TokenText << " token type: "
                     << getTokenTypeName(FormatTok->getType()) << "\n");
          return true;
        }
      }
      if (guessIsObjC(SourceManager, Line->Children, Keywords))
        return true;
    }
    return false;
  }

  bool IsObjC;
};

struct IncludeDirective {
  StringRef Filename;
  StringRef Text;
  unsigned Offset;
  int Category;
  int Priority;
};

struct JavaImportDirective {
  StringRef Identifier;
  StringRef Text;
  unsigned Offset;
  SmallVector<StringRef> AssociatedCommentLines;
  bool IsStatic;
};

} // end anonymous namespace

// Determines whether 'Ranges' intersects with ('Start', 'End').
static bool affectsRange(ArrayRef<tooling::Range> Ranges, unsigned Start,
                         unsigned End) {
  for (const auto &Range : Ranges) {
    if (Range.getOffset() < End &&
        Range.getOffset() + Range.getLength() > Start) {
      return true;
    }
  }
  return false;
}

// Returns a pair (Index, OffsetToEOL) describing the position of the cursor
// before sorting/deduplicating. Index is the index of the include under the
// cursor in the original set of includes. If this include has duplicates, it is
// the index of the first of the duplicates as the others are going to be
// removed. OffsetToEOL describes the cursor's position relative to the end of
// its current line.
// If `Cursor` is not on any #include, `Index` will be UINT_MAX.
static std::pair<unsigned, unsigned>
FindCursorIndex(const SmallVectorImpl<IncludeDirective> &Includes,
                const SmallVectorImpl<unsigned> &Indices, unsigned Cursor) {
  unsigned CursorIndex = UINT_MAX;
  unsigned OffsetToEOL = 0;
  for (int i = 0, e = Includes.size(); i != e; ++i) {
    unsigned Start = Includes[Indices[i]].Offset;
    unsigned End = Start + Includes[Indices[i]].Text.size();
    if (!(Cursor >= Start && Cursor < End))
      continue;
    CursorIndex = Indices[i];
    OffsetToEOL = End - Cursor;
    // Put the cursor on the only remaining #include among the duplicate
    // #includes.
    while (--i >= 0 && Includes[CursorIndex].Text == Includes[Indices[i]].Text)
      CursorIndex = i;
    break;
  }
  return std::make_pair(CursorIndex, OffsetToEOL);
}

// Replace all "\r\n" with "\n".
std::string replaceCRLF(const std::string &Code) {
  std::string NewCode;
  size_t Pos = 0, LastPos = 0;

  do {
    Pos = Code.find("\r\n", LastPos);
    if (Pos == LastPos) {
      ++LastPos;
      continue;
    }
    if (Pos == std::string::npos) {
      NewCode += Code.substr(LastPos);
      break;
    }
    NewCode += Code.substr(LastPos, Pos - LastPos) + "\n";
    LastPos = Pos + 2;
  } while (Pos != std::string::npos);

  return NewCode;
}

// Sorts and deduplicate a block of includes given by 'Includes' alphabetically
// adding the necessary replacement to 'Replaces'. 'Includes' must be in strict
// source order.
// #include directives with the same text will be deduplicated, and only the
// first #include in the duplicate #includes remains. If the `Cursor` is
// provided and put on a deleted #include, it will be moved to the remaining
// #include in the duplicate #includes.
static void sortCppIncludes(const FormatStyle &Style,
                            const SmallVectorImpl<IncludeDirective> &Includes,
                            ArrayRef<tooling::Range> Ranges, StringRef FileName,
                            StringRef Code, tooling::Replacements &Replaces,
                            unsigned *Cursor) {
  tooling::IncludeCategoryManager Categories(Style.IncludeStyle, FileName);
  const unsigned IncludesBeginOffset = Includes.front().Offset;
  const unsigned IncludesEndOffset =
      Includes.back().Offset + Includes.back().Text.size();
  const unsigned IncludesBlockSize = IncludesEndOffset - IncludesBeginOffset;
  if (!affectsRange(Ranges, IncludesBeginOffset, IncludesEndOffset))
    return;
  SmallVector<unsigned, 16> Indices =
      llvm::to_vector<16>(llvm::seq<unsigned>(0, Includes.size()));

  if (Style.SortIncludes == FormatStyle::SI_CaseInsensitive) {
    stable_sort(Indices, [&](unsigned LHSI, unsigned RHSI) {
      const auto LHSFilenameLower = Includes[LHSI].Filename.lower();
      const auto RHSFilenameLower = Includes[RHSI].Filename.lower();
      return std::tie(Includes[LHSI].Priority, LHSFilenameLower,
                      Includes[LHSI].Filename) <
             std::tie(Includes[RHSI].Priority, RHSFilenameLower,
                      Includes[RHSI].Filename);
    });
  } else {
    stable_sort(Indices, [&](unsigned LHSI, unsigned RHSI) {
      return std::tie(Includes[LHSI].Priority, Includes[LHSI].Filename) <
             std::tie(Includes[RHSI].Priority, Includes[RHSI].Filename);
    });
  }

  // The index of the include on which the cursor will be put after
  // sorting/deduplicating.
  unsigned CursorIndex;
  // The offset from cursor to the end of line.
  unsigned CursorToEOLOffset;
  if (Cursor) {
    std::tie(CursorIndex, CursorToEOLOffset) =
        FindCursorIndex(Includes, Indices, *Cursor);
  }

  // Deduplicate #includes.
  Indices.erase(std::unique(Indices.begin(), Indices.end(),
                            [&](unsigned LHSI, unsigned RHSI) {
                              return Includes[LHSI].Text.trim() ==
                                     Includes[RHSI].Text.trim();
                            }),
                Indices.end());

  int CurrentCategory = Includes.front().Category;

  // If the #includes are out of order, we generate a single replacement fixing
  // the entire block. Otherwise, no replacement is generated.
  // In case Style.IncldueStyle.IncludeBlocks != IBS_Preserve, this check is not
  // enough as additional newlines might be added or removed across #include
  // blocks. This we handle below by generating the updated #include blocks and
  // comparing it to the original.
  if (Indices.size() == Includes.size() && is_sorted(Indices) &&
      Style.IncludeStyle.IncludeBlocks == tooling::IncludeStyle::IBS_Preserve) {
    return;
  }

  const auto OldCursor = Cursor ? *Cursor : 0;
  std::string result;
  for (unsigned Index : Indices) {
    if (!result.empty()) {
      result += "\n";
      if (Style.IncludeStyle.IncludeBlocks ==
              tooling::IncludeStyle::IBS_Regroup &&
          CurrentCategory != Includes[Index].Category) {
        result += "\n";
      }
    }
    result += Includes[Index].Text;
    if (Cursor && CursorIndex == Index)
      *Cursor = IncludesBeginOffset + result.size() - CursorToEOLOffset;
    CurrentCategory = Includes[Index].Category;
  }

  if (Cursor && *Cursor >= IncludesEndOffset)
    *Cursor += result.size() - IncludesBlockSize;

  // If the #includes are out of order, we generate a single replacement fixing
  // the entire range of blocks. Otherwise, no replacement is generated.
  if (replaceCRLF(result) == replaceCRLF(std::string(Code.substr(
                                 IncludesBeginOffset, IncludesBlockSize)))) {
    if (Cursor)
      *Cursor = OldCursor;
    return;
  }

  auto Err = Replaces.add(tooling::Replacement(
      FileName, Includes.front().Offset, IncludesBlockSize, result));
  // FIXME: better error handling. For now, just skip the replacement for the
  // release version.
  if (Err) {
    llvm::errs() << toString(std::move(Err)) << "\n";
    assert(false);
  }
}

tooling::Replacements sortCppIncludes(const FormatStyle &Style, StringRef Code,
                                      ArrayRef<tooling::Range> Ranges,
                                      StringRef FileName,
                                      tooling::Replacements &Replaces,
                                      unsigned *Cursor) {
  unsigned Prev = llvm::StringSwitch<size_t>(Code)
                      .StartsWith("\xEF\xBB\xBF", 3) // UTF-8 BOM
                      .Default(0);
  unsigned SearchFrom = 0;
  SmallVector<StringRef, 4> Matches;
  SmallVector<IncludeDirective, 16> IncludesInBlock;

  // In compiled files, consider the first #include to be the main #include of
  // the file if it is not a system #include. This ensures that the header
  // doesn't have hidden dependencies
  // (http://llvm.org/docs/CodingStandards.html#include-style).
  //
  // FIXME: Do some validation, e.g. edit distance of the base name, to fix
  // cases where the first #include is unlikely to be the main header.
  tooling::IncludeCategoryManager Categories(Style.IncludeStyle, FileName);
  bool FirstIncludeBlock = true;
  bool MainIncludeFound = false;
  bool FormattingOff = false;

  // '[' must be the first and '-' the last character inside [...].
  llvm::Regex RawStringRegex(
      "R\"([][A-Za-z0-9_{}#<>%:;.?*+/^&\\$|~!=,'-]*)\\(");
  SmallVector<StringRef, 2> RawStringMatches;
  std::string RawStringTermination = ")\"";

  for (;;) {
    auto Pos = Code.find('\n', SearchFrom);
    StringRef Line =
        Code.substr(Prev, (Pos != StringRef::npos ? Pos : Code.size()) - Prev);

    StringRef Trimmed = Line.trim();

    // #includes inside raw string literals need to be ignored.
    // or we will sort the contents of the string.
    // Skip past until we think we are at the rawstring literal close.
    if (RawStringRegex.match(Trimmed, &RawStringMatches)) {
      std::string CharSequence = RawStringMatches[1].str();
      RawStringTermination = ")" + CharSequence + "\"";
      FormattingOff = true;
    }

    if (Trimmed.contains(RawStringTermination))
      FormattingOff = false;

    bool IsBlockComment = false;

    if (isClangFormatOff(Trimmed)) {
      FormattingOff = true;
    } else if (isClangFormatOn(Trimmed)) {
      FormattingOff = false;
    } else if (Trimmed.starts_with("/*")) {
      IsBlockComment = true;
      Pos = Code.find("*/", SearchFrom + 2);
    }

    const bool EmptyLineSkipped =
        Trimmed.empty() &&
        (Style.IncludeStyle.IncludeBlocks == tooling::IncludeStyle::IBS_Merge ||
         Style.IncludeStyle.IncludeBlocks ==
             tooling::IncludeStyle::IBS_Regroup);

    bool MergeWithNextLine = Trimmed.ends_with("\\");
    if (!FormattingOff && !MergeWithNextLine) {
      if (!IsBlockComment &&
          tooling::HeaderIncludes::IncludeRegex.match(Trimmed, &Matches)) {
        StringRef IncludeName = Matches[2];
        if (Trimmed.contains("/*") && !Trimmed.contains("*/")) {
          // #include with a start of a block comment, but without the end.
          // Need to keep all the lines until the end of the comment together.
          // FIXME: This is somehow simplified check that probably does not work
          // correctly if there are multiple comments on a line.
          Pos = Code.find("*/", SearchFrom);
          Line = Code.substr(
              Prev, (Pos != StringRef::npos ? Pos + 2 : Code.size()) - Prev);
        }
        int Category = Categories.getIncludePriority(
            IncludeName,
            /*CheckMainHeader=*/!MainIncludeFound && FirstIncludeBlock);
        int Priority = Categories.getSortIncludePriority(
            IncludeName, !MainIncludeFound && FirstIncludeBlock);
        if (Category == 0)
          MainIncludeFound = true;
        IncludesInBlock.push_back(
            {IncludeName, Line, Prev, Category, Priority});
      } else if (!IncludesInBlock.empty() && !EmptyLineSkipped) {
        sortCppIncludes(Style, IncludesInBlock, Ranges, FileName, Code,
                        Replaces, Cursor);
        IncludesInBlock.clear();
        if (Trimmed.starts_with("#pragma hdrstop")) // Precompiled headers.
          FirstIncludeBlock = true;
        else
          FirstIncludeBlock = false;
      }
    }
    if (Pos == StringRef::npos || Pos + 1 == Code.size())
      break;

    if (!MergeWithNextLine)
      Prev = Pos + 1;
    SearchFrom = Pos + 1;
  }
  if (!IncludesInBlock.empty()) {
    sortCppIncludes(Style, IncludesInBlock, Ranges, FileName, Code, Replaces,
                    Cursor);
  }
  return Replaces;
}

// Returns group number to use as a first order sort on imports. Gives UINT_MAX
// if the import does not match any given groups.
static unsigned findJavaImportGroup(const FormatStyle &Style,
                                    StringRef ImportIdentifier) {
  unsigned LongestMatchIndex = UINT_MAX;
  unsigned LongestMatchLength = 0;
  for (unsigned I = 0; I < Style.JavaImportGroups.size(); I++) {
    const std::string &GroupPrefix = Style.JavaImportGroups[I];
    if (ImportIdentifier.starts_with(GroupPrefix) &&
        GroupPrefix.length() > LongestMatchLength) {
      LongestMatchIndex = I;
      LongestMatchLength = GroupPrefix.length();
    }
  }
  return LongestMatchIndex;
}

// Sorts and deduplicates a block of includes given by 'Imports' based on
// JavaImportGroups, then adding the necessary replacement to 'Replaces'.
// Import declarations with the same text will be deduplicated. Between each
// import group, a newline is inserted, and within each import group, a
// lexicographic sort based on ASCII value is performed.
static void sortJavaImports(const FormatStyle &Style,
                            const SmallVectorImpl<JavaImportDirective> &Imports,
                            ArrayRef<tooling::Range> Ranges, StringRef FileName,
                            StringRef Code, tooling::Replacements &Replaces) {
  unsigned ImportsBeginOffset = Imports.front().Offset;
  unsigned ImportsEndOffset =
      Imports.back().Offset + Imports.back().Text.size();
  unsigned ImportsBlockSize = ImportsEndOffset - ImportsBeginOffset;
  if (!affectsRange(Ranges, ImportsBeginOffset, ImportsEndOffset))
    return;

  SmallVector<unsigned, 16> Indices =
      llvm::to_vector<16>(llvm::seq<unsigned>(0, Imports.size()));
  SmallVector<unsigned, 16> JavaImportGroups;
  JavaImportGroups.reserve(Imports.size());
  for (const JavaImportDirective &Import : Imports)
    JavaImportGroups.push_back(findJavaImportGroup(Style, Import.Identifier));

  bool StaticImportAfterNormalImport =
      Style.SortJavaStaticImport == FormatStyle::SJSIO_After;
  sort(Indices, [&](unsigned LHSI, unsigned RHSI) {
    // Negating IsStatic to push static imports above non-static imports.
    return std::make_tuple(!Imports[LHSI].IsStatic ^
                               StaticImportAfterNormalImport,
                           JavaImportGroups[LHSI], Imports[LHSI].Identifier) <
           std::make_tuple(!Imports[RHSI].IsStatic ^
                               StaticImportAfterNormalImport,
                           JavaImportGroups[RHSI], Imports[RHSI].Identifier);
  });

  // Deduplicate imports.
  Indices.erase(std::unique(Indices.begin(), Indices.end(),
                            [&](unsigned LHSI, unsigned RHSI) {
                              return Imports[LHSI].Text == Imports[RHSI].Text;
                            }),
                Indices.end());

  bool CurrentIsStatic = Imports[Indices.front()].IsStatic;
  unsigned CurrentImportGroup = JavaImportGroups[Indices.front()];

  std::string result;
  for (unsigned Index : Indices) {
    if (!result.empty()) {
      result += "\n";
      if (CurrentIsStatic != Imports[Index].IsStatic ||
          CurrentImportGroup != JavaImportGroups[Index]) {
        result += "\n";
      }
    }
    for (StringRef CommentLine : Imports[Index].AssociatedCommentLines) {
      result += CommentLine;
      result += "\n";
    }
    result += Imports[Index].Text;
    CurrentIsStatic = Imports[Index].IsStatic;
    CurrentImportGroup = JavaImportGroups[Index];
  }

  // If the imports are out of order, we generate a single replacement fixing
  // the entire block. Otherwise, no replacement is generated.
  if (replaceCRLF(result) == replaceCRLF(std::string(Code.substr(
                                 Imports.front().Offset, ImportsBlockSize)))) {
    return;
  }

  auto Err = Replaces.add(tooling::Replacement(FileName, Imports.front().Offset,
                                               ImportsBlockSize, result));
  // FIXME: better error handling. For now, just skip the replacement for the
  // release version.
  if (Err) {
    llvm::errs() << toString(std::move(Err)) << "\n";
    assert(false);
  }
}

namespace {

const char JavaImportRegexPattern[] =
    "^[\t ]*import[\t ]+(static[\t ]*)?([^\t ]*)[\t ]*;";

} // anonymous namespace

tooling::Replacements sortJavaImports(const FormatStyle &Style, StringRef Code,
                                      ArrayRef<tooling::Range> Ranges,
                                      StringRef FileName,
                                      tooling::Replacements &Replaces) {
  unsigned Prev = 0;
  unsigned SearchFrom = 0;
  llvm::Regex ImportRegex(JavaImportRegexPattern);
  SmallVector<StringRef, 4> Matches;
  SmallVector<JavaImportDirective, 16> ImportsInBlock;
  SmallVector<StringRef> AssociatedCommentLines;

  bool FormattingOff = false;

  for (;;) {
    auto Pos = Code.find('\n', SearchFrom);
    StringRef Line =
        Code.substr(Prev, (Pos != StringRef::npos ? Pos : Code.size()) - Prev);

    StringRef Trimmed = Line.trim();
    if (isClangFormatOff(Trimmed))
      FormattingOff = true;
    else if (isClangFormatOn(Trimmed))
      FormattingOff = false;

    if (ImportRegex.match(Line, &Matches)) {
      if (FormattingOff) {
        // If at least one import line has formatting turned off, turn off
        // formatting entirely.
        return Replaces;
      }
      StringRef Static = Matches[1];
      StringRef Identifier = Matches[2];
      bool IsStatic = false;
      if (Static.contains("static"))
        IsStatic = true;
      ImportsInBlock.push_back(
          {Identifier, Line, Prev, AssociatedCommentLines, IsStatic});
      AssociatedCommentLines.clear();
    } else if (Trimmed.size() > 0 && !ImportsInBlock.empty()) {
      // Associating comments within the imports with the nearest import below
      AssociatedCommentLines.push_back(Line);
    }
    Prev = Pos + 1;
    if (Pos == StringRef::npos || Pos + 1 == Code.size())
      break;
    SearchFrom = Pos + 1;
  }
  if (!ImportsInBlock.empty())
    sortJavaImports(Style, ImportsInBlock, Ranges, FileName, Code, Replaces);
  return Replaces;
}

bool isMpegTS(StringRef Code) {
  // MPEG transport streams use the ".ts" file extension. clang-format should
  // not attempt to format those. MPEG TS' frame format starts with 0x47 every
  // 189 bytes - detect that and return.
  return Code.size() > 188 && Code[0] == 0x47 && Code[188] == 0x47;
}

bool isLikelyXml(StringRef Code) { return Code.ltrim().starts_with("<"); }

tooling::Replacements sortIncludes(const FormatStyle &Style, StringRef Code,
                                   ArrayRef<tooling::Range> Ranges,
                                   StringRef FileName, unsigned *Cursor) {
  tooling::Replacements Replaces;
  if (!Style.SortIncludes || Style.DisableFormat)
    return Replaces;
  if (isLikelyXml(Code))
    return Replaces;
  if (Style.Language == FormatStyle::LanguageKind::LK_JavaScript &&
      isMpegTS(Code)) {
    return Replaces;
  }
  if (Style.Language == FormatStyle::LanguageKind::LK_JavaScript)
    return sortJavaScriptImports(Style, Code, Ranges, FileName);
  if (Style.Language == FormatStyle::LanguageKind::LK_Java)
    return sortJavaImports(Style, Code, Ranges, FileName, Replaces);
  sortCppIncludes(Style, Code, Ranges, FileName, Replaces, Cursor);
  return Replaces;
}

template <typename T>
static Expected<tooling::Replacements>
processReplacements(T ProcessFunc, StringRef Code,
                    const tooling::Replacements &Replaces,
                    const FormatStyle &Style) {
  if (Replaces.empty())
    return tooling::Replacements();

  auto NewCode = applyAllReplacements(Code, Replaces);
  if (!NewCode)
    return NewCode.takeError();
  std::vector<tooling::Range> ChangedRanges = Replaces.getAffectedRanges();
  StringRef FileName = Replaces.begin()->getFilePath();

  tooling::Replacements FormatReplaces =
      ProcessFunc(Style, *NewCode, ChangedRanges, FileName);

  return Replaces.merge(FormatReplaces);
}

Expected<tooling::Replacements>
formatReplacements(StringRef Code, const tooling::Replacements &Replaces,
                   const FormatStyle &Style) {
  // We need to use lambda function here since there are two versions of
  // `sortIncludes`.
  auto SortIncludes = [](const FormatStyle &Style, StringRef Code,
                         std::vector<tooling::Range> Ranges,
                         StringRef FileName) -> tooling::Replacements {
    return sortIncludes(Style, Code, Ranges, FileName);
  };
  auto SortedReplaces =
      processReplacements(SortIncludes, Code, Replaces, Style);
  if (!SortedReplaces)
    return SortedReplaces.takeError();

  // We need to use lambda function here since there are two versions of
  // `reformat`.
  auto Reformat = [](const FormatStyle &Style, StringRef Code,
                     std::vector<tooling::Range> Ranges,
                     StringRef FileName) -> tooling::Replacements {
    return reformat(Style, Code, Ranges, FileName);
  };
  return processReplacements(Reformat, Code, *SortedReplaces, Style);
}

namespace {

inline bool isHeaderInsertion(const tooling::Replacement &Replace) {
  return Replace.getOffset() == UINT_MAX && Replace.getLength() == 0 &&
         tooling::HeaderIncludes::IncludeRegex.match(
             Replace.getReplacementText());
}

inline bool isHeaderDeletion(const tooling::Replacement &Replace) {
  return Replace.getOffset() == UINT_MAX && Replace.getLength() == 1;
}

// FIXME: insert empty lines between newly created blocks.
tooling::Replacements
fixCppIncludeInsertions(StringRef Code, const tooling::Replacements &Replaces,
                        const FormatStyle &Style) {
  if (!Style.isCpp())
    return Replaces;

  tooling::Replacements HeaderInsertions;
  std::set<StringRef> HeadersToDelete;
  tooling::Replacements Result;
  for (const auto &R : Replaces) {
    if (isHeaderInsertion(R)) {
      // Replacements from \p Replaces must be conflict-free already, so we can
      // simply consume the error.
      consumeError(HeaderInsertions.add(R));
    } else if (isHeaderDeletion(R)) {
      HeadersToDelete.insert(R.getReplacementText());
    } else if (R.getOffset() == UINT_MAX) {
      llvm::errs() << "Insertions other than header #include insertion are "
                      "not supported! "
                   << R.getReplacementText() << "\n";
    } else {
      consumeError(Result.add(R));
    }
  }
  if (HeaderInsertions.empty() && HeadersToDelete.empty())
    return Replaces;

  StringRef FileName = Replaces.begin()->getFilePath();
  tooling::HeaderIncludes Includes(FileName, Code, Style.IncludeStyle);

  for (const auto &Header : HeadersToDelete) {
    tooling::Replacements Replaces =
        Includes.remove(Header.trim("\"<>"), Header.starts_with("<"));
    for (const auto &R : Replaces) {
      auto Err = Result.add(R);
      if (Err) {
        // Ignore the deletion on conflict.
        llvm::errs() << "Failed to add header deletion replacement for "
                     << Header << ": " << toString(std::move(Err)) << "\n";
      }
    }
  }

  SmallVector<StringRef, 4> Matches;
  for (const auto &R : HeaderInsertions) {
    auto IncludeDirective = R.getReplacementText();
    bool Matched =
        tooling::HeaderIncludes::IncludeRegex.match(IncludeDirective, &Matches);
    assert(Matched && "Header insertion replacement must have replacement text "
                      "'#include ...'");
    (void)Matched;
    auto IncludeName = Matches[2];
    auto Replace =
        Includes.insert(IncludeName.trim("\"<>"), IncludeName.starts_with("<"),
                        tooling::IncludeDirective::Include);
    if (Replace) {
      auto Err = Result.add(*Replace);
      if (Err) {
        consumeError(std::move(Err));
        unsigned NewOffset =
            Result.getShiftedCodePosition(Replace->getOffset());
        auto Shifted = tooling::Replacement(FileName, NewOffset, 0,
                                            Replace->getReplacementText());
        Result = Result.merge(tooling::Replacements(Shifted));
      }
    }
  }
  return Result;
}

} // anonymous namespace

Expected<tooling::Replacements>
cleanupAroundReplacements(StringRef Code, const tooling::Replacements &Replaces,
                          const FormatStyle &Style) {
  // We need to use lambda function here since there are two versions of
  // `cleanup`.
  auto Cleanup = [](const FormatStyle &Style, StringRef Code,
                    ArrayRef<tooling::Range> Ranges,
                    StringRef FileName) -> tooling::Replacements {
    return cleanup(Style, Code, Ranges, FileName);
  };
  // Make header insertion replacements insert new headers into correct blocks.
  tooling::Replacements NewReplaces =
      fixCppIncludeInsertions(Code, Replaces, Style);
  return cantFail(processReplacements(Cleanup, Code, NewReplaces, Style));
}

namespace internal {
std::pair<tooling::Replacements, unsigned>
reformat(const FormatStyle &Style, StringRef Code,
         ArrayRef<tooling::Range> Ranges, unsigned FirstStartColumn,
         unsigned NextStartColumn, unsigned LastStartColumn, StringRef FileName,
         FormattingAttemptStatus *Status) {
  FormatStyle Expanded = Style;
  expandPresetsBraceWrapping(Expanded);
  expandPresetsSpaceBeforeParens(Expanded);
  expandPresetsSpacesInParens(Expanded);
  Expanded.InsertBraces = false;
  Expanded.RemoveBracesLLVM = false;
  Expanded.RemoveParentheses = FormatStyle::RPS_Leave;
  Expanded.RemoveSemicolon = false;
  switch (Expanded.RequiresClausePosition) {
  case FormatStyle::RCPS_SingleLine:
  case FormatStyle::RCPS_WithPreceding:
    Expanded.IndentRequiresClause = false;
    break;
  default:
    break;
  }

  if (Expanded.DisableFormat)
    return {tooling::Replacements(), 0};
  if (isLikelyXml(Code))
    return {tooling::Replacements(), 0};
  if (Expanded.Language == FormatStyle::LK_JavaScript && isMpegTS(Code))
    return {tooling::Replacements(), 0};

  // JSON only needs the formatting passing.
  if (Style.isJson()) {
    std::vector<tooling::Range> Ranges(1, tooling::Range(0, Code.size()));
    auto Env = Environment::make(Code, FileName, Ranges, FirstStartColumn,
                                 NextStartColumn, LastStartColumn);
    if (!Env)
      return {};
    // Perform the actual formatting pass.
    tooling::Replacements Replaces =
        Formatter(*Env, Style, Status).process().first;
    // add a replacement to remove the "x = " from the result.
    Replaces = Replaces.merge(
        tooling::Replacements(tooling::Replacement(FileName, 0, 4, "")));
    // apply the reformatting changes and the removal of "x = ".
    if (applyAllReplacements(Code, Replaces))
      return {Replaces, 0};
    return {tooling::Replacements(), 0};
  }

  auto Env = Environment::make(Code, FileName, Ranges, FirstStartColumn,
                               NextStartColumn, LastStartColumn);
  if (!Env)
    return {};

  typedef std::function<std::pair<tooling::Replacements, unsigned>(
      const Environment &)>
      AnalyzerPass;

  SmallVector<AnalyzerPass, 16> Passes;

  Passes.emplace_back([&](const Environment &Env) {
    return IntegerLiteralSeparatorFixer().process(Env, Expanded);
  });

  if (Style.isCpp()) {
    if (Style.QualifierAlignment != FormatStyle::QAS_Leave)
      addQualifierAlignmentFixerPasses(Expanded, Passes);

    if (Style.RemoveParentheses != FormatStyle::RPS_Leave) {
      FormatStyle S = Expanded;
      S.RemoveParentheses = Style.RemoveParentheses;
      Passes.emplace_back([&, S = std::move(S)](const Environment &Env) {
        return ParensRemover(Env, S).process(/*SkipAnnotation=*/true);
      });
    }

    if (Style.InsertBraces) {
      FormatStyle S = Expanded;
      S.InsertBraces = true;
      Passes.emplace_back([&, S = std::move(S)](const Environment &Env) {
        return BracesInserter(Env, S).process(/*SkipAnnotation=*/true);
      });
    }

    if (Style.RemoveBracesLLVM) {
      FormatStyle S = Expanded;
      S.RemoveBracesLLVM = true;
      Passes.emplace_back([&, S = std::move(S)](const Environment &Env) {
        return BracesRemover(Env, S).process(/*SkipAnnotation=*/true);
      });
    }

    if (Style.RemoveSemicolon) {
      FormatStyle S = Expanded;
      S.RemoveSemicolon = true;
      Passes.emplace_back([&, S = std::move(S)](const Environment &Env) {
        return SemiRemover(Env, S).process();
      });
    }

    if (Style.FixNamespaceComments) {
      Passes.emplace_back([&](const Environment &Env) {
        return NamespaceEndCommentsFixer(Env, Expanded).process();
      });
    }

    if (Style.SortUsingDeclarations != FormatStyle::SUD_Never) {
      Passes.emplace_back([&](const Environment &Env) {
        return UsingDeclarationsSorter(Env, Expanded).process();
      });
    }
  }

  if (Style.SeparateDefinitionBlocks != FormatStyle::SDS_Leave) {
    Passes.emplace_back([&](const Environment &Env) {
      return DefinitionBlockSeparator(Env, Expanded).process();
    });
  }

  if (Style.Language == FormatStyle::LK_ObjC &&
      !Style.ObjCPropertyAttributeOrder.empty()) {
    Passes.emplace_back([&](const Environment &Env) {
      return ObjCPropertyAttributeOrderFixer(Env, Expanded).process();
    });
  }

  if (Style.isJavaScript() &&
      Style.JavaScriptQuotes != FormatStyle::JSQS_Leave) {
    Passes.emplace_back([&](const Environment &Env) {
      return JavaScriptRequoter(Env, Expanded).process(/*SkipAnnotation=*/true);
    });
  }

  Passes.emplace_back([&](const Environment &Env) {
    return Formatter(Env, Expanded, Status).process();
  });

  if (Style.isJavaScript() &&
      Style.InsertTrailingCommas == FormatStyle::TCS_Wrapped) {
    Passes.emplace_back([&](const Environment &Env) {
      return TrailingCommaInserter(Env, Expanded).process();
    });
  }

  std::optional<std::string> CurrentCode;
  tooling::Replacements Fixes;
  unsigned Penalty = 0;
  for (size_t I = 0, E = Passes.size(); I < E; ++I) {
    std::pair<tooling::Replacements, unsigned> PassFixes = Passes[I](*Env);
    auto NewCode = applyAllReplacements(
        CurrentCode ? StringRef(*CurrentCode) : Code, PassFixes.first);
    if (NewCode) {
      Fixes = Fixes.merge(PassFixes.first);
      Penalty += PassFixes.second;
      if (I + 1 < E) {
        CurrentCode = std::move(*NewCode);
        Env = Environment::make(
            *CurrentCode, FileName,
            tooling::calculateRangesAfterReplacements(Fixes, Ranges),
            FirstStartColumn, NextStartColumn, LastStartColumn);
        if (!Env)
          return {};
      }
    }
  }

  if (Style.QualifierAlignment != FormatStyle::QAS_Leave) {
    // Don't make replacements that replace nothing. QualifierAlignment can
    // produce them if one of its early passes changes e.g. `const volatile` to
    // `volatile const` and then a later pass changes it back again.
    tooling::Replacements NonNoOpFixes;
    for (const tooling::Replacement &Fix : Fixes) {
      StringRef OriginalCode = Code.substr(Fix.getOffset(), Fix.getLength());
      if (OriginalCode != Fix.getReplacementText()) {
        auto Err = NonNoOpFixes.add(Fix);
        if (Err) {
          llvm::errs() << "Error adding replacements : "
                       << toString(std::move(Err)) << "\n";
        }
      }
    }
    Fixes = std::move(NonNoOpFixes);
  }

  return {Fixes, Penalty};
}
} // namespace internal

tooling::Replacements reformat(const FormatStyle &Style, StringRef Code,
                               ArrayRef<tooling::Range> Ranges,
                               StringRef FileName,
                               FormattingAttemptStatus *Status) {
  return internal::reformat(Style, Code, Ranges,
                            /*FirstStartColumn=*/0,
                            /*NextStartColumn=*/0,
                            /*LastStartColumn=*/0, FileName, Status)
      .first;
}

tooling::Replacements cleanup(const FormatStyle &Style, StringRef Code,
                              ArrayRef<tooling::Range> Ranges,
                              StringRef FileName) {
  // cleanups only apply to C++ (they mostly concern ctor commas etc.)
  if (Style.Language != FormatStyle::LK_Cpp)
    return tooling::Replacements();
  auto Env = Environment::make(Code, FileName, Ranges);
  if (!Env)
    return {};
  return Cleaner(*Env, Style).process().first;
}

tooling::Replacements reformat(const FormatStyle &Style, StringRef Code,
                               ArrayRef<tooling::Range> Ranges,
                               StringRef FileName, bool *IncompleteFormat) {
  FormattingAttemptStatus Status;
  auto Result = reformat(Style, Code, Ranges, FileName, &Status);
  if (!Status.FormatComplete)
    *IncompleteFormat = true;
  return Result;
}

tooling::Replacements fixNamespaceEndComments(const FormatStyle &Style,
                                              StringRef Code,
                                              ArrayRef<tooling::Range> Ranges,
                                              StringRef FileName) {
  auto Env = Environment::make(Code, FileName, Ranges);
  if (!Env)
    return {};
  return NamespaceEndCommentsFixer(*Env, Style).process().first;
}

tooling::Replacements sortUsingDeclarations(const FormatStyle &Style,
                                            StringRef Code,
                                            ArrayRef<tooling::Range> Ranges,
                                            StringRef FileName) {
  auto Env = Environment::make(Code, FileName, Ranges);
  if (!Env)
    return {};
  return UsingDeclarationsSorter(*Env, Style).process().first;
}

LangOptions getFormattingLangOpts(const FormatStyle &Style) {
  LangOptions LangOpts;

  FormatStyle::LanguageStandard LexingStd = Style.Standard;
  if (LexingStd == FormatStyle::LS_Auto)
    LexingStd = FormatStyle::LS_Latest;
  if (LexingStd == FormatStyle::LS_Latest)
    LexingStd = FormatStyle::LS_Cpp20;
  LangOpts.CPlusPlus = 1;
  LangOpts.CPlusPlus11 = LexingStd >= FormatStyle::LS_Cpp11;
  LangOpts.CPlusPlus14 = LexingStd >= FormatStyle::LS_Cpp14;
  LangOpts.CPlusPlus17 = LexingStd >= FormatStyle::LS_Cpp17;
  LangOpts.CPlusPlus20 = LexingStd >= FormatStyle::LS_Cpp20;
  LangOpts.Char8 = LexingStd >= FormatStyle::LS_Cpp20;
  // Turning on digraphs in standards before C++0x is error-prone, because e.g.
  // the sequence "<::" will be unconditionally treated as "[:".
  // Cf. Lexer::LexTokenInternal.
  LangOpts.Digraphs = LexingStd >= FormatStyle::LS_Cpp11;

  LangOpts.LineComment = 1;
  LangOpts.CXXOperatorNames = Style.isCpp();
  LangOpts.Bool = 1;
  LangOpts.ObjC = 1;
  LangOpts.MicrosoftExt = 1;    // To get kw___try, kw___finally.
  LangOpts.DeclSpecKeyword = 1; // To get __declspec.
  LangOpts.C99 = 1; // To get kw_restrict for non-underscore-prefixed restrict.
  return LangOpts;
}

const char *StyleOptionHelpDescription =
    "Set coding style. <string> can be:\n"
    "1. A preset: LLVM, GNU, Google, Chromium, Microsoft,\n"
    "   Mozilla, WebKit.\n"
    "2. 'file' to load style configuration from a\n"
    "   .clang-format file in one of the parent directories\n"
    "   of the source file (for stdin, see --assume-filename).\n"
    "   If no .clang-format file is found, falls back to\n"
    "   --fallback-style.\n"
    "   --style=file is the default.\n"
    "3. 'file:<format_file_path>' to explicitly specify\n"
    "   the configuration file.\n"
    "4. \"{key: value, ...}\" to set specific parameters, e.g.:\n"
    "   --style=\"{BasedOnStyle: llvm, IndentWidth: 8}\"";

static FormatStyle::LanguageKind getLanguageByFileName(StringRef FileName) {
  if (FileName.ends_with(".java"))
    return FormatStyle::LK_Java;
  if (FileName.ends_with_insensitive(".js") ||
      FileName.ends_with_insensitive(".mjs") ||
      FileName.ends_with_insensitive(".ts")) {
    return FormatStyle::LK_JavaScript; // (module) JavaScript or TypeScript.
  }
  if (FileName.ends_with(".m") || FileName.ends_with(".mm"))
    return FormatStyle::LK_ObjC;
  if (FileName.ends_with_insensitive(".proto") ||
      FileName.ends_with_insensitive(".protodevel")) {
    return FormatStyle::LK_Proto;
  }
  // txtpb is the canonical extension, and textproto is the legacy canonical
  // extension
  // https://protobuf.dev/reference/protobuf/textformat-spec/#text-format-files
  if (FileName.ends_with_insensitive(".txtpb") ||
      FileName.ends_with_insensitive(".textpb") ||
      FileName.ends_with_insensitive(".pb.txt") ||
      FileName.ends_with_insensitive(".textproto") ||
      FileName.ends_with_insensitive(".asciipb")) {
    return FormatStyle::LK_TextProto;
  }
  if (FileName.ends_with_insensitive(".td"))
    return FormatStyle::LK_TableGen;
  if (FileName.ends_with_insensitive(".cs"))
    return FormatStyle::LK_CSharp;
  if (FileName.ends_with_insensitive(".json"))
    return FormatStyle::LK_Json;
  if (FileName.ends_with_insensitive(".sv") ||
      FileName.ends_with_insensitive(".svh") ||
      FileName.ends_with_insensitive(".v") ||
      FileName.ends_with_insensitive(".vh")) {
    return FormatStyle::LK_Verilog;
  }
  return FormatStyle::LK_Cpp;
}

FormatStyle::LanguageKind guessLanguage(StringRef FileName, StringRef Code) {
  const auto GuessedLanguage = getLanguageByFileName(FileName);
  if (GuessedLanguage == FormatStyle::LK_Cpp) {
    auto Extension = llvm::sys::path::extension(FileName);
    // If there's no file extension (or it's .h), we need to check the contents
    // of the code to see if it contains Objective-C.
    if (!Code.empty() && (Extension.empty() || Extension == ".h")) {
      auto NonEmptyFileName = FileName.empty() ? "guess.h" : FileName;
      Environment Env(Code, NonEmptyFileName, /*Ranges=*/{});
      ObjCHeaderStyleGuesser Guesser(Env, getLLVMStyle());
      Guesser.process();
      if (Guesser.isObjC())
        return FormatStyle::LK_ObjC;
    }
  }
  return GuessedLanguage;
}

// Update StyleOptionHelpDescription above when changing this.
const char *DefaultFormatStyle = "file";

const char *DefaultFallbackStyle = "LLVM";

llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
loadAndParseConfigFile(StringRef ConfigFile, llvm::vfs::FileSystem *FS,
                       FormatStyle *Style, bool AllowUnknownOptions,
                       llvm::SourceMgr::DiagHandlerTy DiagHandler) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Text =
      FS->getBufferForFile(ConfigFile.str());
  if (auto EC = Text.getError())
    return EC;
  if (auto EC = parseConfiguration(*Text.get(), Style, AllowUnknownOptions,
                                   DiagHandler)) {
    return EC;
  }
  return Text;
}

Expected<FormatStyle> getStyle(StringRef StyleName, StringRef FileName,
                               StringRef FallbackStyleName, StringRef Code,
                               llvm::vfs::FileSystem *FS,
                               bool AllowUnknownOptions,
                               llvm::SourceMgr::DiagHandlerTy DiagHandler) {
  FormatStyle Style = getLLVMStyle(guessLanguage(FileName, Code));
  FormatStyle FallbackStyle = getNoStyle();
  if (!getPredefinedStyle(FallbackStyleName, Style.Language, &FallbackStyle))
    return make_string_error("Invalid fallback style: " + FallbackStyleName);

  SmallVector<std::unique_ptr<llvm::MemoryBuffer>, 1> ChildFormatTextToApply;

  if (StyleName.starts_with("{")) {
    // Parse YAML/JSON style from the command line.
    StringRef Source = "<command-line>";
    if (std::error_code ec =
            parseConfiguration(llvm::MemoryBufferRef(StyleName, Source), &Style,
                               AllowUnknownOptions, DiagHandler)) {
      return make_string_error("Error parsing -style: " + ec.message());
    }

    if (!Style.InheritsParentConfig)
      return Style;

    ChildFormatTextToApply.emplace_back(
        llvm::MemoryBuffer::getMemBuffer(StyleName, Source, false));
  }

  if (!FS)
    FS = llvm::vfs::getRealFileSystem().get();
  assert(FS);

  // User provided clang-format file using -style=file:path/to/format/file.
  if (!Style.InheritsParentConfig &&
      StyleName.starts_with_insensitive("file:")) {
    auto ConfigFile = StyleName.substr(5);
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Text =
        loadAndParseConfigFile(ConfigFile, FS, &Style, AllowUnknownOptions,
                               DiagHandler);
    if (auto EC = Text.getError()) {
      return make_string_error("Error reading " + ConfigFile + ": " +
                               EC.message());
    }

    LLVM_DEBUG(llvm::dbgs()
               << "Using configuration file " << ConfigFile << "\n");

    if (!Style.InheritsParentConfig)
      return Style;

    // Search for parent configs starting from the parent directory of
    // ConfigFile.
    FileName = ConfigFile;
    ChildFormatTextToApply.emplace_back(std::move(*Text));
  }

  // If the style inherits the parent configuration it is a command line
  // configuration, which wants to inherit, so we have to skip the check of the
  // StyleName.
  if (!Style.InheritsParentConfig && !StyleName.equals_insensitive("file")) {
    if (!getPredefinedStyle(StyleName, Style.Language, &Style))
      return make_string_error("Invalid value for -style");
    if (!Style.InheritsParentConfig)
      return Style;
  }

  SmallString<128> Path(FileName);
  if (std::error_code EC = FS->makeAbsolute(Path))
    return make_string_error(EC.message());

  // Reset possible inheritance
  Style.InheritsParentConfig = false;

  auto dropDiagnosticHandler = [](const llvm::SMDiagnostic &, void *) {};

  auto applyChildFormatTexts = [&](FormatStyle *Style) {
    for (const auto &MemBuf : llvm::reverse(ChildFormatTextToApply)) {
      auto EC =
          parseConfiguration(*MemBuf, Style, AllowUnknownOptions,
                             DiagHandler ? DiagHandler : dropDiagnosticHandler);
      // It was already correctly parsed.
      assert(!EC);
      static_cast<void>(EC);
    }
  };

  // Look for .clang-format/_clang-format file in the file's parent directories.
  SmallVector<std::string, 2> FilesToLookFor;
  FilesToLookFor.push_back(".clang-format");
  FilesToLookFor.push_back("_clang-format");

  SmallString<128> UnsuitableConfigFiles;
  for (StringRef Directory = Path; !Directory.empty();
       Directory = llvm::sys::path::parent_path(Directory)) {
    auto Status = FS->status(Directory);
    if (!Status ||
        Status->getType() != llvm::sys::fs::file_type::directory_file) {
      continue;
    }

    for (const auto &F : FilesToLookFor) {
      SmallString<128> ConfigFile(Directory);

      llvm::sys::path::append(ConfigFile, F);
      LLVM_DEBUG(llvm::dbgs() << "Trying " << ConfigFile << "...\n");

      Status = FS->status(ConfigFile);
      if (!Status ||
          Status->getType() != llvm::sys::fs::file_type::regular_file) {
        continue;
      }

      llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Text =
          loadAndParseConfigFile(ConfigFile, FS, &Style, AllowUnknownOptions,
                                 DiagHandler);
      if (auto EC = Text.getError()) {
        if (EC != ParseError::Unsuitable) {
          return make_string_error("Error reading " + ConfigFile + ": " +
                                   EC.message());
        }
        if (!UnsuitableConfigFiles.empty())
          UnsuitableConfigFiles.append(", ");
        UnsuitableConfigFiles.append(ConfigFile);
        continue;
      }

      LLVM_DEBUG(llvm::dbgs()
                 << "Using configuration file " << ConfigFile << "\n");

      if (!Style.InheritsParentConfig) {
        if (!ChildFormatTextToApply.empty()) {
          LLVM_DEBUG(llvm::dbgs() << "Applying child configurations\n");
          applyChildFormatTexts(&Style);
        }
        return Style;
      }

      LLVM_DEBUG(llvm::dbgs() << "Inherits parent configuration\n");

      // Reset inheritance of style
      Style.InheritsParentConfig = false;

      ChildFormatTextToApply.emplace_back(std::move(*Text));

      // Breaking out of the inner loop, since we don't want to parse
      // .clang-format AND _clang-format, if both exist. Then we continue the
      // outer loop (parent directories) in search for the parent
      // configuration.
      break;
    }
  }

  if (!UnsuitableConfigFiles.empty()) {
    return make_string_error("Configuration file(s) do(es) not support " +
                             getLanguageName(Style.Language) + ": " +
                             UnsuitableConfigFiles);
  }

  if (!ChildFormatTextToApply.empty()) {
    LLVM_DEBUG(llvm::dbgs()
               << "Applying child configurations on fallback style\n");
    applyChildFormatTexts(&FallbackStyle);
  }

  return FallbackStyle;
}

static bool isClangFormatOnOff(StringRef Comment, bool On) {
  if (Comment == (On ? "/* clang-format on */" : "/* clang-format off */"))
    return true;

  static const char ClangFormatOn[] = "// clang-format on";
  static const char ClangFormatOff[] = "// clang-format off";
  const unsigned Size = (On ? sizeof ClangFormatOn : sizeof ClangFormatOff) - 1;

  return Comment.starts_with(On ? ClangFormatOn : ClangFormatOff) &&
         (Comment.size() == Size || Comment[Size] == ':');
}

bool isClangFormatOn(StringRef Comment) {
  return isClangFormatOnOff(Comment, /*On=*/true);
}

bool isClangFormatOff(StringRef Comment) {
  return isClangFormatOnOff(Comment, /*On=*/false);
}

} // namespace format
} // namespace clang
