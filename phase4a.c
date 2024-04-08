
#include <stdio.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct SleepProc{
    int pid;
    int wakeupTime;
    struct SleepProc* next;
} SleepProc;

int clock_ticks = 0; // amount of clock ticks that have occurred
int sleep_mailbox; // mailbox id for processes making sleep requests 
int sleep_lock; // lock for sleep handler

SleepProc sleepTable[MAXPROC]; // memory for processes created
SleepProc *sleepQueue = NULL; // queue for waking up sleeping procs

int kernSleep(int seconds);
void lock(int lockId);
void unlock(int lockId);
int clockDeviceDriver(char *arg);
void addToQueue(SleepProc* request);
void sleepHandler(USLOSS_Sysargs *sysargs);


void phase4_init(void) {
    memset(sleepTable, 0, sizeof(sleepTable));

    systemCallVec[SYS_SLEEP] = sleepHandler;

    sleep_lock = MboxCreate(1, 0);
    sleep_mailbox = MboxCreate(MAXPROC, sizeof(SleepProc));
    
}

void phase4_start_service_processes(void)
{
    spork("ClockDeviceDriver", clockDeviceDriver, NULL, USLOSS_MIN_STACK, 1);
}

int clockDeviceDriver(char *arg) {
    int status;
    while (1) {
        waitDevice(USLOSS_CLOCK_DEV, 0, &status); 
        clock_ticks++; 

        if (sleepQueue != NULL && sleepQueue->wakeupTime <= clock_ticks) {
            
            int cur_pid = sleepQueue->pid;
            unblockProc(cur_pid);

            SleepProc *temp = &sleepTable[cur_pid%MAXPROC];
            temp->next = NULL;
            temp->pid = -1;
            temp->wakeupTime = -1;

            // remove process from queue
            sleepQueue = sleepQueue->next;
        }
    }
    return 0;
}

void addToQueue(SleepProc* request) {
    SleepProc *head = sleepQueue;
    if (head == NULL) {
        head = request;
        return;
    }
    else if (head->wakeupTime > request->wakeupTime) {
        request->next = head;
        head = request;
        return;
    }

    while (head->next != NULL && head->next->wakeupTime < request->wakeupTime) {
        head = head->next;
    }

    if (head->next == NULL) {
        head->next = request;
    }
    else {
        SleepProc *temp = head->next;
        head->next = request;
        request->next = temp;
    }
}

void sleepHandler(USLOSS_Sysargs *sysargs) {
    lock(sleep_lock);
    int seconds = (int)(long) sysargs->arg1;

    int res = kernSleep(seconds);

    sysargs->arg4 = (void *)(long) res;
    unlock(sleep_lock);
}
 
int kernSleep(int seconds)
{
    // invalid argument
    if (seconds < 0) {
        return -1;
    }

    int wakeup_tick = clock_ticks + (seconds * 10);
    int cur_pid = getpid();

    SleepProc *request = &sleepTable[cur_pid % MAXPROC];
    request->pid = cur_pid;
    request->wakeupTime = wakeup_tick;
    request->next = NULL;

    addToQueue(request);
    blockMe(12);

    return 0;
}

/**
 * Locks a mailbox acting as a mutex.
 * 
 * @param lockId The mailbox ID to be locked.
 */
void lock(int lockId)
{
    if (MboxSend(lockId, NULL, 0) != 0)
    {
        USLOSS_Console("ERROR ERROR ERROR\n");
    }
}

/**
 * Unlocks a mailbox acting as a mutex.
 * 
 * @param lockId The mailbox ID to be unlocked.
 */
void unlock(int lockId)
{
    if (MboxRecv(lockId, NULL, 0) != 0)
    {
        USLOSS_Console("ERROR ERROR ERROR\n");
    }
}


int kernTermRead(char *buffer, int bufferSize, int unitID, int *numCharsRead)
{
    return 0;
}

int kernTermWrite(char *buffer, int bufferSize, int unitID, int *numCharsRead)
{
    return 0;
}

// 4b
int kernDiskRead(void *diskBuffer, int unit, int track, int first, int sectors, int *status)
{
    return 0;
}

// 4b
int kernDiskWrite(void *diskBuffer, int unit, int track, int first, int sectors, int *status)
{
    return 0;
}

// 4b
int kernDiskSize(int unit, int *sector, int *track, int *disk)
{
    return 0;
}
