/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _BOOTSTRAP_H_
#define	_BOOTSTRAP_H_

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/linker_set.h>

/* Commands and return values; nonzero return sets command_errmsg != NULL */
typedef int	(bootblk_cmd_t)(int argc, char *argv[]);
#define	COMMAND_ERRBUFSZ	(256)
extern const char *command_errmsg;
extern char	command_errbuf[COMMAND_ERRBUFSZ];
#define CMD_OK		0
#define CMD_WARN	1
#define CMD_ERROR	2
#define CMD_CRIT	3
#define CMD_FATAL	4

/* interp.c */
void	interact(void);
void	interp_emit_prompt(void);
int	interp_builtin_cmd(int argc, char *argv[]);

/* Called by interp.c for interp_*.c embedded interpreters */
int	interp_include(const char *filename);	/* Execute commands from filename */
void	interp_init(void);			/* Initialize interpreater */
int	interp_run(const char *line);		/* Run a single command */

/* interp_backslash.c */
char	*backslash(const char *str);

/* interp_parse.c */
int	parse(int *argc, char ***argv, const char *str);

/* boot.c */
void	autoboot_maybe(void);
int	getrootmount(char *rootdev);

/* misc.c */
char	*unargv(int argc, char *argv[]);
void	hexdump(caddr_t region, size_t len);
size_t	strlenout(vm_offset_t str);
char	*strdupout(vm_offset_t str);
void	kern_bzero(vm_offset_t dest, size_t len);
int	kern_pread(int fd, vm_offset_t dest, size_t len, off_t off);
void	*alloc_pread(int fd, off_t off, size_t len);

/* bcache.c */
void	bcache_init(size_t nblks, size_t bsize);
void	bcache_add_dev(int);
void	*bcache_allocate(void);
void	bcache_free(void *);
int	bcache_strategy(void *devdata, int rw, daddr_t blk, size_t size,
			char *buf, size_t *rsize);

/*
 * Disk block cache
 */
struct bcache_devdata
{
    int         (*dv_strategy)(void *devdata, int rw, daddr_t blk,
			size_t size, char *buf, size_t *rsize);
    void	*dv_devdata;
    void	*dv_cache;
};

/*
 * Modular console support.
 */
struct console 
{
    const char	*c_name;
    const char	*c_desc;
    int		c_flags;
#define C_PRESENTIN	(1<<0)	    /* console can provide input */
#define C_PRESENTOUT	(1<<1)	    /* console can provide output */
#define C_ACTIVEIN	(1<<2)	    /* user wants input from console */
#define C_ACTIVEOUT	(1<<3)	    /* user wants output to console */
#define	C_WIDEOUT	(1<<4)	    /* c_out routine groks wide chars */
    void	(* c_probe)(struct console *cp);	/* set c_flags to match hardware */
    int		(* c_init)(int arg);			/* reinit XXX may need more args */
    void	(* c_out)(int c);			/* emit c */
    int		(* c_in)(void);				/* wait for and return input */
    int		(* c_ready)(void);			/* return nonzer if input waiting */
};
extern struct console	*consoles[];
void		cons_probe(void);

/*
 * Plug-and-play enumerator/configurator interface.
 */
struct pnphandler 
{
    const char	*pp_name;		/* handler/bus name */
    void	(* pp_enumerate)(void);	/* enumerate PnP devices, add to chain */
};

struct pnpident
{
    char			*id_ident;	/* ASCII identifier, actual format varies with bus/handler */
    STAILQ_ENTRY(pnpident)	id_link;
};

struct pnpinfo
{
    char			*pi_desc;	/* ASCII description, optional */
    int				pi_revision;	/* optional revision (or -1) if not supported */
    char			*pi_module;	/* module/args nominated to handle device */
    int				pi_argc;	/* module arguments */
    char			**pi_argv;
    struct pnphandler		*pi_handler;	/* handler which detected this device */
    STAILQ_HEAD(,pnpident)	pi_ident;	/* list of identifiers */
    STAILQ_ENTRY(pnpinfo)	pi_link;
};

