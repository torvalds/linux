//==--- riscv_vector.td - RISC-V V-ext Builtin function list --------------===//
//
//  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
//  See https://llvm.org/LICENSE.txt for license information.
//  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the builtins for RISC-V V-extension. See:
//
//     https://github.com/riscv/rvv-intrinsic-doc
//
//===----------------------------------------------------------------------===//

include "riscv_vector_common.td"

defvar TypeList = ["c","s","i","l","x","f","d","y"];
defvar EEWList = [["8", "(Log2EEW:3)"],
                  ["16", "(Log2EEW:4)"],
                  ["32", "(Log2EEW:5)"],
                  ["64", "(Log2EEW:6)"]];

class IsFloat<string type> {
  bit val = !or(!eq(type, "x"), !eq(type, "f"), !eq(type, "d"), !eq(type, "y"));
}

let SupportOverloading = false,
    MaskedPolicyScheme = NonePolicy in {
  class RVVVLEMaskBuiltin : RVVOutBuiltin<"m", "mPCUe", "c"> {
    let Name = "vlm_v";
    let IRName = "vlm";
    let HasMasked = false;
  }
}

let SupportOverloading = false,
    UnMaskedPolicyScheme = HasPassthruOperand in {
  multiclass RVVVLEBuiltin<list<string> types> {
    let Name = NAME # "_v",
        IRName = "vle",
        MaskedIRName ="vle_mask" in {
      foreach type = types in {
        def : RVVOutBuiltin<"v", "vPCe", type>;
        if !not(IsFloat<type>.val) then {
          def : RVVOutBuiltin<"Uv", "UvPCUe", type>;
        }
      }
    }
  }
}

multiclass RVVVLEFFBuiltin<list<string> types> {
  let Name = NAME # "_v",
      IRName = "vleff",
      MaskedIRName = "vleff_mask",
      SupportOverloading = false,
      UnMaskedPolicyScheme = HasPassthruOperand,
      ManualCodegen = [{
      {
        if (IsMasked) {
          // Move mask to right before vl.
          std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end() - 1);
          if ((PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA))
            Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
          Ops.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));
          IntrinsicTypes = {ResultType, Ops[4]->getType()};
        } else {
          if (PolicyAttrs & RVV_VTA)
            Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
          IntrinsicTypes = {ResultType, Ops[3]->getType()};
        }
        Value *NewVL = Ops[2];
        Ops.erase(Ops.begin() + 2);
        llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);
        llvm::Value *LoadValue = Builder.CreateCall(F, Ops, "");
        llvm::Value *V = Builder.CreateExtractValue(LoadValue, {0});
        // Store new_vl.
        clang::CharUnits Align;
        if (IsMasked)
          Align = CGM.getNaturalPointeeTypeAlignment(E->getArg(E->getNumArgs()-2)->getType());
        else
          Align = CGM.getNaturalPointeeTypeAlignment(E->getArg(1)->getType());
        llvm::Value *Val = Builder.CreateExtractValue(LoadValue, {1});
        Builder.CreateStore(Val, Address(NewVL, Val->getType(), Align));
        return V;
      }
      }] in {
    foreach type = types in {
      def : RVVBuiltin<"v", "vPCePz", type>;
      // Skip floating types for unsigned versions.
      if !not(IsFloat<type>.val) then {
        def : RVVBuiltin<"Uv", "UvPCUePz", type>;
      }
    }
  }
}

multiclass RVVVLSEBuiltin<list<string> types> {
  let Name = NAME # "_v",
      IRName = "vlse",
      MaskedIRName ="vlse_mask",
      SupportOverloading = false,
      UnMaskedPolicyScheme = HasPassthruOperand in {
    foreach type = types in {
      def : RVVOutBuiltin<"v", "vPCet", type>;
      if !not(IsFloat<type>.val) then {
        def : RVVOutBuiltin<"Uv", "UvPCUet", type>;
      }
    }
  }
}

multiclass RVVIndexedLoad<string op> {
  let UnMaskedPolicyScheme = HasPassthruOperand in {
    foreach type = TypeList in {
      foreach eew_list = EEWList[0-2] in {
        defvar eew = eew_list[0];
        defvar eew_type = eew_list[1];
        let Name = op # eew # "_v", IRName = op, MaskedIRName = op # "_mask",
            RequiredFeatures = !if(!eq(type, "x"), ["Zvfhmin"],
                                   !if(!eq(type, "y"), ["Zvfbfmin"],
                                                   []<string>)) in {
          def: RVVOutOp1Builtin<"v", "vPCe" # eew_type # "Uv", type>;
            if !not(IsFloat<type>.val) then {
              def: RVVOutOp1Builtin<"Uv", "UvPCUe" # eew_type # "Uv", type>;
            }
        }
      }
      defvar eew64 = "64";
      defvar eew64_type = "(Log2EEW:6)";
      let Name = op # eew64 # "_v", IRName = op, MaskedIRName = op # "_mask",
          RequiredFeatures = !if(!eq(type, "x"), ["Zvfhmin", "RV64"],
                                 !if(!eq(type, "y"), ["Zvfbfmin", "RV64"],
                                                 ["RV64"])) in {
          def: RVVOutOp1Builtin<"v", "vPCe" # eew64_type # "Uv", type>;
            if !not(IsFloat<type>.val) then {
              def: RVVOutOp1Builtin<"Uv", "UvPCUe" # eew64_type # "Uv", type>;
            }
        }
    }
  }
}

let HasMaskedOffOperand = false,
    MaskedPolicyScheme = NonePolicy,
    ManualCodegen = [{
      if (IsMasked) {
        // Builtin: (mask, ptr, value, vl). Intrinsic: (value, ptr, mask, vl)
        std::swap(Ops[0], Ops[2]);
      } else {
        // Builtin: (ptr, value, vl). Intrinsic: (value, ptr, vl)
        std::swap(Ops[0], Ops[1]);
      }
      if (IsMasked)
        IntrinsicTypes = {Ops[0]->getType(), Ops[3]->getType()};
      else
        IntrinsicTypes = {Ops[0]->getType(), Ops[2]->getType()};
    }] in {
  class RVVVSEMaskBuiltin : RVVBuiltin<"m", "0PUem", "c"> {
    let Name = "vsm_v";
    let IRName = "vsm";
    let HasMasked = false;
  }
  multiclass RVVVSEBuiltin<list<string> types> {
    let Name = NAME # "_v",
        IRName = "vse",
        MaskedIRName = "vse_mask" in {
      foreach type = types in {
        def : RVVBuiltin<"v", "0Pev", type>;
        if !not(IsFloat<type>.val) then {
          def : RVVBuiltin<"Uv", "0PUeUv", type>;
        }
      }
    }
  }
}

multiclass RVVVSSEBuiltin<list<string> types> {
  let Name = NAME # "_v",
      IRName = "vsse",
      MaskedIRName = "vsse_mask",
      HasMaskedOffOperand = false,
      MaskedPolicyScheme = NonePolicy,
      ManualCodegen = [{
        if (IsMasked) {
          // Builtin: (mask, ptr, stride, value, vl). Intrinsic: (value, ptr, stride, mask, vl)
          std::swap(Ops[0], Ops[3]);
        } else {
          // Builtin: (ptr, stride, value, vl). Intrinsic: (value, ptr, stride, vl)
          std::rotate(Ops.begin(), Ops.begin() + 2, Ops.begin() + 3);
        }
        if (IsMasked)
          IntrinsicTypes = {Ops[0]->getType(), Ops[4]->getType()};
        else
          IntrinsicTypes = {Ops[0]->getType(), Ops[3]->getType()};
      }] in {
    foreach type = types in {
      def : RVVBuiltin<"v", "0Petv", type>;
      if !not(IsFloat<type>.val) then {
        def : RVVBuiltin<"Uv", "0PUetUv", type>;
      }
    }
  }
}

multiclass RVVIndexedStore<string op> {
  let HasMaskedOffOperand = false,
      MaskedPolicyScheme = NonePolicy,
      ManualCodegen = [{
        if (IsMasked) {
          // Builtin: (mask, ptr, index, value, vl). Intrinsic: (value, ptr, index, mask, vl)
          std::swap(Ops[0], Ops[3]);
        } else {
          // Builtin: (ptr, index, value, vl). Intrinsic: (value, ptr, index, vl)
          std::rotate(Ops.begin(), Ops.begin() + 2, Ops.begin() + 3);
        }
        if (IsMasked)
          IntrinsicTypes = {Ops[0]->getType(), Ops[2]->getType(), Ops[4]->getType()};
        else
          IntrinsicTypes = {Ops[0]->getType(), Ops[2]->getType(), Ops[3]->getType()};
      }] in {
      foreach type = TypeList in {
        foreach eew_list = EEWList[0-2] in {
          defvar eew = eew_list[0];
          defvar eew_type = eew_list[1];
          let Name = op # eew  # "_v", IRName = op, MaskedIRName = op # "_mask",
          RequiredFeatures = !if(!eq(type, "x"), ["Zvfhmin"],
                                 !if(!eq(type, "y"), ["Zvfbfmin"],
                                                 []<string>)) in  {
            def : RVVBuiltin<"v", "0Pe" # eew_type # "Uvv", type>;
            if !not(IsFloat<type>.val) then {
              def : RVVBuiltin<"Uv", "0PUe" # eew_type # "UvUv", type>;
            }
          }
        }
        defvar eew64 = "64";
        defvar eew64_type = "(Log2EEW:6)";
        let Name = op # eew64  # "_v", IRName = op, MaskedIRName = op # "_mask",
            RequiredFeatures = !if(!eq(type, "x"), ["Zvfhmin", "RV64"],
                                   !if(!eq(type, "y"), ["Zvfbfmin", "RV64"],
                                                   ["RV64"])) in  {
          def : RVVBuiltin<"v", "0Pe" # eew64_type # "Uvv", type>;
          if !not(IsFloat<type>.val) then {
            def : RVVBuiltin<"Uv", "0PUe" # eew64_type # "UvUv", type>;
          }
        }
      }
  }
}

defvar NFList = [2, 3, 4, 5, 6, 7, 8];
/*
A segment load builtin has different variants.

Therefore a segment unit-stride load builtin can have 4 variants,
1. When unmasked and the policies are all specified as agnostic:
(Address0, ..., Address{NF - 1}, Ptr, VL)
2. When masked and the policies are all specified as agnostic:
(Address0, ..., Address{NF - 1}, Mask, Ptr, VL)
3. When unmasked and one of the policies is specified as undisturbed:
(Address0, ..., Address{NF - 1}, Maskedoff0, ..., Maskedoff{NF - 1},
  Ptr, VL)
4. When masked and one of the policies is specified as undisturbed:
(Address0, ..., Address{NF - 1}, Mask, Maskedoff0, ..., Maskedoff{NF - 1},
  Ptr, VL)

Other variants of segment load builtin share the same structure, but they
have their own extra parameter.

The segment unit-stride fault-only-first load builtin has a 'NewVL'
operand after the 'Ptr' operand.
1. When unmasked and the policies are all specified as agnostic:
(Address0, ..., Address{NF - 1}, Ptr, NewVL, VL)
2. When masked and the policies are all specified as agnostic:
(Address0, ..., Address{NF - 1}, Mask, Ptr, NewVL, VL)
3. When unmasked and one of the policies is specified as undisturbed:
(Address0, ..., Address{NF - 1}, Maskedoff0, ..., Maskedoff{NF - 1},
  Ptr, NewVL, VL)
4. When masked and one of the policies is specified as undisturbed:
(Address0, ..., Address{NF - 1}, Mask, Maskedoff0, ..., Maskedoff{NF - 1},
  Ptr, NewVL, VL)

The segment strided load builtin has a 'Stride' operand after the 'Ptr'
operand.
1. When unmasked and the policies are all specified as agnostic:
(Address0, ..., Address{NF - 1}, Ptr, Stride, VL)
2. When masked and the policies are all specified as agnostic:
(Address0, ..., Address{NF - 1}, Mask, Ptr, Stride, VL)
3. When unmasked and one of the policies is specified as undisturbed:
(Address0, ..., Address{NF - 1}, Maskedoff0, ..., Maskedoff{NF - 1},
  Ptr, Stride, VL)
4. When masked and one of the policies is specified as undisturbed:
(Address0, ..., Address{NF - 1}, Mask, Maskedoff0, ..., Maskedoff{NF - 1},
  Ptr, Stride, VL)

The segment indexed load builtin has a 'Idx' operand after the 'Ptr' operand.
1. When unmasked and the policies are all specified as agnostic:
(Address0, ..., Address{NF - 1}, Ptr, Idx, VL)
2. When masked and the policies are all specified as agnostic:
(Address0, ..., Address{NF - 1}, Mask, Ptr, Idx, VL)
3. When unmasked and one of the policies is specified as undisturbed:
(Address0, ..., Address{NF - 1}, Maskedoff0, ..., Maskedoff{NF - 1},
  Ptr, Idx, VL)
4. When masked and one of the policies is specified as undisturbed:
(Address0, ..., Address{NF - 1}, Mask, Maskedoff0, ..., Maskedoff{NF - 1},
  Ptr, Idx, VL)

Segment load intrinsics has different variants similar to their builtins.

Segment unit-stride load intrinsic,
  Masked: (Vector0, ..., Vector{NF - 1}, Ptr, Mask, VL, Policy)
  Unmasked: (Vector0, ..., Vector{NF - 1}, Ptr, VL)
Segment unit-stride fault-only-first load intrinsic,
  Masked: (Vector0, ..., Vector{NF - 1}, Ptr, Mask, VL, Policy)
  Unmasked: (Vector0, ..., Vector{NF - 1}, Ptr, VL)
Segment strided load intrinsic,
  Masked: (Vector0, ..., Vector{NF - 1}, Ptr, Stride, Mask, VL, Policy)
  Unmasked: (Vector0, ..., Vector{NF - 1}, Ptr, Stride, VL)
Segment indexed load intrinsic,
  Masked: (Vector0, ..., Vector{NF - 1}, Ptr, Index, Mask, VL, Policy)
  Unmasked: (Vector0, ..., Vector{NF - 1}, Ptr, Index, VL)

The Vector(s) is poison when the policy behavior allows us to not care
about any masked-off elements.
*/

class PVString<int nf, bit signed> {
  string S =
    !cond(!eq(nf, 2): !if(signed, "PvPv", "PUvPUv"),
          !eq(nf, 3): !if(signed, "PvPvPv", "PUvPUvPUv"),
          !eq(nf, 4): !if(signed, "PvPvPvPv", "PUvPUvPUvPUv"),
          !eq(nf, 5): !if(signed, "PvPvPvPvPv", "PUvPUvPUvPUvPUv"),
          !eq(nf, 6): !if(signed, "PvPvPvPvPvPv", "PUvPUvPUvPUvPUvPUv"),
          !eq(nf, 7): !if(signed, "PvPvPvPvPvPvPv", "PUvPUvPUvPUvPUvPUvPUv"),
          !eq(nf, 8): !if(signed, "PvPvPvPvPvPvPvPv", "PUvPUvPUvPUvPUvPUvPUvPUv"));
}

class VString<int nf, bit signed> {
  string S = !cond(!eq(nf, 2): !if(signed, "vv", "UvUv"),
                   !eq(nf, 3): !if(signed, "vvv", "UvUvUv"),
                   !eq(nf, 4): !if(signed, "vvvv", "UvUvUvUv"),
                   !eq(nf, 5): !if(signed, "vvvvv", "UvUvUvUvUv"),
                   !eq(nf, 6): !if(signed, "vvvvvv", "UvUvUvUvUvUv"),
                   !eq(nf, 7): !if(signed, "vvvvvvv", "UvUvUvUvUvUvUv"),
                   !eq(nf, 8): !if(signed, "vvvvvvvv", "UvUvUvUvUvUvUvUv"));
}


class FixedVString<int fixed_lmul, int num, string vec> {
  string V = "(LFixedLog2LMUL:" # fixed_lmul # ")" # vec;
  string S = !interleave(!listsplat(V, num), "");
}

