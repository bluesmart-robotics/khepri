file(GLOB_RECURSE scrld ./*.c)
idf_component_register(
  SRCS ${scrld}
  REQUIRES lvgl
  INCLUDE_DIRS "custom" "generated" "generated/guider_customer_fonts")

# Force les polices LVGL pour ce composant
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  LV_FONT_MONTSERRAT_10=1
  LV_FONT_MONTSERRAT_12=1
  LV_FONT_MONTSERRAT_16=1
  LV_FONT_MONTSERRAT_28=1
  LV_FONT_MONTSERRAT_48=1
)
