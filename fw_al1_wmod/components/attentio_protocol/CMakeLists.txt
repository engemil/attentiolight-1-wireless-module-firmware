# attentio_protocol — shared Attentio Protocol (AP) wire core.
#
# attentio_protocol.{h,c} are copied VERBATIM from the STM32 host repo
# (attentiolight-1-firmware/fw_al1mb1/app/attentio_protocol/). The STM32 is the
# source of truth; keep this copy byte-identical so both ends agree on the AP
# packet format, CRC-8, command IDs, and the claim-required command set
# (ap_cmd_requires_claim). Pure C (only <string.h>), no IDF/ChibiOS deps.
idf_component_register(
    SRCS
        "attentio_protocol.c"
    INCLUDE_DIRS
        "include"
)
