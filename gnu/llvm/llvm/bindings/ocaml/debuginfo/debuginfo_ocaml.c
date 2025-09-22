/*===-- debuginfo_ocaml.c - LLVM OCaml Glue ---------------------*- C++ -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file glues LLVM's OCaml interface to its C interface. These functions *|
|* are by and large transparent wrappers to the corresponding C functions.    *|
|*                                                                            *|
|* Note that these functions intentionally take liberties with the CAMLparamX *|
|* macros, since most of the parameters are not GC heap objects.              *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include <string.h>

#include "caml/memory.h"
#include "caml/mlvalues.h"
#include "llvm-c/Core.h"
#include "llvm-c/DebugInfo.h"
#include "llvm-c/Support.h"

#include "llvm_ocaml.h"

// This is identical to the definition in llvm_debuginfo.ml:DIFlag.t
typedef enum {
  i_DIFlagZero,
  i_DIFlagPrivate,
  i_DIFlagProtected,
  i_DIFlagPublic,
  i_DIFlagFwdDecl,
  i_DIFlagAppleBlock,
  i_DIFlagReservedBit4,
  i_DIFlagVirtual,
  i_DIFlagArtificial,
  i_DIFlagExplicit,
  i_DIFlagPrototyped,
  i_DIFlagObjcClassComplete,
  i_DIFlagObjectPointer,
  i_DIFlagVector,
  i_DIFlagStaticMember,
  i_DIFlagLValueReference,
  i_DIFlagRValueReference,
  i_DIFlagReserved,
  i_DIFlagSingleInheritance,
  i_DIFlagMultipleInheritance,
  i_DIFlagVirtualInheritance,
  i_DIFlagIntroducedVirtual,
  i_DIFlagBitField,
  i_DIFlagNoReturn,
  i_DIFlagTypePassByValue,
  i_DIFlagTypePassByReference,
  i_DIFlagEnumClass,
  i_DIFlagFixedEnum,
  i_DIFlagThunk,
  i_DIFlagNonTrivial,
  i_DIFlagBigEndian,
  i_DIFlagLittleEndian,
  i_DIFlagIndirectVirtualBase,
  i_DIFlagAccessibility,
  i_DIFlagPtrToMemberRep
} LLVMDIFlag_i;

static LLVMDIFlags map_DIFlag(LLVMDIFlag_i DIF) {
  switch (DIF) {
  case i_DIFlagZero:
    return LLVMDIFlagZero;
  case i_DIFlagPrivate:
    return LLVMDIFlagPrivate;
  case i_DIFlagProtected:
    return LLVMDIFlagProtected;
  case i_DIFlagPublic:
    return LLVMDIFlagPublic;
  case i_DIFlagFwdDecl:
    return LLVMDIFlagFwdDecl;
  case i_DIFlagAppleBlock:
    return LLVMDIFlagAppleBlock;
  case i_DIFlagReservedBit4:
    return LLVMDIFlagReservedBit4;
  case i_DIFlagVirtual:
    return LLVMDIFlagVirtual;
  case i_DIFlagArtificial:
    return LLVMDIFlagArtificial;
  case i_DIFlagExplicit:
    return LLVMDIFlagExplicit;
  case i_DIFlagPrototyped:
    return LLVMDIFlagPrototyped;
  case i_DIFlagObjcClassComplete:
    return LLVMDIFlagObjcClassComplete;
  case i_DIFlagObjectPointer:
    return LLVMDIFlagObjectPointer;
  case i_DIFlagVector:
    return LLVMDIFlagVector;
  case i_DIFlagStaticMember:
    return LLVMDIFlagStaticMember;
  case i_DIFlagLValueReference:
    return LLVMDIFlagLValueReference;
  case i_DIFlagRValueReference:
    return LLVMDIFlagRValueReference;
  case i_DIFlagReserved:
    return LLVMDIFlagReserved;
  case i_DIFlagSingleInheritance:
    return LLVMDIFlagSingleInheritance;
  case i_DIFlagMultipleInheritance:
    return LLVMDIFlagMultipleInheritance;
  case i_DIFlagVirtualInheritance:
    return LLVMDIFlagVirtualInheritance;
  case i_DIFlagIntroducedVirtual:
    return LLVMDIFlagIntroducedVirtual;
  case i_DIFlagBitField:
    return LLVMDIFlagBitField;
  case i_DIFlagNoReturn:
    return LLVMDIFlagNoReturn;
  case i_DIFlagTypePassByValue:
    return LLVMDIFlagTypePassByValue;
  case i_DIFlagTypePassByReference:
    return LLVMDIFlagTypePassByReference;
  case i_DIFlagEnumClass:
    return LLVMDIFlagEnumClass;
  case i_DIFlagFixedEnum:
    return LLVMDIFlagFixedEnum;
  case i_DIFlagThunk:
    return LLVMDIFlagThunk;
  case i_DIFlagNonTrivial:
    return LLVMDIFlagNonTrivial;
  case i_DIFlagBigEndian:
    return LLVMDIFlagBigEndian;
  case i_DIFlagLittleEndian:
    return LLVMDIFlagLittleEndian;
  case i_DIFlagIndirectVirtualBase:
    return LLVMDIFlagIndirectVirtualBase;
  case i_DIFlagAccessibility:
    return LLVMDIFlagAccessibility;
  case i_DIFlagPtrToMemberRep:
    return LLVMDIFlagPtrToMemberRep;
  }
}

/* unit -> int */
value llvm_debug_metadata_version(value Unit) {
  return Val_int(LLVMDebugMetadataVersion());
}

