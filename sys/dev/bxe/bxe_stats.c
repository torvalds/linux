/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2007-2014 QLogic Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "bxe.h"
#include "bxe_stats.h"

#ifdef __i386__
#define BITS_PER_LONG 32
#else
#define BITS_PER_LONG 64
#endif


static inline long
bxe_hilo(uint32_t *hiref)
{
    uint32_t lo = *(hiref + 1);
#if (BITS_PER_LONG == 64)
    uint32_t hi = *hiref;
    return (HILO_U64(hi, lo));
#else
    return (lo);
#endif
}

static inline uint16_t
bxe_get_port_stats_dma_len(struct bxe_softc *sc)
{
    uint16_t res = 0;
    uint32_t size;

    /* 'newest' convention - shmem2 contains the size of the port stats */
    if (SHMEM2_HAS(sc, sizeof_port_stats)) {
        size = SHMEM2_RD(sc, sizeof_port_stats);
        if (size) {
            res = size;
        }

        /* prevent newer BC from causing buffer overflow */
        if (res > sizeof(struct host_port_stats)) {
            res = sizeof(struct host_port_stats);
        }
    }

    /*
     * Older convention - all BCs support the port stats fields up until
     * the 'not_used' field
     */
    if (!res) {
        res = (offsetof(struct host_port_stats, not_used) + 4);

        /* if PFC stats are supported by the MFW, DMA them as well */
        if (sc->devinfo.bc_ver >= REQ_BC_VER_4_PFC_STATS_SUPPORTED) {
            res += (offsetof(struct host_port_stats, pfc_frames_rx_lo) -
                    offsetof(struct host_port_stats, pfc_frames_tx_hi) + 4);
        }
    }

    res >>= 2;

    DBASSERT(sc, !(res > 2 * DMAE_LEN32_RD_MAX), ("big stats dmae length\n"));
    return (res);
}

/*
 * Init service functions
 */

static void
bxe_dp_stats(struct bxe_softc *sc)
{
    int i;

    BLOGD(sc, DBG_STATS,
          "dumping stats:\n"
          "  fw_stats_req\n"
          "    hdr\n"
          "      cmd_num %d\n"
          "      reserved0 %d\n"
          "      drv_stats_counter %d\n"
          "      reserved1 %d\n"
          "      stats_counters_addrs %x %x\n",
          sc->fw_stats_req->hdr.cmd_num,
          sc->fw_stats_req->hdr.reserved0,
          sc->fw_stats_req->hdr.drv_stats_counter,
          sc->fw_stats_req->hdr.reserved1,
          sc->fw_stats_req->hdr.stats_counters_addrs.hi,
          sc->fw_stats_req->hdr.stats_counters_addrs.lo);

    for (i = 0; i < sc->fw_stats_req->hdr.cmd_num; i++) {
        BLOGD(sc, DBG_STATS,
              "query[%d]\n"
              "  kind %d\n"
              "  index %d\n"
              "  funcID %d\n"
              "  reserved %d\n"
              "  address %x %x\n",
              i,
              sc->fw_stats_req->query[i].kind,
              sc->fw_stats_req->query[i].index,
              sc->fw_stats_req->query[i].funcID,
              sc->fw_stats_req->query[i].reserved,
              sc->fw_stats_req->query[i].address.hi,
              sc->fw_stats_req->query[i].address.lo);
    }
}

/*
 * Post the next statistics ramrod. Protect it with the lock in
 * order to ensure the strict order between statistics ramrods
 * (each ramrod has a sequence number passed in a
 * sc->fw_stats_req->hdr.drv_stats_counter and ramrods must be
 * sent in order).
 */
static void
bxe_storm_stats_post(struct bxe_softc *sc)
{
    int rc;

    if (!sc->stats_pending) {
        BXE_STATS_LOCK(sc);

        if (sc->stats_pending) {
            BXE_STATS_UNLOCK(sc);
            return;
        }

        sc->fw_stats_req->hdr.drv_stats_counter =
            htole16(sc->stats_counter++);

        BLOGD(sc, DBG_STATS,
              "sending statistics ramrod %d\n",
              le16toh(sc->fw_stats_req->hdr.drv_stats_counter));

        /* adjust the ramrod to include VF queues statistics */
        // XXX bxe_iov_adjust_stats_req(sc);

        bxe_dp_stats(sc);

        /* send FW stats ramrod */
        rc = bxe_sp_post(sc, RAMROD_CMD_ID_COMMON_STAT_QUERY, 0,
                         U64_HI(sc->fw_stats_req_mapping),
                         U64_LO(sc->fw_stats_req_mapping),
                         NONE_CONNECTION_TYPE);
        if (rc == 0) {
            sc->stats_pending = 1;
        }

        BXE_STATS_UNLOCK(sc);
    }
}

static void
bxe_hw_stats_post(struct bxe_softc *sc)
{
    struct dmae_cmd *dmae = &sc->stats_dmae;
    uint32_t *stats_comp = BXE_SP(sc, stats_comp);
    int loader_idx;
    uint32_t opcode;

    *stats_comp = DMAE_COMP_VAL;
    if (CHIP_REV_IS_SLOW(sc)) {
        return;
    }

    /* Update MCP's statistics if possible */
    if (sc->func_stx) {
        memcpy(BXE_SP(sc, func_stats), &sc->func_stats,
               sizeof(sc->func_stats));
    }

    /* loader */
    if (sc->executer_idx) {
        loader_idx = PMF_DMAE_C(sc);
        opcode =  bxe_dmae_opcode(sc, DMAE_SRC_PCI, DMAE_DST_GRC,
                                  TRUE, DMAE_COMP_GRC);
        opcode = bxe_dmae_opcode_clr_src_reset(opcode);

        memset(dmae, 0, sizeof(struct dmae_cmd));
        dmae->opcode = opcode;
        dmae->src_addr_lo = U64_LO(BXE_SP_MAPPING(sc, dmae[0]));
        dmae->src_addr_hi = U64_HI(BXE_SP_MAPPING(sc, dmae[0]));
        dmae->dst_addr_lo = ((DMAE_REG_CMD_MEM +
                              sizeof(struct dmae_cmd) *
                              (loader_idx + 1)) >> 2);
        dmae->dst_addr_hi = 0;
        dmae->len = sizeof(struct dmae_cmd) >> 2;
        if (CHIP_IS_E1(sc)) {
            dmae->len--;
        }
        dmae->comp_addr_lo = (dmae_reg_go_c[loader_idx + 1] >> 2);
        dmae->comp_addr_hi = 0;
        dmae->comp_val = 1;

        *stats_comp = 0;
        bxe_post_dmae(sc, dmae, loader_idx);
    } else if (sc->func_stx) {
        *stats_comp = 0;
        bxe_post_dmae(sc, dmae, INIT_DMAE_C(sc));
    }
}

static int
bxe_stats_comp(struct bxe_softc *sc)
{
    uint32_t *stats_comp = BXE_SP(sc, stats_comp);
    int cnt = 10;

    while (*stats_comp != DMAE_COMP_VAL) {
        if (!cnt) {
            BLOGE(sc, "Timeout waiting for stats finished\n");
	    BXE_SET_ERROR_BIT(sc, BXE_ERR_STATS_TO);
            taskqueue_enqueue_timeout(taskqueue_thread,
                &sc->sp_err_timeout_task, hz/10);
            break;

        }

        cnt--;
        DELAY(1000);
    }

    return (1);
}

/*
 * Statistics service functions
 */

static void
bxe_stats_pmf_update(struct bxe_softc *sc)
{
    struct dmae_cmd *dmae;
    uint32_t opcode;
    int loader_idx = PMF_DMAE_C(sc);
    uint32_t *stats_comp = BXE_SP(sc, stats_comp);

    if (sc->devinfo.bc_ver <= 0x06001400) {
        /*
         * Bootcode v6.0.21 fixed a GRC timeout that occurs when accessing
         * BRB registers while the BRB block is in reset. The DMA transfer
         * below triggers this issue resulting in the DMAE to stop
         * functioning. Skip this initial stats transfer for old bootcode
         * versions <= 6.0.20.
         */
        return;
    }

    /* sanity */
    if (!sc->port.pmf || !sc->port.port_stx) {
        BLOGE(sc, "BUG!\n");
        return;
    }

    sc->executer_idx = 0;

    opcode = bxe_dmae_opcode(sc, DMAE_SRC_GRC, DMAE_DST_PCI, FALSE, 0);

    dmae = BXE_SP(sc, dmae[sc->executer_idx++]);
    dmae->opcode = bxe_dmae_opcode_add_comp(opcode, DMAE_COMP_GRC);
    dmae->src_addr_lo = (sc->port.port_stx >> 2);
    dmae->src_addr_hi = 0;
    dmae->dst_addr_lo = U64_LO(BXE_SP_MAPPING(sc, port_stats));
    dmae->dst_addr_hi = U64_HI(BXE_SP_MAPPING(sc, port_stats));
    dmae->len = DMAE_LEN32_RD_MAX;
    dmae->comp_addr_lo = (dmae_reg_go_c[loader_idx] >> 2);
    dmae->comp_addr_hi = 0;
    dmae->comp_val = 1;

    dmae = BXE_SP(sc, dmae[sc->executer_idx++]);
    dmae->opcode = bxe_dmae_opcode_add_comp(opcode, DMAE_COMP_PCI);
    dmae->src_addr_lo = ((sc->port.port_stx >> 2) + DMAE_LEN32_RD_MAX);
    dmae->src_addr_hi = 0;
    dmae->dst_addr_lo = U64_LO(BXE_SP_MAPPING(sc, port_stats) +
                               DMAE_LEN32_RD_MAX * 4);
    dmae->dst_addr_hi = U64_HI(BXE_SP_MAPPING(sc, port_stats) +
                               DMAE_LEN32_RD_MAX * 4);
    dmae->len = (bxe_get_port_stats_dma_len(sc) - DMAE_LEN32_RD_MAX);

    dmae->comp_addr_lo = U64_LO(BXE_SP_MAPPING(sc, stats_comp));
    dmae->comp_addr_hi = U64_HI(BXE_SP_MAPPING(sc, stats_comp));
    dmae->comp_val = DMAE_COMP_VAL;

    *stats_comp = 0;
    bxe_hw_stats_post(sc);
    bxe_stats_comp(sc);
}

