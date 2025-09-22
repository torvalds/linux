//===-- AppleObjCTypeEncodingParser.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AppleObjCTypeEncodingParser.h"

#include "Plugins/ExpressionParser/Clang/ClangUtil.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StringLexer.h"

#include "clang/Basic/TargetInfo.h"

#include <vector>

using namespace lldb_private;

AppleObjCTypeEncodingParser::AppleObjCTypeEncodingParser(
    ObjCLanguageRuntime &runtime)
    : ObjCLanguageRuntime::EncodingToType(), m_runtime(runtime) {
  if (m_scratch_ast_ctx_sp)
    return;

  m_scratch_ast_ctx_sp = std::make_shared<TypeSystemClang>(
      "AppleObjCTypeEncodingParser ASTContext",
      runtime.GetProcess()->GetTarget().GetArchitecture().GetTriple());
}

std::string AppleObjCTypeEncodingParser::ReadStructName(StringLexer &type) {
  StreamString buffer;
  while (type.HasAtLeast(1) && type.Peek() != '=')
    buffer.Printf("%c", type.Next());
  return std::string(buffer.GetString());
}

std::string AppleObjCTypeEncodingParser::ReadQuotedString(StringLexer &type) {
  StreamString buffer;
  while (type.HasAtLeast(1) && type.Peek() != '"')
    buffer.Printf("%c", type.Next());
  StringLexer::Character next = type.Next();
  UNUSED_IF_ASSERT_DISABLED(next);
  assert(next == '"');
  return std::string(buffer.GetString());
}

uint32_t AppleObjCTypeEncodingParser::ReadNumber(StringLexer &type) {
  uint32_t total = 0;
  while (type.HasAtLeast(1) && isdigit(type.Peek()))
    total = 10 * total + (type.Next() - '0');
  return total;
}

// as an extension to the published grammar recent runtimes emit structs like
// this:
// "{CGRect=\"origin\"{CGPoint=\"x\"d\"y\"d}\"size\"{CGSize=\"width\"d\"height\"d}}"

AppleObjCTypeEncodingParser::StructElement::StructElement()
    : type(clang::QualType()) {}

AppleObjCTypeEncodingParser::StructElement
AppleObjCTypeEncodingParser::ReadStructElement(TypeSystemClang &ast_ctx,
                                               StringLexer &type,
                                               bool for_expression) {
  StructElement retval;
  if (type.NextIf('"'))
    retval.name = ReadQuotedString(type);
  if (!type.NextIf('"'))
    return retval;
  uint32_t bitfield_size = 0;
  retval.type = BuildType(ast_ctx, type, for_expression, &bitfield_size);
  retval.bitfield = bitfield_size;
  return retval;
}

clang::QualType AppleObjCTypeEncodingParser::BuildStruct(
    TypeSystemClang &ast_ctx, StringLexer &type, bool for_expression) {
  return BuildAggregate(ast_ctx, type, for_expression, _C_STRUCT_B, _C_STRUCT_E,
                        llvm::to_underlying(clang::TagTypeKind::Struct));
}

clang::QualType AppleObjCTypeEncodingParser::BuildUnion(
    TypeSystemClang &ast_ctx, StringLexer &type, bool for_expression) {
  return BuildAggregate(ast_ctx, type, for_expression, _C_UNION_B, _C_UNION_E,
                        llvm::to_underlying(clang::TagTypeKind::Union));
}

clang::QualType AppleObjCTypeEncodingParser::BuildAggregate(
    TypeSystemClang &ast_ctx, StringLexer &type, bool for_expression,
    char opener, char closer, uint32_t kind) {
  if (!type.NextIf(opener))
    return clang::QualType();
  std::string name(ReadStructName(type));

  // We do not handle templated classes/structs at the moment. If the name has
  // a < in it, we are going to abandon this. We're still obliged to parse it,
  // so we just set a flag that means "Don't actually build anything."

  const bool is_templated = name.find('<') != std::string::npos;

  if (!type.NextIf('='))
    return clang::QualType();
  bool in_union = true;
  std::vector<StructElement> elements;
  while (in_union && type.HasAtLeast(1)) {
    if (type.NextIf(closer)) {
      in_union = false;
      break;
    } else {
      auto element = ReadStructElement(ast_ctx, type, for_expression);
      if (element.type.isNull())
        break;
      else
        elements.push_back(element);
    }
  }
  if (in_union)
    return clang::QualType();

  if (is_templated)
    return clang::QualType(); // This is where we bail out.  Sorry!

  CompilerType union_type(ast_ctx.CreateRecordType(
      nullptr, OptionalClangModuleID(), lldb::eAccessPublic, name, kind,
      lldb::eLanguageTypeC));
  if (union_type) {
    TypeSystemClang::StartTagDeclarationDefinition(union_type);

    unsigned int count = 0;
    for (auto element : elements) {
      if (element.name.empty()) {
        StreamString elem_name;
        elem_name.Printf("__unnamed_%u", count);
        element.name = std::string(elem_name.GetString());
      }
      TypeSystemClang::AddFieldToRecordType(
          union_type, element.name.c_str(), ast_ctx.GetType(element.type),
          lldb::eAccessPublic, element.bitfield);
      ++count;
    }
    TypeSystemClang::CompleteTagDeclarationDefinition(union_type);
  }
  return ClangUtil::GetQualType(union_type);
}

