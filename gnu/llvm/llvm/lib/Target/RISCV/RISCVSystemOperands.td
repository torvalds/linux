//===- RISCVSystemOperands.td ------------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the symbolic operands permitted for various kinds of
// RISC-V system instruction.
//
//===----------------------------------------------------------------------===//

include "llvm/TableGen/SearchableTable.td"

//===----------------------------------------------------------------------===//
// CSR (control and status register read/write) instruction options.
//===----------------------------------------------------------------------===//

class SysReg<string name, bits<12> op> {
  string Name = name;
  // A maximum of one alias is supported right now.
  string AltName = name;
  // A maximum of one deprecated name is supported right now.  Unlike the
  // `AltName` alias, a `DeprecatedName` generates a diagnostic when the name is
  // used to encourage software to migrate away from the name.
  string DeprecatedName = "";
  bits<12> Encoding = op;
  // FIXME: add these additional fields when needed.
  // Privilege Access: Read and Write = 0, 1, 2; Read-Only = 3.
  // Privilege Mode: User = 0, System = 1 or Machine = 3.
  // bits<2> ReadWrite = op{11 - 10};
  // bits<2> XMode = op{9 - 8};
  // Check Extra field name and what bits 7-6 correspond to.
  // bits<2> Extra = op{7 - 6};
  // Register number without the privilege bits.
  // bits<6> Number = op{5 - 0};
  code FeaturesRequired = [{ {} }];
  bit isRV32Only = 0;
}

def SysRegsList : GenericTable {
  let FilterClass = "SysReg";
  // FIXME: add "ReadWrite", "Mode", "Extra", "Number" fields when needed.
  let Fields = [
    "Name", "AltName", "DeprecatedName", "Encoding", "FeaturesRequired",
    "isRV32Only",
  ];

  let PrimaryKey = [ "Encoding" ];
  let PrimaryKeyName = "lookupSysRegByEncoding";
  let PrimaryKeyReturnRange = true;
}

def lookupSysRegByName : SearchIndex {
  let Table = SysRegsList;
  let Key = [ "Name" ];
}

def lookupSysRegByAltName : SearchIndex {
  let Table = SysRegsList;
  let Key = [ "AltName" ];
}

def lookupSysRegByDeprecatedName : SearchIndex {
  let Table = SysRegsList;
  let Key = [ "DeprecatedName" ];
}

// The following CSR encodings match those given in Tables 2.2,
// 2.3, 2.4, 2.5 and 2.6 in the RISC-V Instruction Set Manual
// Volume II: Privileged Architecture.

//===----------------------------------------------------------------------===//
// User Floating-Point CSRs
//===----------------------------------------------------------------------===//

def SysRegFFLAGS : SysReg<"fflags", 0x001>;
def SysRegFRM    : SysReg<"frm", 0x002>;
def SysRegFCSR   : SysReg<"fcsr", 0x003>;

//===----------------------------------------------------------------------===//
// User Counter/Timers
//===----------------------------------------------------------------------===//
def CYCLE   : SysReg<"cycle", 0xC00>;
def TIME    : SysReg<"time", 0xC01>;
def INSTRET : SysReg<"instret", 0xC02>;

// hpmcounter3-hpmcounter31 at 0xC03-0xC1F.
foreach i = 3...31 in
  def : SysReg<"hpmcounter"#i, !add(0xC03, !sub(i, 3))>;

let isRV32Only = 1 in {
def CYCLEH   : SysReg<"cycleh", 0xC80>;
def TIMEH    : SysReg<"timeh", 0xC81>;
def INSTRETH : SysReg<"instreth", 0xC82>;

// hpmcounter3h-hpmcounter31h at 0xC83-0xC9F.
foreach i = 3...31 in
  def : SysReg<"hpmcounter"#i#"h", !add(0xC83, !sub(i, 3))>;
}

//===----------------------------------------------------------------------===//
// Supervisor Trap Setup
//===----------------------------------------------------------------------===//
def : SysReg<"sstatus", 0x100>;
def : SysReg<"sie", 0x104>;
def : SysReg<"stvec", 0x105>;
def : SysReg<"scounteren", 0x106>;
def : SysReg<"stimecmp", 0x14D>;
let isRV32Only = 1 in
def : SysReg<"stimecmph", 0x15D>;

//===----------------------------------------------------------------------===//
// Supervisor Configuration
//===----------------------------------------------------------------------===//

def : SysReg<"senvcfg", 0x10A>;