/* llmodule -> int */
value llvm_get_module_debug_metadata_version(value Module) {
  return Val_int(LLVMGetModuleDebugMetadataVersion(Module_val(Module)));
}

#define DIFlags_val(v) (*(LLVMDIFlags *)(Data_custom_val(v)))

static struct custom_operations diflags_ops = {
    (char *)"DebugInfo.lldiflags", custom_finalize_default,
    custom_compare_default,        custom_hash_default,
    custom_serialize_default,      custom_deserialize_default,
    custom_compare_ext_default};

static value alloc_diflags(LLVMDIFlags Flags) {
  value V = caml_alloc_custom(&diflags_ops, sizeof(LLVMDIFlags), 0, 1);
  DIFlags_val(V) = Flags;
  return V;
}

LLVMDIFlags llvm_diflags_get(value i_Flag) {
  LLVMDIFlags Flags = map_DIFlag(Int_val(i_Flag));
  return alloc_diflags(Flags);
}

LLVMDIFlags llvm_diflags_set(value Flags, value i_Flag) {
  LLVMDIFlags FlagsNew = DIFlags_val(Flags) | map_DIFlag(Int_val(i_Flag));
  return alloc_diflags(FlagsNew);
}

value llvm_diflags_test(value Flags, value i_Flag) {
  LLVMDIFlags Flag = map_DIFlag(Int_val(i_Flag));
  return Val_bool((DIFlags_val(Flags) & Flag) == Flag);
}

#define DIBuilder_val(v) (*(LLVMDIBuilderRef *)(Data_custom_val(v)))

static void llvm_finalize_dibuilder(value B) {
  LLVMDIBuilderFinalize(DIBuilder_val(B));
  LLVMDisposeDIBuilder(DIBuilder_val(B));
}

static struct custom_operations dibuilder_ops = {
    (char *)"DebugInfo.lldibuilder", llvm_finalize_dibuilder,
    custom_compare_default,          custom_hash_default,
    custom_serialize_default,        custom_deserialize_default,
    custom_compare_ext_default};

static value alloc_dibuilder(LLVMDIBuilderRef B) {
  value V = caml_alloc_custom(&dibuilder_ops, sizeof(LLVMDIBuilderRef), 0, 1);
  DIBuilder_val(V) = B;
  return V;
}

/* llmodule -> lldibuilder */
value llvm_dibuilder(value M) {
  return alloc_dibuilder(LLVMCreateDIBuilder(Module_val(M)));
}

value llvm_dibuild_finalize(value Builder) {
  LLVMDIBuilderFinalize(DIBuilder_val(Builder));
  return Val_unit;
}

value llvm_dibuild_create_compile_unit_native(
    value Builder, value Lang, value FileRef, value Producer, value IsOptimized,
    value Flags, value RuntimeVer, value SplitName, value Kind, value DWOId,
    value SplitDebugInline, value DebugInfoForProfiling, value SysRoot,
    value SDK) {
  return to_val(LLVMDIBuilderCreateCompileUnit(
      DIBuilder_val(Builder), Int_val(Lang), Metadata_val(FileRef),
      String_val(Producer), caml_string_length(Producer), Bool_val(IsOptimized),
      String_val(Flags), caml_string_length(Flags), Int_val(RuntimeVer),
      String_val(SplitName), caml_string_length(SplitName), Int_val(Kind),
      Int_val(DWOId), Bool_val(SplitDebugInline),
      Bool_val(DebugInfoForProfiling), String_val(SysRoot),
      caml_string_length(SysRoot), String_val(SDK), caml_string_length(SDK)));
}

value llvm_dibuild_create_compile_unit_bytecode(value *argv, int argn) {
  return llvm_dibuild_create_compile_unit_native(
      argv[0],  // Builder
      argv[1],  // Lang
      argv[2],  // FileRef
      argv[3],  // Producer
      argv[4],  // IsOptimized
      argv[5],  // Flags
      argv[6],  // RuntimeVer
      argv[7],  // SplitName
      argv[8],  // Kind
      argv[9],  // DWOId
      argv[10], // SplitDebugInline
      argv[11], // DebugInfoForProfiling
      argv[12], // SysRoot
      argv[13]  // SDK
  );
}

value llvm_dibuild_create_file(value Builder, value Filename, value Directory) {
  return to_val(LLVMDIBuilderCreateFile(
      DIBuilder_val(Builder), String_val(Filename),
      caml_string_length(Filename), String_val(Directory),
      caml_string_length(Directory)));
}

value llvm_dibuild_create_module_native(value Builder, value ParentScope,
                                        value Name, value ConfigMacros,
                                        value IncludePath, value SysRoot) {
  return to_val(LLVMDIBuilderCreateModule(
      DIBuilder_val(Builder), Metadata_val(ParentScope), String_val(Name),
      caml_string_length(Name), String_val(ConfigMacros),
      caml_string_length(ConfigMacros), String_val(IncludePath),
      caml_string_length(IncludePath), String_val(SysRoot),
      caml_string_length(SysRoot)));
}

