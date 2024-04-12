/*
 * phase4.c
 *
 * This file contains the implementation of Phase 4 of the CS 452 project.
 * It includes the device drivers for the clock, terminal, and disk devices,
 * as well as the necessary system calls for interacting with these devices.
 *
 * The clock device driver implements the Sleep() system call, allowing processes
 * to sleep for a specified number of seconds. The terminal device driver handles
 * the reading and writing of characters to and from the terminal units. The disk
 * device driver provides functionality for reading and writing to the disk, as well
 * as querying the disk size.
 *
 * The file also contains various helper functions and data structures used by the
 * device drivers and system calls. These include functions for locking and unlocking
 * mailboxes, as well as structures for managing sleeping processes.
 *
 * Author: Ishika Patel & Hamad Marhoon
 */

#include <stdio.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct SleepProc
{
    int pid;
    int wakeupTime;
    struct SleepProc *next;
} SleepProc;

int clock_ticks = 0;        // amount of clock ticks that have occurred
int sleep_lock;             // lock for sleep handler
int totalSleepingProcs = 0; // total number of sleeping procs in queue

SleepProc sleepTable[MAXPROC]; // memory for processes created
SleepProc *sleepQueue = NULL;  // queue for waking up sleeping procs

int termWriteLocks[USLOSS_TERM_UNITS];      // write lock for each of 4 terminal devices
int writeRequestMboxIDs[USLOSS_TERM_UNITS]; // holds mailbox ids for terminal write requests

int readBuffersMbox[USLOSS_TERM_UNITS]; // mailbox id for 10 buffers for each unit

int kernSleep(int seconds);
void lock(int lockId);
void unlock(int lockId);
int clockDeviceDriver(char *arg);
int TerminalDeviceDriver(char *arg);
void sleepHandler(USLOSS_Sysargs *sysargs);
void termReadHandler(USLOSS_Sysargs *sysargs);
void termWriteHandler(USLOSS_Sysargs *sysargs);

/*
 * Initializes the phase 4 data structures and sets up the necessary mailboxes and locks
 * It also initializes the system call vectors for sleep, terminal read, and terminal write handlers
 * Additionally, it enables interrupts for the terminal units
 *
 * Returns: void
 */
void phase4_init(void)
{
    memset(sleepTable, 0, sizeof(sleepTable));

    systemCallVec[SYS_SLEEP] = sleepHandler;
    systemCallVec[SYS_TERMREAD] = termReadHandler;
    systemCallVec[SYS_TERMWRITE] = termWriteHandler;

    // for sleep
    sleep_lock = MboxCreate(1, 0);

    // for terminal
    for (int i = 0; i < USLOSS_TERM_UNITS; i++)
    {
        termWriteLocks[i] = MboxCreate(1, 0);
        writeRequestMboxIDs[i] = MboxCreate(0, 0);

        readBuffersMbox[i] = MboxCreate(10, MAXLINE);
    }

    // enabling interrupts for terminal units
    int control = 0;
    control = USLOSS_TERM_CTRL_XMIT_INT(control);
    control = USLOSS_TERM_CTRL_RECV_INT(control);

    USLOSS_DeviceOutput(USLOSS_TERM_DEV, 0, (void *)(long)control);
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, 1, (void *)(long)control);
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, 2, (void *)(long)control);
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, 3, (void *)(long)control);
}

/*
 * Starts the phase 4 service processes, including the clock device driver and
 * the terminal device drivers for each of the four terminal units
 * These processes are created using the spork function
 *
 * Returns: void
 */
void phase4_start_service_processes(void)
{
    spork("ClockDeviceDriver", clockDeviceDriver, NULL, USLOSS_MIN_STACK, 1);
    spork("TerminalDeviceDriver0", TerminalDeviceDriver, "0", USLOSS_MIN_STACK, 1);
    spork("TerminalDeviceDriver1", TerminalDeviceDriver, "1", USLOSS_MIN_STACK, 1);
    spork("TerminalDeviceDriver2", TerminalDeviceDriver, "2", USLOSS_MIN_STACK, 1);
    spork("TerminalDeviceDriver3", TerminalDeviceDriver, "3", USLOSS_MIN_STACK, 1);
}

/*
 * Handles the terminal device driver functionality for a specific terminal unit
 * It continuously waits for interrupts from the terminal and processes them accordingly
 * If a character is received, it is added to the buffer and sent to the read mailbox if a newline is encountered or the buffer is full
 * If the terminal is ready for writing, it sends a message to the write request mailbox
 *
 * Parameters:
 *   arg - a string representing the terminal unit number
 *
 * Returns:
 *   int - always returns 0
 */