//===----------------------------------------------------------------------===//
// Supervisor Trap Handling
//===----------------------------------------------------------------------===//
def : SysReg<"sscratch", 0x140>;
def : SysReg<"sepc", 0x141>;
def : SysReg<"scause", 0x142>;
let DeprecatedName = "sbadaddr" in
def : SysReg<"stval", 0x143>;
def : SysReg<"sip", 0x144>;

//===----------------------------------------------------------------------===//
// Supervisor Protection and Translation
//===----------------------------------------------------------------------===//
let DeprecatedName = "sptbr" in
def : SysReg<"satp", 0x180>;

//===----------------------------------------------------------------------===//
// Quality-of-Service(QoS) Identifiers (Ssqosid)
//===----------------------------------------------------------------------===//
def : SysReg<"srmcfg", 0x181>;

//===----------------------------------------------------------------------===//
// Debug/Trace Registers
//===----------------------------------------------------------------------===//

def : SysReg<"scontext", 0x5A8>;

//===----------------------------------------------------------------------===//
// Supervisor Count Overflow (defined in Sscofpmf)
//===----------------------------------------------------------------------===//

def : SysReg<"scountovf", 0xDA0>;

//===----------------------------------------------------------------------===//
// Hypervisor Trap Setup
//===----------------------------------------------------------------------===//

def : SysReg<"hstatus", 0x600>;
def : SysReg<"hedeleg", 0x602>;
def : SysReg<"hideleg", 0x603>;
def : SysReg<"hie", 0x604>;
def : SysReg<"hcounteren", 0x606>;
def : SysReg<"hgeie", 0x607>;

//===----------------------------------------------------------------------===//
// Hypervisor Trap Handling
//===----------------------------------------------------------------------===//

def : SysReg<"htval", 0x643>;
def : SysReg<"hip", 0x644>;
def : SysReg<"hvip", 0x645>;
def : SysReg<"htinst", 0x64A>;
def : SysReg<"hgeip", 0xE12>;

//===----------------------------------------------------------------------===//
// Hypervisor Configuration
//===----------------------------------------------------------------------===//

def : SysReg<"henvcfg", 0x60A>;
let isRV32Only = 1 in
def : SysReg<"henvcfgh", 0x61A>;

//===----------------------------------------------------------------------===//
// Hypervisor Protection and Translation
//===----------------------------------------------------------------------===//

def : SysReg<"hgatp", 0x680>;

//===----------------------------------------------------------------------===//
// Debug/Trace Registers
//===----------------------------------------------------------------------===//

def : SysReg<"hcontext", 0x6A8>;

//===----------------------------------------------------------------------===//
// Hypervisor Counter/Timer Virtualization Registers
//===----------------------------------------------------------------------===//

def : SysReg<"htimedelta", 0x605>;
let isRV32Only = 1 in
def : SysReg<"htimedeltah", 0x615>;

//===----------------------------------------------------------------------===//
// Virtual Supervisor Registers
//===----------------------------------------------------------------------===//

def : SysReg<"vsstatus", 0x200>;
def : SysReg<"vsie", 0x204>;
def : SysReg<"vstvec", 0x205>;
def : SysReg<"vsscratch", 0x240>;
def : SysReg<"vsepc", 0x241>;
def : SysReg<"vscause", 0x242>;
def : SysReg<"vstval", 0x243>;
def : SysReg<"vsip", 0x244>;
def : SysReg<"vstimecmp", 0x24D>;
let isRV32Only = 1 in
def : SysReg<"vstimecmph", 0x25D>;
def : SysReg<"vsatp", 0x280>;

//===----------------------------------------------------------------------===//
// Machine Information Registers
//===----------------------------------------------------------------------===//

def : SysReg<"mvendorid", 0xF11>;
def : SysReg<"marchid", 0xF12>;
def : SysReg<"mimpid", 0xF13>;
def : SysReg<"mhartid", 0xF14>;
def : SysReg<"mconfigptr", 0xF15>;

//===----------------------------------------------------------------------===//
// Machine Trap Setup
//===----------------------------------------------------------------------===//
def : SysReg<"mstatus", 0x300>;
def : SysReg<"misa", 0x301>;
def : SysReg<"medeleg", 0x302>;
def : SysReg<"mideleg", 0x303>;
def : SysReg<"mie", 0x304>;
def : SysReg<"mtvec", 0x305>;
def : SysReg<"mcounteren", 0x306>;
let isRV32Only = 1 in
def : SysReg<"mstatush", 0x310>;

