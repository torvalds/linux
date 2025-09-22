//===------ RISCVProfiles.td - RISC-V Profiles -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class RISCVProfile<string name, list<SubtargetFeature> features>
    : SubtargetFeature<name, "Is" # NAME, "true",
                       "RISC-V " # name # " profile", features> {
  // Indicates if the profile is not yet ratified, so should be treated as
  // experimental.
  bit Experimental = false;
}
class RISCVExperimentalProfile<string name, list<SubtargetFeature> features>
    : RISCVProfile<"experimental-"#name, features> {
  let Experimental = true;
}

defvar RVI20U32Features = [Feature32Bit, FeatureStdExtI];
defvar RVI20U64Features = [Feature64Bit, FeatureStdExtI];

defvar RVA20U64Features = [Feature64Bit,
                           FeatureStdExtI,
                           FeatureStdExtM,
                           FeatureStdExtA,
                           FeatureStdExtF,
                           FeatureStdExtD,
                           FeatureStdExtC,
                           FeatureStdExtZicntr,
                           FeatureStdExtZiccif,
                           FeatureStdExtZiccrse,
                           FeatureStdExtZiccamoa,
                           FeatureStdExtZa128rs,
                           FeatureStdExtZicclsm];

defvar RVA20S64Features = !listconcat(RVA20U64Features,
                                      [FeatureStdExtZifencei,
                                       FeatureStdExtSvbare,
                                       FeatureStdExtSvade,
                                       FeatureStdExtSsccptr,
                                       FeatureStdExtSstvecd,
                                       FeatureStdExtSstvala]);

defvar RVA22U64Features = [Feature64Bit,
                           FeatureStdExtI,
                           FeatureStdExtM,
                           FeatureStdExtA,
                           FeatureStdExtF,
                           FeatureStdExtD,
                           FeatureStdExtC,
                           FeatureStdExtZicntr,
                           FeatureStdExtZiccif,
                           FeatureStdExtZiccrse,
                           FeatureStdExtZiccamoa,
                           FeatureStdExtZicclsm,
                           FeatureStdExtZa64rs,
                           FeatureStdExtZihpm,
                           FeatureStdExtZihintpause,
                           FeatureStdExtZba,
                           FeatureStdExtZbb,
                           FeatureStdExtZbs,
                           FeatureStdExtZic64b,
                           FeatureStdExtZicbom,
                           FeatureStdExtZicbop,
                           FeatureStdExtZicboz,
                           FeatureStdExtZfhmin,
                           FeatureStdExtZkt];

defvar RVA22S64Features = !listconcat(RVA22U64Features,
                                      [FeatureStdExtZifencei,
                                       FeatureStdExtSvbare,
                                       FeatureStdExtSvade,
                                       FeatureStdExtSsccptr,
                                       FeatureStdExtSstvecd,
                                       FeatureStdExtSstvala,
                                       FeatureStdExtSscounterenw,
                                       FeatureStdExtSvpbmt,
                                       FeatureStdExtSvinval]);

defvar RVA23U64Features = [Feature64Bit,
                           FeatureStdExtI,
                           FeatureStdExtM,
                           FeatureStdExtA,
                           FeatureStdExtF,
                           FeatureStdExtD,
                           FeatureStdExtC,
                           FeatureStdExtZicntr,
                           FeatureStdExtZihpm,
                           FeatureStdExtZiccif,
                           FeatureStdExtZiccrse,
                           FeatureStdExtZiccamoa,
                           FeatureStdExtZicclsm,
                           FeatureStdExtZa64rs,
                           FeatureStdExtZihintpause,
                           FeatureStdExtZba,
                           FeatureStdExtZbb,
                           FeatureStdExtZbs,
                           FeatureStdExtZic64b,
                           FeatureStdExtZicbom,
                           FeatureStdExtZicbop,
                           FeatureStdExtZicboz,
                           FeatureStdExtZfhmin,
                           FeatureStdExtZkt,
                           FeatureStdExtV,
                           FeatureStdExtZvfhmin,
                           FeatureStdExtZvbb,
                           FeatureStdExtZvkt,
                           FeatureStdExtZihintntl,
                           FeatureStdExtZicond,
                           FeatureStdExtZimop,
                           FeatureStdExtZcmop,
                           FeatureStdExtZcb,
                           FeatureStdExtZfa,
                           FeatureStdExtZawrs];

