//===-- R600InstrInfo.td - R600 DAG nodes ------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains DAG node definitions for the R600 target.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// R600 DAG Nodes
//

// Force dependencies for vector trunc stores
def R600dummy_chain : SDNode<"AMDGPUISD::DUMMY_CHAIN", SDTNone, [SDNPHasChain]>;

def R600ExportOp : SDTypeProfile<0, 7, [SDTCisFP<0>, SDTCisInt<1>]>;

def R600_EXPORT: SDNode<"AMDGPUISD::R600_EXPORT", R600ExportOp,
  [SDNPHasChain, SDNPSideEffect]>;