//===----------------------------------------------------------------------===//
// Machine Trap Handling
//===----------------------------------------------------------------------===//
def : SysReg<"mscratch", 0x340>;
def : SysReg<"mepc", 0x341>;
def : SysReg<"mcause", 0x342>;
let DeprecatedName = "mbadaddr" in
def : SysReg<"mtval", 0x343>;
def : SysReg<"mip", 0x344>;
def : SysReg<"mtinst", 0x34A>;
def : SysReg<"mtval2", 0x34B>;

//===----------------------------------------------------------------------===//
// Machine Configuration
//===----------------------------------------------------------------------===//

def : SysReg<"menvcfg", 0x30A>;
let isRV32Only = 1 in
def : SysReg<"menvcfgh", 0x31A>;
def : SysReg<"mseccfg", 0x747>;
let isRV32Only = 1 in
def : SysReg<"mseccfgh", 0x757>;

//===----------------------------------------------------------------------===//
// Machine Protection and Translation
//===----------------------------------------------------------------------===//

// pmpcfg0-pmpcfg15 at 0x3A0-0x3AF. Odd-numbered registers are RV32-only.
foreach i = 0...15 in {
  let isRV32Only = !and(i, 1) in
  def : SysReg<"pmpcfg"#i, !add(0x3A0, i)>;
}

// pmpaddr0-pmpaddr63 at 0x3B0-0x3EF.
foreach i = 0...63 in
  def : SysReg<"pmpaddr"#i, !add(0x3B0, i)>;

//===----------------------------------------------------------------------===//
// Machine Counter and Timers
//===----------------------------------------------------------------------===//
def : SysReg<"mcycle", 0xB00>;
def : SysReg<"minstret", 0xB02>;

// mhpmcounter3-mhpmcounter31 at 0xB03-0xB1F.
foreach i = 3...31 in
  def : SysReg<"mhpmcounter"#i, !add(0xB03, !sub(i, 3))>;

let isRV32Only = 1 in {
def: SysReg<"mcycleh", 0xB80>;
def: SysReg<"minstreth", 0xB82>;

// mhpmcounter3h-mhpmcounter31h at 0xB83-0xB9F.
foreach i = 3...31 in
  def : SysReg<"mhpmcounter"#i#"h", !add(0xB83, !sub(i, 3))>;
}

//===----------------------------------------------------------------------===//
// Machine Counter Setup
//===----------------------------------------------------------------------===//
let AltName = "mucounteren" in // Privileged spec v1.9.1 Name
def : SysReg<"mcountinhibit", 0x320>;

// mhpmevent3-mhpmevent31 at 0x323-0x33F.
foreach i = 3...31 in
  def : SysReg<"mhpmevent"#i, !add(0x323, !sub(i, 3))>;

// mhpmevent3h-mhpmevent31h at 0x723-0x73F
foreach i = 3...31 in {
  let isRV32Only = 1 in
  def : SysReg<"mhpmevent"#i#"h", !add(0x723, !sub(i, 3))>;
}

//===----------------------------------------------------------------------===//
// Supervisor Counter Setup
//===----------------------------------------------------------------------===//
def : SysReg<"scountinhibit", 0x120>;

//===----------------------------------------------------------------------===//
// Debug/ Trace Registers (shared with Debug Mode)
//===----------------------------------------------------------------------===//
def : SysReg<"tselect", 0x7A0>;
def : SysReg<"tdata1", 0x7A1>;
def : SysReg<"tdata2", 0x7A2>;
def : SysReg<"tdata3", 0x7A3>;
def : SysReg<"mcontext", 0x7A8>;

//===----------------------------------------------------------------------===//
// Debug Mode Registers
//===----------------------------------------------------------------------===//
def : SysReg<"dcsr", 0x7B0>;
def : SysReg<"dpc", 0x7B1>;

// "dscratch" is an alternative name for "dscratch0" which appeared in earlier
// drafts of the RISC-V debug spec
let AltName = "dscratch" in
def : SysReg<"dscratch0", 0x7B2>;
def : SysReg<"dscratch1", 0x7B3>;

//===----------------------------------------------------------------------===//
// User Vector CSRs
//===----------------------------------------------------------------------===//
def : SysReg<"vstart", 0x008>;
def : SysReg<"vxsat", 0x009>;
def SysRegVXRM : SysReg<"vxrm", 0x00A>;
def : SysReg<"vcsr", 0x00F>;
def SysRegVL : SysReg<"vl", 0xC20>;
def : SysReg<"vtype", 0xC21>;
def SysRegVLENB: SysReg<"vlenb", 0xC22>;

