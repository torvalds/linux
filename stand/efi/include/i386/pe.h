/* $FreeBSD$ */
/* 
    PE32+ header file
 */
#ifndef _PE_H
#define _PE_H

#define IMAGE_DOS_SIGNATURE                 0x5A4D      // MZ
#define IMAGE_OS2_SIGNATURE                 0x454E      // NE
#define IMAGE_OS2_SIGNATURE_LE              0x454C      // LE
#define IMAGE_NT_SIGNATURE                  0x00004550  // PE00  
#define IMAGE_EDOS_SIGNATURE                0x44454550  // PEED


typedef struct _IMAGE_DOS_HEADER {      // DOS .EXE header
    UINT16   e_magic;                     // Magic number
    UINT16   e_cblp;                      // Bytes on last page of file
    UINT16   e_cp;                        // Pages in file
    UINT16   e_crlc;                      // Relocations
    UINT16   e_cparhdr;                   // Size of header in paragraphs
    UINT16   e_minalloc;                  // Minimum extra paragraphs needed
    UINT16   e_maxalloc;                  // Maximum extra paragraphs needed
    UINT16   e_ss;                        // Initial (relative) SS value
    UINT16   e_sp;                        // Initial SP value
    UINT16   e_csum;                      // Checksum
    UINT16   e_ip;                        // Initial IP value
    UINT16   e_cs;                        // Initial (relative) CS value
    UINT16   e_lfarlc;                    // File address of relocation table
    UINT16   e_ovno;                      // Overlay number
    UINT16   e_res[4];                    // Reserved words
    UINT16   e_oemid;                     // OEM identifier (for e_oeminfo)
    UINT16   e_oeminfo;                   // OEM information; e_oemid specific
    UINT16   e_res2[10];                  // Reserved words
    UINT32   e_lfanew;                    // File address of new exe header
  } IMAGE_DOS_HEADER, *PIMAGE_DOS_HEADER;

typedef struct _IMAGE_OS2_HEADER {      // OS/2 .EXE header
    UINT16   ne_magic;                    // Magic number
    UINT8    ne_ver;                      // Version number
    UINT8    ne_rev;                      // Revision number
    UINT16   ne_enttab;                   // Offset of Entry Table
    UINT16   ne_cbenttab;                 // Number of bytes in Entry Table
    UINT32   ne_crc;                      // Checksum of whole file
    UINT16   ne_flags;                    // Flag UINT16
    UINT16   ne_autodata;                 // Automatic data segment number
    UINT16   ne_heap;                     // Initial heap allocation
    UINT16   ne_stack;                    // Initial stack allocation
    UINT32   ne_csip;                     // Initial CS:IP setting
    UINT32   ne_sssp;                     // Initial SS:SP setting
    UINT16   ne_cseg;                     // Count of file segments
    UINT16   ne_cmod;                     // Entries in Module Reference Table
    UINT16   ne_cbnrestab;                // Size of non-resident name table
    UINT16   ne_segtab;                   // Offset of Segment Table
    UINT16   ne_rsrctab;                  // Offset of Resource Table
    UINT16   ne_restab;                   // Offset of resident name table
    UINT16   ne_modtab;                   // Offset of Module Reference Table
    UINT16   ne_imptab;                   // Offset of Imported Names Table
    UINT32   ne_nrestab;                  // Offset of Non-resident Names Table
    UINT16   ne_cmovent;                  // Count of movable entries
    UINT16   ne_align;                    // Segment alignment shift count
    UINT16   ne_cres;                     // Count of resource segments
    UINT8    ne_exetyp;                   // Target Operating system
    UINT8    ne_flagsothers;              // Other .EXE flags
    UINT16   ne_pretthunks;               // offset to return thunks
    UINT16   ne_psegrefbytes;             // offset to segment ref. bytes
    UINT16   ne_swaparea;                 // Minimum code swap area size
    UINT16   ne_expver;                   // Expected Windows version number
  } IMAGE_OS2_HEADER, *PIMAGE_OS2_HEADER;

//
// File header format.
//

typedef struct _IMAGE_FILE_HEADER {
    UINT16   Machine;
    UINT16   NumberOfSections;
    UINT32   TimeDateStamp;
    UINT32   PointerToSymbolTable;
    UINT32   NumberOfSymbols;
    UINT16   SizeOfOptionalHeader;
    UINT16   Characteristics;
} IMAGE_FILE_HEADER, *PIMAGE_FILE_HEADER;

