cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rpw_edge)

# PlatformIO's SCons build skips the IDF custom commands for these embedded files.
if(EDGE_PLATFORMIO)
    function(edge_embed_certificates component)
        idf_component_get_property(component_dir ${component} COMPONENT_DIR)
        foreach(cert IN LISTS ARGN)
            execute_process(
                COMMAND "${CMAKE_COMMAND}"
                        "-DDATA_FILE=${component_dir}/server_certs/${cert}"
                        "-DSOURCE_FILE=${CMAKE_BINARY_DIR}/${cert}.S"
                        -DFILE_TYPE=TEXT
                        -P "$ENV{IDF_PATH}/tools/cmake/scripts/data_file_embed_asm.cmake"
                COMMAND_ERROR_IS_FATAL ANY)
        endforeach()
    endfunction()

    edge_embed_certificates(espressif__esp_insights https_server.crt)
    edge_embed_certificates(espressif__esp_rainmaker
        rmaker_mqtt_server.crt rmaker_claim_service_server.crt rmaker_ota_server.crt)
endif()
