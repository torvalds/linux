//===- SandboxIR.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Sandbox IR is a lightweight overlay transactional IR on top of LLVM IR.
// Features:
// - You can save/rollback the state of the IR at any time.
// - Any changes made to Sandbox IR will automatically update the underlying
//   LLVM IR so both IRs are always in sync.
// - Feels like LLVM IR, similar API.
//
// SandboxIR forms a class hierarchy that resembles that of LLVM IR
// but is in the `sandboxir` namespace:
//
// namespace sandboxir {
//
//        +- Argument                   +- BinaryOperator
//        |                             |
// Value -+- BasicBlock                 +- BranchInst
//        |                             |
//        +- Function   +- Constant     +- CastInst
//        |             |               |
//        +- User ------+- Instruction -+- CallInst
//                                      |
//                                      +- CmpInst
//                                      |
//                                      +- ExtractElementInst
//                                      |
//                                      +- GetElementPtrInst
//                                      |
//                                      +- InsertElementInst
//                                      |
//                                      +- LoadInst
//                                      |
//                                      +- OpaqueInst
//                                      |
//                                      +- PHINode
//                                      |
//                                      +- ReturnInst
//                                      |
//                                      +- SelectInst
//                                      |
//                                      +- ShuffleVectorInst
//                                      |
//                                      +- StoreInst
//                                      |
//                                      +- UnaryOperator
//
// Use
//
// } // namespace sandboxir
//

#ifndef LLVM_SANDBOXIR_SANDBOXIR_H
#define LLVM_SANDBOXIR_SANDBOXIR_H

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/SandboxIR/Tracker.h"
#include "llvm/SandboxIR/Use.h"
#include "llvm/Support/raw_ostream.h"
#include <iterator>

namespace llvm {

namespace sandboxir {

class BasicBlock;
class Context;
class Function;
class Instruction;
class SelectInst;
class LoadInst;
class ReturnInst;
class StoreInst;
class User;
class Value;

/// Iterator for the `Use` edges of a User's operands.
/// \Returns the operand `Use` when dereferenced.
class OperandUseIterator {
  sandboxir::Use Use;
  /// Don't let the user create a non-empty OperandUseIterator.
  OperandUseIterator(const class Use &Use) : Use(Use) {}
  friend class User;                                  // For constructor
#define DEF_INSTR(ID, OPC, CLASS) friend class CLASS; // For constructor
#include "llvm/SandboxIR/SandboxIRValues.def"

public:
  using difference_type = std::ptrdiff_t;
  using value_type = sandboxir::Use;
  using pointer = value_type *;
  using reference = value_type &;
  using iterator_category = std::input_iterator_tag;

  OperandUseIterator() = default;
  value_type operator*() const;
  OperandUseIterator &operator++();
  bool operator==(const OperandUseIterator &Other) const {
    return Use == Other.Use;
  }
  bool operator!=(const OperandUseIterator &Other) const {
    return !(*this == Other);
  }
};

/// Iterator for the `Use` edges of a Value's users.
/// \Returns a `Use` when dereferenced.
class UserUseIterator {
  sandboxir::Use Use;
  /// Don't let the user create a non-empty UserUseIterator.
  UserUseIterator(const class Use &Use) : Use(Use) {}
  friend class Value; // For constructor

public:
  using difference_type = std::ptrdiff_t;
  using value_type = sandboxir::Use;
  using pointer = value_type *;
  using reference = value_type &;
  using iterator_category = std::input_iterator_tag;

  UserUseIterator() = default;
  value_type operator*() const { return Use; }
  UserUseIterator &operator++();
  bool operator==(const UserUseIterator &Other) const {
    return Use == Other.Use;
  }
  bool operator!=(const UserUseIterator &Other) const {
    return !(*this == Other);
  }
};

/// A SandboxIR Value has users. This is the base class.
class Value {
public:
  enum class ClassID : unsigned {
#define DEF_VALUE(ID, CLASS) ID,
#define DEF_USER(ID, CLASS) ID,
#define DEF_INSTR(ID, OPC, CLASS) ID,
#include "llvm/SandboxIR/SandboxIRValues.def"
  };

protected:
  static const char *getSubclassIDStr(ClassID ID) {
    switch (ID) {
#define DEF_VALUE(ID, CLASS)                                                   \
  case ClassID::ID:                                                            \
    return #ID;
#define DEF_USER(ID, CLASS)                                                    \
  case ClassID::ID:                                                            \
    return #ID;
#define DEF_INSTR(ID, OPC, CLASS)                                              \
  case ClassID::ID:                                                            \
    return #ID;
#include "llvm/SandboxIR/SandboxIRValues.def"
    }
    llvm_unreachable("Unimplemented ID");
  }

