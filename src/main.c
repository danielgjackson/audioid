// AudioId - Daniel Jackson, 2022.

#include <stdio.h>

#include "audioid.h"

int run() {
    audioid_t *audioid = AudioIdCreate();

    AudioIdInit(audioid);

    AudioIdStart(audioid);

    AudioIdShutdown(audioid);

    AudioIdDestroy(audioid);

    return 0;
}

int main(int argc, char *argv[]) {
    printf("AudioID - Daniel Jackson, 2022.\n");
    printf("\n");
    int returnValue = run();
    return returnValue;
}
