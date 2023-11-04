// Author: Christine Mckelvey
// Date: November 04, 2023

#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <string.h>

// Globals
int simClock[2] = {0, 0};

#define SHM_KEY 205431
#define PERMS 0644

typedef struct messages 
{
    // allows the parent to know it got a message from the child
    long mtype;

    // represents what the childs decides to do: request or release
    int requestOrRelease; // 0 means request, 1 means release

    // represent the resource we want to release or request:
    // Example: R0, R1, R2, R3, ... R9
    char resourceType[2]; 
} messages;

int queueID;  
messages msgBuffer;   

// amount of time until we check to terminate child
int lastTerminationCheck = 0;
int checkTerminate = 250000000; // 250ms

// amount of resources the child has of each resource type
// {R0, R1, R2, R3, R4, R5, R6 ,R7, R8 ,R9};
int currentResources[10] = {0, 0, 0, 0, 0, 0, 0 ,0, 0 ,0};

// Function prototypes
void childTask();

int main(int argc, char const *argv[]) {
    // generate randomness
    srand(time(NULL) + getpid());

    // set up the message queue
    key_t msgQueueKey = ftok("msgq.txt", 1);
    if (msgQueueKey == -1) {
        perror("Child failed to generate a in key using ftok.\n");
        exit(1);
    }
    
    queueID = msgget(msgQueueKey, PERMS);
    if (queueID == -1) {
        perror("Child failed to access the message queue.\n");
        exit(1);
    }

    childTask();
    return 0;
}

void childTask() {

    


    while (1) {
        // Access and attach to shared memory
        int sharedMemID = shmget(SHM_KEY, sizeof(int) * 2, 0777);
            if (sharedMemID == -1) {
            perror("Error: Failed to access shared memory using shmget.\n");
            exit(EXIT_FAILURE);
        }

        int* sharedMemPtr = (int*)shmat(sharedMemID, NULL, SHM_RDONLY);
        if (sharedMemPtr == NULL) {
            perror("Error: Failed to attach to shared memory using shmat.\n");
            exit(EXIT_FAILURE);
        }

        simClock[0] = sharedMemPtr[0]; // seconds
        simClock[1] = sharedMemPtr[1]; // nanoseconds
        shmdt(sharedMemPtr);

        // check if 250ms have passed
        if (simClock[0] >= 1 && (simClock[1] - lastTerminationCheck >= checkTerminate)) {
            // check to see if we should potentially terminate
            int randTerm = rand() % 101;
            if (randTerm <= 10) {
                // terminate child
                exit(0);
            }
            lastTerminationCheck = simClock[1];
        }

        // release or request a resource
        int randTerm = rand() % 101;
        if (randTerm <= 10) 
        {
            // release a resource
            char* resourceName = "";   


            msgBuffer.requestOrRelease = 1;
            strcpy(msgBuffer.resourceType, resourceName);
        }
        else 
        {
            // request a resource
            char* resourceName = "";   



            msgBuffer.requestOrRelease = 0;
            strcpy(msgBuffer.resourceType, resourceName);
        }

        // send message back to parent
        msgBuffer.mtype = getppid();
        if (msgsnd(queueID, &msgBuffer, sizeof(messages)-sizeof(long), 0) == -1) {
            perror("msgsnd to parent failed\n");
            exit(1);
        }

        // wait for message back from parent
        messages msgBack;
        if (msgrcv(queueID, &msgBack, sizeof(messages), getpid(), 0) == -1) {
            perror("Error couldn't receive message in parent\n");
            exit(1);
        }

        // check message from parent
        if (msgBuffer.requestOrRelease == 0) {
            // we were granted the resource
            // currentResources[] += 1;
        }
        else {
            // we released the resource
            // currentResources[] -= 1;
        }
    }
};