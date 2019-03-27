/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __BCM_OSAL_ECORE_PACKAGE
#define __BCM_OSAL_ECORE_PACKAGE

#include "qlnx_os.h"
#include "ecore_status.h"
#include <sys/bitstring.h>

#if __FreeBSD_version >= 1200032
#include <linux/bitmap.h>
#else
#if __FreeBSD_version >= 1100090
#include <compat/linuxkpi/common/include/linux/bitops.h>
#else
#include <ofed/include/linux/bitops.h>
#endif
#endif

#define OSAL_NUM_CPUS()	mp_ncpus
/*
 * prototypes of freebsd specific functions required by ecore
 */
extern uint32_t qlnx_pci_bus_get_bar_size(void *ecore_dev, uint8_t bar_id);
extern uint32_t qlnx_pci_read_config_byte(void *ecore_dev, uint32_t pci_reg,
                        uint8_t *reg_value);
extern uint32_t qlnx_pci_read_config_word(void *ecore_dev, uint32_t pci_reg,
                        uint16_t *reg_value);
extern uint32_t qlnx_pci_read_config_dword(void *ecore_dev, uint32_t pci_reg,
                        uint32_t *reg_value);
extern void qlnx_pci_write_config_byte(void *ecore_dev, uint32_t pci_reg,
                        uint8_t reg_value);
extern void qlnx_pci_write_config_word(void *ecore_dev, uint32_t pci_reg,
                        uint16_t reg_value);
extern void qlnx_pci_write_config_dword(void *ecore_dev, uint32_t pci_reg,
                        uint32_t reg_value);
extern int qlnx_pci_find_capability(void *ecore_dev, int cap);
extern int qlnx_pci_find_ext_capability(void *ecore_dev, int ext_cap);

extern uint32_t qlnx_direct_reg_rd32(void *p_hwfn, uint32_t *reg_addr);
extern void qlnx_direct_reg_wr32(void *p_hwfn, void *reg_addr, uint32_t value);
extern void qlnx_direct_reg_wr64(void *p_hwfn, void *reg_addr, uint64_t value);

extern uint32_t qlnx_reg_rd32(void *p_hwfn, uint32_t reg_addr);
extern void qlnx_reg_wr32(void *p_hwfn, uint32_t reg_addr, uint32_t value);
extern void qlnx_reg_wr16(void *p_hwfn, uint32_t reg_addr, uint16_t value);

extern void qlnx_dbell_wr32(void *p_hwfn, uint32_t reg_addr, uint32_t value);
extern void qlnx_dbell_wr32_db(void *p_hwfn, void *reg_addr, uint32_t value);

extern void *qlnx_dma_alloc_coherent(void *ecore_dev, bus_addr_t *phys,
                        uint32_t size);
extern void qlnx_dma_free_coherent(void *ecore_dev, void *v_addr,
                        bus_addr_t phys, uint32_t size);

extern void qlnx_link_update(void *p_hwfn);
extern void qlnx_barrier(void *p_hwfn);

extern void *qlnx_zalloc(uint32_t size);

extern void qlnx_get_protocol_stats(void *cdev, int proto_type,
		void *proto_stats);

extern void qlnx_sp_isr(void *arg);


extern void qlnx_osal_vf_fill_acquire_resc_req(void *p_hwfn, void *p_resc_req,
			void *p_sw_info);
extern void qlnx_osal_iov_vf_cleanup(void *p_hwfn, uint8_t relative_vf_id);
extern int qlnx_iov_chk_ucast(void *p_hwfn, int vfid, void *params);
extern int qlnx_iov_update_vport(void *p_hwfn, uint8_t vfid, void *params,
		uint16_t *tlvs);
extern int qlnx_pf_vf_msg(void *p_hwfn, uint16_t relative_vf_id);
extern void qlnx_vf_flr_update(void *p_hwfn);

#define nothing			do {} while(0)
#ifdef ECORE_PACKAGE

/* Memory Types */
#define u8 uint8_t 
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t
#define s16 uint16_t
#define s32 uint32_t

#ifndef QLNX_RDMA

typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint16_t __be16;
typedef uint32_t __be32;

