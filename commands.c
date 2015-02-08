
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#include "mond.h"
#include "commands.h"
#include "systemThread.h"
#include "monitorThread.h"
#include "webmon.h"
#include "singlyLinkedList.h"

#define SLEEP_DELAY_US 10
#define EXEC_FAIL_STATUS 251   // arbitrary large uncommon number

FileTable *getFileTableEntry(char *file);
ThreadTable *getThreadTableEntry();

extern FileTable fileTable[FILE_TABLE_SIZE];
extern ThreadTable threadTable[THREAD_TABLE_SIZE];
extern ThreadTable systemThreadTable;
extern sem_t availableThreads;
extern int systemThreadState;
extern LinkedList *completedList;
extern int webmonActive;

void add(char *type, char *aux, char *interval, char *logFile) {
   int pidTemp = -1;
   int intervalTemp = -1;
   int isChildFlag = -1;
   int status = -1;

   // get interval
   errno = 0;
   intervalTemp = strtol(interval, NULL, 10);
   if (errno != 0 || intervalTemp <= 0) {
      printf("%s is not a valid interval\n", interval);
      return;
   }

   if (strncmp(type, "-s", MAX_INPUT_LEN - 1) == 0) {

      // setup systemThreadTable
      systemThreadTable.pid = -1;
      systemThreadTable.interval = intervalTemp;
      systemThreadTable.startTime = time(NULL);
      systemThreadTable.fTable = getFileTableEntry(logFile);
      strncpy(systemThreadTable.fileName, logFile, MAX_INPUT_LEN - 1);

      // create pthread
      if (pthread_create(&systemThreadTable.tid, NULL, systemThread, &systemThreadTable) != 0) {
         perror("pthread_create failed");
         exit(-1);
      }

      systemThreadState = SYSTEM_THREAD_RUNNING;

      return;
   }

   // only -p or -e gets here

   // find free table row
   ThreadTable *newThread = getThreadTableEntry();
   if (newThread == NULL) {
      return;
   }

   if (strncmp(type, "-p", MAX_INPUT_LEN - 1) == 0) {
      errno = 0;
      pidTemp = strtol(aux, NULL, 10);
      if (errno != 0 || pidTemp == 0) {
         printf("%s is not a valid process id\n", aux);
         return;
      }
      isChildFlag = 0;
   } else if (strncmp(type, "-e", MAX_INPUT_LEN - 1) == 0) {
      //fork
      if ((pidTemp = vfork()) == -1) {
         perror("fork failed");
         exit(-1);
      } else if (pidTemp == 0) { // child
         if (execlp(aux, aux, NULL) == -1) {
            perror("execlp failed");
            exit(EXEC_FAIL_STATUS);
         }
      }
      isChildFlag = 1;
   } else {
      perror("add failed - unknown argument");
      exit(-1);
   }

   // check for exec fail
   if (strncmp(type, "-e", MAX_INPUT_LEN - 1) == 0) {
      if (waitpid(pidTemp, &status, WNOHANG) == -1) {
         perror("waitpid failed");
         exit(-1);
      }

      if (WIFEXITED(status)) {
         if (WEXITSTATUS(status) == EXEC_FAIL_STATUS) {
            return;
         }
      }
   }

   // initialize table row
   newThread->isChild = isChildFlag;
   newThread->pid = pidTemp;
   newThread->interval = intervalTemp;
   newThread->startTime = time(NULL);
   newThread->fTable = getFileTableEntry(logFile);
   strncpy(newThread->fileName, logFile, MAX_INPUT_LEN - 1);

   // create pthread
   if (pthread_create(&(newThread->tid), NULL, monitorThread, newThread) != 0) {
      perror("pthread_create failed");
      exit(-1);
   }

   // decrement value of available running threads
   sem_wait(&availableThreads);

   return;
}

