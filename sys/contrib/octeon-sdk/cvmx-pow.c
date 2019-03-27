/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/







/**
 * @file
 *
 * Interface to the hardware Packet Order / Work unit.
 *
 * <hr>$Revision: 29727 $<hr>
 */

#include "cvmx.h"
#include "cvmx-pow.h"

/**
 * @INTERNAL
 * This structure stores the internal POW state captured by
 * cvmx_pow_capture(). It is purposely not exposed to the user
 * since the format may change without notice.
 */
typedef struct
{
    cvmx_pow_tag_load_resp_t sstatus[CVMX_MAX_CORES][8];
    cvmx_pow_tag_load_resp_t smemload[2048][8];
    cvmx_pow_tag_load_resp_t sindexload[64][8];
} __cvmx_pow_dump_t;

typedef enum
{
    CVMX_POW_LIST_UNKNOWN=0,
    CVMX_POW_LIST_FREE=1,
    CVMX_POW_LIST_INPUT=2,
    CVMX_POW_LIST_CORE=CVMX_POW_LIST_INPUT+8,
    CVMX_POW_LIST_DESCHED=CVMX_POW_LIST_CORE+32,
    CVMX_POW_LIST_NOSCHED=CVMX_POW_LIST_DESCHED+64,
} __cvmx_pow_list_types_t;

static const char *__cvmx_pow_list_names[] = {
    "Unknown",
    "Free List",
    "Queue 0", "Queue 1", "Queue 2", "Queue 3",
    "Queue 4", "Queue 5", "Queue 6", "Queue 7",
    "Core 0", "Core 1", "Core 2", "Core 3",
    "Core 4", "Core 5", "Core 6", "Core 7",
    "Core 8", "Core 9", "Core 10", "Core 11",
    "Core 12", "Core 13", "Core 14", "Core 15",
    "Core 16", "Core 17", "Core 18", "Core 19",
    "Core 20", "Core 21", "Core 22", "Core 23",
    "Core 24", "Core 25", "Core 26", "Core 27",
    "Core 28", "Core 29", "Core 30", "Core 31",
    "Desched 0", "Desched 1", "Desched 2", "Desched 3",
    "Desched 4", "Desched 5", "Desched 6", "Desched 7",
    "Desched 8", "Desched 9", "Desched 10", "Desched 11",
    "Desched 12", "Desched 13", "Desched 14", "Desched 15",
    "Desched 16", "Desched 17", "Desched 18", "Desched 19",
    "Desched 20", "Desched 21", "Desched 22", "Desched 23",
    "Desched 24", "Desched 25", "Desched 26", "Desched 27",
    "Desched 28", "Desched 29", "Desched 30", "Desched 31",
    "Desched 32", "Desched 33", "Desched 34", "Desched 35",
    "Desched 36", "Desched 37", "Desched 38", "Desched 39",
    "Desched 40", "Desched 41", "Desched 42", "Desched 43",
    "Desched 44", "Desched 45", "Desched 46", "Desched 47",
    "Desched 48", "Desched 49", "Desched 50", "Desched 51",
    "Desched 52", "Desched 53", "Desched 54", "Desched 55",
    "Desched 56", "Desched 57", "Desched 58", "Desched 59",
    "Desched 60", "Desched 61", "Desched 62", "Desched 63",
    "Nosched 0" 
};


/**
 * Return the number of POW entries supported by this chip
 *
 * @return Number of POW entries
 */
int cvmx_pow_get_num_entries(void)
{
    if (OCTEON_IS_MODEL(OCTEON_CN30XX))
        return 64;
    else if (OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN50XX))
        return 256;
    else if (OCTEON_IS_MODEL(OCTEON_CN52XX)
             || OCTEON_IS_MODEL(OCTEON_CN61XX)
             || OCTEON_IS_MODEL(OCTEON_CNF71XX))
        return 512;
    else if (OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX))
	return 1024;
    else
        return 2048;
}