static void
bxe_port_stats_init(struct bxe_softc *sc)
{
    struct dmae_cmd *dmae;
    int port = SC_PORT(sc);
    uint32_t opcode;
    int loader_idx = PMF_DMAE_C(sc);
    uint32_t mac_addr;
    uint32_t *stats_comp = BXE_SP(sc, stats_comp);

    /* sanity */
    if (!sc->link_vars.link_up || !sc->port.pmf) {
        BLOGE(sc, "BUG!\n");
        return;
    }

    sc->executer_idx = 0;

    /* MCP */
    opcode = bxe_dmae_opcode(sc, DMAE_SRC_PCI, DMAE_DST_GRC,
                             TRUE, DMAE_COMP_GRC);

    if (sc->port.port_stx) {
        dmae = BXE_SP(sc, dmae[sc->executer_idx++]);
        dmae->opcode = opcode;
        dmae->src_addr_lo = U64_LO(BXE_SP_MAPPING(sc, port_stats));
        dmae->src_addr_hi = U64_HI(BXE_SP_MAPPING(sc, port_stats));
        dmae->dst_addr_lo = sc->port.port_stx >> 2;
        dmae->dst_addr_hi = 0;
        dmae->len = bxe_get_port_stats_dma_len(sc);
        dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
        dmae->comp_addr_hi = 0;
        dmae->comp_val = 1;
    }

    if (sc->func_stx) {
        dmae = BXE_SP(sc, dmae[sc->executer_idx++]);
        dmae->opcode = opcode;
        dmae->src_addr_lo = U64_LO(BXE_SP_MAPPING(sc, func_stats));
        dmae->src_addr_hi = U64_HI(BXE_SP_MAPPING(sc, func_stats));
        dmae->dst_addr_lo = (sc->func_stx >> 2);
        dmae->dst_addr_hi = 0;
        dmae->len = (sizeof(struct host_func_stats) >> 2);
        dmae->comp_addr_lo = (dmae_reg_go_c[loader_idx] >> 2);
        dmae->comp_addr_hi = 0;
        dmae->comp_val = 1;
    }

    /* MAC */
    opcode = bxe_dmae_opcode(sc, DMAE_SRC_GRC, DMAE_DST_PCI,
                             TRUE, DMAE_COMP_GRC);

    /* EMAC is special */
    if (sc->link_vars.mac_type == ELINK_MAC_TYPE_EMAC) {
        mac_addr = (port ? GRCBASE_EMAC1 : GRCBASE_EMAC0);

        /* EMAC_REG_EMAC_RX_STAT_AC (EMAC_REG_EMAC_RX_STAT_AC_COUNT)*/
        dmae = BXE_SP(sc, dmae[sc->executer_idx++]);
        dmae->opcode = opcode;
        dmae->src_addr_lo = (mac_addr + EMAC_REG_EMAC_RX_STAT_AC) >> 2;
        dmae->src_addr_hi = 0;
        dmae->dst_addr_lo = U64_LO(BXE_SP_MAPPING(sc, mac_stats));
        dmae->dst_addr_hi = U64_HI(BXE_SP_MAPPING(sc, mac_stats));
        dmae->len = EMAC_REG_EMAC_RX_STAT_AC_COUNT;
        dmae->comp_addr_lo = (dmae_reg_go_c[loader_idx] >> 2);
        dmae->comp_addr_hi = 0;
        dmae->comp_val = 1;

        /* EMAC_REG_EMAC_RX_STAT_AC_28 */
        dmae = BXE_SP(sc, dmae[sc->executer_idx++]);
        dmae->opcode = opcode;
        dmae->src_addr_lo = ((mac_addr + EMAC_REG_EMAC_RX_STAT_AC_28) >> 2);
        dmae->src_addr_hi = 0;
        dmae->dst_addr_lo = U64_LO(BXE_SP_MAPPING(sc, mac_stats) +
                                   offsetof(struct emac_stats,
                                            rx_stat_falsecarriererrors));
        dmae->dst_addr_hi = U64_HI(BXE_SP_MAPPING(sc, mac_stats) +
                                   offsetof(struct emac_stats,
                                            rx_stat_falsecarriererrors));
        dmae->len = 1;
        dmae->comp_addr_lo = (dmae_reg_go_c[loader_idx] >> 2);
        dmae->comp_addr_hi = 0;
        dmae->comp_val = 1;

        /* EMAC_REG_EMAC_TX_STAT_AC (EMAC_REG_EMAC_TX_STAT_AC_COUNT)*/
        dmae = BXE_SP(sc, dmae[sc->executer_idx++]);
        dmae->opcode = opcode;
        dmae->src_addr_lo = ((mac_addr + EMAC_REG_EMAC_TX_STAT_AC) >> 2);
        dmae->src_addr_hi = 0;
        dmae->dst_addr_lo = U64_LO(BXE_SP_MAPPING(sc, mac_stats) +
                                   offsetof(struct emac_stats,
                                            tx_stat_ifhcoutoctets));
        dmae->dst_addr_hi = U64_HI(BXE_SP_MAPPING(sc, mac_stats) +
                                   offsetof(struct emac_stats,
                                            tx_stat_ifhcoutoctets));
        dmae->len = EMAC_REG_EMAC_TX_STAT_AC_COUNT;
        dmae->comp_addr_lo = (dmae_reg_go_c[loader_idx] >> 2);
        dmae->comp_addr_hi = 0;
        dmae->comp_val = 1;
    } else {
        uint32_t tx_src_addr_lo, rx_src_addr_lo;
        uint16_t rx_len, tx_len;

        /* configure the params according to MAC type */
        switch (sc->link_vars.mac_type) {
        case ELINK_MAC_TYPE_BMAC:
            mac_addr = (port) ? NIG_REG_INGRESS_BMAC1_MEM :
                                NIG_REG_INGRESS_BMAC0_MEM;

            /* BIGMAC_REGISTER_TX_STAT_GTPKT ..
               BIGMAC_REGISTER_TX_STAT_GTBYT */
            if (CHIP_IS_E1x(sc)) {
                tx_src_addr_lo =
                    ((mac_addr + BIGMAC_REGISTER_TX_STAT_GTPKT) >> 2);
                tx_len = ((8 + BIGMAC_REGISTER_TX_STAT_GTBYT -
                           BIGMAC_REGISTER_TX_STAT_GTPKT) >> 2);
                rx_src_addr_lo =
                    ((mac_addr + BIGMAC_REGISTER_RX_STAT_GR64) >> 2);
                rx_len = ((8 + BIGMAC_REGISTER_RX_STAT_GRIPJ -
                           BIGMAC_REGISTER_RX_STAT_GR64) >> 2);
            } else {
                tx_src_addr_lo =
                    ((mac_addr + BIGMAC2_REGISTER_TX_STAT_GTPOK) >> 2);
                tx_len = ((8 + BIGMAC2_REGISTER_TX_STAT_GTBYT -
                           BIGMAC2_REGISTER_TX_STAT_GTPOK) >> 2);
                rx_src_addr_lo =
                    ((mac_addr + BIGMAC2_REGISTER_RX_STAT_GR64) >> 2);
                rx_len = ((8 + BIGMAC2_REGISTER_RX_STAT_GRIPJ -
                           BIGMAC2_REGISTER_RX_STAT_GR64) >> 2);
            }

            break;

        case ELINK_MAC_TYPE_UMAC: /* handled by MSTAT */
        case ELINK_MAC_TYPE_XMAC: /* handled by MSTAT */
        default:
            mac_addr = (port) ? GRCBASE_MSTAT1 : GRCBASE_MSTAT0;
            tx_src_addr_lo = ((mac_addr + MSTAT_REG_TX_STAT_GTXPOK_LO) >> 2);
            rx_src_addr_lo = ((mac_addr + MSTAT_REG_RX_STAT_GR64_LO) >> 2);
            tx_len =
                (sizeof(sc->sp->mac_stats.mstat_stats.stats_tx) >> 2);
            rx_len =
                (sizeof(sc->sp->mac_stats.mstat_stats.stats_rx) >> 2);
            break;
        }

        /* TX stats */
        dmae = BXE_SP(sc, dmae[sc->executer_idx++]);
        dmae->opcode = opcode;
        dmae->src_addr_lo = tx_src_addr_lo;
        dmae->src_addr_hi = 0;
        dmae->len = tx_len;
        dmae->dst_addr_lo = U64_LO(BXE_SP_MAPPING(sc, mac_stats));
        dmae->dst_addr_hi = U64_HI(BXE_SP_MAPPING(sc, mac_stats));
        dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
        dmae->comp_addr_hi = 0;
        dmae->comp_val = 1;

        /* RX stats */
        dmae = BXE_SP(sc, dmae[sc->executer_idx++]);
        dmae->opcode = opcode;
        dmae->src_addr_hi = 0;
        dmae->src_addr_lo = rx_src_addr_lo;
        dmae->dst_addr_lo =
            U64_LO(BXE_SP_MAPPING(sc, mac_stats) + (tx_len << 2));
        dmae->dst_addr_hi =
            U64_HI(BXE_SP_MAPPING(sc, mac_stats) + (tx_len << 2));
        dmae->len = rx_len;
        dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
        dmae->comp_addr_hi = 0;
        dmae->comp_val = 1;
    }

    /* NIG */
    if (!CHIP_IS_E3(sc)) {
        dmae = BXE_SP(sc, dmae[sc->executer_idx++]);
        dmae->opcode = opcode;
        dmae->src_addr_lo =
            (port ? NIG_REG_STAT1_EGRESS_MAC_PKT0 :
                    NIG_REG_STAT0_EGRESS_MAC_PKT0) >> 2;
        dmae->src_addr_hi = 0;
        dmae->dst_addr_lo = U64_LO(BXE_SP_MAPPING(sc, nig_stats) +
                                   offsetof(struct nig_stats,
                                            egress_mac_pkt0_lo));
        dmae->dst_addr_hi = U64_HI(BXE_SP_MAPPING(sc, nig_stats) +
                                   offsetof(struct nig_stats,
                                            egress_mac_pkt0_lo));
        dmae->len = ((2 * sizeof(uint32_t)) >> 2);
        dmae->comp_addr_lo = (dmae_reg_go_c[loader_idx] >> 2);
        dmae->comp_addr_hi = 0;
        dmae->comp_val = 1;

        dmae = BXE_SP(sc, dmae[sc->executer_idx++]);
        dmae->opcode = opcode;
        dmae->src_addr_lo =
            (port ? NIG_REG_STAT1_EGRESS_MAC_PKT1 :
                    NIG_REG_STAT0_EGRESS_MAC_PKT1) >> 2;
        dmae->src_addr_hi = 0;
        dmae->dst_addr_lo = U64_LO(BXE_SP_MAPPING(sc, nig_stats) +
                                   offsetof(struct nig_stats,
                                            egress_mac_pkt1_lo));
        dmae->dst_addr_hi = U64_HI(BXE_SP_MAPPING(sc, nig_stats) +
                                   offsetof(struct nig_stats,
                                            egress_mac_pkt1_lo));
        dmae->len = ((2 * sizeof(uint32_t)) >> 2);
        dmae->comp_addr_lo = (dmae_reg_go_c[loader_idx] >> 2);
        dmae->comp_addr_hi = 0;
        dmae->comp_val = 1;
    }

    dmae = BXE_SP(sc, dmae[sc->executer_idx++]);
    dmae->opcode = bxe_dmae_opcode(sc, DMAE_SRC_GRC, DMAE_DST_PCI,
                                   TRUE, DMAE_COMP_PCI);
    dmae->src_addr_lo =
        (port ? NIG_REG_STAT1_BRB_DISCARD :
                NIG_REG_STAT0_BRB_DISCARD) >> 2;
    dmae->src_addr_hi = 0;
    dmae->dst_addr_lo = U64_LO(BXE_SP_MAPPING(sc, nig_stats));
    dmae->dst_addr_hi = U64_HI(BXE_SP_MAPPING(sc, nig_stats));
    dmae->len = (sizeof(struct nig_stats) - 4*sizeof(uint32_t)) >> 2;

    dmae->comp_addr_lo = U64_LO(BXE_SP_MAPPING(sc, stats_comp));
    dmae->comp_addr_hi = U64_HI(BXE_SP_MAPPING(sc, stats_comp));
    dmae->comp_val = DMAE_COMP_VAL;

    *stats_comp = 0;
}

