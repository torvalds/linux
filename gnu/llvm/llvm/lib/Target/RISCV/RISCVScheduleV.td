//===- RISCVScheduleV.td - RISC-V Scheduling Definitions V -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
/// Define scheduler resources associated with def operands.

defvar SchedMxList = ["MF8", "MF4", "MF2", "M1", "M2", "M4", "M8"];
// Used for widening and narrowing instructions as it doesn't contain M8.
defvar SchedMxListW = !listremove(SchedMxList, ["M8"]);
// Used for widening reductions, which does contain M8.
defvar SchedMxListWRed = SchedMxList;
defvar SchedMxListFW = !listremove(SchedMxList, ["M8", "MF8"]);
// Used for floating-point as it doesn't contain MF8.
defvar SchedMxListF = !listremove(SchedMxList, ["MF8"]);
// Used for widening floating-point Reduction as it doesn't contain MF8.
defvar SchedMxListFWRed = SchedMxListF;

class SchedSEWSet<string mx, bit isF = 0, bit isWidening = 0> {
  assert !or(!not(isF), !ne(mx, "MF8")), "LMUL shouldn't be MF8 for floating-point";
  defvar t = !cond(!eq(mx, "M1"):  [8, 16, 32, 64],
                   !eq(mx, "M2"):  [8, 16, 32, 64],
                   !eq(mx, "M4"):  [8, 16, 32, 64],
                   !eq(mx, "M8"):  [8, 16, 32, 64],
                   !eq(mx, "MF2"): [8, 16, 32],
                   !eq(mx, "MF4"): [8, 16],
                   !eq(mx, "MF8"): [8]);
  // For floating-point instructions, SEW won't be 8.
  defvar remove8 = !if(isF, !listremove(t, [8]), t);
  // For widening instructions, SEW will not be 64.
  defvar remove64 = !if(isWidening, !listremove(remove8, [64]), remove8);
  list<int> val = remove64;
}

// Helper function to get the largest LMUL from MxList
// Precondition: MxList is sorted in ascending LMUL order.
class LargestLMUL<list<string> MxList> {
  // MX list is sorted from smallest to largest
  string r = !foldl(!head(MxList), MxList, last, curr, curr);
}
// Helper function to get the smallest SEW that can be used with LMUL mx
// Precondition: MxList is sorted in ascending LMUL order and SchedSEWSet<mx>
class SmallestSEW<string mx, bit isF = 0> {
  int r = !head(SchedSEWSet<mx, isF>.val);
}

