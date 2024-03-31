#include <stdio.h>
#include "phase4.h"
#include "phase4_usermode.h"
#include <string.h>
#include <stdlib.h>

int  Sleep(int seconds){
    return 0;
}

int TermRead(char *buffer, int bufSize, int unit, int *lenOut) {
    return 0;
}

int TermWrite(char *buffer, int bufSize, int unit, int *lenOut) {
    return 0;
}

int DiskSize(int unit, int *sector, int *track, int *disk) {
    return 0;
}

int DiskRead(void *buffer, int unit, int track, int firstBlock, int blocks, int *statusOut) {
    return 0;
}

int DiskWrite(void *diskBuffer, int unit, int track, int first, int sectors, int *status) {
    return 0;
}