static int __cvmx_pow_capture_v1(void *buffer, int buffer_size)
{
    __cvmx_pow_dump_t *dump = (__cvmx_pow_dump_t*)buffer;
    int num_cores;
    int num_pow_entries = cvmx_pow_get_num_entries();
    int core;
    int index;
    int bits;

    if (buffer_size < (int)sizeof(__cvmx_pow_dump_t))
    {
        cvmx_dprintf("cvmx_pow_capture: Buffer too small\n");
        return -1;
    }

    num_cores = cvmx_octeon_num_cores();

    /* Read all core related state */
    for (core=0; core<num_cores; core++)
    {
        cvmx_pow_load_addr_t load_addr;
        load_addr.u64 = 0;
        load_addr.sstatus.mem_region = CVMX_IO_SEG;
        load_addr.sstatus.is_io = 1;
        load_addr.sstatus.did = CVMX_OCT_DID_TAG_TAG1;
        load_addr.sstatus.coreid = core;
        for (bits=0; bits<8; bits++)
        {
            load_addr.sstatus.get_rev = (bits & 1) != 0;
            load_addr.sstatus.get_cur = (bits & 2) != 0;
            load_addr.sstatus.get_wqp = (bits & 4) != 0;
            if ((load_addr.sstatus.get_cur == 0) && load_addr.sstatus.get_rev)
                dump->sstatus[core][bits].u64 = -1;
            else
                dump->sstatus[core][bits].u64 = cvmx_read_csr(load_addr.u64);
        }
    }

    /* Read all internal POW entries */
    for (index=0; index<num_pow_entries; index++)
    {
        cvmx_pow_load_addr_t load_addr;
        load_addr.u64 = 0;
        load_addr.smemload.mem_region = CVMX_IO_SEG;
        load_addr.smemload.is_io = 1;
        load_addr.smemload.did = CVMX_OCT_DID_TAG_TAG2;
        load_addr.smemload.index = index;
        for (bits=0; bits<3; bits++)
        {
            load_addr.smemload.get_des = (bits & 1) != 0;
            load_addr.smemload.get_wqp = (bits & 2) != 0;
            dump->smemload[index][bits].u64 = cvmx_read_csr(load_addr.u64);
        }
    }

    /* Read all group and queue pointers */
    for (index=0; index<16; index++)
    {
        cvmx_pow_load_addr_t load_addr;
        load_addr.u64 = 0;
        load_addr.sindexload.mem_region = CVMX_IO_SEG;
        load_addr.sindexload.is_io = 1;
        load_addr.sindexload.did = CVMX_OCT_DID_TAG_TAG3;
        load_addr.sindexload.qosgrp = index;
        for (bits=0; bits<4; bits++)
        {
            load_addr.sindexload.get_rmt =  (bits & 1) != 0;
            load_addr.sindexload.get_des_get_tail =  (bits & 2) != 0;
            /* The first pass only has 8 valid index values */
            if ((load_addr.sindexload.get_rmt == 0) &&
                (load_addr.sindexload.get_des_get_tail == 0) &&
                (index >= 8))
                dump->sindexload[index][bits].u64 = -1;
            else
                dump->sindexload[index][bits].u64 = cvmx_read_csr(load_addr.u64);
        }
    }
    return 0;
}

static int __cvmx_pow_capture_v2(void *buffer, int buffer_size)
{
    __cvmx_pow_dump_t *dump = (__cvmx_pow_dump_t*)buffer;
    int num_cores;
    int num_pow_entries = cvmx_pow_get_num_entries();
    int core;
    int index;
    int bits;

    if (buffer_size < (int)sizeof(__cvmx_pow_dump_t))
    {
        cvmx_dprintf("cvmx_pow_capture: Buffer too small\n");
        return -1;
    }

    num_cores = cvmx_octeon_num_cores();

    /* Read all core related state */
    for (core=0; core<num_cores; core++)
    {
        cvmx_pow_load_addr_t load_addr;
        load_addr.u64 = 0;
        load_addr.sstatus_cn68xx.mem_region = CVMX_IO_SEG;
        load_addr.sstatus_cn68xx.is_io = 1;
        load_addr.sstatus_cn68xx.did = CVMX_OCT_DID_TAG_TAG5;
        load_addr.sstatus_cn68xx.coreid = core;
        for (bits=1; bits<6; bits++)
        {
            load_addr.sstatus_cn68xx.opcode = bits;
            dump->sstatus[core][bits].u64 = cvmx_read_csr(load_addr.u64);
        }
    }
    /* Read all internal POW entries */
    for (index=0; index<num_pow_entries; index++)
    {
        cvmx_pow_load_addr_t load_addr;
        load_addr.u64 = 0;
        load_addr.smemload_cn68xx.mem_region = CVMX_IO_SEG;
        load_addr.smemload_cn68xx.is_io = 1;
        load_addr.smemload_cn68xx.did = CVMX_OCT_DID_TAG_TAG2;
        load_addr.smemload_cn68xx.index = index;
        for (bits=1; bits<5; bits++)
        {
            load_addr.smemload_cn68xx.opcode = bits;
            dump->smemload[index][bits].u64 = cvmx_read_csr(load_addr.u64);
        }
    }

    /* Read all group and queue pointers */
    for (index=0; index<64; index++)
    {
        cvmx_pow_load_addr_t load_addr;
        load_addr.u64 = 0;
        load_addr.sindexload_cn68xx.mem_region = CVMX_IO_SEG;
        load_addr.sindexload_cn68xx.is_io = 1;
        load_addr.sindexload_cn68xx.did = CVMX_OCT_DID_TAG_TAG3;
        load_addr.sindexload_cn68xx.qos_grp = index;
        for (bits=1; bits<7; bits++)
        {
            load_addr.sindexload_cn68xx.opcode = bits;
            dump->sindexload[index][bits].u64 = cvmx_read_csr(load_addr.u64);
        }
    }
    return 0;
}