static __inline unsigned long
roundup_pow_of_two(unsigned long x)
{
	return (1UL << flsl(x - 1));
}

static __inline int
is_power_of_2(unsigned long n)
{
	return (n == roundup_pow_of_two(n));
}
 
static __inline unsigned long
rounddown_pow_of_two(unsigned long x)
{
	return (1UL << (flsl(x) - 1));
}

#define max_t(type, val1, val2) \
	((type)(val1) > (type)(val2) ? (type)(val1) : (val2))
#define min_t(type, val1, val2) \
	((type)(val1) < (type)(val2) ? (type)(val1) : (val2))

#define ARRAY_SIZE(arr)		(sizeof(arr) / sizeof(arr[0]))
#define BUILD_BUG_ON(cond)	nothing

#endif /* #ifndef QLNX_RDMA */

#define OSAL_UNUSED

#define OSAL_CPU_TO_BE64(val) htobe64(val)
#define OSAL_BE64_TO_CPU(val) be64toh(val)

#define OSAL_CPU_TO_BE32(val) htobe32(val)
#define OSAL_BE32_TO_CPU(val) be32toh(val)

#define OSAL_CPU_TO_LE32(val) htole32(val)
#define OSAL_LE32_TO_CPU(val) le32toh(val)

#define OSAL_CPU_TO_BE16(val) htobe16(val)
#define OSAL_BE16_TO_CPU(val) be16toh(val)

#define OSAL_CPU_TO_LE16(val) htole16(val)
#define OSAL_LE16_TO_CPU(val) le16toh(val)

static __inline uint32_t
qlnx_get_cache_line_size(void)
{
	return (CACHE_LINE_SIZE);
}

#define OSAL_CACHE_LINE_SIZE qlnx_get_cache_line_size()

#define OSAL_BE32 uint32_t
#define dma_addr_t bus_addr_t
#define osal_size_t size_t

typedef struct mtx  osal_spinlock_t;
typedef struct mtx  osal_mutex_t;

typedef void * osal_dpc_t;

typedef struct _osal_list_entry_t
{
    struct _osal_list_entry_t *next, *prev;
} osal_list_entry_t;

typedef struct osal_list_t
{
    osal_list_entry_t *head, *tail;
    unsigned long cnt;
} osal_list_t;

/* OSAL functions */

#define OSAL_UDELAY(time)  DELAY(time)
#define OSAL_MSLEEP(time)  qlnx_mdelay(__func__, time)

#define OSAL_ALLOC(dev, GFP, size) qlnx_zalloc(size)
#define OSAL_ZALLOC(dev, GFP, size) qlnx_zalloc(size)
#define OSAL_VALLOC(dev, size) qlnx_zalloc(size)
#define OSAL_VZALLOC(dev, size) qlnx_zalloc(size)

#define OSAL_FREE(dev, memory) free(memory, M_QLNXBUF)
#define OSAL_VFREE(dev, memory) free(memory, M_QLNXBUF)

#define OSAL_MEM_ZERO(mem, size) bzero(mem, size)

#define OSAL_MEMCPY(dst, src, size) memcpy(dst, src, size)

#define OSAL_DMA_ALLOC_COHERENT(dev, phys, size) \
		qlnx_dma_alloc_coherent(dev, phys, size)

#define OSAL_DMA_FREE_COHERENT(dev, virt, phys, size) \
		qlnx_dma_free_coherent(dev, virt, phys, size)
#define OSAL_VF_CQE_COMPLETION(_dev_p, _cqe, _protocol) (0)

#define REG_WR(hwfn, addr, val)  qlnx_reg_wr32(hwfn, addr, val)
#define REG_WR16(hwfn, addr, val) qlnx_reg_wr16(hwfn, addr, val)
#define DIRECT_REG_WR(p_hwfn, addr, value) qlnx_direct_reg_wr32(p_hwfn, addr, value)
#define DIRECT_REG_WR64(p_hwfn, addr, value) \
		qlnx_direct_reg_wr64(p_hwfn, addr, value)
