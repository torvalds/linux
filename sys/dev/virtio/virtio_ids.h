/*-
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
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

#ifndef _VIRTIO_IDS_H_
#define _VIRTIO_IDS_H_

/* VirtIO device IDs. */
#define VIRTIO_ID_NETWORK	1
#define VIRTIO_ID_BLOCK		2
#define VIRTIO_ID_CONSOLE	3
#define VIRTIO_ID_ENTROPY	4
#define VIRTIO_ID_BALLOON	5
#define VIRTIO_ID_IOMEMORY	6
#define VIRTIO_ID_RPMSG		7
#define VIRTIO_ID_SCSI		8
#define VIRTIO_ID_9P		9
#define VIRTIO_ID_RPROC_SERIAL	11
#define VIRTIO_ID_CAIF		12
#define VIRTIO_ID_GPU		16
#define VIRTIO_ID_INPUT		18
#define VIRTIO_ID_VSOCK		19
#define VIRTIO_ID_CRYPTO	20

#endif /* _VIRTIO_IDS_H_ */
