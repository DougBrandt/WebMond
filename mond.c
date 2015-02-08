
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "mond.h"
#include "commands.h"
#include "webmon.h"
#include "singlyLinkedList.h"

void commandThread();
void initFileTable();
void initThreadTables();
void destroyFileTable();
void destroyThreadTables();

FileTable fileTable[FILE_TABLE_SIZE];
ThreadTable threadTable[THREAD_TABLE_SIZE];
ThreadTable systemThreadTable;
sem_t availableThreads;
int systemThreadState = SYSTEM_THREAD_NOT_RUNNING;
LinkedList *completedList = NULL;
int webmonActive = WEBMON_THREAD_NOT_RUNNING;


int main(int argc, char *argv[]) {

   if (sem_init(&availableThreads, 0, THREAD_TABLE_SIZE) == -1) {
      perror("sem_init failed");
      exit(-1);
   }

   initFileTable();
   initThreadTables();

   commandThread();

   return 0;
}

void initFileTable() {

   memset(fileTable, 0, sizeof (FileTable) * FILE_TABLE_SIZE);

   int i = 0;
   for (i = 0; i < FILE_TABLE_SIZE; i++) {
      if (pthread_mutex_init(&fileTable[i].mutex, NULL) != 0) {
         perror("pthread_mutex_init failed");
         exit(-1);
      }
      if (sem_init(&(fileTable[i].count), 0, 0) == -1) {
         perror("sem_init failed");
         exit(-1);
      }
   }

   return;
}

void initThreadTables() {

   memset(threadTable, 0, sizeof (ThreadTable) * THREAD_TABLE_SIZE);
   memset(&systemThreadTable, 0, sizeof (ThreadTable));

   // system thread
   if (pthread_mutex_init(&systemThreadTable.mutex, NULL) != 0) {
      perror("pthread_mutex_init failed");
      exit(-1);
   }

   // command threads
   int i = 0;
   for (i = 0; i < THREAD_TABLE_SIZE; i++) {
      if (pthread_mutex_init(&threadTable[i].mutex, NULL) != 0) {
         perror("pthread_mutex_init failed");
         exit(-1);
      }
   }

   return;
}