#define DIRECT_REG_WR_DB(p_hwfn, addr, value) qlnx_dbell_wr32_db(p_hwfn, addr, value)
#define DIRECT_REG_RD(p_hwfn, addr) qlnx_direct_reg_rd32(p_hwfn, addr)
#define REG_RD(hwfn, addr) qlnx_reg_rd32(hwfn, addr)
#define DOORBELL(hwfn, addr, value) \
		qlnx_dbell_wr32(hwfn, addr, value)

#define OSAL_SPIN_LOCK_ALLOC(p_hwfn, mutex)
#define OSAL_SPIN_LOCK_DEALLOC(mutex) mtx_destroy(mutex)
#define OSAL_SPIN_LOCK_INIT(lock) {\
		mtx_init(lock, __func__, MTX_NETWORK_LOCK, MTX_SPIN); \
	}

#define OSAL_SPIN_UNLOCK(lock) {\
		mtx_unlock(lock); \
	}
#define OSAL_SPIN_LOCK(lock) {\
		mtx_lock(lock); \
	}

#define OSAL_MUTEX_ALLOC(p_hwfn, mutex)
#define OSAL_MUTEX_DEALLOC(mutex) mtx_destroy(mutex)
#define OSAL_MUTEX_INIT(lock) {\
		mtx_init(lock, __func__, MTX_NETWORK_LOCK, MTX_DEF);\
	}

#define OSAL_MUTEX_ACQUIRE(lock) mtx_lock(lock)
#define OSAL_MUTEX_RELEASE(lock) mtx_unlock(lock)

#define OSAL_DPC_ALLOC(hwfn) malloc(PAGE_SIZE, M_QLNXBUF, M_NOWAIT)
#define OSAL_DPC_INIT(dpc, hwfn) nothing
extern void qlnx_schedule_recovery(void *p_hwfn);
#define OSAL_SCHEDULE_RECOVERY_HANDLER(x) do {qlnx_schedule_recovery(x);} while(0)
#define OSAL_HW_ERROR_OCCURRED(hwfn, err_type) nothing
#define OSAL_DPC_SYNC(hwfn) nothing

static inline void OSAL_DCBX_AEN(void *p_hwfn, u32 mib_type)
{
	return;
}

static inline bool OSAL_NVM_IS_ACCESS_ENABLED(void *p_hwfn)
{
	return 1;
}

#define OSAL_LIST_INIT(list) \
    do {                       \
        (list)->head = NULL;  \
        (list)->tail = NULL;  \
        (list)->cnt  = 0;     \
    } while (0)

#define OSAL_LIST_INSERT_ENTRY_AFTER(entry, entry_prev, list) \
do {						\
	(entry)->prev = (entry_prev);		\
	(entry)->next = (entry_prev)->next;	\
	(entry)->next->prev = (entry);		\
	(entry_prev)->next = (entry);		\
	(list)->cnt++;				\
} while (0);

#define OSAL_LIST_SPLICE_TAIL_INIT(new_list, list)	\
do {							\
	((new_list)->tail)->next = ((list)->head);	\
	((list)->head)->prev = ((new_list)->tail);	\
	(list)->head = (new_list)->head;		\
	(list)->cnt = (list)->cnt + (new_list)->cnt;	\
	OSAL_LIST_INIT(new_list);			\
} while (0);

#define OSAL_LIST_PUSH_HEAD(entry, list) \
    do {                                                \
        (entry)->prev = (osal_list_entry_t *)0;        \
        (entry)->next = (list)->head;                  \
        if ((list)->tail == (osal_list_entry_t *)0) { \
            (list)->tail = (entry);                    \
        } else {                                        \
            (list)->head->prev = (entry);              \
        }                                               \
        (list)->head = (entry);                        \
        (list)->cnt++;                                 \
    } while (0)

#define OSAL_LIST_PUSH_TAIL(entry, list) \
    do {                                         \
        (entry)->next = (osal_list_entry_t *)0; \
        (entry)->prev = (list)->tail;           \
        if ((list)->tail) {                     \
            (list)->tail->next = (entry);       \
        } else {                                 \
            (list)->head = (entry);             \
        }                                        \
        (list)->tail = (entry);                 \
        (list)->cnt++;                          \
    } while (0)

