// AudioId - Daniel Jackson, 2022.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "miniaudio.h"
#include "dr_wav.h"
#include "minfft.h"

#include "audioid.h"

//#define WINDOW_OVERLAP 2        // <=1 = none, 2 = half

// Hamming window function (http://en.wikipedia.org/wiki/Window_function)
static double HammingWindow(int index, size_t size)
{
    const double weight = 0.53836;  // 25.0/46.0
	return weight - (1.0 - weight) * cos(2 * M_PI * index / (size - 1));
}

static unsigned int Lerp(const double *start, const double *end, double proportion) {
    const double globalScale = 0.5;
    proportion *= globalScale;

    if (proportion < 0) proportion = 0;
    if (proportion > 1) proportion = 1;

    // Lerp, clamp and pack
    unsigned int retVal = 0;
    for (size_t i = 0; i < 3; i++) {
        double v = proportion * (end[i] - start[i]) + start[i];
        if (v < 0) v = 0;
        if (v > 1) v = 1;
        retVal |= (unsigned int)(255 * v) << (8 * i);
    }
    return retVal;
}

static unsigned int Gradient(double value) {
    const double black[3] = { 0, 0, 0 };
    const double purple[3] = { 0.5, 0, 1 };
    const double orange[3] = { 1, 0.5, 0 };
    const double yellow[3] = { 1, 1, 0 };
    const double white[3] = { 1, 1, 1 };

    if (value <= 0) {           // black
        return Lerp(black, black, 0);
    } else if (value < 0.333) { // to dark purple
        return Lerp(black, purple, (value - 0) * 3);
    } else if (value < 0.666) {  // to orange
        return Lerp(purple, orange, (value - 0.333) * 3);
    } else if (value <= 1)   {  // to yellow
        return Lerp(orange, yellow, (value - 0.666) * 3);
    } else {    // saturate to white at 2.0
        return Lerp(yellow, white, value - 1);
    }
}

static void DebugVisualizeValues(double *values, size_t count) {
    const int mode = 2;    // 0=solid block, 1=left-half block, 2=buffer previous line and upper-half block
    static double *buffer = NULL;   // horrible hack to buffer previous line so output can be two virtual lines per physical line
    static size_t bufferSize = 0;
    static int bufferLine = 0;
    if (mode == 2 && bufferSize < count) {
        bufferSize = count;
        buffer = realloc(buffer, sizeof(double) * bufferSize);
    }
    if (mode == 2 && (bufferLine & 1) == 0) {
        memcpy(buffer, values, sizeof(double) * count);
    } else {
        for (size_t x = 0; x < count; x++) {
            unsigned int c = Gradient(values[x]);
            if (mode == 1) {
                if ((x & 1) == 1) {
                    unsigned int cPrev = Gradient(values[x - 1]);
                    // Left-half block - Unicode: \u258c - UTF-8: \xe2\x96\x8c
                    printf("\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm\u258c", (unsigned char)(cPrev>>0), (unsigned char)(cPrev>>8), (unsigned char)(cPrev>>16), (unsigned char)(c>>0), (unsigned char)(c>>8), (unsigned char)(c>>16));
                }
            } else if (mode == 2) {
                if ((bufferLine & 1) == 1) {
                    unsigned int cPrev = Gradient(buffer[x]);
                    // Upper-half block - Unicode: \u2580 - UTF-8: \xe2\x96\x80
                    printf("\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm\u2580", (unsigned char)(cPrev>>0), (unsigned char)(cPrev>>8), (unsigned char)(cPrev>>16), (unsigned char)(c>>0), (unsigned char)(c>>8), (unsigned char)(c>>16));
                }
            } else {
                // Full Block - Unicode: \u2588 - UTF-8: \xe2\x96\x88
                printf("\x1b[38;2;%d;%d;%dm\u2588", (unsigned char)(c>>0), (unsigned char)(c>>8), (unsigned char)(c>>16));
            }
        }        
        printf("\x1b[0m\n");
    }
    bufferLine++;
}


// Fingerprint state
typedef struct fingerprint_tag {
    size_t maxSamples;
    size_t countResults;    // (maxSamples/2)+1
    size_t countBuckets;    // count of buckets to average into
    double *input;          // user-supplied input, converted to floating point
    minfft_real *weighted;  // window-weighted values before FFT
    minfft_cmpl *output;    // complex output of FFT
    minfft_aux *aux;        // auxillary data needed for FFT
    double *magnitude;      // magnitude of each output
    double *buckets;        // mean magnitude into fewer buckets
    size_t sampleOffset;
} fingerprint_t;

