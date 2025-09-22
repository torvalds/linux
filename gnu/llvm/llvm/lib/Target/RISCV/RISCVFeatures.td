//===-- RISCVFeatures.td - RISC-V Features and Extensions --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// RISC-V subtarget features and instruction predicates.
//===----------------------------------------------------------------------===//

// Subclass of SubtargetFeature to be used when the feature is also a RISC-V
// extension. Extensions have a version and may be experimental.
//
// name      - Name of the extension in lower case.
// major     - Major version of extension.
// minor     - Minor version of extension.
// desc      - Description of extension.
// implies   - Extensions or features implied by this extension.
// fieldname - name of field to create in RISCVSubtarget. By default replaces
//             uses the record name by replacing Feature with Has.
// value     - Value to assign to the field in RISCVSubtarget when this
//             extension is enabled. Usually "true", but can be changed.
class RISCVExtension<string name, int major, int minor, string desc,
                     list<SubtargetFeature> implies = [],
                     string fieldname = !subst("Feature", "Has", NAME),
                     string value = "true">
    : SubtargetFeature<name, fieldname, value, desc, implies> {
  // MajorVersion - The major version for this extension.
  int MajorVersion = major;

  // MinorVersion - The minor version for this extension.
  int MinorVersion = minor;

  // Experimental - Does extension require -menable-experimental-extensions.
  bit Experimental = false;
}

// The groupID/bitmask of RISCVExtension is used to retrieve a specific bit value 
// from __riscv_feature_bits based on the groupID and bitmask.
// groupID - groupID of extension
// bitPos  - bit position of extension bitmask
class RISCVExtensionBitmask<bits<3> groupID, int bitPos> {
    int GroupID = groupID;
    int BitPos = bitPos;
}

// Version of RISCVExtension to be used for Experimental extensions. This
// sets the Experimental flag and prepends experimental- to the -mattr name.
class RISCVExperimentalExtension<string name, int major, int minor, string desc,
                                 list<RISCVExtension> implies = [],
                                 string fieldname = !subst("Feature", "Has", NAME),
                                 string value = "true">
    : RISCVExtension<"experimental-"#name, major, minor, desc, implies,
                     fieldname, value> {
  let Experimental = true;
}

// Integer Extensions

def FeatureStdExtI
    : RISCVExtension<"i", 2, 1,
                     "'I' (Base Integer Instruction Set)">,
      RISCVExtensionBitmask<0, 8>;

def FeatureStdExtE
    : RISCVExtension<"e", 2, 0,
                     "Implements RV{32,64}E (provides 16 rather than 32 GPRs)">;

def FeatureStdExtZic64b
    : RISCVExtension<"zic64b", 1, 0,
                     "'Zic64b' (Cache Block Size Is 64 Bytes)">;

def FeatureStdExtZicbom
    : RISCVExtension<"zicbom", 1, 0,
                     "'Zicbom' (Cache-Block Management Instructions)">;
def HasStdExtZicbom : Predicate<"Subtarget->hasStdExtZicbom()">,
                      AssemblerPredicate<(all_of FeatureStdExtZicbom),
                          "'Zicbom' (Cache-Block Management Instructions)">;

def FeatureStdExtZicbop
    : RISCVExtension<"zicbop", 1, 0,
                     "'Zicbop' (Cache-Block Prefetch Instructions)">;
def HasStdExtZicbop : Predicate<"Subtarget->hasStdExtZicbop()">,
                      AssemblerPredicate<(all_of FeatureStdExtZicbop),
                          "'Zicbop' (Cache-Block Prefetch Instructions)">;

def FeatureStdExtZicboz
    : RISCVExtension<"zicboz", 1, 0,
                     "'Zicboz' (Cache-Block Zero Instructions)">,
      RISCVExtensionBitmask<0, 37>;
def HasStdExtZicboz : Predicate<"Subtarget->hasStdExtZicboz()">,
                      AssemblerPredicate<(all_of FeatureStdExtZicboz),
                          "'Zicboz' (Cache-Block Zero Instructions)">;

def FeatureStdExtZiccamoa
    : RISCVExtension<"ziccamoa", 1, 0,
                     "'Ziccamoa' (Main Memory Supports All Atomics in A)">;

def FeatureStdExtZiccif
    : RISCVExtension<"ziccif", 1, 0,
                     "'Ziccif' (Main Memory Supports Instruction Fetch with Atomicity Requirement)">;

def FeatureStdExtZicclsm
    : RISCVExtension<"zicclsm", 1, 0,
                     "'Zicclsm' (Main Memory Supports Misaligned Loads/Stores)">;

def FeatureStdExtZiccrse
    : RISCVExtension<"ziccrse", 1, 0,
                     "'Ziccrse' (Main Memory Supports Forward Progress on LR/SC Sequences)">;

def FeatureStdExtZicsr
    : RISCVExtension<"zicsr", 2, 0,
                     "'zicsr' (CSRs)">;
def HasStdExtZicsr : Predicate<"Subtarget->hasStdExtZicsr()">,
                     AssemblerPredicate<(all_of FeatureStdExtZicsr),
                                        "'Zicsr' (CSRs)">;

def FeatureStdExtZicntr
    : RISCVExtension<"zicntr", 2, 0,
                     "'Zicntr' (Base Counters and Timers)",
                       [FeatureStdExtZicsr]>;

def FeatureStdExtZicond
    : RISCVExtension<"zicond", 1, 0,
                     "'Zicond' (Integer Conditional Operations)">,
      RISCVExtensionBitmask<0, 38>;
def HasStdExtZicond : Predicate<"Subtarget->hasStdExtZicond()">,
                      AssemblerPredicate<(all_of FeatureStdExtZicond),
                          "'Zicond' (Integer Conditional Operations)">;

def FeatureStdExtZifencei
    : RISCVExtension<"zifencei", 2, 0,
                     "'Zifencei' (fence.i)">;
def HasStdExtZifencei : Predicate<"Subtarget->hasStdExtZifencei()">,
                        AssemblerPredicate<(all_of FeatureStdExtZifencei),
                                           "'Zifencei' (fence.i)">;

def FeatureStdExtZihintpause
    : RISCVExtension<"zihintpause", 2, 0,
                     "'Zihintpause' (Pause Hint)">,
      RISCVExtensionBitmask<0, 40>;
def HasStdExtZihintpause : Predicate<"Subtarget->hasStdExtZihintpause()">,
                           AssemblerPredicate<(all_of FeatureStdExtZihintpause),
                                              "'Zihintpause' (Pause Hint)">;

def FeatureStdExtZihintntl
    : RISCVExtension<"zihintntl", 1, 0,
                     "'Zihintntl' (Non-Temporal Locality Hints)">,
      RISCVExtensionBitmask<0, 39>;
def HasStdExtZihintntl : Predicate<"Subtarget->hasStdExtZihintntl()">,
                         AssemblerPredicate<(all_of FeatureStdExtZihintntl),
                             "'Zihintntl' (Non-Temporal Locality Hints)">;

def FeatureStdExtZihpm
    : RISCVExtension<"zihpm", 2, 0,
                     "'Zihpm' (Hardware Performance Counters)",
                     [FeatureStdExtZicsr]>;

def FeatureStdExtZimop : RISCVExtension<"zimop", 1, 0,
                                        "'Zimop' (May-Be-Operations)">;
def HasStdExtZimop : Predicate<"Subtarget->hasStdExtZimop()">,
                     AssemblerPredicate<(all_of FeatureStdExtZimop),
                                        "'Zimop' (May-Be-Operations)">;

def FeatureStdExtZicfilp
    : RISCVExperimentalExtension<"zicfilp", 1, 0,
                                 "'Zicfilp' (Landing pad)",
                                 [FeatureStdExtZicsr]>;
def HasStdExtZicfilp : Predicate<"Subtarget->hasStdExtZicfilp()">,
                       AssemblerPredicate<(all_of FeatureStdExtZicfilp),
                                          "'Zicfilp' (Landing pad)">;
def NoStdExtZicfilp : Predicate<"!Subtarget->hasStdExtZicfilp()">,
                      AssemblerPredicate<(all_of (not FeatureStdExtZicfilp))>;

def FeatureStdExtZicfiss
    : RISCVExperimentalExtension<"zicfiss", 1, 0,
                                 "'Zicfiss' (Shadow stack)",
                                 [FeatureStdExtZicsr, FeatureStdExtZimop]>;
def HasStdExtZicfiss : Predicate<"Subtarget->hasStdExtZicfiss()">,
                       AssemblerPredicate<(all_of FeatureStdExtZicfiss),
                                          "'Zicfiss' (Shadow stack)">;
def NoHasStdExtZicfiss : Predicate<"!Subtarget->hasStdExtZicfiss()">;

// Multiply Extensions

def FeatureStdExtZmmul
    : RISCVExtension<"zmmul", 1, 0,
                     "'Zmmul' (Integer Multiplication)">;
def HasStdExtZmmul : Predicate<"Subtarget->hasStdExtZmmul()">,
                     AssemblerPredicate<(all_of FeatureStdExtZmmul),
                     "'Zmmul' (Integer Multiplication)">;

