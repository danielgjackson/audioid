// AudioId - Daniel Jackson, 2022.

#ifndef AUDIOID_H
#define AUDIOID_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audioid_tag audioid_t;

// Allocate an audioid object, this will also initialize the object.
audioid_t *AudioIdCreate();

// Destroy the audioid object, this will also shutdown the object.
void AudioIdDestroy();

// Initialize an audioid object with a new configuration
void AudioIdInit(audioid_t *audioid);

// Start audio processing on an audioid object
void AudioIdStart(audioid_t *audioid);

// Shutdown an audioid object (but do not destroy it), this will initialize the object and it can be used again
void AudioIdShutdown(audioid_t *audioid);

#ifdef __cplusplus
}
#endif

#endif
