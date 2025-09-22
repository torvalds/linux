//===-- CompilerType.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_COMPILERTYPE_H
#define LLDB_SYMBOL_COMPILERTYPE_H

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "lldb/lldb-private.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/Support/Casting.h"

namespace lldb_private {

class DataExtractor;
class TypeSystem;

/// Generic representation of a type in a programming language.
///
/// This class serves as an abstraction for a type inside one of the TypeSystems
/// implemented by the language plugins. It does not have any actual logic in it
/// but only stores an opaque pointer and a pointer to the TypeSystem that
/// gives meaning to this opaque pointer. All methods of this class should call
/// their respective method in the TypeSystem interface and pass the opaque
/// pointer along.
///
/// \see lldb_private::TypeSystem
class CompilerType {
public:
  /// Creates a CompilerType with the given TypeSystem and opaque compiler type.
  ///
  /// This constructor should only be called from the respective TypeSystem
  /// implementation.
  ///
  /// \see lldb_private::TypeSystemClang::GetType(clang::QualType)
  CompilerType(lldb::TypeSystemWP type_system,
               lldb::opaque_compiler_type_t type);

  /// This is a minimal wrapper of a TypeSystem shared pointer as
  /// returned by CompilerType which conventien dyn_cast support.
  class TypeSystemSPWrapper {
    lldb::TypeSystemSP m_typesystem_sp;

  public:
    TypeSystemSPWrapper() = default;
    TypeSystemSPWrapper(lldb::TypeSystemSP typesystem_sp)
        : m_typesystem_sp(typesystem_sp) {}

    template <class TypeSystemType> bool isa_and_nonnull() {
      if (auto *ts = m_typesystem_sp.get())
        return llvm::isa<TypeSystemType>(ts);
      return false;
    }

    /// Return a shared_ptr<TypeSystemType> if dyn_cast succeeds.
    template <class TypeSystemType>
    std::shared_ptr<TypeSystemType> dyn_cast_or_null() {
      if (isa_and_nonnull<TypeSystemType>())
        return std::shared_ptr<TypeSystemType>(
            m_typesystem_sp, llvm::cast<TypeSystemType>(m_typesystem_sp.get()));
      return nullptr;
    }

    explicit operator bool() const {
      return static_cast<bool>(m_typesystem_sp);
    }
    bool operator==(const TypeSystemSPWrapper &other) const;
    bool operator!=(const TypeSystemSPWrapper &other) const {
      return !(*this == other);
    }

    /// Only to be used in a one-off situations like
    ///    if (typesystem && typesystem->method())
    /// Do not store this pointer!
    TypeSystem *operator->() const;

    lldb::TypeSystemSP GetSharedPointer() const { return m_typesystem_sp; }
  };

  CompilerType(TypeSystemSPWrapper type_system,
               lldb::opaque_compiler_type_t type);

  CompilerType(const CompilerType &rhs)
      : m_type_system(rhs.m_type_system), m_type(rhs.m_type) {}

  CompilerType() = default;

  /// Operators.
  /// \{
  const CompilerType &operator=(const CompilerType &rhs) {
    m_type_system = rhs.m_type_system;
    m_type = rhs.m_type;
    return *this;
  }

  bool operator<(const CompilerType &rhs) const {
    auto lts = m_type_system.lock();
    auto rts = rhs.m_type_system.lock();
    if (lts.get() == rts.get())
      return m_type < rhs.m_type;
    return lts.get() < rts.get();
  }
  /// \}

  /// Tests.
  /// \{
  explicit operator bool() const {
    return m_type_system.lock() && m_type;
  }

  bool IsValid() const { return (bool)*this; }

  bool IsArrayType(CompilerType *element_type = nullptr,
                   uint64_t *size = nullptr,
                   bool *is_incomplete = nullptr) const;

  bool IsVectorType(CompilerType *element_type = nullptr,
                    uint64_t *size = nullptr) const;

  bool IsArrayOfScalarType() const;

  bool IsAggregateType() const;

  bool IsAnonymousType() const;

  bool IsScopedEnumerationType() const;

  bool IsBeingDefined() const;

  bool IsCharType() const;

  bool IsCompleteType() const;

  bool IsConst() const;

  bool IsDefined() const;

  bool IsFloatingPointType(uint32_t &count, bool &is_complex) const;

  bool IsFunctionType() const;

  uint32_t IsHomogeneousAggregate(CompilerType *base_type_ptr) const;

  size_t GetNumberOfFunctionArguments() const;

  CompilerType GetFunctionArgumentAtIndex(const size_t index) const;

  bool IsVariadicFunctionType() const;

  bool IsFunctionPointerType() const;

  bool IsMemberFunctionPointerType() const;

