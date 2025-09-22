//===--- Distro.cpp - Linux distribution detection support ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Distro.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Threading.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

using namespace clang::driver;
using namespace clang;

static Distro::DistroType DetectOsRelease(llvm::vfs::FileSystem &VFS) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File =
      VFS.getBufferForFile("/etc/os-release");
  if (!File)
    File = VFS.getBufferForFile("/usr/lib/os-release");
  if (!File)
    return Distro::UnknownDistro;

  SmallVector<StringRef, 16> Lines;
  File.get()->getBuffer().split(Lines, "\n");
  Distro::DistroType Version = Distro::UnknownDistro;

  // Obviously this can be improved a lot.
  for (StringRef Line : Lines)
    if (Version == Distro::UnknownDistro && Line.starts_with("ID="))
      Version = llvm::StringSwitch<Distro::DistroType>(Line.substr(3))
                    .Case("alpine", Distro::AlpineLinux)
                    .Case("fedora", Distro::Fedora)
                    .Case("gentoo", Distro::Gentoo)
                    .Case("arch", Distro::ArchLinux)
                    // On SLES, /etc/os-release was introduced in SLES 11.
                    .Case("sles", Distro::OpenSUSE)
                    .Case("opensuse", Distro::OpenSUSE)
                    .Case("exherbo", Distro::Exherbo)
                    .Default(Distro::UnknownDistro);
  return Version;
}

static Distro::DistroType DetectLsbRelease(llvm::vfs::FileSystem &VFS) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File =
      VFS.getBufferForFile("/etc/lsb-release");
  if (!File)
    return Distro::UnknownDistro;

  SmallVector<StringRef, 16> Lines;
  File.get()->getBuffer().split(Lines, "\n");
  Distro::DistroType Version = Distro::UnknownDistro;

  for (StringRef Line : Lines)
    if (Version == Distro::UnknownDistro &&
        Line.starts_with("DISTRIB_CODENAME="))
      Version = llvm::StringSwitch<Distro::DistroType>(Line.substr(17))
                    .Case("hardy", Distro::UbuntuHardy)
                    .Case("intrepid", Distro::UbuntuIntrepid)
                    .Case("jaunty", Distro::UbuntuJaunty)
                    .Case("karmic", Distro::UbuntuKarmic)
                    .Case("lucid", Distro::UbuntuLucid)
                    .Case("maverick", Distro::UbuntuMaverick)
                    .Case("natty", Distro::UbuntuNatty)
                    .Case("oneiric", Distro::UbuntuOneiric)
                    .Case("precise", Distro::UbuntuPrecise)
                    .Case("quantal", Distro::UbuntuQuantal)
                    .Case("raring", Distro::UbuntuRaring)
                    .Case("saucy", Distro::UbuntuSaucy)
                    .Case("trusty", Distro::UbuntuTrusty)
                    .Case("utopic", Distro::UbuntuUtopic)
                    .Case("vivid", Distro::UbuntuVivid)
                    .Case("wily", Distro::UbuntuWily)
                    .Case("xenial", Distro::UbuntuXenial)
                    .Case("yakkety", Distro::UbuntuYakkety)
                    .Case("zesty", Distro::UbuntuZesty)
                    .Case("artful", Distro::UbuntuArtful)
                    .Case("bionic", Distro::UbuntuBionic)
                    .Case("cosmic", Distro::UbuntuCosmic)
                    .Case("disco", Distro::UbuntuDisco)
                    .Case("eoan", Distro::UbuntuEoan)
                    .Case("focal", Distro::UbuntuFocal)
                    .Case("groovy", Distro::UbuntuGroovy)
                    .Case("hirsute", Distro::UbuntuHirsute)
                    .Case("impish", Distro::UbuntuImpish)
                    .Case("jammy", Distro::UbuntuJammy)
                    .Case("kinetic", Distro::UbuntuKinetic)
                    .Case("lunar", Distro::UbuntuLunar)
                    .Case("mantic", Distro::UbuntuMantic)
                    .Case("noble", Distro::UbuntuNoble)
                    .Case("oracular", Distro::UbuntuOracular)
                    .Default(Distro::UnknownDistro);
  return Version;
}

