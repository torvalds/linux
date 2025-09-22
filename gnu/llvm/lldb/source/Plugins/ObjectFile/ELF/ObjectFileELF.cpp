//===-- ObjectFileELF.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ObjectFileELF.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <unordered_map>

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Progress.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/LZMA.h"
#include "lldb/Symbol/DWARFCallFrameInfo.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/FileSpecList.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RangeMap.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/Timer.h"
#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/Decompressor.h"
#include "llvm/Support/ARMBuildAttributes.h"
#include "llvm/Support/CRC.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/MipsABIFlags.h"

#define CASE_AND_STREAM(s, def, width)                                         \
  case def:                                                                    \
    s->Printf("%-*s", width, #def);                                            \
    break;

using namespace lldb;
using namespace lldb_private;
using namespace elf;
using namespace llvm::ELF;

LLDB_PLUGIN_DEFINE(ObjectFileELF)

// ELF note owner definitions
static const char *const LLDB_NT_OWNER_FREEBSD = "FreeBSD";
static const char *const LLDB_NT_OWNER_GNU = "GNU";
static const char *const LLDB_NT_OWNER_NETBSD = "NetBSD";
static const char *const LLDB_NT_OWNER_NETBSDCORE = "NetBSD-CORE";
static const char *const LLDB_NT_OWNER_OPENBSD = "OpenBSD";
static const char *const LLDB_NT_OWNER_ANDROID = "Android";
static const char *const LLDB_NT_OWNER_CORE = "CORE";
static const char *const LLDB_NT_OWNER_LINUX = "LINUX";

// ELF note type definitions
static const elf_word LLDB_NT_FREEBSD_ABI_TAG = 0x01;
static const elf_word LLDB_NT_FREEBSD_ABI_SIZE = 4;

static const elf_word LLDB_NT_GNU_ABI_TAG = 0x01;
static const elf_word LLDB_NT_GNU_ABI_SIZE = 16;

static const elf_word LLDB_NT_GNU_BUILD_ID_TAG = 0x03;

static const elf_word LLDB_NT_NETBSD_IDENT_TAG = 1;
static const elf_word LLDB_NT_NETBSD_IDENT_DESCSZ = 4;
static const elf_word LLDB_NT_NETBSD_IDENT_NAMESZ = 7;
static const elf_word LLDB_NT_NETBSD_PROCINFO = 1;

// GNU ABI note OS constants
static const elf_word LLDB_NT_GNU_ABI_OS_LINUX = 0x00;
static const elf_word LLDB_NT_GNU_ABI_OS_HURD = 0x01;
static const elf_word LLDB_NT_GNU_ABI_OS_SOLARIS = 0x02;

namespace {

//===----------------------------------------------------------------------===//
/// \class ELFRelocation
/// Generic wrapper for ELFRel and ELFRela.
///
/// This helper class allows us to parse both ELFRel and ELFRela relocation
/// entries in a generic manner.
class ELFRelocation {
public:
  /// Constructs an ELFRelocation entry with a personality as given by @p
  /// type.
  ///
  /// \param type Either DT_REL or DT_RELA.  Any other value is invalid.
  ELFRelocation(unsigned type);

  ~ELFRelocation();

  bool Parse(const lldb_private::DataExtractor &data, lldb::offset_t *offset);

  static unsigned RelocType32(const ELFRelocation &rel);

  static unsigned RelocType64(const ELFRelocation &rel);

  static unsigned RelocSymbol32(const ELFRelocation &rel);

  static unsigned RelocSymbol64(const ELFRelocation &rel);

  static elf_addr RelocOffset32(const ELFRelocation &rel);

  static elf_addr RelocOffset64(const ELFRelocation &rel);

  static elf_sxword RelocAddend32(const ELFRelocation &rel);

  static elf_sxword RelocAddend64(const ELFRelocation &rel);

  bool IsRela() { return (reloc.is<ELFRela *>()); }

private:
  typedef llvm::PointerUnion<ELFRel *, ELFRela *> RelocUnion;

  RelocUnion reloc;
};
} // end anonymous namespace

ELFRelocation::ELFRelocation(unsigned type) {
  if (type == DT_REL || type == SHT_REL)
    reloc = new ELFRel();
  else if (type == DT_RELA || type == SHT_RELA)
    reloc = new ELFRela();
  else {
    assert(false && "unexpected relocation type");
    reloc = static_cast<ELFRel *>(nullptr);
  }
}

ELFRelocation::~ELFRelocation() {
  if (reloc.is<ELFRel *>())
    delete reloc.get<ELFRel *>();
  else
    delete reloc.get<ELFRela *>();
}

bool ELFRelocation::Parse(const lldb_private::DataExtractor &data,
                          lldb::offset_t *offset) {
  if (reloc.is<ELFRel *>())
    return reloc.get<ELFRel *>()->Parse(data, offset);
  else
    return reloc.get<ELFRela *>()->Parse(data, offset);
}

unsigned ELFRelocation::RelocType32(const ELFRelocation &rel) {
  if (rel.reloc.is<ELFRel *>())
    return ELFRel::RelocType32(*rel.reloc.get<ELFRel *>());
  else
    return ELFRela::RelocType32(*rel.reloc.get<ELFRela *>());
}

unsigned ELFRelocation::RelocType64(const ELFRelocation &rel) {
  if (rel.reloc.is<ELFRel *>())
    return ELFRel::RelocType64(*rel.reloc.get<ELFRel *>());
  else
    return ELFRela::RelocType64(*rel.reloc.get<ELFRela *>());
}

unsigned ELFRelocation::RelocSymbol32(const ELFRelocation &rel) {
  if (rel.reloc.is<ELFRel *>())
    return ELFRel::RelocSymbol32(*rel.reloc.get<ELFRel *>());
  else
    return ELFRela::RelocSymbol32(*rel.reloc.get<ELFRela *>());
}

unsigned ELFRelocation::RelocSymbol64(const ELFRelocation &rel) {
  if (rel.reloc.is<ELFRel *>())
    return ELFRel::RelocSymbol64(*rel.reloc.get<ELFRel *>());
  else
    return ELFRela::RelocSymbol64(*rel.reloc.get<ELFRela *>());
}

elf_addr ELFRelocation::RelocOffset32(const ELFRelocation &rel) {
  if (rel.reloc.is<ELFRel *>())
    return rel.reloc.get<ELFRel *>()->r_offset;
  else
    return rel.reloc.get<ELFRela *>()->r_offset;
}

elf_addr ELFRelocation::RelocOffset64(const ELFRelocation &rel) {
  if (rel.reloc.is<ELFRel *>())
    return rel.reloc.get<ELFRel *>()->r_offset;
  else
    return rel.reloc.get<ELFRela *>()->r_offset;
}

elf_sxword ELFRelocation::RelocAddend32(const ELFRelocation &rel) {
  if (rel.reloc.is<ELFRel *>())
    return 0;
  else
    return rel.reloc.get<ELFRela *>()->r_addend;
}

elf_sxword  ELFRelocation::RelocAddend64(const ELFRelocation &rel) {
  if (rel.reloc.is<ELFRel *>())
    return 0;
  else
    return rel.reloc.get<ELFRela *>()->r_addend;
}

static user_id_t SegmentID(size_t PHdrIndex) {
  return ~user_id_t(PHdrIndex);
}

bool ELFNote::Parse(const DataExtractor &data, lldb::offset_t *offset) {
  // Read all fields.
  if (data.GetU32(offset, &n_namesz, 3) == nullptr)
    return false;

  // The name field is required to be nul-terminated, and n_namesz includes the
  // terminating nul in observed implementations (contrary to the ELF-64 spec).
  // A special case is needed for cores generated by some older Linux versions,
  // which write a note named "CORE" without a nul terminator and n_namesz = 4.
  if (n_namesz == 4) {
    char buf[4];
    if (data.ExtractBytes(*offset, 4, data.GetByteOrder(), buf) != 4)
      return false;
    if (strncmp(buf, "CORE", 4) == 0) {
      n_name = "CORE";
      *offset += 4;
      return true;
    }
  }

  const char *cstr = data.GetCStr(offset, llvm::alignTo(n_namesz, 4));
  if (cstr == nullptr) {
    Log *log = GetLog(LLDBLog::Symbols);
    LLDB_LOGF(log, "Failed to parse note name lacking nul terminator");

    return false;
  }
  n_name = cstr;
  return true;
}

static uint32_t mipsVariantFromElfFlags (const elf::ELFHeader &header) {
  const uint32_t mips_arch = header.e_flags & llvm::ELF::EF_MIPS_ARCH;
  uint32_t endian = header.e_ident[EI_DATA];
  uint32_t arch_variant = ArchSpec::eMIPSSubType_unknown;
  uint32_t fileclass = header.e_ident[EI_CLASS];

  // If there aren't any elf flags available (e.g core elf file) then return
  // default
  // 32 or 64 bit arch (without any architecture revision) based on object file's class.
  if (header.e_type == ET_CORE) {
    switch (fileclass) {
    case llvm::ELF::ELFCLASS32:
      return (endian == ELFDATA2LSB) ? ArchSpec::eMIPSSubType_mips32el
                                     : ArchSpec::eMIPSSubType_mips32;
    case llvm::ELF::ELFCLASS64:
      return (endian == ELFDATA2LSB) ? ArchSpec::eMIPSSubType_mips64el
                                     : ArchSpec::eMIPSSubType_mips64;
    default:
      return arch_variant;
    }
  }

  switch (mips_arch) {
  case llvm::ELF::EF_MIPS_ARCH_1:
  case llvm::ELF::EF_MIPS_ARCH_2:
  case llvm::ELF::EF_MIPS_ARCH_32:
    return (endian == ELFDATA2LSB) ? ArchSpec::eMIPSSubType_mips32el
                                   : ArchSpec::eMIPSSubType_mips32;
  case llvm::ELF::EF_MIPS_ARCH_32R2:
    return (endian == ELFDATA2LSB) ? ArchSpec::eMIPSSubType_mips32r2el
                                   : ArchSpec::eMIPSSubType_mips32r2;
  case llvm::ELF::EF_MIPS_ARCH_32R6:
    return (endian == ELFDATA2LSB) ? ArchSpec::eMIPSSubType_mips32r6el
                                   : ArchSpec::eMIPSSubType_mips32r6;
  case llvm::ELF::EF_MIPS_ARCH_3:
  case llvm::ELF::EF_MIPS_ARCH_4:
  case llvm::ELF::EF_MIPS_ARCH_5:
  case llvm::ELF::EF_MIPS_ARCH_64:
    return (endian == ELFDATA2LSB) ? ArchSpec::eMIPSSubType_mips64el
                                   : ArchSpec::eMIPSSubType_mips64;
  case llvm::ELF::EF_MIPS_ARCH_64R2:
    return (endian == ELFDATA2LSB) ? ArchSpec::eMIPSSubType_mips64r2el
                                   : ArchSpec::eMIPSSubType_mips64r2;
  case llvm::ELF::EF_MIPS_ARCH_64R6:
    return (endian == ELFDATA2LSB) ? ArchSpec::eMIPSSubType_mips64r6el
                                   : ArchSpec::eMIPSSubType_mips64r6;
  default:
    break;
  }

  return arch_variant;
}

static uint32_t riscvVariantFromElfFlags(const elf::ELFHeader &header) {
  uint32_t fileclass = header.e_ident[EI_CLASS];
  switch (fileclass) {
  case llvm::ELF::ELFCLASS32:
    return ArchSpec::eRISCVSubType_riscv32;
  case llvm::ELF::ELFCLASS64:
    return ArchSpec::eRISCVSubType_riscv64;
  default:
    return ArchSpec::eRISCVSubType_unknown;
  }
}

static uint32_t ppc64VariantFromElfFlags(const elf::ELFHeader &header) {
  uint32_t endian = header.e_ident[EI_DATA];
  if (endian == ELFDATA2LSB)
    return ArchSpec::eCore_ppc64le_generic;
  else
    return ArchSpec::eCore_ppc64_generic;
}

static uint32_t loongarchVariantFromElfFlags(const elf::ELFHeader &header) {
  uint32_t fileclass = header.e_ident[EI_CLASS];
  switch (fileclass) {
  case llvm::ELF::ELFCLASS32:
    return ArchSpec::eLoongArchSubType_loongarch32;
  case llvm::ELF::ELFCLASS64:
    return ArchSpec::eLoongArchSubType_loongarch64;
  default:
    return ArchSpec::eLoongArchSubType_unknown;
  }
}

static uint32_t subTypeFromElfHeader(const elf::ELFHeader &header) {
  if (header.e_machine == llvm::ELF::EM_MIPS)
    return mipsVariantFromElfFlags(header);
  else if (header.e_machine == llvm::ELF::EM_PPC64)
    return ppc64VariantFromElfFlags(header);
  else if (header.e_machine == llvm::ELF::EM_RISCV)
    return riscvVariantFromElfFlags(header);
  else if (header.e_machine == llvm::ELF::EM_LOONGARCH)
    return loongarchVariantFromElfFlags(header);

  return LLDB_INVALID_CPUTYPE;
}

char ObjectFileELF::ID;

// Arbitrary constant used as UUID prefix for core files.
const uint32_t ObjectFileELF::g_core_uuid_magic(0xE210C);

// Static methods.
void ObjectFileELF::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                CreateMemoryInstance, GetModuleSpecifications);
}

void ObjectFileELF::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ObjectFile *ObjectFileELF::CreateInstance(const lldb::ModuleSP &module_sp,
                                          DataBufferSP data_sp,
                                          lldb::offset_t data_offset,
                                          const lldb_private::FileSpec *file,
                                          lldb::offset_t file_offset,
                                          lldb::offset_t length) {
  bool mapped_writable = false;
  if (!data_sp) {
    data_sp = MapFileDataWritable(*file, length, file_offset);
    if (!data_sp)
      return nullptr;
    data_offset = 0;
    mapped_writable = true;
  }

  assert(data_sp);

  if (data_sp->GetByteSize() <= (llvm::ELF::EI_NIDENT + data_offset))
    return nullptr;

  const uint8_t *magic = data_sp->GetBytes() + data_offset;
  if (!ELFHeader::MagicBytesMatch(magic))
    return nullptr;

  // Update the data to contain the entire file if it doesn't already
  if (data_sp->GetByteSize() < length) {
    data_sp = MapFileDataWritable(*file, length, file_offset);
    if (!data_sp)
      return nullptr;
    data_offset = 0;
    mapped_writable = true;
    magic = data_sp->GetBytes();
  }

  // If we didn't map the data as writable take ownership of the buffer.
  if (!mapped_writable) {
    data_sp = std::make_shared<DataBufferHeap>(data_sp->GetBytes(),
                                               data_sp->GetByteSize());
    data_offset = 0;
    magic = data_sp->GetBytes();
  }

  unsigned address_size = ELFHeader::AddressSizeInBytes(magic);
  if (address_size == 4 || address_size == 8) {
    std::unique_ptr<ObjectFileELF> objfile_up(new ObjectFileELF(
        module_sp, data_sp, data_offset, file, file_offset, length));
    ArchSpec spec = objfile_up->GetArchitecture();
    if (spec && objfile_up->SetModulesArchitecture(spec))
      return objfile_up.release();
  }

  return nullptr;
}

ObjectFile *ObjectFileELF::CreateMemoryInstance(
    const lldb::ModuleSP &module_sp, WritableDataBufferSP data_sp,
    const lldb::ProcessSP &process_sp, lldb::addr_t header_addr) {
  if (data_sp && data_sp->GetByteSize() > (llvm::ELF::EI_NIDENT)) {
    const uint8_t *magic = data_sp->GetBytes();
    if (ELFHeader::MagicBytesMatch(magic)) {
      unsigned address_size = ELFHeader::AddressSizeInBytes(magic);
      if (address_size == 4 || address_size == 8) {
        std::unique_ptr<ObjectFileELF> objfile_up(
            new ObjectFileELF(module_sp, data_sp, process_sp, header_addr));
        ArchSpec spec = objfile_up->GetArchitecture();
        if (spec && objfile_up->SetModulesArchitecture(spec))
          return objfile_up.release();
      }
    }
  }
  return nullptr;
}

bool ObjectFileELF::MagicBytesMatch(DataBufferSP &data_sp,
                                    lldb::addr_t data_offset,
                                    lldb::addr_t data_length) {
  if (data_sp &&
      data_sp->GetByteSize() > (llvm::ELF::EI_NIDENT + data_offset)) {
    const uint8_t *magic = data_sp->GetBytes() + data_offset;
    return ELFHeader::MagicBytesMatch(magic);
  }
  return false;
}

static uint32_t calc_crc32(uint32_t init, const DataExtractor &data) {
  return llvm::crc32(init,
                     llvm::ArrayRef(data.GetDataStart(), data.GetByteSize()));
}

uint32_t ObjectFileELF::CalculateELFNotesSegmentsCRC32(
    const ProgramHeaderColl &program_headers, DataExtractor &object_data) {

  uint32_t core_notes_crc = 0;

  for (const ELFProgramHeader &H : program_headers) {
    if (H.p_type == llvm::ELF::PT_NOTE) {
      const elf_off ph_offset = H.p_offset;
      const size_t ph_size = H.p_filesz;

      DataExtractor segment_data;
      if (segment_data.SetData(object_data, ph_offset, ph_size) != ph_size) {
        // The ELF program header contained incorrect data, probably corefile
        // is incomplete or corrupted.
        break;
      }

      core_notes_crc = calc_crc32(core_notes_crc, segment_data);
    }
  }

  return core_notes_crc;
}

static const char *OSABIAsCString(unsigned char osabi_byte) {
#define _MAKE_OSABI_CASE(x)                                                    \
  case x:                                                                      \
    return #x
  switch (osabi_byte) {
    _MAKE_OSABI_CASE(ELFOSABI_NONE);
    _MAKE_OSABI_CASE(ELFOSABI_HPUX);
    _MAKE_OSABI_CASE(ELFOSABI_NETBSD);
    _MAKE_OSABI_CASE(ELFOSABI_GNU);
    _MAKE_OSABI_CASE(ELFOSABI_HURD);
    _MAKE_OSABI_CASE(ELFOSABI_SOLARIS);
    _MAKE_OSABI_CASE(ELFOSABI_AIX);
    _MAKE_OSABI_CASE(ELFOSABI_IRIX);
    _MAKE_OSABI_CASE(ELFOSABI_FREEBSD);
    _MAKE_OSABI_CASE(ELFOSABI_TRU64);
    _MAKE_OSABI_CASE(ELFOSABI_MODESTO);
    _MAKE_OSABI_CASE(ELFOSABI_OPENBSD);
    _MAKE_OSABI_CASE(ELFOSABI_OPENVMS);
    _MAKE_OSABI_CASE(ELFOSABI_NSK);
    _MAKE_OSABI_CASE(ELFOSABI_AROS);
    _MAKE_OSABI_CASE(ELFOSABI_FENIXOS);
    _MAKE_OSABI_CASE(ELFOSABI_C6000_ELFABI);
    _MAKE_OSABI_CASE(ELFOSABI_C6000_LINUX);
    _MAKE_OSABI_CASE(ELFOSABI_ARM);
    _MAKE_OSABI_CASE(ELFOSABI_STANDALONE);
  default:
    return "<unknown-osabi>";
  }
#undef _MAKE_OSABI_CASE
}

