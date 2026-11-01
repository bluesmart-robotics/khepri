# KHÉPRI — écran énergie sur Waveshare ESP32-S3-Touch-AMOLED-1.64 (ESP-IDF 5.5)
# PROJECT_VER est la version comparée au manifeste OTA (release/ecran_energie/version.json) :
# l'augmenter à chaque publication, sinon les écrans ne se mettent pas à jour.
# Deux révisions de carte (écran sur GPIO9 ou GPIO46) : voir README, compiler avec
#   idf.py -B build_V1   -D SDKCONFIG=build_V1/sdkconfig   -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.V1"   build
#   idf.py -B build_V1.1 -D SDKCONFIG=build_V1.1/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.V1.1" build
cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "1.0.0")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(khepri_energie_164)
