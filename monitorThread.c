
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/time.h>

#include "monitorThread.h"
#include "mond.h"
#include "logLibrary.h"
#include "singlyLinkedList.h"

int openProcessFiles(int pid, int *fdStatProc, int *fdStatm);
void printProcessLogs(FILE *fLogFile, int pid, int fdStatProc, int fdStatm);
void closeProcessFiles(int fdStatProc, int fdStatm);

extern sem_t availableThreads;
extern LinkedList *completedList;


void *monitorThread(void *args) {
   int fdStat = -1, fdStatm = -1;
   ThreadTable *threadTableLine = NULL;
   int value = -1;
   int stop = 0;
   pid_t childPid = -1;
   int status = -1;
   int isChildFlag = -1;
   unsigned long sleepTime = -1;
   struct timeval startTime, endTime;
   unsigned long offsetTime = -1;

   ThreadTable *threadTableHandle = (ThreadTable *)args;

   if ((threadTableLine = (ThreadTable *)calloc(1, sizeof (ThreadTable))) == NULL) {
      perror("calloc failed");
      exit(-1);
   }

   while (1) {

      if (gettimeofday(&startTime, NULL) == -1) {
         perror("gettimeofday failed");
         exit(-1);
      }

      /*
       *  What threads use this critical section:
       *    Only the individual monitoring thread uses this critical section.
       *
       *  What shared resources are being protected:
       *    This thread's line of the threadTable is the only resource locked.
       *    We lock the whole line of the threadTable, but are interested in
       *    the pid, isChild flag, and fileTable reference.
       *
       *  Line justification and performance concerns:
       *    Every line in this critical section (except error handling) must
       *    use the shared resources and therefore, must be locked.  As for
       *    performance concerns, open or print may take a small amount of time
       *    blocking for opening and writing the logs.  The information locked
       *    and used is absolutely necessary to proper functioning.  Also, only
       *    the command thread may block while to trying to get access to the
       *    threadTable.
       *
       *  Mutex vs. semaphore decision:
       *    A mutex was used because we only had resources that were
       *    mutually exclusive.  Either it was in use or it wasn't.
       *
       */

      // lock outer
      if (pthread_mutex_lock(&(threadTableHandle->mutex)) != 0) {
         perror("pthread_mutex_lock failed");
         exit(-1);
      }

      childPid = threadTableHandle->pid;
      isChildFlag = threadTableHandle->isChild;

      /*
       *  What threads use this critical section:
       *    Only the individual monitoring thread uses this critical section.
       *
       *  What shared resources are being protected:
       *    This thread's line of the fileTable is the only resource locked.
       *    We lock the whole line of the fileTable, but are interested in
       *    the file pointer.
       *
       *  Line justification and performance concerns:
       *    Every line in this critical section (except error handling and stop
       *    flags) must use the shared resources and therefore, must be locked.
       *    The information locked and used is absolutely necessary to proper
       *    functioning.  All other threads may block while trying to get
       *    access to the fileTable. As for performance concerns, open or print
       *    may take a small amount of time blocking for opening and writing
       *    the logs.  Contention for the fileTable is mitigated by taking into
       *    account the amount of time to acquire the locks when writing to the
       *    logs (ie. the offset is subtracted from the interval time).
       *
       *  Mutex vs. semaphore decision:
       *    A mutex was used because we only had resources that were
       *    mutually exclusive.  Either it was in use or it wasn't.
       *
       */

      // lock inner
      if (pthread_mutex_lock(&(threadTableHandle->fTable->mutex)) != 0) {
         perror("pthread_mutex_lock failed");
         exit(-1);
      }

      // critical section
      if (openProcessFiles(threadTableHandle->pid, &fdStat, &fdStatm) == 0) {
         printProcessLogs(threadTableHandle->fTable->filep, threadTableHandle->pid, fdStat, fdStatm);
         stop = 0;
      } else {
         if (threadTableHandle->endStatus == RUNNING) {
            threadTableHandle->endStatus = EXITED;
         }
         stop = 1;
      }

      // unlock inner
      if (pthread_mutex_unlock(&(threadTableHandle->fTable->mutex)) != 0) {
         perror("pthread_mutex_unlock failed");
         exit(-1);
      }

      // unlock unlock outer
      if (pthread_mutex_unlock(&(threadTableHandle->mutex)) != 0) {
         perror("pthread_mutex_unlock failed");
         exit(-1);
      }

      if (stop == 0) {
         closeProcessFiles(fdStat, fdStatm);
      }

      /*
       *  What threads use this critical section:
       *    Only the individual monitoring thread uses this critical section.
       *
       *  What shared resources are being protected:
       *    This thread's line of the threadTable is the only resource locked.
       *    We lock the whole line of the threadTable because we are interested
       *    in the endStatus.  Optionally, we are interesting in all of the
       *    fields if we are in fact exiting because they are used or reset
       *    (for cleaning up).
       *
       *  Line justification and performance concerns:
       *    Every line in this critical section (except error handling and the
       *    stop flags) must use the shared resources and therefore, must be
       *    locked.  The information locked and used is absolutely necessary
       *    to proper functioning.  Also, as for performance concerns only
       *    the command thread may block while to trying to get access to the
       *    threadTable, but is of little concern because it will only lock for
       *    a short amount of time to perform the exit check or a longer time (on
       *    its way to exiting and cleaning up).
       *
       *  Mutex vs. semaphore decision:
       *    A mutex was used because we only had resources that were
       *    mutually exclusive.  Either it was in use or it wasn't.
       *
       */

      // Check to Terminate Thread
      // lock outer
      if (pthread_mutex_lock(&(threadTableHandle->mutex)) != 0) {
         perror("pthread_mutex_lock failed");
         exit(-1);
      }

      // critical section
      if (threadTableHandle->endStatus != RUNNING) {

         // copy table entry for linked list
         memcpy(threadTableLine, threadTableHandle, sizeof (ThreadTable));

         /*
          *  What threads use this critical section:
          *    Only the individual monitoring thread uses this critical section.
          *
          *  What shared resources are being protected:
          *    This thread's line of the fileTable is the only resource locked.
          *    We lock the whole line of the fileTable, but are interested in
          *    the count semaphore and optionally other fields if we are the
          *    last to use the file.
          *
          *  Line justification and performance concerns:
          *    Every line in this critical section (except error handling) must
          *    use the shared resources and therefore, must be locked. The
          *    information locked and used is absolutely necessary to proper
          *    functioning.  All other threads may block while trying to get
          *    access to the fileTable. As for performance concerns, close may
          *    take a small amount of time.  Contention for the fileTable is
          *    mitigated by taking into account the amount of time to acquire
          *    the locks when writing to the logs (ie. the offset is subtracted
          *    from the interval time).
          *
          *  Mutex vs. semaphore decision:
          *    A mutex was used because we only had resources that were
          *    mutually exclusive.  Either it was in use or it wasn't.
          *
          */

         // clean up file table (close if necessary)
         // lock inner
         if (pthread_mutex_lock(&(threadTableHandle->fTable->mutex)) != 0) {
            perror("pthread_mutex_lock failed");
            exit(-1);
         }

         // critical section
         if (sem_wait(&(threadTableHandle->fTable->count)) == -1) {
            perror("sem_wait failed");
            exit(-1);
         }

         if (sem_getvalue(&(threadTableHandle->fTable->count), &value) == -1) {
            perror("sem_getvalue failed");
            exit(-1);
         }

         if (value == 0) { // last thread using file
            fclose(threadTableHandle->fTable->filep);
            // clean up file table
            threadTableHandle->fTable->filep = NULL;
            threadTableHandle->fTable->dev = 0;
            threadTableHandle->fTable->inode = 0;
         }

         // unlock inner
         if (pthread_mutex_unlock(&(threadTableHandle->fTable->mutex)) != 0) {
            perror("pthread_mutex_unlock failed");
            exit(-1);
         }

         // clean up thread table
         threadTableHandle->tid = 0;
         threadTableHandle->pid = 0;
         threadTableHandle->fTable = NULL;
         threadTableHandle->interval = 0;
         threadTableHandle->startTime = 0;
         threadTableHandle->endTime = 0;
         threadTableHandle->endStatus = RUNNING;

         stop = 1;
      }

      sleepTime = threadTableHandle->interval;

      // unlock unlock outer
      if (pthread_mutex_unlock(&(threadTableHandle->mutex)) != 0) {
         perror("pthread_mutex_unlock failed");
         exit(-1);
      }

      if (stop == 1) {
         break;
      }

      if (isChildFlag == 1) {
         // check for child cleanup
         if (waitpid(childPid, &status, WNOHANG) == -1) {
            perror("waitpid failed");
            exit(-1);
         }
      }

      if (gettimeofday(&endTime, NULL) == -1) {
         perror("gettimeofday failed");
         exit(-1);
      }

      offsetTime = (endTime.tv_sec * CONVERT_SEC_TO_USEC + endTime.tv_usec) -
         (startTime.tv_sec * CONVERT_SEC_TO_USEC + startTime.tv_usec);

      // wait interval time
      longSleep(sleepTime - offsetTime);
   }

   threadTableLine->endTime = time(NULL);

   /*
    *  What threads use this critical section:
    *    Only the monitor thread uses this critical section.
    *
    *  What shared resources are being protected:
    *    The linked list of completed tasks is the only resource locked.
    *
    *  Line justification and performance concerns:
    *    Every line in this critical section (except error handling) must use
    *    the shared resources and therefore, must be locked.  As for performance
    *    concerns none of the lines should block for extended periods of time
    *    and all of the information locked and used is absolutely necessary
    *    to proper functioning.  Also, only the command thread or another dying
    *    tread would block waiting for access to the linked list which should
    *    be of little concern since they are dead and the linked list insert
    *    will be fast since we keep a tail reference rather than using
    *    traversal.
    *
    *  Mutex vs. semaphore decision:
    *    A mutex was used because we only had resources that were
    *    mutually exclusive.  Either it was in use or it wasn't.
    *
    */

   // linked list - add node
   // lock linked list
   if (pthread_mutex_lock(&(completedList->mutex)) != 0) {
      perror("pthread_mutex_lock failed");
      exit(-1);
   }

   // critical section
   if (LLInsertTail(completedList, threadTableLine) == -1) {
      perror("calloc failed");
      exit(-1);
   }

   // unlock linked list
   if (pthread_mutex_unlock(&(completedList->mutex)) != 0) {
      perror("pthread_mutex_unlock failed");
      exit(-1);
   }

   sem_post(&availableThreads);

   return NULL;
}