//
// WARNING : This function is being deprecated
// It's functionality has moved to ArchSpec::SetArchitecture This function is
// only being kept to validate the move.
//
// TODO : Remove this function
static bool GetOsFromOSABI(unsigned char osabi_byte,
                           llvm::Triple::OSType &ostype) {
  switch (osabi_byte) {
  case ELFOSABI_AIX:
    ostype = llvm::Triple::OSType::AIX;
    break;
  case ELFOSABI_FREEBSD:
    ostype = llvm::Triple::OSType::FreeBSD;
    break;
  case ELFOSABI_GNU:
    ostype = llvm::Triple::OSType::Linux;
    break;
  case ELFOSABI_NETBSD:
    ostype = llvm::Triple::OSType::NetBSD;
    break;
  case ELFOSABI_OPENBSD:
    ostype = llvm::Triple::OSType::OpenBSD;
    break;
  case ELFOSABI_SOLARIS:
    ostype = llvm::Triple::OSType::Solaris;
    break;
  default:
    ostype = llvm::Triple::OSType::UnknownOS;
  }
  return ostype != llvm::Triple::OSType::UnknownOS;
}

size_t ObjectFileELF::GetModuleSpecifications(
    const lldb_private::FileSpec &file, lldb::DataBufferSP &data_sp,
    lldb::offset_t data_offset, lldb::offset_t file_offset,
    lldb::offset_t length, lldb_private::ModuleSpecList &specs) {
  Log *log = GetLog(LLDBLog::Modules);

  const size_t initial_count = specs.GetSize();

  if (ObjectFileELF::MagicBytesMatch(data_sp, 0, data_sp->GetByteSize())) {
    DataExtractor data;
    data.SetData(data_sp);
    elf::ELFHeader header;
    lldb::offset_t header_offset = data_offset;
    if (header.Parse(data, &header_offset)) {
      if (data_sp) {
        ModuleSpec spec(file);
        // In Android API level 23 and above, bionic dynamic linker is able to
        // load .so file directly from zip file. In that case, .so file is
        // page aligned and uncompressed, and this module spec should retain the
        // .so file offset and file size to pass through the information from
        // lldb-server to LLDB. For normal file, file_offset should be 0,
        // length should be the size of the file.
        spec.SetObjectOffset(file_offset);
        spec.SetObjectSize(length);

        const uint32_t sub_type = subTypeFromElfHeader(header);
        spec.GetArchitecture().SetArchitecture(
            eArchTypeELF, header.e_machine, sub_type, header.e_ident[EI_OSABI]);

        if (spec.GetArchitecture().IsValid()) {
          llvm::Triple::OSType ostype;
          llvm::Triple::VendorType vendor;
          llvm::Triple::OSType spec_ostype =
              spec.GetArchitecture().GetTriple().getOS();

          LLDB_LOGF(log, "ObjectFileELF::%s file '%s' module OSABI: %s",
                    __FUNCTION__, file.GetPath().c_str(),
                    OSABIAsCString(header.e_ident[EI_OSABI]));

          // SetArchitecture should have set the vendor to unknown
          vendor = spec.GetArchitecture().GetTriple().getVendor();
          assert(vendor == llvm::Triple::UnknownVendor);
          UNUSED_IF_ASSERT_DISABLED(vendor);

          //
          // Validate it is ok to remove GetOsFromOSABI
          GetOsFromOSABI(header.e_ident[EI_OSABI], ostype);
          assert(spec_ostype == ostype);
          if (spec_ostype != llvm::Triple::OSType::UnknownOS) {
            LLDB_LOGF(log,
                      "ObjectFileELF::%s file '%s' set ELF module OS type "
                      "from ELF header OSABI.",
                      __FUNCTION__, file.GetPath().c_str());
          }

          // When ELF file does not contain GNU build ID, the later code will
          // calculate CRC32 with this data_sp file_offset and length. It is
          // important for Android zip .so file, which is a slice of a file,
          // to not access the outside of the file slice range.
          if (data_sp->GetByteSize() < length)
            data_sp = MapFileData(file, length, file_offset);
          if (data_sp)
            data.SetData(data_sp);
          // In case there is header extension in the section #0, the header we
          // parsed above could have sentinel values for e_phnum, e_shnum, and
          // e_shstrndx.  In this case we need to reparse the header with a
          // bigger data source to get the actual values.
          if (header.HasHeaderExtension()) {
            lldb::offset_t header_offset = data_offset;
            header.Parse(data, &header_offset);
          }

          uint32_t gnu_debuglink_crc = 0;
          std::string gnu_debuglink_file;
          SectionHeaderColl section_headers;
          lldb_private::UUID &uuid = spec.GetUUID();

          GetSectionHeaderInfo(section_headers, data, header, uuid,
                               gnu_debuglink_file, gnu_debuglink_crc,
                               spec.GetArchitecture());

          llvm::Triple &spec_triple = spec.GetArchitecture().GetTriple();

          LLDB_LOGF(log,
                    "ObjectFileELF::%s file '%s' module set to triple: %s "
                    "(architecture %s)",
                    __FUNCTION__, file.GetPath().c_str(),
                    spec_triple.getTriple().c_str(),
                    spec.GetArchitecture().GetArchitectureName());

          if (!uuid.IsValid()) {
            uint32_t core_notes_crc = 0;

            if (!gnu_debuglink_crc) {
              LLDB_SCOPED_TIMERF(
                  "Calculating module crc32 %s with size %" PRIu64 " KiB",
                  file.GetFilename().AsCString(),
                  (length - file_offset) / 1024);

              // For core files - which usually don't happen to have a
              // gnu_debuglink, and are pretty bulky - calculating whole
              // contents crc32 would be too much of luxury.  Thus we will need
              // to fallback to something simpler.
              if (header.e_type == llvm::ELF::ET_CORE) {
                ProgramHeaderColl program_headers;
                GetProgramHeaderInfo(program_headers, data, header);

                core_notes_crc =
                    CalculateELFNotesSegmentsCRC32(program_headers, data);
              } else {
                gnu_debuglink_crc = calc_crc32(0, data);
              }
            }
            using u32le = llvm::support::ulittle32_t;
            if (gnu_debuglink_crc) {
              // Use 4 bytes of crc from the .gnu_debuglink section.
              u32le data(gnu_debuglink_crc);
              uuid = UUID(&data, sizeof(data));
            } else if (core_notes_crc) {
              // Use 8 bytes - first 4 bytes for *magic* prefix, mainly to make
              // it look different form .gnu_debuglink crc followed by 4 bytes
              // of note segments crc.
              u32le data[] = {u32le(g_core_uuid_magic), u32le(core_notes_crc)};
              uuid = UUID(data, sizeof(data));
            }
          }

          specs.Append(spec);
        }
      }
    }
  }

  return specs.GetSize() - initial_count;
}

// ObjectFile protocol

ObjectFileELF::ObjectFileELF(const lldb::ModuleSP &module_sp,
                             DataBufferSP data_sp, lldb::offset_t data_offset,
                             const FileSpec *file, lldb::offset_t file_offset,
                             lldb::offset_t length)
    : ObjectFile(module_sp, file, file_offset, length, data_sp, data_offset) {
  if (file)
    m_file = *file;
}

ObjectFileELF::ObjectFileELF(const lldb::ModuleSP &module_sp,
                             DataBufferSP header_data_sp,
                             const lldb::ProcessSP &process_sp,
                             addr_t header_addr)
    : ObjectFile(module_sp, process_sp, header_addr, header_data_sp) {}

bool ObjectFileELF::IsExecutable() const {
  return ((m_header.e_type & ET_EXEC) != 0) || (m_header.e_entry != 0);
}

bool ObjectFileELF::SetLoadAddress(Target &target, lldb::addr_t value,
                                   bool value_is_offset) {
  ModuleSP module_sp = GetModule();
  if (module_sp) {
    size_t num_loaded_sections = 0;
    SectionList *section_list = GetSectionList();
    if (section_list) {
      if (!value_is_offset) {
        addr_t base = GetBaseAddress().GetFileAddress();
        if (base == LLDB_INVALID_ADDRESS)
          return false;
        value -= base;
      }

      const size_t num_sections = section_list->GetSize();
      size_t sect_idx = 0;

      for (sect_idx = 0; sect_idx < num_sections; ++sect_idx) {
        // Iterate through the object file sections to find all of the sections
        // that have SHF_ALLOC in their flag bits.
        SectionSP section_sp(section_list->GetSectionAtIndex(sect_idx));

        // PT_TLS segments can have the same p_vaddr and p_paddr as other
        // PT_LOAD segments so we shouldn't load them. If we do load them, then
        // the SectionLoadList will incorrectly fill in the instance variable
        // SectionLoadList::m_addr_to_sect with the same address as a PT_LOAD
        // segment and we won't be able to resolve addresses in the PT_LOAD
        // segment whose p_vaddr entry matches that of the PT_TLS. Any variables
        // that appear in the PT_TLS segments get resolved by the DWARF
        // expressions. If this ever changes we will need to fix all object
        // file plug-ins, but until then, we don't want PT_TLS segments to
        // remove the entry from SectionLoadList::m_addr_to_sect when we call
        // SetSectionLoadAddress() below.
        if (section_sp->IsThreadSpecific())
          continue;
        if (section_sp->Test(SHF_ALLOC) ||
            section_sp->GetType() == eSectionTypeContainer) {
          lldb::addr_t load_addr = section_sp->GetFileAddress();
          // We don't want to update the load address of a section with type
          // eSectionTypeAbsoluteAddress as they already have the absolute load
          // address already specified
          if (section_sp->GetType() != eSectionTypeAbsoluteAddress)
            load_addr += value;

          // On 32-bit systems the load address have to fit into 4 bytes. The
          // rest of the bytes are the overflow from the addition.
          if (GetAddressByteSize() == 4)
            load_addr &= 0xFFFFFFFF;

          if (target.GetSectionLoadList().SetSectionLoadAddress(section_sp,
                                                                load_addr))
            ++num_loaded_sections;
        }
      }
      return num_loaded_sections > 0;
    }
  }
  return false;
}

ByteOrder ObjectFileELF::GetByteOrder() const {
  if (m_header.e_ident[EI_DATA] == ELFDATA2MSB)
    return eByteOrderBig;
  if (m_header.e_ident[EI_DATA] == ELFDATA2LSB)
    return eByteOrderLittle;
  return eByteOrderInvalid;
}

uint32_t ObjectFileELF::GetAddressByteSize() const {
  return m_data.GetAddressByteSize();
}

AddressClass ObjectFileELF::GetAddressClass(addr_t file_addr) {
  Symtab *symtab = GetSymtab();
  if (!symtab)
    return AddressClass::eUnknown;

  // The address class is determined based on the symtab. Ask it from the
  // object file what contains the symtab information.
  ObjectFile *symtab_objfile = symtab->GetObjectFile();
  if (symtab_objfile != nullptr && symtab_objfile != this)
    return symtab_objfile->GetAddressClass(file_addr);

  auto res = ObjectFile::GetAddressClass(file_addr);
  if (res != AddressClass::eCode)
    return res;

  auto ub = m_address_class_map.upper_bound(file_addr);
  if (ub == m_address_class_map.begin()) {
    // No entry in the address class map before the address. Return default
    // address class for an address in a code section.
    return AddressClass::eCode;
  }

  // Move iterator to the address class entry preceding address
  --ub;

  return ub->second;
}

size_t ObjectFileELF::SectionIndex(const SectionHeaderCollIter &I) {
  return std::distance(m_section_headers.begin(), I);
}

size_t ObjectFileELF::SectionIndex(const SectionHeaderCollConstIter &I) const {
  return std::distance(m_section_headers.begin(), I);
}

bool ObjectFileELF::ParseHeader() {
  lldb::offset_t offset = 0;
  return m_header.Parse(m_data, &offset);
}

UUID ObjectFileELF::GetUUID() {
  // Need to parse the section list to get the UUIDs, so make sure that's been
  // done.
  if (!ParseSectionHeaders() && GetType() != ObjectFile::eTypeCoreFile)
    return UUID();

  if (!m_uuid) {
    using u32le = llvm::support::ulittle32_t;
    if (GetType() == ObjectFile::eTypeCoreFile) {
      uint32_t core_notes_crc = 0;

      if (!ParseProgramHeaders())
        return UUID();

      core_notes_crc =
          CalculateELFNotesSegmentsCRC32(m_program_headers, m_data);

      if (core_notes_crc) {
        // Use 8 bytes - first 4 bytes for *magic* prefix, mainly to make it
        // look different form .gnu_debuglink crc - followed by 4 bytes of note
        // segments crc.
        u32le data[] = {u32le(g_core_uuid_magic), u32le(core_notes_crc)};
        m_uuid = UUID(data, sizeof(data));
      }
    } else {
      if (!m_gnu_debuglink_crc)
        m_gnu_debuglink_crc = calc_crc32(0, m_data);
      if (m_gnu_debuglink_crc) {
        // Use 4 bytes of crc from the .gnu_debuglink section.
        u32le data(m_gnu_debuglink_crc);
        m_uuid = UUID(&data, sizeof(data));
      }
    }
  }

  return m_uuid;
}

std::optional<FileSpec> ObjectFileELF::GetDebugLink() {
  if (m_gnu_debuglink_file.empty())
    return std::nullopt;
  return FileSpec(m_gnu_debuglink_file);
}

uint32_t ObjectFileELF::GetDependentModules(FileSpecList &files) {
  size_t num_modules = ParseDependentModules();
  uint32_t num_specs = 0;

  for (unsigned i = 0; i < num_modules; ++i) {
    if (files.AppendIfUnique(m_filespec_up->GetFileSpecAtIndex(i)))
      num_specs++;
  }

  return num_specs;
}

Address ObjectFileELF::GetImageInfoAddress(Target *target) {
  if (!ParseDynamicSymbols())
    return Address();

  SectionList *section_list = GetSectionList();
  if (!section_list)
    return Address();

  // Find the SHT_DYNAMIC (.dynamic) section.
  SectionSP dynsym_section_sp(
      section_list->FindSectionByType(eSectionTypeELFDynamicLinkInfo, true));
  if (!dynsym_section_sp)
    return Address();
  assert(dynsym_section_sp->GetObjectFile() == this);

  user_id_t dynsym_id = dynsym_section_sp->GetID();
  const ELFSectionHeaderInfo *dynsym_hdr = GetSectionHeaderByIndex(dynsym_id);
  if (!dynsym_hdr)
    return Address();

  for (size_t i = 0; i < m_dynamic_symbols.size(); ++i) {
    ELFDynamic &symbol = m_dynamic_symbols[i];

    if (symbol.d_tag == DT_DEBUG) {
      // Compute the offset as the number of previous entries plus the size of
      // d_tag.
      addr_t offset = i * dynsym_hdr->sh_entsize + GetAddressByteSize();
      return Address(dynsym_section_sp, offset);
    }
    // MIPS executables uses DT_MIPS_RLD_MAP_REL to support PIE. DT_MIPS_RLD_MAP
    // exists in non-PIE.
    else if ((symbol.d_tag == DT_MIPS_RLD_MAP ||
              symbol.d_tag == DT_MIPS_RLD_MAP_REL) &&
             target) {
      addr_t offset = i * dynsym_hdr->sh_entsize + GetAddressByteSize();
      addr_t dyn_base = dynsym_section_sp->GetLoadBaseAddress(target);
      if (dyn_base == LLDB_INVALID_ADDRESS)
        return Address();

      Status error;
      if (symbol.d_tag == DT_MIPS_RLD_MAP) {
        // DT_MIPS_RLD_MAP tag stores an absolute address of the debug pointer.
        Address addr;
        if (target->ReadPointerFromMemory(dyn_base + offset, error, addr, true))
          return addr;
      }
      if (symbol.d_tag == DT_MIPS_RLD_MAP_REL) {
        // DT_MIPS_RLD_MAP_REL tag stores the offset to the debug pointer,
        // relative to the address of the tag.
        uint64_t rel_offset;
        rel_offset = target->ReadUnsignedIntegerFromMemory(
            dyn_base + offset, GetAddressByteSize(), UINT64_MAX, error, true);
        if (error.Success() && rel_offset != UINT64_MAX) {
          Address addr;
          addr_t debug_ptr_address =
              dyn_base + (offset - GetAddressByteSize()) + rel_offset;
          addr.SetOffset(debug_ptr_address);
          return addr;
        }
      }
    }
  }

  return Address();
}

lldb_private::Address ObjectFileELF::GetEntryPointAddress() {
  if (m_entry_point_address.IsValid())
    return m_entry_point_address;

  if (!ParseHeader() || !IsExecutable())
    return m_entry_point_address;

  SectionList *section_list = GetSectionList();
  addr_t offset = m_header.e_entry;

  if (!section_list)
    m_entry_point_address.SetOffset(offset);
  else
    m_entry_point_address.ResolveAddressUsingFileSections(offset, section_list);
  return m_entry_point_address;
}

Address ObjectFileELF::GetBaseAddress() {
  if (GetType() == ObjectFile::eTypeObjectFile) {
    for (SectionHeaderCollIter I = std::next(m_section_headers.begin());
         I != m_section_headers.end(); ++I) {
      const ELFSectionHeaderInfo &header = *I;
      if (header.sh_flags & SHF_ALLOC)
        return Address(GetSectionList()->FindSectionByID(SectionIndex(I)), 0);
    }
    return LLDB_INVALID_ADDRESS;
  }

  for (const auto &EnumPHdr : llvm::enumerate(ProgramHeaders())) {
    const ELFProgramHeader &H = EnumPHdr.value();
    if (H.p_type != PT_LOAD)
      continue;

    return Address(
        GetSectionList()->FindSectionByID(SegmentID(EnumPHdr.index())), 0);
  }
  return LLDB_INVALID_ADDRESS;
}

