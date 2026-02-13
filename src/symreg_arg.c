// SPDX-License-Identifier: MIT
/* Copyright (c) 2026 Microchip Technology Inc. and its subsidiaries.
 * Microchip switch symbolic read tool
 *
 * License: MIT
 * Copyright (c) 2026 Microchip Technology Inc.
 */

#include "symreg.h"

#include <argp.h>
#include <argz.h>

static int argp_parse_opt(int key, char *arg, struct argp_state *s)
{
    sr_arg_t *a = s->input;
    switch (key) {
        case 'r':
            a->raw = true;
            break;
        case 'a':
            a->address = true;
            break;
        case 'c': {
            u32 val;
            int rc = sr_str2u32(arg, &val);
            if (rc) {
                argp_error(s, "Parameter to --vcap must be a number");
            }
            a->vcap = true;
            a->vcap_inst = val;
            break;
        }
        case 'b': {
            u32 val;
            int rc = sr_str2u32(arg, &val);
            if (rc) {
                argp_error(s, "Parameter to --base must be a number");
            }
            if ((val == 2) || (val == 10) || (val == 16)) {
                a->base = val;
            } else {
                argp_error(s, "BASE must be 2, 10 or 16");
            }
            break;
        }
        case 'f':
            a->force = true;
            break;
        case 'n':
            a->nosem = true;
            break;
        case 'm':
            a->mac = true;
            break;
        case 'v':
            a->vlan = true;
            break;
        case 's':
            a->stream = true;
            break;
        case 'd':
            a->dd = true;
            break;
        case ARGP_KEY_INIT:
            a->argz = 0;
            a->argz_len = 0;
            break;
        case ARGP_KEY_ARG: {
            error_t err = argz_add(&a->argz, &a->argz_len, arg);
            if (err) {
                argp_failure(s, 1, err, "No memory");
            }
            break;
        }
        case ARGP_KEY_END: {
            size_t count = argz_count(a->argz, a->argz_len);
            if (!(count || a->mac || a->vlan || a->vcap || a->stream || a->dd)) {
                argp_error(s, "Missing arguments");
            }
            break;
        }
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

int sr_arg_parse(int argc, char **argv, sr_arg_t *arg)
{
    struct argp_option argp_opt[] = {
        { "address", 'a',      0, 0, "Show actual addresses"},
        { "base",    'b', "BASE", 0, "Base to use in output: 2, 10 or 16 (default)"},
        { "force",   'f',      0, 0, "Force write using wildcards without warning"},
        { "nosem",   'n',      0, 0, "Bypass semaphore"},
        { "vcap",    'c', "INST", 0, "Dump VCAP table"},
        { "mac",     'm',      0, 0, "Dump MAC table"},
        { "vlan",    'v',      0, 0, "Dump VLAN table"},
        { "stream",  's',      0, 0, "Dump STREAM table"},
        { "dd",      'd',      0, 0, "Dump Duplicate Discard table"},
        { "raw",     'r',      0, 0, "Read/write raw mode"},
        { 0 }
    };

    struct argp argp = {
        argp_opt, argp_parse_opt, "REG [VAL] REG [VAL]...",
        "Read or write one or more register value(s).\v"
        "If VAL is present it is a write otherwise a read.\n"
        "Example:\n"
        " ANA_VCAP_S1_CFG[2,1] 0x2           # register write\n"
        " ANA_VCAP_S1_CFG[2,1]               # register read\n\n"
        " ANA_VCAP_S1_CFG[2,1].KEY_IP4_CFG 1 # field write\n"
        " ANA_VCAP_S1_CFG[2,1].KEY_IP6_CFG   # field read\n"
        "\n"
        "Use * as wildcard\n"
        "Example:\n"
        " '*'  # read all registers. Most shells requires \\* or '*' to prevent globbing\n"
        " ANA* # read all registers starting with ANA"
    };

    return argp_parse(&argp, argc, argv, 0, 0, arg);
}

/* vim: set ts=4 sw=4 sts=4 tw=120 cc=80,120 et ft=c cino=(0,w1,Ws,t0,\:s,l1 :*/