STAILQ_HEAD(pnpinfo_stql, pnpinfo);

extern struct pnphandler	*pnphandlers[];		/* provided by MD code */

void			pnp_addident(struct pnpinfo *pi, char *ident);
struct pnpinfo		*pnp_allocinfo(void);
void			pnp_freeinfo(struct pnpinfo *pi);
void			pnp_addinfo(struct pnpinfo *pi);
char			*pnp_eisaformat(uint8_t *data);

/*
 *  < 0	- No ISA in system
 * == 0	- Maybe ISA, search for read data port
 *  > 0	- ISA in system, value is read data port address
 */
extern int			isapnp_readport;

/*
 * Version information
 */
extern char bootprog_info[];

/*
 * Interpreter information
 */
extern const char bootprog_interp[];
#define	INTERP_DEFINE(interpstr) \
const char bootprog_interp[] = "$Interpreter:" interpstr


/*
 * Preloaded file metadata header.
 *
 * Metadata are allocated on our heap, and copied into kernel space
 * before executing the kernel.
 */
struct file_metadata 
{
    size_t			md_size;
    uint16_t			md_type;
    struct file_metadata	*md_next;
    char			md_data[1];	/* data are immediately appended */
};

struct preloaded_file;
struct mod_depend;

struct kernel_module
{
    char			*m_name;	/* module name */
    int				m_version;	/* module version */
/*    char			*m_args;*/	/* arguments for the module */
    struct preloaded_file	*m_fp;
    struct kernel_module	*m_next;
};

/*
 * Preloaded file information. Depending on type, file can contain
 * additional units called 'modules'.
 *
 * At least one file (the kernel) must be loaded in order to boot.
 * The kernel is always loaded first.
 *
 * String fields (m_name, m_type) should be dynamically allocated.
 */
struct preloaded_file
{
    char			*f_name;	/* file name */
    char			*f_type;	/* verbose file type, eg 'ELF kernel', 'pnptable', etc. */
    char			*f_args;	/* arguments for the file */
    struct file_metadata	*f_metadata;	/* metadata that will be placed in the module directory */
    int				f_loader;	/* index of the loader that read the file */
    vm_offset_t			f_addr;		/* load address */
    size_t			f_size;		/* file size */
    struct kernel_module	*f_modules;	/* list of modules if any */
    struct preloaded_file	*f_next;	/* next file */
};

struct file_format
{
    /* Load function must return EFTYPE if it can't handle the module supplied */
    int		(* l_load)(char *filename, uint64_t dest, struct preloaded_file **result);
    /* Only a loader that will load a kernel (first module) should have an exec handler */
    int		(* l_exec)(struct preloaded_file *mp);
};

extern struct file_format	*file_formats[];	/* supplied by consumer */
extern struct preloaded_file	*preloaded_files;

int			mod_load(char *name, struct mod_depend *verinfo, int argc, char *argv[]);
int			mod_loadkld(const char *name, int argc, char *argv[]);
void			unload(void);

struct preloaded_file *file_alloc(void);
struct preloaded_file *file_findfile(const char *name, const char *type);
struct file_metadata *file_findmetadata(struct preloaded_file *fp, int type);
struct preloaded_file *file_loadraw(const char *name, char *type, int insert);
void file_discard(struct preloaded_file *fp);
void file_addmetadata(struct preloaded_file *fp, int type, size_t size, void *p);
int  file_addmodule(struct preloaded_file *fp, char *modname, int version,
	struct kernel_module **newmp);
void file_removemetadata(struct preloaded_file *fp);

/* MI module loaders */
#ifdef __elfN
/* Relocation types. */
#define ELF_RELOC_REL	1
#define ELF_RELOC_RELA	2

/* Relocation offset for some architectures */
extern uint64_t __elfN(relocation_offset);

