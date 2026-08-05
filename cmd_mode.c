/*
 * CLI mode system — Arista EOS-like hierarchical command modes
 * Compiled only in FULL builds (Makefile FULL=1, -DFULL_CLI).
 */
#include "cmd_parser.h"
#include "rtl837x_common.h"
#include "machine.h"

#pragma codeseg BANK3
#pragma constseg BANK3

/* Mode definitions are in cmd_parser.h; cli_mode lives in cmd_parser.c
 * (shared with the Lite build where it is used for API privilege
 * separation only). */

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

/* ── Mode permission check is in cmd_parser.c (BANK2) ── */
