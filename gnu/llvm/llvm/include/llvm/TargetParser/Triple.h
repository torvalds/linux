//===-- llvm/TargetParser/Triple.h - Target triple helper class--*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_TRIPLE_H
#define LLVM_TARGETPARSER_TRIPLE_H

#include "llvm/ADT/Twine.h"
#include "llvm/Support/VersionTuple.h"

// Some system headers or GCC predefined macros conflict with identifiers in
// this file.  Undefine them here.
#undef NetBSD
#undef mips
#undef sparc

namespace llvm {

/// Triple - Helper class for working with autoconf configuration names. For
/// historical reasons, we also call these 'triples' (they used to contain
/// exactly three fields).
///
/// Configuration names are strings in the canonical form:
///   ARCHITECTURE-VENDOR-OPERATING_SYSTEM
/// or
///   ARCHITECTURE-VENDOR-OPERATING_SYSTEM-ENVIRONMENT
///
/// This class is used for clients which want to support arbitrary
/// configuration names, but also want to implement certain special
/// behavior for particular configurations. This class isolates the mapping
/// from the components of the configuration name to well known IDs.
///
/// At its core the Triple class is designed to be a wrapper for a triple
/// string; the constructor does not change or normalize the triple string.
/// Clients that need to handle the non-canonical triples that users often
/// specify should use the normalize method.
///
/// See autoconf/config.guess for a glimpse into what configuration names
/// look like in practice.
class Triple {
public:
  enum ArchType {
    UnknownArch,

    arm,            // ARM (little endian): arm, armv.*, xscale
    armeb,          // ARM (big endian): armeb
    aarch64,        // AArch64 (little endian): aarch64
    aarch64_be,     // AArch64 (big endian): aarch64_be
    aarch64_32,     // AArch64 (little endian) ILP32: aarch64_32
    arc,            // ARC: Synopsys ARC
    avr,            // AVR: Atmel AVR microcontroller
    bpfel,          // eBPF or extended BPF or 64-bit BPF (little endian)
    bpfeb,          // eBPF or extended BPF or 64-bit BPF (big endian)
    csky,           // CSKY: csky
    dxil,           // DXIL 32-bit DirectX bytecode
    hexagon,        // Hexagon: hexagon
    loongarch32,    // LoongArch (32-bit): loongarch32
    loongarch64,    // LoongArch (64-bit): loongarch64
    m68k,           // M68k: Motorola 680x0 family
    mips,           // MIPS: mips, mipsallegrex, mipsr6
    mipsel,         // MIPSEL: mipsel, mipsallegrexe, mipsr6el
    mips64,         // MIPS64: mips64, mips64r6, mipsn32, mipsn32r6
    mips64el,       // MIPS64EL: mips64el, mips64r6el, mipsn32el, mipsn32r6el
    msp430,         // MSP430: msp430
    ppc,            // PPC: powerpc
    ppcle,          // PPCLE: powerpc (little endian)
    ppc64,          // PPC64: powerpc64, ppu
    ppc64le,        // PPC64LE: powerpc64le
    r600,           // R600: AMD GPUs HD2XXX - HD6XXX
    amdgcn,         // AMDGCN: AMD GCN GPUs
    riscv32,        // RISC-V (32-bit): riscv32
    riscv64,        // RISC-V (64-bit): riscv64
    sparc,          // Sparc: sparc
    sparcv9,        // Sparcv9: Sparcv9
    sparcel,        // Sparc: (endianness = little). NB: 'Sparcle' is a CPU variant
    systemz,        // SystemZ: s390x
    tce,            // TCE (http://tce.cs.tut.fi/): tce
    tcele,          // TCE little endian (http://tce.cs.tut.fi/): tcele
    thumb,          // Thumb (little endian): thumb, thumbv.*
    thumbeb,        // Thumb (big endian): thumbeb
    x86,            // X86: i[3-9]86
    x86_64,         // X86-64: amd64, x86_64
    xcore,          // XCore: xcore
    xtensa,         // Tensilica: Xtensa
    nvptx,          // NVPTX: 32-bit
    nvptx64,        // NVPTX: 64-bit
    le32,           // le32: generic little-endian 32-bit CPU (PNaCl)
    le64,           // le64: generic little-endian 64-bit CPU (PNaCl)
    amdil,          // AMDIL
    amdil64,        // AMDIL with 64-bit pointers
    hsail,          // AMD HSAIL
    hsail64,        // AMD HSAIL with 64-bit pointers
    spir,           // SPIR: standard portable IR for OpenCL 32-bit version
    spir64,         // SPIR: standard portable IR for OpenCL 64-bit version
    spirv,          // SPIR-V with logical memory layout.
    spirv32,        // SPIR-V with 32-bit pointers
    spirv64,        // SPIR-V with 64-bit pointers
    kalimba,        // Kalimba: generic kalimba
    shave,          // SHAVE: Movidius vector VLIW processors
    lanai,          // Lanai: Lanai 32-bit
    wasm32,         // WebAssembly with 32-bit pointers
    wasm64,         // WebAssembly with 64-bit pointers
    renderscript32, // 32-bit RenderScript
    renderscript64, // 64-bit RenderScript
    ve,             // NEC SX-Aurora Vector Engine
    LastArchType = ve
  };
  enum SubArchType {
    NoSubArch,

