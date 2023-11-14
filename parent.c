// Author: Christine Mckelvey
// Date: November 13, 2023

#include <time.h>
#include <stdio.h>
#include <errno.h>
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
    int resourceType; // R0, R1, etc
    pid_t targetChild; // child that wants to release or request resource
} messages;

int msgqId; // message queue ID
messages buffer; // message queue Buffer

// Process Control Block structure
typedef struct PCB {
    int occupied; // either true or false
    pid_t pid;   // process id of this child
    int expectingResponse;  // indicate whether expecting a response from this child
    int startSeconds; // time when it was created
    int startNano; // time when it was created
} process_PCB;

struct PCB childTable[18];

unsigned shmID;             
unsigned* shmPtr; 

char* filename = NULL;    
int processCount;      
int simultaneousCount; 
int processSpawnRate;  

int oneSecond = 1000000000; // nano seconds to seconds
int oneSecondPassed = 0;

int quarterSecond = 250000000; // process schedule time
int quarterSecondPassed = 0;

int totalLaunched = 0;
int totalTerminated = 0;
unsigned long long launchTimePassed = 0;

// resources and allocated tables
int allocatedMatrix[10][18];
int requestMatrix[10][18];
int allResources[10];

// Function prototypes
void showResourceTables();
void showProcessTable();
void launchChildren();
void checkChildMessage();
void sendChildMessage(int i);
void incrementSimulatedClock();
void handleTermination();

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

    // create message queue file
    system("touch msgq.txt"); 

    // initialize process table
    for (int i = 0; i < 18; i++)  
    {
        childTable[i].occupied = 0; // either true or false
        childTable[i].pid = 0;   // process id of this child
        childTable[i].startSeconds = 0; // time when it was created
        childTable[i].startNano = 0;    // time when it was created
        childTable[i].expectingResponse = 0; // do we expect message from this child
    }

    // setup resource matrices
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 18; j++)
        {
            allocatedMatrix[i][j] = 0;
            requestMatrix[i][j] = 0;
        }
    }

    // setup all resources vector
    for (int i =0; i<10; i++) {
        allResources[i] = 0;
    }

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

// Function to print the process table
void showProcessTable() {
    // Open the file in append mode
    FILE* file = fopen(filename, "a+");

    if (file == NULL) {
        perror("Error opening file");
        return;
    }

    // Print to the file
    fprintf(file, "\nOSS PID: %d SysClockS: %d SysclockNano: %d\nProcess Table: \n%-6s%-10s%-8s%-12s%-12s\n",
            getpid(), simClock[0], simClock[1], "Entry", "Occupied", "PID", "StartS", "StartN");

    for (int i = 0; i < totalLaunched; i++) {
        fprintf(file, "%-6d%-10d%-8d%-12u%-12u\n",
                i, childTable[i].occupied, childTable[i].pid, childTable[i].startSeconds, childTable[i].startNano);
    }
    fprintf(file, "\n");

    // Print to the screen
    printf("\nOSS PID: %d SysClockS: %d SysclockNano: %d\nProcess Table: \n%-6s%-10s%-8s%-12s%-12s\n",
            getpid(), simClock[0], simClock[1], "Entry", "Occupied", "PID", "StartS", "StartN");

    for (int i = 0; i < totalLaunched; i++) {
        printf("%-6d%-10d%-8d%-12u%-12u\n",
                i, childTable[i].occupied, childTable[i].pid, childTable[i].startSeconds, childTable[i].startNano);
    }
    printf("\n");

    // Close the file
    fclose(file);
}

