/*****************************************************************************

Copyright (c) 2010 by Nanoradio AB

This software is copyrighted by and is the sole property of Nanoradio AB.
All rights, title, ownership, or other interests in the
software remain the property of Nanoradio AB.  This software may
only be used in accordance with the corresponding license agreement. 
Any unauthorized use, duplication, transmission, distribution, or disclosure 
of this software is expressly forbidden.

This Copyright notice may not be removed or modified without prior written
consent of Nanoradio AB.

Nanoradio AB reserves the right to modify this software without notice.

Nanoradio AB
Torshamnsgatan 39                       info@nanoradio.se
164 40 Kista                            http://www.nanoradio.se
SWEDEN

Module Description :
==================
*****************************************************************************/

#include "driverenv.h"
#include <stddef.h> // for offsetof(...)
#include "transport.h"
#include "transport_platform.h"
#include "file_numbers.h"
#include "we_dlm.h"

#undef   FILE_NUMBER
#define  FILE_NUMBER         FILE_AXF_FIRMEWARE_C

#ifndef EM_ARM
#define EM_ARM 40
#endif
#ifndef EM_NORC
#define EM_NORC 0x625
#endif

/**************************************/
/*            data_handler            */
/**************************************/

struct bm_descr {
   uint32_t buf_ptr;
   uint32_t ctrl;
   uint16_t size;
   uint16_t expedited_size;
   uint32_t next;
};

#define BM_SIZE     sizeof(struct bm_descr)

struct load_header {
    struct bm_descr base;
    struct bm_descr len;
    struct bm_descr addr;
    struct bm_descr payload;
};

#define ARM_START_EXEC_ADDR     0x46000
#define ARM_BM_BASE             0x46100
#define ARM_TRIG_SWI_ADDR       0x00072040
#define ARM_TRIG_SWI_DATA       0xffffffff

#define NORC_START_EXEC_ADDR    0x0004FFFC
#define NORC_BM_BASE            0
#define NORC_TRIG_SWI_ADDR      0xfffc1030
#define NORC_TRIG_SWI_DATA      0x00000002

#define HARMLESS_ADDRESS_IN_NRX2_CHIP 0x60000

#define BUFFER_SIZE          1500
#define PAYLOAD_HEADERSIZE   6
#define PAYLOAD_MAXSIZE      60

#undef E
#ifdef __GNUC__ /* really C99 */
#define E(X) .X =
#else
#define E(X)
#endif

struct load_header load_header = {
    E(base) {
        E(buf_ptr) BM_SIZE,
        E(ctrl) 0,
        E(size) 3 * BM_SIZE,
        E(expedited_size) 0,
        E(next) BM_SIZE
    },
    E(len) {
        E(buf_ptr) 3 * BM_SIZE + offsetof(struct bm_descr, size),
        E(ctrl) 0,
        E(size) sizeof(uint16_t),
        E(expedited_size) 0,
        E(next) 2 * BM_SIZE
    },
    E(addr) {
        E(buf_ptr) 3 * BM_SIZE + offsetof(struct bm_descr, buf_ptr),
        E(ctrl) 0,
        E(size) sizeof(uint32_t),
        E(expedited_size) 0,
        E(next) 3 * BM_SIZE
    },
    E(payload) {
        E(buf_ptr) 0,
        E(ctrl) 0,
        E(size) 0,
        E(expedited_size) 0,
        E(next) BM_SIZE
    }
};

/* 32-bit ELF base types. */
typedef uint32_t    Elf32_Addr;
typedef uint16_t    Elf32_Half;
typedef uint32_t    Elf32_Off;
typedef int32_t     Elf32_Sword;
typedef uint32_t    Elf32_Word;

//#pragma pack(push)
//#pragma pack(1)

#define EI_NIDENT            16

typedef struct elf32_hdr {
    unsigned char    e_ident[EI_NIDENT];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;  /* Entry point */
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
}
Elf32_Ehdr;

