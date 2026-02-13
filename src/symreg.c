// SPDX-License-Identifier: MIT
/* Copyright (c) 2026 Microchip Technology Inc. and its subsidiaries.
 * Microchip switch symbolic read tool
 *
 * License: MIT
 * Copyright (c) 2026 Microchip Technology Inc.
 */

#include "symreg.h"

static const sr_ops_t *sr_ops; /* Define before including symreg_spec_*.h */

#include "symreg_spec.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <argz.h>
#include <regex.h>

static sr_arg_t a;
static sr_io_t *io;

/* Generic buffer size */
#define BUF_SIZE 512

static int sr_sem_wait(void)
{
    int sem = 0;
    if (sr_ops->wait)
        sem = sr_ops->wait();
    else
        printf("WARNING: sr_sem_wait() not implemented!\n");
    return sem;
}

static void sr_sem_signal(void)
{
    if (sr_ops->signal)
        sr_ops->signal();
    else
        printf("WARNING: sr_sem_signal() not implemented!\n");
}

static void sr_dump_mac(void)
{
    if (sr_ops->dump_mac)
        sr_ops->dump_mac();
    else
        printf("WARNING: sr_dump_mac() not implemented!\n");
}

static void sr_dump_vlan(void)
{
    if (sr_ops->dump_vlan)
        sr_ops->dump_vlan();
    else
        printf("WARNING: sr_dump_mac() not implemented!\n");
}

static void sr_dump_stream(void)
{
    if (sr_ops->dump_stream)
        sr_ops->dump_stream();
    else
        printf("WARNING: sr_dump_stream() not implemented!\n");
}

static void sr_dump_dd(void)
{
    if (sr_ops->dump_dd)
        sr_ops->dump_dd();
    else
        printf("WARNING: sr_dump_dd() not implemented!\n");
}

static void sr_dump_vcap(u32 instance)
{
    if (sr_ops->dump_vcap)
        sr_ops->dump_vcap(instance);
    else
        printf("WARNING: sr_dump_vcap() not implemented!\n");
}

static void sr_reg_write(sr_region_t region, ulong addr, u32 value)
{
    io->write(io, region, addr, value);
}

static u32 sr_reg_read(sr_region_t region, ulong addr)
{
    return io->read(io, region, addr);
}

static ulong sr_reg_addr(sr_region_t region, ulong addr)
{
    return io->addr(io, region, addr);
}

static void sr_reg_rmw(sr_region_t region, ulong addr, u32 value, u32 mask)
{
    u32 val = sr_reg_read(region, addr);
    val = ((val & ~mask) | (value & mask));
    sr_reg_write(region, addr, val);
}

static void sr_fld_write(sr_region_t region, ulong addr, u32 value, uint width, uint pos)
{
    uint high_pos = width + pos - 1;
    sr_reg_rmw(region, addr, SR_FLD_SET(value, high_pos, pos), SR_FLD_MASK(high_pos, pos));
}

static u32 sr_fld_read(sr_region_t region, ulong addr, uint width, uint pos)
{
    uint high_pos = width + pos - 1;
    return SR_FLD_GET(sr_reg_read(region, addr), high_pos, pos);
}

typedef struct {
    int first; /* -1 if all instances - otherwise first instance */
    int last; /* if greater than first then instances first..last, otherwise first instance only */
} sr_inst_range_t;

typedef enum {
    SR_CMD_OP_READ,
    SR_CMD_OP_WRITE
} sr_cmd_op_t;

typedef struct {
    char *reg;
    char *fld;
    sr_inst_range_t rng[SR_REG_ADDR_COMP_LAST];
    sr_cmd_op_t op;
    u32 value; /* value if op == SR_CMD_OP_WRITE */
} sr_cmd_info_t;

static void sr_cmd_info_t_init(sr_cmd_info_t *ci)
{
    int i;
    for (i = 0; i < SR_REG_ADDR_COMP_LAST; i++) {
        ci->rng[i].first = -1;
        ci->rng[i].last = -1;
    }
    ci->reg = NULL;
    ci->fld = NULL;
}

static void sr_cmd_info_t_dump(const sr_cmd_info_t *ci)
{
    int i;
    printf("reg: %s ", ci->reg);
    if (ci->fld) {
        printf("fld: %s ", ci->fld);
    }
    for (i = 0; i < SR_REG_ADDR_COMP_LAST; i++) {
        printf("rng[%d]: %d,%d ", i, ci->rng[i].first, ci->rng[i].last);
    }
    if (ci->op == SR_CMD_OP_READ) {
        printf("read\n");
    } else {
        printf("write 0x%08x\n", ci->value);
    }
}

