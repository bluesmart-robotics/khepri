# Polices Montserrat + symboles LVGL (Latin-1), générées par lv_font_conv : 14, 18, 24, 32, 64 px
file(GLOB FONTS "fonts/*.c")
idf_component_register(SRCS ${FONTS} INCLUDE_DIRS "include" REQUIRES lvgl)