multiclass RVVNonTupleVCreateBuiltin<int dst_lmul, list<int> src_lmul_list> {
  defvar dst_v = FixedVString<dst_lmul, 1, "v">.V;
  defvar dst_uv = FixedVString<dst_lmul, 1, "Uv">.V;
  foreach src_lmul = src_lmul_list in {
    defvar num = !shl(1, !sub(dst_lmul, src_lmul));

    defvar src_v = FixedVString<src_lmul, num, "v">.V;
    defvar src_s = FixedVString<src_lmul, num, "v">.S;
    def vcreate # src_v # dst_v : RVVBuiltin<src_v # dst_v,
                                             dst_v # src_s,
                                             "csilxfd">;
    let RequiredFeatures = ["Zvfbfmin"] in
      def vcreate_bf16 # src_v # dst_v : RVVBuiltin<src_v # dst_v,
                                                    dst_v # src_s,
                                                    "y", dst_v>;

    defvar src_uv = FixedVString<src_lmul, num, "Uv">.V;
    defvar src_us = FixedVString<src_lmul, num, "Uv">.S;
    def vcreate_u # src_uv # dst_uv : RVVBuiltin<src_uv # dst_uv,
                                                 dst_uv # src_us,
                                                 "csil">;
  }
}

multiclass RVVPseudoUnaryBuiltin<string IR, string type_range> {
  let Name = NAME,
      IRName = IR,
      MaskedIRName = IR # "_mask",
      UnMaskedPolicyScheme = HasPassthruOperand,
      ManualCodegen = [{
      {
        if (IsMasked) {
          std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end() - 1);
          if ((PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA))
            Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
        } else {
          if (PolicyAttrs & RVV_VTA)
            Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
        }
        auto ElemTy = cast<llvm::VectorType>(ResultType)->getElementType();
        Ops.insert(Ops.begin() + 2, llvm::Constant::getNullValue(ElemTy));

        if (IsMasked) {
          Ops.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));
          // maskedoff, op1, op2, mask, vl, policy
          IntrinsicTypes = {ResultType, ElemTy, Ops[4]->getType()};
        } else {
          // passthru, op1, op2, vl
          IntrinsicTypes = {ResultType, ElemTy, Ops[3]->getType()};
        }
        break;
      }
      }] in {
        def : RVVBuiltin<"v", "vv", type_range>;
  }
}

multiclass RVVPseudoVNotBuiltin<string IR, string type_range> {
  let Name = NAME,
      IRName = IR,
      MaskedIRName = IR # "_mask",
      UnMaskedPolicyScheme = HasPassthruOperand,
      ManualCodegen = [{
      {
        if (IsMasked) {
          std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end() - 1);
          if ((PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA))
            Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
        } else {
          if (PolicyAttrs & RVV_VTA)
            Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
        }
        auto ElemTy = cast<llvm::VectorType>(ResultType)->getElementType();
        Ops.insert(Ops.begin() + 2,
                   llvm::Constant::getAllOnesValue(ElemTy));
        if (IsMasked) {
          Ops.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));
          // maskedoff, op1, po2, mask, vl, policy
          IntrinsicTypes = {ResultType,
                            ElemTy,
                            Ops[4]->getType()};
        } else {
          // passthru, op1, op2, vl
          IntrinsicTypes = {ResultType,
                            ElemTy,
                            Ops[3]->getType()};
        }
        break;
      }
      }] in {
        def : RVVBuiltin<"v", "vv", type_range>;
        def : RVVBuiltin<"Uv", "UvUv", type_range>;
  }
}

multiclass RVVPseudoMaskBuiltin<string IR, string type_range> {
  let Name = NAME,
      IRName = IR,
      HasMasked = false,
      ManualCodegen = [{
      {
        // op1, vl
        IntrinsicTypes = {ResultType,
                          Ops[1]->getType()};
        Ops.insert(Ops.begin() + 1, Ops[0]);
        break;
      }
      }] in {
        def : RVVBuiltin<"m", "mm", type_range>;
  }
}

multiclass RVVPseudoVFUnaryBuiltin<string IR, string type_range> {
  let Name = NAME,
      IRName = IR,
      MaskedIRName = IR # "_mask",
      UnMaskedPolicyScheme = HasPassthruOperand,
      ManualCodegen = [{
      {
        if (IsMasked) {
          std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end() - 1);
          if ((PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA))
            Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
          Ops.insert(Ops.begin() + 2, Ops[1]);
          Ops.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));
          // maskedoff, op1, op2, mask, vl
          IntrinsicTypes = {ResultType,
                            Ops[2]->getType(),
                            Ops.back()->getType()};
        } else {
          if (PolicyAttrs & RVV_VTA)
            Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
          // op1, po2, vl
          IntrinsicTypes = {ResultType,
                            Ops[1]->getType(), Ops[2]->getType()};
          Ops.insert(Ops.begin() + 2, Ops[1]);
          break;
        }
        break;
      }
      }] in {
        def : RVVBuiltin<"v", "vv", type_range>;
  }
}

multiclass RVVPseudoVWCVTBuiltin<string IR, string MName, string type_range,
                                 list<list<string>> suffixes_prototypes> {
  let Name = NAME,
      OverloadedName = MName,
      IRName = IR,
      MaskedIRName = IR # "_mask",
      UnMaskedPolicyScheme = HasPassthruOperand,
      ManualCodegen = [{
      {
        if (IsMasked) {
          std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end() - 1);
          if ((PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA))
            Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
        } else {
          if (PolicyAttrs & RVV_VTA)
            Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
        }
        auto ElemTy = cast<llvm::VectorType>(ResultType)->getElementType();
        Ops.insert(Ops.begin() + 2, llvm::Constant::getNullValue(ElemTy));
        if (IsMasked) {
          Ops.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));
          // maskedoff, op1, op2, mask, vl, policy
          IntrinsicTypes = {ResultType,
                            Ops[1]->getType(),
                            ElemTy,
                            Ops[4]->getType()};
        } else {
          // passtru, op1, op2, vl
          IntrinsicTypes = {ResultType,
                            Ops[1]->getType(),
                            ElemTy,
                            Ops[3]->getType()};
        }
        break;
      }
      }] in {
        foreach s_p = suffixes_prototypes in {
          def : RVVBuiltin<s_p[0], s_p[1], type_range>;
        }
  }
}

multiclass RVVPseudoVNCVTBuiltin<string IR, string MName, string type_range,
                                 list<list<string>> suffixes_prototypes> {
  let Name = NAME,
      OverloadedName = MName,
      IRName = IR,
      MaskedIRName = IR # "_mask",
      UnMaskedPolicyScheme = HasPassthruOperand,
      ManualCodegen = [{
      {
        if (IsMasked) {
          std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end() - 1);
          if ((PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA))
            Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
        } else {
          if (PolicyAttrs & RVV_VTA)
            Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
        }
        Ops.insert(Ops.begin() + 2, llvm::Constant::getNullValue(Ops.back()->getType()));
        if (IsMasked) {
          Ops.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));
          // maskedoff, op1, xlen, mask, vl
          IntrinsicTypes = {ResultType,
                            Ops[1]->getType(),
                            Ops[4]->getType(),
                            Ops[4]->getType()};
        } else {
          // passthru, op1, xlen, vl
          IntrinsicTypes = {ResultType,
                  Ops[1]->getType(),
                  Ops[3]->getType(),
                  Ops[3]->getType()};
        }
        break;
      }
      }] in {
        foreach s_p = suffixes_prototypes in {
          def : RVVBuiltin<s_p[0], s_p[1], type_range>;
        }
  }
}

let HeaderCode =
[{
#define __riscv_vlenb() __builtin_rvv_vlenb()
}] in
def vlenb_macro: RVVHeader;

let HasBuiltinAlias = false, HasVL = false, HasMasked = false,
    UnMaskedPolicyScheme = NonePolicy, MaskedPolicyScheme = NonePolicy,
    Log2LMUL = [0], IRName = "",
    ManualCodegen = [{
    {
      LLVMContext &Context = CGM.getLLVMContext();
      llvm::MDBuilder MDHelper(Context);

      llvm::Metadata *Ops[] = {llvm::MDString::get(Context, "vlenb")};
      llvm::MDNode *RegName = llvm::MDNode::get(Context, Ops);
      llvm::Value *Metadata = llvm::MetadataAsValue::get(Context, RegName);
      llvm::Function *F =
        CGM.getIntrinsic(llvm::Intrinsic::read_register, {SizeTy});
      return Builder.CreateCall(F, Metadata);
    }
    }] in
{
  def vlenb : RVVBuiltin<"", "u", "i">;
}

// 6. Configuration-Setting Instructions
// 6.1. vsetvli/vsetvl instructions

// vsetvl/vsetvlmax are a macro because they require constant integers in SEW
// and LMUL.
let HeaderCode =
[{
#define __riscv_vsetvl_e8mf4(avl) __builtin_rvv_vsetvli((size_t)(avl), 0, 6)
#define __riscv_vsetvl_e8mf2(avl) __builtin_rvv_vsetvli((size_t)(avl), 0, 7)
#define __riscv_vsetvl_e8m1(avl) __builtin_rvv_vsetvli((size_t)(avl), 0, 0)
#define __riscv_vsetvl_e8m2(avl) __builtin_rvv_vsetvli((size_t)(avl), 0, 1)
#define __riscv_vsetvl_e8m4(avl) __builtin_rvv_vsetvli((size_t)(avl), 0, 2)
#define __riscv_vsetvl_e8m8(avl) __builtin_rvv_vsetvli((size_t)(avl), 0, 3)

#define __riscv_vsetvl_e16mf2(avl) __builtin_rvv_vsetvli((size_t)(avl), 1, 7)
#define __riscv_vsetvl_e16m1(avl) __builtin_rvv_vsetvli((size_t)(avl), 1, 0)
#define __riscv_vsetvl_e16m2(avl) __builtin_rvv_vsetvli((size_t)(avl), 1, 1)
#define __riscv_vsetvl_e16m4(avl) __builtin_rvv_vsetvli((size_t)(avl), 1, 2)
#define __riscv_vsetvl_e16m8(avl) __builtin_rvv_vsetvli((size_t)(avl), 1, 3)

#define __riscv_vsetvl_e32m1(avl) __builtin_rvv_vsetvli((size_t)(avl), 2, 0)
#define __riscv_vsetvl_e32m2(avl) __builtin_rvv_vsetvli((size_t)(avl), 2, 1)
#define __riscv_vsetvl_e32m4(avl) __builtin_rvv_vsetvli((size_t)(avl), 2, 2)
#define __riscv_vsetvl_e32m8(avl) __builtin_rvv_vsetvli((size_t)(avl), 2, 3)

#if __riscv_v_elen >= 64
#define __riscv_vsetvl_e8mf8(avl) __builtin_rvv_vsetvli((size_t)(avl), 0, 5)
#define __riscv_vsetvl_e16mf4(avl) __builtin_rvv_vsetvli((size_t)(avl), 1, 6)
#define __riscv_vsetvl_e32mf2(avl) __builtin_rvv_vsetvli((size_t)(avl), 2, 7)

#define __riscv_vsetvl_e64m1(avl) __builtin_rvv_vsetvli((size_t)(avl), 3, 0)
#define __riscv_vsetvl_e64m2(avl) __builtin_rvv_vsetvli((size_t)(avl), 3, 1)
#define __riscv_vsetvl_e64m4(avl) __builtin_rvv_vsetvli((size_t)(avl), 3, 2)
#define __riscv_vsetvl_e64m8(avl) __builtin_rvv_vsetvli((size_t)(avl), 3, 3)
#endif

#define __riscv_vsetvlmax_e8mf4() __builtin_rvv_vsetvlimax(0, 6)
#define __riscv_vsetvlmax_e8mf2() __builtin_rvv_vsetvlimax(0, 7)
#define __riscv_vsetvlmax_e8m1() __builtin_rvv_vsetvlimax(0, 0)
#define __riscv_vsetvlmax_e8m2() __builtin_rvv_vsetvlimax(0, 1)
#define __riscv_vsetvlmax_e8m4() __builtin_rvv_vsetvlimax(0, 2)
#define __riscv_vsetvlmax_e8m8() __builtin_rvv_vsetvlimax(0, 3)

#define __riscv_vsetvlmax_e16mf2() __builtin_rvv_vsetvlimax(1, 7)
#define __riscv_vsetvlmax_e16m1() __builtin_rvv_vsetvlimax(1, 0)
#define __riscv_vsetvlmax_e16m2() __builtin_rvv_vsetvlimax(1, 1)
#define __riscv_vsetvlmax_e16m4() __builtin_rvv_vsetvlimax(1, 2)
#define __riscv_vsetvlmax_e16m8() __builtin_rvv_vsetvlimax(1, 3)

#define __riscv_vsetvlmax_e32m1() __builtin_rvv_vsetvlimax(2, 0)
#define __riscv_vsetvlmax_e32m2() __builtin_rvv_vsetvlimax(2, 1)
#define __riscv_vsetvlmax_e32m4() __builtin_rvv_vsetvlimax(2, 2)
#define __riscv_vsetvlmax_e32m8() __builtin_rvv_vsetvlimax(2, 3)

#if __riscv_v_elen >= 64
#define __riscv_vsetvlmax_e8mf8() __builtin_rvv_vsetvlimax(0, 5)
#define __riscv_vsetvlmax_e16mf4() __builtin_rvv_vsetvlimax(1, 6)
#define __riscv_vsetvlmax_e32mf2() __builtin_rvv_vsetvlimax(2, 7)

#define __riscv_vsetvlmax_e64m1() __builtin_rvv_vsetvlimax(3, 0)
#define __riscv_vsetvlmax_e64m2() __builtin_rvv_vsetvlimax(3, 1)
#define __riscv_vsetvlmax_e64m4() __builtin_rvv_vsetvlimax(3, 2)
#define __riscv_vsetvlmax_e64m8() __builtin_rvv_vsetvlimax(3, 3)
#endif

}] in
def vsetvl_macro: RVVHeader;

let HasBuiltinAlias = false,
    HasVL = false,
    HasMasked = false,
    MaskedPolicyScheme = NonePolicy,
    Log2LMUL = [0],
    ManualCodegen = [{IntrinsicTypes = {ResultType};}] in // Set XLEN type
{
  def vsetvli : RVVBuiltin<"", "zzKzKz", "i">;
  def vsetvlimax : RVVBuiltin<"", "zKzKz", "i">;
}

// 7. Vector Loads and Stores
// 7.4. Vector Unit-Stride Instructions
def vlm: RVVVLEMaskBuiltin;
defm vle8: RVVVLEBuiltin<["c"]>;
defm vle16: RVVVLEBuiltin<["s"]>;
let Name = "vle16_v", RequiredFeatures = ["Zvfhmin"] in
  defm vle16_h: RVVVLEBuiltin<["x"]>;
let Name = "vle16_v", RequiredFeatures = ["Zvfbfmin"] in
  defm vle16_bf16 : RVVVLEBuiltin<["y"]>;
defm vle32: RVVVLEBuiltin<["i","f"]>;
defm vle64: RVVVLEBuiltin<["l","d"]>;

def vsm : RVVVSEMaskBuiltin;
defm vse8 : RVVVSEBuiltin<["c"]>;
defm vse16: RVVVSEBuiltin<["s"]>;
let Name = "vse16_v", RequiredFeatures = ["Zvfhmin"] in
  defm vse16_h: RVVVSEBuiltin<["x"]>;
let Name = "vse16_v", RequiredFeatures = ["Zvfbfmin"] in
  defm vse16_bf16: RVVVSEBuiltin<["y"]>;
defm vse32: RVVVSEBuiltin<["i","f"]>;
defm vse64: RVVVSEBuiltin<["l","d"]>;

// 7.5. Vector Strided Instructions
defm vlse8: RVVVLSEBuiltin<["c"]>;
defm vlse16: RVVVLSEBuiltin<["s"]>;
let Name = "vlse16_v", RequiredFeatures = ["Zvfhmin"] in
  defm vlse16_h: RVVVLSEBuiltin<["x"]>;
let Name = "vlse16_v", RequiredFeatures = ["Zvfbfmin"] in
  defm vlse16_bf16: RVVVLSEBuiltin<["y"]>;