static void sr_cmd_info_t_free(sr_cmd_info_t *ci)
{
    free(ci->reg);
    free(ci->fld);
}

static ulong sr_reg2addr(const sr_reg_spec_t *r,
                         sr_region_t *region,
                         u32 tgt_inst, u32 grp_inst, u32 reg_inst)
{
    ulong addr = 0;

    if (tgt_inst >= r->ra[SR_REG_ADDR_COMP_TGT].cnt) {
        fprintf(stderr, "ERROR: %s: invalid tgt instance %d!\n", r->name, tgt_inst);
        exit(EXIT_FAILURE);
    }
    if (grp_inst >= r->ra[SR_REG_ADDR_COMP_GRP].cnt) {
        fprintf(stderr, "ERROR: %s: invalid grp instance %d!\n", r->name, grp_inst);
        exit(EXIT_FAILURE);
    }
    if (reg_inst >= r->ra[SR_REG_ADDR_COMP_REG].cnt) {
        fprintf(stderr, "ERROR: %s: invalid reg instance %d!\n", r->name, reg_inst);
        exit(EXIT_FAILURE);
    }

    /* Region */
    *region = sr_tgt_offsets[r->tgt_offset + tgt_inst].region;

#if !defined(__i386__) && !defined(__x86_64__) && defined(USE_ONE_UIO_REGION)
    /* Add chip range offsets */
    addr += sr_tgt_chip_offsets[*region];
#endif

    /* Target part */
    /* We are not using target addr amd width to calculate
     * target offset but instead an explicit tgt_offset per target.
     */
    addr += sr_tgt_offsets[r->tgt_offset + tgt_inst].offset;

    /* Group part */
    addr += r->ra[SR_REG_ADDR_COMP_GRP].addr + (r->ra[SR_REG_ADDR_COMP_GRP].width * grp_inst);

    /* Register part */
    addr += r->ra[SR_REG_ADDR_COMP_REG].addr + (r->ra[SR_REG_ADDR_COMP_REG].width * reg_inst);

    return addr;
}

/* If exact, regex must start with '^' and end with '$'.
 * If match_str contains a '*', it must be prepended with '.'
 */
static char *sr_make_regex(const char *match_str, char *buf, int len, bool exact)
{
    int i, j = 0, l = strlen(match_str);

    assert(((l/2) - 2) < len); /* worstcase */

    if (exact) {
        buf[j++] = '^';
    }
    for (i = 0; i < l; i++) {
        if (match_str[i] == '*') {
            buf[j++] = '.';
        }
        buf[j++] = match_str[i];
    }
    if (exact) {
        buf[j++] = '$';
    }
    buf[j] = '\0';
    return buf;
}

typedef struct {
    const sr_reg_spec_t *reg;
    const sr_fld_spec_t *fld;
    sr_inst_range_t rng[SR_REG_ADDR_COMP_LAST];
    sr_cmd_op_t op;
    u32 value; /* value if op == SR_CMD_OP_WRITE */
} sr_reg_info_t;

static void sr_reg_info_t_dump(const sr_reg_info_t *ri)
{
    int i;
    if (ri->reg && ri->reg->name) {
        printf("reg: %s ", ri->reg->name);
    }
    if (ri->fld && ri->fld->name) {
        printf("fld: %s ", ri->fld->name);
    }
    for (i = 0; i < SR_REG_ADDR_COMP_LAST; i++) {
        printf("rng[%d]: %d,%d ", i, ri->rng[i].first, ri->rng[i].last);
    }
    if (ri->op == SR_CMD_OP_READ) {
        printf("read\n");
    } else {
        printf("write 0x%08x\n", ri->value);
    }
}

/* Convert the instances supplied by user to the instances needed by the register.
 *
 * Examples:
 * User writes DEV_PORT_MISC[3]
 * Register is specified as DEV[0-6]:PORT_MODE:PORT_MISC
 * Result is DEV[3]:PORT_MODE:PORT_MISC
 * Note that the user supplied parameter is used as target instance
 *
 * User writes ANA_VCAP_S1_CFG[2,1]
 * Register is specified as ANA:PORT[0-7]:VCAP_S1_KEY_CFG[0-2]
 * Result is  ANA:PORT[2]:VCAP_S1_KEY_CFG[1]
 * Note that the user supplied parameters are used as group and register instances
 */
