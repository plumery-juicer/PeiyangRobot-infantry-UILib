cmake_minimum_required(VERSION 3.16)

project(referee-hud-ui VERSION 1.2.4 LANGUAGES CXX)

set(REFEREE_HUD_UI_FREERTOS_TARGET "" CACHE STRING
    "Optional FreeRTOS CMake target to link, for example FreeRTOS::Kernel, freertos_kernel, or FreeRTOS."
)
set(REFEREE_HUD_UI_FREERTOS_INCLUDE_DIRS "" CACHE STRING
    "Optional semicolon-separated FreeRTOS include dirs used when no FreeRTOS CMake target is available."
)
set(REFEREE_HUD_UI_FREERTOS_HEADER "" CACHE STRING
    "Optional FreeRTOS header include token, for example FreeRTOS.h or path/to/FreeRTOS.h."
)
set(REFEREE_HUD_UI_FREERTOS_QUEUE_HEADER "" CACHE STRING
    "Optional FreeRTOS queue header include token, for example queue.h or path/to/queue.h."
)

add_library(referee-hud-ui INTERFACE)
add_library(referee_hud::ui ALIAS referee-hud-ui)

target_sources(referee-hud-ui
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/referee-hud-ui.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/referee-hud-crc.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/ui-renderer-srvc.cpp
)

target_include_directories(referee-hud-ui
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_compile_features(referee-hud-ui INTERFACE cxx_std_17)

set(_referee_hud_ui_freertos_target "")
if(REFEREE_HUD_UI_FREERTOS_TARGET)
    if(NOT TARGET ${REFEREE_HUD_UI_FREERTOS_TARGET})
        message(FATAL_ERROR
            "REFEREE_HUD_UI_FREERTOS_TARGET is '${REFEREE_HUD_UI_FREERTOS_TARGET}', "
            "but that CMake target does not exist."
        )
    endif()
    set(_referee_hud_ui_freertos_target ${REFEREE_HUD_UI_FREERTOS_TARGET})
elseif(TARGET FreeRTOS::Kernel)
    set(_referee_hud_ui_freertos_target FreeRTOS::Kernel)
elseif(TARGET freertos_kernel)
    set(_referee_hud_ui_freertos_target freertos_kernel)
elseif(TARGET FreeRTOS)
    set(_referee_hud_ui_freertos_target FreeRTOS)
elseif(TARGET freertos)
    set(_referee_hud_ui_freertos_target freertos)
endif()

if(_referee_hud_ui_freertos_target)
    target_link_libraries(referee-hud-ui INTERFACE ${_referee_hud_ui_freertos_target})
endif()

if(REFEREE_HUD_UI_FREERTOS_INCLUDE_DIRS)
    target_include_directories(referee-hud-ui
        INTERFACE
            ${REFEREE_HUD_UI_FREERTOS_INCLUDE_DIRS}
    )
endif()

if(REFEREE_HUD_UI_FREERTOS_HEADER)
    target_compile_definitions(referee-hud-ui
        INTERFACE
            "REFEREE_HUD_FREERTOS_HEADER=\"${REFEREE_HUD_UI_FREERTOS_HEADER}\""
    )
endif()

if(REFEREE_HUD_UI_FREERTOS_QUEUE_HEADER)
    target_compile_definitions(referee-hud-ui
        INTERFACE
            "REFEREE_HUD_FREERTOS_QUEUE_HEADER=\"${REFEREE_HUD_UI_FREERTOS_QUEUE_HEADER}\""
    )
endif()
