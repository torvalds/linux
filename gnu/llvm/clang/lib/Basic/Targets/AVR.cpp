//===--- AVR.cpp - Implement AVR target feature support -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements AVR TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "AVR.h"
#include "clang/Basic/MacroBuilder.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

namespace clang {
namespace targets {

/// Information about a specific microcontroller.
struct LLVM_LIBRARY_VISIBILITY MCUInfo {
  const char *Name;
  const char *DefineName;
  StringRef Arch; // The __AVR_ARCH__ value.
  const int NumFlashBanks; // Set to 0 for the devices do not support LPM/ELPM.
};

// NOTE: This list has been synchronized with gcc-avr 5.4.0 and avr-libc 2.0.0.
static MCUInfo AVRMcus[] = {
    {"avr1", NULL, "1", 0},
    {"at90s1200", "__AVR_AT90S1200__", "1", 0},
    {"attiny11", "__AVR_ATtiny11__", "1", 0},
    {"attiny12", "__AVR_ATtiny12__", "1", 0},
    {"attiny15", "__AVR_ATtiny15__", "1", 0},
    {"attiny28", "__AVR_ATtiny28__", "1", 0},
    {"avr2", NULL, "2", 1},
    {"at90s2313", "__AVR_AT90S2313__", "2", 1},
    {"at90s2323", "__AVR_AT90S2323__", "2", 1},
    {"at90s2333", "__AVR_AT90S2333__", "2", 1},
    {"at90s2343", "__AVR_AT90S2343__", "2", 1},
    {"attiny22", "__AVR_ATtiny22__", "2", 1},
    {"attiny26", "__AVR_ATtiny26__", "2", 1},
    {"at86rf401", "__AVR_AT86RF401__", "25", 1},
    {"at90s4414", "__AVR_AT90S4414__", "2", 1},
    {"at90s4433", "__AVR_AT90S4433__", "2", 1},
    {"at90s4434", "__AVR_AT90S4434__", "2", 1},
    {"at90s8515", "__AVR_AT90S8515__", "2", 1},
    {"at90c8534", "__AVR_AT90c8534__", "2", 1},
    {"at90s8535", "__AVR_AT90S8535__", "2", 1},
    {"avr25", NULL, "25", 1},
    {"ata5272", "__AVR_ATA5272__", "25", 1},
    {"ata6616c", "__AVR_ATA6616c__", "25", 1},
    {"attiny13", "__AVR_ATtiny13__", "25", 1},
    {"attiny13a", "__AVR_ATtiny13A__", "25", 1},
    {"attiny2313", "__AVR_ATtiny2313__", "25", 1},
    {"attiny2313a", "__AVR_ATtiny2313A__", "25", 1},
    {"attiny24", "__AVR_ATtiny24__", "25", 1},
    {"attiny24a", "__AVR_ATtiny24A__", "25", 1},
    {"attiny4313", "__AVR_ATtiny4313__", "25", 1},
    {"attiny44", "__AVR_ATtiny44__", "25", 1},
    {"attiny44a", "__AVR_ATtiny44A__", "25", 1},
    {"attiny84", "__AVR_ATtiny84__", "25", 1},
    {"attiny84a", "__AVR_ATtiny84A__", "25", 1},
    {"attiny25", "__AVR_ATtiny25__", "25", 1},
    {"attiny45", "__AVR_ATtiny45__", "25", 1},
    {"attiny85", "__AVR_ATtiny85__", "25", 1},
    {"attiny261", "__AVR_ATtiny261__", "25", 1},
    {"attiny261a", "__AVR_ATtiny261A__", "25", 1},
    {"attiny441", "__AVR_ATtiny441__", "25", 1},
    {"attiny461", "__AVR_ATtiny461__", "25", 1},
    {"attiny461a", "__AVR_ATtiny461A__", "25", 1},
    {"attiny841", "__AVR_ATtiny841__", "25", 1},
    {"attiny861", "__AVR_ATtiny861__", "25", 1},
    {"attiny861a", "__AVR_ATtiny861A__", "25", 1},
    {"attiny87", "__AVR_ATtiny87__", "25", 1},
    {"attiny43u", "__AVR_ATtiny43U__", "25", 1},
    {"attiny48", "__AVR_ATtiny48__", "25", 1},
    {"attiny88", "__AVR_ATtiny88__", "25", 1},
    {"attiny828", "__AVR_ATtiny828__", "25", 1},
    {"avr3", NULL, "3", 1},
    {"at43usb355", "__AVR_AT43USB355__", "3", 1},
    {"at76c711", "__AVR_AT76C711__", "3", 1},
    {"avr31", NULL, "31", 1},
    {"atmega103", "__AVR_ATmega103__", "31", 1},
    {"at43usb320", "__AVR_AT43USB320__", "31", 1},
    {"avr35", NULL, "35", 1},
    {"attiny167", "__AVR_ATtiny167__", "35", 1},
    {"at90usb82", "__AVR_AT90USB82__", "35", 1},
    {"at90usb162", "__AVR_AT90USB162__", "35", 1},
    {"ata5505", "__AVR_ATA5505__", "35", 1},
    {"ata6617c", "__AVR_ATA6617C__", "35", 1},
    {"ata664251", "__AVR_ATA664251__", "35", 1},
    {"atmega8u2", "__AVR_ATmega8U2__", "35", 1},
    {"atmega16u2", "__AVR_ATmega16U2__", "35", 1},
    {"atmega32u2", "__AVR_ATmega32U2__", "35", 1},
    {"attiny1634", "__AVR_ATtiny1634__", "35", 1},
    {"avr4", NULL, "4", 1},
    {"atmega8", "__AVR_ATmega8__", "4", 1},
    {"ata6289", "__AVR_ATA6289__", "4", 1},
    {"atmega8a", "__AVR_ATmega8A__", "4", 1},
    {"ata6285", "__AVR_ATA6285__", "4", 1},
    {"ata6286", "__AVR_ATA6286__", "4", 1},
    {"ata6612c", "__AVR_ATA6612C__", "4", 1},
    {"atmega48", "__AVR_ATmega48__", "4", 1},
    {"atmega48a", "__AVR_ATmega48A__", "4", 1},
    {"atmega48pa", "__AVR_ATmega48PA__", "4", 1},
    {"atmega48pb", "__AVR_ATmega48PB__", "4", 1},
    {"atmega48p", "__AVR_ATmega48P__", "4", 1},
    {"atmega88", "__AVR_ATmega88__", "4", 1},
    {"atmega88a", "__AVR_ATmega88A__", "4", 1},
    {"atmega88p", "__AVR_ATmega88P__", "4", 1},
    {"atmega88pa", "__AVR_ATmega88PA__", "4", 1},
    {"atmega88pb", "__AVR_ATmega88PB__", "4", 1},
    {"atmega8515", "__AVR_ATmega8515__", "4", 1},
    {"atmega8535", "__AVR_ATmega8535__", "4", 1},
    {"atmega8hva", "__AVR_ATmega8HVA__", "4", 1},
    {"at90pwm1", "__AVR_AT90PWM1__", "4", 1},
    {"at90pwm2", "__AVR_AT90PWM2__", "4", 1},
    {"at90pwm2b", "__AVR_AT90PWM2B__", "4", 1},
    {"at90pwm3", "__AVR_AT90PWM3__", "4", 1},
    {"at90pwm3b", "__AVR_AT90PWM3B__", "4", 1},
    {"at90pwm81", "__AVR_AT90PWM81__", "4", 1},
    {"avr5", NULL, "5", 1},
    {"ata5702m322", "__AVR_ATA5702M322__", "5", 1},
    {"ata5782", "__AVR_ATA5782__", "5", 1},
    {"ata5790", "__AVR_ATA5790__", "5", 1},
    {"ata5790n", "__AVR_ATA5790N__", "5", 1},
    {"ata5791", "__AVR_ATA5791__", "5", 1},
    {"ata5795", "__AVR_ATA5795__", "5", 1},
    {"ata5831", "__AVR_ATA5831__", "5", 1},
    {"ata6613c", "__AVR_ATA6613C__", "5", 1},
    {"ata6614q", "__AVR_ATA6614Q__", "5", 1},
    {"ata8210", "__AVR_ATA8210__", "5", 1},
    {"ata8510", "__AVR_ATA8510__", "5", 1},
    {"atmega16", "__AVR_ATmega16__", "5", 1},
    {"atmega16a", "__AVR_ATmega16A__", "5", 1},
    {"atmega161", "__AVR_ATmega161__", "5", 1},
    {"atmega162", "__AVR_ATmega162__", "5", 1},
    {"atmega163", "__AVR_ATmega163__", "5", 1},
    {"atmega164a", "__AVR_ATmega164A__", "5", 1},
    {"atmega164p", "__AVR_ATmega164P__", "5", 1},
    {"atmega164pa", "__AVR_ATmega164PA__", "5", 1},
    {"atmega165", "__AVR_ATmega165__", "5", 1},
    {"atmega165a", "__AVR_ATmega165A__", "5", 1},
    {"atmega165p", "__AVR_ATmega165P__", "5", 1},
    {"atmega165pa", "__AVR_ATmega165PA__", "5", 1},
    {"atmega168", "__AVR_ATmega168__", "5", 1},
    {"atmega168a", "__AVR_ATmega168A__", "5", 1},
    {"atmega168p", "__AVR_ATmega168P__", "5", 1},
    {"atmega168pa", "__AVR_ATmega168PA__", "5", 1},
    {"atmega168pb", "__AVR_ATmega168PB__", "5", 1},
    {"atmega169", "__AVR_ATmega169__", "5", 1},
    {"atmega169a", "__AVR_ATmega169A__", "5", 1},
    {"atmega169p", "__AVR_ATmega169P__", "5", 1},
    {"atmega169pa", "__AVR_ATmega169PA__", "5", 1},
    {"atmega32", "__AVR_ATmega32__", "5", 1},
    {"atmega32a", "__AVR_ATmega32A__", "5", 1},
    {"atmega323", "__AVR_ATmega323__", "5", 1},
    {"atmega324a", "__AVR_ATmega324A__", "5", 1},
    {"atmega324p", "__AVR_ATmega324P__", "5", 1},
    {"atmega324pa", "__AVR_ATmega324PA__", "5", 1},
    {"atmega324pb", "__AVR_ATmega324PB__", "5", 1},
    {"atmega325", "__AVR_ATmega325__", "5", 1},
    {"atmega325a", "__AVR_ATmega325A__", "5", 1},
    {"atmega325p", "__AVR_ATmega325P__", "5", 1},
    {"atmega325pa", "__AVR_ATmega325PA__", "5", 1},
    {"atmega3250", "__AVR_ATmega3250__", "5", 1},
    {"atmega3250a", "__AVR_ATmega3250A__", "5", 1},
    {"atmega3250p", "__AVR_ATmega3250P__", "5", 1},
    {"atmega3250pa", "__AVR_ATmega3250PA__", "5", 1},
    {"atmega328", "__AVR_ATmega328__", "5", 1},
    {"atmega328p", "__AVR_ATmega328P__", "5", 1},
    {"atmega328pb", "__AVR_ATmega328PB__", "5", 1},
    {"atmega329", "__AVR_ATmega329__", "5", 1},
    {"atmega329a", "__AVR_ATmega329A__", "5", 1},
    {"atmega329p", "__AVR_ATmega329P__", "5", 1},
    {"atmega329pa", "__AVR_ATmega329PA__", "5", 1},
    {"atmega3290", "__AVR_ATmega3290__", "5", 1},
    {"atmega3290a", "__AVR_ATmega3290A__", "5", 1},
    {"atmega3290p", "__AVR_ATmega3290P__", "5", 1},
    {"atmega3290pa", "__AVR_ATmega3290PA__", "5", 1},
    {"atmega406", "__AVR_ATmega406__", "5", 1},
    {"atmega64", "__AVR_ATmega64__", "5", 1},
    {"atmega64a", "__AVR_ATmega64A__", "5", 1},
    {"atmega640", "__AVR_ATmega640__", "5", 1},
    {"atmega644", "__AVR_ATmega644__", "5", 1},
    {"atmega644a", "__AVR_ATmega644A__", "5", 1},
    {"atmega644p", "__AVR_ATmega644P__", "5", 1},
    {"atmega644pa", "__AVR_ATmega644PA__", "5", 1},
    {"atmega645", "__AVR_ATmega645__", "5", 1},
    {"atmega645a", "__AVR_ATmega645A__", "5", 1},
    {"atmega645p", "__AVR_ATmega645P__", "5", 1},
    {"atmega649", "__AVR_ATmega649__", "5", 1},
    {"atmega649a", "__AVR_ATmega649A__", "5", 1},
    {"atmega649p", "__AVR_ATmega649P__", "5", 1},
    {"atmega6450", "__AVR_ATmega6450__", "5", 1},
    {"atmega6450a", "__AVR_ATmega6450A__", "5", 1},
    {"atmega6450p", "__AVR_ATmega6450P__", "5", 1},
    {"atmega6490", "__AVR_ATmega6490__", "5", 1},
    {"atmega6490a", "__AVR_ATmega6490A__", "5", 1},
    {"atmega6490p", "__AVR_ATmega6490P__", "5", 1},
    {"atmega64rfr2", "__AVR_ATmega64RFR2__", "5", 1},
    {"atmega644rfr2", "__AVR_ATmega644RFR2__", "5", 1},
    {"atmega16hva", "__AVR_ATmega16HVA__", "5", 1},
    {"atmega16hva2", "__AVR_ATmega16HVA2__", "5", 1},
    {"atmega16hvb", "__AVR_ATmega16HVB__", "5", 1},
    {"atmega16hvbrevb", "__AVR_ATmega16HVBREVB__", "5", 1},
    {"atmega32hvb", "__AVR_ATmega32HVB__", "5", 1},
    {"atmega32hvbrevb", "__AVR_ATmega32HVBREVB__", "5", 1},
    {"atmega64hve", "__AVR_ATmega64HVE__", "5", 1},
    {"atmega64hve2", "__AVR_ATmega64HVE2__", "5", 1},
    {"at90can32", "__AVR_AT90CAN32__", "5", 1},
    {"at90can64", "__AVR_AT90CAN64__", "5", 1},
    {"at90pwm161", "__AVR_AT90PWM161__", "5", 1},
    {"at90pwm216", "__AVR_AT90PWM216__", "5", 1},
    {"at90pwm316", "__AVR_AT90PWM316__", "5", 1},
    {"atmega32c1", "__AVR_ATmega32C1__", "5", 1},
    {"atmega64c1", "__AVR_ATmega64C1__", "5", 1},
    {"atmega16m1", "__AVR_ATmega16M1__", "5", 1},
    {"atmega32m1", "__AVR_ATmega32M1__", "5", 1},
    {"atmega64m1", "__AVR_ATmega64M1__", "5", 1},
    {"atmega16u4", "__AVR_ATmega16U4__", "5", 1},
    {"atmega32u4", "__AVR_ATmega32U4__", "5", 1},
    {"atmega32u6", "__AVR_ATmega32U6__", "5", 1},
    {"at90usb646", "__AVR_AT90USB646__", "5", 1},
    {"at90usb647", "__AVR_AT90USB647__", "5", 1},
    {"at90scr100", "__AVR_AT90SCR100__", "5", 1},
    {"at94k", "__AVR_AT94K__", "5", 1},
    {"m3000", "__AVR_AT000__", "5", 1},
    {"avr51", NULL, "51", 2},
    {"atmega128", "__AVR_ATmega128__", "51", 2},
    {"atmega128a", "__AVR_ATmega128A__", "51", 2},
    {"atmega1280", "__AVR_ATmega1280__", "51", 2},
    {"atmega1281", "__AVR_ATmega1281__", "51", 2},
    {"atmega1284", "__AVR_ATmega1284__", "51", 2},
    {"atmega1284p", "__AVR_ATmega1284P__", "51", 2},
    {"atmega128rfa1", "__AVR_ATmega128RFA1__", "51", 2},
    {"atmega128rfr2", "__AVR_ATmega128RFR2__", "51", 2},
    {"atmega1284rfr2", "__AVR_ATmega1284RFR2__", "51", 2},
    {"at90can128", "__AVR_AT90CAN128__", "51", 2},
    {"at90usb1286", "__AVR_AT90USB1286__", "51", 2},
    {"at90usb1287", "__AVR_AT90USB1287__", "51", 2},
    {"avr6", NULL, "6", 4},
    {"atmega2560", "__AVR_ATmega2560__", "6", 4},
    {"atmega2561", "__AVR_ATmega2561__", "6", 4},
    {"atmega256rfr2", "__AVR_ATmega256RFR2__", "6", 4},
    {"atmega2564rfr2", "__AVR_ATmega2564RFR2__", "6", 4},
    {"avrxmega2", NULL, "102", 1},
    {"atxmega16a4", "__AVR_ATxmega16A4__", "102", 1},
    {"atxmega16a4u", "__AVR_ATxmega16A4U__", "102", 1},
    {"atxmega16c4", "__AVR_ATxmega16C4__", "102", 1},
    {"atxmega16d4", "__AVR_ATxmega16D4__", "102", 1},
    {"atxmega32a4", "__AVR_ATxmega32A4__", "102", 1},
    {"atxmega32a4u", "__AVR_ATxmega32A4U__", "102", 1},
    {"atxmega32c3", "__AVR_ATxmega32C3__", "102", 1},
    {"atxmega32c4", "__AVR_ATxmega32C4__", "102", 1},
    {"atxmega32d3", "__AVR_ATxmega32D3__", "102", 1},
    {"atxmega32d4", "__AVR_ATxmega32D4__", "102", 1},
    {"atxmega32e5", "__AVR_ATxmega32E5__", "102", 1},
    {"atxmega16e5", "__AVR_ATxmega16E5__", "102", 1},
    {"atxmega8e5", "__AVR_ATxmega8E5__", "102", 1},
    {"avrxmega4", NULL, "104", 1},
    {"atxmega64a3", "__AVR_ATxmega64A3__", "104", 1},
    {"atxmega64a3u", "__AVR_ATxmega64A3U__", "104", 1},
    {"atxmega64a4u", "__AVR_ATxmega64A4U__", "104", 1},
    {"atxmega64b1", "__AVR_ATxmega64B1__", "104", 1},
    {"atxmega64b3", "__AVR_ATxmega64B3__", "104", 1},
    {"atxmega64c3", "__AVR_ATxmega64C3__", "104", 1},
    {"atxmega64d3", "__AVR_ATxmega64D3__", "104", 1},
    {"atxmega64d4", "__AVR_ATxmega64D4__", "104", 1},
    {"avrxmega5", NULL, "105", 1},
    {"atxmega64a1", "__AVR_ATxmega64A1__", "105", 1},
    {"atxmega64a1u", "__AVR_ATxmega64A1U__", "105", 1},
    {"avrxmega6", NULL, "106", 6},
    {"atxmega128a3", "__AVR_ATxmega128A3__", "106", 2},
    {"atxmega128a3u", "__AVR_ATxmega128A3U__", "106", 2},
    {"atxmega128b1", "__AVR_ATxmega128B1__", "106", 2},
    {"atxmega128b3", "__AVR_ATxmega128B3__", "106", 2},
    {"atxmega128c3", "__AVR_ATxmega128C3__", "106", 2},
    {"atxmega128d3", "__AVR_ATxmega128D3__", "106", 2},
    {"atxmega128d4", "__AVR_ATxmega128D4__", "106", 2},
    {"atxmega192a3", "__AVR_ATxmega192A3__", "106", 3},
    {"atxmega192a3u", "__AVR_ATxmega192A3U__", "106", 3},
    {"atxmega192c3", "__AVR_ATxmega192C3__", "106", 3},
    {"atxmega192d3", "__AVR_ATxmega192D3__", "106", 3},
    {"atxmega256a3", "__AVR_ATxmega256A3__", "106", 4},
    {"atxmega256a3u", "__AVR_ATxmega256A3U__", "106", 4},
    {"atxmega256a3b", "__AVR_ATxmega256A3B__", "106", 4},
    {"atxmega256a3bu", "__AVR_ATxmega256A3BU__", "106", 4},
    {"atxmega256c3", "__AVR_ATxmega256C3__", "106", 4},
    {"atxmega256d3", "__AVR_ATxmega256D3__", "106", 4},
    {"atxmega384c3", "__AVR_ATxmega384C3__", "106", 6},
    {"atxmega384d3", "__AVR_ATxmega384D3__", "106", 6},
    {"avrxmega7", NULL, "107", 2},
    {"atxmega128a1", "__AVR_ATxmega128A1__", "107", 2},
    {"atxmega128a1u", "__AVR_ATxmega128A1U__", "107", 2},
    {"atxmega128a4u", "__AVR_ATxmega128A4U__", "107", 2},
    {"avrtiny", NULL, "100", 0},
    {"attiny4", "__AVR_ATtiny4__", "100", 0},
    {"attiny5", "__AVR_ATtiny5__", "100", 0},
    {"attiny9", "__AVR_ATtiny9__", "100", 0},
    {"attiny10", "__AVR_ATtiny10__", "100", 0},
    {"attiny20", "__AVR_ATtiny20__", "100", 0},
    {"attiny40", "__AVR_ATtiny40__", "100", 0},
    {"attiny102", "__AVR_ATtiny102__", "100", 0},
    {"attiny104", "__AVR_ATtiny104__", "100", 0},
    {"avrxmega3", NULL, "103", 1},
    {"attiny202", "__AVR_ATtiny202__", "103", 1},
    {"attiny402", "__AVR_ATtiny402__", "103", 1},
    {"attiny204", "__AVR_ATtiny204__", "103", 1},
    {"attiny404", "__AVR_ATtiny404__", "103", 1},
    {"attiny804", "__AVR_ATtiny804__", "103", 1},
    {"attiny1604", "__AVR_ATtiny1604__", "103", 1},
    {"attiny406", "__AVR_ATtiny406__", "103", 1},
    {"attiny806", "__AVR_ATtiny806__", "103", 1},
    {"attiny1606", "__AVR_ATtiny1606__", "103", 1},
    {"attiny807", "__AVR_ATtiny807__", "103", 1},
    {"attiny1607", "__AVR_ATtiny1607__", "103", 1},
    {"attiny212", "__AVR_ATtiny212__", "103", 1},
    {"attiny412", "__AVR_ATtiny412__", "103", 1},
    {"attiny214", "__AVR_ATtiny214__", "103", 1},
    {"attiny414", "__AVR_ATtiny414__", "103", 1},
    {"attiny814", "__AVR_ATtiny814__", "103", 1},
    {"attiny1614", "__AVR_ATtiny1614__", "103", 1},
    {"attiny416", "__AVR_ATtiny416__", "103", 1},
    {"attiny816", "__AVR_ATtiny816__", "103", 1},
    {"attiny1616", "__AVR_ATtiny1616__", "103", 1},
    {"attiny3216", "__AVR_ATtiny3216__", "103", 1},
    {"attiny417", "__AVR_ATtiny417__", "103", 1},
    {"attiny817", "__AVR_ATtiny817__", "103", 1},
    {"attiny1617", "__AVR_ATtiny1617__", "103", 1},
    {"attiny3217", "__AVR_ATtiny3217__", "103", 1},
    {"attiny1624", "__AVR_ATtiny1624__", "103", 1},
    {"attiny1626", "__AVR_ATtiny1626__", "103", 1},
    {"attiny1627", "__AVR_ATtiny1627__", "103", 1},
    {"atmega808", "__AVR_ATmega808__", "103", 1},
    {"atmega809", "__AVR_ATmega809__", "103", 1},
    {"atmega1608", "__AVR_ATmega1608__", "103", 1},
    {"atmega1609", "__AVR_ATmega1609__", "103", 1},
    {"atmega3208", "__AVR_ATmega3208__", "103", 1},
    {"atmega3209", "__AVR_ATmega3209__", "103", 1},
    {"atmega4808", "__AVR_ATmega4808__", "103", 1},
    {"atmega4809", "__AVR_ATmega4809__", "103", 1},
};

} // namespace targets
} // namespace clang