static void sr_inst_convert(const sr_reg_spec_t *reg,
                            const sr_inst_range_t *ci,
                            sr_inst_range_t *ri)
{
    int i, current = 0;

    assert(reg && ci && ri);

    for (i = 0; i < SR_REG_ADDR_COMP_LAST; i++) {
        if (reg->ra[i].cnt > 1) {
            if (ci[current].first < 0) {
                ri[i].first = 0;
                ri[i].last = reg->ra[i].cnt - 1;
            } else {
                ri[i].first = ci[current].first;
                if (ci[current].last > ci[current].first) {
                    ri[i].last = ci[current].last;
                } else {
                    ri[i].last = ci[current].first;
                }
            }
            current++; /* parameter consumed */
        } else {
            ri[i].first = ri[i].last = 0;
        }
    }
}

typedef void (*sr_match_cb)(const sr_reg_info_t *ri, void *cb_arg);

/* Use cb = NULL to only count the matches */
static int sr_match(const sr_cmd_info_t *ci, bool exact, sr_match_cb cb, void *cb_arg)
{
    regex_t re_reg = {0}, re_fld = {0};
    int rc, i, j, cnt = 0;
    char buf[BUF_SIZE];
    sr_reg_info_t ri;

    assert(ci);

    rc = regcomp(&re_reg,
                 sr_make_regex(ci->reg, buf, BUF_SIZE, exact),
                 REG_EXTENDED | REG_ICASE | REG_NOSUB);
    if (rc) {
        regerror(rc, &re_reg, buf, BUF_SIZE);
        fprintf(stderr, "regcomp line %d:\n", __LINE__);
        goto error;
    }

    if (ci->fld) {
        rc = regcomp(&re_fld,
                     sr_make_regex(ci->fld, buf, BUF_SIZE, exact),
                     REG_EXTENDED | REG_ICASE | REG_NOSUB);
        if (rc) {
            regerror(rc, &re_fld, buf, BUF_SIZE);
            fprintf(stderr, "regcomp line %d:\n", __LINE__);
            goto error;
        }
    }

    i = 0;
    while (reg_spec[i].name) {
        ri.reg = &reg_spec[i++]; /* <- Do not use i afterwards */
        rc = regexec(&re_reg, ri.reg->name, 0, 0, 0);
        if (rc == REG_NOMATCH) {
            continue;
        }
        if (rc) {
            regerror(rc, &re_reg, buf, BUF_SIZE);
            fprintf(stderr, "regexec line %d:\n", __LINE__);
            goto error;
        }

        ri.fld = NULL;
        sr_inst_convert(ri.reg, ci->rng, ri.rng);
#if 0
        printf("Exact %d, cb %p, cb_arg %p\n", exact, cb, cb_arg);
        sr_cmd_info_t_dump(ci);
        sr_reg_info_t_dump(&ri);
#endif
        ri.op = ci->op;
        ri.value = ci->value;

        if (ci->fld) {
            if (ri.reg->flds) {
                j = 0;
                while (ri.reg->flds[j].name) {
                    ri.fld = &ri.reg->flds[j++]; /* <- Do not use j afterwards */
                    rc = regexec(&re_fld, ri.fld->name, 0, 0, 0);
                    if (rc == REG_NOMATCH) {
                        continue;
                    }
                    if (rc) {
                        regerror(rc, &re_fld, buf, BUF_SIZE);
                        fprintf(stderr, "regexec line %d:\n", __LINE__);
                        goto error;
                    }
                    if (cb) {
                        cb(&ri, cb_arg);
                    }
                    cnt++;
                }
            }
        } else {
            if (cb) {
                cb(&ri, cb_arg);
            }
            cnt++;
        }
    }

    regfree(&re_reg);
    regfree(&re_fld);
    return cnt;

error:
    fprintf(stderr, "%s!\n", buf);
    regfree(&re_reg);
    regfree(&re_fld);
    exit(EXIT_FAILURE);
}

typedef struct {
    char *addr;
    sr_cmd_op_t op;
    u32 value; /* value if op == SR_CMD_OP_WRITE */
} sr_cmd_t;

static void sr_cmd_t_free(sr_cmd_t *c)
{
    free(c->addr);
}

static void regmatch_t_dump(char *addr, char *buf, regmatch_t *rm, size_t num)
{
    int i;
    for (i = 0; i < num; i++) {
        if (rm[i].rm_so > -1) {
            sr_strslice(addr, buf, rm[i].rm_so, rm[i].rm_eo);
            printf("rm[%2d]: s %2d e %2d buf: '%s'\n", i, rm[i].rm_so, rm[i].rm_eo, buf);
        }
    }
}

static int sr_vml_insert(char *str, char *ins, int len, int pos)
{
    int l = strlen(str);
    int i;

    if (str[pos] == '[') {
        str[pos] = ',';
        len -= 1;
    }

    for (i = l + len; i > pos; --i) {
        str[i] = str[i - len];
    }

    memcpy(str + pos, ins, len);
    return len;
}