void printRunning(ThreadTable *line) {
   char pidStr[MAX_INPUT_LEN] = "";

   /*
    *  What threads use this critical section:
    *    Only the command thread uses this critical section.
    *
    *  What shared resources are being protected:
    *    The table row that is passed in is locked so that it cannot be
    *    modified while we are accessing/print the information.
    *
    *  Line justification and performance concerns:
    *    Every line in this critical section (except error handling) must use
    *    the shared resources and therefore, must be locked.  As for
    *    performance concerns, there are only 3 lines.  None of the lines
    *    should block for extended periods of time and all of the information
    *    locked and used is absolutely necessary to proper functioning.
    *
    *  Mutex vs. semaphore decision:
    *    A mutex was used because we only had resources that were
    *    mutually exclusive.  Either it was in use or it wasn't.
    *
    */

   // lock
   if (pthread_mutex_lock(&(line->mutex)) != 0) {
      perror("pthread_mutex_lock failed");
      exit(-1);
   }

   // critical section
   if (line->startTime != 0) {
      if (snprintf(pidStr, MAX_INPUT_LEN, "%lu", (unsigned long)line->pid) < 0) {
         perror("snprintf failed");
         exit(-1);
      }

      printf("|%11lu  |  %10s  |  %10lu  |  %10lu  |  %-1s\n",
            (unsigned long)line->tid,
            (line->pid == -1) ? "system" : pidStr,
            (unsigned long)line->startTime,
            line->interval,
            line->fileName);
   }

   // unlock
   if (pthread_mutex_unlock(&(line->mutex)) != 0) {
      perror("pthread_mutex_unlock failed");
      exit(-1);
   }

   return;
}

void listActive() {
   int i = 0;

   printf("-------------------------\n");
   printf(" List of Active Monitors \n");
   printf("-------------------------\n");
   printf("|  Thread Id  |  Process Id  |  Start Time  |   Interval   |  Log File\n");
   printf("| ----------- | ------------ | ------------ | ------------ | ----------\n");

   if (systemThreadState == SYSTEM_THREAD_RUNNING) {
      printRunning(&systemThreadTable);
   }

   for (i = 0; i < THREAD_TABLE_SIZE; i++) {
      printRunning(&(threadTable[i]));
   }

   return;
}

void listCompleted() {
   char pidStr[MAX_INPUT_LEN] = "";
   int i = 0;

   printf("----------------------------\n");
   printf(" List of Completed Monitors \n");
   printf("----------------------------\n");
   printf("|  Thread Id  |  Process Id  |  Start Time  |   End Time   |   Interval   |  Log File\n");
   printf("| ----------- | ------------ | ------------ | ------------ | ------------ | ----------\n");

   /*
    *  What threads use this critical section:
    *    Only the command thread uses this critical section.
    *
    *  What shared resources are being protected:
    *    The linked list of completed tasks is the only resource locked.
    *
    *  Line justification and performance concerns:
    *    Every line in this critical section (except error handling and for
    *    loop control flow) must use the shared resources and therefore, must
    *    be locked.  As for performance concerns none of the lines
    *    should block for extended periods of time and all of the information
    *    locked and used is absolutely necessary to proper functioning.  Also,
    *    only exiting monitoring threads would block waiting for access to the
    *    linked list which should be of little concern since they are dead.
    *
    *  Mutex vs. semaphore decision:
    *    A mutex was used because we only had resources that were
    *    mutually exclusive.  Either it was in use or it wasn't.
    *
    */

   // lock
   if (pthread_mutex_lock(&(completedList->mutex)) != 0) {
      perror("pthread_mutex_lock failed");
      exit(-1);
   }

   // critical section
   for (i = 0; i < LLSize(completedList); i++) {
      ThreadTable *line = (ThreadTable *)LLGet(completedList, i);
      if (snprintf(pidStr, MAX_INPUT_LEN, "%lu", (unsigned long)line->pid) < 0) {
         perror("snprintf failed");
         exit(-1);
      }

      printf("|%11lu  |  %10s  |  %10lu  |  %10lu  |  %10lu  |  %-1s\n",
            (unsigned long)line->tid,
            (line->pid == -1) ? "system" : pidStr,
            (unsigned long)line->startTime,
            (unsigned long)line->endTime,
            line->interval,
            line->fileName);
   }

   // unlock
   if (pthread_mutex_unlock(&(completedList->mutex)) != 0) {
      perror("pthread_mutex_unlock failed");
      exit(-1);
   }

   return;
}

