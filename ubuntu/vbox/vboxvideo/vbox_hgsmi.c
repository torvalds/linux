/*
 * Contributed by Hans de Goede <hdegoede@redhat.com>
 *
 * Copyright (C) 2017-2019 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "vbox_drv.h"
#include "vboxvideo_vbe.h"

/* One-at-a-Time Hash from http://www.burtleburtle.net/bob/hash/doobs.html */
static u32 hgsmi_hash_process(u32 hash, const u8 *data, int size)
{
	while (size--) {
		hash += *data++;
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	return hash;
}

static u32 hgsmi_hash_end(u32 hash)
{
	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return hash;
}

/* Not really a checksum but that is the naming used in all vbox code */
static u32 hgsmi_checksum(u32 offset,
			  const HGSMIBUFFERHEADER *header,
			  const HGSMIBUFFERTAIL *tail)
{
	u32 checksum;

	checksum = hgsmi_hash_process(0, (u8 *)&offset, sizeof(offset));
	checksum = hgsmi_hash_process(checksum, (u8 *)header, sizeof(*header));
	/* 4 -> Do not checksum the checksum itself */
	checksum = hgsmi_hash_process(checksum, (u8 *)tail, 4);

	return hgsmi_hash_end(checksum);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)
void *gen_pool_dma_alloc(struct gen_pool *pool, size_t size, dma_addr_t *dma)
{
	unsigned long vaddr = gen_pool_alloc(pool, size);

	if (vaddr)
		*dma = gen_pool_virt_to_phys(pool, vaddr);
	return (void *)vaddr;
}
#endif

void *hgsmi_buffer_alloc(struct gen_pool *guest_pool, size_t size,
			 u8 channel, u16 channel_info)
{
	HGSMIBUFFERHEADER *h;
	HGSMIBUFFERTAIL *t;
	size_t total_size;
	dma_addr_t offset;

	total_size = size + sizeof(*h) + sizeof(*t);
	h = gen_pool_dma_alloc(guest_pool, total_size, &offset);
	if (!h)
		return NULL;

	t = (HGSMIBUFFERTAIL *)((u8 *)h + sizeof(*h) + size);

	h->u8Flags = HGSMI_BUFFER_HEADER_F_SEQ_SINGLE;
	h->u32DataSize = size;
	h->u8Channel = channel;
	h->u16ChannelInfo = channel_info;
	memset(&h->u.au8Union, 0, sizeof(h->u.au8Union));

	t->reserved = 0;
	t->u32Checksum = hgsmi_checksum(offset, h, t);

	return (u8 *)h + sizeof(*h);
}

void hgsmi_buffer_free(struct gen_pool *guest_pool, void *buf)
{
	HGSMIBUFFERHEADER *h =
		(HGSMIBUFFERHEADER *)((u8 *)buf - sizeof(*h));
	size_t total_size = h->u32DataSize + sizeof(*h) +
					     sizeof(HGSMIBUFFERTAIL);

	gen_pool_free(guest_pool, (unsigned long)h, total_size);
}

int hgsmi_buffer_submit(struct gen_pool *guest_pool, void *buf)
{
	phys_addr_t offset;

	offset = gen_pool_virt_to_phys(guest_pool, (unsigned long)buf -
						   sizeof(HGSMIBUFFERHEADER));
	outl(offset, VGA_PORT_HGSMI_GUEST);
	/* Make the compiler aware that the host has changed memory. */
	mb();

	return 0;
}