// ParseDependentModules
size_t ObjectFileELF::ParseDependentModules() {
  if (m_filespec_up)
    return m_filespec_up->GetSize();

  m_filespec_up = std::make_unique<FileSpecList>();

  if (!ParseSectionHeaders())
    return 0;

  SectionList *section_list = GetSectionList();
  if (!section_list)
    return 0;

  // Find the SHT_DYNAMIC section.
  Section *dynsym =
      section_list->FindSectionByType(eSectionTypeELFDynamicLinkInfo, true)
          .get();
  if (!dynsym)
    return 0;
  assert(dynsym->GetObjectFile() == this);

  const ELFSectionHeaderInfo *header = GetSectionHeaderByIndex(dynsym->GetID());
  if (!header)
    return 0;
  // sh_link: section header index of string table used by entries in the
  // section.
  Section *dynstr = section_list->FindSectionByID(header->sh_link).get();
  if (!dynstr)
    return 0;

  DataExtractor dynsym_data;
  DataExtractor dynstr_data;
  if (ReadSectionData(dynsym, dynsym_data) &&
      ReadSectionData(dynstr, dynstr_data)) {
    ELFDynamic symbol;
    const lldb::offset_t section_size = dynsym_data.GetByteSize();
    lldb::offset_t offset = 0;

    // The only type of entries we are concerned with are tagged DT_NEEDED,
    // yielding the name of a required library.
    while (offset < section_size) {
      if (!symbol.Parse(dynsym_data, &offset))
        break;

      if (symbol.d_tag != DT_NEEDED)
        continue;

      uint32_t str_index = static_cast<uint32_t>(symbol.d_val);
      const char *lib_name = dynstr_data.PeekCStr(str_index);
      FileSpec file_spec(lib_name);
      FileSystem::Instance().Resolve(file_spec);
      m_filespec_up->Append(file_spec);
    }
  }

  return m_filespec_up->GetSize();
}

// GetProgramHeaderInfo
size_t ObjectFileELF::GetProgramHeaderInfo(ProgramHeaderColl &program_headers,
                                           DataExtractor &object_data,
                                           const ELFHeader &header) {
  // We have already parsed the program headers
  if (!program_headers.empty())
    return program_headers.size();

  // If there are no program headers to read we are done.
  if (header.e_phnum == 0)
    return 0;

  program_headers.resize(header.e_phnum);
  if (program_headers.size() != header.e_phnum)
    return 0;

  const size_t ph_size = header.e_phnum * header.e_phentsize;
  const elf_off ph_offset = header.e_phoff;
  DataExtractor data;
  if (data.SetData(object_data, ph_offset, ph_size) != ph_size)
    return 0;

  uint32_t idx;
  lldb::offset_t offset;
  for (idx = 0, offset = 0; idx < header.e_phnum; ++idx) {
    if (!program_headers[idx].Parse(data, &offset))
      break;
  }

  if (idx < program_headers.size())
    program_headers.resize(idx);

  return program_headers.size();
}

// ParseProgramHeaders
bool ObjectFileELF::ParseProgramHeaders() {
  return GetProgramHeaderInfo(m_program_headers, m_data, m_header) != 0;
}

lldb_private::Status
ObjectFileELF::RefineModuleDetailsFromNote(lldb_private::DataExtractor &data,
                                           lldb_private::ArchSpec &arch_spec,
                                           lldb_private::UUID &uuid) {
  Log *log = GetLog(LLDBLog::Modules);
  Status error;

  lldb::offset_t offset = 0;

  while (true) {
    // Parse the note header.  If this fails, bail out.
    const lldb::offset_t note_offset = offset;
    ELFNote note = ELFNote();
    if (!note.Parse(data, &offset)) {
      // We're done.
      return error;
    }

    LLDB_LOGF(log, "ObjectFileELF::%s parsing note name='%s', type=%" PRIu32,
              __FUNCTION__, note.n_name.c_str(), note.n_type);

    // Process FreeBSD ELF notes.
    if ((note.n_name == LLDB_NT_OWNER_FREEBSD) &&
        (note.n_type == LLDB_NT_FREEBSD_ABI_TAG) &&
        (note.n_descsz == LLDB_NT_FREEBSD_ABI_SIZE)) {
      // Pull out the min version info.
      uint32_t version_info;
      if (data.GetU32(&offset, &version_info, 1) == nullptr) {
        error.SetErrorString("failed to read FreeBSD ABI note payload");
        return error;
      }

      // Convert the version info into a major/minor number.
      const uint32_t version_major = version_info / 100000;
      const uint32_t version_minor = (version_info / 1000) % 100;

      char os_name[32];
      snprintf(os_name, sizeof(os_name), "freebsd%" PRIu32 ".%" PRIu32,
               version_major, version_minor);

      // Set the elf OS version to FreeBSD.  Also clear the vendor.
      arch_spec.GetTriple().setOSName(os_name);
      arch_spec.GetTriple().setVendor(llvm::Triple::VendorType::UnknownVendor);

      LLDB_LOGF(log,
                "ObjectFileELF::%s detected FreeBSD %" PRIu32 ".%" PRIu32
                ".%" PRIu32,
                __FUNCTION__, version_major, version_minor,
                static_cast<uint32_t>(version_info % 1000));
    }
    // Process GNU ELF notes.
    else if (note.n_name == LLDB_NT_OWNER_GNU) {
      switch (note.n_type) {
      case LLDB_NT_GNU_ABI_TAG:
        if (note.n_descsz == LLDB_NT_GNU_ABI_SIZE) {
          // Pull out the min OS version supporting the ABI.
          uint32_t version_info[4];
          if (data.GetU32(&offset, &version_info[0], note.n_descsz / 4) ==
              nullptr) {
            error.SetErrorString("failed to read GNU ABI note payload");
            return error;
          }

          // Set the OS per the OS field.
          switch (version_info[0]) {
          case LLDB_NT_GNU_ABI_OS_LINUX:
            arch_spec.GetTriple().setOS(llvm::Triple::OSType::Linux);
            arch_spec.GetTriple().setVendor(
                llvm::Triple::VendorType::UnknownVendor);
            LLDB_LOGF(log,
                      "ObjectFileELF::%s detected Linux, min version %" PRIu32
                      ".%" PRIu32 ".%" PRIu32,
                      __FUNCTION__, version_info[1], version_info[2],
                      version_info[3]);
            // FIXME we have the minimal version number, we could be propagating
            // that.  version_info[1] = OS Major, version_info[2] = OS Minor,
            // version_info[3] = Revision.
            break;
          case LLDB_NT_GNU_ABI_OS_HURD:
            arch_spec.GetTriple().setOS(llvm::Triple::OSType::UnknownOS);
            arch_spec.GetTriple().setVendor(
                llvm::Triple::VendorType::UnknownVendor);
            LLDB_LOGF(log,
                      "ObjectFileELF::%s detected Hurd (unsupported), min "
                      "version %" PRIu32 ".%" PRIu32 ".%" PRIu32,
                      __FUNCTION__, version_info[1], version_info[2],
                      version_info[3]);
            break;
          case LLDB_NT_GNU_ABI_OS_SOLARIS:
            arch_spec.GetTriple().setOS(llvm::Triple::OSType::Solaris);
            arch_spec.GetTriple().setVendor(
                llvm::Triple::VendorType::UnknownVendor);
            LLDB_LOGF(log,
                      "ObjectFileELF::%s detected Solaris, min version %" PRIu32
                      ".%" PRIu32 ".%" PRIu32,
                      __FUNCTION__, version_info[1], version_info[2],
                      version_info[3]);
            break;
          default:
            LLDB_LOGF(log,
                      "ObjectFileELF::%s unrecognized OS in note, id %" PRIu32
                      ", min version %" PRIu32 ".%" PRIu32 ".%" PRIu32,
                      __FUNCTION__, version_info[0], version_info[1],
                      version_info[2], version_info[3]);
            break;
          }
        }
        break;

      case LLDB_NT_GNU_BUILD_ID_TAG:
        // Only bother processing this if we don't already have the uuid set.
        if (!uuid.IsValid()) {
          // 16 bytes is UUID|MD5, 20 bytes is SHA1. Other linkers may produce a
          // build-id of a different length. Accept it as long as it's at least
          // 4 bytes as it will be better than our own crc32.
          if (note.n_descsz >= 4) {
            if (const uint8_t *buf = data.PeekData(offset, note.n_descsz)) {
              // Save the build id as the UUID for the module.
              uuid = UUID(buf, note.n_descsz);
            } else {
              error.SetErrorString("failed to read GNU_BUILD_ID note payload");
              return error;
            }
          }
        }
        break;
      }
      if (arch_spec.IsMIPS() &&
          arch_spec.GetTriple().getOS() == llvm::Triple::OSType::UnknownOS)
        // The note.n_name == LLDB_NT_OWNER_GNU is valid for Linux platform
        arch_spec.GetTriple().setOS(llvm::Triple::OSType::Linux);
    }
    // Process NetBSD ELF executables and shared libraries
    else if ((note.n_name == LLDB_NT_OWNER_NETBSD) &&
             (note.n_type == LLDB_NT_NETBSD_IDENT_TAG) &&
             (note.n_descsz == LLDB_NT_NETBSD_IDENT_DESCSZ) &&
             (note.n_namesz == LLDB_NT_NETBSD_IDENT_NAMESZ)) {
      // Pull out the version info.
      uint32_t version_info;
      if (data.GetU32(&offset, &version_info, 1) == nullptr) {
        error.SetErrorString("failed to read NetBSD ABI note payload");
        return error;
      }
      // Convert the version info into a major/minor/patch number.
      //     #define __NetBSD_Version__ MMmmrrpp00
      //
      //     M = major version
      //     m = minor version; a minor number of 99 indicates current.
      //     r = 0 (since NetBSD 3.0 not used)
      //     p = patchlevel
      const uint32_t version_major = version_info / 100000000;
      const uint32_t version_minor = (version_info % 100000000) / 1000000;
      const uint32_t version_patch = (version_info % 10000) / 100;
      // Set the elf OS version to NetBSD.  Also clear the vendor.
      arch_spec.GetTriple().setOSName(
          llvm::formatv("netbsd{0}.{1}.{2}", version_major, version_minor,
                        version_patch).str());
      arch_spec.GetTriple().setVendor(llvm::Triple::VendorType::UnknownVendor);
    }
    // Process NetBSD ELF core(5) notes
    else if ((note.n_name == LLDB_NT_OWNER_NETBSDCORE) &&
             (note.n_type == LLDB_NT_NETBSD_PROCINFO)) {
      // Set the elf OS version to NetBSD.  Also clear the vendor.
      arch_spec.GetTriple().setOS(llvm::Triple::OSType::NetBSD);
      arch_spec.GetTriple().setVendor(llvm::Triple::VendorType::UnknownVendor);
    }
    // Process OpenBSD ELF notes.
    else if (note.n_name == LLDB_NT_OWNER_OPENBSD) {
      // Set the elf OS version to OpenBSD.  Also clear the vendor.
      arch_spec.GetTriple().setOS(llvm::Triple::OSType::OpenBSD);
      arch_spec.GetTriple().setVendor(llvm::Triple::VendorType::UnknownVendor);
    } else if (note.n_name == LLDB_NT_OWNER_ANDROID) {
      arch_spec.GetTriple().setOS(llvm::Triple::OSType::Linux);
      arch_spec.GetTriple().setEnvironment(
          llvm::Triple::EnvironmentType::Android);
    } else if (note.n_name == LLDB_NT_OWNER_LINUX) {
      // This is sometimes found in core files and usually contains extended
      // register info
      arch_spec.GetTriple().setOS(llvm::Triple::OSType::Linux);
    } else if (note.n_name == LLDB_NT_OWNER_CORE) {
      // Parse the NT_FILE to look for stuff in paths to shared libraries
      // The contents look like this in a 64 bit ELF core file:
      //
      // count     = 0x000000000000000a (10)
      // page_size = 0x0000000000001000 (4096)
      // Index start              end                file_ofs           path
      // ===== ------------------ ------------------ ------------------ -------------------------------------
      // [  0] 0x0000000000401000 0x0000000000000000                    /tmp/a.out
      // [  1] 0x0000000000600000 0x0000000000601000 0x0000000000000000 /tmp/a.out
      // [  2] 0x0000000000601000 0x0000000000602000 0x0000000000000001 /tmp/a.out
      // [  3] 0x00007fa79c9ed000 0x00007fa79cba8000 0x0000000000000000 /lib/x86_64-linux-gnu/libc-2.19.so
      // [  4] 0x00007fa79cba8000 0x00007fa79cda7000 0x00000000000001bb /lib/x86_64-linux-gnu/libc-2.19.so
      // [  5] 0x00007fa79cda7000 0x00007fa79cdab000 0x00000000000001ba /lib/x86_64-linux-gnu/libc-2.19.so
      // [  6] 0x00007fa79cdab000 0x00007fa79cdad000 0x00000000000001be /lib/x86_64-linux-gnu/libc-2.19.so
      // [  7] 0x00007fa79cdb2000 0x00007fa79cdd5000 0x0000000000000000 /lib/x86_64-linux-gnu/ld-2.19.so
      // [  8] 0x00007fa79cfd4000 0x00007fa79cfd5000 0x0000000000000022 /lib/x86_64-linux-gnu/ld-2.19.so
      // [  9] 0x00007fa79cfd5000 0x00007fa79cfd6000 0x0000000000000023 /lib/x86_64-linux-gnu/ld-2.19.so
      //
      // In the 32 bit ELFs the count, page_size, start, end, file_ofs are
      // uint32_t.
      //
      // For reference: see readelf source code (in binutils).
      if (note.n_type == NT_FILE) {
        uint64_t count = data.GetAddress(&offset);
        const char *cstr;
        data.GetAddress(&offset); // Skip page size
        offset += count * 3 *
                  data.GetAddressByteSize(); // Skip all start/end/file_ofs
        for (size_t i = 0; i < count; ++i) {
          cstr = data.GetCStr(&offset);
          if (cstr == nullptr) {
            error.SetErrorStringWithFormat("ObjectFileELF::%s trying to read "
                                           "at an offset after the end "
                                           "(GetCStr returned nullptr)",
                                           __FUNCTION__);
            return error;
          }
          llvm::StringRef path(cstr);
          if (path.contains("/lib/x86_64-linux-gnu") || path.contains("/lib/i386-linux-gnu")) {
            arch_spec.GetTriple().setOS(llvm::Triple::OSType::Linux);
            break;
          }
        }
        if (arch_spec.IsMIPS() &&
            arch_spec.GetTriple().getOS() == llvm::Triple::OSType::UnknownOS)
          // In case of MIPSR6, the LLDB_NT_OWNER_GNU note is missing for some
          // cases (e.g. compile with -nostdlib) Hence set OS to Linux
          arch_spec.GetTriple().setOS(llvm::Triple::OSType::Linux);
      }
    }

    // Calculate the offset of the next note just in case "offset" has been
    // used to poke at the contents of the note data
    offset = note_offset + note.GetByteSize();
  }

  return error;
}

void ObjectFileELF::ParseARMAttributes(DataExtractor &data, uint64_t length,
                                       ArchSpec &arch_spec) {
  lldb::offset_t Offset = 0;

  uint8_t FormatVersion = data.GetU8(&Offset);
  if (FormatVersion != llvm::ELFAttrs::Format_Version)
    return;

  Offset = Offset + sizeof(uint32_t); // Section Length
  llvm::StringRef VendorName = data.GetCStr(&Offset);

  if (VendorName != "aeabi")
    return;

  if (arch_spec.GetTriple().getEnvironment() ==
      llvm::Triple::UnknownEnvironment)
    arch_spec.GetTriple().setEnvironment(llvm::Triple::EABI);

  while (Offset < length) {
    uint8_t Tag = data.GetU8(&Offset);
    uint32_t Size = data.GetU32(&Offset);

    if (Tag != llvm::ARMBuildAttrs::File || Size == 0)
      continue;

    while (Offset < length) {
      uint64_t Tag = data.GetULEB128(&Offset);
      switch (Tag) {
      default:
        if (Tag < 32)
          data.GetULEB128(&Offset);
        else if (Tag % 2 == 0)
          data.GetULEB128(&Offset);
        else
          data.GetCStr(&Offset);

        break;

      case llvm::ARMBuildAttrs::CPU_raw_name:
      case llvm::ARMBuildAttrs::CPU_name:
        data.GetCStr(&Offset);

        break;

      case llvm::ARMBuildAttrs::ABI_VFP_args: {
        uint64_t VFPArgs = data.GetULEB128(&Offset);

        if (VFPArgs == llvm::ARMBuildAttrs::BaseAAPCS) {
          if (arch_spec.GetTriple().getEnvironment() ==
                  llvm::Triple::UnknownEnvironment ||
              arch_spec.GetTriple().getEnvironment() == llvm::Triple::EABIHF)
            arch_spec.GetTriple().setEnvironment(llvm::Triple::EABI);

          arch_spec.SetFlags(ArchSpec::eARM_abi_soft_float);
        } else if (VFPArgs == llvm::ARMBuildAttrs::HardFPAAPCS) {
          if (arch_spec.GetTriple().getEnvironment() ==
                  llvm::Triple::UnknownEnvironment ||
              arch_spec.GetTriple().getEnvironment() == llvm::Triple::EABI)
            arch_spec.GetTriple().setEnvironment(llvm::Triple::EABIHF);

          arch_spec.SetFlags(ArchSpec::eARM_abi_hard_float);
        }

        break;
      }
      }
    }
  }
}