    ARMSubArch_v9_5a,
    ARMSubArch_v9_4a,
    ARMSubArch_v9_3a,
    ARMSubArch_v9_2a,
    ARMSubArch_v9_1a,
    ARMSubArch_v9,
    ARMSubArch_v8_9a,
    ARMSubArch_v8_8a,
    ARMSubArch_v8_7a,
    ARMSubArch_v8_6a,
    ARMSubArch_v8_5a,
    ARMSubArch_v8_4a,
    ARMSubArch_v8_3a,
    ARMSubArch_v8_2a,
    ARMSubArch_v8_1a,
    ARMSubArch_v8,
    ARMSubArch_v8r,
    ARMSubArch_v8m_baseline,
    ARMSubArch_v8m_mainline,
    ARMSubArch_v8_1m_mainline,
    ARMSubArch_v7,
    ARMSubArch_v7em,
    ARMSubArch_v7m,
    ARMSubArch_v7s,
    ARMSubArch_v7k,
    ARMSubArch_v7ve,
    ARMSubArch_v6,
    ARMSubArch_v6m,
    ARMSubArch_v6k,
    ARMSubArch_v6t2,
    ARMSubArch_v5,
    ARMSubArch_v5te,
    ARMSubArch_v4t,

    AArch64SubArch_arm64e,
    AArch64SubArch_arm64ec,

    KalimbaSubArch_v3,
    KalimbaSubArch_v4,
    KalimbaSubArch_v5,

    MipsSubArch_r6,

    PPCSubArch_spe,

    // SPIR-V sub-arch corresponds to its version.
    SPIRVSubArch_v10,
    SPIRVSubArch_v11,
    SPIRVSubArch_v12,
    SPIRVSubArch_v13,
    SPIRVSubArch_v14,
    SPIRVSubArch_v15,
    SPIRVSubArch_v16,

    // DXIL sub-arch corresponds to its version.
    DXILSubArch_v1_0,
    DXILSubArch_v1_1,
    DXILSubArch_v1_2,
    DXILSubArch_v1_3,
    DXILSubArch_v1_4,
    DXILSubArch_v1_5,
    DXILSubArch_v1_6,
    DXILSubArch_v1_7,
    DXILSubArch_v1_8,
    LatestDXILSubArch = DXILSubArch_v1_8,
  };
  enum VendorType {
    UnknownVendor,

    Apple,
    PC,
    SCEI,
    Freescale,
    IBM,
    ImaginationTechnologies,
    MipsTechnologies,
    NVIDIA,
    CSR,
    AMD,
    Mesa,
    SUSE,
    OpenEmbedded,
    LastVendorType = OpenEmbedded
  };
  enum OSType {
    UnknownOS,

    Darwin,
    DragonFly,
    FreeBSD,
    Fuchsia,
    IOS,
    KFreeBSD,
    Linux,
    Lv2, // PS3
    MacOSX,
    NetBSD,
    OpenBSD,
    Solaris,
    UEFI,
    Win32,
    ZOS,
    Haiku,
    RTEMS,
    NaCl, // Native Client
    AIX,
    CUDA,   // NVIDIA CUDA
    NVCL,   // NVIDIA OpenCL
    AMDHSA, // AMD HSA Runtime
    PS4,
    PS5,
    ELFIAMCU,
    TvOS,      // Apple tvOS
    WatchOS,   // Apple watchOS
    BridgeOS,  // Apple bridgeOS
    DriverKit, // Apple DriverKit
    XROS,      // Apple XROS
    Mesa3D,
    AMDPAL,     // AMD PAL Runtime
    HermitCore, // HermitCore Unikernel/Multikernel
    Hurd,       // GNU/Hurd
    WASI,       // Experimental WebAssembly OS
    Emscripten,
    ShaderModel, // DirectX ShaderModel
    LiteOS,
    Serenity,
    Vulkan, // Vulkan SPIR-V
    LastOSType = Vulkan
  };
  enum EnvironmentType {
    UnknownEnvironment,

    GNU,
    GNUABIN32,
    GNUABI64,
    GNUEABI,
    GNUEABIHF,
    GNUF32,
    GNUF64,
    GNUSF,
    GNUX32,
    GNUILP32,
    CODE16,
    EABI,
    EABIHF,
    Android,
    Musl,
    MuslEABI,
    MuslEABIHF,
    MuslX32,

    MSVC,
    Itanium,
    Cygnus,
    CoreCLR,
    Simulator, // Simulator variants of other systems, e.g., Apple's iOS
    MacABI,    // Mac Catalyst variant of Apple's iOS deployment target.

    // Shader Stages
    // The order of these values matters, and must be kept in sync with the
    // language options enum in Clang. The ordering is enforced in
    // static_asserts in Triple.cpp and in Clang.
    Pixel,
    Vertex,
    Geometry,
    Hull,
    Domain,
    Compute,
    Library,
    RayGeneration,
    Intersection,
    AnyHit,
    ClosestHit,
    Miss,
    Callable,
    Mesh,
    Amplification,
    OpenCL,
    OpenHOS,

    PAuthTest,

    GNUT64,
    GNUEABIT64,
    GNUEABIHFT64,

    LastEnvironmentType = GNUEABIHFT64
  };
  enum ObjectFormatType {
    UnknownObjectFormat,

    COFF,
    DXContainer,
    ELF,
    GOFF,
    MachO,
    SPIRV,
    Wasm,
    XCOFF,
  };

private:
  std::string Data;

