# al1_ble — NimBLE peripheral + custom Attentio BLE Service.
#
# al1_ble.c     : NVS + NimBLE host bring-up, GAP/SM (Just-Works + LESC +
#                 bonding), advertising, host task.
# al1_ble_gatt.c: the Attentio GATT service (TX write / RX notify chars).
idf_component_register(
    SRCS
        "al1_ble.c"
        "al1_ble_gatt.c"
        "al1_ble_ap_bridge.c"
        "ap_reasm.c"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        bt
        nvs_flash
        al1_link
        attentio_protocol
)