  /// For isa/dyn_cast.
  ClassID SubclassID;
#ifndef NDEBUG
  /// A unique ID used for forming the name (used for debugging).
  unsigned UID;
#endif
  /// The LLVM Value that corresponds to this SandboxIR Value.
  /// NOTE: Some sandboxir Instructions, like Packs, may include more than one
  /// value and in these cases `Val` points to the last instruction in program
  /// order.
  llvm::Value *Val = nullptr;

  friend class Context;    // For getting `Val`.
  friend class User;       // For getting `Val`.
  friend class Use;        // For getting `Val`.
  friend class SelectInst; // For getting `Val`.
  friend class LoadInst;   // For getting `Val`.
  friend class StoreInst;  // For getting `Val`.
  friend class ReturnInst; // For getting `Val`.

  /// All values point to the context.
  Context &Ctx;
  // This is used by eraseFromParent().
  void clearValue() { Val = nullptr; }
  template <typename ItTy, typename SBTy> friend class LLVMOpUserItToSBTy;

  Value(ClassID SubclassID, llvm::Value *Val, Context &Ctx);

public:
  virtual ~Value() = default;
  ClassID getSubclassID() const { return SubclassID; }

  using use_iterator = UserUseIterator;
  using const_use_iterator = UserUseIterator;

  use_iterator use_begin();
  const_use_iterator use_begin() const {
    return const_cast<Value *>(this)->use_begin();
  }
  use_iterator use_end() { return use_iterator(Use(nullptr, nullptr, Ctx)); }
  const_use_iterator use_end() const {
    return const_cast<Value *>(this)->use_end();
  }

  iterator_range<use_iterator> uses() {
    return make_range<use_iterator>(use_begin(), use_end());
  }
  iterator_range<const_use_iterator> uses() const {
    return make_range<const_use_iterator>(use_begin(), use_end());
  }

  /// Helper for mapped_iterator.
  struct UseToUser {
    User *operator()(const Use &Use) const { return &*Use.getUser(); }
  };

  using user_iterator = mapped_iterator<sandboxir::UserUseIterator, UseToUser>;
  using const_user_iterator = user_iterator;

  user_iterator user_begin();
  user_iterator user_end() {
    return user_iterator(Use(nullptr, nullptr, Ctx), UseToUser());
  }
  const_user_iterator user_begin() const {
    return const_cast<Value *>(this)->user_begin();
  }
  const_user_iterator user_end() const {
    return const_cast<Value *>(this)->user_end();
  }

  iterator_range<user_iterator> users() {
    return make_range<user_iterator>(user_begin(), user_end());
  }
  iterator_range<const_user_iterator> users() const {
    return make_range<const_user_iterator>(user_begin(), user_end());
  }
  /// \Returns the number of user edges (not necessarily to unique users).
  /// WARNING: This is a linear-time operation.
  unsigned getNumUses() const;
  /// Return true if this value has N uses or more.
  /// This is logically equivalent to getNumUses() >= N.
  /// WARNING: This can be expensive, as it is linear to the number of users.
  bool hasNUsesOrMore(unsigned Num) const {
    unsigned Cnt = 0;
    for (auto It = use_begin(), ItE = use_end(); It != ItE; ++It) {
      if (++Cnt >= Num)
        return true;
    }
    return false;
  }
  /// Return true if this Value has exactly N uses.
  bool hasNUses(unsigned Num) const {
    unsigned Cnt = 0;
    for (auto It = use_begin(), ItE = use_end(); It != ItE; ++It) {
      if (++Cnt > Num)
        return false;
    }
    return Cnt == Num;
  }

  Type *getType() const { return Val->getType(); }

  Context &getContext() const { return Ctx; }

  void replaceUsesWithIf(Value *OtherV,
                         llvm::function_ref<bool(const Use &)> ShouldReplace);
  void replaceAllUsesWith(Value *Other);