  /// The parsed arch type.
  ArchType Arch{};

  /// The parsed subarchitecture type.
  SubArchType SubArch{};

  /// The parsed vendor type.
  VendorType Vendor{};

  /// The parsed OS type.
  OSType OS{};

  /// The parsed Environment type.
  EnvironmentType Environment{};

  /// The object format type.
  ObjectFormatType ObjectFormat{};

public:
  /// @name Constructors
  /// @{

  /// Default constructor is the same as an empty string and leaves all
  /// triple fields unknown.
  Triple() = default;

  explicit Triple(const Twine &Str);
  Triple(const Twine &ArchStr, const Twine &VendorStr, const Twine &OSStr);
  Triple(const Twine &ArchStr, const Twine &VendorStr, const Twine &OSStr,
         const Twine &EnvironmentStr);

  bool operator==(const Triple &Other) const {
    return Arch == Other.Arch && SubArch == Other.SubArch &&
           Vendor == Other.Vendor && OS == Other.OS &&
           Environment == Other.Environment &&
           ObjectFormat == Other.ObjectFormat;
  }

  bool operator!=(const Triple &Other) const {
    return !(*this == Other);
  }

  /// @}
  /// @name Normalization
  /// @{

  /// Turn an arbitrary machine specification into the canonical triple form (or
  /// something sensible that the Triple class understands if nothing better can
  /// reasonably be done).  In particular, it handles the common case in which
  /// otherwise valid components are in the wrong order.
  static std::string normalize(StringRef Str);

  /// Return the normalized form of this triple's string.
  std::string normalize() const { return normalize(Data); }

  /// @}
  /// @name Typed Component Access
  /// @{

  /// Get the parsed architecture type of this triple.
  ArchType getArch() const { return Arch; }

  /// get the parsed subarchitecture type for this triple.
  SubArchType getSubArch() const { return SubArch; }

  /// Get the parsed vendor type of this triple.
  VendorType getVendor() const { return Vendor; }

  /// Get the parsed operating system type of this triple.
  OSType getOS() const { return OS; }

  /// Does this triple have the optional environment (fourth) component?
  bool hasEnvironment() const {
    return getEnvironmentName() != "";
  }

  /// Get the parsed environment type of this triple.
  EnvironmentType getEnvironment() const { return Environment; }

  /// Parse the version number from the OS name component of the
  /// triple, if present.
  ///
  /// For example, "fooos1.2.3" would return (1, 2, 3).
  VersionTuple getEnvironmentVersion() const;

  /// Get the object format for this triple.
  ObjectFormatType getObjectFormat() const { return ObjectFormat; }

  /// Parse the version number from the OS name component of the triple, if
  /// present.
  ///
  /// For example, "fooos1.2.3" would return (1, 2, 3).
  VersionTuple getOSVersion() const;

  /// Return just the major version number, this is specialized because it is a
  /// common query.
  unsigned getOSMajorVersion() const { return getOSVersion().getMajor(); }

  /// Parse the version number as with getOSVersion and then translate generic
  /// "darwin" versions to the corresponding OS X versions.  This may also be
  /// called with IOS triples but the OS X version number is just set to a
  /// constant 10.4.0 in that case.  Returns true if successful.
  bool getMacOSXVersion(VersionTuple &Version) const;

  /// Parse the version number as with getOSVersion.  This should only be called
  /// with IOS or generic triples.
  VersionTuple getiOSVersion() const;

  /// Parse the version number as with getOSVersion.  This should only be called
  /// with WatchOS or generic triples.
  VersionTuple getWatchOSVersion() const;

  /// Parse the version number as with getOSVersion.
  VersionTuple getDriverKitVersion() const;

  /// Parse the Vulkan version number from the OSVersion and SPIR-V version
  /// (SubArch).  This should only be called with Vulkan SPIR-V triples.
  VersionTuple getVulkanVersion() const;

  /// Parse the DXIL version number from the OSVersion and DXIL version
  /// (SubArch).  This should only be called with DXIL triples.
  VersionTuple getDXILVersion() const;

  /// @}
  /// @name Direct Component Access
  /// @{

  const std::string &str() const { return Data; }

  const std::string &getTriple() const { return Data; }

  /// Get the architecture (first) component of the triple.
  StringRef getArchName() const;

  /// Get the vendor (second) component of the triple.
  StringRef getVendorName() const;

  /// Get the operating system (third) component of the triple.
  StringRef getOSName() const;

  /// Get the optional environment (fourth) component of the triple, or "" if
  /// empty.
  StringRef getEnvironmentName() const;

  /// Get the operating system and optional environment components as a single
  /// string (separated by a '-' if the environment component is present).
  StringRef getOSAndEnvironmentName() const;

  /// Get the version component of the environment component as a single
  /// string (the version after the environment).
  ///
  /// For example, "fooos1.2.3" would return "1.2.3".
  StringRef getEnvironmentVersionString() const;

  /// @}
  /// @name Convenience Predicates
  /// @{

  /// Returns the pointer width of this architecture.
  static unsigned getArchPointerBitWidth(llvm::Triple::ArchType Arch);

  /// Returns the pointer width of this architecture.
  unsigned getArchPointerBitWidth() const {
    return getArchPointerBitWidth(getArch());
  }

