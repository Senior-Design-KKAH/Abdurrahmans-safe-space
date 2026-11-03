set(DEMO_NODE_ID "1" CACHE STRING "Demo role: sender=1, head=0")
if(NOT DEMO_NODE_ID MATCHES "^[01]$")
    message(FATAL_ERROR "DEMO_NODE_ID must be 0 (head) or 1 (sender)")
endif()
option(DEMO_ML_ONLY "Run sender inference without initializing/servicing LoRa" OFF)
if(DEMO_ML_ONLY AND NOT DEMO_NODE_ID STREQUAL "1")
    message(FATAL_ERROR "DEMO_ML_ONLY is only valid for the sensing node")
endif()
option(DEMO_LORA_TX_TEST "Standalone sender TX bench test without a receiver" OFF)
if(DEMO_LORA_TX_TEST AND (DEMO_ML_ONLY OR NOT DEMO_NODE_ID STREQUAL "1"))
    message(FATAL_ERROR "TX test requires sender=1 and DEMO_ML_ONLY=OFF")
endif()
if(DEMO_LORA_TX_TEST)
    set(demo_sources "lora_tx_test.cpp" "radio/LoRa.cpp")
else()
    set(demo_sources "radio.cpp" "communication_api.cpp" "application.cpp" "inference.cpp"
        "radio/LoRa.cpp" "logmel/logmel.c" "generated/model_data.cc" "generated/clip.cc")
endif()
idf_component_register(
    SRCS ${demo_sources}
    INCLUDE_DIRS "." "radio" "logmel" "generated"
    REQUIRES arduino-esp32 esp-tflite-micro esp_timer esp_system heap)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LOGMEL_USE_LOGF NODE_ID=${DEMO_NODE_ID}
                           DEMO_ML_ONLY=$<BOOL:${DEMO_ML_ONLY}>)
