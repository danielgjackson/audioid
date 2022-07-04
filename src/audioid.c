// AudioId - Daniel Jackson, 2022.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "miniaudio.h"
#include "minfft.h"

#include "audioid.h"

// State
typedef struct audioid_tag {
    int dummy;
} audioid_t;

// Allocate an audioid object, this will also initialize the object.
audioid_t *AudioIdCreate() {
    audioid_t *audioid = (audioid_t *)malloc(sizeof(audioid_t));
    memset(audioid, 0, sizeof(*audioid));
    AudioIdInit(audioid);
    return audioid;
}

// Destroy the audioid object, this will also shutdown the object.
void AudioIdDestroy(audioid_t *audioid) {
    AudioIdShutdown(audioid);
    free(audioid);
}

// Initialize an audioid object with a new configuration
void AudioIdInit(audioid_t *audioid) {
    memset(audioid, 0, sizeof(*audioid));
}

// Start audio processing on an audioid object
void AudioIdStart(audioid_t *audioid) {
    // TODO: Start
}

// Shutdown an audioid object (but do not destroy it), this will initialize the object and it can be used again
void AudioIdShutdown(audioid_t *audioid) {
    // TODO: Shutdown 
    AudioIdInit(audioid);
}