  /// Test whether the architecture is 64-bit
  ///
  /// Note that this tests for 64-bit pointer width, and nothing else. Note
  /// that we intentionally expose only three predicates, 64-bit, 32-bit, and
  /// 16-bit. The inner details of pointer width for particular architectures
  /// is not summed up in the triple, and so only a coarse grained predicate
  /// system is provided.
  bool isArch64Bit() const;

  /// Test whether the architecture is 32-bit
  ///
  /// Note that this tests for 32-bit pointer width, and nothing else.
  bool isArch32Bit() const;

  /// Test whether the architecture is 16-bit
  ///
  /// Note that this tests for 16-bit pointer width, and nothing else.
  bool isArch16Bit() const;

  /// Helper function for doing comparisons against version numbers included in
  /// the target triple.
  bool isOSVersionLT(unsigned Major, unsigned Minor = 0,
                     unsigned Micro = 0) const {
    if (Minor == 0) {
      return getOSVersion() < VersionTuple(Major);
    }
    if (Micro == 0) {
      return getOSVersion() < VersionTuple(Major, Minor);
    }
    return getOSVersion() < VersionTuple(Major, Minor, Micro);
  }

  bool isOSVersionLT(const Triple &Other) const {
    return getOSVersion() < Other.getOSVersion();
  }

  /// Comparison function for checking OS X version compatibility, which handles
  /// supporting skewed version numbering schemes used by the "darwin" triples.
  bool isMacOSXVersionLT(unsigned Major, unsigned Minor = 0,
                         unsigned Micro = 0) const;

  /// Is this a Mac OS X triple. For legacy reasons, we support both "darwin"
  /// and "osx" as OS X triples.
  bool isMacOSX() const {
    return getOS() == Triple::Darwin || getOS() == Triple::MacOSX;
  }

  /// Is this an iOS triple.
  /// Note: This identifies tvOS as a variant of iOS. If that ever
  /// changes, i.e., if the two operating systems diverge or their version
  /// numbers get out of sync, that will need to be changed.
  /// watchOS has completely different version numbers so it is not included.
  bool isiOS() const {
    return getOS() == Triple::IOS || isTvOS();
  }

  /// Is this an Apple tvOS triple.
  bool isTvOS() const {
    return getOS() == Triple::TvOS;
  }

  /// Is this an Apple watchOS triple.
  bool isWatchOS() const {
    return getOS() == Triple::WatchOS;
  }

  bool isWatchABI() const {
    return getSubArch() == Triple::ARMSubArch_v7k;
  }

  /// Is this an Apple XROS triple.
  bool isXROS() const { return getOS() == Triple::XROS; }

  /// Is this an Apple DriverKit triple.
  bool isDriverKit() const { return getOS() == Triple::DriverKit; }

  bool isOSzOS() const { return getOS() == Triple::ZOS; }

  /// Is this a "Darwin" OS (macOS, iOS, tvOS, watchOS, XROS, or DriverKit).
  bool isOSDarwin() const {
    return isMacOSX() || isiOS() || isWatchOS() || isDriverKit() || isXROS();
  }

  bool isSimulatorEnvironment() const {
    return getEnvironment() == Triple::Simulator;
  }

  bool isMacCatalystEnvironment() const {
    return getEnvironment() == Triple::MacABI;
  }

  /// Returns true for targets that run on a macOS machine.
  bool isTargetMachineMac() const {
    return isMacOSX() || (isOSDarwin() && (isSimulatorEnvironment() ||
                                           isMacCatalystEnvironment()));
  }

  bool isOSNetBSD() const {
    return getOS() == Triple::NetBSD;
  }

  bool isOSOpenBSD() const {
    return getOS() == Triple::OpenBSD;
  }

  bool isOSFreeBSD() const {
    return getOS() == Triple::FreeBSD;
  }

  bool isOSFuchsia() const {
    return getOS() == Triple::Fuchsia;
  }

  bool isOSDragonFly() const { return getOS() == Triple::DragonFly; }

  bool isOSSolaris() const {
    return getOS() == Triple::Solaris;
  }

  bool isOSIAMCU() const {
    return getOS() == Triple::ELFIAMCU;
  }

  bool isOSUnknown() const { return getOS() == Triple::UnknownOS; }

  bool isGNUEnvironment() const {
    EnvironmentType Env = getEnvironment();
    return Env == Triple::GNU || Env == Triple::GNUT64 ||
           Env == Triple::GNUABIN32 || Env == Triple::GNUABI64 ||
           Env == Triple::GNUEABI || Env == Triple::GNUEABIT64 ||
           Env == Triple::GNUEABIHF || Env == Triple::GNUEABIHFT64 ||
           Env == Triple::GNUF32 || Env == Triple::GNUF64 ||
           Env == Triple::GNUSF || Env == Triple::GNUX32;
  }

  /// Tests whether the OS is Haiku.
  bool isOSHaiku() const {
    return getOS() == Triple::Haiku;
  }

  /// Tests whether the OS is UEFI.
  bool isUEFI() const {
    return getOS() == Triple::UEFI;
  }

  /// Tests whether the OS is Windows.
  bool isOSWindows() const {
    return getOS() == Triple::Win32;
  }

  /// Checks if the environment is MSVC.
  bool isKnownWindowsMSVCEnvironment() const {
    return isOSWindows() && getEnvironment() == Triple::MSVC;
  }

