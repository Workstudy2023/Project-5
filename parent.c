// Author: Christine Mckelvey
// Date: November 14, 2023 

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

char* filename = NULL; // logfile.txt
int processCount;      
int simultaneousCount; 
int processSpawnRate;  

// deadlock detection variables
int oneSecond = 1000000000; // nano seconds to seconds
int oneSecondPassed = 0;

// half a seconds instead
int quarterSecond = 500000000; // process schedule time
int quarterSecondPassed = 0;

// process launching variables
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
void runDetectionAlgorithm();

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
                printf("\noss [-h] [-n proc] [-s simul] [-t timeToLaunchNewChild] [-f logfile]\n");
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
        handleTermination();
    }
    shmPtr = (unsigned*)shmat(shmID, NULL, 0);
    if (shmPtr == NULL) 
    {
        perror("Unable to connect to the shared memory segment.\n");
        handleTermination();
    }
    memcpy(shmPtr, simClock, sizeof(unsigned) * 2);

    // make message queue
    key_t messageQueueKey = ftok("msgq.txt", 1);
    if (messageQueueKey == -1) 
    {
        perror("Unable to generate a key for the message queue.\n");
        handleTermination();
    }

    msgqId = msgget(messageQueueKey, PERMS | IPC_CREAT);
    if (msgqId == -1) 
    {
        perror("Unable to create or access the message queue.\n");
        handleTermination();
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
        handleTermination();
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
    int numProcesses = totalLaunched;

    // Open the file in append mode
    FILE* file = fopen(filename, "a+");

    if (file == NULL) {
        perror("Error opening file");
        handleTermination();
    }

    // Print to the file
    fprintf(file, "Allocated Matrix:\n");
    fprintf(file, "%4s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s\n",
            "", "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9");

    // append allocation table data
    for (int j = 0; j < numProcesses; j++) {
        fprintf(file, "P%-3d ", j);

        for (int i = 0; i < numResources; i++) {
            fprintf(file, " %-3d", allocatedMatrix[i][j]);
        }

        fprintf(file, "\n");
    }


    // update all resources list if necessary
    for (int j=0; j<10; j++) {
        allResources[j] = 0;
    }
    for (int j = 0; j < numProcesses; j++) {
        for (int i = 0; i < numResources; i++) {
            allResources[i] += allocatedMatrix[i][j];
        }
    }

    fprintf(file, "\n");
    fprintf(file, "Requested Matrix:\n");
    fprintf(file, "%4s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s\n",
            "", "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9");

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
            "", "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9");

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
            "", "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9");

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
    while (totalTerminated != processCount) {
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
                    handleTermination();
                }

                fprintf(file, "\nMaster detected process P%d terminated\n", i);
                printf("\nMaster detected process P%d terminated\n", i);
                
                // child has terminated
                // so we clear the child resources
                fprintf(file, "Releasing resources: ");
                printf("Releasing resources: ");

                for (int c=0; c<10; c++) {
                    if (allocatedMatrix[c][i] != 0)
                    {
                        allResources[c] -= allocatedMatrix[c][i];
                        fprintf(file, "R%d: %d ", c, allocatedMatrix[c][i]);
                        printf("R%d: %d ", c, allocatedMatrix[c][i]);
                    }

                    requestMatrix[c][i] = 0;
                    allocatedMatrix[c][i] = 0;
                }

                fprintf(file, "\n");
                printf("\n");

                childTable[i].occupied = 0;
                childTable[i].expectingResponse = 0;
                totalTerminated += 1;
                fclose(file);
            }
        }

        // check if we should stop
        if (processCount == totalTerminated) {
            handleTermination();
        }

        // check and send messages to the children
        checkChildMessage();
        for (int i=0; i<totalLaunched; i++) 
        {
            if (childTable[i].occupied == 1) {
                if (childTable[i].expectingResponse == 0) {
                    // send a message to this child process
                    sendChildMessage(i);
                }
            }
        }

        // run deadlock detection algorithm
        if (simClock[0] >= oneSecondPassed + 1) {
            runDetectionAlgorithm();
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

    handleTermination();
}
 