int openProcessFiles(int pid, int *fdStatProc, int *fdStatm) {
   char file[MAX_INPUT_LEN] = "";

   if (snprintf(file, MAX_INPUT_LEN - 1, "/proc/%d/stat", pid) < 0) {
      perror("snprintf failed");
      exit(-1);
   }

   if ((*fdStatProc = open(file, O_RDONLY)) == -1) {
      return -1;
   }

   if (snprintf(file, MAX_INPUT_LEN - 1, "/proc/%d/statm", pid) < 0) {
      perror("snprintf failed");
      exit(-1);
   }

   if ((*fdStatm = open(file, O_RDONLY)) == -1) {
      return -1;
   }

   return 0;
}

void printProcessLogs(FILE *fLogFile, int pid, int fdStatProc, int fdStatm) {
   char timeStr[MAX_INPUT_LEN] = "";

   // log statistics
   fprintf(fLogFile, "[%s] Process(%d) ", generateLogTime(timeStr), pid);
   printQuery(fLogFile, fdStatProc, " [STAT] executable", 0, 1);
   printQuery(fLogFile, fdStatProc, " stat", 0, 2);
   printQuery(fLogFile, fdStatProc, " minorfaults", 0, 9);
   printQuery(fLogFile, fdStatProc, " majorfaults", 0, 11);
   printQuery(fLogFile, fdStatProc, " usermodetime", 0, 13);
   printQuery(fLogFile, fdStatProc, " kernelmodetime", 0, 14);
   printQuery(fLogFile, fdStatProc, " priority", 0, 17);
   printQuery(fLogFile, fdStatProc, " nice", 0, 18);
   printQuery(fLogFile, fdStatProc, " nothreads", 0, 19);
   printQuery(fLogFile, fdStatProc, " vsize", 0, 22);
   printQuery(fLogFile, fdStatProc, " rss", 0, 23);
   fprintf(fLogFile, " [STATM]");
   printQuery(fLogFile, fdStatm, " program", 0, 0);
   printQuery(fLogFile, fdStatm, " residentset", 0, 1);
   printQuery(fLogFile, fdStatm, " share", 0, 2);
   printQuery(fLogFile, fdStatm, " text", 0, 3);
   printQuery(fLogFile, fdStatm, " data", 0, 5);
   fprintf(fLogFile, "\n");

   return;
}

void closeProcessFiles(int fdStatProc, int fdStatm) {
   close(fdStatProc);
   close(fdStatm);

   return;
}

