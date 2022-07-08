// AudioId - Daniel Jackson, 2022.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "miniaudio.h"
#include "dr_wav.h"
#include "minfft.h"

#include "audioid.h"

// Detector state
typedef struct audioid_tag {
    // Configuration
    const char *filename;
    unsigned int sampleRate;

    // Audio device capture
    ma_device_config deviceConfig;
    ma_device device;
    bool deviceInitialized;

    // Audio file input
    ma_decoder_config decoderConfig;
    ma_decoder decoder;
    bool decoderInitialized;

    // State
    size_t totalSamples;

} audioid_t;

// Fingerprint state
// typedef struct fingerprint_tag {

//     minfft_real *input;
//     minfft_cmpl *output; // (numSamples/2)+1
//     minfft_aux *aux;

//     aux = minfft_mkaux_realdft_1d(numSamples);
//     minfft_realdft(input, output, aux);
// 	   minfft_free_aux(aux);

// } fingerprint_t;


// Process sample data
static void AudioIdProcess(audioid_t *audioid, int16_t *samples, size_t sampleCount) {
    audioid->totalSamples += sampleCount;
    printf("SAMPLE-DATA: %zu samples (%zu ms), total %0.2f seconds\n", sampleCount, (1000 * sampleCount / audioid->sampleRate), (double)audioid->totalSamples / audioid->sampleRate);
    return;
}

// MiniAudio device data callback
static void data_callback(ma_device *device, void *_output, const void *input, ma_uint32 frameCount) {
    audioid_t *audioid = (audioid_t *)device->pUserData;
    AudioIdProcess(audioid, (int16_t *)input, (size_t)frameCount);
    return;
}

// Allocate an audioid object, this will also initialize the object.
audioid_t *AudioIdCreate() {
    audioid_t *audioid = (audioid_t *)malloc(sizeof(audioid_t));
    memset(audioid, 0, sizeof(*audioid));
    AudioIdInit(audioid, NULL);
    return audioid;
}

// Destroy the audioid object, this will also shutdown the object.
void AudioIdDestroy(audioid_t *audioid) {
    AudioIdShutdown(audioid);
    free(audioid);
}

// Initialize an audioid object with a new configuration
void AudioIdInit(audioid_t *audioid, const char *filename) {
    memset(audioid, 0, sizeof(*audioid));
    audioid->sampleRate = AUDIOID_SAMPLE_RATE;
    audioid->filename = filename;
}

// Start audio processing on an audioid object
bool AudioIdStart(audioid_t *audioid) {
    AudioIdShutdown(audioid);

    ma_result result;

    if (audioid->filename != NULL) {
        fprintf(stderr, "AUDIOID: Opening file: %s\n", audioid->filename);

        audioid->decoderConfig = ma_decoder_config_init(ma_format_s16, 1, audioid->sampleRate);

        result = ma_decoder_init_file(audioid->filename, &audioid->decoderConfig, &audioid->decoder);
        if (result != MA_SUCCESS) {
            fprintf(stderr, "ERROR: Could not load file: %s\n", audioid->filename);
            return false;
        }
        audioid->decoderInitialized = true;
        return true;
    } else {
        fprintf(stderr, "AUDIOID: Configuring audio capture...\n");

        audioid->deviceConfig = ma_device_config_init(ma_device_type_capture);
        audioid->deviceConfig.capture.format   = ma_format_s16;
        audioid->deviceConfig.capture.channels = 1;
        audioid->deviceConfig.sampleRate       = audioid->sampleRate;
        audioid->deviceConfig.dataCallback     = data_callback;
        audioid->deviceConfig.pUserData        = audioid;

        result = ma_device_init(NULL, &audioid->deviceConfig, &audioid->device);
        if (result != MA_SUCCESS) {
            fprintf(stderr, "ERROR: Failed to initialize capture device.\n");
            return false;
        }

        result = ma_device_start(&audioid->device);
        if (result != MA_SUCCESS) {
            ma_device_uninit(&audioid->device);
            fprintf(stderr, "ERROR: Failed to start device.\n");
            return false;
        }

        fprintf(stderr, "AUDIOID: ...audio capture configured.\n");
        audioid->deviceInitialized = true;

        return true;
    }
}

// Wait until audio processing has completed
void AudioIdWaitUntilDone(audioid_t *audioid) {
    if (audioid->filename != NULL) {
        if (audioid->decoderInitialized) {
            #define MAX_FRAME_COUNT 1000
            int16_t samples[MAX_FRAME_COUNT];
            for(;;) {
                ma_uint64 framesRead = 0;
                ma_result result = ma_decoder_read_pcm_frames(&audioid->decoder, &samples, MAX_FRAME_COUNT, &framesRead);
                if (framesRead <= 0) break;
                fprintf(stderr, "RESULT: %d\n", (int)framesRead);
                AudioIdProcess(audioid, samples, (size_t)framesRead);
                if (result != MA_SUCCESS) break;
            }
        }
    } else {
        if (audioid->deviceInitialized) {
            fprintf(stderr, "AUDIOID: Press Enter to stop live input...\n");
            getchar();
        }
    }
}

// Shutdown an audioid object (but do not destroy it), the object can be used again
void AudioIdShutdown(audioid_t *audioid) {
    if (audioid->deviceInitialized) {
        fprintf(stderr, "AUDIOID: Stopping audio capture.\n");
        ma_device_uninit(&audioid->device);
        audioid->deviceInitialized = false;
    }
    if (audioid->decoderInitialized) {
        fprintf(stderr, "AUDIOID: Closing audio file.\n");
        ma_decoder_uninit(&audioid->decoder);
        audioid->decoderInitialized = false;
    }
}
