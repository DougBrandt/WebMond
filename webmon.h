
#ifndef __WEBMON_THREAD_H_
#define __WEBMON_THREAD_H_

#define WEBMON_THREAD_RUNNING 1
#define WEBMON_THREAD_NOT_RUNNING 0

typedef struct {
   int intervalSec;
   int refreshSec;
   char file[MAX_INPUT_LEN];
} WebmonParams;

void *webmonThread(void *args);

#endif // __WEBMON_THREAD_H_