value llvm_dibuild_create_module_bytecode(value *argv, int argn) {
  return llvm_dibuild_create_module_native(argv[0], // Builder
                                           argv[1], // ParentScope
                                           argv[2], // Name
                                           argv[3], // ConfigMacros
                                           argv[4], // IncludePath
                                           argv[5]  // SysRoot
  );
}

value llvm_dibuild_create_namespace(value Builder, value ParentScope,
                                    value Name, value ExportSymbols) {
  return to_val(LLVMDIBuilderCreateNameSpace(
      DIBuilder_val(Builder), Metadata_val(ParentScope), String_val(Name),
      caml_string_length(Name), Bool_val(ExportSymbols)));
}

value llvm_dibuild_create_function_native(value Builder, value Scope,
                                          value Name, value LinkageName,
                                          value File, value LineNo, value Ty,
                                          value IsLocalToUnit,
                                          value IsDefinition, value ScopeLine,
                                          value Flags, value IsOptimized) {
  return to_val(LLVMDIBuilderCreateFunction(
      DIBuilder_val(Builder), Metadata_val(Scope), String_val(Name),
      caml_string_length(Name), String_val(LinkageName),
      caml_string_length(LinkageName), Metadata_val(File), Int_val(LineNo),
      Metadata_val(Ty), Bool_val(IsLocalToUnit), Bool_val(IsDefinition),
      Int_val(ScopeLine), DIFlags_val(Flags), Bool_val(IsOptimized)));
}

value llvm_dibuild_create_function_bytecode(value *argv, int argn) {
  return llvm_dibuild_create_function_native(argv[0],  // Builder,
                                             argv[1],  // Scope
                                             argv[2],  // Name
                                             argv[3],  // LinkageName
                                             argv[4],  // File
                                             argv[5],  // LineNo
                                             argv[6],  // Ty
                                             argv[7],  // IsLocalUnit
                                             argv[8],  // IsDefinition
                                             argv[9],  // ScopeLine
                                             argv[10], // Flags
                                             argv[11]  // IsOptimized
  );
}

value llvm_dibuild_create_lexical_block(value Builder, value Scope, value File,
                                        value Line, value Column) {
  return to_val(LLVMDIBuilderCreateLexicalBlock(
      DIBuilder_val(Builder), Metadata_val(Scope), Metadata_val(File),
      Int_val(Line), Int_val(Column)));
}

value llvm_metadata_null(value Unit) { return to_val(NULL); }

value llvm_dibuild_create_debug_location(value Ctx, value Line, value Column,
                                         value Scope, value InlinedAt) {
  return to_val(LLVMDIBuilderCreateDebugLocation(
      Context_val(Ctx), Int_val(Line), Int_val(Column), Metadata_val(Scope),
      Metadata_val(InlinedAt)));
}

value llvm_di_location_get_line(value Location) {
  return Val_int(LLVMDILocationGetLine(Metadata_val(Location)));
}

value llvm_di_location_get_column(value Location) {
  return Val_int(LLVMDILocationGetColumn(Metadata_val(Location)));
}

value llvm_di_location_get_scope(value Location) {
  return to_val(LLVMDILocationGetScope(Metadata_val(Location)));
}

value llvm_di_location_get_inlined_at(value Location) {
  return ptr_to_option(LLVMDILocationGetInlinedAt(Metadata_val(Location)));
}

value llvm_di_scope_get_file(value Scope) {
  return ptr_to_option(LLVMDIScopeGetFile(Metadata_val(Scope)));
}

value llvm_di_file_get_directory(value File) {
  unsigned Len;
  const char *Directory = LLVMDIFileGetDirectory(Metadata_val(File), &Len);
  return cstr_to_string(Directory, Len);
}

value llvm_di_file_get_filename(value File) {
  unsigned Len;
  const char *Filename = LLVMDIFileGetFilename(Metadata_val(File), &Len);
  return cstr_to_string(Filename, Len);
}

value llvm_di_file_get_source(value File) {
  unsigned Len;
  const char *Source = LLVMDIFileGetSource(Metadata_val(File), &Len);
  return cstr_to_string(Source, Len);
}

value llvm_dibuild_get_or_create_type_array(value Builder, value Data) {
  mlsize_t Count = Wosize_val(Data);
  LLVMMetadataRef *Temp = from_val_array(Data);
  LLVMMetadataRef Metadata =
      LLVMDIBuilderGetOrCreateTypeArray(DIBuilder_val(Builder), Temp, Count);
  free(Temp);
  return to_val(Metadata);
}

value llvm_dibuild_get_or_create_array(value Builder, value Data) {
  mlsize_t Count = Wosize_val(Data);
  LLVMMetadataRef *Temp = from_val_array(Data);
  LLVMMetadataRef Metadata =
      LLVMDIBuilderGetOrCreateArray(DIBuilder_val(Builder), Temp, Count);
  free(Temp);
  return to_val(Metadata);
}

value llvm_dibuild_create_subroutine_type(value Builder, value File,
                                          value ParameterTypes, value Flags) {
  mlsize_t Count = Wosize_val(ParameterTypes);
  LLVMMetadataRef *Temp = from_val_array(ParameterTypes);
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateSubroutineType(
      DIBuilder_val(Builder), Metadata_val(File), Temp,
      Wosize_val(ParameterTypes), DIFlags_val(Flags));
  free(Temp);
  return to_val(Metadata);
}