void commandThread() {
   char *input = NULL, *token = NULL;
   char defaultInterval[MAX_INPUT_LEN] = "1000000";
   char defaultLogFile[MAX_INPUT_LEN] = "logFile.txt";
   char typeFlag = '\0';

   // initialize linked list
   if (InitLL(&completedList) == -1) {
      perror("calloc failed");
      exit(-1);
   }

   printf("=== Welcome to the Mond Logger ===\n");
   printf("Default Interval Time set to: %s\n", defaultInterval);
   printf("Default Log File set to: %s\n", defaultLogFile);

   while (1) {

      typeFlag = '\0';
      free(input);
      input = NULL;

      input = readline("mond: $> ");

      if (input != NULL) {
         add_history(input);
      } else { // input == NULL
         printf("\n");
         continue;
      }

      token = strtok(input, " ");

      if (strncmpSafe("add", token, MAX_INPUT_LEN - 1) == 0) {
         char *type = NULL, *aux = NULL;
         char *interval = defaultInterval, *logFile = defaultLogFile;
         token = strtok(NULL, " ");
         if (strncmpSafe("-s", token, MAX_INPUT_LEN - 1) == 0) {
            type = "-s";
            typeFlag = 's';
            aux = NULL;
            if (systemThreadState == SYSTEM_THREAD_RUNNING) {
               printf("ERROR: system thread already running\n");
               continue;
            }
         } else if (strncmpSafe("-p", token, MAX_INPUT_LEN - 1) == 0) {
            token = strtok(NULL, " ");
            type = "-p";
            if (token != NULL) {
               aux = token;
            } else {
               printf("ERROR: bad input\n");
               continue;
            }
         } else if (strncmpSafe("-e", token, MAX_INPUT_LEN - 1) == 0) {
            token = strtok(NULL, " ");
            type = "-e";
            aux = token;
         } else {
            printf("ERROR: bad input\n");
            continue;
         }

         token = strtok(NULL, " ");
         if (strncmpSafe("-i", token, MAX_INPUT_LEN - 1) == 0) {
            token = strtok(NULL, " ");
            interval = token;

            token = strtok(NULL, " ");
            if (strncmpSafe("-f", token, MAX_INPUT_LEN - 1) == 0) {
               token = strtok(NULL, " ");
               if (token != NULL) {
                  logFile = token;
               } else {
                  printf("ERROR: bad input\n");
                  continue;
               }
            }
         } else if (strncmpSafe("-f", token, MAX_INPUT_LEN - 1) == 0) {
            token = strtok(NULL, " ");
            if (token != NULL) {
               logFile = token;
            } else {
               printf("ERROR: bad input\n");
               continue;
            }
         } else if (token != NULL) {
            printf("ERROR: bad input\n");
            continue;
         }

         int semValue = -1;
         if (sem_getvalue(&availableThreads, &semValue) == -1) {
            perror("sem_getvalue failed");
            exit(-1);
         }

         if (semValue > 0 || typeFlag == 's') {
            // call add functionality
            add(type, aux, interval, logFile);
         } else {
            printf("Maximum number of threads already reached.\n");
            continue;
         }

      } else if (strncmpSafe("set", token, MAX_INPUT_LEN - 1) == 0) {
         token = strtok(NULL, " ");
         if (strncmpSafe("interval", token, MAX_INPUT_LEN - 1) == 0) {
            token = strtok(NULL, " ");
            if (token == NULL) {
               printf("%s is not a valid interval\n", token);
               continue;
            }
            // set default interval
            int intervalTemp = 0;
            errno = 0;
            intervalTemp = strtol(token, NULL, 10);
            if (errno != 0 || intervalTemp <= 0) {
               printf("%s is not a valid interval\n", token);
               continue;
            }

            if (strncpy(defaultInterval, token, MAX_INPUT_LEN - 1) == NULL) {
               perror("strncpy failed");
               exit(-1);
            }
         } else if (strncmpSafe("logfile", token, MAX_INPUT_LEN - 1) == 0) {
            token = strtok(NULL, " ");
            // set default log file
            if (token != NULL) {
               strncpy(defaultLogFile, token, MAX_INPUT_LEN - 1);
            } else {
               printf("ERROR: bad input\n");
            }
         } else {
            printf("ERROR: bad input\n");
         }
      } else if (strncmpSafe("listactive", token, MAX_INPUT_LEN - 1) == 0) {
         listActive();
      } else if (strncmpSafe("listcompleted", token, MAX_INPUT_LEN - 1) == 0) {
         listCompleted();
      } else if (strncmpSafe("remove", token, MAX_INPUT_LEN - 1) == 0) {
         token = strtok(NULL, " ");
         if (strncmpSafe("-s", token, MAX_INPUT_LEN - 1) == 0) {
            pthread_t systemThreadId = -1;

            /*
             *  What threads use this critical section:
             *    Only the command thread uses this critical section.
             *
             *  What shared resources are being protected:
             *    The systemThreadTable is the only resource locked. We lock
             *    the whole line, but are interested in the thread id.
             *
             *  Line justification and performance concerns:
             *    We need to copy the thread id to a local variable in order to
             *    pass it to the removeThread function which actually removes
             *    the thread.  Performance is not a concern here because it is
             *    only a single lined simple assignment.
             *
             *  Mutex vs. semaphore decision:
             *    A mutex was used because we only had resources that were
             *    mutually exclusive.  Either it was in use or it wasn't.
             *
             */

            // lock
            if (pthread_mutex_lock(&(systemThreadTable.mutex)) == -1) {
               perror("pthread_mutex_lock failed");
               exit(-1);
            }

            // critical
            systemThreadId = systemThreadTable.tid;

            // unlock
            if (pthread_mutex_unlock(&(systemThreadTable.mutex)) == -1) {
               perror("pthread_mutex_unlock failed");
               exit(-1);
            }

            removeThread(systemThreadId);

         } else if (strncmpSafe("-t", token, MAX_INPUT_LEN - 1) == 0) {
            token = strtok(NULL, " ");
            errno = 0;
            pthread_t threadId = strtoll(token, NULL, 10);
            if (errno != 0 || threadId == 0) {
               printf("%s is not a valid thread id %lu\n", token, (unsigned long)threadId);
               continue;
            }
            removeThread(threadId);
         } else {
            printf("ERROR: bad input\n");
         }
      } else if (strncmpSafe("kill", token, MAX_INPUT_LEN - 1) == 0) {
         token = strtok(NULL, " ");
         errno = 0;
         pid_t pid = strtol(token, NULL, 10);
         if (errno != 0 || pid == 0) {
            printf("%s is not a valid process id\n", token);
            continue;
         }

         killProcess(pid);
      } else if (strncmpSafe("exit", token, MAX_INPUT_LEN - 1) == 0) {
         char *response = NULL;
         int semValue = -1;
         if (sem_getvalue(&availableThreads, &semValue) == -1) {
            perror("sem_getvalue failed");
         }

         if (semValue != THREAD_TABLE_SIZE || systemThreadState == SYSTEM_THREAD_RUNNING) {
            if ((response = readline(EXIT_PROMPT)) != NULL) {
               token = strtok(response, " ");
               if (strncmpSafe("y", token, MAX_INPUT_LEN - 1) == 0) {
                  exitMond();
                  break;
               }
            }
         } else {
            exitMond();
            break;
         }
      } else if (strncmpSafe("webmon", token, MAX_INPUT_LEN - 1) == 0) {

         if (webmonActive == WEBMON_THREAD_RUNNING) {
            printf("web monitor already running\n");
            continue;
         }

         if ((token = strtok(NULL, " ")) == NULL) {
            printf("incorrect webmon parameters\n");
            continue;
         }
         errno = 0;
         int webInterval = strtol(token, NULL, 10);
         if (errno != 0 || webInterval <= 0) {
            printf("%s is not a valid process id\n", token);
            continue;
         }

         if ((token = strtok(NULL, " ")) == NULL) {
            printf("incorrect webmon parameters\n");
            continue;
         }
         errno = 0;
         int refreshRate = strtol(token, NULL, 10);
         if (errno != 0 || refreshRate <= 0) {
            printf("%s is not a valid process id\n", token);
            continue;
         }

         if ((token = strtok(NULL, " ")) == NULL) {
            printf("incorrect webmon parameters\n");
            continue;
         }

         startWebmon(webInterval, refreshRate, token);
      } else {
         if (token != NULL) {
            printf("%s: command not found\n", token);
         }
      }

   }

   // clean up Linked List
   LLClear(completedList);
   DestroyLL(&completedList);
   destroyFileTable();
   destroyThreadTables();

   return;
}

int strncmpSafe(const char *s1, const char *s2, size_t n) {

   if (s1 == NULL || s2 == NULL) {
      return -2;
   }

   return strncmp(s1, s2, n);
}

void destroyFileTable() {

   int i = 0;
   for (i = 0; i < FILE_TABLE_SIZE; i++) {
      if (pthread_mutex_destroy(&fileTable[i].mutex) != 0) {
         perror("pthread_mutex_destroy failed");
         exit(-1);
      }
      if (sem_destroy(&(fileTable[i].count)) == -1) {
         perror("sem_destroy failed");
         exit(-1);
      }
   }

   return;
}

void destroyThreadTables() {

   // system thread
   if (pthread_mutex_destroy(&systemThreadTable.mutex) != 0) {
      perror("pthread_mutex_destroy failed");
      exit(-1);
   }

   // command threads
   int i = 0;
   for (i = 0; i < THREAD_TABLE_SIZE; i++) {
      if (pthread_mutex_destroy(&threadTable[i].mutex) != 0) {
         perror("pthread_mutex_destroy failed");
         exit(-1);
      }
   }

   return;
}

