// WebAssemblyInstrAtomics.td-WebAssembly Atomic codegen support-*- tablegen -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// WebAssembly Atomic operand code-gen constructs.
///
//===----------------------------------------------------------------------===//

let UseNamedOperandTable = 1 in
multiclass ATOMIC_I<dag oops_r, dag iops_r, dag oops_s, dag iops_s,
                    list<dag> pattern_r, string asmstr_r,
                    string asmstr_s, bits<32> atomic_op,
                    bit is64 = false> {
  defm "" : I<oops_r, iops_r, oops_s, iops_s, pattern_r, asmstr_r, asmstr_s,
              !or(0xfe00, !and(0xff, atomic_op)), is64>,
            Requires<[HasAtomics]>;
}

multiclass ATOMIC_NRI<dag oops, dag iops, list<dag> pattern, string asmstr = "",
                      bits<32> atomic_op = -1> {
  defm "" : NRI<oops, iops, pattern, asmstr,
                !or(0xfe00, !and(0xff, atomic_op))>,
            Requires<[HasAtomics]>;
}

//===----------------------------------------------------------------------===//
// Atomic wait / notify
//===----------------------------------------------------------------------===//

let hasSideEffects = 1 in {
defm MEMORY_ATOMIC_NOTIFY_A32 :
  ATOMIC_I<(outs I32:$dst),
           (ins P2Align:$p2align, offset32_op:$off, I32:$addr, I32:$count),
           (outs), (ins P2Align:$p2align, offset32_op:$off), [],
           "memory.atomic.notify \t$dst, ${off}(${addr})${p2align}, $count",
           "memory.atomic.notify \t${off}${p2align}", 0x00, false>;
defm MEMORY_ATOMIC_NOTIFY_A64 :
  ATOMIC_I<(outs I32:$dst),
           (ins P2Align:$p2align, offset64_op:$off, I64:$addr, I32:$count),
           (outs), (ins P2Align:$p2align, offset64_op:$off), [],
           "memory.atomic.notify \t$dst, ${off}(${addr})${p2align}, $count",
           "memory.atomic.notify \t${off}${p2align}", 0x00, true>;
let mayLoad = 1 in {
defm MEMORY_ATOMIC_WAIT32_A32 :
  ATOMIC_I<(outs I32:$dst),
           (ins P2Align:$p2align, offset32_op:$off, I32:$addr, I32:$exp,
                I64:$timeout),
           (outs), (ins P2Align:$p2align, offset32_op:$off), [],
           "memory.atomic.wait32 \t$dst, ${off}(${addr})${p2align}, $exp, $timeout",
           "memory.atomic.wait32 \t${off}${p2align}", 0x01, false>;
defm MEMORY_ATOMIC_WAIT32_A64 :
  ATOMIC_I<(outs I32:$dst),
           (ins P2Align:$p2align, offset64_op:$off, I64:$addr, I32:$exp,
                I64:$timeout),
           (outs), (ins P2Align:$p2align, offset64_op:$off), [],
           "memory.atomic.wait32 \t$dst, ${off}(${addr})${p2align}, $exp, $timeout",
           "memory.atomic.wait32 \t${off}${p2align}", 0x01, true>;
defm MEMORY_ATOMIC_WAIT64_A32 :
  ATOMIC_I<(outs I32:$dst),
           (ins P2Align:$p2align, offset32_op:$off, I32:$addr, I64:$exp,
                I64:$timeout),
           (outs), (ins P2Align:$p2align, offset32_op:$off), [],
           "memory.atomic.wait64 \t$dst, ${off}(${addr})${p2align}, $exp, $timeout",
           "memory.atomic.wait64 \t${off}${p2align}", 0x02, false>;
defm MEMORY_ATOMIC_WAIT64_A64 :
  ATOMIC_I<(outs I32:$dst),
           (ins P2Align:$p2align, offset64_op:$off, I64:$addr, I64:$exp,
                I64:$timeout),
           (outs), (ins P2Align:$p2align, offset64_op:$off), [],
           "memory.atomic.wait64 \t$dst, ${off}(${addr})${p2align}, $exp, $timeout",
           "memory.atomic.wait64 \t${off}${p2align}", 0x02, true>;
} // mayLoad = 1
} // hasSideEffects = 1

def NotifyPat_A32 :
  Pat<(i32 (int_wasm_memory_atomic_notify (AddrOps32 offset32_op:$offset, I32:$addr), I32:$count)),
      (MEMORY_ATOMIC_NOTIFY_A32 0, $offset, $addr, $count)>,
  Requires<[HasAddr32, HasAtomics]>;