  /// Checks if the environment could be MSVC.
  bool isWindowsMSVCEnvironment() const {
    return isKnownWindowsMSVCEnvironment() ||
           (isOSWindows() && getEnvironment() == Triple::UnknownEnvironment);
  }

  // Checks if we're using the Windows Arm64EC ABI.
  bool isWindowsArm64EC() const {
    return getArch() == Triple::aarch64 &&
           getSubArch() == Triple::AArch64SubArch_arm64ec;
  }

  bool isWindowsCoreCLREnvironment() const {
    return isOSWindows() && getEnvironment() == Triple::CoreCLR;
  }

  bool isWindowsItaniumEnvironment() const {
    return isOSWindows() && getEnvironment() == Triple::Itanium;
  }

  bool isWindowsCygwinEnvironment() const {
    return isOSWindows() && getEnvironment() == Triple::Cygnus;
  }

  bool isWindowsGNUEnvironment() const {
    return isOSWindows() && getEnvironment() == Triple::GNU;
  }

  /// Tests for either Cygwin or MinGW OS
  bool isOSCygMing() const {
    return isWindowsCygwinEnvironment() || isWindowsGNUEnvironment();
  }

  /// Is this a "Windows" OS targeting a "MSVCRT.dll" environment.
  bool isOSMSVCRT() const {
    return isWindowsMSVCEnvironment() || isWindowsGNUEnvironment() ||
           isWindowsItaniumEnvironment();
  }

  /// Tests whether the OS is NaCl (Native Client)
  bool isOSNaCl() const {
    return getOS() == Triple::NaCl;
  }

  /// Tests whether the OS is Linux.
  bool isOSLinux() const {
    return getOS() == Triple::Linux;
  }

  /// Tests whether the OS is kFreeBSD.
  bool isOSKFreeBSD() const {
    return getOS() == Triple::KFreeBSD;
  }

  /// Tests whether the OS is Hurd.
  bool isOSHurd() const {
    return getOS() == Triple::Hurd;
  }

  /// Tests whether the OS is WASI.
  bool isOSWASI() const {
    return getOS() == Triple::WASI;
  }

  /// Tests whether the OS is Emscripten.
  bool isOSEmscripten() const {
    return getOS() == Triple::Emscripten;
  }

  /// Tests whether the OS uses glibc.
  bool isOSGlibc() const {
    return (getOS() == Triple::Linux || getOS() == Triple::KFreeBSD ||
            getOS() == Triple::Hurd) &&
           !isAndroid();
  }

  /// Tests whether the OS is AIX.
  bool isOSAIX() const {
    return getOS() == Triple::AIX;
  }

  bool isOSSerenity() const {
    return getOS() == Triple::Serenity;
  }

  /// Tests whether the OS uses the ELF binary format.
  bool isOSBinFormatELF() const {
    return getObjectFormat() == Triple::ELF;
  }

  /// Tests whether the OS uses the COFF binary format.
  bool isOSBinFormatCOFF() const {
    return getObjectFormat() == Triple::COFF;
  }

  /// Tests whether the OS uses the GOFF binary format.
  bool isOSBinFormatGOFF() const { return getObjectFormat() == Triple::GOFF; }

  /// Tests whether the environment is MachO.
  bool isOSBinFormatMachO() const {
    return getObjectFormat() == Triple::MachO;
  }

  /// Tests whether the OS uses the Wasm binary format.
  bool isOSBinFormatWasm() const {
    return getObjectFormat() == Triple::Wasm;
  }

  /// Tests whether the OS uses the XCOFF binary format.
  bool isOSBinFormatXCOFF() const {
    return getObjectFormat() == Triple::XCOFF;
  }

  /// Tests whether the OS uses the DXContainer binary format.
  bool isOSBinFormatDXContainer() const {
    return getObjectFormat() == Triple::DXContainer;
  }

  /// Tests whether the target is the PS4 platform.
  bool isPS4() const {
    return getArch() == Triple::x86_64 &&
           getVendor() == Triple::SCEI &&
           getOS() == Triple::PS4;
  }

  /// Tests whether the target is the PS5 platform.
  bool isPS5() const {
    return getArch() == Triple::x86_64 &&
      getVendor() == Triple::SCEI &&
      getOS() == Triple::PS5;
  }

  /// Tests whether the target is the PS4 or PS5 platform.
  bool isPS() const { return isPS4() || isPS5(); }

  /// Tests whether the target is Android
  bool isAndroid() const { return getEnvironment() == Triple::Android; }

  bool isAndroidVersionLT(unsigned Major) const {
    assert(isAndroid() && "Not an Android triple!");

    VersionTuple Version = getEnvironmentVersion();

    // 64-bit targets did not exist before API level 21 (Lollipop).
    if (isArch64Bit() && Version.getMajor() < 21)
      return VersionTuple(21) < VersionTuple(Major);

    return Version < VersionTuple(Major);
  }

  /// Tests whether the environment is musl-libc
  bool isMusl() const {
    return getEnvironment() == Triple::Musl ||
           getEnvironment() == Triple::MuslEABI ||
           getEnvironment() == Triple::MuslEABIHF ||
           getEnvironment() == Triple::MuslX32 ||
           getEnvironment() == Triple::OpenHOS || isOSLiteOS();
  }