// GetSectionHeaderInfo
size_t ObjectFileELF::GetSectionHeaderInfo(SectionHeaderColl &section_headers,
                                           DataExtractor &object_data,
                                           const elf::ELFHeader &header,
                                           lldb_private::UUID &uuid,
                                           std::string &gnu_debuglink_file,
                                           uint32_t &gnu_debuglink_crc,
                                           ArchSpec &arch_spec) {
  // Don't reparse the section headers if we already did that.
  if (!section_headers.empty())
    return section_headers.size();

  // Only initialize the arch_spec to okay defaults if they're not already set.
  // We'll refine this with note data as we parse the notes.
  if (arch_spec.GetTriple().getOS() == llvm::Triple::OSType::UnknownOS) {
    llvm::Triple::OSType ostype;
    llvm::Triple::OSType spec_ostype;
    const uint32_t sub_type = subTypeFromElfHeader(header);
    arch_spec.SetArchitecture(eArchTypeELF, header.e_machine, sub_type,
                              header.e_ident[EI_OSABI]);

    // Validate if it is ok to remove GetOsFromOSABI. Note, that now the OS is
    // determined based on EI_OSABI flag and the info extracted from ELF notes
    // (see RefineModuleDetailsFromNote). However in some cases that still
    // might be not enough: for example a shared library might not have any
    // notes at all and have EI_OSABI flag set to System V, as result the OS
    // will be set to UnknownOS.
    GetOsFromOSABI(header.e_ident[EI_OSABI], ostype);
    spec_ostype = arch_spec.GetTriple().getOS();
    assert(spec_ostype == ostype);
    UNUSED_IF_ASSERT_DISABLED(spec_ostype);
  }

  if (arch_spec.GetMachine() == llvm::Triple::mips ||
      arch_spec.GetMachine() == llvm::Triple::mipsel ||
      arch_spec.GetMachine() == llvm::Triple::mips64 ||
      arch_spec.GetMachine() == llvm::Triple::mips64el) {
    switch (header.e_flags & llvm::ELF::EF_MIPS_ARCH_ASE) {
    case llvm::ELF::EF_MIPS_MICROMIPS:
      arch_spec.SetFlags(ArchSpec::eMIPSAse_micromips);
      break;
    case llvm::ELF::EF_MIPS_ARCH_ASE_M16:
      arch_spec.SetFlags(ArchSpec::eMIPSAse_mips16);
      break;
    case llvm::ELF::EF_MIPS_ARCH_ASE_MDMX:
      arch_spec.SetFlags(ArchSpec::eMIPSAse_mdmx);
      break;
    default:
      break;
    }
  }

  if (arch_spec.GetMachine() == llvm::Triple::arm ||
      arch_spec.GetMachine() == llvm::Triple::thumb) {
    if (header.e_flags & llvm::ELF::EF_ARM_SOFT_FLOAT)
      arch_spec.SetFlags(ArchSpec::eARM_abi_soft_float);
    else if (header.e_flags & llvm::ELF::EF_ARM_VFP_FLOAT)
      arch_spec.SetFlags(ArchSpec::eARM_abi_hard_float);
  }

  if (arch_spec.GetMachine() == llvm::Triple::riscv32 ||
      arch_spec.GetMachine() == llvm::Triple::riscv64) {
    uint32_t flags = arch_spec.GetFlags();

    if (header.e_flags & llvm::ELF::EF_RISCV_RVC)
      flags |= ArchSpec::eRISCV_rvc;
    if (header.e_flags & llvm::ELF::EF_RISCV_RVE)
      flags |= ArchSpec::eRISCV_rve;

    if ((header.e_flags & llvm::ELF::EF_RISCV_FLOAT_ABI_SINGLE) ==
        llvm::ELF::EF_RISCV_FLOAT_ABI_SINGLE)
      flags |= ArchSpec::eRISCV_float_abi_single;
    else if ((header.e_flags & llvm::ELF::EF_RISCV_FLOAT_ABI_DOUBLE) ==
             llvm::ELF::EF_RISCV_FLOAT_ABI_DOUBLE)
      flags |= ArchSpec::eRISCV_float_abi_double;
    else if ((header.e_flags & llvm::ELF::EF_RISCV_FLOAT_ABI_QUAD) ==
             llvm::ELF::EF_RISCV_FLOAT_ABI_QUAD)
      flags |= ArchSpec::eRISCV_float_abi_quad;

    arch_spec.SetFlags(flags);
  }

  // If there are no section headers we are done.
  if (header.e_shnum == 0)
    return 0;

  Log *log = GetLog(LLDBLog::Modules);

  section_headers.resize(header.e_shnum);
  if (section_headers.size() != header.e_shnum)
    return 0;

  const size_t sh_size = header.e_shnum * header.e_shentsize;
  const elf_off sh_offset = header.e_shoff;
  DataExtractor sh_data;
  if (sh_data.SetData(object_data, sh_offset, sh_size) != sh_size)
    return 0;

  uint32_t idx;
  lldb::offset_t offset;
  for (idx = 0, offset = 0; idx < header.e_shnum; ++idx) {
    if (!section_headers[idx].Parse(sh_data, &offset))
      break;
  }
  if (idx < section_headers.size())
    section_headers.resize(idx);

  const unsigned strtab_idx = header.e_shstrndx;
  if (strtab_idx && strtab_idx < section_headers.size()) {
    const ELFSectionHeaderInfo &sheader = section_headers[strtab_idx];
    const size_t byte_size = sheader.sh_size;
    const Elf64_Off offset = sheader.sh_offset;
    lldb_private::DataExtractor shstr_data;

    if (shstr_data.SetData(object_data, offset, byte_size) == byte_size) {
      for (SectionHeaderCollIter I = section_headers.begin();
           I != section_headers.end(); ++I) {
        static ConstString g_sect_name_gnu_debuglink(".gnu_debuglink");
        const ELFSectionHeaderInfo &sheader = *I;
        const uint64_t section_size =
            sheader.sh_type == SHT_NOBITS ? 0 : sheader.sh_size;
        ConstString name(shstr_data.PeekCStr(I->sh_name));

        I->section_name = name;

        if (arch_spec.IsMIPS()) {
          uint32_t arch_flags = arch_spec.GetFlags();
          DataExtractor data;
          if (sheader.sh_type == SHT_MIPS_ABIFLAGS) {

            if (section_size && (data.SetData(object_data, sheader.sh_offset,
                                              section_size) == section_size)) {
              // MIPS ASE Mask is at offset 12 in MIPS.abiflags section
              lldb::offset_t offset = 12; // MIPS ABI Flags Version: 0
              arch_flags |= data.GetU32(&offset);

              // The floating point ABI is at offset 7
              offset = 7;
              switch (data.GetU8(&offset)) {
              case llvm::Mips::Val_GNU_MIPS_ABI_FP_ANY:
                arch_flags |= lldb_private::ArchSpec::eMIPS_ABI_FP_ANY;
                break;
              case llvm::Mips::Val_GNU_MIPS_ABI_FP_DOUBLE:
                arch_flags |= lldb_private::ArchSpec::eMIPS_ABI_FP_DOUBLE;
                break;
              case llvm::Mips::Val_GNU_MIPS_ABI_FP_SINGLE:
                arch_flags |= lldb_private::ArchSpec::eMIPS_ABI_FP_SINGLE;
                break;
              case llvm::Mips::Val_GNU_MIPS_ABI_FP_SOFT:
                arch_flags |= lldb_private::ArchSpec::eMIPS_ABI_FP_SOFT;
                break;
              case llvm::Mips::Val_GNU_MIPS_ABI_FP_OLD_64:
                arch_flags |= lldb_private::ArchSpec::eMIPS_ABI_FP_OLD_64;
                break;
              case llvm::Mips::Val_GNU_MIPS_ABI_FP_XX:
                arch_flags |= lldb_private::ArchSpec::eMIPS_ABI_FP_XX;
                break;
              case llvm::Mips::Val_GNU_MIPS_ABI_FP_64:
                arch_flags |= lldb_private::ArchSpec::eMIPS_ABI_FP_64;
                break;
              case llvm::Mips::Val_GNU_MIPS_ABI_FP_64A:
                arch_flags |= lldb_private::ArchSpec::eMIPS_ABI_FP_64A;
                break;
              }
            }
          }
          // Settings appropriate ArchSpec ABI Flags
          switch (header.e_flags & llvm::ELF::EF_MIPS_ABI) {
          case llvm::ELF::EF_MIPS_ABI_O32:
            arch_flags |= lldb_private::ArchSpec::eMIPSABI_O32;
            break;
          case EF_MIPS_ABI_O64:
            arch_flags |= lldb_private::ArchSpec::eMIPSABI_O64;
            break;
          case EF_MIPS_ABI_EABI32:
            arch_flags |= lldb_private::ArchSpec::eMIPSABI_EABI32;
            break;
          case EF_MIPS_ABI_EABI64:
            arch_flags |= lldb_private::ArchSpec::eMIPSABI_EABI64;
            break;
          default:
            // ABI Mask doesn't cover N32 and N64 ABI.
            if (header.e_ident[EI_CLASS] == llvm::ELF::ELFCLASS64)
              arch_flags |= lldb_private::ArchSpec::eMIPSABI_N64;
            else if (header.e_flags & llvm::ELF::EF_MIPS_ABI2)
              arch_flags |= lldb_private::ArchSpec::eMIPSABI_N32;
            break;
          }
          arch_spec.SetFlags(arch_flags);
        }

        if (arch_spec.GetMachine() == llvm::Triple::arm ||
            arch_spec.GetMachine() == llvm::Triple::thumb) {
          DataExtractor data;

          if (sheader.sh_type == SHT_ARM_ATTRIBUTES && section_size != 0 &&
              data.SetData(object_data, sheader.sh_offset, section_size) == section_size)
            ParseARMAttributes(data, section_size, arch_spec);
        }

        if (name == g_sect_name_gnu_debuglink) {
          DataExtractor data;
          if (section_size && (data.SetData(object_data, sheader.sh_offset,
                                            section_size) == section_size)) {
            lldb::offset_t gnu_debuglink_offset = 0;
            gnu_debuglink_file = data.GetCStr(&gnu_debuglink_offset);
            gnu_debuglink_offset = llvm::alignTo(gnu_debuglink_offset, 4);
            data.GetU32(&gnu_debuglink_offset, &gnu_debuglink_crc, 1);
          }
        }

        // Process ELF note section entries.
        bool is_note_header = (sheader.sh_type == SHT_NOTE);

        // The section header ".note.android.ident" is stored as a
        // PROGBITS type header but it is actually a note header.
        static ConstString g_sect_name_android_ident(".note.android.ident");
        if (!is_note_header && name == g_sect_name_android_ident)
          is_note_header = true;

        if (is_note_header) {
          // Allow notes to refine module info.
          DataExtractor data;
          if (section_size && (data.SetData(object_data, sheader.sh_offset,
                                            section_size) == section_size)) {
            Status error = RefineModuleDetailsFromNote(data, arch_spec, uuid);
            if (error.Fail()) {
              LLDB_LOGF(log, "ObjectFileELF::%s ELF note processing failed: %s",
                        __FUNCTION__, error.AsCString());
            }
          }
        }
      }

      // Make any unknown triple components to be unspecified unknowns.
      if (arch_spec.GetTriple().getVendor() == llvm::Triple::UnknownVendor)
        arch_spec.GetTriple().setVendorName(llvm::StringRef());
      if (arch_spec.GetTriple().getOS() == llvm::Triple::UnknownOS)
        arch_spec.GetTriple().setOSName(llvm::StringRef());

      return section_headers.size();
    }
  }

  section_headers.clear();
  return 0;
}

llvm::StringRef
ObjectFileELF::StripLinkerSymbolAnnotations(llvm::StringRef symbol_name) const {
  size_t pos = symbol_name.find('@');
  return symbol_name.substr(0, pos);
}

// ParseSectionHeaders
size_t ObjectFileELF::ParseSectionHeaders() {
  return GetSectionHeaderInfo(m_section_headers, m_data, m_header, m_uuid,
                              m_gnu_debuglink_file, m_gnu_debuglink_crc,
                              m_arch_spec);
}

const ObjectFileELF::ELFSectionHeaderInfo *
ObjectFileELF::GetSectionHeaderByIndex(lldb::user_id_t id) {
  if (!ParseSectionHeaders())
    return nullptr;

  if (id < m_section_headers.size())
    return &m_section_headers[id];

  return nullptr;
}

lldb::user_id_t ObjectFileELF::GetSectionIndexByName(const char *name) {
  if (!name || !name[0] || !ParseSectionHeaders())
    return 0;
  for (size_t i = 1; i < m_section_headers.size(); ++i)
    if (m_section_headers[i].section_name == ConstString(name))
      return i;
  return 0;
}

static SectionType GetSectionTypeFromName(llvm::StringRef Name) {
  if (Name.consume_front(".debug_")) {
    return llvm::StringSwitch<SectionType>(Name)
        .Case("abbrev", eSectionTypeDWARFDebugAbbrev)
        .Case("abbrev.dwo", eSectionTypeDWARFDebugAbbrevDwo)
        .Case("addr", eSectionTypeDWARFDebugAddr)
        .Case("aranges", eSectionTypeDWARFDebugAranges)
        .Case("cu_index", eSectionTypeDWARFDebugCuIndex)
        .Case("frame", eSectionTypeDWARFDebugFrame)
        .Case("info", eSectionTypeDWARFDebugInfo)
        .Case("info.dwo", eSectionTypeDWARFDebugInfoDwo)
        .Cases("line", "line.dwo", eSectionTypeDWARFDebugLine)
        .Cases("line_str", "line_str.dwo", eSectionTypeDWARFDebugLineStr)
        .Case("loc", eSectionTypeDWARFDebugLoc)
        .Case("loc.dwo", eSectionTypeDWARFDebugLocDwo)
        .Case("loclists", eSectionTypeDWARFDebugLocLists)
        .Case("loclists.dwo", eSectionTypeDWARFDebugLocListsDwo)
        .Case("macinfo", eSectionTypeDWARFDebugMacInfo)
        .Cases("macro", "macro.dwo", eSectionTypeDWARFDebugMacro)
        .Case("names", eSectionTypeDWARFDebugNames)
        .Case("pubnames", eSectionTypeDWARFDebugPubNames)
        .Case("pubtypes", eSectionTypeDWARFDebugPubTypes)
        .Case("ranges", eSectionTypeDWARFDebugRanges)
        .Case("rnglists", eSectionTypeDWARFDebugRngLists)
        .Case("rnglists.dwo", eSectionTypeDWARFDebugRngListsDwo)
        .Case("str", eSectionTypeDWARFDebugStr)
        .Case("str.dwo", eSectionTypeDWARFDebugStrDwo)
        .Case("str_offsets", eSectionTypeDWARFDebugStrOffsets)
        .Case("str_offsets.dwo", eSectionTypeDWARFDebugStrOffsetsDwo)
        .Case("tu_index", eSectionTypeDWARFDebugTuIndex)
        .Case("types", eSectionTypeDWARFDebugTypes)
        .Case("types.dwo", eSectionTypeDWARFDebugTypesDwo)
        .Default(eSectionTypeOther);
  }
  return llvm::StringSwitch<SectionType>(Name)
      .Case(".ARM.exidx", eSectionTypeARMexidx)
      .Case(".ARM.extab", eSectionTypeARMextab)
      .Case(".ctf", eSectionTypeDebug)
      .Cases(".data", ".tdata", eSectionTypeData)
      .Case(".eh_frame", eSectionTypeEHFrame)
      .Case(".gnu_debugaltlink", eSectionTypeDWARFGNUDebugAltLink)
      .Case(".gosymtab", eSectionTypeGoSymtab)
      .Case(".text", eSectionTypeCode)
      .Case(".swift_ast", eSectionTypeSwiftModules)
      .Default(eSectionTypeOther);
}

SectionType ObjectFileELF::GetSectionType(const ELFSectionHeaderInfo &H) const {
  switch (H.sh_type) {
  case SHT_PROGBITS:
    if (H.sh_flags & SHF_EXECINSTR)
      return eSectionTypeCode;
    break;
  case SHT_NOBITS:
    if (H.sh_flags & SHF_ALLOC)
      return eSectionTypeZeroFill;
    break;
  case SHT_SYMTAB:
    return eSectionTypeELFSymbolTable;
  case SHT_DYNSYM:
    return eSectionTypeELFDynamicSymbols;
  case SHT_RELA:
  case SHT_REL:
    return eSectionTypeELFRelocationEntries;
  case SHT_DYNAMIC:
    return eSectionTypeELFDynamicLinkInfo;
  }
  return GetSectionTypeFromName(H.section_name.GetStringRef());
}

static uint32_t GetTargetByteSize(SectionType Type, const ArchSpec &arch) {
  switch (Type) {
  case eSectionTypeData:
  case eSectionTypeZeroFill:
    return arch.GetDataByteSize();
  case eSectionTypeCode:
    return arch.GetCodeByteSize();
  default:
    return 1;
  }
}

static Permissions GetPermissions(const ELFSectionHeader &H) {
  Permissions Perm = Permissions(0);
  if (H.sh_flags & SHF_ALLOC)
    Perm |= ePermissionsReadable;
  if (H.sh_flags & SHF_WRITE)
    Perm |= ePermissionsWritable;
  if (H.sh_flags & SHF_EXECINSTR)
    Perm |= ePermissionsExecutable;
  return Perm;
}

static Permissions GetPermissions(const ELFProgramHeader &H) {
  Permissions Perm = Permissions(0);
  if (H.p_flags & PF_R)
    Perm |= ePermissionsReadable;
  if (H.p_flags & PF_W)
    Perm |= ePermissionsWritable;
  if (H.p_flags & PF_X)
    Perm |= ePermissionsExecutable;
  return Perm;
}

namespace {

using VMRange = lldb_private::Range<addr_t, addr_t>;

struct SectionAddressInfo {
  SectionSP Segment;
  VMRange Range;
};

// (Unlinked) ELF object files usually have 0 for every section address, meaning
// we need to compute synthetic addresses in order for "file addresses" from
// different sections to not overlap. This class handles that logic.
class VMAddressProvider {
  using VMMap = llvm::IntervalMap<addr_t, SectionSP, 4,
                                       llvm::IntervalMapHalfOpenInfo<addr_t>>;

  ObjectFile::Type ObjectType;
  addr_t NextVMAddress = 0;
  VMMap::Allocator Alloc;
  VMMap Segments{Alloc};
  VMMap Sections{Alloc};
  lldb_private::Log *Log = GetLog(LLDBLog::Modules);
  size_t SegmentCount = 0;
  std::string SegmentName;

  VMRange GetVMRange(const ELFSectionHeader &H) {
    addr_t Address = H.sh_addr;
    addr_t Size = H.sh_flags & SHF_ALLOC ? H.sh_size : 0;

    // When this is a debug file for relocatable file, the address is all zero
    // and thus needs to use accumulate method
    if ((ObjectType == ObjectFile::Type::eTypeObjectFile ||
         (ObjectType == ObjectFile::Type::eTypeDebugInfo && H.sh_addr == 0)) &&
        Segments.empty() && (H.sh_flags & SHF_ALLOC)) {
      NextVMAddress =
          llvm::alignTo(NextVMAddress, std::max<addr_t>(H.sh_addralign, 1));
      Address = NextVMAddress;
      NextVMAddress += Size;
    }
    return VMRange(Address, Size);
  }

public:
  VMAddressProvider(ObjectFile::Type Type, llvm::StringRef SegmentName)
      : ObjectType(Type), SegmentName(std::string(SegmentName)) {}

  std::string GetNextSegmentName() const {
    return llvm::formatv("{0}[{1}]", SegmentName, SegmentCount).str();
  }

