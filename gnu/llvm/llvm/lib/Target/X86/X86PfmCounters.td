//===-- X86PfmCounters.td - X86 Hardware Counters ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This describes the available hardware counters for various subtargets.
//
//===----------------------------------------------------------------------===//

def UnhaltedCoreCyclesPfmCounter : PfmCounter<"unhalted_core_cycles">;
def UopsIssuedPfmCounter : PfmCounter<"uops_issued:any">;

// No default counters on X86.
def DefaultPfmCounters : ProcPfmCounters {}
def : PfmCountersDefaultBinding<DefaultPfmCounters>;

// Intel X86 Counters.
defvar DefaultIntelPfmValidationCounters = [
  PfmValidationCounter<InstructionRetired, "INSTRUCTIONS_RETIRED">,
  PfmValidationCounter<L1DCacheLoadMiss, "MEM_LOAD_UOPS_RETIRED:L1_MISS">,
  PfmValidationCounter<L1ICacheLoadMiss, "L1-ICACHE-LOAD-MISSES">,
  PfmValidationCounter<DataTLBLoadMiss, "DTLB_LOAD_MISSES:MISS_CAUSES_A_WALK">,
  PfmValidationCounter<DataTLBStoreMiss, "DTLB_STORE_MISSES:MISS_CAUSES_A_WALK">,
  PfmValidationCounter<InstructionTLBLoadMiss, "ITLB_MISSES:MISS_CAUSES_A_WALK">,
  PfmValidationCounter<BranchPredictionMiss, "BRANCH-MISSES">
];

def PentiumPfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cpu_clk_unhalted">;
  let UopsCounter = PfmCounter<"uops_retired">;
}
def : PfmCountersBinding<"pentiumpro", PentiumPfmCounters>;
def : PfmCountersBinding<"pentium2", PentiumPfmCounters>;
def : PfmCountersBinding<"pentium3", PentiumPfmCounters>;
def : PfmCountersBinding<"pentium3m", PentiumPfmCounters>;
def : PfmCountersBinding<"pentium-m", PentiumPfmCounters>;

def CorePfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = PfmCounter<"uops_retired:any">;
}
def : PfmCountersBinding<"yonah", CorePfmCounters>;
def : PfmCountersBinding<"prescott", CorePfmCounters>;

def AtomPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = PfmCounter<"uops_retired:any">;
}
def : PfmCountersBinding<"bonnell", AtomPfmCounters>;
def : PfmCountersBinding<"atom", AtomPfmCounters>;

def SLMPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = PfmCounter<"uops_retired:any">;
  let IssueCounters = [
    PfmIssueCounter<"SLM_MEC_RSV", "mem_uop_retired:any_ld + mem_uop_retired:any_st">
  ];
}
def : PfmCountersBinding<"silvermont", SLMPfmCounters>;
def : PfmCountersBinding<"goldmont", SLMPfmCounters>;
def : PfmCountersBinding<"goldmont-plus", SLMPfmCounters>;
def : PfmCountersBinding<"tremont", SLMPfmCounters>;

def KnightPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = PfmCounter<"uops_retired:all">;
}
def : PfmCountersBinding<"knl", KnightPfmCounters>;
def : PfmCountersBinding<"knm", KnightPfmCounters>;

def Core2PfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = PfmCounter<"uops_retired:any">;
  let IssueCounters = [
    PfmIssueCounter<"SBPort0",  "rs_uops_dispatched_cycles:port_0">,
    PfmIssueCounter<"SBPort1",  "rs_uops_dispatched_cycles:port_1">,
    PfmIssueCounter<"SBPort23", "rs_uops_dispatched_cycles:port_2 + rs_uops_dispatched_cycles:port_3">,
    PfmIssueCounter<"SBPort4",  "rs_uops_dispatched_cycles:port_4">,
    PfmIssueCounter<"SBPort5",  "rs_uops_dispatched_cycles:port_5">
  ];
}
def : PfmCountersBinding<"core2", Core2PfmCounters>;
def : PfmCountersBinding<"penryn", Core2PfmCounters>;

def NehalemPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = PfmCounter<"uops_retired:any">;
  let IssueCounters = [
    PfmIssueCounter<"SBPort0",  "uops_executed:port0">,
    PfmIssueCounter<"SBPort1",  "uops_executed:port1">,
    PfmIssueCounter<"SBPort23", "uops_executed:port2_core + uops_executed:port3_core">,
    PfmIssueCounter<"SBPort4",  "uops_executed:port4_core">,
    PfmIssueCounter<"SBPort5",  "uops_executed:port5">
  ];
}
def : PfmCountersBinding<"nehalem", NehalemPfmCounters>;
def : PfmCountersBinding<"corei7", NehalemPfmCounters>;
def : PfmCountersBinding<"westmere", NehalemPfmCounters>;

def SandyBridgePfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = UopsIssuedPfmCounter;
  let IssueCounters = [
    PfmIssueCounter<"SBPort0",  "uops_dispatched_port:port_0">,
    PfmIssueCounter<"SBPort1",  "uops_dispatched_port:port_1">,
    PfmIssueCounter<"SBPort23", "uops_dispatched_port:port_2 + uops_dispatched_port:port_3">,
    PfmIssueCounter<"SBPort4",  "uops_dispatched_port:port_4">,
    PfmIssueCounter<"SBPort5",  "uops_dispatched_port:port_5">
  ];
  let ValidationCounters = DefaultIntelPfmValidationCounters;
}
def : PfmCountersBinding<"sandybridge", SandyBridgePfmCounters>;
def : PfmCountersBinding<"ivybridge", SandyBridgePfmCounters>;

def HaswellPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = UopsIssuedPfmCounter;
  let IssueCounters = [
    PfmIssueCounter<"HWPort0", "uops_executed_port:port_0">,
    PfmIssueCounter<"HWPort1", "uops_executed_port:port_1">,
    PfmIssueCounter<"HWPort2", "uops_executed_port:port_2">,
    PfmIssueCounter<"HWPort3", "uops_executed_port:port_3">,
    PfmIssueCounter<"HWPort4", "uops_executed_port:port_4">,
    PfmIssueCounter<"HWPort5", "uops_executed_port:port_5">,
    PfmIssueCounter<"HWPort6", "uops_executed_port:port_6">,
    PfmIssueCounter<"HWPort7", "uops_executed_port:port_7">
  ];
  let ValidationCounters = DefaultIntelPfmValidationCounters;
}
def : PfmCountersBinding<"haswell", HaswellPfmCounters>;

def BroadwellPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = UopsIssuedPfmCounter;
  let IssueCounters = [
    PfmIssueCounter<"BWPort0", "uops_executed_port:port_0">,
    PfmIssueCounter<"BWPort1", "uops_executed_port:port_1">,
    PfmIssueCounter<"BWPort2", "uops_executed_port:port_2">,
    PfmIssueCounter<"BWPort3", "uops_executed_port:port_3">,
    PfmIssueCounter<"BWPort4", "uops_executed_port:port_4">,
    PfmIssueCounter<"BWPort5", "uops_executed_port:port_5">,
    PfmIssueCounter<"BWPort6", "uops_executed_port:port_6">,
    PfmIssueCounter<"BWPort7", "uops_executed_port:port_7">
  ];
  let ValidationCounters = DefaultIntelPfmValidationCounters;
}
def : PfmCountersBinding<"broadwell", BroadwellPfmCounters>;

def SkylakeClientPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = UopsIssuedPfmCounter;
  let IssueCounters = [
    PfmIssueCounter<"SKLPort0", "uops_dispatched_port:port_0">,
    PfmIssueCounter<"SKLPort1", "uops_dispatched_port:port_1">,
    PfmIssueCounter<"SKLPort2", "uops_dispatched_port:port_2">,
    PfmIssueCounter<"SKLPort3", "uops_dispatched_port:port_3">,
    PfmIssueCounter<"SKLPort4", "uops_dispatched_port:port_4">,
    PfmIssueCounter<"SKLPort5", "uops_dispatched_port:port_5">,
    PfmIssueCounter<"SKLPort6", "uops_dispatched_port:port_6">,
    PfmIssueCounter<"SKLPort7", "uops_dispatched_port:port_7">
  ];
  let ValidationCounters = DefaultIntelPfmValidationCounters;
}
def : PfmCountersBinding<"skylake", SkylakeClientPfmCounters>;

def SkylakeServerPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = UopsIssuedPfmCounter;
  let IssueCounters = [
    PfmIssueCounter<"SKXPort0", "uops_dispatched_port:port_0">,
    PfmIssueCounter<"SKXPort1", "uops_dispatched_port:port_1">,
    PfmIssueCounter<"SKXPort2", "uops_dispatched_port:port_2">,
    PfmIssueCounter<"SKXPort3", "uops_dispatched_port:port_3">,
    PfmIssueCounter<"SKXPort4", "uops_dispatched_port:port_4">,
    PfmIssueCounter<"SKXPort5", "uops_dispatched_port:port_5">,
    PfmIssueCounter<"SKXPort6", "uops_dispatched_port:port_6">,
    PfmIssueCounter<"SKXPort7", "uops_dispatched_port:port_7">
  ];
  let ValidationCounters = DefaultIntelPfmValidationCounters;
}
def : PfmCountersBinding<"skylake-avx512", SkylakeServerPfmCounters>;
def : PfmCountersBinding<"cascadelake", SkylakeServerPfmCounters>;
def : PfmCountersBinding<"cannonlake", SkylakeServerPfmCounters>;

def IceLakePfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = UopsIssuedPfmCounter;
  let IssueCounters = [
    PfmIssueCounter<"ICXPort0",  "uops_dispatched_port:port_0">,
    PfmIssueCounter<"ICXPort1",  "uops_dispatched_port:port_1">,
    PfmIssueCounter<"ICXPort23", "uops_dispatched_port:port_2_3">,
    PfmIssueCounter<"ICXPort49", "uops_dispatched_port:port_4_9">,
    PfmIssueCounter<"ICXPort5",  "uops_dispatched_port:port_5">,
    PfmIssueCounter<"ICXPort6",  "uops_dispatched_port:port_6">,
    PfmIssueCounter<"ICXPort78", "uops_dispatched_port:port_7_8">
  ];
  let ValidationCounters = DefaultIntelPfmValidationCounters;
}
def : PfmCountersBinding<"icelake-client", IceLakePfmCounters>;
def : PfmCountersBinding<"icelake-server", IceLakePfmCounters>;
def : PfmCountersBinding<"rocketlake", IceLakePfmCounters>;
def : PfmCountersBinding<"tigerlake", IceLakePfmCounters>;

def AlderLakePfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = UopsIssuedPfmCounter;
  let IssueCounters = [
    PfmIssueCounter<"ADLPPort00", "uops_dispatched_port:port_0">,
    PfmIssueCounter<"ADLPPort01", "uops_dispatched_port:port_1">,
    PfmIssueCounter<"ADLPPort02_03_10", "uops_dispatched_port:port_2_3_10">,
    PfmIssueCounter<"ADLPPort04_09", "uops_dispatched_port:port_4_9">,
    PfmIssueCounter<"ADLPPort05_11", "uops_dispatched_port:port_5_11">,
    PfmIssueCounter<"ADLPPort06", "uops_dispatched_port:port_6">,
    PfmIssueCounter<"ADLPPort07_08", "uops_dispatched_port:port_7_8">
  ];
  let ValidationCounters = DefaultIntelPfmValidationCounters;
}
def : PfmCountersBinding<"alderlake", AlderLakePfmCounters>;

// AMD X86 Counters.
defvar DefaultAMDPfmValidationCounters = [
  PfmValidationCounter<InstructionRetired, "RETIRED_INSTRUCTIONS">,
  PfmValidationCounter<L1DCacheLoadMiss, "L1-DCACHE-LOAD-MISSES">,
  PfmValidationCounter<L1DCacheStoreMiss, "L1-DCACHE-STORE-MISSES">,
  PfmValidationCounter<L1ICacheLoadMiss, "L1-ICACHE-LOAD-MISSES">,
  PfmValidationCounter<DataTLBLoadMiss, "DTLB-LOAD-MISSES">,
  PfmValidationCounter<InstructionTLBLoadMiss, "ITLB-LOAD-MISSES">,
  PfmValidationCounter<BranchPredictionMiss, "BRANCH-MISSES">
];

// Set basic counters for AMD cpus that we know libpfm4 supports.
def DefaultAMDPfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cpu_clk_unhalted">;
  let UopsCounter = PfmCounter<"retired_uops">;
}
def : PfmCountersBinding<"athlon", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"athlon-tbird", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"athlon-4", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"athlon-xp", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"athlon-mp", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"k8", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"opteron", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"athlon64", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"athlon-fx", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"k8-sse3", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"opteron-sse3", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"athlon64-sse3", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"amdfam10", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"barcelona", DefaultAMDPfmCounters>;

def BdVer2PfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cpu_clk_unhalted">;
  let UopsCounter = PfmCounter<"retired_uops">;
  let IssueCounters = [
    PfmIssueCounter<"PdFPU0", "dispatched_fpu_ops:ops_pipe0 + dispatched_fpu_ops:ops_dual_pipe0">,
    PfmIssueCounter<"PdFPU1", "dispatched_fpu_ops:ops_pipe1 + dispatched_fpu_ops:ops_dual_pipe1">,
    PfmIssueCounter<"PdFPU2", "dispatched_fpu_ops:ops_pipe2 + dispatched_fpu_ops:ops_dual_pipe2">,
    PfmIssueCounter<"PdFPU3", "dispatched_fpu_ops:ops_pipe3 + dispatched_fpu_ops:ops_dual_pipe3">
  ];
}
def : PfmCountersBinding<"bdver1", BdVer2PfmCounters>;
def : PfmCountersBinding<"bdver2", BdVer2PfmCounters>;