static int sr_vml_find_insert_pos(char *str)
{
    int l = strlen(str);
    int i;

    for (i = 0; i < l; ++i) {
        if (str[i] == '.' || str[i] == '[') {
            return i;
        }
    }

    return l;
}

/* Try to parse strings that contain also the groups and have also the index in
 * their target/groups. In this way it is possible to copy-paste strings from vml
 * without needing to edit the string.
 * The purpose of this function is to convert the vml string to a string that is
 * understood by symreg.
 * Maybe all this can be removed and use another string in regcomp function
 * bellow.
 */
static char *sr_vml_convert(char *addr)
{
#define VML_INDEX_LEN 100
    char t_index_str[VML_INDEX_LEN], g_index_str[VML_INDEX_LEN];
    char *t_index = NULL, *g_index = NULL, *g_pos = NULL;
    int t_index_len, g_index_len, g_pos_len;
    int len = strlen(addr);
    bool found = false;
    int i, pos;
    char *c;

    /* If there is no ':' then is the old format and there is nothing to do here
     * so just bail out
     */
    for (i = 0; i < len; ++i) {
        if (addr[i] == ':') {
            found = true;
            break;
        }
    }

    if (found == false)
        return addr;

    memset(t_index_str, 0, VML_INDEX_LEN);
    memset(g_index_str, 0, VML_INDEX_LEN);

    /* So there is a vml format */
    for (i = 0; i < len; ++i) {
        /* Try to find the position of the group */
        if (addr[i] == ':') {
            /* If it is the first char then save the starting position, otherwise
             * save the length
             */
            if (g_pos == NULL) {
                    g_pos = &addr[i];
                    continue;
            } else {
                    /* Here we can finish the processing because all the info
                     * needed is collected
                     */
                    g_pos_len = &addr[i] - g_pos;
                    break;
            }
        }

        /* This can be the start of the index for target or for group
         * but if we seen already a ':' it means that is for group otherwise
         * is for target
         */
        if (addr[i] == '[') {
            if (g_pos == NULL)
                t_index = &addr[i];
            else
                g_index = &addr[i];
        }

        if (addr[i] == ']') {
            if (g_pos == NULL) {
                t_index_len = &addr[i] - t_index + 1;
                memcpy(t_index_str, t_index, t_index_len);
            } else {
                g_index_len = &addr[i] - g_index + 1;
                memcpy(g_index_str, g_index, g_index_len);
            }
        }
    }

    /* Now it is needed to remove the group(g_pos) from the string */
    for (i = g_pos - addr; i < len; ++i)
        addr[i] = addr[i + g_pos_len];
    addr[g_pos - addr] = '_';

    addr[len - g_pos_len] = '\0';
    len = strlen(addr);

    /* First check if anything else is needed, if there is no target/group index
     * then nothing is needed so we can just bail out
     */
    if (t_index_str[0] == '\0' && g_index_str[0] == '\0')
        return addr;

    /* Now it is needed to remove the target index */
    if (t_index_str[0] != '\0') {
        for (i = t_index - &addr[0]; i < len; ++i)
            addr[i] = addr[i + t_index_len];

        addr[len - t_index_len] = '\0';
        len = strlen(addr);
    }

    /* Now the target index + group that includes also the index are removed
     * from the string, what is left is to insert the indexes at the right place
     * We start with the group because it needs to be inserted before . or [
     * because the same the target needs to be inserted
     */
    if (g_index_str[0] != '\0') {
        pos = sr_vml_find_insert_pos(addr);
        len += sr_vml_insert(addr, g_index_str, g_index_len, pos);
    }

    if (t_index_str[0] != '\0') {
        pos = sr_vml_find_insert_pos(addr);
        len += sr_vml_insert(addr, t_index_str, t_index_len, pos);
    }

    addr[len] = '\0';
    return addr;
}

