// AudioId - Daniel Jackson, 2022.

#ifdef _WIN32
    #include <windows.h>
#endif

#include <stdio.h>
#include <string.h>

#include "audioid.h"

const bool debugFlow = false;

int run(const char *filename, int visualize, bool learn, const char *stateFile, const char *labelFile, const char *outputStateFile) {
if (debugFlow) fprintf(stderr, "Create...\n");
    audioid_t *audioid = AudioIdCreate();

if (debugFlow) fprintf(stderr, "Init...\n");
    AudioIdInit(audioid, visualize);

    // Load state
    if (stateFile != NULL) {
if (debugFlow) fprintf(stderr, "Load...\n");
        AudioIdStateLoad(audioid, stateFile);
    }

    // Configure
    if (learn) {
        // Configure to learn from labelled audio
if (debugFlow) fprintf(stderr, "Learning...\n");
        AudioIdConfigLearn(audioid, filename, labelFile);
    } else {
        // Configure to recognize from audio -- optionally pre-recoded audio from a file, live captured audio otherwise.
if (debugFlow) fprintf(stderr, "Recognizing...\n");
        AudioIdConfigRecognize(audioid, filename, labelFile);
    }

    // Start processing
if (debugFlow) fprintf(stderr, "Start...\n");
    AudioIdStart(audioid);

    // Block until completed
if (debugFlow) fprintf(stderr, "Wait...\n");
    AudioIdWaitUntilDone(audioid);
if (debugFlow) fprintf(stderr, "Done!\n");

    // Save state
    if (outputStateFile != NULL) {
if (debugFlow) fprintf(stderr, "Saving...\n");
        AudioIdStateSave(audioid, outputStateFile);
    }

if (debugFlow) fprintf(stderr, "Shutdown...\n");
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
        else if (allowFlags && strcmp(argv[i], "--state") == 0) {
            if (i + 1 < argc) { stateFile = argv[++i]; }
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
        printf("\n");
        printf("Usage:  audioid [--state state.ini] [--visualize[:reduced]] [sound.wav] [--labels sound.txt [--learn [--write-state state.ini]]]\n");
        printf("\n");
        return 1;
    }

    int returnValue = run(filename, visualize, learn, stateFile, labelFile, outputStateFile);
    return returnValue;
}