#define OSAL_LIST_FIRST_ENTRY(list, type, field) \
	(type *)((list)->head)

#define OSAL_LIST_REMOVE_ENTRY(entry, list) \
    do {                                                           \
        if ((list)->head == (entry)) {                            \
            if ((list)->head) {                                   \
                (list)->head = (list)->head->next;               \
                if ((list)->head) {                               \
                    (list)->head->prev = (osal_list_entry_t *)0; \
                } else {                                           \
                    (list)->tail = (osal_list_entry_t *)0;       \
                }                                                  \
                (list)->cnt--;                                    \
            }                                                      \
        } else if ((list)->tail == (entry)) {                     \
            if ((list)->tail) {                                   \
                (list)->tail = (list)->tail->prev;               \
                if ((list)->tail) {                               \
                    (list)->tail->next = (osal_list_entry_t *)0; \
                } else {                                           \
                    (list)->head = (osal_list_entry_t *)0;       \
                }                                                  \
                (list)->cnt--;                                    \
            }                                                      \
        } else {                                                   \
            (entry)->prev->next = (entry)->next;                   \
            (entry)->next->prev = (entry)->prev;                   \
            (list)->cnt--;                                        \
        }                                                          \
    } while (0)


#define OSAL_LIST_IS_EMPTY(list) \
	((list)->cnt == 0)

#define OSAL_LIST_NEXT(entry, field, type) \
    (type *)((&((entry)->field))->next)

#define OSAL_LIST_FOR_EACH_ENTRY(entry, list, field, type) \
	for (entry = OSAL_LIST_FIRST_ENTRY(list, type, field); \
		entry;                                              \
		entry = OSAL_LIST_NEXT(entry, field, type))

#define OSAL_LIST_FOR_EACH_ENTRY_SAFE(entry, tmp_entry, list, field, type) \
     for (entry = OSAL_LIST_FIRST_ENTRY(list, type, field),        \
          tmp_entry = (entry) ? OSAL_LIST_NEXT(entry, field, type) : NULL;    \
          entry != NULL;                                             \
          entry = (type *)tmp_entry,                                         \
          tmp_entry = (entry) ? OSAL_LIST_NEXT(entry, field, type) : NULL)


#define OSAL_BAR_SIZE(dev, bar_id) qlnx_pci_bus_get_bar_size(dev, bar_id)

#define OSAL_PCI_READ_CONFIG_BYTE(dev, reg, value) \
		qlnx_pci_read_config_byte(dev, reg, value);
#define OSAL_PCI_READ_CONFIG_WORD(dev, reg, value) \
		qlnx_pci_read_config_word(dev, reg, value);
#define OSAL_PCI_READ_CONFIG_DWORD(dev, reg, value) \
		qlnx_pci_read_config_dword(dev, reg, value);

#define OSAL_PCI_WRITE_CONFIG_BYTE(dev, reg, value) \
		qlnx_pci_write_config_byte(dev, reg, value);
#define OSAL_PCI_WRITE_CONFIG_WORD(dev, reg, value) \
		qlnx_pci_write_config_word(dev, reg, value);
#define OSAL_PCI_WRITE_CONFIG_DWORD(dev, reg, value) \
		qlnx_pci_write_config_dword(dev, reg, value);

#define OSAL_PCI_FIND_CAPABILITY(dev, cap) qlnx_pci_find_capability(dev, cap)
#define OSAL_PCI_FIND_EXT_CAPABILITY(dev, ext_cap) \
				qlnx_pci_find_ext_capability(dev, ext_cap)

#define OSAL_MMIOWB(dev) qlnx_barrier(dev)
#define OSAL_BARRIER(dev) qlnx_barrier(dev)

#define OSAL_SMP_MB(dev) mb()
#define OSAL_SMP_RMB(dev) rmb()
#define OSAL_SMP_WMB(dev) wmb()
#define OSAL_RMB(dev) rmb()
#define OSAL_WMB(dev) wmb()
#define OSAL_DMA_SYNC(dev, addr, length, is_post)