  /// Tests whether the target is OHOS
  /// LiteOS default enviroment is also OHOS, but omited on triple.
  bool isOHOSFamily() const { return isOpenHOS() || isOSLiteOS(); }

  bool isOpenHOS() const { return getEnvironment() == Triple::OpenHOS; }

  bool isOSLiteOS() const { return getOS() == Triple::LiteOS; }

  /// Tests whether the target is DXIL.
  bool isDXIL() const {
    return getArch() == Triple::dxil;
  }

  bool isShaderModelOS() const {
    return getOS() == Triple::ShaderModel;
  }

  bool isVulkanOS() const { return getOS() == Triple::Vulkan; }

  bool isShaderStageEnvironment() const {
    EnvironmentType Env = getEnvironment();
    return Env == Triple::Pixel || Env == Triple::Vertex ||
           Env == Triple::Geometry || Env == Triple::Hull ||
           Env == Triple::Domain || Env == Triple::Compute ||
           Env == Triple::Library || Env == Triple::RayGeneration ||
           Env == Triple::Intersection || Env == Triple::AnyHit ||
           Env == Triple::ClosestHit || Env == Triple::Miss ||
           Env == Triple::Callable || Env == Triple::Mesh ||
           Env == Triple::Amplification;
  }

  /// Tests whether the target is SPIR (32- or 64-bit).
  bool isSPIR() const {
    return getArch() == Triple::spir || getArch() == Triple::spir64;
  }

  /// Tests whether the target is SPIR-V (32/64-bit/Logical).
  bool isSPIRV() const {
    return getArch() == Triple::spirv32 || getArch() == Triple::spirv64 ||
           getArch() == Triple::spirv;
  }

  /// Tests whether the target is SPIR-V Logical
  bool isSPIRVLogical() const {
    return getArch() == Triple::spirv;
  }

  /// Tests whether the target is NVPTX (32- or 64-bit).
  bool isNVPTX() const {
    return getArch() == Triple::nvptx || getArch() == Triple::nvptx64;
  }

  /// Tests whether the target is AMDGCN
  bool isAMDGCN() const { return getArch() == Triple::amdgcn; }

  bool isAMDGPU() const {
    return getArch() == Triple::r600 || getArch() == Triple::amdgcn;
  }

  /// Tests whether the target is Thumb (little and big endian).
  bool isThumb() const {
    return getArch() == Triple::thumb || getArch() == Triple::thumbeb;
  }

  /// Tests whether the target is ARM (little and big endian).
  bool isARM() const {
    return getArch() == Triple::arm || getArch() == Triple::armeb;
  }

  /// Tests whether the target supports the EHABI exception
  /// handling standard.
  bool isTargetEHABICompatible() const {
    return (isARM() || isThumb()) &&
           (getEnvironment() == Triple::EABI ||
            getEnvironment() == Triple::GNUEABI ||
            getEnvironment() == Triple::GNUEABIT64 ||
            getEnvironment() == Triple::MuslEABI ||
            getEnvironment() == Triple::EABIHF ||
            getEnvironment() == Triple::GNUEABIHF ||
            getEnvironment() == Triple::GNUEABIHFT64 ||
            getEnvironment() == Triple::OpenHOS ||
            getEnvironment() == Triple::MuslEABIHF || isAndroid()) &&
           isOSBinFormatELF();
  }

  /// Tests whether the target is T32.
  bool isArmT32() const {
    switch (getSubArch()) {
    case Triple::ARMSubArch_v8m_baseline:
    case Triple::ARMSubArch_v7s:
    case Triple::ARMSubArch_v7k:
    case Triple::ARMSubArch_v7ve:
    case Triple::ARMSubArch_v6:
    case Triple::ARMSubArch_v6m:
    case Triple::ARMSubArch_v6k:
    case Triple::ARMSubArch_v6t2:
    case Triple::ARMSubArch_v5:
    case Triple::ARMSubArch_v5te:
    case Triple::ARMSubArch_v4t:
      return false;
    default:
      return true;
    }
  }

  /// Tests whether the target is an M-class.
  bool isArmMClass() const {
    switch (getSubArch()) {
    case Triple::ARMSubArch_v6m:
    case Triple::ARMSubArch_v7m:
    case Triple::ARMSubArch_v7em:
    case Triple::ARMSubArch_v8m_mainline:
    case Triple::ARMSubArch_v8m_baseline:
    case Triple::ARMSubArch_v8_1m_mainline:
      return true;
    default:
      return false;
    }
  }

  /// Tests whether the target is AArch64 (little and big endian).
  bool isAArch64() const {
    return getArch() == Triple::aarch64 || getArch() == Triple::aarch64_be ||
           getArch() == Triple::aarch64_32;
  }

  /// Tests whether the target is AArch64 and pointers are the size specified by
  /// \p PointerWidth.
  bool isAArch64(int PointerWidth) const {
    assert(PointerWidth == 64 || PointerWidth == 32);
    if (!isAArch64())
      return false;
    return getArch() == Triple::aarch64_32 ||
                   getEnvironment() == Triple::GNUILP32
               ? PointerWidth == 32
               : PointerWidth == 64;
  }

  /// Tests whether the target is 32-bit LoongArch.
  bool isLoongArch32() const { return getArch() == Triple::loongarch32; }

  /// Tests whether the target is 64-bit LoongArch.
  bool isLoongArch64() const { return getArch() == Triple::loongarch64; }

