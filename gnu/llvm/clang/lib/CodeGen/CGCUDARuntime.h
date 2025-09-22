//===----- CGCUDARuntime.h - Interface to CUDA Runtimes ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides an abstract class for CUDA code generation.  Concrete
// subclasses of this implement code generation for specific CUDA
// runtime libraries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGCUDARUNTIME_H
#define LLVM_CLANG_LIB_CODEGEN_CGCUDARUNTIME_H

#include "clang/AST/GlobalDecl.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Frontend/Offloading/Utility.h"
#include "llvm/IR/GlobalValue.h"

namespace llvm {
class Function;
class GlobalVariable;
}

namespace clang {

class CUDAKernelCallExpr;
class NamedDecl;
class VarDecl;

namespace CodeGen {

class CodeGenFunction;
class CodeGenModule;
class FunctionArgList;
class ReturnValueSlot;
class RValue;

class CGCUDARuntime {
protected:
  CodeGenModule &CGM;

public:
  // Global variable properties that must be passed to CUDA runtime.
  class DeviceVarFlags {
  public:
    enum DeviceVarKind {
      Variable, // Variable
      Surface,  // Builtin surface
      Texture,  // Builtin texture
    };

  private:
    LLVM_PREFERRED_TYPE(DeviceVarKind)
    unsigned Kind : 2;
    LLVM_PREFERRED_TYPE(bool)
    unsigned Extern : 1;
    LLVM_PREFERRED_TYPE(bool)
    unsigned Constant : 1;   // Constant variable.
    LLVM_PREFERRED_TYPE(bool)
    unsigned Managed : 1;    // Managed variable.
    LLVM_PREFERRED_TYPE(bool)
    unsigned Normalized : 1; // Normalized texture.
    int SurfTexType;         // Type of surface/texutre.

  public:
    DeviceVarFlags(DeviceVarKind K, bool E, bool C, bool M, bool N, int T)
        : Kind(K), Extern(E), Constant(C), Managed(M), Normalized(N),
          SurfTexType(T) {}

    DeviceVarKind getKind() const { return static_cast<DeviceVarKind>(Kind); }
    bool isExtern() const { return Extern; }
    bool isConstant() const { return Constant; }
    bool isManaged() const { return Managed; }
    bool isNormalized() const { return Normalized; }
    int getSurfTexType() const { return SurfTexType; }
  };

  CGCUDARuntime(CodeGenModule &CGM) : CGM(CGM) {}
  virtual ~CGCUDARuntime();

  virtual RValue EmitCUDAKernelCallExpr(CodeGenFunction &CGF,
                                        const CUDAKernelCallExpr *E,
                                        ReturnValueSlot ReturnValue);

  /// Emits a kernel launch stub.
  virtual void emitDeviceStub(CodeGenFunction &CGF, FunctionArgList &Args) = 0;

  /// Check whether a variable is a device variable and register it if true.
  virtual void handleVarRegistration(const VarDecl *VD,
                                     llvm::GlobalVariable &Var) = 0;

  /// Finalize generated LLVM module. Returns a module constructor function
  /// to be added or a null pointer.
  virtual llvm::Function *finalizeModule() = 0;

  /// Returns function or variable name on device side even if the current
  /// compilation is for host.
  virtual std::string getDeviceSideName(const NamedDecl *ND) = 0;

  /// Get kernel handle by stub function.
  virtual llvm::GlobalValue *getKernelHandle(llvm::Function *Stub,
                                             GlobalDecl GD) = 0;

  /// Get kernel stub by kernel handle.
  virtual llvm::Function *getKernelStub(llvm::GlobalValue *Handle) = 0;

  /// Adjust linkage of shadow variables in host compilation.
  virtual void
  internalizeDeviceSideVar(const VarDecl *D,
                           llvm::GlobalValue::LinkageTypes &Linkage) = 0;
};

/// Creates an instance of a CUDA runtime class.
CGCUDARuntime *CreateNVCUDARuntime(CodeGenModule &CGM);

}
}

#endif