  bool
  IsBlockPointerType(CompilerType *function_pointer_type_ptr = nullptr) const;

  bool IsIntegerType(bool &is_signed) const;

  bool IsEnumerationType(bool &is_signed) const;

  bool IsIntegerOrEnumerationType(bool &is_signed) const;

  bool IsPolymorphicClass() const;

  /// \param target_type    Can pass nullptr.
  bool IsPossibleDynamicType(CompilerType *target_type, bool check_cplusplus,
                             bool check_objc) const;

  bool IsPointerToScalarType() const;

  bool IsRuntimeGeneratedType() const;

  bool IsPointerType(CompilerType *pointee_type = nullptr) const;

  bool IsPointerOrReferenceType(CompilerType *pointee_type = nullptr) const;

  bool IsReferenceType(CompilerType *pointee_type = nullptr,
                       bool *is_rvalue = nullptr) const;

  bool ShouldTreatScalarValueAsAddress() const;

  bool IsScalarType() const;

  bool IsTemplateType() const;

  bool IsTypedefType() const;

  bool IsVoidType() const;

  /// This is used when you don't care about the signedness of the integer.
  bool IsInteger() const;

  bool IsFloat() const;

  /// This is used when you don't care about the signedness of the enum.
  bool IsEnumerationType() const;

  bool IsUnscopedEnumerationType() const;

  bool IsIntegerOrUnscopedEnumerationType() const;

  bool IsSigned() const;

  bool IsNullPtrType() const;

  bool IsBoolean() const;

  bool IsEnumerationIntegerTypeSigned() const;

  bool IsScalarOrUnscopedEnumerationType() const;

  bool IsPromotableIntegerType() const;

  bool IsPointerToVoid() const;

  bool IsRecordType() const;

  //// Checks whether `target_base` is a virtual base of `type` (direct or
  /// indirect). If it is, stores the first virtual base type on the path from
  /// `type` to `target_type`. Parameter "virtual_base" is where the first
  /// virtual base type gets stored. Parameter "carry_virtual" is used to
  /// denote that we're in a recursive check of virtual base classes and we
  /// have already seen a virtual base class (so should only check direct
  /// base classes).
  /// Note: This may only be defined in TypeSystemClang.
  bool IsVirtualBase(CompilerType target_base, CompilerType *virtual_base,
                     bool carry_virtual = false) const;

  /// This may only be defined in TypeSystemClang.
  bool IsContextuallyConvertibleToBool() const;

  bool IsBasicType() const;

  std::string TypeDescription();

  bool CompareTypes(CompilerType rhs) const;

  const char *GetTypeTag();

  /// Go through the base classes and count non-empty ones.
  uint32_t GetNumberOfNonEmptyBaseClasses();

  /// \}

  /// Type Completion.
  /// \{
  bool GetCompleteType() const;
  /// \}

  bool IsForcefullyCompleted() const;

  /// AST related queries.
  /// \{
  size_t GetPointerByteSize() const;
  /// \}

  unsigned GetPtrAuthKey() const;

  unsigned GetPtrAuthDiscriminator() const;

  bool GetPtrAuthAddressDiversity() const;

  /// Accessors.
  /// \{

  /// Returns a shared pointer to the type system. The
  /// TypeSystem::TypeSystemSPWrapper can be compared for equality.
  TypeSystemSPWrapper GetTypeSystem() const;

  ConstString GetTypeName(bool BaseOnly = false) const;

  ConstString GetDisplayTypeName() const;

  uint32_t
  GetTypeInfo(CompilerType *pointee_or_element_compiler_type = nullptr) const;

  lldb::LanguageType GetMinimumLanguage();

  lldb::opaque_compiler_type_t GetOpaqueQualType() const { return m_type; }

  lldb::TypeClass GetTypeClass() const;

  void SetCompilerType(lldb::TypeSystemWP type_system,
                       lldb::opaque_compiler_type_t type);
  void SetCompilerType(TypeSystemSPWrapper type_system,
                       lldb::opaque_compiler_type_t type);

  unsigned GetTypeQualifiers() const;
  /// \}

  /// Creating related types.
  /// \{
  CompilerType GetArrayElementType(ExecutionContextScope *exe_scope) const;

  CompilerType GetArrayType(uint64_t size) const;

  CompilerType GetCanonicalType() const;

  CompilerType GetFullyUnqualifiedType() const;

  CompilerType GetEnumerationIntegerType() const;

  /// Returns -1 if this isn't a function of if the function doesn't
  /// have a prototype Returns a value >= 0 if there is a prototype.
  int GetFunctionArgumentCount() const;

  CompilerType GetFunctionArgumentTypeAtIndex(size_t idx) const;

  CompilerType GetFunctionReturnType() const;

  size_t GetNumMemberFunctions() const;