  /// \Returns the LLVM IR name of the bottom-most LLVM value.
  StringRef getName() const { return Val->getName(); }

#ifndef NDEBUG
  /// Should crash if there is something wrong with the instruction.
  virtual void verify() const = 0;
  /// Returns the unique id in the form 'SB<number>.' like 'SB1.'
  std::string getUid() const;
  virtual void dumpCommonHeader(raw_ostream &OS) const;
  void dumpCommonFooter(raw_ostream &OS) const;
  void dumpCommonPrefix(raw_ostream &OS) const;
  void dumpCommonSuffix(raw_ostream &OS) const;
  void printAsOperandCommon(raw_ostream &OS) const;
  friend raw_ostream &operator<<(raw_ostream &OS, const sandboxir::Value &V) {
    V.dump(OS);
    return OS;
  }
  virtual void dump(raw_ostream &OS) const = 0;
  LLVM_DUMP_METHOD virtual void dump() const = 0;
#endif
};

/// Argument of a sandboxir::Function.
class Argument : public sandboxir::Value {
  Argument(llvm::Argument *Arg, sandboxir::Context &Ctx)
      : sandboxir::Value(ClassID::Argument, Arg, Ctx) {}
  friend class Context; // For constructor.

public:
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::Argument;
  }
#ifndef NDEBUG
  void verify() const final {
    assert(isa<llvm::Argument>(Val) && "Expected Argument!");
  }
  friend raw_ostream &operator<<(raw_ostream &OS,
                                 const sandboxir::Argument &TArg) {
    TArg.dump(OS);
    return OS;
  }
  void printAsOperand(raw_ostream &OS) const;
  void dump(raw_ostream &OS) const final;
  LLVM_DUMP_METHOD void dump() const final;
#endif
};

/// A sandboxir::User has operands.
class User : public Value {
protected:
  User(ClassID ID, llvm::Value *V, Context &Ctx) : Value(ID, V, Ctx) {}

  /// \Returns the Use edge that corresponds to \p OpIdx.
  /// Note: This is the default implementation that works for instructions that
  /// match the underlying LLVM instruction. All others should use a different
  /// implementation.
  Use getOperandUseDefault(unsigned OpIdx, bool Verify) const;
  /// \Returns the Use for the \p OpIdx'th operand. This is virtual to allow
  /// instructions to deviate from the LLVM IR operands, which is a requirement
  /// for sandboxir Instructions that consist of more than one LLVM Instruction.
  virtual Use getOperandUseInternal(unsigned OpIdx, bool Verify) const = 0;
  friend class OperandUseIterator; // for getOperandUseInternal()

  /// The default implementation works only for single-LLVMIR-instruction
  /// Users and only if they match exactly the LLVM instruction.
  unsigned getUseOperandNoDefault(const Use &Use) const {
    return Use.LLVMUse->getOperandNo();
  }
  /// \Returns the operand index of \p Use.
  virtual unsigned getUseOperandNo(const Use &Use) const = 0;
  friend unsigned Use::getOperandNo() const; // For getUseOperandNo()

#ifndef NDEBUG
  void verifyUserOfLLVMUse(const llvm::Use &Use) const;
#endif // NDEBUG

public:
  /// For isa/dyn_cast.
  static bool classof(const Value *From);
  using op_iterator = OperandUseIterator;
  using const_op_iterator = OperandUseIterator;
  using op_range = iterator_range<op_iterator>;
  using const_op_range = iterator_range<const_op_iterator>;

  virtual op_iterator op_begin() {
    assert(isa<llvm::User>(Val) && "Expect User value!");
    return op_iterator(getOperandUseInternal(0, /*Verify=*/false));
  }
  virtual op_iterator op_end() {
    assert(isa<llvm::User>(Val) && "Expect User value!");
    return op_iterator(
        getOperandUseInternal(getNumOperands(), /*Verify=*/false));
  }
  virtual const_op_iterator op_begin() const {
    return const_cast<User *>(this)->op_begin();
  }
  virtual const_op_iterator op_end() const {
    return const_cast<User *>(this)->op_end();
  }

  op_range operands() { return make_range<op_iterator>(op_begin(), op_end()); }
  const_op_range operands() const {
    return make_range<const_op_iterator>(op_begin(), op_end());
  }
  Value *getOperand(unsigned OpIdx) const { return getOperandUse(OpIdx).get(); }
  /// \Returns the operand edge for \p OpIdx. NOTE: This should also work for
  /// OpIdx == getNumOperands(), which is used for op_end().
  Use getOperandUse(unsigned OpIdx) const {
    return getOperandUseInternal(OpIdx, /*Verify=*/true);
  }
  virtual unsigned getNumOperands() const {
    return isa<llvm::User>(Val) ? cast<llvm::User>(Val)->getNumOperands() : 0;
  }