def NotifyPat_A64 :
  Pat<(i32 (int_wasm_memory_atomic_notify (AddrOps64 offset64_op:$offset, I64:$addr), I32:$count)),
      (MEMORY_ATOMIC_NOTIFY_A64 0, $offset, $addr, $count)>,
  Requires<[HasAddr64, HasAtomics]>;


multiclass WaitPat<ValueType ty, Intrinsic kind, string inst> {
  def WaitPat_A32 :
    Pat<(i32 (kind (AddrOps32 offset32_op:$offset, I32:$addr), ty:$exp, I64:$timeout)),
        (!cast<NI>(inst#_A32) 0, $offset, $addr, $exp, $timeout)>,
    Requires<[HasAddr32, HasAtomics]>;
  def WaitPat_A64 :
    Pat<(i32 (kind (AddrOps64 offset64_op:$offset, I64:$addr), ty:$exp, I64:$timeout)),
        (!cast<NI>(inst#_A64) 0, $offset, $addr, $exp, $timeout)>,
    Requires<[HasAddr64, HasAtomics]>;
}

defm : WaitPat<i32, int_wasm_memory_atomic_wait32, "MEMORY_ATOMIC_WAIT32">;
defm : WaitPat<i64, int_wasm_memory_atomic_wait64, "MEMORY_ATOMIC_WAIT64">;

//===----------------------------------------------------------------------===//
// Atomic fences
//===----------------------------------------------------------------------===//

// A compiler fence instruction that prevents reordering of instructions.
let Defs = [ARGUMENTS] in {
let isPseudo = 1, hasSideEffects = 1 in
defm COMPILER_FENCE : ATOMIC_NRI<(outs), (ins), [], "compiler_fence">;
let hasSideEffects = 1 in
defm ATOMIC_FENCE : ATOMIC_NRI<(outs), (ins i8imm:$flags), [], "atomic.fence",
                               0x03>;
} // Defs = [ARGUMENTS]

//===----------------------------------------------------------------------===//
// Atomic loads
//===----------------------------------------------------------------------===//

multiclass AtomicLoad<WebAssemblyRegClass rc, string name, int atomic_op> {
  defm "" : WebAssemblyLoad<rc, name, !or(0xfe00, !and(0xff, atomic_op)),
                            [HasAtomics]>;
}

defm ATOMIC_LOAD_I32 : AtomicLoad<I32, "i32.atomic.load", 0x10>;
defm ATOMIC_LOAD_I64 : AtomicLoad<I64, "i64.atomic.load", 0x11>;

// Select loads
defm : LoadPat<i32, atomic_load_32, "ATOMIC_LOAD_I32">;
defm : LoadPat<i64, atomic_load_64, "ATOMIC_LOAD_I64">;

// Extending loads. Note that there are only zero-extending atomic loads, no
// sign-extending loads.
defm ATOMIC_LOAD8_U_I32 : AtomicLoad<I32, "i32.atomic.load8_u", 0x12>;
defm ATOMIC_LOAD16_U_I32 : AtomicLoad<I32, "i32.atomic.load16_u", 0x13>;
defm ATOMIC_LOAD8_U_I64 : AtomicLoad<I64, "i64.atomic.load8_u", 0x14>;
defm ATOMIC_LOAD16_U_I64 : AtomicLoad<I64, "i64.atomic.load16_u", 0x15>;
defm ATOMIC_LOAD32_U_I64 : AtomicLoad<I64, "i64.atomic.load32_u", 0x16>;

// Fragments for extending loads. These are different from regular loads because
// the SDNodes are derived from AtomicSDNode rather than LoadSDNode and
// therefore don't have the extension type field. So instead of matching that,
// we match the patterns that the type legalizer expands them to.

// Unlike regular loads, extension to i64 is handled differently than i32.
// i64 (zext (i8 (atomic_load_8))) gets legalized to
// i64 (and (i64 (anyext (i32 (atomic_load_8)))), 255)
// Extension to i32 is elided by SelectionDAG as our atomic loads are
// zero-extending.
def zext_aload_8_64 :
  PatFrag<(ops node:$addr),
          (i64 (zext (i32 (atomic_load_8 node:$addr))))>;
def zext_aload_16_64 :
  PatFrag<(ops node:$addr),
          (i64 (zext (i32 (atomic_load_16 node:$addr))))>;
def zext_aload_32_64 :
  PatFrag<(ops node:$addr),
          (i64 (zext (i32 (atomic_load_32 node:$addr))))>;

// We don't have single sext atomic load instructions. So for sext loads, we
// match bare subword loads (for 32-bit results) and anyext loads (for 64-bit
// results) and select a zext load; the next instruction will be sext_inreg
// which is selected by itself.
def sext_aload_8_64 :
  PatFrag<(ops node:$addr), (anyext (i32 (atomic_load_8 node:$addr)))>;
def sext_aload_16_64 :
  PatFrag<(ops node:$addr), (anyext (i32 (atomic_load_16 node:$addr)))>;

// Select zero-extending loads
defm : LoadPat<i64, zext_aload_8_64, "ATOMIC_LOAD8_U_I64">;
defm : LoadPat<i64, zext_aload_16_64, "ATOMIC_LOAD16_U_I64">;
defm : LoadPat<i64, zext_aload_32_64, "ATOMIC_LOAD32_U_I64">;

// Select sign-extending loads
defm : LoadPat<i32, atomic_load_8, "ATOMIC_LOAD8_U_I32">;
defm : LoadPat<i32, atomic_load_16, "ATOMIC_LOAD16_U_I32">;
defm : LoadPat<i64, sext_aload_8_64, "ATOMIC_LOAD8_U_I64">;
defm : LoadPat<i64, sext_aload_16_64, "ATOMIC_LOAD16_U_I64">;
// 32->64 sext load gets selected as i32.atomic.load, i64.extend_i32_s


//===----------------------------------------------------------------------===//
// Atomic stores
//===----------------------------------------------------------------------===//

multiclass AtomicStore<WebAssemblyRegClass rc, string name, int atomic_op> {
  defm "" : WebAssemblyStore<rc, name, !or(0xfe00, !and(0xff, atomic_op)),
                             [HasAtomics]>;
}

defm ATOMIC_STORE_I32 : AtomicStore<I32, "i32.atomic.store", 0x17>;
defm ATOMIC_STORE_I64 : AtomicStore<I64, "i64.atomic.store", 0x18>;

// We used to need an 'atomic' version of store patterns because store and atomic_store
// nodes have different operand orders.
//
// TODO: This is no longer true and atomic_store and store patterns
// can be unified.

multiclass AStorePat<ValueType ty, PatFrag kind, string inst> {
  def : Pat<(kind ty:$val, (AddrOps32 offset32_op:$offset, I32:$addr)),
            (!cast<NI>(inst#_A32) 0, $offset, $addr, $val)>,
        Requires<[HasAddr32, HasAtomics]>;
  def : Pat<(kind ty:$val, (AddrOps64 offset64_op:$offset, I64:$addr)),
            (!cast<NI>(inst#_A64) 0, $offset, $addr, $val)>,
        Requires<[HasAddr64, HasAtomics]>;
}
defm : AStorePat<i32, atomic_store_32, "ATOMIC_STORE_I32">;
defm : AStorePat<i64, atomic_store_64, "ATOMIC_STORE_I64">;

// Truncating stores.
defm ATOMIC_STORE8_I32 : AtomicStore<I32, "i32.atomic.store8", 0x19>;
defm ATOMIC_STORE16_I32 : AtomicStore<I32, "i32.atomic.store16", 0x1a>;
defm ATOMIC_STORE8_I64 : AtomicStore<I64, "i64.atomic.store8", 0x1b>;
defm ATOMIC_STORE16_I64 : AtomicStore<I64, "i64.atomic.store16", 0x1c>;
defm ATOMIC_STORE32_I64 : AtomicStore<I64, "i64.atomic.store32", 0x1d>;

// Fragments for truncating stores.

// We don't have single truncating atomic store instructions. For 32-bit
// instructions, we just need to match bare atomic stores. On the other hand,
// truncating stores from i64 values are once truncated to i32 first.
class trunc_astore_64<PatFrag kind> :
  PatFrag<(ops node:$val, node:$addr),
          (kind (i32 (trunc (i64 node:$val))), node:$addr)>;
def trunc_astore_8_64 : trunc_astore_64<atomic_store_8>;
def trunc_astore_16_64 : trunc_astore_64<atomic_store_16>;
def trunc_astore_32_64 : trunc_astore_64<atomic_store_32>;

// Truncating stores with no constant offset
defm : AStorePat<i32, atomic_store_8, "ATOMIC_STORE8_I32">;
defm : AStorePat<i32, atomic_store_16, "ATOMIC_STORE16_I32">;
defm : AStorePat<i64, trunc_astore_8_64, "ATOMIC_STORE8_I64">;
defm : AStorePat<i64, trunc_astore_16_64, "ATOMIC_STORE16_I64">;
defm : AStorePat<i64, trunc_astore_32_64, "ATOMIC_STORE32_I64">;

//===----------------------------------------------------------------------===//
// Atomic binary read-modify-writes
//===----------------------------------------------------------------------===//

multiclass WebAssemblyBinRMW<WebAssemblyRegClass rc, string name,
                             int atomic_op> {
  defm "_A32" :
    ATOMIC_I<(outs rc:$dst),
             (ins P2Align:$p2align, offset32_op:$off, I32:$addr, rc:$val),
             (outs), (ins P2Align:$p2align, offset32_op:$off), [],
             !strconcat(name, "\t$dst, ${off}(${addr})${p2align}, $val"),
             !strconcat(name, "\t${off}${p2align}"), atomic_op, false>;
  defm "_A64" :
    ATOMIC_I<(outs rc:$dst),
             (ins P2Align:$p2align, offset64_op:$off, I64:$addr, rc:$val),
             (outs), (ins P2Align:$p2align, offset64_op:$off), [],
             !strconcat(name, "\t$dst, ${off}(${addr})${p2align}, $val"),
             !strconcat(name, "\t${off}${p2align}"), atomic_op, true>;
}

defm ATOMIC_RMW_ADD_I32 : WebAssemblyBinRMW<I32, "i32.atomic.rmw.add", 0x1e>;
defm ATOMIC_RMW_ADD_I64 : WebAssemblyBinRMW<I64, "i64.atomic.rmw.add", 0x1f>;
defm ATOMIC_RMW8_U_ADD_I32 :
  WebAssemblyBinRMW<I32, "i32.atomic.rmw8.add_u", 0x20>;
defm ATOMIC_RMW16_U_ADD_I32 :
  WebAssemblyBinRMW<I32, "i32.atomic.rmw16.add_u", 0x21>;
defm ATOMIC_RMW8_U_ADD_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw8.add_u", 0x22>;
defm ATOMIC_RMW16_U_ADD_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw16.add_u", 0x23>;
defm ATOMIC_RMW32_U_ADD_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw32.add_u", 0x24>;

defm ATOMIC_RMW_SUB_I32 : WebAssemblyBinRMW<I32, "i32.atomic.rmw.sub", 0x25>;
defm ATOMIC_RMW_SUB_I64 : WebAssemblyBinRMW<I64, "i64.atomic.rmw.sub", 0x26>;
defm ATOMIC_RMW8_U_SUB_I32 :
  WebAssemblyBinRMW<I32, "i32.atomic.rmw8.sub_u", 0x27>;
defm ATOMIC_RMW16_U_SUB_I32 :
  WebAssemblyBinRMW<I32, "i32.atomic.rmw16.sub_u", 0x28>;
defm ATOMIC_RMW8_U_SUB_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw8.sub_u", 0x29>;
defm ATOMIC_RMW16_U_SUB_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw16.sub_u", 0x2a>;
defm ATOMIC_RMW32_U_SUB_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw32.sub_u", 0x2b>;

defm ATOMIC_RMW_AND_I32 : WebAssemblyBinRMW<I32, "i32.atomic.rmw.and", 0x2c>;
defm ATOMIC_RMW_AND_I64 : WebAssemblyBinRMW<I64, "i64.atomic.rmw.and", 0x2d>;
defm ATOMIC_RMW8_U_AND_I32 :
  WebAssemblyBinRMW<I32, "i32.atomic.rmw8.and_u", 0x2e>;
defm ATOMIC_RMW16_U_AND_I32 :
  WebAssemblyBinRMW<I32, "i32.atomic.rmw16.and_u", 0x2f>;
defm ATOMIC_RMW8_U_AND_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw8.and_u", 0x30>;
defm ATOMIC_RMW16_U_AND_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw16.and_u", 0x31>;
defm ATOMIC_RMW32_U_AND_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw32.and_u", 0x32>;

defm ATOMIC_RMW_OR_I32 : WebAssemblyBinRMW<I32, "i32.atomic.rmw.or", 0x33>;
defm ATOMIC_RMW_OR_I64 : WebAssemblyBinRMW<I64, "i64.atomic.rmw.or", 0x34>;
defm ATOMIC_RMW8_U_OR_I32 :
  WebAssemblyBinRMW<I32, "i32.atomic.rmw8.or_u", 0x35>;
defm ATOMIC_RMW16_U_OR_I32 :
  WebAssemblyBinRMW<I32, "i32.atomic.rmw16.or_u", 0x36>;
defm ATOMIC_RMW8_U_OR_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw8.or_u", 0x37>;
defm ATOMIC_RMW16_U_OR_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw16.or_u", 0x38>;
defm ATOMIC_RMW32_U_OR_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw32.or_u", 0x39>;

defm ATOMIC_RMW_XOR_I32 : WebAssemblyBinRMW<I32, "i32.atomic.rmw.xor", 0x3a>;
defm ATOMIC_RMW_XOR_I64 : WebAssemblyBinRMW<I64, "i64.atomic.rmw.xor", 0x3b>;
defm ATOMIC_RMW8_U_XOR_I32 :
  WebAssemblyBinRMW<I32, "i32.atomic.rmw8.xor_u", 0x3c>;
defm ATOMIC_RMW16_U_XOR_I32 :
  WebAssemblyBinRMW<I32, "i32.atomic.rmw16.xor_u", 0x3d>;
defm ATOMIC_RMW8_U_XOR_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw8.xor_u", 0x3e>;
defm ATOMIC_RMW16_U_XOR_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw16.xor_u", 0x3f>;
defm ATOMIC_RMW32_U_XOR_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw32.xor_u", 0x40>;

defm ATOMIC_RMW_XCHG_I32 :
  WebAssemblyBinRMW<I32, "i32.atomic.rmw.xchg", 0x41>;
defm ATOMIC_RMW_XCHG_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw.xchg", 0x42>;
defm ATOMIC_RMW8_U_XCHG_I32 :
  WebAssemblyBinRMW<I32, "i32.atomic.rmw8.xchg_u", 0x43>;
defm ATOMIC_RMW16_U_XCHG_I32 :
  WebAssemblyBinRMW<I32, "i32.atomic.rmw16.xchg_u", 0x44>;
defm ATOMIC_RMW8_U_XCHG_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw8.xchg_u", 0x45>;
defm ATOMIC_RMW16_U_XCHG_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw16.xchg_u", 0x46>;
defm ATOMIC_RMW32_U_XCHG_I64 :
  WebAssemblyBinRMW<I64, "i64.atomic.rmw32.xchg_u", 0x47>;

multiclass BinRMWPat<ValueType ty, PatFrag kind, string inst> {
  def : Pat<(ty (kind (AddrOps32 offset32_op:$offset, I32:$addr), ty:$val)),
            (!cast<NI>(inst#_A32) 0, $offset, $addr, $val)>,
        Requires<[HasAddr32, HasAtomics]>;
  def : Pat<(ty (kind (AddrOps64 offset64_op:$offset, I64:$addr), ty:$val)),
            (!cast<NI>(inst#_A64) 0, $offset, $addr, $val)>,
        Requires<[HasAddr64, HasAtomics]>;
}

// Patterns for various addressing modes.
multiclass BinRMWPattern<PatFrag rmw_32, PatFrag rmw_64, string inst_32,
                         string inst_64> {
  defm : BinRMWPat<i32, rmw_32, inst_32>;
  defm : BinRMWPat<i64, rmw_64, inst_64>;
}

defm : BinRMWPattern<atomic_load_add_i32, atomic_load_add_i64,
                     "ATOMIC_RMW_ADD_I32", "ATOMIC_RMW_ADD_I64">;
defm : BinRMWPattern<atomic_load_sub_i32, atomic_load_sub_i64,
                     "ATOMIC_RMW_SUB_I32", "ATOMIC_RMW_SUB_I64">;
defm : BinRMWPattern<atomic_load_and_i32, atomic_load_and_i64,
                     "ATOMIC_RMW_AND_I32", "ATOMIC_RMW_AND_I64">;
defm : BinRMWPattern<atomic_load_or_i32, atomic_load_or_i64,
                     "ATOMIC_RMW_OR_I32", "ATOMIC_RMW_OR_I64">;
defm : BinRMWPattern<atomic_load_xor_i32, atomic_load_xor_i64,
                     "ATOMIC_RMW_XOR_I32", "ATOMIC_RMW_XOR_I64">;
defm : BinRMWPattern<atomic_swap_i32, atomic_swap_i64,
                     "ATOMIC_RMW_XCHG_I32", "ATOMIC_RMW_XCHG_I64">;

// Truncating & zero-extending binary RMW patterns.
// These are combined patterns of truncating store patterns and zero-extending
// load patterns above.
class zext_bin_rmw_8_32<PatFrag kind> :
  PatFrag<(ops node:$addr, node:$val), (i32 (kind node:$addr, node:$val))>;
class zext_bin_rmw_16_32<PatFrag kind> : zext_bin_rmw_8_32<kind>;
class zext_bin_rmw_8_64<PatFrag kind> :
  PatFrag<(ops node:$addr, node:$val),
          (zext (i32 (kind node:$addr, (i32 (trunc (i64 node:$val))))))>;
class zext_bin_rmw_16_64<PatFrag kind> : zext_bin_rmw_8_64<kind>;
class zext_bin_rmw_32_64<PatFrag kind> : zext_bin_rmw_8_64<kind>;

// Truncating & sign-extending binary RMW patterns.
// These are combined patterns of truncating store patterns and sign-extending
// load patterns above. We match subword RMWs (for 32-bit) and anyext RMWs (for
// 64-bit) and select a zext RMW; the next instruction will be sext_inreg which
// is selected by itself.
class sext_bin_rmw_8_32<PatFrag kind> :
  PatFrag<(ops node:$addr, node:$val), (kind node:$addr, node:$val)>;
class sext_bin_rmw_16_32<PatFrag kind> : sext_bin_rmw_8_32<kind>;
class sext_bin_rmw_8_64<PatFrag kind> :
  PatFrag<(ops node:$addr, node:$val),
          (anyext (i32 (kind node:$addr, (i32 (trunc (i64 node:$val))))))>;
class sext_bin_rmw_16_64<PatFrag kind> : sext_bin_rmw_8_64<kind>;
// 32->64 sext RMW gets selected as i32.atomic.rmw.***, i64.extend_i32_s

// Patterns for various addressing modes for truncating-extending binary RMWs.
multiclass BinRMWTruncExtPattern<
  PatFrag rmw_8, PatFrag rmw_16, PatFrag rmw_32,
  string inst8_32, string inst16_32, string inst8_64, string inst16_64, string inst32_64> {
  // Truncating-extending binary RMWs
  defm : BinRMWPat<i32, zext_bin_rmw_8_32<rmw_8>, inst8_32>;
  defm : BinRMWPat<i32, zext_bin_rmw_16_32<rmw_16>, inst16_32>;
  defm : BinRMWPat<i64, zext_bin_rmw_8_64<rmw_8>, inst8_64>;
  defm : BinRMWPat<i64, zext_bin_rmw_16_64<rmw_16>, inst16_64>;
  defm : BinRMWPat<i64, zext_bin_rmw_32_64<rmw_32>, inst32_64>;

  defm : BinRMWPat<i32, sext_bin_rmw_8_32<rmw_8>, inst8_32>;
  defm : BinRMWPat<i32, sext_bin_rmw_16_32<rmw_16>, inst16_32>;
  defm : BinRMWPat<i64, sext_bin_rmw_8_64<rmw_8>, inst8_64>;
  defm : BinRMWPat<i64, sext_bin_rmw_16_64<rmw_16>, inst16_64>;
}

defm : BinRMWTruncExtPattern<
  atomic_load_add_i8, atomic_load_add_i16, atomic_load_add_i32,
  "ATOMIC_RMW8_U_ADD_I32", "ATOMIC_RMW16_U_ADD_I32",
  "ATOMIC_RMW8_U_ADD_I64", "ATOMIC_RMW16_U_ADD_I64", "ATOMIC_RMW32_U_ADD_I64">;
defm : BinRMWTruncExtPattern<
  atomic_load_sub_i8, atomic_load_sub_i16, atomic_load_sub_i32,
  "ATOMIC_RMW8_U_SUB_I32", "ATOMIC_RMW16_U_SUB_I32",
  "ATOMIC_RMW8_U_SUB_I64", "ATOMIC_RMW16_U_SUB_I64", "ATOMIC_RMW32_U_SUB_I64">;
defm : BinRMWTruncExtPattern<
  atomic_load_and_i8, atomic_load_and_i16, atomic_load_and_i32,
  "ATOMIC_RMW8_U_AND_I32", "ATOMIC_RMW16_U_AND_I32",
  "ATOMIC_RMW8_U_AND_I64", "ATOMIC_RMW16_U_AND_I64", "ATOMIC_RMW32_U_AND_I64">;
defm : BinRMWTruncExtPattern<
  atomic_load_or_i8, atomic_load_or_i16, atomic_load_or_i32,
  "ATOMIC_RMW8_U_OR_I32", "ATOMIC_RMW16_U_OR_I32",
  "ATOMIC_RMW8_U_OR_I64", "ATOMIC_RMW16_U_OR_I64", "ATOMIC_RMW32_U_OR_I64">;
defm : BinRMWTruncExtPattern<
  atomic_load_xor_i8, atomic_load_xor_i16, atomic_load_xor_i32,
  "ATOMIC_RMW8_U_XOR_I32", "ATOMIC_RMW16_U_XOR_I32",
  "ATOMIC_RMW8_U_XOR_I64", "ATOMIC_RMW16_U_XOR_I64", "ATOMIC_RMW32_U_XOR_I64">;
defm : BinRMWTruncExtPattern<
  atomic_swap_i8, atomic_swap_i16, atomic_swap_i32,
  "ATOMIC_RMW8_U_XCHG_I32", "ATOMIC_RMW16_U_XCHG_I32",
  "ATOMIC_RMW8_U_XCHG_I64", "ATOMIC_RMW16_U_XCHG_I64",
  "ATOMIC_RMW32_U_XCHG_I64">;

//===----------------------------------------------------------------------===//
// Atomic ternary read-modify-writes
//===----------------------------------------------------------------------===//

// TODO LLVM IR's cmpxchg instruction returns a pair of {loaded value, success
// flag}. When we use the success flag or both values, we can't make use of i64
// truncate/extend versions of instructions for now, which is suboptimal.
// Consider adding a pass after instruction selection that optimizes this case
// if it is frequent.

multiclass WebAssemblyTerRMW<WebAssemblyRegClass rc, string name,
                             int atomic_op> {
  defm "_A32" :
    ATOMIC_I<(outs rc:$dst),
             (ins P2Align:$p2align, offset32_op:$off, I32:$addr, rc:$exp,
                  rc:$new_),
             (outs), (ins P2Align:$p2align, offset32_op:$off), [],
             !strconcat(name, "\t$dst, ${off}(${addr})${p2align}, $exp, $new_"),
             !strconcat(name, "\t${off}${p2align}"), atomic_op, false>;
  defm "_A64" :
    ATOMIC_I<(outs rc:$dst),
             (ins P2Align:$p2align, offset64_op:$off, I64:$addr, rc:$exp,
                  rc:$new_),
             (outs), (ins P2Align:$p2align, offset64_op:$off), [],
             !strconcat(name, "\t$dst, ${off}(${addr})${p2align}, $exp, $new_"),
             !strconcat(name, "\t${off}${p2align}"), atomic_op, true>;
}

defm ATOMIC_RMW_CMPXCHG_I32 :
  WebAssemblyTerRMW<I32, "i32.atomic.rmw.cmpxchg", 0x48>;
defm ATOMIC_RMW_CMPXCHG_I64 :
  WebAssemblyTerRMW<I64, "i64.atomic.rmw.cmpxchg", 0x49>;
defm ATOMIC_RMW8_U_CMPXCHG_I32 :
  WebAssemblyTerRMW<I32, "i32.atomic.rmw8.cmpxchg_u", 0x4a>;
defm ATOMIC_RMW16_U_CMPXCHG_I32 :
  WebAssemblyTerRMW<I32, "i32.atomic.rmw16.cmpxchg_u", 0x4b>;
defm ATOMIC_RMW8_U_CMPXCHG_I64 :
  WebAssemblyTerRMW<I64, "i64.atomic.rmw8.cmpxchg_u", 0x4c>;
defm ATOMIC_RMW16_U_CMPXCHG_I64 :
  WebAssemblyTerRMW<I64, "i64.atomic.rmw16.cmpxchg_u", 0x4d>;
defm ATOMIC_RMW32_U_CMPXCHG_I64 :
  WebAssemblyTerRMW<I64, "i64.atomic.rmw32.cmpxchg_u", 0x4e>;

multiclass TerRMWPat<ValueType ty, PatFrag kind, string inst> {
  def : Pat<(ty (kind (AddrOps32 offset32_op:$offset, I32:$addr), ty:$exp, ty:$new)),
            (!cast<NI>(inst#_A32) 0, $offset, $addr, $exp, $new)>,
        Requires<[HasAddr32, HasAtomics]>;
  def : Pat<(ty (kind (AddrOps64 offset64_op:$offset, I64:$addr), ty:$exp, ty:$new)),
            (!cast<NI>(inst#_A64) 0, $offset, $addr, $exp, $new)>,
        Requires<[HasAddr64, HasAtomics]>;
}

defm : TerRMWPat<i32, atomic_cmp_swap_i32, "ATOMIC_RMW_CMPXCHG_I32">;
defm : TerRMWPat<i64, atomic_cmp_swap_i64, "ATOMIC_RMW_CMPXCHG_I64">;

// Truncating & zero-extending ternary RMW patterns.
// DAG legalization & optimization before instruction selection may introduce
// additional nodes such as anyext or assertzext depending on operand types.
class zext_ter_rmw_8_32<PatFrag kind> :
  PatFrag<(ops node:$addr, node:$exp, node:$new),
          (i32 (kind node:$addr, node:$exp, node:$new))>;
class zext_ter_rmw_16_32<PatFrag kind> : zext_ter_rmw_8_32<kind>;
class zext_ter_rmw_8_64<PatFrag kind> :
  PatFrag<(ops node:$addr, node:$exp, node:$new),
          (zext (i32 (assertzext (i32 (kind node:$addr,
                                            (i32 (trunc (i64 node:$exp))),
                                            (i32 (trunc (i64 node:$new))))))))>;
class zext_ter_rmw_16_64<PatFrag kind> : zext_ter_rmw_8_64<kind>;
class zext_ter_rmw_32_64<PatFrag kind> :
  PatFrag<(ops node:$addr, node:$exp, node:$new),
          (zext (i32 (kind node:$addr,
                           (i32 (trunc (i64 node:$exp))),
                           (i32 (trunc (i64 node:$new))))))>;

// Truncating & sign-extending ternary RMW patterns.
// We match subword RMWs (for 32-bit) and anyext RMWs (for 64-bit) and select a
// zext RMW; the next instruction will be sext_inreg which is selected by
// itself.
class sext_ter_rmw_8_32<PatFrag kind> :
  PatFrag<(ops node:$addr, node:$exp, node:$new),
          (kind node:$addr, node:$exp, node:$new)>;
class sext_ter_rmw_16_32<PatFrag kind> : sext_ter_rmw_8_32<kind>;
class sext_ter_rmw_8_64<PatFrag kind> :
  PatFrag<(ops node:$addr, node:$exp, node:$new),
          (anyext (i32 (assertzext (i32
            (kind node:$addr,
                  (i32 (trunc (i64 node:$exp))),
                  (i32 (trunc (i64 node:$new))))))))>;
class sext_ter_rmw_16_64<PatFrag kind> : sext_ter_rmw_8_64<kind>;
// 32->64 sext RMW gets selected as i32.atomic.rmw.***, i64.extend_i32_s

defm : TerRMWPat<i32, zext_ter_rmw_8_32<atomic_cmp_swap_i8>, "ATOMIC_RMW8_U_CMPXCHG_I32">;
defm : TerRMWPat<i32, zext_ter_rmw_16_32<atomic_cmp_swap_i16>, "ATOMIC_RMW16_U_CMPXCHG_I32">;
defm : TerRMWPat<i64, zext_ter_rmw_8_64<atomic_cmp_swap_i8>, "ATOMIC_RMW8_U_CMPXCHG_I64">;
defm : TerRMWPat<i64, zext_ter_rmw_16_64<atomic_cmp_swap_i16>, "ATOMIC_RMW16_U_CMPXCHG_I64">;
defm : TerRMWPat<i64, zext_ter_rmw_32_64<atomic_cmp_swap_i32>, "ATOMIC_RMW32_U_CMPXCHG_I64">;

defm : TerRMWPat<i32, sext_ter_rmw_8_32<atomic_cmp_swap_i8>, "ATOMIC_RMW8_U_CMPXCHG_I32">;
defm : TerRMWPat<i32, sext_ter_rmw_16_32<atomic_cmp_swap_i16>, "ATOMIC_RMW16_U_CMPXCHG_I32">;
defm : TerRMWPat<i64, sext_ter_rmw_8_64<atomic_cmp_swap_i8>, "ATOMIC_RMW8_U_CMPXCHG_I64">;
defm : TerRMWPat<i64, sext_ter_rmw_16_64<atomic_cmp_swap_i16>, "ATOMIC_RMW16_U_CMPXCHG_I64">;