defm vlse32: RVVVLSEBuiltin<["i","f"]>;
defm vlse64: RVVVLSEBuiltin<["l","d"]>;

defm vsse8 : RVVVSSEBuiltin<["c"]>;
defm vsse16: RVVVSSEBuiltin<["s"]>;
let Name = "vsse16_v", RequiredFeatures = ["Zvfhmin"] in
  defm vsse16_h: RVVVSSEBuiltin<["x"]>;
let Name = "vsse16_v", RequiredFeatures = ["Zvfbfmin"] in
  defm vsse16_bf: RVVVSSEBuiltin<["y"]>;
defm vsse32: RVVVSSEBuiltin<["i","f"]>;
defm vsse64: RVVVSSEBuiltin<["l","d"]>;

// 7.6. Vector Indexed Instructions
defm : RVVIndexedLoad<"vluxei">;
defm : RVVIndexedLoad<"vloxei">;

defm : RVVIndexedStore<"vsuxei">;
defm : RVVIndexedStore<"vsoxei">;

// 7.7. Unit-stride Fault-Only-First Loads
defm vle8ff: RVVVLEFFBuiltin<["c"]>;
defm vle16ff: RVVVLEFFBuiltin<["s"]>;
let Name = "vle16ff_v", RequiredFeatures = ["Zvfhmin"] in
  defm vle16ff: RVVVLEFFBuiltin<["x"]>;
let Name = "vle16ff_v", RequiredFeatures = ["Zvfbfmin"] in
  defm vle16ff: RVVVLEFFBuiltin<["y"]>;
defm vle32ff: RVVVLEFFBuiltin<["i", "f"]>;
defm vle64ff: RVVVLEFFBuiltin<["l", "d"]>;

multiclass RVVUnitStridedSegLoadTuple<string op> {
  foreach type = TypeList in {
    defvar eew = !cond(!eq(type, "c") : "8",
                       !eq(type, "s") : "16",
                       !eq(type, "i") : "32",
                       !eq(type, "l") : "64",
                       !eq(type, "x") : "16",
                       !eq(type, "f") : "32",
                       !eq(type, "d") : "64",
                       !eq(type, "y") : "16");
      foreach nf = NFList in {
        let Name = op # nf # "e" # eew # "_v",
            IRName = op # nf,
            MaskedIRName = op # nf # "_mask",
            NF = nf,
            RequiredFeatures = !if(!eq(type, "x"), ["Zvfhmin"],
                                   !if(!eq(type, "y"), ["Zvfbfmin"],
                                                   []<string>)),
            ManualCodegen = [{
    {
      llvm::Type *ElementVectorType = cast<StructType>(ResultType)->elements()[0];
      IntrinsicTypes = {ElementVectorType, Ops.back()->getType()};
      SmallVector<llvm::Value*, 12> Operands;

      bool NoPassthru =
        (IsMasked && (PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA)) |
        (!IsMasked && (PolicyAttrs & RVV_VTA));
      unsigned Offset = IsMasked ? NoPassthru ? 1 : 2 : NoPassthru ? 0 : 1;

      if (NoPassthru) { // Push poison into passthru
        Operands.append(NF, llvm::PoisonValue::get(ElementVectorType));
      } else { // Push intrinsics operands into passthru
        llvm::Value *PassthruOperand = IsMasked ? Ops[1] : Ops[0];
        for (unsigned I = 0; I < NF; ++I)
          Operands.push_back(Builder.CreateExtractValue(PassthruOperand, {I}));
      }

      Operands.push_back(Ops[Offset]); // Ptr
      if (IsMasked)
        Operands.push_back(Ops[0]);
      Operands.push_back(Ops[Offset + 1]); // VL
      if (IsMasked)
        Operands.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));

      llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);

      llvm::Value *LoadValue = Builder.CreateCall(F, Operands, "");
      if (ReturnValue.isNull())
        return LoadValue;
      else
        return Builder.CreateStore(LoadValue, ReturnValue.getValue());
    }
    }] in {
        defvar T = "(Tuple:" # nf # ")";
        def : RVVBuiltin<T # "v", T # "vPCe", type>;
        if !not(IsFloat<type>.val) then {
          def : RVVBuiltin<T # "Uv", T # "UvPCUe", type>;
        }
      }
    }
  }
}

multiclass RVVUnitStridedSegStoreTuple<string op> {
  foreach type = TypeList in {
    defvar eew = !cond(!eq(type, "c") : "8",
                       !eq(type, "s") : "16",
                       !eq(type, "i") : "32",
                       !eq(type, "l") : "64",
                       !eq(type, "x") : "16",
                       !eq(type, "f") : "32",
                       !eq(type, "d") : "64",
                       !eq(type, "y") : "16");
      foreach nf = NFList in {
      let Name = op # nf # "e" # eew # "_v",
          IRName = op # nf,
          MaskedIRName = op # nf # "_mask",
          NF = nf,
          HasMaskedOffOperand = false,
          RequiredFeatures = !if(!eq(type, "x"), ["Zvfhmin"],
                                 !if(!eq(type, "y"), ["Zvfbfmin"],
                                                 []<string>)),
          ManualCodegen = [{
    {
      // Masked
      // Builtin: (mask, ptr, v_tuple, vl)
      // Intrinsic: (val0, val1, ..., ptr, mask, vl)
      // Unmasked
      // Builtin: (ptr, v_tuple, vl)
      // Intrinsic: (val0, val1, ..., ptr, vl)
      unsigned Offset = IsMasked ? 1 : 0;
      llvm::Value *VTupleOperand = Ops[Offset + 1];

      SmallVector<llvm::Value*, 12> Operands;
      for (unsigned I = 0; I < NF; ++I) {
        llvm::Value *V = Builder.CreateExtractValue(VTupleOperand, {I});
        Operands.push_back(V);
      }
      Operands.push_back(Ops[Offset]); // Ptr
      if (IsMasked)
        Operands.push_back(Ops[0]);
      Operands.push_back(Ops[Offset + 2]); // VL

      IntrinsicTypes = {Operands[0]->getType(), Operands.back()->getType()};
      llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);
      return Builder.CreateCall(F, Operands, "");
   }
      }] in {
        defvar T = "(Tuple:" # nf # ")";
        def : RVVBuiltin<T # "v", "0Pe" # T # "v", type>;
        if !not(IsFloat<type>.val) then {
          def : RVVBuiltin<T # "Uv", "0PUe" # T # "Uv", type>;
        }
      }
    }
  }
}

multiclass RVVUnitStridedSegLoadFFTuple<string op> {
  foreach type = TypeList in {
    defvar eew = !cond(!eq(type, "c") : "8",
                       !eq(type, "s") : "16",
                       !eq(type, "i") : "32",
                       !eq(type, "l") : "64",
                       !eq(type, "x") : "16",
                       !eq(type, "f") : "32",
                       !eq(type, "d") : "64",
                       !eq(type, "y") : "16");
      foreach nf = NFList in {
        let Name = op # nf # "e" # eew # "ff_v",
            IRName = op # nf # "ff",
            MaskedIRName = op # nf # "ff_mask",
            NF = nf,
            RequiredFeatures = !if(!eq(type, "x"), ["Zvfhmin"],
                                   !if(!eq(type, "y"), ["Zvfbfmin"],
                                                   []<string>)),
            ManualCodegen = [{
    {
      llvm::Type *ElementVectorType = cast<StructType>(ResultType)->elements()[0];
      IntrinsicTypes = {ElementVectorType, Ops.back()->getType()};
      SmallVector<llvm::Value*, 12> Operands;

      bool NoPassthru =
        (IsMasked && (PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA)) |
        (!IsMasked && (PolicyAttrs & RVV_VTA));
      unsigned Offset = IsMasked ? NoPassthru ? 1 : 2 : NoPassthru ? 0 : 1;

      if (NoPassthru) { // Push poison into passthru
        Operands.append(NF, llvm::PoisonValue::get(ElementVectorType));
      } else { // Push intrinsics operands into passthru
        llvm::Value *PassthruOperand = IsMasked ? Ops[1] : Ops[0];
        for (unsigned I = 0; I < NF; ++I)
          Operands.push_back(Builder.CreateExtractValue(PassthruOperand, {I}));
      }

      Operands.push_back(Ops[Offset]); // Ptr
      if (IsMasked)
        Operands.push_back(Ops[0]);
      Operands.push_back(Ops[Offset + 2]); // vl
      if (IsMasked)
        Operands.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));

      llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);

      llvm::Value *LoadValue = Builder.CreateCall(F, Operands, "");
      // Get alignment from the new vl operand
      clang::CharUnits Align =
          CGM.getNaturalPointeeTypeAlignment(E->getArg(Offset + 1)->getType());

      llvm::Value *ReturnTuple = llvm::PoisonValue::get(ResultType);
      for (unsigned I = 0; I < NF; ++I) {
        llvm::Value *V = Builder.CreateExtractValue(LoadValue, {I});
        ReturnTuple = Builder.CreateInsertValue(ReturnTuple, V, {I});
      }

      // Store new_vl
      llvm::Value *V = Builder.CreateExtractValue(LoadValue, {NF});
      Builder.CreateStore(V, Address(Ops[Offset + 1], V->getType(), Align));

      if (ReturnValue.isNull())
        return ReturnTuple;
      else
        return Builder.CreateStore(ReturnTuple, ReturnValue.getValue());
    }
    }] in {
        defvar T = "(Tuple:" # nf # ")";
        def : RVVBuiltin<T # "v", T # "vPCePz", type>;
        if !not(IsFloat<type>.val) then {
          def : RVVBuiltin<T # "Uv", T # "UvPCUePz", type>;
        }
      }
    }
  }
}

multiclass RVVStridedSegLoadTuple<string op> {
  foreach type = TypeList in {
    defvar eew = !cond(!eq(type, "c") : "8",
                       !eq(type, "s") : "16",
                       !eq(type, "i") : "32",
                       !eq(type, "l") : "64",
                       !eq(type, "x") : "16",
                       !eq(type, "f") : "32",
                       !eq(type, "d") : "64",
                       !eq(type, "y") : "16");
      foreach nf = NFList in {
        let Name = op # nf # "e" # eew # "_v",
            IRName = op # nf,
            MaskedIRName = op # nf # "_mask",
            NF = nf,
            RequiredFeatures = !if(!eq(type, "x"), ["Zvfhmin"],
                                   !if(!eq(type, "y"), ["Zvfbfmin"],
                                                   []<string>)),
            ManualCodegen = [{
    {
      llvm::Type *ElementVectorType = cast<StructType>(ResultType)->elements()[0];
      IntrinsicTypes = {ElementVectorType, Ops.back()->getType()};
      SmallVector<llvm::Value*, 12> Operands;

      bool NoPassthru =
        (IsMasked && (PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA)) |
        (!IsMasked && (PolicyAttrs & RVV_VTA));
      unsigned Offset = IsMasked ? NoPassthru ? 1 : 2 : NoPassthru ? 0 : 1;

      if (NoPassthru) { // Push poison into passthru
        Operands.append(NF, llvm::PoisonValue::get(ElementVectorType));
      } else { // Push intrinsics operands into passthru
        llvm::Value *PassthruOperand = IsMasked ? Ops[1] : Ops[0];
        for (unsigned I = 0; I < NF; ++I)
          Operands.push_back(Builder.CreateExtractValue(PassthruOperand, {I}));
      }

      Operands.push_back(Ops[Offset]); // Ptr
      Operands.push_back(Ops[Offset + 1]); // Stride
      if (IsMasked)
        Operands.push_back(Ops[0]);
      Operands.push_back(Ops[Offset + 2]); // VL
      if (IsMasked)
        Operands.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));

      llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);
      llvm::Value *LoadValue = Builder.CreateCall(F, Operands, "");

      if (ReturnValue.isNull())
        return LoadValue;
      else
        return Builder.CreateStore(LoadValue, ReturnValue.getValue());
    }
    }] in {
        defvar T = "(Tuple:" # nf # ")";
        def : RVVBuiltin<T # "v", T # "vPCet", type>;
        if !not(IsFloat<type>.val) then {
          def : RVVBuiltin<T # "Uv", T # "UvPCUet", type>;
        }
      }
    }
  }
}

multiclass RVVStridedSegStoreTuple<string op> {
  foreach type = TypeList in {
    defvar eew = !cond(!eq(type, "c") : "8",
                       !eq(type, "s") : "16",
                       !eq(type, "i") : "32",
                       !eq(type, "l") : "64",
                       !eq(type, "x") : "16",
                       !eq(type, "f") : "32",
                       !eq(type, "d") : "64",
                       !eq(type, "y") : "16");
      foreach nf = NFList in {
        let Name = op # nf # "e" # eew # "_v",
            IRName = op # nf,
            MaskedIRName = op # nf # "_mask",
            NF = nf,
            HasMaskedOffOperand = false,
            MaskedPolicyScheme = NonePolicy,
            RequiredFeatures = !if(!eq(type, "x"), ["Zvfhmin"],
                                   !if(!eq(type, "y"), ["Zvfbfmin"],
                                                   []<string>)),
            ManualCodegen = [{
    {
      // Masked
      // Builtin: (mask, ptr, stride, v_tuple, vl)
      // Intrinsic: (val0, val1, ..., ptr, stride, mask, vl)
      // Unmasked
      // Builtin: (ptr, stride, v_tuple, vl)
      // Intrinsic: (val0, val1, ..., ptr, stride, vl)
      unsigned Offset = IsMasked ? 1 : 0;
      llvm::Value *VTupleOperand = Ops[Offset + 2];

      SmallVector<llvm::Value*, 12> Operands;
      for (unsigned I = 0; I < NF; ++I) {
        llvm::Value *V = Builder.CreateExtractValue(VTupleOperand, {I});
        Operands.push_back(V);
      }
      Operands.push_back(Ops[Offset]); // Ptr
      Operands.push_back(Ops[Offset + 1]); // Stride
      if (IsMasked)
        Operands.push_back(Ops[0]);
      Operands.push_back(Ops[Offset + 3]); // VL

      IntrinsicTypes = {Operands[0]->getType(), Operands.back()->getType()};
      llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);
      return Builder.CreateCall(F, Operands, "");
    }
            }] in {
        defvar T = "(Tuple:" # nf # ")";
        def : RVVBuiltin<T # "v", "0Pet" # T # "v", type>;
        if !not(IsFloat<type>.val) then {
          def : RVVBuiltin<T # "Uv", "0PUet" # T # "Uv", type>;
        }
      }
    }
  }
}

multiclass RVVIndexedSegLoadTuple<string op> {
  foreach type = TypeList in {
    foreach eew_info = EEWList in {
      defvar eew = eew_info[0];
      defvar eew_type = eew_info[1];
      foreach nf = NFList in {
        let Name = op # nf # "ei" # eew # "_v",
            IRName = op # nf,
            MaskedIRName = op # nf # "_mask",
            NF = nf,
            RequiredFeatures = !if(!eq(type, "x"), ["Zvfhmin"],
                                   !if(!eq(type, "y"), ["Zvfbfmin"],
                                                   []<string>)),
            ManualCodegen = [{
    {
      llvm::Type *ElementVectorType = cast<StructType>(ResultType)->elements()[0];
      IntrinsicTypes = {ElementVectorType, Ops.back()->getType()};
      SmallVector<llvm::Value*, 12> Operands;

      bool NoPassthru =
        (IsMasked && (PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA)) |
        (!IsMasked && (PolicyAttrs & RVV_VTA));
      unsigned Offset = IsMasked ? NoPassthru ? 1 : 2 : NoPassthru ? 0 : 1;

      if (NoPassthru) { // Push poison into passthru
        Operands.append(NF, llvm::PoisonValue::get(ElementVectorType));
      } else { // Push intrinsics operands into passthru
        llvm::Value *PassthruOperand = IsMasked ? Ops[1] : Ops[0];
        for (unsigned I = 0; I < NF; ++I)
          Operands.push_back(Builder.CreateExtractValue(PassthruOperand, {I}));
      }

      Operands.push_back(Ops[Offset]); // Ptr
      Operands.push_back(Ops[Offset + 1]); // Idx
      if (IsMasked)
        Operands.push_back(Ops[0]);
      Operands.push_back(Ops[Offset + 2]); // VL
      if (IsMasked)
        Operands.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));

      IntrinsicTypes = {ElementVectorType, Ops[Offset + 1]->getType(),
                        Ops.back()->getType()};
      llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);
      llvm::Value *LoadValue = Builder.CreateCall(F, Operands, "");

      if (ReturnValue.isNull())
        return LoadValue;
      else
        return Builder.CreateStore(LoadValue, ReturnValue.getValue());
    }
    }] in {
          defvar T = "(Tuple:" # nf # ")";
          def : RVVBuiltin<T # "v", T # "vPCe" # eew_type # "Uv", type>;
          if !not(IsFloat<type>.val) then {
            def : RVVBuiltin<T # "Uv", T # "UvPCUe" # eew_type # "Uv", type>;
          }
        }
      }
    }
  }
}