static int sr_parse_cmd(const sr_cmd_t *cmd, sr_cmd_info_t *ci) {
#define SR_NUM_REG_MATCH 21 /* Calculated as 1 + number of '(' in regex */
    regmatch_t rm[SR_NUM_REG_MATCH];
    char buf[BUF_SIZE];
    regex_t re = {0};
    int i, j, rc;
    char *addr = cmd->addr;

    assert(cmd && addr && ci);
    addr = sr_vml_convert(addr);

    rc = regcomp(&re,
                 "^"                                         // start of string
                 "([[:alpha:]*][[:alnum:]*_]*)"              // reg name e.g. "ANA_VCAP_S1_CFG" or "ANA_VCAP*"
                 "(\\["                                      // optional instance parameters start
                 "(([[:digit:]]+)(-([[:digit:]]+))?)?"       // first parameter e.g. "[0]" or "[0-2]"
                 "(,((([[:digit:]]+)(-([[:digit:]]+))?))?)?" // second parameter e.g. "[0,3]" or "[0-2,3-4]"
                 "(,((([[:digit:]]+)(-([[:digit:]]+))?))?)?" // third parameter e.g. "[,3,5]" or "[,,5-6]" where empty means all instances
                 "])?"                                       // optional instance parameters end
                 "([.]([[:alpha:]*][[:alnum:]*_]*)?)?"       // optional field name e.g. ".KEY_IP6_CFG", ".KEY_IP*" or "." for all keys
                 "$",                                        // end of string
                 REG_EXTENDED);
    if (rc) {
        goto error;
    }

    rc = regexec(&re, addr, SR_NUM_REG_MATCH, rm, 0);
    if (rc == 0) {

        //regmatch_t_dump(addr, buf, rm, SR_NUM_REG_MATCH);

        i = 1;
        if (rm[i].rm_so > -1) {
            sr_strslice(addr, buf, rm[i].rm_so, rm[i].rm_eo);
            ci->reg = sr_strdup(buf);
        }
        i = 4;
        if (rm[i].rm_so > -1) {
            sr_strslice(addr, buf, rm[i].rm_so, rm[i].rm_eo);
            ci->rng[0].first = atoi(buf);
        }
        i = 6;
        if (rm[i].rm_so > -1) {
            sr_strslice(addr, buf, rm[i].rm_so, rm[i].rm_eo);
            ci->rng[0].last = atoi(buf);
        }
        i = 10;
        if (rm[i].rm_so > -1) {
            sr_strslice(addr, buf, rm[i].rm_so, rm[i].rm_eo);
            ci->rng[1].first = atoi(buf);
        }
        i = 12;
        if (rm[i].rm_so > -1) {
            sr_strslice(addr, buf, rm[i].rm_so, rm[i].rm_eo);
            ci->rng[1].last = atoi(buf);
        }
        i = 16;
        if (rm[i].rm_so > -1) {
            sr_strslice(addr, buf, rm[i].rm_so, rm[i].rm_eo);
            ci->rng[2].first = atoi(buf);
        }
        i = 18;
        if (rm[i].rm_so > -1) {
            sr_strslice(addr, buf, rm[i].rm_so, rm[i].rm_eo);
            ci->rng[2].last = atoi(buf);
        }
        i = 19;
        if (rm[i].rm_so > -1) {
            i = 20;
            if (rm[i].rm_so > -1) {
                sr_strslice(addr, buf, rm[i].rm_so, rm[i].rm_eo);
                ci->fld = sr_strdup(buf);
            } else {
                ci->fld = sr_strdup("*");
            }
        }
        ci->op = cmd->op;
        ci->value = cmd->value;
    } else if (rc != REG_NOMATCH) {
        goto error;
    }
    regfree(&re);
    return rc;

error:
    regerror(rc, &re, buf, BUF_SIZE);
    fprintf(stderr, "!%s!\n", buf);
    regfree(&re);
    exit(EXIT_FAILURE);
}

static void pr_base(u32 val, uint base)
{
    if (base == 2) {
        int i, v;
        printf("0b");
        for (i = 31; i >=0; i--) {
            v = (val >> i) & 1;
            printf("%d", v);
            if (i && (i % 4) == 0) {
                printf(".");
            }
        }
    } else if (base == 10) {
        printf("%u", val);
    } else {
        printf("0x%08x", val);
    }
}

typedef struct {
    bool show_addr;
    uint base;
    bool raw;
} sr_cmd_interactive_parm_t;