// Only valid value for e_type
#define ET_EXEC              2

typedef struct elf32_phdr {
    Elf32_Word    p_type;
    Elf32_Off     p_offset;
    Elf32_Addr    p_vaddr;
    Elf32_Addr    p_paddr;
    Elf32_Word    p_filesz;
    Elf32_Word    p_memsz;
    Elf32_Word    p_flags;
    Elf32_Word    p_align;
}
Elf32_Phdr;

typedef struct elf32_shdr {
    Elf32_Word    sh_name;
    Elf32_Word    sh_type;
    Elf32_Word    sh_flags;
    Elf32_Addr    sh_addr;
    Elf32_Off     sh_offset;
    Elf32_Word    sh_size;
    Elf32_Word    sh_link;
    Elf32_Word    sh_info;
    Elf32_Word    sh_addralign;
    Elf32_Word    sh_entsize;
}
Elf32_Shdr;

// Section header information is defined by the program
#define SHT_PROGBITS              1

//#pragma pack(pop)

/**************************************/
/*            axf/elf helper          */
/**************************************/

struct axf_hlpr {
    const char* path;           // Path to file
    de_file_ref_t fh;           // File handler
    int len;                    // File length
    int seek;                   // Current position in file
    Elf32_Ehdr *ehdr;           // Elf Header
    Elf32_Phdr *phdr;           // Elf Program Header
    Elf32_Shdr *shdr;           // Elf Section Header
    Elf32_Shdr *shdr_shstrtab;  // Elf Section Header string table
    Elf32_Addr mib_table_addr;  // Address to mib_table
    Elf32_Word mib_table_size;  // Size of mib_table
};

/**************************************/
/*  Architecture dependent variables  */
/**************************************/
struct arch_dep {
    unsigned int bm_offset;       // Offset for first 64 bytes of fw write
    unsigned int start_exec_addr; // Program start address
    unsigned int trig_swi_addr;   // Software start trigger address
    unsigned int trig_swi_data;   // Software start trigger data
};

#define ELFPH_ELM(_src,_elm) ((Elf32_Phdr*)(_src))[_elm]

#define IS_DLM_ENTRY(ehdr, phdr) \
   (ehdr->e_machine == EM_ARM && (phdr)->p_paddr >= 0x1000000)

#define IS_MIB_TABLE(ehdr, phdr)                                        \
   (axf->mib_table_addr >= (phdr)->p_vaddr                                   \
    && axf->mib_table_addr + axf->mib_table_size <= (phdr)->p_vaddr + (phdr)->p_filesz)

static void file_seek(
      struct axf_hlpr *axf,
      int offset)
{

    if ( axf->seek > offset ) 
    {
        de_fclose(axf->fh);

        axf->fh = de_fopen(axf->path, DE_FRDONLY);
        
        DE_BUG_ON(!de_f_is_open(axf->fh), "file_seek failed to reopen file!");
        
        axf->seek = 0;
    }

    while ( axf->seek < offset) 
    {
        int read;
        char tmp[256];
        int read_size = sizeof(tmp);
        
        if( offset - axf->seek < read_size )
        {
            read_size = 1;
        }

        read = de_fread(axf->fh, tmp, read_size);
        
        if (read <= 0) break;
        axf->seek += read;
    }
}

static int file_size(struct axf_hlpr *axf) {

    int seek = axf->seek;
    int size = 0;
    int read;
    int tmp[256];
    int read_size = sizeof(tmp);

    // Re-open file
    de_fclose(axf->fh);
    axf->fh = de_fopen(axf->path, DE_FRDONLY);
    DE_BUG_ON(!de_f_is_open(axf->fh), "file_size failed to reopen file!");

    axf->seek = 0;

    do
    {
        read = de_fread(axf->fh, tmp, read_size);
        size += read;
        axf->seek += read;
    }while(read == read_size);

    file_seek(axf, seek);

    return size;
}