multiclass RVVIndexedSegStoreTuple<string op> {
  foreach type = TypeList in {
    foreach eew_info = EEWList in {
      defvar eew = eew_info[0];
      defvar eew_type = eew_info[1];
      foreach nf = NFList in {
        let Name = op # nf # "ei" # eew # "_v",
            IRName = op # nf,
            MaskedIRName = op # nf # "_mask",
            NF = nf,
            HasMaskedOffOperand = false,
            MaskedPolicyScheme = NonePolicy,
            RequiredFeatures = !if(!eq(type, "x"), ["Zvfhmin"],
                                   !if(!eq(type, "y"), ["Zvfbfmin"],
                                                   []<string>)),
            ManualCodegen = [{
    {
      // Masked
      // Builtin: (mask, ptr, index, v_tuple, vl)
      // Intrinsic: (val0, val1, ..., ptr, index, mask, vl)
      // Unmasked
      // Builtin: (ptr, index, v_tuple, vl)
      // Intrinsic: (val0, val1, ..., ptr, index, vl)
      unsigned Offset = IsMasked ? 1 : 0;
      llvm::Value *VTupleOperand = Ops[Offset + 2];

      SmallVector<llvm::Value*, 12> Operands;
      for (unsigned I = 0; I < NF; ++I) {
        llvm::Value *V = Builder.CreateExtractValue(VTupleOperand, {I});
        Operands.push_back(V);
      }
      Operands.push_back(Ops[Offset]); // Ptr
      Operands.push_back(Ops[Offset + 1]); // Idx
      if (IsMasked)
        Operands.push_back(Ops[0]);
      Operands.push_back(Ops[Offset + 3]); // VL

      IntrinsicTypes = {Operands[0]->getType(), Ops[Offset + 1]->getType(),
                        Operands.back()->getType()};
      llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);
      return Builder.CreateCall(F, Operands, "");
    }
            }] in {
          defvar T = "(Tuple:" # nf # ")";
          def : RVVBuiltin<T # "v", "0Pe" # eew_type # "Uv" # T # "v", type>;
          if !not(IsFloat<type>.val) then {
            def : RVVBuiltin<T # "Uv", "0PUe" # eew_type # "Uv" # T # "Uv", type>;
          }
        }
      }
    }
  }
}

// 7.8 Vector Load/Store Segment Instructions
let UnMaskedPolicyScheme = HasPassthruOperand,
    IsTuple = true in {
  defm : RVVUnitStridedSegLoadTuple<"vlseg">;
  defm : RVVUnitStridedSegLoadFFTuple<"vlseg">;
  defm : RVVStridedSegLoadTuple<"vlsseg">;
  defm : RVVIndexedSegLoadTuple<"vluxseg">;
  defm : RVVIndexedSegLoadTuple<"vloxseg">;
}

let UnMaskedPolicyScheme = NonePolicy,
    MaskedPolicyScheme = NonePolicy,
    IsTuple = true in {
defm : RVVUnitStridedSegStoreTuple<"vsseg">;
defm : RVVStridedSegStoreTuple<"vssseg">;
defm : RVVIndexedSegStoreTuple<"vsuxseg">;
defm : RVVIndexedSegStoreTuple<"vsoxseg">;
}

// 11. Vector Integer Arithmetic Instructions
// 11.1. Vector Single-Width Integer Add and Subtract
let UnMaskedPolicyScheme = HasPassthruOperand in {
defm vadd : RVVIntBinBuiltinSet;
defm vsub : RVVIntBinBuiltinSet;
defm vrsub : RVVOutOp1BuiltinSet<"vrsub", "csil",
                                 [["vx", "v", "vve"],
                                  ["vx", "Uv", "UvUvUe"]]>;
}
defm vneg_v : RVVPseudoUnaryBuiltin<"vrsub", "csil">;

// 11.2. Vector Widening Integer Add/Subtract
// Widening unsigned integer add/subtract, 2*SEW = SEW +/- SEW
let UnMaskedPolicyScheme = HasPassthruOperand in {
defm vwaddu : RVVUnsignedWidenBinBuiltinSet;
defm vwsubu : RVVUnsignedWidenBinBuiltinSet;
// Widening signed integer add/subtract, 2*SEW = SEW +/- SEW
defm vwadd : RVVSignedWidenBinBuiltinSet;
defm vwsub : RVVSignedWidenBinBuiltinSet;
// Widening unsigned integer add/subtract, 2*SEW = 2*SEW +/- SEW
defm vwaddu : RVVUnsignedWidenOp0BinBuiltinSet;
defm vwsubu : RVVUnsignedWidenOp0BinBuiltinSet;
// Widening signed integer add/subtract, 2*SEW = 2*SEW +/- SEW
defm vwadd : RVVSignedWidenOp0BinBuiltinSet;
defm vwsub : RVVSignedWidenOp0BinBuiltinSet;
}
defm vwcvtu_x_x_v : RVVPseudoVWCVTBuiltin<"vwaddu", "vwcvtu_x", "csi",
                                          [["Uw", "UwUv"]]>;
defm vwcvt_x_x_v : RVVPseudoVWCVTBuiltin<"vwadd", "vwcvt_x", "csi",
                                         [["w", "wv"]]>;

// 11.3. Vector Integer Extension
let UnMaskedPolicyScheme = HasPassthruOperand in {
let Log2LMUL = [-3, -2, -1, 0, 1, 2] in {
  def vsext_vf2 : RVVIntExt<"vsext", "w", "wv", "csi">;
  def vzext_vf2 : RVVIntExt<"vzext", "Uw", "UwUv", "csi">;
}
let Log2LMUL = [-3, -2, -1, 0, 1] in {
  def vsext_vf4 : RVVIntExt<"vsext", "q", "qv", "cs">;
  def vzext_vf4 : RVVIntExt<"vzext", "Uq", "UqUv", "cs">;
}
let Log2LMUL = [-3, -2, -1, 0] in {
  def vsext_vf8 : RVVIntExt<"vsext", "o", "ov", "c">;
  def vzext_vf8 : RVVIntExt<"vzext", "Uo", "UoUv", "c">;
}
}

// 11.4. Vector Integer Add-with-Carry / Subtract-with-Borrow Instructions
let HasMasked = false, MaskedPolicyScheme = NonePolicy in {
  let UnMaskedPolicyScheme = HasPassthruOperand in {
    defm vadc : RVVCarryinBuiltinSet;
    defm vsbc : RVVCarryinBuiltinSet;
  }
  defm vmadc : RVVCarryOutInBuiltinSet<"vmadc_carry_in">;
  defm vmadc : RVVIntMaskOutBuiltinSet;
  defm vmsbc : RVVCarryOutInBuiltinSet<"vmsbc_borrow_in">;
  defm vmsbc : RVVIntMaskOutBuiltinSet;
}

// 11.5. Vector Bitwise Logical Instructions
let UnMaskedPolicyScheme = HasPassthruOperand in {
defm vand : RVVIntBinBuiltinSet;
defm vxor : RVVIntBinBuiltinSet;
defm vor : RVVIntBinBuiltinSet;
}
defm vnot_v : RVVPseudoVNotBuiltin<"vxor", "csil">;

// 11.6. Vector Single-Width Shift Instructions
let UnMaskedPolicyScheme = HasPassthruOperand in {
defm vsll : RVVShiftBuiltinSet;
defm vsrl : RVVUnsignedShiftBuiltinSet;
defm vsra : RVVSignedShiftBuiltinSet;

// 11.7. Vector Narrowing Integer Right Shift Instructions
defm vnsrl : RVVUnsignedNShiftBuiltinSet;
defm vnsra : RVVSignedNShiftBuiltinSet;
}
defm vncvt_x_x_w : RVVPseudoVNCVTBuiltin<"vnsrl", "vncvt_x", "csi",
                                         [["v", "vw"],
                                          ["Uv", "UvUw"]]>;

// 11.8. Vector Integer Compare Instructions
let MaskedPolicyScheme = HasPassthruOperand,
    HasTailPolicy = false in {
defm vmseq : RVVIntMaskOutBuiltinSet;
defm vmsne : RVVIntMaskOutBuiltinSet;
defm vmsltu : RVVUnsignedMaskOutBuiltinSet;
defm vmslt : RVVSignedMaskOutBuiltinSet;
defm vmsleu : RVVUnsignedMaskOutBuiltinSet;
defm vmsle : RVVSignedMaskOutBuiltinSet;
defm vmsgtu : RVVUnsignedMaskOutBuiltinSet;
defm vmsgt : RVVSignedMaskOutBuiltinSet;
defm vmsgeu : RVVUnsignedMaskOutBuiltinSet;
defm vmsge : RVVSignedMaskOutBuiltinSet;
}

// 11.9. Vector Integer Min/Max Instructions
let UnMaskedPolicyScheme = HasPassthruOperand in {
defm vminu : RVVUnsignedBinBuiltinSet;
defm vmin : RVVSignedBinBuiltinSet;
defm vmaxu : RVVUnsignedBinBuiltinSet;
defm vmax : RVVSignedBinBuiltinSet;

// 11.10. Vector Single-Width Integer Multiply Instructions
defm vmul : RVVIntBinBuiltinSet;
defm vmulh : RVVSignedBinBuiltinSet;
defm vmulhu : RVVUnsignedBinBuiltinSet;
defm vmulhsu : RVVOutOp1BuiltinSet<"vmulhsu", "csil",
                                   [["vv", "v", "vvUv"],
                                    ["vx", "v", "vvUe"]]>;

// 11.11. Vector Integer Divide Instructions
defm vdivu : RVVUnsignedBinBuiltinSet;
defm vdiv : RVVSignedBinBuiltinSet;
defm vremu : RVVUnsignedBinBuiltinSet;
defm vrem : RVVSignedBinBuiltinSet;
}

// 11.12. Vector Widening Integer Multiply Instructions
let Log2LMUL = [-3, -2, -1, 0, 1, 2], UnMaskedPolicyScheme = HasPassthruOperand in {
defm vwmul : RVVOutOp0Op1BuiltinSet<"vwmul", "csi",
                                    [["vv", "w", "wvv"],
                                     ["vx", "w", "wve"]]>;
defm vwmulu : RVVOutOp0Op1BuiltinSet<"vwmulu", "csi",
                                     [["vv", "Uw", "UwUvUv"],
                                      ["vx", "Uw", "UwUvUe"]]>;
defm vwmulsu : RVVOutOp0Op1BuiltinSet<"vwmulsu", "csi",
                                      [["vv", "w", "wvUv"],
                                       ["vx", "w", "wvUe"]]>;
}

// 11.13. Vector Single-Width Integer Multiply-Add Instructions
let UnMaskedPolicyScheme = HasPolicyOperand in {
defm vmacc  : RVVIntTerBuiltinSet;
defm vnmsac : RVVIntTerBuiltinSet;
defm vmadd  : RVVIntTerBuiltinSet;
defm vnmsub : RVVIntTerBuiltinSet;

// 11.14. Vector Widening Integer Multiply-Add Instructions
let HasMaskedOffOperand = false,
    Log2LMUL = [-3, -2, -1, 0, 1, 2] in {
defm vwmaccu : RVVOutOp1Op2BuiltinSet<"vwmaccu", "csi",
                                      [["vv", "Uw", "UwUwUvUv"],
                                       ["vx", "Uw", "UwUwUeUv"]]>;
defm vwmacc : RVVOutOp1Op2BuiltinSet<"vwmacc", "csi",
                                     [["vv", "w", "wwvv"],
                                      ["vx", "w", "wwev"]]>;
defm vwmaccsu : RVVOutOp1Op2BuiltinSet<"vwmaccsu", "csi",
                                       [["vv", "w", "wwvUv"],
                                        ["vx", "w", "wweUv"]]>;
defm vwmaccus : RVVOutOp1Op2BuiltinSet<"vwmaccus", "csi",
                                       [["vx", "w", "wwUev"]]>;
}
}

// 11.15. Vector Integer Merge Instructions
// C/C++ Operand: (mask, op1, op2, vl), Intrinsic: (passthru, op1, op2, mask, vl)
let HasMasked = false,
    UnMaskedPolicyScheme = HasPassthruOperand,
    MaskedPolicyScheme = NonePolicy,
    ManualCodegen = [{
      // insert poison passthru
      if (PolicyAttrs & RVV_VTA)
        Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
      IntrinsicTypes = {ResultType, Ops[2]->getType(), Ops.back()->getType()};
    }] in {
  defm vmerge : RVVOutOp1BuiltinSet<"vmerge", "csil",
                                    [["vvm", "v", "vvvm"],
                                     ["vxm", "v", "vvem"],
                                     ["vvm", "Uv", "UvUvUvm"],
                                     ["vxm", "Uv", "UvUvUem"]]>;
}

// 11.16. Vector Integer Move Instructions
let HasMasked = false,
    UnMaskedPolicyScheme = HasPassthruOperand,
    MaskedPolicyScheme = NonePolicy,
    OverloadedName = "vmv_v" in {
    defm vmv_v : RVVOutBuiltinSet<"vmv_v_v", "csil",
                                   [["v", "Uv", "UvUv"]]>;
    defm vmv_v : RVVOutBuiltinSet<"vmv_v_v", "csilfd",
                                   [["v", "v", "vv"]]>;
    let RequiredFeatures = ["Zvfhmin"] in
      defm vmv_v : RVVOutBuiltinSet<"vmv_v_v", "x",
                                    [["v", "v", "vv"]]>;
  let SupportOverloading = false in
    defm vmv_v : RVVOutBuiltinSet<"vmv_v_x", "csil",
                                   [["x", "v", "ve"],
                                    ["x", "Uv", "UvUe"]]>;
}

// 12. Vector Fixed-Point Arithmetic Instructions
let HeaderCode =
[{
enum __RISCV_VXRM {
  __RISCV_VXRM_RNU = 0,
  __RISCV_VXRM_RNE = 1,
  __RISCV_VXRM_RDN = 2,
  __RISCV_VXRM_ROD = 3,
};
}] in
def vxrm_enum : RVVHeader;

