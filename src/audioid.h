// AudioId - Daniel Jackson, 2022.

#ifndef AUDIOID_H
#define AUDIOID_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef struct audioid_tag audioid_t;

// Allocate an audioid object, this will also initialize the object.
audioid_t *AudioIdCreate();

// Destroy the audioid object, this will also shutdown the object.
void AudioIdDestroy();

// Initialize an audioid object with an empty configuration
void AudioIdInit(audioid_t *audioid, bool visualize);

// Configure to learn from labelled audio
void AudioIdConfigLearn(audioid_t *audioid, const char *filename, const char *labelFile);

// Configure to recognize from audio -- optionally pre-recoded audio from a file, live captured audio otherwise.
void AudioIdConfigRecognize(audioid_t *audioid, const char *filename, const char *labelFile);

// Start audio processing on an audioid object
bool AudioIdStart(audioid_t *audioid);

// Wait until audio processing has completed
void AudioIdWaitUntilDone(audioid_t *audioid);

// Load state
bool AudioIdStateLoad(audioid_t *audioid, const char *filename);

// Save state
bool AudioIdStateSave(audioid_t *audioid, const char *filename);

// Shutdown an audioid object (but do not destroy it), the object can be used again
void AudioIdShutdown(audioid_t *audioid);


// ---

typedef struct fingerprint_tag fingerprint_t;

fingerprint_t *FingerprintCreate(size_t numSamples);
void FingerprintDestroy(fingerprint_t *fingerprint);


#ifdef __cplusplus
}
#endif

#endif