static void sr_cmd_interactive_exec_cb(const sr_reg_info_t *ri, void *cb_arg)
{
    sr_cmd_interactive_parm_t *p = cb_arg;
    char buf1[BUF_SIZE];
    char buf2[BUF_SIZE];
    int t, g, r, width;

    bool t_show = (ri->reg->ra[SR_REG_ADDR_COMP_TGT].cnt > 1);
    bool g_show = (ri->reg->ra[SR_REG_ADDR_COMP_GRP].cnt > 1);
    bool r_show = (ri->reg->ra[SR_REG_ADDR_COMP_REG].cnt > 1);

    /* Loop through all instances */
    for (t = ri->rng[SR_REG_ADDR_COMP_TGT].first;
         t <= ri->rng[SR_REG_ADDR_COMP_TGT].last;
         t++) {
        for (g = ri->rng[SR_REG_ADDR_COMP_GRP].first;
             g <= ri->rng[SR_REG_ADDR_COMP_GRP].last;
             g++) {
            for (r = ri->rng[SR_REG_ADDR_COMP_REG].first;
                 r <= ri->rng[SR_REG_ADDR_COMP_REG].last;
                 r++) {
                buf2[0] = '\0';
                if (t_show || g_show || r_show) {
                    strcpy(buf2, "[");
                    if (t_show) {
                        snprintf(buf1, BUF_SIZE, "%d", t);
                        strcat(buf2, buf1);
                    }
                    if (g_show) {
                        snprintf(buf1, BUF_SIZE, "%s%d", t_show ? "," : "", g);
                        strcat(buf2, buf1);
                    }
                    if (r_show) {
                        snprintf(buf1, BUF_SIZE, "%s%d", (t_show || g_show) ? "," : "", r);
                        strcat(buf2, buf1);
                    }
                    strcat(buf2, "]");
                }
                sr_region_t region;
                ulong addr = sr_reg2addr(ri->reg, &region, t, g, r);
                u32 rval;

                if (p->show_addr) {
                    printf("%p ", (char*)sr_reg_addr(region, addr));
                }
                if (ri->fld) {
                    (void)snprintf(buf1, BUF_SIZE, "%s%s.%s", ri->reg->name, strlen(buf2) ? buf2 : "", ri->fld->name);
                    width = SR_MAX_REG_FLD_WIDTH + 10; /* Make room for instance info e.g. [1,4,12] */
                    rval = sr_fld_read(region, addr, ri->fld->width, ri->fld->pos);
                } else {
                    if (p->raw && ri->op == SR_CMD_OP_WRITE)
                        ; /* No read if raw write operation */
                    else {
                        snprintf(buf1, BUF_SIZE, "%s%s", ri->reg->name, strlen(buf2) ? buf2 : "");
                        width = SR_MAX_REG_WIDTH + 10; /* Make room for instance info e.g. [1,4,12] */
                        rval = sr_reg_read(region, addr);
                    }
                }

                if (!p->raw) {
                    printf("%-*s = ", width, buf1);
                    pr_base(rval, p->base);
                } else {
                    if (ri->op == SR_CMD_OP_READ)
                        pr_base(rval, p->base);
                }

                if (ri->op == SR_CMD_OP_WRITE) {
                    if (ri->fld) {
                        sr_fld_write(region, addr, ri->value, ri->fld->width, ri->fld->pos);
                        rval = sr_fld_read(region, addr, ri->fld->width, ri->fld->pos);
                    } else {
                        sr_reg_write(region, addr, ri->value);
                        if (!p->raw)
                            rval = sr_reg_read(region, addr);
                    }
                    if (!p->raw) {
                        printf(" -> ");
                        pr_base(rval, p->base);
                    }
                }
                if (!p->raw) {
                    printf("\n");
                } else {
                    if (ri->op == SR_CMD_OP_READ)
                        printf("\n");
                }
            }
        }
    }
}

static void sr_cmd_interactive_hint_cb(const sr_reg_info_t *ri, void *cb_arg)
{
    if (ri->fld) {
        printf("%s.%s\n", ri->reg->name, ri->fld->name);
    } else {
        printf("%s\n", ri->reg->name);
    }
}

static int sr_cmd_interactive(const sr_cmd_t *cmd) {
    sr_cmd_interactive_parm_t p;
    sr_cmd_info_t ci;
    bool wc = false;
    int cnt, rc = 0;

    sr_cmd_info_t_init(&ci);

    if (sr_parse_cmd(cmd, &ci)) {
        printf("'%s' is not a valid target\n", cmd->addr);
        rc = 1;
        goto out;
    }

    //sr_cmd_info_t_dump(&ci);

    if ((ci.reg && strchr(ci.reg, '*')) || (ci.fld && strchr(ci.fld, '*'))) {
        wc = true;
    }

    if ((cmd->op == SR_CMD_OP_WRITE) && (a.force == false) && wc) {
        printf("Are you sure to write using wildcards?\n"
               "Press ctrl-c to terminate or enter to continue!\n");
        getchar();
        printf("You have been warned!\n");
    }

    p.show_addr = a.address;
    p.base = a.base;
    p.raw = a.raw;
    cnt = sr_match(&ci, true, sr_cmd_interactive_exec_cb, &p);
    if (cnt < 0) {
        printf("Error %d from sr_match()!\n", cnt);
    } else if (cnt == 0) {
        cnt = sr_match(&ci, false, NULL, NULL);
        if (cnt < 0) {
            printf("Error %d from sr_match()!\n", cnt);
        } else if (cnt == 0) {
            printf("No matches!\n");
        } else if ((cnt == 1) || wc) {
            sr_match(&ci, false, sr_cmd_interactive_exec_cb, &p);
        } else {
            printf("Multiple matches without using wildcard:\n");
            sr_match(&ci, false, sr_cmd_interactive_hint_cb, NULL);
        }
    }

out:
    sr_cmd_info_t_free(&ci);
    return rc;
}