// 12.1. Vector Single-Width Saturating Add and Subtract
let UnMaskedPolicyScheme = HasPassthruOperand in {
defm vsaddu : RVVUnsignedBinBuiltinSet;
defm vsadd : RVVSignedBinBuiltinSet;
defm vssubu : RVVUnsignedBinBuiltinSet;
defm vssub : RVVSignedBinBuiltinSet;

let ManualCodegen = [{
  {
    // LLVM intrinsic
    // Unmasked: (passthru, op0, op1, round_mode, vl)
    // Masked:   (passthru, vector_in, vector_in/scalar_in, mask, vxrm, vl, policy)

    SmallVector<llvm::Value*, 7> Operands;
    bool HasMaskedOff = !(
        (IsMasked && (PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA)) ||
        (!IsMasked && PolicyAttrs & RVV_VTA));
    unsigned Offset = IsMasked ?
        (HasMaskedOff ? 2 : 1) : (HasMaskedOff ? 1 : 0);

    if (!HasMaskedOff)
      Operands.push_back(llvm::PoisonValue::get(ResultType));
    else
      Operands.push_back(Ops[IsMasked ? 1 : 0]);

    Operands.push_back(Ops[Offset]); // op0
    Operands.push_back(Ops[Offset + 1]); // op1

    if (IsMasked)
      Operands.push_back(Ops[0]); // mask

    Operands.push_back(Ops[Offset + 2]); // vxrm
    Operands.push_back(Ops[Offset + 3]); // vl

    if (IsMasked)
      Operands.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));

    IntrinsicTypes = {ResultType, Ops[Offset + 1]->getType(), Ops.back()->getType()};
    llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);
    return Builder.CreateCall(F, Operands, "");
  }
}] in {
  // 12.2. Vector Single-Width Averaging Add and Subtract
  defm vaaddu : RVVUnsignedBinBuiltinSetRoundingMode;
  defm vaadd : RVVSignedBinBuiltinSetRoundingMode;
  defm vasubu : RVVUnsignedBinBuiltinSetRoundingMode;
  defm vasub : RVVSignedBinBuiltinSetRoundingMode;

  // 12.3. Vector Single-Width Fractional Multiply with Rounding and Saturation
  defm vsmul : RVVSignedBinBuiltinSetRoundingMode;

  // 12.4. Vector Single-Width Scaling Shift Instructions
  defm vssrl : RVVUnsignedShiftBuiltinSetRoundingMode;
  defm vssra : RVVSignedShiftBuiltinSetRoundingMode;
}

let ManualCodegen = [{
  {
    // LLVM intrinsic
    // Unmasked: (passthru, op0, op1, round_mode, vl)
    // Masked:   (passthru, vector_in, vector_in/scalar_in, mask, vxrm, vl, policy)

    SmallVector<llvm::Value*, 7> Operands;
    bool HasMaskedOff = !(
        (IsMasked && (PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA)) ||
        (!IsMasked && PolicyAttrs & RVV_VTA));
    unsigned Offset = IsMasked ?
        (HasMaskedOff ? 2 : 1) : (HasMaskedOff ? 1 : 0);

    if (!HasMaskedOff)
      Operands.push_back(llvm::PoisonValue::get(ResultType));
    else
      Operands.push_back(Ops[IsMasked ? 1 : 0]);

    Operands.push_back(Ops[Offset]); // op0
    Operands.push_back(Ops[Offset + 1]); // op1

    if (IsMasked)
      Operands.push_back(Ops[0]); // mask

    Operands.push_back(Ops[Offset + 2]); // vxrm
    Operands.push_back(Ops[Offset + 3]); // vl

    if (IsMasked)
      Operands.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));

    IntrinsicTypes = {ResultType, Ops[Offset]->getType(), Ops[Offset + 1]->getType(),
                      Ops.back()->getType()};
    llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);
    return Builder.CreateCall(F, Operands, "");
  }
}] in {
  // 12.5. Vector Narrowing Fixed-Point Clip Instructions
  defm vnclipu : RVVUnsignedNShiftBuiltinSetRoundingMode;
  defm vnclip : RVVSignedNShiftBuiltinSetRoundingMode;
}
}

// 13. Vector Floating-Point Instructions
let HeaderCode =
[{
enum __RISCV_FRM {
  __RISCV_FRM_RNE = 0,
  __RISCV_FRM_RTZ = 1,
  __RISCV_FRM_RDN = 2,
  __RISCV_FRM_RUP = 3,
  __RISCV_FRM_RMM = 4,
};
}] in def frm_enum : RVVHeader;

let UnMaskedPolicyScheme = HasPassthruOperand in {
let ManualCodegen = [{
  {
    // LLVM intrinsic
    // Unmasked: (passthru, op0, op1, round_mode, vl)
    // Masked:   (passthru, vector_in, vector_in/scalar_in, mask, frm, vl, policy)

    SmallVector<llvm::Value*, 7> Operands;
    bool HasMaskedOff = !(
        (IsMasked && (PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA)) ||
        (!IsMasked && PolicyAttrs & RVV_VTA));
    bool HasRoundModeOp = IsMasked ?
      (HasMaskedOff ? Ops.size() == 6 : Ops.size() == 5) :
      (HasMaskedOff ? Ops.size() == 5 : Ops.size() == 4);

    unsigned Offset = IsMasked ?
        (HasMaskedOff ? 2 : 1) : (HasMaskedOff ? 1 : 0);

    if (!HasMaskedOff)
      Operands.push_back(llvm::PoisonValue::get(ResultType));
    else
      Operands.push_back(Ops[IsMasked ? 1 : 0]);

    Operands.push_back(Ops[Offset]); // op0
    Operands.push_back(Ops[Offset + 1]); // op1

    if (IsMasked)
      Operands.push_back(Ops[0]); // mask

    if (HasRoundModeOp) {
      Operands.push_back(Ops[Offset + 2]); // frm
      Operands.push_back(Ops[Offset + 3]); // vl
    } else {
      Operands.push_back(ConstantInt::get(Ops[Offset + 2]->getType(), 7)); // frm
      Operands.push_back(Ops[Offset + 2]); // vl
    }

    if (IsMasked)
      Operands.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));

    IntrinsicTypes = {ResultType, Ops[Offset + 1]->getType(),
                      Operands.back()->getType()};
    llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);
    return Builder.CreateCall(F, Operands, "");
  }
}] in {
  let HasFRMRoundModeOp = true in {
    // 13.2. Vector Single-Width Floating-Point Add/Subtract Instructions
    defm vfadd  : RVVFloatingBinBuiltinSetRoundingMode;
    defm vfsub  : RVVFloatingBinBuiltinSetRoundingMode;
    defm vfrsub : RVVFloatingBinVFBuiltinSetRoundingMode;

    // 13.3. Vector Widening Floating-Point Add/Subtract Instructions
    // Widening FP add/subtract, 2*SEW = 2*SEW +/- SEW
    defm vfwadd : RVVFloatingWidenOp0BinBuiltinSetRoundingMode;
    defm vfwsub : RVVFloatingWidenOp0BinBuiltinSetRoundingMode;

    // 13.4. Vector Single-Width Floating-Point Multiply/Divide Instructions
    defm vfmul  : RVVFloatingBinBuiltinSetRoundingMode;
    defm vfdiv  : RVVFloatingBinBuiltinSetRoundingMode;
    defm vfrdiv : RVVFloatingBinVFBuiltinSetRoundingMode;
  }
  // 13.2. Vector Single-Width Floating-Point Add/Subtract Instructions
  defm vfadd  : RVVFloatingBinBuiltinSet;
  defm vfsub  : RVVFloatingBinBuiltinSet;
  defm vfrsub : RVVFloatingBinVFBuiltinSet;

  // 13.3. Vector Widening Floating-Point Add/Subtract Instructions
  // Widening FP add/subtract, 2*SEW = 2*SEW +/- SEW
  defm vfwadd : RVVFloatingWidenOp0BinBuiltinSet;
  defm vfwsub : RVVFloatingWidenOp0BinBuiltinSet;

  // 13.4. Vector Single-Width Floating-Point Multiply/Divide Instructions
  defm vfmul  : RVVFloatingBinBuiltinSet;
  defm vfdiv  : RVVFloatingBinBuiltinSet;
  defm vfrdiv : RVVFloatingBinVFBuiltinSet;
}

let ManualCodegen = [{
  {
    // LLVM intrinsic
    // Unmasked: (passthru, op0, op1, round_mode, vl)
    // Masked:   (passthru, vector_in, vector_in/scalar_in, mask, frm, vl, policy)

    SmallVector<llvm::Value*, 7> Operands;
    bool HasMaskedOff = !(
        (IsMasked && (PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA)) ||
        (!IsMasked && PolicyAttrs & RVV_VTA));
    bool HasRoundModeOp = IsMasked ?
      (HasMaskedOff ? Ops.size() == 6 : Ops.size() == 5) :
      (HasMaskedOff ? Ops.size() == 5 : Ops.size() == 4);

    unsigned Offset = IsMasked ?
        (HasMaskedOff ? 2 : 1) : (HasMaskedOff ? 1 : 0);

    if (!HasMaskedOff)
      Operands.push_back(llvm::PoisonValue::get(ResultType));
    else
      Operands.push_back(Ops[IsMasked ? 1 : 0]);

    Operands.push_back(Ops[Offset]); // op0
    Operands.push_back(Ops[Offset + 1]); // op1

    if (IsMasked)
      Operands.push_back(Ops[0]); // mask

    if (HasRoundModeOp) {
      Operands.push_back(Ops[Offset + 2]); // frm
      Operands.push_back(Ops[Offset + 3]); // vl
    } else {
      Operands.push_back(ConstantInt::get(Ops[Offset + 2]->getType(), 7)); // frm
      Operands.push_back(Ops[Offset + 2]); // vl
    }

    if (IsMasked)
      Operands.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));

    IntrinsicTypes = {ResultType, Ops[Offset]->getType(), Ops[Offset + 1]->getType(),
                      Ops.back()->getType()};
    llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);
    return Builder.CreateCall(F, Operands, "");
  }
}] in {
  let HasFRMRoundModeOp = true in {
    // 13.3. Vector Widening Floating-Point Add/Subtract Instructions
    // Widening FP add/subtract, 2*SEW = SEW +/- SEW
    defm vfwadd : RVVFloatingWidenBinBuiltinSetRoundingMode;
    defm vfwsub : RVVFloatingWidenBinBuiltinSetRoundingMode;

    // 13.5. Vector Widening Floating-Point Multiply
    let Log2LMUL = [-2, -1, 0, 1, 2] in {
      defm vfwmul : RVVOutOp0Op1BuiltinSet<"vfwmul", "xf",
                                          [["vv", "w", "wvvu"],
                                            ["vf", "w", "wveu"]]>;
    }
  }
  // 13.3. Vector Widening Floating-Point Add/Subtract Instructions
  // Widening FP add/subtract, 2*SEW = SEW +/- SEW
  defm vfwadd : RVVFloatingWidenBinBuiltinSet;
  defm vfwsub : RVVFloatingWidenBinBuiltinSet;

  // 13.5. Vector Widening Floating-Point Multiply
  let Log2LMUL = [-2, -1, 0, 1, 2] in {
    defm vfwmul : RVVOutOp0Op1BuiltinSet<"vfwmul", "xf",
                                        [["vv", "w", "wvv"],
                                          ["vf", "w", "wve"]]>;
  }
}
}


let UnMaskedPolicyScheme = HasPolicyOperand in {
let ManualCodegen = [{
  {
    // LLVM intrinsic
    // Unmasked: (passthru, op0, op1, round_mode, vl)
    // Masked:   (passthru, vector_in, vector_in/scalar_in, mask, frm, vl, policy)

    SmallVector<llvm::Value*, 7> Operands;
    bool HasRoundModeOp = IsMasked ? Ops.size() == 6 : Ops.size() == 5;

    unsigned Offset = IsMasked ? 2 : 1;

    Operands.push_back(Ops[IsMasked ? 1 : 0]); // passthrough

    Operands.push_back(Ops[Offset]); // op0
    Operands.push_back(Ops[Offset + 1]); // op1

    if (IsMasked)
      Operands.push_back(Ops[0]); // mask

    if (HasRoundModeOp) {
      Operands.push_back(Ops[Offset + 2]); // frm
      Operands.push_back(Ops[Offset + 3]); // vl
    } else {
      Operands.push_back(ConstantInt::get(Ops[Offset + 2]->getType(), 7)); // frm
      Operands.push_back(Ops[Offset + 2]); // vl
    }

    Operands.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));

    IntrinsicTypes = {ResultType, Ops[Offset]->getType(),
                      Operands.back()->getType()};

    llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);

    return Builder.CreateCall(F, Operands, "");
  }
}] in {
  let HasFRMRoundModeOp = 1 in {
    // 13.6. Vector Single-Width Floating-Point Fused Multiply-Add Instructions
    defm vfmacc  : RVVFloatingTerBuiltinSetRoundingMode;
    defm vfnmacc : RVVFloatingTerBuiltinSetRoundingMode;
    defm vfmsac  : RVVFloatingTerBuiltinSetRoundingMode;
    defm vfnmsac : RVVFloatingTerBuiltinSetRoundingMode;
    defm vfmadd  : RVVFloatingTerBuiltinSetRoundingMode;
    defm vfnmadd : RVVFloatingTerBuiltinSetRoundingMode;
    defm vfmsub  : RVVFloatingTerBuiltinSetRoundingMode;
    defm vfnmsub : RVVFloatingTerBuiltinSetRoundingMode;
  }
  // 13.6. Vector Single-Width Floating-Point Fused Multiply-Add Instructions
  defm vfmacc  : RVVFloatingTerBuiltinSet;
  defm vfnmacc : RVVFloatingTerBuiltinSet;
  defm vfmsac  : RVVFloatingTerBuiltinSet;
  defm vfnmsac : RVVFloatingTerBuiltinSet;
  defm vfmadd  : RVVFloatingTerBuiltinSet;
  defm vfnmadd : RVVFloatingTerBuiltinSet;
  defm vfmsub  : RVVFloatingTerBuiltinSet;
  defm vfnmsub : RVVFloatingTerBuiltinSet;
}

let ManualCodegen = [{
  {
    // LLVM intrinsic
    // Unmasked: (passthru, op0, op1, round_mode, vl)
    // Masked:   (passthru, vector_in, vector_in/scalar_in, mask, frm, vl, policy)

    SmallVector<llvm::Value*, 7> Operands;
    bool HasRoundModeOp = IsMasked ? Ops.size() == 6 : Ops.size() == 5;

    unsigned Offset = IsMasked ? 2 : 1;

    Operands.push_back(Ops[IsMasked ? 1 : 0]); // passthrough

    Operands.push_back(Ops[Offset]); // op0
    Operands.push_back(Ops[Offset + 1]); // op1

    if (IsMasked)
      Operands.push_back(Ops[0]); // mask

    if (HasRoundModeOp) {
      Operands.push_back(Ops[Offset + 2]); // frm
      Operands.push_back(Ops[Offset + 3]); // vl
    } else {
      Operands.push_back(ConstantInt::get(Ops[Offset + 2]->getType(), 7)); // frm
      Operands.push_back(Ops[Offset + 2]); // vl
    }

    Operands.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));

    IntrinsicTypes = {ResultType, Ops[Offset]->getType(), Ops[Offset + 1]->getType(),
                      Operands.back()->getType()};

    llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);

    return Builder.CreateCall(F, Operands, "");
  }
}] in {
  let HasFRMRoundModeOp = 1 in {
    // 13.7. Vector Widening Floating-Point Fused Multiply-Add Instructions
    defm vfwmacc  : RVVFloatingWidenTerBuiltinSetRoundingMode;
    defm vfwnmacc : RVVFloatingWidenTerBuiltinSetRoundingMode;
    defm vfwmsac  : RVVFloatingWidenTerBuiltinSetRoundingMode;
    defm vfwnmsac : RVVFloatingWidenTerBuiltinSetRoundingMode;

    // Vector BF16 widening multiply-accumulate
    let Log2LMUL = [-2, -1, 0, 1, 2],
        RequiredFeatures = ["Zvfbfwma"],
        HasMaskedOffOperand = false in
    defm vfwmaccbf16 : RVVOutOp1Op2BuiltinSet<"vfwmaccbf16", "y",
                                              [["vv", "Fw", "FwFwvvu"],
                                               ["vf", "Fw", "FwFwevu"]]>;
  }
  // 13.7. Vector Widening Floating-Point Fused Multiply-Add Instructions
  defm vfwmacc  : RVVFloatingWidenTerBuiltinSet;
  defm vfwnmacc : RVVFloatingWidenTerBuiltinSet;
  defm vfwmsac  : RVVFloatingWidenTerBuiltinSet;
  defm vfwnmsac : RVVFloatingWidenTerBuiltinSet;

  // Vector BF16 widening multiply-accumulate
  let Log2LMUL = [-2, -1, 0, 1, 2],
      RequiredFeatures = ["Zvfbfwma"],
      HasMaskedOffOperand = false in
  defm vfwmaccbf16 : RVVOutOp1Op2BuiltinSet<"vfwmaccbf16", "y",
                                            [["vv", "Fw", "FwFwvv"],
                                             ["vf", "Fw", "FwFwev"]]>;
}

}

