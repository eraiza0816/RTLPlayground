#include "cmd_parser.h"
#include "machine.h"

#pragma codeseg BANK3
#pragma constseg BANK3

// Accessed from cmd_editor.c
extern __xdata uint8_t cursor;
extern __xdata uint8_t cmd_line_len;

struct cmd_entry {
    char __code *name;
    char __code *desc;
};

struct cmd_group {
    char __code *name;
    char __code *desc;
    struct cmd_entry __code *subcmds;
    uint8_t mode_mask;
};

static uint8_t find_word_idx(__xdata uint8_t *buf, uint8_t cur,
                              uint8_t *word_start, uint8_t *word_len)
{
    uint8_t start = cur;
    while (start > 0 && buf[start-1] != ' ')
        start--;
    *word_start = start;
    *word_len = cur - start;

    uint8_t idx = 0;
    uint8_t i = 0;
    uint8_t in_word = 0;
    while (i < start) {
        if (buf[i] != ' ' && !in_word) {
            in_word = 1;
            idx++;
        } else if (buf[i] == ' ') {
            in_word = 0;
        }
        i++;
    }
    return idx;
}

static uint8_t is_prefix(__xdata uint8_t *buf, uint8_t start, uint8_t len, char __code *str)
{
    uint8_t i = 0;
    while (i < len) {
        if (str[i] == '\0' || buf[start + i] != str[i])
            return 0;
        i++;
    }
    return 1;
}

static uint8_t word_exact_match(__xdata uint8_t *buf, uint8_t start, uint8_t len, char __code *str)
{
    uint8_t i = 0;
    while (i < len) {
        if (str[i] == '\0' || buf[start + i] != str[i])
            return 0;
        i++;
    }
    return (str[i] == '\0');
}

static void redraw_line(void)
{
    for (uint8_t i = 0; i < cmd_line_len; i++)
        write_char(cmd_buffer[i]);
    uint8_t back = cmd_line_len - cursor;
    while (back--)
        write_char('\010');
}

__code struct cmd_entry sfp_cmds[] = {
    {"1g",        "Force 1Gbps speed"},
    {"2g5",       "Force 2.5Gbps speed"},
    {"10g",       "Force 10Gbps speed"},
    {"100m",      "Force 100Mbps speed"},
    {"auto",      "Auto-negotiate speed"},
    {"describe",  "Show detailed SFP module information"},
    {"dump",      "Hex dump of SFP EEPROM"},
    {"save",      "Save SFP EEPROM to flash backup"},
    {"restore",   "Restore SFP EEPROM from flash backup"},
    {"fix",       "Fix SFP EEPROM for copper passthrough"},
    {"patch",     "Patch SFP EEPROM"},
    {"clone",     "Clone SFP EEPROM from flash buffer"},
    {"checksum",  "Verify or fix SFP EEPROM checksums"},
    {"write",     "Write a byte to SFP EEPROM"},
    {"bulk",      "Bulk-write SFP EEPROM from hex data"},
    {0, 0}
};

__code struct cmd_entry port_cmds[] = {
    {"show",   "Show port status and PHY information"},
    {"name",   "Set custom port name"},
    {"10m",    "Force 10Mbps speed"},
    {"100m",   "Force 100Mbps speed"},
    {"1g",     "Force 1Gbps speed"},
    {"2g5",    "Force 2.5Gbps speed"},
    {"5g",     "Force 5Gbps speed"},
    {"10g",    "Force 10Gbps speed"},
    {"auto",   "Auto-negotiate speed"},
    {"on",     "Enable port"},
    {"off",    "Disable port"},
    {"duplex", "Set duplex mode (half/full)"},
    {0, 0}
};

__code struct cmd_entry vlan_cmds[] = {
    {"show", "Show all VLANs or members of a VLAN"},
    {"mgmt", "Set management VLAN ID"},
    {"d",    "Delete a VLAN"},
    {0, 0}
};

__code struct cmd_entry flash_cmds[] = {
    {"s", "Read security registers"},
    {"j", "Read JEDEC ID"},
    {"u", "Read unique ID"},
    {0, 0}
};

__code struct cmd_entry l2_cmds[] = {
    {"forget", "Flush dynamically learned L2 entries"},
    {"del",    "Delete a specific L2 entry"},
    {0, 0}
};

__code struct cmd_entry igmp_cmds[] = {
    {"on",   "Enable IGMP snooping"},
    {"off",  "Disable IGMP snooping"},
    {"show", "Show IGMP snooping status"},
    {0, 0}
};

