
#ifndef __COMMANDS_H_
#define __COMMANDS_H_

#include <pthread.h>

void startWebmon(int intervalSec, int refreshSec, char *file);

void add(char *type, char *aux, char *interval, char *logFile);
void listActive();
void listCompleted();
void removeThread(pthread_t tid);
void killProcess(pid_t pid);
void exitMond();

#endif // __COMMANDS_H_