  virtual void setOperand(unsigned OperandIdx, Value *Operand);
  /// Replaces any operands that match \p FromV with \p ToV. Returns whether any
  /// operands were replaced.
  bool replaceUsesOfWith(Value *FromV, Value *ToV);

#ifndef NDEBUG
  void verify() const override {
    assert(isa<llvm::User>(Val) && "Expected User!");
  }
  void dumpCommonHeader(raw_ostream &OS) const final;
  void dump(raw_ostream &OS) const override {
    // TODO: Remove this tmp implementation once we get the Instruction classes.
  }
  LLVM_DUMP_METHOD void dump() const override {
    // TODO: Remove this tmp implementation once we get the Instruction classes.
  }
#endif
};

class Constant : public sandboxir::User {
  Constant(llvm::Constant *C, sandboxir::Context &SBCtx)
      : sandboxir::User(ClassID::Constant, C, SBCtx) {}
  friend class Context; // For constructor.
  Use getOperandUseInternal(unsigned OpIdx, bool Verify) const final {
    return getOperandUseDefault(OpIdx, Verify);
  }

public:
  static Constant *createInt(Type *Ty, uint64_t V, Context &Ctx,
                             bool IsSigned = false);
  /// For isa/dyn_cast.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::Constant ||
           From->getSubclassID() == ClassID::Function;
  }
  sandboxir::Context &getParent() const { return getContext(); }
  unsigned getUseOperandNo(const Use &Use) const final {
    return getUseOperandNoDefault(Use);
  }
#ifndef NDEBUG
  void verify() const final {
    assert(isa<llvm::Constant>(Val) && "Expected Constant!");
  }
  friend raw_ostream &operator<<(raw_ostream &OS,
                                 const sandboxir::Constant &SBC) {
    SBC.dump(OS);
    return OS;
  }
  void dump(raw_ostream &OS) const override;
  LLVM_DUMP_METHOD void dump() const override;
#endif
};

/// Iterator for `Instruction`s in a `BasicBlock.
/// \Returns an sandboxir::Instruction & when derereferenced.
class BBIterator {
public:
  using difference_type = std::ptrdiff_t;
  using value_type = Instruction;
  using pointer = value_type *;
  using reference = value_type &;
  using iterator_category = std::bidirectional_iterator_tag;

private:
  llvm::BasicBlock *BB;
  llvm::BasicBlock::iterator It;
  Context *Ctx;
  pointer getInstr(llvm::BasicBlock::iterator It) const;

public:
  BBIterator() : BB(nullptr), Ctx(nullptr) {}
  BBIterator(llvm::BasicBlock *BB, llvm::BasicBlock::iterator It, Context *Ctx)
      : BB(BB), It(It), Ctx(Ctx) {}
  reference operator*() const { return *getInstr(It); }
  BBIterator &operator++();
  BBIterator operator++(int) {
    auto Copy = *this;
    ++*this;
    return Copy;
  }
  BBIterator &operator--();
  BBIterator operator--(int) {
    auto Copy = *this;
    --*this;
    return Copy;
  }
  bool operator==(const BBIterator &Other) const {
    assert(Ctx == Other.Ctx && "BBIterators in different context!");
    return It == Other.It;
  }
  bool operator!=(const BBIterator &Other) const { return !(*this == Other); }
  /// \Returns the SBInstruction that corresponds to this iterator, or null if
  /// the instruction is not found in the IR-to-SandboxIR tables.
  pointer get() const { return getInstr(It); }
};

/// A sandboxir::User with operands, opcode and linked with previous/next
/// instructions in an instruction list.
class Instruction : public sandboxir::User {
public:
  enum class Opcode {
#define DEF_VALUE(ID, CLASS)
#define DEF_USER(ID, CLASS)
#define OP(OPC) OPC,
#define DEF_INSTR(ID, OPC, CLASS) OPC
#include "llvm/SandboxIR/SandboxIRValues.def"
  };

protected:
  Instruction(ClassID ID, Opcode Opc, llvm::Instruction *I,
              sandboxir::Context &SBCtx)
      : sandboxir::User(ID, I, SBCtx), Opc(Opc) {}

  Opcode Opc;

  /// A SandboxIR Instruction may map to multiple LLVM IR Instruction. This
  /// returns its topmost LLVM IR instruction.
  llvm::Instruction *getTopmostLLVMInstruction() const;
  friend class SelectInst; // For getTopmostLLVMInstruction().
  friend class LoadInst;   // For getTopmostLLVMInstruction().
  friend class StoreInst;  // For getTopmostLLVMInstruction().
  friend class ReturnInst; // For getTopmostLLVMInstruction().

