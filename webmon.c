
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "mond.h"
#include "webmon.h"
#include "logLibrary.h"
#include "singlyLinkedList.h"

#define GRAPH_HISTORY_LEN 10

void webmonHeader(FILE *file, int refreshSec, LinkedList *loadList);
void webmonSettings(FILE *file, int intervalSec, int refreshSec);
void webmonActiveThreads(FILE *file);
void webmonCompletedThreads(FILE *file);
void webmonFileTable(FILE *file);
void webmonGraph(FILE *file);
void webmonFooter(FILE *file);
void printRunningWebmon(FILE *file, ThreadTable *line);
void updateLoadList(LinkedList *loadList);
char *generateWebmonTime(time_t *timep, char *timeStr);

extern FileTable fileTable[FILE_TABLE_SIZE];
extern ThreadTable threadTable[THREAD_TABLE_SIZE];
extern ThreadTable systemThreadTable;
extern int systemThreadState;
extern LinkedList *completedList;

void *webmonThread(void *args) {
   WebmonParams webmonParams;
   FILE *file = NULL;
   LinkedList *loadList = NULL;

   memcpy(&webmonParams, args, sizeof (WebmonParams));
   free(args);
   args = NULL;

   InitLL(&loadList);

   while (1) {
      if ((file = fopen(webmonParams.file, "w")) == NULL) {
         perror("fopen failed");
         exit(-1);
      }

      updateLoadList(loadList);

      webmonHeader(file, webmonParams.refreshSec, loadList);
      webmonSettings(file, webmonParams.intervalSec, webmonParams.refreshSec);
      webmonActiveThreads(file);
      webmonCompletedThreads(file);
      webmonFileTable(file);
      webmonGraph(file);
      webmonFooter(file);

      if (fclose(file) != 0) {
         perror("fclose failed");
         exit(-1);
      }

      if (sleep(webmonParams.intervalSec) == -1) {
         if (errno != EINTR) {
            perror("sleep failed");
            exit(-1);
         }
      }
   }

   DestroyLL(&loadList);

   return NULL;
}


void updateLoadList(LinkedList *loadList) {
   int fd = -1;
   char *line = NULL, *load_1 = NULL, *load_5 = NULL, *load_15 = NULL;

   if ((fd = open("/proc/loadavg", O_RDONLY)) == -1) {
      perror("open failed");
      exit(-1);
   }

   load_1 = queryFileByLoc(fd, 0, 0);
   load_5 = queryFileByLoc(fd, 0, 1);
   load_15 = queryFileByLoc(fd, 0, 2);

   if ((line = calloc(1, sizeof (char) * MAX_INPUT_LEN)) == NULL) {
      perror("calloc failed");
      exit(-1);
   }

   snprintf(line, MAX_TIME_LEN - 1, ", %s, %s, %s]", load_1, load_5, load_15);

   if (LLSize(loadList) <= GRAPH_HISTORY_LEN) {
      LLInsertTail(loadList, (void *)line);
   }
   else {
      LLRemoveHead(loadList);
      LLInsertTail(loadList, (void *)line);
   }

   free(load_1);
   free(load_5);
   free(load_15);

   close(fd);

   return;
}

void webmonHeader(FILE *file, int refreshSec, LinkedList *loadList) {

   fprintf(file, "\n\
<html>\n\
   <head>\n\
      <title>System Monitor - Web Extension</title>\n\
      <meta http-equiv=\"refresh\" content=\"%d\">\n\
      <script type=\"text/javascript\" src=\"https://www.google.com/jsapi\"></script>\n\
      <script type=\"text/javascript\">\n\
      google.load(\"visualization\", \"1\", {packages:[\"corechart\"]});\n\
      google.setOnLoadCallback(drawChart);\n\
      function drawChart() {\n\
      var data = google.visualization.arrayToDataTable([\n\
         ['Data Point', '1 Minute', '5 Minute', '15 Minute'],\n\
      ", refreshSec);

   int i = 0;
   for (i = 0; i < LLSize(loadList); i++) {
      fprintf(file, "\
         ['%d' %s,\n\
         ", i, (char *)LLGet(loadList, i));
   }

   fprintf(file, "\n\
         ]);\n\
\n\
      var options = {\n\
      title: 'Computer Load Averages'\n\
      };\n\
      var chart = new google.visualization.LineChart(document.getElementById('chart_div'));\n\
      chart.draw(data, options);\n\
      }\n\
      </script>\n\
\n\
   </head>\n\
   <body>\n\
      <h2>\n\
         System Monitor - Web Extension\n\
      </h2>\n\
      <p>\n\
         By Douglas Brandt & Kerry S.\n\
      </p>\n\
      ");

   return;
}

void webmonSettings(FILE *file, int intervalSec, int refreshSec) {
   fprintf(file, "\n\
\
      <h3>\n\
         Settings\n\
      </h3>\n\
      <ul>\n\
         <li>\n\
            webmon refresh rate = %d seconds\n\
         </li>\n\
         <li>\n\
            html refresh rate = %d seconds\n\
         </li>\n\
      </ul>\n\
               ", intervalSec, refreshSec);

   return;
}