int TerminalDeviceDriver(char *arg)
{
    int unitID = atoi(arg);
    int status;
    char buff[MAXLINE] = "";
    int length = 0;

    while (1)
    {
        waitDevice(USLOSS_TERM_DEV, unitID, &status);

        // checks if terminal interrupt needs to read a character
        if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY)
        {

            // get character from status register
            char receivedChar = USLOSS_TERM_STAT_CHAR(status);
            buff[length] = receivedChar;
            length += 1;

            // deliver buffer to kernRead if new line character or buffer size reached limit
            if (receivedChar == '\n' || length == MAXLINE)
            {

                // send buffer to mailbox
                MboxCondSend(readBuffersMbox[unitID], buff, length);
                strcpy(buff, "");
                length = 0;
            }
        }

        // checks if terminal is ready for writing a character
        if (USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY)
        {
            MboxCondSend(writeRequestMboxIDs[unitID], NULL, 0);
        }
    }

    return 0;
}

/*
 * Reads a line of input from the specified terminal unit and stores it in the provided buffer
 * It retrieves the input from the corresponding read buffer mailbox
 * The function copies the characters from the read buffer to the user-provided buffer, up to the specified buffer size
 * It also sets the number of characters read in the numCharsRead pointer
 *
 * Parameters:
 *   buffer - a character array to store the read input
 *   bufferSize - the size of the provided buffer
 *   unitID - the ID of the terminal unit to read from
 *   numCharsRead - a pointer to an integer to store the number of characters read
 *
 * Returns:
 *   int - returns 0 on success, -1 if invalid parameters are provided
 */
int kernTermRead(char *buffer, int bufferSize, int unitID, int *numCharsRead)
{
    if (unitID < 0 || unitID >= USLOSS_TERM_UNITS || buffer == NULL || bufferSize <= 0)
    {
        return -1;
    }

    char readBuff[MAXLINE + 1];
    *numCharsRead = MboxRecv(readBuffersMbox[unitID], readBuff, MAXLINE);

    // copy only characters up to bufferSize given by user
    memcpy(buffer, readBuff, bufferSize);

    // set number of characters read to bufferSize since we only copied a max of bufferSize characters
    if (*numCharsRead > bufferSize)
    {
        *numCharsRead = bufferSize;
    }
    buffer[*numCharsRead] = '\0';
    return 0;
}

/*
 * Writes the contents of the provided buffer to the specified terminal unit
 * It acquires the write lock for the terminal unit before writing
 * The function iterates through the buffer and writes each character to the terminal
 * It waits for the terminal to be ready for writing before sending each character
 * The number of characters written is stored in the numCharsWritten pointer
 *
 * Parameters:
 *   buffer - a character array containing the data to be written
 *   bufferSize - the size of the provided buffer
 *   unitID - the ID of the terminal unit to write to
 *   numCharsWritten - a pointer to an integer to store the number of characters written
 *
 * Returns:
 *   int - returns 0 on success, -1 if invalid parameters are provided
 */
int kernTermWrite(char *buffer, int bufferSize, int unitID, int *numCharsWritten)
{
    // USLOSS_Console("in kernTermWrite function\n");
    //  invalid input
    if (unitID < 0 || unitID >= USLOSS_TERM_UNITS || buffer == NULL || bufferSize <= 0)
    {
        return -1;
    }

    // USLOSS_Console("beginnging to copy characters to term %d\n", unitID);
    for (int i = 0; i < bufferSize; i++)
    {

        // wait to write
        MboxRecv(writeRequestMboxIDs[unitID], NULL, 0);

        int control = 0x1;
        control |= 0x2;
        control |= 0x4;
        control |= (buffer[i] << 8);

        if (USLOSS_DeviceOutput(USLOSS_TERM_DEV, unitID, (void *)(long)control) != USLOSS_DEV_OK)
        {
            return -1;
        }
        *numCharsWritten += 1;
    }
    return 0;
}

/*
 * System call handler for the terminal read operation
 * It extracts the necessary arguments from the USLOSS_Sysargs structure
 * and calls the kernTermRead function with the provided arguments
 * The result of the kernTermRead function is stored back in the USLOSS_Sysargs structure
 *
 * Parameters:
 *   sysargs - pointer to the USLOSS_Sysargs structure containing the system call arguments
 *
 * Returns:
 *   void
 */
void termReadHandler(USLOSS_Sysargs *sysargs)
{
    char *buffer = (char *)sysargs->arg1;
    int bufferSize = (int)(long)sysargs->arg2;
    int unitID = (int)(long)sysargs->arg3;
    int numCharsRead = 0;

    int res = kernTermRead(buffer, bufferSize, unitID, &numCharsRead);

    sysargs->arg2 = (void *)(long)numCharsRead;
    sysargs->arg4 = (void *)(long)res;
}