let UnMaskedPolicyScheme = HasPassthruOperand in {
let ManualCodegen = [{
  {
    // LLVM intrinsic
    // Unmasked: (passthru, op0, round_mode, vl)
    // Masked:   (passthru, op0, mask, frm, vl, policy)

    SmallVector<llvm::Value*, 7> Operands;
    bool HasMaskedOff = !(
        (IsMasked && (PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA)) ||
        (!IsMasked && PolicyAttrs & RVV_VTA));
    bool HasRoundModeOp = IsMasked ?
      (HasMaskedOff ? Ops.size() == 5 : Ops.size() == 4) :
      (HasMaskedOff ? Ops.size() == 4 : Ops.size() == 3);

    unsigned Offset = IsMasked ?
        (HasMaskedOff ? 2 : 1) : (HasMaskedOff ? 1 : 0);

    if (!HasMaskedOff)
      Operands.push_back(llvm::PoisonValue::get(ResultType));
    else
      Operands.push_back(Ops[IsMasked ? 1 : 0]);

    Operands.push_back(Ops[Offset]); // op0

    if (IsMasked)
      Operands.push_back(Ops[0]); // mask

    if (HasRoundModeOp) {
      Operands.push_back(Ops[Offset + 1]); // frm
      Operands.push_back(Ops[Offset + 2]); // vl
    } else {
      Operands.push_back(ConstantInt::get(Ops[Offset + 1]->getType(), 7)); // frm
      Operands.push_back(Ops[Offset + 1]); // vl
    }

    if (IsMasked)
      Operands.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));

    IntrinsicTypes = {ResultType, Operands.back()->getType()};
    llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);
    return Builder.CreateCall(F, Operands, "");
  }
}] in {
  let HasFRMRoundModeOp = 1 in {
    // 13.8. Vector Floating-Point Square-Root Instruction
    defm vfsqrt : RVVOutBuiltinSet<"vfsqrt", "xfd", [["v", "v", "vvu"]]>;

    // 13.10. Vector Floating-Point Reciprocal Estimate Instruction
    defm vfrec7 : RVVOutBuiltinSet<"vfrec7", "xfd", [["v", "v", "vvu"]]>;
  }
  // 13.8. Vector Floating-Point Square-Root Instruction
  defm vfsqrt : RVVOutBuiltinSet<"vfsqrt", "xfd", [["v", "v", "vv"]]>;

  // 13.10. Vector Floating-Point Reciprocal Estimate Instruction
  defm vfrec7 : RVVOutBuiltinSet<"vfrec7", "xfd", [["v", "v", "vv"]]>;
}

// 13.9. Vector Floating-Point Reciprocal Square-Root Estimate Instruction
def vfrsqrt7 : RVVFloatingUnaryVVBuiltin;

// 13.11. Vector Floating-Point MIN/MAX Instructions
defm vfmin : RVVFloatingBinBuiltinSet;
defm vfmax : RVVFloatingBinBuiltinSet;

// 13.12. Vector Floating-Point Sign-Injection Instructions
defm vfsgnj  : RVVFloatingBinBuiltinSet;
defm vfsgnjn : RVVFloatingBinBuiltinSet;
defm vfsgnjx : RVVFloatingBinBuiltinSet;
}
defm vfneg_v : RVVPseudoVFUnaryBuiltin<"vfsgnjn", "xfd">;
defm vfabs_v : RVVPseudoVFUnaryBuiltin<"vfsgnjx", "xfd">;

// 13.13. Vector Floating-Point Compare Instructions
let MaskedPolicyScheme = HasPassthruOperand,
    HasTailPolicy = false in {
defm vmfeq : RVVFloatingMaskOutBuiltinSet;
defm vmfne : RVVFloatingMaskOutBuiltinSet;
defm vmflt : RVVFloatingMaskOutBuiltinSet;
defm vmfle : RVVFloatingMaskOutBuiltinSet;
defm vmfgt : RVVFloatingMaskOutBuiltinSet;
defm vmfge : RVVFloatingMaskOutBuiltinSet;
}

// 13.14. Vector Floating-Point Classify Instruction
let Name = "vfclass_v", UnMaskedPolicyScheme = HasPassthruOperand in
  def vfclass : RVVOp0Builtin<"Uv", "Uvv", "xfd">;

// 13.15. Vector Floating-Point Merge Instruction
// C/C++ Operand: (mask, op1, op2, vl), Builtin: (op1, op2, mask, vl)
let HasMasked = false,
    UnMaskedPolicyScheme = HasPassthruOperand,
    MaskedPolicyScheme = NonePolicy,
    ManualCodegen = [{
      // insert poison passthru
      if (PolicyAttrs & RVV_VTA)
        Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
      IntrinsicTypes = {ResultType, Ops[2]->getType(), Ops.back()->getType()};
    }] in {
  defm vmerge : RVVOutOp1BuiltinSet<"vmerge", "fd",
                                    [["vvm", "v", "vvvm"]]>;
  let RequiredFeatures = ["Zvfhmin"] in
    defm vmerge : RVVOutOp1BuiltinSet<"vmerge", "x",
                                      [["vvm", "v", "vvvm"]]>;
  defm vfmerge : RVVOutOp1BuiltinSet<"vfmerge", "xfd",
                                     [["vfm", "v", "vvem"]]>;
}

// 13.16. Vector Floating-Point Move Instruction
let HasMasked = false,
    UnMaskedPolicyScheme = HasPassthruOperand,
    SupportOverloading = false,
    MaskedPolicyScheme = NonePolicy,
    OverloadedName = "vfmv_v" in
  defm vfmv_v : RVVOutBuiltinSet<"vfmv_v_f", "xfd",
                                  [["f", "v", "ve"]]>;

// 13.17. Single-Width Floating-Point/Integer Type-Convert Instructions
let UnMaskedPolicyScheme = HasPassthruOperand in {
def vfcvt_rtz_xu_f_v : RVVConvToUnsignedBuiltin<"vfcvt_rtz_xu">;
def vfcvt_rtz_x_f_v : RVVConvToSignedBuiltin<"vfcvt_rtz_x">;

// 13.18. Widening Floating-Point/Integer Type-Convert Instructions
let Log2LMUL = [-3, -2, -1, 0, 1, 2] in {
  def vfwcvt_rtz_xu_f_v : RVVConvToWidenUnsignedBuiltin<"vfwcvt_rtz_xu">;
  def vfwcvt_rtz_x_f_v : RVVConvToWidenSignedBuiltin<"vfwcvt_rtz_x">;
  def vfwcvt_f_xu_v : RVVConvBuiltin<"Fw", "FwUv", "csi", "vfwcvt_f">;
  def vfwcvt_f_x_v : RVVConvBuiltin<"Fw", "Fwv", "csi", "vfwcvt_f">;
  def vfwcvt_f_f_v : RVVConvBuiltin<"w", "wv", "f", "vfwcvt_f">;
  let RequiredFeatures = ["Zvfhmin"] in
    def vfwcvt_f_f_v_fp16 : RVVConvBuiltin<"w", "wv", "x", "vfwcvt_f"> {
      let Name = "vfwcvt_f_f_v";
      let IRName = "vfwcvt_f_f_v";
      let MaskedIRName = "vfwcvt_f_f_v_mask";
    }
}

// 13.19. Narrowing Floating-Point/Integer Type-Convert Instructions
let Log2LMUL = [-3, -2, -1, 0, 1, 2] in {
  def vfncvt_rtz_xu_f_w : RVVConvToNarrowingUnsignedBuiltin<"vfncvt_rtz_xu">;
  def vfncvt_rtz_x_f_w : RVVConvToNarrowingSignedBuiltin<"vfncvt_rtz_x">;
  def vfncvt_rod_f_f_w : RVVConvBuiltin<"v", "vw", "xf", "vfncvt_rod_f">;
}

// Zvfbfmin - Vector convert BF16 to FP32
let Log2LMUL = [-2, -1, 0, 1, 2] in
def vfwcvtbf16_f_f_v : RVVConvBuiltin<"Fw", "Fwv", "y", "vfwcvtbf16_f">;

let ManualCodegen = [{
  {
    // LLVM intrinsic
    // Unmasked: (passthru, op0, frm, vl)
    // Masked:   (passthru, op0, mask, frm, vl, policy)
    SmallVector<llvm::Value*, 7> Operands;
    bool HasMaskedOff = !(
        (IsMasked && (PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA)) ||
        (!IsMasked && PolicyAttrs & RVV_VTA));
    bool HasRoundModeOp = IsMasked ?
      (HasMaskedOff ? Ops.size() == 5 : Ops.size() == 4) :
      (HasMaskedOff ? Ops.size() == 4 : Ops.size() == 3);

    unsigned Offset = IsMasked ?
        (HasMaskedOff ? 2 : 1) : (HasMaskedOff ? 1 : 0);

    if (!HasMaskedOff)
      Operands.push_back(llvm::PoisonValue::get(ResultType));
    else
      Operands.push_back(Ops[IsMasked ? 1 : 0]);

    Operands.push_back(Ops[Offset]); // op0

    if (IsMasked)
      Operands.push_back(Ops[0]); // mask

    if (HasRoundModeOp) {
      Operands.push_back(Ops[Offset + 1]); // frm
      Operands.push_back(Ops[Offset + 2]); // vl
    } else {
      Operands.push_back(ConstantInt::get(Ops[Offset + 1]->getType(), 7)); // frm
      Operands.push_back(Ops[Offset + 1]); // vl
    }

    if (IsMasked)
      Operands.push_back(ConstantInt::get(Ops.back()->getType(), PolicyAttrs));

    IntrinsicTypes = {ResultType, Ops[Offset]->getType(),
                      Operands.back()->getType()};
    llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);
    return Builder.CreateCall(F, Operands, "");
  }
}] in {
  let HasFRMRoundModeOp = 1 in {
    // 14.17. Single-Width Floating-Point/Integer Type-Convert Instructions
    let OverloadedName = "vfcvt_x" in
      defm :
        RVVConvBuiltinSet<"vfcvt_x_f_v", "xfd", [["Iv", "Ivvu"]]>;
    let OverloadedName = "vfcvt_xu" in
      defm :
        RVVConvBuiltinSet<"vfcvt_xu_f_v", "xfd", [["Uv", "Uvvu"]]>;
    let OverloadedName = "vfcvt_f" in {
      defm :
        RVVConvBuiltinSet<"vfcvt_f_x_v", "sil", [["Fv", "Fvvu"]]>;
      defm :
        RVVConvBuiltinSet<"vfcvt_f_xu_v", "sil", [["Fv", "FvUvu"]]>;
    }

    // 13.18. Widening Floating-Point/Integer Type-Convert Instructions
    let Log2LMUL = [-3, -2, -1, 0, 1, 2] in {
      let OverloadedName = "vfwcvt_x" in
        defm :
          RVVConvBuiltinSet<"vfwcvt_x_f_v", "xf", [["Iw", "Iwvu"]]>;
      let OverloadedName = "vfwcvt_xu" in
        defm :
          RVVConvBuiltinSet<"vfwcvt_xu_f_v", "xf", [["Uw", "Uwvu"]]>;
    }
    // 13.19. Narrowing Floating-Point/Integer Type-Convert Instructions
    let Log2LMUL = [-3, -2, -1, 0, 1, 2] in {
      let OverloadedName = "vfncvt_x" in
        defm :
          RVVConvBuiltinSet<"vfncvt_x_f_w", "csi", [["Iv", "IvFwu"]]>;
      let OverloadedName = "vfncvt_xu" in
        defm :
          RVVConvBuiltinSet<"vfncvt_xu_f_w", "csi", [["Uv", "UvFwu"]]>;
      let OverloadedName = "vfncvt_f" in {
        defm :
          RVVConvBuiltinSet<"vfncvt_f_x_w", "csi", [["Fv", "Fvwu"]]>;
        defm :
          RVVConvBuiltinSet<"vfncvt_f_xu_w", "csi", [["Fv", "FvUwu"]]>;
      }
      let OverloadedName = "vfncvt_f" in {
        defm : RVVConvBuiltinSet<"vfncvt_f_f_w", "f", [["v", "vwu"]]>;
        let RequiredFeatures = ["Zvfhmin"] in
        defm : RVVConvBuiltinSet<"vfncvt_f_f_w", "x", [["v", "vwu"]]>;
      }
    }

    // Zvfbfmin - Vector convert FP32 to BF16
    let Log2LMUL = [-2, -1, 0, 1, 2],
        OverloadedName = "vfncvtbf16_f" in
    defm : RVVConvBuiltinSet<"vfncvtbf16_f_f_w", "y", [["v", "vFwu"]]>;
  }

  // 13.17. Single-Width Floating-Point/Integer Type-Convert Instructions
  let OverloadedName = "vfcvt_x" in
    defm :
      RVVConvBuiltinSet<"vfcvt_x_f_v", "xfd", [["Iv", "Ivv"]]>;
  let OverloadedName = "vfcvt_xu" in
    defm :
      RVVConvBuiltinSet<"vfcvt_xu_f_v", "xfd", [["Uv", "Uvv"]]>;
  let OverloadedName = "vfcvt_f" in {
    defm :
      RVVConvBuiltinSet<"vfcvt_f_x_v", "sil", [["Fv", "Fvv"]]>;
    defm :
      RVVConvBuiltinSet<"vfcvt_f_xu_v", "sil", [["Fv", "FvUv"]]>;
  }

  // 13.18. Widening Floating-Point/Integer Type-Convert Instructions
  let Log2LMUL = [-3, -2, -1, 0, 1, 2] in {
    let OverloadedName = "vfwcvt_x" in
      defm :
        RVVConvBuiltinSet<"vfwcvt_x_f_v", "xf", [["Iw", "Iwv"]]>;
    let OverloadedName = "vfwcvt_xu" in
      defm :
        RVVConvBuiltinSet<"vfwcvt_xu_f_v", "xf", [["Uw", "Uwv"]]>;
  }
  // 13.19. Narrowing Floating-Point/Integer Type-Convert Instructions
  let Log2LMUL = [-3, -2, -1, 0, 1, 2] in {
    let OverloadedName = "vfncvt_x" in
      defm :
        RVVConvBuiltinSet<"vfncvt_x_f_w", "csi", [["Iv", "IvFw"]]>;
    let OverloadedName = "vfncvt_xu" in
      defm :
        RVVConvBuiltinSet<"vfncvt_xu_f_w", "csi", [["Uv", "UvFw"]]>;
    let OverloadedName = "vfncvt_f" in {
      defm :
        RVVConvBuiltinSet<"vfncvt_f_x_w", "csi", [["Fv", "Fvw"]]>;
      defm :
        RVVConvBuiltinSet<"vfncvt_f_xu_w", "csi", [["Fv", "FvUw"]]>;
    }
    let OverloadedName = "vfncvt_f" in {
      defm : RVVConvBuiltinSet<"vfncvt_f_f_w", "f", [["v", "vw"]]>;
      let RequiredFeatures = ["Zvfhmin"] in
      defm : RVVConvBuiltinSet<"vfncvt_f_f_w", "x", [["v", "vw"]]>;
    }
  }

  // Zvfbfmin - Vector convert FP32 to BF16
  let Log2LMUL = [-2, -1, 0, 1, 2],
      OverloadedName = "vfncvtbf16_f" in
  defm : RVVConvBuiltinSet<"vfncvtbf16_f_f_w", "y", [["v", "vFw"]]>;
}
}