value llvm_dibuild_create_enumerator(value Builder, value Name, value Value,
                                     value IsUnsigned) {
  return to_val(LLVMDIBuilderCreateEnumerator(
      DIBuilder_val(Builder), String_val(Name), caml_string_length(Name),
      (int64_t)Int_val(Value), Bool_val(IsUnsigned)));
}

value llvm_dibuild_create_enumeration_type_native(
    value Builder, value Scope, value Name, value File, value LineNumber,
    value SizeInBits, value AlignInBits, value Elements, value ClassTy) {
  mlsize_t Count = Wosize_val(Elements);
  LLVMMetadataRef *Temp = from_val_array(Elements);
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateEnumerationType(
      DIBuilder_val(Builder), Metadata_val(Scope), String_val(Name),
      caml_string_length(Name), Metadata_val(File), Int_val(LineNumber),
      (uint64_t)Int_val(SizeInBits), Int_val(AlignInBits), Temp, Count,
      Metadata_val(ClassTy));
  free(Temp);
  return to_val(Metadata);
}

value llvm_dibuild_create_enumeration_type_bytecode(value *argv, int argn) {
  return llvm_dibuild_create_enumeration_type_native(argv[0], // Builder
                                                     argv[1], // Scope
                                                     argv[2], // Name
                                                     argv[3], // File
                                                     argv[4], // LineNumber
                                                     argv[5], // SizeInBits
                                                     argv[6], // AlignInBits
                                                     argv[7], // Elements
                                                     argv[8]  // ClassTy
  );
}

value llvm_dibuild_create_union_type_native(
    value Builder, value Scope, value Name, value File, value LineNumber,
    value SizeInBits, value AlignInBits, value Flags, value Elements,
    value RunTimeLanguage, value UniqueId) {
  LLVMMetadataRef *Temp = from_val_array(Elements);
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateUnionType(
      DIBuilder_val(Builder), Metadata_val(Scope), String_val(Name),
      caml_string_length(Name), Metadata_val(File), Int_val(LineNumber),
      (uint64_t)Int_val(SizeInBits), Int_val(AlignInBits), DIFlags_val(Flags),
      Temp, Wosize_val(Elements), Int_val(RunTimeLanguage),
      String_val(UniqueId), caml_string_length(UniqueId));
  free(Temp);
  return to_val(Metadata);
}

value llvm_dibuild_create_union_type_bytecode(value *argv, int argn) {
  return llvm_dibuild_create_union_type_native(argv[0], // Builder
                                               argv[1], // Scope
                                               argv[2], // Name
                                               argv[3], // File
                                               argv[4], // LineNumber
                                               argv[5], // SizeInBits
                                               argv[6], // AlignInBits
                                               argv[7], // Flags
                                               argv[8], // Elements
                                               argv[9], // RunTimeLanguage
                                               argv[10] // UniqueId
  );
}

value llvm_dibuild_create_array_type(value Builder, value Size,
                                     value AlignInBits, value Ty,
                                     value Subscripts) {
  LLVMMetadataRef *Temp = from_val_array(Subscripts);
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateArrayType(
      DIBuilder_val(Builder), (uint64_t)Int_val(Size), Int_val(AlignInBits),
      Metadata_val(Ty), Temp, Wosize_val(Subscripts));
  free(Temp);
  return to_val(Metadata);
}

value llvm_dibuild_create_vector_type(value Builder, value Size,
                                      value AlignInBits, value Ty,
                                      value Subscripts) {
  LLVMMetadataRef *Temp = from_val_array(Subscripts);
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateVectorType(
      DIBuilder_val(Builder), (uint64_t)Int_val(Size), Int_val(AlignInBits),
      Metadata_val(Ty), Temp, Wosize_val(Subscripts));
  free(Temp);
  return to_val(Metadata);
}

value llvm_dibuild_create_unspecified_type(value Builder, value Name) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateUnspecifiedType(
      DIBuilder_val(Builder), String_val(Name), caml_string_length(Name));
  return to_val(Metadata);
}

value llvm_dibuild_create_basic_type(value Builder, value Name,
                                     value SizeInBits, value Encoding,
                                     value Flags) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateBasicType(
      DIBuilder_val(Builder), String_val(Name), caml_string_length(Name),
      (uint64_t)Int_val(SizeInBits), Int_val(Encoding), DIFlags_val(Flags));
  return to_val(Metadata);
}

value llvm_dibuild_create_pointer_type_native(value Builder, value PointeeTy,
                                              value SizeInBits,
                                              value AlignInBits,
                                              value AddressSpace, value Name) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreatePointerType(
      DIBuilder_val(Builder), Metadata_val(PointeeTy),
      (uint64_t)Int_val(SizeInBits), Int_val(AlignInBits),
      Int_val(AddressSpace), String_val(Name), caml_string_length(Name));
  return to_val(Metadata);
}

value llvm_dibuild_create_pointer_type_bytecode(value *argv, int argn) {
  return llvm_dibuild_create_pointer_type_native(argv[0], // Builder
                                                 argv[1], // PointeeTy
                                                 argv[2], // SizeInBits
                                                 argv[3], // AlignInBits
                                                 argv[4], // AddressSpace
                                                 argv[5]  // Name
  );
}

