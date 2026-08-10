# IGMP (Internet Group Management Protocol) and MLD (Multicast Listener Discovery)

Type: reference · Feature: IP multicast snooping, the hardware querier and MLD
IGMP (for IPv4) and MLD (for IPv6) are protocols that control the distribution
of Layer-3 Multicast packets on the LAN, which otherwise would be flooded across the
entire network. For this to work, IGMP/MLD messages are sent, in particular
from MC consumers (e.g. the video-player that plays an IP-Multicast stream), but
also Multicast-aware routers to control switching of the IP-MC or underlying
L2-MC packets. The main usage in home networks is IPTV. 

The RTL8372/3 SoC supports managing IPv4-MC using either Destination-IP (the IPv4
multicast group address)/Source-IP (typically 0.0.0.0) matching or via controlling
the switching of the underlying L2-MC packets (i.e. packets in 01:00:5e:xx:yy:zz, where
xx:yy:zz are the LSBs of the IPv4-MC address). The DIP/SIP-based switching is
not VLAN-aware, meaning a stream will be available in all VLANs if subscribed to.
This is not a problem in a typical home network, however. The L2-based method
is VLAN aware, but currently not supported in the software.

Although there is hardware support for IPv6/MLD-based Multicast management (i.e. intelligent
management by the switch), the current software does not implement managing IPv6 Multicast.
Instead, all IPv6 Multicast pakets will be flooded to all ports, just as an unmanaged
switch would do.

The current software support works by trapping IGMP packets (only v3 supported, which
is used in the vast majority of today's networks) to the CPU of the switch which will
update the L3 and L2 switching tables to include switch ports in a stream or remove
them. This trapping to the CPU is also called IGMP snooping. While there is support
in the HW to handle IGMP/MLD packets (v3 has only limited support) entirely in hardware
and even send out reports, it is currently not understood
how this works, and instead IGMP is handled entirely in software, which also allows
to fully support IGMPv3 packet which are the standard in present-day networks.

In addition to software IGMP snooping, the firmware also uses the ASIC's hardware
engines for the management plane:

- **IGMP querier** (RTL8373): the ASIC itself sends General Queries at a 60 s
  interval, driven by `IGMP_CTRL` (0x5290) — bit 0 `IGMP_MLD_EN`, bits 5-7
  `LEAVE_TIMER`, and the query interval register 0x5294.
- **MLD snooping**: the ASIC's IPv6 multicast engine (`RTL837X_IPV6_PORT_MC_LM_ACT`
  0x4f7c and the MLD control bits of `RTL837X_IGMP_CTRL`) manages IPv6 multicast
  listener groups in hardware, trapping MLD reports to the CPU which updates the
  L3/L2 tables, the same way IPv4 works.

## IP-MC control
The relevant registers for controlling IP-MC switching are:
```
#define RTL837X_IPV4_PORT_MC_LM_ACT	0x4f78
#define RTL837X_IPV6_PORT_MC_LM_ACT	0x4f7c
#define RTL837X_IGMP_PORT_CFG		0x52a0
#define IGMP_MAX_GROUP			0x00ff0000
#define IGMP_PROTOCOL_ENABLE		0x00007c00
#define IGMP_TRAP			0x0000002a
#define IGMP_FLOOD			0x00000015
#define IGMP_ASIC			0x00000000
#define RTL837X_IGMP_ROUTER_PORT	0x529c
#define RTL837X_IPV4_UNKN_MC_FLD_PMSK	0x5368
#define RTL837X_IPV6_UNKN_MC_FLD_PMSK	0x536c
#define RTL837X_IGMP_TRAP_CFG		0x50bc
#define IGMP_TRAP_PRIORITY		0x7
#define IGMP_CPU_PORT			0x00010000
```
`RTL837X_IPV4_PORT_MC_LM_ACT/RTL837X_IPV6_PORT_MC_LM_ACT` control the action when an
IP-MC packet is encountered at a switch port and there is no rule for forwarding in
the forwarding tables. The default action is to flood such Lookup-Miss packets to all
ports. This is the configuration without IGMP/MLD enabled.

When IGMP/MLD is turned on, the Lookup-Miss action will be changed to drop such packets
unless a rule is found in the forwarding tables, which will need to be configured by
IGMP packets.

Switching on IGMP also configures all ports via `RTL837X_IGMP_PORT_CFG` to trap all
incoming IGMP packets to the CPU. `RTL837X_IGMP_TRAP_CFG` then is used to configure
priority and CPU-Port of trapped IGMP/MLD packets. 

Configuration of the IP-MC-forwarding to the listening ports is done by managing the
forwarding tables of the switch, see [L2 learning](l2.md).


## IGMP API
The code currently provides the following functions:
```
void igmp_setup(void) __banked;
void igmp_enable(void) __banked;
void igmp_router_port_set(uint16_t pmask) __banked;
void igmp_packet_handler(void) __banked;
void igmp_show(void) __banked;
```c
`igmp_setup()` is called at boot-time and configures flooding of all IP-MC packets by
default, as otherwise no IP-MC would be possible in the network.

`igmp_enable()`starts IGMP which cause IGMP packets to be handled by the CPU and forwarding
of IP-MC packets to be limited to only subscribed ports.

`igmp_router_port_set()`configures forwarding ports for IGMP messages.

`igmp_packet_handler()` implements handling of trapped IGMP packets by the CPU.

`igmp_show()` prints out the IGMP configuration on the CLI.


## IGMP configuration on the CLI / WebUI

Commands on the serial/telnet console and via rtlpctl:

```
igmp [on|off|show]
igmp querier [on|off|show]
igmp mld [on|off|show]
```

- `igmp on` / `igmp off` — enable/disable IGMP snooping
- `igmp querier on` — start the ASIC's IGMP querier (60 s query interval);
  `off` stops it; `show` prints the state
- `igmp mld on` / `off` — enable/disable MLD (IPv6) snooping in the ASIC
- `igmp show` — show the IGMP/MLD configuration and the learned groups

The WebUI exposes the same toggles on the L2 Configuration panel
(IGMP snooping, IGMP querier, MLD snooping) and shows the learned
multicast groups.

## JSON endpoint

`GET /igmp.json` returns the live multicast state:

```json
{"mld_en":0,"querier":0,"ops":[0,0,0,0,0,0],"groups":[]}
```

- `mld_en` — the ASIC's IGMP/MLD engine enable bit
- `querier` — whether the hardware querier is running
- `ops` — per-port MLDv1/MLDv2 operation bits
- `groups` — the learned multicast groups (index + port mask)

## rtlpctl

```
rtlpctl igmp on|off|show
rtlpctl igmp querier on|off|show
rtlpctl igmp mld on|off|show
```

To verify IGMP snooping with a real multicast stream, see
[How-to: Test IGMP with IP-MC streaming using VLC](how-to/igmp-streaming-test.md).