def FeatureStdExtM
    : RISCVExtension<"m", 2, 0,
                     "'M' (Integer Multiplication and Division)",
                     [FeatureStdExtZmmul]>,
      RISCVExtensionBitmask<0, 12>;
def HasStdExtM : Predicate<"Subtarget->hasStdExtM()">,
                 AssemblerPredicate<(all_of FeatureStdExtM),
                     "'M' (Integer Multiplication and Division)">;

// Atomic Extensions

def FeatureStdExtA
    : RISCVExtension<"a", 2, 1,
                     "'A' (Atomic Instructions)">,
      RISCVExtensionBitmask<0, 0>;
def HasStdExtA : Predicate<"Subtarget->hasStdExtA()">,
                 AssemblerPredicate<(all_of FeatureStdExtA),
                                    "'A' (Atomic Instructions)">;

def FeatureStdExtZtso
    : RISCVExtension<"ztso", 1, 0,
                     "'Ztso' (Memory Model - Total Store Order)">,
      RISCVExtensionBitmask<0, 47>;
def HasStdExtZtso : Predicate<"Subtarget->hasStdExtZtso()">,
                    AssemblerPredicate<(all_of FeatureStdExtZtso),
                        "'Ztso' (Memory Model - Total Store Order)">;
def NotHasStdExtZtso : Predicate<"!Subtarget->hasStdExtZtso()">;

def FeatureStdExtZa64rs : RISCVExtension<"za64rs", 1, 0,
                                         "'Za64rs' (Reservation Set Size of at Most 64 Bytes)">;

def FeatureStdExtZa128rs : RISCVExtension<"za128rs", 1, 0,
                                          "'Za128rs' (Reservation Set Size of at Most 128 Bytes)">;

def FeatureStdExtZaamo
    : RISCVExtension<"zaamo", 1, 0,
                     "'Zaamo' (Atomic Memory Operations)">;
def HasStdExtAOrZaamo
    : Predicate<"Subtarget->hasStdExtA() || Subtarget->hasStdExtZaamo()">,
      AssemblerPredicate<(any_of FeatureStdExtA, FeatureStdExtZaamo),
                         "'A' (Atomic Instructions) or "
                         "'Zaamo' (Atomic Memory Operations)">;

def FeatureStdExtZabha
    : RISCVExtension<"zabha", 1, 0,
                     "'Zabha' (Byte and Halfword Atomic Memory Operations)">;
def HasStdExtZabha : Predicate<"Subtarget->hasStdExtZabha()">,
                     AssemblerPredicate<(all_of FeatureStdExtZabha),
                         "'Zabha' (Byte and Halfword Atomic Memory Operations)">;

def FeatureStdExtZacas
    : RISCVExperimentalExtension<"zacas", 1, 0,
                                 "'Zacas' (Atomic Compare-And-Swap Instructions)">,
      RISCVExtensionBitmask<0, 26>;
def HasStdExtZacas : Predicate<"Subtarget->hasStdExtZacas()">,
                     AssemblerPredicate<(all_of FeatureStdExtZacas),
                         "'Zacas' (Atomic Compare-And-Swap Instructions)">;
def NoStdExtZacas : Predicate<"!Subtarget->hasStdExtZacas()">;

def FeatureStdExtZalasr
    : RISCVExperimentalExtension<"zalasr", 0, 1,
                                 "'Zalasr' (Load-Acquire and Store-Release Instructions)">;
def HasStdExtZalasr : Predicate<"Subtarget->hasStdExtZalasr()">,
                      AssemblerPredicate<(all_of FeatureStdExtZalasr),
                          "'Zalasr' (Load-Acquire and Store-Release Instructions)">;

def FeatureStdExtZalrsc
    : RISCVExtension<"zalrsc", 1, 0,
                     "'Zalrsc' (Load-Reserved/Store-Conditional)">;
def HasStdExtAOrZalrsc
    : Predicate<"Subtarget->hasStdExtA() || Subtarget->hasStdExtZalrsc()">,
      AssemblerPredicate<(any_of FeatureStdExtA, FeatureStdExtZalrsc),
                         "'A' (Atomic Instructions) or "
                         "'Zalrsc' (Load-Reserved/Store-Conditional)">;

def FeatureStdExtZama16b
    : RISCVExtension<"zama16b", 1, 0,
                     "'Zama16b' (Atomic 16-byte misaligned loads, stores and AMOs)">;

def FeatureStdExtZawrs : RISCVExtension<"zawrs", 1, 0,
                                        "'Zawrs' (Wait on Reservation Set)">;
def HasStdExtZawrs : Predicate<"Subtarget->hasStdExtZawrs()">,
                     AssemblerPredicate<(all_of FeatureStdExtZawrs),
                                        "'Zawrs' (Wait on Reservation Set)">;

// Floating Point Extensions

def FeatureStdExtF
    : RISCVExtension<"f", 2, 2,
                     "'F' (Single-Precision Floating-Point)",
                     [FeatureStdExtZicsr]>,
      RISCVExtensionBitmask<0, 5>;
def HasStdExtF : Predicate<"Subtarget->hasStdExtF()">,
                 AssemblerPredicate<(all_of FeatureStdExtF),
                                    "'F' (Single-Precision Floating-Point)">;

def FeatureStdExtD
    : RISCVExtension<"d", 2, 2,
                     "'D' (Double-Precision Floating-Point)",
                     [FeatureStdExtF]>,
      RISCVExtensionBitmask<0, 3>;
def HasStdExtD : Predicate<"Subtarget->hasStdExtD()">,
                 AssemblerPredicate<(all_of FeatureStdExtD),
                                    "'D' (Double-Precision Floating-Point)">;

def FeatureStdExtZfhmin
    : RISCVExtension<"zfhmin", 1, 0,
                     "'Zfhmin' (Half-Precision Floating-Point Minimal)",
                     [FeatureStdExtF]>,
      RISCVExtensionBitmask<0, 36>;
def HasStdExtZfhmin : Predicate<"Subtarget->hasStdExtZfhmin()">,
                      AssemblerPredicate<(all_of FeatureStdExtZfhmin),
                          "'Zfh' (Half-Precision Floating-Point) or "
                          "'Zfhmin' (Half-Precision Floating-Point Minimal)">;

def FeatureStdExtZfh
    : RISCVExtension<"zfh", 1, 0,
                     "'Zfh' (Half-Precision Floating-Point)",
                     [FeatureStdExtZfhmin]>,
      RISCVExtensionBitmask<0, 35>;
def HasStdExtZfh : Predicate<"Subtarget->hasStdExtZfh()">,
                   AssemblerPredicate<(all_of FeatureStdExtZfh),
                       "'Zfh' (Half-Precision Floating-Point)">;
def NoStdExtZfh : Predicate<"!Subtarget->hasStdExtZfh()">;

def FeatureStdExtZfbfmin
    : RISCVExtension<"zfbfmin", 1, 0, "'Zfbfmin' (Scalar BF16 Converts)",
                     [FeatureStdExtF]>;
def HasStdExtZfbfmin : Predicate<"Subtarget->hasStdExtZfbfmin()">,
                       AssemblerPredicate<(all_of FeatureStdExtZfbfmin),
                                          "'Zfbfmin' (Scalar BF16 Converts)">;

def HasHalfFPLoadStoreMove
    : Predicate<"Subtarget->hasHalfFPLoadStoreMove()">,
      AssemblerPredicate<(any_of FeatureStdExtZfh, FeatureStdExtZfhmin,
                                 FeatureStdExtZfbfmin),
                         "'Zfh' (Half-Precision Floating-Point) or "
                                    "'Zfhmin' (Half-Precision Floating-Point Minimal) or "
                                    "'Zfbfmin' (Scalar BF16 Converts)">;

def FeatureStdExtZfa
    : RISCVExtension<"zfa", 1, 0,
                     "'Zfa' (Additional Floating-Point)",
                     [FeatureStdExtF]>,
      RISCVExtensionBitmask<0, 34>;
def HasStdExtZfa : Predicate<"Subtarget->hasStdExtZfa()">,
                   AssemblerPredicate<(all_of FeatureStdExtZfa),
                                      "'Zfa' (Additional Floating-Point)">;

def FeatureStdExtZfinx
    : RISCVExtension<"zfinx", 1, 0,
                     "'Zfinx' (Float in Integer)",
                     [FeatureStdExtZicsr]>;
def HasStdExtZfinx : Predicate<"Subtarget->hasStdExtZfinx()">,
                     AssemblerPredicate<(all_of FeatureStdExtZfinx),
                                        "'Zfinx' (Float in Integer)">;

def FeatureStdExtZdinx
    : RISCVExtension<"zdinx", 1, 0,
                     "'Zdinx' (Double in Integer)",
                     [FeatureStdExtZfinx]>;
def HasStdExtZdinx : Predicate<"Subtarget->hasStdExtZdinx()">,
                     AssemblerPredicate<(all_of FeatureStdExtZdinx),
                                        "'Zdinx' (Double in Integer)">;

def FeatureStdExtZhinxmin
    : RISCVExtension<"zhinxmin", 1, 0,
                     "'Zhinxmin' (Half Float in Integer Minimal)",
                     [FeatureStdExtZfinx]>;
