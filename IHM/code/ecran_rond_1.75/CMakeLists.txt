# KHÉPRI — écran pilote sur Waveshare ESP32-S3-Touch-AMOLED-1.75 (ESP-IDF 5.5)
# PROJECT_VER est la version comparée au manifeste OTA (release/ecran_rond/version.json) :
# l'augmenter à chaque publication, sinon les écrans ne se mettent pas à jour.
cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "1.5.0")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(khepri_display_175)
