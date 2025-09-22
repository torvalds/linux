// WebAssemblyInstrMemory.td-WebAssembly Memory codegen support -*- tablegen -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// WebAssembly Memory operand code-gen constructs.
///
//===----------------------------------------------------------------------===//

// TODO:
//  - WebAssemblyTargetLowering having to do with atomics
//  - Each has optional alignment.

// WebAssembly has i8/i16/i32/i64/f32/f64 memory types, but doesn't have i8/i16
// local types. These memory-only types instead zero- or sign-extend into local
// types when loading, and truncate when storing.

// Address Operands

// These patterns match the static (offset) and dynamic (address stack operand)
// operands for loads and stores, based on a combination of target global
// addresses and constants.
// For example,
// (load (add tga, x))   -> load offset=tga, addr=x
// (store v, tga)        -> store v, offset=tga, addr=0
// (load (add const, x)) -> load offset=const, addr=x
// (store v, const)      -> store v, offset=const, addr=0
// (load x)              -> load offset=0, addr=x
def AddrOps32 : ComplexPattern<i32, 2, "SelectAddrOperands32">;
def AddrOps64 : ComplexPattern<i64, 2, "SelectAddrOperands64">;

// Defines atomic and non-atomic loads, regular and extending.
multiclass WebAssemblyLoad<WebAssemblyRegClass rc, string Name, int Opcode,
                           list<Predicate> reqs = []> {
  let mayLoad = 1, UseNamedOperandTable = 1 in {
  defm "_A32": I<(outs rc:$dst),
                 (ins P2Align:$p2align, offset32_op:$off, I32:$addr),
                 (outs), (ins P2Align:$p2align, offset32_op:$off),
                 [], !strconcat(Name, "\t$dst, ${off}(${addr})${p2align}"),
                 !strconcat(Name, "\t${off}${p2align}"), Opcode, false>,
               Requires<reqs>;
  defm "_A64": I<(outs rc:$dst),
                 (ins P2Align:$p2align, offset64_op:$off, I64:$addr),
                 (outs), (ins P2Align:$p2align, offset64_op:$off),
                 [], !strconcat(Name, "\t$dst, ${off}(${addr})${p2align}"),
                 !strconcat(Name, "\t${off}${p2align}"), Opcode, true>,
               Requires<reqs>;
  }
}

// Basic load.
// FIXME: When we can break syntax compatibility, reorder the fields in the
// asmstrings to match the binary encoding.
defm LOAD_I32 : WebAssemblyLoad<I32, "i32.load", 0x28, []>;
defm LOAD_I64 : WebAssemblyLoad<I64, "i64.load", 0x29, []>;
defm LOAD_F32 : WebAssemblyLoad<F32, "f32.load", 0x2a, []>;
defm LOAD_F64 : WebAssemblyLoad<F64, "f64.load", 0x2b, []>;

// Extending load.
defm LOAD8_S_I32 : WebAssemblyLoad<I32, "i32.load8_s", 0x2c, []>;
defm LOAD8_U_I32 : WebAssemblyLoad<I32, "i32.load8_u", 0x2d, []>;
defm LOAD16_S_I32 : WebAssemblyLoad<I32, "i32.load16_s", 0x2e, []>;
defm LOAD16_U_I32 : WebAssemblyLoad<I32, "i32.load16_u", 0x2f, []>;
defm LOAD8_S_I64 : WebAssemblyLoad<I64, "i64.load8_s", 0x30, []>;
defm LOAD8_U_I64 : WebAssemblyLoad<I64, "i64.load8_u", 0x31, []>;
defm LOAD16_S_I64 : WebAssemblyLoad<I64, "i64.load16_s", 0x32, []>;
defm LOAD16_U_I64 : WebAssemblyLoad<I64, "i64.load16_u", 0x33, []>;
defm LOAD32_S_I64 : WebAssemblyLoad<I64, "i64.load32_s", 0x34, []>;
defm LOAD32_U_I64 : WebAssemblyLoad<I64, "i64.load32_u", 0x35, []>;

// Half-precision load.
defm LOAD_F16_F32 :
  WebAssemblyLoad<F32, "f32.load_f16", 0xfc30, [HasHalfPrecision]>;

// Pattern matching