static void axf_destroy(struct axf_hlpr *axf) {
    if ( axf->ehdr != NULL )
    {
        DriverEnvironment_Free(axf->ehdr);
        axf->ehdr = NULL;
    }
    
    if ( axf->phdr != NULL )
    {
        DriverEnvironment_Free(axf->phdr);
        axf->phdr = NULL;
    }
    
    if (axf->shdr != NULL )
    {
        DriverEnvironment_Free(axf->shdr);
        axf->shdr = NULL;
    }

    axf->shdr_shstrtab = NULL;

    if ( axf->fh >= 0 )
    {
        de_fclose(axf->fh);
        axf->fh = 0;
    }
}

/* Create a data chunk of PAYLOAD_HEADERSIZE + PAYLOAD_MAXSIZE bytes long
 * --------------------------------------------------
 * | len | len | addr | addr | addr | addr | data ...
 * --------------------------------------------------
 */
static size_t
make_data_header(unsigned char *buf,
                 size_t len,
                 Elf32_Addr addr,
                 void *data,
                 size_t data_len)
{
   unsigned char *hdr = buf;

   while(len > 0 && data_len > 0) {
      size_t l = data_len;
      if(l > PAYLOAD_MAXSIZE)
         l = PAYLOAD_MAXSIZE;

      hdr[0] = l & 0xff;
      hdr[1] = (l >> 8) & 0xff;
      hdr[2] = addr & 0xff;
      hdr[3] = (addr >> 8) & 0xff;
      hdr[4] = (addr >> 16) & 0xff;
      hdr[5] = (addr >> 24) & 0xff;

      memcpy(hdr + PAYLOAD_HEADERSIZE, data, l);

      addr += l;
      data_len -= l;
      data = (unsigned char*)data + l;
      hdr += PAYLOAD_HEADERSIZE + l;
      len -= PAYLOAD_HEADERSIZE + l;
   }
   return hdr - buf;
}

/*!
 * @return 0 on no file, 1 on ok, -1 on failure
 */