void FingerprintInit(fingerprint_t *fingerprint, size_t maxSamples, size_t countBuckets) {
    memset(fingerprint, 0, sizeof(*fingerprint));
    fingerprint->maxSamples = maxSamples;
    fingerprint->countBuckets = countBuckets;
    fingerprint->countResults = (fingerprint->maxSamples / 2) + 1;
    fingerprint->sampleOffset = 0;
    fingerprint->aux = minfft_mkaux_realdft_1d(fingerprint->maxSamples);
    fingerprint->input = malloc(sizeof(double) * fingerprint->maxSamples);
    fingerprint->weighted = malloc(sizeof(minfft_real) * fingerprint->maxSamples);
    fingerprint->output = malloc(sizeof(minfft_cmpl) * fingerprint->countResults);
    fingerprint->magnitude = malloc(sizeof(double) * fingerprint->countResults);
    fingerprint->buckets = malloc(sizeof(double) * fingerprint->countBuckets);
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
    if (fingerprint->weighted != NULL) {
        free(fingerprint->weighted);
        fingerprint->weighted = NULL;
    }
    if (fingerprint->output != NULL) {
        free(fingerprint->output);
        fingerprint->output = NULL;
    }
    if (fingerprint->magnitude != NULL) {
        free(fingerprint->magnitude);
        fingerprint->magnitude = NULL;
    }
    if (fingerprint->buckets != NULL) {
        free(fingerprint->buckets);
        fingerprint->buckets = NULL;
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

// If the buffer is full, return the bucket-mean magnitude data and count of results
double *FingerprintBuckets(fingerprint_t *fingerprint, size_t *outCountResults) {
    if (fingerprint->sampleOffset >= fingerprint->maxSamples) {
        if (outCountResults != NULL) *outCountResults = fingerprint->countBuckets;
        return fingerprint->buckets;
    } else {
        if (outCountResults != NULL) *outCountResults = 0;
        return NULL;
    }
}

// Add samples to the buffer, returning the number of samples consumed in this step.  Use FingerprintMagnitude()/FingerprintBuckets() to check if results are available.
size_t FingerprintAddSamples(fingerprint_t *fingerprint, int16_t *samples, size_t sampleCount) {
    // Special case: adding no samples does not return as another filled buffer, even if the buffer is currently filled
    if (sampleCount == 0) {
        return 0;
    }

    // If adding to a full buffer, restart the buffer -- slide window by half if overlapping
    if (fingerprint->sampleOffset >= fingerprint->maxSamples && sampleCount > 0) {
#if defined(WINDOW_OVERLAP) && (WINDOW_OVERLAP > 1)
        size_t offset = fingerprint->maxSamples / WINDOW_OVERLAP;
        size_t length = fingerprint->maxSamples - offset;
        memmove(fingerprint->input, fingerprint->input + offset * sizeof(double), length * sizeof(double));
        fingerprint->sampleOffset = fingerprint->maxSamples * (WINDOW_OVERLAP - 1) / WINDOW_OVERLAP;
#else
        fingerprint->sampleOffset = 0;
#endif
    }

    // Determine how many of these samples will be used
    size_t samplesRemaining = fingerprint->maxSamples - fingerprint->sampleOffset;
    size_t samplesUsed = sampleCount > samplesRemaining ? samplesRemaining : sampleCount;

    // Add up to sampleCount samples to fingerprint->input (scaled as floating point real data)
    for (size_t i = 0; i < samplesUsed; i++) {
        int index = fingerprint->sampleOffset + i;
        double value = (double)samples[i] / 32768;
        fingerprint->input[index] = (double)value;
    }
    fingerprint->sampleOffset += samplesUsed;

    // If the buffer has just filled
    if (fingerprint->sampleOffset >= fingerprint->maxSamples && samplesUsed > 0) {
        // Window-weight samples for FFT
        for (size_t i = 0; i < fingerprint->maxSamples; i++) {
            double weight = HammingWindow(i, fingerprint->maxSamples);
            double value = weight * fingerprint->input[i];
            fingerprint->weighted[i] = (minfft_real)value;
        }

        // Compute FFT
        minfft_realdft(fingerprint->weighted, fingerprint->output, fingerprint->aux);

        // Compute magnitude
        for (size_t i = 0; i < fingerprint->countResults; i++) {
            #ifdef complex
                fingerprint->magnitude[i] = cabs(fingerprint->output[i]);
            #else
                minfft_real nr = fingerprint->output[i][0];
                minfft_real ni = fingerprint->output[i][1];
                fingerprint->magnitude[i] = sqrt(nr * nr + ni * ni);
            #endif
        }

        // Compute averaged buckets
        for (size_t i = 0; i < fingerprint->countBuckets; i++) {
            size_t iStartAt = i * fingerprint->countResults / fingerprint->countBuckets;
            size_t iEndBefore = (i + 1) * fingerprint->countResults / fingerprint->countBuckets;
            double mean = 0;
            for (size_t i = iStartAt; i < iEndBefore; i++) {
                mean += fingerprint->magnitude[i];
            }
            if (iEndBefore > iStartAt) {
                mean /= iEndBefore - iStartAt;
            }
            fingerprint->buckets[i] = mean;
        }
    }

    // Return the number of samples consumed
    return samplesUsed;
}


// Detector state
typedef struct audioid_tag {
    // Configuration
    const char *filename;
    const char *labelFile;
    unsigned int sampleRate;
    size_t windowSize;
    size_t countBuckets;
    bool verbose;

    // Audio device capture
    ma_device_config deviceConfig;
    ma_device device;
    bool deviceInitialized;

    // Audio file input
    ma_decoder_config decoderConfig;
    ma_decoder decoder;
    bool decoderInitialized;

    // Labels
    const char **labels;
    size_t countLabels;

    // State
    size_t totalSamples;

    fingerprint_t fingerprint;
} audioid_t;


static const char *AudioIdGetLabelName(audioid_t *audioid, size_t id) {
    if (id >= audioid->countLabels) return NULL;
    return audioid->labels[id];
}

static size_t AudioIdGetLabelId(audioid_t *audioid, const char *label) {
    // Return existing label id
    for (size_t id = 0; id < audioid->countLabels; id++) {
        if (strcmp(audioid->labels[id], label) == 0) {
            return id;
        }
    }
    // Add the label if it is new
    audioid->labels = (const char **)realloc(audioid->labels, sizeof(const char *) * (audioid->countLabels + 1));
    audioid->labels[audioid->countLabels] = strdup(label);
    // Return the new label id
    return ++audioid->countLabels;
}

static void AudioIdFreeLabels(audioid_t *audioid) {
    for (size_t id = 0; id < audioid->countLabels; id++) {
        free((void *)audioid->labels[id]);
        audioid->labels[id] = NULL;
    }
    free(audioid->labels);
    audioid->labels = NULL;
    audioid->countLabels = 0;
}


// Process sample data
static void AudioIdProcess(audioid_t *audioid, int16_t *samples, size_t sampleCount) {
    audioid->totalSamples += sampleCount;
    if (audioid->verbose) fprintf(stderr, "SAMPLE-DATA: %zu samples (%zu ms), total %0.2f seconds\n", sampleCount, (1000 * sampleCount / audioid->sampleRate), (double)audioid->totalSamples / audioid->sampleRate);
    size_t offset = 0;
    while (offset < sampleCount) {
        offset += FingerprintAddSamples(&audioid->fingerprint, samples + offset, sampleCount - offset);
        size_t countResults = 0;
        double *buckets = FingerprintBuckets(&audioid->fingerprint, &countResults);
        if (buckets != NULL && countResults > 0) {
            if (audioid->verbose) fprintf(stderr, ">>> %d results.\n", (int)countResults);
            DebugVisualizeValues(buckets, countResults);
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
    AudioIdInit(audioid, NULL, NULL);
    return audioid;
}

// Destroy the audioid object, this will also shutdown the object.
void AudioIdDestroy(audioid_t *audioid) {
    AudioIdShutdown(audioid);
    free(audioid);
}

// Initialize an audioid object with a new configuration
void AudioIdInit(audioid_t *audioid, const char *filename, const char *labelFile) {
    memset(audioid, 0, sizeof(*audioid));
    audioid->filename = filename;
    audioid->labelFile = labelFile;

    // TODO: Move these to parameters
    audioid->sampleRate = AUDIOID_SAMPLE_RATE;
audioid->windowSize = 2048; // 2048 / AUDIOID_SAMPLE_RATE = 0.128s // 1024+1 results
//audioid->windowSize = 512;
    audioid->countBuckets = 128;  // average into buckets
    audioid->verbose = false;
}

// Start audio processing on an audioid object
bool AudioIdStart(audioid_t *audioid) {
    AudioIdShutdown(audioid);

    ma_result result;

    FingerprintInit(&audioid->fingerprint, audioid->windowSize, audioid->countBuckets);

    if (audioid->labelFile != NULL) {
        fprintf(stderr, "AUDIOID: Opening label file: %s\n", audioid->labelFile);
        FILE *fp = fopen(audioid->labelFile, "r");
        char lineBuffer[256];
        for (size_t lineNumber = 1; ; lineNumber++) {
            char *line = fgets(lineBuffer, sizeof(lineBuffer) - 1, fp);
            if (line == NULL) break;
            //if (line[0] != '\0' && line[strlen(line) - 1] == '\n') line[strlen(line) - 1] = '\0';
            //if (line[0] != '\0' && line[strlen(line) - 1] == '\r') line[strlen(line) - 1] = '\0';
            //fprintf(stderr, "LABEL-LINE: %s", line);

            double start, end;
            const char *labelString = NULL;
            const char *token = strtok(line,"\t");
            if (token != NULL) start = atof(token);
            token = strtok(NULL, "\t");
            if (token != NULL) end = atof(token);
            token = strtok(NULL, "\t\r\n");
            if (token != NULL) labelString = token;

            if (labelString == NULL) {
                fprintf(stderr, "ERROR: Labels file line %zu does not contain required values.\n", lineNumber);
            } else {
                size_t labelId = AudioIdGetLabelId(audioid, labelString);
                fprintf(stderr, "LABEL: %zu/%s (%0.2f-%0.2f)\n", labelId, labelString, start, end);
            }
        }
        fclose(fp);
    }

    if (audioid->filename != NULL) {
        fprintf(stderr, "AUDIOID: Opening sound file: %s\n", audioid->filename);

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
                if (audioid->verbose) fprintf(stderr, "READ: %d\n", (int)framesRead);
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
    AudioIdFreeLabels(audioid);
}
