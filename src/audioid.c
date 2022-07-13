// AudioId - Daniel Jackson, 2022.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "miniaudio.h"
#include "dr_wav.h"
#include "minfft.h"

#include "audioid.h"


// Fingerprint state
typedef struct fingerprint_tag {
    size_t maxSamples;
    size_t countResults; // (maxSamples/2)+1
    minfft_real *input;
    minfft_cmpl *output;
    minfft_aux *aux;
    size_t sampleOffset;
    double *magnitude;
} fingerprint_t;

void FingerprintInit(fingerprint_t *fingerprint, size_t maxSamples) {
    memset(fingerprint, 0, sizeof(*fingerprint));
    fingerprint->maxSamples = maxSamples;
    fingerprint->countResults = (fingerprint->maxSamples / 2) + 1;
    fingerprint->sampleOffset = 0;
    fingerprint->input = malloc(sizeof(minfft_real) * fingerprint->maxSamples);
    fingerprint->output = malloc(sizeof(minfft_cmpl) * fingerprint->countResults);
    fingerprint->aux = minfft_mkaux_realdft_1d(fingerprint->maxSamples);
    fingerprint->magnitude = malloc(sizeof(double) * fingerprint->countResults);
}

void FingerprintDestroy(fingerprint_t *fingerprint) {
    if (fingerprint->aux != NULL) {
        minfft_free_aux(fingerprint->aux);
        fingerprint->aux = NULL;
    }
    if (fingerprint->input != NULL) {
        free(fingerprint->input);
        fingerprint->input = NULL;
    }
    if (fingerprint->output != NULL) {
        free(fingerprint->output);
        fingerprint->output = NULL;
    }
    if (fingerprint->magnitude != NULL) {
        free(fingerprint->magnitude);
        fingerprint->magnitude = NULL;
    }
}

// If the buffer is full, return the magnitude data and count of results
double *FingerprintMagnitude(fingerprint_t *fingerprint, size_t *outCountResults) {
    if (fingerprint->sampleOffset >= fingerprint->maxSamples) {
        if (outCountResults != NULL) *outCountResults = fingerprint->countResults;
        return fingerprint->magnitude;
    } else {
        if (outCountResults != NULL) *outCountResults = 0;
        return NULL;
    }
}

// Add samples to the buffer, returning the number of samples consumed in this step.  Use FingerprintMagnitude() to check if results are available.
size_t FingerprintAddSamples(fingerprint_t *fingerprint, int16_t *samples, size_t sampleCount) {
    // Special case: adding no samples does not return as another filled buffer, even if the buffer is currently filled
    if (sampleCount == 0) {
        return 0;
    }

    // If adding to a full buffer, restart the buffer
    if (fingerprint->sampleOffset >= fingerprint->maxSamples && sampleCount > 0) {
        fingerprint->sampleOffset = 0;
    }

    // Determine how many of these samples will be used
    size_t samplesRemaining = fingerprint->maxSamples - fingerprint->sampleOffset;
    size_t samplesUsed = sampleCount > samplesRemaining ? samplesRemaining : sampleCount;

    // Add up to sampleCount samples to fingerprint->input (scaled as floating point real data)
    for (size_t i = 0; i < samplesUsed; i++) {
        fingerprint->input[fingerprint->sampleOffset + i] = (minfft_real)samples[i] / 32768;
    }
    fingerprint->sampleOffset += samplesUsed;

    // If the buffer has just filled
    if (fingerprint->sampleOffset >= fingerprint->maxSamples && samplesUsed > 0) {
        // TODO: Should a windowing function be applied to the data?

        // Compute FFT
        minfft_realdft(fingerprint->input, fingerprint->output, fingerprint->aux);

        // Computer magnitude
        for (size_t i = 0; i < fingerprint->countResults; i++) {
            #ifdef complex
                fingerprint->magnitude[i] = cabs(fingerprint->output[i]);
            #else
                minfft_real nr = fingerprint->output[i][0];
                minfft_real ni = fingerprint->output[i][1];
                fingerprint->magnitude[i] = sqrt(nr * nr + ni * ni);
            #endif
        }
    }

    // Return the number of samples consumed
    return samplesUsed;
}




// Detector state
typedef struct audioid_tag {
    // Configuration
    const char *filename;
    unsigned int sampleRate;
    double windowSize;      // FFT window size in seconds

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

    fingerprint_t fingerprint;
} audioid_t;



// Process sample data
static void AudioIdProcess(audioid_t *audioid, int16_t *samples, size_t sampleCount) {
    audioid->totalSamples += sampleCount;
    printf("SAMPLE-DATA: %zu samples (%zu ms), total %0.2f seconds\n", sampleCount, (1000 * sampleCount / audioid->sampleRate), (double)audioid->totalSamples / audioid->sampleRate);
    size_t offset = 0;
    while (offset < sampleCount) {
        offset += FingerprintAddSamples(&audioid->fingerprint, samples + offset, sampleCount - offset);
        size_t countResults = 0;
        double *magnitude = FingerprintMagnitude(&audioid->fingerprint, &countResults);
        if (magnitude != NULL && countResults > 0) {
            printf(">>> %d results.\n", (int)countResults);
        }
    }
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
    audioid->windowSize = 0.128; // * 16000 = 2048 samples
    audioid->filename = filename;
}

// Start audio processing on an audioid object
bool AudioIdStart(audioid_t *audioid) {
    AudioIdShutdown(audioid);

    ma_result result;

    size_t numSamples = (size_t)(audioid->sampleRate * audioid->windowSize);
    FingerprintInit(&audioid->fingerprint, numSamples);

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
            #define MAX_FRAME_COUNT 1024
            int16_t samples[MAX_FRAME_COUNT];
            for(;;) {
                ma_uint64 framesRead = 0;
                ma_result result = ma_decoder_read_pcm_frames(&audioid->decoder, &samples, MAX_FRAME_COUNT, &framesRead);
                if (framesRead <= 0) break;
fprintf(stderr, "READ: %d\n", (int)framesRead);
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
    FingerprintDestroy(&audioid->fingerprint);
}