  /// \Returns the LLVM IR Instructions that this SandboxIR maps to in program
  /// order.
  virtual SmallVector<llvm::Instruction *, 1> getLLVMInstrs() const = 0;
  friend class EraseFromParent; // For getLLVMInstrs().

public:
  static const char *getOpcodeName(Opcode Opc);
#ifndef NDEBUG
  friend raw_ostream &operator<<(raw_ostream &OS, Opcode Opc) {
    OS << getOpcodeName(Opc);
    return OS;
  }
#endif
  /// This is used by BasicBlock::iterator.
  virtual unsigned getNumOfIRInstrs() const = 0;
  /// \Returns a BasicBlock::iterator for this Instruction.
  BBIterator getIterator() const;
  /// \Returns the next sandboxir::Instruction in the block, or nullptr if at
  /// the end of the block.
  Instruction *getNextNode() const;
  /// \Returns the previous sandboxir::Instruction in the block, or nullptr if
  /// at the beginning of the block.
  Instruction *getPrevNode() const;
  /// \Returns this Instruction's opcode. Note that SandboxIR has its own opcode
  /// state to allow for new SandboxIR-specific instructions.
  Opcode getOpcode() const { return Opc; }
  /// Detach this from its parent BasicBlock without deleting it.
  void removeFromParent();
  /// Detach this Value from its parent and delete it.
  void eraseFromParent();
  /// Insert this detached instruction before \p BeforeI.
  void insertBefore(Instruction *BeforeI);
  /// Insert this detached instruction after \p AfterI.
  void insertAfter(Instruction *AfterI);
  /// Insert this detached instruction into \p BB at \p WhereIt.
  void insertInto(BasicBlock *BB, const BBIterator &WhereIt);
  /// Move this instruction to \p WhereIt.
  void moveBefore(BasicBlock &BB, const BBIterator &WhereIt);
  /// Move this instruction before \p Before.
  void moveBefore(Instruction *Before) {
    moveBefore(*Before->getParent(), Before->getIterator());
  }
  /// Move this instruction after \p After.
  void moveAfter(Instruction *After) {
    moveBefore(*After->getParent(), std::next(After->getIterator()));
  }
  /// \Returns the BasicBlock containing this Instruction, or null if it is
  /// detached.
  BasicBlock *getParent() const;
  /// For isa/dyn_cast.
  static bool classof(const sandboxir::Value *From);

#ifndef NDEBUG
  friend raw_ostream &operator<<(raw_ostream &OS,
                                 const sandboxir::Instruction &SBI) {
    SBI.dump(OS);
    return OS;
  }
  void dump(raw_ostream &OS) const override;
  LLVM_DUMP_METHOD void dump() const override;
#endif
};

class SelectInst : public Instruction {
  /// Use Context::createSelectInst(). Don't call the
  /// constructor directly.
  SelectInst(llvm::SelectInst *CI, Context &Ctx)
      : Instruction(ClassID::Select, Opcode::Select, CI, Ctx) {}
  friend Context; // for SelectInst()
  Use getOperandUseInternal(unsigned OpIdx, bool Verify) const final {
    return getOperandUseDefault(OpIdx, Verify);
  }
  SmallVector<llvm::Instruction *, 1> getLLVMInstrs() const final {
    return {cast<llvm::Instruction>(Val)};
  }
  static Value *createCommon(Value *Cond, Value *True, Value *False,
                             const Twine &Name, IRBuilder<> &Builder,
                             Context &Ctx);

public:
  unsigned getUseOperandNo(const Use &Use) const final {
    return getUseOperandNoDefault(Use);
  }
  unsigned getNumOfIRInstrs() const final { return 1u; }
  static Value *create(Value *Cond, Value *True, Value *False,
                       Instruction *InsertBefore, Context &Ctx,
                       const Twine &Name = "");
  static Value *create(Value *Cond, Value *True, Value *False,
                       BasicBlock *InsertAtEnd, Context &Ctx,
                       const Twine &Name = "");
  Value *getCondition() { return getOperand(0); }
  Value *getTrueValue() { return getOperand(1); }
  Value *getFalseValue() { return getOperand(2); }

  void setCondition(Value *New) { setOperand(0, New); }
  void setTrueValue(Value *New) { setOperand(1, New); }
  void setFalseValue(Value *New) { setOperand(2, New); }
  void swapValues() { cast<llvm::SelectInst>(Val)->swapValues(); }
  /// For isa/dyn_cast.
  static bool classof(const Value *From);
#ifndef NDEBUG
  void verify() const final {
    assert(isa<llvm::SelectInst>(Val) && "Expected SelectInst!");
  }
  void dump(raw_ostream &OS) const override;
  LLVM_DUMP_METHOD void dump() const override;
#endif
};

class LoadInst final : public Instruction {
  /// Use LoadInst::create() instead of calling the constructor.
  LoadInst(llvm::LoadInst *LI, Context &Ctx)
      : Instruction(ClassID::Load, Opcode::Load, LI, Ctx) {}
  friend Context; // for LoadInst()
  Use getOperandUseInternal(unsigned OpIdx, bool Verify) const final {
    return getOperandUseDefault(OpIdx, Verify);
  }
  SmallVector<llvm::Instruction *, 1> getLLVMInstrs() const final {
    return {cast<llvm::Instruction>(Val)};
  }

public:
  unsigned getUseOperandNo(const Use &Use) const final {
    return getUseOperandNoDefault(Use);
  }

