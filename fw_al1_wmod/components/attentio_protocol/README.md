# attentio_protocol (shared AP wire core)

`attentio_protocol.{h,c}` are a **verbatim copy** from the STM32 host repo:

```
attentiolight-1-firmware/fw_al1mb1/app/attentio_protocol/
```

The STM32 is the source of truth. This copy must stay **byte-identical** so both
MCUs share one definition of:

- the AP packet format `[SYNC 0xA5][LEN][CMD][PAYLOAD ≤252][CRC8]`,
- CRC-8/CCITT (`ap_crc8`), the parser (`ap_parse_byte`) and builders
  (`ap_build_packet` / `ap_build_error` / …),
- the command IDs (`AP_CMD_*`) and error codes (`AP_ERR_*`),
- the claim-required command set (`ap_cmd_requires_claim`), which the ESP32 BLE
  bridge uses to pre-gate non-controlling clients with the exact list the STM32
  MICB enforces.

To update: change it in the STM32 repo first, then re-copy both files here
(verify with `sha1sum`). Do not edit this copy in place.

This mirrors how `al1_link`'s `al1_frame.c` / `crc16_ccitt.c` are shared between
the two repos.
