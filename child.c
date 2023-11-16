// Author: Christine Mckelvey
// Date: November 14, 2023

#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <string.h>

// Globals
unsigned int simClock[2];

#define SHM_KEY 205431
#define PERMS 0644

typedef struct messages {
    long mtype; // allows the parent to know its receiving a message
    int requestOrRelease; // 0 means request, 1 means release
    int resourceType; // R0, R1, etc
    pid_t targetChild; // child that wants to release or request resource
} messages;

int queueID;  
messages msgBuffer;   

int lastDecisionCheck = 0;
int lastTerminationCheck = 0;

// amount of resources the child has of each resource type
// R0, R1, R2, R3, R4, R5, R6, R7, R8, R9
int currentResources[10] = {0, 0, 0, 0, 0, 0, 0 ,0, 0 ,0};

// Function prototypes
int timePassed();
void childTask();
void childAction();

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

// Function to update time and check for termination
int timePassed() {
    // check if 250ms have passed
    // check to see if we should potentially terminate
    if (simClock[0] >= 1 && ((simClock[1] >= lastTerminationCheck + 250000000) 
    || (simClock[1] == 0))) 
    {
        // 15 % chance of termination
        int randTerm = rand() % 101;
        if (randTerm <= 15) 
        {
            exit(0);
        }
        lastTerminationCheck = simClock[1];
    }

    // see if 1ms has passed because
    // thats when we can send a release or request to the parent
    if (simClock[1] >= lastDecisionCheck + 1000000
    || (simClock[1] == 0 && simClock[0] > 1)) 
    {
        lastDecisionCheck = simClock[1];
        return 1;
    }

    return 0;
}

// Function to update clock, check timer and get initial parent messages
void childTask() { 
    // receive and send messages
    while (1) {
        // Get message from parent
        if (msgrcv(queueID, &msgBuffer, sizeof(msgBuffer), getpid(), 0) == -1) 
        {
            perror("Failed to receive a message in the child.\n");
            exit(1);
        }

        // check and wait to see if 1 ms has passed
        // afterward we can send a message back to the parent
        while (1) {
            // update the clock
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

            // store the new simulated clock time
            simClock[0] = sharedMemPtr[0]; // seconds
            simClock[1] = sharedMemPtr[1]; // nanoseconds
            shmdt(sharedMemPtr);

            if (timePassed() == 1) 
            {
                // release or request a resource
                int choice = rand() % 101;

                if (choice <= 10) {
                    // Set requestOrRelease parameter to 1 for release
                    childAction(1);
                }
                else {
                    // Set requestOrRelease parameter to 0 for request
                    childAction(0);
                }
                break;
            }
        }
    }
}

// Function to make requst or release and send message to parent
void childAction(int requestOrRelease) {
    // 0 means request, 1 means release
    if (requestOrRelease == 1) 
    {
        // check if there are any resources that we can release
        // because its possible that there isn't any currently
        int canReleaseResource = 0;
        for (int i=0; i<10; i++) {
            if (currentResources[i] != 0) {
                canReleaseResource = 1;
                break;
            }
        }
        if (canReleaseResource == 1) {
            while (1) {
                // find a random resource to release
                int randResource = rand() % 10; // R0, R1, R2, .. R9 
                if (currentResources[randResource] != 0) {
                    msgBuffer.resourceType = randResource;
                    msgBuffer.requestOrRelease = requestOrRelease;
                    break;
                }
            }
        }
        else {
            // request a resource instead;
            // because we dont have any to release.
            msgBuffer.requestOrRelease = 0;
            msgBuffer.resourceType = rand() % 10; // R0, R1, R2, .. R9 
        }
    }
    else 
    {
        // check if there are any resources that we can request
        // because its possible that there isn't any left
        int canRequestResource = 0;
        for (int i=0; i<10; i++) {
            if (currentResources[i] != 20) {
                canRequestResource = 1;
                break;
            }
        }
        if (canRequestResource == 1) {
            while (1) {
                // find a random resource to request
                int randResource = rand() % 10; // R0, R1, R2, .. R9 
                if (currentResources[randResource] != 20) {
                    msgBuffer.resourceType = randResource;
                    msgBuffer.requestOrRelease = 0;
                    break;
                }
            }
        }
        else {
            // release a resource instead
            // because we cant request any
            msgBuffer.requestOrRelease = 1;
            msgBuffer.resourceType = rand() % 10; // R0, R1, R2, .. R9 
        }
    }

    // Send and receive messages as before
    msgBuffer.mtype = getppid();
    msgBuffer.targetChild = getpid();
    if (msgsnd(queueID, &msgBuffer, sizeof(messages) - sizeof(long), 0) == -1) {
        perror("msgsnd to parent failed\n");
        exit(1);
    }

    // Wait for message back from parent.
    messages msgBackFromParent;
    if (msgrcv(queueID, &msgBackFromParent, sizeof(messages), getpid(), 0) == -1) {
        perror("Error couldn't receive message in parent\n");
        exit(1);
    }
    
    // Update resource amount 
    // check decision and update child current resources
    if (msgBuffer.requestOrRelease == 0) {
        currentResources[msgBuffer.resourceType] += 1;
    } 
    else {
        currentResources[msgBuffer.resourceType] -= 1;
    } 
}