/*
 * System call handler for the terminal write operation
 * It extracts the necessary arguments from the USLOSS_Sysargs structure
 * and calls the kernTermWrite function with the provided arguments
 * The result of the kernTermWrite function is stored back in the USLOSS_Sysargs structure
 *
 * Parameters:
 *   sysargs - pointer to the USLOSS_Sysargs structure containing the system call arguments
 *
 * Returns:
 *   void
 */
void termWriteHandler(USLOSS_Sysargs *sysargs)
{

    char *buffer = (char *)sysargs->arg1;
    int bufferSize = (int)(long)sysargs->arg2;
    int unitID = (int)(long)sysargs->arg3;
    int numCharsWritten = 0;

    // USLOSS_Console("acquiring lock kernTermWrite for unit %d\n", unitID);
    lock(termWriteLocks[unitID]);
    int res = kernTermWrite(buffer, bufferSize, unitID, &numCharsWritten);
    // USLOSS_Console("releading lock kernTermWritefor unit %d\n", unitID);
    unlock(termWriteLocks[unitID]);

    sysargs->arg2 = (void *)(long)numCharsWritten;
    sysargs->arg4 = (void *)(long)res;
}

/*
 * System call handler for the sleep operation
 * It extracts the necessary arguments from the USLOSS_Sysargs structure
 * and calls the kernSleep function with the provided arguments
 * The result of the kernSleep function is stored back in the USLOSS_Sysargs structure
 *
 * Parameters:
 *   sysargs - pointer to the USLOSS_Sysargs structure containing the system call arguments
 *
 * Returns:
 *   void
 */
void sleepHandler(USLOSS_Sysargs *sysargs)
{
    int seconds = (int)(long)sysargs->arg1;

    int res = kernSleep(seconds);

    sysargs->arg4 = (void *)(long)res;
}

/*
 * Handles the clock device driver functionality
 * It continuously waits for interrupts from the clock device
 * Upon receiving an interrupt, it increments the clock_ticks counter
 * and checks the sleep queue to wake up any processes whose sleep time has expired
 * The function acquires and releases the sleep_lock to ensure thread safety
 *
 * Parameters:
 *   arg - unused parameter, provided for consistency with other device driver functions
 *
 * Returns:
 *   int - always returns 0
 */
int clockDeviceDriver(char *arg)
{
    int status;
    while (1)
    {
        waitDevice(USLOSS_CLOCK_DEV, 0, &status);

        lock(sleep_lock);
        clock_ticks++;

        // iterating through the sleep queue to wake up any processes whose time is up
        while (sleepQueue != NULL && sleepQueue->wakeupTime <= clock_ticks)
        {
            SleepProc *toWake = sleepQueue;
            unblockProc(toWake->pid);
            sleepQueue = sleepQueue->next;
        }

        unlock(sleep_lock);
    }

    return 0;
}

/*
 * Puts the current process to sleep for the specified number of seconds
 * It calculates the wake-up time based on the current clock ticks and the requested sleep duration
 * The function creates a SleepProc structure to store the process information and adds it to the sleep queue
 * It acquires and releases the sleep_lock to ensure thread safety
 * The process is then blocked until it is woken up by the clock device driver
 *
 * Parameters:
 *   seconds - the number of seconds the process should sleep
 *
 * Returns:
 *   int - returns 0 on success, -1 if an invalid sleep duration is provided
 */
int kernSleep(int seconds)
{
    lock(sleep_lock);

    // invalid argument
    if (seconds < 0)
    {
        return -1;
    }

    int wakeup_tick = clock_ticks + (seconds * 10);
    int cur_pid = getpid();

    // save data into struct
    SleepProc *request = &sleepTable[cur_pid % MAXPROC];
    request->pid = cur_pid;
    request->wakeupTime = wakeup_tick;
    request->next = NULL;

    // add struct to sleep queue
    if (sleepQueue == NULL)
    {
        sleepQueue = request;
    }
    else
    {

        // finds the next place to insert process in the sleep queue
        SleepProc *prev = NULL;
        SleepProc *current = sleepQueue;
        while (current != NULL && current->wakeupTime <= request->wakeupTime)
        {
            prev = current;
            current = current->next;
        }
        if (prev == NULL)
        {
            request->next = sleepQueue;
            sleepQueue = request;
        }
        else
        {
            request->next = prev->next;
            prev->next = request;
        }
    }

    totalSleepingProcs++;

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
    MboxSend(lockId, NULL, 0);
}

/**
 * Unlocks a mailbox acting as a mutex.
 *
 * @param lockId The mailbox ID to be unlocked.
 */
void unlock(int lockId)
{
    MboxRecv(lockId, NULL, 0);
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