__code struct cmd_entry stp_cmds[] = {
    {"on",  "Enable Spanning Tree Protocol"},
    {"off", "Disable Spanning Tree Protocol"},
    {0, 0}
};

__code struct cmd_entry isolate_cmds[] = {
    {"show", "Show isolation configuration"},
    {"off",  "Disable port isolation"},
    {0, 0}
};

__code struct cmd_entry ingress_cmds[] = {
    {"u", "Allow untagged frames only"},
    {"t", "Allow tagged frames only"},
    {"a", "Allow any frames"},
    {0, 0}
};

__code struct cmd_entry mirror_cmds[] = {
    {"status", "Show mirroring status"},
    {"off",    "Disable port mirroring"},
    {0, 0}
};

__code struct cmd_entry lag_cmds[] = {
    {"show", "Show LAG status and member ports"},
    {0, 0}
};

__code struct cmd_entry laghash_cmds[] = {
    {"spa",   "Source port number"},
    {"smac",  "Source MAC address"},
    {"dmac",  "Destination MAC address"},
    {"sip",   "Source IP address"},
    {"dip",   "Destination IP address"},
    {"sport", "Source TCP/UDP port"},
    {"dport", "Destination TCP/UDP port"},
    {0, 0}
};

__code struct cmd_entry eee_cmds[] = {
    {"on",     "Enable Energy Efficient Ethernet"},
    {"off",    "Disable Energy Efficient Ethernet"},
    {"status", "Show EEE status"},
    {0, 0}
};

__code struct cmd_entry bw_cmds[] = {
    {"in",     "Configure ingress bandwidth"},
    {"out",    "Configure egress bandwidth"},
    {"status", "Show bandwidth control status"},
    {0, 0}
};

__code struct cmd_entry telnet_cmds[] = {
    {"on",  "Enable telnet server"},
    {"off", "Disable telnet server"},
    {0, 0}
};

__code struct cmd_entry web_cmds[] = {
    {"on",  "Enable web interface"},
    {"off", "Disable web interface"},
    {0, 0}
};

__code struct cmd_entry mtu_cmds[] = {
    {"show", "Show MTU for all ports"},
    {0, 0}
};

