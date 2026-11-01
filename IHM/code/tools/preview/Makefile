# Aperçus PC des deux écrans (LVGL natif, sans ESP-IDF)
#   make && ./preview <dossier>            écran rond 1.75 (régime moteur)
#           ./preview_energie <dossier>    écran rectangulaire 1.64 (énergie)
ROOT := ../..
LV := $(ROOT)/components/lvgl
P := $(ROOT)/components/khepri_proto
FONTS := $(wildcard $(ROOT)/components/kh_fonts/fonts/*.c)
COMMON := $(FONTS) $(P)/src/kh_model.c $(P)/src/khepri_espnow.c
DEFS := -DLV_CONF_SKIP -DLV_LVGL_H_INCLUDE_SIMPLE -DLV_MEM_CUSTOM=1 -DLV_COLOR_16_SWAP=0 -DLV_USE_PERF_MONITOR=0 \
        -DLV_FONT_MONTSERRAT_14=1 -DLV_USE_KEYBOARD=1 -DLV_USE_TEXTAREA=1 -DLV_USE_LIST=1
INC := -I. -I$(LV) -I$(LV)/src -I$(P)/include -I$(ROOT)/components/kh_common/include -I$(ROOT)/components/kh_fonts/include
LV_SRC := $(shell find $(LV)/src -name '*.c')
LV_OBJ := $(patsubst $(LV)/src/%.c,obj/lv/%.o,$(LV_SRC))

all: preview preview_energie

obj/lv/%.o: $(LV)/src/%.c
	@mkdir -p $(dir $@)
	@$(CC) -O1 -w $(DEFS) $(INC) -c $< -o $@
liblv.a: $(LV_OBJ)
	ar rcs $@ $^

preview: preview.c $(ROOT)/ecran_rond_1.75/main/khepri_screen.c $(COMMON) liblv.a
	$(CC) -O1 -w $(DEFS) $(INC) -I$(ROOT)/ecran_rond_1.75/main -o $@ $(filter %.c,$^) liblv.a -lm
preview_energie: preview_energie.c $(ROOT)/ecran_energie_1.64/main/energie_screen.c $(COMMON) liblv.a
	$(CC) -O1 -w $(DEFS) $(INC) -I$(ROOT)/ecran_energie_1.64/main -o $@ $(filter %.c,$^) liblv.a -lm
clean:
	rm -rf preview preview_energie liblv.a obj