clang::QualType AppleObjCTypeEncodingParser::BuildArray(
    TypeSystemClang &ast_ctx, StringLexer &type, bool for_expression) {
  if (!type.NextIf(_C_ARY_B))
    return clang::QualType();
  uint32_t size = ReadNumber(type);
  clang::QualType element_type(BuildType(ast_ctx, type, for_expression));
  if (!type.NextIf(_C_ARY_E))
    return clang::QualType();
  CompilerType array_type(ast_ctx.CreateArrayType(
      CompilerType(ast_ctx.weak_from_this(), element_type.getAsOpaquePtr()),
      size, false));
  return ClangUtil::GetQualType(array_type);
}

// the runtime can emit these in the form of @"SomeType", giving more specifics
// this would be interesting for expression parser interop, but since we
// actually try to avoid exposing the ivar info to the expression evaluator,
// consume but ignore the type info and always return an 'id'; if anything,
// dynamic typing will resolve things for us anyway
clang::QualType AppleObjCTypeEncodingParser::BuildObjCObjectPointerType(
    TypeSystemClang &clang_ast_ctx, StringLexer &type, bool for_expression) {
  if (!type.NextIf(_C_ID))
    return clang::QualType();

  clang::ASTContext &ast_ctx = clang_ast_ctx.getASTContext();

  std::string name;

  if (type.NextIf('"')) {
    // We have to be careful here.  We're used to seeing
    //   @"NSString"
    // but in records it is possible that the string following an @ is the name
    // of the next field and @ means "id". This is the case if anything
    // unquoted except for "}", the end of the type, or another name follows
    // the quoted string.
    //
    // E.g.
    // - @"NSString"@ means "id, followed by a field named NSString of type id"
    // - @"NSString"} means "a pointer to NSString and the end of the struct" -
    // @"NSString""nextField" means "a pointer to NSString and a field named
    // nextField" - @"NSString" followed by the end of the string means "a
    // pointer to NSString"
    //
    // As a result, the rule is: If we see @ followed by a quoted string, we
    // peek. - If we see }, ), ], the end of the string, or a quote ("), the
    // quoted string is a class name. - If we see anything else, the quoted
    // string is a field name and we push it back onto type.

    name = ReadQuotedString(type);

    if (type.HasAtLeast(1)) {
      switch (type.Peek()) {
      default:
        // roll back
        type.PutBack(name.length() +
                     2); // undo our consumption of the string and of the quotes
        name.clear();
        break;
      case _C_STRUCT_E:
      case _C_UNION_E:
      case _C_ARY_E:
      case '"':
        // the quoted string is a class name – see the rule
        break;
      }
    } else {
      // the quoted string is a class name – see the rule
    }
  }

  if (for_expression && !name.empty()) {
    size_t less_than_pos = name.find('<');

    if (less_than_pos != std::string::npos) {
      if (less_than_pos == 0)
        return ast_ctx.getObjCIdType();
      else
        name.erase(less_than_pos);
    }

    DeclVendor *decl_vendor = m_runtime.GetDeclVendor();
    if (!decl_vendor)
      return clang::QualType();

    auto types = decl_vendor->FindTypes(ConstString(name), /*max_matches*/ 1);

    if (types.empty()) {
      // The user can forward-declare something that has no definition. The
      // runtime doesn't prohibit this at all. This is a rare and very weird
      // case. Assert assert in debug builds so we catch other weird cases.
      assert(false && "forward declaration without definition");
      LLDB_LOG(GetLog(LLDBLog::Types),
               "forward declaration without definition: {0}", name);
      return ast_ctx.getObjCIdType();
    }

    return ClangUtil::GetQualType(types.front().GetPointerType());
  } else {
    // We're going to resolve this dynamically anyway, so just smile and wave.
    return ast_ctx.getObjCIdType();
  }
}