// 14. Vector Reduction Operations
// 14.1. Vector Single-Width Integer Reduction Instructions
let UnMaskedPolicyScheme = HasPassthruOperand,
    MaskedPolicyScheme = HasPassthruOperand,
    HasMaskPolicy = false in {
defm vredsum : RVVIntReductionBuiltinSet;
defm vredmaxu : RVVUnsignedReductionBuiltin;
defm vredmax : RVVSignedReductionBuiltin;
defm vredminu : RVVUnsignedReductionBuiltin;
defm vredmin : RVVSignedReductionBuiltin;
defm vredand : RVVIntReductionBuiltinSet;
defm vredor : RVVIntReductionBuiltinSet;
defm vredxor : RVVIntReductionBuiltinSet;

// 14.2. Vector Widening Integer Reduction Instructions
// Vector Widening Integer Reduction Operations
let HasMaskedOffOperand = true in {
  defm vwredsum : RVVOutOp0BuiltinSet<"vwredsum", "csi",
                                      [["vs", "vSw", "SwvSw"]]>;
  defm vwredsumu : RVVOutOp0BuiltinSet<"vwredsumu", "csi",
                                       [["vs", "UvUSw", "USwUvUSw"]]>;
}

// 14.3. Vector Single-Width Floating-Point Reduction Instructions
defm vfredmax : RVVFloatingReductionBuiltin;
defm vfredmin : RVVFloatingReductionBuiltin;
let ManualCodegen = [{
  {
    // LLVM intrinsic
    // Unmasked: (passthru, op0, op1, round_mode, vl)
    // Masked:   (passthru, vector_in, vector_in/scalar_in, mask, frm, vl, policy)

    SmallVector<llvm::Value*, 7> Operands;
    bool HasMaskedOff = !(
        (IsMasked && (PolicyAttrs & RVV_VTA) && (PolicyAttrs & RVV_VMA)) ||
        (!IsMasked && PolicyAttrs & RVV_VTA));
    bool HasRoundModeOp = IsMasked ?
      (HasMaskedOff ? Ops.size() == 6 : Ops.size() == 5) :
      (HasMaskedOff ? Ops.size() == 5 : Ops.size() == 4);

    unsigned Offset = IsMasked ?
        (HasMaskedOff ? 2 : 1) : (HasMaskedOff ? 1 : 0);

    if (!HasMaskedOff)
      Operands.push_back(llvm::PoisonValue::get(ResultType));
    else
      Operands.push_back(Ops[IsMasked ? 1 : 0]);

    Operands.push_back(Ops[Offset]); // op0
    Operands.push_back(Ops[Offset + 1]); // op1

    if (IsMasked)
      Operands.push_back(Ops[0]); // mask

    if (HasRoundModeOp) {
      Operands.push_back(Ops[Offset + 2]); // frm
      Operands.push_back(Ops[Offset + 3]); // vl
    } else {
      Operands.push_back(ConstantInt::get(Ops[Offset + 2]->getType(), 7)); // frm
      Operands.push_back(Ops[Offset + 2]); // vl
    }

    IntrinsicTypes = {ResultType, Ops[Offset]->getType(),
                      Ops.back()->getType()};
    llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);
    return Builder.CreateCall(F, Operands, "");
  }
}] in {
  let HasFRMRoundModeOp = 1 in {
    // 14.3. Vector Single-Width Floating-Point Reduction Instructions
    defm vfredusum : RVVFloatingReductionBuiltinRoundingMode;
    defm vfredosum : RVVFloatingReductionBuiltinRoundingMode;

    // 14.4. Vector Widening Floating-Point Reduction Instructions
    defm vfwredusum : RVVFloatingWidenReductionBuiltinRoundingMode;
    defm vfwredosum : RVVFloatingWidenReductionBuiltinRoundingMode;
  }
  // 14.3. Vector Single-Width Floating-Point Reduction Instructions
  defm vfredusum : RVVFloatingReductionBuiltin;
  defm vfredosum : RVVFloatingReductionBuiltin;

  // 14.4. Vector Widening Floating-Point Reduction Instructions
  defm vfwredusum : RVVFloatingWidenReductionBuiltin;
  defm vfwredosum : RVVFloatingWidenReductionBuiltin;
}
}

// 15. Vector Mask Instructions
// 15.1. Vector Mask-Register Logical Instructions
def vmand    : RVVMaskBinBuiltin;
def vmnand   : RVVMaskBinBuiltin;
def vmandn   : RVVMaskBinBuiltin;
def vmxor    : RVVMaskBinBuiltin;
def vmor     : RVVMaskBinBuiltin;
def vmnor    : RVVMaskBinBuiltin;
def vmorn    : RVVMaskBinBuiltin;
def vmxnor   : RVVMaskBinBuiltin;
// pseudoinstructions
def vmclr    : RVVMaskNullaryBuiltin;
def vmset    : RVVMaskNullaryBuiltin;
defm vmmv_m : RVVPseudoMaskBuiltin<"vmand", "c">;
defm vmnot_m : RVVPseudoMaskBuiltin<"vmnand", "c">;

let MaskedPolicyScheme = NonePolicy in {
// 15.2. Vector count population in mask vcpop.m
def vcpop : RVVMaskOp0Builtin<"um">;

// 15.3. vfirst find-first-set mask bit
def vfirst : RVVMaskOp0Builtin<"lm">;
}

let MaskedPolicyScheme = HasPassthruOperand,
    HasTailPolicy = false in {
// 15.4. vmsbf.m set-before-first mask bit
def vmsbf : RVVMaskUnaryBuiltin;

// 15.5. vmsif.m set-including-first mask bit
def vmsif : RVVMaskUnaryBuiltin;

// 15.6. vmsof.m set-only-first mask bit
def vmsof : RVVMaskUnaryBuiltin;
}

let UnMaskedPolicyScheme = HasPassthruOperand, SupportOverloading = false in {
  // 15.8. Vector Iota Instruction
  defm viota : RVVOutBuiltinSet<"viota", "csil", [["m", "Uv", "Uvm"]]>;

  // 15.9. Vector Element Index Instruction
  defm vid : RVVOutBuiltinSet<"vid", "csil", [["v", "v", "v"],
                                              ["v", "Uv", "Uv"]]>;
}

// 16. Vector Permutation Instructions
// 16.1. Integer Scalar Move Instructions
let HasMasked = false, MaskedPolicyScheme = NonePolicy in {
  let HasVL = false, OverloadedName = "vmv_x" in
    defm vmv_x : RVVOp0BuiltinSet<"vmv_x_s", "csil",
                                   [["s", "ve", "ev"],
                                    ["s", "UvUe", "UeUv"]]>;
  let OverloadedName = "vmv_s",
      UnMaskedPolicyScheme = HasPassthruOperand,
      SupportOverloading = false in
    defm vmv_s : RVVOutBuiltinSet<"vmv_s_x", "csil",
                                   [["x", "v", "ve"],
                                    ["x", "Uv", "UvUe"]]>;
}

// 16.2. Floating-Point Scalar Move Instructions
let HasMasked = false, MaskedPolicyScheme = NonePolicy in {
  let HasVL = false, OverloadedName = "vfmv_f" in
    defm vfmv_f : RVVOp0BuiltinSet<"vfmv_f_s", "xfd",
                                     [["s", "ve", "ev"]]>;
  let OverloadedName = "vfmv_s",
      UnMaskedPolicyScheme = HasPassthruOperand,
      SupportOverloading = false in
    defm vfmv_s : RVVOutBuiltinSet<"vfmv_s_f", "xfd",
                                     [["f", "v", "ve"],
                                      ["x", "Uv", "UvUe"]]>;
}

// 16.3. Vector Slide Instructions
// 16.3.1. Vector Slideup Instructions
defm vslideup   : RVVSlideUpBuiltinSet;
// 16.3.2. Vector Slidedown Instructions
defm vslidedown : RVVSlideDownBuiltinSet;

// 16.3.3. Vector Slide1up Instructions
let UnMaskedPolicyScheme = HasPassthruOperand in {
defm vslide1up : RVVSlideOneBuiltinSet;
defm vfslide1up : RVVFloatingBinVFBuiltinSet;

// 16.3.4. Vector Slide1down Instruction
defm vslide1down : RVVSlideOneBuiltinSet;
defm vfslide1down : RVVFloatingBinVFBuiltinSet;

// 16.4. Vector Register Gather Instructions
// signed and floating type
defm vrgather : RVVOutBuiltinSet<"vrgather_vv", "csilxfd",
                                 [["vv", "v", "vvUv"]]>;
defm vrgather : RVVOutBuiltinSet<"vrgather_vx", "csilxfd",
                                 [["vx", "v", "vvz"]]>;
defm vrgatherei16 : RVVOutBuiltinSet<"vrgatherei16_vv", "csilxfd",
                                     [["vv", "v", "vv(Log2EEW:4)Uv"]]>;
// unsigned type
defm vrgather : RVVOutBuiltinSet<"vrgather_vv", "csil",
                                 [["vv", "Uv", "UvUvUv"]]>;
defm vrgather : RVVOutBuiltinSet<"vrgather_vx", "csil",
                                 [["vx", "Uv", "UvUvz"]]>;
defm vrgatherei16 : RVVOutBuiltinSet<"vrgatherei16_vv", "csil",
                                     [["vv", "Uv", "UvUv(Log2EEW:4)Uv"]]>;
}

// 16.5. Vector Compress Instruction
let HasMasked = false,
    UnMaskedPolicyScheme = HasPassthruOperand,
    MaskedPolicyScheme = NonePolicy,
    ManualCodegen = [{
      // insert poison passthru
      if (PolicyAttrs & RVV_VTA)
        Ops.insert(Ops.begin(), llvm::PoisonValue::get(ResultType));
      IntrinsicTypes = {ResultType, Ops.back()->getType()};
    }] in {
  // signed and floating type
  defm vcompress : RVVOutBuiltinSet<"vcompress", "csilxfd",
                                    [["vm", "v", "vvm"]]>;
  // unsigned type
  defm vcompress : RVVOutBuiltinSet<"vcompress", "csil",
                                    [["vm", "Uv", "UvUvm"]]>;
}