static void sr_cmd_write_cb(const sr_reg_info_t *ri, void *cb_arg)
{
    /* Ignore all but the first instance */
    int t = ri->rng[SR_REG_ADDR_COMP_TGT].first;
    int g = ri->rng[SR_REG_ADDR_COMP_GRP].first;
    int r = ri->rng[SR_REG_ADDR_COMP_REG].first;
    sr_region_t region;
    ulong addr = sr_reg2addr(ri->reg, &region, t, g, r);
    if (ri->fld) {
        sr_fld_write(region, addr, ri->value, ri->fld->width, ri->fld->pos);
    } else {
        sr_reg_write(region, addr, ri->value);
    }
}

/* Write a 32 bit value into a register or field
 * Example using register:
 *  sr_cmd_write("ANA_MACLDATA", 0x1234);
 * Example using field:
 *  sr_cmd_write("ANA_MACHDATA.VID", 0x42);
 */
void sr_cmd_write(char *addr, u32 value)
{
    sr_cmd_t cmd = {0};
    sr_cmd_info_t ci;
    int cnt;

    cmd.addr = sr_strdup(addr);
    cmd.op = SR_CMD_OP_WRITE;
    cmd.value = value;

    sr_cmd_info_t_init(&ci);

    if (sr_parse_cmd(&cmd, &ci)) {
        printf("'%s' is not a valid target\n", cmd.addr);
        goto out;
    }

    if ((ci.reg && strchr(ci.reg, '*')) || (ci.fld && strchr(ci.fld, '*'))) {
        printf("Wildcards not supported!\n");
        goto out;
    }

    cnt = sr_match(&ci, true, sr_cmd_write_cb, NULL);
    if (cnt < 0) {
        printf("Error %d from sr_match()!\n", cnt);
    } else if (cnt != 1) {
        printf("Zero or multiple matches using %s!\n", cmd.addr);
    }

out:
    sr_cmd_info_t_free(&ci);
    sr_cmd_t_free(&cmd);
}

typedef struct {
    u32 value;
} sr_cmd_read_parm_t;

static void sr_cmd_read_cb(const sr_reg_info_t *ri, void *cb_arg)
{
    /* Ignore all but the first instance */
    int t = ri->rng[SR_REG_ADDR_COMP_TGT].first;
    int g = ri->rng[SR_REG_ADDR_COMP_GRP].first;
    int r = ri->rng[SR_REG_ADDR_COMP_REG].first;
    sr_region_t region;
    ulong addr = sr_reg2addr(ri->reg, &region, t, g, r);
    sr_cmd_read_parm_t *p = cb_arg;
    if (ri->fld) {
        p->value = sr_fld_read(region, addr, ri->fld->width, ri->fld->pos);
    } else {
        p->value = sr_reg_read(region, addr);
    }
}

/* Read a 32 bit value from a register or field
 * Example using register:
 *  u32 tmp = sr_cmd_read("ANA_MACLDATA");
 * Example using field:
 *  u32 tmp = sr_cmd_read("ANA_MACHDATA.VID");
 */
u32 sr_cmd_read(char *addr)
{
    sr_cmd_read_parm_t p = {0};
    sr_cmd_t cmd = {0};
    sr_cmd_info_t ci;
    int cnt;

    cmd.addr = sr_strdup(addr);
    cmd.op = SR_CMD_OP_READ;

    sr_cmd_info_t_init(&ci);

    if (sr_parse_cmd(&cmd, &ci)) {
        printf("'%s' is not a valid target\n", cmd.addr);
        goto out;
    }

    if ((ci.reg && strchr(ci.reg, '*')) || (ci.fld && strchr(ci.fld, '*'))) {
        printf("Wildcards not supported!\n");
        goto out;
    }

    cnt = sr_match(&ci, true, sr_cmd_read_cb, &p);
    if (cnt < 0) {
        printf("Error %d from sr_match()!\n", cnt);
    } else if (cnt != 1) {
        printf("Zero or multiple matches using %s!\n", cmd.addr);
    }

out:
    sr_cmd_info_t_free(&ci);
    sr_cmd_t_free(&cmd);
    return p.value;
}