multiclass LoadPat<ValueType ty, SDPatternOperator kind, string Name> {
      def : Pat<(ty (kind (AddrOps32 offset32_op:$offset, I32:$addr))),
           (!cast<NI>(Name # "_A32") 0,
                                     offset32_op:$offset,
                                     I32:$addr)>,
                                     Requires<[HasAddr32]>;

      def : Pat<(ty (kind (AddrOps64 offset64_op:$offset, I64:$addr))),
           (!cast<NI>(Name # "_A64") 0,
                                     offset64_op:$offset,
                                     I64:$addr)>,
                                     Requires<[HasAddr64]>;
}

defm : LoadPat<i32, load, "LOAD_I32">;
defm : LoadPat<i64, load, "LOAD_I64">;
defm : LoadPat<f32, load, "LOAD_F32">;
defm : LoadPat<f64, load, "LOAD_F64">;

defm : LoadPat<i32, sextloadi8, "LOAD8_S_I32">;
defm : LoadPat<i32, sextloadi16, "LOAD16_S_I32">;
defm : LoadPat<i64, sextloadi8, "LOAD8_S_I64">;
defm : LoadPat<i64, sextloadi16, "LOAD16_S_I64">;
defm : LoadPat<i64, sextloadi32, "LOAD32_S_I64">;

defm : LoadPat<i32, zextloadi8, "LOAD8_U_I32">;
defm : LoadPat<i32, zextloadi16, "LOAD16_U_I32">;
defm : LoadPat<i64, zextloadi8, "LOAD8_U_I64">;
defm : LoadPat<i64, zextloadi16, "LOAD16_U_I64">;
defm : LoadPat<i64, zextloadi32, "LOAD32_U_I64">;

defm : LoadPat<i32, extloadi8, "LOAD8_U_I32">;
defm : LoadPat<i32, extloadi16, "LOAD16_U_I32">;
defm : LoadPat<i64, extloadi8, "LOAD8_U_I64">;
defm : LoadPat<i64, extloadi16, "LOAD16_U_I64">;
defm : LoadPat<i64, extloadi32, "LOAD32_U_I64">;

defm : LoadPat<f32, int_wasm_loadf16_f32, "LOAD_F16_F32">;

// Defines atomic and non-atomic stores, regular and truncating
multiclass WebAssemblyStore<WebAssemblyRegClass rc, string Name, int Opcode,
                            list<Predicate> reqs = []> {
  let mayStore = 1, UseNamedOperandTable = 1 in
  defm "_A32" : I<(outs),
                  (ins P2Align:$p2align, offset32_op:$off, I32:$addr, rc:$val),
                  (outs),
                  (ins P2Align:$p2align, offset32_op:$off), [],
                  !strconcat(Name, "\t${off}(${addr})${p2align}, $val"),
                  !strconcat(Name, "\t${off}${p2align}"), Opcode, false>,
                Requires<reqs>;
  let mayStore = 1, UseNamedOperandTable = 1 in
  defm "_A64" : I<(outs),
                  (ins P2Align:$p2align, offset64_op:$off, I64:$addr, rc:$val),
                  (outs),
                  (ins P2Align:$p2align, offset64_op:$off), [],
                  !strconcat(Name, "\t${off}(${addr})${p2align}, $val"),
                  !strconcat(Name, "\t${off}${p2align}"), Opcode, true>,
                Requires<reqs>;
}

// Basic store.
// Note: WebAssembly inverts SelectionDAG's usual operand order.
defm STORE_I32  : WebAssemblyStore<I32, "i32.store", 0x36>;
defm STORE_I64  : WebAssemblyStore<I64, "i64.store", 0x37>;
defm STORE_F32  : WebAssemblyStore<F32, "f32.store", 0x38>;
defm STORE_F64  : WebAssemblyStore<F64, "f64.store", 0x39>;

multiclass StorePat<ValueType ty, SDPatternOperator kind, string Name> {
  def : Pat<(kind ty:$val, (AddrOps32 offset32_op:$offset, I32:$addr)),
            (!cast<NI>(Name # "_A32") 0,
                                      offset32_op:$offset,
                                      I32:$addr,
                                      ty:$val)>,
            Requires<[HasAddr32]>;
  def : Pat<(kind ty:$val, (AddrOps64 offset64_op:$offset, I64:$addr)),
            (!cast<NI>(Name # "_A64") 0,
                                      offset64_op:$offset,
                                      I64:$addr,
                                      ty:$val)>,
            Requires<[HasAddr64]>;
}

defm : StorePat<i32, store, "STORE_I32">;
defm : StorePat<i64, store, "STORE_I64">;
defm : StorePat<f32, store, "STORE_F32">;
defm : StorePat<f64, store, "STORE_F64">;

// Truncating store.
defm STORE8_I32 : WebAssemblyStore<I32, "i32.store8", 0x3a>;
defm STORE16_I32 : WebAssemblyStore<I32, "i32.store16", 0x3b>;
defm STORE8_I64 : WebAssemblyStore<I64, "i64.store8", 0x3c>;
defm STORE16_I64 : WebAssemblyStore<I64, "i64.store16", 0x3d>;
defm STORE32_I64 : WebAssemblyStore<I64, "i64.store32", 0x3e>;

// Half-precision store.
defm STORE_F16_F32 :
  WebAssemblyStore<F32, "f32.store_f16", 0xfc31, [HasHalfPrecision]>;

defm : StorePat<i32, truncstorei8, "STORE8_I32">;
defm : StorePat<i32, truncstorei16, "STORE16_I32">;
defm : StorePat<i64, truncstorei8, "STORE8_I64">;
defm : StorePat<i64, truncstorei16, "STORE16_I64">;
defm : StorePat<i64, truncstorei32, "STORE32_I64">;

defm : StorePat<f32, int_wasm_storef16_f32, "STORE_F16_F32">;

multiclass MemoryOps<WebAssemblyRegClass rc, string B> {
// Current memory size.
defm MEMORY_SIZE_A#B : I<(outs rc:$dst), (ins i32imm:$flags),
                         (outs), (ins i32imm:$flags),
                         [(set rc:$dst,
                           (int_wasm_memory_size (i32 imm:$flags)))],
                         "memory.size\t$dst, $flags", "memory.size\t$flags",
                         0x3f>;

// Grow memory.
defm MEMORY_GROW_A#B : I<(outs rc:$dst), (ins i32imm:$flags, rc:$delta),
                         (outs), (ins i32imm:$flags),
                         [(set rc:$dst,
                           (int_wasm_memory_grow (i32 imm:$flags),
                             rc:$delta))],
                         "memory.grow\t$dst, $flags, $delta",
                         "memory.grow\t$flags", 0x40>;
}

defm : MemoryOps<I32, "32">;
defm : MemoryOps<I64, "64">;