static bool ArchHasELPM(StringRef Arch) {
  return llvm::StringSwitch<bool>(Arch)
    .Cases("31", "51", "6", true)
    .Cases("102", "104", "105", "106", "107", true)
    .Default(false);
}

static bool ArchHasELPMX(StringRef Arch) {
  return llvm::StringSwitch<bool>(Arch)
    .Cases("51", "6", true)
    .Cases("102", "104", "105", "106", "107", true)
    .Default(false);
}

static bool ArchHasMOVW(StringRef Arch) {
  return llvm::StringSwitch<bool>(Arch)
    .Cases("25", "35", "4", "5", "51", "6", true)
    .Cases("102", "103", "104", "105", "106", "107", true)
    .Default(false);
}

static bool ArchHasLPMX(StringRef Arch) {
  return ArchHasMOVW(Arch); // same architectures
}

static bool ArchHasMUL(StringRef Arch) {
  return llvm::StringSwitch<bool>(Arch)
    .Cases("4", "5", "51", "6", true)
    .Cases("102", "103", "104", "105", "106", "107", true)
    .Default(false);
}

static bool ArchHasJMPCALL(StringRef Arch) {
  return llvm::StringSwitch<bool>(Arch)
    .Cases("3", "31", "35", "5", "51", "6", true)
    .Cases("102", "103", "104", "105", "106", "107", true)
    .Default(false);
}

