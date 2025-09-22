//===---------------------------------------------------------------------===//
// AVR Device Definitions
//===---------------------------------------------------------------------===//

// :TODO: Implement the skip errata, see `gcc/config/avr/avr-arch.h` for details
// :TODO: We define all devices with SRAM to have all variants of LD/ST/LDD/STD.
//        In reality, avr1 (no SRAM) has one variant each of `LD` and `ST`.
//        avr2 (with SRAM) adds the rest of the variants.

// A feature set aggregates features, grouping them. We don't want to create a
// new member in AVRSubtarget (to store a value) for each set because we do not
// care if the set is supported, only the subfeatures inside the set. We fix
// this by simply setting the same dummy member for all feature sets, which is
// then ignored.
class FeatureSet<string name, string desc, list<SubtargetFeature> i>
    : SubtargetFeature<name, "HasFeatureSet"#NAME, "true", desc, i>;

// A family of microcontrollers, defining a set of supported features.
class Family<string name, list<SubtargetFeature> i>
    : FeatureSet<
          name, !strconcat("The device is a part of the ", name, " family"), i>;

// The device has SRAM, and supports the bare minimum of
// SRAM-relevant instructions.
//
// These are:
// LD - all 9 variants
// ST - all 9 variants
// LDD - two variants for Y and Z
// STD - two variants for Y and Z
// `LDS Rd, K`
// `STS k, Rr`
// `PUSH`/`POP`
def FeatureSRAM : SubtargetFeature<"sram", "HasSRAM", "true",
                                   "The device has random access memory">;

// The device supports the `JMP k` and `CALL k` instructions.
def FeatureJMPCALL : SubtargetFeature<"jmpcall", "HasJMPCALL", "true",
                                      "The device supports the `JMP` and "
                                      "`CALL` instructions">;

// The device supports the indirect branches `IJMP` and `ICALL`.
def FeatureIJMPCALL : SubtargetFeature<"ijmpcall", "HasSRAMIJMPCALL", "true",
                                       "The device supports `IJMP`/`ICALL`"
                                       "instructions">;

// The device supports the extended indirect branches `EIJMP` and `EICALL`.
def FeatureEIJMPCALL : SubtargetFeature<"eijmpcall", "HasEIJMPCALL", "true",
                                        "The device supports the "
                                        "`EIJMP`/`EICALL` instructions">;

// The device supports `ADDI Rd, K`, `SUBI Rd, K`.
def FeatureADDSUBIW : SubtargetFeature<"addsubiw", "HasADDSUBIW", "true",
                                       "Enable 16-bit register-immediate "
                                       "addition and subtraction instructions">;

// The device has an 8-bit stack pointer (SP) register.
def FeatureSmallStack
    : SubtargetFeature<"smallstack", "HasSmallStack", "true",
                       "The device has an 8-bit "
                       "stack pointer">;

// The device supports the 16-bit GPR pair MOVW instruction.
def FeatureMOVW : SubtargetFeature<"movw", "HasMOVW", "true",
                                   "The device supports the 16-bit MOVW "
                                   "instruction">;

// The device supports the `LPM` instruction, with implied destination being r0.
def FeatureLPM : SubtargetFeature<"lpm", "HasLPM", "true",
                                  "The device supports the `LPM` instruction">;

// The device supports the `LPM Rd, Z[+] instruction.
def FeatureLPMX : SubtargetFeature<"lpmx", "HasLPMX", "true",
                                   "The device supports the `LPM Rd, Z[+]` "
                                   "instruction">;

// The device supports the `ELPM` instruction.
def FeatureELPM : SubtargetFeature<"elpm", "HasELPM", "true",
                                   "The device supports the ELPM instruction">;

// The device supports the `ELPM Rd, Z[+]` instructions.
def FeatureELPMX : SubtargetFeature<"elpmx", "HasELPMX", "true",
                                    "The device supports the `ELPM Rd, Z[+]` "
                                    "instructions">;

// The device supports the `SPM` instruction.
def FeatureSPM : SubtargetFeature<"spm", "HasSPM", "true",
                                  "The device supports the `SPM` instruction">;

// The device supports the `SPM Z+` instruction.
def FeatureSPMX : SubtargetFeature<"spmx", "HasSPMX", "true",
                                   "The device supports the `SPM Z+` "
                                   "instruction">;

// The device supports the `DES k` instruction.
def FeatureDES : SubtargetFeature<"des", "HasDES", "true",
                                  "The device supports the `DES k` encryption "
                                  "instruction">;

