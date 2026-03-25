// Copyright (c) 2004-2025 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#define LAN9645X_DEBUG_PATH "/sys/kernel/debug/lan9645x/mem"
#define DT_COMPATIBLE_PATH  "/proc/device-tree/compatible"
#define PCI_DEVICES_PATH    "/sys/bus/pci/devices"

static const char *detect_lan9645x(void)
{
    if (access(LAN9645X_DEBUG_PATH, F_OK) == 0)
        return "lan9645x";
    return NULL;
}

static const char *detect_device_tree(void)
{
    char buf[4096];
    FILE *f;
    ssize_t n;

    f = fopen(DT_COMPATIBLE_PATH, "r");
    if (!f)
        return NULL;

    // compatible is a concatenation of NUL-terminated strings
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n <= 0)
        return NULL;
    buf[n] = '\0';

    // Walk each NUL-terminated string in the buffer
    for (const char *p = buf; p < buf + n; p += strlen(p) + 1) {
        if (strstr(p, "microchip,lan966"))
            return "lan966x";
        if (strstr(p, "microchip,sparx5"))
            return "sparx5";
        if (strstr(p, "microchip,lan9691"))
            return "lan969x";
    }

    return NULL;
}

static int read_pci_hex(const char *path)
{
    char buf[32];
    FILE *f;

    f = fopen(path, "r");
    if (!f)
        return -1;

    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    return (int)strtol(buf, NULL, 16);
}

static const char *detect_pcie(void)
{
    DIR *dir;
    struct dirent *ent;

    dir = opendir(PCI_DEVICES_PATH);
    if (!dir)
        return NULL;

    while ((ent = readdir(dir)) != NULL) {
        char path[512];
        int vendor, device;

        if (ent->d_name[0] == '.')
            continue;

        snprintf(path, sizeof(path), "%s/%s/vendor",
                 PCI_DEVICES_PATH, ent->d_name);
        vendor = read_pci_hex(path);
        if (vendor < 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s/device",
                 PCI_DEVICES_PATH, ent->d_name);
        device = read_pci_hex(path);
        if (device < 0)
            continue;

        if (vendor == 0x1055 && device == 0x9660) {
            closedir(dir);
            return "lan966x";
        }
        if (vendor == 0x1055 && device == 0x9690) {
            closedir(dir);
            return "lan969x";
        }
        if (vendor == 0x101b && device == 0xb006) {
            closedir(dir);
            return "sparx5";
        }
    }

    closedir(dir);
    return NULL;
}

static const char *auto_detect(void)
{
    const char *soc;

    soc = detect_lan9645x();
    if (soc)
        return soc;

    soc = detect_device_tree();
    if (soc)
        return soc;

    soc = detect_pcie();
    if (soc)
        return soc;

    return NULL;
}

int main(int argc, char *argv[])
{
    const char *soc = NULL;
    char binary[64];
    int i, fwd_argc;
    char **fwd_argv;

    // Check for --chip override (consume it before forwarding)
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--chip") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "symreg: --chip requires an argument\n");
                return 1;
            }
            soc = argv[i + 1];
            // Build forwarded argv without --chip and its argument
            fwd_argc = argc - 2;
            fwd_argv = malloc((fwd_argc + 1) * sizeof(char *));
            if (!fwd_argv) {
                perror("malloc");
                return 1;
            }
            fwd_argv[0] = argv[0];
            memcpy(&fwd_argv[1], &argv[1],
                   (i - 1) * sizeof(char *));
            memcpy(&fwd_argv[i], &argv[i + 2],
                   (argc - i - 2) * sizeof(char *));
            fwd_argv[fwd_argc] = NULL;
            goto dispatch;
        }
    }

    // No --chip: auto-detect
    soc = auto_detect();
    if (!soc) {
        fprintf(stderr, "symreg: unable to detect SoC type\n");
        return 1;
    }

    fwd_argc = argc;
    fwd_argv = argv;

dispatch:
    snprintf(binary, sizeof(binary), "symreg_%s", soc);

    // Update argv[0] so the target sees its own name
    fwd_argv[0] = binary;

    execvp(binary, fwd_argv);
    // execvp only returns on error
    perror(binary);
    return 1;
}