  std::optional<VMRange> GetAddressInfo(const ELFProgramHeader &H) {
    if (H.p_memsz == 0) {
      LLDB_LOG(Log, "Ignoring zero-sized {0} segment. Corrupt object file?",
               SegmentName);
      return std::nullopt;
    }

    if (Segments.overlaps(H.p_vaddr, H.p_vaddr + H.p_memsz)) {
      LLDB_LOG(Log, "Ignoring overlapping {0} segment. Corrupt object file?",
               SegmentName);
      return std::nullopt;
    }
    return VMRange(H.p_vaddr, H.p_memsz);
  }

  std::optional<SectionAddressInfo> GetAddressInfo(const ELFSectionHeader &H) {
    VMRange Range = GetVMRange(H);
    SectionSP Segment;
    auto It = Segments.find(Range.GetRangeBase());
    if ((H.sh_flags & SHF_ALLOC) && It.valid()) {
      addr_t MaxSize;
      if (It.start() <= Range.GetRangeBase()) {
        MaxSize = It.stop() - Range.GetRangeBase();
        Segment = *It;
      } else
        MaxSize = It.start() - Range.GetRangeBase();
      if (Range.GetByteSize() > MaxSize) {
        LLDB_LOG(Log, "Shortening section crossing segment boundaries. "
                      "Corrupt object file?");
        Range.SetByteSize(MaxSize);
      }
    }
    if (Range.GetByteSize() > 0 &&
        Sections.overlaps(Range.GetRangeBase(), Range.GetRangeEnd())) {
      LLDB_LOG(Log, "Ignoring overlapping section. Corrupt object file?");
      return std::nullopt;
    }
    if (Segment)
      Range.Slide(-Segment->GetFileAddress());
    return SectionAddressInfo{Segment, Range};
  }

  void AddSegment(const VMRange &Range, SectionSP Seg) {
    Segments.insert(Range.GetRangeBase(), Range.GetRangeEnd(), std::move(Seg));
    ++SegmentCount;
  }

  void AddSection(SectionAddressInfo Info, SectionSP Sect) {
    if (Info.Range.GetByteSize() == 0)
      return;
    if (Info.Segment)
      Info.Range.Slide(Info.Segment->GetFileAddress());
    Sections.insert(Info.Range.GetRangeBase(), Info.Range.GetRangeEnd(),
                    std::move(Sect));
  }
};
}

// We have to do this because ELF doesn't have section IDs, and also
// doesn't require section names to be unique.  (We use the section index
// for section IDs, but that isn't guaranteed to be the same in separate
// debug images.)
static SectionSP FindMatchingSection(const SectionList &section_list,
                                     SectionSP section) {
  SectionSP sect_sp;

  addr_t vm_addr = section->GetFileAddress();
  ConstString name = section->GetName();
  offset_t byte_size = section->GetByteSize();
  bool thread_specific = section->IsThreadSpecific();
  uint32_t permissions = section->GetPermissions();
  uint32_t alignment = section->GetLog2Align();

  for (auto sect : section_list) {
    if (sect->GetName() == name &&
        sect->IsThreadSpecific() == thread_specific &&
        sect->GetPermissions() == permissions &&
        sect->GetByteSize() == byte_size && sect->GetFileAddress() == vm_addr &&
        sect->GetLog2Align() == alignment) {
      sect_sp = sect;
      break;
    } else {
      sect_sp = FindMatchingSection(sect->GetChildren(), section);
      if (sect_sp)
        break;
    }
  }

  return sect_sp;
}

void ObjectFileELF::CreateSections(SectionList &unified_section_list) {
  if (m_sections_up)
    return;

  m_sections_up = std::make_unique<SectionList>();
  VMAddressProvider regular_provider(GetType(), "PT_LOAD");
  VMAddressProvider tls_provider(GetType(), "PT_TLS");

  for (const auto &EnumPHdr : llvm::enumerate(ProgramHeaders())) {
    const ELFProgramHeader &PHdr = EnumPHdr.value();
    if (PHdr.p_type != PT_LOAD && PHdr.p_type != PT_TLS)
      continue;

    VMAddressProvider &provider =
        PHdr.p_type == PT_TLS ? tls_provider : regular_provider;
    auto InfoOr = provider.GetAddressInfo(PHdr);
    if (!InfoOr)
      continue;

    uint32_t Log2Align = llvm::Log2_64(std::max<elf_xword>(PHdr.p_align, 1));
    SectionSP Segment = std::make_shared<Section>(
        GetModule(), this, SegmentID(EnumPHdr.index()),
        ConstString(provider.GetNextSegmentName()), eSectionTypeContainer,
        InfoOr->GetRangeBase(), InfoOr->GetByteSize(), PHdr.p_offset,
        PHdr.p_filesz, Log2Align, /*flags*/ 0);
    Segment->SetPermissions(GetPermissions(PHdr));
    Segment->SetIsThreadSpecific(PHdr.p_type == PT_TLS);
    m_sections_up->AddSection(Segment);

    provider.AddSegment(*InfoOr, std::move(Segment));
  }

  ParseSectionHeaders();
  if (m_section_headers.empty())
    return;

  for (SectionHeaderCollIter I = std::next(m_section_headers.begin());
       I != m_section_headers.end(); ++I) {
    const ELFSectionHeaderInfo &header = *I;

    ConstString &name = I->section_name;
    const uint64_t file_size =
        header.sh_type == SHT_NOBITS ? 0 : header.sh_size;

    VMAddressProvider &provider =
        header.sh_flags & SHF_TLS ? tls_provider : regular_provider;
    auto InfoOr = provider.GetAddressInfo(header);
    if (!InfoOr)
      continue;

    SectionType sect_type = GetSectionType(header);

    const uint32_t target_bytes_size =
        GetTargetByteSize(sect_type, m_arch_spec);

    elf::elf_xword log2align =
        (header.sh_addralign == 0) ? 0 : llvm::Log2_64(header.sh_addralign);

    SectionSP section_sp(new Section(
        InfoOr->Segment, GetModule(), // Module to which this section belongs.
        this,            // ObjectFile to which this section belongs and should
                         // read section data from.
        SectionIndex(I), // Section ID.
        name,            // Section name.
        sect_type,       // Section type.
        InfoOr->Range.GetRangeBase(), // VM address.
        InfoOr->Range.GetByteSize(),  // VM size in bytes of this section.
        header.sh_offset,             // Offset of this section in the file.
        file_size,           // Size of the section as found in the file.
        log2align,           // Alignment of the section
        header.sh_flags,     // Flags for this section.
        target_bytes_size)); // Number of host bytes per target byte

    section_sp->SetPermissions(GetPermissions(header));
    section_sp->SetIsThreadSpecific(header.sh_flags & SHF_TLS);
    (InfoOr->Segment ? InfoOr->Segment->GetChildren() : *m_sections_up)
        .AddSection(section_sp);
    provider.AddSection(std::move(*InfoOr), std::move(section_sp));
  }

  // For eTypeDebugInfo files, the Symbol Vendor will take care of updating the
  // unified section list.
  if (GetType() != eTypeDebugInfo)
    unified_section_list = *m_sections_up;

  // If there's a .gnu_debugdata section, we'll try to read the .symtab that's
  // embedded in there and replace the one in the original object file (if any).
  // If there's none in the orignal object file, we add it to it.
  if (auto gdd_obj_file = GetGnuDebugDataObjectFile()) {
    if (auto gdd_objfile_section_list = gdd_obj_file->GetSectionList()) {
      if (SectionSP symtab_section_sp =
              gdd_objfile_section_list->FindSectionByType(
                  eSectionTypeELFSymbolTable, true)) {
        SectionSP module_section_sp = unified_section_list.FindSectionByType(
            eSectionTypeELFSymbolTable, true);
        if (module_section_sp)
          unified_section_list.ReplaceSection(module_section_sp->GetID(),
                                              symtab_section_sp);
        else
          unified_section_list.AddSection(symtab_section_sp);
      }
    }
  }
}

std::shared_ptr<ObjectFileELF> ObjectFileELF::GetGnuDebugDataObjectFile() {
  if (m_gnu_debug_data_object_file != nullptr)
    return m_gnu_debug_data_object_file;

  SectionSP section =
      GetSectionList()->FindSectionByName(ConstString(".gnu_debugdata"));
  if (!section)
    return nullptr;

  if (!lldb_private::lzma::isAvailable()) {
    GetModule()->ReportWarning(
        "No LZMA support found for reading .gnu_debugdata section");
    return nullptr;
  }

  // Uncompress the data
  DataExtractor data;
  section->GetSectionData(data);
  llvm::SmallVector<uint8_t, 0> uncompressedData;
  auto err = lldb_private::lzma::uncompress(data.GetData(), uncompressedData);
  if (err) {
    GetModule()->ReportWarning(
        "An error occurred while decompression the section {0}: {1}",
        section->GetName().AsCString(), llvm::toString(std::move(err)).c_str());
    return nullptr;
  }

  // Construct ObjectFileELF object from decompressed buffer
  DataBufferSP gdd_data_buf(
      new DataBufferHeap(uncompressedData.data(), uncompressedData.size()));
  auto fspec = GetFileSpec().CopyByAppendingPathComponent(
      llvm::StringRef("gnu_debugdata"));
  m_gnu_debug_data_object_file.reset(new ObjectFileELF(
      GetModule(), gdd_data_buf, 0, &fspec, 0, gdd_data_buf->GetByteSize()));

  // This line is essential; otherwise a breakpoint can be set but not hit.
  m_gnu_debug_data_object_file->SetType(ObjectFile::eTypeDebugInfo);

  ArchSpec spec = m_gnu_debug_data_object_file->GetArchitecture();
  if (spec && m_gnu_debug_data_object_file->SetModulesArchitecture(spec))
    return m_gnu_debug_data_object_file;

  return nullptr;
}

// Find the arm/aarch64 mapping symbol character in the given symbol name.
// Mapping symbols have the form of "$<char>[.<any>]*". Additionally we
// recognize cases when the mapping symbol prefixed by an arbitrary string
// because if a symbol prefix added to each symbol in the object file with
// objcopy then the mapping symbols are also prefixed.
static char FindArmAarch64MappingSymbol(const char *symbol_name) {
  if (!symbol_name)
    return '\0';

  const char *dollar_pos = ::strchr(symbol_name, '$');
  if (!dollar_pos || dollar_pos[1] == '\0')
    return '\0';

  if (dollar_pos[2] == '\0' || dollar_pos[2] == '.')
    return dollar_pos[1];
  return '\0';
}

#define STO_MIPS_ISA (3 << 6)
#define STO_MICROMIPS (2 << 6)
#define IS_MICROMIPS(ST_OTHER) (((ST_OTHER)&STO_MIPS_ISA) == STO_MICROMIPS)

// private
std::pair<unsigned, ObjectFileELF::FileAddressToAddressClassMap>
ObjectFileELF::ParseSymbols(Symtab *symtab, user_id_t start_id,
                            SectionList *section_list, const size_t num_symbols,
                            const DataExtractor &symtab_data,
                            const DataExtractor &strtab_data) {
  ELFSymbol symbol;
  lldb::offset_t offset = 0;
  // The changes these symbols would make to the class map. We will also update
  // m_address_class_map but need to tell the caller what changed because the
  // caller may be another object file.
  FileAddressToAddressClassMap address_class_map;

  static ConstString text_section_name(".text");
  static ConstString init_section_name(".init");
  static ConstString fini_section_name(".fini");
  static ConstString ctors_section_name(".ctors");
  static ConstString dtors_section_name(".dtors");

  static ConstString data_section_name(".data");
  static ConstString rodata_section_name(".rodata");
  static ConstString rodata1_section_name(".rodata1");
  static ConstString data2_section_name(".data1");
  static ConstString bss_section_name(".bss");
  static ConstString opd_section_name(".opd"); // For ppc64

  // On Android the oatdata and the oatexec symbols in the oat and odex files
  // covers the full .text section what causes issues with displaying unusable
  // symbol name to the user and very slow unwinding speed because the
  // instruction emulation based unwind plans try to emulate all instructions
  // in these symbols. Don't add these symbols to the symbol list as they have
  // no use for the debugger and they are causing a lot of trouble. Filtering
  // can't be restricted to Android because this special object file don't
  // contain the note section specifying the environment to Android but the
  // custom extension and file name makes it highly unlikely that this will
  // collide with anything else.
  llvm::StringRef file_extension = m_file.GetFileNameExtension();
  bool skip_oatdata_oatexec =
      file_extension == ".oat" || file_extension == ".odex";

  ArchSpec arch = GetArchitecture();
  ModuleSP module_sp(GetModule());
  SectionList *module_section_list =
      module_sp ? module_sp->GetSectionList() : nullptr;

  // We might have debug information in a separate object, in which case
  // we need to map the sections from that object to the sections in the
  // main object during symbol lookup.  If we had to compare the sections
  // for every single symbol, that would be expensive, so this map is
  // used to accelerate the process.
  std::unordered_map<lldb::SectionSP, lldb::SectionSP> section_map;

  unsigned i;
  for (i = 0; i < num_symbols; ++i) {
    if (!symbol.Parse(symtab_data, &offset))
      break;

    const char *symbol_name = strtab_data.PeekCStr(symbol.st_name);
    if (!symbol_name)
      symbol_name = "";

    // No need to add non-section symbols that have no names
    if (symbol.getType() != STT_SECTION &&
        (symbol_name == nullptr || symbol_name[0] == '\0'))
      continue;

    // Skipping oatdata and oatexec sections if it is requested. See details
    // above the definition of skip_oatdata_oatexec for the reasons.
    if (skip_oatdata_oatexec && (::strcmp(symbol_name, "oatdata") == 0 ||
                                 ::strcmp(symbol_name, "oatexec") == 0))
      continue;

    SectionSP symbol_section_sp;
    SymbolType symbol_type = eSymbolTypeInvalid;
    Elf64_Half shndx = symbol.st_shndx;

    switch (shndx) {
    case SHN_ABS:
      symbol_type = eSymbolTypeAbsolute;
      break;
    case SHN_UNDEF:
      symbol_type = eSymbolTypeUndefined;
      break;
    default:
      symbol_section_sp = section_list->FindSectionByID(shndx);
      break;
    }

    // If a symbol is undefined do not process it further even if it has a STT
    // type
    if (symbol_type != eSymbolTypeUndefined) {
      switch (symbol.getType()) {
      default:
      case STT_NOTYPE:
        // The symbol's type is not specified.
        break;

      case STT_OBJECT:
        // The symbol is associated with a data object, such as a variable, an
        // array, etc.
        symbol_type = eSymbolTypeData;
        break;

      case STT_FUNC:
        // The symbol is associated with a function or other executable code.
        symbol_type = eSymbolTypeCode;
        break;

      case STT_SECTION:
        // The symbol is associated with a section. Symbol table entries of
        // this type exist primarily for relocation and normally have STB_LOCAL
        // binding.
        break;

      case STT_FILE:
        // Conventionally, the symbol's name gives the name of the source file
        // associated with the object file. A file symbol has STB_LOCAL
        // binding, its section index is SHN_ABS, and it precedes the other
        // STB_LOCAL symbols for the file, if it is present.
        symbol_type = eSymbolTypeSourceFile;
        break;

      case STT_GNU_IFUNC:
        // The symbol is associated with an indirect function. The actual
        // function will be resolved if it is referenced.
        symbol_type = eSymbolTypeResolver;
        break;
      }
    }

    if (symbol_type == eSymbolTypeInvalid && symbol.getType() != STT_SECTION) {
      if (symbol_section_sp) {
        ConstString sect_name = symbol_section_sp->GetName();
        if (sect_name == text_section_name || sect_name == init_section_name ||
            sect_name == fini_section_name || sect_name == ctors_section_name ||
            sect_name == dtors_section_name) {
          symbol_type = eSymbolTypeCode;
        } else if (sect_name == data_section_name ||
                   sect_name == data2_section_name ||
                   sect_name == rodata_section_name ||
                   sect_name == rodata1_section_name ||
                   sect_name == bss_section_name) {
          symbol_type = eSymbolTypeData;
        }
      }
    }

    int64_t symbol_value_offset = 0;
    uint32_t additional_flags = 0;

    if (arch.IsValid()) {
      if (arch.GetMachine() == llvm::Triple::arm) {
        if (symbol.getBinding() == STB_LOCAL) {
          char mapping_symbol = FindArmAarch64MappingSymbol(symbol_name);
          if (symbol_type == eSymbolTypeCode) {
            switch (mapping_symbol) {
            case 'a':
              // $a[.<any>]* - marks an ARM instruction sequence
              address_class_map[symbol.st_value] = AddressClass::eCode;
              break;
            case 'b':
            case 't':
              // $b[.<any>]* - marks a THUMB BL instruction sequence
              // $t[.<any>]* - marks a THUMB instruction sequence
              address_class_map[symbol.st_value] =
                  AddressClass::eCodeAlternateISA;
              break;
            case 'd':
              // $d[.<any>]* - marks a data item sequence (e.g. lit pool)
              address_class_map[symbol.st_value] = AddressClass::eData;
              break;
            }
          }
          if (mapping_symbol)
            continue;
        }
      } else if (arch.GetMachine() == llvm::Triple::aarch64) {
        if (symbol.getBinding() == STB_LOCAL) {
          char mapping_symbol = FindArmAarch64MappingSymbol(symbol_name);
          if (symbol_type == eSymbolTypeCode) {
            switch (mapping_symbol) {
            case 'x':
              // $x[.<any>]* - marks an A64 instruction sequence
              address_class_map[symbol.st_value] = AddressClass::eCode;
              break;
            case 'd':
              // $d[.<any>]* - marks a data item sequence (e.g. lit pool)
              address_class_map[symbol.st_value] = AddressClass::eData;
              break;
            }
          }
          if (mapping_symbol)
            continue;
        }
      }

      if (arch.GetMachine() == llvm::Triple::arm) {
        if (symbol_type == eSymbolTypeCode) {
          if (symbol.st_value & 1) {
            // Subtracting 1 from the address effectively unsets the low order
            // bit, which results in the address actually pointing to the
            // beginning of the symbol. This delta will be used below in
            // conjunction with symbol.st_value to produce the final
            // symbol_value that we store in the symtab.
            symbol_value_offset = -1;
            address_class_map[symbol.st_value ^ 1] =
                AddressClass::eCodeAlternateISA;
          } else {
            // This address is ARM
            address_class_map[symbol.st_value] = AddressClass::eCode;
          }
        }
      }

      /*
       * MIPS:
       * The bit #0 of an address is used for ISA mode (1 for microMIPS, 0 for
       * MIPS).
       * This allows processor to switch between microMIPS and MIPS without any
       * need
       * for special mode-control register. However, apart from .debug_line,
       * none of
       * the ELF/DWARF sections set the ISA bit (for symbol or section). Use
       * st_other
       * flag to check whether the symbol is microMIPS and then set the address
       * class
       * accordingly.
      */
      if (arch.IsMIPS()) {
        if (IS_MICROMIPS(symbol.st_other))
          address_class_map[symbol.st_value] = AddressClass::eCodeAlternateISA;
        else if ((symbol.st_value & 1) && (symbol_type == eSymbolTypeCode)) {
          symbol.st_value = symbol.st_value & (~1ull);
          address_class_map[symbol.st_value] = AddressClass::eCodeAlternateISA;
        } else {
          if (symbol_type == eSymbolTypeCode)
            address_class_map[symbol.st_value] = AddressClass::eCode;
          else if (symbol_type == eSymbolTypeData)
            address_class_map[symbol.st_value] = AddressClass::eData;
          else
            address_class_map[symbol.st_value] = AddressClass::eUnknown;
        }
      }
    }

    // symbol_value_offset may contain 0 for ARM symbols or -1 for THUMB
    // symbols. See above for more details.
    uint64_t symbol_value = symbol.st_value + symbol_value_offset;

    if (symbol_section_sp &&
        CalculateType() != ObjectFile::Type::eTypeObjectFile)
      symbol_value -= symbol_section_sp->GetFileAddress();

    if (symbol_section_sp && module_section_list &&
        module_section_list != section_list) {
      auto section_it = section_map.find(symbol_section_sp);
      if (section_it == section_map.end()) {
        section_it = section_map
                         .emplace(symbol_section_sp,
                                  FindMatchingSection(*module_section_list,
                                                      symbol_section_sp))
                         .first;
      }
      if (section_it->second)
        symbol_section_sp = section_it->second;
    }

    bool is_global = symbol.getBinding() == STB_GLOBAL;
    uint32_t flags = symbol.st_other << 8 | symbol.st_info | additional_flags;
    llvm::StringRef symbol_ref(symbol_name);

    // Symbol names may contain @VERSION suffixes. Find those and strip them
    // temporarily.
    size_t version_pos = symbol_ref.find('@');
    bool has_suffix = version_pos != llvm::StringRef::npos;
    llvm::StringRef symbol_bare = symbol_ref.substr(0, version_pos);
    Mangled mangled(symbol_bare);

    // Now append the suffix back to mangled and unmangled names. Only do it if
    // the demangling was successful (string is not empty).
    if (has_suffix) {
      llvm::StringRef suffix = symbol_ref.substr(version_pos);

      llvm::StringRef mangled_name = mangled.GetMangledName().GetStringRef();
      if (!mangled_name.empty())
        mangled.SetMangledName(ConstString((mangled_name + suffix).str()));

      ConstString demangled = mangled.GetDemangledName();
      llvm::StringRef demangled_name = demangled.GetStringRef();
      if (!demangled_name.empty())
        mangled.SetDemangledName(ConstString((demangled_name + suffix).str()));
    }

    // In ELF all symbol should have a valid size but it is not true for some
    // function symbols coming from hand written assembly. As none of the
    // function symbol should have 0 size we try to calculate the size for
    // these symbols in the symtab with saying that their original size is not
    // valid.
    bool symbol_size_valid =
        symbol.st_size != 0 || symbol.getType() != STT_FUNC;

    bool is_trampoline = false;
    if (arch.IsValid() && (arch.GetMachine() == llvm::Triple::aarch64)) {
      // On AArch64, trampolines are registered as code.
      // If we detect a trampoline (which starts with __AArch64ADRPThunk_ or
      // __AArch64AbsLongThunk_) we register the symbol as a trampoline. This
      // way we will be able to detect the trampoline when we step in a function
      // and step through the trampoline.
      if (symbol_type == eSymbolTypeCode) {
        llvm::StringRef trampoline_name = mangled.GetName().GetStringRef();
        if (trampoline_name.starts_with("__AArch64ADRPThunk_") ||
            trampoline_name.starts_with("__AArch64AbsLongThunk_")) {
          symbol_type = eSymbolTypeTrampoline;
          is_trampoline = true;
        }
      }
    }

    Symbol dc_symbol(
        i + start_id, // ID is the original symbol table index.
        mangled,
        symbol_type,                    // Type of this symbol
        is_global,                      // Is this globally visible?
        false,                          // Is this symbol debug info?
        is_trampoline,                  // Is this symbol a trampoline?
        false,                          // Is this symbol artificial?
        AddressRange(symbol_section_sp, // Section in which this symbol is
                                        // defined or null.
                     symbol_value,      // Offset in section or symbol value.
                     symbol.st_size),   // Size in bytes of this symbol.
        symbol_size_valid,              // Symbol size is valid
        has_suffix,                     // Contains linker annotations?
        flags);                         // Symbol flags.
    if (symbol.getBinding() == STB_WEAK)
      dc_symbol.SetIsWeak(true);
    symtab->AddSymbol(dc_symbol);
  }

  m_address_class_map.merge(address_class_map);
  return {i, address_class_map};
}