// The device supports the Read-Write-Modify instructions
// XCH, LAS, LAC, and LAT.
def FeatureRMW : SubtargetFeature<"rmw", "SupportsRMW", "true",
                                  "The device supports the read-write-modify "
                                  "instructions: XCH, LAS, LAC, LAT">;

// The device supports the `[F]MUL[S][U]` family of instructions.
def FeatureMultiplication
    : SubtargetFeature<"mul", "SupportsMultiplication", "true",
                       "The device supports the "
                       "multiplication instructions">;

// The device supports the `BREAK` instruction.
def FeatureBREAK : SubtargetFeature<"break", "HasBREAK", "true",
                                    "The device supports the `BREAK` debugging "
                                    "instruction">;

// The device has instruction encodings specific to the Tiny core.
def FeatureTinyEncoding
    : SubtargetFeature<"tinyencoding", "HasTinyEncoding", "true",
                       "The device has Tiny core specific "
                       "instruction encodings">;

// When writing a 16-bit port or storing a 16-bit word, do the low byte first.
def FeatureLowByteFirst
    : SubtargetFeature<"lowbytefirst", "HasLowByteFirst", "true",
                       "Do the low byte first when writing a 16-bit port or "
                       "storing a 16-bit word">;

// The device has CPU registers mapped in data address space
def FeatureMMR : SubtargetFeature<"memmappedregs", "HasMemMappedGPR", "true",
                                  "The device has CPU registers "
                                  "mapped in data address space">;

class ELFArch<string name>
    : SubtargetFeature<"", "ELFArch", !strconcat("ELF::", name), "">;

// ELF e_flags architecture values
def ELFArchAVR1 : ELFArch<"EF_AVR_ARCH_AVR1">;
def ELFArchAVR2 : ELFArch<"EF_AVR_ARCH_AVR2">;
def ELFArchAVR25 : ELFArch<"EF_AVR_ARCH_AVR25">;
def ELFArchAVR3 : ELFArch<"EF_AVR_ARCH_AVR3">;
def ELFArchAVR31 : ELFArch<"EF_AVR_ARCH_AVR31">;
def ELFArchAVR35 : ELFArch<"EF_AVR_ARCH_AVR35">;
def ELFArchAVR4 : ELFArch<"EF_AVR_ARCH_AVR4">;
def ELFArchAVR5 : ELFArch<"EF_AVR_ARCH_AVR5">;
def ELFArchAVR51 : ELFArch<"EF_AVR_ARCH_AVR51">;
def ELFArchAVR6 : ELFArch<"EF_AVR_ARCH_AVR6">;
def ELFArchTiny : ELFArch<"EF_AVR_ARCH_AVRTINY">;
def ELFArchXMEGA1 : ELFArch<"EF_AVR_ARCH_XMEGA1">;
def ELFArchXMEGA2 : ELFArch<"EF_AVR_ARCH_XMEGA2">;
def ELFArchXMEGA3 : ELFArch<"EF_AVR_ARCH_XMEGA3">;
def ELFArchXMEGA4 : ELFArch<"EF_AVR_ARCH_XMEGA4">;
def ELFArchXMEGA5 : ELFArch<"EF_AVR_ARCH_XMEGA5">;
def ELFArchXMEGA6 : ELFArch<"EF_AVR_ARCH_XMEGA6">;
def ELFArchXMEGA7 : ELFArch<"EF_AVR_ARCH_XMEGA7">;

//===---------------------------------------------------------------------===//
// AVR Families
//===---------------------------------------------------------------------===//

// The device has at least the bare minimum that **every** single AVR
// device should have.
def FamilyAVR0 : Family<"avr0", []>;

def FamilyAVR1 : Family<"avr1", [FamilyAVR0, FeatureLPM, FeatureMMR]>;

def FamilyAVR2
    : Family<"avr2",
             [FamilyAVR1, FeatureIJMPCALL, FeatureADDSUBIW, FeatureSRAM]>;

def FamilyAVR25
    : Family<"avr25",
             [FamilyAVR2, FeatureMOVW, FeatureLPMX, FeatureSPM, FeatureBREAK]>;

def FamilyAVR3 : Family<"avr3", [FamilyAVR2, FeatureJMPCALL]>;

def FamilyAVR31 : Family<"avr31", [FamilyAVR3, FeatureELPM]>;

