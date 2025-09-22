//===--- AVR.cpp - AVR ToolChain Implementations ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AVR.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/TargetParser/SubtargetFeature.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

namespace {

// NOTE: This list has been synchronized with gcc-avr 7.3.0 and avr-libc 2.0.0.
constexpr struct {
  StringRef Name;
  StringRef SubPath;
  StringRef Family;
  unsigned DataAddr;
} MCUInfo[] = {
    {"at90s1200", "", "avr1", 0},
    {"attiny11", "", "avr1", 0},
    {"attiny12", "", "avr1", 0},
    {"attiny15", "", "avr1", 0},
    {"attiny28", "", "avr1", 0},
    {"at90s2313", "tiny-stack", "avr2", 0x800060},
    {"at90s2323", "tiny-stack", "avr2", 0x800060},
    {"at90s2333", "tiny-stack", "avr2", 0x800060},
    {"at90s2343", "tiny-stack", "avr2", 0x800060},
    {"at90s4433", "tiny-stack", "avr2", 0x800060},
    {"attiny22", "tiny-stack", "avr2", 0x800060},
    {"attiny26", "tiny-stack", "avr2", 0x800060},
    {"at90s4414", "", "avr2", 0x800060},
    {"at90s4434", "", "avr2", 0x800060},
    {"at90s8515", "", "avr2", 0x800060},
    {"at90c8534", "", "avr2", 0x800060},
    {"at90s8535", "", "avr2", 0x800060},
    {"attiny13", "avr25/tiny-stack", "avr25", 0x800060},
    {"attiny13a", "avr25/tiny-stack", "avr25", 0x800060},
    {"attiny2313", "avr25/tiny-stack", "avr25", 0x800060},
    {"attiny2313a", "avr25/tiny-stack", "avr25", 0x800060},
    {"attiny24", "avr25/tiny-stack", "avr25", 0x800060},
    {"attiny24a", "avr25/tiny-stack", "avr25", 0x800060},
    {"attiny25", "avr25/tiny-stack", "avr25", 0x800060},
    {"attiny261", "avr25/tiny-stack", "avr25", 0x800060},
    {"attiny261a", "avr25/tiny-stack", "avr25", 0x800060},
    {"at86rf401", "avr25", "avr25", 0x800060},
    {"ata5272", "avr25", "avr25", 0x800100},
    {"ata6616c", "avr25", "avr25", 0x800100},
    {"attiny4313", "avr25", "avr25", 0x800060},
    {"attiny44", "avr25", "avr25", 0x800060},
    {"attiny44a", "avr25", "avr25", 0x800060},
    {"attiny84", "avr25", "avr25", 0x800060},
    {"attiny84a", "avr25", "avr25", 0x800060},
    {"attiny45", "avr25", "avr25", 0x800060},
    {"attiny85", "avr25", "avr25", 0x800060},
    {"attiny441", "avr25", "avr25", 0x800100},
    {"attiny461", "avr25", "avr25", 0x800060},
    {"attiny461a", "avr25", "avr25", 0x800060},
    {"attiny841", "avr25", "avr25", 0x800100},
    {"attiny861", "avr25", "avr25", 0x800060},
    {"attiny861a", "avr25", "avr25", 0x800060},
    {"attiny87", "avr25", "avr25", 0x800100},
    {"attiny43u", "avr25", "avr25", 0x800060},
    {"attiny48", "avr25", "avr25", 0x800100},
    {"attiny88", "avr25", "avr25", 0x800100},
    {"attiny828", "avr25", "avr25", 0x800100},
    {"at43usb355", "avr3", "avr3", 0x800100},
    {"at76c711", "avr3", "avr3", 0x800060},
    {"atmega103", "avr31", "avr31", 0x800060},
    {"at43usb320", "avr31", "avr31", 0x800060},
    {"attiny167", "avr35", "avr35", 0x800100},
    {"at90usb82", "avr35", "avr35", 0x800100},
    {"at90usb162", "avr35", "avr35", 0x800100},
    {"ata5505", "avr35", "avr35", 0x800100},
    {"ata6617c", "avr35", "avr35", 0x800100},
    {"ata664251", "avr35", "avr35", 0x800100},
    {"atmega8u2", "avr35", "avr35", 0x800100},
    {"atmega16u2", "avr35", "avr35", 0x800100},
    {"atmega32u2", "avr35", "avr35", 0x800100},
    {"attiny1634", "avr35", "avr35", 0x800100},
    {"atmega8", "avr4", "avr4", 0x800060},
    {"ata6289", "avr4", "avr4", 0x800100},
    {"atmega8a", "avr4", "avr4", 0x800060},
    {"ata6285", "avr4", "avr4", 0x800100},
    {"ata6286", "avr4", "avr4", 0x800100},
    {"ata6612c", "avr4", "avr4", 0x800100},
    {"atmega48", "avr4", "avr4", 0x800100},
    {"atmega48a", "avr4", "avr4", 0x800100},
    {"atmega48pa", "avr4", "avr4", 0x800100},
    {"atmega48pb", "avr4", "avr4", 0x800100},
    {"atmega48p", "avr4", "avr4", 0x800100},
    {"atmega88", "avr4", "avr4", 0x800100},
    {"atmega88a", "avr4", "avr4", 0x800100},
    {"atmega88p", "avr4", "avr4", 0x800100},
    {"atmega88pa", "avr4", "avr4", 0x800100},
    {"atmega88pb", "avr4", "avr4", 0x800100},
    {"atmega8515", "avr4", "avr4", 0x800060},
    {"atmega8535", "avr4", "avr4", 0x800060},
    {"atmega8hva", "avr4", "avr4", 0x800100},
    {"at90pwm1", "avr4", "avr4", 0x800100},
    {"at90pwm2", "avr4", "avr4", 0x800100},
    {"at90pwm2b", "avr4", "avr4", 0x800100},
    {"at90pwm3", "avr4", "avr4", 0x800100},
    {"at90pwm3b", "avr4", "avr4", 0x800100},
    {"at90pwm81", "avr4", "avr4", 0x800100},
    {"ata5702m322", "avr5", "avr5", 0x800200},
    {"ata5782", "avr5", "avr5", 0x800200},
    {"ata5790", "avr5", "avr5", 0x800100},
    {"ata5790n", "avr5", "avr5", 0x800100},
    {"ata5791", "avr5", "avr5", 0x800100},
    {"ata5795", "avr5", "avr5", 0x800100},
    {"ata5831", "avr5", "avr5", 0x800200},
    {"ata6613c", "avr5", "avr5", 0x800100},
    {"ata6614q", "avr5", "avr5", 0x800100},
    {"ata8210", "avr5", "avr5", 0x800200},
    {"ata8510", "avr5", "avr5", 0x800200},
    {"atmega16", "avr5", "avr5", 0x800060},
    {"atmega16a", "avr5", "avr5", 0x800060},
    {"atmega161", "avr5", "avr5", 0x800060},
    {"atmega162", "avr5", "avr5", 0x800100},
    {"atmega163", "avr5", "avr5", 0x800060},
    {"atmega164a", "avr5", "avr5", 0x800100},
    {"atmega164p", "avr5", "avr5", 0x800100},
    {"atmega164pa", "avr5", "avr5", 0x800100},
    {"atmega165", "avr5", "avr5", 0x800100},
    {"atmega165a", "avr5", "avr5", 0x800100},
    {"atmega165p", "avr5", "avr5", 0x800100},
    {"atmega165pa", "avr5", "avr5", 0x800100},
    {"atmega168", "avr5", "avr5", 0x800100},
    {"atmega168a", "avr5", "avr5", 0x800100},
    {"atmega168p", "avr5", "avr5", 0x800100},
    {"atmega168pa", "avr5", "avr5", 0x800100},
    {"atmega168pb", "avr5", "avr5", 0x800100},
    {"atmega169", "avr5", "avr5", 0x800100},
    {"atmega169a", "avr5", "avr5", 0x800100},
    {"atmega169p", "avr5", "avr5", 0x800100},
    {"atmega169pa", "avr5", "avr5", 0x800100},
    {"atmega32", "avr5", "avr5", 0x800060},
    {"atmega32a", "avr5", "avr5", 0x800060},
    {"atmega323", "avr5", "avr5", 0x800060},
    {"atmega324a", "avr5", "avr5", 0x800100},
    {"atmega324p", "avr5", "avr5", 0x800100},
    {"atmega324pa", "avr5", "avr5", 0x800100},
    {"atmega324pb", "avr5", "avr5", 0x800100},
    {"atmega325", "avr5", "avr5", 0x800100},
    {"atmega325a", "avr5", "avr5", 0x800100},
    {"atmega325p", "avr5", "avr5", 0x800100},
    {"atmega325pa", "avr5", "avr5", 0x800100},
    {"atmega3250", "avr5", "avr5", 0x800100},
    {"atmega3250a", "avr5", "avr5", 0x800100},
    {"atmega3250p", "avr5", "avr5", 0x800100},
    {"atmega3250pa", "avr5", "avr5", 0x800100},
    {"atmega328", "avr5", "avr5", 0x800100},
    {"atmega328p", "avr5", "avr5", 0x800100},
    {"atmega328pb", "avr5", "avr5", 0x800100},
    {"atmega329", "avr5", "avr5", 0x800100},
    {"atmega329a", "avr5", "avr5", 0x800100},
    {"atmega329p", "avr5", "avr5", 0x800100},
    {"atmega329pa", "avr5", "avr5", 0x800100},
    {"atmega3290", "avr5", "avr5", 0x800100},
    {"atmega3290a", "avr5", "avr5", 0x800100},
    {"atmega3290p", "avr5", "avr5", 0x800100},
    {"atmega3290pa", "avr5", "avr5", 0x800100},
    {"atmega406", "avr5", "avr5", 0x800100},
    {"atmega64", "avr5", "avr5", 0x800100},
    {"atmega64a", "avr5", "avr5", 0x800100},
    {"atmega640", "avr5", "avr5", 0x800200},
    {"atmega644", "avr5", "avr5", 0x800100},
    {"atmega644a", "avr5", "avr5", 0x800100},
    {"atmega644p", "avr5", "avr5", 0x800100},
    {"atmega644pa", "avr5", "avr5", 0x800100},
    {"atmega645", "avr5", "avr5", 0x800100},
    {"atmega645a", "avr5", "avr5", 0x800100},
    {"atmega645p", "avr5", "avr5", 0x800100},
    {"atmega649", "avr5", "avr5", 0x800100},
    {"atmega649a", "avr5", "avr5", 0x800100},
    {"atmega649p", "avr5", "avr5", 0x800100},
    {"atmega6450", "avr5", "avr5", 0x800100},
    {"atmega6450a", "avr5", "avr5", 0x800100},
    {"atmega6450p", "avr5", "avr5", 0x800100},
    {"atmega6490", "avr5", "avr5", 0x800100},
    {"atmega6490a", "avr5", "avr5", 0x800100},
    {"atmega6490p", "avr5", "avr5", 0x800100},
    {"atmega64rfr2", "avr5", "avr5", 0x800200},
    {"atmega644rfr2", "avr5", "avr5", 0x800200},
    {"atmega16hva", "avr5", "avr5", 0x800100},
    {"atmega16hva2", "avr5", "avr5", 0x800100},
    {"atmega16hvb", "avr5", "avr5", 0x800100},
    {"atmega16hvbrevb", "avr5", "avr5", 0x800100},
    {"atmega32hvb", "avr5", "avr5", 0x800100},
    {"atmega32hvbrevb", "avr5", "avr5", 0x800100},
    {"atmega64hve", "avr5", "avr5", 0x800100},
    {"atmega64hve2", "avr5", "avr5", 0x800100},
    {"at90can32", "avr5", "avr5", 0x800100},
    {"at90can64", "avr5", "avr5", 0x800100},
    {"at90pwm161", "avr5", "avr5", 0x800100},
    {"at90pwm216", "avr5", "avr5", 0x800100},
    {"at90pwm316", "avr5", "avr5", 0x800100},
    {"atmega32c1", "avr5", "avr5", 0x800100},
    {"atmega64c1", "avr5", "avr5", 0x800100},
    {"atmega16m1", "avr5", "avr5", 0x800100},
    {"atmega32m1", "avr5", "avr5", 0x800100},
    {"atmega64m1", "avr5", "avr5", 0x800100},
    {"atmega16u4", "avr5", "avr5", 0x800100},
    {"atmega32u4", "avr5", "avr5", 0x800100},
    {"atmega32u6", "avr5", "avr5", 0x800100},
    {"at90usb646", "avr5", "avr5", 0x800100},
    {"at90usb647", "avr5", "avr5", 0x800100},
    {"at90scr100", "avr5", "avr5", 0x800100},
    {"at94k", "avr5", "avr5", 0x800060},
    {"m3000", "avr5", "avr5", 0x800060},
    {"atmega128", "avr51", "avr51", 0x800100},
    {"atmega128a", "avr51", "avr51", 0x800100},
    {"atmega1280", "avr51", "avr51", 0x800200},
    {"atmega1281", "avr51", "avr51", 0x800200},
    {"atmega1284", "avr51", "avr51", 0x800100},
    {"atmega1284p", "avr51", "avr51", 0x800100},
    {"atmega128rfa1", "avr51", "avr51", 0x800200},
    {"atmega128rfr2", "avr51", "avr51", 0x800200},
    {"atmega1284rfr2", "avr51", "avr51", 0x800200},
    {"at90can128", "avr51", "avr51", 0x800200},
    {"at90usb1286", "avr51", "avr51", 0x800200},
    {"at90usb1287", "avr51", "avr51", 0x800200},
    {"atmega2560", "avr6", "avr6", 0x800200},
    {"atmega2561", "avr6", "avr6", 0x800200},
    {"atmega256rfr2", "avr6", "avr6", 0x800200},
    {"atmega2564rfr2", "avr6", "avr6", 0x800200},
    {"attiny4", "avrtiny", "avrtiny", 0x800040},
    {"attiny5", "avrtiny", "avrtiny", 0x800040},
    {"attiny9", "avrtiny", "avrtiny", 0x800040},
    {"attiny10", "avrtiny", "avrtiny", 0x800040},
    {"attiny20", "avrtiny", "avrtiny", 0x800040},
    {"attiny40", "avrtiny", "avrtiny", 0x800040},
    {"attiny102", "avrtiny", "avrtiny", 0x800040},
    {"attiny104", "avrtiny", "avrtiny", 0x800040},
    {"atxmega16a4", "avrxmega2", "avrxmega2", 0x802000},
    {"atxmega16a4u", "avrxmega2", "avrxmega2", 0x802000},
    {"atxmega16c4", "avrxmega2", "avrxmega2", 0x802000},
    {"atxmega16d4", "avrxmega2", "avrxmega2", 0x802000},
    {"atxmega32a4", "avrxmega2", "avrxmega2", 0x802000},
    {"atxmega32a4u", "avrxmega2", "avrxmega2", 0x802000},
    {"atxmega32c3", "avrxmega2", "avrxmega2", 0x802000},
    {"atxmega32c4", "avrxmega2", "avrxmega2", 0x802000},
    {"atxmega32d3", "avrxmega2", "avrxmega2", 0x802000},
    {"atxmega32d4", "avrxmega2", "avrxmega2", 0x802000},
    {"atxmega32e5", "avrxmega2", "avrxmega2", 0x802000},
    {"atxmega16e5", "avrxmega2", "avrxmega2", 0x802000},
    {"atxmega8e5", "avrxmega2", "avrxmega2", 0x802000},
    {"atxmega64a3", "avrxmega4", "avrxmega4", 0x802000},
    {"atxmega64a3u", "avrxmega4", "avrxmega4", 0x802000},
    {"atxmega64a4u", "avrxmega4", "avrxmega4", 0x802000},
    {"atxmega64b1", "avrxmega4", "avrxmega4", 0x802000},
    {"atxmega64b3", "avrxmega4", "avrxmega4", 0x802000},
    {"atxmega64c3", "avrxmega4", "avrxmega4", 0x802000},
    {"atxmega64d3", "avrxmega4", "avrxmega4", 0x802000},
    {"atxmega64d4", "avrxmega4", "avrxmega4", 0x802000},
    {"atxmega64a1", "avrxmega5", "avrxmega5", 0x802000},
    {"atxmega64a1u", "avrxmega5", "avrxmega5", 0x802000},
    {"atxmega128a3", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega128a3u", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega128b1", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega128b3", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega128c3", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega128d3", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega128d4", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega192a3", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega192a3u", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega192c3", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega192d3", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega256a3", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega256a3u", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega256a3b", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega256a3bu", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega256c3", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega256d3", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega384c3", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega384d3", "avrxmega6", "avrxmega6", 0x802000},
    {"atxmega128a1", "avrxmega7", "avrxmega7", 0x802000},
    {"atxmega128a1u", "avrxmega7", "avrxmega7", 0x802000},
    {"atxmega128a4u", "avrxmega7", "avrxmega7", 0x802000},
    {"attiny202", "avrxmega3/short-calls", "avrxmega3", 0x803F80},
    {"attiny204", "avrxmega3/short-calls", "avrxmega3", 0x803F80},
    {"attiny212", "avrxmega3/short-calls", "avrxmega3", 0x803F80},
    {"attiny214", "avrxmega3/short-calls", "avrxmega3", 0x803F80},
    {"attiny402", "avrxmega3/short-calls", "avrxmega3", 0x803F00},
    {"attiny404", "avrxmega3/short-calls", "avrxmega3", 0x803F00},
    {"attiny406", "avrxmega3/short-calls", "avrxmega3", 0x803F00},
    {"attiny412", "avrxmega3/short-calls", "avrxmega3", 0x803F00},
    {"attiny414", "avrxmega3/short-calls", "avrxmega3", 0x803F00},
    {"attiny416", "avrxmega3/short-calls", "avrxmega3", 0x803F00},
    {"attiny417", "avrxmega3/short-calls", "avrxmega3", 0x803F00},
    {"attiny804", "avrxmega3/short-calls", "avrxmega3", 0x803E00},
    {"attiny806", "avrxmega3/short-calls", "avrxmega3", 0x803E00},
    {"attiny807", "avrxmega3/short-calls", "avrxmega3", 0x803E00},
    {"attiny814", "avrxmega3/short-calls", "avrxmega3", 0x803E00},
    {"attiny816", "avrxmega3/short-calls", "avrxmega3", 0x803E00},
    {"attiny817", "avrxmega3/short-calls", "avrxmega3", 0x803E00},
    {"atmega808", "avrxmega3/short-calls", "avrxmega3", 0x803C00},
    {"atmega809", "avrxmega3/short-calls", "avrxmega3", 0x803C00},
    {"atmega1608", "avrxmega3", "avrxmega3", 0x803800},
    {"atmega1609", "avrxmega3", "avrxmega3", 0x803800},
    {"atmega3208", "avrxmega3", "avrxmega3", 0x803000},
    {"atmega3209", "avrxmega3", "avrxmega3", 0x803000},
    {"atmega4808", "avrxmega3", "avrxmega3", 0x802800},
    {"atmega4809", "avrxmega3", "avrxmega3", 0x802800},
    {"attiny1604", "avrxmega3", "avrxmega3", 0x803C00},
    {"attiny1606", "avrxmega3", "avrxmega3", 0x803C00},
    {"attiny1607", "avrxmega3", "avrxmega3", 0x803C00},
    {"attiny1614", "avrxmega3", "avrxmega3", 0x803800},
    {"attiny1616", "avrxmega3", "avrxmega3", 0x803800},
    {"attiny1617", "avrxmega3", "avrxmega3", 0x803800},
    {"attiny1624", "avrxmega3", "avrxmega3", 0x803800},
    {"attiny1626", "avrxmega3", "avrxmega3", 0x803800},
    {"attiny1627", "avrxmega3", "avrxmega3", 0x803800},
    {"attiny3216", "avrxmega3", "avrxmega3", 0x803800},
    {"attiny3217", "avrxmega3", "avrxmega3", 0x803800},
};

std::string GetMCUSubPath(StringRef MCUName) {
  for (const auto &MCU : MCUInfo)
    if (MCU.Name == MCUName)
      return std::string(MCU.SubPath);
  return "";
}

std::optional<StringRef> GetMCUFamilyName(StringRef MCUName) {
  for (const auto &MCU : MCUInfo)
    if (MCU.Name == MCUName)
      return std::optional<StringRef>(MCU.Family);
  return std::nullopt;
}

std::optional<unsigned> GetMCUSectionAddressData(StringRef MCUName) {
  for (const auto &MCU : MCUInfo)
    if (MCU.Name == MCUName && MCU.DataAddr > 0)
      return std::optional<unsigned>(MCU.DataAddr);
  return std::nullopt;
}

const StringRef PossibleAVRLibcLocations[] = {
    "/avr",
    "/usr/avr",
    "/usr/lib/avr",
};

} // end anonymous namespace