value llvm_dibuild_create_struct_type_native(
    value Builder, value Scope, value Name, value File, value LineNumber,
    value SizeInBits, value AlignInBits, value Flags, value DerivedFrom,
    value Elements, value RunTimeLanguage, value VTableHolder, value UniqueId) {
  LLVMMetadataRef *Temp = from_val_array(Elements);
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateStructType(
      DIBuilder_val(Builder), Metadata_val(Scope), String_val(Name),
      caml_string_length(Name), Metadata_val(File), Int_val(LineNumber),
      (uint64_t)Int_val(SizeInBits), Int_val(AlignInBits), DIFlags_val(Flags),
      Metadata_val(DerivedFrom), Temp, Wosize_val(Elements),
      Int_val(RunTimeLanguage), Metadata_val(VTableHolder),
      String_val(UniqueId), caml_string_length(UniqueId));
  free(Temp);
  return to_val(Metadata);
}

value llvm_dibuild_create_struct_type_bytecode(value *argv, int argn) {
  return llvm_dibuild_create_struct_type_native(argv[0],  // Builder
                                                argv[1],  // Scope
                                                argv[2],  // Name
                                                argv[3],  // File
                                                argv[4],  // LineNumber
                                                argv[5],  // SizeInBits
                                                argv[6],  // AlignInBits
                                                argv[7],  // Flags
                                                argv[8],  // DeriviedFrom
                                                argv[9],  // Elements
                                                argv[10], // RunTimeLanguage
                                                argv[11], // VTableHolder
                                                argv[12]  // UniqueId
  );
}

value llvm_dibuild_create_member_type_native(value Builder, value Scope,
                                             value Name, value File,
                                             value LineNumber, value SizeInBits,
                                             value AlignInBits,
                                             value OffsetInBits, value Flags,
                                             value Ty) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateMemberType(
      DIBuilder_val(Builder), Metadata_val(Scope), String_val(Name),
      caml_string_length(Name), Metadata_val(File), Int_val(LineNumber),
      (uint64_t)Int_val(SizeInBits), Int_val(AlignInBits),
      (uint64_t)Int_val(OffsetInBits), DIFlags_val(Flags), Metadata_val(Ty));
  return to_val(Metadata);
}

value llvm_dibuild_create_member_type_bytecode(value *argv, int argn) {
  return llvm_dibuild_create_member_type_native(argv[0], // Builder
                                                argv[1], // Scope
                                                argv[2], // Name
                                                argv[3], // File
                                                argv[4], // LineNumber
                                                argv[5], // SizeInBits
                                                argv[6], // AlignInBits
                                                argv[7], // OffsetInBits
                                                argv[8], // Flags
                                                argv[9]  // Ty
  );
}

value llvm_dibuild_create_static_member_type_native(
    value Builder, value Scope, value Name, value File, value LineNumber,
    value Type, value Flags, value ConstantVal, value AlignInBits) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateStaticMemberType(
      DIBuilder_val(Builder), Metadata_val(Scope), String_val(Name),
      caml_string_length(Name), Metadata_val(File), Int_val(LineNumber),
      Metadata_val(Type), DIFlags_val(Flags), Value_val(ConstantVal),
      Int_val(AlignInBits));
  return to_val(Metadata);
}

value llvm_dibuild_create_static_member_type_bytecode(value *argv, int argn) {
  return llvm_dibuild_create_static_member_type_native(argv[0], // Builder
                                                       argv[1], // Scope
                                                       argv[2], // Name
                                                       argv[3], // File
                                                       argv[4], // LineNumber
                                                       argv[5], // Type
                                                       argv[6], // Flags,
                                                       argv[7], // ConstantVal
                                                       argv[8]  // AlignInBits
  );
}

value llvm_dibuild_create_member_pointer_type_native(
    value Builder, value PointeeType, value ClassType, value SizeInBits,
    value AlignInBits, value Flags) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateMemberPointerType(
      DIBuilder_val(Builder), Metadata_val(PointeeType),
      Metadata_val(ClassType), (uint64_t)Int_val(SizeInBits),
      Int_val(AlignInBits), llvm_diflags_get(Flags));
  return to_val(Metadata);
}

value llvm_dibuild_create_member_pointer_type_bytecode(value *argv, int argn) {
  return llvm_dibuild_create_member_pointer_type_native(argv[0], // Builder
                                                        argv[1], // PointeeType
                                                        argv[2], // ClassType
                                                        argv[3], // SizeInBits
                                                        argv[4], // AlignInBits
                                                        argv[5]  // Flags
  );
}

value llvm_dibuild_create_object_pointer_type(value Builder, value Type) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateObjectPointerType(
      DIBuilder_val(Builder), Metadata_val(Type));
  return to_val(Metadata);
}

value llvm_dibuild_create_qualified_type(value Builder, value Tag, value Type) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateQualifiedType(
      DIBuilder_val(Builder), Int_val(Tag), Metadata_val(Type));
  return to_val(Metadata);
}

value llvm_dibuild_create_reference_type(value Builder, value Tag, value Type) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateReferenceType(
      DIBuilder_val(Builder), Int_val(Tag), Metadata_val(Type));
  return to_val(Metadata);
}