/* sr_parse_args parses all arguments except options.
 *
 * Valid arguments are one of more of:
 * REG_SPEC [WRITE_VALUE]
 *
 * If WRITE_VALUE is present it is a write otherwise a read.
 *
 * Example:
 * ANA_VCAP_S1_CFG[2,1].KEY_IP4_CFG 1 # field write
 * ANA_VCAP_S1_CFG[2,1].KEY_IP6_CFG   # field read
 * ANA_VCAP_S1_CFG[2,1] 0x2           # register write
 * ANA_VCAP_S1_CFG[2,1]               # register read
 */
static int sr_parse_args(char *argz, size_t argz_len)
{
#define SR_CMD_MAX 10
    sr_cmd_t cmds[SR_CMD_MAX] = {0}; /* Consider using malloc()/realloc() */
    bool accept_number = false;
    int rc, cmd, cmd_ix = 0;
    char *arg = 0;
    u32 value;

    while ((arg = argz_next(argz, argz_len, arg))) {
        rc = sr_str2u32(arg, &value);
        switch (rc) {
            case -3:
                if (accept_number) {
                    fprintf(stderr, "%s is not a valid number!\n", arg);
                } else {
                    fprintf(stderr, "%s is not a valid register specification!\n", arg);
                }
                exit(EXIT_FAILURE);
            case -2:
                if (accept_number) {
                    fprintf(stderr, "%s is out of range!\n", arg);
                } else {
                    fprintf(stderr, "%s is not a valid register specification!\n", arg);
                }
                exit(EXIT_FAILURE);
            case -1: /* we got an addr - save it and start a new entry */
                if (cmd_ix == SR_CMD_MAX) {
                    fprintf(stderr, "Only up to %d commands supported!\n", SR_CMD_MAX);
                    exit(EXIT_FAILURE);
                }
                cmds[cmd_ix].addr = sr_strdup(arg);
                accept_number = true;
                cmd_ix++;
                break;
            case 0: /* we got a number - save it in previous entry */
                if (accept_number) {
                    cmds[cmd_ix - 1].op = SR_CMD_OP_WRITE;
                    cmds[cmd_ix - 1].value = value;
                    accept_number = false;
                } else {
                    fprintf(stderr, "%s is not a valid register specification!\n", arg);
                    exit(EXIT_FAILURE);
                }
                break;
            default:
                fprintf(stderr, "Invalid return code %d!\n", rc);
                exit(EXIT_FAILURE);
        }
    }

    rc = 0;
    for (cmd = 0; cmd < cmd_ix; cmd++) {
        rc = sr_cmd_interactive(&cmds[cmd]);
        sr_cmd_t_free(&cmds[cmd]);
        if (rc) {
            break;
        }
    }
    return rc;
}

static void bye(void)
{
    if (io) {
        if (a.nosem == false) {
            sr_sem_signal();
        }
        sr_io_close(io);
    }
    free(a.argz);
}

int main(int argc, char **argv) {
    int rc;
    u32 bid;

    if (sr_ops == NULL) {
        fprintf(stderr, "No user defined operations!\n");
        exit(EXIT_FAILURE);
    }

    rc = atexit(bye);
    if (rc) {
        fprintf(stderr, "Cannot set exit function!\n");
        exit(EXIT_FAILURE);
    }

    a.raw = false;
    rc = sr_arg_parse(argc, argv, &a);
    if (rc) {
        fprintf(stderr, "Error %d from sr_arg_parse()!\n", rc);
        exit(EXIT_FAILURE);
    }

    if (sr_ops->io_init)
        io = sr_ops->io_init();
    else {
        fprintf(stderr, "No symreg initialization\n");
        exit(EXIT_FAILURE);
    }

    if (a.nosem == false) {
        rc = sr_sem_wait();
        if (rc) {
            fprintf(stderr, "Unable to get semaphore!\n");
            exit(EXIT_FAILURE);
        }
    }

    if (a.mac || a.vlan || a.vcap || a.stream || a.dd) {
        if (a.mac) {
            sr_dump_mac();
        }
        if (a.vlan) {
            sr_dump_vlan();
        }
        if (a.stream) {
            sr_dump_stream();
        }
        if (a.dd) {
            sr_dump_dd();
        }
        if (a.vcap) {
            sr_dump_vcap(a.vcap_inst);
        }
    } else {
        rc = sr_parse_args(a.argz, a.argz_len);
    }
    return rc;
}
/* vim: set ts=4 sw=4 sts=4 tw=120 cc=80,120 et ft=c cino=(0,w1,Ws,t0,\:s,l1 :*/