void webmonActiveThreads(FILE *file) {

   fprintf(file, "\n\
\
      <h3>\n\
         Active Threads\n\
      </h3>\n\
      <table border=\"1\", cellpadding=\"2\">\n\
         <tr>\n\
            <td>Thread Id</td>\n\
            <td>Process Id</td>\n\
            <td>Start Time</td>\n\
            <td>Interval (&#956sec)</td>\n\
            <td>Log File</td>\n\
         </tr>\n\
               ");

   if (systemThreadState == SYSTEM_THREAD_RUNNING) {
      printRunningWebmon(file, &systemThreadTable);
   }

   int i = 0;
   for (i = 0; i < THREAD_TABLE_SIZE; i++) {
      printRunningWebmon(file, &(threadTable[i]));
   }

   fprintf(file, "\n\
      </table>");

   return;
}

void webmonCompletedThreads(FILE *file) {
   char pidStr[MAX_INPUT_LEN] = "";
   char startTimeStr[MAX_INPUT_LEN] = "";
   char endTimeStr[MAX_INPUT_LEN] = "";
   int i = 0;

   fprintf(file, "\n\
\
      <h3>\n\
         Completed Threads\n\
      </h3>\n\
      <table border=\"1\", cellpadding=\"2\">\n\
         <tr>\n\
            <td>Thread Id</td>\n\
            <td>Process Id</td>\n\
            <td>Start Time</td>\n\
            <td>End Time</td>\n\
            <td>End Status</td>\n\
            <td>Interval (&#956sec)</td>\n\
            <td>Log File</td>\n\
         </tr>\n\
               ");

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

      fprintf(file, "\n\
\
         <tr>\n\
            <td>%11lu</td>\n\
            <td>%10s</td>\n\
            <td>%10s</td>\n\
            <td>%10s</td>\n\
            <td>%10s</td>\n\
            <td>%10lu</td>\n\
            <td>%-1s</td>\n\
         </tr>\n",
            (unsigned long)line->tid,
            (line->pid == -1) ? "system" : pidStr,
            generateWebmonTime(&(line->startTime), startTimeStr),
            generateWebmonTime(&(line->endTime), endTimeStr),
            (line->endStatus == KILLED) ? "killed" : (line->endStatus == STOPPED) ? "stopped" : "exited",
            line->interval,
            line->fileName);
   }

   // unlock
   if (pthread_mutex_unlock(&(completedList->mutex)) != 0) {
      perror("pthread_mutex_unlock failed");
      exit(-1);
   }

   fprintf(file, "\n\
      </table>");

   return;
}

void webmonFileTable(FILE *file) {
   int i = 0;
   int value = 0;

   fprintf(file, "\n\
\
      <h3>\n\
         File Table\n\
      </h3>\n\
      <table border=\"1\", cellpadding=\"2\">\n\
         <tr>\n\
            <td>Device Id</td>\n\
            <td>Inode Id</td>\n\
            <td>Count</td>\n\
         </tr>\n\
               ");

   for (i = 0; i < FILE_TABLE_SIZE; i++) {
      // lock
      if (pthread_mutex_lock(&(fileTable[i].mutex)) != 0) {
         perror("pthread_mutex_lock failed");
         exit(-1);
      }

      // critical section
      if (fileTable[i].dev != 0 && fileTable[i].inode != 0) {

         if (sem_getvalue(&(fileTable[i].count), &value) == -1) {
            perror("sem_getvalue failed");
            exit(-1);
         }

         fprintf(file, "\n\
         <tr>\n\
            <td>%10lu</td>\n\
            <td>%10lu</td>\n\
            <td>%10d</td>\n\
         </tr>\n",
                  (unsigned long) fileTable[i].dev,
                  (unsigned long)fileTable[i].inode,
                  value);

      }

      // unlock
      if (pthread_mutex_unlock(&(fileTable[i].mutex)) != 0) {
         perror("pthread_mutex_unlock failed");
         exit(-1);
      }
   }

   fprintf(file, "\n\
      </table>");

   return;
}

void webmonGraph(FILE *file) {
   fprintf(file, "\n\
         <h3> Utilization Graph</h3>");
   fprintf(file, "\n\
         <div id=\"chart_div\" style=\"width: 900px; height: 500px;\"></div>");

   return;
}

void webmonFooter(FILE *file) {
   fprintf(file, "\n\
   </body>\n\
</html>\n\
         ");

   return;
}

void printRunningWebmon(FILE *file, ThreadTable *line) {
   char pidStr[MAX_INPUT_LEN] = "";
   char timeStr[MAX_INPUT_LEN] = "";

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

      fprintf(file, "\n\
         <tr>\n\
            <td>%11lu</td>\n\
            <td>%10s</td>\n\
            <td>%10s</td>\n\
            <td>%10lu</td>\n\
            <td>%-1s</td>\n\
         </tr>\n",
            (unsigned long)line->tid,
            (line->pid == -1) ? "system" : pidStr,
            generateWebmonTime(&(line->startTime), timeStr),
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

char *generateWebmonTime(time_t *timep, char *timeStr) {
   struct tm *tm;

   memset(timeStr, 0, sizeof (char) * MAX_INPUT_LEN);

   tm = localtime(timep);
   strftime(timeStr, MAX_TIME_LEN - 1, "%a %b %d %T %Y", tm);

   return timeStr;
}