clang::QualType
AppleObjCTypeEncodingParser::BuildType(TypeSystemClang &clang_ast_ctx,
                                       StringLexer &type, bool for_expression,
                                       uint32_t *bitfield_bit_size) {
  if (!type.HasAtLeast(1))
    return clang::QualType();

  clang::ASTContext &ast_ctx = clang_ast_ctx.getASTContext();

  switch (type.Peek()) {
  default:
    break;
  case _C_STRUCT_B:
    return BuildStruct(clang_ast_ctx, type, for_expression);
  case _C_ARY_B:
    return BuildArray(clang_ast_ctx, type, for_expression);
  case _C_UNION_B:
    return BuildUnion(clang_ast_ctx, type, for_expression);
  case _C_ID:
    return BuildObjCObjectPointerType(clang_ast_ctx, type, for_expression);
  }

  switch (type.Next()) {
  default:
    type.PutBack(1);
    return clang::QualType();
  case _C_CHR:
    return ast_ctx.CharTy;
  case _C_INT:
    return ast_ctx.IntTy;
  case _C_SHT:
    return ast_ctx.ShortTy;
  case _C_LNG:
    return ast_ctx.getIntTypeForBitwidth(32, true);
  // this used to be done like this:
  //   return clang_ast_ctx->GetIntTypeFromBitSize(32, true).GetQualType();
  // which uses one of the constants if one is available, but we don't think
  // all this work is necessary.
  case _C_LNG_LNG:
    return ast_ctx.LongLongTy;
  case _C_UCHR:
    return ast_ctx.UnsignedCharTy;
  case _C_UINT:
    return ast_ctx.UnsignedIntTy;
  case _C_USHT:
    return ast_ctx.UnsignedShortTy;
  case _C_ULNG:
    return ast_ctx.getIntTypeForBitwidth(32, false);
  // see note for _C_LNG
  case _C_ULNG_LNG:
    return ast_ctx.UnsignedLongLongTy;
  case _C_FLT:
    return ast_ctx.FloatTy;
  case _C_DBL:
    return ast_ctx.DoubleTy;
  case _C_BOOL:
    return ast_ctx.BoolTy;
  case _C_VOID:
    return ast_ctx.VoidTy;
  case _C_CHARPTR:
    return ast_ctx.getPointerType(ast_ctx.CharTy);
  case _C_CLASS:
    return ast_ctx.getObjCClassType();
  case _C_SEL:
    return ast_ctx.getObjCSelType();
  case _C_BFLD: {
    uint32_t size = ReadNumber(type);
    if (bitfield_bit_size) {
      *bitfield_bit_size = size;
      return ast_ctx.UnsignedIntTy; // FIXME: the spec is fairly vague here.
    } else
      return clang::QualType();
  }
  case _C_CONST: {
    clang::QualType target_type =
        BuildType(clang_ast_ctx, type, for_expression);
    if (target_type.isNull())
      return clang::QualType();
    else if (target_type == ast_ctx.UnknownAnyTy)
      return ast_ctx.UnknownAnyTy;
    else
      return ast_ctx.getConstType(target_type);
  }
  case _C_PTR: {
    if (!for_expression && type.NextIf(_C_UNDEF)) {
      // if we are not supporting the concept of unknownAny, but what is being
      // created here is an unknownAny*, then we can just get away with a void*
      // this is theoretically wrong (in the same sense as 'theoretically
      // nothing exists') but is way better than outright failure in many
      // practical cases
      return ast_ctx.VoidPtrTy;
    } else {
      clang::QualType target_type =
          BuildType(clang_ast_ctx, type, for_expression);
      if (target_type.isNull())
        return clang::QualType();
      else if (target_type == ast_ctx.UnknownAnyTy)
        return ast_ctx.UnknownAnyTy;
      else
        return ast_ctx.getPointerType(target_type);
    }
  }
  case _C_UNDEF:
    return for_expression ? ast_ctx.UnknownAnyTy : clang::QualType();
  }
}

CompilerType AppleObjCTypeEncodingParser::RealizeType(TypeSystemClang &ast_ctx,
                                                      const char *name,
                                                      bool for_expression) {
  if (name && name[0]) {
    StringLexer lexer(name);
    clang::QualType qual_type = BuildType(ast_ctx, lexer, for_expression);
    return ast_ctx.GetType(qual_type);
  }
  return CompilerType();
}
