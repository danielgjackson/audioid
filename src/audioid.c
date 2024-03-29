// AudioId - Daniel Jackson, 2022.

// TODO: Divide into components with smaller responsibilities.

#ifdef _MSC_VER
    #define _CRT_SECURE_NO_WARNINGS     // fopen / strtok
    #define _CRT_NONSTDC_NO_DEPRECATE   // strdup
#endif

#ifdef _WIN32
    #define _USE_MATH_DEFINES
	#define USE_FTIME
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#ifdef USE_FTIME
    #include <sys/timeb.h>
#endif

#include "miniaudio.h"
#include "dr_wav.h"
#include "minfft.h"

#include "audioid.h"

#define AUDIOID_SAMPLE_RATE 16000
#define AUDIOID_VERBOSE false
#define WINDOW_OVERLAP 2        // <=1 = none, 2 = half
#define HAMMING_WEIGHT 0.53836  // 25.0/46.0
#define FFT_WINDOW_SIZE 2048    // 1024+1 results
#define FFT_BUCKET_COUNT 256    // 128
#define AUDIOID_DEFAULT_CYCLE_COUNT (4*WINDOW_OVERLAP)  // 8
#define LOG_SCALE
#define LABEL_ID_UNKNOWN (-1)
#define MODAL_SIZE ((AUDIOID_DEFAULT_CYCLE_COUNT) * 150 / 100)
#define MAX_STATES 64
#define REPORT_MAX_INTERVAL 1.0


// Returns the number of seconds since the epoch
static double TimeNow()
{
#ifdef USE_FTIME
	struct timeb tp;
	ftime(&tp);
	return ((unsigned long long)tp.time * 1000 + tp.millitm) / 1000.0;
#else
	struct timespec tp;
	if (clock_gettime(CLOCK_REALTIME, &tp) == -1) return 0;
	return (double)tp.tv_sec + (tp.tv_nsec / 1000000000.0);
#endif
}


// Reduced loss of precision for running stats, informed by: https://www.johndcook.com/blog/standard_deviation/
typedef struct {
	unsigned int count;
	double mean, sumVar;
    //double min, max;
} running_stats_t;
void running_stats_clear(running_stats_t *self) {
	self->count = 0;
}
void running_stats_add(running_stats_t *self, double x) {
    double newMean, newSumVar;
	self->count++;
	if (self->count == 1) {
		newMean = x;
		newSumVar = 0;
		// self->min = x;
		// self->max = x;
	} else {
		newMean = self->mean + (x - self->mean) / self->count;
		newSumVar = self->sumVar + (x - self->mean) * (x - newMean);
	}
	self->mean = newMean;
    self->sumVar = newSumVar;
	// if (x < self->min) self->min = x;
	// if (x > self->max) self->max = x;
}
unsigned int running_stats_count(running_stats_t *self) {
	return self->count;
}
double running_stats_mean(running_stats_t *self) {
	if (self->count == 0) return 0;
	return self->mean;
}
double running_stats_variance(running_stats_t *self) {
	if (self->count <= 1) return 0;
	return self->sumVar / (self->count - 1);
}
double running_stats_stddev(running_stats_t *self) {
	return sqrt(running_stats_variance(self));
}
// double running_stats_range(running_stats_t *self) {
// 	if (self->count == 0) return 0;
// 	return self->max - self->min;
// }


