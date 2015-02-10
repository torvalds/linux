#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libelf.h>
#include <gelf.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include "libbpf.h"
#include "bpf_helpers.h"
#include "bpf_load.h"

static char license[128];
static bool processed_sec[128];
int map_fd[MAX_MAPS];
int prog_fd[MAX_PROGS];
int prog_cnt;

static int load_and_attach(const char *event, struct bpf_insn *prog, int size)
{
	int fd;
	bool is_socket = strncmp(event, "socket", 6) == 0;

	if (!is_socket)
		/* tracing events tbd */
		return -1;

	fd = bpf_prog_load(BPF_PROG_TYPE_SOCKET_FILTER,
			   prog, size, license);

	if (fd < 0) {
		printf("bpf_prog_load() err=%d\n%s", errno, bpf_log_buf);
		return -1;
	}

	prog_fd[prog_cnt++] = fd;

	return 0;
}

static int load_maps(struct bpf_map_def *maps, int len)
{
	int i;

	for (i = 0; i < len / sizeof(struct bpf_map_def); i++) {

		map_fd[i] = bpf_create_map(maps[i].type,
					   maps[i].key_size,
					   maps[i].value_size,
					   maps[i].max_entries);
		if (map_fd[i] < 0)
			return 1;
	}
	return 0;
}

static int get_sec(Elf *elf, int i, GElf_Ehdr *ehdr, char **shname,
		   GElf_Shdr *shdr, Elf_Data **data)
{
	Elf_Scn *scn;

	scn = elf_getscn(elf, i);
	if (!scn)
		return 1;

	if (gelf_getshdr(scn, shdr) != shdr)
		return 2;

	*shname = elf_strptr(elf, ehdr->e_shstrndx, shdr->sh_name);
	if (!*shname || !shdr->sh_size)
		return 3;

	*data = elf_getdata(scn, 0);
	if (!*data || elf_getdata(scn, *data) != NULL)
		return 4;

	return 0;
}

static int parse_relo_and_apply(Elf_Data *data, Elf_Data *symbols,
				GElf_Shdr *shdr, struct bpf_insn *insn)
{
	int i, nrels;

	nrels = shdr->sh_size / shdr->sh_entsize;

	for (i = 0; i < nrels; i++) {
		GElf_Sym sym;
		GElf_Rel rel;
		unsigned int insn_idx;

		gelf_getrel(data, i, &rel);

		insn_idx = rel.r_offset / sizeof(struct bpf_insn);

		gelf_getsym(symbols, GELF_R_SYM(rel.r_info), &sym);

		if (insn[insn_idx].code != (BPF_LD | BPF_IMM | BPF_DW)) {
			printf("invalid relo for insn[%d].code 0x%x\n",
			       insn_idx, insn[insn_idx].code);
			return 1;
		}
		insn[insn_idx].src_reg = BPF_PSEUDO_MAP_FD;
		insn[insn_idx].imm = map_fd[sym.st_value / sizeof(struct bpf_map_def)];
	}

	return 0;
}

int load_bpf_file(char *path)
{
	int fd, i;
	Elf *elf;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr, shdr_prog;
	Elf_Data *data, *data_prog, *symbols = NULL;
	char *shname, *shname_prog;

	if (elf_version(EV_CURRENT) == EV_NONE)
		return 1;

	fd = open(path, O_RDONLY, 0);
	if (fd < 0)
		return 1;

	elf = elf_begin(fd, ELF_C_READ, NULL);

	if (!elf)
		return 1;

	if (gelf_getehdr(elf, &ehdr) != &ehdr)
		return 1;

	/* scan over all elf sections to get license and map info */
	for (i = 1; i < ehdr.e_shnum; i++) {

		if (get_sec(elf, i, &ehdr, &shname, &shdr, &data))
			continue;

		if (0) /* helpful for llvm debugging */
			printf("section %d:%s data %p size %zd link %d flags %d\n",
			       i, shname, data->d_buf, data->d_size,
			       shdr.sh_link, (int) shdr.sh_flags);

		if (strcmp(shname, "license") == 0) {
			processed_sec[i] = true;
			memcpy(license, data->d_buf, data->d_size);
		} else if (strcmp(shname, "maps") == 0) {
			processed_sec[i] = true;
			if (load_maps(data->d_buf, data->d_size))
				return 1;
		} else if (shdr.sh_type == SHT_SYMTAB) {
			symbols = data;
		}
	}

	/* load programs that need map fixup (relocations) */
	for (i = 1; i < ehdr.e_shnum; i++) {

		if (get_sec(elf, i, &ehdr, &shname, &shdr, &data))
			continue;
		if (shdr.sh_type == SHT_REL) {
			struct bpf_insn *insns;

			if (get_sec(elf, shdr.sh_info, &ehdr, &shname_prog,
				    &shdr_prog, &data_prog))
				continue;

			insns = (struct bpf_insn *) data_prog->d_buf;

			processed_sec[shdr.sh_info] = true;
			processed_sec[i] = true;

			if (parse_relo_and_apply(data, symbols, &shdr, insns))
				continue;

			if (memcmp(shname_prog, "events/", 7) == 0 ||
			    memcmp(shname_prog, "socket", 6) == 0)
				load_and_attach(shname_prog, insns, data_prog->d_size);
		}
	}

	/* load programs that don't use maps */
	for (i = 1; i < ehdr.e_shnum; i++) {

		if (processed_sec[i])
			continue;

		if (get_sec(elf, i, &ehdr, &shname, &shdr, &data))
			continue;

		if (memcmp(shname, "events/", 7) == 0 ||
		    memcmp(shname, "socket", 6) == 0)
			load_and_attach(shname, data->d_buf, data->d_size);
	}

	close(fd);
	return 0;
}
