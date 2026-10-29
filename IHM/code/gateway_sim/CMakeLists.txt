# KHÉPRI — passerelle de simulation (rôle du maître) pour tester l'écran pilote
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components/esp-now"
                         "${CMAKE_CURRENT_LIST_DIR}/../components/cmake_utilities"
                         "${CMAKE_CURRENT_LIST_DIR}/../components/khepri_proto")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(khepri_gateway_sim)
