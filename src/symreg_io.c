// SPDX-License-Identifier: MIT
/* Copyright (c) 2026 Microchip Technology Inc. and its subsidiaries.
 * Microchip switch symbolic read tool
 *
 * License: MIT
 * Copyright (c) 2026 Microchip Technology Inc.
 */

#include "symreg.h"
#include "symreg_spec.h"

#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/un.h>

#define SR_IO_NUM_REGIONS       10

static int io_fds;

int sr_io_close(sr_io_t *io)
{
    int rc = 0;
    int i = 0;

    if (io) {
        close(io_fds);
        free(io);
    }
    return rc;
}

static ulong sr_io_fd_addr(sr_io_t *io, int region, ulong addr)
{
    return addr;
}

static u32 sr_io_fd_read(sr_io_t *io, int region, ulong addr)
{
    unsigned char tmp[4] = { 0 };
    ssize_t rd;
    /* printf("fd_read region[%d]: 0x%lx\n", region, addr); */

    rd = pread(io_fds, tmp, sizeof(tmp), addr);
    if (rd != sizeof(tmp)) {
        printf("fd_read err: %ld\n", rd);
    }

    return (u32)tmp[3] << 24 |
           (u32)tmp[2] << 16 |
           (u32)tmp[1] <<  8 |
           (u32)tmp[0];
}

 static void sr_io_fd_write(sr_io_t *io, int region, ulong addr, u32 value)
{
    unsigned char val[4];
    ssize_t wr;
    /* printf("fd_write region[%d]: 0x%lx value=%u\n", region, addr, value); */

    val[0] = value >> 0;
    val[1] = value >> 8;
    val[2] = value >> 16;
    val[3] = value >> 24;

    wr = pwrite(io_fds, val, sizeof(val), addr);
    if (wr != sizeof(val)) {
        printf("fd_read err: %ld\n", wr);
    }
}


sr_io_t *sr_io_fd_init(char *path)
{
    sr_io_t *io;

    io_fds = open(path, O_RDWR);
    if (io_fds < 1) {
        fprintf(stderr, "Could not open: %s\n", path);
        exit(EXIT_FAILURE);
    }

    io = sr_zalloc(sizeof(sr_io_t));

    io->read = sr_io_fd_read;
    io->write = sr_io_fd_write;
    io->addr = sr_io_fd_addr;

    return io;
}

/* vim: set ts=4 sw=4 sts=4 tw=120 cc=80,120 et ft=c cino=(0,w1,Ws,t0,\:s,l1 :*/