#define IMAGE_SIZEOF_FILE_HEADER             20

#define IMAGE_FILE_RELOCS_STRIPPED           0x0001  // Relocation info stripped from file.
#define IMAGE_FILE_EXECUTABLE_IMAGE          0x0002  // File is executable  (i.e. no unresolved externel references).
#define IMAGE_FILE_LINE_NUMS_STRIPPED        0x0004  // Line nunbers stripped from file.
#define IMAGE_FILE_LOCAL_SYMS_STRIPPED       0x0008  // Local symbols stripped from file.
#define IMAGE_FILE_BYTES_REVERSED_LO         0x0080  // Bytes of machine word are reversed.
#define IMAGE_FILE_32BIT_MACHINE             0x0100  // 32 bit word machine.
#define IMAGE_FILE_DEBUG_STRIPPED            0x0200  // Debugging info stripped from file in .DBG file
#define IMAGE_FILE_SYSTEM                    0x1000  // System File.
#define IMAGE_FILE_DLL                       0x2000  // File is a DLL.
#define IMAGE_FILE_BYTES_REVERSED_HI         0x8000  // Bytes of machine word are reversed.

#define IMAGE_FILE_MACHINE_UNKNOWN           0
#define IMAGE_FILE_MACHINE_I386              0x14c   // Intel 386.
#define IMAGE_FILE_MACHINE_R3000             0x162   // MIPS little-endian, 0540 big-endian
#define IMAGE_FILE_MACHINE_R4000             0x166   // MIPS little-endian
#define IMAGE_FILE_MACHINE_ALPHA             0x184   // Alpha_AXP
#define IMAGE_FILE_MACHINE_POWERPC           0x1F0   // IBM PowerPC Little-Endian
#define IMAGE_FILE_MACHINE_TAHOE             0x7cc   // Intel EM machine
//
// Directory format.
//

typedef struct _IMAGE_DATA_DIRECTORY {
    UINT32   VirtualAddress;
    UINT32   Size;
} IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;

#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES    16

//
// Optional header format.
//

typedef struct _IMAGE_OPTIONAL_HEADER {
    //
    // Standard fields.
    //

    UINT16    Magic;
    UINT8     MajorLinkerVersion;
    UINT8     MinorLinkerVersion;
    UINT32    SizeOfCode;
    UINT32    SizeOfInitializedData;
    UINT32    SizeOfUninitializedData;
    UINT32    AddressOfEntryPoint;
    UINT32    BaseOfCode;
    UINT32    BaseOfData;
                
    //
    // NT additional fields.
    //

    UINT32   ImageBase;
    UINT32   SectionAlignment;
    UINT32   FileAlignment;
    UINT16   MajorOperatingSystemVersion;
    UINT16   MinorOperatingSystemVersion;
    UINT16   MajorImageVersion;
    UINT16   MinorImageVersion;
    UINT16   MajorSubsystemVersion;
    UINT16   MinorSubsystemVersion;
    UINT32   Reserved1;
    UINT32   SizeOfImage;
    UINT32   SizeOfHeaders;
    UINT32   CheckSum;
    UINT16   Subsystem;
    UINT16   DllCharacteristics;
    UINT32   SizeOfStackReserve;
    UINT32   SizeOfStackCommit;
    UINT32   SizeOfHeapReserve;
    UINT32   SizeOfHeapCommit;
    UINT32   LoaderFlags;
    UINT32   NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} IMAGE_OPTIONAL_HEADER, *PIMAGE_OPTIONAL_HEADER;

typedef struct _IMAGE_ROM_OPTIONAL_HEADER {
    UINT16  Magic;
    UINT8   MajorLinkerVersion;
    UINT8   MinorLinkerVersion;
    UINT32  SizeOfCode;
    UINT32  SizeOfInitializedData;
    UINT32  SizeOfUninitializedData;
    UINT32  AddressOfEntryPoint;
    UINT32  BaseOfCode;
    UINT32  BaseOfData;
    UINT32  BaseOfBss;
    UINT32  GprMask;
    UINT32  CprMask[4];
    UINT32  GpValue;
} IMAGE_ROM_OPTIONAL_HEADER, *PIMAGE_ROM_OPTIONAL_HEADER;