/**
 * Store the current POW internal state into the supplied
 * buffer. It is recommended that you pass a buffer of at least
 * 128KB. The format of the capture may change based on SDK
 * version and Octeon chip.
 *
 * @param buffer Buffer to store capture into
 * @param buffer_size
 *               The size of the supplied buffer
 *
 * @return Zero on sucess, negative on failure
 */
int cvmx_pow_capture(void *buffer, int buffer_size)
{
    if (octeon_has_feature(OCTEON_FEATURE_PKND))
        return __cvmx_pow_capture_v2(buffer, buffer_size);
    else
        return __cvmx_pow_capture_v1(buffer, buffer_size);
}

/**
 * Function to display a POW internal queue to the user
 *
 * @param name       User visible name for the queue
 * @param name_param Parameter for printf in creating the name
 * @param valid      Set if the queue contains any elements
 * @param has_one    Set if the queue contains exactly one element
 * @param head       The head pointer
 * @param tail       The tail pointer
 */
static void __cvmx_pow_display_list(const char *name, int name_param, int valid, int has_one, uint64_t head, uint64_t tail)
{
    printf(name, name_param);
    printf(": ");
    if (valid)
    {
        if (has_one)
            printf("One element index=%llu(0x%llx)\n", CAST64(head), CAST64(head));
        else
            printf("Multiple elements head=%llu(0x%llx) tail=%llu(0x%llx)\n", CAST64(head), CAST64(head), CAST64(tail), CAST64(tail));
    }
    else
        printf("Empty\n");
}


/**
 * Mark which list a POW entry is on. Print a warning message if the
 * entry is already on a list. This happens if the POW changed while
 * the capture was running.
 *
 * @param entry_num  Entry number to mark
 * @param entry_type List type
 * @param entry_list Array to store marks
 *
 * @return Zero on success, negative if already on a list
 */
static int __cvmx_pow_entry_mark_list(int entry_num, __cvmx_pow_list_types_t entry_type, uint8_t entry_list[])
{
    if (entry_list[entry_num] == 0)
    {
        entry_list[entry_num] = entry_type;
        return 0;
    }
    else
    {
        printf("\nWARNING: Entry %d already on list %s, but we tried to add it to %s\n",
               entry_num, __cvmx_pow_list_names[entry_list[entry_num]], __cvmx_pow_list_names[entry_type]);
        return -1;
    }
}


/**
 * Display a list and mark all elements on the list as belonging to
 * the list.
 *
 * @param entry_type Type of the list to display and mark
 * @param dump       POW capture data
 * @param entry_list Array to store marks in
 * @param valid      Set if the queue contains any elements
 * @param has_one    Set if the queue contains exactly one element
 * @param head       The head pointer
 * @param tail       The tail pointer
 */