static int axf_fw_init(struct axf_hlpr *axf, struct arch_dep *arch) {

#define ELFMAG                                  \
    "\177ELF"  /* magic */                      \
    "\1"       /* ELF32 */                      \
    "\1"       /* LSB */                        \
    "\1"       /* file version */               \
    "\0"       /* ABI */                        \
    "\0"       /* ABI version */ /* padding follows */
#define ELFMAG_SIZE (sizeof(ELFMAG) - 1)

    int read;
    int size;

    axf->fh = 0;
    axf->ehdr = NULL;
    axf->phdr = NULL;
    axf->shdr = NULL;
    axf->seek = 0;

    do {
        axf->fh = de_fopen(axf->path, DE_FRDONLY);
        
        if (!de_f_is_open(axf->fh)) {
            return 0;
        }

        axf->len = file_size(axf);

        // Read ELF Header
        axf->ehdr = (Elf32_Ehdr*)DriverEnvironment_Malloc(sizeof(Elf32_Ehdr));
        if (axf->ehdr == NULL) break;

        read = de_fread(axf->fh, axf->ehdr, sizeof(Elf32_Ehdr));
        if ( read < sizeof(Elf32_Ehdr) ) break;
        axf->seek += read;

        // Check ELF Header: Identification
        if(DE_MEMCMP(axf->ehdr, ELFMAG, ELFMAG_SIZE) != 0) {
            DE_TRACE_STATIC(TR_INITIALIZE,"no ELF magic\n");
            return 0;
        }

        // Check ELF Header: File type
        if (axf->ehdr->e_type != ET_EXEC) {
            DE_TRACE_INT(TR_INITIALIZE,"%d not ET_EXEC\n", axf->ehdr->e_type);
            return 0;
        }

        // Check ELF Header: Machine type
        if(axf->ehdr->e_machine == EM_ARM) {
            arch->bm_offset       = ARM_BM_BASE;
            arch->start_exec_addr = ARM_START_EXEC_ADDR;
            arch->trig_swi_addr   = ARM_TRIG_SWI_ADDR;
            arch->trig_swi_data   = ARM_TRIG_SWI_DATA;
        } else if(axf->ehdr->e_machine == EM_NORC) {
            arch->bm_offset       = NORC_BM_BASE;
            arch->start_exec_addr = NORC_START_EXEC_ADDR;
            arch->trig_swi_addr   = NORC_TRIG_SWI_ADDR;
            arch->trig_swi_data   = NORC_TRIG_SWI_DATA;
        } else {
            return -1;
        }

        // Check ELF Header: Program header structure
        if(sizeof(*axf->phdr) != axf->ehdr->e_phentsize) {
            DE_TRACE_STATIC(TR_INITIALIZE,"ELF file with unexpected program header size\n");
           return -1;
        }

        // Check ELF Header: Program header integrity
        if(axf->len < axf->ehdr->e_phoff + axf->ehdr->e_phnum * axf->ehdr->e_phentsize) {
            DE_TRACE_STATIC(TR_INITIALIZE,"ELF program headers extend beyond end of file\n");
           return -1;
        }

        // Read Program header
        size = (axf->ehdr->e_phentsize) * (axf->ehdr->e_phnum);
        axf->phdr = (Elf32_Phdr*)DriverEnvironment_Malloc(size);
        if (axf->phdr == NULL) break;

        file_seek(axf, axf->ehdr->e_phoff);
        if ( axf->seek != axf->ehdr->e_phoff ) break;

        read = de_fread(axf->fh, axf->phdr, size);
        if (read < size) break;
        axf->seek += read;

        // Check ELF header: Section header structure
        if(sizeof(*axf->shdr) != axf->ehdr->e_shentsize) {
            DE_TRACE_STATIC(TR_INITIALIZE,"ELF file with unexpected section header size\n");
           return -1;
        }

        // Check ELF header: Section header integrity
        if(axf->len < axf->ehdr->e_shoff + axf->ehdr->e_shnum * axf->ehdr->e_shentsize) {
            DE_TRACE_STATIC(TR_INITIALIZE,"ELF section headers extend beyond end of file\n");
           return -1;
        }

        // Read Section header
        size = (axf->ehdr->e_shentsize) * (axf->ehdr->e_shnum);
        axf->shdr = (Elf32_Shdr*)DriverEnvironment_Malloc(size);
        if (axf->shdr == NULL) break;

        file_seek(axf, axf->ehdr->e_shoff);
        if ( axf->seek != axf->ehdr->e_shoff ) break;

        read = de_fread(axf->fh, axf->shdr, size);
        if (read < size) break;
        axf->seek += read;

        // Check ELF header: Section header string index integrity
        if(axf->ehdr->e_shstrndx >= axf->ehdr->e_shnum) {
            DE_TRACE_STATIC(TR_INITIALIZE,"ELF section header string index points to missing section\n");
           return -1;
        }

        // Check ELF header: Section header string table integrity
        axf->shdr_shstrtab = &axf->shdr[axf->ehdr->e_shstrndx];
        if(axf->shdr_shstrtab->sh_offset >= axf->len) {
            DE_TRACE_STATIC(TR_INITIALIZE,"ELF section header string table starts beyond end of file\n");
            return -1;
        }

        // Check ELF header: Section header string table integrity
        if(axf->shdr_shstrtab->sh_offset + axf->shdr_shstrtab->sh_size >= axf->len) {
            DE_TRACE_STATIC(TR_INITIALIZE,"ELF section header string table extends beyond end of file\n");
            return -1;
        }

        return 1;   /* init ok */
    } while (0);

    /* something went wrong, do cleanup */
    axf_destroy(axf);

    return -1;
}

