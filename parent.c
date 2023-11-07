// Author: Christine Mckelvey
// Date: November 06, 2023

#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <getopt.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>

#define SHM_KEY 205431  
#define PERMS 0644     

unsigned int simClock[2] = {0, 0};

// message queue structure
typedef struct messages {
    long mtype;
    int requestOrRelease; // 0 means request, 1 means release
    char resourceType[3]; 
} messages;

int msgqId; // message queue ID
messages buffer; // message queue Buffer

// Process Control Block structure
typedef struct PCB {
    int occupied; // either true or false
    pid_t pid;   // process id of this child
    int startSeconds; // time when it was created
    int startNano; // time when it was created
} process_PCB;

struct PCB workerTable[18];

unsigned shmID;             
unsigned* shmPtr; 

char* filename = NULL;    
int processCount;      
int simultaneousCount; 
int processSpawnRate;  

int oneSecond = 1000000000; // nano seconds to seconds
int scheduleTime = 50000000; // process schedule time
int tableLogInterval = 500000000;

unsigned long long launchTimeElapsed = 0;
unsigned long long totalElapsed = 0;  // current elapsed time
unsigned long long totalPrevious = 0; // previous elapsed tiem

int totalLaunched = 0;
int totalTerminated = 0;

// resource and allocated tables
int resourcesAllocated[10][18];
int resourcesRequested[10][18];

// Function prototypes
void launchChildren();
void handleTermination();
void incrementSimulatedClock(unsigned int n);
void showProcessTable(int i);
void sendMessage(process_PCB* targetWorker);


int main(int argc, char** argv) {
    // register signal handlers for interruption and timeout
    srand(time(NULL) + getpid());
    signal(SIGINT, handleTermination);
    signal(SIGALRM, handleTermination);
    alarm(5); 

    // check arguments
    char argument;
    while ((argument = getopt(argc, argv, "f:hn:s:t:")) != -1) {
        switch (argument) {
            case 'f': {
                char* opened_file = optarg;
                FILE* file = fopen(opened_file, "r");
                if (file) 
                {
                    filename = opened_file;
                    fclose(file);
                } 
                else {
                    printf("file doesn't exist.\n");
                    exit(1);
                }
                break;
            }           
            case 'h':
                printf("oss [-h] [-n proc] [-s simul] [-t timeToLaunchNewChild] [-f logfile]\n");
                printf("h is the help screen\n"
                    "n is the total number of child processes oss will ever launch\n"
                    "s specifies the maximum number of concurrent running processes\n"
                    "t is for processes speed as they will trickle into the system at a speed dependent on parameter\n"
                    "f is for a logfile as previously\n\n");
                exit(0);
            case 'n':
                processCount = atoi(optarg);
                if (processCount > 18) {
                    printf("invalid processCount\n");
                    exit(1);
                }                
                break;
            case 's':
                simultaneousCount = atoi(optarg);
                if (simultaneousCount > 18) {
                    printf("invalid simultaneousCount\n");
                    exit(1);
                }
                break;
            case 't':
                processSpawnRate = atoi(optarg);
                break;
            default:
                printf("invalid commands\n");
                exit(1);
        }
    }
    if (processCount == 0 || processSpawnRate == 0 || simultaneousCount == 0 || filename == NULL) {
        printf("invalid commands\n");
        exit(1);
    }    

    // initialize process table
    for (int i = 0; i < 18; i++)  
    {
        workerTable[i].occupied = 0; // either true or false
        workerTable[i].pid = 0;   // process id of this child
        workerTable[i].startSeconds = 0; // time when it was created
        workerTable[i].startNano = 0;    // time when it was created
    }

    // setup other matrices/arrays
    // 

    // make shared memory
    shmID = shmget(SHM_KEY, sizeof(unsigned) * 2, 0777 | IPC_CREAT);
    if (shmID == -1) 
    {
        perror("Unable to acquire the shared memory segment.\n");
        exit(1);
    }
    shmPtr = (unsigned*)shmat(shmID, NULL, 0);
    if (shmPtr == NULL) 
    {
        perror("Unable to connect to the shared memory segment.\n");
        exit(1);
    }
    memcpy(shmPtr, simClock, sizeof(unsigned) * 2);

    // make message queue
    key_t messageQueueKey = ftok("msgq.txt", 1);
    if (messageQueueKey == -1) 
    {
        perror("Unable to generate a key for the message queue.\n");
        exit(1);
    }

    msgqId = msgget(messageQueueKey, PERMS | IPC_CREAT);
    if (msgqId == -1) 
    {
        perror("Unable to create or access the message queue.\n");
        exit(1);
    }

    launchChildren();
    return 0;
}

void handleTermination() {
    // kill all child processes
    kill(0, SIGTERM);
    msgctl(msgqId, IPC_RMID, NULL);
    shmdt(shmPtr);
    shmctl(shmID, IPC_RMID, NULL);
    exit(0);
}

void showProcessTable(int index) {
}

void incrementSimulatedClock(unsigned nanoseconds) {

    simClock[1] += nanoseconds;
    if (simClock[1] >= 1000000000) 
    {
        // Calculate the number of seconds to add
        // Update seconds and adjust nanoseconds
        unsigned secondsToAdd = simClock[1] / 1000000000;
        simClock[0] += secondsToAdd;
        simClock[1] %= 1000000000;
    }

    totalElapsed += nanoseconds;
    memcpy(shmPtr, simClock, sizeof(unsigned int) * 2);
}

void launchChildren() {
    while (totalTerminated != processCount)  {
        // update clock
        launchTimeElapsed += 100000;
        incrementSimulatedClock(100000);
        
        // determine if we should launch a child
        if (launchTimeElapsed >= processSpawnRate || totalLaunched == 0) 
        {
            if (totalLaunched < processCount && totalLaunched < simultaneousCount + totalTerminated) {
                // launch new child
                pid_t pid = fork();
                if (pid == 0) 
                {
                    char* args[] = {"./worker", NULL};
                    execvp(args[0], args);
                }
                else 
                {
                    workerTable[totalLaunched].pid = pid;
                    workerTable[totalLaunched].occupied = 1;
                    workerTable[totalLaunched].startSeconds = simClock[0];
                    workerTable[totalLaunched].startNano = simClock[1];
                } 

                totalLaunched += 1;
            }
            launchTimeElapsed = 0;
        }

        // ... 

        // break out of loop
        if (simClock[0] >= 10) {
            break;
        }
    }
}



void sendMessage(process_PCB* targetWorker) {
}   