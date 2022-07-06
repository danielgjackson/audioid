// AudioId - Daniel Jackson, 2022.

#include <stdio.h>
#include <string.h>

#include "audioid.h"

int run(const char *filename) {
    audioid_t *audioid = AudioIdCreate();

    AudioIdInit(audioid, filename);

    AudioIdStart(audioid);

    AudioIdWaitUntilDone(audioid);
    
    AudioIdShutdown(audioid);

    AudioIdDestroy(audioid);

    return 0;
}

int main(int argc, char *argv[]) {
    bool help = false;
    bool allowFlags = true;
    int positional = 0;
    const char *filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (allowFlags && strcmp(argv[i], "--") == 0) { allowFlags = false; }
        else if (allowFlags && strcmp(argv[i], "--help") == 0) { help = true; }
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
        printf("Usage:  audioid [filename]\n");
        printf("\n");
        return 1;
    }

    int returnValue = run(filename);
    return returnValue;
}
