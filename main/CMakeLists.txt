idf_component_register(
    SRCS "main.c" "acoustic_capture.c" "signal_processing.c" "mfcc.c"
         "quantization.c" "delta_codec.c" "payload.c"
    INCLUDE_DIRS "."
    REQUIRES esp_adc
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