// Function to run deadlock detection algorithm code
void runDetectionAlgorithm() {
    // update seconds counter
    oneSecondPassed = simClock[0];

    // Open the file in append mode
    FILE* file = fopen(filename, "a+");
    if (file == NULL) {
        perror("Error opening file");
        handleTermination();;
    }

    // print values in allResource vector
    int numResources = 10;   
    int numProcesses = totalLaunched;

    // Master running deadlock detection
    fprintf(file, "Master running deadlock detection at time %u:%u\n", simClock[0], simClock[1]);
    printf("Master running deadlock detection at time %u:%u\n",simClock[0], simClock[1]);

    // check for available resources
    // give resource to children, in wait queue, that are currently waiting for 
    // the resource, if its available.
    for (int i = 0; i < numProcesses; i++) {
        for (int j = 0; j < numResources; j++) {
            if (requestMatrix[j][i] == 1 && allResources[j] != 20) 
            {    
                allResources[j] += 1;
                requestMatrix[j][i] = 0;
                allocatedMatrix[j][i] += 1;
                childTable[i].expectingResponse = 0;

                fprintf(file, "Master detected resource R%d is available, now granting it to process P%d\n    Master removing process P%d from wait queue at time %u:%u\n", 
                    j, i, i, simClock[0], simClock[1]);

                printf("Master detected resource R%d is available, now granting it to process P%d\n    Master removing process P%d from wait queue at time %u:%u\n", 
                    j, i, i, simClock[0], simClock[1]);

                // send resource message back to child that was waiting
                buffer.mtype = childTable[i].pid;
                if (msgsnd(msgqId, &buffer, sizeof(messages) - sizeof(long), 0) == -1) {
                    perror("msgsnd to child failed\n");
                    handleTermination();
                }      

                break;
            }
        }
    }

    // get the last child so we can remove it from the deadlock (our deadlock policy)
    int deadlockedCount = 0;
    int leastActiveChild = 0;
    for (int i = 0; i < numProcesses; i++) 
    {
        for (int j = 0; j < numResources; j++) 
        {
            if (requestMatrix[j][i] == 1) 
            {
                // update the last child
                // this child is potentially deadlocked
                leastActiveChild = i;
                deadlockedCount += 1;
            }
        }
    }

    // remove deadlock
    // we consider a deadlock if there are more than 1 processes waiting for a resource
    if (deadlockedCount > 1)
    {
        printf("Processes ");
        fprintf(file, "Processes ");
        for (int i=0; i<totalLaunched; i++) 
        {
            for (int j=0; j<10; j++)
            {
                if (requestMatrix[j][i] == 1)
                {
                    fprintf(file, "P%d ", i);
                    printf("P%d ", i);
                }
            }
        }
        printf("are deadlocked.\nMaster terminating P%d to remove deadlock\n", leastActiveChild);
        fprintf(file, "are deadlocked.\nMaster terminating P%d to remove deadlock\n", leastActiveChild);
        
        // terminate deadlocked child
        if (kill(childTable[leastActiveChild].pid, SIGKILL) == -1) {
            perror("kill error in parent\n");
            handleTermination();
        }
        else {
            int childStatus;
            if (waitpid(childTable[leastActiveChild].pid, &childStatus, 0) == -1) {
                perror("waitpid error in parent\n");
                handleTermination();
            }
        }

        // clear removed child's resources
        fprintf(file, "Master terminated Process P%d \nReleasing process P%d resources: "
            , leastActiveChild, leastActiveChild);
        printf("Master terminated Process P%d \nReleasing process P%d resources: "
            , leastActiveChild, leastActiveChild);

        for (int c=0; c<10; c++) 
        {
            if (allocatedMatrix[c][leastActiveChild] > 0)
            {
                fprintf(file, "R%d:%d ", c, allocatedMatrix[c][leastActiveChild]);
                printf("R%d:%d ", c, allocatedMatrix[c][leastActiveChild]);
                allResources[c] -= allocatedMatrix[c][leastActiveChild];
            }

            requestMatrix[c][leastActiveChild] = 0;
            allocatedMatrix[c][leastActiveChild] = 0;
        }

        fprintf(file, "\n\n");
        printf("\n\n");

        childTable[leastActiveChild].occupied = 0;
        childTable[leastActiveChild].expectingResponse = 0;
        totalTerminated += 1;
    }
    else
    {
        // no deadlocks detected
        fprintf(file, "No deadlocks detected\n\n");
        printf("No deadlocks detected\n\n");
    }

    // Close file
    fclose(file);            

    // Run detection again to check if deadlock is gone
    if (deadlockedCount > 1) {
        runDetectionAlgorithm();
    }
}

