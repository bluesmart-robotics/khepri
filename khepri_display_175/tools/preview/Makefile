# Aperçus PC de l'écran pilote (LVGL natif, sans ESP-IDF)
ROOT := ../..
LV := $(ROOT)/components/lvgl
P := $(ROOT)/components/khepri_proto
SRC := preview.c $(ROOT)/main/khepri_screen.c $(wildcard $(ROOT)/main/fonts/*.c) $(P)/src/kh_model.c $(P)/src/khepri_espnow.c \
       $(shell find $(LV)/src -name '*.c')
DEFS := -DLV_CONF_SKIP -DLV_LVGL_H_INCLUDE_SIMPLE -DLV_MEM_CUSTOM=1 -DLV_COLOR_16_SWAP=0 -DLV_USE_PERF_MONITOR=0 \
        -DLV_FONT_MONTSERRAT_14=1
preview: $(SRC)
	$(CC) -O1 -w $(DEFS) -I. -I$(LV) -I$(LV)/src -I$(ROOT)/main -I$(P)/include -o $@ $(SRC) -lm
clean:
	rm -f preview
