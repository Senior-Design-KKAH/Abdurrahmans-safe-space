#include "Config.h"
#if NODE_ID != 0
#include <Arduino.h>
#include <cmath>
#include "esp_heap_caps.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "logmel.h"
#include "model_data.h"
#include "clip.h"
#include "MLResult.h"
#include "inference.h"

static logmel_stream_t frontend;
static tflite::MicroInterpreter* interpreter=nullptr;
static TfLiteTensor *input=nullptr, *output=nullptr;

bool initializeInference() {
    const auto* model=tflite::GetModel(g_model);
    if(g_model_len!=35344 || model->version()!=TFLITE_SCHEMA_VERSION) return false;
    constexpr unsigned arenaBytes=64*1024;
    auto* arena=static_cast<uint8_t*>(heap_caps_aligned_alloc(16,arenaBytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    if(!arena) return false;
    static tflite::MicroMutableOpResolver<8> resolver;
    if(resolver.AddAdd()!=kTfLiteOk || resolver.AddConv2D()!=kTfLiteOk ||
       resolver.AddFullyConnected()!=kTfLiteOk || resolver.AddLogistic()!=kTfLiteOk ||
       resolver.AddMaxPool2D()!=kTfLiteOk || resolver.AddMean()!=kTfLiteOk ||
       resolver.AddMul()!=kTfLiteOk) return false;
    static tflite::MicroInterpreter instance(model,resolver,arena,arenaBytes);
    interpreter=&instance;
    if(interpreter->AllocateTensors()!=kTfLiteOk) return false;
    input=interpreter->input(0); output=interpreter->output(0);
    if(!input || !output || input->type!=kTfLiteInt8 || input->bytes!=LOGMEL_OUTPUT_COUNT ||
       output->type!=kTfLiteInt8 || output->bytes!=1 ||
       !std::isfinite(input->params.scale) || input->params.scale<=0 ||
       !std::isfinite(output->params.scale) || output->params.scale<=0) return false;
    Serial.printf("MODEL stream1s_w8_d4 sha256=%s arena_used=%u reserved=%u clip=80000 samples in flash\n",
                  kModelHash,unsigned(interpreter->arena_used_bytes()),arenaBytes);
    return true;
}

bool inferClip(float& mean) {
    logmel_init(&frontend,input->params.scale,input->params.zero_point,input->data.int8,nullptr);
    float sum=0.0f;
    for(unsigned w=0;w<10;w++) {
        unsigned done=0; int ready=0;
        while(done<8000) {
            const unsigned n=(8000-done<160)?8000-done:160;
            const size_t consumed=logmel_push(&frontend,kClipPcm+w*8000+done,n,&ready);
            if(consumed!=n || (ready!=(done+n==8000))) return false;
            done+=consumed;
            // The radio loop runs independently; also let core 0's idle task run.
            vTaskDelay(1);
        }
        if(interpreter->Invoke()!=kTfLiteOk) {
            Serial.printf("INFERENCE FAIL window=%u\n",w+1); return false;
        }
        const float probability=(int(output->data.int8[0])-output->params.zero_point)*output->params.scale;
        if(!ml::validProbability(probability)) {
            Serial.printf("INVALID OUTPUT window=%u\n",w+1); return false;
        }
        sum+=probability;
        Serial.printf("WINDOW %u samples=[%u,%u) probability=%.9f bits=%08lx\n",
                      w+1,w*8000,(w+1)*8000,double(probability),(unsigned long)ml::floatBits(probability));
        vTaskDelay(1);
    }
    mean=sum/10.0f;
    return ml::validProbability(mean);
}
#endif
