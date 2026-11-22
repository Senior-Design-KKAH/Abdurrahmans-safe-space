idf_component_register(
    SRCS "main.cpp" "capture.cpp" "model.cpp" "lora/LoRa.cpp"
         "logmel/logmel.c" "generated/model_data.cc"
    INCLUDE_DIRS "../../link" "lora" "logmel" "generated"
    REQUIRES arduino-esp32 esp_adc esp_timer esp-tflite-micro esp_system heap)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LOGMEL_USE_LOGF)
target_compile_options(${COMPONENT_LIB} PRIVATE -ffp-contract=off)

option(EDGE_MODEL_CHECK "Include the known development clip and serial model check" OFF)
if(EDGE_MODEL_CHECK)
    set(clip_dir "${CMAKE_CURRENT_LIST_DIR}/../../../half-demo/main/generated")
    target_sources(${COMPONENT_LIB} PRIVATE "${clip_dir}/clip.cc")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${clip_dir}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE EDGE_MODEL_CHECK=1)
endif()