  TypeMemberFunctionImpl GetMemberFunctionAtIndex(size_t idx);

  /// If this type is a reference to a type (L value or R value reference),
  /// return a new type with the reference removed, else return the current type
  /// itself.
  CompilerType GetNonReferenceType() const;

  /// If this type is a pointer type, return the type that the pointer points
  /// to, else return an invalid type.
  CompilerType GetPointeeType() const;

  /// Return a new CompilerType that is a pointer to this type
  CompilerType GetPointerType() const;

  /// Return a new CompilerType that is a L value reference to this type if this
  /// type is valid and the type system supports L value references, else return
  /// an invalid type.
  CompilerType GetLValueReferenceType() const;

  /// Return a new CompilerType that is a R value reference to this type if this
  /// type is valid and the type system supports R value references, else return
  /// an invalid type.
  CompilerType GetRValueReferenceType() const;

  /// Return a new CompilerType adds a const modifier to this type if this type
  /// is valid and the type system supports const modifiers, else return an
  /// invalid type.
  CompilerType AddConstModifier() const;

  /// Return a new CompilerType adds a volatile modifier to this type if this
  /// type is valid and the type system supports volatile modifiers, else return
  /// an invalid type.
  CompilerType AddVolatileModifier() const;

  /// Return a new CompilerType that is the atomic type of this type. If this
  /// type is not valid or the type system doesn't support atomic types, this
  /// returns an invalid type.
  CompilerType GetAtomicType() const;

  /// Return a new CompilerType adds a restrict modifier to this type if this
  /// type is valid and the type system supports restrict modifiers, else return
  /// an invalid type.
  CompilerType AddRestrictModifier() const;

  /// Create a typedef to this type using "name" as the name of the typedef this
  /// type is valid and the type system supports typedefs, else return an
  /// invalid type.
  /// \param payload   The typesystem-specific \p lldb::Type payload.
  CompilerType CreateTypedef(const char *name,
                             const CompilerDeclContext &decl_ctx,
                             uint32_t payload) const;

  /// If the current object represents a typedef type, get the underlying type
  CompilerType GetTypedefedType() const;

  /// Create related types using the current type's AST
  CompilerType GetBasicTypeFromAST(lldb::BasicType basic_type) const;

  /// Return a new CompilerType adds a ptrauth modifier from the given 32-bit
  /// opaque payload to this type if this type is valid and the type system
  /// supports ptrauth modifiers, else return an invalid type. Note that this
  /// does not check if this type is a pointer.
  CompilerType AddPtrAuthModifier(uint32_t payload) const;
  /// \}

  /// Exploring the type.
  /// \{
  struct IntegralTemplateArgument;

  /// Return the size of the type in bytes.
  std::optional<uint64_t> GetByteSize(ExecutionContextScope *exe_scope) const;
  /// Return the size of the type in bits.
  std::optional<uint64_t> GetBitSize(ExecutionContextScope *exe_scope) const;

  lldb::Encoding GetEncoding(uint64_t &count) const;

  lldb::Format GetFormat() const;

  std::optional<size_t> GetTypeBitAlign(ExecutionContextScope *exe_scope) const;

  llvm::Expected<uint32_t>
  GetNumChildren(bool omit_empty_base_classes,
                 const ExecutionContext *exe_ctx) const;

  lldb::BasicType GetBasicTypeEnumeration() const;

  /// If this type is an enumeration, iterate through all of its enumerators
  /// using a callback. If the callback returns true, keep iterating, else abort
  /// the iteration.
  void ForEachEnumerator(
      std::function<bool(const CompilerType &integer_type, ConstString name,
                         const llvm::APSInt &value)> const &callback) const;

  uint32_t GetNumFields() const;

  CompilerType GetFieldAtIndex(size_t idx, std::string &name,
                               uint64_t *bit_offset_ptr,
                               uint32_t *bitfield_bit_size_ptr,
                               bool *is_bitfield_ptr) const;

  uint32_t GetNumDirectBaseClasses() const;

  uint32_t GetNumVirtualBaseClasses() const;

  CompilerType GetDirectBaseClassAtIndex(size_t idx,
                                         uint32_t *bit_offset_ptr) const;

  CompilerType GetVirtualBaseClassAtIndex(size_t idx,
                                          uint32_t *bit_offset_ptr) const;

  CompilerDecl GetStaticFieldWithName(llvm::StringRef name) const;

  uint32_t GetIndexOfFieldWithName(const char *name,
                                   CompilerType *field_compiler_type = nullptr,
                                   uint64_t *bit_offset_ptr = nullptr,
                                   uint32_t *bitfield_bit_size_ptr = nullptr,
                                   bool *is_bitfield_ptr = nullptr) const;