#define OSAL_FIND_FIRST_BIT find_first_bit
#define OSAL_SET_BIT(bit, bitmap) bit_set((bitstr_t *)bitmap, bit)
#define OSAL_CLEAR_BIT(bit, bitmap) bit_clear((bitstr_t *)bitmap, bit)
#define OSAL_TEST_BIT(bit, bitmap) bit_test((bitstr_t *)bitmap, bit)
#define OSAL_FIND_FIRST_ZERO_BIT(bitmap, length) \
		find_first_zero_bit(bitmap, length)

#define OSAL_LINK_UPDATE(hwfn, ptt) qlnx_link_update(hwfn)

#define QLNX_DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define QLNX_ROUNDUP(x, y) ((((x) + ((y) - 1)) / (y)) * (y))

#define OSAL_NUM_ACTIVE_CPU() mp_ncpus

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(size, to_what) QLNX_DIV_ROUND_UP((size), (to_what))
#endif

#define ROUNDUP(value, to_what) QLNX_ROUNDUP((value), (to_what))

#define OSAL_ROUNDUP_POW_OF_TWO(val) roundup_pow_of_two((val))

static __inline uint32_t
qlnx_log2(uint32_t x)
{
	uint32_t log = 0;

	while (x >>= 1) log++;

	return (log);
}

#define OSAL_LOG2(val) qlnx_log2(val)
#define OFFSETOF(str, field) offsetof(str, field)
#define PRINT device_printf
#define PRINT_ERR device_printf
#define OSAL_ASSERT(is_assert) nothing
#define OSAL_BEFORE_PF_START(cdev, my_id) {};
#define OSAL_AFTER_PF_STOP(cdev, my_id) {};

#define INLINE __inline
#define OSAL_INLINE __inline
#define OSAL_UNLIKELY 
#define OSAL_NULL NULL


#define OSAL_MAX_T(type, __max1, __max2) max_t(type, __max1, __max2)
#define OSAL_MIN_T(type, __max1, __max2) min_t(type, __max1, __max2)

#define __iomem
#define OSAL_IOMEM

#define int_ptr_t void *
#define osal_int_ptr_t void *
#define OSAL_BUILD_BUG_ON(cond) nothing
#define REG_ADDR(hwfn, offset) (void *)((u8 *)(hwfn->regview) + (offset))
#define OSAL_REG_ADDR(hwfn, offset) (void *)((u8 *)(hwfn->regview) + (offset))

#define OSAL_PAGE_SIZE PAGE_SIZE

#define OSAL_STRCPY(dst, src) strcpy(dst, src)
#define OSAL_STRNCPY(dst, src, bytes) strncpy(dst, src, bytes)
#define OSAL_STRLEN(src) strlen(src)
#define OSAL_SPRINTF	sprintf
#define OSAL_SNPRINTF   snprintf
#define OSAL_MEMSET	memset
#define OSAL_ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#define osal_uintptr_t u64

#define OSAL_SLOWPATH_IRQ_REQ(p_hwfn) (0)
#define OSAL_GET_PROTOCOL_STATS(p_hwfn, type, stats) \
		qlnx_get_protocol_stats(p_hwfn, type, stats);
