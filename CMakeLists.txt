cmake_minimum_required(VERSION 3.12)
project(osd_tac VERSION 0.1)

set(CMAKE_EXPORT_COMPILE_COMMANDS True)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Warray-bounds -Wstrict-flex-arrays -fstrict-flex-arrays=3")
set(OSD_TAC_EXE osd_tac)
set(OSD_TAC_SRC
    osd_tac.c
)
add_executable(${OSD_TAC_EXE} ${OSD_TAC_SRC})

find_library(XOSD_LIBRARY_PATH xosd REQUIRED)
message(XOSD_LIBRARY_PATH="${XOSD_LIBRARY_PATH}")
message(XOSD_INCLUDE_PATH="${XOSD_INCLUDE_PATH}")

target_link_libraries(
    ${OSD_TAC_EXE}
    xosd
    pthread
)
