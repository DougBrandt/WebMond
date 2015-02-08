
#ifndef __LOG_LIBRARY_H_
#define __LOG_LIBRARY_H_

#include <stdio.h>

#define MAX_TIME_LEN 100
#define CONVERT_SEC_TO_USEC 1000000
#define MAX_USEC_SLEEP 1000000

char *queryFileByLoc(int fd, int row, int col);
void printQuery(FILE *fLogFile, int fdSrc, char *queryField, int row, int col);
char *generateLogTime(char *timeStr);
void longSleep(long sleepTime);

#endif // __LOG_LIBRARY_H_