// Creates WriteRes for (name, mx, resources) tuple
multiclass LMULWriteResMX<string name, list<ProcResourceKind> resources,
                          string mx, bit IsWorstCase> {
  def : WriteRes<!cast<SchedWrite>(name # "_" # mx), resources>;
  if IsWorstCase then
    def : WriteRes<!cast<SchedWrite>(name # "_WorstCase"), resources>;
}
multiclass LMULSEWWriteResMXSEW<string name, list<ProcResourceKind> resources,
                             string mx, int sew,  bit IsWorstCase> {
  def : WriteRes<!cast<SchedWrite>(name # "_" # mx # "_E" # sew), resources>;
  if IsWorstCase then
    def : WriteRes<!cast<SchedWrite>(name # "_WorstCase"), resources>;
}

// Define a SchedAlias for the SchedWrite associated with (name, mx) whose
// behavior is aliased to a Variant. The Variant has Latency predLad and
// ReleaseAtCycles predCycles if the SchedPredicate Pred is true, otherwise has
// Latency noPredLat and ReleaseAtCycles noPredCycles. The WorstCase SchedWrite
// is created similiarly if IsWorstCase is true.
multiclass LMULWriteResMXVariant<string name, SchedPredicateBase Pred,
                                 list<ProcResourceKind> resources,
                                 int predLat, list<int> predAcquireCycles,
                                 list<int> predReleaseCycles, int noPredLat,
                                 list<int> noPredAcquireCycles,
                                 list<int> noPredReleaseCycles,
                                 string mx, bit IsWorstCase> {
  defvar nameMX = name # "_" # mx;

  // Define the different behaviors
  def nameMX # "_Pred" : SchedWriteRes<resources>{
    let Latency = predLat;
    let AcquireAtCycles = predAcquireCycles;
    let ReleaseAtCycles = predReleaseCycles;
  }
  def nameMX # "_NoPred" : SchedWriteRes<resources> {
    let Latency = noPredLat;
    let AcquireAtCycles = noPredAcquireCycles;
    let ReleaseAtCycles = noPredReleaseCycles;
  }

  // Define SchedVars
  def nameMX # PredSchedVar
      : SchedVar<Pred, [!cast<SchedWriteRes>(NAME # nameMX # "_Pred")]>;
  def nameMX # NoPredSchedVar
      : SchedVar<NoSchedPred, [!cast<SchedWriteRes>(NAME # nameMX #"_NoPred")]>;
  // Allow multiclass to refer to SchedVars -- need to have NAME prefix.
  defvar PredSchedVar = !cast<SchedVar>(NAME # nameMX # PredSchedVar);
  defvar NoPredSchedVar = !cast<SchedVar>(NAME # nameMX # NoPredSchedVar);

  // Tie behavior to predicate
  def NAME # nameMX # "_Variant"
      : SchedWriteVariant<[PredSchedVar, NoPredSchedVar]>;
  def : SchedAlias<
    !cast<SchedReadWrite>(nameMX),
    !cast<SchedReadWrite>(NAME # nameMX # "_Variant")>;

  if IsWorstCase then {
    def NAME # name # "_WorstCase_Variant"
      : SchedWriteVariant<[PredSchedVar, NoPredSchedVar]>;
    def : SchedAlias<
      !cast<SchedReadWrite>(name # "_WorstCase"),
      !cast<SchedReadWrite>(NAME # name # "_WorstCase_Variant")>;
  }
}

// Define multiclasses to define SchedWrite, SchedRead,  WriteRes, and
// ReadAdvance for each (name, LMUL) pair and for each LMUL in each of the
// SchedMxList variants above. Each multiclass is responsible for defining
// a record that represents the WorseCase behavior for name.
multiclass LMULSchedWritesImpl<string name, list<string> MxList> {
  def name # "_WorstCase" : SchedWrite;
  foreach mx = MxList in {
    def name # "_" # mx : SchedWrite;
  }
}
multiclass LMULSchedReadsImpl<string name, list<string> MxList> {
  def name # "_WorstCase" : SchedRead;
  foreach mx = MxList in {
    def name # "_" # mx : SchedRead;
  }
}
multiclass LMULWriteResImpl<string name, list<ProcResourceKind> resources> {
  if !exists<SchedWrite>(name # "_WorstCase") then
    def : WriteRes<!cast<SchedWrite>(name # "_WorstCase"), resources>;
  foreach mx = SchedMxList in {
    if !exists<SchedWrite>(name # "_" # mx) then
      def : WriteRes<!cast<SchedWrite>(name # "_" # mx), resources>;
  }
}
multiclass LMULReadAdvanceImpl<string name, int val,
                               list<SchedWrite> writes = []> {
  if !exists<SchedRead>(name # "_WorstCase") then
    def : ReadAdvance<!cast<SchedRead>(name # "_WorstCase"), val, writes>;
  foreach mx = SchedMxList in {
    if !exists<SchedRead>(name # "_" # mx) then
      def : ReadAdvance<!cast<SchedRead>(name # "_" # mx), val, writes>;
  }
}

// Define multiclasses to define SchedWrite, SchedRead,  WriteRes, and
// ReadAdvance for each (name, LMUL, SEW) tuple for each LMUL in each of the
// SchedMxList variants above. Each multiclass is responsible for defining
// a record that represents the WorseCase behavior for name.
multiclass LMULSEWSchedWritesImpl<string name, list<string> MxList, bit isF = 0,
                                  bit isWidening = 0> {
  def name # "_WorstCase" : SchedWrite;
  foreach mx = MxList in {
    foreach sew = SchedSEWSet<mx, isF, isWidening>.val in
      def name # "_" # mx # "_E" # sew : SchedWrite;
  }
}
multiclass LMULSEWSchedReadsImpl<string name, list<string> MxList, bit isF = 0,
                                 bit isWidening = 0> {
  def name # "_WorstCase" : SchedRead;
  foreach mx = MxList in {
    foreach sew = SchedSEWSet<mx, isF, isWidening>.val in
      def name # "_" # mx # "_E" # sew : SchedRead;
  }
}
multiclass LMULSEWWriteResImpl<string name, list<ProcResourceKind> resources,
                               list<string> MxList, bit isF = 0,
                               bit isWidening = 0> {
  if !exists<SchedWrite>(name # "_WorstCase") then
    def : WriteRes<!cast<SchedWrite>(name # "_WorstCase"), resources>;
  foreach mx = MxList in {
    foreach sew = SchedSEWSet<mx, isF, isWidening>.val in
      if !exists<SchedWrite>(name # "_" # mx # "_E" # sew) then
        def : WriteRes<!cast<SchedWrite>(name # "_" # mx # "_E" # sew), resources>;
  }
}
multiclass LMULSEWReadAdvanceImpl<string name, int val, list<SchedWrite> writes = [],
                                  list<string> MxList, bit isF = 0,
                                  bit isWidening = 0> {
  if !exists<SchedRead>(name # "_WorstCase") then
    def : ReadAdvance<!cast<SchedRead>(name # "_WorstCase"), val, writes>;
  foreach mx = MxList in {
    foreach sew = SchedSEWSet<mx, isF, isWidening>.val in
      if !exists<SchedRead>(name # "_" # mx # "_E" # sew) then
        def : ReadAdvance<!cast<SchedRead>(name # "_" # mx # "_E" # sew), val, writes>;
  }
}
// Define classes to define list containing all SchedWrites for each (name, LMUL)
// pair for each LMUL in each of the SchedMxList variants above and name in
// argument `names`. These classes can be used to construct a list of existing
// definitions of writes corresponding to each (name, LMUL) pair, that are needed
// by the ReadAdvance. For example:
// ```
//   defm "" : LMULReadAdvance<"ReadVIALUX", 1,
//                             LMULSchedWriteList<["WriteVMovSX"]>.value>;
// ```
class LMULSchedWriteListImpl<list<string> names, list<string> MxList> {
  list<SchedWrite> value = !foldl([]<SchedWrite>,
                                  !foreach(name, names,
                                    !foreach(mx, MxList, !cast<SchedWrite>(name # "_" # mx))),
                                  all, writes, !listconcat(all, writes));
}

multiclass LMULSchedWrites<string name> : LMULSchedWritesImpl<name, SchedMxList>;
multiclass LMULSchedReads<string name> : LMULSchedReadsImpl<name, SchedMxList>;
multiclass LMULWriteRes<string name, list<ProcResourceKind> resources>
  : LMULWriteResImpl<name, resources>;
multiclass LMULReadAdvance<string name, int val, list<SchedWrite> writes = []>
  : LMULReadAdvanceImpl<name, val, writes>;
class LMULSchedWriteList<list<string> names> : LMULSchedWriteListImpl<names, SchedMxList>;

multiclass LMULSEWSchedWrites<string name> : LMULSEWSchedWritesImpl<name, SchedMxList>;
multiclass LMULSEWSchedReads<string name> : LMULSEWSchedReadsImpl<name, SchedMxList>;
multiclass LMULSEWWriteRes<string name, list<ProcResourceKind> resources>
  : LMULSEWWriteResImpl<name, resources, SchedMxList>;
multiclass LMULSEWReadAdvance<string name, int val, list<SchedWrite> writes = []>
  : LMULSEWReadAdvanceImpl<name, val, writes, SchedMxList>;

multiclass LMULSEWSchedWritesWRed<string name>
    : LMULSEWSchedWritesImpl<name, SchedMxListWRed, isWidening=1>;
multiclass LMULSEWWriteResWRed<string name, list<ProcResourceKind> resources>
    : LMULSEWWriteResImpl<name, resources, SchedMxListWRed, isWidening=1>;

multiclass LMULSEWSchedWritesFWRed<string name>
    : LMULSEWSchedWritesImpl<name, SchedMxListFWRed, isF=1, isWidening=1>;
multiclass LMULSEWWriteResFWRed<string name, list<ProcResourceKind> resources>
    : LMULSEWWriteResImpl<name, resources, SchedMxListFWRed, isF=1, isWidening=1>;

multiclass LMULSEWSchedWritesF<string name> : LMULSEWSchedWritesImpl<name, SchedMxListF, isF=1>;
multiclass LMULSEWSchedReadsF<string name> : LMULSEWSchedReadsImpl<name, SchedMxListF, isF=1>;
multiclass LMULSEWWriteResF<string name, list<ProcResourceKind> resources>
  : LMULSEWWriteResImpl<name, resources, SchedMxListF, isF=1>;
multiclass LMULSEWReadAdvanceF<string name, int val, list<SchedWrite> writes = []>
  : LMULSEWReadAdvanceImpl<name, val, writes, SchedMxListF, isF=1>;

multiclass LMULSchedWritesW<string name> : LMULSchedWritesImpl<name, SchedMxListW>;
multiclass LMULSchedReadsW<string name> : LMULSchedReadsImpl<name, SchedMxListW>;
multiclass LMULWriteResW<string name, list<ProcResourceKind> resources>
  : LMULWriteResImpl<name, resources>;
multiclass LMULReadAdvanceW<string name, int val, list<SchedWrite> writes = []>
  : LMULReadAdvanceImpl<name, val, writes>;
class LMULSchedWriteListW<list<string> names> : LMULSchedWriteListImpl<names, SchedMxListW>;

multiclass LMULSchedWritesFW<string name> : LMULSchedWritesImpl<name, SchedMxListFW>;
multiclass LMULSchedReadsFW<string name> : LMULSchedReadsImpl<name, SchedMxListFW>;
multiclass LMULWriteResFW<string name, list<ProcResourceKind> resources>
  : LMULWriteResImpl<name, resources>;
multiclass LMULReadAdvanceFW<string name, int val, list<SchedWrite> writes = []>
  : LMULReadAdvanceImpl<name, val, writes>;
class LMULSchedWriteListFW<list<string> names> : LMULSchedWriteListImpl<names, SchedMxListFW>;

multiclass LMULSEWSchedWritesW<string name>
    : LMULSEWSchedWritesImpl<name, SchedMxListW, isF = 0, isWidening = 1>;
multiclass LMULSEWSchedReadsW<string name>
    : LMULSEWSchedReadsImpl<name, SchedMxListW, isF = 0, isWidening = 1>;
multiclass LMULSEWWriteResW<string name, list<ProcResourceKind> resources>
    : LMULSEWWriteResImpl<name, resources, SchedMxListW, isF = 0,
                          isWidening = 1>;
multiclass
    LMULSEWReadAdvanceW<string name, int val, list<SchedWrite> writes = []>
    : LMULSEWReadAdvanceImpl<name, val, writes, SchedMxListW, isF = 0,
                             isWidening = 1>;

multiclass LMULSEWSchedWritesFW<string name>
    : LMULSEWSchedWritesImpl<name, SchedMxListFW, isF = 1, isWidening = 1>;
multiclass LMULSEWSchedReadsFW<string name>
    : LMULSEWSchedReadsImpl<name, SchedMxListFW, isF = 1, isWidening = 1>;
multiclass LMULSEWWriteResFW<string name, list<ProcResourceKind> resources>
    : LMULSEWWriteResImpl<name, resources, SchedMxListFW, isF = 1,
                          isWidening = 1>;
multiclass
    LMULSEWReadAdvanceFW<string name, int val, list<SchedWrite> writes = []>
    : LMULSEWReadAdvanceImpl<name, val, writes, SchedMxListFW, isF = 1,
                             isWidening = 1>;

// 3.6 Vector Byte Length vlenb
def WriteRdVLENB      : SchedWrite;

// 6. Configuration-Setting Instructions
def WriteVSETVLI      : SchedWrite;
def WriteVSETIVLI     : SchedWrite;
def WriteVSETVL       : SchedWrite;

// 7. Vector Loads and Stores
// 7.4. Vector Unit-Stride Instructions
defm "" : LMULSchedWrites<"WriteVLDE">;
defm "" : LMULSchedWrites<"WriteVSTE">;
// 7.4.1. Vector Unit-Strided Mask
defm "" : LMULSchedWrites<"WriteVLDM">;
defm "" : LMULSchedWrites<"WriteVSTM">;
// 7.5. Vector Strided Instructions
defm "" : LMULSchedWrites<"WriteVLDS8">;
defm "" : LMULSchedWrites<"WriteVLDS16">;
defm "" : LMULSchedWrites<"WriteVLDS32">;
defm "" : LMULSchedWrites<"WriteVLDS64">;
defm "" : LMULSchedWrites<"WriteVSTS8">;
defm "" : LMULSchedWrites<"WriteVSTS16">;
defm "" : LMULSchedWrites<"WriteVSTS32">;
defm "" : LMULSchedWrites<"WriteVSTS64">;
// 7.6. Vector Indexed Instructions
defm "" : LMULSchedWrites<"WriteVLDUX8">;
defm "" : LMULSchedWrites<"WriteVLDUX16">;
defm "" : LMULSchedWrites<"WriteVLDUX32">;
defm "" : LMULSchedWrites<"WriteVLDUX64">;
defm "" : LMULSchedWrites<"WriteVLDOX8">;
defm "" : LMULSchedWrites<"WriteVLDOX16">;
defm "" : LMULSchedWrites<"WriteVLDOX32">;
defm "" : LMULSchedWrites<"WriteVLDOX64">;
defm "" : LMULSchedWrites<"WriteVSTUX8">;
defm "" : LMULSchedWrites<"WriteVSTUX16">;
defm "" : LMULSchedWrites<"WriteVSTUX32">;
defm "" : LMULSchedWrites<"WriteVSTUX64">;
defm "" : LMULSchedWrites<"WriteVSTOX8">;
defm "" : LMULSchedWrites<"WriteVSTOX16">;
defm "" : LMULSchedWrites<"WriteVSTOX32">;
defm "" : LMULSchedWrites<"WriteVSTOX64">;
// 7.7. Vector Unit-stride Fault-Only-First Loads
defm "" : LMULSchedWrites<"WriteVLDFF">;
// 7.8. Vector Segment Instructions
foreach nf=2-8 in {
  foreach eew = [8, 16, 32, 64] in {
    defm "" : LMULSchedWrites<"WriteVLSEG" # nf # e # eew>;
    defm "" : LMULSchedWrites<"WriteVSSEG" # nf # e # eew>;
    defm "" : LMULSchedWrites<"WriteVLSEGFF" # nf # e # eew>;
    defm "" : LMULSchedWrites<"WriteVLSSEG" # nf # e # eew>;
    defm "" : LMULSchedWrites<"WriteVSSSEG" # nf # e # eew>;
    defm "" : LMULSchedWrites<"WriteVLUXSEG" # nf # e # eew>;
    defm "" : LMULSchedWrites<"WriteVLOXSEG" # nf # e # eew>;
    defm "" : LMULSchedWrites<"WriteVSUXSEG" # nf # e # eew>;
    defm "" : LMULSchedWrites<"WriteVSOXSEG" # nf # e # eew>;
  }
}
// 7.9. Vector Whole Register Instructions
def WriteVLD1R        : SchedWrite;
def WriteVLD2R        : SchedWrite;
def WriteVLD4R        : SchedWrite;
def WriteVLD8R        : SchedWrite;
def WriteVST1R        : SchedWrite;
def WriteVST2R        : SchedWrite;
def WriteVST4R        : SchedWrite;
def WriteVST8R        : SchedWrite;

// 11. Vector Integer Arithmetic Instructions
// 11.1. Vector Single-Width Integer Add and Subtract
// 11.5. Vector Bitwise Logical Instructions
defm "" : LMULSchedWrites<"WriteVIALUV">;
defm "" : LMULSchedWrites<"WriteVIALUX">;
defm "" : LMULSchedWrites<"WriteVIALUI">;
// 11.2. Vector Widening Integer Add/Subtract
defm "" : LMULSchedWritesW<"WriteVIWALUV">;
defm "" : LMULSchedWritesW<"WriteVIWALUX">;
defm "" : LMULSchedWritesW<"WriteVIWALUI">;
// 11.3. Vector Integer Extension
defm "" : LMULSchedWrites<"WriteVExtV">;
// 11.4. Vector Integer Arithmetic with Carry or Borrow Instructions
defm "" : LMULSchedWrites<"WriteVICALUV">;
defm "" : LMULSchedWrites<"WriteVICALUX">;
defm "" : LMULSchedWrites<"WriteVICALUI">;
// 11.6. Vector Single-Width Bit Shift Instructions
defm "" : LMULSchedWrites<"WriteVShiftV">;
defm "" : LMULSchedWrites<"WriteVShiftX">;
defm "" : LMULSchedWrites<"WriteVShiftI">;
// 11.7. Vector Narrowing Integer Right Shift Instructions
defm "" : LMULSchedWritesW<"WriteVNShiftV">;
defm "" : LMULSchedWritesW<"WriteVNShiftX">;
defm "" : LMULSchedWritesW<"WriteVNShiftI">;
// 11.8. Vector Integer Comparison Instructions
defm "" : LMULSchedWrites<"WriteVICmpV">;
defm "" : LMULSchedWrites<"WriteVICmpX">;
defm "" : LMULSchedWrites<"WriteVICmpI">;
// 11.9. Vector Integer Min/Max Instructions
defm "" : LMULSchedWrites<"WriteVIMinMaxV">;
defm "" : LMULSchedWrites<"WriteVIMinMaxX">;
// 11.10. Vector Single-Width Integer Multiply Instructions
defm "" : LMULSchedWrites<"WriteVIMulV">;
defm "" : LMULSchedWrites<"WriteVIMulX">;
// 11.11. Vector Integer Divide Instructions
defm "" : LMULSEWSchedWrites<"WriteVIDivV">;
defm "" : LMULSEWSchedWrites<"WriteVIDivX">;
// 11.12. Vector Widening Integer Multiply Instructions
defm "" : LMULSchedWritesW<"WriteVIWMulV">;
defm "" : LMULSchedWritesW<"WriteVIWMulX">;
// 11.13. Vector Single-Width Integer Multiply-Add Instructions
defm "" : LMULSchedWrites<"WriteVIMulAddV">;
defm "" : LMULSchedWrites<"WriteVIMulAddX">;
// 11.14. Vector Widening Integer Multiply-Add Instructions
defm "" : LMULSchedWritesW<"WriteVIWMulAddV">;
defm "" : LMULSchedWritesW<"WriteVIWMulAddX">;
// 11.15. Vector Integer Merge Instructions
defm "" : LMULSchedWrites<"WriteVIMergeV">;
defm "" : LMULSchedWrites<"WriteVIMergeX">;
defm "" : LMULSchedWrites<"WriteVIMergeI">;
// 11.16. Vector Integer Move Instructions
defm "" : LMULSchedWrites<"WriteVIMovV">;
defm "" : LMULSchedWrites<"WriteVIMovX">;
defm "" : LMULSchedWrites<"WriteVIMovI">;

// 12. Vector Fixed-Point Arithmetic Instructions
// 12.1. Vector Single-Width Saturating Add and Subtract
defm "" : LMULSchedWrites<"WriteVSALUV">;
defm "" : LMULSchedWrites<"WriteVSALUX">;
defm "" : LMULSchedWrites<"WriteVSALUI">;
// 12.2. Vector Single-Width Averaging Add and Subtract
defm "" : LMULSchedWrites<"WriteVAALUV">;
defm "" : LMULSchedWrites<"WriteVAALUX">;
// 12.3. Vector Single-Width Fractional Multiply with Rounding and Saturation
defm "" : LMULSchedWrites<"WriteVSMulV">;
defm "" : LMULSchedWrites<"WriteVSMulX">;
// 12.4. Vector Single-Width Scaling Shift Instructions
defm "" : LMULSchedWrites<"WriteVSShiftV">;
defm "" : LMULSchedWrites<"WriteVSShiftX">;
defm "" : LMULSchedWrites<"WriteVSShiftI">;
// 12.5. Vector Narrowing Fixed-Point Clip Instructions
defm "" : LMULSchedWritesW<"WriteVNClipV">;
defm "" : LMULSchedWritesW<"WriteVNClipX">;
defm "" : LMULSchedWritesW<"WriteVNClipI">;

// 13. Vector Floating-Point Instructions
// 13.2. Vector Single-Width Floating-Point Add/Subtract Instructions
defm "" : LMULSEWSchedWritesF<"WriteVFALUV">;
defm "" : LMULSEWSchedWritesF<"WriteVFALUF">;
// 13.3. Vector Widening Floating-Point Add/Subtract Instructions
defm "" : LMULSEWSchedWritesFW<"WriteVFWALUV">;
defm "" : LMULSEWSchedWritesFW<"WriteVFWALUF">;
// 13.4. Vector Single-Width Floating-Point Multiply/Divide Instructions
defm "" : LMULSEWSchedWritesF<"WriteVFMulV">;
defm "" : LMULSEWSchedWritesF<"WriteVFMulF">;
defm "" : LMULSEWSchedWritesF<"WriteVFDivV">;
defm "" : LMULSEWSchedWritesF<"WriteVFDivF">;
// 13.5. Vector Widening Floating-Point Multiply
defm "" : LMULSEWSchedWritesFW<"WriteVFWMulV">;
defm "" : LMULSEWSchedWritesFW<"WriteVFWMulF">;
// 13.6. Vector Single-Width Floating-Point Fused Multiply-Add Instructions
defm "" : LMULSEWSchedWritesF<"WriteVFMulAddV">;
defm "" : LMULSEWSchedWritesF<"WriteVFMulAddF">;
// 13.7. Vector Widening Floating-Point Fused Multiply-Add Instructions
defm "" : LMULSEWSchedWritesFW<"WriteVFWMulAddV">;
defm "" : LMULSEWSchedWritesFW<"WriteVFWMulAddF">;
// 13.8. Vector Floating-Point Square-Root Instruction
defm "" : LMULSEWSchedWritesF<"WriteVFSqrtV">;
// 13.9. Vector Floating-Point Reciprocal Square-Root Estimate Instruction
// 13.10. Vector Floating-Point Reciprocal Estimate Instruction
defm "" : LMULSEWSchedWritesF<"WriteVFRecpV">;
// 13.11. Vector Floating-Point MIN/MAX Instructions
defm "" : LMULSEWSchedWritesF<"WriteVFMinMaxV">;
defm "" : LMULSEWSchedWritesF<"WriteVFMinMaxF">;
// 13.12. Vector Floating-Point Sign-Injection Instructions
defm "" : LMULSEWSchedWritesF<"WriteVFSgnjV">;
defm "" : LMULSEWSchedWritesF<"WriteVFSgnjF">;
// 13.13. Vector Floating-Point Compare Instructions
defm "" : LMULSchedWrites<"WriteVFCmpV">;
defm "" : LMULSchedWrites<"WriteVFCmpF">;
// 13.14. Vector Floating-Point Classify Instruction
defm "" : LMULSchedWrites<"WriteVFClassV">;
// 13.15. Vector Floating-Point Merge Instruction
defm "" : LMULSchedWrites<"WriteVFMergeV">;
// 13.16. Vector Floating-Point Move Instruction
defm "" : LMULSchedWrites<"WriteVFMovV">;
// 13.17. Single-Width Floating-Point/Integer Type-Convert Instructions
defm "" : LMULSEWSchedWritesF<"WriteVFCvtIToFV">;
defm "" : LMULSchedWrites<"WriteVFCvtFToIV">;
// 13.18. Widening Floating-Point/Integer Type-Convert Instructions
defm "" : LMULSEWSchedWritesW<"WriteVFWCvtIToFV">;
defm "" : LMULSchedWritesFW<"WriteVFWCvtFToIV">;
defm "" : LMULSEWSchedWritesFW<"WriteVFWCvtFToFV">;
// 13.19. Narrowing Floating-Point/Integer Type-Convert Instructions
defm "" : LMULSEWSchedWritesFW<"WriteVFNCvtIToFV">;
defm "" : LMULSchedWritesW<"WriteVFNCvtFToIV">;
defm "" : LMULSEWSchedWritesFW<"WriteVFNCvtFToFV">;

// 14. Vector Reduction Operations
// The latency of reduction is determined by the size of the read resource.
// The LMUL range of read resource(VS2) for reduction operantion is between
// MF8 and M8. Use the _From suffix to indicate the number of the
// LMUL from VS2.
// 14.1. Vector Single-Width Integer Reduction Instructions
defm "" : LMULSEWSchedWrites<"WriteVIRedV_From">;
defm "" : LMULSEWSchedWrites<"WriteVIRedMinMaxV_From">;
// 14.2. Vector Widening Integer Reduction Instructions
defm "" : LMULSEWSchedWritesWRed<"WriteVIWRedV_From">;
// 14.3. Vector Single-Width Floating-Point Reduction Instructions
defm "" : LMULSEWSchedWritesF<"WriteVFRedV_From">;
defm "" : LMULSEWSchedWritesF<"WriteVFRedOV_From">;
defm "" : LMULSEWSchedWritesF<"WriteVFRedMinMaxV_From">;
// 14.4. Vector Widening Floating-Point Reduction Instructions
defm "" : LMULSEWSchedWritesFWRed<"WriteVFWRedV_From">;
defm "" : LMULSEWSchedWritesFWRed<"WriteVFWRedOV_From">;

// 15. Vector Mask Instructions
// 15.1. Vector Mask-Register Logical Instructions
defm "" : LMULSchedWrites<"WriteVMALUV">;
// 15.2. Vector Mask Population Count
defm "" : LMULSchedWrites<"WriteVMPopV">;
// 15.3. Vector Find-First-Set Mask Bit
defm "" : LMULSchedWrites<"WriteVMFFSV">;
// 15.4. Vector Set-Before-First Mask Bit
// 15.5. Vector Set-Including-First Mask Bit
// 15.6. Vector Set-only-First Mask Bit
defm "" : LMULSchedWrites<"WriteVMSFSV">;
// 15.8. Vector Iota Instruction
defm "" : LMULSchedWrites<"WriteVIotaV">;
// 15.9. Vector Element Index Instruction
defm "" : LMULSchedWrites<"WriteVIdxV">;

// 16. Vector Permutation Instructions
// 16.1. Integer Scalar Move Instructions
def WriteVMovSX : SchedWrite;
def WriteVMovXS : SchedWrite;
// 16.2. Floating-Point Scalar Move Instructions
def WriteVMovSF : SchedWrite;
def WriteVMovFS : SchedWrite;
// 16.3. Vector Slide Instructions
defm "" : LMULSchedWrites<"WriteVSlideUpX">;
defm "" : LMULSchedWrites<"WriteVSlideDownX">;
defm "" : LMULSchedWrites<"WriteVSlideI">;
defm "" : LMULSchedWrites<"WriteVISlide1X">;
defm "" : LMULSchedWrites<"WriteVFSlide1F">;
// 16.4. Vector Register Gather Instructions
defm "" : LMULSEWSchedWrites<"WriteVRGatherVV">;
defm "" : LMULSEWSchedWrites<"WriteVRGatherEI16VV">;
defm "" : LMULSchedWrites<"WriteVRGatherVX">;
defm "" : LMULSchedWrites<"WriteVRGatherVI">;
// 16.5. Vector Compress Instruction
defm "" : LMULSEWSchedWrites<"WriteVCompressV">;
// 16.6. Whole Vector Register Move
// These are already LMUL aware
def WriteVMov1V       : SchedWrite;
def WriteVMov2V       : SchedWrite;
def WriteVMov4V       : SchedWrite;
def WriteVMov8V       : SchedWrite;

//===----------------------------------------------------------------------===//
/// Define scheduler resources associated with use operands.

// 6. Configuration-Setting Instructions
def ReadVSETVLI       : SchedRead;
def ReadVSETVL        : SchedRead;

// 7. Vector Loads and Stores
def ReadVLDX : SchedRead;
def ReadVSTX : SchedRead;
// 7.4. Vector Unit-Stride Instructions
defm "" : LMULSchedReads<"ReadVSTEV">;
// 7.4.1. Vector Unit-Strided Mask
defm "" : LMULSchedReads<"ReadVSTM">;
// 7.5. Vector Strided Instructions
def ReadVLDSX : SchedRead;
def ReadVSTSX : SchedRead;
defm "" : LMULSchedReads<"ReadVSTS8V">;
defm "" : LMULSchedReads<"ReadVSTS16V">;
defm "" : LMULSchedReads<"ReadVSTS32V">;
defm "" : LMULSchedReads<"ReadVSTS64V">;
// 7.6. Vector Indexed Instructions
defm "" : LMULSchedReads<"ReadVLDUXV">;
defm "" : LMULSchedReads<"ReadVLDOXV">;
defm "" : LMULSchedReads<"ReadVSTUX8">;
defm "" : LMULSchedReads<"ReadVSTUX16">;
defm "" : LMULSchedReads<"ReadVSTUX32">;
defm "" : LMULSchedReads<"ReadVSTUX64">;
defm "" : LMULSchedReads<"ReadVSTUXV">;
defm "" : LMULSchedReads<"ReadVSTUX8V">;
defm "" : LMULSchedReads<"ReadVSTUX16V">;
defm "" : LMULSchedReads<"ReadVSTUX32V">;
defm "" : LMULSchedReads<"ReadVSTUX64V">;
defm "" : LMULSchedReads<"ReadVSTOX8">;
defm "" : LMULSchedReads<"ReadVSTOX16">;
defm "" : LMULSchedReads<"ReadVSTOX32">;
defm "" : LMULSchedReads<"ReadVSTOX64">;
defm "" : LMULSchedReads<"ReadVSTOXV">;
defm "" : LMULSchedReads<"ReadVSTOX8V">;
defm "" : LMULSchedReads<"ReadVSTOX16V">;
defm "" : LMULSchedReads<"ReadVSTOX32V">;
defm "" : LMULSchedReads<"ReadVSTOX64V">;
// 7.9. Vector Whole Register Instructions
// These are already LMUL aware
def ReadVST1R         : SchedRead;
def ReadVST2R         : SchedRead;
def ReadVST4R         : SchedRead;
def ReadVST8R         : SchedRead;

// 11. Vector Integer Arithmetic Instructions
// 11.1. Vector Single-Width Integer Add and Subtract
// 11.5. Vector Bitwise Logical Instructions
defm "" : LMULSchedReads<"ReadVIALUV">;
defm "" : LMULSchedReads<"ReadVIALUX">;
// 11.2. Vector Widening Integer Add/Subtract
defm "" : LMULSchedReadsW<"ReadVIWALUV">;
defm "" : LMULSchedReadsW<"ReadVIWALUX">;
// 11.3. Vector Integer Extension
defm "" : LMULSchedReads<"ReadVExtV">;
// 11.4. Vector Integer Arithmetic with Carry or Borrow Instructions
defm "" : LMULSchedReads<"ReadVICALUV">;
defm "" : LMULSchedReads<"ReadVICALUX">;
// 11.6. Vector Single-Width Bit Shift Instructions
defm "" : LMULSchedReads<"ReadVShiftV">;
defm "" : LMULSchedReads<"ReadVShiftX">;
// 11.7. Vector Narrowing Integer Right Shift Instructions
defm "" : LMULSchedReadsW<"ReadVNShiftV">;
defm "" : LMULSchedReadsW<"ReadVNShiftX">;
// 11.8. Vector Integer Comparison Instructions
defm "" : LMULSchedReads<"ReadVICmpV">;
defm "" : LMULSchedReads<"ReadVICmpX">;
// 11.9. Vector Integer Min/Max Instructions
defm "" : LMULSchedReads<"ReadVIMinMaxV">;
defm "" : LMULSchedReads<"ReadVIMinMaxX">;
// 11.10. Vector Single-Width Integer Multiply Instructions
defm "" : LMULSchedReads<"ReadVIMulV">;
defm "" : LMULSchedReads<"ReadVIMulX">;
// 11.11. Vector Integer Divide Instructions
defm "" : LMULSEWSchedReads<"ReadVIDivV">;
defm "" : LMULSEWSchedReads<"ReadVIDivX">;
// 11.12. Vector Widening Integer Multiply Instructions
defm "" : LMULSchedReadsW<"ReadVIWMulV">;
defm "" : LMULSchedReadsW<"ReadVIWMulX">;
// 11.13. Vector Single-Width Integer Multiply-Add Instructions
defm "" : LMULSchedReads<"ReadVIMulAddV">;
defm "" : LMULSchedReads<"ReadVIMulAddX">;
// 11.14. Vector Widening Integer Multiply-Add Instructions
defm "" : LMULSchedReadsW<"ReadVIWMulAddV">;
defm "" : LMULSchedReadsW<"ReadVIWMulAddX">;
// 11.15. Vector Integer Merge Instructions
defm "" : LMULSchedReads<"ReadVIMergeV">;
defm "" : LMULSchedReads<"ReadVIMergeX">;
// 11.16. Vector Integer Move Instructions
defm "" : LMULSchedReads<"ReadVIMovV">;
defm "" : LMULSchedReads<"ReadVIMovX">;

// 12. Vector Fixed-Point Arithmetic Instructions
// 12.1. Vector Single-Width Saturating Add and Subtract
defm "" : LMULSchedReads<"ReadVSALUV">;
defm "" : LMULSchedReads<"ReadVSALUX">;
// 12.2. Vector Single-Width Averaging Add and Subtract
defm "" : LMULSchedReads<"ReadVAALUV">;
defm "" : LMULSchedReads<"ReadVAALUX">;
// 12.3. Vector Single-Width Fractional Multiply with Rounding and Saturation
defm "" : LMULSchedReads<"ReadVSMulV">;
defm "" : LMULSchedReads<"ReadVSMulX">;
// 12.4. Vector Single-Width Scaling Shift Instructions
defm "" : LMULSchedReads<"ReadVSShiftV">;
defm "" : LMULSchedReads<"ReadVSShiftX">;
// 12.5. Vector Narrowing Fixed-Point Clip Instructions
defm "" : LMULSchedReadsW<"ReadVNClipV">;
defm "" : LMULSchedReadsW<"ReadVNClipX">;

// 13. Vector Floating-Point Instructions
// 13.2. Vector Single-Width Floating-Point Add/Subtract Instructions
defm "" : LMULSEWSchedReadsF<"ReadVFALUV">;
defm "" : LMULSEWSchedReadsF<"ReadVFALUF">;
// 13.3. Vector Widening Floating-Point Add/Subtract Instructions
defm "" : LMULSEWSchedReadsFW<"ReadVFWALUV">;
defm "" : LMULSEWSchedReadsFW<"ReadVFWALUF">;
// 13.4. Vector Single-Width Floating-Point Multiply/Divide Instructions
defm "" : LMULSEWSchedReadsF<"ReadVFMulV">;
defm "" : LMULSEWSchedReadsF<"ReadVFMulF">;
defm "" : LMULSEWSchedReadsF<"ReadVFDivV">;
defm "" : LMULSEWSchedReadsF<"ReadVFDivF">;
// 13.5. Vector Widening Floating-Point Multiply
defm "" : LMULSEWSchedReadsFW<"ReadVFWMulV">;
defm "" : LMULSEWSchedReadsFW<"ReadVFWMulF">;
// 13.6. Vector Single-Width Floating-Point Fused Multiply-Add Instructions
defm "" : LMULSEWSchedReadsF<"ReadVFMulAddV">;
defm "" : LMULSEWSchedReadsF<"ReadVFMulAddF">;
// 13.7. Vector Widening Floating-Point Fused Multiply-Add Instructions
defm "" : LMULSEWSchedReadsFW<"ReadVFWMulAddV">;
defm "" : LMULSEWSchedReadsFW<"ReadVFWMulAddF">;
// 13.8. Vector Floating-Point Square-Root Instruction
defm "" : LMULSEWSchedReadsF<"ReadVFSqrtV">;
// 13.9. Vector Floating-Point Reciprocal Square-Root Estimate Instruction
// 13.10. Vector Floating-Point Reciprocal Estimate Instruction
defm "" : LMULSEWSchedReadsF<"ReadVFRecpV">;
// 13.11. Vector Floating-Point MIN/MAX Instructions
defm "" : LMULSEWSchedReadsF<"ReadVFMinMaxV">;
defm "" : LMULSEWSchedReadsF<"ReadVFMinMaxF">;
// 13.12. Vector Floating-Point Sign-Injection Instructions
defm "" : LMULSEWSchedReadsF<"ReadVFSgnjV">;
defm "" : LMULSEWSchedReadsF<"ReadVFSgnjF">;
// 13.13. Vector Floating-Point Compare Instructions
defm "" : LMULSchedReads<"ReadVFCmpV">;
defm "" : LMULSchedReads<"ReadVFCmpF">;
// 13.14. Vector Floating-Point Classify Instruction
defm "" : LMULSchedReads<"ReadVFClassV">;
// 13.15. Vector Floating-Point Merge Instruction
defm "" : LMULSchedReads<"ReadVFMergeV">;
defm "" : LMULSchedReads<"ReadVFMergeF">;
// 13.16. Vector Floating-Point Move Instruction
defm "" : LMULSchedReads<"ReadVFMovF">;
// 13.17. Single-Width Floating-Point/Integer Type-Convert Instructions
defm "" : LMULSEWSchedReadsF<"ReadVFCvtIToFV">;
defm "" : LMULSchedReads<"ReadVFCvtFToIV">;
// 13.18. Widening Floating-Point/Integer Type-Convert Instructions
defm "" : LMULSEWSchedReadsW<"ReadVFWCvtIToFV">;
defm "" : LMULSchedReadsFW<"ReadVFWCvtFToIV">;
defm "" : LMULSEWSchedReadsFW<"ReadVFWCvtFToFV">;
// 13.19. Narrowing Floating-Point/Integer Type-Convert Instructions
defm "" : LMULSEWSchedReadsFW<"ReadVFNCvtIToFV">;
defm "" : LMULSchedReadsW<"ReadVFNCvtFToIV">;
defm "" : LMULSEWSchedReadsFW<"ReadVFNCvtFToFV">;

// 14. Vector Reduction Operations
// 14.1. Vector Single-Width Integer Reduction Instructions
def ReadVIRedV        : SchedRead;
def ReadVIRedV0       : SchedRead;
// 14.2. Vector Widening Integer Reduction Instructions
def ReadVIWRedV       : SchedRead;
def ReadVIWRedV0      : SchedRead;
// 14.3. Vector Single-Width Floating-Point Reduction Instructions
def ReadVFRedV        : SchedRead;
def ReadVFRedV0       : SchedRead;
def ReadVFRedOV       : SchedRead;
def ReadVFRedOV0      : SchedRead;
def ReadVFRedMinMaxV  : SchedRead;
// 14.4. Vector Widening Floating-Point Reduction Instructions
def ReadVFWRedV       : SchedRead;
def ReadVFWRedV0      : SchedRead;
def ReadVFWRedOV      : SchedRead;
def ReadVFWRedOV0     : SchedRead;

// 15. Vector Mask Instructions
// 15.1. Vector Mask-Register Logical Instructions
defm "" : LMULSchedReads<"ReadVMALUV">;
// 15.2. Vector Mask Population Count
defm "" : LMULSchedReads<"ReadVMPopV">;
// 15.3. Vector Find-First-Set Mask Bit
defm "" : LMULSchedReads<"ReadVMFFSV">;
// 15.4. Vector Set-Before-First Mask Bit
// 15.5. Vector Set-Including-First Mask Bit
// 15.6. Vector Set-only-First Mask Bit
defm "" : LMULSchedReads<"ReadVMSFSV">;
// 15.8. Vector Iota Instruction
defm "" : LMULSchedReads<"ReadVIotaV">;

// 16. Vector Permutation Instructions
// 16.1. Integer Scalar Move Instructions
def ReadVMovXS : SchedRead;
def ReadVMovSX_V : SchedRead;
def ReadVMovSX_X : SchedRead;
// 16.2. Floating-Point Scalar Move Instructions
def ReadVMovFS : SchedRead;
def ReadVMovSF_V : SchedRead;
def ReadVMovSF_F : SchedRead;
// 16.3. Vector Slide Instructions
defm "" : LMULSchedReads<"ReadVISlideV">;
defm "" : LMULSchedReads<"ReadVISlideX">;
defm "" : LMULSchedReads<"ReadVFSlideV">;
defm "" : LMULSchedReads<"ReadVFSlideF">;
// 16.4. Vector Register Gather Instructions
defm "" : LMULSEWSchedReads<"ReadVRGatherVV_data">;
defm "" : LMULSEWSchedReads<"ReadVRGatherVV_index">;
defm "" : LMULSEWSchedReads<"ReadVRGatherEI16VV_data">;
defm "" : LMULSEWSchedReads<"ReadVRGatherEI16VV_index">;
defm "" : LMULSchedReads<"ReadVRGatherVX_data">;
defm "" : LMULSchedReads<"ReadVRGatherVX_index">;
defm "" : LMULSchedReads<"ReadVRGatherVI_data">;
// 16.5. Vector Compress Instruction
defm "" : LMULSEWSchedReads<"ReadVCompressV">;
// 16.6. Whole Vector Register Move
// These are already LMUL aware
def ReadVMov1V        : SchedRead;
def ReadVMov2V        : SchedRead;
def ReadVMov4V        : SchedRead;
def ReadVMov8V        : SchedRead;

// Others
def ReadVMask         : SchedRead;
def ReadVMergeOp_WorstCase : SchedRead;
foreach mx = SchedMxList in {
  def ReadVMergeOp_ # mx : SchedRead;
  foreach sew = SchedSEWSet<mx>.val in
    def ReadVMergeOp_ # mx  # "_E" # sew : SchedRead;
}

//===----------------------------------------------------------------------===//
/// Define default scheduler resources for V.

multiclass UnsupportedSchedV {
let Unsupported = true in {

// 3.6 Vector Byte Length vlenb
def : WriteRes<WriteRdVLENB, []>;

// 6. Configuration-Setting Instructions
def : WriteRes<WriteVSETVLI, []>;
def : WriteRes<WriteVSETIVLI, []>;
def : WriteRes<WriteVSETVL, []>;

// 7. Vector Loads and Stores
defm "" : LMULWriteRes<"WriteVLDE", []>;
defm "" : LMULWriteRes<"WriteVSTE", []>;
defm "" : LMULWriteRes<"WriteVLDM", []>;
defm "" : LMULWriteRes<"WriteVSTM", []>;
defm "" : LMULWriteRes<"WriteVLDS8", []>;
defm "" : LMULWriteRes<"WriteVLDS16", []>;
defm "" : LMULWriteRes<"WriteVLDS32", []>;
defm "" : LMULWriteRes<"WriteVLDS64", []>;
defm "" : LMULWriteRes<"WriteVSTS8", []>;
defm "" : LMULWriteRes<"WriteVSTS16", []>;
defm "" : LMULWriteRes<"WriteVSTS32", []>;
defm "" : LMULWriteRes<"WriteVSTS64", []>;
defm "" : LMULWriteRes<"WriteVLDUX8", []>;
defm "" : LMULWriteRes<"WriteVLDUX16", []>;
defm "" : LMULWriteRes<"WriteVLDUX32", []>;
defm "" : LMULWriteRes<"WriteVLDUX64", []>;
defm "" : LMULWriteRes<"WriteVLDOX8", []>;
defm "" : LMULWriteRes<"WriteVLDOX16", []>;
defm "" : LMULWriteRes<"WriteVLDOX32", []>;
defm "" : LMULWriteRes<"WriteVLDOX64", []>;
defm "" : LMULWriteRes<"WriteVSTUX8", []>;
defm "" : LMULWriteRes<"WriteVSTUX16", []>;
defm "" : LMULWriteRes<"WriteVSTUX32", []>;
defm "" : LMULWriteRes<"WriteVSTUX64", []>;
defm "" : LMULWriteRes<"WriteVSTOX8", []>;
defm "" : LMULWriteRes<"WriteVSTOX16", []>;
defm "" : LMULWriteRes<"WriteVSTOX32", []>;
defm "" : LMULWriteRes<"WriteVSTOX64", []>;
defm "" : LMULWriteRes<"WriteVLDFF", []>;
// These are already LMUL aware
def : WriteRes<WriteVLD1R, []>;
def : WriteRes<WriteVLD2R, []>;
def : WriteRes<WriteVLD4R, []>;
def : WriteRes<WriteVLD8R, []>;
def : WriteRes<WriteVST1R, []>;
def : WriteRes<WriteVST2R, []>;
def : WriteRes<WriteVST4R, []>;
def : WriteRes<WriteVST8R, []>;
// Vector Segment Loads and Stores
foreach nf=2-8 in {
  foreach eew = [8, 16, 32, 64] in {
    defm "" : LMULWriteRes <"WriteVLSEG" # nf # "e" # eew, []>;
    defm "" : LMULWriteRes <"WriteVLSEGFF" # nf # "e" # eew, []>;
    defm "" : LMULWriteRes <"WriteVSSEG" # nf # "e" # eew, []>;
    defm "" : LMULWriteRes <"WriteVLSSEG" # nf # "e" # eew, []>;
    defm "" : LMULWriteRes <"WriteVSSSEG" # nf # "e" # eew, []>;
    defm "" : LMULWriteRes <"WriteVLUXSEG" # nf # "e" # eew, []>;
    defm "" : LMULWriteRes <"WriteVLOXSEG" # nf # "e" # eew, []>;
    defm "" : LMULWriteRes <"WriteVSUXSEG" # nf # "e" # eew, []>;
    defm "" : LMULWriteRes <"WriteVSOXSEG" # nf # "e" # eew, []>;
  }
}

// 11. Vector Integer Arithmetic Instructions
defm "" : LMULWriteRes<"WriteVIALUV", []>;
defm "" : LMULWriteRes<"WriteVIALUX", []>;
defm "" : LMULWriteRes<"WriteVIALUI", []>;
defm "" : LMULWriteResW<"WriteVIWALUV", []>;
defm "" : LMULWriteResW<"WriteVIWALUX", []>;
defm "" : LMULWriteResW<"WriteVIWALUI", []>;
defm "" : LMULWriteRes<"WriteVExtV", []>;
defm "" : LMULWriteRes<"WriteVICALUV", []>;
defm "" : LMULWriteRes<"WriteVICALUX", []>;
defm "" : LMULWriteRes<"WriteVICALUI", []>;
defm "" : LMULWriteRes<"WriteVShiftV", []>;
defm "" : LMULWriteRes<"WriteVShiftX", []>;
defm "" : LMULWriteRes<"WriteVShiftI", []>;
defm "" : LMULWriteResW<"WriteVNShiftV", []>;
defm "" : LMULWriteResW<"WriteVNShiftX", []>;
defm "" : LMULWriteResW<"WriteVNShiftI", []>;
defm "" : LMULWriteRes<"WriteVICmpV", []>;
defm "" : LMULWriteRes<"WriteVICmpX", []>;
defm "" : LMULWriteRes<"WriteVICmpI", []>;
defm "" : LMULWriteRes<"WriteVIMinMaxV", []>;
defm "" : LMULWriteRes<"WriteVIMinMaxX", []>;
defm "" : LMULWriteRes<"WriteVIMulV", []>;
defm "" : LMULWriteRes<"WriteVIMulX", []>;
defm "" : LMULSEWWriteRes<"WriteVIDivV", []>;
defm "" : LMULSEWWriteRes<"WriteVIDivX", []>;
defm "" : LMULWriteResW<"WriteVIWMulV", []>;
defm "" : LMULWriteResW<"WriteVIWMulX", []>;
defm "" : LMULWriteRes<"WriteVIMulAddV", []>;
defm "" : LMULWriteRes<"WriteVIMulAddX", []>;
defm "" : LMULWriteResW<"WriteVIWMulAddV", []>;
defm "" : LMULWriteResW<"WriteVIWMulAddX", []>;
defm "" : LMULWriteRes<"WriteVIMergeV", []>;
defm "" : LMULWriteRes<"WriteVIMergeX", []>;
defm "" : LMULWriteRes<"WriteVIMergeI", []>;
defm "" : LMULWriteRes<"WriteVIMovV", []>;
defm "" : LMULWriteRes<"WriteVIMovX", []>;
defm "" : LMULWriteRes<"WriteVIMovI", []>;

// 12. Vector Fixed-Point Arithmetic Instructions
defm "" : LMULWriteRes<"WriteVSALUV", []>;
defm "" : LMULWriteRes<"WriteVSALUX", []>;
defm "" : LMULWriteRes<"WriteVSALUI", []>;
defm "" : LMULWriteRes<"WriteVAALUV", []>;
defm "" : LMULWriteRes<"WriteVAALUX", []>;
defm "" : LMULWriteRes<"WriteVSMulV", []>;
defm "" : LMULWriteRes<"WriteVSMulX", []>;
defm "" : LMULWriteRes<"WriteVSShiftV", []>;
defm "" : LMULWriteRes<"WriteVSShiftX", []>;
defm "" : LMULWriteRes<"WriteVSShiftI", []>;
defm "" : LMULWriteResW<"WriteVNClipV", []>;
defm "" : LMULWriteResW<"WriteVNClipX", []>;
defm "" : LMULWriteResW<"WriteVNClipI", []>;

// 13. Vector Floating-Point Instructions
defm "" : LMULSEWWriteResF<"WriteVFALUV", []>;
defm "" : LMULSEWWriteResF<"WriteVFALUF", []>;
defm "" : LMULSEWWriteResFW<"WriteVFWALUV", []>;
defm "" : LMULSEWWriteResFW<"WriteVFWALUF", []>;
defm "" : LMULSEWWriteResF<"WriteVFMulV", []>;
defm "" : LMULSEWWriteResF<"WriteVFMulF", []>;
defm "" : LMULSEWWriteResF<"WriteVFDivV", []>;
defm "" : LMULSEWWriteResF<"WriteVFDivF", []>;
defm "" : LMULSEWWriteResFW<"WriteVFWMulV", []>;
defm "" : LMULSEWWriteResFW<"WriteVFWMulF", []>;
defm "" : LMULSEWWriteResF<"WriteVFMulAddV", []>;
defm "" : LMULSEWWriteResF<"WriteVFMulAddF", []>;
defm "" : LMULSEWWriteResFW<"WriteVFWMulAddV", []>;
defm "" : LMULSEWWriteResFW<"WriteVFWMulAddF", []>;
defm "" : LMULSEWWriteResF<"WriteVFSqrtV", []>;
defm "" : LMULSEWWriteResF<"WriteVFRecpV", []>;
defm "" : LMULSEWWriteResF<"WriteVFMinMaxV", []>;
defm "" : LMULSEWWriteResF<"WriteVFMinMaxF", []>;
defm "" : LMULSEWWriteResF<"WriteVFSgnjV", []>;
defm "" : LMULSEWWriteResF<"WriteVFSgnjF", []>;
defm "" : LMULWriteRes<"WriteVFCmpV", []>;
defm "" : LMULWriteRes<"WriteVFCmpF", []>;
defm "" : LMULWriteRes<"WriteVFClassV", []>;
defm "" : LMULWriteRes<"WriteVFMergeV", []>;
defm "" : LMULWriteRes<"WriteVFMovV", []>;
defm "" : LMULSEWWriteResF<"WriteVFCvtIToFV", []>;
defm "" : LMULWriteRes<"WriteVFCvtFToIV", []>;
defm "" : LMULSEWWriteResW<"WriteVFWCvtIToFV", []>;
defm "" : LMULWriteResFW<"WriteVFWCvtFToIV", []>;
defm "" : LMULSEWWriteResFW<"WriteVFWCvtFToFV", []>;
defm "" : LMULSEWWriteResFW<"WriteVFNCvtIToFV", []>;
defm "" : LMULWriteResW<"WriteVFNCvtFToIV", []>;
defm "" : LMULSEWWriteResFW<"WriteVFNCvtFToFV", []>;

// 14. Vector Reduction Operations
defm "" : LMULSEWWriteRes<"WriteVIRedV_From", []>;
defm "" : LMULSEWWriteRes<"WriteVIRedMinMaxV_From", []>;
defm "" : LMULSEWWriteResWRed<"WriteVIWRedV_From", []>;
defm "" : LMULSEWWriteResF<"WriteVFRedV_From", []>;
defm "" : LMULSEWWriteResF<"WriteVFRedOV_From", []>;
defm "" : LMULSEWWriteResF<"WriteVFRedMinMaxV_From", []>;
defm "" : LMULSEWWriteResFWRed<"WriteVFWRedV_From", []>;
defm "" : LMULSEWWriteResFWRed<"WriteVFWRedOV_From", []>;

// 15. Vector Mask Instructions
defm "" : LMULWriteRes<"WriteVMALUV", []>;
defm "" : LMULWriteRes<"WriteVMPopV", []>;
defm "" : LMULWriteRes<"WriteVMFFSV", []>;
defm "" : LMULWriteRes<"WriteVMSFSV", []>;
defm "" : LMULWriteRes<"WriteVIotaV", []>;
defm "" : LMULWriteRes<"WriteVIdxV", []>;

// 16. Vector Permutation Instructions
def : WriteRes<WriteVMovSX, []>;
def : WriteRes<WriteVMovXS, []>;
def : WriteRes<WriteVMovSF, []>;
def : WriteRes<WriteVMovFS, []>;
defm "" : LMULWriteRes<"WriteVSlideUpX", []>;
defm "" : LMULWriteRes<"WriteVSlideDownX", []>;
defm "" : LMULWriteRes<"WriteVSlideI", []>;
defm "" : LMULWriteRes<"WriteVISlide1X", []>;
defm "" : LMULWriteRes<"WriteVFSlide1F", []>;
defm "" : LMULSEWWriteRes<"WriteVRGatherVV", []>;
defm "" : LMULSEWWriteRes<"WriteVRGatherEI16VV", []>;
defm "" : LMULWriteRes<"WriteVRGatherVX", []>;
defm "" : LMULWriteRes<"WriteVRGatherVI", []>;
defm "" : LMULSEWWriteRes<"WriteVCompressV", []>;
// These are already LMUL aware
def : WriteRes<WriteVMov1V, []>;
def : WriteRes<WriteVMov2V, []>;
def : WriteRes<WriteVMov4V, []>;
def : WriteRes<WriteVMov8V, []>;

// 6. Configuration-Setting Instructions
def : ReadAdvance<ReadVSETVLI, 0>;
def : ReadAdvance<ReadVSETVL, 0>;

// 7. Vector Loads and Stores
def : ReadAdvance<ReadVLDX, 0>;
def : ReadAdvance<ReadVSTX, 0>;
defm "" : LMULReadAdvance<"ReadVSTEV", 0>;
defm "" : LMULReadAdvance<"ReadVSTM", 0>;
def : ReadAdvance<ReadVLDSX, 0>;
def : ReadAdvance<ReadVSTSX, 0>;
defm "" : LMULReadAdvance<"ReadVSTS8V", 0>;
defm "" : LMULReadAdvance<"ReadVSTS16V", 0>;
defm "" : LMULReadAdvance<"ReadVSTS32V", 0>;
defm "" : LMULReadAdvance<"ReadVSTS64V", 0>;
defm "" : LMULReadAdvance<"ReadVLDUXV", 0>;
defm "" : LMULReadAdvance<"ReadVLDOXV", 0>;
defm "" : LMULReadAdvance<"ReadVSTUXV", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX8", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX16", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX32", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX64", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX8V", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX16V", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX32V", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX64V", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX8", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX16", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX32", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX64", 0>;
defm "" : LMULReadAdvance<"ReadVSTOXV", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX8V", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX16V", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX32V", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX64V", 0>;
// These are already LMUL aware
def : ReadAdvance<ReadVST1R, 0>;
def : ReadAdvance<ReadVST2R, 0>;
def : ReadAdvance<ReadVST4R, 0>;
def : ReadAdvance<ReadVST8R, 0>;

// 11. Vector Integer Arithmetic Instructions
defm "" : LMULReadAdvance<"ReadVIALUV", 0>;
defm "" : LMULReadAdvance<"ReadVIALUX", 0>;
defm "" : LMULReadAdvanceW<"ReadVIWALUV", 0>;
defm "" : LMULReadAdvanceW<"ReadVIWALUX", 0>;
defm "" : LMULReadAdvance<"ReadVExtV", 0>;
defm "" : LMULReadAdvance<"ReadVICALUV", 0>;
defm "" : LMULReadAdvance<"ReadVICALUX", 0>;
defm "" : LMULReadAdvance<"ReadVShiftV", 0>;
defm "" : LMULReadAdvance<"ReadVShiftX", 0>;
defm "" : LMULReadAdvanceW<"ReadVNShiftV", 0>;
defm "" : LMULReadAdvanceW<"ReadVNShiftX", 0>;
defm "" : LMULReadAdvance<"ReadVICmpV", 0>;
defm "" : LMULReadAdvance<"ReadVICmpX", 0>;
defm "" : LMULReadAdvance<"ReadVIMinMaxV", 0>;
defm "" : LMULReadAdvance<"ReadVIMinMaxX", 0>;
defm "" : LMULReadAdvance<"ReadVIMulV", 0>;
defm "" : LMULReadAdvance<"ReadVIMulX", 0>;
defm "" : LMULSEWReadAdvance<"ReadVIDivV", 0>;
defm "" : LMULSEWReadAdvance<"ReadVIDivX", 0>;
defm "" : LMULReadAdvanceW<"ReadVIWMulV", 0>;
defm "" : LMULReadAdvanceW<"ReadVIWMulX", 0>;
defm "" : LMULReadAdvance<"ReadVIMulAddV", 0>;
defm "" : LMULReadAdvance<"ReadVIMulAddX", 0>;
defm "" : LMULReadAdvanceW<"ReadVIWMulAddV", 0>;
defm "" : LMULReadAdvanceW<"ReadVIWMulAddX", 0>;
defm "" : LMULReadAdvance<"ReadVIMergeV", 0>;
defm "" : LMULReadAdvance<"ReadVIMergeX", 0>;
defm "" : LMULReadAdvance<"ReadVIMovV", 0>;
defm "" : LMULReadAdvance<"ReadVIMovX", 0>;

// 12. Vector Fixed-Point Arithmetic Instructions
defm "" : LMULReadAdvance<"ReadVSALUV", 0>;
defm "" : LMULReadAdvance<"ReadVSALUX", 0>;
defm "" : LMULReadAdvance<"ReadVAALUV", 0>;
defm "" : LMULReadAdvance<"ReadVAALUX", 0>;
defm "" : LMULReadAdvance<"ReadVSMulV", 0>;
defm "" : LMULReadAdvance<"ReadVSMulX", 0>;
defm "" : LMULReadAdvance<"ReadVSShiftV", 0>;
defm "" : LMULReadAdvance<"ReadVSShiftX", 0>;
defm "" : LMULReadAdvanceW<"ReadVNClipV", 0>;
defm "" : LMULReadAdvanceW<"ReadVNClipX", 0>;

// 13. Vector Floating-Point Instructions
defm "" : LMULSEWReadAdvanceF<"ReadVFALUV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFALUF", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWALUV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWALUF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMulV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMulF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFDivV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFDivF", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWMulV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWMulF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMulAddV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMulAddF", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWMulAddV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWMulAddF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFSqrtV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFRecpV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMinMaxV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMinMaxF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFSgnjV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFSgnjF", 0>;
defm "" : LMULReadAdvance<"ReadVFCmpV", 0>;
defm "" : LMULReadAdvance<"ReadVFCmpF", 0>;
defm "" : LMULReadAdvance<"ReadVFClassV", 0>;
defm "" : LMULReadAdvance<"ReadVFMergeV", 0>;
defm "" : LMULReadAdvance<"ReadVFMergeF", 0>;
defm "" : LMULReadAdvance<"ReadVFMovF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFCvtIToFV", 0>;
defm "" : LMULReadAdvance<"ReadVFCvtFToIV", 0>;
defm "" : LMULSEWReadAdvanceW<"ReadVFWCvtIToFV", 0>;
defm "" : LMULReadAdvanceFW<"ReadVFWCvtFToIV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWCvtFToFV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFNCvtIToFV", 0>;
defm "" : LMULReadAdvanceW<"ReadVFNCvtFToIV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFNCvtFToFV", 0>;

// 14. Vector Reduction Operations
def : ReadAdvance<ReadVIRedV, 0>;
def : ReadAdvance<ReadVIRedV0, 0>;
def : ReadAdvance<ReadVIWRedV, 0>;
def : ReadAdvance<ReadVIWRedV0, 0>;
def : ReadAdvance<ReadVFRedV, 0>;
def : ReadAdvance<ReadVFRedV0, 0>;
def : ReadAdvance<ReadVFRedOV, 0>;
def : ReadAdvance<ReadVFRedOV0, 0>;
def : ReadAdvance<ReadVFRedMinMaxV, 0>;
def : ReadAdvance<ReadVFWRedV, 0>;
def : ReadAdvance<ReadVFWRedV0, 0>;
def : ReadAdvance<ReadVFWRedOV, 0>;
def : ReadAdvance<ReadVFWRedOV0, 0>;

// 15. Vector Mask Instructions
defm "" : LMULReadAdvance<"ReadVMALUV", 0>;
defm "" : LMULReadAdvance<"ReadVMPopV", 0>;
defm "" : LMULReadAdvance<"ReadVMFFSV", 0>;
defm "" : LMULReadAdvance<"ReadVMSFSV", 0>;
defm "" : LMULReadAdvance<"ReadVIotaV", 0>;

// 16. Vector Permutation Instructions
def : ReadAdvance<ReadVMovXS, 0>;
def : ReadAdvance<ReadVMovSX_V, 0>;
def : ReadAdvance<ReadVMovSX_X, 0>;
def : ReadAdvance<ReadVMovFS, 0>;
def : ReadAdvance<ReadVMovSF_V, 0>;
def : ReadAdvance<ReadVMovSF_F, 0>;
defm "" : LMULReadAdvance<"ReadVISlideV", 0>;
defm "" : LMULReadAdvance<"ReadVISlideX", 0>;
defm "" : LMULReadAdvance<"ReadVFSlideV", 0>;
defm "" : LMULReadAdvance<"ReadVFSlideF", 0>;
defm "" : LMULSEWReadAdvance<"ReadVRGatherVV_data", 0>;
defm "" : LMULSEWReadAdvance<"ReadVRGatherVV_index", 0>;
defm "" : LMULSEWReadAdvance<"ReadVRGatherEI16VV_data", 0>;
defm "" : LMULSEWReadAdvance<"ReadVRGatherEI16VV_index", 0>;
defm "" : LMULReadAdvance<"ReadVRGatherVX_data", 0>;
defm "" : LMULReadAdvance<"ReadVRGatherVX_index", 0>;
defm "" : LMULReadAdvance<"ReadVRGatherVI_data", 0>;
defm "" : LMULReadAdvance<"ReadVGatherV", 0>;
defm "" : LMULSEWReadAdvance<"ReadVCompressV", 0>;
// These are already LMUL aware
def : ReadAdvance<ReadVMov1V, 0>;
def : ReadAdvance<ReadVMov2V, 0>;
def : ReadAdvance<ReadVMov4V, 0>;
def : ReadAdvance<ReadVMov8V, 0>;

// Others
def : ReadAdvance<ReadVMask, 0>;
def : ReadAdvance<ReadVMergeOp_WorstCase, 0>;
foreach mx = SchedMxList in {
  def : ReadAdvance<!cast<SchedRead>("ReadVMergeOp_" # mx), 0>;
  foreach sew = SchedSEWSet<mx>.val in
    def : ReadAdvance<!cast<SchedRead>("ReadVMergeOp_" # mx  # "_E" # sew), 0>;
}

} // Unsupported
} // UnsupportedSchedV