def HasStdExtZhinxmin : Predicate<"Subtarget->hasStdExtZhinxmin()">,
                        AssemblerPredicate<(all_of FeatureStdExtZhinxmin),
                            "'Zhinx' (Half Float in Integer) or "
                            "'Zhinxmin' (Half Float in Integer Minimal)">;

def FeatureStdExtZhinx
    : RISCVExtension<"zhinx", 1, 0,
                     "'Zhinx' (Half Float in Integer)",
                     [FeatureStdExtZhinxmin]>;
def HasStdExtZhinx : Predicate<"Subtarget->hasStdExtZhinx()">,
                     AssemblerPredicate<(all_of FeatureStdExtZhinx),
                                        "'Zhinx' (Half Float in Integer)">;
def NoStdExtZhinx : Predicate<"!Subtarget->hasStdExtZhinx()">;

// Compressed Extensions

def FeatureStdExtC
    : RISCVExtension<"c", 2, 0,
                     "'C' (Compressed Instructions)">,
      RISCVExtensionBitmask<0, 2>;
def HasStdExtC : Predicate<"Subtarget->hasStdExtC()">,
                 AssemblerPredicate<(all_of FeatureStdExtC),
                                    "'C' (Compressed Instructions)">;

def FeatureNoRVCHints
    : SubtargetFeature<"no-rvc-hints", "EnableRVCHintInstrs", "false",
                       "Disable RVC Hint Instructions.">;
def HasRVCHints : Predicate<"Subtarget->enableRVCHintInstrs()">,
                  AssemblerPredicate<(all_of(not FeatureNoRVCHints)),
                                     "RVC Hint Instructions">;

def FeatureStdExtZca
    : RISCVExtension<"zca", 1, 0,
                     "'Zca' (part of the C extension, excluding compressed "
                     "floating point loads/stores)">;

def HasStdExtCOrZca
    : Predicate<"Subtarget->hasStdExtCOrZca()">,
      AssemblerPredicate<(any_of FeatureStdExtC, FeatureStdExtZca),
                         "'C' (Compressed Instructions) or "
                         "'Zca' (part of the C extension, excluding "
                         "compressed floating point loads/stores)">;

def FeatureStdExtZcb
    : RISCVExtension<"zcb", 1, 0,
                     "'Zcb' (Compressed basic bit manipulation instructions)",
                     [FeatureStdExtZca]>;
def HasStdExtZcb : Predicate<"Subtarget->hasStdExtZcb()">,
                   AssemblerPredicate<(all_of FeatureStdExtZcb),
                       "'Zcb' (Compressed basic bit manipulation instructions)">;

def FeatureStdExtZcd
    : RISCVExtension<"zcd", 1, 0,
                     "'Zcd' (Compressed Double-Precision Floating-Point Instructions)",
                     [FeatureStdExtD, FeatureStdExtZca]>;

def HasStdExtCOrZcd
    : Predicate<"Subtarget->hasStdExtCOrZcd()">,
      AssemblerPredicate<(any_of FeatureStdExtC, FeatureStdExtZcd),
                         "'C' (Compressed Instructions) or "
                         "'Zcd' (Compressed Double-Precision Floating-Point Instructions)">;

def FeatureStdExtZcf
    : RISCVExtension<"zcf", 1, 0,
                     "'Zcf' (Compressed Single-Precision Floating-Point Instructions)",
                     [FeatureStdExtF, FeatureStdExtZca]>;

def FeatureStdExtZcmp
    : RISCVExtension<"zcmp", 1, 0,
                     "'Zcmp' (sequenced instructions for code-size reduction)",
                     [FeatureStdExtZca]>;
def HasStdExtZcmp : Predicate<"Subtarget->hasStdExtZcmp() && !Subtarget->hasStdExtC()">,
                    AssemblerPredicate<(all_of FeatureStdExtZcmp),
                        "'Zcmp' (sequenced instructions for code-size reduction)">;

def FeatureStdExtZcmt
    : RISCVExtension<"zcmt", 1, 0,
                     "'Zcmt' (table jump instructions for code-size reduction)",
                     [FeatureStdExtZca, FeatureStdExtZicsr]>;
def HasStdExtZcmt : Predicate<"Subtarget->hasStdExtZcmt()">,
                           AssemblerPredicate<(all_of FeatureStdExtZcmt),
                           "'Zcmt' (table jump instructions for code-size reduction)">;

def FeatureStdExtZce
    : RISCVExtension<"zce", 1, 0,
                     "'Zce' (Compressed extensions for microcontrollers)",
                     [FeatureStdExtZcb, FeatureStdExtZcmp, FeatureStdExtZcmt]>;

def HasStdExtCOrZcfOrZce
    : Predicate<"Subtarget->hasStdExtC() || Subtarget->hasStdExtZcf() "
                "Subtarget->hasStdExtZce()">,
      AssemblerPredicate<(any_of FeatureStdExtC, FeatureStdExtZcf,
                                 FeatureStdExtZce),
                         "'C' (Compressed Instructions) or "
                         "'Zcf' (Compressed Single-Precision Floating-Point Instructions)">;

def FeatureStdExtZcmop
    : RISCVExtension<"zcmop", 1, 0,
                     "'Zcmop' (Compressed May-Be-Operations)",
                     [FeatureStdExtZca]>;
def HasStdExtZcmop : Predicate<"Subtarget->hasStdExtZcmop()">,
                     AssemblerPredicate<(all_of FeatureStdExtZcmop),
                         "'Zcmop' (Compressed May-Be-Operations)">;

// Bitmanip Extensions

def FeatureStdExtZba
    : RISCVExtension<"zba", 1, 0,
                     "'Zba' (Address Generation Instructions)">,
      RISCVExtensionBitmask<0, 27>;
def HasStdExtZba : Predicate<"Subtarget->hasStdExtZba()">,
                   AssemblerPredicate<(all_of FeatureStdExtZba),
                                      "'Zba' (Address Generation Instructions)">;
def NotHasStdExtZba : Predicate<"!Subtarget->hasStdExtZba()">;

def FeatureStdExtZbb
    : RISCVExtension<"zbb", 1, 0,
                     "'Zbb' (Basic Bit-Manipulation)">,
      RISCVExtensionBitmask<0, 28>;
def HasStdExtZbb : Predicate<"Subtarget->hasStdExtZbb()">,
                   AssemblerPredicate<(all_of FeatureStdExtZbb),
                                      "'Zbb' (Basic Bit-Manipulation)">;
def NoStdExtZbb : Predicate<"!Subtarget->hasStdExtZbb()">,
                  AssemblerPredicate<(all_of (not FeatureStdExtZbb))>;

def FeatureStdExtZbc
    : RISCVExtension<"zbc", 1, 0,
                     "'Zbc' (Carry-Less Multiplication)">,
      RISCVExtensionBitmask<0, 29>;
def HasStdExtZbc : Predicate<"Subtarget->hasStdExtZbc()">,
                   AssemblerPredicate<(all_of FeatureStdExtZbc),
                                      "'Zbc' (Carry-Less Multiplication)">;

def FeatureStdExtZbs
    : RISCVExtension<"zbs", 1, 0,
                     "'Zbs' (Single-Bit Instructions)">,
      RISCVExtensionBitmask<0, 33>;
def HasStdExtZbs : Predicate<"Subtarget->hasStdExtZbs()">,
                   AssemblerPredicate<(all_of FeatureStdExtZbs),
                                      "'Zbs' (Single-Bit Instructions)">;

// Bitmanip Extensions for Cryptography Extensions

def FeatureStdExtB
    : RISCVExtension<"b", 1, 0,
                     "'B' (the collection of the Zba, Zbb, Zbs extensions)",
                     [FeatureStdExtZba, FeatureStdExtZbb, FeatureStdExtZbs]>;
def HasStdExtB : Predicate<"Subtarget->hasStdExtB()">,
                           AssemblerPredicate<(all_of FeatureStdExtB),
                           "'B' (the collection of the Zba, Zbb, Zbs extensions)">;

def FeatureStdExtZbkb
    : RISCVExtension<"zbkb", 1, 0,
                     "'Zbkb' (Bitmanip instructions for Cryptography)">,
      RISCVExtensionBitmask<0, 30>;
def HasStdExtZbkb : Predicate<"Subtarget->hasStdExtZbkb()">,
                    AssemblerPredicate<(all_of FeatureStdExtZbkb),
                        "'Zbkb' (Bitmanip instructions for Cryptography)">;

def FeatureStdExtZbkx
    : RISCVExtension<"zbkx", 1, 0,
                     "'Zbkx' (Crossbar permutation instructions)">,
      RISCVExtensionBitmask<0, 32>;
def HasStdExtZbkx : Predicate<"Subtarget->hasStdExtZbkx()">,
                    AssemblerPredicate<(all_of FeatureStdExtZbkx),
                        "'Zbkx' (Crossbar permutation instructions)">;

def HasStdExtZbbOrZbkb
    : Predicate<"Subtarget->hasStdExtZbb() || Subtarget->hasStdExtZbkb()">,
      AssemblerPredicate<(any_of FeatureStdExtZbb, FeatureStdExtZbkb),
                         "'Zbb' (Basic Bit-Manipulation) or "
                         "'Zbkb' (Bitmanip instructions for Cryptography)">;

