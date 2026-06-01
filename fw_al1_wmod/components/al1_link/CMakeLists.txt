# al1_link — STM32↔ESP32 UART link transport component.
#
# al1_frame.c + crc16_ccitt.c are pure C (no IDF deps) and are also compiled
# stand-alone by the host unit test (see test/). al1_link.c adds the ESP-IDF
# UART driver + FreeRTOS task runtime.
idf_component_register(
    SRCS
        "al1_link.c"
        "al1_frame.c"
        "crc16_ccitt.c"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        driver
)