static bool ArchHas3BytePC(StringRef Arch) {
  // These devices have more than 128kB of program memory.
  // Note:
  //   - Not fully correct for arch 106: only about half the chips have more
  //     than 128kB program memory and therefore a 3 byte PC.
  //   - Doesn't match GCC entirely: avr-gcc thinks arch 107 goes beyond 128kB
  //     but in fact it doesn't.
  return llvm::StringSwitch<bool>(Arch)
    .Case("6", true)
    .Case("106", true)
    .Default(false);
}

bool AVRTargetInfo::isValidCPUName(StringRef Name) const {
  return llvm::any_of(
      AVRMcus, [&](const MCUInfo &Info) { return Info.Name == Name; });
}

void AVRTargetInfo::fillValidCPUList(SmallVectorImpl<StringRef> &Values) const {
  for (const MCUInfo &Info : AVRMcus)
    Values.push_back(Info.Name);
}

bool AVRTargetInfo::setCPU(const std::string &Name) {
  // Set the ABI field based on the device or family name.
  auto It = llvm::find_if(
      AVRMcus, [&](const MCUInfo &Info) { return Info.Name == Name; });
  if (It != std::end(AVRMcus)) {
    CPU = Name;
    ABI = (It->Arch == "100") ? "avrtiny" : "avr";
    DefineName = It->DefineName;
    Arch = It->Arch;
    NumFlashBanks = It->NumFlashBanks;
    return true;
  }

  // Parameter Name is neither valid family name nor valid device name.
  return false;
}