/// AVR Toolchain
AVRToolChain::AVRToolChain(const Driver &D, const llvm::Triple &Triple,
                           const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  GCCInstallation.init(Triple, Args);

  if (getCPUName(D, Args, Triple).empty())
    D.Diag(diag::warn_drv_avr_mcu_not_specified);

  // Only add default libraries if the user hasn't explicitly opted out.
  if (!Args.hasArg(options::OPT_nostdlib) &&
      !Args.hasArg(options::OPT_nodefaultlibs) && GCCInstallation.isValid()) {
    GCCInstallPath = GCCInstallation.getInstallPath();
    std::string GCCParentPath(GCCInstallation.getParentLibPath());
    getProgramPaths().push_back(GCCParentPath + "/../bin");
  }
}

void AVRToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                             ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc) ||
      DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  // Omit if there is no avr-libc installed.
  std::optional<std::string> AVRLibcRoot = findAVRLibcInstallation();
  if (!AVRLibcRoot)
    return;

  // Add 'avr-libc/include' to clang system include paths if applicable.
  std::string AVRInc = *AVRLibcRoot + "/include";
  if (llvm::sys::fs::is_directory(AVRInc))
    addSystemInclude(DriverArgs, CC1Args, AVRInc);
}

void AVRToolChain::addClangTargetOptions(
    const llvm::opt::ArgList &DriverArgs, llvm::opt::ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadKind) const {
  // By default, use `.ctors` (not `.init_array`), as required by libgcc, which
  // runs constructors/destructors on AVR.
  if (!DriverArgs.hasFlag(options::OPT_fuse_init_array,
                          options::OPT_fno_use_init_array, false))
    CC1Args.push_back("-fno-use-init-array");
  // Use `-fno-use-cxa-atexit` as default, since avr-libc does not support
  // `__cxa_atexit()`.
  if (!DriverArgs.hasFlag(options::OPT_fuse_cxa_atexit,
                          options::OPT_fno_use_cxa_atexit, false))
    CC1Args.push_back("-fno-use-cxa-atexit");
}

