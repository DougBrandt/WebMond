
#ifndef __MOND_H_
#define __MOND_H_

#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_INPUT_LEN 256
#define FILE_TABLE_SIZE 11
#define THREAD_TABLE_SIZE 10
#define SYSTEM_THREAD_ID -1
#define EXIT_PROMPT "You still have threads actively monitoring. Do you really want to exit? (y/n)"

#define SYSTEM_THREAD_RUNNING 1
#define SYSTEM_THREAD_NOT_RUNNING 0

typedef enum {
   RUNNING = 0,
   KILLED = 1,
   STOPPED = 2,
   EXITED = 3,
} TerminationStatus;

typedef struct {
   pthread_mutex_t mutex;
   sem_t count;
   FILE *filep;
   dev_t dev;
   ino_t inode;
} FileTable;

typedef struct {
   pthread_mutex_t mutex;
   pthread_t tid;
   pid_t pid;
   FileTable *fTable;
   int isChild;

   char fileName[MAX_INPUT_LEN];
   unsigned long interval;
   time_t startTime;
   time_t endTime;
   TerminationStatus endStatus;
} ThreadTable;


int strncmpSafe(const char *s1, const char *s2, size_t n);


#endif //__MOND_H_