  unsigned getNumOfIRInstrs() const final { return 1u; }
  static LoadInst *create(Type *Ty, Value *Ptr, MaybeAlign Align,
                          Instruction *InsertBefore, Context &Ctx,
                          const Twine &Name = "");
  static LoadInst *create(Type *Ty, Value *Ptr, MaybeAlign Align,
                          BasicBlock *InsertAtEnd, Context &Ctx,
                          const Twine &Name = "");
  /// For isa/dyn_cast.
  static bool classof(const Value *From);
  Value *getPointerOperand() const;
  Align getAlign() const { return cast<llvm::LoadInst>(Val)->getAlign(); }
  bool isUnordered() const { return cast<llvm::LoadInst>(Val)->isUnordered(); }
  bool isSimple() const { return cast<llvm::LoadInst>(Val)->isSimple(); }
#ifndef NDEBUG
  void verify() const final {
    assert(isa<llvm::LoadInst>(Val) && "Expected LoadInst!");
  }
  void dump(raw_ostream &OS) const override;
  LLVM_DUMP_METHOD void dump() const override;
#endif
};

class StoreInst final : public Instruction {
  /// Use StoreInst::create().
  StoreInst(llvm::StoreInst *SI, Context &Ctx)
      : Instruction(ClassID::Store, Opcode::Store, SI, Ctx) {}
  friend Context; // for StoreInst()
  Use getOperandUseInternal(unsigned OpIdx, bool Verify) const final {
    return getOperandUseDefault(OpIdx, Verify);
  }
  SmallVector<llvm::Instruction *, 1> getLLVMInstrs() const final {
    return {cast<llvm::Instruction>(Val)};
  }

public:
  unsigned getUseOperandNo(const Use &Use) const final {
    return getUseOperandNoDefault(Use);
  }
  unsigned getNumOfIRInstrs() const final { return 1u; }
  static StoreInst *create(Value *V, Value *Ptr, MaybeAlign Align,
                           Instruction *InsertBefore, Context &Ctx);
  static StoreInst *create(Value *V, Value *Ptr, MaybeAlign Align,
                           BasicBlock *InsertAtEnd, Context &Ctx);
  /// For isa/dyn_cast.
  static bool classof(const Value *From);
  Value *getValueOperand() const;
  Value *getPointerOperand() const;
  Align getAlign() const { return cast<llvm::StoreInst>(Val)->getAlign(); }
  bool isSimple() const { return cast<llvm::StoreInst>(Val)->isSimple(); }
  bool isUnordered() const { return cast<llvm::StoreInst>(Val)->isUnordered(); }
#ifndef NDEBUG
  void verify() const final {
    assert(isa<llvm::StoreInst>(Val) && "Expected StoreInst!");
  }
  void dump(raw_ostream &OS) const override;
  LLVM_DUMP_METHOD void dump() const override;
#endif
};

class ReturnInst final : public Instruction {
  /// Use ReturnInst::create() instead of calling the constructor.
  ReturnInst(llvm::Instruction *I, Context &Ctx)
      : Instruction(ClassID::Ret, Opcode::Ret, I, Ctx) {}
  ReturnInst(ClassID SubclassID, llvm::Instruction *I, Context &Ctx)
      : Instruction(SubclassID, Opcode::Ret, I, Ctx) {}
  friend class Context; // For accessing the constructor in create*()
  Use getOperandUseInternal(unsigned OpIdx, bool Verify) const final {
    return getOperandUseDefault(OpIdx, Verify);
  }
  SmallVector<llvm::Instruction *, 1> getLLVMInstrs() const final {
    return {cast<llvm::Instruction>(Val)};
  }
  static ReturnInst *createCommon(Value *RetVal, IRBuilder<> &Builder,
                                  Context &Ctx);

public:
  static ReturnInst *create(Value *RetVal, Instruction *InsertBefore,
                            Context &Ctx);
  static ReturnInst *create(Value *RetVal, BasicBlock *InsertAtEnd,
                            Context &Ctx);
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::Ret;
  }
  unsigned getUseOperandNo(const Use &Use) const final {
    return getUseOperandNoDefault(Use);
  }
  unsigned getNumOfIRInstrs() const final { return 1u; }
  /// \Returns null if there is no return value.
  Value *getReturnValue() const;
#ifndef NDEBUG
  void verify() const final {}
  void dump(raw_ostream &OS) const override;
  LLVM_DUMP_METHOD void dump() const override;
#endif
};