static void
setup_load_header(uint32_t base)
{
   if (base == 0)
   {
      /*
      This code will cause garbage data to be written at
      some harmless address in the chip.
      Necessary in order to support nrx2(B11) while still
      being backwards compatible with nrx1 without any
      changes in transport layer part of fw download procedure.
      */
      char * w_pos;
      uint16_t len;
      uint32_t addr;

      w_pos = (char *)&load_header;
      len   = sizeof(load_header) - sizeof(len) - sizeof(addr);
      addr  = HARMLESS_ADDRESS_IN_NRX2_CHIP;
      memcpy(w_pos, (char *)&len, sizeof(len));
      memcpy(&w_pos[sizeof(len)], (char *)&addr, sizeof(addr));
   }
   else
   {
      load_header.base.buf_ptr = base + BM_SIZE;
      load_header.base.next    = base + BM_SIZE;
      load_header.len.buf_ptr  = base + 3 * BM_SIZE + offsetof(struct bm_descr, size);
      load_header.len.next     = base + 2 * BM_SIZE;
      load_header.addr.buf_ptr = base + 3 * BM_SIZE + offsetof(struct bm_descr, buf_ptr);
      load_header.addr.next    = base + 3 * BM_SIZE;
      load_header.payload.next = base + BM_SIZE;
   }
}

