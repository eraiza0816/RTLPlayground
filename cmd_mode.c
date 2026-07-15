/*
 * CLI mode system — Arista EOS-like hierarchical command modes
 */
#include "cmd_parser.h"
#include "rtl837x_common.h"
#include "machine.h"

#pragma codeseg BANK3
#pragma constseg BANK3

/* Mode definitions are in cmd_parser.h */

/* Current mode (initialized to EXEC by BSS init) */
__xdata uint8_t cli_mode;

/* Context for sub-modes */
__xdata uint8_t cli_context_port;
__xdata uint16_t cli_context_vlan;

/* ── Mode transition commands ── */

void parse_enable(void) __banked
{
    if (cli_mode == MODE_EXEC)
        cli_mode = MODE_PRIVILEGED;
}

void parse_disable(void) __banked
{
    cli_mode = MODE_EXEC;
}

void parse_configure_terminal(void) __banked
{
    if (cli_mode == MODE_PRIVILEGED)
        cli_mode = MODE_CONFIG;
    else
        print_string("Not in privileged mode (use 'enable' first)\n");
}

void parse_exit(void) __banked
{
    switch (cli_mode) {
    case MODE_CONFIG_IF:
    case MODE_CONFIG_VLAN:
        cli_mode = MODE_CONFIG; break;
    case MODE_CONFIG:
        cli_mode = MODE_PRIVILEGED; break;
    case MODE_PRIVILEGED:
        cli_mode = MODE_EXEC; break;
    }
}

void parse_end(void) __banked
{
    if (cli_mode >= MODE_CONFIG)
        cli_mode = MODE_PRIVILEGED;
}

/* ── Mode permission check ── */

struct mode_entry {
    char __code *name;
    uint8_t mode_mask;
};

__code struct mode_entry mode_allow[] = {
    {"reset",       (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"sfp",         (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"stat",        (1<<MODE_EXEC)|(1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"flash",       (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"sds",         (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"gpio",        (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"regget",      (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"regset",      (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"sdsget",      (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"sdsset",      (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"phyget",      (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"physet",      (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"rnd",         (1<<MODE_EXEC)|(1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"version",     (1<<MODE_EXEC)|(1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"time",        (1<<MODE_EXEC)|(1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"history",     (1<<MODE_EXEC)|(1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"port",        (1<<MODE_CONFIG)|(1<<MODE_CONFIG_IF)},
    {"mtu",         (1<<MODE_CONFIG)},
    {"ip",          (1<<MODE_CONFIG)},
    {"gw",          (1<<MODE_CONFIG)},
    {"netmask",     (1<<MODE_CONFIG)},
    {"vlan",        (1<<MODE_CONFIG)},
    {"pvid",        (1<<MODE_CONFIG)},
    {"ingress",     (1<<MODE_CONFIG)},
    {"isolate",     (1<<MODE_CONFIG)},
    {"mirror",      (1<<MODE_CONFIG)},
    {"lag",         (1<<MODE_CONFIG)},
    {"laghash",     (1<<MODE_CONFIG)},
    {"stp",         (1<<MODE_CONFIG)},
    {"igmp",        (1<<MODE_CONFIG)},
    {"hostname",    (1<<MODE_CONFIG)},
    {"eee",         (1<<MODE_CONFIG)},
    {"bw",          (1<<MODE_CONFIG)},
    {"passwd",      (1<<MODE_CONFIG)},
    {"telnet",      (1<<MODE_CONFIG)},
    {"web",         (1<<MODE_CONFIG)},
    {"show",        (1<<MODE_EXEC)|(1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"commit",      (1<<MODE_PRIVILEGED)},
#ifdef NO_WEB
    {"xmodem",      (1<<MODE_PRIVILEGED)},
#endif
    {0, 0}
};

uint8_t cmd_mode_allowed(uint8_t start) __banked
{
    struct mode_entry __code *e = mode_allow;
    for (; e->name; e++) {
        uint8_t i = 0;
        while (e->name[i] && cmd_buffer[start + i] == e->name[i])
            i++;
        if (e->name[i] == '\0') {
            uint8_t n = cmd_buffer[start + i];
            if (n != '\0' && n != ' ')
                continue;
            return (e->mode_mask >> cli_mode) & 1;
        }
    }
    return 0;
}
