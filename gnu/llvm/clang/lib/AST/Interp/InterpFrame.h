//===--- InterpFrame.h - Call Frame implementation for the VM ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the class storing information about stack frames in the interpreter.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_INTERPFRAME_H
#define LLVM_CLANG_AST_INTERP_INTERPFRAME_H

#include "Frame.h"
#include "Program.h"

namespace clang {
namespace interp {
class Function;
class InterpState;
class Pointer;

/// Frame storing local variables.
class InterpFrame final : public Frame {
public:
  /// The frame of the previous function.
  InterpFrame *Caller;

  /// Creates a new frame for a method call.
  InterpFrame(InterpState &S, const Function *Func, InterpFrame *Caller,
              CodePtr RetPC, unsigned ArgSize);

  /// Creates a new frame with the values that make sense.
  /// I.e., the caller is the current frame of S,
  /// the This() pointer is the current Pointer on the top of S's stack,
  /// and the RVO pointer is before that.
  InterpFrame(InterpState &S, const Function *Func, CodePtr RetPC,
              unsigned VarArgSize = 0);

  /// Destroys the frame, killing all live pointers to stack slots.
  ~InterpFrame();

  /// Invokes the destructors for a scope.
  void destroy(unsigned Idx);

  /// Pops the arguments off the stack.
  void popArgs();

  /// Describes the frame with arguments for diagnostic purposes.
  void describe(llvm::raw_ostream &OS) const override;

  /// Returns the parent frame object.
  Frame *getCaller() const override;

  /// Returns the location of the call to the frame.
  SourceRange getCallRange() const override;

  /// Returns the caller.
  const FunctionDecl *getCallee() const override;

  /// Returns the current function.
  const Function *getFunction() const { return Func; }

  /// Returns the offset on the stack at which the frame starts.
  size_t getFrameOffset() const { return FrameOffset; }

  /// Returns the value of a local variable.
  template <typename T> const T &getLocal(unsigned Offset) const {
    return localRef<T>(Offset);
  }

  /// Mutates a local variable.
  template <typename T> void setLocal(unsigned Offset, const T &Value) {
    localRef<T>(Offset) = Value;
    localInlineDesc(Offset)->IsInitialized = true;
  }

  /// Returns a pointer to a local variables.
  Pointer getLocalPointer(unsigned Offset) const;

  /// Returns the value of an argument.
  template <typename T> const T &getParam(unsigned Offset) const {
    auto Pt = Params.find(Offset);
    if (Pt == Params.end())
      return stackRef<T>(Offset);
    return Pointer(reinterpret_cast<Block *>(Pt->second.get())).deref<T>();
  }

  /// Mutates a local copy of a parameter.
  template <typename T> void setParam(unsigned Offset, const T &Value) {
     getParamPointer(Offset).deref<T>() = Value;
  }

  /// Returns a pointer to an argument - lazily creates a block.
  Pointer getParamPointer(unsigned Offset);

  /// Returns the 'this' pointer.
  const Pointer &getThis() const { return This; }

  /// Returns the RVO pointer, if the Function has one.
  const Pointer &getRVOPtr() const { return RVOPtr; }

  /// Checks if the frame is a root frame - return should quit the interpreter.
  bool isRoot() const { return !Func; }

  /// Returns the PC of the frame's code start.
  CodePtr getPC() const { return Func->getCodeBegin(); }

  /// Returns the return address of the frame.
  CodePtr getRetPC() const { return RetPC; }

  /// Map a location to a source.
  virtual SourceInfo getSource(CodePtr PC) const;
  const Expr *getExpr(CodePtr PC) const;
  SourceLocation getLocation(CodePtr PC) const;
  SourceRange getRange(CodePtr PC) const;

  unsigned getDepth() const { return Depth; }

  void dump() const { dump(llvm::errs(), 0); }
  void dump(llvm::raw_ostream &OS, unsigned Indent = 0) const;

private:
  /// Returns an original argument from the stack.
  template <typename T> const T &stackRef(unsigned Offset) const {
    assert(Args);
    return *reinterpret_cast<const T *>(Args - ArgSize + Offset);
  }

  /// Returns an offset to a local.
  template <typename T> T &localRef(unsigned Offset) const {
    return getLocalPointer(Offset).deref<T>();
  }

  /// Returns a pointer to a local's block.
  Block *localBlock(unsigned Offset) const {
    return reinterpret_cast<Block *>(Locals.get() + Offset - sizeof(Block));
  }

  /// Returns the inline descriptor of the local.
  InlineDescriptor *localInlineDesc(unsigned Offset) const {
    return reinterpret_cast<InlineDescriptor *>(Locals.get() + Offset);
  }

private:
  /// Reference to the interpreter state.
  InterpState &S;
  /// Depth of this frame.
  unsigned Depth;
  /// Reference to the function being executed.
  const Function *Func;
  /// Current object pointer for methods.
  Pointer This;
  /// Pointer the non-primitive return value gets constructed in.
  Pointer RVOPtr;
  /// Return address.
  CodePtr RetPC;
  /// The size of all the arguments.
  const unsigned ArgSize;
  /// Pointer to the arguments in the callee's frame.
  char *Args = nullptr;
  /// Fixed, initial storage for known local variables.
  std::unique_ptr<char[]> Locals;
  /// Offset on the stack at entry.
  const size_t FrameOffset;
  /// Mapping from arg offsets to their argument blocks.
  llvm::DenseMap<unsigned, std::unique_ptr<char[]>> Params;
};

} // namespace interp
} // namespace clang

#endif