#define IMAGE_SIZEOF_ROM_OPTIONAL_HEADER      56
#define IMAGE_SIZEOF_STD_OPTIONAL_HEADER      28
#define IMAGE_SIZEOF_NT_OPTIONAL_HEADER      224

#define IMAGE_NT_OPTIONAL_HDR_MAGIC        0x10b
#define IMAGE_ROM_OPTIONAL_HDR_MAGIC       0x107

typedef struct _IMAGE_NT_HEADERS {
    UINT32 Signature;
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_OPTIONAL_HEADER OptionalHeader;
} IMAGE_NT_HEADERS, *PIMAGE_NT_HEADERS;

typedef struct _IMAGE_ROM_HEADERS {
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_ROM_OPTIONAL_HEADER OptionalHeader;
} IMAGE_ROM_HEADERS, *PIMAGE_ROM_HEADERS;

#define IMAGE_FIRST_SECTION( ntheader ) ((PIMAGE_SECTION_HEADER)        \
    ((UINT32)ntheader +                                                  \
     FIELD_OFFSET( IMAGE_NT_HEADERS, OptionalHeader ) +                 \
     ((PIMAGE_NT_HEADERS)(ntheader))->FileHeader.SizeOfOptionalHeader   \
    ))


// Subsystem Values

#define IMAGE_SUBSYSTEM_UNKNOWN              0   // Unknown subsystem.
#define IMAGE_SUBSYSTEM_NATIVE               1   // Image doesn't require a subsystem.
#define IMAGE_SUBSYSTEM_WINDOWS_GUI          2   // Image runs in the Windows GUI subsystem.
#define IMAGE_SUBSYSTEM_WINDOWS_CUI          3   // Image runs in the Windows character subsystem.
#define IMAGE_SUBSYSTEM_OS2_CUI              5   // image runs in the OS/2 character subsystem.
#define IMAGE_SUBSYSTEM_POSIX_CUI            7   // image run  in the Posix character subsystem.


// Directory Entries

#define IMAGE_DIRECTORY_ENTRY_EXPORT         0   // Export Directory
#define IMAGE_DIRECTORY_ENTRY_IMPORT         1   // Import Directory
#define IMAGE_DIRECTORY_ENTRY_RESOURCE       2   // Resource Directory
#define IMAGE_DIRECTORY_ENTRY_EXCEPTION      3   // Exception Directory
#define IMAGE_DIRECTORY_ENTRY_SECURITY       4   // Security Directory
#define IMAGE_DIRECTORY_ENTRY_BASERELOC      5   // Base Relocation Table
#define IMAGE_DIRECTORY_ENTRY_DEBUG          6   // Debug Directory
#define IMAGE_DIRECTORY_ENTRY_COPYRIGHT      7   // Description String
#define IMAGE_DIRECTORY_ENTRY_GLOBALPTR      8   // Machine Value (MIPS GP)
#define IMAGE_DIRECTORY_ENTRY_TLS            9   // TLS Directory
#define IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG   10   // Load Configuration Directory

//
// Section header format.
//

#define IMAGE_SIZEOF_SHORT_NAME              8

typedef struct _IMAGE_SECTION_HEADER {
    UINT8   Name[IMAGE_SIZEOF_SHORT_NAME];
    union {
            UINT32   PhysicalAddress;
            UINT32   VirtualSize;
    } Misc;
    UINT32   VirtualAddress;
    UINT32   SizeOfRawData;
    UINT32   PointerToRawData;
    UINT32   PointerToRelocations;
    UINT32   PointerToLinenumbers;
    UINT16   NumberOfRelocations;
    UINT16   NumberOfLinenumbers;
    UINT32   Characteristics;
} IMAGE_SECTION_HEADER, *PIMAGE_SECTION_HEADER;

#define IMAGE_SIZEOF_SECTION_HEADER          40

#define IMAGE_SCN_TYPE_NO_PAD                0x00000008  // Reserved.

#define IMAGE_SCN_CNT_CODE                   0x00000020  // Section contains code.
#define IMAGE_SCN_CNT_INITIALIZED_DATA       0x00000040  // Section contains initialized data.
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA     0x00000080  // Section contains uninitialized data.

#define IMAGE_SCN_LNK_OTHER                  0x00000100  // Reserved.
#define IMAGE_SCN_LNK_INFO                   0x00000200  // Section contains comments or some other type of information.
#define IMAGE_SCN_LNK_REMOVE                 0x00000800  // Section contents will not become part of image.
#define IMAGE_SCN_LNK_COMDAT                 0x00001000  // Section contents comdat.