void removeThread(pthread_t tid) {
   int found = 0;

   /*
    *  What threads use this critical section:
    *    Only the command thread uses this critical section.
    *
    *  What shared resources are being protected:
    *    The systemThreadTable is the only resource locked. We lock
    *    the whole line, but are interested in the thread id and endStatus.
    *
    *  Line justification and performance concerns:
    *    Every line in this critical section (except error handling and a
    *    single assignment) must use the shared resources and therefore, must
    *    be locked.  As for performance concerns none of the lines
    *    should block for extended periods of time and all of the information
    *    locked and used is absolutely necessary to proper functioning.  Also,
    *    only the system thread would block waiting for access to the
    *    systemThreadTable which shouldn't be of concern assuming this is a
    *    short locking period.
    *
    *  Mutex vs. semaphore decision:
    *    A mutex was used because we only had resources that were
    *    mutually exclusive.  Either it was in use or it wasn't.
    *
    */

   // Check System Thread
   // lock
   if (pthread_mutex_lock(&(systemThreadTable.mutex)) != 0) {
      perror("pthread_mutex_lock failed");
      exit(-1);
   }

   // critical section
   if (pthread_equal(systemThreadTable.tid, tid) != 0) { // equal
      // tell it to stop
      systemThreadTable.endStatus = STOPPED;
      found = 1;
   }

   // unlock
   if (pthread_mutex_unlock(&(systemThreadTable.mutex)) != 0) {
      perror("pthread_mutex_unlock failed");
      exit(-1);
   }

   // Check Monitor Threads
   int i = 0;
   for (i = 0; i < THREAD_TABLE_SIZE && found == 0; i++) {

      /*
       *  What threads use this critical section:
       *    Only the command thread uses this critical section.
       *
       *  What shared resources are being protected:
       *    Each line of the threadTable individually is the only resource 
       *    locked. We lock the whole line, but are interested in the thread id
       *    and endStatus.
       *
       *  Line justification and performance concerns:
       *    Every line in this critical section (except error handling and a
       *    single assignment) must use the shared resources and therefore, must
       *    be locked.  As for performance concerns none of the lines
       *    should block for extended periods of time and all of the information
       *    locked and used is absolutely necessary to proper functioning.  Also,
       *    only individual monitoring thread would block waiting for access to the
       *    systemThreadTable which shouldn't be of concern assuming this is a
       *    short locking period.  This has been optimized so that when found,
       *    the for loop exits early if possible so that further resources
       *    aren't locked unnecessarily.  We choose finer grain line locking as
       *    opposed to full table locking in order to reduce wait times.
       *
       *  Mutex vs. semaphore decision:
       *    A mutex was used because we only had resources that were
       *    mutually exclusive.  Either it was in use or it wasn't.
       *
       */

      // lock
      if (pthread_mutex_lock(&(threadTable[i].mutex)) != 0) {
         perror("pthread_mutex_lock failed");
         exit(-1);
      }

      // critical section
      if (pthread_equal(threadTable[i].tid, tid) != 0) { // equal
         // tell it to stop
         threadTable[i].endStatus = STOPPED;
         found = 1;
      }

      // unlock
      if (pthread_mutex_unlock(&(threadTable[i].mutex)) != 0) {
         perror("pthread_mutex_unlock failed");
         exit(-1);
      }
   }

   if (found == 0) {
      printf("Thread not found\n");
   }

   return;
}

