#include <stdio.h>
#include "phase4.h"
#include "phase4_usermode.h"
#include <string.h>
#include <stdlib.h>

void phase4_init(void) {
    return;
}

int  kernSleep(int seconds) {
    return 0;
}

int  kernDiskRead (void *diskBuffer, int unit, int track, int first, int sectors, int *status) {
    return 0;
}

int  kernDiskWrite(void *diskBuffer, int unit, int track, int first, int sectors, int *status) {
    return 0;
}

int  kernDiskSize (int unit, int *sector, int *track, int *disk) {
    return 0;
}

int  kernTermRead (char *buffer, int bufferSize, int unitID, int *numCharsRead) {
    return 0;
}

int  kernTermWrite(char *buffer, int bufferSize, int unitID, int *numCharsRead) {
    return 0;
}