//===----------------------------------------------------------------------===//
// Shadow Stack CSR
//===----------------------------------------------------------------------===//
def : SysReg<"ssp", 0x011>;

//===----------------------------------------------------------------------===//
// State Enable Extension (Smstateen)
//===----------------------------------------------------------------------===//

// sstateen0-sstateen3 at 0x10C-0x10F, mstateen0-mstateen3 at 0x30C-0x30F,
// mstateen0h-mstateen3h at 0x31C-0x31F, hstateen0-hstateen3 at 0x60C-0x60F,
// and hstateen0h-hstateen3h at 0x61C-0x61F.
foreach i = 0...3 in {
  def : SysReg<"sstateen"#i, !add(0x10C, i)>;
  def : SysReg<"mstateen"#i, !add(0x30C, i)>;
  let isRV32Only = 1 in
  def : SysReg<"mstateen"#i#"h", !add(0x31C, i)>;
  def : SysReg<"hstateen"#i, !add(0x60C, i)>;
  let isRV32Only = 1 in
  def : SysReg<"hstateen"#i#"h", !add(0x61C, i)>;
}

//===-----------------------------------------------
// Entropy Source CSR
//===-----------------------------------------------

def SEED : SysReg<"seed", 0x015>;

//===-----------------------------------------------
// Advanced Interrupt Architecture
//===-----------------------------------------------

// Machine-level CSRs
def : SysReg<"miselect", 0x350>;
def : SysReg<"mireg", 0x351>;
foreach i = 2...3 in {
  def : SysReg<"mireg"#i, !add(0x350, i)>;
}
foreach i = 4...6 in {
  def : SysReg<"mireg"#i, !add(0x351, i)>;
}
def : SysReg<"mtopei", 0x35C>;
def : SysReg<"mtopi", 0xFB0>;
def : SysReg<"mvien", 0x308>;
def : SysReg<"mvip", 0x309>;
let isRV32Only = 1 in {
def : SysReg<"midelegh", 0x313>;
def : SysReg<"mieh", 0x314>;
def : SysReg<"mvienh", 0x318>;
def : SysReg<"mviph", 0x319>;
def : SysReg<"miph", 0x354>;
} // isRV32Only

// Supervisor-level CSRs
def : SysReg<"siselect", 0x150>;
def : SysReg<"sireg", 0x151>;
foreach i = 2...3 in {
  def : SysReg<"sireg"#i, !add(0x150, i)>;
}
foreach i = 4...6 in {
  def : SysReg<"sireg"#i, !add(0x151, i)>;
}
def : SysReg<"stopei", 0x15C>;
def : SysReg<"stopi", 0xDB0>;
let isRV32Only = 1 in {
def : SysReg<"sieh", 0x114>;
def : SysReg<"siph", 0x154>;
} // isRV32Only

// Hypervisor and VS CSRs
def : SysReg<"hvien", 0x608>;
def : SysReg<"hvictl", 0x609>;
def : SysReg<"hviprio1", 0x646>;
def : SysReg<"hviprio2", 0x647>;
def : SysReg<"vsiselect", 0x250>;
def : SysReg<"vsireg", 0x251>;
foreach i = 2...3 in {
  def : SysReg<"vsireg"#i, !add(0x250, i)>;
}
foreach i = 4...6 in {
  def : SysReg<"vsireg"#i, !add(0x251, i)>;
}
def : SysReg<"vstopei", 0x25C>;
def : SysReg<"vstopi", 0xEB0>;
let isRV32Only = 1 in {
def : SysReg<"hidelegh", 0x613>;
def : SysReg<"hvienh", 0x618>;
def : SysReg<"hviph", 0x655>;
def : SysReg<"hviprio1h", 0x656>;
def : SysReg<"hviprio2h", 0x657>;
def : SysReg<"vsieh", 0x214>;
def : SysReg<"vsiph", 0x254>;
} // isRV32Only

//===-----------------------------------------------
// Jump Vector Table CSR
//===-----------------------------------------------

def : SysReg<"jvt", 0x017>;

//===-----------------------------------------------
// Resumable Non-Maskable Interrupts(Smrnmi) CSRs
//===-----------------------------------------------
def : SysReg<"mnscratch", 0x740>;
def : SysReg<"mnepc", 0x741>;
def : SysReg<"mncause", 0x742>;
def : SysReg<"mnstatus", 0x744>;