void killProcess(pid_t pid) {
   int found = 0;
   ThreadTable *line = NULL;

   // Check Monitor Threads
   int i = 0;
   for (i = 0; i < THREAD_TABLE_SIZE && found == 0; i++) {

      /*
       *  What threads use this critical section:
       *    Only the command thread uses this critical section.
       *
       *  What shared resources are being protected:
       *    Each line of the threadTable individually is the only resource
       *    locked. We lock the whole line, but are interested in the pid.
       *
       *  Line justification and performance concerns:
       *    Every line in this critical section (except the optimiztion flag)
       *    must use the shared resources and therefore, must be locked.  As
       *    for performance concerns none of the lines should block for
       *    extended periods of time and all of the information locked and used
       *    is absolutely necessary to proper functioning.  Also, only
       *    individual monitoring thread would block waiting for access to the
       *    threadTable which shouldn't be of concern assuming this is a
       *    short locking period.  This has been optimized so that when found,
       *    the for loop exits early if possible so that further resources
       *    aren't locked unnecessarily.  We choose finer grain line locking as
       *    opposed to full table locking in order to reduce wait times.
       *
       *  Mutex vs. semaphore decision:
       *    A mutex was used because we only had resources that were
       *    mutually exclusive.  Either it was in use or it wasn't.
       *
       */

      // lock
      if (pthread_mutex_lock(&(threadTable[i].mutex)) != 0) {
         perror("pthread_mutex_lock failed");
         exit(-1);
      }

      // critical section
      if (threadTable[i].pid == pid) { // equal
         line = &(threadTable[i]);
         found = 1;
      }

      // unlock
      if (pthread_mutex_unlock(&(threadTable[i].mutex)) != 0) {
         perror("pthread_mutex_unlock failed");
         exit(-1);
      }
   }

   if (found == 0) {
      printf("Process not found\n");
   } else {
      if (kill(pid, SIGTERM) == -1) {
         perror("kill failed");
         if (errno != EPERM) {
            exit(-1);
         }
         return;
      }

      /*
       *  What threads use this critical section:
       *    Only the command thread uses this critical section.
       *
       *  What shared resources are being protected:
       *    Each line of the threadTable individually is the only resource
       *    locked. We lock this line of the threadTable, but are interested in
       *    the endStatus.
       *
       *  Line justification and performance concerns:
       *    Every line in this critical section must use the shared resources
       *    and therefore, must be locked.  As for performance concerns, none
       *    of the lines should block for extended periods of time and all of
       *    the information locked and used is absolutely necessary to proper
       *    functioning.  Also, only individual monitoring thread would block
       *    waiting for access to the threadTable which shouldn't be of concern
       *    assuming this is a short locking period.
       *
       *  Mutex vs. semaphore decision:
       *    A mutex was used because we only had resources that were
       *    mutually exclusive.  Either it was in use or it wasn't.
       *
       */

      // lock
      if (pthread_mutex_lock(&(line->mutex)) != 0) {
         perror("pthread_mutex_lock failed");
         exit(-1);
      }

      // tell it to stop
      line->endStatus = KILLED;

      // unlock
      if (pthread_mutex_unlock(&(line->mutex)) != 0) {
         perror("pthread_mutex_unlock failed");
         exit(-1);
      }
   }

   return;
}