// Function to print the resource tables
void showResourceTables() {
    int numResources = 10;
    int numProcesses = 18;

    // Open the file in append mode
    FILE* file = fopen(filename, "a+");

    if (file == NULL) {
        perror("Error opening file");
        return;
    }

    // Print to the file
    fprintf(file, "Allocated Matrix:\n");
    fprintf(file, "%4s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s\n",
            "", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10");

    // append allocation table data
    for (int j = 0; j < numProcesses; j++) {
        fprintf(file, "P%-3d ", j);

        for (int i = 0; i < numResources; i++) {
            fprintf(file, " %-3d", allocatedMatrix[i][j]);
        }

        fprintf(file, "\n");
    }

    fprintf(file, "\n");
    fprintf(file, "Requested Matrix:\n");
    fprintf(file, "%4s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s\n",
            "", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10");

    // append requested table data
    for (int j = 0; j < numProcesses; j++) {
        fprintf(file, "P%-3d ", j);

        for (int i = 0; i < numResources; i++) {
            fprintf(file, " %-3d", requestMatrix[i][j]);
        }

        fprintf(file, "\n");
    }
    fprintf(file, "\n");

    // Print to the screen
    printf("Allocated Matrix:\n");
    printf("%4s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s\n",
            "", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10");

    // append allocation table data
    for (int j = 0; j < numProcesses; j++) {
        printf("P%-3d ", j);

        for (int i = 0; i < numResources; i++) {
            printf(" %-3d", allocatedMatrix[i][j]);
        }

        printf("\n");
    }

    printf("\n");
    printf("Requested Matrix:\n");
    printf("%4s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s\n",
            "", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10");

    // append requested table data
    for (int j = 0; j < numProcesses; j++) {
        printf("P%-3d ", j);

        for (int i = 0; i < numResources; i++) {
            printf(" %-3d", requestMatrix[i][j]);
        }

        printf("\n");
    }
    printf("\n");

    // Close the file
    fclose(file);
}

// Function to launch new children, check deadlocks, and clear resources
void launchChildren() {
    while (totalTerminated != processCount)  {
        // update clock
        launchTimePassed += 100000;
        incrementSimulatedClock();
    
        // determine if we should launch a child
        if (launchTimePassed >= processSpawnRate || totalLaunched == 0) 
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
                    childTable[totalLaunched].pid = pid;
                    childTable[totalLaunched].occupied = 1;
                    childTable[totalLaunched].expectingResponse = 0;
                    childTable[totalLaunched].startSeconds = simClock[0];
                    childTable[totalLaunched].startNano = simClock[1];
                } 
                totalLaunched += 1;
            }

            launchTimePassed = 0;
        }

        // check if any child processes terminated
        for (int i=0; i<totalLaunched; i++) 
        {
            int childStatus;
            pid_t childPid = childTable[i].pid;
            pid_t result = waitpid(childPid, &childStatus, WNOHANG);

            if (result > 0) {
                FILE* file = fopen(filename, "a+");
                if (file == NULL) {
                    perror("Error opening file");
                    return;
                }

                fprintf(file, "Master detected process P%d terminated\n", i);
                printf("Master detected process P%d terminated\n", i);
                
                // child has terminated
                // so we clear the child resources
                fprintf(file, "Releasing resources: ");
                printf("Releasing resources: ");

                for (int c=0; c<10; c++) {
                    if (allocatedMatrix[c][i] > 0)
                    {
                        fprintf(file, "R%d: %d ", c, allocatedMatrix[c][i]);
                        printf("R%d: %d ", c, allocatedMatrix[c][i]);

                        allResources[c] += 1;
                        allocatedMatrix[c][i] = 0;
                    }
                    requestMatrix[c][i] = 0;
                }

                fprintf(file, "\n");
                printf("\n");

                childTable[i].occupied = 0;
                childTable[i].expectingResponse = 0;
                totalTerminated += 1;
                fclose(file);
            }
        }

        // check and send messages to the children
        for (int i=0; i<totalLaunched; i++) 
        {
            if (childTable[i].occupied == 1) {
                if (childTable[i].expectingResponse == 1) {
                    // check for a message from this child process
                    checkChildMessage(i);
                }
                else {
                    // send a message to this child process
                    sendChildMessage(i);
                    childTable[i].expectingResponse = 1;
                }
            }
        }

        // run deadlock detection algorithm
        if (simClock[0] >= oneSecondPassed + 1) 
        {
            // // Open the file in append mode
            // FILE* file = fopen(filename, "a+");

            // if (file == NULL) {
            //     perror("Error opening file");
            //     return;
            // }

            // // Master running deadlock detection

            // fprintf(file, "Master running deadlock detection at time %u:%u\n",
            //     simClock[0], simClock[1]);
            // fprintf(file, "\n");

            // printf("Master running deadlock detection at time %u:%u\n",
            //     simClock[0], simClock[1]);
            // printf("\n");

            // // check ungoing resource requests
            // for (int i=0; i<totalLaunched; i++) {
            //     for (int j=0; j<10; j++) {
            //         if (allResources[j] < 20 && requestMatrix[j][i] == 1)
            //         {
            //             allResources[j] += 1;
            //             requestMatrix[j][i] -= 1;
            //             allocatedMatrix[j][i] += 1;
            //             childTable[i].expectingResponse = 0;

            //             fprintf(file, "Master granting resource R%d to process P%d and removing it from wait queue\n", j, i);
            //             fprintf(file, "\n");

            //             printf("Master granting resource R%d to process P%d and removing it from wait queue\n", j, i);
            //             printf("\n");

            //             // send resource message back to child that was waiting
            //             sendChildMessage(i);
            //             childTable[i].expectingResponse = 1;
            //             break;
            //         }
            //     }
            // }

            // // get child with least amount of time in the system
            // int newestChild = -1;
            // for (int i=totalLaunched; i>0; i--) {
            //     for (int j=0; j<10; j++) {
            //         if (requestMatrix[j][i] == 1)
            //         {
            //             if (newestChild == -1) {
            //                 fprintf(file, "Processes ");
            //                 printf("Processes");
            //                 newestChild = i;
            //             }   

            //             fprintf(file, "P%d ", i);
            //             printf("P%d ", i);
            //             break;
            //         }
            //     }
            // }

            // // remove deadlock
            // // we will remove the child with the least amount of "work" done
            // if (newestChild != -1)
            // {
            //     fprintf(file, "deadlocked\n");
            //     fprintf(file, "\n");

            //     printf("deadlocked\n");
            //     printf("\n");

            //     fprintf(file, "Master terminating P%d to remove deadlock\n", newestChild);
            //     fprintf(file, "\n");

            //     printf("Master terminating P%d to remove deadlock\n", newestChild);
            //     printf("\n");

            //     fprintf(file, "Process P%d terminated", newestChild);
            //     fprintf(file, "\n");

            //     printf("Process P%d terminated\n", newestChild);
            //     printf("\n");

            //     // update matrices
            //     fprintf(file, "    Resources released: ");
            //     printf("    Resources released: ");

            //     for (int r=0; r<10; r++) {
            //         if (allocatedMatrix[r][newestChild] > 0) {

            //             fprintf(file, "R%d : %d ", r, allocatedMatrix[r][newestChild]);
            //             printf("R%d : %d ", r, allocatedMatrix[r][newestChild]);

            //             allResources[r] -= allocatedMatrix[r][newestChild];
            //         }
            //         allocatedMatrix[r][newestChild] = 0;
            //         requestMatrix[r][newestChild] = 0;
            //     }

            //     fprintf(file, "\n");
            //     printf("\n");

            //     childTable[newestChild].occupied = 0;
            //     childTable[newestChild].expectingResponse = 0;
            //     pid_t removeChild = childTable[newestChild].pid;
            //     totalTerminated += 1;

            //     // terminate the child process
            //     if (kill(removeChild, SIGTERM) == -1) {
            //         perror("kill error in parent\n");
            //         exit(1);
            //     }
                
            //     // wait for the child process to terminate
            //     int childStatus;
            //     if (waitpid(removeChild, &childStatus, 0) == -1) {
            //         perror("waitpid error in parent\n");
            //         exit(1);
            //     }
            
            //     printf("Removed child P%d to remove deadlock\n", newestChild);
            // }
            // else 
            // {
            //     fprintf(file, "No deadlocks detected\n");
            //     printf("No deadlocks detected\n");
            // }
            
            // quarterSecondPassed = simClock[1];
            // fclose(file);            

            oneSecondPassed = simClock[0];   
        }

        // show all the resource and process information
        if (simClock[1] >= quarterSecondPassed + quarterSecond 
        || (simClock[1] == 0 && simClock[0] > 1)) 
        {
            showProcessTable();
            showResourceTables();
            quarterSecondPassed = simClock[1];
        }

    }
}
 
// Function to send a message to a child
void sendChildMessage(int targetChild) {
    // Send a message to the child
    // update expecting response flag for the child
    buffer.mtype = childTable[targetChild].pid;
    if (msgsnd(msgqId, &buffer, sizeof(messages) - sizeof(long), 0) == -1) {
        perror("msgsnd to child failed\n");
        exit(1);
    }

}

// Function to check a message from children
void checkChildMessage(int targetChild) {
    // Open the file in append mode
    FILE* file = fopen(filename, "a+");

    if (file == NULL) {
        perror("Error opening file");
        return;
    }

    // get child message
    messages childMsg;
    if (msgrcv(msgqId, &childMsg, sizeof(messages), getpid(), IPC_NOWAIT) == -1) {
        if (errno == ENOMSG) {
            // No message available
        } 
        else {
            perror("Error receiving message in child process");
            exit(1); 
        }
    } 
    else
    {
        // testing here

        // verify the child who sent message
        pid_t senderPID = childMsg.targetChild;
        for (int i=0; i<processCount; i++) 
        {
            if (childTable[i].pid == senderPID) {
                targetChild = i;
            }
        }
        
        // check child message content
        int sendMessageBack = 0;
        if (childMsg.requestOrRelease == 1) 
        {            
            fprintf(file, "Master has acknowledged Process P%d releasing R%d at time %u:%u\n",
                targetChild, childMsg.resourceType, simClock[0], simClock[1]);
            fprintf(file, "\n");
            
            printf("Master has acknowledged Process P%d releasing R%d at time %u:%u\n",
                targetChild, childMsg.resourceType, simClock[0], simClock[1]);
            printf("\n");

            // child is releasing a resource
            allResources[childMsg.resourceType] -= 1;
            allocatedMatrix[childMsg.resourceType][targetChild] -= 1;
            childTable[targetChild].expectingResponse = 0;
            sendMessageBack = 1;
        }
        else 
        {
            fprintf(file, "Master has detected Process P%d requesting R%d at time %u:%u\n",
                targetChild, childMsg.resourceType, simClock[0], simClock[1]);
            fprintf(file, "\n");

            printf("Master has detected Process P%d requesting R%d at time %u:%u\n",
                targetChild, childMsg.resourceType, simClock[0], simClock[1]);
            printf("\n");
            
            // child is requesting a resource
            if (allResources[childMsg.resourceType] != 20) 
            {
                fprintf(file, "Master granting P%d request R%d at time %u:%u\n", 
                    targetChild, childMsg.resourceType, simClock[0], simClock[1]);
                fprintf(file, "\n");

                printf("Master granting P%d request R%d at time %u:%u\n", 
                    targetChild, childMsg.resourceType, simClock[0], simClock[1]);
                printf("\n");

                allResources[childMsg.resourceType] += 1;
                allocatedMatrix[childMsg.resourceType][targetChild] += 1;                
                childTable[targetChild].expectingResponse = 0;
                sendMessageBack = 1;
            }
            else 
            {
                // cant give child resource
                // put them in wait queue
                printf("Master: no instances of R%d available, P%d added to wait queue at time %u:%u\n",
                    childMsg.resourceType, targetChild, simClock[0], simClock[1]);
                printf("\n");

                fprintf(file, "Master: no instances of R%d available, P%d added to wait queue at time %u:%u\n",
                    childMsg.resourceType, targetChild, simClock[0], simClock[1]);
                fprintf(file, "\n");

                // childTable[targetChild].expectingResponse = 1;
                // requestMatrix[childMsg.resourceType][targetChild] = 1;
            }
        }

        // send confirmation message back
        if (sendMessageBack == 1)
        {
            buffer.mtype = childTable[targetChild].pid;
            if (msgsnd(msgqId, &buffer, sizeof(messages) - sizeof(long), 0) == -1) {
                perror("msgsnd to child failed\n");
                exit(1);
            }
        }
    }

    fclose(file);
}

// Function to update the click by 0.1 milliseconds
void incrementSimulatedClock() {
    int nanoseconds = 100000;
    simClock[1] += nanoseconds;

    if (simClock[1] >= 1000000000) 
    {
        // Calculate the number of seconds to add
        // Update seconds and adjust nanoseconds
        unsigned secondsToAdd = simClock[1] / 1000000000;
        simClock[0] += secondsToAdd;
        simClock[1] %= 1000000000;
    }

    memcpy(shmPtr, simClock, sizeof(unsigned int) * 2);
}

// Function to clean up the code
void handleTermination() {
    // kill all child processes
    // clean msg queue and shared memory
    kill(0, SIGTERM);
    msgctl(msgqId, IPC_RMID, NULL);
    shmdt(shmPtr);
    shmctl(shmID, IPC_RMID, NULL);
    exit(0);
}