#define IMAGE_SCN_ALIGN_1BYTES               0x00100000  //
#define IMAGE_SCN_ALIGN_2BYTES               0x00200000  //
#define IMAGE_SCN_ALIGN_4BYTES               0x00300000  //
#define IMAGE_SCN_ALIGN_8BYTES               0x00400000  //
#define IMAGE_SCN_ALIGN_16BYTES              0x00500000  // Default alignment if no others are specified.
#define IMAGE_SCN_ALIGN_32BYTES              0x00600000  //
#define IMAGE_SCN_ALIGN_64BYTES              0x00700000  //

#define IMAGE_SCN_MEM_DISCARDABLE            0x02000000  // Section can be discarded.
#define IMAGE_SCN_MEM_NOT_CACHED             0x04000000  // Section is not cachable.
#define IMAGE_SCN_MEM_NOT_PAGED              0x08000000  // Section is not pageable.
#define IMAGE_SCN_MEM_SHARED                 0x10000000  // Section is shareable.
#define IMAGE_SCN_MEM_EXECUTE                0x20000000  // Section is executable.
#define IMAGE_SCN_MEM_READ                   0x40000000  // Section is readable.
#define IMAGE_SCN_MEM_WRITE                  0x80000000  // Section is writeable.

//
// Symbol format.
//


#define IMAGE_SIZEOF_SYMBOL                  18

//
// Section values.
//
// Symbols have a section number of the section in which they are
// defined. Otherwise, section numbers have the following meanings:
//

#define IMAGE_SYM_UNDEFINED           (UINT16)0           // Symbol is undefined or is common.
#define IMAGE_SYM_ABSOLUTE            (UINT16)-1          // Symbol is an absolute value.
#define IMAGE_SYM_DEBUG               (UINT16)-2          // Symbol is a special debug item.

//
// Type (fundamental) values.
//

#define IMAGE_SYM_TYPE_NULL                  0           // no type.
#define IMAGE_SYM_TYPE_VOID                  1           //
#define IMAGE_SYM_TYPE_CHAR                  2           // type character.
#define IMAGE_SYM_TYPE_SHORT                 3           // type short integer.
#define IMAGE_SYM_TYPE_INT                   4           //
#define IMAGE_SYM_TYPE_LONG                  5           //
#define IMAGE_SYM_TYPE_FLOAT                 6           //
#define IMAGE_SYM_TYPE_DOUBLE                7           //
#define IMAGE_SYM_TYPE_STRUCT                8           //
#define IMAGE_SYM_TYPE_UNION                 9           //
#define IMAGE_SYM_TYPE_ENUM                  10          // enumeration.
#define IMAGE_SYM_TYPE_MOE                   11          // member of enumeration.
#define IMAGE_SYM_TYPE_BYTE                  12          //
#define IMAGE_SYM_TYPE_WORD                  13          //
#define IMAGE_SYM_TYPE_UINT                  14          //
#define IMAGE_SYM_TYPE_DWORD                 15          //

//
// Type (derived) values.
//

#define IMAGE_SYM_DTYPE_NULL                 0           // no derived type.
#define IMAGE_SYM_DTYPE_POINTER              1           // pointer.
#define IMAGE_SYM_DTYPE_FUNCTION             2           // function.
#define IMAGE_SYM_DTYPE_ARRAY                3           // array.

//
// Storage classes.
//