static int axf_build_fw_buf(unsigned char *buf, struct axf_hlpr *axf, struct arch_dep *arch)
{
    int size;
    int pad_size;
    int offset;
    int read;
    unsigned char* segment;
    int i;

    size = 0;
    pad_size = 0;
    offset = 0;
    read = 0;
    segment = NULL;

    // Check size
    size += sizeof(load_header);
    for(i = 0; i < axf->ehdr->e_phnum; i++)
    {
       if(IS_DLM_ENTRY(axf->ehdr, &axf->phdr[i])) {
           continue;
       }
       if(axf->len < axf->phdr[i].p_offset + axf->phdr[i].p_filesz) {
           return -1;
       }
       // Size of the data
       size += axf->phdr[i].p_filesz;

       // Size of the addr and length of each 60 byte block
       size += ((axf->phdr[i].p_filesz +  PAYLOAD_MAXSIZE - 1) / PAYLOAD_MAXSIZE) * PAYLOAD_HEADERSIZE;
    }
    size += PAYLOAD_HEADERSIZE + 4; /* exec addr */
    size += PAYLOAD_HEADERSIZE + 4; /* trig */

#ifndef NO_FIRMWARE_PADDING
    /* Pad the firmware blob to a multiple of the hardware transport
       size alignment. There is also a min size requirement, but for
       sake of simplicity, assume that the firmware is never smaller
       than that (usually 32 bytes if it exists). */
#undef REMAIN
#define REMAIN(S, A) ((S) & ((A) - 1))
    if(REMAIN(size, wifiEngineState.config.pdu_size_alignment) != 0) {
        pad_size = wifiEngineState.config.pdu_size_alignment - REMAIN(size, wifiEngineState.config.pdu_size_alignment);
        while(pad_size <= PAYLOAD_HEADERSIZE) {
            /* the bm dma should work with zero bytes, but
             * make_data_header gets confused with empty buffers, so make
             * sure there is some data for now */
            pad_size += wifiEngineState.config.pdu_size_alignment;
        }
    } else {
        pad_size = 0;
    }

    size += pad_size;
#endif /* NO_FIRMWARE_PADDING */

    // Start moving data to buffert
    memcpy(buf + offset, &load_header, sizeof(load_header));
    offset += sizeof(load_header);

#ifndef NO_FIRMWARE_PADDING
    if(pad_size != 0) {
        DE_BUG_ON(pad_size > axf->len, "pad_size !< axf->len: pad_size = %zu, len = %zu\n", pad_size, axf->len);
#define SAFE_RAM_ADDRESS 0x40000 /* XXX */
        offset += make_data_header(buf + offset,
                                   size - offset,
                                   SAFE_RAM_ADDRESS,
                                   buf, /* Read junk data from somewhere */
                                   pad_size - PAYLOAD_HEADERSIZE);
    }
#endif /* NO_FIRMWARE_PADDING */

    we_dlm_flush();
    for(i = 0; i < axf->ehdr->e_phnum; i++) {
        file_seek(axf, axf->phdr[i].p_offset);
        segment = DriverEnvironment_Malloc(axf->phdr[i].p_filesz);
        if (segment != NULL) {
            read = de_fread(axf->fh, segment, axf->phdr[i].p_filesz);
            axf->seek += read;
        } else {
            DE_TRACE_STATIC(TR_INITIALIZE,"Unable to allocate memory for reading segments\n");
            return -1;
        }

        if(IS_DLM_ENTRY(axf->ehdr, &axf->phdr[i])) {
            DE_TRACE_STATIC(TR_INITIALIZE,"Found DLM entery\n");
            char *p = we_dlm_register("unknown",
                                axf->phdr[i].p_vaddr,
                                axf->phdr[i].p_filesz);
            if(p) {
                memcpy(p, segment, axf->phdr[i].p_filesz);
            }
        } else {
            offset += make_data_header(buf + offset,
                                       size - offset,
                                       axf->phdr[i].p_vaddr,
                                       segment,
                                       axf->phdr[i].p_filesz);
            if(IS_MIB_TABLE(axf->ehdr, &axf->phdr[i])) {
                if( axf->mib_table_size == 0 )
                    axf->mib_table_size = axf->phdr[i].p_filesz;
                /* if the mib table is in a separate load segment, then
                 * mib_table_addr == p_vaddr and mib_table_size ==
                 * p_filesz, if not this will select a piece of the
                 * segment */
                WiFiEngine_RegisterMIBTable(segment + axf->mib_table_addr - axf->phdr[i].p_vaddr,
                                            axf->mib_table_size,
                                            axf->mib_table_addr);
/*            if(IS_MIB_TABLE(axf->ehdr, &axf->phdr[i])) {
                if( axf->mib_table_size == 0 )
                    axf->mib_table_size = axf->phdr[i].p_filesz;
                 if the mib table is in a separate load segment, then
                 * mib_table_addr == p_vaddr and mib_table_size ==
                 * p_filesz, if not this will select a piece of the
                 * segment
                 Store mib table as an dlm object for later use
                char *p = we_dlm_register("mib_table",
                        axf->mib_table_addr,
                        axf->mib_table_size);
                if(p) {
                    memcpy(p, segment, axf->phdr[i].p_filesz);
                } */
            }
        }
        DriverEnvironment_Free(segment);
    }
    offset += make_data_header(buf + offset,
                   size - offset,
                   arch->start_exec_addr,
                   &axf->ehdr->e_entry,
                   sizeof(axf->ehdr->e_entry));
    offset += make_data_header(buf + offset,
                   size - offset,
                   arch->trig_swi_addr,
                   &arch->trig_swi_data,
                   sizeof(arch->trig_swi_data));
    DE_BUG_ON(offset != size, "offset = %zu, size = %zu", offset, size);
    DE_TRACE_INT(TR_INITIALIZE,"Loaded %u bytes into memory (including padding)\n", offset);

    return offset;
}