// Hamming window function (http://en.wikipedia.org/wiki/Window_function)
static double HammingWindow(size_t index, size_t size) {
    const double weight = HAMMING_WEIGHT;   // 0.53836;  // 25.0/46.0
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

static void DebugVisualizeValues(running_stats_t *values, size_t count, bool showMatch, int groupMatchInterval, const char *closestGroup, const char *closestLabel, double closestDistance) {
    const int mode = 2;    // 0=solid block, 1=left-half block, 2=buffer previous line and upper-half block
    static double *buffer = NULL;   // horrible (non-threadsafe) hack to buffer previous line so output can be two virtual lines per physical line
    char thisResult[256] = "";
    static char lastResult[256] = "";   // horrible (non-threadsafe) hack to buffer previous line end so output can be two virtual lines per physical line
    static size_t bufferSize = 0;
    static int bufferLine = 0;

    thisResult[0] = '\0';
    if (showMatch) {
        const char *color = "";
        if (groupMatchInterval == 0) {
            color = "\x1b[31m"; // "XXX";
        } else if (groupMatchInterval == 1) {
            color = "\x1b[32m"; // "!!!";
        }
        sprintf(thisResult, " %s%.5s %0.2f\x1b[0m", color, closestLabel ? closestLabel : "-", closestDistance);
    }

    if (mode == 2 && bufferSize < count) {
        bufferSize = count;
        buffer = realloc(buffer, sizeof(double) * bufferSize);
        if (buffer == NULL) { fprintf(stderr, "ERROR: Memory failure (buffer).\n"); exit(-1); }
    }
    if (mode == 2 && (bufferLine & 1) == 0) {
        for (size_t x = 0; x < count; x++) {
            buffer[x] = running_stats_mean(&values[x]);
        }
        strcpy(lastResult, thisResult);
        strcat(lastResult, "\t");
        thisResult[0] = '\0';
    } else {
        for (size_t x = 0; x < count; x++) {
            double v = running_stats_mean(&values[x]);
            unsigned int c = Gradient(v);
            if (mode == 1) {
                if ((x & 1) == 1) {
                    double vPrev = running_stats_mean(&values[x - 1]);
                    unsigned int cPrev = Gradient(vPrev);
                    // Left-half block - Unicode: \u258c - UTF-8: \xe2\x96\x8c
                    printf(u8"\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm\u258c", (unsigned char)(cPrev>>0), (unsigned char)(cPrev>>8), (unsigned char)(cPrev>>16), (unsigned char)(c>>0), (unsigned char)(c>>8), (unsigned char)(c>>16));
                }
            } else if (mode == 2) {
                if ((bufferLine & 1) == 1) {
                    unsigned int cPrev = Gradient(buffer[x]);
                    // Upper-half block - Unicode: \u2580 - UTF-8: \xe2\x96\x80
                    printf(u8"\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm\u2580", (unsigned char)(cPrev>>0), (unsigned char)(cPrev>>8), (unsigned char)(cPrev>>16), (unsigned char)(c>>0), (unsigned char)(c>>8), (unsigned char)(c>>16));
                }
            } else {
                // Full Block - Unicode: \u2588 - UTF-8: \xe2\x96\x88
                printf(u8"\x1b[38;2;%d;%d;%dm\u2588", (unsigned char)(c>>0), (unsigned char)(c>>8), (unsigned char)(c>>16));
            }
        }        
        printf("\x1b[0m%s%s\n", lastResult, thisResult);
    }
    fflush(stdout);
    bufferLine++;
}


// Fingerprint state
typedef struct fingerprint_tag {
    size_t maxSamples;      // number of samples per FFT (can be windowed at WINDOW_OVERLAP)
    size_t countResults;    // (maxSamples/2)+1
    size_t countBuckets;    // count of quantized bucket
    size_t cycleCount;      // length of cycle stats are accumulated over
    double *input;          // user-supplied input, converted to floating point
    minfft_real *weighted;  // window-weighted values before FFT
    minfft_cmpl *output;    // complex output of FFT
    minfft_aux *aux;        // auxillary data needed for FFT
    double *magnitude;      // magnitude of each output
    double *buckets;        // mean magnitude into fewer buckets
    running_stats_t **stats;// stats for each bucket, repeated for overlap cycle of buckets
    double *meanStats;      // mean stats
    size_t sampleOffset;    // index for next sample
    size_t cycle;           // index of stats cycle 
} fingerprint_t;

void FingerprintResetStats(fingerprint_t *fingerprint, size_t cycle) {
    // Clear stats
    for (size_t i = 0; i < fingerprint->countBuckets; i++) {
        running_stats_clear(&fingerprint->stats[cycle][i]);
    }
}

void FingerprintAccumulateStats(fingerprint_t *fingerprint) {
    // Reset the oldest phase
    FingerprintResetStats(fingerprint, fingerprint->cycle);
    // Next phase
    fingerprint->cycle = (fingerprint->cycle + 1) % fingerprint->cycleCount;
    // Add to all phases of the cycle
    for (size_t j = 0; j < fingerprint->cycleCount; j++) {
        for (size_t i = 0; i < fingerprint->countBuckets; i++) {
            running_stats_add(&fingerprint->stats[j][i], fingerprint->buckets[i]);
        }
    }
}

running_stats_t *FingerprintStats(fingerprint_t *fingerprint) {
    return fingerprint->stats[fingerprint->cycle];
}

/*
double *FingerprintMeanStats(fingerprint_t *fingerprint) {
    // Calculate mean stats for the current phase
    for (size_t i = 0; i < fingerprint->countBuckets; i++) {
        fingerprint->meanStats[i] = running_stats_mean(&fingerprint->stats[fingerprint->cycle][i]);
    }
    return fingerprint->meanStats;
}
*/

void FingerprintInit(fingerprint_t *fingerprint, size_t maxSamples, size_t countBuckets, size_t cycleCount) {
    memset(fingerprint, 0, sizeof(*fingerprint));
    fingerprint->maxSamples = maxSamples;
    fingerprint->countBuckets = countBuckets;
    fingerprint->cycleCount = cycleCount;
    fingerprint->countResults = (fingerprint->maxSamples / 2) + 1;
    fingerprint->sampleOffset = 0;
    fingerprint->aux = minfft_mkaux_realdft_1d((int)fingerprint->maxSamples);
    fingerprint->input = malloc(sizeof(double) * fingerprint->maxSamples);
    fingerprint->weighted = malloc(sizeof(minfft_real) * fingerprint->maxSamples);
    fingerprint->output = malloc(sizeof(minfft_cmpl) * fingerprint->countResults);
    fingerprint->magnitude = malloc(sizeof(double) * fingerprint->countResults);
    fingerprint->buckets = malloc(sizeof(double) * fingerprint->countBuckets);
    fingerprint->stats = malloc(sizeof(running_stats_t*) * fingerprint->countBuckets);
    for (size_t i = 0; i < fingerprint->cycleCount; i++) {
        fingerprint->stats[i] = malloc(sizeof(running_stats_t) * fingerprint->countBuckets);
    }
    for (size_t i = 0; i < fingerprint->cycleCount; i++) {
        FingerprintResetStats(fingerprint, i);
    }
    fingerprint->meanStats = malloc(sizeof(double) * fingerprint->countBuckets);
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
    if (fingerprint->stats != NULL) {
        free(fingerprint->stats);
        fingerprint->stats = NULL;
    }
    if (fingerprint->meanStats != NULL) {
        free(fingerprint->meanStats);
        fingerprint->meanStats = NULL;
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
        memmove(fingerprint->input, fingerprint->input + offset, length * sizeof(double));
        fingerprint->sampleOffset = length;
#else
        fingerprint->sampleOffset = 0;
#endif
    }

    // Determine how many of these samples will be used
    size_t samplesRemaining = fingerprint->maxSamples - fingerprint->sampleOffset;
    size_t samplesUsed = sampleCount > samplesRemaining ? samplesRemaining : sampleCount;

    // Add up to sampleCount samples to fingerprint->input (scaled as floating point real data)
    for (size_t i = 0; i < samplesUsed; i++) {
        size_t index = fingerprint->sampleOffset + i;
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
        size_t startFFT = 0;
        size_t countFFT = fingerprint->countResults;
#ifdef LOG_SCALE
        double logScale = log((double)countFFT) / log((double)fingerprint->countBuckets);
#endif
        for (size_t i = 0; i < fingerprint->countBuckets; i++) {
#ifdef LOG_SCALE
            size_t iStartAt = startFFT + (size_t)pow((double)i, logScale);
            size_t iEndBefore = startFFT + (size_t)pow((double)(i + 1), logScale);
//printf(">>> %d -> [%d-%d)\n", i, iStartAt, iEndBefore); fflush(stdout); if (i + 1 >= fingerprint->countBuckets) exit(1);
#else
            size_t iStartAt = startFFT + i * countFFT / fingerprint->countBuckets;
            size_t iEndBefore = startFFT + (i + 1) * countFFT / fingerprint->countBuckets;
#endif
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

double Distance(size_t countBuckets, running_stats_t *buckets, running_stats_t *stats) {
#if 0
    // Cosine similarity
    double sumAB = 0;
    double sumAA = 0;
    double sumBB = 0;
    for (size_t i = 0; i < countBuckets; i++) {
        double a = running_stats_mean(&stats[i]);
        double b = running_stats_mean(&buckets[i]);
        sumAB += a * b;
        sumAA += a * a;
        sumBB += b * b;
    }
    double divisor = sqrt(sumAA) * sqrt(sumBB);

    // Range -1=opposite to 1=same 
    double cosineSimilarity;
    if (divisor < 0.00001) {
        cosineSimilarity = 0;
    } else {
        cosineSimilarity = sumAB / divisor;
    }

    return 1.0 - cosineSimilarity;
#elif 0
    // Distribution comparison
    double sumZ = 0;
    for (size_t i = 0; i < countBuckets; i++) {
        double meanA = running_stats_mean(&stats[i]);
        double stddevA = running_stats_stddev(&stats[i]);
        double countA = running_stats_count(&stats[i]);
        double meanB = running_stats_mean(&buckets[i]);
        double stddevB = running_stats_stddev(&buckets[i]);
        double countB = running_stats_count(&buckets[i]);

        double sigmaA = countA > 0 ? stddevA / sqrt(countA) : 0;
        double sigmaB = countB > 0 ? stddevB / sqrt(countB) : 0;
        double divisor = sqrt(sigmaA * sigmaA + sigmaB * sigmaB);

        double meanDiff = meanA - meanB;
        double z = meanDiff / (divisor > 0 ? divisor : 1);

        sumZ += fabs(z);
    }
    return sumZ;
#elif 0
    // TF-IDF-inspired
    #error "Not implemented"

#elif 0
    // Distance (normalized)
    double sumAA = 0;
    double sumBB = 0;
    for (size_t i = 0; i < countBuckets; i++) {
        double a = running_stats_mean(&stats[i]);
        double b = running_stats_mean(&buckets[i]);
        sumAA += a * a;
        sumBB += b * b;
    }
    double normA = sqrt(sumAA);
    double normB = sqrt(sumBB);
    if (normA < 0.001) normA = 0.001;
    if (normB < 0.001) normB = 0.001;

    double totalDistance = 0;
    for (size_t i = 0; i < countBuckets; i++) {
        double a = running_stats_mean(&stats[i]) / normA;
        double b = running_stats_mean(&buckets[i]) / normB;
        double diff = b - a;
        double dist = sqrt(diff * diff);
        totalDistance += dist;
    }
    double result = totalDistance / countBuckets;
    return result;
#elif 1
    // Distance (not normalized)
    double totalDistance = 0;
    for (size_t i = 0; i < countBuckets; i++) {
        double a = running_stats_mean(&stats[i]);
        double b = running_stats_mean(&buckets[i]);
        double diff = b - a;
        double dist = sqrt(diff * diff);
        totalDistance += dist;
    }
    double result = totalDistance / countBuckets;
    return result;
#else
    #error "No distance metric"
#endif
}


typedef struct interval_tag {
    size_t id;      // label id for this interval
    double start;
    double end;
} interval_t;

typedef struct label_tag {
    const char *labelText;
    const char *labelGroup;
    running_stats_t *stats;
    double scale;
    double limit;
    size_t matchingGroup;
    double minDuration;
    int onlyAfterEvent;
    double onlyWithinInterval;
    double lastFinished;
} label_t;

// Detector state
typedef struct audioid_tag {
    // Configuration
    const char *filename;
    const char *labelFile;

    unsigned int sampleRate;
    size_t windowSize;
    size_t countBuckets;
    size_t cycleCount;
    bool verbose;
    int visualize;
    bool learn;

    // Audio device capture
    ma_device_config deviceConfig;
    ma_device device;
    bool deviceInitialized;

    // Audio file input
    ma_decoder_config decoderConfig;
    ma_decoder decoder;
    bool decoderInitialized;

    // Labels
    size_t countLabels;
    label_t *labels;

    // Intervals
    interval_t *intervals;
    size_t maxIntervals;
    size_t countIntervals;
    interval_t *lastInterval;
    size_t nextInterval;

    // State
    size_t totalSamples;
    int stateIndex;
    int lastState;
    int stateHistory[MODAL_SIZE];
    double stateChangeTime;
    double lastReport;
    bool stateLatched;

    // Current FFT fingerprint
    fingerprint_t fingerprint;
} audioid_t;

static const char *AudioIdGetLabelName(audioid_t *audioid, size_t id) {
    if (id >= audioid->countLabels) return NULL;
    return audioid->labels[id].labelText;
}

static size_t AudioIdGetLabelId(audioid_t *audioid, const char *labelText) {
    // Return existing label id
    for (size_t id = 0; id < audioid->countLabels; id++) {
        if (strcmp(audioid->labels[id].labelText, labelText) == 0) {
            return id;
        }
    }

    // Check limits
    if (audioid->countLabels >= MAX_STATES) {
        fprintf(stderr, "ERROR: Too many states (%zu)\n", audioid->countLabels);
        exit(-1);
    }

    // Add the new label
    audioid->labels = (label_t *)realloc((void *)audioid->labels, sizeof(label_t) * (audioid->countLabels + 1));
    if (audioid->labels == NULL) { fprintf(stderr, "ERROR: Memory failure (labels).\n"); exit(-1); }

    audioid->labels[audioid->countLabels].labelText = strdup(labelText);

    // Initial '?'/'!'/ prefix to flag label (unused)
    bool flagged = (labelText[0] == '?') || (labelText[0] == '!');

    // Add the new label's group
    char *group = strdup(labelText + (flagged ? 1 : 0)); // Duplicate (and remove flag prefix)
    char *groupSep = strchr(group, '/');
    if (groupSep != NULL) *groupSep = '\0'; // Leave only the group
    audioid->labels[audioid->countLabels].labelGroup = group;

    // Stats
    audioid->labels[audioid->countLabels].stats = (running_stats_t *)malloc(sizeof(running_stats_t) * audioid->countBuckets);
    for (size_t i = 0; i < audioid->countBuckets; i++) {
        running_stats_clear(&audioid->labels[audioid->countLabels].stats[i]);
    }

    // Other per-label values
    audioid->labels[audioid->countLabels].scale = 1.0;
    audioid->labels[audioid->countLabels].limit = -1.0; // < 0 = no limit applied
    audioid->labels[audioid->countLabels].minDuration = -1.0; // < 0 = disabled
    audioid->labels[audioid->countLabels].onlyAfterEvent = LABEL_ID_UNKNOWN;
    audioid->labels[audioid->countLabels].onlyWithinInterval = 0.0;
    audioid->labels[audioid->countLabels].lastFinished = -1.0;

    // Find an earlier matching group
    size_t matchingGroup = audioid->countLabels;
    for (size_t id = 0; id < audioid->countLabels; id++) {
        if (strcmp(audioid->labels[id].labelGroup, group) == 0) {
            matchingGroup = id;
            break;
        }
    }
    audioid->labels[audioid->countLabels].matchingGroup = matchingGroup;

    // Return the new label id
    return audioid->countLabels++;
}

static void AudioIdFreeLabels(audioid_t *audioid) {
    for (size_t id = 0; id < audioid->countLabels; id++) {
        free((void *)audioid->labels[id].labelText);
        free((void *)audioid->labels[id].labelGroup);
        free((void *)audioid->labels[id].stats);
    }
    free(audioid->labels);
    audioid->labels = NULL;
    audioid->countLabels = 0;
}



static size_t AudioIdAddInterval(audioid_t *audioid, const char *label, double start, double end) {
    // Allocate more space
    if (audioid->countIntervals >= audioid->maxIntervals) {
        audioid->maxIntervals += audioid->maxIntervals + 1;
        audioid->intervals = (interval_t *)realloc(audioid->intervals, sizeof(interval_t) * audioid->maxIntervals);
        if (audioid->intervals == NULL) { fprintf(stderr, "ERROR: Memory failure (intervals).\n"); exit(-1); }
    }
    interval_t *newInterval = &audioid->intervals[audioid->countIntervals];
    memset(newInterval, 0, sizeof(*newInterval));
    newInterval->id = AudioIdGetLabelId(audioid, label);
    newInterval->start = start;
    newInterval->end = end;
    if (audioid->countIntervals > 0 && start < audioid->intervals[audioid->countIntervals - 1].end) {
        fprintf(stderr, "WARNING: Interval #%zu starts (%0.2f) before the previous interval ends (%0.2f) -- intervals must not overlap.\n", audioid->countIntervals, start, audioid->intervals[audioid->countIntervals - 1].end);
    }
    if (end < start) {
        fprintf(stderr, "WARNING: Interval #%zu ends (%0.2f) before it starts (%0.2f) -- does not form a valid interval.\n", audioid->countIntervals, end, start);
    }
    audioid->countIntervals++;
    return audioid->countIntervals - 1;
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
            // Current recording time
            double time = (double)audioid->totalSamples / audioid->sampleRate;

            // For live recordings, use the system epoch time
            if (audioid->filename == NULL) {
                time = TimeNow();
            }

            // If we are making our way through the labelled intervals...
            interval_t *interval = NULL;
            if (audioid->countIntervals > 0) {
                // Advance until we are within one
                while (audioid->nextInterval < audioid->countIntervals) {
                    // Next interval has not started yet (between intervals)
                    if (time < audioid->intervals[audioid->nextInterval].start) {
                        break;
                    }
                    // Within the next interval
                    if (time < audioid->intervals[audioid->nextInterval].end) {
                        interval = &audioid->intervals[audioid->nextInterval];
                        break;
                    }
                    // After the next interval
                    audioid->nextInterval++;
                }
            }

            // Interval boundary
            if (audioid->lastInterval != interval) {
                if (audioid->verbose || true) {
                    if (audioid->lastInterval != NULL) {
                        fprintf(stderr, "\n--- END INTERVAL ---\n");
                    }

                    if (interval != NULL) {
                        fprintf(stderr, "\n--- @%.2f INTERVAL #%d (%.2f-%.2f): %s ---\n", time, (int)audioid->nextInterval, audioid->intervals[audioid->nextInterval].start, audioid->intervals[audioid->nextInterval].end, AudioIdGetLabelName(audioid, audioid->intervals[audioid->nextInterval].id));
                    }
                }
                audioid->lastInterval = interval;
            }

            // Add stats to current interval
            if (interval != NULL && audioid->learn) {
                size_t id = interval->id;
                running_stats_t *stats = audioid->labels[id].stats;
                for (size_t i = 0; i < audioid->countBuckets; i++) {
                    running_stats_add(&stats[i], audioid->fingerprint.buckets[i]);
                }
            }

            // Add to cycled stats (only 1-cycle in learning mode)
            FingerprintAccumulateStats(&audioid->fingerprint);
            running_stats_t *inputStats = FingerprintStats(&audioid->fingerprint);

//buckets = FingerprintMeanStats(&audioid->fingerprint);

            // Recognition mode
            int closestLabel = LABEL_ID_UNKNOWN;
            double closestDistance = 0;
            if (!audioid->learn) {
                for (size_t id = 0; id < audioid->countLabels; id++) {
                    running_stats_t *stats = audioid->labels[id].stats;
                    double scale = audioid->labels[id].scale;
                    double limit = audioid->labels[id].limit;
                    double rawDistance = Distance(audioid->countBuckets, inputStats, stats);
                    double distance = scale * rawDistance;
                    bool withinLimit = (limit < 0) || (distance < limit);
                    if (withinLimit && (closestLabel == LABEL_ID_UNKNOWN || distance < closestDistance)) {
                        closestLabel = (int)id;
                        closestDistance = distance;
                    }
                }


                // ------ STATE ------
                // State is matching group
                int thisState = (closestLabel == LABEL_ID_UNKNOWN) ? LABEL_ID_UNKNOWN : (int)audioid->labels[closestLabel].matchingGroup;

                // Add to modal filter
                audioid->stateHistory[audioid->stateIndex % MODAL_SIZE] = thisState;
                audioid->stateIndex++;

                // Modal filter
                int unknownCount = 0;
                int matchingGroupCount[MAX_STATES] = {0};
                for (size_t i = 0; i < MODAL_SIZE; i++) {
                    int label = audioid->stateHistory[i];
                    if (label == LABEL_ID_UNKNOWN) {
                        unknownCount++;
                    } else if (label < audioid->countLabels) {
                        size_t group = audioid->labels[label].matchingGroup;
                        if (group < MAX_STATES) matchingGroupCount[group]++;
                        else { fprintf(stderr, "ERROR: Internal error in group index (@%zu = label %d = group %zu)\n", i, label, group); exit(-1); }
                    } else { fprintf(stderr, "ERROR: Internal error in history label (@%zu = label %d)\n", i, label); exit(-1); }
                }
                int currentState = LABEL_ID_UNKNOWN;
                int maxCount = unknownCount;
                for (size_t i = 0; i < audioid->countLabels; i++) {
                    if (matchingGroupCount[i] > maxCount) {
                        maxCount = matchingGroupCount[i];
                        currentState = (int)i;
                    }
                }

                // Previous state duration
                double duration = time - audioid->stateChangeTime;

                // Hypothesis change
                if (currentState != audioid->lastState) {
                    // Latched event end
                    if (audioid->lastState != LABEL_ID_UNKNOWN && audioid->stateLatched) {
                        audioid->labels[audioid->lastState].lastFinished = time;
                        if (!audioid->visualize) {
                            fprintf(stdout, "%.3f\te:end\t%s\t%.3f\n", time, (audioid->lastState == LABEL_ID_UNKNOWN ? "-" : audioid->labels[audioid->lastState].labelGroup), duration);
                            fflush(stdout);
                        }
                    }
                    audioid->stateLatched = false;
                    audioid->stateChangeTime = time;
                    audioid->lastState = currentState;
                    duration = 0;
                    audioid->lastReport = 0;    // force report
                }

                // Report current state
                bool report = false;
                report |= audioid->lastReport == 0; // just changed
                report |= time >= audioid->lastReport + REPORT_MAX_INTERVAL;    // maximum interval exceeded
                if (report) {
                    // Report 'hear'
                    if (!audioid->visualize) {
                        fprintf(stdout, "%.3f\t%s\t%s\t%.3f\n", time, audioid->stateLatched ? "e:cont" : "hear", (currentState == LABEL_ID_UNKNOWN ? "-" : audioid->labels[currentState].labelGroup), duration);
                        fflush(stdout);
                    }
                    audioid->lastReport = time;
                }

                // Latch state?
                if (!audioid->stateLatched && currentState != LABEL_ID_UNKNOWN) {
                    bool latch = true;
                    // If there is not required minimum duration set (>=0): do not latch; or there is and it has not yet occurred: do not latch
                    if (audioid->labels[currentState].minDuration < 0) latch = false;
                    if (audioid->labels[currentState].minDuration >= 0 && duration < audioid->labels[currentState].minDuration) latch = false;
                    // If there is a required previous event...
                    if (audioid->labels[currentState].onlyAfterEvent >= 0) {
                        double lastFinished = audioid->labels[audioid->labels[currentState].onlyAfterEvent].lastFinished;
                        double onlyWithinInterval = audioid->labels[currentState].onlyWithinInterval;
                        // ...if it has not occurred, or finished longer ago than the maximum interval before this event started: do not latch 
                        if (lastFinished < 0 || time > lastFinished + onlyWithinInterval + duration) latch = false;
                    }
                    if (latch) {
                        fprintf(stdout, "%.3f\te:start\t%s\t%.3f\n", time, (audioid->lastState == LABEL_ID_UNKNOWN ? "-" : audioid->labels[audioid->lastState].labelGroup), duration);
                        audioid->stateLatched = true;
                        audioid->lastReport = time;
                    }
                }

                // ------------
            }

            // Output
            if (audioid->verbose) fprintf(stderr, ">>> %d results.\n", (int)countResults);
            if (audioid->visualize) {
if (audioid->visualize == 1 || (audioid->visualize == 2 && ((audioid->learn || audioid->labelFile == NULL || (interval != NULL && strcmp(audioid->labels[interval->id].labelGroup, "silence") != 0)) && audioid->fingerprint.cycle == 0))) // only output labelled regions
{
                const char *closestLabelName = closestLabel == LABEL_ID_UNKNOWN ? NULL : audioid->labels[closestLabel].labelText;
                const char *closestGroupName = closestLabel == LABEL_ID_UNKNOWN ? NULL : audioid->labels[closestLabel].labelGroup;
                const char *intervalGroup = interval != NULL ? audioid->labels[interval->id].labelGroup : NULL;
                
                bool showMatch = !audioid->learn;

                int groupMatchInterval = -1;    // don't care
                if (intervalGroup != NULL) {
                    if (closestGroupName == NULL || strcmp(intervalGroup, closestGroupName) != 0) {
                        groupMatchInterval = 0; // Does not match
                    } else {
                        groupMatchInterval = 1; // Matches
                    }
                }

                DebugVisualizeValues(inputStats, countResults, showMatch, groupMatchInterval, closestGroupName, closestLabelName, closestDistance);
}
            }



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
    AudioIdInit(audioid, false);
    return audioid;
}

// Destroy the audioid object, this will also shutdown the object.
void AudioIdDestroy(audioid_t *audioid) {
    AudioIdShutdown(audioid);
    free(audioid);
}

// Initialize an audioid object with an empty configuration
void AudioIdInit(audioid_t *audioid, int visualize) {
    memset(audioid, 0, sizeof(*audioid));

    // Defaults
    audioid->sampleRate = AUDIOID_SAMPLE_RATE;
    audioid->windowSize = FFT_WINDOW_SIZE; // 2048 / AUDIOID_SAMPLE_RATE = 0.128s // 1024+1 results
    audioid->countBuckets = FFT_BUCKET_COUNT; // 128
    audioid->verbose = AUDIOID_VERBOSE;
    audioid->visualize = visualize;
    audioid->cycleCount = AUDIOID_DEFAULT_CYCLE_COUNT;

    // State
    for (size_t i = 0; i < sizeof(audioid->stateHistory) / sizeof(audioid->stateHistory[0]); i++) {
        audioid->stateHistory[i] = LABEL_ID_UNKNOWN;
    }
    audioid->stateIndex = 0;
    audioid->lastState = LABEL_ID_UNKNOWN;
}

// Configure to learn from labelled audio
void AudioIdConfigLearn(audioid_t *audioid, const char *filename, const char *labelFile) {
    audioid->learn = true;
    audioid->filename = filename;
    audioid->labelFile = labelFile;
}

// Configure to recognize from audio -- optionally pre-recoded audio from a file, live captured audio otherwise; optionally with labels for diagnostic use
void AudioIdConfigRecognize(audioid_t *audioid, const char *filename, const char *labelFile) {
    audioid->learn = false;
    audioid->filename = filename;
    audioid->labelFile = labelFile;
}

// Start audio processing on an audioid object
bool AudioIdStart(audioid_t *audioid) {
    ma_result result;

    FingerprintInit(&audioid->fingerprint, audioid->windowSize, audioid->countBuckets, audioid->cycleCount);

    if (audioid->labelFile != NULL) {
        fprintf(stderr, "AUDIOID: Opening label file: %s\n", audioid->labelFile);
        FILE *fp = fopen(audioid->labelFile, "r");
        if (fp == NULL) {
            fprintf(stderr, "ERROR: Problem opening label file: %s\n", audioid->labelFile);
            return false;
        }
        char lineBuffer[256];
        for (size_t lineNumber = 1; ; lineNumber++) {
            char *line = fgets(lineBuffer, sizeof(lineBuffer) - 1, fp);
            if (line == NULL) break;

            double start = -1, end = -1;
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
                AudioIdAddInterval(audioid, labelString, start, end);
            }
        }
        fclose(fp);

        // Display intervals
        for (size_t i = 0; i < audioid->countIntervals; i++) {
            interval_t *interval = &audioid->intervals[i];
            if (audioid->verbose) fprintf(stderr, "INTERVAL: #%zu %zu/%s (%0.2f-%0.2f)\n", i + 1, interval->id, AudioIdGetLabelName(audioid, interval->id), interval->start, interval->end);
        }
    }

    if (audioid->filename != NULL) {
        if (audioid->verbose) fprintf(stderr, "AUDIOID: Opening sound file: %s\n", audioid->filename);

        audioid->decoderConfig = ma_decoder_config_init(ma_format_s16, 1, audioid->sampleRate);

        result = ma_decoder_init_file(audioid->filename, &audioid->decoderConfig, &audioid->decoder);
        if (result != MA_SUCCESS) {
            fprintf(stderr, "ERROR: Could not load file: %s\n", audioid->filename);
            return false;
        }
        audioid->decoderInitialized = true;
    } else {
        if (audioid->verbose) fprintf(stderr, "AUDIOID: Configuring audio capture...\n");

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

        if (audioid->verbose) fprintf(stderr, "AUDIOID: ...audio capture configured.\n");
        audioid->deviceInitialized = true;
    }
    return true;
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

// Load state
bool AudioIdStateLoad(audioid_t *audioid, const char *filename) {
    int errors = 0;

    FILE *fp = fopen(filename, "rt");
    if (!fp) {
        fprintf(stderr, "ERROR: Problem opening state file for reading: %s\n", filename);
        return false;
    }

    bool globalSection = true;
    size_t labelId = -1;
    const size_t bufferSize = 65536;
    char *buffer = (char *)malloc(bufferSize);
    if (!buffer) {
        fprintf(stderr, "ERROR: Problem allocating memory for reading: %zu\n", bufferSize);
        fclose(fp);
        return false;
    }

    for (size_t lineNumber = 1; ; lineNumber++) {
        // Read next line
        char *line = fgets(buffer, (int)bufferSize - 1, fp);
        buffer[bufferSize - 1] = '\0';

        // EOF (or read error)
        if (line == NULL) break;

        // Trim trailing whitespace
        for (char *end = line + strlen(line); end > line && (*(end - 1) == '\n' || *(end - 1) == '\r' || *(end - 1) == ' '); end--) *(end - 1) = '\0';

        // Empty line
        if (line[0] == '\0') continue;

        // Comment line
        if (line[0] == '#') continue;

        // Label section
        if (line[0] == '[') {
            if (line[1] == ']') {
                globalSection = true;
            } else {
                char *label = strtok(line + 1, "]\r\n");
                labelId = AudioIdGetLabelId(audioid, label);
                globalSection = false;
            }
            continue;
        }

        // Split into name/value pair
        static char *empty = "";
        const char *name = line;
        char *value = empty;
        char *equals = strchr(line, '=');
        if (equals != NULL) {
            *equals = '\0';
            value = equals + 1;
            // Trim space either side of the equals
            for (char *p = equals; p > line && *(p - 1) == ' '; p--) *(p - 1) = '\0'; 
            for (char *p = equals + 1; *p == ' '; p++) { *p = '\0'; value = p + 1; }
            // Trim quotes
            if (value[0] == '\"') value++;
            if (value[0] != '\0' && value[strlen(value) - 1] == '\"') value[strlen(value) - 1] = '\0';
        }

        if (globalSection) {
            if (strcmp(name, "bucketcount") == 0) {
                size_t bucketCount = (size_t)atoi(value);
                if (bucketCount != audioid->countBuckets) {
                    fprintf(stderr, "ERROR: State file was saved with a different bucket count (%zu) to this program (%zu) and is not compatible: %s\n", bucketCount, audioid->countBuckets, filename);
                    errors++;
                }
            } else {
                fprintf(stderr, "ERROR: Problem reading state file %s global-section line %zu unrecognized name: %s\n", filename, lineNumber, name);
                errors++;
            }
        } else {
            if (strcmp(name, "stats") == 0) {
                // Read stats
                size_t index = 0;
                char *stat = value;
                for (;;) {
                    // Trim and find end of token
                    while (*stat == ' ') stat++;
                    if (*stat == '\0') break;
                    char *end = strchr(stat, ';');
                    if (end != NULL) { *end = '\0'; }

                    char *count = strtok(stat, " ");
                    char *mean = strtok(NULL, " ");
                    char *sumVar = strtok(NULL, " ");
                    if (count != NULL && mean != NULL && sumVar != NULL) {
                        if (index < audioid->countBuckets) {
                            running_stats_t *stats = &audioid->labels[labelId].stats[index];
                            stats->count = atoi(count);
                            stats->mean = atoi(mean);
                            stats->sumVar = atoi(sumVar);
                        } else {
                            fprintf(stderr, "ERROR: Problem reading state file %s section %s line %zu stat index %zu exceeds bucket count %zu: %s\n", filename, audioid->labels[labelId].labelText, lineNumber, index, audioid->countBuckets, name);
                            errors++;
                        }
                        index++;
                    } else {
                        fprintf(stderr, "ERROR: Problem reading state file %s section %s line %zu malformed stat at index %zu: %s\n", filename, audioid->labels[labelId].labelText, lineNumber, index, name);
                        errors++;
                    }

                    // Next token
                    if (!end) break;
                    stat = end + 1; 
                }
                
                if (index != audioid->countBuckets) {
                    fprintf(stderr, "ERROR: Problem reading state file %s section %s line %zu stat count %zu does not equal bucket count %zu: %s\n", filename, AudioIdGetLabelName(audioid, labelId), lineNumber, index, audioid->countBuckets, name);
                    errors++;
                }
            } else if (strcmp(name, "scale") == 0) {
                audioid->labels[labelId].scale = atof(value);
            } else if (strcmp(name, "limit") == 0) {
                audioid->labels[labelId].limit = atof(value);
            } else if (strcmp(name, "minduration") == 0) {
                audioid->labels[labelId].minDuration = atof(value);
            } else if (strcmp(name, "afterevent") == 0) {
                audioid->labels[labelId].onlyAfterEvent = (int)AudioIdGetLabelId(audioid, value);
            } else if (strcmp(name, "withininterval") == 0) {
                audioid->labels[labelId].onlyWithinInterval = atof(value);
            } else {
                fprintf(stderr, "ERROR: Problem reading state file %s section %s line %zu unrecognized name: %s\n", filename, AudioIdGetLabelName(audioid, labelId), lineNumber, name);
                errors++;
            }
        }
    }
    free(buffer);
    fclose(fp);
    return (errors == 0);
}

// Save state
bool AudioIdStateSave(audioid_t *audioid, const char *filename) {
    FILE *fp = fopen(filename, "wt");
    if (!fp) {
        fprintf(stderr, "ERROR: Problem opening state file for writing: %s\n", filename);
        return false;
    }

    fprintf(fp, "# AudioID state file -- this file will be overwritten if the --write-state option is used\n");
    fprintf(fp, "\n");
    fprintf(fp, "bucketcount = %zu\n", audioid->countBuckets);
    fprintf(fp, "\n");
    for (size_t id = 0; id < audioid->countLabels; id++) {
        fprintf(fp, "[%s]\n", audioid->labels[id].labelText);

        fprintf(fp, "stats = \"");
        for (size_t i = 0; i < audioid->countBuckets; i++) {
            running_stats_t *stats = &audioid->labels[id].stats[i];
            fprintf(fp, "%s%u %f %f", i == 0 ? "" : "; ", stats->count, stats->mean, stats->sumVar);
        }
        fprintf(fp, "\"\n");
        fprintf(fp, "scale = %f\n", audioid->labels[id].scale);
        fprintf(fp, "limit = %f\n", audioid->labels[id].limit);

        fprintf(fp, "\n");
    }

    fclose(fp);
    return true;
}

// Shutdown an audioid object (but do not destroy it), the object can be used again
void AudioIdShutdown(audioid_t *audioid) {
    if (audioid->deviceInitialized) {
        if (audioid->verbose) fprintf(stderr, "AUDIOID: Stopping audio capture.\n");
        ma_device_uninit(&audioid->device);
        audioid->deviceInitialized = false;
    }
    if (audioid->decoderInitialized) {
        if (audioid->verbose) fprintf(stderr, "AUDIOID: Closing audio file.\n");
        ma_decoder_uninit(&audioid->decoder);
        audioid->decoderInitialized = false;
    }
    if (audioid->intervals != NULL) {
        free(audioid->intervals);
        audioid->intervals = NULL;
    }
    audioid->countIntervals = 0;
    AudioIdFreeLabels(audioid);
    FingerprintDestroy(&audioid->fingerprint);
}