struct elf_file;
typedef Elf_Addr (symaddr_fn)(struct elf_file *ef, Elf_Size symidx);

int	__elfN(loadfile)(char *filename, uint64_t dest, struct preloaded_file **result);
int	__elfN(obj_loadfile)(char *filename, uint64_t dest,
	    struct preloaded_file **result);
int	__elfN(reloc)(struct elf_file *ef, symaddr_fn *symaddr,
	    const void *reldata, int reltype, Elf_Addr relbase,
	    Elf_Addr dataaddr, void *data, size_t len);
int __elfN(loadfile_raw)(char *filename, uint64_t dest,
	    struct preloaded_file **result, int multiboot);
int __elfN(load_modmetadata)(struct preloaded_file *fp, uint64_t dest);
#endif

/*
 * Support for commands 
 */
struct bootblk_command 
{
    const char		*c_name;
    const char		*c_desc;
    bootblk_cmd_t	*c_fn;
};

#define COMMAND_SET(tag, key, desc, func)				\
    static bootblk_cmd_t func;						\
    static struct bootblk_command _cmd_ ## tag = { key, desc, func };	\
    DATA_SET(Xcommand_set, _cmd_ ## tag)

SET_DECLARE(Xcommand_set, struct bootblk_command);

/* 
 * The intention of the architecture switch is to provide a convenient
 * encapsulation of the interface between the bootstrap MI and MD code.
 * MD code may selectively populate the switch at runtime based on the
 * actual configuration of the target system.
 */
struct arch_switch
{
    /* Automatically load modules as required by detected hardware */
    int		(*arch_autoload)(void);
    /* Locate the device for (name), return pointer to tail in (*path) */
    int		(*arch_getdev)(void **dev, const char *name, const char **path);
    /* Copy from local address space to module address space, similar to bcopy() */
    ssize_t	(*arch_copyin)(const void *src, vm_offset_t dest,
			       const size_t len);
    /* Copy to local address space from module address space, similar to bcopy() */
    ssize_t	(*arch_copyout)(const vm_offset_t src, void *dest,
				const size_t len);
    /* Read from file to module address space, same semantics as read() */
    ssize_t	(*arch_readin)(const int fd, vm_offset_t dest,
			       const size_t len);
    /* Perform ISA byte port I/O (only for systems with ISA) */
    int		(*arch_isainb)(int port);
    void	(*arch_isaoutb)(int port, int value);

    /*
     * Interface to adjust the load address according to the "object"
     * being loaded.
     */
    uint64_t	(*arch_loadaddr)(u_int type, void *data, uint64_t addr);
#define	LOAD_ELF	1	/* data points to the ELF header. */
#define	LOAD_RAW	2	/* data points to the file name. */

    /*
     * Interface to inform MD code about a loaded (ELF) segment. This
     * can be used to flush caches and/or set up translations.
     */
#ifdef __elfN
    void	(*arch_loadseg)(Elf_Ehdr *eh, Elf_Phdr *ph, uint64_t delta);
#else
    void	(*arch_loadseg)(void *eh, void *ph, uint64_t delta);
#endif

    /* Probe ZFS pool(s), if needed. */
    void	(*arch_zfs_probe)(void);

    /* Return the hypervisor name/type or NULL if not virtualized. */
    const char *(*arch_hypervisor)(void);

    /* For kexec-type loaders, get ksegment structure */
    void	(*arch_kexec_kseg_get)(int *nseg, void **kseg);
};
extern struct arch_switch archsw;

/* This must be provided by the MD code, but should it be in the archsw? */
void	delay(int delay);

void	dev_cleanup(void);

time_t	time(time_t *tloc);

#ifndef CTASSERT
#define	CTASSERT(x)	_Static_assert(x, "compile-time assertion failed")
#endif

#ifdef LOADER_VERIEXEC
#include <verify_file.h>
#endif

#endif /* !_BOOTSTRAP_H_ */
