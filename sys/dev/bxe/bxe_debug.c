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

#include "ddb/ddb.h"
#include "ddb/db_sym.h"
#include "ddb/db_lex.h"

#ifdef BXE_REG_NO_INLINE

/*
 * Debug versions of the 8/16/32 bit OS register read/write functions to
 * capture/display values read/written from/to the controller.
 */

void
bxe_reg_write8(struct bxe_softc *sc, bus_size_t offset, uint8_t val)
{
    BLOGD(sc, DBG_REGS, "offset=0x%08lx val=0x%02x\n", offset, val);
    bus_space_write_1(sc->bar[BAR0].tag,
                      sc->bar[BAR0].handle,
                      offset,
                      val);
}

void
bxe_reg_write16(struct bxe_softc *sc, bus_size_t offset, uint16_t val)
{
    if ((offset % 2) != 0) {
        BLOGD(sc, DBG_REGS, "Unaligned 16-bit write to 0x%08lx\n", offset);
    }

    BLOGD(sc, DBG_REGS, "offset=0x%08lx val=0x%04x\n", offset, val);
    bus_space_write_2(sc->bar[BAR0].tag,
                      sc->bar[BAR0].handle,
                      offset,
                      val);
}

void
bxe_reg_write32(struct bxe_softc *sc, bus_size_t offset, uint32_t val)
{
    if ((offset % 4) != 0) {
        BLOGD(sc, DBG_REGS, "Unaligned 32-bit write to 0x%08lx\n", offset);
    }

    BLOGD(sc, DBG_REGS, "offset=0x%08lx val=0x%08x\n", offset, val);
    bus_space_write_4(sc->bar[BAR0].tag,
                      sc->bar[BAR0].handle,
                      offset,
                      val);
}

uint8_t
bxe_reg_read8(struct bxe_softc *sc, bus_size_t offset)
{
    uint8_t val;

    val = bus_space_read_1(sc->bar[BAR0].tag,
                           sc->bar[BAR0].handle,
                           offset);
    BLOGD(sc, DBG_REGS, "offset=0x%08lx val=0x%02x\n", offset, val);

    return (val);
}

uint16_t
bxe_reg_read16(struct bxe_softc *sc, bus_size_t offset)
{
    uint16_t val;

    if ((offset % 2) != 0) {
        BLOGD(sc, DBG_REGS, "Unaligned 16-bit read from 0x%08lx\n", offset);
    }

    val = bus_space_read_2(sc->bar[BAR0].tag,
                           sc->bar[BAR0].handle,
                           offset);
    BLOGD(sc, DBG_REGS, "offset=0x%08lx val=0x%08x\n", offset, val);

    return (val);
}

uint32_t
bxe_reg_read32(struct bxe_softc *sc, bus_size_t offset)
{
    uint32_t val;

    if ((offset % 4) != 0) {
        BLOGD(sc, DBG_REGS, "Unaligned 32-bit read from 0x%08lx\n", offset);
    }

    val = bus_space_read_4(sc->bar[BAR0].tag,
                           sc->bar[BAR0].handle,
                           offset);
    BLOGD(sc, DBG_REGS, "offset=0x%08lx val=0x%08x\n", offset, val);

    return (val);
}

#endif /* BXE_REG_NO_INLINE */

#ifdef ELINK_DEBUG

void
elink_cb_dbg(struct bxe_softc *sc,
             char             *fmt)
{
    char buf[128];
    if (__predict_false(sc->debug & DBG_PHY)) {
        snprintf(buf, sizeof(buf), "ELINK: %s", fmt);
        device_printf(sc->dev, "%s", buf);
    }
}

void
elink_cb_dbg1(struct bxe_softc *sc,
              char             *fmt,
              uint32_t         arg1)
{
    char tmp[128], buf[128];
    if (__predict_false(sc->debug & DBG_PHY)) {
        snprintf(tmp, sizeof(tmp), "ELINK: %s", fmt);
        snprintf(buf, sizeof(buf), tmp, arg1);
        device_printf(sc->dev, "%s", buf);
    }
}

void
elink_cb_dbg2(struct bxe_softc *sc,
              char             *fmt,
              uint32_t         arg1,
              uint32_t         arg2)
{
    char tmp[128], buf[128];
    if (__predict_false(sc->debug & DBG_PHY)) {
        snprintf(tmp, sizeof(tmp), "ELINK: %s", fmt);
        snprintf(buf, sizeof(buf), tmp, arg1, arg2);
        device_printf(sc->dev, "%s", buf);
    }
}

void
elink_cb_dbg3(struct bxe_softc *sc,
              char             *fmt,
              uint32_t         arg1,
              uint32_t         arg2,
              uint32_t         arg3)
{
    char tmp[128], buf[128];
    if (__predict_false(sc->debug & DBG_PHY)) {
        snprintf(tmp, sizeof(tmp), "ELINK: %s", fmt);
        snprintf(buf, sizeof(buf), tmp, arg1, arg2, arg3);
        device_printf(sc->dev, "%s", buf);
    }
}

#endif /* ELINK_DEBUG */