// The Carry-less multiply subextension for cryptography is a subset of basic
// carry-less multiply subextension. The former should be enabled if the latter
// is enabled.
def FeatureStdExtZbkc
    : RISCVExtension<"zbkc", 1, 0,
                     "'Zbkc' (Carry-less multiply instructions for "
                     "Cryptography)">,
      RISCVExtensionBitmask<0, 31>;
def HasStdExtZbkc
    : Predicate<"Subtarget->hasStdExtZbkc()">,
      AssemblerPredicate<(all_of FeatureStdExtZbkc),
          "'Zbkc' (Carry-less multiply instructions for Cryptography)">;

def HasStdExtZbcOrZbkc
    : Predicate<"Subtarget->hasStdExtZbc() || Subtarget->hasStdExtZbkc()">,
      AssemblerPredicate<(any_of FeatureStdExtZbc, FeatureStdExtZbkc),
                         "'Zbc' (Carry-Less Multiplication) or "
                         "'Zbkc' (Carry-less multiply instructions "
                         "for Cryptography)">;

// Cryptography Extensions

def FeatureStdExtZknd
    : RISCVExtension<"zknd", 1, 0,
                     "'Zknd' (NIST Suite: AES Decryption)">,
      RISCVExtensionBitmask<0, 41>;
def HasStdExtZknd : Predicate<"Subtarget->hasStdExtZknd()">,
                    AssemblerPredicate<(all_of FeatureStdExtZknd),
                                       "'Zknd' (NIST Suite: AES Decryption)">;

def FeatureStdExtZkne
    : RISCVExtension<"zkne", 1, 0,
                     "'Zkne' (NIST Suite: AES Encryption)">,
      RISCVExtensionBitmask<0, 42>;
def HasStdExtZkne : Predicate<"Subtarget->hasStdExtZkne()">,
                    AssemblerPredicate<(all_of FeatureStdExtZkne),
                                       "'Zkne' (NIST Suite: AES Encryption)">;

// Some instructions belong to both Zknd and Zkne subextensions.
// They should be enabled if either has been specified.
def HasStdExtZkndOrZkne
    : Predicate<"Subtarget->hasStdExtZknd() || Subtarget->hasStdExtZkne()">,
      AssemblerPredicate<(any_of FeatureStdExtZknd, FeatureStdExtZkne),
                         "'Zknd' (NIST Suite: AES Decryption) or "
                         "'Zkne' (NIST Suite: AES Encryption)">;

def FeatureStdExtZknh
    : RISCVExtension<"zknh", 1, 0,
                     "'Zknh' (NIST Suite: Hash Function Instructions)">,
      RISCVExtensionBitmask<0, 43>;
def HasStdExtZknh : Predicate<"Subtarget->hasStdExtZknh()">,
                    AssemblerPredicate<(all_of FeatureStdExtZknh),
                        "'Zknh' (NIST Suite: Hash Function Instructions)">;

def FeatureStdExtZksed
    : RISCVExtension<"zksed", 1, 0,
                     "'Zksed' (ShangMi Suite: SM4 Block Cipher Instructions)">,
      RISCVExtensionBitmask<0, 44>;
def HasStdExtZksed : Predicate<"Subtarget->hasStdExtZksed()">,
                     AssemblerPredicate<(all_of FeatureStdExtZksed),
                         "'Zksed' (ShangMi Suite: SM4 Block Cipher Instructions)">;

def FeatureStdExtZksh
    : RISCVExtension<"zksh", 1, 0,
                     "'Zksh' (ShangMi Suite: SM3 Hash Function Instructions)">,
      RISCVExtensionBitmask<0, 45>;
def HasStdExtZksh : Predicate<"Subtarget->hasStdExtZksh()">,
                    AssemblerPredicate<(all_of FeatureStdExtZksh),
                        "'Zksh' (ShangMi Suite: SM3 Hash Function Instructions)">;

def FeatureStdExtZkr
    : RISCVExtension<"zkr", 1, 0,
                     "'Zkr' (Entropy Source Extension)">;
def HasStdExtZkr : Predicate<"Subtarget->hasStdExtZkr()">,
                   AssemblerPredicate<(all_of FeatureStdExtZkr),
                                      "'Zkr' (Entropy Source Extension)">;

def FeatureStdExtZkn
    : RISCVExtension<"zkn", 1, 0,
                     "'Zkn' (NIST Algorithm Suite)",
                     [FeatureStdExtZbkb,
                      FeatureStdExtZbkc,
                      FeatureStdExtZbkx,
                      FeatureStdExtZkne,
                      FeatureStdExtZknd,
                      FeatureStdExtZknh]>;

def FeatureStdExtZks
    : RISCVExtension<"zks", 1, 0,
                     "'Zks' (ShangMi Algorithm Suite)",
                     [FeatureStdExtZbkb,
                      FeatureStdExtZbkc,
                      FeatureStdExtZbkx,
                      FeatureStdExtZksed,
                      FeatureStdExtZksh]>;

def FeatureStdExtZkt
    : RISCVExtension<"zkt", 1, 0,
                     "'Zkt' (Data Independent Execution Latency)">,
      RISCVExtensionBitmask<0, 46>;

def FeatureStdExtZk
    : RISCVExtension<"zk", 1, 0,
                     "'Zk' (Standard scalar cryptography extension)",
                     [FeatureStdExtZkn,
                      FeatureStdExtZkr,
                      FeatureStdExtZkt]>;

// Vector Extensions

def FeatureStdExtZvl32b : RISCVExtension<"zvl32b", 1, 0,
                                         "'Zvl' (Minimum Vector Length) 32", [],
                                         "ZvlLen", "32">;

