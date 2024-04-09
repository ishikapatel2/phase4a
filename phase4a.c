
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

typedef struct WriteRequest {
    char buffer[MAXLINE + 1]; // data to be written.
    int length;                // length of the data.
    int mboxID;
} WriteRequest;

int clock_ticks = 0; // amount of clock ticks that have occurred
int sleep_lock; // lock for sleep handler
int totalSleepingProcs = 0; // total number of sleeping procs in queue

SleepProc sleepTable[MAXPROC]; // memory for processes created
SleepProc *sleepQueue = NULL; // queue for waking up sleeping procs

int termLocks[USLOSS_TERM_UNITS]; // lock for each of 4 terminal devices
int writeRequestMboxIDs[USLOSS_TERM_UNITS]; // holds mailbox ids for terminal write requests.

int kernSleep(int seconds);
void lock(int lockId);
void unlock(int lockId);
int clockDeviceDriver(char *arg);
int TerminalDeviceDriver(char *arg);
void addToQueue(SleepProc* request);
void sleepHandler(USLOSS_Sysargs *sysargs);
void termReadHandler(USLOSS_Sysargs *sysargs);
void termWriteHandler(USLOSS_Sysargs *sysargs);


void phase4_init(void) {
    memset(sleepTable, 0, sizeof(sleepTable));

    systemCallVec[SYS_SLEEP] = sleepHandler;
    systemCallVec[SYS_TERMREAD] = termReadHandler;
    systemCallVec[SYS_TERMWRITE] = termWriteHandler;

    // for sleep
    sleep_lock = MboxCreate(1, 0);
    // sleep_mailbox = MboxCreate(MAXPROC, sizeof(SleepProc));

    // for terminal
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        termLocks[i] = MboxCreate(1, 0); 
        writeRequestMboxIDs[i] = MboxCreate(10, sizeof(WriteRequest));
    }

}

void phase4_start_service_processes(void)
{
    spork("ClockDeviceDriver", clockDeviceDriver, NULL, USLOSS_MIN_STACK, 1);
    spork("TerminalDeviceDriver0", TerminalDeviceDriver, "0", USLOSS_MIN_STACK, 1);
    spork("TerminalDeviceDriver1", TerminalDeviceDriver, "1", USLOSS_MIN_STACK, 1);
    spork("TerminalDeviceDriver2", TerminalDeviceDriver, "2", USLOSS_MIN_STACK, 1);
    spork("TerminalDeviceDriver3", TerminalDeviceDriver, "3", USLOSS_MIN_STACK, 1);
}


int kernTermRead(char *buffer, int bufferSize, int unitID, int *numCharsRead)
{
    return 0;
}

int kernTermWrite(char *buffer, int bufferSize, int unitID, int *numCharsWritten)
{
    // invalid input
    if (unitID < 0 || unitID >= USLOSS_TERM_UNITS || buffer == NULL || bufferSize <= 0 || bufferSize > MAXLINE) {
        return -1; 
    }

    // initialize the write request
    WriteRequest request;
    strncpy(request.buffer, buffer, bufferSize); // Copy the buffer content to the request
    request.buffer[bufferSize] = '\0'; // Ensure null-termination
    request.length = bufferSize;
    request.mboxID = MboxCreate(0, 0); // Create a zero-slot mailbox for signaling completion

    if (request.mboxID < 0) {
        return -1;
    }

    // send write request to the corresponding terminal's mailbox
    int sendStatus = MboxSend(writeRequestMboxIDs[unitID], &request, sizeof(request));
    if (sendStatus < 0) {
        MboxRelease(request.mboxID);
        return -1;
    }

    // wait for the write operation to complete
    MboxRecv(request.mboxID, NULL, 0);

    // release mailbox
    MboxRelease(request.mboxID); 

    // update the number of characters written
    if (numCharsWritten != NULL) {
        *numCharsWritten = bufferSize;
    }

    return 0;
}

// system call which calls the kernTermRead when triggered by the user
void termReadHandler(USLOSS_Sysargs *sysargs) { 
    char *buffer = (char *)sysargs->arg1;
    int bufferSize = (int)(long)sysargs->arg2;
    int unitID = (int)(long)sysargs->arg3;
    int numCharsRead = 0;

    int res = kernTermRead(buffer, bufferSize, unitID, &numCharsRead);

    sysargs->arg2 = (void *)(long) numCharsRead;
    sysargs->arg4 = (void *)(long) res;
}

// system call which calls the kernTermWrite when triggered by the user
void termWriteHandler(USLOSS_Sysargs *sysargs) {
    char *buffer = (char *)sysargs->arg1;
    int bufferSize = (int)(long)sysargs->arg2;
    int unitID = (int)(long)sysargs->arg3;
    int numCharsWritten = 0;

    int res = kernTermWrite(buffer, bufferSize, unitID, &numCharsWritten);

    sysargs->arg2 = (void *)(long) numCharsWritten;
    sysargs->arg4 = (void *)(long) res;
}

// system call which calls the kernalSleep when triggered by the user
void sleepHandler(USLOSS_Sysargs *sysargs) {   
    int seconds = (int)(long) sysargs->arg1;

    int res = kernSleep(seconds);

    sysargs->arg4 = (void *)(long) res;
}

int TerminalDeviceDriver(char *arg) {
    int unitID = atoi(arg);
    int status;
    while (1) {
        waitDevice(USLOSS_TERM_DEV, unitID, &status); 

        // checks if terminal interrupt needs to read a character
        // if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY) {
        //     //char receivedChar = USLOSS_TERM_STAT_CHAR(status);
        //     continue;
        // }

        // checks if terminal is ready for writing a character
        if (USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY) {
            WriteRequest request;

            // checks if there are pending write requests for this terminal
            if (MboxCondRecv(writeRequestMboxIDs[unitID], &request, sizeof(request)) > 0) {
                
                // If the current request is not fully written, write the next character
                for (int i = 0; i < request.length; i++) {
                    // int control = (request.buffer[i] & 0xFF) << 8; // Character to transmit.
                    // control |= USLOSS_TERM_CTRL_XMIT_CHAR; // Bit to start transmission.
                    // control |= USLOSS_TERM_CTRL_RECV_INT;  // Enable receive interrupts as an example.
                    int control = 0x1;
                    control |= 0x2;
                    control |= 0x4;
                    control |= (request.buffer[i] << 8);
                    USLOSS_DeviceOutput(USLOSS_TERM_DEV, unitID, (void *)(long)control);
                
                }   
            
            }
             MboxSend(request.mboxID, NULL, 0);
        }

       
    }

    return 0;
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