def FamilyAVR35
    : Family<"avr35",
             [FamilyAVR3, FeatureMOVW, FeatureLPMX, FeatureSPM, FeatureBREAK]>;

def FamilyAVR4 : Family<"avr4",
                        [FamilyAVR2, FeatureMultiplication, FeatureMOVW,
                         FeatureLPMX, FeatureSPM, FeatureBREAK]>;

def FamilyAVR5 : Family<"avr5",
                        [FamilyAVR3, FeatureMultiplication, FeatureMOVW,
                         FeatureLPMX, FeatureSPM, FeatureBREAK]>;

def FamilyAVR51 : Family<"avr51", [FamilyAVR5, FeatureELPM, FeatureELPMX]>;

def FamilyAVR6 : Family<"avr6", [FamilyAVR51, FeatureEIJMPCALL]>;

def FamilyTiny
    : Family<"avrtiny",
             [FamilyAVR0, FeatureBREAK, FeatureSRAM, FeatureTinyEncoding,
              FeatureSmallStack]>;

def FamilyXMEGA3 : Family<"xmega3",
                          [FamilyAVR0, FeatureLPM, FeatureIJMPCALL,
                           FeatureADDSUBIW, FeatureSRAM, FeatureJMPCALL,
                           FeatureMultiplication, FeatureMOVW, FeatureLPMX,
                           FeatureBREAK, FeatureLowByteFirst]>;

def FamilyXMEGA : Family<"xmega",
                         [FamilyAVR0, FeatureLPM, FeatureIJMPCALL,
                          FeatureADDSUBIW, FeatureSRAM, FeatureJMPCALL,
                          FeatureMultiplication, FeatureMOVW, FeatureLPMX,
                          FeatureSPM, FeatureBREAK, FeatureEIJMPCALL,
                          FeatureSPMX, FeatureDES, FeatureELPM, FeatureELPMX,
                          FeatureLowByteFirst]>;

def FamilyXMEGAU : Family<"xmegau", [FamilyXMEGA, FeatureRMW]>;

def FeatureSetSpecial
    : FeatureSet<"special",
                 "Enable use of the entire instruction "
                 "set - used for debugging",
                 [
                   FeatureSRAM, FeatureJMPCALL, FeatureIJMPCALL,
                   FeatureEIJMPCALL, FeatureADDSUBIW, FeatureMOVW, FeatureLPM,
                   FeatureLPMX, FeatureELPM, FeatureELPMX, FeatureSPM,
                   FeatureSPMX, FeatureDES, FeatureRMW, FeatureMultiplication,
                   FeatureBREAK, FeatureMMR
                 ]>;

//===---------------------------------------------------------------------===//
// AVR microcontrollers supported.
//===---------------------------------------------------------------------===//

class Device<string Name, Family Fam, ELFArch Arch,
             list<SubtargetFeature> ExtraFeatures = []>
    : Processor<Name, NoItineraries, !listconcat([Fam, Arch], ExtraFeatures)>;

// Generic MCUs
// Note that several versions of GCC has strange ELF architecture
// settings for backwards compatibility - see `gas/config/tc-avr.c`
// in AVR binutils. We do not replicate this.
def : Device<"avr1", FamilyAVR1, ELFArchAVR1>;
def : Device<"avr2", FamilyAVR2, ELFArchAVR2>;
def : Device<"avr25", FamilyAVR25, ELFArchAVR25>;
def : Device<"avr3", FamilyAVR3, ELFArchAVR3>;
def : Device<"avr31", FamilyAVR31, ELFArchAVR31>;
def : Device<"avr35", FamilyAVR35, ELFArchAVR35>;
def : Device<"avr4", FamilyAVR4, ELFArchAVR4>;
def : Device<"avr5", FamilyAVR5, ELFArchAVR5>;
def : Device<"avr51", FamilyAVR51, ELFArchAVR51>;
def : Device<"avr6", FamilyAVR6, ELFArchAVR6>;
def : Device<"avrxmega1", FamilyXMEGA, ELFArchXMEGA1>;
def : Device<"avrxmega2", FamilyXMEGA, ELFArchXMEGA2>;
def : Device<"avrxmega3", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"avrxmega4", FamilyXMEGA, ELFArchXMEGA4>;
def : Device<"avrxmega5", FamilyXMEGA, ELFArchXMEGA5>;
def : Device<"avrxmega6", FamilyXMEGA, ELFArchXMEGA6>;
def : Device<"avrxmega7", FamilyXMEGA, ELFArchXMEGA7>;
def : Device<"avrtiny", FamilyTiny, ELFArchTiny>;