static void
bxe_func_stats_init(struct bxe_softc *sc)
{
    struct dmae_cmd *dmae = &sc->stats_dmae;
    uint32_t *stats_comp = BXE_SP(sc, stats_comp);

    /* sanity */
    if (!sc->func_stx) {
        BLOGE(sc, "BUG!\n");
        return;
    }

    sc->executer_idx = 0;
    memset(dmae, 0, sizeof(struct dmae_cmd));

    dmae->opcode = bxe_dmae_opcode(sc, DMAE_SRC_PCI, DMAE_DST_GRC,
                                   TRUE, DMAE_COMP_PCI);
    dmae->src_addr_lo = U64_LO(BXE_SP_MAPPING(sc, func_stats));
    dmae->src_addr_hi = U64_HI(BXE_SP_MAPPING(sc, func_stats));
    dmae->dst_addr_lo = (sc->func_stx >> 2);
    dmae->dst_addr_hi = 0;
    dmae->len = (sizeof(struct host_func_stats) >> 2);
    dmae->comp_addr_lo = U64_LO(BXE_SP_MAPPING(sc, stats_comp));
    dmae->comp_addr_hi = U64_HI(BXE_SP_MAPPING(sc, stats_comp));
    dmae->comp_val = DMAE_COMP_VAL;

    *stats_comp = 0;
}

static void
bxe_stats_start(struct bxe_softc *sc)
{
    /*
     * VFs travel through here as part of the statistics FSM, but no action
     * is required
     */
    if (IS_VF(sc)) {
        return;
    }

    if (sc->port.pmf) {
        bxe_port_stats_init(sc);
    }

    else if (sc->func_stx) {
        bxe_func_stats_init(sc);
    }

    bxe_hw_stats_post(sc);
    bxe_storm_stats_post(sc);
}

static void
bxe_stats_pmf_start(struct bxe_softc *sc)
{
    bxe_stats_comp(sc);
    bxe_stats_pmf_update(sc);
    bxe_stats_start(sc);
}

static void
bxe_stats_restart(struct bxe_softc *sc)
{
    /*
     * VFs travel through here as part of the statistics FSM, but no action
     * is required
     */
    if (IS_VF(sc)) {
        return;
    }

    bxe_stats_comp(sc);
    bxe_stats_start(sc);
}

static void
bxe_bmac_stats_update(struct bxe_softc *sc)
{
    struct host_port_stats *pstats = BXE_SP(sc, port_stats);
    struct bxe_eth_stats *estats = &sc->eth_stats;
    struct {
        uint32_t lo;
        uint32_t hi;
    } diff;

    if (CHIP_IS_E1x(sc)) {
        struct bmac1_stats *new = BXE_SP(sc, mac_stats.bmac1_stats);

        /* the macros below will use "bmac1_stats" type */
        UPDATE_STAT64(rx_stat_grerb, rx_stat_ifhcinbadoctets);
        UPDATE_STAT64(rx_stat_grfcs, rx_stat_dot3statsfcserrors);
        UPDATE_STAT64(rx_stat_grund, rx_stat_etherstatsundersizepkts);
        UPDATE_STAT64(rx_stat_grovr, rx_stat_dot3statsframestoolong);
        UPDATE_STAT64(rx_stat_grfrg, rx_stat_etherstatsfragments);
        UPDATE_STAT64(rx_stat_grjbr, rx_stat_etherstatsjabbers);
        UPDATE_STAT64(rx_stat_grxcf, rx_stat_maccontrolframesreceived);
        UPDATE_STAT64(rx_stat_grxpf, rx_stat_xoffstateentered);
        UPDATE_STAT64(rx_stat_grxpf, rx_stat_mac_xpf);

        UPDATE_STAT64(tx_stat_gtxpf, tx_stat_outxoffsent);
        UPDATE_STAT64(tx_stat_gtxpf, tx_stat_flowcontroldone);
        UPDATE_STAT64(tx_stat_gt64, tx_stat_etherstatspkts64octets);
        UPDATE_STAT64(tx_stat_gt127,
                      tx_stat_etherstatspkts65octetsto127octets);
        UPDATE_STAT64(tx_stat_gt255,
                      tx_stat_etherstatspkts128octetsto255octets);
        UPDATE_STAT64(tx_stat_gt511,
                      tx_stat_etherstatspkts256octetsto511octets);
        UPDATE_STAT64(tx_stat_gt1023,
                      tx_stat_etherstatspkts512octetsto1023octets);
        UPDATE_STAT64(tx_stat_gt1518,
                      tx_stat_etherstatspkts1024octetsto1522octets);
        UPDATE_STAT64(tx_stat_gt2047, tx_stat_mac_2047);
        UPDATE_STAT64(tx_stat_gt4095, tx_stat_mac_4095);
        UPDATE_STAT64(tx_stat_gt9216, tx_stat_mac_9216);
        UPDATE_STAT64(tx_stat_gt16383, tx_stat_mac_16383);
        UPDATE_STAT64(tx_stat_gterr,
                      tx_stat_dot3statsinternalmactransmiterrors);
        UPDATE_STAT64(tx_stat_gtufl, tx_stat_mac_ufl);
    } else {
        struct bmac2_stats *new = BXE_SP(sc, mac_stats.bmac2_stats);
        struct bxe_fw_port_stats_old *fwstats = &sc->fw_stats_old;

        /* the macros below will use "bmac2_stats" type */
        UPDATE_STAT64(rx_stat_grerb, rx_stat_ifhcinbadoctets);
        UPDATE_STAT64(rx_stat_grfcs, rx_stat_dot3statsfcserrors);
        UPDATE_STAT64(rx_stat_grund, rx_stat_etherstatsundersizepkts);
        UPDATE_STAT64(rx_stat_grovr, rx_stat_dot3statsframestoolong);
        UPDATE_STAT64(rx_stat_grfrg, rx_stat_etherstatsfragments);
        UPDATE_STAT64(rx_stat_grjbr, rx_stat_etherstatsjabbers);
        UPDATE_STAT64(rx_stat_grxcf, rx_stat_maccontrolframesreceived);
        UPDATE_STAT64(rx_stat_grxpf, rx_stat_xoffstateentered);
        UPDATE_STAT64(rx_stat_grxpf, rx_stat_mac_xpf);
        UPDATE_STAT64(tx_stat_gtxpf, tx_stat_outxoffsent);
        UPDATE_STAT64(tx_stat_gtxpf, tx_stat_flowcontroldone);
        UPDATE_STAT64(tx_stat_gt64, tx_stat_etherstatspkts64octets);
        UPDATE_STAT64(tx_stat_gt127,
                      tx_stat_etherstatspkts65octetsto127octets);
        UPDATE_STAT64(tx_stat_gt255,
                      tx_stat_etherstatspkts128octetsto255octets);
        UPDATE_STAT64(tx_stat_gt511,
                      tx_stat_etherstatspkts256octetsto511octets);
        UPDATE_STAT64(tx_stat_gt1023,
                      tx_stat_etherstatspkts512octetsto1023octets);
        UPDATE_STAT64(tx_stat_gt1518,
                      tx_stat_etherstatspkts1024octetsto1522octets);
        UPDATE_STAT64(tx_stat_gt2047, tx_stat_mac_2047);
        UPDATE_STAT64(tx_stat_gt4095, tx_stat_mac_4095);
        UPDATE_STAT64(tx_stat_gt9216, tx_stat_mac_9216);
        UPDATE_STAT64(tx_stat_gt16383, tx_stat_mac_16383);
        UPDATE_STAT64(tx_stat_gterr,
                      tx_stat_dot3statsinternalmactransmiterrors);
        UPDATE_STAT64(tx_stat_gtufl, tx_stat_mac_ufl);

        /* collect PFC stats */
        pstats->pfc_frames_tx_hi = new->tx_stat_gtpp_hi;
        pstats->pfc_frames_tx_lo = new->tx_stat_gtpp_lo;
        ADD_64(pstats->pfc_frames_tx_hi, fwstats->pfc_frames_tx_hi,
               pstats->pfc_frames_tx_lo, fwstats->pfc_frames_tx_lo);

        pstats->pfc_frames_rx_hi = new->rx_stat_grpp_hi;
        pstats->pfc_frames_rx_lo = new->rx_stat_grpp_lo;
        ADD_64(pstats->pfc_frames_rx_hi, fwstats->pfc_frames_rx_hi,
               pstats->pfc_frames_rx_lo, fwstats->pfc_frames_rx_lo);
    }

    estats->pause_frames_received_hi = pstats->mac_stx[1].rx_stat_mac_xpf_hi;
    estats->pause_frames_received_lo = pstats->mac_stx[1].rx_stat_mac_xpf_lo;

    estats->pause_frames_sent_hi = pstats->mac_stx[1].tx_stat_outxoffsent_hi;
    estats->pause_frames_sent_lo = pstats->mac_stx[1].tx_stat_outxoffsent_lo;

    estats->pfc_frames_received_hi = pstats->pfc_frames_rx_hi;
    estats->pfc_frames_received_lo = pstats->pfc_frames_rx_lo;
    estats->pfc_frames_sent_hi = pstats->pfc_frames_tx_hi;
    estats->pfc_frames_sent_lo = pstats->pfc_frames_tx_lo;
}

