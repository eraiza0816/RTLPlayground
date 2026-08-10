# How-to: Test IGMP with IP-MC streaming using VLC

Type: how-to · Task: verify IGMP snooping and IP multicast switching

A simple test verifying the IGMP and IP-MC switching capabilities of the
switch.

## Prerequisites

- 2 Linux/Windows devices with a GUI
- a switch running RTLPlayground with IGMP available

## Steps

1. Connect the switch to an MC-aware router (e.g. to your home network).
2. Connect the 2 GUI devices to the switch.  The connection to the router
   makes sure that the devices send out IGMP messages on the ports
   connected to the switch — which they only do if they are aware that
   there is an MC-aware router in the network.  Make sure the 2 GUI
   devices are in the home network (e.g. via DHCP).
3. Start streaming on one of the devices:

   ```
   vlc your_video.mp4 --sout="#std{access=udp, mux=ts, dst=239.255.0.1:8090}"
   ```

   At this point you should see all switch ports flickering heavily as
   the MC stream is switched to all switch ports, including flooding your
   home network.  If you do not see any packets arriving at the switch,
   force the output interface of VLC with `--miface=<ifname>`.
4. Enable IGMP on the switch CLI:

   ```
   igmp on
   ```

   The flickering should now stop on all ports except the port where the
   streaming device is connected: the switch drops all IP-MC packets as
   there are no listeners.
5. On the second device start listening to the stream:

   ```
   vlc udp://@239.255.0.1:8090
   ```

   You should see the port LED of the port the displaying machine is
   connected to start flickering, and after some synchronization the
   video should start playing.
6. Stop VLC on the listening device — the IP-MC frames should stop being
   switched to it (the port LEDs stop flickering).

## See also

- [IGMP reference](../igmp.md)
