/* /proc device interface for Nanoradio Linux WiFi driver. */
/* $Id: firmware.c 16491 2010-10-21 13:07:16Z joda $ */
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/netdevice.h>

#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>

#include "nanoutil.h"
#include "nanoparam.h"
#include "nanonet.h"

#include "driverenv.h"

#include "we_dlm.h"

#include "px.h"

#include <linux/elf.h>
#ifndef EM_ARM
#define EM_ARM 40
#endif
#ifndef EM_NORC
#define EM_NORC 0x625
#endif

struct bm_descr {
   uint32_t buf_ptr;
   uint32_t ctrl;
   uint16_t size;
   uint16_t expedited_size;
   uint32_t next;
};

#define BM_SIZE		sizeof(struct bm_descr)

#define ARM_START_EXEC_ADDR     0x46000
#define ARM_BM_BASE             0x46100
#define ARM_TRIG_SWI_ADDR       0x00072040
#define ARM_TRIG_SWI_DATA       0xffffffff

#define NORC_START_EXEC_ADDR    0x0004FFFC
#define NORC_BM_BASE            0
#define NORC_TRIG_SWI_ADDR      0xfffc1030
#define NORC_TRIG_SWI_DATA      0x00000002

#define HARMLESS_ADDRESS_IN_NRX2_CHIP 0x60000

#define IS_DLM_ENTRY(ehdr, phdr) \
   (ehdr->e_machine == EM_ARM && (phdr)->p_paddr >= 0x1000000) 

/* XXX should really be p_memsz below, but the mib table should not
 * have any bss */
#define IS_MIB_TABLE(ehdr, phdr)                                        \
   (mib_table_addr >= (phdr)->p_vaddr                                   \
    && mib_table_addr + mib_table_size <= (phdr)->p_vaddr + (phdr)->p_filesz)

struct load_header {
   struct bm_descr base;
   struct bm_descr len;
   struct bm_descr addr;
   struct bm_descr payload;
};

struct load_header load_header = {
   .base = {
      .buf_ptr = BM_SIZE,
      .ctrl = 0,
      .size = 3 * BM_SIZE,
      .expedited_size = 0,
      .next = BM_SIZE
   },
   .len = {
      .buf_ptr = 3 * BM_SIZE + offsetof(struct bm_descr, size),
      .ctrl = 0,
      .size = sizeof(uint16_t),
      .expedited_size = 0,
      .next = 2 * BM_SIZE
   },
   .addr = {
      .buf_ptr = 3 * BM_SIZE + offsetof(struct bm_descr, buf_ptr),
      .ctrl = 0,
      .size = sizeof(uint32_t),
      .expedited_size = 0,
      .next = 3 * BM_SIZE
   },
   .payload = {
      .buf_ptr = 0,
      .ctrl = 0,
      .size = 0,
      .expedited_size = 0,
      .next = BM_SIZE
   }
};

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

#define PAYLOAD_HEADERSIZE 6
#define PAYLOAD_MAXSIZE 60

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

static int nrx_fw_init(struct nrx_px_softc*);
static int nrx_fw_release(struct nrx_px_softc*, struct inode*, struct file*);

struct nrx_px_entry fw_px_entry = {
   .name = "firmware", 
   .mode = S_IRUSR|S_IWUSR, 
   .blocksize = 128 * 1024, 
   .init = nrx_fw_init,
   .open = NULL,
   .release = nrx_fw_release
};

int
nano_fw_download(struct net_device *dev)
{
   struct nrx_px_softc *psc;
   struct nrx_softc *sc = netdev_priv(dev);
   int ret;

   KDEBUG(TRACE, "ENTRY");

   psc = nrx_px_lookup(&fw_px_entry, sc->proc_dir);
   
   if(psc == NULL) {
      KDEBUG(ERROR, "proc entry not found");
      return -EIO;
   }

   nrx_px_rlock(psc);

   if(nrx_px_size(psc) == 0) {
      KDEBUG(TRACE, "no firmware");
      nrx_px_runlock(psc);
      return -EINVAL;
   }

   if(sc->transport == NULL || sc->transport->fw_download == NULL){
      KDEBUG(TRACE, "no transport firmware interface");
      ret = -EINVAL;
   } else{
      ret =  (*sc->transport->fw_download)(nrx_px_data(psc), 
                                             nrx_px_size(psc), 
                                             sc->transport_data);
   }
   nrx_px_runlock(psc);

   if (ret == 0 && sc->transport->params_buf != NULL) {
      WiFiEngine_LoadWLANParameters(sc->transport->params_buf,
                                    sc->transport->params_len);
   }
   return ret;
}