  /// Tests whether the target is LoongArch (32- and 64-bit).
  bool isLoongArch() const { return isLoongArch32() || isLoongArch64(); }

  /// Tests whether the target is MIPS 32-bit (little and big endian).
  bool isMIPS32() const {
    return getArch() == Triple::mips || getArch() == Triple::mipsel;
  }

  /// Tests whether the target is MIPS 64-bit (little and big endian).
  bool isMIPS64() const {
    return getArch() == Triple::mips64 || getArch() == Triple::mips64el;
  }

  /// Tests whether the target is MIPS (little and big endian, 32- or 64-bit).
  bool isMIPS() const {
    return isMIPS32() || isMIPS64();
  }

  /// Tests whether the target is PowerPC (32- or 64-bit LE or BE).
  bool isPPC() const {
    return getArch() == Triple::ppc || getArch() == Triple::ppc64 ||
           getArch() == Triple::ppcle || getArch() == Triple::ppc64le;
  }

  /// Tests whether the target is 32-bit PowerPC (little and big endian).
  bool isPPC32() const {
    return getArch() == Triple::ppc || getArch() == Triple::ppcle;
  }

  /// Tests whether the target is 64-bit PowerPC (little and big endian).
  bool isPPC64() const {
    return getArch() == Triple::ppc64 || getArch() == Triple::ppc64le;
  }

  /// Tests whether the target 64-bit PowerPC big endian ABI is ELFv2.
  bool isPPC64ELFv2ABI() const {
    return (getArch() == Triple::ppc64 &&
            ((getOS() == Triple::FreeBSD &&
              (getOSMajorVersion() >= 13 || getOSVersion().empty())) ||
             getOS() == Triple::OpenBSD || isMusl()));
  }

  /// Tests whether the target 32-bit PowerPC uses Secure PLT.
  bool isPPC32SecurePlt() const {
    return ((getArch() == Triple::ppc || getArch() == Triple::ppcle) &&
            ((getOS() == Triple::FreeBSD &&
              (getOSMajorVersion() >= 13 || getOSVersion().empty())) ||
             getOS() == Triple::NetBSD || getOS() == Triple::OpenBSD ||
             isMusl()));
  }

  /// Tests whether the target is 32-bit RISC-V.
  bool isRISCV32() const { return getArch() == Triple::riscv32; }

  /// Tests whether the target is 64-bit RISC-V.
  bool isRISCV64() const { return getArch() == Triple::riscv64; }

  /// Tests whether the target is RISC-V (32- and 64-bit).
  bool isRISCV() const { return isRISCV32() || isRISCV64(); }

  /// Tests whether the target is 32-bit SPARC (little and big endian).
  bool isSPARC32() const {
    return getArch() == Triple::sparc || getArch() == Triple::sparcel;
  }

  /// Tests whether the target is 64-bit SPARC (big endian).
  bool isSPARC64() const { return getArch() == Triple::sparcv9; }

  /// Tests whether the target is SPARC.
  bool isSPARC() const { return isSPARC32() || isSPARC64(); }

  /// Tests whether the target is SystemZ.
  bool isSystemZ() const {
    return getArch() == Triple::systemz;
  }

  /// Tests whether the target is x86 (32- or 64-bit).
  bool isX86() const {
    return getArch() == Triple::x86 || getArch() == Triple::x86_64;
  }

  /// Tests whether the target is VE
  bool isVE() const {
    return getArch() == Triple::ve;
  }

  /// Tests whether the target is wasm (32- and 64-bit).
  bool isWasm() const {
    return getArch() == Triple::wasm32 || getArch() == Triple::wasm64;
  }

  // Tests whether the target is CSKY
  bool isCSKY() const {
    return getArch() == Triple::csky;
  }

  /// Tests whether the target is the Apple "arm64e" AArch64 subarch.
  bool isArm64e() const {
    return getArch() == Triple::aarch64 &&
           getSubArch() == Triple::AArch64SubArch_arm64e;
  }

  /// Tests whether the target is X32.
  bool isX32() const {
    EnvironmentType Env = getEnvironment();
    return Env == Triple::GNUX32 || Env == Triple::MuslX32;
  }

  /// Tests whether the target is eBPF.
  bool isBPF() const {
    return getArch() == Triple::bpfel || getArch() == Triple::bpfeb;
  }

  /// Tests if the target forces 64-bit time_t on a 32-bit architecture.
  bool isTime64ABI() const {
    EnvironmentType Env = getEnvironment();
    return Env == Triple::GNUT64 || Env == Triple::GNUEABIT64 ||
           Env == Triple::GNUEABIHFT64;
  }

  /// Tests if the target forces hardfloat.
  bool isHardFloatABI() const {
    EnvironmentType Env = getEnvironment();
    return Env == llvm::Triple::GNUEABIHF ||
           Env == llvm::Triple::GNUEABIHFT64 ||
           Env == llvm::Triple::MuslEABIHF ||
           Env == llvm::Triple::EABIHF;
  }

  /// Tests whether the target supports comdat
  bool supportsCOMDAT() const {
    return !(isOSBinFormatMachO() || isOSBinFormatXCOFF() ||
             isOSBinFormatDXContainer());
  }