#define IMAGE_SYM_CLASS_END_OF_FUNCTION      (BYTE )-1
#define IMAGE_SYM_CLASS_NULL                 0
#define IMAGE_SYM_CLASS_AUTOMATIC            1
#define IMAGE_SYM_CLASS_EXTERNAL             2
#define IMAGE_SYM_CLASS_STATIC               3
#define IMAGE_SYM_CLASS_REGISTER             4
#define IMAGE_SYM_CLASS_EXTERNAL_DEF         5
#define IMAGE_SYM_CLASS_LABEL                6
#define IMAGE_SYM_CLASS_UNDEFINED_LABEL      7
#define IMAGE_SYM_CLASS_MEMBER_OF_STRUCT     8
#define IMAGE_SYM_CLASS_ARGUMENT             9
#define IMAGE_SYM_CLASS_STRUCT_TAG           10
#define IMAGE_SYM_CLASS_MEMBER_OF_UNION      11
#define IMAGE_SYM_CLASS_UNION_TAG            12
#define IMAGE_SYM_CLASS_TYPE_DEFINITION      13
#define IMAGE_SYM_CLASS_UNDEFINED_STATIC     14
#define IMAGE_SYM_CLASS_ENUM_TAG             15
#define IMAGE_SYM_CLASS_MEMBER_OF_ENUM       16
#define IMAGE_SYM_CLASS_REGISTER_PARAM       17
#define IMAGE_SYM_CLASS_BIT_FIELD            18
#define IMAGE_SYM_CLASS_BLOCK                100
#define IMAGE_SYM_CLASS_FUNCTION             101
#define IMAGE_SYM_CLASS_END_OF_STRUCT        102
#define IMAGE_SYM_CLASS_FILE                 103
// new
#define IMAGE_SYM_CLASS_SECTION              104
#define IMAGE_SYM_CLASS_WEAK_EXTERNAL        105

// type packing constants

#define N_BTMASK                            017
#define N_TMASK                             060
#define N_TMASK1                            0300
#define N_TMASK2                            0360
#define N_BTSHFT                            4
#define N_TSHIFT                            2

// MACROS

//
// Communal selection types.
//

#define IMAGE_COMDAT_SELECT_NODUPLICATES   1
#define IMAGE_COMDAT_SELECT_ANY            2
#define IMAGE_COMDAT_SELECT_SAME_SIZE      3
#define IMAGE_COMDAT_SELECT_EXACT_MATCH    4
#define IMAGE_COMDAT_SELECT_ASSOCIATIVE    5

#define IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY 1
#define IMAGE_WEAK_EXTERN_SEARCH_LIBRARY   2
#define IMAGE_WEAK_EXTERN_SEARCH_ALIAS     3


//
// Relocation format.
//

typedef struct _IMAGE_RELOCATION {
    UINT32   VirtualAddress;
    UINT32   SymbolTableIndex;
    UINT16    Type;
} IMAGE_RELOCATION;

#define IMAGE_SIZEOF_RELOCATION              10

//
// I386 relocation types.
//

#define IMAGE_REL_I386_ABSOLUTE              0           // Reference is absolute, no relocation is necessary
#define IMAGE_REL_I386_DIR16                 01          // Direct 16-bit reference to the symbols virtual address
#define IMAGE_REL_I386_REL16                 02          // PC-relative 16-bit reference to the symbols virtual address
#define IMAGE_REL_I386_DIR32                 06          // Direct 32-bit reference to the symbols virtual address
#define IMAGE_REL_I386_DIR32NB               07          // Direct 32-bit reference to the symbols virtual address, base not included
#define IMAGE_REL_I386_SEG12                 011         // Direct 16-bit reference to the segment-selector bits of a 32-bit virtual address
#define IMAGE_REL_I386_SECTION               012
#define IMAGE_REL_I386_SECREL                013
#define IMAGE_REL_I386_REL32                 024         // PC-relative 32-bit reference to the symbols virtual address

//
// MIPS relocation types.
//

#define IMAGE_REL_MIPS_ABSOLUTE              0           // Reference is absolute, no relocation is necessary
#define IMAGE_REL_MIPS_REFHALF               01
#define IMAGE_REL_MIPS_REFWORD               02
#define IMAGE_REL_MIPS_JMPADDR               03
#define IMAGE_REL_MIPS_REFHI                 04
#define IMAGE_REL_MIPS_REFLO                 05
#define IMAGE_REL_MIPS_GPREL                 06
#define IMAGE_REL_MIPS_LITERAL               07
#define IMAGE_REL_MIPS_SECTION               012
#define IMAGE_REL_MIPS_SECREL                013
#define IMAGE_REL_MIPS_REFWORDNB             042
#define IMAGE_REL_MIPS_PAIR                  045

//
// Alpha Relocation types.
//

