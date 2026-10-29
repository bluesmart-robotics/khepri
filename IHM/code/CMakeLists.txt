# KHÉPRI — écran pilote sur Waveshare ESP32-S3-Touch-AMOLED-1.75 (ESP-IDF 5.5)
# PROJECT_VER est la version comparée au manifeste OTA (release/version.json) : l'augmenter à
# chaque publication, sinon les écrans ne se mettent pas à jour.
cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "1.1.3")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(khepri_display_175)