defvar RVA23S64Features = !listconcat(RVA23U64Features,
                                      [FeatureStdExtZifencei,
                                       FeatureStdExtSvbare,
                                       FeatureStdExtSvade,
                                       FeatureStdExtSsccptr,
                                       FeatureStdExtSstvecd,
                                       FeatureStdExtSstvala,
                                       FeatureStdExtSscounterenw,
                                       FeatureStdExtSvpbmt,
                                       FeatureStdExtSvinval,
                                       FeatureStdExtSvnapot,
                                       FeatureStdExtSstc,
                                       FeatureStdExtSscofpmf,
                                       FeatureStdExtSsnpm,
                                       FeatureStdExtSsu64xl,
                                       FeatureStdExtH,
                                       FeatureStdExtSsstateen,
                                       FeatureStdExtShcounterenw,
                                       FeatureStdExtShvstvala,
                                       FeatureStdExtShtvala,
                                       FeatureStdExtShvstvecd,
                                       FeatureStdExtShvsatpa,
                                       FeatureStdExtShgatpa]);

defvar RVB23U64Features = [Feature64Bit,
                           FeatureStdExtI,
                           FeatureStdExtM,
                           FeatureStdExtA,
                           FeatureStdExtF,
                           FeatureStdExtD,
                           FeatureStdExtC,
                           FeatureStdExtZicntr,
                           FeatureStdExtZihpm,
                           FeatureStdExtZiccif,
                           FeatureStdExtZiccrse,
                           FeatureStdExtZiccamoa,
                           FeatureStdExtZicclsm,
                           FeatureStdExtZa64rs,
                           FeatureStdExtZihintpause,
                           FeatureStdExtZba,
                           FeatureStdExtZbb,
                           FeatureStdExtZbs,
                           FeatureStdExtZic64b,
                           FeatureStdExtZicbom,
                           FeatureStdExtZicbop,
                           FeatureStdExtZicboz,
                           FeatureStdExtZkt,
                           FeatureStdExtZihintntl,
                           FeatureStdExtZicond,
                           FeatureStdExtZimop,
                           FeatureStdExtZcmop,
                           FeatureStdExtZcb,
                           FeatureStdExtZfa,
                           FeatureStdExtZawrs];

defvar RVB23S64Features = !listconcat(RVB23U64Features,
                                      [FeatureStdExtZifencei,
                                       FeatureStdExtSvnapot,
                                       FeatureStdExtSvbare,
                                       FeatureStdExtSvade,
                                       FeatureStdExtSsccptr,
                                       FeatureStdExtSstvecd,
                                       FeatureStdExtSstvala,
                                       FeatureStdExtSscounterenw,
                                       FeatureStdExtSvpbmt,
                                       FeatureStdExtSvinval,
                                       FeatureStdExtSstc,
                                       FeatureStdExtSscofpmf,
                                       FeatureStdExtSsu64xl]);

defvar RVM23U32Features = [Feature32Bit,
                           FeatureStdExtI,
                           FeatureStdExtM,
                           FeatureStdExtZba,
                           FeatureStdExtZbb,
                           FeatureStdExtZbs,
                           FeatureStdExtZicond,
                           FeatureStdExtZihintpause,
                           FeatureStdExtZihintntl,
                           FeatureStdExtZce,
                           FeatureStdExtZicbop,
                           FeatureStdExtZimop,
                           FeatureStdExtZcmop];

def RVI20U32 : RISCVProfile<"rvi20u32", RVI20U32Features>;
def RVI20U64 : RISCVProfile<"rvi20u64", RVI20U64Features>;
def RVA20U64 : RISCVProfile<"rva20u64", RVA20U64Features>;
def RVA20S64 : RISCVProfile<"rva20s64", RVA20S64Features>;
def RVA22U64 : RISCVProfile<"rva22u64", RVA22U64Features>;
def RVA22S64 : RISCVProfile<"rva22s64", RVA22S64Features>;
def RVA23U64 : RISCVExperimentalProfile<"rva23u64", RVA23U64Features>;
def RVA23S64 : RISCVExperimentalProfile<"rva23s64", RVA23S64Features>;
def RVB23U64 : RISCVExperimentalProfile<"rvb23u64", RVB23U64Features>;
def RVB23S64 : RISCVExperimentalProfile<"rvb23s64", RVB23S64Features>;
def RVM23U32 : RISCVExperimentalProfile<"rvm23u32", RVM23U32Features>;