static void __cvmx_pow_display_list_and_walk(__cvmx_pow_list_types_t entry_type,
                                             __cvmx_pow_dump_t *dump, uint8_t entry_list[],
                                             int valid, int has_one, uint64_t head, uint64_t tail)
{
    __cvmx_pow_display_list(__cvmx_pow_list_names[entry_type], 0, valid, has_one, head, tail);
    if (valid)
    {
        if (has_one)
            __cvmx_pow_entry_mark_list(head, entry_type, entry_list);
        else
        {
            while (head != tail)
            {
                if (__cvmx_pow_entry_mark_list(head, entry_type, entry_list))
                    break;
                if (octeon_has_feature(OCTEON_FEATURE_PKND))
                {
                    if (entry_type >= CVMX_POW_LIST_INPUT && entry_type < CVMX_POW_LIST_CORE)
                 
                        head = dump->smemload[head][4].s_smemload3_cn68xx.next_index;
                    else
                        head = dump->smemload[head][4].s_smemload3_cn68xx.fwd_index;
                }
                else
                    head = dump->smemload[head][0].s_smemload0.next_index;
            }
            __cvmx_pow_entry_mark_list(tail, entry_type, entry_list);
        }
    }
}


void __cvmx_pow_display_v1(void *buffer, int buffer_size)
{
    __cvmx_pow_dump_t *dump = (__cvmx_pow_dump_t*)buffer;
    int num_pow_entries = cvmx_pow_get_num_entries();
    int num_cores;
    int core;
    int index;
    uint8_t entry_list[2048];

    if (buffer_size < (int)sizeof(__cvmx_pow_dump_t))
    {
        cvmx_dprintf("cvmx_pow_dump: Buffer too small\n");
        return;
    }

    memset(entry_list, 0, sizeof(entry_list));
    num_cores = cvmx_octeon_num_cores();

    /* Print the free list info */
    __cvmx_pow_display_list_and_walk(CVMX_POW_LIST_FREE, dump, entry_list,
                                     dump->sindexload[0][0].sindexload0.free_val,
                                     dump->sindexload[0][0].sindexload0.free_one,
                                     dump->sindexload[0][0].sindexload0.free_head,
                                     dump->sindexload[0][0].sindexload0.free_tail);

    /* Print the core state */
    for (core=0; core<num_cores; core++)
    {
        const int bit_rev = 1;
        const int bit_cur = 2;
        const int bit_wqp = 4;
        printf("Core %d State:  tag=%s,0x%08x", core,
               OCT_TAG_TYPE_STRING(dump->sstatus[core][bit_cur].s_sstatus2.tag_type),
               dump->sstatus[core][bit_cur].s_sstatus2.tag);
        if (dump->sstatus[core][bit_cur].s_sstatus2.tag_type != CVMX_POW_TAG_TYPE_NULL_NULL)
        {
            __cvmx_pow_entry_mark_list(dump->sstatus[core][bit_cur].s_sstatus2.index, CVMX_POW_LIST_CORE + core, entry_list);
            printf(" grp=%d",                   dump->sstatus[core][bit_cur].s_sstatus2.grp);
            printf(" wqp=0x%016llx",            CAST64(dump->sstatus[core][bit_cur|bit_wqp].s_sstatus4.wqp));
            printf(" index=%d",                 dump->sstatus[core][bit_cur].s_sstatus2.index);
            if (dump->sstatus[core][bit_cur].s_sstatus2.head)
                printf(" head");
            else
                printf(" prev=%d", dump->sstatus[core][bit_cur|bit_rev].s_sstatus3.revlink_index);
            if (dump->sstatus[core][bit_cur].s_sstatus2.tail)
                printf(" tail");
            else
                printf(" next=%d", dump->sstatus[core][bit_cur].s_sstatus2.link_index);
        }

        if (dump->sstatus[core][0].s_sstatus0.pend_switch)
        {
            printf(" pend_switch=%d",           dump->sstatus[core][0].s_sstatus0.pend_switch);
            printf(" pend_switch_full=%d",      dump->sstatus[core][0].s_sstatus0.pend_switch_full);
            printf(" pend_switch_null=%d",      dump->sstatus[core][0].s_sstatus0.pend_switch_null);
        }

        if (dump->sstatus[core][0].s_sstatus0.pend_desched)
        {
            printf(" pend_desched=%d",          dump->sstatus[core][0].s_sstatus0.pend_desched);
            printf(" pend_desched_switch=%d",   dump->sstatus[core][0].s_sstatus0.pend_desched_switch);
            printf(" pend_nosched=%d",          dump->sstatus[core][0].s_sstatus0.pend_nosched);
            if (dump->sstatus[core][0].s_sstatus0.pend_desched_switch)
                printf(" pend_grp=%d",              dump->sstatus[core][0].s_sstatus0.pend_grp);
        }

        if (dump->sstatus[core][0].s_sstatus0.pend_new_work)
        {
            if (dump->sstatus[core][0].s_sstatus0.pend_new_work_wait)
                printf(" (Waiting for work)");
            else
                printf(" (Getting work)");
        }
        if (dump->sstatus[core][0].s_sstatus0.pend_null_rd)
            printf(" pend_null_rd=%d",          dump->sstatus[core][0].s_sstatus0.pend_null_rd);
        if (dump->sstatus[core][0].s_sstatus0.pend_nosched_clr)
        {
            printf(" pend_nosched_clr=%d",      dump->sstatus[core][0].s_sstatus0.pend_nosched_clr);
            printf(" pend_index=%d",            dump->sstatus[core][0].s_sstatus0.pend_index);
        }
        if (dump->sstatus[core][0].s_sstatus0.pend_switch ||
            (dump->sstatus[core][0].s_sstatus0.pend_desched &&
            dump->sstatus[core][0].s_sstatus0.pend_desched_switch))
        {
            printf(" pending tag=%s,0x%08x",
                   OCT_TAG_TYPE_STRING(dump->sstatus[core][0].s_sstatus0.pend_type),
                   dump->sstatus[core][0].s_sstatus0.pend_tag);
        }
        if (dump->sstatus[core][0].s_sstatus0.pend_nosched_clr)
            printf(" pend_wqp=0x%016llx\n",     CAST64(dump->sstatus[core][bit_wqp].s_sstatus1.pend_wqp));
        printf("\n");
    }

    /* Print out the state of the nosched list and the 16 deschedule lists. */
    __cvmx_pow_display_list_and_walk(CVMX_POW_LIST_NOSCHED, dump, entry_list,
                            dump->sindexload[0][2].sindexload1.nosched_val,
                            dump->sindexload[0][2].sindexload1.nosched_one,
                            dump->sindexload[0][2].sindexload1.nosched_head,
                            dump->sindexload[0][2].sindexload1.nosched_tail);
    for (index=0; index<16; index++)
    {
        __cvmx_pow_display_list_and_walk(CVMX_POW_LIST_DESCHED + index, dump, entry_list,
                                dump->sindexload[index][2].sindexload1.des_val,
                                dump->sindexload[index][2].sindexload1.des_one,
                                dump->sindexload[index][2].sindexload1.des_head,
                                dump->sindexload[index][2].sindexload1.des_tail);
    }

    /* Print out the state of the 8 internal input queues */
    for (index=0; index<8; index++)
    {
        __cvmx_pow_display_list_and_walk(CVMX_POW_LIST_INPUT + index, dump, entry_list,
                                dump->sindexload[index][0].sindexload0.loc_val,
                                dump->sindexload[index][0].sindexload0.loc_one,
                                dump->sindexload[index][0].sindexload0.loc_head,
                                dump->sindexload[index][0].sindexload0.loc_tail);
    }

    /* Print out the state of the 16 memory queues */
    for (index=0; index<8; index++)
    {
        const char *name;
        if (dump->sindexload[index][1].sindexload2.rmt_is_head)
            name = "Queue %da Memory (is head)";
        else
            name = "Queue %da Memory";
        __cvmx_pow_display_list(name, index,
                                dump->sindexload[index][1].sindexload2.rmt_val,
                                dump->sindexload[index][1].sindexload2.rmt_one,
                                dump->sindexload[index][1].sindexload2.rmt_head,
                                dump->sindexload[index][3].sindexload3.rmt_tail);
        if (dump->sindexload[index+8][1].sindexload2.rmt_is_head)
            name = "Queue %db Memory (is head)";
        else
            name = "Queue %db Memory";
        __cvmx_pow_display_list(name, index,
                                dump->sindexload[index+8][1].sindexload2.rmt_val,
                                dump->sindexload[index+8][1].sindexload2.rmt_one,
                                dump->sindexload[index+8][1].sindexload2.rmt_head,
                                dump->sindexload[index+8][3].sindexload3.rmt_tail);
    }

    /* Print out each of the internal POW entries. Each entry has a tag, group,
        wqe, and possibly a next pointer. The next pointer is only valid if this
        entry isn't make as a tail */
    for (index=0; index<num_pow_entries; index++)
    {
        printf("Entry %d(%-10s): tag=%s,0x%08x grp=%d wqp=0x%016llx", index,
               __cvmx_pow_list_names[entry_list[index]],
               OCT_TAG_TYPE_STRING(dump->smemload[index][0].s_smemload0.tag_type),
               dump->smemload[index][0].s_smemload0.tag,
               dump->smemload[index][0].s_smemload0.grp,
               CAST64(dump->smemload[index][2].s_smemload1.wqp));
        if (dump->smemload[index][0].s_smemload0.tail)
            printf(" tail");
        else
            printf(" next=%d", dump->smemload[index][0].s_smemload0.next_index);
        if (entry_list[index] >= CVMX_POW_LIST_DESCHED)
        {
            printf(" nosched=%d", dump->smemload[index][1].s_smemload2.nosched);
            if (dump->smemload[index][1].s_smemload2.pend_switch)
            {
                printf(" pending tag=%s,0x%08x",
                       OCT_TAG_TYPE_STRING(dump->smemload[index][1].s_smemload2.pend_type),
                       dump->smemload[index][1].s_smemload2.pend_tag);
            }
        }
        printf("\n");
    }
}

