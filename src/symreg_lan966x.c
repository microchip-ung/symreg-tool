// SPDX-License-Identifier: MIT
/* Copyright (c) 2026 Microchip Technology Inc. and its subsidiaries.
 * Microchip switch symbolic read tool
 *
 * License: MIT
 * Copyright (c) 2026 Microchip Technology Inc.
 */

#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include "symreg.h"

static int sr_lan966x_wait(void)
{
    return 0;
}

static void sr_lan966x_signal(void)
{
}

static sr_io_t *sr_lan966x_init(void) {
        return sr_io_fd_init("/sys/kernel/debug/symreg/mem");
}

static void sr_lan966x_dump_mac(void)
{
    u32 machdata = 0, macldata = 0, macaccess, mactindx;
    int cnt = 0;
    while (1) {
        wr("ANA_MACHDATA", machdata);
        wr("ANA_MACLDATA", macldata);
        wr("ANA_MACACCESS.MAC_TABLE_CMD", 4); // Get Next
        do {
            us(1000);
        } while (rd("ANA_MACACCESS.MAC_TABLE_CMD"));
        if (rd("ANA_MACACCESS.VALID") == 0) {
            break;
        }
        if (!cnt) {
            pr("MACHDATA   MACLDATA   MACACCESS  MACTINDX\n");
        }
        machdata = rd("ANA_MACHDATA");
        macldata = rd("ANA_MACLDATA");
        mactindx = rd("ANA_MACTINDX");
        macaccess = rd("ANA_MACACCESS");
        pr("0x%08x 0x%08x 0x%08x 0x%08x\n",
           machdata, macldata, macaccess, mactindx);
        cnt++;
    }
    if (!cnt) {
        pr("MAC table is empty!\n");
    }
}

static void sr_lan966x_dump_vlan(void)
{
    u32 vlantidx, vlanaccess;
    int i, cnt = 0;
    for (i = 0; i < 4096; i++) {
        wr("ANA_VLANTIDX.V_INDEX", i);
        wr("ANA_VLANACCESS.VLAN_TBL_CMD", 1); // Read
        do {
            us(1000);
        } while (rd("ANA_VLANACCESS.VLAN_TBL_CMD"));
        vlantidx = rd("ANA_VLANTIDX");
        vlanaccess = rd("ANA_VLANACCESS");
        if (vlanaccess || (vlantidx & ~GENMASK(11,0))) {
            if (!cnt) {
                pr("VID  VLANTIDX   VLANACCESS\n");
            }
            pr("%4d 0x%08x 0x%08x\n",
               i, vlantidx, vlanaccess);
            cnt++;
        }
    }
    if (!cnt) {
        pr("VLAN table is empty!\n");
    }
}

static char* spr(const char *fmt, ...)
{
    static char buf[256];
    va_list args;
    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    va_end(args);
    return buf;
}

static void lan966x_vcap_read(u32 inst, u32 row)
{
    wr(spr("VCAP_UPDATE_CTRL[%u].UPDATE_CMD", inst), 1); /* read command */
    wr(spr("VCAP_UPDATE_CTRL[%u].UPDATE_ADDR", inst), row);
    wr(spr("VCAP_UPDATE_CTRL[%u].UPDATE_SHOT", inst), 1);

    do {
        us(1000);
    } while (rd(spr("VCAP_UPDATE_CTRL[%u].UPDATE_SHOT", inst)));
}

#define SR_LAN966X_VCAP_MAX_ENTRY_WIDTH 12 /* Max entry width (32bit words) */
#define SR_LAN966X_VCAP_MAX_ACTION_WIDTH 12 /* Max action width (32bit words) */
#define SR_LAN966X_VCAP_MAX_COUNTER_WIDTH 4 /* Max counter width (32bit words) */
struct sr_lan966x_vcap_data {
    u32 ent[SR_LAN966X_VCAP_MAX_ENTRY_WIDTH]; /* ENTRY_DAT */
    u32 msk[SR_LAN966X_VCAP_MAX_ENTRY_WIDTH]; /* MASK_DAT */
    u32 act[SR_LAN966X_VCAP_MAX_ACTION_WIDTH];  /* ACTION_DAT */
    u32 cnt[SR_LAN966X_VCAP_MAX_COUNTER_WIDTH]; /* CNT_DAT */
    u32 tg;                                     /* TG_DAT */
};

#define ENTRY_DAT  0x01
#define MASK_DAT   0x02
#define ACTION_DAT 0x04
#define CNT_DAT    0x08
#define TG_DAT     0x10

static const char *vcap_name[] = {"ES0", "IS1", "IS2"};

static u32 sr_lan966x_read_data(u32 inst, u32 row,
                                struct sr_lan966x_vcap_data *data)
{
    u32 val, mask = 0;
    int i;

    lan966x_vcap_read(inst, row);

