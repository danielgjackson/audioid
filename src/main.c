// AudioId - Daniel Jackson, 2022.

#ifdef _WIN32
    #include <windows.h>
#endif

#include <stdio.h>
#include <string.h>

#include "audioid.h"

int run(const char *filename, int visualize, bool learn, const char *eventsFile, const char *stateFile, const char *labelFile, const char *outputStateFile) {
    audioid_t *audioid = AudioIdCreate();

    AudioIdInit(audioid, visualize);

    // Load events file (before state, so label groups parent correctly)
    if (eventsFile != NULL) {
        if (!AudioIdStateLoad(audioid, eventsFile)) {
            fprintf(stderr, "ERROR: Problem loading events: %s\n", eventsFile);
            return -1;
        }
    }

    // Load state
    if (stateFile != NULL) {
        if (!AudioIdStateLoad(audioid, stateFile)) {
            fprintf(stderr, "ERROR: Problem loading state: %s\n", stateFile);
            return -1;
        }
    }

    // Configure
    if (learn) {
        // Configure to learn from labelled audio
        AudioIdConfigLearn(audioid, filename, labelFile);
    } else {
        // Configure to recognize from audio -- optionally pre-recoded audio from a file, live captured audio otherwise.
        AudioIdConfigRecognize(audioid, filename, labelFile);
    }

    // Start processing
    if (!AudioIdStart(audioid)) {
        fprintf(stderr, "ERROR: Problem starting.\n");
        return -1;
    }

    // Block until completed
    AudioIdWaitUntilDone(audioid);

    // Save state
    if (outputStateFile != NULL) {
        AudioIdStateSave(audioid, outputStateFile);
    }

    AudioIdShutdown(audioid);

    AudioIdDestroy(audioid);

    return 0;
}

int main(int argc, char *argv[]) {
    bool help = false;
    bool allowFlags = true;
    int positional = 0;
    const char *filename = NULL;
    const char *labelFile = NULL;
    const char *eventsFile = NULL;
    const char *stateFile = NULL;
    const char *outputStateFile = NULL;
    int visualize = 0;
    bool learn = false;

    #ifdef _WIN32
        SetConsoleOutputCP(65001);    // CP_UTF8 65001
        setvbuf(stdout, NULL, _IOFBF, 1024);
    #endif

    for (int i = 1; i < argc; i++) {
        if (allowFlags && strcmp(argv[i], "--") == 0) { allowFlags = false; }
        else if (allowFlags && strcmp(argv[i], "--help") == 0) { help = true; }
        else if (allowFlags && strcmp(argv[i], "--visualize") == 0) { visualize = 1; }
        else if (allowFlags && strcmp(argv[i], "--visualize:reduced") == 0) { visualize = 2; }
        else if (allowFlags && strcmp(argv[i], "--learn") == 0) { learn = true; }
        else if (allowFlags && strcmp(argv[i], "--events") == 0) {
            if (i + 1 < argc) eventsFile = argv[++i]; 
            else { printf("ERROR: Missing parameter value for: --events\n"); help = true; }
        }
        else if (allowFlags && strcmp(argv[i], "--state") == 0) {
            if (i + 1 < argc) stateFile = argv[++i]; 
            else { printf("ERROR: Missing parameter value for: --state\n"); help = true; }
        }
        else if (allowFlags && strcmp(argv[i], "--labels") == 0) {
            if (i + 1 < argc) { labelFile = argv[++i]; }
            else { printf("ERROR: Missing parameter value for: --labels\n"); help = true; }
        }
        else if (allowFlags && strcmp(argv[i], "--write-state") == 0) {
            if (i + 1 < argc) { outputStateFile = argv[++i]; }
            else { printf("ERROR: Missing parameter value for: --write-state\n"); help = true; }
        }
        else if (allowFlags && argv[i][0] == '-') {
            printf("ERROR: Unknown flag: %s\n", argv[i]);
            return 1;
        }
        else {
            if (positional == 0) { filename = argv[i]; }
            else {
                printf("ERROR: Unexpected positional argument: %s\n", argv[i]);
                return 1;
            }
            positional++;
        }
    }

    if (help) {
        printf("AudioID - Daniel Jackson, 2022.\n");
        printf("https://github.com/danielgjackson/audioid\n");
        printf("\n");
        printf("Usage:  audioid [--events events.ini] [--state state.ini] [--visualize[:reduced]] [sound.wav] [--labels sound.txt [--learn [--write-state state.ini]]]\n");
        printf("\n");
        printf("This program is available under the MIT license, and makes use of:\n");
        printf("\n");
        printf("  * miniaudio.h - Copyright David Reid, Public Domain/MIT-0, https://miniaud.io\n");
        printf("  * minfft.{c,h} dr_wav.h - Copyright (c) 2016-2022 Alexander Mukhin, MIT License, https://github.com/aimukhin/minfft\n");
        printf("\n");
        return 1;
    }

    int returnValue = run(filename, visualize, learn, eventsFile, stateFile, labelFile, outputStateFile);
    return returnValue;
}