foreach i = { 6-16 } in {
  defvar I = !shl(1, i);
  def FeatureStdExtZvl#I#b :
      RISCVExtension<"zvl"#I#"b", 1, 0,
                     "'Zvl' (Minimum Vector Length) "#I,
                     [!cast<RISCVExtension>("FeatureStdExtZvl"#!srl(I, 1)#"b")],
                     "ZvlLen", !cast<string>(I)>;
}

def FeatureStdExtZve32x
    : RISCVExtension<"zve32x", 1, 0,
                     "'Zve32x' (Vector Extensions for Embedded Processors "
                     "with maximal 32 EEW)",
                     [FeatureStdExtZicsr, FeatureStdExtZvl32b]>;


def FeatureStdExtZve32f
    : RISCVExtension<"zve32f", 1, 0,
                     "'Zve32f' (Vector Extensions for Embedded Processors "
                     "with maximal 32 EEW and F extension)",
                     [FeatureStdExtZve32x, FeatureStdExtF]>;

def FeatureStdExtZve64x
    : RISCVExtension<"zve64x", 1, 0,
                     "'Zve64x' (Vector Extensions for Embedded Processors "
                     "with maximal 64 EEW)",
                     [FeatureStdExtZve32x, FeatureStdExtZvl64b]>;

def FeatureStdExtZve64f
    : RISCVExtension<"zve64f", 1, 0,
                     "'Zve64f' (Vector Extensions for Embedded Processors "
                     "with maximal 64 EEW and F extension)",
                     [FeatureStdExtZve32f, FeatureStdExtZve64x]>;

def FeatureStdExtZve64d
    : RISCVExtension<"zve64d", 1, 0,
                     "'Zve64d' (Vector Extensions for Embedded Processors "
                     "with maximal 64 EEW, F and D extension)",
                     [FeatureStdExtZve64f, FeatureStdExtD]>;

def FeatureStdExtV
    : RISCVExtension<"v", 1, 0,
                     "'V' (Vector Extension for Application Processors)",
                     [FeatureStdExtZvl128b, FeatureStdExtZve64d]>,
      RISCVExtensionBitmask<0, 21>;

def FeatureStdExtZvfbfmin
    : RISCVExtension<"zvfbfmin", 1, 0, "'Zvbfmin' (Vector BF16 Converts)",
                     [FeatureStdExtZve32f]>;
def HasStdExtZvfbfmin : Predicate<"Subtarget->hasStdExtZvfbfmin()">,
                        AssemblerPredicate<(all_of FeatureStdExtZvfbfmin),
                            "'Zvfbfmin' (Vector BF16 Converts)">;

def FeatureStdExtZvfbfwma
    : RISCVExtension<"zvfbfwma", 1, 0,
                     "'Zvfbfwma' (Vector BF16 widening mul-add)",
                     [FeatureStdExtZvfbfmin, FeatureStdExtZfbfmin]>;
def HasStdExtZvfbfwma : Predicate<"Subtarget->hasStdExtZvfbfwma()">,
                        AssemblerPredicate<(all_of FeatureStdExtZvfbfwma),
                            "'Zvfbfwma' (Vector BF16 widening mul-add)">;

def FeatureStdExtZvfhmin
    : RISCVExtension<"zvfhmin", 1, 0,
                     "'Zvfhmin' (Vector Half-Precision Floating-Point Minimal)",
                     [FeatureStdExtZve32f]>,
      RISCVExtensionBitmask<0, 51>;

def FeatureStdExtZvfh
    : RISCVExtension<"zvfh", 1, 0,
                     "'Zvfh' (Vector Half-Precision Floating-Point)",
                     [FeatureStdExtZvfhmin, FeatureStdExtZfhmin]>,
      RISCVExtensionBitmask<0, 50>;

def HasStdExtZfhOrZvfh
    : Predicate<"Subtarget->hasStdExtZfh() || Subtarget->hasStdExtZvfh()">,
      AssemblerPredicate<(any_of FeatureStdExtZfh, FeatureStdExtZvfh),
                         "'Zfh' (Half-Precision Floating-Point) or "
                         "'Zvfh' (Vector Half-Precision Floating-Point)">;

// Vector Cryptography and Bitmanip Extensions

def FeatureStdExtZvkb
    : RISCVExtension<"zvkb", 1, 0,
                     "'Zvkb' (Vector Bit-manipulation used in Cryptography)">,
      RISCVExtensionBitmask<0, 52>;
def HasStdExtZvkb : Predicate<"Subtarget->hasStdExtZvkb()">,
                    AssemblerPredicate<(all_of FeatureStdExtZvkb),
                        "'Zvkb' (Vector Bit-manipulation used in Cryptography)">;

def FeatureStdExtZvbb
    : RISCVExtension<"zvbb", 1, 0,
                     "'Zvbb' (Vector basic bit-manipulation instructions)",
                     [FeatureStdExtZvkb]>,
      RISCVExtensionBitmask<0, 48>;
def HasStdExtZvbb : Predicate<"Subtarget->hasStdExtZvbb()">,
                    AssemblerPredicate<(all_of FeatureStdExtZvbb),
                        "'Zvbb' (Vector basic bit-manipulation instructions)">;

def FeatureStdExtZvbc
    : RISCVExtension<"zvbc", 1, 0,
                     "'Zvbc' (Vector Carryless Multiplication)">,
      RISCVExtensionBitmask<0, 49>;
def HasStdExtZvbc : Predicate<"Subtarget->hasStdExtZvbc()">,
                    AssemblerPredicate<(all_of FeatureStdExtZvbc),
                        "'Zvbc' (Vector Carryless Multiplication)">;

def FeatureStdExtZvkg
    : RISCVExtension<"zvkg", 1, 0,
                     "'Zvkg' (Vector GCM instructions for Cryptography)">,
      RISCVExtensionBitmask<0, 53>;
def HasStdExtZvkg : Predicate<"Subtarget->hasStdExtZvkg()">,
                    AssemblerPredicate<(all_of FeatureStdExtZvkg),
                        "'Zvkg' (Vector GCM instructions for Cryptography)">;

def FeatureStdExtZvkned
    : RISCVExtension<"zvkned", 1, 0,
                     "'Zvkned' (Vector AES Encryption & Decryption (Single Round))">,
      RISCVExtensionBitmask<0, 54>;
def HasStdExtZvkned : Predicate<"Subtarget->hasStdExtZvkned()">,
                      AssemblerPredicate<(all_of FeatureStdExtZvkned),
                          "'Zvkned' (Vector AES Encryption & Decryption (Single Round))">;

def FeatureStdExtZvknha
    : RISCVExtension<"zvknha", 1, 0,
                     "'Zvknha' (Vector SHA-2 (SHA-256 only))">,
      RISCVExtensionBitmask<0, 55>;
def HasStdExtZvknha : Predicate<"Subtarget->hasStdExtZvknha()">,
                      AssemblerPredicate<(all_of FeatureStdExtZvknha),
                          "'Zvknha' (Vector SHA-2 (SHA-256 only))">;

def FeatureStdExtZvknhb
    : RISCVExtension<"zvknhb", 1, 0,
                     "'Zvknhb' (Vector SHA-2 (SHA-256 and SHA-512))",
                     [FeatureStdExtZve64x]>,
      RISCVExtensionBitmask<0, 56>;
def HasStdExtZvknhb : Predicate<"Subtarget->hasStdExtZvknhb()">,
                      AssemblerPredicate<(all_of FeatureStdExtZvknhb),
                          "'Zvknhb' (Vector SHA-2 (SHA-256 and SHA-512))">;

def HasStdExtZvknhaOrZvknhb : Predicate<"Subtarget->hasStdExtZvknha() || Subtarget->hasStdExtZvknhb()">,
                              AssemblerPredicate<(any_of FeatureStdExtZvknha, FeatureStdExtZvknhb),
                                  "'Zvknha' or 'Zvknhb' (Vector SHA-2)">;

def FeatureStdExtZvksed
    : RISCVExtension<"zvksed", 1, 0,
                     "'Zvksed' (SM4 Block Cipher Instructions)">,
      RISCVExtensionBitmask<0, 57>;
def HasStdExtZvksed : Predicate<"Subtarget->hasStdExtZvksed()">,
                      AssemblerPredicate<(all_of FeatureStdExtZvksed),
                          "'Zvksed' (SM4 Block Cipher Instructions)">;

def FeatureStdExtZvksh
    : RISCVExtension<"zvksh", 1, 0,
                     "'Zvksh' (SM3 Hash Function Instructions)">,
      RISCVExtensionBitmask<0, 58>;
def HasStdExtZvksh : Predicate<"Subtarget->hasStdExtZvksh()">,
                     AssemblerPredicate<(all_of FeatureStdExtZvksh),
                         "'Zvksh' (SM3 Hash Function Instructions)">;

def FeatureStdExtZvkt
    : RISCVExtension<"zvkt", 1, 0,
                     "'Zvkt' (Vector Data-Independent Execution Latency)">,
      RISCVExtensionBitmask<0, 59>;

// Zvk short-hand extensions

def FeatureStdExtZvkn
    : RISCVExtension<"zvkn", 1, 0,
                     "'Zvkn' (shorthand for 'Zvkned', 'Zvknhb', 'Zvkb', and "
                     "'Zvkt')",
                     [FeatureStdExtZvkned, FeatureStdExtZvknhb,
                      FeatureStdExtZvkb, FeatureStdExtZvkt]>;

def FeatureStdExtZvknc
    : RISCVExtension<"zvknc", 1, 0,
                     "'Zvknc' (shorthand for 'Zvknc' and 'Zvbc')",
                     [FeatureStdExtZvkn, FeatureStdExtZvbc]>;

def FeatureStdExtZvkng
    : RISCVExtension<"zvkng", 1, 0,
                     "'zvkng' (shorthand for 'Zvkn' and 'Zvkg')",
                     [FeatureStdExtZvkn, FeatureStdExtZvkg]>;

def FeatureStdExtZvks
    : RISCVExtension<"zvks", 1, 0,
                     "'Zvks' (shorthand for 'Zvksed', 'Zvksh', 'Zvkb', and "
                     "'Zvkt')",
                     [FeatureStdExtZvksed, FeatureStdExtZvksh,
                      FeatureStdExtZvkb, FeatureStdExtZvkt]>;

def FeatureStdExtZvksc
    : RISCVExtension<"zvksc", 1, 0,
                     "'Zvksc' (shorthand for 'Zvks' and 'Zvbc')",
                     [FeatureStdExtZvks, FeatureStdExtZvbc]>;

def FeatureStdExtZvksg
    : RISCVExtension<"zvksg", 1, 0,
                     "'Zvksg' (shorthand for 'Zvks' and 'Zvkg')",
                     [FeatureStdExtZvks, FeatureStdExtZvkg]>;
// Vector instruction predicates

def HasVInstructions    : Predicate<"Subtarget->hasVInstructions()">,
      AssemblerPredicate<
          (any_of FeatureStdExtZve32x),
          "'V' (Vector Extension for Application Processors), 'Zve32x' "
          "(Vector Extensions for Embedded Processors)">;
def HasVInstructionsI64 : Predicate<"Subtarget->hasVInstructionsI64()">,
      AssemblerPredicate<
          (any_of FeatureStdExtZve64x),
          "'V' (Vector Extension for Application Processors) or 'Zve64x' "
          "(Vector Extensions for Embedded Processors)">;
def HasVInstructionsAnyF : Predicate<"Subtarget->hasVInstructionsAnyF()">,
      AssemblerPredicate<
          (any_of FeatureStdExtZve32f),
          "'V' (Vector Extension for Application Processors), 'Zve32f' "
          "(Vector Extensions for Embedded Processors)">;

def HasVInstructionsF16Minimal : Predicate<"Subtarget->hasVInstructionsF16Minimal()">,
      AssemblerPredicate<(any_of FeatureStdExtZvfhmin, FeatureStdExtZvfh),
                         "'Zvfhmin' (Vector Half-Precision Floating-Point Minimal) or "
                         "'Zvfh' (Vector Half-Precision Floating-Point)">;

def HasVInstructionsBF16 : Predicate<"Subtarget->hasVInstructionsBF16()">;
def HasVInstructionsF16 : Predicate<"Subtarget->hasVInstructionsF16()">;
def HasVInstructionsF64 : Predicate<"Subtarget->hasVInstructionsF64()">;

def HasVInstructionsFullMultiply : Predicate<"Subtarget->hasVInstructionsFullMultiply()">;

// Hypervisor Extensions

def FeatureStdExtH
    : RISCVExtension<"h", 1, 0,
                     "'H' (Hypervisor)">;

def HasStdExtH : Predicate<"Subtarget->hasStdExtH()">,
                 AssemblerPredicate<(all_of FeatureStdExtH),
                                    "'H' (Hypervisor)">;

// Supervisor extensions

def FeatureStdExtShgatpa
    : RISCVExtension<"shgatpa", 1, 0,
                     "'Sgatpa' (SvNNx4 mode supported for all modes supported by satp, as well as Bare)">;
def FeatureStdExtShvsatpa
    : RISCVExtension<"shvsatpa", 1, 0,
                     "'Svsatpa' (vsatp supports all modes supported by satp)">;

def FeatureStdExtSmaia
    : RISCVExtension<"smaia", 1, 0,
                     "'Smaia' (Advanced Interrupt Architecture Machine Level)">;
def FeatureStdExtSsaia
    : RISCVExtension<"ssaia", 1, 0,
                     "'Ssaia' (Advanced Interrupt Architecture Supervisor "
                     "Level)">;

def FeatureStdExtSmcsrind
    : RISCVExtension<"smcsrind", 1, 0,
                     "'Smcsrind' (Indirect CSR Access Machine Level)">;
def FeatureStdExtSscsrind
    : RISCVExtension<"sscsrind", 1, 0,
                     "'Sscsrind' (Indirect CSR Access Supervisor Level)">;

def FeatureStdExtSmepmp
    : RISCVExtension<"smepmp", 1, 0,
                     "'Smepmp' (Enhanced Physical Memory Protection)">;

def FeatureStdExtSmcdeleg
    : RISCVExtension<"smcdeleg", 1, 0,
                     "'Smcdeleg' (Counter Delegation Machine Level)">;
def FeatureStdExtSsccfg
    : RISCVExtension<"ssccfg", 1, 0,
                     "'Ssccfg' (Counter Configuration Supervisor Level)">;

def FeatureStdExtSsccptr
    : RISCVExtension<"ssccptr", 1, 0,
                     "'Ssccptr' (Main memory supports page table reads)">;

def FeatureStdExtSscofpmf
    : RISCVExtension<"sscofpmf", 1, 0,
                     "'Sscofpmf' (Count Overflow and Mode-Based Filtering)">;

def FeatureStdExtShcounterenw
    : RISCVExtension<"shcounterenw", 1, 0,
                     "'Shcounterenw' (Support writeable hcounteren enable "
                     "bit for any hpmcounter that is not read-only zero)">;
def FeatureStdExtSscounterenw
    : RISCVExtension<"sscounterenw", 1, 0,
                     "'Sscounterenw' (Support writeable scounteren enable "
                     "bit for any hpmcounter that is not read-only zero)">;

def FeatureStdExtSmstateen
    : RISCVExtension<"smstateen", 1, 0,
                     "'Smstateen' (Machine-mode view of the state-enable extension)">;
def FeatureStdExtSsstateen
    : RISCVExtension<"ssstateen", 1, 0,
                     "'Ssstateen' (Supervisor-mode view of the state-enable extension)">;

def FeatureStdExtSsstrict
    : RISCVExtension<"ssstrict", 1, 0,
                     "'Ssstrict' (No non-conforming extensions are present)">;

def FeatureStdExtSstc
    : RISCVExtension<"sstc", 1, 0,
                     "'Sstc' (Supervisor-mode timer interrupts)">;

def FeaturesStdExtSsqosid
    : RISCVExperimentalExtension<"ssqosid", 1, 0,
                                 "'Ssqosid' (Quality-of-Service (QoS) Identifiers)">;

def FeatureStdExtShtvala
    : RISCVExtension<"shtvala", 1, 0,
                     "'Shtvala' (htval provides all needed values)">;
def FeatureStdExtShvstvala
    : RISCVExtension<"shvstvala", 1, 0,
                     "'Shvstvala' (vstval provides all needed values)">;
def FeatureStdExtSstvala
    : RISCVExtension<"sstvala", 1, 0,
                     "'Sstvala' (stval provides all needed values)">;

def FeatureStdExtShvstvecd
    : RISCVExtension<"shvstvecd", 1, 0,
                     "'Shvstvecd' (vstvec supports Direct mode)">;
def FeatureStdExtSstvecd
    : RISCVExtension<"sstvecd", 1, 0,
                     "'Sstvecd' (stvec supports Direct mode)">;

def FeatureStdExtSsu64xl
    : RISCVExtension<"ssu64xl", 1, 0,
                     "'Ssu64xl' (UXLEN=64 supported)">;

def FeatureStdExtSvade
    : RISCVExtension<"svade", 1, 0,
                     "'Svade' (Raise exceptions on improper A/D bits)">;

def FeatureStdExtSvadu
    : RISCVExtension<"svadu", 1, 0,
                     "'Svadu' (Hardware A/D updates)">;

def FeatureStdExtSvbare
    : RISCVExtension<"svbare", 1, 0,
                     "'Svbare' $(satp mode Bare supported)">;

def FeatureStdExtSvinval
    : RISCVExtension<"svinval", 1, 0,
                     "'Svinval' (Fine-Grained Address-Translation Cache Invalidation)">;
def HasStdExtSvinval : Predicate<"Subtarget->hasStdExtSvinval()">,
                       AssemblerPredicate<(all_of FeatureStdExtSvinval),
                           "'Svinval' (Fine-Grained Address-Translation Cache Invalidation)">;

def FeatureStdExtSvnapot
    : RISCVExtension<"svnapot", 1, 0,
                     "'Svnapot' (NAPOT Translation Contiguity)">;

def FeatureStdExtSvpbmt
    : RISCVExtension<"svpbmt", 1, 0,
                     "'Svpbmt' (Page-Based Memory Types)">;

// Pointer Masking extensions

// A supervisor-level extension that provides pointer masking for the next lower
// privilege mode (U-mode), and for VS- and VU-modes if the H extension is
// present.
def FeatureStdExtSsnpm
    : RISCVExperimentalExtension<"ssnpm", 1, 0,
                                 "'Ssnpm' (Supervisor-level Pointer Masking for next lower privilege mode)">;

// A machine-level extension that provides pointer masking for the next lower
// privilege mode (S/HS if S-mode is implemented, or U-mode otherwise).
def FeatureStdExtSmnpm
    : RISCVExperimentalExtension<"smnpm", 1, 0,
                                 "'Smnpm' (Machine-level Pointer Masking for next lower privilege mode)">;

// A machine-level extension that provides pointer masking for M-mode.
def FeatureStdExtSmmpm
    : RISCVExperimentalExtension<"smmpm", 1, 0,
                                 "'Smmpm' (Machine-level Pointer Masking for M-mode)">;

// An extension that indicates that there is pointer-masking support available
// in supervisor mode, with some facility provided in the supervisor execution
// environment to control pointer masking.
def FeatureStdExtSspm
    : RISCVExperimentalExtension<"sspm", 1, 0,
                                 "'Sspm' (Indicates Supervisor-mode Pointer Masking)">;

// An extension that indicates that there is pointer-masking support available
// in user mode, with some facility provided in the application execution
// environment to control pointer masking.
def FeatureStdExtSupm
    : RISCVExperimentalExtension<"supm", 1, 0,
                                 "'Supm' (Indicates User-mode Pointer Masking)">;

//===----------------------------------------------------------------------===//
// Vendor extensions
//===----------------------------------------------------------------------===//

// Ventana Extenions

def FeatureVendorXVentanaCondOps
    : RISCVExtension<"xventanacondops", 1, 0,
                     "'XVentanaCondOps' (Ventana Conditional Ops)">;
def HasVendorXVentanaCondOps : Predicate<"Subtarget->hasVendorXVentanaCondOps()">,
                               AssemblerPredicate<(all_of FeatureVendorXVentanaCondOps),
                                   "'XVentanaCondOps' (Ventana Conditional Ops)">;

// T-Head Extensions

def FeatureVendorXTHeadBa
    : RISCVExtension<"xtheadba", 1, 0,
                     "'XTHeadBa' (T-Head address calculation instructions)">;
def HasVendorXTHeadBa : Predicate<"Subtarget->hasVendorXTHeadBa()">,
                        AssemblerPredicate<(all_of FeatureVendorXTHeadBa),
                            "'XTHeadBa' (T-Head address calculation instructions)">;

def FeatureVendorXTHeadBb
    : RISCVExtension<"xtheadbb", 1, 0,
                     "'XTHeadBb' (T-Head basic bit-manipulation instructions)">;
def HasVendorXTHeadBb : Predicate<"Subtarget->hasVendorXTHeadBb()">,
                        AssemblerPredicate<(all_of FeatureVendorXTHeadBb),
                            "'XTHeadBb' (T-Head basic bit-manipulation instructions)">;

def FeatureVendorXTHeadBs
    : RISCVExtension<"xtheadbs", 1, 0,
                     "'XTHeadBs' (T-Head single-bit instructions)">;
def HasVendorXTHeadBs : Predicate<"Subtarget->hasVendorXTHeadBs()">,
                        AssemblerPredicate<(all_of FeatureVendorXTHeadBs),
                            "'XTHeadBs' (T-Head single-bit instructions)">;

def FeatureVendorXTHeadCondMov
    : RISCVExtension<"xtheadcondmov", 1, 0,
                     "'XTHeadCondMov' (T-Head conditional move instructions)">;
def HasVendorXTHeadCondMov : Predicate<"Subtarget->hasVendorXTHeadCondMov()">,
                             AssemblerPredicate<(all_of FeatureVendorXTHeadCondMov),
                                 "'XTHeadCondMov' (T-Head conditional move instructions)">;

def FeatureVendorXTHeadCmo
    : RISCVExtension<"xtheadcmo", 1, 0,
                     "'XTHeadCmo' (T-Head cache management instructions)">;
def HasVendorXTHeadCmo : Predicate<"Subtarget->hasVendorXTHeadCmo()">,
                         AssemblerPredicate<(all_of FeatureVendorXTHeadCmo),
                             "'XTHeadCmo' (T-Head cache management instructions)">;

def FeatureVendorXTHeadFMemIdx
    : RISCVExtension<"xtheadfmemidx", 1, 0,
                     "'XTHeadFMemIdx' (T-Head FP Indexed Memory Operations)">;
def HasVendorXTHeadFMemIdx : Predicate<"Subtarget->hasVendorXTHeadFMemIdx()">,
                             AssemblerPredicate<(all_of FeatureVendorXTHeadFMemIdx),
                                 "'XTHeadFMemIdx' (T-Head FP Indexed Memory Operations)">;

def FeatureVendorXTHeadMac
    : RISCVExtension<"xtheadmac", 1, 0,
                     "'XTHeadMac' (T-Head Multiply-Accumulate Instructions)">;
def HasVendorXTHeadMac : Predicate<"Subtarget->hasVendorXTHeadMac()">,
                         AssemblerPredicate<(all_of FeatureVendorXTHeadMac),
                             "'XTHeadMac' (T-Head Multiply-Accumulate Instructions)">;

def FeatureVendorXTHeadMemIdx
    : RISCVExtension<"xtheadmemidx", 1, 0,
                     "'XTHeadMemIdx' (T-Head Indexed Memory Operations)">;
def HasVendorXTHeadMemIdx : Predicate<"Subtarget->hasVendorXTHeadMemIdx()">,
                            AssemblerPredicate<(all_of FeatureVendorXTHeadMemIdx),
                                "'XTHeadMemIdx' (T-Head Indexed Memory Operations)">;

def FeatureVendorXTHeadMemPair
    : RISCVExtension<"xtheadmempair", 1, 0,
                     "'XTHeadMemPair' (T-Head two-GPR Memory Operations)">;
def HasVendorXTHeadMemPair : Predicate<"Subtarget->hasVendorXTHeadMemPair()">,
                             AssemblerPredicate<(all_of FeatureVendorXTHeadMemPair),
                                 "'XTHeadMemPair' (T-Head two-GPR Memory Operations)">;

def FeatureVendorXTHeadSync
    : RISCVExtension<"xtheadsync", 1, 0,
                     "'XTHeadSync' (T-Head multicore synchronization instructions)">;
def HasVendorXTHeadSync : Predicate<"Subtarget->hasVendorXTHeadSync()">,
                          AssemblerPredicate<(all_of FeatureVendorXTHeadSync),
                              "'XTHeadSync' (T-Head multicore synchronization instructions)">;

def FeatureVendorXTHeadVdot
    : RISCVExtension<"xtheadvdot", 1, 0,
                     "'XTHeadVdot' (T-Head Vector Extensions for Dot)",
                     [FeatureStdExtV]>;
def HasVendorXTHeadVdot : Predicate<"Subtarget->hasVendorXTHeadVdot()">,
                          AssemblerPredicate<(all_of FeatureVendorXTHeadVdot),
                              "'XTHeadVdot' (T-Head Vector Extensions for Dot)">;

// SiFive Extensions

def FeatureVendorXSfvcp
    : RISCVExtension<"xsfvcp", 1, 0,
                     "'XSfvcp' (SiFive Custom Vector Coprocessor Interface Instructions)",
                     [FeatureStdExtZve32x]>;
def HasVendorXSfvcp : Predicate<"Subtarget->hasVendorXSfvcp()">,
                      AssemblerPredicate<(all_of FeatureVendorXSfvcp),
                          "'XSfvcp' (SiFive Custom Vector Coprocessor Interface Instructions)">;

def FeatureVendorXSfvqmaccdod
    : RISCVExtension<"xsfvqmaccdod", 1, 0,
                     "'XSfvqmaccdod' (SiFive Int8 Matrix Multiplication Instructions (2-by-8 and 8-by-2))",
                     [FeatureStdExtZve32x]>;
def HasVendorXSfvqmaccdod
    : Predicate<"Subtarget->hasVendorXSfvqmaccdod()">,
      AssemblerPredicate<(all_of FeatureVendorXSfvqmaccdod),
                         "'XSfvqmaccdod' (SiFive Int8 Matrix Multiplication Instructions (2-by-8 and 8-by-2))">;

def FeatureVendorXSfvqmaccqoq
    : RISCVExtension<"xsfvqmaccqoq", 1, 0,
                     "'XSfvqmaccqoq' (SiFive Int8 Matrix Multiplication Instructions (4-by-8 and 8-by-4))",
                     [FeatureStdExtZve32x]>;
def HasVendorXSfvqmaccqoq
    : Predicate<"Subtarget->hasVendorXSfvqmaccqoq()">,
      AssemblerPredicate<(all_of FeatureVendorXSfvqmaccqoq),
                         "'XSfvqmaccqoq' (SiFive Int8 Matrix Multiplication Instructions (4-by-8 and 8-by-4))">;

def FeatureVendorXSfvfwmaccqqq
    : RISCVExtension<"xsfvfwmaccqqq", 1, 0,
                     "'XSfvfwmaccqqq' (SiFive Matrix Multiply Accumulate Instruction and 4-by-4))",
                     [FeatureStdExtZvfbfmin]>;
def HasVendorXSfvfwmaccqqq
    : Predicate<"Subtarget->hasVendorXSfvfwmaccqqq()">,
      AssemblerPredicate<(all_of FeatureVendorXSfvfwmaccqqq),
                         "'XSfvfwmaccqqq' (SiFive Matrix Multiply Accumulate Instruction and 4-by-4))">;

def FeatureVendorXSfvfnrclipxfqf
    : RISCVExtension<"xsfvfnrclipxfqf", 1, 0,
                     "'XSfvfnrclipxfqf' (SiFive FP32-to-int8 Ranged Clip Instructions)",
                     [FeatureStdExtZve32f]>;
def HasVendorXSfvfnrclipxfqf
    : Predicate<"Subtarget->hasVendorXSfvfnrclipxfqf()">,
      AssemblerPredicate<(all_of FeatureVendorXSfvfnrclipxfqf),
                         "'XSfvfnrclipxfqf' (SiFive FP32-to-int8 Ranged Clip Instructions)">;

def FeatureVendorXSiFivecdiscarddlone
    : RISCVExtension<"xsifivecdiscarddlone", 1, 0,
                     "'XSiFivecdiscarddlone' (SiFive sf.cdiscard.d.l1 Instruction)", []>;
def HasVendorXSiFivecdiscarddlone
    : Predicate<"Subtarget->hasVendorXSiFivecdiscarddlone()">,
      AssemblerPredicate<(all_of FeatureVendorXSiFivecdiscarddlone),
                         "'XSiFivecdiscarddlone' (SiFive sf.cdiscard.d.l1 Instruction)">;

def FeatureVendorXSiFivecflushdlone
    : RISCVExtension<"xsifivecflushdlone", 1, 0,
                     "'XSiFivecflushdlone' (SiFive sf.cflush.d.l1 Instruction)", []>;
def HasVendorXSiFivecflushdlone
    : Predicate<"Subtarget->hasVendorXSiFivecflushdlone()">,
      AssemblerPredicate<(all_of FeatureVendorXSiFivecflushdlone),
                         "'XSiFivecflushdlone' (SiFive sf.cflush.d.l1 Instruction)">;

def FeatureVendorXSfcease
    : RISCVExtension<"xsfcease", 1, 0,
                     "'XSfcease' (SiFive sf.cease Instruction)", []>;
def HasVendorXSfcease
    : Predicate<"Subtarget->hasVendorXSfcease()">,
      AssemblerPredicate<(all_of FeatureVendorXSfcease),
                         "'XSfcease' (SiFive sf.cease Instruction)">;

// Core-V Extensions

def FeatureVendorXCVelw
   : RISCVExtension<"xcvelw", 1, 0,
                    "'XCVelw' (CORE-V Event Load Word)">;
def HasVendorXCVelw
   : Predicate<"Subtarget->hasVendorXCVelw()">,
     AssemblerPredicate<(any_of FeatureVendorXCVelw),
                        "'XCVelw' (CORE-V Event Load Word)">;

def FeatureVendorXCVbitmanip
    : RISCVExtension<"xcvbitmanip", 1, 0,
                     "'XCVbitmanip' (CORE-V Bit Manipulation)">;
def HasVendorXCVbitmanip
    : Predicate<"Subtarget->hasVendorXCVbitmanip()">,
      AssemblerPredicate<(all_of FeatureVendorXCVbitmanip),
                         "'XCVbitmanip' (CORE-V Bit Manipulation)">;

def FeatureVendorXCVmac
    : RISCVExtension<"xcvmac", 1, 0,
                     "'XCVmac' (CORE-V Multiply-Accumulate)">;
def HasVendorXCVmac
    : Predicate<"Subtarget->hasVendorXCVmac()">,
      AssemblerPredicate<(all_of FeatureVendorXCVmac),
                         "'XCVmac' (CORE-V Multiply-Accumulate)">;

def FeatureVendorXCVmem
    : RISCVExtension<"xcvmem", 1, 0,
                     "'XCVmem' (CORE-V Post-incrementing Load & Store)">;
def HasVendorXCVmem
    : Predicate<"Subtarget->hasVendorXCVmem()">,
      AssemblerPredicate<(any_of FeatureVendorXCVmem),
                         "'XCVmem' (CORE-V Post-incrementing Load & Store)">;

def FeatureVendorXCValu
    : RISCVExtension<"xcvalu", 1, 0,
                     "'XCValu' (CORE-V ALU Operations)">;
def HasVendorXCValu
    : Predicate<"Subtarget->hasVendorXCValu()">,
      AssemblerPredicate<(all_of FeatureVendorXCValu),
                         "'XCValu' (CORE-V ALU Operations)">;

def FeatureVendorXCVsimd
    : RISCVExtension<"xcvsimd", 1, 0,
                     "'XCVsimd' (CORE-V SIMD ALU)">;
def HasVendorXCVsimd
    : Predicate<"Subtarget->hasVendorXCVsimd()">,
      AssemblerPredicate<(any_of FeatureVendorXCVsimd),
                         "'XCVsimd' (CORE-V SIMD ALU)">;

def FeatureVendorXCVbi
    : RISCVExtension<"xcvbi", 1, 0,
                     "'XCVbi' (CORE-V Immediate Branching)">;
def HasVendorXCVbi
    : Predicate<"Subtarget->hasVendorXCVbi()">,
      AssemblerPredicate<(all_of FeatureVendorXCVbi),
                         "'XCVbi' (CORE-V Immediate Branching)">;

// WCH / Nanjing Qinheng Microelectronics Extension(s)

def FeatureVendorXwchc
    : RISCVExtension<"xwchc", 2, 2,
                     "'Xwchc' (WCH/QingKe additional compressed opcodes)">;
def HasVendorXwchc
    : Predicate<"Subtarget->hasVendorXwchc()">,
      AssemblerPredicate<(all_of FeatureVendorXwchc),
                         "'Xwchc' (WCH/QingKe additional compressed opcodes)">;

//===----------------------------------------------------------------------===//
// LLVM specific features and extensions
//===----------------------------------------------------------------------===//

// Feature32Bit exists to mark CPUs that support RV32 to distinquish them from
// tuning CPU names.
def Feature32Bit
    : SubtargetFeature<"32bit", "IsRV32", "true", "Implements RV32">;
def Feature64Bit
    : SubtargetFeature<"64bit", "IsRV64", "true", "Implements RV64">;
def IsRV64 : Predicate<"Subtarget->is64Bit()">,
             AssemblerPredicate<(all_of Feature64Bit),
                                "RV64I Base Instruction Set">;
def IsRV32 : Predicate<"!Subtarget->is64Bit()">,
             AssemblerPredicate<(all_of (not Feature64Bit)),
                                "RV32I Base Instruction Set">;

defvar RV32 = DefaultMode;
def RV64           : HwMode<"+64bit", [IsRV64]>;

def FeatureRelax
    : SubtargetFeature<"relax", "EnableLinkerRelax", "true",
                       "Enable Linker relaxation.">;

foreach i = {1-31} in
  def FeatureReserveX#i :
      SubtargetFeature<"reserve-x"#i, "UserReservedRegister[RISCV::X"#i#"]",
                       "true", "Reserve X"#i>;

def FeatureSaveRestore : SubtargetFeature<"save-restore", "EnableSaveRestore",
                                          "true", "Enable save/restore.">;

def FeatureNoTrailingSeqCstFence : SubtargetFeature<"no-trailing-seq-cst-fence",
                                          "EnableTrailingSeqCstFence",
                                          "false",
                                          "Disable trailing fence for seq-cst store.">;

def FeatureUnalignedScalarMem
   : SubtargetFeature<"unaligned-scalar-mem", "EnableUnalignedScalarMem",
                      "true", "Has reasonably performant unaligned scalar "
                      "loads and stores">;

def FeatureUnalignedVectorMem
   : SubtargetFeature<"unaligned-vector-mem", "EnableUnalignedVectorMem",
                      "true", "Has reasonably performant unaligned vector "
                      "loads and stores">;

def FeaturePostRAScheduler : SubtargetFeature<"use-postra-scheduler",
    "UsePostRAScheduler", "true", "Schedule again after register allocation">;

def FeaturePredictableSelectIsExpensive
    : SubtargetFeature<"predictable-select-expensive", "PredictableSelectIsExpensive", "true",
                       "Prefer likely predicted branches over selects">;

def TuneOptimizedZeroStrideLoad
   : SubtargetFeature<"optimized-zero-stride-load", "HasOptimizedZeroStrideLoad",
                      "true", "Optimized (perform fewer memory operations)"
                      "zero-stride vector load">;

def Experimental
   : SubtargetFeature<"experimental", "HasExperimental",
                      "true", "Experimental intrinsics">;

// Some vector hardware implementations do not process all VLEN bits in parallel
// and instead split over multiple cycles. DLEN refers to the datapath width
// that can be done in parallel.
def TuneDLenFactor2
   : SubtargetFeature<"dlen-factor-2", "DLenFactor2", "true",
                      "Vector unit DLEN(data path width) is half of VLEN">;

def TuneNoDefaultUnroll
    : SubtargetFeature<"no-default-unroll", "EnableDefaultUnroll", "false",
                       "Disable default unroll preference.">;

// SiFive 7 is able to fuse integer ALU operations with a preceding branch
// instruction.
def TuneShortForwardBranchOpt
    : SubtargetFeature<"short-forward-branch-opt", "HasShortForwardBranchOpt",
                       "true", "Enable short forward branch optimization">;
def HasShortForwardBranchOpt : Predicate<"Subtarget->hasShortForwardBranchOpt()">;
def NoShortForwardBranchOpt : Predicate<"!Subtarget->hasShortForwardBranchOpt()">;

// Some subtargets require a S2V transfer buffer to move scalars into vectors.
// FIXME: Forming .vx/.vf/.wx/.wf can reduce register pressure.
def TuneNoSinkSplatOperands
    : SubtargetFeature<"no-sink-splat-operands", "SinkSplatOperands",
                       "false", "Disable sink splat operands to enable .vx, .vf,"
                       ".wx, and .wf instructions">;

def TunePreferWInst
    : SubtargetFeature<"prefer-w-inst", "PreferWInst", "true",
                       "Prefer instructions with W suffix">;

def TuneConditionalCompressedMoveFusion
    : SubtargetFeature<"conditional-cmv-fusion", "HasConditionalCompressedMoveFusion",
                       "true", "Enable branch+c.mv fusion">;
def HasConditionalMoveFusion : Predicate<"Subtarget->hasConditionalMoveFusion()">;
def NoConditionalMoveFusion  : Predicate<"!Subtarget->hasConditionalMoveFusion()">;

def TuneSiFive7 : SubtargetFeature<"sifive7", "RISCVProcFamily", "SiFive7",
                                   "SiFive 7-Series processors",
                                   [TuneNoDefaultUnroll,
                                    TuneShortForwardBranchOpt]>;

def TuneVentanaVeyron : SubtargetFeature<"ventana-veyron", "RISCVProcFamily", "VentanaVeyron",
                                         "Ventana Veyron-Series processors">;

// Assume that lock-free native-width atomics are available, even if the target
// and operating system combination would not usually provide them. The user
// is responsible for providing any necessary __sync implementations. Code
// built with this feature is not ABI-compatible with code built without this
// feature, if atomic variables are exposed across the ABI boundary.
def FeatureForcedAtomics : SubtargetFeature<
    "forced-atomics", "HasForcedAtomics", "true",
    "Assume that lock-free native-width atomics are available">;
def HasAtomicLdSt
    : Predicate<"Subtarget->hasStdExtA() || Subtarget->hasForcedAtomics()">;

def FeatureTaggedGlobals : SubtargetFeature<"tagged-globals",
    "AllowTaggedGlobals",
    "true", "Use an instruction sequence for taking the address of a global "
    "that allows a memory tag in the upper address bits">;

def FeatureForcedSWShadowStack : SubtargetFeature<
    "forced-sw-shadow-stack", "HasForcedSWShadowStack", "true",
    "Implement shadow stack with software.">;
def HasForcedSWShadowStack : Predicate<"Subtarget->hasForcedSWShadowStack()">;
