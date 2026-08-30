# How-to: Recode a Fibre Channel SFP module to Ethernet

Type: how-to · Task: make an FC SFP module present itself as an Ethernet
module (and fix its checksums)

Some Fibre Channel SFP modules do not advertise Ethernet compliance
codes, so the switch may not accept them.  The `sfp <slot> patch`
command rewrites the compliance bytes and fixes the checksums.

## Steps

1. Inspect the module first:

   ```
   > sfp 1 describe
     Identifier: 0x03 (SFP)
     Connector: 0x03 (LC)
     Vendor: FINISAR
     PN: FTLF8536P4BCV
     Rate: 96 x100MBd
     Compliance:
     CC_BASE: 0x35 (BAD)
     CC_EXT: 0xce (OK)
   ```

   Note the **CC_BASE: BAD** — the checksum does not match the data.
2. Patch the module:

   ```
   > sfp 1 patch
     Patch OK
   ```
3. Verify the result:

   ```
   > sfp 1 describe
     Compliance: 10GBase-LR 1000Base-LX
     CC_BASE: 0x31 (OK)
   ```

   The compliance codes now include 10GBase-LR / 1000Base-LX and the
   checksum is valid.  The patch also rewrites the signalling rate
   (byte 12) to 0x67 (10.3 GBit/s), so the switch configures the
   SerDes for 10G automatically when the module is inserted — no
   manual `sfp <slot> 10g` needed.

## See also

- [SFP+ reference](../sfp.md)