void exitMond() {
   int value = -1;

   if (systemThreadState == SYSTEM_THREAD_RUNNING) { // stop system thread

      /*
       *  What threads use this critical section:
       *    Only the command thread uses this critical section.
       *
       *  What shared resources are being protected:
       *    The systemTableTable individually is the only resource locked. We lock
       *    this line of the systemThreadTable, but are only interested in the
       *    endStatus.
       *
       *  Line justification and performance concerns:
       *    Every line in this critical section must use the shared resources
       *    and therefore, must be locked.  As for performance concerns, none
       *    of the lines should block for extended periods of time and all of
       *    the information locked and used is absolutely necessary to proper
       *    functioning.  Also, only the system thread would block waiting for
       *    access to the systemThreadTable which shouldn't be of concern
       *    because we are trying to exit it.
       *
       *  Mutex vs. semaphore decision:
       *    A mutex was used because we only had resources that were
       *    mutually exclusive.  Either it was in use or it wasn't.
       *
       */

      // lock
      if (pthread_mutex_lock(&(systemThreadTable.mutex)) != 0) {
         perror("pthread_mutex_lock failed");
         exit(-1);
      }

      // critical section
      systemThreadTable.endStatus = STOPPED;

      // unlock
      if (pthread_mutex_unlock(&(systemThreadTable.mutex)) != 0) {
         perror("pthread_mutex_unlock failed");
         exit(-1);
      }
   }

   // Check Monitor Threads
   int i = 0;
   for (i = 0; i < THREAD_TABLE_SIZE; i++) {  // stop monitor threads

      /*
       *  What threads use this critical section:
       *    Only the command thread uses this critical section.
       *
       *  What shared resources are being protected:
       *    Each line of the threadTable individually is the only resource
       *    locked. We lock each line of the threadTable, but are interested in
       *    the startTime and endStatus.
       *
       *  Line justification and performance concerns:
       *    Every line in this critical section must use the shared resources
       *    and therefore, must be locked.  As for performance concerns, none
       *    of the lines should block for extended periods of time and all of
       *    the information locked and used is absolutely necessary to proper
       *    functioning.  Also, only individual monitoring thread would block
       *    waiting for access to the threadTable which shouldn't be of concern
       *    assuming this is a short locking period.
       *
       *  Mutex vs. semaphore decision:
       *    A mutex was used because we only had resources that were
       *    mutually exclusive.  Either it was in use or it wasn't.
       *
       */

      // lock
      if (pthread_mutex_lock(&(threadTable[i].mutex)) != 0) {
         perror("pthread_mutex_lock failed");
         exit(-1);
      }

      // critical section
      if (threadTable[i].startTime != 0) { // equal
         // tell it to stop
         threadTable[i].endStatus = STOPPED;
      }

      // unlock
      if (pthread_mutex_unlock(&(threadTable[i].mutex)) != 0) {
         perror("pthread_mutex_unlock failed");
         exit(-1);
      }
   }

   // wait for system thread to end
   while (systemThreadState != SYSTEM_THREAD_NOT_RUNNING) {
      usleep(SLEEP_DELAY_US);
   }

   if (sem_getvalue(&availableThreads, &value) == -1) {
      perror("sem_getvalue failed");
      exit(-1);
   }

   // wait for all monitor threads to end
   while (value != THREAD_TABLE_SIZE) {
      if (sem_getvalue(&availableThreads, &value) == -1) {
         perror("sem_getvalue failed");
         exit(-1);
      }
      usleep(SLEEP_DELAY_US);
   }

   return;
}

