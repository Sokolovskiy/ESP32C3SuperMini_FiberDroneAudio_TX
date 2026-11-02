
idf_component_register(
    SRCS "main.c" "audio_capture.c" "audio_processing.c" "audio_protocol.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_i2s esp_driver_uart esp_driver_gpio esp_timer
)