static void
bxe_mstat_stats_update(struct bxe_softc *sc)
{
    struct host_port_stats *pstats = BXE_SP(sc, port_stats);
    struct bxe_eth_stats *estats = &sc->eth_stats;
    struct mstat_stats *new = BXE_SP(sc, mac_stats.mstat_stats);

    ADD_STAT64(stats_rx.rx_grerb, rx_stat_ifhcinbadoctets);
    ADD_STAT64(stats_rx.rx_grfcs, rx_stat_dot3statsfcserrors);
    ADD_STAT64(stats_rx.rx_grund, rx_stat_etherstatsundersizepkts);
    ADD_STAT64(stats_rx.rx_grovr, rx_stat_dot3statsframestoolong);
    ADD_STAT64(stats_rx.rx_grfrg, rx_stat_etherstatsfragments);
    ADD_STAT64(stats_rx.rx_grxcf, rx_stat_maccontrolframesreceived);
    ADD_STAT64(stats_rx.rx_grxpf, rx_stat_xoffstateentered);
    ADD_STAT64(stats_rx.rx_grxpf, rx_stat_mac_xpf);
    ADD_STAT64(stats_tx.tx_gtxpf, tx_stat_outxoffsent);
    ADD_STAT64(stats_tx.tx_gtxpf, tx_stat_flowcontroldone);

    /* collect pfc stats */
    ADD_64(pstats->pfc_frames_tx_hi, new->stats_tx.tx_gtxpp_hi,
           pstats->pfc_frames_tx_lo, new->stats_tx.tx_gtxpp_lo);
    ADD_64(pstats->pfc_frames_rx_hi, new->stats_rx.rx_grxpp_hi,
           pstats->pfc_frames_rx_lo, new->stats_rx.rx_grxpp_lo);

    ADD_STAT64(stats_tx.tx_gt64, tx_stat_etherstatspkts64octets);
    ADD_STAT64(stats_tx.tx_gt127, tx_stat_etherstatspkts65octetsto127octets);
    ADD_STAT64(stats_tx.tx_gt255, tx_stat_etherstatspkts128octetsto255octets);
    ADD_STAT64(stats_tx.tx_gt511, tx_stat_etherstatspkts256octetsto511octets);
    ADD_STAT64(stats_tx.tx_gt1023,
               tx_stat_etherstatspkts512octetsto1023octets);
    ADD_STAT64(stats_tx.tx_gt1518,
               tx_stat_etherstatspkts1024octetsto1522octets);
    ADD_STAT64(stats_tx.tx_gt2047, tx_stat_mac_2047);

    ADD_STAT64(stats_tx.tx_gt4095, tx_stat_mac_4095);
    ADD_STAT64(stats_tx.tx_gt9216, tx_stat_mac_9216);
    ADD_STAT64(stats_tx.tx_gt16383, tx_stat_mac_16383);

    ADD_STAT64(stats_tx.tx_gterr, tx_stat_dot3statsinternalmactransmiterrors);
    ADD_STAT64(stats_tx.tx_gtufl, tx_stat_mac_ufl);

    estats->etherstatspkts1024octetsto1522octets_hi =
        pstats->mac_stx[1].tx_stat_etherstatspkts1024octetsto1522octets_hi;
    estats->etherstatspkts1024octetsto1522octets_lo =
        pstats->mac_stx[1].tx_stat_etherstatspkts1024octetsto1522octets_lo;

    estats->etherstatspktsover1522octets_hi =
        pstats->mac_stx[1].tx_stat_mac_2047_hi;
    estats->etherstatspktsover1522octets_lo =
        pstats->mac_stx[1].tx_stat_mac_2047_lo;

    ADD_64(estats->etherstatspktsover1522octets_hi,
           pstats->mac_stx[1].tx_stat_mac_4095_hi,
           estats->etherstatspktsover1522octets_lo,
           pstats->mac_stx[1].tx_stat_mac_4095_lo);

    ADD_64(estats->etherstatspktsover1522octets_hi,
           pstats->mac_stx[1].tx_stat_mac_9216_hi,
           estats->etherstatspktsover1522octets_lo,
           pstats->mac_stx[1].tx_stat_mac_9216_lo);

    ADD_64(estats->etherstatspktsover1522octets_hi,
           pstats->mac_stx[1].tx_stat_mac_16383_hi,
           estats->etherstatspktsover1522octets_lo,
           pstats->mac_stx[1].tx_stat_mac_16383_lo);

    estats->pause_frames_received_hi = pstats->mac_stx[1].rx_stat_mac_xpf_hi;
    estats->pause_frames_received_lo = pstats->mac_stx[1].rx_stat_mac_xpf_lo;

    estats->pause_frames_sent_hi = pstats->mac_stx[1].tx_stat_outxoffsent_hi;
    estats->pause_frames_sent_lo = pstats->mac_stx[1].tx_stat_outxoffsent_lo;

    estats->pfc_frames_received_hi = pstats->pfc_frames_rx_hi;
    estats->pfc_frames_received_lo = pstats->pfc_frames_rx_lo;
    estats->pfc_frames_sent_hi = pstats->pfc_frames_tx_hi;
    estats->pfc_frames_sent_lo = pstats->pfc_frames_tx_lo;
}

static void
bxe_emac_stats_update(struct bxe_softc *sc)
{
    struct emac_stats *new = BXE_SP(sc, mac_stats.emac_stats);
    struct host_port_stats *pstats = BXE_SP(sc, port_stats);
    struct bxe_eth_stats *estats = &sc->eth_stats;

    UPDATE_EXTEND_STAT(rx_stat_ifhcinbadoctets);
    UPDATE_EXTEND_STAT(tx_stat_ifhcoutbadoctets);
    UPDATE_EXTEND_STAT(rx_stat_dot3statsfcserrors);
    UPDATE_EXTEND_STAT(rx_stat_dot3statsalignmenterrors);
    UPDATE_EXTEND_STAT(rx_stat_dot3statscarriersenseerrors);
    UPDATE_EXTEND_STAT(rx_stat_falsecarriererrors);
    UPDATE_EXTEND_STAT(rx_stat_etherstatsundersizepkts);
    UPDATE_EXTEND_STAT(rx_stat_dot3statsframestoolong);
    UPDATE_EXTEND_STAT(rx_stat_etherstatsfragments);
    UPDATE_EXTEND_STAT(rx_stat_etherstatsjabbers);
    UPDATE_EXTEND_STAT(rx_stat_maccontrolframesreceived);
    UPDATE_EXTEND_STAT(rx_stat_xoffstateentered);
    UPDATE_EXTEND_STAT(rx_stat_xonpauseframesreceived);
    UPDATE_EXTEND_STAT(rx_stat_xoffpauseframesreceived);
    UPDATE_EXTEND_STAT(tx_stat_outxonsent);
    UPDATE_EXTEND_STAT(tx_stat_outxoffsent);
    UPDATE_EXTEND_STAT(tx_stat_flowcontroldone);
    UPDATE_EXTEND_STAT(tx_stat_etherstatscollisions);
    UPDATE_EXTEND_STAT(tx_stat_dot3statssinglecollisionframes);
    UPDATE_EXTEND_STAT(tx_stat_dot3statsmultiplecollisionframes);
    UPDATE_EXTEND_STAT(tx_stat_dot3statsdeferredtransmissions);
    UPDATE_EXTEND_STAT(tx_stat_dot3statsexcessivecollisions);
    UPDATE_EXTEND_STAT(tx_stat_dot3statslatecollisions);
    UPDATE_EXTEND_STAT(tx_stat_etherstatspkts64octets);
    UPDATE_EXTEND_STAT(tx_stat_etherstatspkts65octetsto127octets);
    UPDATE_EXTEND_STAT(tx_stat_etherstatspkts128octetsto255octets);
    UPDATE_EXTEND_STAT(tx_stat_etherstatspkts256octetsto511octets);
    UPDATE_EXTEND_STAT(tx_stat_etherstatspkts512octetsto1023octets);
    UPDATE_EXTEND_STAT(tx_stat_etherstatspkts1024octetsto1522octets);
    UPDATE_EXTEND_STAT(tx_stat_etherstatspktsover1522octets);
    UPDATE_EXTEND_STAT(tx_stat_dot3statsinternalmactransmiterrors);

    estats->pause_frames_received_hi =
        pstats->mac_stx[1].rx_stat_xonpauseframesreceived_hi;
    estats->pause_frames_received_lo =
        pstats->mac_stx[1].rx_stat_xonpauseframesreceived_lo;
    ADD_64(estats->pause_frames_received_hi,
           pstats->mac_stx[1].rx_stat_xoffpauseframesreceived_hi,
           estats->pause_frames_received_lo,
           pstats->mac_stx[1].rx_stat_xoffpauseframesreceived_lo);

    estats->pause_frames_sent_hi =
        pstats->mac_stx[1].tx_stat_outxonsent_hi;
    estats->pause_frames_sent_lo =
        pstats->mac_stx[1].tx_stat_outxonsent_lo;
    ADD_64(estats->pause_frames_sent_hi,
           pstats->mac_stx[1].tx_stat_outxoffsent_hi,
           estats->pause_frames_sent_lo,
           pstats->mac_stx[1].tx_stat_outxoffsent_lo);
}

