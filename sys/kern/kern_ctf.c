/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 John Birrell <jb@freebsd.org>
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

/*
 * Note this file is included by both link_elf.c and link_elf_obj.c.
 *
 * The CTF header structure definition can't be used here because it's
 * (annoyingly) covered by the CDDL. We will just use a few bytes from
 * it as an integer array where we 'know' what they mean.
 */
#define CTF_HDR_SIZE		36
#define CTF_HDR_STRTAB_U32	7
#define CTF_HDR_STRLEN_U32	8

#ifdef DDB_CTF
static void *
z_alloc(void *nil, u_int items, u_int size)
{
	void *ptr;

	ptr = malloc(items * size, M_TEMP, M_NOWAIT);
	return ptr;
}

static void
z_free(void *nil, void *ptr)
{
	free(ptr, M_TEMP);
}

#endif

static int
link_elf_ctf_get(linker_file_t lf, linker_ctf_t *lc)
{
#ifdef DDB_CTF
	Elf_Ehdr *hdr = NULL;
	Elf_Shdr *shdr = NULL;
	caddr_t ctftab = NULL;
	caddr_t raw = NULL;
	caddr_t shstrtab = NULL;
	elf_file_t ef = (elf_file_t) lf;
	int flags;
	int i;
	int nbytes;
	size_t sz;
	struct nameidata nd;
	struct thread *td = curthread;
	uint8_t ctf_hdr[CTF_HDR_SIZE];
#endif
	int error = 0;

	if (lf == NULL || lc == NULL)
		return (EINVAL);

	/* Set the defaults for no CTF present. That's not a crime! */
	bzero(lc, sizeof(*lc));

#ifdef DDB_CTF
	/*
	 * First check if we've tried to load CTF data previously and the
	 * CTF ELF section wasn't found. We flag that condition by setting
	 * ctfcnt to -1. See below.
	 */
	if (ef->ctfcnt < 0)
		return (EFTYPE);

	/* Now check if we've already loaded the CTF data.. */
	if (ef->ctfcnt > 0) {
		/* We only need to load once. */
		lc->ctftab = ef->ctftab;
		lc->ctfcnt = ef->ctfcnt;
		lc->symtab = ef->ddbsymtab;
		lc->strtab = ef->ddbstrtab;
		lc->strcnt = ef->ddbstrcnt;
		lc->nsym   = ef->ddbsymcnt;
		lc->ctfoffp = (uint32_t **) &ef->ctfoff;
		lc->typoffp = (uint32_t **) &ef->typoff;
		lc->typlenp = &ef->typlen;
		return (0);
	}

	/*
	 * We need to try reading the CTF data. Flag no CTF data present
	 * by default and if we actually succeed in reading it, we'll
	 * update ctfcnt to the number of bytes read.
	 */
	ef->ctfcnt = -1;

	NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, lf->pathname, td);
	flags = FREAD;
	error = vn_open(&nd, &flags, 0, NULL);
	if (error)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);

	/* Allocate memory for the FLF header. */
	hdr = malloc(sizeof(*hdr), M_LINKER, M_WAITOK);

	/* Read the ELF header. */
	if ((error = vn_rdwr(UIO_READ, nd.ni_vp, hdr, sizeof(*hdr),
	    0, UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED, NULL,
	    td)) != 0)
		goto out;

	/* Sanity check. */
	if (!IS_ELF(*hdr)) {
		error = ENOEXEC;
		goto out;
	}

	nbytes = hdr->e_shnum * hdr->e_shentsize;
	if (nbytes == 0 || hdr->e_shoff == 0 ||
	    hdr->e_shentsize != sizeof(Elf_Shdr)) {
		error = ENOEXEC;
		goto out;
	}

	/* Allocate memory for all the section headers */
	shdr = malloc(nbytes, M_LINKER, M_WAITOK);

	/* Read all the section headers */
	if ((error = vn_rdwr(UIO_READ, nd.ni_vp, (caddr_t)shdr, nbytes,
	    hdr->e_shoff, UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
	    NULL, td)) != 0)
		goto out;

	/*
	 * We need to search for the CTF section by name, so if the
	 * section names aren't present, then we can't locate the
	 * .SUNW_ctf section containing the CTF data.
	 */
	if (hdr->e_shstrndx == 0 || shdr[hdr->e_shstrndx].sh_type != SHT_STRTAB) {
		printf("%s(%d): module %s e_shstrndx is %d, sh_type is %d\n",
		    __func__, __LINE__, lf->pathname, hdr->e_shstrndx,
		    shdr[hdr->e_shstrndx].sh_type);
		error = EFTYPE;
		goto out;
	}

	/* Allocate memory to buffer the section header strings. */
	shstrtab = malloc(shdr[hdr->e_shstrndx].sh_size, M_LINKER, M_WAITOK);

	/* Read the section header strings. */
	if ((error = vn_rdwr(UIO_READ, nd.ni_vp, shstrtab,
	    shdr[hdr->e_shstrndx].sh_size, shdr[hdr->e_shstrndx].sh_offset,
	    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED, NULL, td)) != 0)
		goto out;

	/* Search for the section containing the CTF data. */
	for (i = 0; i < hdr->e_shnum; i++)
		if (strcmp(".SUNW_ctf", shstrtab + shdr[i].sh_name) == 0)
			break;

	/* Check if the CTF section wasn't found. */
	if (i >= hdr->e_shnum) {
		printf("%s(%d): module %s has no .SUNW_ctf section\n",
		    __func__, __LINE__, lf->pathname);
		error = EFTYPE;
		goto out;
	}

	/* Read the CTF header. */
	if ((error = vn_rdwr(UIO_READ, nd.ni_vp, ctf_hdr, sizeof(ctf_hdr),
	    shdr[i].sh_offset, UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred,
	    NOCRED, NULL, td)) != 0)
		goto out;

	/* Check the CTF magic number. (XXX check for big endian!) */
	if (ctf_hdr[0] != 0xf1 || ctf_hdr[1] != 0xcf) {
		printf("%s(%d): module %s has invalid format\n",
		    __func__, __LINE__, lf->pathname);
		error = EFTYPE;
		goto out;
	}

	/* Check if version 2. */
	if (ctf_hdr[2] != 2) {
		printf("%s(%d): module %s CTF format version is %d "
		    "(2 expected)\n",
		    __func__, __LINE__, lf->pathname, ctf_hdr[2]);
		error = EFTYPE;
		goto out;
	}

	/* Check if the data is compressed. */
	if ((ctf_hdr[3] & 0x1) != 0) {
		uint32_t *u32 = (uint32_t *) ctf_hdr;

		/*
		 * The last two fields in the CTF header are the offset
		 * from the end of the header to the start of the string
		 * data and the length of that string data. se this
		 * information to determine the decompressed CTF data
		 * buffer required.
		 */
		sz = u32[CTF_HDR_STRTAB_U32] + u32[CTF_HDR_STRLEN_U32] +
		    sizeof(ctf_hdr);

		/*
		 * Allocate memory for the compressed CTF data, including
		 * the header (which isn't compressed).
		 */
		raw = malloc(shdr[i].sh_size, M_LINKER, M_WAITOK);
	} else {
		/*
		 * The CTF data is not compressed, so the ELF section
		 * size is the same as the buffer size required.
		 */
		sz = shdr[i].sh_size;
	}

	/*
	 * Allocate memory to buffer the CTF data in its decompressed
	 * form.
	 */
	ctftab = malloc(sz, M_LINKER, M_WAITOK);

	/*
	 * Read the CTF data into the raw buffer if compressed, or
	 * directly into the CTF buffer otherwise.
	 */
	if ((error = vn_rdwr(UIO_READ, nd.ni_vp, raw == NULL ? ctftab : raw,
	    shdr[i].sh_size, shdr[i].sh_offset, UIO_SYSSPACE, IO_NODELOCKED,
	    td->td_ucred, NOCRED, NULL, td)) != 0)
		goto out;

	/* Check if decompression is required. */
	if (raw != NULL) {
		z_stream zs;
		int ret;

		/*
		 * The header isn't compressed, so copy that into the
		 * CTF buffer first.
		 */
		bcopy(ctf_hdr, ctftab, sizeof(ctf_hdr));

		/* Initialise the zlib structure. */
		bzero(&zs, sizeof(zs));
		zs.zalloc = z_alloc;
		zs.zfree = z_free;

		if (inflateInit(&zs) != Z_OK) {
			error = EIO;
			goto out;
		}

		zs.avail_in = shdr[i].sh_size - sizeof(ctf_hdr);
		zs.next_in = ((uint8_t *) raw) + sizeof(ctf_hdr);
		zs.avail_out = sz - sizeof(ctf_hdr);
		zs.next_out = ((uint8_t *) ctftab) + sizeof(ctf_hdr);
		ret = inflate(&zs, Z_FINISH);
		inflateEnd(&zs);
		if (ret != Z_STREAM_END) {
			printf("%s(%d): zlib inflate returned %d\n", __func__, __LINE__, ret);
			error = EIO;
			goto out;
		}
	}

	/* Got the CTF data! */
	ef->ctftab = ctftab;
	ef->ctfcnt = shdr[i].sh_size;

	/* We'll retain the memory allocated for the CTF data. */
	ctftab = NULL;

	/* Let the caller use the CTF data read. */
	lc->ctftab = ef->ctftab;
	lc->ctfcnt = ef->ctfcnt;
	lc->symtab = ef->ddbsymtab;
	lc->strtab = ef->ddbstrtab;
	lc->strcnt = ef->ddbstrcnt;
	lc->nsym   = ef->ddbsymcnt;
	lc->ctfoffp = (uint32_t **) &ef->ctfoff;
	lc->typoffp = (uint32_t **) &ef->typoff;
	lc->typlenp = &ef->typlen;

out:
	VOP_UNLOCK(nd.ni_vp, 0);
	vn_close(nd.ni_vp, FREAD, td->td_ucred, td);

	if (hdr != NULL)
		free(hdr, M_LINKER);
	if (shdr != NULL)
		free(shdr, M_LINKER);
	if (shstrtab != NULL)
		free(shstrtab, M_LINKER);
	if (ctftab != NULL)
		free(ctftab, M_LINKER);
	if (raw != NULL)
		free(raw, M_LINKER);
#else
	error = EOPNOTSUPP;
#endif

	return (error);
}