__code struct cmd_group top_cmds[] = {
    {"reset",   "Perform software reset of the switch",               0, (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"sfp",     "SFP module control and configuration",               sfp_cmds, (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"stat",    "Show port statistics and packet counters",           0, (1<<MODE_EXEC)|(1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"flash",   "Read flash metadata (security/JEDEC/UID)",           flash_cmds, (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"port",    "Port speed, duplex, and name configuration",         port_cmds, (1<<MODE_CONFIG)|(1<<MODE_CONFIG_IF)},
    {"mtu",     "Per-port maximum frame size (MTU)",                  mtu_cmds, (1<<MODE_CONFIG)},
    {"ip",      "Show or set IP address, enable DHCP",                0, (1<<MODE_CONFIG)},
    {"gw",      "Show or set default gateway",                        0, (1<<MODE_CONFIG)},
    {"netmask", "Show or set network mask",                           0, (1<<MODE_CONFIG)},
    {"l2",      "L2 MAC address table show, forget, delete",          l2_cmds, (1<<MODE_CONFIG)},
    {"igmp",    "IGMP snooping control",                              igmp_cmds, (1<<MODE_CONFIG)},
    {"stp",     "Spanning Tree Protocol control",                     stp_cmds, (1<<MODE_CONFIG)},
    {"pvid",    "Set port VLAN ID (PVID)",                            0, (1<<MODE_CONFIG)},
    {"vlan",    "VLAN create, delete, show, and management",          vlan_cmds, (1<<MODE_CONFIG)},
    {"isolate", "Port isolation configuration",                       isolate_cmds, (1<<MODE_CONFIG)},
    {"ingress", "Ingress VLAN filter mode",                           ingress_cmds, (1<<MODE_CONFIG)},
    {"mirror",  "Port mirroring configuration",                       mirror_cmds, (1<<MODE_CONFIG)},
    {"lag",     "Link Aggregation Group configuration",               lag_cmds, (1<<MODE_CONFIG)},
    {"laghash", "LAG hash algorithm selection",                       laghash_cmds, (1<<MODE_CONFIG)},
    {"sds",     "Show SerDes mode register",                          0, (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"gpio",    "Read and print GPIO input status",                   0, (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"regget",  "Read switch register by address (hex)",              0, (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"regset",  "Write switch register by address (hex)",             0, (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"sdsget",  "Read SerDes register",                               0, (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"sdsset",  "Write SerDes register",                              0, (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"phyget",  "Read PHY register (Clause-45)",                      0, (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"physet",  "Write PHY register (Clause-45)",                     0, (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"rnd",     "Generate hardware random number",                    0, (1<<MODE_EXEC)|(1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"passwd",  "Set web interface password",                         0, (1<<MODE_CONFIG)},
    {"hostname","Show or set device hostname",                        0, (1<<MODE_CONFIG)},
    {"eee",     "Energy Efficient Ethernet control",                  eee_cmds, (1<<MODE_CONFIG)},
    {"bw",      "Per-port bandwidth control",                         bw_cmds, (1<<MODE_CONFIG)},
    {"telnet",  "Telnet server control",                              telnet_cmds, (1<<MODE_CONFIG)},
    {"web",     "Web interface control",                              web_cmds, (1<<MODE_CONFIG)},
    {"commit",  "Save running configuration to flash",                0, (1<<MODE_PRIVILEGED)},
    {"show",    "Show system information",                            0, (1<<MODE_EXEC)|(1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"version", "Print software version and build info",              0, (1<<MODE_EXEC)|(1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"time",    "Show internal tick and hardware counters",           0, (1<<MODE_EXEC)|(1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"history", "Show command history",                               0, (1<<MODE_EXEC)|(1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)},
    {"xmodem",  "Receive firmware update via XMODEM (serial)",         0, (1<<MODE_PRIVILEGED)},
    {"enable",  "Enter privileged mode",                              0, (1<<MODE_EXEC)},
    {"disable", "Return to user EXEC mode",                           0, (1<<MODE_PRIVILEGED)},
    {"configure","Enter global configuration mode (configure terminal)",0, (1<<MODE_PRIVILEGED)},
    {"exit",    "Exit current mode (back one level)",                 0, (1<<MODE_PRIVILEGED)|(1<<MODE_CONFIG)|(1<<MODE_CONFIG_IF)|(1<<MODE_CONFIG_VLAN)},
    {"end",     "Return to privileged EXEC mode",                     0, (1<<MODE_CONFIG)|(1<<MODE_CONFIG_IF)|(1<<MODE_CONFIG_VLAN)},
    {0, 0, 0, 0}
};

static uint8_t common_prefix_len(__code uint8_t *first, __code uint8_t *second)
{
    uint8_t i = 0;
    while (first[i] && first[i] == second[i])
        i++;
    return i;
}

void cmd_complete(void) __banked __reentrant
{
    uint8_t word_start, word_len;
    uint8_t word_idx = find_word_idx(cmd_buffer, cursor, &word_start, &word_len);

    if (word_idx == 0) {
        struct cmd_group __code *g = top_cmds;
        char __code *matches[16];
        uint8_t n = 0;

        while (g->name && n < 16) {
            if ((g->mode_mask & (1 << cli_mode)) &&
                is_prefix(cmd_buffer, word_start, word_len, g->name)) {
                matches[n++] = g->name;
            }
            g++;
        }

        if (n == 0) return;

        if (n == 1) {
            char __code *match = matches[0];
            uint8_t ml = 0;
            while (match[ml]) ml++;
            int8_t d = ml - word_len;
            if (d > 0)
                for (uint8_t i = cmd_line_len; i > word_start + word_len; i--)
                    cmd_buffer[i + d - 1] = cmd_buffer[i - 1];
            else if (d < 0)
                for (uint8_t i = word_start + word_len; i < cmd_line_len; i++)
                    cmd_buffer[i + d] = cmd_buffer[i];
            for (uint8_t i = 0; i < ml; i++)
                cmd_buffer[word_start + i] = match[i];
            cmd_line_len += d;
            cursor = word_start + ml;
            write_char('\r');
            print_string("\033[K");
            redraw_line();
            return;
        }

        /* Find longest common prefix */
        uint8_t cp = 0xff;
        for (uint8_t i = 1; i < n; i++) {
            uint8_t l = common_prefix_len(matches[0], matches[i]);
            if (l < cp) cp = l;
        }

        /* Extend to common prefix if longer than current word */
        if (cp > word_len) {
            int8_t d = cp - word_len;
            for (uint8_t i = cmd_line_len; i > word_start + word_len; i--)
                cmd_buffer[i + d - 1] = cmd_buffer[i - 1];
            for (uint8_t i = 0; i < cp; i++)
                cmd_buffer[word_start + i] = matches[0][i];
            cmd_line_len += d;
            cursor = word_start + cp;
            write_char('\r');
            print_string("\033[K");
            redraw_line();
            return;
        }

        /* List matches */
        write_char('\n');
        for (uint8_t i = 0; i < n; i++) {
            write_char(' '); write_char(' ');
            print_string(matches[i]);
            write_char('\n');
        }
        print_cmd_prompt();
        redraw_line();
        return;
    }

    {
        uint8_t fe = 0;
        while (fe < cmd_line_len && cmd_buffer[fe] != ' ')
            fe++;
        struct cmd_group __code *g = top_cmds;
        struct cmd_entry __code *table = 0;
        while (g->name) {
            if (word_exact_match(cmd_buffer, 0, fe, g->name)) {
                table = g->subcmds;
                break;
            }
            g++;
        }
        if (!table) return;

        struct cmd_entry __code *e = table;
        char __code *matches[16];
        uint8_t n = 0;
        while (e->name && n < 16) {
            if (is_prefix(cmd_buffer, word_start, word_len, e->name)) {
                matches[n++] = e->name;
            }
            e++;
        }
        if (n == 0) return;

        if (n == 1) {
            char __code *match = matches[0];
            uint8_t ml = 0;
            while (match[ml]) ml++;
            int8_t d = ml - word_len;
            if (d > 0)
                for (uint8_t i = cmd_line_len; i > word_start + word_len; i--)
                    cmd_buffer[i + d - 1] = cmd_buffer[i - 1];
            else if (d < 0)
                for (uint8_t i = word_start + word_len; i < cmd_line_len; i++)
                    cmd_buffer[i + d] = cmd_buffer[i];
            for (uint8_t i = 0; i < ml; i++)
                cmd_buffer[word_start + i] = match[i];
            cmd_line_len += d;
            cursor = word_start + ml;
            write_char('\n');
            print_cmd_prompt();
            redraw_line();
            return;
        }

        /* Find longest common prefix */
        uint8_t cp = 0xff;
        for (uint8_t i = 1; i < n; i++) {
            uint8_t l = common_prefix_len(matches[0], matches[i]);
            if (l < cp) cp = l;
        }

        /* Extend to common prefix if longer than current word */
        if (cp > word_len) {
            int8_t d = cp - word_len;
            for (uint8_t i = cmd_line_len; i > word_start + word_len; i--)
                cmd_buffer[i + d - 1] = cmd_buffer[i - 1];
            for (uint8_t i = 0; i < cp; i++)
                cmd_buffer[word_start + i] = matches[0][i];
            cmd_line_len += d;
            cursor = word_start + cp;
            write_char('\n');
            print_cmd_prompt();
            redraw_line();
            return;
        }

        /* List matches */
        write_char('\n');
        for (uint8_t i = 0; i < n; i++) {
            write_char(' '); write_char(' ');
            print_string(matches[i]);
            write_char('\n');
        }
        print_cmd_prompt();
        redraw_line();
    }
}

void cmd_help(void) __banked __reentrant
{
    uint8_t word_start;
    uint8_t word_len;
    uint8_t word_idx = find_word_idx(cmd_buffer, cursor, &word_start, &word_len);
    (void)word_start;
    (void)word_len;

    write_char('\n');

    if (word_idx == 0) {
        struct cmd_group __code *g = top_cmds;
        while (g->name) {
            if (!(g->mode_mask & (1 << cli_mode))) { g++; continue; }
            write_char(' '); write_char(' ');
            uint8_t i = 0;
            while (g->name[i]) { write_char(g->name[i]); i++; }
            while (i < 10) { write_char(' '); i++; }
            write_char(' '); write_char(' ');
            print_string(g->desc);
            write_char('\n');
            g++;
        }
    } else {
        uint8_t fe = 0;
        while (fe < cmd_line_len && cmd_buffer[fe] != ' ')
            fe++;
        struct cmd_group __code *g = top_cmds;
        while (g->name) {
            if (word_exact_match(cmd_buffer, 0, fe, g->name)) {
                struct cmd_entry __code *sub = g->subcmds;
                if (!sub) {
                    print_string("  (no sub-commands)\n");
                    break;
                }
                while (sub->name) {
                    write_char(' '); write_char(' ');
                    uint8_t i = 0;
                    while (sub->name[i]) { write_char(sub->name[i]); i++; }
                    while (i < 10) { write_char(' '); i++; }
                    write_char(' '); write_char(' ');
                    print_string(sub->desc);
                    write_char('\n');
                    sub++;
                }
                break;
            }
            g++;
        }
        if (!g->name)
            print_string("  (unknown command)\n");
    }

    redraw_line();
}