static int
bxe_hw_stats_update(struct bxe_softc *sc)
{
    struct nig_stats *new = BXE_SP(sc, nig_stats);
    struct nig_stats *old = &(sc->port.old_nig_stats);
    struct host_port_stats *pstats = BXE_SP(sc, port_stats);
    struct bxe_eth_stats *estats = &sc->eth_stats;
    uint32_t lpi_reg, nig_timer_max;
    struct {
        uint32_t lo;
        uint32_t hi;
    } diff;

    switch (sc->link_vars.mac_type) {
    case ELINK_MAC_TYPE_BMAC:
        bxe_bmac_stats_update(sc);
        break;

    case ELINK_MAC_TYPE_EMAC:
        bxe_emac_stats_update(sc);
        break;

    case ELINK_MAC_TYPE_UMAC:
    case ELINK_MAC_TYPE_XMAC:
        bxe_mstat_stats_update(sc);
        break;

    case ELINK_MAC_TYPE_NONE: /* unreached */
        BLOGD(sc, DBG_STATS,
              "stats updated by DMAE but no MAC active\n");
        return (-1);

    default: /* unreached */
        BLOGE(sc, "stats update failed, unknown MAC type\n");
    }

    ADD_EXTEND_64(pstats->brb_drop_hi, pstats->brb_drop_lo,
                  new->brb_discard - old->brb_discard);
    ADD_EXTEND_64(estats->brb_truncate_hi, estats->brb_truncate_lo,
                  new->brb_truncate - old->brb_truncate);

    if (!CHIP_IS_E3(sc)) {
        UPDATE_STAT64_NIG(egress_mac_pkt0,
                          etherstatspkts1024octetsto1522octets);
        UPDATE_STAT64_NIG(egress_mac_pkt1,
                          etherstatspktsover1522octets);
    }

    memcpy(old, new, sizeof(struct nig_stats));

    memcpy(&(estats->rx_stat_ifhcinbadoctets_hi), &(pstats->mac_stx[1]),
           sizeof(struct mac_stx));
    estats->brb_drop_hi = pstats->brb_drop_hi;
    estats->brb_drop_lo = pstats->brb_drop_lo;

    pstats->host_port_stats_counter++;

    if (CHIP_IS_E3(sc)) {
        lpi_reg = (SC_PORT(sc)) ?
                      MISC_REG_CPMU_LP_SM_ENT_CNT_P1 :
                      MISC_REG_CPMU_LP_SM_ENT_CNT_P0;
        estats->eee_tx_lpi += REG_RD(sc, lpi_reg);
    }

    if (!BXE_NOMCP(sc)) {
        nig_timer_max = SHMEM_RD(sc, port_mb[SC_PORT(sc)].stat_nig_timer);
        if (nig_timer_max != estats->nig_timer_max) {
            estats->nig_timer_max = nig_timer_max;
	    /*NOTE: not setting error bit */
            BLOGE(sc, "invalid NIG timer max (%u)\n",
                  estats->nig_timer_max);
        }
    }

    return (0);
}

static int
bxe_storm_stats_validate_counters(struct bxe_softc *sc)
{
    struct stats_counter *counters = &sc->fw_stats_data->storm_counters;
    uint16_t cur_stats_counter;

    /*
     * Make sure we use the value of the counter
     * used for sending the last stats ramrod.
     */
    BXE_STATS_LOCK(sc);
    cur_stats_counter = (sc->stats_counter - 1);
    BXE_STATS_UNLOCK(sc);

    /* are storm stats valid? */
    if (le16toh(counters->xstats_counter) != cur_stats_counter) {
        BLOGD(sc, DBG_STATS,
              "stats not updated by xstorm, "
              "counter 0x%x != stats_counter 0x%x\n",
              le16toh(counters->xstats_counter), sc->stats_counter);
        return (-EAGAIN);
    }

    if (le16toh(counters->ustats_counter) != cur_stats_counter) {
        BLOGD(sc, DBG_STATS,
              "stats not updated by ustorm, "
              "counter 0x%x != stats_counter 0x%x\n",
              le16toh(counters->ustats_counter), sc->stats_counter);
        return (-EAGAIN);
    }

    if (le16toh(counters->cstats_counter) != cur_stats_counter) {
        BLOGD(sc, DBG_STATS,
              "stats not updated by cstorm, "
              "counter 0x%x != stats_counter 0x%x\n",
              le16toh(counters->cstats_counter), sc->stats_counter);
        return (-EAGAIN);
    }

    if (le16toh(counters->tstats_counter) != cur_stats_counter) {
        BLOGD(sc, DBG_STATS,
              "stats not updated by tstorm, "
              "counter 0x%x != stats_counter 0x%x\n",
              le16toh(counters->tstats_counter), sc->stats_counter);
        return (-EAGAIN);
    }

    return (0);
}

static int
bxe_storm_stats_update(struct bxe_softc *sc)
{
    struct tstorm_per_port_stats *tport =
        &sc->fw_stats_data->port.tstorm_port_statistics;
    struct tstorm_per_pf_stats *tfunc =
        &sc->fw_stats_data->pf.tstorm_pf_statistics;
    struct host_func_stats *fstats = &sc->func_stats;
    struct bxe_eth_stats *estats = &sc->eth_stats;
    struct bxe_eth_stats_old *estats_old = &sc->eth_stats_old;
    int i;

    /* vfs stat counter is managed by pf */
    if (IS_PF(sc) && bxe_storm_stats_validate_counters(sc)) {
        return (-EAGAIN);
    }

    estats->error_bytes_received_hi = 0;
    estats->error_bytes_received_lo = 0;

    for (i = 0; i < sc->num_queues; i++) {
        struct bxe_fastpath *fp = &sc->fp[i];
        struct tstorm_per_queue_stats *tclient =
            &sc->fw_stats_data->queue_stats[i].tstorm_queue_statistics;
        struct tstorm_per_queue_stats *old_tclient = &fp->old_tclient;
        struct ustorm_per_queue_stats *uclient =
            &sc->fw_stats_data->queue_stats[i].ustorm_queue_statistics;
        struct ustorm_per_queue_stats *old_uclient = &fp->old_uclient;
        struct xstorm_per_queue_stats *xclient =
            &sc->fw_stats_data->queue_stats[i].xstorm_queue_statistics;
        struct xstorm_per_queue_stats *old_xclient = &fp->old_xclient;
        struct bxe_eth_q_stats *qstats = &fp->eth_q_stats;
        struct bxe_eth_q_stats_old *qstats_old = &fp->eth_q_stats_old;

        uint32_t diff;

        BLOGD(sc, DBG_STATS,
              "queue[%d]: ucast_sent 0x%x bcast_sent 0x%x mcast_sent 0x%x\n",
              i, xclient->ucast_pkts_sent, xclient->bcast_pkts_sent,
              xclient->mcast_pkts_sent);

        BLOGD(sc, DBG_STATS, "---------------\n");

        UPDATE_QSTAT(tclient->rcv_bcast_bytes,
                     total_broadcast_bytes_received);
        UPDATE_QSTAT(tclient->rcv_mcast_bytes,
                     total_multicast_bytes_received);
        UPDATE_QSTAT(tclient->rcv_ucast_bytes,
                     total_unicast_bytes_received);

        /*
         * sum to total_bytes_received all
         * unicast/multicast/broadcast
         */
        qstats->total_bytes_received_hi =
            qstats->total_broadcast_bytes_received_hi;
        qstats->total_bytes_received_lo =
            qstats->total_broadcast_bytes_received_lo;

        ADD_64(qstats->total_bytes_received_hi,
               qstats->total_multicast_bytes_received_hi,
               qstats->total_bytes_received_lo,
               qstats->total_multicast_bytes_received_lo);

        ADD_64(qstats->total_bytes_received_hi,
               qstats->total_unicast_bytes_received_hi,
               qstats->total_bytes_received_lo,
               qstats->total_unicast_bytes_received_lo);

        qstats->valid_bytes_received_hi = qstats->total_bytes_received_hi;
        qstats->valid_bytes_received_lo = qstats->total_bytes_received_lo;

        UPDATE_EXTEND_TSTAT(rcv_ucast_pkts, total_unicast_packets_received);
        UPDATE_EXTEND_TSTAT(rcv_mcast_pkts, total_multicast_packets_received);
        UPDATE_EXTEND_TSTAT(rcv_bcast_pkts, total_broadcast_packets_received);
        UPDATE_EXTEND_E_TSTAT(pkts_too_big_discard,
                              etherstatsoverrsizepkts, 32);
        UPDATE_EXTEND_E_TSTAT(no_buff_discard, no_buff_discard, 16);

        SUB_EXTEND_USTAT(ucast_no_buff_pkts, total_unicast_packets_received);
        SUB_EXTEND_USTAT(mcast_no_buff_pkts,
                         total_multicast_packets_received);
        SUB_EXTEND_USTAT(bcast_no_buff_pkts,
                         total_broadcast_packets_received);
        UPDATE_EXTEND_E_USTAT(ucast_no_buff_pkts, no_buff_discard);
        UPDATE_EXTEND_E_USTAT(mcast_no_buff_pkts, no_buff_discard);
        UPDATE_EXTEND_E_USTAT(bcast_no_buff_pkts, no_buff_discard);

        UPDATE_QSTAT(xclient->bcast_bytes_sent,
                     total_broadcast_bytes_transmitted);
        UPDATE_QSTAT(xclient->mcast_bytes_sent,
                     total_multicast_bytes_transmitted);
        UPDATE_QSTAT(xclient->ucast_bytes_sent,
                     total_unicast_bytes_transmitted);

        /*
         * sum to total_bytes_transmitted all
         * unicast/multicast/broadcast
         */
        qstats->total_bytes_transmitted_hi =
            qstats->total_unicast_bytes_transmitted_hi;
        qstats->total_bytes_transmitted_lo =
            qstats->total_unicast_bytes_transmitted_lo;

        ADD_64(qstats->total_bytes_transmitted_hi,
               qstats->total_broadcast_bytes_transmitted_hi,
               qstats->total_bytes_transmitted_lo,
               qstats->total_broadcast_bytes_transmitted_lo);

        ADD_64(qstats->total_bytes_transmitted_hi,
               qstats->total_multicast_bytes_transmitted_hi,
               qstats->total_bytes_transmitted_lo,
               qstats->total_multicast_bytes_transmitted_lo);

        UPDATE_EXTEND_XSTAT(ucast_pkts_sent,
                            total_unicast_packets_transmitted);
        UPDATE_EXTEND_XSTAT(mcast_pkts_sent,
                            total_multicast_packets_transmitted);
        UPDATE_EXTEND_XSTAT(bcast_pkts_sent,
                            total_broadcast_packets_transmitted);

        UPDATE_EXTEND_TSTAT(checksum_discard,
                            total_packets_received_checksum_discarded);
        UPDATE_EXTEND_TSTAT(ttl0_discard,
                            total_packets_received_ttl0_discarded);

        UPDATE_EXTEND_XSTAT(error_drop_pkts,
                            total_transmitted_dropped_packets_error);

        /* TPA aggregations completed */
        UPDATE_EXTEND_E_USTAT(coalesced_events, total_tpa_aggregations);
        /* Number of network frames aggregated by TPA */
        UPDATE_EXTEND_E_USTAT(coalesced_pkts, total_tpa_aggregated_frames);
        /* Total number of bytes in completed TPA aggregations */
        UPDATE_QSTAT(uclient->coalesced_bytes, total_tpa_bytes);

        UPDATE_ESTAT_QSTAT_64(total_tpa_bytes);

        UPDATE_FSTAT_QSTAT(total_bytes_received);
        UPDATE_FSTAT_QSTAT(total_bytes_transmitted);
        UPDATE_FSTAT_QSTAT(total_unicast_packets_received);
        UPDATE_FSTAT_QSTAT(total_multicast_packets_received);
        UPDATE_FSTAT_QSTAT(total_broadcast_packets_received);
        UPDATE_FSTAT_QSTAT(total_unicast_packets_transmitted);
        UPDATE_FSTAT_QSTAT(total_multicast_packets_transmitted);
        UPDATE_FSTAT_QSTAT(total_broadcast_packets_transmitted);
        UPDATE_FSTAT_QSTAT(valid_bytes_received);
    }

    ADD_64(estats->total_bytes_received_hi,
           estats->rx_stat_ifhcinbadoctets_hi,
           estats->total_bytes_received_lo,
           estats->rx_stat_ifhcinbadoctets_lo);

    ADD_64_LE(estats->total_bytes_received_hi,
              tfunc->rcv_error_bytes.hi,
              estats->total_bytes_received_lo,
              tfunc->rcv_error_bytes.lo);

    ADD_64_LE(estats->error_bytes_received_hi,
              tfunc->rcv_error_bytes.hi,
              estats->error_bytes_received_lo,
              tfunc->rcv_error_bytes.lo);

    UPDATE_ESTAT(etherstatsoverrsizepkts, rx_stat_dot3statsframestoolong);

    ADD_64(estats->error_bytes_received_hi,
           estats->rx_stat_ifhcinbadoctets_hi,
           estats->error_bytes_received_lo,
           estats->rx_stat_ifhcinbadoctets_lo);

    if (sc->port.pmf) {
        struct bxe_fw_port_stats_old *fwstats = &sc->fw_stats_old;
        UPDATE_FW_STAT(mac_filter_discard);
        UPDATE_FW_STAT(mf_tag_discard);
        UPDATE_FW_STAT(brb_truncate_discard);
        UPDATE_FW_STAT(mac_discard);
    }

    fstats->host_func_stats_start = ++fstats->host_func_stats_end;

    sc->stats_pending = 0;

    return (0);
}

