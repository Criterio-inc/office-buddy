# Emulatorn för Office Buddy.
#
# Bygger samma LVGL-version som firmwaren använder mot vår egen ritkod i
# delat/, och länkar mot SDL2 i stället för AMOLED-panelen.
#
#   cmake -S sim -B sim/build -G Ninja && ninja -C sim/build
#   ./sim/build/office-buddy-sim

cmake_minimum_required(VERSION 3.16)
project(office-buddy-sim C)

set(CMAKE_C_STANDARD 11)
set(ROT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# LVGL hämtas av firmwarebygget. Tills det körts lånar vi Projektpulsens
# kopia, som är exakt samma version (9.5.0) mot samma kort.
set(LVGL_DIR ${ROT}/firmware/managed_components/lvgl__lvgl)
if(NOT EXISTS ${LVGL_DIR}/lvgl.h)
  set(LVGL_DIR ${ROT}/../projektpulsen/firmware/managed_components/lvgl__lvgl)
endif()
if(NOT EXISTS ${LVGL_DIR}/lvgl.h)
  message(FATAL_ERROR
    "LVGL saknas. Kör en gång:\n"
    "  source ~/esp/esp-idf/export.sh && cd ${ROT}/firmware && idf.py set-target esp32s3")
endif()
message(STATUS "LVGL från ${LVGL_DIR}")

# Vår lv_conf.h ska hittas före LVGL:s egen mall.
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(LV_CONF_BUILD_DISABLE_EXAMPLES 1)
set(LV_CONF_BUILD_DISABLE_DEMOS 1)
add_subdirectory(${LVGL_DIR} lvgl_build)

find_package(SDL2 REQUIRED)
# LVGL:s SDL-drivrutin inkluderar <SDL2/SDL.h>, så biblioteket självt måste
# se SDL:s headers, inte bara vår körbara fil.
target_link_libraries(lvgl PUBLIC SDL2::SDL2)

add_executable(office-buddy-sim
  main.c
  ${ROT}/delat/ansikte.c
  ${ROT}/delat/humor.c
  ${ROT}/delat/protokoll.c
  ${ROT}/delat/lv_font_replik.c
  ${ROT}/delat/lv_font_siffror.c
  ${ROT}/delat/lv_font_datum.c
  ${ROT}/delat/vyer.c
)
target_include_directories(office-buddy-sim PRIVATE ${ROT}/delat)
target_link_libraries(office-buddy-sim PRIVATE lvgl SDL2::SDL2 m)