    for (i = 0; i < SR_LAN966X_VCAP_MAX_ENTRY_WIDTH; i++) {
        if ((val = rd(spr("VCAP_ENTRY_DAT[%u,%u]", inst, i)))) {
            mask |= ENTRY_DAT;
        }
        data->ent[i] = val;
        if ((val = rd(spr("VCAP_MASK_DAT[%u,%u]", inst, i)))) {
            mask |= MASK_DAT;
        }
        data->msk[i] = ~val; /* Invert mask! */
    }
    for (i = 0; i < SR_LAN966X_VCAP_MAX_ACTION_WIDTH; i++) {
        if ((val = rd(spr("VCAP_ACTION_DAT[%u,%u]", inst, i)))) {
            data->act[i] = val;
            mask |= ACTION_DAT;
        }
    }
    for (i = 0; i < SR_LAN966X_VCAP_MAX_COUNTER_WIDTH; i++) {
        if ((val = rd(spr("VCAP_CNT_DAT[%u,%u]", inst, i)))) {
            mask |= CNT_DAT;
        }
        data->cnt[i] = val;
    }
    if ((val = rd(spr("VCAP_TG_DAT[%u]", inst)))) {
        mask |= TG_DAT;
    }
    data->tg = val;
    return mask;
}

static void sr_lan966x_dump_data(u32 mask,
                                 struct sr_lan966x_vcap_data *data)
{
    const int last_ent = SR_LAN966X_VCAP_MAX_ENTRY_WIDTH - 1;
    const int last_act = SR_LAN966X_VCAP_MAX_ACTION_WIDTH - 1;
    const int last_cnt = SR_LAN966X_VCAP_MAX_COUNTER_WIDTH - 1;
    int i;
    pr("%-3s: ", "ENT");
    if (mask & ENTRY_DAT) {
        for (i = 0; i < SR_LAN966X_VCAP_MAX_ENTRY_WIDTH; i++) {
            pr("%s%08x", (i == 0) ? "" : ".", data->ent[last_ent - i]);
        }
    } else {
        pr("All zeros!");
    }
    pr("\n%-3s: ", "MSK");
    if (mask & MASK_DAT) {
        for (i = 0; i < SR_LAN966X_VCAP_MAX_ENTRY_WIDTH; i++) {
            pr("%s%08x", (i == 0) ? "" : ".", data->msk[last_ent - i]);
        }
    } else {
        pr("All zeros!");
    }
    pr("\n%-3s: ", "ACT");
    if (mask & ACTION_DAT) {
        for (i = 0; i < SR_LAN966X_VCAP_MAX_ACTION_WIDTH; i++) {
            pr("%s%08x", (i == 0) ? "" : ".", data->act[last_ent - i]);
        }
    } else {
        pr("All zeros!");
    }
    pr("\n%-3s: ", "CNT");
    if (mask & CNT_DAT) {
        for (i = 0; i < SR_LAN966X_VCAP_MAX_COUNTER_WIDTH; i++) {
            pr("%s%08x", (i == 0) ? "" : ".", data->cnt[last_cnt - i]);
        }
    } else {
        pr("All zeros!");
    }
    pr("\n%-3s: ", "TG");
    if (mask & TG_DAT) {
        pr("%08x\n", data->tg);
    } else {
        pr("All zeros!\n");
    }
}

static void sr_lan966x_dump_vcap(u32 inst)
{
    u32 ver, r, row, num_rows, mask;
    u32 num_inst = sizeof(vcap_name)/sizeof(vcap_name[0]);
    struct sr_lan966x_vcap_data data = {};

    if (inst >= num_inst) {
        pr("Invalid instance %u\n", inst);
        return;
    }

    ver = rd(spr("VCAP_VER[%u]", inst));
    num_rows = rd(spr("VCAP_ENTRY_CNT[%u]", inst));

    for (r = 0; r < num_rows; r++) {
        row = num_rows - r - 1; /* Reverse row */
        mask = sr_lan966x_read_data(inst, row, &data);
        if (mask) {
            pr("%s version %u row %u (VCAP internal row %u):\n",
               vcap_name[inst], ver, r, row);
            sr_lan966x_dump_data(mask, &data);
        } else {
            pr("%s version %u row %u: All zeros!\n", vcap_name[inst], ver, row);
        }
    }
}

const sr_ops_t sr_lan966x_ops = {
    .name      = "lan966x",
    .wait      = sr_lan966x_wait,
    .signal    = sr_lan966x_signal,
    .dump_mac  = sr_lan966x_dump_mac,
    .dump_vlan = sr_lan966x_dump_vlan,
    .dump_vcap = sr_lan966x_dump_vcap,
    .io_init   = sr_lan966x_init,
};

/* vim: set ts=4 sw=4 sts=4 tw=120 cc=80,120 et ft=c cino=(0,w1,Ws,t0,\:s,l1 :*/