static void
bxe_net_stats_update(struct bxe_softc *sc)
{

    for (int i = 0; i < sc->num_queues; i++)
        if_inc_counter(sc->ifp, IFCOUNTER_IQDROPS,
	    le32toh(sc->fp[i].old_tclient.checksum_discard));
}

uint64_t
bxe_get_counter(if_t ifp, ift_counter cnt)
{
	struct bxe_softc *sc;
	struct bxe_eth_stats *estats;

	sc = if_getsoftc(ifp);
	estats = &sc->eth_stats;

	switch (cnt) {
	case IFCOUNTER_IPACKETS:
		return (bxe_hilo(&estats->total_unicast_packets_received_hi) +
		    bxe_hilo(&estats->total_multicast_packets_received_hi) +
		    bxe_hilo(&estats->total_broadcast_packets_received_hi));
	case IFCOUNTER_OPACKETS:
		return (bxe_hilo(&estats->total_unicast_packets_transmitted_hi) +
		    bxe_hilo(&estats->total_multicast_packets_transmitted_hi) +
		    bxe_hilo(&estats->total_broadcast_packets_transmitted_hi));
	case IFCOUNTER_IBYTES:
		return (bxe_hilo(&estats->total_bytes_received_hi));
	case IFCOUNTER_OBYTES:
		return (bxe_hilo(&estats->total_bytes_transmitted_hi));
	case IFCOUNTER_IERRORS:
		return (bxe_hilo(&estats->rx_stat_etherstatsundersizepkts_hi) +
		    bxe_hilo(&estats->etherstatsoverrsizepkts_hi) +
		    bxe_hilo(&estats->brb_drop_hi) +
		    bxe_hilo(&estats->brb_truncate_hi) +
		    bxe_hilo(&estats->rx_stat_dot3statsfcserrors_hi) +
		    bxe_hilo(&estats->rx_stat_dot3statsalignmenterrors_hi) +
		    bxe_hilo(&estats->no_buff_discard_hi));
	case IFCOUNTER_OERRORS:
		return (bxe_hilo(&estats->rx_stat_dot3statscarriersenseerrors_hi) +
		    bxe_hilo(&estats->tx_stat_dot3statsinternalmactransmiterrors_hi));
	case IFCOUNTER_IMCASTS:
		return (bxe_hilo(&estats->total_multicast_packets_received_hi));
	case IFCOUNTER_COLLISIONS:
		return (bxe_hilo(&estats->tx_stat_etherstatscollisions_hi) +
		    bxe_hilo(&estats->tx_stat_dot3statslatecollisions_hi) +
		    bxe_hilo(&estats->tx_stat_dot3statsexcessivecollisions_hi));
	default:
		return (if_get_counter_default(ifp, cnt));
	}
}

static void
bxe_drv_stats_update(struct bxe_softc *sc)
{
    struct bxe_eth_stats *estats = &sc->eth_stats;
    int i;

    for (i = 0; i < sc->num_queues; i++) {
        struct bxe_eth_q_stats *qstats = &sc->fp[i].eth_q_stats;
        struct bxe_eth_q_stats_old *qstats_old = &sc->fp[i].eth_q_stats_old;

        UPDATE_ESTAT_QSTAT(rx_calls);
        UPDATE_ESTAT_QSTAT(rx_pkts);
        UPDATE_ESTAT_QSTAT(rx_tpa_pkts);
        UPDATE_ESTAT_QSTAT(rx_erroneous_jumbo_sge_pkts);
        UPDATE_ESTAT_QSTAT(rx_bxe_service_rxsgl);
        UPDATE_ESTAT_QSTAT(rx_jumbo_sge_pkts);
        UPDATE_ESTAT_QSTAT(rx_soft_errors);
        UPDATE_ESTAT_QSTAT(rx_hw_csum_errors);
        UPDATE_ESTAT_QSTAT(rx_ofld_frames_csum_ip);
        UPDATE_ESTAT_QSTAT(rx_ofld_frames_csum_tcp_udp);
        UPDATE_ESTAT_QSTAT(rx_budget_reached);
        UPDATE_ESTAT_QSTAT(tx_pkts);
        UPDATE_ESTAT_QSTAT(tx_soft_errors);
        UPDATE_ESTAT_QSTAT(tx_ofld_frames_csum_ip);
        UPDATE_ESTAT_QSTAT(tx_ofld_frames_csum_tcp);
        UPDATE_ESTAT_QSTAT(tx_ofld_frames_csum_udp);
        UPDATE_ESTAT_QSTAT(tx_ofld_frames_lso);
        UPDATE_ESTAT_QSTAT(tx_ofld_frames_lso_hdr_splits);
        UPDATE_ESTAT_QSTAT(tx_encap_failures);
        UPDATE_ESTAT_QSTAT(tx_hw_queue_full);
        UPDATE_ESTAT_QSTAT(tx_hw_max_queue_depth);
        UPDATE_ESTAT_QSTAT(tx_dma_mapping_failure);
        UPDATE_ESTAT_QSTAT(tx_max_drbr_queue_depth);
        UPDATE_ESTAT_QSTAT(tx_window_violation_std);
        UPDATE_ESTAT_QSTAT(tx_window_violation_tso);
        //UPDATE_ESTAT_QSTAT(tx_unsupported_tso_request_ipv6);
        //UPDATE_ESTAT_QSTAT(tx_unsupported_tso_request_not_tcp);
        UPDATE_ESTAT_QSTAT(tx_chain_lost_mbuf);
        UPDATE_ESTAT_QSTAT(tx_frames_deferred);
        UPDATE_ESTAT_QSTAT(tx_queue_xoff);

        /* mbuf driver statistics */
        UPDATE_ESTAT_QSTAT(mbuf_defrag_attempts);
        UPDATE_ESTAT_QSTAT(mbuf_defrag_failures);
        UPDATE_ESTAT_QSTAT(mbuf_rx_bd_alloc_failed);
        UPDATE_ESTAT_QSTAT(mbuf_rx_bd_mapping_failed);
        UPDATE_ESTAT_QSTAT(mbuf_rx_tpa_alloc_failed);
        UPDATE_ESTAT_QSTAT(mbuf_rx_tpa_mapping_failed);
        UPDATE_ESTAT_QSTAT(mbuf_rx_sge_alloc_failed);
        UPDATE_ESTAT_QSTAT(mbuf_rx_sge_mapping_failed);

        /* track the number of allocated mbufs */
        UPDATE_ESTAT_QSTAT(mbuf_alloc_tx);
        UPDATE_ESTAT_QSTAT(mbuf_alloc_rx);
        UPDATE_ESTAT_QSTAT(mbuf_alloc_sge);
        UPDATE_ESTAT_QSTAT(mbuf_alloc_tpa);
    }
}

static uint8_t
bxe_edebug_stats_stopped(struct bxe_softc *sc)
{
    uint32_t val;

    if (SHMEM2_HAS(sc, edebug_driver_if[1])) {
        val = SHMEM2_RD(sc, edebug_driver_if[1]);

        if (val == EDEBUG_DRIVER_IF_OP_CODE_DISABLE_STAT) {
            return (TRUE);
        }
    }

    return (FALSE);
}

static void
bxe_stats_update(struct bxe_softc *sc)
{
    uint32_t *stats_comp = BXE_SP(sc, stats_comp);

    if (bxe_edebug_stats_stopped(sc)) {
        return;
    }

    if (IS_PF(sc)) {
        if (*stats_comp != DMAE_COMP_VAL) {
            return;
        }

        if (sc->port.pmf) {
            bxe_hw_stats_update(sc);
        }

        if (bxe_storm_stats_update(sc)) {
            if (sc->stats_pending++ == 3) {
		if (if_getdrvflags(sc->ifp) & IFF_DRV_RUNNING) {
		    BLOGE(sc, "Storm stats not updated for 3 times, resetting\n");
		    BXE_SET_ERROR_BIT(sc, BXE_ERR_STATS_TO);
		    taskqueue_enqueue_timeout(taskqueue_thread,
                            &sc->sp_err_timeout_task, hz/10);
		}
            }
            return;
        }
    } else {
        /*
         * VF doesn't collect HW statistics, and doesn't get completions,
         * performs only update.
         */
        bxe_storm_stats_update(sc);
    }

    bxe_net_stats_update(sc);
    bxe_drv_stats_update(sc);

    /* vf is done */
    if (IS_VF(sc)) {
        return;
    }

    bxe_hw_stats_post(sc);
    bxe_storm_stats_post(sc);
}