static int
parse_elf_fw(struct nrx_px_softc *psc, unsigned char *data, size_t len)
{
   int ret;
   int i;
   size_t size, offset;
#ifndef NO_FIRMWARE_PADDING
   size_t pad_size;
#endif
   unsigned char *tmp_fw;
   Elf32_Ehdr *ehdr;
   Elf32_Phdr *phdr;
   Elf32_Shdr *shdr;
   Elf32_Shdr *shdr_shstrtab;
   char *shstrtab;
   Elf32_Addr mib_table_addr = 0x40000;
   Elf32_Word mib_table_size = 0; /* unknown */

   unsigned int bm_offset;
   unsigned int start_exec_addr;
   unsigned int trig_swi_addr;
   unsigned int trig_swi_data;
   
   if(len < sizeof(*ehdr))
      return -EINVAL;

   ehdr = (Elf32_Ehdr*)data;

   if(ehdr->e_type != ET_EXEC) 
      return -EINVAL;

   if(ehdr->e_machine == EM_ARM) {
      bm_offset       = ARM_BM_BASE;
      start_exec_addr = ARM_START_EXEC_ADDR;
      trig_swi_addr   = ARM_TRIG_SWI_ADDR;
      trig_swi_data   = ARM_TRIG_SWI_DATA;
   } else if(ehdr->e_machine == EM_NORC) {
      bm_offset       = NORC_BM_BASE;
      start_exec_addr = NORC_START_EXEC_ADDR;
      trig_swi_addr   = NORC_TRIG_SWI_ADDR;
      trig_swi_data   = NORC_TRIG_SWI_DATA;
   } else {
      return -EINVAL;
   }

   /* validate phdr structures */
   if(sizeof(*phdr) != ehdr->e_phentsize) {
      KDEBUG(ERROR, "ELF file with unexpected phdr size");
      return -EINVAL;
   }
   
   if(len < ehdr->e_phoff + ehdr->e_phnum * ehdr->e_phentsize) {
      KDEBUG(ERROR, "ELF phdrs extend beyond end of file");
      return -EINVAL;
   }

   phdr = (Elf32_Phdr*)(data + ehdr->e_phoff);

   /* validate shdr structures */
   if(sizeof(*shdr) != ehdr->e_shentsize) {
      KDEBUG(ERROR, "ELF file with unexpected shdr size");
      return -EINVAL;
   }

   if(len < ehdr->e_shoff + ehdr->e_shnum * ehdr->e_shentsize) {
      KDEBUG(ERROR, "ELF shdrs extend beyond end of file");
      return -EINVAL;
   }

   shdr = (Elf32_Shdr*)(data + ehdr->e_shoff);

   if(ehdr->e_shstrndx >= ehdr->e_shnum) {
      KDEBUG(ERROR, "ELF shstrndx index points to missing section");
      return -EINVAL;
   }

   shdr_shstrtab = &shdr[ehdr->e_shstrndx];
   if(shdr_shstrtab->sh_offset >= len) {
      KDEBUG(ERROR, "ELF shstrtab starts beyond end of file");
      return -EINVAL;
   }

   if(shdr_shstrtab->sh_offset + shdr_shstrtab->sh_size >= len) {
      KDEBUG(ERROR, "ELF shstrtab extends beyond end of file");
      return -EINVAL;
   }

   shstrtab = (char*)data + shdr_shstrtab->sh_offset;
   /* walk the section headers looking for a mib table */
   for(i = 0; i < ehdr->e_shnum; i++) {
      if(shdr[i].sh_type != SHT_PROGBITS)
         continue;
      if(shdr[i].sh_name >= shdr_shstrtab->sh_size) {
         KDEBUG(TRACE, "ELF section %u name points outside shstrtab", i);
         continue;
      }
      /* if there is a section with a matching name, assume the mib
       * table vaddr is the same as the section address */
      /* .mibtable used with GNU toolchain */
      if(shdr_shstrtab->sh_size - shdr[i].sh_name >= sizeof(".mibtable")
         && memcmp(&shstrtab[shdr[i].sh_name], ".mibtable", 
                   sizeof(".mibtable")) == 0) {
         KDEBUG(TRACE, "found MIB table section (%u)", i);
         mib_table_addr = shdr[i].sh_addr;
         mib_table_size = shdr[i].sh_size;
         break;
      }
      /* MIB_TABLE used with ARM toolchain */
      if(shdr_shstrtab->sh_size - shdr[i].sh_name >= sizeof("MIB_TABLE")
         && memcmp(&shstrtab[shdr[i].sh_name], "MIB_TABLE", 
                   sizeof("MIB_TABLE")) == 0) {
         KDEBUG(TRACE, "found MIB table section (%u)", i);
         mib_table_addr = shdr[i].sh_addr;
         mib_table_size = shdr[i].sh_size;
         break;
      }
   }
   KDEBUG(TRACE, "mib_table_address = %x/%u", mib_table_addr, mib_table_size);

   /* check size */
   size = 0;
   size += sizeof(load_header);
   for(i = 0; i < ehdr->e_phnum; i++)
   {
      if(IS_DLM_ENTRY(ehdr, &phdr[i]))
	 continue;
      if(len < phdr[i].p_offset + phdr[i].p_filesz)
	 return -EINVAL;
      size += phdr[i].p_filesz;
      size += ((phdr[i].p_filesz +  PAYLOAD_MAXSIZE - 1) / PAYLOAD_MAXSIZE) * PAYLOAD_HEADERSIZE;
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

   tmp_fw = vmalloc(size);

   setup_load_header(bm_offset);

   offset = 0;
   memcpy(tmp_fw + offset, &load_header, sizeof(load_header));
   offset += sizeof(load_header);
   
#ifndef NO_FIRMWARE_PADDING
   if(pad_size != 0) {
      ASSERT(pad_size < len);
      offset += make_data_header(tmp_fw + offset,
                                 size - offset,
#define SAFE_RAM_ADDRESS 0x40000 /* XXX */
                                 SAFE_RAM_ADDRESS,
                                 data, 
                                 pad_size - PAYLOAD_HEADERSIZE);
   }
#endif /* NO_FIRMWARE_PADDING */

   we_dlm_flush();
   for(i = 0; i < ehdr->e_phnum; i++) {
      if(IS_DLM_ENTRY(ehdr, &phdr[i])) {
         char *p = we_dlm_register("unknown",
				   phdr[i].p_vaddr,
				   phdr[i].p_filesz);
         if(p) {
            memcpy(p, data + phdr[i].p_offset, phdr[i].p_filesz);
         }
      } else {
         offset += make_data_header(tmp_fw + offset,
                                    size - offset,
                                    phdr[i].p_vaddr,
                                    data + phdr[i].p_offset,
                                    phdr[i].p_filesz);
	 if(IS_MIB_TABLE(ehdr, &phdr[i])) {
            if(mib_table_size == 0)
               mib_table_size = phdr[i].p_filesz;
            /* if the mib table is in a separate load segment, then
             * mib_table_addr == p_vaddr and mib_table_size ==
             * p_filesz, if not this will select a piece of the
             * segment */
            WiFiEngine_RegisterMIBTable(data + phdr[i].p_offset         \
                                        + mib_table_addr - phdr[i].p_vaddr, 
                                        mib_table_size,
                                        mib_table_addr);
	 }
      }
   }
   offset += make_data_header(tmp_fw + offset,
			      size - offset,
			      start_exec_addr,
			      &ehdr->e_entry,
			      sizeof(ehdr->e_entry));
   
   offset += make_data_header(tmp_fw + offset,
			      size - offset,
			      trig_swi_addr,
			      &trig_swi_data,
			      sizeof(trig_swi_data));
   ret = nrx_px_copy(psc, tmp_fw, size);
   vfree(tmp_fw);
   if(ret < 0) {
      KDEBUG(ERROR, "failed to copy firmware (%d)", ret);
      ASSERT(nrx_px_setsize(psc, 0) == 0);
      return ret;
   }
   DE_BUG_ON(offset != size, "offset = %zu, size = %zu", offset, size);
   return 0;
}

static inline void make_filename(char *filename, size_t len, const char *name)
{
   const char *sep = "";
   if(*nrx_config == '\0' || nrx_config[strlen(nrx_config) - 1] != '/')
      sep = "/";
   snprintf(filename, len, "%s%s%s", nrx_config, sep, name);
}

static int
nrx_fw_init(struct nrx_px_softc *psc)
{
   int status;
   char filename[128];
   nrx_px_wlock(psc);
   make_filename(filename, sizeof(filename), "x_mac.axf");
   status = nrx_px_read_file(psc, filename);
   if(status != 0) {
      make_filename(filename, sizeof(filename), "x_mac.bin");
      status = nrx_px_read_file(psc, filename);
   }
   if(status == 0) {
      KDEBUG(TRACE, "loaded firmware from %s", filename);
      nrx_fw_release(psc, NULL, NULL);
   }
   nrx_px_wunlock(psc);
   return 0;
}

static int
nrx_fw_release(struct nrx_px_softc *psc, 
               struct inode *inode, 
               struct file *file)
{
   struct net_device *dev = nrx_px_priv(psc);
   struct nrx_softc *sc = netdev_priv(dev);
   
   /* already locked */

   void *elf_data = nrx_px_data(psc);
   size_t elf_size = nrx_px_size(psc);

   KDEBUG(TRACE, "ENTRY");

   //determine data type (elf or binary)
   
   if(elf_size >= 4 && memcmp(elf_data, "\177ELF", 4) == 0) {
      if(parse_elf_fw(psc, elf_data, elf_size) == 0) {
         nrx_set_flag(sc, NRX_FLAG_HAVE_FW);
         nrx_schedule_event(sc, HZ/4); //Delay task until sc is initialized
      }
   } else if(elf_size >= 4 && memcmp(elf_data, "\x10\x61\x04\x00", 4) == 0) {
      nrx_set_flag(sc, NRX_FLAG_HAVE_FW);
      nrx_schedule_event(sc, HZ/4); //Delay task until sc is initialized
   } else {
      KDEBUG(TRACE, "data of unknown type copied to firmware file");
   }

   return 0;
}

EXPORT_SYMBOL(nano_fw_download);