// Specific MCUs
// NOTE: This list has been synchronized with gcc-avr 5.4.0 and avr-libc 2.0.0.
def : Device<"at90s1200", FamilyAVR0, ELFArchAVR1, [FeatureSmallStack]>;
def : Device<"attiny11", FamilyAVR1, ELFArchAVR1, [FeatureSmallStack]>;
def : Device<"attiny12", FamilyAVR1, ELFArchAVR1, [FeatureSmallStack]>;
def : Device<"attiny15", FamilyAVR1, ELFArchAVR1, [FeatureSmallStack]>;
def : Device<"attiny28", FamilyAVR1, ELFArchAVR1, [FeatureSmallStack]>;
def : Device<"at90s2313", FamilyAVR2, ELFArchAVR2, [FeatureSmallStack]>;
def : Device<"at90s2323", FamilyAVR2, ELFArchAVR2, [FeatureSmallStack]>;
def : Device<"at90s2333", FamilyAVR2, ELFArchAVR2, [FeatureSmallStack]>;
def : Device<"at90s2343", FamilyAVR2, ELFArchAVR2, [FeatureSmallStack]>;
def : Device<"attiny22", FamilyAVR2, ELFArchAVR2, [FeatureSmallStack]>;
def : Device<"attiny26", FamilyAVR2, ELFArchAVR2,
             [FeatureLPMX, FeatureSmallStack]>;
def : Device<"at86rf401", FamilyAVR2, ELFArchAVR25, [FeatureMOVW, FeatureLPMX]>;
def : Device<"at90s4414", FamilyAVR2, ELFArchAVR2, [FeatureSmallStack]>;
def : Device<"at90s4433", FamilyAVR2, ELFArchAVR2, [FeatureSmallStack]>;
def : Device<"at90s4434", FamilyAVR2, ELFArchAVR2, [FeatureSmallStack]>;
def : Device<"at90s8515", FamilyAVR2, ELFArchAVR2>;
def : Device<"at90c8534", FamilyAVR2, ELFArchAVR2>;
def : Device<"at90s8535", FamilyAVR2, ELFArchAVR2>;
def : Device<"ata5272", FamilyAVR25, ELFArchAVR25>;
def : Device<"ata6616c", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny13", FamilyAVR25, ELFArchAVR25, [FeatureSmallStack]>;
def : Device<"attiny13a", FamilyAVR25, ELFArchAVR25, [FeatureSmallStack]>;
def : Device<"attiny2313", FamilyAVR25, ELFArchAVR25, [FeatureSmallStack]>;
def : Device<"attiny2313a", FamilyAVR25, ELFArchAVR25, [FeatureSmallStack]>;
def : Device<"attiny24", FamilyAVR25, ELFArchAVR25, [FeatureSmallStack]>;
def : Device<"attiny24a", FamilyAVR25, ELFArchAVR25, [FeatureSmallStack]>;
def : Device<"attiny4313", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny44", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny44a", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny84", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny84a", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny25", FamilyAVR25, ELFArchAVR25, [FeatureSmallStack]>;
def : Device<"attiny45", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny85", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny261", FamilyAVR25, ELFArchAVR25, [FeatureSmallStack]>;
def : Device<"attiny261a", FamilyAVR25, ELFArchAVR25, [FeatureSmallStack]>;
def : Device<"attiny441", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny461", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny461a", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny841", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny861", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny861a", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny87", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny43u", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny48", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny88", FamilyAVR25, ELFArchAVR25>;
def : Device<"attiny828", FamilyAVR25, ELFArchAVR25>;
def : Device<"at43usb355", FamilyAVR3, ELFArchAVR3>;
def : Device<"at76c711", FamilyAVR3, ELFArchAVR3>;
def : Device<"atmega103", FamilyAVR31, ELFArchAVR31>;
def : Device<"at43usb320", FamilyAVR31, ELFArchAVR31>;
def : Device<"attiny167", FamilyAVR35, ELFArchAVR35>;
def : Device<"at90usb82", FamilyAVR35, ELFArchAVR35>;
def : Device<"at90usb162", FamilyAVR35, ELFArchAVR35>;
def : Device<"ata5505", FamilyAVR35, ELFArchAVR35>;
def : Device<"ata6617c", FamilyAVR35, ELFArchAVR35>;
def : Device<"ata664251", FamilyAVR35, ELFArchAVR35>;
def : Device<"atmega8u2", FamilyAVR35, ELFArchAVR35>;
def : Device<"atmega16u2", FamilyAVR35, ELFArchAVR35>;
def : Device<"atmega32u2", FamilyAVR35, ELFArchAVR35>;
def : Device<"attiny1634", FamilyAVR35, ELFArchAVR35>;
def : Device<"atmega8", FamilyAVR2, ELFArchAVR4,
             [FeatureMultiplication, FeatureMOVW, FeatureLPMX, FeatureSPM]>;