// Miscellaneous
let HasMasked = false, HasVL = false, IRName = "" in {
  let Name = "vreinterpret_v", MaskedPolicyScheme = NonePolicy,
      ManualCodegen = [{
        if (ResultType->isIntOrIntVectorTy(1) ||
            Ops[0]->getType()->isIntOrIntVectorTy(1)) {
          assert(isa<ScalableVectorType>(ResultType) &&
                 isa<ScalableVectorType>(Ops[0]->getType()));

          LLVMContext &Context = CGM.getLLVMContext();
          ScalableVectorType *Boolean64Ty =
              ScalableVectorType::get(llvm::Type::getInt1Ty(Context), 64);

          if (ResultType->isIntOrIntVectorTy(1)) {
            // Casting from m1 vector integer -> vector boolean
            // Ex: <vscale x 8 x i8>
            //     --(bitcast)--------> <vscale x 64 x i1>
            //     --(vector_extract)-> <vscale x  8 x i1>
            llvm::Value *BitCast = Builder.CreateBitCast(Ops[0], Boolean64Ty);
            return Builder.CreateExtractVector(ResultType, BitCast,
                                               ConstantInt::get(Int64Ty, 0));
          } else {
            // Casting from vector boolean -> m1 vector integer
            // Ex: <vscale x  1 x i1>
            //       --(vector_insert)-> <vscale x 64 x i1>
            //       --(bitcast)-------> <vscale x  8 x i8>
            llvm::Value *Boolean64Val =
              Builder.CreateInsertVector(Boolean64Ty,
                                         llvm::PoisonValue::get(Boolean64Ty),
                                         Ops[0],
                                         ConstantInt::get(Int64Ty, 0));
            return Builder.CreateBitCast(Boolean64Val, ResultType);
          }
        }
        return Builder.CreateBitCast(Ops[0], ResultType);
      }] in {
    // Reinterpret between different type under the same SEW and LMUL
    def vreinterpret_i_u : RVVBuiltin<"Uvv", "vUv", "csil", "v">;
    def vreinterpret_i_f : RVVBuiltin<"Fvv", "vFv", "il", "v">;
    def vreinterpret_u_i : RVVBuiltin<"vUv", "Uvv", "csil", "Uv">;
    def vreinterpret_u_f : RVVBuiltin<"FvUv", "UvFv", "il", "Uv">;
    def vreinterpret_f_i : RVVBuiltin<"vFv", "Fvv", "il", "Fv">;
    def vreinterpret_f_u : RVVBuiltin<"UvFv", "FvUv", "il", "Fv">;
    let RequiredFeatures = ["Zvfhmin"] in {
      def vreinterpret_i_h : RVVBuiltin<"Fvv", "vFv", "s", "v">;
      def vreinterpret_u_h : RVVBuiltin<"FvUv", "UvFv", "s", "Uv">;
      def vreinterpret_h_i : RVVBuiltin<"vFv", "Fvv", "s", "Fv">;
      def vreinterpret_h_u : RVVBuiltin<"UvFv", "FvUv", "s", "Fv">;
    }
    let RequiredFeatures = ["Zvfbfmin"] in {
      def vreinterpret_i_bf16 : RVVBuiltin<"vIv", "Ivv", "y", "Iv">;
      def vreinterpret_u_bf16 : RVVBuiltin<"vUv", "Uvv", "y", "Uv">;
      def vreinterpret_bf16_i : RVVBuiltin<"Ivv", "vIv", "y", "v">;
      def vreinterpret_bf16_u : RVVBuiltin<"Uvv", "vUv", "y", "v">;
    }

    // Reinterpret between different SEW under the same LMUL
    foreach dst_sew = ["(FixedSEW:8)", "(FixedSEW:16)", "(FixedSEW:32)",
                       "(FixedSEW:64)"] in {
      def vreinterpret_i_ # dst_sew : RVVBuiltin<"v" # dst_sew # "v",
                                                 dst_sew # "vv", "csil", dst_sew # "v">;
      def vreinterpret_u_ # dst_sew : RVVBuiltin<"Uv" # dst_sew # "Uv",
                                                 dst_sew # "UvUv", "csil", dst_sew # "Uv">;
    }

    // Existing users of FixedSEW - the reinterpretation between different SEW
    // and same LMUL has the implicit assumption that if FixedSEW is set to the
    // given element width, then the type will be identified as invalid, thus
    // skipping definition of reinterpret of SEW=8 to SEW=8. However this blocks
    // our usage here of defining all possible combinations of a fixed SEW to
    // any boolean. So we need to separately define SEW=8 here.
    // Reinterpret from LMUL=1 integer type to vector boolean type
    def vreintrepret_m1_b8_signed :
        RVVBuiltin<"Svm",
                    "mSv",
                    "c", "m">;
    def vreintrepret_m1_b8_usigned :
        RVVBuiltin<"USvm",
                    "mUSv",
                    "c", "m">;

    // Reinterpret from vector boolean type to LMUL=1 integer type
    def vreintrepret_b8_m1_signed :
        RVVBuiltin<"mSv",
                    "Svm",
                    "c", "Sv">;
    def vreintrepret_b8_m1_usigned :
        RVVBuiltin<"mUSv",
                    "USvm",
                    "c", "USv">;

    foreach dst_sew = ["16", "32", "64"] in {
      // Reinterpret from LMUL=1 integer type to vector boolean type
      def vreinterpret_m1_b # dst_sew # _signed:
        RVVBuiltin<"(FixedSEW:" # dst_sew # ")Svm",
                    "m(FixedSEW:" # dst_sew # ")Sv",
                    "c", "m">;
      def vreinterpret_m1_b # dst_sew # _unsigned:
        RVVBuiltin<"(FixedSEW:" # dst_sew # ")USvm",
                    "m(FixedSEW:" # dst_sew # ")USv",
                    "c", "m">;
      // Reinterpret from vector boolean type to LMUL=1 integer type
      def vreinterpret_b # dst_sew # _m1_signed:
        RVVBuiltin<"m(FixedSEW:" # dst_sew # ")Sv",
                    "(FixedSEW:" # dst_sew # ")Svm",
                    "c", "(FixedSEW:" # dst_sew # ")Sv">;
      def vreinterpret_b # dst_sew # _m1_unsigned:
        RVVBuiltin<"m(FixedSEW:" # dst_sew # ")USv",
                    "(FixedSEW:" # dst_sew # ")USvm",
                    "c", "(FixedSEW:" # dst_sew # ")USv">;
    }
  }

  let Name = "vundefined", SupportOverloading = false,
      MaskedPolicyScheme = NonePolicy,
      ManualCodegen = [{
        return llvm::PoisonValue::get(ResultType);
      }] in {
    def vundefined : RVVBuiltin<"v", "v", "csilxfd">;
    let RequiredFeatures = ["Zvfbfmin"] in
      def vundefined_bf16 : RVVBuiltin<"v", "v", "y">;
    def vundefined_u : RVVBuiltin<"Uv", "Uv", "csil">;

    foreach nf = NFList in {
      let NF = nf in {
        defvar T = "(Tuple:" # nf # ")";
        def : RVVBuiltin<T # "v", T # "v", "csilxfd">;
        let RequiredFeatures = ["Zvfbfmin"] in
          def : RVVBuiltin<T # "v", T # "v", "y">;
        def : RVVBuiltin<T # "Uv", T # "Uv", "csil">;
      }
    }

  }

  // LMUL truncation
  // C/C++ Operand: VecTy, IR Operand: VecTy, Index
  let Name = "vlmul_trunc_v", OverloadedName = "vlmul_trunc",
      MaskedPolicyScheme = NonePolicy,
      ManualCodegen = [{ {
        return Builder.CreateExtractVector(ResultType, Ops[0],
                                           ConstantInt::get(Int64Ty, 0));
      } }] in {
    foreach dst_lmul = ["(SFixedLog2LMUL:-3)", "(SFixedLog2LMUL:-2)", "(SFixedLog2LMUL:-1)",
                        "(SFixedLog2LMUL:0)", "(SFixedLog2LMUL:1)", "(SFixedLog2LMUL:2)"] in {
      def vlmul_trunc # dst_lmul : RVVBuiltin<"v" # dst_lmul # "v",
                                              dst_lmul # "vv", "csilxfd", dst_lmul # "v">;
      let RequiredFeatures = ["Zvfbfmin"] in
        def vlmul_trunc_bf16 # dst_lmul : RVVBuiltin<"v" # dst_lmul # "v",
                                                     dst_lmul # "vv", "y", dst_lmul # "v">;
      def vlmul_trunc_u # dst_lmul : RVVBuiltin<"Uv" # dst_lmul # "Uv",
                                                dst_lmul # "UvUv", "csil", dst_lmul # "Uv">;
    }
  }

  // LMUL extension
  // C/C++ Operand: SubVecTy, IR Operand: VecTy, SubVecTy, Index
  let Name = "vlmul_ext_v", OverloadedName = "vlmul_ext",
      MaskedPolicyScheme = NonePolicy,
      ManualCodegen = [{
        return Builder.CreateInsertVector(ResultType,
                                          llvm::PoisonValue::get(ResultType),
                                          Ops[0], ConstantInt::get(Int64Ty, 0));
      }] in {
    foreach dst_lmul = ["(LFixedLog2LMUL:-2)", "(LFixedLog2LMUL:-1)", "(LFixedLog2LMUL:-0)",
                        "(LFixedLog2LMUL:1)", "(LFixedLog2LMUL:2)", "(LFixedLog2LMUL:3)"] in {
      def vlmul_ext # dst_lmul : RVVBuiltin<"v" # dst_lmul # "v",
                                            dst_lmul # "vv", "csilxfd", dst_lmul # "v">;
      let RequiredFeatures = ["Zvfbfmin"] in
        def vlmul_ext_bf16 # dst_lmul : RVVBuiltin<"v" # dst_lmul # "v",
                                                   dst_lmul # "vv", "y", dst_lmul # "v">;
      def vlmul_ext_u # dst_lmul : RVVBuiltin<"Uv" # dst_lmul # "Uv",
                                              dst_lmul # "UvUv", "csil", dst_lmul # "Uv">;
    }
  }

  let Name = "vget_v", MaskedPolicyScheme = NonePolicy,
      ManualCodegen = [{
      {
        if (isa<StructType>(Ops[0]->getType())) // For tuple type
          // Extract value from index (operand 1) of vtuple (operand 0)
          return Builder.CreateExtractValue(
            Ops[0],
            {(unsigned)cast<ConstantInt>(Ops[1])->getZExtValue()});
        auto *VecTy = cast<ScalableVectorType>(ResultType);
        auto *OpVecTy = cast<ScalableVectorType>(Ops[0]->getType());
        // Mask to only valid indices.
        unsigned MaxIndex = OpVecTy->getMinNumElements() / VecTy->getMinNumElements();
        assert(isPowerOf2_32(MaxIndex));
        Ops[1] = Builder.CreateZExt(Ops[1], Builder.getInt64Ty());
        Ops[1] = Builder.CreateAnd(Ops[1], MaxIndex - 1);
        Ops[1] = Builder.CreateMul(Ops[1],
                                   ConstantInt::get(Ops[1]->getType(),
                                                    VecTy->getMinNumElements()));
        return Builder.CreateExtractVector(ResultType, Ops[0], Ops[1]);
      }
      }] in {
    foreach dst_lmul = ["(SFixedLog2LMUL:0)", "(SFixedLog2LMUL:1)", "(SFixedLog2LMUL:2)"] in {
      def : RVVBuiltin<"v" # dst_lmul # "v", dst_lmul # "vvKz", "csilxfdy", dst_lmul # "v">;
      def : RVVBuiltin<"Uv" # dst_lmul # "Uv", dst_lmul # "UvUvKz", "csil", dst_lmul # "Uv">;
    }
    foreach nf = NFList in {
      defvar T = "(Tuple:" # nf # ")";
      def : RVVBuiltin<T # "vv", "v" # T # "vKz", "csilxfdy", "v">;
      def : RVVBuiltin<T # "UvUv", "Uv" # T # "UvKz", "csil", "Uv">;
    }
  }

  let Name = "vset_v", MaskedPolicyScheme = NonePolicy,
      ManualCodegen = [{
      {
        if (isa<StructType>(ResultType)) // For tuple type
          // Insert value (operand 2) into index (operand 1) of vtuple (operand 0)
          return Builder.CreateInsertValue(
            Ops[0], Ops[2],
            {(unsigned)cast<ConstantInt>(Ops[1])->getZExtValue()});
        auto *ResVecTy = cast<ScalableVectorType>(ResultType);
        auto *VecTy = cast<ScalableVectorType>(Ops[2]->getType());
        // Mask to only valid indices.
        unsigned MaxIndex = ResVecTy->getMinNumElements() / VecTy->getMinNumElements();
        assert(isPowerOf2_32(MaxIndex));
        Ops[1] = Builder.CreateZExt(Ops[1], Builder.getInt64Ty());
        Ops[1] = Builder.CreateAnd(Ops[1], MaxIndex - 1);
        Ops[1] = Builder.CreateMul(Ops[1],
                                   ConstantInt::get(Ops[1]->getType(),
                                                    VecTy->getMinNumElements()));
        return Builder.CreateInsertVector(ResultType, Ops[0], Ops[2], Ops[1]);
      }
      }] in {
    foreach dst_lmul = ["(LFixedLog2LMUL:1)", "(LFixedLog2LMUL:2)", "(LFixedLog2LMUL:3)"] in {
      def : RVVBuiltin<"v" # dst_lmul # "v", dst_lmul # "v" # dst_lmul # "vKzv", "csilxfd">;
      let RequiredFeatures = ["Zvfbfmin"] in
        def : RVVBuiltin<"v" # dst_lmul # "v", dst_lmul # "v" # dst_lmul # "vKzv", "y">;
      def : RVVBuiltin<"Uv" # dst_lmul # "Uv", dst_lmul # "Uv" # dst_lmul #"UvKzUv", "csil">;
    }
    foreach nf = NFList in {
      defvar T = "(Tuple:" # nf # ")";
      def : RVVBuiltin<"v" # T # "v", T # "v" # T # "vKzv", "csilxfd">;
      let RequiredFeatures = ["Zvfbfmin"] in
        def : RVVBuiltin<"v" # T # "v", T # "v" # T # "vKzv", "y">;
      def : RVVBuiltin<"Uv" # T # "Uv", T # "Uv" # T # "UvKzUv", "csil">;
    }
  }

  let Name = "vcreate_v",
      UnMaskedPolicyScheme = NonePolicy,
      MaskedPolicyScheme = NonePolicy,
      SupportOverloading = false,
      ManualCodegen = [{
      {
        if (isa<StructType>(ResultType)) {
          unsigned NF = cast<StructType>(ResultType)->getNumElements();
          llvm::Value *ReturnTuple = llvm::PoisonValue::get(ResultType);
          for (unsigned I = 0; I < NF; ++I) {
            ReturnTuple = Builder.CreateInsertValue(ReturnTuple, Ops[I], {I});
          }
          return ReturnTuple;
        }
        llvm::Value *ReturnVector = llvm::PoisonValue::get(ResultType);
        auto *VecTy = cast<ScalableVectorType>(Ops[0]->getType());
        for (unsigned I = 0, N = Ops.size(); I < N; ++I) {
          llvm::Value *Idx =
            ConstantInt::get(Builder.getInt64Ty(),
                              VecTy->getMinNumElements() * I);
          ReturnVector =
            Builder.CreateInsertVector(ResultType, ReturnVector, Ops[I], Idx);
        }
        return ReturnVector;
      }
      }] in {

    // Since the vcreate_v uses LFixedLog2LMUL, setting the Log2LMUL to [-3] can
    // avoid creating the intrinsics which contain the same name and prototype.
    let Log2LMUL = [-3] in {
      defm : RVVNonTupleVCreateBuiltin<1, [0]>;
      defm : RVVNonTupleVCreateBuiltin<2, [0, 1]>;
      defm : RVVNonTupleVCreateBuiltin<3, [0, 1, 2]>;
    }

    foreach nf = NFList in {
      let NF = nf in {
        defvar T = "(Tuple:" # nf # ")";
        defvar V = VString<nf, /*signed=*/true>.S;
        defvar UV = VString<nf, /*signed=*/false>.S;
        def : RVVBuiltin<T # "v", T # "v" # V, "csilxfdy">;
        let RequiredFeatures = ["Zvfbfmin"] in
          def : RVVBuiltin<T # "v", T # "v" # V, "y">;
        def : RVVBuiltin<T # "Uv", T # "Uv" # UV, "csil">;
      }
    }
  }
}

multiclass RVVOutBuiltinSetZvbb {
  let OverloadedName = NAME in
    defm "" : RVVOutBuiltinSet<NAME, "csil", [["v", "v", "vv"],
                                              ["v", "Uv", "UvUv"]]>;
}

multiclass RVVOutBuiltinSetZvk<bit HasVV = 1, bit HasVS = 1> {
  // vaesz only has 'vs' and vgmul only has 'vv' and they do not have ambiguous
  // prototypes like other zvkned instructions (e.g. vaesdf), so we don't
  // need to encode the operand mnemonics into its intrinsic function name.
  if HasVV then {
    defvar name = NAME # !if(!eq(NAME, "vgmul"), "", "_vv");
    let OverloadedName = name in
      defm "" : RVVOutBuiltinSet<NAME # "_vv", "i",
                                 [["vv", "Uv", "UvUvUv"]]>;
  }

  if HasVS then {
    foreach vs2_lmul = ["(SEFixedLog2LMUL:-1)", "(SEFixedLog2LMUL:0)",
                        "(SEFixedLog2LMUL:1)", "(SEFixedLog2LMUL:2)"] in {
    defvar name = NAME # !if(!eq(NAME, "vaesz"), "", "_vs");
    let OverloadedName = name, IRName = NAME # "_vs", Name = NAME # "_vs",
        IntrinsicTypes = [-1, 1] in
      def NAME # vs2_lmul
          : RVVBuiltin<vs2_lmul # "UvUv", "UvUv" # vs2_lmul # "Uv", "i">;
    }
  }
}

multiclass RVVOutOp2BuiltinSetVVZvk<string type_range = "i">
    : RVVOutOp2BuiltinSet<NAME, type_range, [["vv", "Uv", "UvUvUvUv"]]>;

multiclass RVVOutOp2BuiltinSetVIZvk<string type_range = "i">
    : RVVOutOp2BuiltinSet<NAME, type_range, [["vi", "Uv", "UvUvUvKz"]]>;

multiclass RVVSignedWidenBinBuiltinSetVwsll
    : RVVWidenBuiltinSet<NAME, "csi",
                         [["vv", "Uw", "UwUvUv"],
                          ["vx", "Uw", "UwUvz"]]>;

let UnMaskedPolicyScheme = HasPassthruOperand in {
  // zvkb
  let RequiredFeatures = ["Zvkb", "Experimental"] in {
    defm vandn   : RVVUnsignedBinBuiltinSet;
    defm vbrev8  : RVVOutBuiltinSetZvbb;
    defm vrev8   : RVVOutBuiltinSetZvbb;
    defm vrol    : RVVUnsignedShiftBuiltinSet;
    defm vror    : RVVUnsignedShiftBuiltinSet;
  }

  // zvbb
  let RequiredFeatures = ["Zvbb", "Experimental"] in {
    defm vbrev   : RVVOutBuiltinSetZvbb;
    defm vclz    : RVVOutBuiltinSetZvbb;
    defm vctz    : RVVOutBuiltinSetZvbb;
    let IRName = "vcpopv", MaskedIRName = "vcpopv_mask" in
    defm vcpop   : RVVOutBuiltinSetZvbb;
    let OverloadedName = "vwsll" in
    defm vwsll   : RVVSignedWidenBinBuiltinSetVwsll;
  }

  // zvbc
  let RequiredFeatures = ["Zvbc", "Experimental"] in {
    defm vclmul  : RVVInt64BinBuiltinSet;
    defm vclmulh : RVVInt64BinBuiltinSet;
  }
}

let UnMaskedPolicyScheme = HasPolicyOperand, HasMasked = false in {
  // zvkg
  let RequiredFeatures = ["Zvkg", "Experimental"] in {
    defm vghsh   : RVVOutOp2BuiltinSetVVZvk;
    defm vgmul   : RVVOutBuiltinSetZvk<HasVV=1, HasVS=0>;
  }

  // zvkned
  let RequiredFeatures = ["Zvkned", "Experimental"] in {
    defm vaesdf  : RVVOutBuiltinSetZvk;
    defm vaesdm  : RVVOutBuiltinSetZvk;
    defm vaesef  : RVVOutBuiltinSetZvk;
    defm vaesem  : RVVOutBuiltinSetZvk;
    let UnMaskedPolicyScheme = HasPassthruOperand in
    defm vaeskf1 : RVVOutOp1BuiltinSet<"vaeskf1", "i", [["vi", "Uv", "UvUvKz"]]>;
    defm vaeskf2 : RVVOutOp2BuiltinSetVIZvk;
    defm vaesz   : RVVOutBuiltinSetZvk<HasVV=0>;
  }

  // zvknha
  let RequiredFeatures = ["Zvknha", "Experimental"] in {
    defm vsha2ch : RVVOutOp2BuiltinSetVVZvk<"i">;
    defm vsha2cl : RVVOutOp2BuiltinSetVVZvk<"i">;
    defm vsha2ms : RVVOutOp2BuiltinSetVVZvk<"i">;
  }

  // zvknhb
  let RequiredFeatures = ["Zvknhb", "Experimental"] in {
    defm vsha2ch : RVVOutOp2BuiltinSetVVZvk<"il">;
    defm vsha2cl : RVVOutOp2BuiltinSetVVZvk<"il">;
    defm vsha2ms : RVVOutOp2BuiltinSetVVZvk<"il">;
  }

  // zvksed
  let RequiredFeatures = ["Zvksed", "Experimental"] in {
    let UnMaskedPolicyScheme = HasPassthruOperand in
    defm vsm4k   : RVVOutOp1BuiltinSet<"vsm4k", "i", [["vi", "Uv", "UvUvKz"]]>;
    defm vsm4r   : RVVOutBuiltinSetZvk;
  }

  // zvksh
  let RequiredFeatures = ["Zvksh", "Experimental"] in {
    defm vsm3c   : RVVOutOp2BuiltinSetVIZvk;
    let UnMaskedPolicyScheme = HasPassthruOperand in
    defm vsm3me  : RVVOutOp1BuiltinSet<"vsm3me", "i", [["vv", "Uv", "UvUvUv"]]>;
  }
}