FileTable *getFileTableEntry(char *file) {
   FileTable *fTable = NULL;
   FileTable *openFileEntry = NULL;
   FILE *filep = NULL;
   int i = 0;
   struct stat buf;

   if (stat(file, &buf) == -1) {
      if (errno != ENOENT) {
         perror("stat failed");
         exit(-1);
      } else {
         if ((filep = fopen(file, "w")) == NULL) {
            perror("fopen failed");
            exit(-1);
         }
         if (stat(file, &buf) == -1) {
            perror("stat failed");
            exit(-1);
         }
         if (fclose(filep) != 0) {
            perror("fclose");
            exit(-1);
         }
      }
   }

   for (i = 0; i < FILE_TABLE_SIZE; i++) {

      /*
       *  What threads use this critical section:
       *    Only the command thread uses this critical section.
       *
       *  What shared resources are being protected:
       *    Each line of the fileTable individually is the only resource
       *    locked. We lock each line of the fileTable, but are interested in
       *    the device id and the inode.
       *
       *  Line justification and performance concerns:
       *    Every line in this critical section must use the shared resources
       *    and therefore, must be locked.  As for performance concerns, none
       *    of the lines should block for extended periods of time and all of
       *    the information locked and used is absolutely necessary to proper
       *    functioning.  Also, any monitoring thread or system thread would
       *    block trying to access its row of the fileTable which shouldn't be
       *    of concern assuming this is a short locking period.
       *
       *  Mutex vs. semaphore decision:
       *    A mutex was used because we only had resources that were
       *    mutually exclusive.  Either it was in use or it wasn't.
       *
       */

      // lock
      if (pthread_mutex_lock(&(fileTable[i].mutex)) != 0) {
         perror("pthread_mutex_lock failed");
         exit(-1);
      }

      // critical section
      if (fileTable[i].dev == 0 && fileTable[i].inode == 0) {
         openFileEntry = &fileTable[i];
      }

      if (fileTable[i].dev == buf.st_dev && fileTable[i].inode == buf.st_ino) {
         fTable = &fileTable[i];
         sem_post(&fileTable[i].count);
      }

      // unlock
      if (pthread_mutex_unlock(&(fileTable[i].mutex)) != 0) {
         perror("pthread_mutex_unlock failed");
         exit(-1);
      }

      // stop looking - it was found
      if (fTable != NULL) {
         return fTable;
      }
   }

   if ((filep = fopen(file, "w")) == NULL) {
      perror("fopen failed");
      exit(-1);
   }

   sem_post(&(openFileEntry->count));
   openFileEntry->filep = filep;
   openFileEntry->dev = buf.st_dev;
   openFileEntry->inode = buf.st_ino;

   return openFileEntry;
}

ThreadTable *getThreadTableEntry() {
   ThreadTable *availableThread = NULL;
   int i = 0;

   for (i = 0; i < THREAD_TABLE_SIZE; i++) {

      /*
       *  What threads use this critical section:
       *    Only the command thread uses this critical section.
       *
       *  What shared resources are being protected:
       *    Each line of the threadTable individually is the only resource
       *    locked. We lock each line of the threadTable, but are interested in
       *    the startTime.
       *
       *  Line justification and performance concerns:
       *    Every line in this critical section must use the shared resources
       *    and therefore, must be locked.  As for performance concerns, none
       *    of the lines should block for extended periods of time and all of
       *    the information locked and used is absolutely necessary to proper
       *    functioning.  Also, any monitoring thread would block trying to access
       *    its row of the threadTable which shouldn't be of concern assuming this
       *    is a short locking period.  We did optimize this section, so that when
       *    a free row is found we stop looping and return the information.
       *
       *  Mutex vs. semaphore decision:
       *    A mutex was used because we only had resources that were
       *    mutually exclusive.  Either it was in use or it wasn't.
       *
       */

      // lock
      if (pthread_mutex_lock(&(threadTable[i].mutex)) != 0) {
         perror("pthread_mutex_lock failed");
         exit(-1);
      }

      // critical section
      if (threadTable[i].startTime == 0) {
         availableThread = &(threadTable[i]);
      }

      // unlock
      if (pthread_mutex_unlock(&(threadTable[i].mutex)) != 0) {
         perror("pthread_mutex_unlock failed");
         exit(-1);
      }

      // stop looking - it was found
      if (availableThread != NULL) {
         break;
      }
   }

   return availableThread;
}

void startWebmon(int intervalSec, int refreshSec, char *file) {
   pthread_t webmonHandle;
   WebmonParams *webmonParams = NULL;

   if ((webmonParams = (WebmonParams *)calloc(1, sizeof (WebmonParams))) == NULL) {
      perror("calloc failed");
      exit(-1);
   }

   webmonParams->intervalSec = intervalSec;
   webmonParams->refreshSec = refreshSec;
   strncpy(webmonParams->file, file, MAX_INPUT_LEN - 1);

   // create pthread
   if (pthread_create(&webmonHandle, NULL, webmonThread, webmonParams) != 0) {
      perror("pthread_create failed");
      exit(-1);
   }

   webmonActive = WEBMON_THREAD_RUNNING;

   return;
}