#define IMAGE_REL_ALPHA_ABSOLUTE             0x0
#define IMAGE_REL_ALPHA_REFLONG              0x1
#define IMAGE_REL_ALPHA_REFQUAD              0x2
#define IMAGE_REL_ALPHA_GPREL32              0x3
#define IMAGE_REL_ALPHA_LITERAL              0x4
#define IMAGE_REL_ALPHA_LITUSE               0x5
#define IMAGE_REL_ALPHA_GPDISP               0x6
#define IMAGE_REL_ALPHA_BRADDR               0x7
#define IMAGE_REL_ALPHA_HINT                 0x8
#define IMAGE_REL_ALPHA_INLINE_REFLONG       0x9
#define IMAGE_REL_ALPHA_REFHI                0xA
#define IMAGE_REL_ALPHA_REFLO                0xB
#define IMAGE_REL_ALPHA_PAIR                 0xC
#define IMAGE_REL_ALPHA_MATCH                0xD
#define IMAGE_REL_ALPHA_SECTION              0xE
#define IMAGE_REL_ALPHA_SECREL               0xF
#define IMAGE_REL_ALPHA_REFLONGNB            0x10

//
// IBM PowerPC relocation types.
//

#define IMAGE_REL_PPC_ABSOLUTE 0x0000  // NOP
#define IMAGE_REL_PPC_ADDR64   0x0001  // 64-bit address
#define IMAGE_REL_PPC_ADDR32   0x0002  // 32-bit address
#define IMAGE_REL_PPC_ADDR24   0x0003  // 26-bit address, shifted left 2 (branch absolute)
#define IMAGE_REL_PPC_ADDR16   0x0004  // 16-bit address
#define IMAGE_REL_PPC_ADDR14   0x0005  // 16-bit address, shifted left 2 (load doubleword)
#define IMAGE_REL_PPC_REL24    0x0006  // 26-bit PC-relative offset, shifted left 2 (branch relative)
#define IMAGE_REL_PPC_REL14    0x0007  // 16-bit PC-relative offset, shifted left 2 (br cond relative)
#define IMAGE_REL_PPC_TOCREL16 0x0008  // 16-bit offset from TOC base
#define IMAGE_REL_PPC_TOCREL14 0x0009  // 16-bit offset from TOC base, shifted left 2 (load doubleword)

#define IMAGE_REL_PPC_ADDR32NB 0x000A  // 32-bit addr w/o image base
#define IMAGE_REL_PPC_SECREL   0x000B  // va of containing section (as in an image sectionhdr)
#define IMAGE_REL_PPC_SECTION  0x000C  // sectionheader number
#define IMAGE_REL_PPC_IFGLUE   0x000D  // substitute TOC restore instruction iff symbol is glue code
#define IMAGE_REL_PPC_IMGLUE   0x000E  // symbol is glue code; virtual address is TOC restore instruction

#define IMAGE_REL_PPC_TYPEMASK 0x00FF  // mask to isolate above values in IMAGE_RELOCATION.Type

// Flag bits in IMAGE_RELOCATION.TYPE

#define IMAGE_REL_PPC_NEG      0x0100  // subtract reloc value rather than adding it
#define IMAGE_REL_PPC_BRTAKEN  0x0200  // fix branch prediction bit to predict branch taken
#define IMAGE_REL_PPC_BRNTAKEN 0x0400  // fix branch prediction bit to predict branch not taken
#define IMAGE_REL_PPC_TOCDEFN  0x0800  // toc slot defined in file (or, data in toc)

//
// Based relocation format.
//

typedef struct _IMAGE_BASE_RELOCATION {
    UINT32   VirtualAddress;
    UINT32   SizeOfBlock;
//  UINT16    TypeOffset[1];
} IMAGE_BASE_RELOCATION, *PIMAGE_BASE_RELOCATION;

#define IMAGE_SIZEOF_BASE_RELOCATION         8

//
// Based relocation types.
//

#define IMAGE_REL_BASED_ABSOLUTE              0
#define IMAGE_REL_BASED_HIGH                  1
#define IMAGE_REL_BASED_LOW                   2
#define IMAGE_REL_BASED_HIGHLOW               3
#define IMAGE_REL_BASED_HIGHADJ               4
#define IMAGE_REL_BASED_MIPS_JMPADDR          5
#define IMAGE_REL_BASED_DIR64                 10

//
// Line number format.
//

typedef struct _IMAGE_LINENUMBER {
    union {
        UINT32   SymbolTableIndex;               // Symbol table index of function name if Linenumber is 0.
        UINT32   VirtualAddress;                 // Virtual address of line number.
    } Type;
    UINT16    Linenumber;                         // Line number.
} IMAGE_LINENUMBER;

#define IMAGE_SIZEOF_LINENUMBER              6

