//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Automatically generated file, do not edit!
//===----------------------------------------------------------------------===//

def ArchV5: SubtargetFeature<"v5", "HexagonArchVersion", "Hexagon::ArchEnum::V5", "Enable Hexagon V5 architecture">;
def HasV5 : Predicate<"HST->hasV5Ops()">, AssemblerPredicate<(all_of ArchV5)>;
def ArchV55: SubtargetFeature<"v55", "HexagonArchVersion", "Hexagon::ArchEnum::V55", "Enable Hexagon V55 architecture">;
def HasV55 : Predicate<"HST->hasV55Ops()">, AssemblerPredicate<(all_of ArchV55)>;
def ArchV60: SubtargetFeature<"v60", "HexagonArchVersion", "Hexagon::ArchEnum::V60", "Enable Hexagon V60 architecture">;
def HasV60 : Predicate<"HST->hasV60Ops()">, AssemblerPredicate<(all_of ArchV60)>;
def ArchV62: SubtargetFeature<"v62", "HexagonArchVersion", "Hexagon::ArchEnum::V62", "Enable Hexagon V62 architecture">;
def HasV62 : Predicate<"HST->hasV62Ops()">, AssemblerPredicate<(all_of ArchV62)>;
def ArchV65: SubtargetFeature<"v65", "HexagonArchVersion", "Hexagon::ArchEnum::V65", "Enable Hexagon V65 architecture">;
def HasV65 : Predicate<"HST->hasV65Ops()">, AssemblerPredicate<(all_of ArchV65)>;
def ArchV66: SubtargetFeature<"v66", "HexagonArchVersion", "Hexagon::ArchEnum::V66", "Enable Hexagon V66 architecture">;
def HasV66 : Predicate<"HST->hasV66Ops()">, AssemblerPredicate<(all_of ArchV66)>;
def ArchV67: SubtargetFeature<"v67", "HexagonArchVersion", "Hexagon::ArchEnum::V67", "Enable Hexagon V67 architecture">;
def HasV67 : Predicate<"HST->hasV67Ops()">, AssemblerPredicate<(all_of ArchV67)>;
def ArchV68: SubtargetFeature<"v68", "HexagonArchVersion", "Hexagon::ArchEnum::V68", "Enable Hexagon V68 architecture">;
def HasV68 : Predicate<"HST->hasV68Ops()">, AssemblerPredicate<(all_of ArchV68)>;
def ArchV69: SubtargetFeature<"v69", "HexagonArchVersion", "Hexagon::ArchEnum::V69", "Enable Hexagon V69 architecture">;
def HasV69 : Predicate<"HST->hasV69Ops()">, AssemblerPredicate<(all_of ArchV69)>;
def ArchV71: SubtargetFeature<"v71", "HexagonArchVersion", "Hexagon::ArchEnum::V71", "Enable Hexagon V71 architecture">;
def HasV71 : Predicate<"HST->hasV71Ops()">, AssemblerPredicate<(all_of ArchV71)>;
def ArchV73: SubtargetFeature<"v73", "HexagonArchVersion", "Hexagon::ArchEnum::V73", "Enable Hexagon V73 architecture">;
def HasV73 : Predicate<"HST->hasV73Ops()">, AssemblerPredicate<(all_of ArchV73)>;