std::pair<unsigned, ObjectFileELF::FileAddressToAddressClassMap>
ObjectFileELF::ParseSymbolTable(Symtab *symbol_table, user_id_t start_id,
                                lldb_private::Section *symtab) {
  if (symtab->GetObjectFile() != this) {
    // If the symbol table section is owned by a different object file, have it
    // do the parsing.
    ObjectFileELF *obj_file_elf =
        static_cast<ObjectFileELF *>(symtab->GetObjectFile());
    auto [num_symbols, address_class_map] =
        obj_file_elf->ParseSymbolTable(symbol_table, start_id, symtab);

    // The other object file returned the changes it made to its address
    // class map, make the same changes to ours.
    m_address_class_map.merge(address_class_map);

    return {num_symbols, address_class_map};
  }

  // Get section list for this object file.
  SectionList *section_list = m_sections_up.get();
  if (!section_list)
    return {};

  user_id_t symtab_id = symtab->GetID();
  const ELFSectionHeaderInfo *symtab_hdr = GetSectionHeaderByIndex(symtab_id);
  assert(symtab_hdr->sh_type == SHT_SYMTAB ||
         symtab_hdr->sh_type == SHT_DYNSYM);

  // sh_link: section header index of associated string table.
  user_id_t strtab_id = symtab_hdr->sh_link;
  Section *strtab = section_list->FindSectionByID(strtab_id).get();

  if (symtab && strtab) {
    assert(symtab->GetObjectFile() == this);
    assert(strtab->GetObjectFile() == this);

    DataExtractor symtab_data;
    DataExtractor strtab_data;
    if (ReadSectionData(symtab, symtab_data) &&
        ReadSectionData(strtab, strtab_data)) {
      size_t num_symbols = symtab_data.GetByteSize() / symtab_hdr->sh_entsize;

      return ParseSymbols(symbol_table, start_id, section_list, num_symbols,
                          symtab_data, strtab_data);
    }
  }

  return {0, {}};
}

size_t ObjectFileELF::ParseDynamicSymbols() {
  if (m_dynamic_symbols.size())
    return m_dynamic_symbols.size();

  SectionList *section_list = GetSectionList();
  if (!section_list)
    return 0;

  // Find the SHT_DYNAMIC section.
  Section *dynsym =
      section_list->FindSectionByType(eSectionTypeELFDynamicLinkInfo, true)
          .get();
  if (!dynsym)
    return 0;
  assert(dynsym->GetObjectFile() == this);

  ELFDynamic symbol;
  DataExtractor dynsym_data;
  if (ReadSectionData(dynsym, dynsym_data)) {
    const lldb::offset_t section_size = dynsym_data.GetByteSize();
    lldb::offset_t cursor = 0;

    while (cursor < section_size) {
      if (!symbol.Parse(dynsym_data, &cursor))
        break;

      m_dynamic_symbols.push_back(symbol);
    }
  }

  return m_dynamic_symbols.size();
}

const ELFDynamic *ObjectFileELF::FindDynamicSymbol(unsigned tag) {
  if (!ParseDynamicSymbols())
    return nullptr;

  DynamicSymbolCollIter I = m_dynamic_symbols.begin();
  DynamicSymbolCollIter E = m_dynamic_symbols.end();
  for (; I != E; ++I) {
    ELFDynamic *symbol = &*I;

    if (symbol->d_tag == tag)
      return symbol;
  }

  return nullptr;
}

unsigned ObjectFileELF::PLTRelocationType() {
  // DT_PLTREL
  //  This member specifies the type of relocation entry to which the
  //  procedure linkage table refers. The d_val member holds DT_REL or
  //  DT_RELA, as appropriate. All relocations in a procedure linkage table
  //  must use the same relocation.
  const ELFDynamic *symbol = FindDynamicSymbol(DT_PLTREL);

  if (symbol)
    return symbol->d_val;

  return 0;
}

// Returns the size of the normal plt entries and the offset of the first
// normal plt entry. The 0th entry in the plt table is usually a resolution
// entry which have different size in some architectures then the rest of the
// plt entries.
static std::pair<uint64_t, uint64_t>
GetPltEntrySizeAndOffset(const ELFSectionHeader *rel_hdr,
                         const ELFSectionHeader *plt_hdr) {
  const elf_xword num_relocations = rel_hdr->sh_size / rel_hdr->sh_entsize;

  // Clang 3.3 sets entsize to 4 for 32-bit binaries, but the plt entries are
  // 16 bytes. So round the entsize up by the alignment if addralign is set.
  elf_xword plt_entsize =
      plt_hdr->sh_addralign
          ? llvm::alignTo(plt_hdr->sh_entsize, plt_hdr->sh_addralign)
          : plt_hdr->sh_entsize;

  // Some linkers e.g ld for arm, fill plt_hdr->sh_entsize field incorrectly.
  // PLT entries relocation code in general requires multiple instruction and
  // should be greater than 4 bytes in most cases. Try to guess correct size
  // just in case.
  if (plt_entsize <= 4) {
    // The linker haven't set the plt_hdr->sh_entsize field. Try to guess the
    // size of the plt entries based on the number of entries and the size of
    // the plt section with the assumption that the size of the 0th entry is at
    // least as big as the size of the normal entries and it isn't much bigger
    // then that.
    if (plt_hdr->sh_addralign)
      plt_entsize = plt_hdr->sh_size / plt_hdr->sh_addralign /
                    (num_relocations + 1) * plt_hdr->sh_addralign;
    else
      plt_entsize = plt_hdr->sh_size / (num_relocations + 1);
  }

  elf_xword plt_offset = plt_hdr->sh_size - num_relocations * plt_entsize;

  return std::make_pair(plt_entsize, plt_offset);
}

static unsigned ParsePLTRelocations(
    Symtab *symbol_table, user_id_t start_id, unsigned rel_type,
    const ELFHeader *hdr, const ELFSectionHeader *rel_hdr,
    const ELFSectionHeader *plt_hdr, const ELFSectionHeader *sym_hdr,
    const lldb::SectionSP &plt_section_sp, DataExtractor &rel_data,
    DataExtractor &symtab_data, DataExtractor &strtab_data) {
  ELFRelocation rel(rel_type);
  ELFSymbol symbol;
  lldb::offset_t offset = 0;

  uint64_t plt_offset, plt_entsize;
  std::tie(plt_entsize, plt_offset) =
      GetPltEntrySizeAndOffset(rel_hdr, plt_hdr);
  const elf_xword num_relocations = rel_hdr->sh_size / rel_hdr->sh_entsize;

  typedef unsigned (*reloc_info_fn)(const ELFRelocation &rel);
  reloc_info_fn reloc_type;
  reloc_info_fn reloc_symbol;

  if (hdr->Is32Bit()) {
    reloc_type = ELFRelocation::RelocType32;
    reloc_symbol = ELFRelocation::RelocSymbol32;
  } else {
    reloc_type = ELFRelocation::RelocType64;
    reloc_symbol = ELFRelocation::RelocSymbol64;
  }

  unsigned slot_type = hdr->GetRelocationJumpSlotType();
  unsigned i;
  for (i = 0; i < num_relocations; ++i) {
    if (!rel.Parse(rel_data, &offset))
      break;

    if (reloc_type(rel) != slot_type)
      continue;

    lldb::offset_t symbol_offset = reloc_symbol(rel) * sym_hdr->sh_entsize;
    if (!symbol.Parse(symtab_data, &symbol_offset))
      break;

    const char *symbol_name = strtab_data.PeekCStr(symbol.st_name);
    uint64_t plt_index = plt_offset + i * plt_entsize;

    Symbol jump_symbol(
        i + start_id,          // Symbol table index
        symbol_name,           // symbol name.
        eSymbolTypeTrampoline, // Type of this symbol
        false,                 // Is this globally visible?
        false,                 // Is this symbol debug info?
        true,                  // Is this symbol a trampoline?
        true,                  // Is this symbol artificial?
        plt_section_sp, // Section in which this symbol is defined or null.
        plt_index,      // Offset in section or symbol value.
        plt_entsize,    // Size in bytes of this symbol.
        true,           // Size is valid
        false,          // Contains linker annotations?
        0);             // Symbol flags.

    symbol_table->AddSymbol(jump_symbol);
  }

  return i;
}

unsigned
ObjectFileELF::ParseTrampolineSymbols(Symtab *symbol_table, user_id_t start_id,
                                      const ELFSectionHeaderInfo *rel_hdr,
                                      user_id_t rel_id) {
  assert(rel_hdr->sh_type == SHT_RELA || rel_hdr->sh_type == SHT_REL);

  // The link field points to the associated symbol table.
  user_id_t symtab_id = rel_hdr->sh_link;

  // If the link field doesn't point to the appropriate symbol name table then
  // try to find it by name as some compiler don't fill in the link fields.
  if (!symtab_id)
    symtab_id = GetSectionIndexByName(".dynsym");

  // Get PLT section.  We cannot use rel_hdr->sh_info, since current linkers
  // point that to the .got.plt or .got section instead of .plt.
  user_id_t plt_id = GetSectionIndexByName(".plt");

  if (!symtab_id || !plt_id)
    return 0;

  const ELFSectionHeaderInfo *plt_hdr = GetSectionHeaderByIndex(plt_id);
  if (!plt_hdr)
    return 0;

  const ELFSectionHeaderInfo *sym_hdr = GetSectionHeaderByIndex(symtab_id);
  if (!sym_hdr)
    return 0;

  SectionList *section_list = m_sections_up.get();
  if (!section_list)
    return 0;

  Section *rel_section = section_list->FindSectionByID(rel_id).get();
  if (!rel_section)
    return 0;

  SectionSP plt_section_sp(section_list->FindSectionByID(plt_id));
  if (!plt_section_sp)
    return 0;

  Section *symtab = section_list->FindSectionByID(symtab_id).get();
  if (!symtab)
    return 0;

  // sh_link points to associated string table.
  Section *strtab = section_list->FindSectionByID(sym_hdr->sh_link).get();
  if (!strtab)
    return 0;

  DataExtractor rel_data;
  if (!ReadSectionData(rel_section, rel_data))
    return 0;

  DataExtractor symtab_data;
  if (!ReadSectionData(symtab, symtab_data))
    return 0;

  DataExtractor strtab_data;
  if (!ReadSectionData(strtab, strtab_data))
    return 0;

  unsigned rel_type = PLTRelocationType();
  if (!rel_type)
    return 0;

  return ParsePLTRelocations(symbol_table, start_id, rel_type, &m_header,
                             rel_hdr, plt_hdr, sym_hdr, plt_section_sp,
                             rel_data, symtab_data, strtab_data);
}

static void ApplyELF64ABS64Relocation(Symtab *symtab, ELFRelocation &rel,
                                      DataExtractor &debug_data,
                                      Section *rel_section) {
  Symbol *symbol = symtab->FindSymbolByID(ELFRelocation::RelocSymbol64(rel));
  if (symbol) {
    addr_t value = symbol->GetAddressRef().GetFileAddress();
    DataBufferSP &data_buffer_sp = debug_data.GetSharedDataBuffer();
    // ObjectFileELF creates a WritableDataBuffer in CreateInstance.
    WritableDataBuffer *data_buffer =
        llvm::cast<WritableDataBuffer>(data_buffer_sp.get());
    uint64_t *dst = reinterpret_cast<uint64_t *>(
        data_buffer->GetBytes() + rel_section->GetFileOffset() +
        ELFRelocation::RelocOffset64(rel));
    uint64_t val_offset = value + ELFRelocation::RelocAddend64(rel);
    memcpy(dst, &val_offset, sizeof(uint64_t));
  }
}

static void ApplyELF64ABS32Relocation(Symtab *symtab, ELFRelocation &rel,
                                      DataExtractor &debug_data,
                                      Section *rel_section, bool is_signed) {
  Symbol *symbol = symtab->FindSymbolByID(ELFRelocation::RelocSymbol64(rel));
  if (symbol) {
    addr_t value = symbol->GetAddressRef().GetFileAddress();
    value += ELFRelocation::RelocAddend32(rel);
    if ((!is_signed && (value > UINT32_MAX)) ||
        (is_signed &&
         ((int64_t)value > INT32_MAX || (int64_t)value < INT32_MIN))) {
      Log *log = GetLog(LLDBLog::Modules);
      LLDB_LOGF(log, "Failed to apply debug info relocations");
      return;
    }
    uint32_t truncated_addr = (value & 0xFFFFFFFF);
    DataBufferSP &data_buffer_sp = debug_data.GetSharedDataBuffer();
    // ObjectFileELF creates a WritableDataBuffer in CreateInstance.
    WritableDataBuffer *data_buffer =
        llvm::cast<WritableDataBuffer>(data_buffer_sp.get());
    uint32_t *dst = reinterpret_cast<uint32_t *>(
        data_buffer->GetBytes() + rel_section->GetFileOffset() +
        ELFRelocation::RelocOffset32(rel));
    memcpy(dst, &truncated_addr, sizeof(uint32_t));
  }
}

static void ApplyELF32ABS32RelRelocation(Symtab *symtab, ELFRelocation &rel,
                                         DataExtractor &debug_data,
                                         Section *rel_section) {
  Log *log = GetLog(LLDBLog::Modules);
  Symbol *symbol = symtab->FindSymbolByID(ELFRelocation::RelocSymbol32(rel));
  if (symbol) {
    addr_t value = symbol->GetAddressRef().GetFileAddress();
    if (value == LLDB_INVALID_ADDRESS) {
      const char *name = symbol->GetName().GetCString();
      LLDB_LOGF(log, "Debug info symbol invalid: %s", name);
      return;
    }
    assert(llvm::isUInt<32>(value) && "Valid addresses are 32-bit");
    DataBufferSP &data_buffer_sp = debug_data.GetSharedDataBuffer();
    // ObjectFileELF creates a WritableDataBuffer in CreateInstance.
    WritableDataBuffer *data_buffer =
        llvm::cast<WritableDataBuffer>(data_buffer_sp.get());
    uint8_t *dst = data_buffer->GetBytes() + rel_section->GetFileOffset() +
                   ELFRelocation::RelocOffset32(rel);
    // Implicit addend is stored inline as a signed value.
    int32_t addend;
    memcpy(&addend, dst, sizeof(int32_t));
    // The sum must be positive. This extra check prevents UB from overflow in
    // the actual range check below.
    if (addend < 0 && static_cast<uint32_t>(-addend) > value) {
      LLDB_LOGF(log, "Debug info relocation overflow: 0x%" PRIx64,
                static_cast<int64_t>(value) + addend);
      return;
    }
    if (!llvm::isUInt<32>(value + addend)) {
      LLDB_LOGF(log, "Debug info relocation out of range: 0x%" PRIx64, value);
      return;
    }
    uint32_t addr = value + addend;
    memcpy(dst, &addr, sizeof(uint32_t));
  }
}

