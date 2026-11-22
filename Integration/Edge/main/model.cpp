#include <cmath>
#include <cstdio>
#include "edge.h"
#include "esp_heap_caps.h"
#include "model_data.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

static logmel_stream_t frontend;
static tflite::MicroInterpreter* interpreter;
static TfLiteTensor *input, *output;

bool initializeModel() {
    const auto* model = tflite::GetModel(g_model);
    if (g_model_len != 35344 || model->version() != TFLITE_SCHEMA_VERSION) return false;

    static tflite::MicroMutableOpResolver<7> resolver;
    if (resolver.AddAdd() != kTfLiteOk || resolver.AddConv2D() != kTfLiteOk ||
        resolver.AddFullyConnected() != kTfLiteOk || resolver.AddLogistic() != kTfLiteOk ||
        resolver.AddMaxPool2D() != kTfLiteOk || resolver.AddMean() != kTfLiteOk ||
        resolver.AddMul() != kTfLiteOk) return false;

    constexpr unsigned arenaBytes = 64 * 1024;
    auto* arena = static_cast<uint8_t*>(heap_caps_aligned_alloc(
        16, arenaBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!arena) return false;
    static tflite::MicroInterpreter instance(model, resolver, arena, arenaBytes);
    if (instance.AllocateTensors() != kTfLiteOk) return false;
    input = instance.input(0);
    output = instance.output(0);
    if (!input || !output || input->type != kTfLiteInt8 || output->type != kTfLiteInt8 ||
        input->bytes != LOGMEL_OUTPUT_COUNT || output->bytes != 1 ||
        !std::isfinite(input->params.scale) || input->params.scale <= 0 ||
        !std::isfinite(output->params.scale) || output->params.scale <= 0) return false;
    interpreter = &instance;
    std::printf("MODEL stream1s_w8_d4 sha256=%s arena_used=%u reserved=%u\n",
                kModelHash, unsigned(instance.arena_used_bytes()), arenaBytes);
    return true;
}

bool scoreWindow(const int16_t* pcm, float& score) {
    if (!interpreter) return false;
    logmel_init(&frontend, input->params.scale, input->params.zero_point, input->data.int8, nullptr);
    int ready = 0;
    if (logmel_push(&frontend, pcm, samplesPerWindow, &ready) != samplesPerWindow || !ready ||
        interpreter->Invoke() != kTfLiteOk) return false;
    score = (int(output->data.int8[0]) - output->params.zero_point) * output->params.scale;
    return std::isfinite(score) && score >= 0.0f && score <= 1.0f;
}