/// An LLLVM Instruction that has no SandboxIR equivalent class gets mapped to
/// an OpaqueInstr.
class OpaqueInst : public sandboxir::Instruction {
  OpaqueInst(llvm::Instruction *I, sandboxir::Context &Ctx)
      : sandboxir::Instruction(ClassID::Opaque, Opcode::Opaque, I, Ctx) {}
  OpaqueInst(ClassID SubclassID, llvm::Instruction *I, sandboxir::Context &Ctx)
      : sandboxir::Instruction(SubclassID, Opcode::Opaque, I, Ctx) {}
  friend class Context; // For constructor.
  Use getOperandUseInternal(unsigned OpIdx, bool Verify) const final {
    return getOperandUseDefault(OpIdx, Verify);
  }
  SmallVector<llvm::Instruction *, 1> getLLVMInstrs() const final {
    return {cast<llvm::Instruction>(Val)};
  }

public:
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::Opaque;
  }
  unsigned getUseOperandNo(const Use &Use) const final {
    return getUseOperandNoDefault(Use);
  }
  unsigned getNumOfIRInstrs() const final { return 1u; }
#ifndef NDEBUG
  void verify() const final {
    // Nothing to do
  }
  friend raw_ostream &operator<<(raw_ostream &OS,
                                 const sandboxir::OpaqueInst &OI) {
    OI.dump(OS);
    return OS;
  }
  void dump(raw_ostream &OS) const override;
  LLVM_DUMP_METHOD void dump() const override;
#endif
};

/// Contains a list of sandboxir::Instruction's.
class BasicBlock : public Value {
  /// Builds a graph that contains all values in \p BB in their original form
  /// i.e., no vectorization is taking place here.
  void buildBasicBlockFromLLVMIR(llvm::BasicBlock *LLVMBB);
  friend class Context;     // For `buildBasicBlockFromIR`
  friend class Instruction; // For LLVM Val.

  BasicBlock(llvm::BasicBlock *BB, Context &SBCtx)
      : Value(ClassID::Block, BB, SBCtx) {
    buildBasicBlockFromLLVMIR(BB);
  }

public:
  ~BasicBlock() = default;
  /// For isa/dyn_cast.
  static bool classof(const Value *From) {
    return From->getSubclassID() == Value::ClassID::Block;
  }
  Function *getParent() const;
  using iterator = BBIterator;
  iterator begin() const;
  iterator end() const {
    auto *BB = cast<llvm::BasicBlock>(Val);
    return iterator(BB, BB->end(), &Ctx);
  }
  std::reverse_iterator<iterator> rbegin() const {
    return std::make_reverse_iterator(end());
  }
  std::reverse_iterator<iterator> rend() const {
    return std::make_reverse_iterator(begin());
  }
  Context &getContext() const { return Ctx; }
  Instruction *getTerminator() const;
  bool empty() const { return begin() == end(); }
  Instruction &front() const;
  Instruction &back() const;

#ifndef NDEBUG
  void verify() const final {
    assert(isa<llvm::BasicBlock>(Val) && "Expected BasicBlock!");
  }
  friend raw_ostream &operator<<(raw_ostream &OS, const BasicBlock &SBBB) {
    SBBB.dump(OS);
    return OS;
  }
  void dump(raw_ostream &OS) const final;
  LLVM_DUMP_METHOD void dump() const final;
#endif
};

class Context {
protected:
  LLVMContext &LLVMCtx;
  Tracker IRTracker;

  /// Maps LLVM Value to the corresponding sandboxir::Value. Owns all
  /// SandboxIR objects.
  DenseMap<llvm::Value *, std::unique_ptr<sandboxir::Value>>
      LLVMValueToValueMap;

  /// Remove \p V from the maps and returns the unique_ptr.
  std::unique_ptr<Value> detachLLVMValue(llvm::Value *V);
  /// Remove \p SBV from all SandboxIR maps and stop owning it. This effectively
  /// detaches \p V from the underlying IR.
  std::unique_ptr<Value> detach(Value *V);
  friend void Instruction::eraseFromParent(); // For detach().
  /// Take ownership of VPtr and store it in `LLVMValueToValueMap`.
  Value *registerValue(std::unique_ptr<Value> &&VPtr);
  friend class EraseFromParent; // For registerValue().
  /// This is the actual function that creates sandboxir values for \p V,
  /// and among others handles all instruction types.
  Value *getOrCreateValueInternal(llvm::Value *V, llvm::User *U = nullptr);
  /// Get or create a sandboxir::Argument for an existing LLVM IR \p LLVMArg.
  Argument *getOrCreateArgument(llvm::Argument *LLVMArg) {
    auto Pair = LLVMValueToValueMap.insert({LLVMArg, nullptr});
    auto It = Pair.first;
    if (Pair.second) {
      It->second = std::unique_ptr<Argument>(new Argument(LLVMArg, *this));
      return cast<Argument>(It->second.get());
    }
    return cast<Argument>(It->second.get());
  }
  /// Get or create a sandboxir::Value for an existing LLVM IR \p LLVMV.
  Value *getOrCreateValue(llvm::Value *LLVMV) {
    return getOrCreateValueInternal(LLVMV, 0);
  }
  /// Get or create a sandboxir::Constant from an existing LLVM IR \p LLVMC.
  Constant *getOrCreateConstant(llvm::Constant *LLVMC) {
    return cast<Constant>(getOrCreateValueInternal(LLVMC, 0));
  }
  friend class Constant; // For getOrCreateConstant().
  /// Create a sandboxir::BasicBlock for an existing LLVM IR \p BB. This will
  /// also create all contents of the block.
  BasicBlock *createBasicBlock(llvm::BasicBlock *BB);