value llvm_dibuild_create_null_ptr_type(value Builder) {
  return to_val(LLVMDIBuilderCreateNullPtrType(DIBuilder_val(Builder)));
}

value llvm_dibuild_create_typedef_native(value Builder, value Type, value Name,
                                         value File, value LineNo, value Scope,
                                         value AlignInBits) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateTypedef(
      DIBuilder_val(Builder), Metadata_val(Type), String_val(Name),
      caml_string_length(Name), Metadata_val(File), Int_val(LineNo),
      Metadata_val(Scope), Int_val(AlignInBits));
  return to_val(Metadata);
}

value llvm_dibuild_create_typedef_bytecode(value *argv, int argn) {

  return llvm_dibuild_create_typedef_native(argv[0], // Builder
                                            argv[1], // Type
                                            argv[2], // Name
                                            argv[3], // File
                                            argv[4], // LineNo
                                            argv[5], // Scope
                                            argv[6]  // AlignInBits
  );
}

value llvm_dibuild_create_inheritance_native(value Builder, value Ty,
                                             value BaseTy, value BaseOffset,
                                             value VBPtrOffset, value Flags) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateInheritance(
      DIBuilder_val(Builder), Metadata_val(Ty), Metadata_val(BaseTy),
      (uint64_t)Int_val(BaseOffset), Int_val(VBPtrOffset), DIFlags_val(Flags));
  return to_val(Metadata);
}

value llvm_dibuild_create_inheritance_bytecode(value *argv, int arg) {
  return llvm_dibuild_create_inheritance_native(argv[0], // Builder
                                                argv[1], // Ty
                                                argv[2], // BaseTy
                                                argv[3], // BaseOffset
                                                argv[4], // VBPtrOffset
                                                argv[5]  // Flags
  );
}

value llvm_dibuild_create_forward_decl_native(
    value Builder, value Tag, value Name, value Scope, value File, value Line,
    value RuntimeLang, value SizeInBits, value AlignInBits,
    value UniqueIdentifier) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateForwardDecl(
      DIBuilder_val(Builder), Int_val(Tag), String_val(Name),
      caml_string_length(Name), Metadata_val(Scope), Metadata_val(File),
      Int_val(Line), Int_val(RuntimeLang), (uint64_t)Int_val(SizeInBits),
      Int_val(AlignInBits), String_val(UniqueIdentifier),
      caml_string_length(UniqueIdentifier));
  return to_val(Metadata);
}

value llvm_dibuild_create_forward_decl_bytecode(value *argv, int arg) {

  return llvm_dibuild_create_forward_decl_native(argv[0], // Builder
                                                 argv[1], // Tag
                                                 argv[2], // Name
                                                 argv[3], // Scope
                                                 argv[4], // File
                                                 argv[5], // Line
                                                 argv[6], // RuntimeLang
                                                 argv[7], // SizeInBits
                                                 argv[8], // AlignInBits
                                                 argv[9]  // UniqueIdentifier
  );
}

value llvm_dibuild_create_replaceable_composite_type_native(
    value Builder, value Tag, value Name, value Scope, value File, value Line,
    value RuntimeLang, value SizeInBits, value AlignInBits, value Flags,
    value UniqueIdentifier) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateReplaceableCompositeType(
      DIBuilder_val(Builder), Int_val(Tag), String_val(Name),
      caml_string_length(Name), Metadata_val(Scope), Metadata_val(File),
      Int_val(Line), Int_val(RuntimeLang), (uint64_t)Int_val(SizeInBits),
      Int_val(AlignInBits), DIFlags_val(Flags), String_val(UniqueIdentifier),
      caml_string_length(UniqueIdentifier));
  return to_val(Metadata);
}

value llvm_dibuild_create_replaceable_composite_type_bytecode(value *argv,
                                                              int arg) {

  return llvm_dibuild_create_replaceable_composite_type_native(
      argv[0], // Builder
      argv[1], // Tag
      argv[2], // Name
      argv[3], // Scope
      argv[4], // File
      argv[5], // Line
      argv[6], // RuntimeLang
      argv[7], // SizeInBits
      argv[8], // AlignInBits
      argv[9], // Flags
      argv[10] // UniqueIdentifier
  );
}

value llvm_dibuild_create_bit_field_member_type_native(
    value Builder, value Scope, value Name, value File, value LineNum,
    value SizeInBits, value OffsetInBits, value StorageOffsetInBits,
    value Flags, value Ty) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateBitFieldMemberType(
      DIBuilder_val(Builder), Metadata_val(Scope), String_val(Name),
      caml_string_length(Name), Metadata_val(File), Int_val(LineNum),
      (uint64_t)Int_val(SizeInBits), (uint64_t)Int_val(OffsetInBits),
      (uint64_t)Int_val(StorageOffsetInBits), DIFlags_val(Flags),
      Metadata_val(Ty));
  return to_val(Metadata);
}

value llvm_dibuild_create_bit_field_member_type_bytecode(value *argv, int arg) {

  return llvm_dibuild_create_bit_field_member_type_native(
      argv[0], // Builder
      argv[1], // Scope
      argv[2], // Name
      argv[3], // File
      argv[4], // LineNum
      argv[5], // SizeInBits
      argv[6], // OffsetInBits
      argv[7], // StorageOffsetInBits
      argv[8], // Flags
      argv[9]  // Ty
  );
}