static void
bxe_port_stats_stop(struct bxe_softc *sc)
{
    struct dmae_cmd *dmae;
    uint32_t opcode;
    int loader_idx = PMF_DMAE_C(sc);
    uint32_t *stats_comp = BXE_SP(sc, stats_comp);

    sc->executer_idx = 0;

    opcode = bxe_dmae_opcode(sc, DMAE_SRC_PCI, DMAE_DST_GRC, FALSE, 0);

    if (sc->port.port_stx) {
        dmae = BXE_SP(sc, dmae[sc->executer_idx++]);

        if (sc->func_stx) {
            dmae->opcode = bxe_dmae_opcode_add_comp(opcode, DMAE_COMP_GRC);
        } else {
            dmae->opcode = bxe_dmae_opcode_add_comp(opcode, DMAE_COMP_PCI);
        }

        dmae->src_addr_lo = U64_LO(BXE_SP_MAPPING(sc, port_stats));
        dmae->src_addr_hi = U64_HI(BXE_SP_MAPPING(sc, port_stats));
        dmae->dst_addr_lo = sc->port.port_stx >> 2;
        dmae->dst_addr_hi = 0;
        dmae->len = bxe_get_port_stats_dma_len(sc);
        if (sc->func_stx) {
            dmae->comp_addr_lo = (dmae_reg_go_c[loader_idx] >> 2);
            dmae->comp_addr_hi = 0;
            dmae->comp_val = 1;
        } else {
            dmae->comp_addr_lo = U64_LO(BXE_SP_MAPPING(sc, stats_comp));
            dmae->comp_addr_hi = U64_HI(BXE_SP_MAPPING(sc, stats_comp));
            dmae->comp_val = DMAE_COMP_VAL;

            *stats_comp = 0;
        }
    }

    if (sc->func_stx) {
        dmae = BXE_SP(sc, dmae[sc->executer_idx++]);
        dmae->opcode = bxe_dmae_opcode_add_comp(opcode, DMAE_COMP_PCI);
        dmae->src_addr_lo = U64_LO(BXE_SP_MAPPING(sc, func_stats));
        dmae->src_addr_hi = U64_HI(BXE_SP_MAPPING(sc, func_stats));
        dmae->dst_addr_lo = (sc->func_stx >> 2);
        dmae->dst_addr_hi = 0;
        dmae->len = (sizeof(struct host_func_stats) >> 2);
        dmae->comp_addr_lo = U64_LO(BXE_SP_MAPPING(sc, stats_comp));
        dmae->comp_addr_hi = U64_HI(BXE_SP_MAPPING(sc, stats_comp));
        dmae->comp_val = DMAE_COMP_VAL;

        *stats_comp = 0;
    }
}

static void
bxe_stats_stop(struct bxe_softc *sc)
{
    uint8_t update = FALSE;

    bxe_stats_comp(sc);

    if (sc->port.pmf) {
        update = bxe_hw_stats_update(sc) == 0;
    }

    update |= bxe_storm_stats_update(sc) == 0;

    if (update) {
        bxe_net_stats_update(sc);

        if (sc->port.pmf) {
            bxe_port_stats_stop(sc);
        }

        bxe_hw_stats_post(sc);
        bxe_stats_comp(sc);
    }
}

static void
bxe_stats_do_nothing(struct bxe_softc *sc)
{
    return;
}

static const struct {
    void (*action)(struct bxe_softc *sc);
    enum bxe_stats_state next_state;
} bxe_stats_stm[STATS_STATE_MAX][STATS_EVENT_MAX] = {
    {
    /* DISABLED PMF */ { bxe_stats_pmf_update, STATS_STATE_DISABLED },
    /*      LINK_UP */ { bxe_stats_start,      STATS_STATE_ENABLED },
    /*      UPDATE  */ { bxe_stats_do_nothing, STATS_STATE_DISABLED },
    /*      STOP    */ { bxe_stats_do_nothing, STATS_STATE_DISABLED }
    },
    {
    /* ENABLED  PMF */ { bxe_stats_pmf_start,  STATS_STATE_ENABLED },
    /*      LINK_UP */ { bxe_stats_restart,    STATS_STATE_ENABLED },
    /*      UPDATE  */ { bxe_stats_update,     STATS_STATE_ENABLED },
    /*      STOP    */ { bxe_stats_stop,       STATS_STATE_DISABLED }
    }
};

void bxe_stats_handle(struct bxe_softc     *sc,
                      enum bxe_stats_event event)
{
    enum bxe_stats_state state;

    if (__predict_false(sc->panic)) {
        return;
    }

    BXE_STATS_LOCK(sc);
    state = sc->stats_state;
    sc->stats_state = bxe_stats_stm[state][event].next_state;
    BXE_STATS_UNLOCK(sc);

    bxe_stats_stm[state][event].action(sc);

    if (event != STATS_EVENT_UPDATE) {
        BLOGD(sc, DBG_STATS,
              "state %d -> event %d -> state %d\n",
              state, event, sc->stats_state);
    }
}

static void
bxe_port_stats_base_init(struct bxe_softc *sc)
{
    struct dmae_cmd *dmae;
    uint32_t *stats_comp = BXE_SP(sc, stats_comp);

    /* sanity */
    if (!sc->port.pmf || !sc->port.port_stx) {
        BLOGE(sc, "BUG!\n");
        return;
    }

    sc->executer_idx = 0;

    dmae = BXE_SP(sc, dmae[sc->executer_idx++]);
    dmae->opcode = bxe_dmae_opcode(sc, DMAE_SRC_PCI, DMAE_DST_GRC,
                                   TRUE, DMAE_COMP_PCI);
    dmae->src_addr_lo = U64_LO(BXE_SP_MAPPING(sc, port_stats));
    dmae->src_addr_hi = U64_HI(BXE_SP_MAPPING(sc, port_stats));
    dmae->dst_addr_lo = (sc->port.port_stx >> 2);
    dmae->dst_addr_hi = 0;
    dmae->len = bxe_get_port_stats_dma_len(sc);
    dmae->comp_addr_lo = U64_LO(BXE_SP_MAPPING(sc, stats_comp));
    dmae->comp_addr_hi = U64_HI(BXE_SP_MAPPING(sc, stats_comp));
    dmae->comp_val = DMAE_COMP_VAL;

    *stats_comp = 0;
    bxe_hw_stats_post(sc);
    bxe_stats_comp(sc);
}

/*
 * This function will prepare the statistics ramrod data the way
 * we will only have to increment the statistics counter and
 * send the ramrod each time we have to.
 */
static void
bxe_prep_fw_stats_req(struct bxe_softc *sc)
{
    int i;
    int first_queue_query_index;
    struct stats_query_header *stats_hdr = &sc->fw_stats_req->hdr;
    bus_addr_t cur_data_offset;
    struct stats_query_entry *cur_query_entry;

    stats_hdr->cmd_num = sc->fw_stats_num;
    stats_hdr->drv_stats_counter = 0;

    /*
     * The storm_counters struct contains the counters of completed
     * statistics requests per storm which are incremented by FW
     * each time it completes hadning a statistics ramrod. We will
     * check these counters in the timer handler and discard a
     * (statistics) ramrod completion.
     */
    cur_data_offset = (sc->fw_stats_data_mapping +
                       offsetof(struct bxe_fw_stats_data, storm_counters));

    stats_hdr->stats_counters_addrs.hi = htole32(U64_HI(cur_data_offset));
    stats_hdr->stats_counters_addrs.lo = htole32(U64_LO(cur_data_offset));

    /*
     * Prepare the first stats ramrod (will be completed with
     * the counters equal to zero) - init counters to somethig different.
     */
    memset(&sc->fw_stats_data->storm_counters, 0xff,
           sizeof(struct stats_counter));

    /**** Port FW statistics data ****/
    cur_data_offset = (sc->fw_stats_data_mapping +
                       offsetof(struct bxe_fw_stats_data, port));

    cur_query_entry = &sc->fw_stats_req->query[BXE_PORT_QUERY_IDX];

    cur_query_entry->kind = STATS_TYPE_PORT;
    /* For port query index is a DONT CARE */
    cur_query_entry->index = SC_PORT(sc);
    /* For port query funcID is a DONT CARE */
    cur_query_entry->funcID = htole16(SC_FUNC(sc));
    cur_query_entry->address.hi = htole32(U64_HI(cur_data_offset));
    cur_query_entry->address.lo = htole32(U64_LO(cur_data_offset));

    /**** PF FW statistics data ****/
    cur_data_offset = (sc->fw_stats_data_mapping +
                       offsetof(struct bxe_fw_stats_data, pf));

    cur_query_entry = &sc->fw_stats_req->query[BXE_PF_QUERY_IDX];

    cur_query_entry->kind = STATS_TYPE_PF;
    /* For PF query index is a DONT CARE */
    cur_query_entry->index = SC_PORT(sc);
    cur_query_entry->funcID = htole16(SC_FUNC(sc));
    cur_query_entry->address.hi = htole32(U64_HI(cur_data_offset));
    cur_query_entry->address.lo = htole32(U64_LO(cur_data_offset));

    /**** Clients' queries ****/
    cur_data_offset = (sc->fw_stats_data_mapping +
                       offsetof(struct bxe_fw_stats_data, queue_stats));

    /*
     * First queue query index depends whether FCoE offloaded request will
     * be included in the ramrod
     */
    first_queue_query_index = (BXE_FIRST_QUEUE_QUERY_IDX - 1);

    for (i = 0; i < sc->num_queues; i++) {
        cur_query_entry =
            &sc->fw_stats_req->query[first_queue_query_index + i];

        cur_query_entry->kind = STATS_TYPE_QUEUE;
        cur_query_entry->index = bxe_stats_id(&sc->fp[i]);
        cur_query_entry->funcID = htole16(SC_FUNC(sc));
        cur_query_entry->address.hi = htole32(U64_HI(cur_data_offset));
        cur_query_entry->address.lo = htole32(U64_LO(cur_data_offset));

        cur_data_offset += sizeof(struct per_queue_stats);
    }
}

