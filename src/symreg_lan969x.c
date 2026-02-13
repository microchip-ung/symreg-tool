// SPDX-License-Identifier: MIT
/* Copyright (c) 2026 Microchip Technology Inc. and its subsidiaries.
 * Microchip switch symbolic read tool
 *
 * License: MIT
 * Copyright (c) 2026 Microchip Technology Inc.
 */

#include "symreg.h"

static int sr_lan969x_wait(void)
{
    return 0;
}

static void sr_lan969x_signal(void)
{
}

static sr_io_t *sr_lan969x_init(void) {
    return sr_io_fd_init("/sys/kernel/debug/symreg/mem");
}

const sr_ops_t sr_lan969x_ops = {
    .name      = "lan969x",
    .wait      = sr_lan969x_wait,
    .signal    = sr_lan969x_signal,
    .io_init   = sr_lan969x_init,
};

/* vim: set ts=4 sw=4 sts=4 tw=120 cc=80,120 et ft=c cino=(0,w1,Ws,t0,\:s,l1 :*/