value llvm_dibuild_create_class_type_native(
    value Builder, value Scope, value Name, value File, value LineNumber,
    value SizeInBits, value AlignInBits, value OffsetInBits, value Flags,
    value DerivedFrom, value Elements, value VTableHolder,
    value TemplateParamsNode, value UniqueIdentifier) {
  LLVMMetadataRef *Temp = from_val_array(Elements);
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateClassType(
      DIBuilder_val(Builder), Metadata_val(Scope), String_val(Name),
      caml_string_length(Name), Metadata_val(File), Int_val(LineNumber),
      (uint64_t)Int_val(SizeInBits), Int_val(AlignInBits),
      (uint64_t)Int_val(OffsetInBits), DIFlags_val(Flags),
      Metadata_val(DerivedFrom), Temp, Wosize_val(Elements),
      Metadata_val(VTableHolder), Metadata_val(TemplateParamsNode),
      String_val(UniqueIdentifier), caml_string_length(UniqueIdentifier));
  free(Temp);
  return to_val(Metadata);
}

value llvm_dibuild_create_class_type_bytecode(value *argv, int arg) {

  return llvm_dibuild_create_class_type_native(argv[0],  // Builder
                                               argv[1],  // Scope
                                               argv[2],  // Name
                                               argv[3],  // File
                                               argv[4],  // LineNumber
                                               argv[5],  // SizeInBits
                                               argv[6],  // AlignInBits
                                               argv[7],  // OffsetInBits
                                               argv[8],  // Flags
                                               argv[9],  // DerivedFrom
                                               argv[10], // Elements
                                               argv[11], // VTableHolder
                                               argv[12], // TemplateParamsNode
                                               argv[13]  // UniqueIdentifier
  );
}

value llvm_dibuild_create_artificial_type(value Builder, value Type) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateArtificialType(
      DIBuilder_val(Builder), Metadata_val(Type));
  return to_val(Metadata);
}

value llvm_di_type_get_name(value DType) {
  size_t Len;
  const char *Name = LLVMDITypeGetName(Metadata_val(DType), &Len);
  return cstr_to_string(Name, Len);
}

value llvm_di_type_get_size_in_bits(value DType) {
  uint64_t Size = LLVMDITypeGetSizeInBits(Metadata_val(DType));
  return Val_int((int)Size);
}

value llvm_di_type_get_offset_in_bits(value DType) {
  uint64_t Size = LLVMDITypeGetOffsetInBits(Metadata_val(DType));
  return Val_int((int)Size);
}

value llvm_di_type_get_align_in_bits(value DType) {
  uint32_t Size = LLVMDITypeGetAlignInBits(Metadata_val(DType));
  return Val_int(Size);
}

value llvm_di_type_get_line(value DType) {
  unsigned Line = LLVMDITypeGetLine(Metadata_val(DType));
  return Val_int(Line);
}

value llvm_di_type_get_flags(value DType) {
  LLVMDIFlags Flags = LLVMDITypeGetFlags(Metadata_val(DType));
  return alloc_diflags(Flags);
}

value llvm_get_subprogram(value Func) {
  return ptr_to_option(LLVMGetSubprogram(Value_val(Func)));
}

value llvm_set_subprogram(value Func, value SP) {
  LLVMSetSubprogram(Value_val(Func), Metadata_val(SP));
  return Val_unit;
}

value llvm_di_subprogram_get_line(value Subprogram) {
  return Val_int(LLVMDISubprogramGetLine(Metadata_val(Subprogram)));
}

value llvm_instr_get_debug_loc(value Inst) {
  return ptr_to_option(LLVMInstructionGetDebugLoc(Value_val(Inst)));
}

value llvm_instr_set_debug_loc(value Inst, value Loc) {
  LLVMInstructionSetDebugLoc(Value_val(Inst), Metadata_val(Loc));
  return Val_unit;
}

value llvm_dibuild_create_constant_value_expression(value Builder,
                                                    value Value) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateConstantValueExpression(
      DIBuilder_val(Builder), (uint64_t)Int_val(Value));
  return to_val(Metadata);
}

value llvm_dibuild_create_global_variable_expression_native(
    value Builder, value Scope, value Name, value Linkage, value File,
    value Line, value Ty, value LocalToUnit, value Expr, value Decl,
    value AlignInBits) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateGlobalVariableExpression(
      DIBuilder_val(Builder), Metadata_val(Scope), String_val(Name),
      caml_string_length(Name), String_val(Linkage),
      caml_string_length(Linkage), Metadata_val(File), Int_val(Line),
      Metadata_val(Ty), Bool_val(LocalToUnit), Metadata_val(Expr),
      Metadata_val(Decl), Int_val(AlignInBits));
  return to_val(Metadata);
}

value llvm_dibuild_create_global_variable_expression_bytecode(value *argv,
                                                              int arg) {

  return llvm_dibuild_create_global_variable_expression_native(
      argv[0], // Builder
      argv[1], // Scope
      argv[2], // Name
      argv[3], // Linkage
      argv[4], // File
      argv[5], // Line
      argv[6], // Ty
      argv[7], // LocalToUnit
      argv[8], // Expr
      argv[9], // Decl
      argv[10] // AlignInBits
  );
}

