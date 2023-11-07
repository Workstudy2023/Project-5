My Drive
// Author: Christine Mckelvey
// Date: November 06, 2023

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
unsigned int simClock[2] = {0, 0};

#define SHM_KEY 205431
#define PERMS 0644

typedef struct messages {
    long mtype;
    int requestOrRelease; // 0 means request, 1 means release
    int resourceType; // R0, R1, etc
} messages;

int queueID;  
messages msgBuffer;   

// amount of time until we check to terminate child
int lastTerminationCheck = 0;
int checkTerminate = 250000000; // 250ms

int performDecision = 1000000;  // 1ms
int lastDecision = 0;

// amount of resources the child has of each resource type
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
    while (1) 
    {
        // get message from parent
        // if (msgrcv(queueID, &msgBuffer, sizeof(msgBuffer), getpid(), 0) == -1) 
        // {
        //     perror("Failed to receive a message in the child.\n");
        //     exit(1);
        // }
    
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

        // release or request resource after 1ms
        if (simClock[1] >= lastDecision + performDecision) {
            lastDecision = simClock[1];

            // check if 250ms have passed
            if (simClock[0] >= 1 && (simClock[1] >= lastTerminationCheck + checkTerminate)) {
                // check to see if we should potentially terminate
                int randTerm = rand() % 101;
                if (randTerm <= 10) {
                    // terminate child
                    printf("Child terminating at time: [%u: %u]\n", simClock[0], simClock[1]);
                    exit(0);
                }
                lastTerminationCheck = simClock[1];
            }

            // release or request a resource
            int randTerm = rand() % 101;
            if (randTerm <= 10) 
            {
                // release resource
                while(1) {
                    // check if all resources are zero
                    int resouceCount = 0;
                    for(int i=0; i<10; i++) {
                        if (currentResources[i] != 0) {
                            resouceCount += 1;
                        }
                    }

                    if (resouceCount == 0) 
                    {
                        int randResource = rand() % 10; // R0, R1, R2, .. R9
                        msgBuffer.resourceType = randResource;
                        msgBuffer.requestOrRelease = 0;
                        printf("Child %d, requesting resource: R%d\n", getpid(), randResource);   
                        break;
                    }
                     
                    int randResource = rand() % 10; // R0, R1, R2, .. R9
                    if (currentResources[randResource] != 0) 
                    {
                        msgBuffer.resourceType = randResource;
                        msgBuffer.requestOrRelease = 1;
                        printf("Child %d, releasing resource: R%d\n", getpid(), randResource); 
                        break;
                    }
                }
            }
            else 
            {
                // request resource
                while(1) 
                {
                    // determine if we have all resources
                    int resouceCount = 0;
                    for(int i=0; i<10; i++) {
                        if (currentResources[i] == 20) {
                            resouceCount += 1;
                        }
                    }

                    if (resouceCount != 10)
                    {
                        // decide if we can request the resource
                        int randResource = rand() % 10; // R0, R1, R2, .. R9
                        if (currentResources[randResource] != 20) 
                        {
                            msgBuffer.resourceType = randResource;
                            msgBuffer.requestOrRelease = 0;
                            printf("Child %d, requesting resource: R%d\n", getpid(), randResource);   
                            break;
                        }
                    }
                    else 
                    {
                        // release a resource
                        int randResource = rand() % 10; // R0, R1, R2, .. R9
                        msgBuffer.resourceType = randResource;
                        msgBuffer.requestOrRelease = 1;
                        printf("Child %d, releasing resource: R%d\n", getpid(), randResource); 
                        break;
                    }
                }
            }

            // 0 means request, 1 means release
            if (msgBuffer.requestOrRelease == 0)
            {
                // give resource
                currentResources[msgBuffer.resourceType] += 1;
            }
            else
            {
               // release request
                currentResources[msgBuffer.resourceType] -= 1;
            }

            // // send message back to parent
            // msgBuffer.mtype = getppid();
            // if (msgsnd(queueID, &msgBuffer, sizeof(messages)-sizeof(long), 0) == -1) {
            //     perror("msgsnd to parent failed\n");
            //     exit(1);
            // }

            // // wait for message back from parent
            // messages msgBack;
            // if (msgrcv(queueID, &msgBack, sizeof(messages), getpid(), 0) == -1) {
            //     perror("Error couldn't receive message in parent\n");
            //     exit(1);
            // }

            // // check message from parent
            // if (msgBack.requestOrRelease == 0) 
            // {
            //     // give resource
            //     currentResources[msgBuffer.resourceType] += 1;
            // }
            // else 
            // {
            //     // release request
            //     currentResources[msgBuffer.resourceType] -= 1;
            // }

            // display updated resources
            printf("\n");
            for(int i=0; i<10; i++) {
                printf("R%d Amount: %d\n", i, currentResources[i]);
            }
            printf("\n");
        }
    }
};