static Distro::DistroType DetectDistro(llvm::vfs::FileSystem &VFS) {
  Distro::DistroType Version = Distro::UnknownDistro;

  // Newer freedesktop.org's compilant systemd-based systems
  // should provide /etc/os-release or /usr/lib/os-release.
  Version = DetectOsRelease(VFS);
  if (Version != Distro::UnknownDistro)
    return Version;

  // Older systems might provide /etc/lsb-release.
  Version = DetectLsbRelease(VFS);
  if (Version != Distro::UnknownDistro)
    return Version;

  // Otherwise try some distro-specific quirks for Red Hat...
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File =
      VFS.getBufferForFile("/etc/redhat-release");

  if (File) {
    StringRef Data = File.get()->getBuffer();
    if (Data.starts_with("Fedora release"))
      return Distro::Fedora;
    if (Data.starts_with("Red Hat Enterprise Linux") ||
        Data.starts_with("CentOS") || Data.starts_with("Scientific Linux")) {
      if (Data.contains("release 7"))
        return Distro::RHEL7;
      else if (Data.contains("release 6"))
        return Distro::RHEL6;
      else if (Data.contains("release 5"))
        return Distro::RHEL5;
    }
    return Distro::UnknownDistro;
  }

  // ...for Debian
  File = VFS.getBufferForFile("/etc/debian_version");
  if (File) {
    StringRef Data = File.get()->getBuffer();
    // Contents: < major.minor > or < codename/sid >
    int MajorVersion;
    if (!Data.split('.').first.getAsInteger(10, MajorVersion)) {
      switch (MajorVersion) {
      case 5:
        return Distro::DebianLenny;
      case 6:
        return Distro::DebianSqueeze;
      case 7:
        return Distro::DebianWheezy;
      case 8:
        return Distro::DebianJessie;
      case 9:
        return Distro::DebianStretch;
      case 10:
        return Distro::DebianBuster;
      case 11:
        return Distro::DebianBullseye;
      case 12:
        return Distro::DebianBookworm;
      case 13:
        return Distro::DebianTrixie;
      default:
        return Distro::UnknownDistro;
      }
    }
    return llvm::StringSwitch<Distro::DistroType>(Data.split("\n").first)
        .Case("squeeze/sid", Distro::DebianSqueeze)
        .Case("wheezy/sid", Distro::DebianWheezy)
        .Case("jessie/sid", Distro::DebianJessie)
        .Case("stretch/sid", Distro::DebianStretch)
        .Case("buster/sid", Distro::DebianBuster)
        .Case("bullseye/sid", Distro::DebianBullseye)
        .Case("bookworm/sid", Distro::DebianBookworm)
        .Case("trixie/sid", Distro::DebianTrixie)
        .Default(Distro::UnknownDistro);
  }

  // ...for SUSE
  File = VFS.getBufferForFile("/etc/SuSE-release");
  if (File) {
    StringRef Data = File.get()->getBuffer();
    SmallVector<StringRef, 8> Lines;
    Data.split(Lines, "\n");
    for (const StringRef &Line : Lines) {
      if (!Line.trim().starts_with("VERSION"))
        continue;
      std::pair<StringRef, StringRef> SplitLine = Line.split('=');
      // Old versions have split VERSION and PATCHLEVEL
      // Newer versions use VERSION = x.y
      std::pair<StringRef, StringRef> SplitVer =
          SplitLine.second.trim().split('.');
      int Version;

      // OpenSUSE/SLES 10 and older are not supported and not compatible
      // with our rules, so just treat them as Distro::UnknownDistro.
      if (!SplitVer.first.getAsInteger(10, Version) && Version > 10)
        return Distro::OpenSUSE;
      return Distro::UnknownDistro;
    }
    return Distro::UnknownDistro;
  }

  // ...and others.
  if (VFS.exists("/etc/gentoo-release"))
    return Distro::Gentoo;

  return Distro::UnknownDistro;
}

static Distro::DistroType GetDistro(llvm::vfs::FileSystem &VFS,
                                    const llvm::Triple &TargetOrHost) {
  // If we don't target Linux, no need to check the distro. This saves a few
  // OS calls.
  if (!TargetOrHost.isOSLinux())
    return Distro::UnknownDistro;

  // True if we're backed by a real file system.
  const bool onRealFS = (llvm::vfs::getRealFileSystem() == &VFS);

  // If the host is not running Linux, and we're backed by a real file
  // system, no need to check the distro. This is the case where someone
  // is cross-compiling from BSD or Windows to Linux, and it would be
  // meaningless to try to figure out the "distro" of the non-Linux host.
  llvm::Triple HostTriple(llvm::sys::getProcessTriple());
  if (!HostTriple.isOSLinux() && onRealFS)
    return Distro::UnknownDistro;

  if (onRealFS) {
    // If we're backed by a real file system, perform
    // the detection only once and save the result.
    static Distro::DistroType LinuxDistro = DetectDistro(VFS);
    return LinuxDistro;
  }
  // This is mostly for passing tests which uses llvm::vfs::InMemoryFileSystem,
  // which is not "real".
  return DetectDistro(VFS);
}

Distro::Distro(llvm::vfs::FileSystem &VFS, const llvm::Triple &TargetOrHost)
    : DistroVal(GetDistro(VFS, TargetOrHost)) {}