  llvm::Expected<CompilerType> GetChildCompilerTypeAtIndex(
      ExecutionContext *exe_ctx, size_t idx, bool transparent_pointers,
      bool omit_empty_base_classes, bool ignore_array_bounds,
      std::string &child_name, uint32_t &child_byte_size,
      int32_t &child_byte_offset, uint32_t &child_bitfield_bit_size,
      uint32_t &child_bitfield_bit_offset, bool &child_is_base_class,
      bool &child_is_deref_of_parent, ValueObject *valobj,
      uint64_t &language_flags) const;

  /// Lookup a child given a name. This function will match base class names and
  /// member member names in "clang_type" only, not descendants.
  uint32_t GetIndexOfChildWithName(llvm::StringRef name,
                                   bool omit_empty_base_classes) const;

  /// Lookup a child member given a name. This function will match member names
  /// only and will descend into "clang_type" children in search for the first
  /// member in this class, or any base class that matches "name".
  /// TODO: Return all matches for a given name by returning a
  /// vector<vector<uint32_t>>
  /// so we catch all names that match a given child name, not just the first.
  size_t
  GetIndexOfChildMemberWithName(llvm::StringRef name,
                                bool omit_empty_base_classes,
                                std::vector<uint32_t> &child_indexes) const;

  CompilerType GetDirectNestedTypeWithName(llvm::StringRef name) const;

  /// Return the number of template arguments the type has.
  /// If expand_pack is true, then variadic argument packs are automatically
  /// expanded to their supplied arguments. If it is false an argument pack
  /// will only count as 1 argument.
  size_t GetNumTemplateArguments(bool expand_pack = false) const;

  // Return the TemplateArgumentKind of the template argument at index idx.
  // If expand_pack is true, then variadic argument packs are automatically
  // expanded to their supplied arguments. With expand_pack set to false, an
  // arguement pack will count as 1 argument and return a type of Pack.
  lldb::TemplateArgumentKind
  GetTemplateArgumentKind(size_t idx, bool expand_pack = false) const;
  CompilerType GetTypeTemplateArgument(size_t idx,
                                       bool expand_pack = false) const;

  /// Returns the value of the template argument and its type.
  /// If expand_pack is true, then variadic argument packs are automatically
  /// expanded to their supplied arguments. With expand_pack set to false, an
  /// arguement pack will count as 1 argument and it is invalid to call this
  /// method on the pack argument.
  std::optional<IntegralTemplateArgument>
  GetIntegralTemplateArgument(size_t idx, bool expand_pack = false) const;

  CompilerType GetTypeForFormatters() const;

  LazyBool ShouldPrintAsOneLiner(ValueObject *valobj) const;

  bool IsMeaninglessWithoutDynamicResolution() const;
  /// \}

  /// Dumping types.
  /// \{
#ifndef NDEBUG
  /// Convenience LLVM-style dump method for use in the debugger only.
  /// Don't call this function from actual code.
  LLVM_DUMP_METHOD void dump() const;
#endif

  bool DumpTypeValue(Stream *s, lldb::Format format, const DataExtractor &data,
                     lldb::offset_t data_offset, size_t data_byte_size,
                     uint32_t bitfield_bit_size, uint32_t bitfield_bit_offset,
                     ExecutionContextScope *exe_scope);

  /// Dump to stdout.
  void DumpTypeDescription(lldb::DescriptionLevel level =
                           lldb::eDescriptionLevelFull) const;

  /// Print a description of the type to a stream. The exact implementation
  /// varies, but the expectation is that eDescriptionLevelFull returns a
  /// source-like representation of the type, whereas eDescriptionLevelVerbose
  /// does a dump of the underlying AST if applicable.
  void DumpTypeDescription(Stream *s, lldb::DescriptionLevel level =
                                          lldb::eDescriptionLevelFull) const;
  /// \}

  bool GetValueAsScalar(const DataExtractor &data, lldb::offset_t data_offset,
                        size_t data_byte_size, Scalar &value,
                        ExecutionContextScope *exe_scope) const;
  void Clear() {
    m_type_system = {};
    m_type = nullptr;
  }

private:
#ifndef NDEBUG
  /// If the type is valid, ask the TypeSystem to verify the integrity
  /// of the type to catch CompilerTypes that mix and match invalid
  /// TypeSystem/Opaque type pairs.
  bool Verify() const;
#endif

  lldb::TypeSystemWP m_type_system;
  lldb::opaque_compiler_type_t m_type = nullptr;
};

bool operator==(const CompilerType &lhs, const CompilerType &rhs);
bool operator!=(const CompilerType &lhs, const CompilerType &rhs);

struct CompilerType::IntegralTemplateArgument {
  llvm::APSInt value;
  CompilerType type;
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_COMPILERTYPE_H