std::optional<std::string>
AVRTargetInfo::handleAsmEscapedChar(char EscChar) const {
  switch (EscChar) {
  // "%~" represents for 'r' depends on the device has long jump/call.
  case '~':
    return ArchHasJMPCALL(Arch) ? std::string("") : std::string(1, 'r');

  // "%!" represents for 'e' depends on the PC register size.
  case '!':
    return ArchHas3BytePC(Arch) ? std::string(1, 'e') : std::string("");

  // This is an invalid escape character for AVR.
  default:
    return std::nullopt;
  }
}

void AVRTargetInfo::getTargetDefines(const LangOptions &Opts,
                                     MacroBuilder &Builder) const {
  Builder.defineMacro("AVR");
  Builder.defineMacro("__AVR");
  Builder.defineMacro("__AVR__");

  if (ABI == "avrtiny")
    Builder.defineMacro("__AVR_TINY__", "1");

  if (DefineName.size() != 0)
      Builder.defineMacro(DefineName);

  Builder.defineMacro("__AVR_ARCH__", Arch);

  // TODO: perhaps we should use the information from AVRDevices.td instead?
  if (ArchHasELPM(Arch))
    Builder.defineMacro("__AVR_HAVE_ELPM__");
  if (ArchHasELPMX(Arch))
    Builder.defineMacro("__AVR_HAVE_ELPMX__");
  if (ArchHasMOVW(Arch))
    Builder.defineMacro("__AVR_HAVE_MOVW__");
  if (ArchHasLPMX(Arch))
    Builder.defineMacro("__AVR_HAVE_LPMX__");
  if (ArchHasMUL(Arch))
    Builder.defineMacro("__AVR_HAVE_MUL__");
  if (ArchHasJMPCALL(Arch))
    Builder.defineMacro("__AVR_HAVE_JMP_CALL__");
  if (ArchHas3BytePC(Arch)) {
    // Note: some devices do support eijmp/eicall even though this macro isn't
    // set. This is the case if they have less than 128kB flash and so
    // eijmp/eicall isn't very useful anyway. (This matches gcc, although it's
    // debatable whether we should be bug-compatible in this case).
    Builder.defineMacro("__AVR_HAVE_EIJMP_EICALL__");
    Builder.defineMacro("__AVR_3_BYTE_PC__");
  } else {
    Builder.defineMacro("__AVR_2_BYTE_PC__");
  }

  if (NumFlashBanks >= 1)
    Builder.defineMacro("__flash", "__attribute__((__address_space__(1)))");
  if (NumFlashBanks >= 2)
    Builder.defineMacro("__flash1", "__attribute__((__address_space__(2)))");
  if (NumFlashBanks >= 3)
    Builder.defineMacro("__flash2", "__attribute__((__address_space__(3)))");
  if (NumFlashBanks >= 4)
    Builder.defineMacro("__flash3", "__attribute__((__address_space__(4)))");
  if (NumFlashBanks >= 5)
    Builder.defineMacro("__flash4", "__attribute__((__address_space__(5)))");
  if (NumFlashBanks >= 6)
    Builder.defineMacro("__flash5", "__attribute__((__address_space__(6)))");
}
