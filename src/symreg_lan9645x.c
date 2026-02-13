// SPDX-License-Identifier: MIT
/* Copyright (c) 2026 Microchip Technology Inc. and its subsidiaries.
 * Microchip switch symbolic read tool
 *
 * License: MIT
 * Copyright (c) 2026 Microchip Technology Inc.
 */

#include "symreg.h"

static int sr_lan9645x_wait(void)
{
    return 0;
}

static void sr_lan9645x_signal(void)
{
}

static sr_io_t *sr_lan9645x_init(void) {
        return sr_io_fd_init("/sys/kernel/debug/lan9645x/mem");
}

static void sr_lan9645x_dump_mac(void)
{
    u32 mach = 0, macl = 0, maca;
    int cnt = 0;
    while (1) {
        wr("ANA_MACHDATA", mach);
        wr("ANA_MACLDATA", macl);
        wr("ANA_MACACCESS.MAC_TABLE_CMD", 4); // Get Next
        do {
            us(5);
            maca = rd("ANA_MACACCESS");
        } while (maca & 0xf); // cmd_idle
        if (((maca >> 12) & 0x1) == 0) { // valid bit
            break;
        }
        if (!cnt) {
            pr("MACHDATA   MACLDATA   MACACCESS  PGID   TYPE AGED CNG\n");
        }
        mach = rd("ANA_MACHDATA");
        macl = rd("ANA_MACLDATA");
        pr("0x%08x 0x%08x 0x%08x 0x%04u %u    %u    %u\n", mach, macl,
           maca, (maca >> 4) & 0x3f, (maca >> 10) & 0x3,
           (maca >> 13) & 0x1, (maca >> 17) & 0x1);
        cnt++;
    }
    if (!cnt) {
        pr("MAC table is empty!\n");
    }
}

static void sr_lan9645x_dump_vlan(void)
{
    u32 vlantidx, vlanaccess, vlan_port_mask;
    int i, cnt = 0;
    for (i = 0; i < 4096; i++) {
        wr("ANA_VLANTIDX.V_INDEX", i);
        wr("ANA_VLANACCESS.VLAN_TBL_CMD", 1); // Read
        do {
            us(5);
        } while (rd("ANA_VLANACCESS.VLAN_TBL_CMD"));
        vlantidx = rd("ANA_VLANTIDX");
        vlanaccess = rd("ANA_VLANACCESS");
        vlan_port_mask = rd("ANA_VLAN_PORT_MASK");
        if (vlanaccess || vlan_port_mask) {
            if (!cnt) {
                pr("VID  VLANTIDX   VLANACCESS   VLAN_PORT_MASK\n");
            }
            pr("%4d 0x%08x 0x%08x 0x%08x\n",
               i, vlantidx, vlanaccess, vlan_port_mask);
            cnt++;
        }
    }
    if (!cnt) {
        pr("VLAN table is empty!\n");
    }
}


static void sr_lan9645x_dump_stream(void)
{
    u32 streamtidx, streamaccess, split_mask, input_port_mask, stream_time, stream_red;
    int i, cnt = 0;
    for (i = 0; i < 64; i++) { // 128 isdxes on ASIC
        wr("ANA_STREAMTIDX.S_INDEX", i);
        wr("ANA_STREAMACCESS.STREAM_TBL_CMD", 1);
        do { us(5); } while (rd("ANA_STREAMACCESS.STREAM_TBL_CMD"));
        streamtidx = rd("ANA_STREAMTIDX");
        streamaccess = rd("ANA_STREAMACCESS");
        split_mask = rd("ANA_SPLIT_MASK");
        input_port_mask = rd("ANA_INPUT_PORT_MASK");
        stream_time = rd("ANA_STREAM_TIME");
        stream_red = rd("ANA_STREAM_RED");
        if (!cnt++) {
                pr("ISDX STREAMTIDX STREAMACCESS SPLIT_MASK INPUT_PORT_MASK  STREAM_TIME  STREAM_RED\n");
        }
        pr("%4d 0x%08x   0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
        i, streamtidx, streamaccess, split_mask, input_port_mask, stream_time, stream_red);
    }
}

static void sr_lan9645x_dump_dupl_disc(void)
{
    u32 row, col;
    u32 disc_access, READ_CMD = 3;
    u32 smac_lsb, smac_msb, seqno, portmask, age_flag;
    u32 cfg0, cfg1, cfg2;
    u64 smac;
    int cnt = 0;
    for (row = 0; row < 64; row++) {
        for (col = 0; col < 4; col++) { // asic 8 cols
            disc_access = (col & 0x3) << 17 | (row & 0x3f) << 11 | (READ_CMD<<7) | (1<<5);
            wr("QSYS_DISC_ACCESS_CTRL", disc_access);
            do { us(5); } while (rd("QSYS_DISC_ACCESS_CTRL.DISC_TABLE_ACCESS_SHOT"));

            cfg2 = rd("QSYS_DISC_ACCESS_CFG_2");
            if ((cfg2 & 0x1) == 0) // ENTRY_VLD==0
                    continue;

            age_flag = (cfg2 >> 5) & 0x7;
            portmask = (cfg2 >> 1) & 0xf;
            cfg1 = rd("QSYS_DISC_ACCESS_CFG_1");
            cfg0 = rd("QSYS_DISC_ACCESS_CFG_0");
            seqno = cfg0 >> 16;
            smac = (u64)(cfg0 & 0xffff) << 32 | (u64)cfg1;

            if (!cnt++)
                pr("(R,C)   MAC            SEQNO  PORTMASK       AGE_FLAG  CFG_2\n");
            pr("(%2u,%u) 0x%012lux 0x%04x 0x%02x 0x%01x 0x%08x\n",
               row, col, smac, seqno, portmask, age_flag, cfg2);
        }
    }
}


const sr_ops_t sr_lan9645x_ops = {
    .name        = "lan9645x",
    .wait        = sr_lan9645x_wait,
    .signal      = sr_lan9645x_signal,
    .dump_mac    = sr_lan9645x_dump_mac,
    .dump_vlan   = sr_lan9645x_dump_vlan,
    .dump_stream = sr_lan9645x_dump_stream,
    .dump_dd     = sr_lan9645x_dump_dupl_disc,
    .io_init     = sr_lan9645x_init,
};

/* vim: set ts=4 sw=4 sts=4 tw=120 cc=80,120 et ft=c cino=(0,w1,Ws,t0,\:s,l1 :*/
