// AudioId - Daniel Jackson, 2022.

#ifdef _WIN32
    #include <windows.h>
#endif

#include <stdio.h>
#include <string.h>

#include "audioid.h"

int run(const char *filename, const char *stateFile, const char *labelFile, const char *outputStateFile) {
    audioid_t *audioid = AudioIdCreate();

    AudioIdInit(audioid, filename, labelFile);

// Learn
if (labelFile != NULL) {
//    if (stateFile != NULL) AudioIdStateLoad(stateFile);
}

    AudioIdStart(audioid);

    AudioIdWaitUntilDone(audioid);

// if (outputStateFile != NULL) AudioIdStateSave(outputStateFile);

// Recognize
if (labelFile == NULL) {
    
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
    const char *stateFile = NULL;
    const char *outputStateFile = NULL;

    #ifdef _WIN32
        SetConsoleOutputCP(65001);    // CP_UTF8 65001
        setvbuf(stdout, NULL, _IOFBF, 1024);
    #endif

    for (int i = 1; i < argc; i++) {
        if (allowFlags && strcmp(argv[i], "--") == 0) { allowFlags = false; }
        else if (allowFlags && strcmp(argv[i], "--help") == 0) { help = true; }
        else if (allowFlags && strcmp(argv[i], "--state") == 0) {
            if (i + 1 < argc) { stateFile = argv[++i]; }
            else { printf("ERROR: State file not specified\n"); help = true; }
        }
        else if (allowFlags && strcmp(argv[i], "--learn-labels") == 0) {
            if (i + 1 < argc) { labelFile = argv[++i]; }
            else { printf("ERROR: Label file not specified\n"); help = true; }
        }
        else if (allowFlags && strcmp(argv[i], "--write-state") == 0) {
            if (i + 1 < argc) { outputStateFile = argv[++i]; }
            else { printf("ERROR: Write state file not specified\n"); help = true; }
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
        printf("Usage:  audioid [--state state.txt] [sound.wav] [--learn-labels sound.txt] [--write-state state.txt]\n");
        printf("\n");
        return 1;
    }

    int returnValue = run(filename, stateFile, labelFile, outputStateFile);
    return returnValue;
}