//
// Archive format.
//

#define IMAGE_ARCHIVE_START_SIZE             8
#define IMAGE_ARCHIVE_START                  "!<arch>\n"
#define IMAGE_ARCHIVE_END                    "`\n"
#define IMAGE_ARCHIVE_PAD                    "\n"
#define IMAGE_ARCHIVE_LINKER_MEMBER          "/               "
#define IMAGE_ARCHIVE_LONGNAMES_MEMBER       "//              "

typedef struct _IMAGE_ARCHIVE_MEMBER_HEADER {
    UINT8     Name[16];                          // File member name - `/' terminated.
    UINT8     Date[12];                          // File member date - decimal.
    UINT8     UserID[6];                         // File member user id - decimal.
    UINT8     GroupID[6];                        // File member group id - decimal.
    UINT8     Mode[8];                           // File member mode - octal.
    UINT8     Size[10];                          // File member size - decimal.
    UINT8     EndHeader[2];                      // String to end header.
} IMAGE_ARCHIVE_MEMBER_HEADER, *PIMAGE_ARCHIVE_MEMBER_HEADER;

#define IMAGE_SIZEOF_ARCHIVE_MEMBER_HDR      60

//
// DLL support.
//

//
// Export Format
//

typedef struct _IMAGE_EXPORT_DIRECTORY {
    UINT32   Characteristics;
    UINT32   TimeDateStamp;
    UINT16   MajorVersion;
    UINT16   MinorVersion;
    UINT32   Name;
    UINT32   Base;
    UINT32   NumberOfFunctions;
    UINT32   NumberOfNames;
    UINT32   *AddressOfFunctions;
    UINT32   *AddressOfNames;
    UINT32   *AddressOfNameOrdinals;
} IMAGE_EXPORT_DIRECTORY, *PIMAGE_EXPORT_DIRECTORY;

//
// Import Format
//

typedef struct _IMAGE_IMPORT_BY_NAME {
    UINT16    Hint;
    UINT8     Name[1];
} IMAGE_IMPORT_BY_NAME, *PIMAGE_IMPORT_BY_NAME;

typedef struct _IMAGE_THUNK_DATA {
    union {
        UINT32 Function;
        UINT32 Ordinal;
        PIMAGE_IMPORT_BY_NAME AddressOfData;
    } u1;
} IMAGE_THUNK_DATA, *PIMAGE_THUNK_DATA;

#define IMAGE_ORDINAL_FLAG 0x80000000
#define IMAGE_SNAP_BY_ORDINAL(Ordinal) ((Ordinal & IMAGE_ORDINAL_FLAG) != 0)
#define IMAGE_ORDINAL(Ordinal) (Ordinal & 0xffff)

typedef struct _IMAGE_IMPORT_DESCRIPTOR {
    UINT32   Characteristics;
    UINT32   TimeDateStamp;
    UINT32   ForwarderChain;
    UINT32   Name;
    PIMAGE_THUNK_DATA FirstThunk;
} IMAGE_IMPORT_DESCRIPTOR, *PIMAGE_IMPORT_DESCRIPTOR;

#define IMAGE_DEBUG_TYPE_CODEVIEW   2

typedef struct {
  UINT32    Characteristics;
  UINT32    TimeDateStamp;
  UINT16    MajorVersion;
  UINT16    MinorVersion;
  UINT32    Type;
  UINT32    SizeOfData;
  UINT32    RVA;
  UINT32    FileOffset;
} IMAGE_DEBUG_DIRECTORY_ENTRY;

#define CODEVIEW_SIGNATURE_NB10  0x3031424E // "NB10"

typedef struct {
  UINT32    Signature; // "NB10"
  UINT32    Unknown;
  UINT32    Unknown2;
  UINT32    Unknown3;     
  //
  // Filename of .PDB goes here
  //
} EFI_IMAGE_DEBUG_CODEVIEW_NB10_ENTRY;

#define CODEVIEW_SIGNATURE_RSDS  0x53445352 // "RSDS"

typedef struct {
  UINT32    Signature; // "RSDS"
  UINT32    Unknown;
  UINT32    Unknown2;
  UINT32    Unknown3;     
  UINT32    Unknown4;     
  UINT32    Unknown5;     
  //
  // Filename of .PDB goes here
  //
} EFI_IMAGE_DEBUG_CODEVIEW_RSDS_ENTRY;

#endif
