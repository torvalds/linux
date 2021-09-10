#ifndef _LINUX_VIRTIO_IDS_H
#define _LINUX_VIRTIO_IDS_H
/*
 * Virtio IDs
 *
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */

#define VIRTIO_ID_NET			1 /* virtio net */
#define VIRTIO_ID_BLOCK			2 /* virtio block */
#define VIRTIO_ID_CONSOLE		3 /* virtio console */
#define VIRTIO_ID_RNG			4 /* virtio rng */
#define VIRTIO_ID_BALLOON		5 /* virtio balloon */
#define VIRTIO_ID_IOMEM			6 /* virtio ioMemory */
#define VIRTIO_ID_RPMSG			7 /* virtio remote processor messaging */
#define VIRTIO_ID_SCSI			8 /* virtio scsi */
#define VIRTIO_ID_9P			9 /* 9p virtio console */
#define VIRTIO_ID_MAC80211_WLAN		10 /* virtio WLAN MAC */
#define VIRTIO_ID_RPROC_SERIAL		11 /* virtio remoteproc serial link */
#define VIRTIO_ID_CAIF			12 /* Virtio caif */
#define VIRTIO_ID_MEMORY_BALLOON	13 /* virtio memory balloon */
#define VIRTIO_ID_GPU			16 /* virtio GPU */
#define VIRTIO_ID_CLOCK			17 /* virtio clock/timer */
#define VIRTIO_ID_INPUT			18 /* virtio input */
#define VIRTIO_ID_VSOCK			19 /* virtio vsock transport */
#define VIRTIO_ID_CRYPTO		20 /* virtio crypto */
#define VIRTIO_ID_SIGNAL_DIST		21 /* virtio signal distribution device */
#define VIRTIO_ID_PSTORE		22 /* virtio pstore device */
#define VIRTIO_ID_IOMMU			23 /* virtio IOMMU */
#define VIRTIO_ID_MEM			24 /* virtio mem */
#define VIRTIO_ID_SOUND			25 /* virtio sound */
#define VIRTIO_ID_FS			26 /* virtio filesystem */
#define VIRTIO_ID_PMEM			27 /* virtio pmem */
#define VIRTIO_ID_MAC80211_HWSIM	29 /* virtio mac80211-hwsim */
#define VIRTIO_ID_SCMI			32 /* virtio SCMI */
#define VIRTIO_ID_I2C_ADAPTER		34 /* virtio i2c adapter */
#define VIRTIO_ID_BT			40 /* virtio bluetooth */
#define VIRTIO_ID_GPIO			41 /* virtio gpio */

/*
 * Virtio Transitional IDs
 */

#define VIRTIO_TRANS_ID_NET		1000 /* transitional virtio net */
#define VIRTIO_TRANS_ID_BLOCK		1001 /* transitional virtio block */
#define VIRTIO_TRANS_ID_BALLOON		1002 /* transitional virtio balloon */
#define VIRTIO_TRANS_ID_CONSOLE		1003 /* transitional virtio console */
#define VIRTIO_TRANS_ID_SCSI		1004 /* transitional virtio SCSI */
#define VIRTIO_TRANS_ID_RNG		1005 /* transitional virtio rng */
#define VIRTIO_TRANS_ID_9P		1009 /* transitional virtio 9p console */

#endif /* _LINUX_VIRTIO_IDS_H */
