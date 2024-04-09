
#include <stdio.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct SleepProc {
    int pid;
    int wakeupTime;
    struct SleepProc* next;
} SleepProc;

int clock_ticks = 0; // amount of clock ticks that have occurred
int sleep_mailbox; // mailbox id for processes making sleep requests 
int sleep_lock; // lock for sleep handler
int totalSleepingProcs = 0; // total number of sleeping procs in queue

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
        
        lock(sleep_lock);
        clock_ticks++; 
        
        // iterating through the sleep queue to wake up any processes whose time is up
        while (sleepQueue != NULL && sleepQueue->wakeupTime <= clock_ticks) {
            SleepProc *toWake = sleepQueue;        
            unblockProc(toWake->pid); 
            sleepQueue = sleepQueue->next;
        }

        unlock(sleep_lock);
    }

    return 0;
}

void addToQueue(SleepProc* request) {
    if (sleepQueue == NULL) {
        sleepQueue = request;
    } 
    else {

        // finds the next place to insert process in the sleep queue 
        SleepProc *prev = NULL;
        SleepProc *current = sleepQueue;
        while (current != NULL && current->wakeupTime <= request->wakeupTime) {
            prev = current;
            current = current->next;
        }
        if (prev == NULL) { 
            request->next = sleepQueue;
            sleepQueue = request;
        } 
        else { 
            request->next = prev->next;
            prev->next = request;
        }
    }

    totalSleepingProcs++;
}

// system call which calls the kernalSleep when triggered by the user
void sleepHandler(USLOSS_Sysargs *sysargs) {   
    int seconds = (int)(long) sysargs->arg1;

    int res = kernSleep(seconds);

    sysargs->arg4 = (void *)(long) res;
}
 
int kernSleep(int seconds)
{
    lock(sleep_lock);

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

    unlock(sleep_lock);
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