void
bxe_stats_init(struct bxe_softc *sc)
{
    int /*abs*/port = SC_PORT(sc);
    int mb_idx = SC_FW_MB_IDX(sc);
    int i;

    sc->stats_pending = 0;
    sc->executer_idx = 0;
    sc->stats_counter = 0;

    /* port and func stats for management */
    if (!BXE_NOMCP(sc)) {
        sc->port.port_stx = SHMEM_RD(sc, port_mb[port].port_stx);
        sc->func_stx = SHMEM_RD(sc, func_mb[mb_idx].fw_mb_param);
    } else {
        sc->port.port_stx = 0;
        sc->func_stx = 0;
    }

    BLOGD(sc, DBG_STATS, "port_stx 0x%x func_stx 0x%x\n",
          sc->port.port_stx, sc->func_stx);

    /* pmf should retrieve port statistics from SP on a non-init*/
    if (!sc->stats_init && sc->port.pmf && sc->port.port_stx) {
        bxe_stats_handle(sc, STATS_EVENT_PMF);
    }

    port = SC_PORT(sc);
    /* port stats */
    memset(&(sc->port.old_nig_stats), 0, sizeof(struct nig_stats));
    sc->port.old_nig_stats.brb_discard =
        REG_RD(sc, NIG_REG_STAT0_BRB_DISCARD + port*0x38);
    sc->port.old_nig_stats.brb_truncate =
        REG_RD(sc, NIG_REG_STAT0_BRB_TRUNCATE + port*0x38);
    if (!CHIP_IS_E3(sc)) {
        REG_RD_DMAE(sc, NIG_REG_STAT0_EGRESS_MAC_PKT0 + port*0x50,
                    &(sc->port.old_nig_stats.egress_mac_pkt0_lo), 2);
        REG_RD_DMAE(sc, NIG_REG_STAT0_EGRESS_MAC_PKT1 + port*0x50,
                    &(sc->port.old_nig_stats.egress_mac_pkt1_lo), 2);
    }

    /* function stats */
    for (i = 0; i < sc->num_queues; i++) {
        memset(&sc->fp[i].old_tclient, 0, sizeof(sc->fp[i].old_tclient));
        memset(&sc->fp[i].old_uclient, 0, sizeof(sc->fp[i].old_uclient));
        memset(&sc->fp[i].old_xclient, 0, sizeof(sc->fp[i].old_xclient));
        if (sc->stats_init) {
            memset(&sc->fp[i].eth_q_stats, 0,
                   sizeof(sc->fp[i].eth_q_stats));
            memset(&sc->fp[i].eth_q_stats_old, 0,
                   sizeof(sc->fp[i].eth_q_stats_old));
        }
    }

    /* prepare statistics ramrod data */
    bxe_prep_fw_stats_req(sc);

    if (sc->stats_init) {
        memset(&sc->net_stats_old, 0, sizeof(sc->net_stats_old));
        memset(&sc->fw_stats_old, 0, sizeof(sc->fw_stats_old));
        memset(&sc->eth_stats_old, 0, sizeof(sc->eth_stats_old));
        memset(&sc->eth_stats, 0, sizeof(sc->eth_stats));
        memset(&sc->func_stats, 0, sizeof(sc->func_stats));

        /* Clean SP from previous statistics */
        if (sc->func_stx) {
            memset(BXE_SP(sc, func_stats), 0, sizeof(struct host_func_stats));
            bxe_func_stats_init(sc);
            bxe_hw_stats_post(sc);
            bxe_stats_comp(sc);
        }
    }

    sc->stats_state = STATS_STATE_DISABLED;

    if (sc->port.pmf && sc->port.port_stx) {
        bxe_port_stats_base_init(sc);
    }

    /* mark the end of statistics initialization */
    sc->stats_init = FALSE;
}

void
bxe_save_statistics(struct bxe_softc *sc)
{
    int i;

    /* save queue statistics */
    for (i = 0; i < sc->num_queues; i++) {
        struct bxe_fastpath *fp = &sc->fp[i];
        struct bxe_eth_q_stats *qstats = &fp->eth_q_stats;
        struct bxe_eth_q_stats_old *qstats_old = &fp->eth_q_stats_old;

        UPDATE_QSTAT_OLD(total_unicast_bytes_received_hi);
        UPDATE_QSTAT_OLD(total_unicast_bytes_received_lo);
        UPDATE_QSTAT_OLD(total_broadcast_bytes_received_hi);
        UPDATE_QSTAT_OLD(total_broadcast_bytes_received_lo);
        UPDATE_QSTAT_OLD(total_multicast_bytes_received_hi);
        UPDATE_QSTAT_OLD(total_multicast_bytes_received_lo);
        UPDATE_QSTAT_OLD(total_unicast_bytes_transmitted_hi);
        UPDATE_QSTAT_OLD(total_unicast_bytes_transmitted_lo);
        UPDATE_QSTAT_OLD(total_broadcast_bytes_transmitted_hi);
        UPDATE_QSTAT_OLD(total_broadcast_bytes_transmitted_lo);
        UPDATE_QSTAT_OLD(total_multicast_bytes_transmitted_hi);
        UPDATE_QSTAT_OLD(total_multicast_bytes_transmitted_lo);
        UPDATE_QSTAT_OLD(total_tpa_bytes_hi);
        UPDATE_QSTAT_OLD(total_tpa_bytes_lo);
    }

    /* store port firmware statistics */
    if (sc->port.pmf) {
        struct bxe_eth_stats *estats = &sc->eth_stats;
        struct bxe_fw_port_stats_old *fwstats = &sc->fw_stats_old;
        struct host_port_stats *pstats = BXE_SP(sc, port_stats);

        fwstats->pfc_frames_rx_hi = pstats->pfc_frames_rx_hi;
        fwstats->pfc_frames_rx_lo = pstats->pfc_frames_rx_lo;
        fwstats->pfc_frames_tx_hi = pstats->pfc_frames_tx_hi;
        fwstats->pfc_frames_tx_lo = pstats->pfc_frames_tx_lo;

        if (IS_MF(sc)) {
            UPDATE_FW_STAT_OLD(mac_filter_discard);
            UPDATE_FW_STAT_OLD(mf_tag_discard);
            UPDATE_FW_STAT_OLD(brb_truncate_discard);
            UPDATE_FW_STAT_OLD(mac_discard);
        }
    }
}

void
bxe_afex_collect_stats(struct bxe_softc *sc,
                       void             *void_afex_stats,
                       uint32_t         stats_type)
{
    int i;
    struct afex_stats *afex_stats = (struct afex_stats *)void_afex_stats;
    struct bxe_eth_stats *estats = &sc->eth_stats;

    memset(afex_stats, 0, sizeof(struct afex_stats));

    for (i = 0; i < sc->num_queues; i++) {
        struct bxe_eth_q_stats *qstats = &sc->fp[i].eth_q_stats;

        ADD_64(afex_stats->rx_unicast_bytes_hi,
               qstats->total_unicast_bytes_received_hi,
               afex_stats->rx_unicast_bytes_lo,
               qstats->total_unicast_bytes_received_lo);

        ADD_64(afex_stats->rx_broadcast_bytes_hi,
               qstats->total_broadcast_bytes_received_hi,
               afex_stats->rx_broadcast_bytes_lo,
               qstats->total_broadcast_bytes_received_lo);

        ADD_64(afex_stats->rx_multicast_bytes_hi,
               qstats->total_multicast_bytes_received_hi,
               afex_stats->rx_multicast_bytes_lo,
               qstats->total_multicast_bytes_received_lo);

        ADD_64(afex_stats->rx_unicast_frames_hi,
               qstats->total_unicast_packets_received_hi,
               afex_stats->rx_unicast_frames_lo,
               qstats->total_unicast_packets_received_lo);

        ADD_64(afex_stats->rx_broadcast_frames_hi,
               qstats->total_broadcast_packets_received_hi,
               afex_stats->rx_broadcast_frames_lo,
               qstats->total_broadcast_packets_received_lo);

        ADD_64(afex_stats->rx_multicast_frames_hi,
               qstats->total_multicast_packets_received_hi,
               afex_stats->rx_multicast_frames_lo,
               qstats->total_multicast_packets_received_lo);

        /*
         * sum to rx_frames_discarded all discarded
         * packets due to size, ttl0 and checksum
         */
        ADD_64(afex_stats->rx_frames_discarded_hi,
               qstats->total_packets_received_checksum_discarded_hi,
               afex_stats->rx_frames_discarded_lo,
               qstats->total_packets_received_checksum_discarded_lo);

        ADD_64(afex_stats->rx_frames_discarded_hi,
               qstats->total_packets_received_ttl0_discarded_hi,
               afex_stats->rx_frames_discarded_lo,
               qstats->total_packets_received_ttl0_discarded_lo);

        ADD_64(afex_stats->rx_frames_discarded_hi,
               qstats->etherstatsoverrsizepkts_hi,
               afex_stats->rx_frames_discarded_lo,
               qstats->etherstatsoverrsizepkts_lo);

        ADD_64(afex_stats->rx_frames_dropped_hi,
               qstats->no_buff_discard_hi,
               afex_stats->rx_frames_dropped_lo,
               qstats->no_buff_discard_lo);

        ADD_64(afex_stats->tx_unicast_bytes_hi,
               qstats->total_unicast_bytes_transmitted_hi,
               afex_stats->tx_unicast_bytes_lo,
               qstats->total_unicast_bytes_transmitted_lo);

        ADD_64(afex_stats->tx_broadcast_bytes_hi,
               qstats->total_broadcast_bytes_transmitted_hi,
               afex_stats->tx_broadcast_bytes_lo,
               qstats->total_broadcast_bytes_transmitted_lo);

        ADD_64(afex_stats->tx_multicast_bytes_hi,
               qstats->total_multicast_bytes_transmitted_hi,
               afex_stats->tx_multicast_bytes_lo,
               qstats->total_multicast_bytes_transmitted_lo);

        ADD_64(afex_stats->tx_unicast_frames_hi,
               qstats->total_unicast_packets_transmitted_hi,
               afex_stats->tx_unicast_frames_lo,
               qstats->total_unicast_packets_transmitted_lo);

        ADD_64(afex_stats->tx_broadcast_frames_hi,
               qstats->total_broadcast_packets_transmitted_hi,
               afex_stats->tx_broadcast_frames_lo,
               qstats->total_broadcast_packets_transmitted_lo);

        ADD_64(afex_stats->tx_multicast_frames_hi,
               qstats->total_multicast_packets_transmitted_hi,
               afex_stats->tx_multicast_frames_lo,
               qstats->total_multicast_packets_transmitted_lo);

        ADD_64(afex_stats->tx_frames_dropped_hi,
               qstats->total_transmitted_dropped_packets_error_hi,
               afex_stats->tx_frames_dropped_lo,
               qstats->total_transmitted_dropped_packets_error_lo);
    }

    /*
     * If port stats are requested, add them to the PMF
     * stats, as anyway they will be accumulated by the
     * MCP before sent to the switch
     */
    if ((sc->port.pmf) && (stats_type == VICSTATST_UIF_INDEX)) {
        ADD_64(afex_stats->rx_frames_dropped_hi,
               0,
               afex_stats->rx_frames_dropped_lo,
               estats->mac_filter_discard);
        ADD_64(afex_stats->rx_frames_dropped_hi,
               0,
               afex_stats->rx_frames_dropped_lo,
               estats->brb_truncate_discard);
        ADD_64(afex_stats->rx_frames_discarded_hi,
               0,
               afex_stats->rx_frames_discarded_lo,
               estats->mac_discard);
    }
}

