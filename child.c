My Drive
// Author: Christine Mckelvey
// Date: November 04, 2023

#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/msg.h>

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
int checkTerminate = 250000000; // 250ms

// amount of resources the child has of each resource type
int currentResources[10] = {0, 0, 0, 0, 0, 0, 0 ,0, 0 ,0};

// Function prototypes
void childTask();

//
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
    // 
};