void __cvmx_pow_display_v2(void *buffer, int buffer_size)
{
    __cvmx_pow_dump_t *dump = (__cvmx_pow_dump_t*)buffer;
    int num_pow_entries = cvmx_pow_get_num_entries();
    int num_cores;
    int core;
    int index;
    uint8_t entry_list[2048];

    if (buffer_size < (int)sizeof(__cvmx_pow_dump_t))
    {
        cvmx_dprintf("cvmx_pow_dump: Buffer too small, pow_dump_t = 0x%x, buffer_size = 0x%x\n", (int)sizeof(__cvmx_pow_dump_t), buffer_size);
        return;
    }

    memset(entry_list, 0, sizeof(entry_list));
    num_cores = cvmx_octeon_num_cores();

    /* Print the free list info */
    {
        int valid[3], has_one[3], head[3], tail[3], qnum_head, qnum_tail;
        int idx;

        valid[0] = dump->sindexload[0][4].sindexload1_cn68xx.queue_val;
        valid[1] = dump->sindexload[0][5].sindexload1_cn68xx.queue_val;
        valid[2] = dump->sindexload[0][6].sindexload1_cn68xx.queue_val;
        has_one[0] = dump->sindexload[0][4].sindexload1_cn68xx.queue_one;
        has_one[1] = dump->sindexload[0][5].sindexload1_cn68xx.queue_one;
        has_one[2] = dump->sindexload[0][6].sindexload1_cn68xx.queue_one;
        head[0] = dump->sindexload[0][4].sindexload1_cn68xx.queue_head;
        head[1] = dump->sindexload[0][5].sindexload1_cn68xx.queue_head;
        head[2] = dump->sindexload[0][6].sindexload1_cn68xx.queue_head;
        tail[0] = dump->sindexload[0][4].sindexload1_cn68xx.queue_tail;
        tail[1] = dump->sindexload[0][5].sindexload1_cn68xx.queue_tail;
        tail[2] = dump->sindexload[0][6].sindexload1_cn68xx.queue_tail;
        qnum_head = dump->sindexload[0][4].sindexload1_cn68xx.qnum_head;
        qnum_tail = dump->sindexload[0][4].sindexload1_cn68xx.qnum_tail;

        printf("Free List: qnum_head=%d, qnum_tail=%d\n", qnum_head, qnum_tail);
        printf("Free0: valid=%d, one=%d, head=%llu, tail=%llu\n", valid[0], has_one[0], CAST64(head[0]), CAST64(tail[0]));
        printf("Free1: valid=%d, one=%d, head=%llu, tail=%llu\n", valid[1], has_one[1], CAST64(head[1]), CAST64(tail[1]));
        printf("Free2: valid=%d, one=%d, head=%llu, tail=%llu\n", valid[2], has_one[2], CAST64(head[2]), CAST64(tail[2]));
        
        idx=qnum_head;
        while (valid[0] || valid[1] || valid[2])
        {
            int qidx = idx % 3;

            if (head[qidx] == tail[qidx])
                valid[qidx] = 0;

            if (__cvmx_pow_entry_mark_list(head[qidx], CVMX_POW_LIST_FREE, entry_list))   
                break;
            head[qidx] = dump->smemload[head[qidx]][4].s_smemload3_cn68xx.fwd_index;
            //printf("qidx = %d, idx = %d, head[qidx] = %d\n", qidx, idx, head[qidx]);
            idx++;
        }
    }
            
    /* Print the core state */
    for (core = 0; core < num_cores; core++)
    {
        int pendtag = 1;
        int pendwqp = 2;
        int tag = 3;
        int wqp = 4;
        int links = 5;

        printf("Core %d State: tag=%s,0x%08x", core, 
               OCT_TAG_TYPE_STRING(dump->sstatus[core][tag].s_sstatus2_cn68xx.tag_type),
               dump->sstatus[core][tag].s_sstatus2_cn68xx.tag);
        if (dump->sstatus[core][tag].s_sstatus2_cn68xx.tag_type != CVMX_POW_TAG_TYPE_NULL_NULL)
        {
            __cvmx_pow_entry_mark_list(dump->sstatus[core][tag].s_sstatus2_cn68xx.index, CVMX_POW_LIST_CORE + core, entry_list);
            printf(" grp=%d",                   dump->sstatus[core][tag].s_sstatus2_cn68xx.grp);
            printf(" wqp=0x%016llx",            CAST64(dump->sstatus[core][wqp].s_sstatus3_cn68xx.wqp));
            printf(" index=%d",                 dump->sstatus[core][tag].s_sstatus2_cn68xx.index);
            if (dump->sstatus[core][links].s_sstatus4_cn68xx.head)
                printf(" head");
            else
                printf(" prev=%d", dump->sstatus[core][links].s_sstatus4_cn68xx.revlink_index);
            if (dump->sstatus[core][links].s_sstatus4_cn68xx.tail)
                printf(" tail");
            else
                printf(" next=%d", dump->sstatus[core][links].s_sstatus4_cn68xx.link_index);
        }
        if (dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_switch)
        {
            printf(" pend_switch=%d",           dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_switch);
        }
                                                                                
        if (dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_desched)
        {
            printf(" pend_desched=%d",          dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_desched);
            printf(" pend_nosched=%d",          dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_nosched);
        }
        if (dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_get_work)
        {
            if (dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_get_work_wait)
                printf(" (Waiting for work)");
            else
                printf(" (Getting work)");
        }
        if (dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_alloc_we)
            printf(" pend_alloc_we=%d",          dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_alloc_we);
        if (dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_nosched_clr)
        {
            printf(" pend_nosched_clr=%d",      dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_nosched_clr);
            printf(" pend_index=%d",            dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_index);
        }
        if (dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_switch)
        {
            printf(" pending tag=%s,0x%08x",
                   OCT_TAG_TYPE_STRING(dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_type),
                   dump->sstatus[core][pendtag].s_sstatus0_cn68xx.pend_tag);
        }
        if (dump->sstatus[core][pendwqp].s_sstatus1_cn68xx.pend_nosched_clr)
            printf(" pend_wqp=0x%016llx\n",     CAST64(dump->sstatus[core][pendwqp].s_sstatus1_cn68xx.pend_wqp));
        printf("\n");
    }

    /* Print out the state of the nosched list and the 16 deschedule lists. */
    __cvmx_pow_display_list_and_walk(CVMX_POW_LIST_NOSCHED, dump, entry_list,
                            dump->sindexload[0][3].sindexload0_cn68xx.queue_val,
                            dump->sindexload[0][3].sindexload0_cn68xx.queue_one,
                            dump->sindexload[0][3].sindexload0_cn68xx.queue_head,
                            dump->sindexload[0][3].sindexload0_cn68xx.queue_tail);
    for (index=0; index<64; index++)
    {
        __cvmx_pow_display_list_and_walk(CVMX_POW_LIST_DESCHED + index, dump, entry_list,
                                dump->sindexload[index][2].sindexload0_cn68xx.queue_val,
                                dump->sindexload[index][2].sindexload0_cn68xx.queue_one,
                                dump->sindexload[index][2].sindexload0_cn68xx.queue_head,
                                dump->sindexload[index][2].sindexload0_cn68xx.queue_tail);
    }

    /* Print out the state of the 8 internal input queues */
    for (index=0; index<8; index++)
    {
        __cvmx_pow_display_list_and_walk(CVMX_POW_LIST_INPUT + index, dump, entry_list,
                                dump->sindexload[index][1].sindexload0_cn68xx.queue_val,
                                dump->sindexload[index][1].sindexload0_cn68xx.queue_one,
                                dump->sindexload[index][1].sindexload0_cn68xx.queue_head,
                                dump->sindexload[index][1].sindexload0_cn68xx.queue_tail);
    }

    /* Print out the state of the 16 memory queues */
    for (index=0; index<8; index++)
    {
        const char *name;
        if (dump->sindexload[index][1].sindexload0_cn68xx.queue_head)
            name = "Queue %da Memory (is head)";
        else
            name = "Queue %da Memory";
        __cvmx_pow_display_list(name, index,
                                dump->sindexload[index][1].sindexload0_cn68xx.queue_val,
                                dump->sindexload[index][1].sindexload0_cn68xx.queue_one,
                                dump->sindexload[index][1].sindexload0_cn68xx.queue_head,
                                dump->sindexload[index][1].sindexload0_cn68xx.queue_tail);
        if (dump->sindexload[index+8][1].sindexload0_cn68xx.queue_head)
            name = "Queue %db Memory (is head)";
        else
            name = "Queue %db Memory";
        __cvmx_pow_display_list(name, index,
                                dump->sindexload[index+8][1].sindexload0_cn68xx.queue_val,
                                dump->sindexload[index+8][1].sindexload0_cn68xx.queue_one,
                                dump->sindexload[index+8][1].sindexload0_cn68xx.queue_head,
                                dump->sindexload[index+8][1].sindexload0_cn68xx.queue_tail);
    }

    /* Print out each of the internal POW entries. Each entry has a tag, group,
       wqe, and possibly a next pointer. The next pointer is only valid if this
       entry isn't make as a tail */
    for (index=0; index<num_pow_entries; index++)
    {
        printf("Entry %d(%-10s): tag=%s,0x%08x grp=%d wqp=0x%016llx", index,
               __cvmx_pow_list_names[entry_list[index]],
               OCT_TAG_TYPE_STRING(dump->smemload[index][1].s_smemload0_cn68xx.tag_type),
               dump->smemload[index][1].s_smemload0_cn68xx.tag,
               dump->smemload[index][2].s_smemload1_cn68xx.grp,
               CAST64(dump->smemload[index][2].s_smemload1_cn68xx.wqp));
        if (dump->smemload[index][1].s_smemload0_cn68xx.tail)
            printf(" tail");
        else
            printf(" next=%d", dump->smemload[index][4].s_smemload3_cn68xx.fwd_index);
        if (entry_list[index] >= CVMX_POW_LIST_DESCHED)
        {
            printf(" prev=%d", dump->smemload[index][4].s_smemload3_cn68xx.fwd_index);
            printf(" nosched=%d", dump->smemload[index][1].s_smemload1_cn68xx.nosched);
            if (dump->smemload[index][3].s_smemload2_cn68xx.pend_switch)
            {
                printf(" pending tag=%s,0x%08x",
                       OCT_TAG_TYPE_STRING(dump->smemload[index][3].s_smemload2_cn68xx.pend_type),
                       dump->smemload[index][3].s_smemload2_cn68xx.pend_tag);
            }
        }
        printf("\n");
    }
}

/**
 * Dump a POW capture to the console in a human readable format.
 *
 * @param buffer POW capture from cvmx_pow_capture()
 * @param buffer_size
 *               Size of the buffer
 */
void cvmx_pow_display(void *buffer, int buffer_size)
{
    printf("POW Display Start\n");

    if (octeon_has_feature(OCTEON_FEATURE_PKND))
        __cvmx_pow_display_v2(buffer, buffer_size);
    else
        __cvmx_pow_display_v1(buffer, buffer_size);
        
    printf("POW Display End\n");
    return;
}

