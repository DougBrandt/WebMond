
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "mond.h"
#include "systemThread.h"
#include "logLibrary.h"
#include "singlyLinkedList.h"

void openSysFiles(int *fdStat, int *fdMem, int *fdLoad, int *fdDisk);
void printSysLogs(FILE *fLogFile, int fdStat, int fdMem, int fdLoad, int fdDisk);
void closeSysFiles(int fdStat, int fdMem, int fdLoad, int fdDisk);

extern int systemThreadState;
extern LinkedList *completedList;


void *systemThread(void *args) {
   int fdStat = -1, fdMem = -1, fdLoad = -1, fdDisk = -1;
   int stop = 0;
   ThreadTable *threadTableLine = NULL;
   int value = -1;
   unsigned long sleepTime = -1;
   struct timeval startTime, endTime;
   unsigned long offsetTime = -1;

   ThreadTable *threadTableHandle = (ThreadTable *)args;

   openSysFiles(&fdStat, &fdMem, &fdLoad, &fdDisk);

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
       *    Only the system thread uses this critical section.
       *
       *  What shared resources are being protected:
       *    The systemThreadTable is the only resource locked. We lock the
       *    whole line of the threadTable, but are interested in the fileTable
       *    reference.
       *
       *  Line justification and performance concerns:
       *    Every line in this critical section (except error handling) must
       *    use the shared resources and therefore, must be locked.  As for
       *    performance concerns, print may take a small amount of time
       *    writing the logs.  The information locked and used is absolutely
       *    necessary to proper functioning.  Also, only the command thread
       *    may block while to trying to get access to the systemThreadTable,
       *    but only the printing of listactive would be delayed.
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

      /*
       *  What threads use this critical section:
       *    Only the system thread uses this critical section.
       *
       *  What shared resources are being protected:
       *    This thread's line of the fileTable is the only resource locked.
       *    We lock the whole line of the fileTable, but are interested in
       *    the file pointer.
       *
       *  Line justification and performance concerns:
       *    Every line in this critical section (except error handling) must
       *    use the shared resources and therefore, must be locked.
       *    The information locked and used is absolutely necessary to proper
       *    functioning.  All other threads may block while trying to get
       *    access to the fileTable. As for performance concerns, print
       *    may take a small amount of time blocking for writing
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
      printSysLogs(threadTableHandle->fTable->filep, fdStat, fdMem, fdLoad, fdDisk);

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

      /*
       *  What threads use this critical section:
       *    Only the system thread uses this critical section.
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
       *    systemThreadTable, but is of little concern because it will only lock
       *    for a short amount of time to perform the exit check or a longer time (on
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
      if (threadTableHandle->endStatus == STOPPED) {

         // copy table entry for linked list
         memcpy(threadTableLine, threadTableHandle, sizeof (ThreadTable));

         /*
          *  What threads use this critical section:
          *    Only the system thread uses this critical section.
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
    *    Only the system thread uses this critical section.
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

   systemThreadState = SYSTEM_THREAD_NOT_RUNNING;

   return NULL;
}

void openSysFiles(int *fdStat, int *fdMem, int *fdLoad, int *fdDisk) {

   if ((*fdStat = open("/proc/stat", O_RDONLY)) == -1) {
      perror("open failed");
      exit(-1);
   }

   if ((*fdMem = open("/proc/meminfo", O_RDONLY)) == -1) {
      perror("open failed");
      exit(-1);
   }

   if ((*fdLoad = open("/proc/loadavg", O_RDONLY)) == -1) {
      perror("open failed");
      exit(-1);
   }

   if ((*fdDisk = open("/proc/diskstats", O_RDONLY)) == -1) {
      perror("open failed");
      exit(-1);
   }

   return;
}

void printSysLogs(FILE *fLogFile, int fdStat, int fdMem, int fdLoad, int fdDisk) {
   char timeStr[MAX_TIME_LEN] = "";

   // log statistics
   fprintf(fLogFile, "[%s] System ", generateLogTime(timeStr));
   fprintf(fLogFile, " [PROCESS]");
   printQuery(fLogFile, fdStat, " cpuusermode", 0, 1);
   printQuery(fLogFile, fdStat, " cpusystemmode", 0, 3);
   printQuery(fLogFile, fdStat, " idletaskrunning", 0, 4);
   printQuery(fLogFile, fdStat, " iowaittime", 0, 5);
   printQuery(fLogFile, fdStat, " irqservicetime", 0, 6);
   printQuery(fLogFile, fdStat, " softirqservicetime", 0, 7);
   printQuery(fLogFile, fdStat, " intr", 2, 1);
   printQuery(fLogFile, fdStat, " ctxt", 3, 1);
   printQuery(fLogFile, fdStat, " forks", 5, 1);
   printQuery(fLogFile, fdStat, " runnable", 6, 1);
   printQuery(fLogFile, fdStat, " blocked", 7, 1);
   fprintf(fLogFile, " [MEMORY]");
   printQuery(fLogFile, fdMem, " memtotal", 0, 1);
   printQuery(fLogFile, fdMem, " memfree", 1, 1);
   printQuery(fLogFile, fdMem, " cached", 3, 1);
   printQuery(fLogFile, fdMem, " swapcached", 4, 1);
   printQuery(fLogFile, fdMem, " active", 5, 1);
   printQuery(fLogFile, fdMem, " inactive", 6, 1);
   fprintf(fLogFile, " [LOADAVG]");
   printQuery(fLogFile, fdLoad, " 1min", 0, 0);
   printQuery(fLogFile, fdLoad, " 5min", 0, 1);
   printQuery(fLogFile, fdLoad, " 15min", 0, 2);
   fprintf(fLogFile, " [DISKSTATS(sda)]");
   printQuery(fLogFile, fdDisk, " totalnoreads", 16, 3);
   printQuery(fLogFile, fdDisk, " totalsectorsread", 16, 5);
   printQuery(fLogFile, fdDisk, " nomsread", 16, 6);
   printQuery(fLogFile, fdDisk, " totalnowrites", 16, 7);
   printQuery(fLogFile, fdDisk, " nosectorswritten", 16, 9);
   printQuery(fLogFile, fdDisk, " nomswritten", 16, 10);
   fprintf(fLogFile, "\n") ;

   return;
}

void closeSysFiles(int fdStat, int fdMem, int fdLoad, int fdDisk) {
   close(fdStat);
   close(fdMem);
   close(fdLoad);
   close(fdDisk);

   return;
}