Tool *AVRToolChain::buildLinker() const {
  return new tools::AVR::Linker(getTriple(), *this);
}

std::string
AVRToolChain::getCompilerRT(const llvm::opt::ArgList &Args, StringRef Component,
                            FileType Type = ToolChain::FT_Static) const {
  assert(Type == ToolChain::FT_Static && "AVR only supports static libraries");
  // Since AVR can never be a host environment, its compiler-rt library files
  // should always have ".a" suffix, even on windows.
  SmallString<32> File("/libclang_rt.");
  File += Component.str();
  File += ".a";
  // Return the default compiler-rt path appended with
  // "avr/libclang_rt.$COMPONENT.a".
  SmallString<256> Path(ToolChain::getCompilerRTPath());
  llvm::sys::path::append(Path, "avr");
  llvm::sys::path::append(Path, File.str());
  return std::string(Path);
}

void AVR::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                               const InputInfo &Output,
                               const InputInfoList &Inputs, const ArgList &Args,
                               const char *LinkingOutput) const {
  const auto &TC = static_cast<const AVRToolChain &>(getToolChain());
  const Driver &D = getToolChain().getDriver();

  // Compute information about the target AVR.
  std::string CPU = getCPUName(D, Args, getToolChain().getTriple());
  std::optional<StringRef> FamilyName = GetMCUFamilyName(CPU);
  std::optional<std::string> AVRLibcRoot = TC.findAVRLibcInstallation();
  std::optional<unsigned> SectionAddressData = GetMCUSectionAddressData(CPU);

  // Compute the linker program path, and use GNU "avr-ld" as default.
  const Arg *A = Args.getLastArg(options::OPT_fuse_ld_EQ);
  std::string Linker = A ? getToolChain().GetLinkerPath(nullptr)
                         : getToolChain().GetProgramPath(getShortName());

  ArgStringList CmdArgs;

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  // Enable garbage collection of unused sections.
  if (!Args.hasArg(options::OPT_r))
    CmdArgs.push_back("--gc-sections");

  // Add library search paths before we specify libraries.
  Args.AddAllArgs(CmdArgs, options::OPT_L);
  getToolChain().AddFilePathLibArgs(Args, CmdArgs);

  // Currently we only support libgcc and compiler-rt.
  auto RtLib = TC.GetRuntimeLibType(Args);
  assert(
      (RtLib == ToolChain::RLT_Libgcc || RtLib == ToolChain::RLT_CompilerRT) &&
      "unknown runtime library");

  // Only add default libraries if the user hasn't explicitly opted out.
  bool LinkStdlib = false;
  if (!Args.hasArg(options::OPT_nostdlib) && !Args.hasArg(options::OPT_r) &&
      !Args.hasArg(options::OPT_nodefaultlibs)) {
    if (!CPU.empty()) {
      if (!FamilyName) {
        // We do not have an entry for this CPU in the family
        // mapping table yet.
        D.Diag(diag::warn_drv_avr_family_linking_stdlibs_not_implemented)
            << CPU;
      } else if (!AVRLibcRoot) {
        // No avr-libc found and so no runtime linked.
        D.Diag(diag::warn_drv_avr_libc_not_found);
      } else {
        std::string SubPath = GetMCUSubPath(CPU);
        // Add path of avr-libc.
        CmdArgs.push_back(
            Args.MakeArgString(Twine("-L") + *AVRLibcRoot + "/lib/" + SubPath));
        if (RtLib == ToolChain::RLT_Libgcc)
          CmdArgs.push_back(Args.MakeArgString("-L" + TC.getGCCInstallPath() +
                                               "/" + SubPath));
        LinkStdlib = true;
      }
    }
    if (!LinkStdlib)
      D.Diag(diag::warn_drv_avr_stdlib_not_linked);
  }

  if (!Args.hasArg(options::OPT_r)) {
    if (SectionAddressData) {
      CmdArgs.push_back(
          Args.MakeArgString("--defsym=__DATA_REGION_ORIGIN__=0x" +
                             Twine::utohexstr(*SectionAddressData)));
    } else {
      // We do not have an entry for this CPU in the address mapping table
      // yet.
      D.Diag(diag::warn_drv_avr_linker_section_addresses_not_implemented)
          << CPU;
    }
  }

  if (D.isUsingLTO()) {
    assert(!Inputs.empty() && "Must have at least one input.");
    // Find the first filename InputInfo object.
    auto Input = llvm::find_if(
        Inputs, [](const InputInfo &II) -> bool { return II.isFilename(); });
    if (Input == Inputs.end())
      // For a very rare case, all of the inputs to the linker are
      // InputArg. If that happens, just use the first InputInfo.
      Input = Inputs.begin();

    addLTOOptions(TC, Args, CmdArgs, Output, *Input,
                  D.getLTOMode() == LTOK_Thin);
  }

  // If the family name is known, we can link with the device-specific libgcc.
  // Without it, libgcc will simply not be linked. This matches avr-gcc
  // behavior.
  if (LinkStdlib) {
    assert(!CPU.empty() && "CPU name must be known in order to link stdlibs");

    CmdArgs.push_back("--start-group");

    // Add the object file for the CRT.
    std::string CrtFileName = std::string("-l:crt") + CPU + std::string(".o");
    CmdArgs.push_back(Args.MakeArgString(CrtFileName));

    // Link to libgcc.
    if (RtLib == ToolChain::RLT_Libgcc)
      CmdArgs.push_back("-lgcc");

    // Link to generic libraries of avr-libc.
    CmdArgs.push_back("-lm");
    CmdArgs.push_back("-lc");

    // Add the link library specific to the MCU.
    CmdArgs.push_back(Args.MakeArgString(std::string("-l") + CPU));

    // Add the relocatable inputs.
    AddLinkerInputs(getToolChain(), Inputs, Args, CmdArgs, JA);

    // We directly use libclang_rt.builtins.a as input file, instead of using
    // '-lclang_rt.builtins'.
    if (RtLib == ToolChain::RLT_CompilerRT) {
      std::string RtLib =
          getToolChain().getCompilerRT(Args, "builtins", ToolChain::FT_Static);
      if (llvm::sys::fs::exists(RtLib))
        CmdArgs.push_back(Args.MakeArgString(RtLib));
    }

    CmdArgs.push_back("--end-group");

    // Add avr-libc's linker script to lld by default, if it exists.
    if (!Args.hasArg(options::OPT_T) &&
        Linker.find("avr-ld") == std::string::npos) {
      std::string Path(*AVRLibcRoot + "/lib/ldscripts/");
      Path += *FamilyName;
      Path += ".x";
      if (llvm::sys::fs::exists(Path))
        CmdArgs.push_back(Args.MakeArgString("-T" + Path));
    }
    // Otherwise add user specified linker script to either avr-ld or lld.
    else
      Args.AddAllArgs(CmdArgs, options::OPT_T);

    if (Args.hasFlag(options::OPT_mrelax, options::OPT_mno_relax, true))
      CmdArgs.push_back("--relax");
  } else {
    AddLinkerInputs(getToolChain(), Inputs, Args, CmdArgs, JA);
  }

  // Specify the family name as the emulation mode to use.
  // This is almost always required because otherwise avr-ld
  // will assume 'avr2' and warn about the program being larger
  // than the bare minimum supports.
  if (Linker.find("avr-ld") != std::string::npos && FamilyName)
    CmdArgs.push_back(Args.MakeArgString(std::string("-m") + *FamilyName));

  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileCurCP(), Args.MakeArgString(Linker),
      CmdArgs, Inputs, Output));
}

std::optional<std::string> AVRToolChain::findAVRLibcInstallation() const {
  // Search avr-libc installation according to avr-gcc installation.
  std::string GCCParent(GCCInstallation.getParentLibPath());
  std::string Path(GCCParent + "/avr");
  if (llvm::sys::fs::is_directory(Path))
    return Path;
  Path = GCCParent + "/../avr";
  if (llvm::sys::fs::is_directory(Path))
    return Path;

  // Search avr-libc installation from possible locations, and return the first
  // one that exists, if there is no avr-gcc installed.
  for (StringRef PossiblePath : PossibleAVRLibcLocations) {
    std::string Path = getDriver().SysRoot + PossiblePath.str();
    if (llvm::sys::fs::is_directory(Path))
      return Path;
  }

  return std::nullopt;
}
