//===- TargetItinerary.td - Target Itinerary Description --*- tablegen -*-====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the target-independent scheduling interfaces
// which should be implemented by each target that uses instruction
// itineraries for scheduling. Itineraries are detailed reservation
// tables for each instruction class. They are most appropriate for
// in-order machine with complicated scheduling or bundling constraints.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Processor functional unit - These values represent the function units
// available across all chip sets for the target.  Eg., IntUnit, FPUnit, ...
// These may be independent values for each chip set or may be shared across
// all chip sets of the target.  Each functional unit is treated as a resource
// during scheduling and has an affect instruction order based on availability
// during a time interval.
//
class FuncUnit;

//===----------------------------------------------------------------------===//
// Pipeline bypass / forwarding - These values specifies the symbolic names of
// pipeline bypasses which can be used to forward results of instructions
// that are forwarded to uses.
class Bypass;
def NoBypass : Bypass;

class ReservationKind<bits<1> val> {
  int Value = val;
}

def Required : ReservationKind<0>;
def Reserved : ReservationKind<1>;

//===----------------------------------------------------------------------===//
// Instruction stage - These values represent a non-pipelined step in
// the execution of an instruction.  Cycles represents the number of
// discrete time slots needed to complete the stage.  Units represent
// the choice of functional units that can be used to complete the
// stage.  Eg. IntUnit1, IntUnit2. TimeInc indicates how many cycles
// should elapse from the start of this stage to the start of the next
// stage in the itinerary.  For example:
//
// A stage is specified in one of two ways:
//
//   InstrStage<1, [FU_x, FU_y]>     - TimeInc defaults to Cycles
//   InstrStage<1, [FU_x, FU_y], 0>  - TimeInc explicit
//

class InstrStage<int cycles, list<FuncUnit> units,
                 int timeinc = -1,
                 ReservationKind kind = Required> {
  int Cycles          = cycles;       // length of stage in machine cycles
  list<FuncUnit> Units = units;       // choice of functional units
  int TimeInc         = timeinc;      // cycles till start of next stage
  int Kind            = kind.Value;   // kind of FU reservation
}

//===----------------------------------------------------------------------===//
// Instruction itinerary - An itinerary represents a sequential series of steps
// required to complete an instruction.  Itineraries are represented as lists of
// instruction stages.
//

//===----------------------------------------------------------------------===//
// Instruction itinerary classes - These values represent 'named' instruction
// itinerary.  Using named itineraries simplifies managing groups of
// instructions across chip sets.  An instruction uses the same itinerary class
// across all chip sets.  Thus a new chip set can be added without modifying
// instruction information.
//
class InstrItinClass;
def NoItinerary : InstrItinClass;

//===----------------------------------------------------------------------===//
// Instruction itinerary data - These values provide a runtime map of an
// instruction itinerary class (name) to its itinerary data.
//
// NumMicroOps represents the number of micro-operations that each instruction
// in the class are decoded to. If the number is zero, then it means the
// instruction can decode into variable number of micro-ops and it must be
// determined dynamically. This directly relates to the itineraries
// global IssueWidth property, which constrains the number of microops
// that can issue per cycle.
//
// OperandCycles are optional "cycle counts". They specify the cycle after
// instruction issue the values which correspond to specific operand indices
// are defined or read. Bypasses are optional "pipeline forwarding paths", if
// a def by an instruction is available on a specific bypass and the use can
// read from the same bypass, then the operand use latency is reduced by one.
//
//  InstrItinData<IIC_iLoad_i , [InstrStage<1, [A9_Pipe1]>,
//                               InstrStage<1, [A9_AGU]>],
//                              [3, 1], [A9_LdBypass]>,
//  InstrItinData<IIC_iMVNr   , [InstrStage<1, [A9_Pipe0, A9_Pipe1]>],
//                              [1, 1], [NoBypass, A9_LdBypass]>,
//
// In this example, the instruction of IIC_iLoadi reads its input on cycle 1
// (after issue) and the result of the load is available on cycle 3. The result
// is available via forwarding path A9_LdBypass. If it's used by the first
// source operand of instructions of IIC_iMVNr class, then the operand latency
// is reduced by 1.
class InstrItinData<InstrItinClass Class, list<InstrStage> stages,
                    list<int> operandcycles = [],
                    list<Bypass> bypasses = [], int uops = 1> {
  InstrItinClass TheClass = Class;
  int NumMicroOps = uops;
  list<InstrStage> Stages = stages;
  list<int> OperandCycles = operandcycles;
  list<Bypass> Bypasses = bypasses;
}

//===----------------------------------------------------------------------===//
// Processor itineraries - These values represent the set of all itinerary
// classes for a given chip set.
//
// Set property values to -1 to use the default.
// See InstrItineraryProps for comments and defaults.
class ProcessorItineraries<list<FuncUnit> fu, list<Bypass> bp,
                           list<InstrItinData> iid> {
  list<FuncUnit> FU = fu;
  list<Bypass> BP = bp;
  list<InstrItinData> IID = iid;
  // The packetizer automaton to use for this itinerary. By default all
  // itineraries for a target are bundled up into the same automaton. This only
  // works correctly when there are no conflicts in functional unit IDs between
  // itineraries. For example, given two itineraries A<[SLOT_A]>, B<[SLOT_B]>,
  // SLOT_A and SLOT_B will be assigned the same functional unit index, and
  // the generated packetizer will confuse instructions referencing these slots.
  //
  // To avoid this, setting PacketizerNamespace to non-"" will cause this
  // itinerary to be generated in a different automaton. The subtarget will need
  // to declare a method "create##Namespace##DFAPacketizer()".
  string PacketizerNamespace = "";
}

// NoItineraries - A marker that can be used by processors without schedule
// info. Subtargets using NoItineraries can bypass the scheduler's
// expensive HazardRecognizer because no reservation table is needed.
def NoItineraries : ProcessorItineraries<[], [], []>;

//===----------------------------------------------------------------------===//
// Combo Function Unit data - This is a map of combo function unit names to
// the list of functional units that are included in the combination.
//
class ComboFuncData<FuncUnit ComboFunc, list<FuncUnit> funclist> {
  FuncUnit TheComboFunc = ComboFunc;
  list<FuncUnit> FuncList = funclist;
}

//===----------------------------------------------------------------------===//
// Combo Function Units - This is a list of all combo function unit data.
class ComboFuncUnits<list<ComboFuncData> cfd> {
  list<ComboFuncData> CFD = cfd;
}