def BdVer3PfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cpu_clk_unhalted">;
  let UopsCounter = PfmCounter<"retired_uops">;
  let IssueCounters = [
    PfmIssueCounter<"SrFPU0", "dispatched_fpu_ops:ops_pipe0 + dispatched_fpu_ops:ops_dual_pipe0">,
    PfmIssueCounter<"SrFPU1", "dispatched_fpu_ops:ops_pipe1 + dispatched_fpu_ops:ops_dual_pipe1">,
    PfmIssueCounter<"SrFPU2", "dispatched_fpu_ops:ops_pipe2 + dispatched_fpu_ops:ops_dual_pipe2">
  ];
}
def : PfmCountersBinding<"bdver3", BdVer3PfmCounters>;
def : PfmCountersBinding<"bdver4", BdVer3PfmCounters>;

def BtVer1PfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cpu_clk_unhalted">;
  let UopsCounter = PfmCounter<"retired_uops">;
  let IssueCounters = [
    PfmIssueCounter<"BtFPU0", "dispatched_fpu:pipe0">,
    PfmIssueCounter<"BtFPU1", "dispatched_fpu:pipe1">
  ];
}
def : PfmCountersBinding<"btver1", BtVer1PfmCounters>;

def BtVer2PfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cpu_clk_unhalted">;
  let UopsCounter = PfmCounter<"retired_uops">;
  let IssueCounters = [
    PfmIssueCounter<"JFPU0", "dispatched_fpu:pipe0">,
    PfmIssueCounter<"JFPU1", "dispatched_fpu:pipe1">
  ];
}
def : PfmCountersBinding<"btver2", BtVer2PfmCounters>;

def ZnVer1PfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cycles_not_in_halt">;
  let UopsCounter = PfmCounter<"retired_uops">;
  let IssueCounters = [
    PfmIssueCounter<"ZnFPU0", "fpu_pipe_assignment:total0">,
    PfmIssueCounter<"ZnFPU1", "fpu_pipe_assignment:total1">,
    PfmIssueCounter<"ZnFPU2", "fpu_pipe_assignment:total2">,
    PfmIssueCounter<"ZnFPU3", "fpu_pipe_assignment:total3">,
    PfmIssueCounter<"ZnAGU", "ls_dispatch:ld_st_dispatch + ls_dispatch:ld_dispatch + ls_dispatch:store_dispatch">,
    PfmIssueCounter<"ZnDivider", "div_op_count">
  ];
  let ValidationCounters = DefaultAMDPfmValidationCounters;
}
def : PfmCountersBinding<"znver1", ZnVer1PfmCounters>;

def ZnVer2PfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cycles_not_in_halt">;
  let UopsCounter = PfmCounter<"retired_uops">;
  let IssueCounters = [
    PfmIssueCounter<"Zn2AGU", "ls_dispatch:ld_st_dispatch + ls_dispatch:ld_dispatch + ls_dispatch:store_dispatch">,
    PfmIssueCounter<"Zn2Divider", "div_op_count">
  ];
  let ValidationCounters = DefaultAMDPfmValidationCounters;
}
def : PfmCountersBinding<"znver2", ZnVer2PfmCounters>;

def ZnVer3PfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cycles_not_in_halt">;
  let UopsCounter = PfmCounter<"retired_ops">;
  let IssueCounters = [
    PfmIssueCounter<"Zn3Int", "ops_type_dispatched_from_decoder:int_disp_retire_mode">,
    PfmIssueCounter<"Zn3FPU", "ops_type_dispatched_from_decoder:fp_disp_retire_mode">,
    PfmIssueCounter<"Zn3Load", "ls_dispatch:ld_dispatch">,
    PfmIssueCounter<"Zn3Store", "ls_dispatch:store_dispatch">,
    PfmIssueCounter<"Zn3Divider", "div_op_count">
  ];
  let ValidationCounters = DefaultAMDPfmValidationCounters;
}
def : PfmCountersBinding<"znver3", ZnVer3PfmCounters>;

def ZnVer4PfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cycles_not_in_halt">;
  let UopsCounter = PfmCounter<"retired_ops">;
  let IssueCounters = [
    PfmIssueCounter<"Zn4Int", "ops_type_dispatched_from_decoder:int_disp_retire_mode">,
    PfmIssueCounter<"Zn4FPU", "ops_type_dispatched_from_decoder:fp_disp_retire_mode">,
    PfmIssueCounter<"Zn4Load", "ls_dispatch:ld_dispatch">,
    PfmIssueCounter<"Zn4Store", "ls_dispatch:store_dispatch">,
    PfmIssueCounter<"Zn4Divider", "div_op_count">,
    PfmIssueCounter<"Zn4AGU", "ls_dispatch:ld_st_dispatch + ls_dispatch:ld_dispatch + ls_dispatch:store_dispatch">
  ];
  let ValidationCounters = DefaultAMDPfmValidationCounters;
}
def : PfmCountersBinding<"znver4", ZnVer4PfmCounters>;
def : PfmCountersBinding<"znver5", ZnVer4PfmCounters>;
