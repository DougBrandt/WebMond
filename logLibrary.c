
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "logLibrary.h"


/*
 * Note: row and col are base 0 for the first line, first item
 *
 * Return: A (char *) is returned and the space needs to be freed
 */
char *queryFileByLoc(int fd, int row, int col) {
   int curRow = 0;
   int curCol = 0;
   char *linePtr = NULL, *tokenPtr = NULL, *retPtr = NULL;
   size_t nSize = -1;
   FILE *file = NULL;

   if (lseek(fd, SEEK_SET, 0) == -1) {
      perror("lseek failed");
      exit(-1);
   }

   if ((file = fdopen(fd, "r")) == NULL) {
      perror("fdopen failed");
      exit(-1);
   }

   // move to the correct row
   while (curRow <= row) {
      if (getline(&linePtr, &nSize, file) == -1) {
         if (feof(file)) {
            if (linePtr != NULL) {
               free(linePtr);
               linePtr = NULL;
            }
            return NULL;
         }
         perror("getline failed");
         exit(-1);
      }
      curRow++;
   }

   // move to the correct column
   while (curCol <= col) {
      if ((tokenPtr = strtok((curCol == 0) ? linePtr : NULL, " ")) == NULL) {
         if (linePtr != NULL) {
            free(linePtr);
            linePtr = NULL;
         }
         return NULL;
      }

      curCol++;
   }

   if ((retPtr = (char *)calloc(1, (sizeof(char) * strlen(tokenPtr)) + 1)) == NULL) {
      perror("calloc failed");
      exit(-1);
   }

   if (tokenPtr != NULL) {
      strcpy(retPtr, tokenPtr);
   }

   if (linePtr != NULL) {
      free(linePtr);
      linePtr = NULL;
   }

   // remove ending '\n' for only end of line cases
   if (retPtr[strlen(retPtr) - 1] == '\n') {
      retPtr[strlen(retPtr) - 1] = '\0';
   }

   return retPtr;
}

void printQuery(FILE *fLogFile, int fdSrc, char *queryField, int row, int col) {
   char *retQuery = queryFileByLoc(fdSrc, row, col);
   fprintf(fLogFile, "%s %s", queryField, retQuery);
   free(retQuery);
   return;
}

char *generateLogTime(char *timeStr) {
   time_t timep;
   struct tm *tm;

   time(&timep);
   tm = localtime(&timep);
   strftime(timeStr, MAX_TIME_LEN - 1, "%a %b %d %T %Y", tm);

   return timeStr;
}

/*
 * param: sleepTime in usec
 */
void longSleep(long sleepTime) {
   if (sleepTime <= 0) {
      return;
   }

   if (sleepTime < MAX_USEC_SLEEP) { // small enough
      if (usleep(sleepTime) == -1) {
         if (errno != EINTR) {
            perror("usleep failed");
            exit(-1);
         }
      }
   } else { // too big
      sleep(sleepTime / MAX_USEC_SLEEP);
      if (usleep(sleepTime % MAX_USEC_SLEEP) == -1) {
         if (errno != EINTR) {
            perror("usleep failed");
            exit(-1);
         }
      }
   }

   return;
}