def : Device<"ata6289", FamilyAVR4, ELFArchAVR4>;
def : Device<"atmega8a", FamilyAVR2, ELFArchAVR4,
             [FeatureMultiplication, FeatureMOVW, FeatureLPMX, FeatureSPM]>;
def : Device<"ata6285", FamilyAVR4, ELFArchAVR4>;
def : Device<"ata6286", FamilyAVR4, ELFArchAVR4>;
def : Device<"ata6612c", FamilyAVR4, ELFArchAVR4>;
def : Device<"atmega48", FamilyAVR4, ELFArchAVR4>;
def : Device<"atmega48a", FamilyAVR4, ELFArchAVR4>;
def : Device<"atmega48pa", FamilyAVR4, ELFArchAVR4>;
def : Device<"atmega48pb", FamilyAVR4, ELFArchAVR4>;
def : Device<"atmega48p", FamilyAVR4, ELFArchAVR4>;
def : Device<"atmega88", FamilyAVR4, ELFArchAVR4>;
def : Device<"atmega88a", FamilyAVR4, ELFArchAVR4>;
def : Device<"atmega88p", FamilyAVR4, ELFArchAVR4>;
def : Device<"atmega88pa", FamilyAVR4, ELFArchAVR4>;
def : Device<"atmega88pb", FamilyAVR4, ELFArchAVR4>;
def : Device<"atmega8515", FamilyAVR2, ELFArchAVR4,
             [FeatureMultiplication, FeatureMOVW, FeatureLPMX, FeatureSPM]>;
def : Device<"atmega8535", FamilyAVR2, ELFArchAVR4,
             [FeatureMultiplication, FeatureMOVW, FeatureLPMX, FeatureSPM]>;
def : Device<"atmega8hva", FamilyAVR4, ELFArchAVR4>;
def : Device<"at90pwm1", FamilyAVR4, ELFArchAVR4>;
def : Device<"at90pwm2", FamilyAVR4, ELFArchAVR4>;
def : Device<"at90pwm2b", FamilyAVR4, ELFArchAVR4>;
def : Device<"at90pwm3", FamilyAVR4, ELFArchAVR4>;
def : Device<"at90pwm3b", FamilyAVR4, ELFArchAVR4>;
def : Device<"at90pwm81", FamilyAVR4, ELFArchAVR4>;
def : Device<"ata5702m322", FamilyAVR5, ELFArchAVR5>;
def : Device<"ata5782", FamilyAVR5, ELFArchAVR5>;
def : Device<"ata5790", FamilyAVR5, ELFArchAVR5>;
def : Device<"ata5790n", FamilyAVR5, ELFArchAVR5>;
def : Device<"ata5791", FamilyAVR5, ELFArchAVR5>;
def : Device<"ata5795", FamilyAVR5, ELFArchAVR5>;
def : Device<"ata5831", FamilyAVR5, ELFArchAVR5>;
def : Device<"ata6613c", FamilyAVR5, ELFArchAVR5>;
def : Device<"ata6614q", FamilyAVR5, ELFArchAVR5>;
def : Device<"ata8210", FamilyAVR5, ELFArchAVR5>;
def : Device<"ata8510", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega16", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega16a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega161", FamilyAVR3, ELFArchAVR5,
             [FeatureMultiplication, FeatureMOVW, FeatureLPMX, FeatureSPM]>;
def : Device<"atmega162", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega163", FamilyAVR3, ELFArchAVR5,
             [FeatureMultiplication, FeatureMOVW, FeatureLPMX, FeatureSPM]>;