  friend class BasicBlock; // For getOrCreateValue().

  IRBuilder<ConstantFolder> LLVMIRBuilder;
  auto &getLLVMIRBuilder() { return LLVMIRBuilder; }

  SelectInst *createSelectInst(llvm::SelectInst *SI);
  friend SelectInst; // For createSelectInst()
  LoadInst *createLoadInst(llvm::LoadInst *LI);
  friend LoadInst; // For createLoadInst()
  StoreInst *createStoreInst(llvm::StoreInst *SI);
  friend StoreInst; // For createStoreInst()
  ReturnInst *createReturnInst(llvm::ReturnInst *I);
  friend ReturnInst; // For createReturnInst()

public:
  Context(LLVMContext &LLVMCtx)
      : LLVMCtx(LLVMCtx), IRTracker(*this),
        LLVMIRBuilder(LLVMCtx, ConstantFolder()) {}

  Tracker &getTracker() { return IRTracker; }
  /// Convenience function for `getTracker().save()`
  void save() { IRTracker.save(); }
  /// Convenience function for `getTracker().revert()`
  void revert() { IRTracker.revert(); }
  /// Convenience function for `getTracker().accept()`
  void accept() { IRTracker.accept(); }

  sandboxir::Value *getValue(llvm::Value *V) const;
  const sandboxir::Value *getValue(const llvm::Value *V) const {
    return getValue(const_cast<llvm::Value *>(V));
  }
  /// Create a sandboxir::Function for an existing LLVM IR \p F, including all
  /// blocks and instructions.
  /// This is the main API function for creating Sandbox IR.
  Function *createFunction(llvm::Function *F);

  /// \Returns the number of values registered with Context.
  size_t getNumValues() const { return LLVMValueToValueMap.size(); }
};

class Function : public sandboxir::Value {
  /// Helper for mapped_iterator.
  struct LLVMBBToBB {
    Context &Ctx;
    LLVMBBToBB(Context &Ctx) : Ctx(Ctx) {}
    BasicBlock &operator()(llvm::BasicBlock &LLVMBB) const {
      return *cast<BasicBlock>(Ctx.getValue(&LLVMBB));
    }
  };
  /// Use Context::createFunction() instead.
  Function(llvm::Function *F, sandboxir::Context &Ctx)
      : sandboxir::Value(ClassID::Function, F, Ctx) {}
  friend class Context; // For constructor.

public:
  /// For isa/dyn_cast.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::Function;
  }

  Argument *getArg(unsigned Idx) const {
    llvm::Argument *Arg = cast<llvm::Function>(Val)->getArg(Idx);
    return cast<Argument>(Ctx.getValue(Arg));
  }

  size_t arg_size() const { return cast<llvm::Function>(Val)->arg_size(); }
  bool arg_empty() const { return cast<llvm::Function>(Val)->arg_empty(); }

  using iterator = mapped_iterator<llvm::Function::iterator, LLVMBBToBB>;
  iterator begin() const {
    LLVMBBToBB BBGetter(Ctx);
    return iterator(cast<llvm::Function>(Val)->begin(), BBGetter);
  }
  iterator end() const {
    LLVMBBToBB BBGetter(Ctx);
    return iterator(cast<llvm::Function>(Val)->end(), BBGetter);
  }

#ifndef NDEBUG
  void verify() const final {
    assert(isa<llvm::Function>(Val) && "Expected Function!");
  }
  void dumpNameAndArgs(raw_ostream &OS) const;
  void dump(raw_ostream &OS) const final;
  LLVM_DUMP_METHOD void dump() const final;
#endif
};

} // namespace sandboxir
} // namespace llvm

#endif // LLVM_SANDBOXIR_SANDBOXIR_H