#define OSAL_POLL_MODE_DPC(hwfn) {if (cold) qlnx_sp_isr(hwfn);}
#define OSAL_WARN(cond, fmt, args...) \
	if (cond) printf("%s: WARNING: " fmt, __func__, ## args);

#define OSAL_BITMAP_WEIGHT(bitmap, nbits) bitmap_weight(bitmap, nbits)
#define OSAL_GET_RDMA_SB_ID(p_hwfn, cnq_id) ecore_rdma_get_sb_id(p_hwfn, cnq_id)

static inline int
qlnx_test_and_change_bit(long bit, volatile unsigned long *var)
{
	long val;

	var += BIT_WORD(bit);
	bit %= BITS_PER_LONG;
	bit = (1UL << bit);

	val = *var;

#if __FreeBSD_version >= 1100000
	if (val & bit) 
		return (test_and_clear_bit(bit, var));

	return (test_and_set_bit(bit, var));
#else
	if (val & bit) 
		return (test_and_clear_bit(bit, (long *)var));

	return (test_and_set_bit(bit, (long *)var));

#endif
}

#if __FreeBSD_version < 1100000
static inline unsigned
bitmap_weight(unsigned long *bitmap, unsigned nbits)
{
        unsigned bit;
        unsigned retval = 0;

        for_each_set_bit(bit, bitmap, nbits)
                retval++;
        return (retval);
}

#endif


#define OSAL_TEST_AND_FLIP_BIT qlnx_test_and_change_bit
#define OSAL_TEST_AND_CLEAR_BIT test_and_clear_bit
#define OSAL_MEMCMP memcmp
#define OSAL_SPIN_LOCK_IRQSAVE(x,y) {y=0; mtx_lock(x);}
#define OSAL_SPIN_UNLOCK_IRQSAVE(x,y) {y= 0; mtx_unlock(x);}

static inline u32
OSAL_CRC32(u32 crc, u8 *ptr, u32 length)
{
        int i;

        while (length--) {
                crc ^= *ptr++;
                for (i = 0; i < 8; i++)
                        crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
        }
        return crc;
}

static inline void
OSAL_CRC8_POPULATE(u8 * cdu_crc8_table, u8 polynomial)
{
	return;
}

static inline u8
OSAL_CRC8(u8 * cdu_crc8_table, u8 * data_to_crc, int data_to_crc_len, u8 init_value)
{
    return ECORE_NOTIMPL;
}

#define OSAL_HW_INFO_CHANGE(p_hwfn, offset)
#define OSAL_MFW_TLV_REQ(p_hwfn)
#define OSAL_LLDP_RX_TLVS(p_hwfn, buffer, len)
#define OSAL_MFW_CMD_PREEMPT(p_hwfn)
#define OSAL_TRANSCEIVER_UPDATE(p_hwfn)
#define OSAL_MFW_FILL_TLV_DATA(p_hwfn, group, data) (0)

#define OSAL_VF_UPDATE_ACQUIRE_RESC_RESP(p_hwfn, res) (0)

#define OSAL_VF_FILL_ACQUIRE_RESC_REQ(p_hwfn, req, vf_sw_info) \
		qlnx_osal_vf_fill_acquire_resc_req(p_hwfn, req, vf_sw_info)

#define OSAL_IOV_PF_RESP_TYPE(p_hwfn, relative_vf_id, status)
#define OSAL_IOV_VF_CLEANUP(p_hwfn, relative_vf_id) \
		qlnx_osal_iov_vf_cleanup(p_hwfn, relative_vf_id)

#define OSAL_IOV_VF_ACQUIRE(p_hwfn, relative_vf_id) ECORE_SUCCESS
#define OSAL_IOV_GET_OS_TYPE()	VFPF_ACQUIRE_OS_FREEBSD
#define OSAL_IOV_PRE_START_VPORT(p_hwfn, relative_vf_id, params) ECORE_SUCCESS
#define OSAL_IOV_POST_START_VPORT(p_hwfn, relative_vf_id, vport_id, opaque_fid) 
#define OSAL_PF_VALIDATE_MODIFY_TUNN_CONFIG(p_hwfn, x, y, z) ECORE_SUCCESS
#define OSAL_IOV_CHK_UCAST(p_hwfn, vfid, params) \
			qlnx_iov_chk_ucast(p_hwfn, vfid, params);
#define OSAL_PF_VF_MALICIOUS(p_hwfn, relative_vf_id) 
#define OSAL_IOV_VF_MSG_TYPE(p_hwfn, relative_vf_id, type)
#define OSAL_IOV_VF_VPORT_UPDATE(p_hwfn, vfid, params, tlvs) \
		qlnx_iov_update_vport(p_hwfn, vfid, params, tlvs)
#define OSAL_PF_VF_MSG(p_hwfn, relative_vf_id) \
		qlnx_pf_vf_msg(p_hwfn, relative_vf_id)

#define OSAL_VF_FLR_UPDATE(p_hwfn) qlnx_vf_flr_update(p_hwfn)
#define OSAL_IOV_VF_VPORT_STOP(p_hwfn, vf)

#endif /* #ifdef ECORE_PACKAGE */

#endif /* #ifdef __BCM_OSAL_ECORE_PACKAGE */