def : Device<"atmega164a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega164p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega164pa", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega165", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega165a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega165p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega165pa", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega168", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega168a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega168p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega168pa", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega168pb", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega169", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega169a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega169p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega169pa", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega32", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega32a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega323", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega324a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega324p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega324pa", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega324pb", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega325", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega325a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega325p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega325pa", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega3250", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega3250a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega3250p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega3250pa", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega328", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega328p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega328pb", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega329", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega329a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega329p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega329pa", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega3290", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega3290a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega3290p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega3290pa", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega406", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega64", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega64a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega640", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega644", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega644a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega644p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega644pa", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega645", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega645a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega645p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega649", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega649a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega649p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega6450", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega6450a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega6450p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega6490", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega6490a", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega6490p", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega64rfr2", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega644rfr2", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega16hva", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega16hva2", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega16hvb", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega16hvbrevb", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega32hvb", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega32hvbrevb", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega64hve", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega64hve2", FamilyAVR5, ELFArchAVR5>;
def : Device<"at90can32", FamilyAVR5, ELFArchAVR5>;
def : Device<"at90can64", FamilyAVR5, ELFArchAVR5>;
def : Device<"at90pwm161", FamilyAVR5, ELFArchAVR5>;
def : Device<"at90pwm216", FamilyAVR5, ELFArchAVR5>;
def : Device<"at90pwm316", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega32c1", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega64c1", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega16m1", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega32m1", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega64m1", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega16u4", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega32u4", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega32u6", FamilyAVR5, ELFArchAVR5>;
def : Device<"at90usb646", FamilyAVR5, ELFArchAVR5>;
def : Device<"at90usb647", FamilyAVR5, ELFArchAVR5>;
def : Device<"at90scr100", FamilyAVR5, ELFArchAVR5>;
def : Device<"at94k", FamilyAVR3, ELFArchAVR5,
             [FeatureMultiplication, FeatureMOVW, FeatureLPMX]>;
