set(UI_FONT_SOURCES)
foreach(font_size IN ITEMS 14 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 43 48)
    list(APPEND UI_FONT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../../ui/fonts/barlow_${font_size}.c")
endforeach()
idf_component_register(
 SRCS "main.cpp" "power_control.cpp" "ble_midi_transport.cpp" "../../core/music.cpp" "../../core/application.cpp" "../../ui/ui.cpp" "../../ui/setup.cpp" "../../ui/power.cpp" ${UI_FONT_SOURCES}
 INCLUDE_DIRS "../../include" "../../experimental/bounce" "../../experimental/ripples"
 PRIV_REQUIRES usb esp_tinyusb nvs_flash esp_driver_gpio esp_driver_i2c esp_driver_usb_serial_jtag esp_timer bt)
target_sources(${COMPONENT_LIB} PRIVATE "power_diagnostics.cpp" "../../ui/boot_logo.cpp" "motion_reset.cpp" "../../core/face_down_reset.cpp")
target_sources(${COMPONENT_LIB} PRIVATE "../../core/chords.cpp" "../../ui/chord_editor.cpp")
target_sources(${COMPONENT_LIB} PRIVATE "../../ui/charge_screen.cpp")
target_sources(${COMPONENT_LIB} PRIVATE "usb_midi_transport.cpp" "speaker_audio.cpp" "../../core/synth.cpp" "../../ui/travel.cpp" "../../core/arpeggiator.cpp" "../../ui/arpeggiator.cpp")
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_sources(${COMPONENT_LIB} PRIVATE "../../experimental/bounce/world.cpp" "../../experimental/bounce/menu.cpp" "../../experimental/bounce/view.cpp" "../../experimental/ripples/scheduler.cpp" "../../ui/bounce.cpp" "../../ui/bounce_mode.cpp")
# Keep the procedural wheel renderer optimized even in the diagnostic IDF build.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
idf_component_get_property(adapter_lib espressif__esp_lvgl_adapter COMPONENT_LIB)
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
idf_component_get_property(board_lib esp32_s3_touch_amoled_1_75c COMPONENT_LIB)
target_compile_definitions(${board_lib} PRIVATE esp_lv_adapter_register_touch=sm_register_two_touch)
target_sources(${board_lib} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/touch_control.cpp")
target_compile_options(${lvgl_lib} PRIVATE -O2)
target_compile_options(${adapter_lib} PRIVATE -O2)
target_compile_options(${adapter_lib} PRIVATE
    "SHELL:-include \"${CMAKE_CURRENT_LIST_DIR}/../compat/lvgl92_adapter.h\"")
