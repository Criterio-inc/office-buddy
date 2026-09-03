# Ansiktet och humöret ligger i delat/ och byggs även in i emulatorn. Kopior
# här skulle förr eller senare glida isär, därför pekar vi dit.
set(DELAT ${CMAKE_CURRENT_LIST_DIR}/../../delat)

idf_component_register(
    SRCS "office_buddy.c"
         "klocka.c"
         "hardvarukoll.c"
         "usb_lank.c"
         "panelstrom.c"
         "rorelse.c"
         "ljud.c"
         "knappar.c"
         "${DELAT}/ansikte.c"
         "${DELAT}/humor.c"
         "${DELAT}/protokoll.c"
         "${DELAT}/lv_font_replik.c"
         "${DELAT}/lv_font_siffror.c"
         "${DELAT}/lv_font_datum.c"
         "${DELAT}/vyer.c"
    INCLUDE_DIRS "." "${DELAT}"
    REQUIRES esp_psram esp_driver_i2c spi_flash esp_hw_support esp_driver_usb_serial_jtag vfs nvs_flash esp_driver_gpio)