unsigned ObjectFileELF::ApplyRelocations(
    Symtab *symtab, const ELFHeader *hdr, const ELFSectionHeader *rel_hdr,
    const ELFSectionHeader *symtab_hdr, const ELFSectionHeader *debug_hdr,
    DataExtractor &rel_data, DataExtractor &symtab_data,
    DataExtractor &debug_data, Section *rel_section) {
  ELFRelocation rel(rel_hdr->sh_type);
  lldb::addr_t offset = 0;
  const unsigned num_relocations = rel_hdr->sh_size / rel_hdr->sh_entsize;
  typedef unsigned (*reloc_info_fn)(const ELFRelocation &rel);
  reloc_info_fn reloc_type;
  reloc_info_fn reloc_symbol;

  if (hdr->Is32Bit()) {
    reloc_type = ELFRelocation::RelocType32;
    reloc_symbol = ELFRelocation::RelocSymbol32;
  } else {
    reloc_type = ELFRelocation::RelocType64;
    reloc_symbol = ELFRelocation::RelocSymbol64;
  }

  for (unsigned i = 0; i < num_relocations; ++i) {
    if (!rel.Parse(rel_data, &offset)) {
      GetModule()->ReportError(".rel{0}[{1:d}] failed to parse relocation",
                               rel_section->GetName().AsCString(), i);
      break;
    }
    Symbol *symbol = nullptr;

    if (hdr->Is32Bit()) {
      switch (hdr->e_machine) {
      case llvm::ELF::EM_ARM:
        switch (reloc_type(rel)) {
        case R_ARM_ABS32:
          ApplyELF32ABS32RelRelocation(symtab, rel, debug_data, rel_section);
          break;
        case R_ARM_REL32:
          GetModule()->ReportError("unsupported AArch32 relocation:"
                                   " .rel{0}[{1}], type {2}",
                                   rel_section->GetName().AsCString(), i,
                                   reloc_type(rel));
          break;
        default:
          assert(false && "unexpected relocation type");
        }
        break;
      case llvm::ELF::EM_386:
        switch (reloc_type(rel)) {
        case R_386_32:
          symbol = symtab->FindSymbolByID(reloc_symbol(rel));
          if (symbol) {
            addr_t f_offset =
                rel_section->GetFileOffset() + ELFRelocation::RelocOffset32(rel);
            DataBufferSP &data_buffer_sp = debug_data.GetSharedDataBuffer();
            // ObjectFileELF creates a WritableDataBuffer in CreateInstance.
            WritableDataBuffer *data_buffer =
                llvm::cast<WritableDataBuffer>(data_buffer_sp.get());
            uint32_t *dst = reinterpret_cast<uint32_t *>(
                data_buffer->GetBytes() + f_offset);

            addr_t value = symbol->GetAddressRef().GetFileAddress();
            if (rel.IsRela()) {
              value += ELFRelocation::RelocAddend32(rel);
            } else {
              value += *dst;
            }
            *dst = value;
          } else {
            GetModule()->ReportError(".rel{0}[{1}] unknown symbol id: {2:d}",
                                    rel_section->GetName().AsCString(), i,
                                    reloc_symbol(rel));
          }
          break;
        case R_386_NONE:
        case R_386_PC32:
          GetModule()->ReportError("unsupported i386 relocation:"
                                   " .rel{0}[{1}], type {2}",
                                   rel_section->GetName().AsCString(), i,
                                   reloc_type(rel));
          break;
        default:
          assert(false && "unexpected relocation type");
          break;
        }
        break;
      default:
        GetModule()->ReportError("unsupported 32-bit ELF machine arch: {0}", hdr->e_machine);
        break;
      }
    } else {
      switch (hdr->e_machine) {
      case llvm::ELF::EM_AARCH64:
        switch (reloc_type(rel)) {
        case R_AARCH64_ABS64:
          ApplyELF64ABS64Relocation(symtab, rel, debug_data, rel_section);
          break;
        case R_AARCH64_ABS32:
          ApplyELF64ABS32Relocation(symtab, rel, debug_data, rel_section, true);
          break;
        default:
          assert(false && "unexpected relocation type");
        }
        break;
      case llvm::ELF::EM_LOONGARCH:
        switch (reloc_type(rel)) {
        case R_LARCH_64:
          ApplyELF64ABS64Relocation(symtab, rel, debug_data, rel_section);
          break;
        case R_LARCH_32:
          ApplyELF64ABS32Relocation(symtab, rel, debug_data, rel_section, true);
          break;
        default:
          assert(false && "unexpected relocation type");
        }
        break;
      case llvm::ELF::EM_X86_64:
        switch (reloc_type(rel)) {
        case R_X86_64_64:
          ApplyELF64ABS64Relocation(symtab, rel, debug_data, rel_section);
          break;
        case R_X86_64_32:
          ApplyELF64ABS32Relocation(symtab, rel, debug_data, rel_section,
                                    false);
          break;
        case R_X86_64_32S:
          ApplyELF64ABS32Relocation(symtab, rel, debug_data, rel_section, true);
          break;
        case R_X86_64_PC32:
        default:
          assert(false && "unexpected relocation type");
        }
        break;
      default:
        GetModule()->ReportError("unsupported 64-bit ELF machine arch: {0}", hdr->e_machine);
        break;
      }
    }
  }

  return 0;
}

unsigned ObjectFileELF::RelocateDebugSections(const ELFSectionHeader *rel_hdr,
                                              user_id_t rel_id,
                                              lldb_private::Symtab *thetab) {
  assert(rel_hdr->sh_type == SHT_RELA || rel_hdr->sh_type == SHT_REL);

  // Parse in the section list if needed.
  SectionList *section_list = GetSectionList();
  if (!section_list)
    return 0;

  user_id_t symtab_id = rel_hdr->sh_link;
  user_id_t debug_id = rel_hdr->sh_info;

  const ELFSectionHeader *symtab_hdr = GetSectionHeaderByIndex(symtab_id);
  if (!symtab_hdr)
    return 0;

  const ELFSectionHeader *debug_hdr = GetSectionHeaderByIndex(debug_id);
  if (!debug_hdr)
    return 0;

  Section *rel = section_list->FindSectionByID(rel_id).get();
  if (!rel)
    return 0;

  Section *symtab = section_list->FindSectionByID(symtab_id).get();
  if (!symtab)
    return 0;

  Section *debug = section_list->FindSectionByID(debug_id).get();
  if (!debug)
    return 0;

  DataExtractor rel_data;
  DataExtractor symtab_data;
  DataExtractor debug_data;

  if (GetData(rel->GetFileOffset(), rel->GetFileSize(), rel_data) &&
      GetData(symtab->GetFileOffset(), symtab->GetFileSize(), symtab_data) &&
      GetData(debug->GetFileOffset(), debug->GetFileSize(), debug_data)) {
    ApplyRelocations(thetab, &m_header, rel_hdr, symtab_hdr, debug_hdr,
                     rel_data, symtab_data, debug_data, debug);
  }

  return 0;
}

void ObjectFileELF::ParseSymtab(Symtab &lldb_symtab) {
  ModuleSP module_sp(GetModule());
  if (!module_sp)
    return;

  Progress progress("Parsing symbol table",
                    m_file.GetFilename().AsCString("<Unknown>"));
  ElapsedTime elapsed(module_sp->GetSymtabParseTime());

  // We always want to use the main object file so we (hopefully) only have one
  // cached copy of our symtab, dynamic sections, etc.
  ObjectFile *module_obj_file = module_sp->GetObjectFile();
  if (module_obj_file && module_obj_file != this)
    return module_obj_file->ParseSymtab(lldb_symtab);

  SectionList *section_list = module_sp->GetSectionList();
  if (!section_list)
    return;

  uint64_t symbol_id = 0;

  // Sharable objects and dynamic executables usually have 2 distinct symbol
  // tables, one named ".symtab", and the other ".dynsym". The dynsym is a
  // smaller version of the symtab that only contains global symbols. The
  // information found in the dynsym is therefore also found in the symtab,
  // while the reverse is not necessarily true.
  Section *symtab =
      section_list->FindSectionByType(eSectionTypeELFSymbolTable, true).get();
  if (symtab) {
    auto [num_symbols, address_class_map] =
        ParseSymbolTable(&lldb_symtab, symbol_id, symtab);
    m_address_class_map.merge(address_class_map);
    symbol_id += num_symbols;
  }

  // The symtab section is non-allocable and can be stripped, while the
  // .dynsym section which should always be always be there. To support the
  // minidebuginfo case we parse .dynsym when there's a .gnu_debuginfo
  // section, nomatter if .symtab was already parsed or not. This is because
  // minidebuginfo normally removes the .symtab symbols which have their
  // matching .dynsym counterparts.
  if (!symtab ||
      GetSectionList()->FindSectionByName(ConstString(".gnu_debugdata"))) {
    Section *dynsym =
        section_list->FindSectionByType(eSectionTypeELFDynamicSymbols, true)
            .get();
    if (dynsym) {
      auto [num_symbols, address_class_map] =
          ParseSymbolTable(&lldb_symtab, symbol_id, dynsym);
      symbol_id += num_symbols;
      m_address_class_map.merge(address_class_map);
    }
  }

  // DT_JMPREL
  //      If present, this entry's d_ptr member holds the address of
  //      relocation
  //      entries associated solely with the procedure linkage table.
  //      Separating
  //      these relocation entries lets the dynamic linker ignore them during
  //      process initialization, if lazy binding is enabled. If this entry is
  //      present, the related entries of types DT_PLTRELSZ and DT_PLTREL must
  //      also be present.
  const ELFDynamic *symbol = FindDynamicSymbol(DT_JMPREL);
  if (symbol) {
    const ELFDynamic *pltrelsz = FindDynamicSymbol(DT_PLTRELSZ);
    assert(pltrelsz != NULL);
    // Synthesize trampoline symbols to help navigate the PLT.
    addr_t addr = symbol->d_ptr;
    Section *reloc_section =
        section_list->FindSectionContainingFileAddress(addr).get();
    if (reloc_section && pltrelsz->d_val > 0) {
      user_id_t reloc_id = reloc_section->GetID();
      const ELFSectionHeaderInfo *reloc_header =
          GetSectionHeaderByIndex(reloc_id);
      if (reloc_header)
        ParseTrampolineSymbols(&lldb_symtab, symbol_id, reloc_header, reloc_id);
    }
  }

  if (DWARFCallFrameInfo *eh_frame =
          GetModule()->GetUnwindTable().GetEHFrameInfo()) {
    ParseUnwindSymbols(&lldb_symtab, eh_frame);
  }

  // In the event that there's no symbol entry for the entry point we'll
  // artificially create one. We delegate to the symtab object the figuring
  // out of the proper size, this will usually make it span til the next
  // symbol it finds in the section. This means that if there are missing
  // symbols the entry point might span beyond its function definition.
  // We're fine with this as it doesn't make it worse than not having a
  // symbol entry at all.
  if (CalculateType() == eTypeExecutable) {
    ArchSpec arch = GetArchitecture();
    auto entry_point_addr = GetEntryPointAddress();
    bool is_valid_entry_point =
        entry_point_addr.IsValid() && entry_point_addr.IsSectionOffset();
    addr_t entry_point_file_addr = entry_point_addr.GetFileAddress();
    if (is_valid_entry_point && !lldb_symtab.FindSymbolContainingFileAddress(
                                    entry_point_file_addr)) {
      uint64_t symbol_id = lldb_symtab.GetNumSymbols();
      // Don't set the name for any synthetic symbols, the Symbol
      // object will generate one if needed when the name is accessed
      // via accessors.
      SectionSP section_sp = entry_point_addr.GetSection();
      Symbol symbol(
          /*symID=*/symbol_id,
          /*name=*/llvm::StringRef(), // Name will be auto generated.
          /*type=*/eSymbolTypeCode,
          /*external=*/true,
          /*is_debug=*/false,
          /*is_trampoline=*/false,
          /*is_artificial=*/true,
          /*section_sp=*/section_sp,
          /*offset=*/0,
          /*size=*/0, // FDE can span multiple symbols so don't use its size.
          /*size_is_valid=*/false,
          /*contains_linker_annotations=*/false,
          /*flags=*/0);
      // When the entry point is arm thumb we need to explicitly set its
      // class address to reflect that. This is important because expression
      // evaluation relies on correctly setting a breakpoint at this
      // address.
      if (arch.GetMachine() == llvm::Triple::arm &&
          (entry_point_file_addr & 1)) {
        symbol.GetAddressRef().SetOffset(entry_point_addr.GetOffset() ^ 1);
        m_address_class_map[entry_point_file_addr ^ 1] =
            AddressClass::eCodeAlternateISA;
      } else {
        m_address_class_map[entry_point_file_addr] = AddressClass::eCode;
      }
      lldb_symtab.AddSymbol(symbol);
    }
  }
}

void ObjectFileELF::RelocateSection(lldb_private::Section *section)
{
  static const char *debug_prefix = ".debug";

  // Set relocated bit so we stop getting called, regardless of whether we
  // actually relocate.
  section->SetIsRelocated(true);

  // We only relocate in ELF relocatable files
  if (CalculateType() != eTypeObjectFile)
    return;

  const char *section_name = section->GetName().GetCString();
  // Can't relocate that which can't be named
  if (section_name == nullptr)
    return;

  // We don't relocate non-debug sections at the moment
  if (strncmp(section_name, debug_prefix, strlen(debug_prefix)))
    return;

  // Relocation section names to look for
  std::string needle = std::string(".rel") + section_name;
  std::string needlea = std::string(".rela") + section_name;

  for (SectionHeaderCollIter I = m_section_headers.begin();
       I != m_section_headers.end(); ++I) {
    if (I->sh_type == SHT_RELA || I->sh_type == SHT_REL) {
      const char *hay_name = I->section_name.GetCString();
      if (hay_name == nullptr)
        continue;
      if (needle == hay_name || needlea == hay_name) {
        const ELFSectionHeader &reloc_header = *I;
        user_id_t reloc_id = SectionIndex(I);
        RelocateDebugSections(&reloc_header, reloc_id, GetSymtab());
        break;
      }
    }
  }
}

void ObjectFileELF::ParseUnwindSymbols(Symtab *symbol_table,
                                       DWARFCallFrameInfo *eh_frame) {
  SectionList *section_list = GetSectionList();
  if (!section_list)
    return;

  // First we save the new symbols into a separate list and add them to the
  // symbol table after we collected all symbols we want to add. This is
  // neccessary because adding a new symbol invalidates the internal index of
  // the symtab what causing the next lookup to be slow because it have to
  // recalculate the index first.
  std::vector<Symbol> new_symbols;

  size_t num_symbols = symbol_table->GetNumSymbols();
  uint64_t last_symbol_id =
      num_symbols ? symbol_table->SymbolAtIndex(num_symbols - 1)->GetID() : 0;
  eh_frame->ForEachFDEEntries([&](lldb::addr_t file_addr, uint32_t size,
                                  dw_offset_t) {
    Symbol *symbol = symbol_table->FindSymbolAtFileAddress(file_addr);
    if (symbol) {
      if (!symbol->GetByteSizeIsValid()) {
        symbol->SetByteSize(size);
        symbol->SetSizeIsSynthesized(true);
      }
    } else {
      SectionSP section_sp =
          section_list->FindSectionContainingFileAddress(file_addr);
      if (section_sp) {
        addr_t offset = file_addr - section_sp->GetFileAddress();
        uint64_t symbol_id = ++last_symbol_id;
        // Don't set the name for any synthetic symbols, the Symbol
        // object will generate one if needed when the name is accessed
        // via accessors.
        Symbol eh_symbol(
            /*symID=*/symbol_id,
            /*name=*/llvm::StringRef(), // Name will be auto generated.
            /*type=*/eSymbolTypeCode,
            /*external=*/true,
            /*is_debug=*/false,
            /*is_trampoline=*/false,
            /*is_artificial=*/true,
            /*section_sp=*/section_sp,
            /*offset=*/offset,
            /*size=*/0, // FDE can span multiple symbols so don't use its size.
            /*size_is_valid=*/false,
            /*contains_linker_annotations=*/false,
            /*flags=*/0);
        new_symbols.push_back(eh_symbol);
      }
    }
    return true;
  });

  for (const Symbol &s : new_symbols)
    symbol_table->AddSymbol(s);
}

bool ObjectFileELF::IsStripped() {
  // TODO: determine this for ELF
  return false;
}

//===----------------------------------------------------------------------===//
// Dump
//
// Dump the specifics of the runtime file container (such as any headers
// segments, sections, etc).
void ObjectFileELF::Dump(Stream *s) {
  ModuleSP module_sp(GetModule());
  if (!module_sp) {
    return;
  }

  std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
  s->Printf("%p: ", static_cast<void *>(this));
  s->Indent();
  s->PutCString("ObjectFileELF");

  ArchSpec header_arch = GetArchitecture();

  *s << ", file = '" << m_file
     << "', arch = " << header_arch.GetArchitectureName() << "\n";

  DumpELFHeader(s, m_header);
  s->EOL();
  DumpELFProgramHeaders(s);
  s->EOL();
  DumpELFSectionHeaders(s);
  s->EOL();
  SectionList *section_list = GetSectionList();
  if (section_list)
    section_list->Dump(s->AsRawOstream(), s->GetIndentLevel(), nullptr, true,
                       UINT32_MAX);
  Symtab *symtab = GetSymtab();
  if (symtab)
    symtab->Dump(s, nullptr, eSortOrderNone);
  s->EOL();
  DumpDependentModules(s);
  s->EOL();
}