value llvm_di_global_variable_expression_get_variable(value GVE) {
  return ptr_to_option(
      LLVMDIGlobalVariableExpressionGetVariable(Metadata_val(GVE)));
}

value llvm_di_variable_get_line(value Variable) {
  return Val_int(LLVMDIVariableGetLine(Metadata_val(Variable)));
}

value llvm_di_variable_get_file(value Variable) {
  return ptr_to_option(LLVMDIVariableGetFile(Metadata_val(Variable)));
}

value llvm_get_metadata_kind(value Metadata) {
  return Val_int(LLVMGetMetadataKind(Metadata_val(Metadata)));
}

value llvm_dibuild_create_auto_variable_native(value Builder, value Scope,
                                               value Name, value File,
                                               value Line, value Ty,
                                               value AlwaysPreserve,
                                               value Flags, value AlignInBits) {
  return to_val(LLVMDIBuilderCreateAutoVariable(
      DIBuilder_val(Builder), Metadata_val(Scope), String_val(Name),
      caml_string_length(Name), Metadata_val(File), Int_val(Line),
      Metadata_val(Ty), Bool_val(AlwaysPreserve), DIFlags_val(Flags),
      Int_val(AlignInBits)));
}

value llvm_dibuild_create_auto_variable_bytecode(value *argv, int arg) {

  return llvm_dibuild_create_auto_variable_native(argv[0], // Builder
                                                  argv[1], // Scope
                                                  argv[2], // Name
                                                  argv[3], // File
                                                  argv[4], // Line
                                                  argv[5], // Ty
                                                  argv[6], // AlwaysPreserve
                                                  argv[7], // Flags
                                                  argv[8]  // AlignInBits
  );
}

value llvm_dibuild_create_parameter_variable_native(
    value Builder, value Scope, value Name, value ArgNo, value File, value Line,
    value Ty, value AlwaysPreserve, value Flags) {
  LLVMMetadataRef Metadata = LLVMDIBuilderCreateParameterVariable(
      DIBuilder_val(Builder), Metadata_val(Scope), String_val(Name),
      caml_string_length(Name), (unsigned)Int_val(ArgNo), Metadata_val(File),
      Int_val(Line), Metadata_val(Ty), Bool_val(AlwaysPreserve),
      DIFlags_val(Flags));
  return to_val(Metadata);
}

value llvm_dibuild_create_parameter_variable_bytecode(value *argv, int arg) {
  return llvm_dibuild_create_parameter_variable_native(
      argv[0], // Builder
      argv[1], // Scope
      argv[2], // Name
      argv[3], // ArgNo
      argv[4], // File
      argv[5], // Line
      argv[6], // Ty
      argv[7], // AlwaysPreserve
      argv[8]  // Flags
  );
}

value llvm_dibuild_insert_declare_before_native(value Builder, value Storage,
                                                value VarInfo, value Expr,
                                                value DebugLoc, value Instr) {
  LLVMDbgRecordRef Value = LLVMDIBuilderInsertDeclareRecordBefore(
      DIBuilder_val(Builder), Value_val(Storage), Metadata_val(VarInfo),
      Metadata_val(Expr), Metadata_val(DebugLoc), Value_val(Instr));
  return to_val(Value);
}

value llvm_dibuild_insert_declare_before_bytecode(value *argv, int arg) {

  return llvm_dibuild_insert_declare_before_native(argv[0], // Builder
                                                   argv[1], // Storage
                                                   argv[2], // VarInfo
                                                   argv[3], // Expr
                                                   argv[4], // DebugLoc
                                                   argv[5]  // Instr
  );
}

value llvm_dibuild_insert_declare_at_end_native(value Builder, value Storage,
                                                value VarInfo, value Expr,
                                                value DebugLoc, value Block) {
  LLVMDbgRecordRef Value = LLVMDIBuilderInsertDeclareRecordAtEnd(
      DIBuilder_val(Builder), Value_val(Storage), Metadata_val(VarInfo),
      Metadata_val(Expr), Metadata_val(DebugLoc), BasicBlock_val(Block));
  return to_val(Value);
}

value llvm_dibuild_insert_declare_at_end_bytecode(value *argv, int arg) {
  return llvm_dibuild_insert_declare_at_end_native(argv[0], // Builder
                                                   argv[1], // Storage
                                                   argv[2], // VarInfo
                                                   argv[3], // Expr
                                                   argv[4], // DebugLoc
                                                   argv[5]  // Block
  );
}

value llvm_dibuild_expression(value Builder, value Addr) {
  return to_val(LLVMDIBuilderCreateExpression(
      DIBuilder_val(Builder), (uint64_t *)Op_val(Addr), Wosize_val(Addr)));
}

/* llmodule -> bool */
value llvm_is_new_dbg_info_format(value Module) {
  return Val_bool(LLVMIsNewDbgInfoFormat(Module_val(Module)));
}

/* llmodule -> bool -> unit */
value llvm_set_is_new_dbg_info_format(value Module, value UseNewFormat) {
  LLVMSetIsNewDbgInfoFormat(Module_val(Module), Bool_val(UseNewFormat));
  return Val_unit;
}