def : Device<"m3000", FamilyAVR5, ELFArchAVR5>;
def : Device<"atmega128", FamilyAVR51, ELFArchAVR51>;
def : Device<"atmega128a", FamilyAVR51, ELFArchAVR51>;
def : Device<"atmega1280", FamilyAVR51, ELFArchAVR51>;
def : Device<"atmega1281", FamilyAVR51, ELFArchAVR51>;
def : Device<"atmega1284", FamilyAVR51, ELFArchAVR51>;
def : Device<"atmega1284p", FamilyAVR51, ELFArchAVR51>;
def : Device<"atmega128rfa1", FamilyAVR51, ELFArchAVR51>;
def : Device<"atmega128rfr2", FamilyAVR51, ELFArchAVR51>;
def : Device<"atmega1284rfr2", FamilyAVR51, ELFArchAVR51>;
def : Device<"at90can128", FamilyAVR51, ELFArchAVR51>;
def : Device<"at90usb1286", FamilyAVR51, ELFArchAVR51>;
def : Device<"at90usb1287", FamilyAVR51, ELFArchAVR51>;
def : Device<"atmega2560", FamilyAVR6, ELFArchAVR6>;
def : Device<"atmega2561", FamilyAVR6, ELFArchAVR6>;
def : Device<"atmega256rfr2", FamilyAVR6, ELFArchAVR6>;
def : Device<"atmega2564rfr2", FamilyAVR6, ELFArchAVR6>;
def : Device<"atxmega16a4", FamilyXMEGA, ELFArchXMEGA2>;
def : Device<"atxmega16a4u", FamilyXMEGAU, ELFArchXMEGA2>;
def : Device<"atxmega16c4", FamilyXMEGAU, ELFArchXMEGA2>;
def : Device<"atxmega16d4", FamilyXMEGA, ELFArchXMEGA2>;
def : Device<"atxmega32a4", FamilyXMEGA, ELFArchXMEGA2>;
def : Device<"atxmega32a4u", FamilyXMEGAU, ELFArchXMEGA2>;
def : Device<"atxmega32c3", FamilyXMEGAU, ELFArchXMEGA2>;
def : Device<"atxmega32c4", FamilyXMEGAU, ELFArchXMEGA2>;
def : Device<"atxmega32d3", FamilyXMEGA, ELFArchXMEGA2>;
def : Device<"atxmega32d4", FamilyXMEGA, ELFArchXMEGA2>;
def : Device<"atxmega32e5", FamilyXMEGAU, ELFArchXMEGA2>;
def : Device<"atxmega16e5", FamilyXMEGAU, ELFArchXMEGA2>;
def : Device<"atxmega8e5", FamilyXMEGAU, ELFArchXMEGA2>;
def : Device<"atxmega64a3", FamilyXMEGA, ELFArchXMEGA4>;
def : Device<"atxmega64a3u", FamilyXMEGAU, ELFArchXMEGA4>;
def : Device<"atxmega64a4u", FamilyXMEGAU, ELFArchXMEGA4>;
def : Device<"atxmega64b1", FamilyXMEGAU, ELFArchXMEGA4>;
def : Device<"atxmega64b3", FamilyXMEGAU, ELFArchXMEGA4>;
def : Device<"atxmega64c3", FamilyXMEGAU, ELFArchXMEGA4>;
def : Device<"atxmega64d3", FamilyXMEGA, ELFArchXMEGA4>;
def : Device<"atxmega64d4", FamilyXMEGA, ELFArchXMEGA4>;
def : Device<"atxmega64a1", FamilyXMEGA, ELFArchXMEGA5>;
def : Device<"atxmega64a1u", FamilyXMEGAU, ELFArchXMEGA5>;
def : Device<"atxmega128a3", FamilyXMEGA, ELFArchXMEGA6>;
def : Device<"atxmega128a3u", FamilyXMEGAU, ELFArchXMEGA6>;
def : Device<"atxmega128b1", FamilyXMEGAU, ELFArchXMEGA6>;
def : Device<"atxmega128b3", FamilyXMEGAU, ELFArchXMEGA6>;
def : Device<"atxmega128c3", FamilyXMEGAU, ELFArchXMEGA6>;
def : Device<"atxmega128d3", FamilyXMEGA, ELFArchXMEGA6>;
def : Device<"atxmega128d4", FamilyXMEGA, ELFArchXMEGA6>;
def : Device<"atxmega192a3", FamilyXMEGA, ELFArchXMEGA6>;
def : Device<"atxmega192a3u", FamilyXMEGAU, ELFArchXMEGA6>;
def : Device<"atxmega192c3", FamilyXMEGAU, ELFArchXMEGA6>;
def : Device<"atxmega192d3", FamilyXMEGA, ELFArchXMEGA6>;
def : Device<"atxmega256a3", FamilyXMEGA, ELFArchXMEGA6>;
def : Device<"atxmega256a3u", FamilyXMEGAU, ELFArchXMEGA6>;
def : Device<"atxmega256a3b", FamilyXMEGA, ELFArchXMEGA6>;
def : Device<"atxmega256a3bu", FamilyXMEGAU, ELFArchXMEGA6>;
def : Device<"atxmega256c3", FamilyXMEGAU, ELFArchXMEGA6>;
def : Device<"atxmega256d3", FamilyXMEGA, ELFArchXMEGA6>;
def : Device<"atxmega384c3", FamilyXMEGAU, ELFArchXMEGA6>;
def : Device<"atxmega384d3", FamilyXMEGA, ELFArchXMEGA6>;
def : Device<"atxmega128a1", FamilyXMEGA, ELFArchXMEGA7>;
def : Device<"atxmega128a1u", FamilyXMEGAU, ELFArchXMEGA7>;
def : Device<"atxmega128a4u", FamilyXMEGAU, ELFArchXMEGA7>;
def : Device<"attiny4", FamilyTiny, ELFArchTiny>;
def : Device<"attiny5", FamilyTiny, ELFArchTiny>;
def : Device<"attiny9", FamilyTiny, ELFArchTiny>;
def : Device<"attiny10", FamilyTiny, ELFArchTiny>;
def : Device<"attiny20", FamilyTiny, ELFArchTiny>;
def : Device<"attiny40", FamilyTiny, ELFArchTiny>;
def : Device<"attiny102", FamilyTiny, ELFArchTiny>;
def : Device<"attiny104", FamilyTiny, ELFArchTiny>;
def : Device<"attiny202", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny402", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny204", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny404", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny804", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny1604", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny406", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny806", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny1606", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny807", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny1607", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny212", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny412", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny214", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny414", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny814", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny1614", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny416", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny816", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny1616", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny3216", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny417", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny817", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny1617", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny3217", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny1624", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny1626", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"attiny1627", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"atmega808", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"atmega809", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"atmega1608", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"atmega1609", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"atmega3208", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"atmega3209", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"atmega4808", FamilyXMEGA3, ELFArchXMEGA3>;
def : Device<"atmega4809", FamilyXMEGA3, ELFArchXMEGA3>;