  /// Tests whether the target uses emulated TLS as default.
  ///
  /// Note: Android API level 29 (10) introduced ELF TLS.
  bool hasDefaultEmulatedTLS() const {
    return (isAndroid() && isAndroidVersionLT(29)) || isOSOpenBSD() ||
           isWindowsCygwinEnvironment() || isOHOSFamily();
  }

  /// True if the target supports both general-dynamic and TLSDESC, and TLSDESC
  /// is enabled by default.
  bool hasDefaultTLSDESC() const { return isAndroid() && isRISCV64(); }

  /// Tests whether the target uses -data-sections as default.
  bool hasDefaultDataSections() const {
    return isOSBinFormatXCOFF() || isWasm();
  }

  /// Tests if the environment supports dllimport/export annotations.
  bool hasDLLImportExport() const { return isOSWindows() || isPS(); }

  /// @}
  /// @name Mutators
  /// @{

  /// Set the architecture (first) component of the triple to a known type.
  void setArch(ArchType Kind, SubArchType SubArch = NoSubArch);

  /// Set the vendor (second) component of the triple to a known type.
  void setVendor(VendorType Kind);

  /// Set the operating system (third) component of the triple to a known type.
  void setOS(OSType Kind);

  /// Set the environment (fourth) component of the triple to a known type.
  void setEnvironment(EnvironmentType Kind);

  /// Set the object file format.
  void setObjectFormat(ObjectFormatType Kind);

  /// Set all components to the new triple \p Str.
  void setTriple(const Twine &Str);

  /// Set the architecture (first) component of the triple by name.
  void setArchName(StringRef Str);

  /// Set the vendor (second) component of the triple by name.
  void setVendorName(StringRef Str);

  /// Set the operating system (third) component of the triple by name.
  void setOSName(StringRef Str);

  /// Set the optional environment (fourth) component of the triple by name.
  void setEnvironmentName(StringRef Str);

  /// Set the operating system and optional environment components with a single
  /// string.
  void setOSAndEnvironmentName(StringRef Str);

  /// @}
  /// @name Helpers to build variants of a particular triple.
  /// @{

  /// Form a triple with a 32-bit variant of the current architecture.
  ///
  /// This can be used to move across "families" of architectures where useful.
  ///
  /// \returns A new triple with a 32-bit architecture or an unknown
  ///          architecture if no such variant can be found.
  llvm::Triple get32BitArchVariant() const;

  /// Form a triple with a 64-bit variant of the current architecture.
  ///
  /// This can be used to move across "families" of architectures where useful.
  ///
  /// \returns A new triple with a 64-bit architecture or an unknown
  ///          architecture if no such variant can be found.
  llvm::Triple get64BitArchVariant() const;

  /// Form a triple with a big endian variant of the current architecture.
  ///
  /// This can be used to move across "families" of architectures where useful.
  ///
  /// \returns A new triple with a big endian architecture or an unknown
  ///          architecture if no such variant can be found.
  llvm::Triple getBigEndianArchVariant() const;

  /// Form a triple with a little endian variant of the current architecture.
  ///
  /// This can be used to move across "families" of architectures where useful.
  ///
  /// \returns A new triple with a little endian architecture or an unknown
  ///          architecture if no such variant can be found.
  llvm::Triple getLittleEndianArchVariant() const;

  /// Tests whether the target triple is little endian.
  ///
  /// \returns true if the triple is little endian, false otherwise.
  bool isLittleEndian() const;

  /// Test whether target triples are compatible.
  bool isCompatibleWith(const Triple &Other) const;

  /// Merge target triples.
  std::string merge(const Triple &Other) const;

  /// Some platforms have different minimum supported OS versions that
  /// varies by the architecture specified in the triple. This function
  /// returns the minimum supported OS version for this triple if one an exists,
  /// or an invalid version tuple if this triple doesn't have one.
  VersionTuple getMinimumSupportedOSVersion() const;

  /// @}
  /// @name Static helpers for IDs.
  /// @{

  /// Get the canonical name for the \p Kind architecture.
  static StringRef getArchTypeName(ArchType Kind);

  /// Get the architecture name based on \p Kind and \p SubArch.
  static StringRef getArchName(ArchType Kind, SubArchType SubArch = NoSubArch);

  /// Get the "prefix" canonical name for the \p Kind architecture. This is the
  /// prefix used by the architecture specific builtins, and is suitable for
  /// passing to \see Intrinsic::getIntrinsicForClangBuiltin().
  ///
  /// \return - The architecture prefix, or 0 if none is defined.
  static StringRef getArchTypePrefix(ArchType Kind);

  /// Get the canonical name for the \p Kind vendor.
  static StringRef getVendorTypeName(VendorType Kind);

  /// Get the canonical name for the \p Kind operating system.
  static StringRef getOSTypeName(OSType Kind);

  /// Get the canonical name for the \p Kind environment.
  static StringRef getEnvironmentTypeName(EnvironmentType Kind);

  /// Get the name for the \p Object format.
  static StringRef getObjectFormatTypeName(ObjectFormatType ObjectFormat);

  /// @}
  /// @name Static helpers for converting alternate architecture names.
  /// @{

  /// The canonical type for the given LLVM architecture name (e.g., "x86").
  static ArchType getArchTypeForLLVMName(StringRef Str);

  /// @}

  /// Returns a canonicalized OS version number for the specified OS.
  static VersionTuple getCanonicalVersionForOS(OSType OSKind,
                                               const VersionTuple &Version);
};

} // End llvm namespace


#endif