// DumpELFHeader
//
// Dump the ELF header to the specified output stream
void ObjectFileELF::DumpELFHeader(Stream *s, const ELFHeader &header) {
  s->PutCString("ELF Header\n");
  s->Printf("e_ident[EI_MAG0   ] = 0x%2.2x\n", header.e_ident[EI_MAG0]);
  s->Printf("e_ident[EI_MAG1   ] = 0x%2.2x '%c'\n", header.e_ident[EI_MAG1],
            header.e_ident[EI_MAG1]);
  s->Printf("e_ident[EI_MAG2   ] = 0x%2.2x '%c'\n", header.e_ident[EI_MAG2],
            header.e_ident[EI_MAG2]);
  s->Printf("e_ident[EI_MAG3   ] = 0x%2.2x '%c'\n", header.e_ident[EI_MAG3],
            header.e_ident[EI_MAG3]);

  s->Printf("e_ident[EI_CLASS  ] = 0x%2.2x\n", header.e_ident[EI_CLASS]);
  s->Printf("e_ident[EI_DATA   ] = 0x%2.2x ", header.e_ident[EI_DATA]);
  DumpELFHeader_e_ident_EI_DATA(s, header.e_ident[EI_DATA]);
  s->Printf("\ne_ident[EI_VERSION] = 0x%2.2x\n", header.e_ident[EI_VERSION]);
  s->Printf("e_ident[EI_PAD    ] = 0x%2.2x\n", header.e_ident[EI_PAD]);

  s->Printf("e_type      = 0x%4.4x ", header.e_type);
  DumpELFHeader_e_type(s, header.e_type);
  s->Printf("\ne_machine   = 0x%4.4x\n", header.e_machine);
  s->Printf("e_version   = 0x%8.8x\n", header.e_version);
  s->Printf("e_entry     = 0x%8.8" PRIx64 "\n", header.e_entry);
  s->Printf("e_phoff     = 0x%8.8" PRIx64 "\n", header.e_phoff);
  s->Printf("e_shoff     = 0x%8.8" PRIx64 "\n", header.e_shoff);
  s->Printf("e_flags     = 0x%8.8x\n", header.e_flags);
  s->Printf("e_ehsize    = 0x%4.4x\n", header.e_ehsize);
  s->Printf("e_phentsize = 0x%4.4x\n", header.e_phentsize);
  s->Printf("e_phnum     = 0x%8.8x\n", header.e_phnum);
  s->Printf("e_shentsize = 0x%4.4x\n", header.e_shentsize);
  s->Printf("e_shnum     = 0x%8.8x\n", header.e_shnum);
  s->Printf("e_shstrndx  = 0x%8.8x\n", header.e_shstrndx);
}

// DumpELFHeader_e_type
//
// Dump an token value for the ELF header member e_type
void ObjectFileELF::DumpELFHeader_e_type(Stream *s, elf_half e_type) {
  switch (e_type) {
  case ET_NONE:
    *s << "ET_NONE";
    break;
  case ET_REL:
    *s << "ET_REL";
    break;
  case ET_EXEC:
    *s << "ET_EXEC";
    break;
  case ET_DYN:
    *s << "ET_DYN";
    break;
  case ET_CORE:
    *s << "ET_CORE";
    break;
  default:
    break;
  }
}

// DumpELFHeader_e_ident_EI_DATA
//
// Dump an token value for the ELF header member e_ident[EI_DATA]
void ObjectFileELF::DumpELFHeader_e_ident_EI_DATA(Stream *s,
                                                  unsigned char ei_data) {
  switch (ei_data) {
  case ELFDATANONE:
    *s << "ELFDATANONE";
    break;
  case ELFDATA2LSB:
    *s << "ELFDATA2LSB - Little Endian";
    break;
  case ELFDATA2MSB:
    *s << "ELFDATA2MSB - Big Endian";
    break;
  default:
    break;
  }
}

// DumpELFProgramHeader
//
// Dump a single ELF program header to the specified output stream
void ObjectFileELF::DumpELFProgramHeader(Stream *s,
                                         const ELFProgramHeader &ph) {
  DumpELFProgramHeader_p_type(s, ph.p_type);
  s->Printf(" %8.8" PRIx64 " %8.8" PRIx64 " %8.8" PRIx64, ph.p_offset,
            ph.p_vaddr, ph.p_paddr);
  s->Printf(" %8.8" PRIx64 " %8.8" PRIx64 " %8.8x (", ph.p_filesz, ph.p_memsz,
            ph.p_flags);

  DumpELFProgramHeader_p_flags(s, ph.p_flags);
  s->Printf(") %8.8" PRIx64, ph.p_align);
}

// DumpELFProgramHeader_p_type
//
// Dump an token value for the ELF program header member p_type which describes
// the type of the program header
void ObjectFileELF::DumpELFProgramHeader_p_type(Stream *s, elf_word p_type) {
  const int kStrWidth = 15;
  switch (p_type) {
    CASE_AND_STREAM(s, PT_NULL, kStrWidth);
    CASE_AND_STREAM(s, PT_LOAD, kStrWidth);
    CASE_AND_STREAM(s, PT_DYNAMIC, kStrWidth);
    CASE_AND_STREAM(s, PT_INTERP, kStrWidth);
    CASE_AND_STREAM(s, PT_NOTE, kStrWidth);
    CASE_AND_STREAM(s, PT_SHLIB, kStrWidth);
    CASE_AND_STREAM(s, PT_PHDR, kStrWidth);
    CASE_AND_STREAM(s, PT_TLS, kStrWidth);
    CASE_AND_STREAM(s, PT_GNU_EH_FRAME, kStrWidth);
  default:
    s->Printf("0x%8.8x%*s", p_type, kStrWidth - 10, "");
    break;
  }
}

// DumpELFProgramHeader_p_flags
//
// Dump an token value for the ELF program header member p_flags
void ObjectFileELF::DumpELFProgramHeader_p_flags(Stream *s, elf_word p_flags) {
  *s << ((p_flags & PF_X) ? "PF_X" : "    ")
     << (((p_flags & PF_X) && (p_flags & PF_W)) ? '+' : ' ')
     << ((p_flags & PF_W) ? "PF_W" : "    ")
     << (((p_flags & PF_W) && (p_flags & PF_R)) ? '+' : ' ')
     << ((p_flags & PF_R) ? "PF_R" : "    ");
}

// DumpELFProgramHeaders
//
// Dump all of the ELF program header to the specified output stream
void ObjectFileELF::DumpELFProgramHeaders(Stream *s) {
  if (!ParseProgramHeaders())
    return;

  s->PutCString("Program Headers\n");
  s->PutCString("IDX  p_type          p_offset p_vaddr  p_paddr  "
                "p_filesz p_memsz  p_flags                   p_align\n");
  s->PutCString("==== --------------- -------- -------- -------- "
                "-------- -------- ------------------------- --------\n");

  for (const auto &H : llvm::enumerate(m_program_headers)) {
    s->Format("[{0,2}] ", H.index());
    ObjectFileELF::DumpELFProgramHeader(s, H.value());
    s->EOL();
  }
}

// DumpELFSectionHeader
//
// Dump a single ELF section header to the specified output stream
void ObjectFileELF::DumpELFSectionHeader(Stream *s,
                                         const ELFSectionHeaderInfo &sh) {
  s->Printf("%8.8x ", sh.sh_name);
  DumpELFSectionHeader_sh_type(s, sh.sh_type);
  s->Printf(" %8.8" PRIx64 " (", sh.sh_flags);
  DumpELFSectionHeader_sh_flags(s, sh.sh_flags);
  s->Printf(") %8.8" PRIx64 " %8.8" PRIx64 " %8.8" PRIx64, sh.sh_addr,
            sh.sh_offset, sh.sh_size);
  s->Printf(" %8.8x %8.8x", sh.sh_link, sh.sh_info);
  s->Printf(" %8.8" PRIx64 " %8.8" PRIx64, sh.sh_addralign, sh.sh_entsize);
}

// DumpELFSectionHeader_sh_type
//
// Dump an token value for the ELF section header member sh_type which
// describes the type of the section
void ObjectFileELF::DumpELFSectionHeader_sh_type(Stream *s, elf_word sh_type) {
  const int kStrWidth = 12;
  switch (sh_type) {
    CASE_AND_STREAM(s, SHT_NULL, kStrWidth);
    CASE_AND_STREAM(s, SHT_PROGBITS, kStrWidth);
    CASE_AND_STREAM(s, SHT_SYMTAB, kStrWidth);
    CASE_AND_STREAM(s, SHT_STRTAB, kStrWidth);
    CASE_AND_STREAM(s, SHT_RELA, kStrWidth);
    CASE_AND_STREAM(s, SHT_HASH, kStrWidth);
    CASE_AND_STREAM(s, SHT_DYNAMIC, kStrWidth);
    CASE_AND_STREAM(s, SHT_NOTE, kStrWidth);
    CASE_AND_STREAM(s, SHT_NOBITS, kStrWidth);
    CASE_AND_STREAM(s, SHT_REL, kStrWidth);
    CASE_AND_STREAM(s, SHT_SHLIB, kStrWidth);
    CASE_AND_STREAM(s, SHT_DYNSYM, kStrWidth);
    CASE_AND_STREAM(s, SHT_LOPROC, kStrWidth);
    CASE_AND_STREAM(s, SHT_HIPROC, kStrWidth);
    CASE_AND_STREAM(s, SHT_LOUSER, kStrWidth);
    CASE_AND_STREAM(s, SHT_HIUSER, kStrWidth);
  default:
    s->Printf("0x%8.8x%*s", sh_type, kStrWidth - 10, "");
    break;
  }
}

// DumpELFSectionHeader_sh_flags
//
// Dump an token value for the ELF section header member sh_flags
void ObjectFileELF::DumpELFSectionHeader_sh_flags(Stream *s,
                                                  elf_xword sh_flags) {
  *s << ((sh_flags & SHF_WRITE) ? "WRITE" : "     ")
     << (((sh_flags & SHF_WRITE) && (sh_flags & SHF_ALLOC)) ? '+' : ' ')
     << ((sh_flags & SHF_ALLOC) ? "ALLOC" : "     ")
     << (((sh_flags & SHF_ALLOC) && (sh_flags & SHF_EXECINSTR)) ? '+' : ' ')
     << ((sh_flags & SHF_EXECINSTR) ? "EXECINSTR" : "         ");
}

// DumpELFSectionHeaders
//
// Dump all of the ELF section header to the specified output stream
void ObjectFileELF::DumpELFSectionHeaders(Stream *s) {
  if (!ParseSectionHeaders())
    return;

  s->PutCString("Section Headers\n");
  s->PutCString("IDX  name     type         flags                            "
                "addr     offset   size     link     info     addralgn "
                "entsize  Name\n");
  s->PutCString("==== -------- ------------ -------------------------------- "
                "-------- -------- -------- -------- -------- -------- "
                "-------- ====================\n");

  uint32_t idx = 0;
  for (SectionHeaderCollConstIter I = m_section_headers.begin();
       I != m_section_headers.end(); ++I, ++idx) {
    s->Printf("[%2u] ", idx);
    ObjectFileELF::DumpELFSectionHeader(s, *I);
    const char *section_name = I->section_name.AsCString("");
    if (section_name)
      *s << ' ' << section_name << "\n";
  }
}

void ObjectFileELF::DumpDependentModules(lldb_private::Stream *s) {
  size_t num_modules = ParseDependentModules();

  if (num_modules > 0) {
    s->PutCString("Dependent Modules:\n");
    for (unsigned i = 0; i < num_modules; ++i) {
      const FileSpec &spec = m_filespec_up->GetFileSpecAtIndex(i);
      s->Printf("   %s\n", spec.GetFilename().GetCString());
    }
  }
}

ArchSpec ObjectFileELF::GetArchitecture() {
  if (!ParseHeader())
    return ArchSpec();

  if (m_section_headers.empty()) {
    // Allow elf notes to be parsed which may affect the detected architecture.
    ParseSectionHeaders();
  }

  if (CalculateType() == eTypeCoreFile &&
      !m_arch_spec.TripleOSWasSpecified()) {
    // Core files don't have section headers yet they have PT_NOTE program
    // headers that might shed more light on the architecture
    for (const elf::ELFProgramHeader &H : ProgramHeaders()) {
      if (H.p_type != PT_NOTE || H.p_offset == 0 || H.p_filesz == 0)
        continue;
      DataExtractor data;
      if (data.SetData(m_data, H.p_offset, H.p_filesz) == H.p_filesz) {
        UUID uuid;
        RefineModuleDetailsFromNote(data, m_arch_spec, uuid);
      }
    }
  }
  return m_arch_spec;
}

ObjectFile::Type ObjectFileELF::CalculateType() {
  switch (m_header.e_type) {
  case llvm::ELF::ET_NONE:
    // 0 - No file type
    return eTypeUnknown;

  case llvm::ELF::ET_REL:
    // 1 - Relocatable file
    return eTypeObjectFile;

  case llvm::ELF::ET_EXEC:
    // 2 - Executable file
    return eTypeExecutable;

  case llvm::ELF::ET_DYN:
    // 3 - Shared object file
    return eTypeSharedLibrary;

  case ET_CORE:
    // 4 - Core file
    return eTypeCoreFile;

  default:
    break;
  }
  return eTypeUnknown;
}

ObjectFile::Strata ObjectFileELF::CalculateStrata() {
  switch (m_header.e_type) {
  case llvm::ELF::ET_NONE:
    // 0 - No file type
    return eStrataUnknown;

  case llvm::ELF::ET_REL:
    // 1 - Relocatable file
    return eStrataUnknown;

  case llvm::ELF::ET_EXEC:
    // 2 - Executable file
    {
      SectionList *section_list = GetSectionList();
      if (section_list) {
        static ConstString loader_section_name(".interp");
        SectionSP loader_section =
            section_list->FindSectionByName(loader_section_name);
        if (loader_section) {
          char buffer[256];
          size_t read_size =
              ReadSectionData(loader_section.get(), 0, buffer, sizeof(buffer));

          // We compare the content of .interp section
          // It will contains \0 when counting read_size, so the size needs to
          // decrease by one
          llvm::StringRef loader_name(buffer, read_size - 1);
          llvm::StringRef freebsd_kernel_loader_name("/red/herring");
          if (loader_name == freebsd_kernel_loader_name)
            return eStrataKernel;
        }
      }
      return eStrataUser;
    }

  case llvm::ELF::ET_DYN:
    // 3 - Shared object file
    // TODO: is there any way to detect that an shared library is a kernel
    // related executable by inspecting the program headers, section headers,
    // symbols, or any other flag bits???
    return eStrataUnknown;

  case ET_CORE:
    // 4 - Core file
    // TODO: is there any way to detect that an core file is a kernel
    // related executable by inspecting the program headers, section headers,
    // symbols, or any other flag bits???
    return eStrataUnknown;

  default:
    break;
  }
  return eStrataUnknown;
}

size_t ObjectFileELF::ReadSectionData(Section *section,
                       lldb::offset_t section_offset, void *dst,
                       size_t dst_len) {
  // If some other objectfile owns this data, pass this to them.
  if (section->GetObjectFile() != this)
    return section->GetObjectFile()->ReadSectionData(section, section_offset,
                                                     dst, dst_len);

  if (!section->Test(SHF_COMPRESSED))
    return ObjectFile::ReadSectionData(section, section_offset, dst, dst_len);

  // For compressed sections we need to read to full data to be able to
  // decompress.
  DataExtractor data;
  ReadSectionData(section, data);
  return data.CopyData(section_offset, dst_len, dst);
}

size_t ObjectFileELF::ReadSectionData(Section *section,
                                      DataExtractor &section_data) {
  // If some other objectfile owns this data, pass this to them.
  if (section->GetObjectFile() != this)
    return section->GetObjectFile()->ReadSectionData(section, section_data);

  size_t result = ObjectFile::ReadSectionData(section, section_data);
  if (result == 0 || !(section->Get() & llvm::ELF::SHF_COMPRESSED))
    return result;

  auto Decompressor = llvm::object::Decompressor::create(
      section->GetName().GetStringRef(),
      {reinterpret_cast<const char *>(section_data.GetDataStart()),
       size_t(section_data.GetByteSize())},
      GetByteOrder() == eByteOrderLittle, GetAddressByteSize() == 8);
  if (!Decompressor) {
    GetModule()->ReportWarning(
        "Unable to initialize decompressor for section '{0}': {1}",
        section->GetName().GetCString(),
        llvm::toString(Decompressor.takeError()).c_str());
    section_data.Clear();
    return 0;
  }

  auto buffer_sp =
      std::make_shared<DataBufferHeap>(Decompressor->getDecompressedSize(), 0);
  if (auto error = Decompressor->decompress(
          {buffer_sp->GetBytes(), size_t(buffer_sp->GetByteSize())})) {
    GetModule()->ReportWarning("Decompression of section '{0}' failed: {1}",
                               section->GetName().GetCString(),
                               llvm::toString(std::move(error)).c_str());
    section_data.Clear();
    return 0;
  }

  section_data.SetData(buffer_sp);
  return buffer_sp->GetByteSize();
}

llvm::ArrayRef<ELFProgramHeader> ObjectFileELF::ProgramHeaders() {
  ParseProgramHeaders();
  return m_program_headers;
}

DataExtractor ObjectFileELF::GetSegmentData(const ELFProgramHeader &H) {
  return DataExtractor(m_data, H.p_offset, H.p_filesz);
}

bool ObjectFileELF::AnySegmentHasPhysicalAddress() {
  for (const ELFProgramHeader &H : ProgramHeaders()) {
    if (H.p_paddr != 0)
      return true;
  }
  return false;
}

std::vector<ObjectFile::LoadableData>
ObjectFileELF::GetLoadableData(Target &target) {
  // Create a list of loadable data from loadable segments, using physical
  // addresses if they aren't all null
  std::vector<LoadableData> loadables;
  bool should_use_paddr = AnySegmentHasPhysicalAddress();
  for (const ELFProgramHeader &H : ProgramHeaders()) {
    LoadableData loadable;
    if (H.p_type != llvm::ELF::PT_LOAD)
      continue;
    loadable.Dest = should_use_paddr ? H.p_paddr : H.p_vaddr;
    if (loadable.Dest == LLDB_INVALID_ADDRESS)
      continue;
    if (H.p_filesz == 0)
      continue;
    auto segment_data = GetSegmentData(H);
    loadable.Contents = llvm::ArrayRef<uint8_t>(segment_data.GetDataStart(),
                                                segment_data.GetByteSize());
    loadables.push_back(loadable);
  }
  return loadables;
}

lldb::WritableDataBufferSP
ObjectFileELF::MapFileDataWritable(const FileSpec &file, uint64_t Size,
                                   uint64_t Offset) {
  return FileSystem::Instance().CreateWritableDataBuffer(file.GetPath(), Size,
                                                         Offset);
}