extern struct mtx bxe_prev_mtx;

void
bxe_dump_mem(struct bxe_softc *sc,
             char             *tag,
             uint8_t          *mem,
             uint32_t         len)
{
    char buf[256];
    char c[32];
    int  xx;

    mtx_lock(&bxe_prev_mtx);

    BLOGI(sc, "++++++++++++ %s\n", tag);
    strcpy(buf, "** 000: ");

    for (xx = 0; xx < len; xx++)
    {
        if ((xx != 0) && (xx % 16 == 0))
        {
            BLOGI(sc, "%s\n", buf);
            strcpy(buf, "** ");
            snprintf(c, sizeof(c), "%03x", xx);
            strcat(buf, c);
            strcat(buf, ": ");
        }

        snprintf(c, sizeof(c), "%02x ", *mem);
        strcat(buf, c);

        mem++;
    }

    BLOGI(sc, "%s\n", buf);
    BLOGI(sc, "------------ %s\n", tag);

    mtx_unlock(&bxe_prev_mtx);
}

void
bxe_dump_mbuf_data(struct bxe_softc *sc,
                   char             *tag,
                   struct mbuf      *m,
                   uint8_t          contents)
{
    char buf[256];
    char c[32];
    uint8_t *memp;
    int i, xx = 0;

    mtx_lock(&bxe_prev_mtx);

    BLOGI(sc, "++++++++++++ %s\n", tag);

    while (m)
    {
        memp = m->m_data;
        strcpy(buf, "** > ");
        snprintf(c, sizeof(c), "%03x", xx);
        strcat(buf, c);
        strcat(buf, ": ");

        if (contents)
        {
            for (i = 0; i < m->m_len; i++)
            {
                if ((xx != 0) && (xx % 16 == 0))
                {
                    BLOGI(sc, "%s\n", buf);
                    strcpy(buf, "**   ");
                    snprintf(c, sizeof(c), "%03x", xx);
                    strcat(buf, c);
                    strcat(buf, ": ");
                }

                snprintf(c, sizeof(c), "%02x ", *memp);
                strcat(buf, c);

                memp++;
                xx++;
            }
        }
        else
        {
            snprintf(c, sizeof(c), "%d", m->m_len);
            strcat(buf, c);
            xx += m->m_len;
        }

        BLOGI(sc, "%s\n", buf);
        m = m->m_next;
    }

    BLOGI(sc, "------------ %s\n", tag);

    mtx_unlock(&bxe_prev_mtx);
}

#ifdef DDB

static void bxe_ddb_usage()
{
    db_printf("Usage: bxe[/hpv] <instance> [<address>]\n");
}

static db_cmdfcn_t bxe_ddb;
_DB_SET(_cmd, bxe, bxe_ddb, db_cmd_table, CS_OWN, NULL);

static void bxe_ddb(db_expr_t blah1,
                    boolean_t blah2,
                    db_expr_t blah3,
                    char      *blah4)
{
    char if_xname[IFNAMSIZ];
    if_t ifp = NULL;
    struct bxe_softc *sc;
    db_expr_t next_arg;
    int index;
    int tok;
    int mod_phys_addr = FALSE;
    int mod_virt_addr = FALSE;
    db_addr_t addr;

    tok = db_read_token();
    if (tok == tSLASH) {
        tok = db_read_token();
        if (tok != tIDENT) {
            db_printf("ERROR: bad modifier\n");
            bxe_ddb_usage();
            goto bxe_ddb_done;
        }
        if (strcmp(db_tok_string, "h") == 0) {
            bxe_ddb_usage();
            goto bxe_ddb_done;
        } else if (strcmp(db_tok_string, "p") == 0) {
            mod_phys_addr = TRUE;
        } else if (strcmp(db_tok_string, "v") == 0) {
            mod_virt_addr = TRUE;
        }
    } else {
        db_unread_token(tok);
    }

    if (!db_expression((db_expr_t *)&index)) {
        db_printf("ERROR: bxe index missing\n");
        bxe_ddb_usage();
        goto bxe_ddb_done;
    }

    snprintf(if_xname, sizeof(if_xname), "bxe%d", index);
    if ((ifp = ifunit_ref(if_xname)) == NULL) /* XXX */
    {
        db_printf("ERROR: Invalid interface %s\n", if_xname);
        goto bxe_ddb_done;
    }

    sc = (struct bxe_softc *)if_getsoftc(ifp);
    db_printf("ifnet=%p (%s)\n", ifp, if_xname);
    db_printf("softc=%p\n", sc);
    db_printf("  dev=%p\n", sc->dev);
    db_printf("  BDF=%d:%d:%d\n",
              sc->pcie_bus, sc->pcie_device, sc->pcie_func);

    if (mod_phys_addr || mod_virt_addr) {
        if (!db_expression((db_addr_t *)&addr)) {
            db_printf("ERROR: Invalid address\n");
            bxe_ddb_usage();
            goto bxe_ddb_done;
        }

        db_printf("addr=%p", addr);
    }

bxe_ddb_done:

    db_flush_lex();
    if (ifp) if_rele(ifp);
}

#endif /* DDB */