static int axf_read_mib_table(struct axf_hlpr *axf)
{
    int i;
    int read;
    char shstrtab[sizeof(".mibtable")]; // sizeof(".mibtable") and sizeof("MIB_TABLE")

    /* walk the section headers looking for a mib table */
    for(i = 0; i < axf->ehdr->e_shnum; i++) {
        if(axf->shdr[i].sh_type != SHT_PROGBITS)
            continue;
        if(axf->shdr[i].sh_name >= axf->shdr_shstrtab->sh_size) {
            DE_TRACE_INT(TR_INITIALIZE,"ELF section %u name points outside shstrtab\n", i);
            continue;
        }
        // Read the name from the string table
        file_seek(axf, axf->shdr_shstrtab->sh_offset + axf->shdr[i].sh_name);
        read = de_fread(axf->fh, &shstrtab, sizeof(shstrtab));
        if (read != sizeof(shstrtab)) {
            return -1;
        }
        axf->seek += read;
        /* If there is a section with a matching name, assume the mib
         * table vaddr is the same as the section address */
        /* .mibtable used with GNU toolchain */
        if(axf->shdr_shstrtab->sh_size - axf->shdr[i].sh_name >= sizeof(".mibtable")
            && memcmp(&shstrtab, ".mibtable", sizeof(".mibtable")) == 0) {
            DE_TRACE_INT(TR_INITIALIZE,"found MIB table section (%u)\n", i);
            axf->mib_table_addr = axf->shdr[i].sh_addr;
            axf->mib_table_size = axf->shdr[i].sh_size;
            break;
        }
        /* MIB_TABLE used with ARM toolchain */
        if(axf->shdr_shstrtab->sh_size - axf->shdr[i].sh_name >= sizeof("MIB_TABLE")
            && memcmp(&shstrtab, "MIB_TABLE", sizeof("MIB_TABLE")) == 0) {
            DE_TRACE_INT(TR_INITIALIZE,"found MIB table section (%u)\n", i);
            axf->mib_table_addr = axf->shdr[i].sh_addr;
            axf->mib_table_size = axf->shdr[i].sh_size;
            break;
        }
    }
    DE_TRACE_INT2(TR_INITIALIZE,"mib_table_address = 0x%x/%u\n", (int)axf->mib_table_addr, (int)axf->mib_table_size);

    return 0;
}

/*! 
 * Will read and pars an axf firmware image into a buffer, this to prepare for
 * fast recovery.
 *
 * Will do assert if firmware image is larger then buffer, this to prevent 
 * loading of incomplete firmware image.
 *
 * @return bytes loaded into buffer, 0 on no file.
 */
static int read_axf_into_mem(const char *path, void *buf, size_t buf_len)
{
    int ret;
    struct axf_hlpr axf;
    struct arch_dep arch;

    DE_ASSERT(buf != NULL);

    axf.path = path;

    // Read the file and read headers into memory
    ret = axf_fw_init(&axf, &arch);
    if(ret <= 0) {
        return ret;
    }

    // Setup the structure for the first 64 bytes that is written to the hardware
    setup_load_header(arch.bm_offset);

    axf_read_mib_table(&axf);

    // Create the buffert with length address and data chunks
    // Returns the amount of data that was put in the buffert
    ret = axf_build_fw_buf(buf, &axf, &arch);

    axf_destroy(&axf);

    return ret;
}

/*! 
 * Will read a pre compiled firmware image with data headers and all into a 
 * buffer, this is to save space on filesystem and to prepare for fast recovery.
 *
 * Will do assert if firmware image is larger then buffer, this to prevent 
 * loading of incomplete firmware image.
 *
 * @return bytes loaded into buffer, 0 on no file.
 */
static int read_bin_into_mem(
      const char *path, 
      void *buf, 
      size_t buf_len)
{
    int read, read2;
    de_file_ref_t fh;
    char tmp;

    fh = de_fopen(path, DE_FRDONLY);
    if (!de_f_is_open(fh)) 
    {
        return 0;
    }

    read = de_fread(fh, buf, buf_len);

    read2 = de_fread(fh, &tmp, sizeof(tmp));
    DE_BUG_ON(read2 > 0, "firmware file larger then buffer\n");

    de_fclose(fh);

    return read;
}


/*****************************************************************************
G L O B A L   F U N C T I O N S
*****************************************************************************/
int nr_read_firmware(
      const char *filename, 
      void *buf, 
      size_t buf_len)
{
    int ret;
    ret = read_axf_into_mem(filename, buf, buf_len);
    if(ret < 0)
        ret = read_bin_into_mem(filename, buf, buf_len);
    return ret;       
}

/* Local Variables: */
/* c-basic-offset: 4 */
/* indent-tabs-mode: nil */
/* End: */
