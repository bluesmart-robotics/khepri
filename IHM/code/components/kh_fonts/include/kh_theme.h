/*
 * kh_theme.h — thème commun aux écrans KHÉPRI : fond noir pur (pixel AMOLED éteint) et code couleur
 * aéronautique. Vert = normal, ambre = attention, rouge = danger ; le jaune « soleil » ne sert
 * qu'à l'énergie solaire. Mêmes couleurs sur les deux écrans : une couleur garde le même sens.
 */
#pragma once
#include "kh_fonts.h"
#include "lvgl.h"

#define KH_COL_BG lv_color_hex(0x000000)
#define KH_COL_CARD lv_color_hex(0x22252E)
#define KH_COL_CARD2 lv_color_hex(0x2A2D38)
#define KH_COL_TEXT lv_color_hex(0xF2F4F8)
#define KH_COL_TEXT_DIM lv_color_hex(0x7A8099)
#define KH_COL_TEXT_MID lv_color_hex(0xB0B8CC)
#define KH_COL_GREEN lv_color_hex(0x4ADE80)
#define KH_COL_AMBER lv_color_hex(0xFBB040)
#define KH_COL_RED lv_color_hex(0xF4614A)
#define KH_COL_BLUE lv_color_hex(0x60A5FA)
#define KH_COL_ACCENT lv_color_hex(0x7DD3C8)
#define KH_COL_SUN lv_color_hex(0xFCD34D)