// Function to send a message to a child
void sendChildMessage(int targetChild) {
    // Send a message to the child
    // update expecting response flag for the child
    buffer.mtype = childTable[targetChild].pid;
    if (msgsnd(msgqId, &buffer, sizeof(messages) - sizeof(long), 0) == -1) {
        perror("msgsnd to child failed\n");
        handleTermination();
    }
    childTable[targetChild].expectingResponse = 1;
}

// Function to check a message from children
void checkChildMessage() {        
    // Open the file in append mode
    FILE* file = fopen(filename, "a+");
    if (file == NULL) {
        perror("Error opening file");
        handleTermination();
    }

    // get child message
    messages childMsg;
    if (msgrcv(msgqId, &childMsg, sizeof(messages), getpid(), IPC_NOWAIT) == -1) {
        if (errno == ENOMSG) {
            // No message available
        } 
        else {
            perror("Error receiving message in child process");
            handleTermination(); 
        }
    } 
    else
    {
        // which child sent us a message
        // get the child who sent message
        int targetChild = 0;
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
            fprintf(file, "Master has acknowledged Process P%d releasing R%d at time %u:%u\n\n",
                targetChild, childMsg.resourceType, simClock[0], simClock[1]);
            
            printf("Master has acknowledged Process P%d releasing R%d at time %u:%u\n\n",
                targetChild, childMsg.resourceType, simClock[0], simClock[1]);

            // child is releasing a resource
            allResources[childMsg.resourceType] -= 1;
            allocatedMatrix[childMsg.resourceType][targetChild] -= 1;
            sendMessageBack = 1;
        }
        else 
        {
            fprintf(file, "\nMaster has detected Process P%d requesting R%d at time %u:%u\n",
                targetChild, childMsg.resourceType, simClock[0], simClock[1]);

            printf("\nMaster has detected Process P%d requesting R%d at time %u:%u\n",
                targetChild, childMsg.resourceType, simClock[0], simClock[1]);
            
            // child is requesting a resource
            if (allResources[childMsg.resourceType] != 20) 
            {
                fprintf(file, "Master granting P%d request R%d at time %u:%u\n", 
                    targetChild, childMsg.resourceType, simClock[0], simClock[1]);

                printf("Master granting P%d request R%d at time %u:%u\n", 
                    targetChild, childMsg.resourceType, simClock[0], simClock[1]);

                allResources[childMsg.resourceType] += 1;
                allocatedMatrix[childMsg.resourceType][targetChild] += 1;                
                sendMessageBack = 1;
            }
            else 
            {
                // cant give child resource so put them in wait queue
                printf("Master: no instances of R%d available, P%d added to wait queue at time %u:%u\n\n",
                    childMsg.resourceType, targetChild, simClock[0], simClock[1]);

                fprintf(file, "Master: no instances of R%d available, P%d added to wait queue at time %u:%u\n\n",
                    childMsg.resourceType, targetChild, simClock[0], simClock[1]);

                requestMatrix[childMsg.resourceType][targetChild] = 1;
            }
        }

        // send confirmation message back
        if (sendMessageBack == 1)
        {
            buffer.mtype = childTable[targetChild].pid;
            childTable[targetChild].expectingResponse = 0;

            if (msgsnd(msgqId, &buffer, sizeof(messages) - sizeof(long), 0) == -1) {
                perror("msgsnd to child failed\n");
                handleTermination();
            }
        }
    }

    // update all resources list if necessary
    for (int j=0; j<10; j++) {
        allResources[j] = 0;
    }
    for (int j = 0; j < totalLaunched; j++) {
        for (int i = 0; i < 10; i++) {
            allResources[i] += allocatedMatrix[i][j];
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