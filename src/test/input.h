#ifndef INPUT_H
#define INPUT_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>      // _beginthreadex
#define THREAD_RET    unsigned int __stdcall
#define THREAD_FUNC   unsigned int __stdcall
typedef HANDLE        THREAD_HANDLE;
typedef CRITICAL_SECTION  MUTEX_T;
#else
#include <pthread.h>
#include <unistd.h>       // sleep
#define THREAD_RET    void*
#define THREAD_FUNC   void*
typedef pthread_t     THREAD_HANDLE;
typedef pthread_mutex_t   MUTEX_T;
#endif

static char input_buffer[512];
static int  got_input = 0;
static int  keep_running = 1;
static MUTEX_T mutex;

THREAD_RET input_thread_func(void* arg) {
    (void)arg;
    char line[256];

    while (keep_running) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

#ifdef _WIN32
        EnterCriticalSection(&mutex);
#else
        pthread_mutex_lock(&mutex);
#endif
        strncpy(input_buffer, line, sizeof(input_buffer) - 1);
        input_buffer[sizeof(input_buffer) - 1] = '\0';
        got_input = 1;
#ifdef _WIN32
        LeaveCriticalSection(&mutex);
#else
        pthread_mutex_unlock(&mutex);
#endif

        if (strcmp(line, "exit") == 0) {
            keep_running = 0;
        }
    }

#ifdef _WIN32
    _endthread();
#else
    return NULL;
#endif
}

void mutex_init(MUTEX_T* m) {
#ifdef _WIN32
    InitializeCriticalSection(m);
#else
    pthread_mutex_init(m, NULL);
#endif
}

void mutex_destroy(MUTEX_T* m) {
#ifdef _WIN32
    DeleteCriticalSection(m);
#else
    pthread_mutex_destroy(m);
#endif
}

void mutex_lock(MUTEX_T* m) {
#ifdef _WIN32
    EnterCriticalSection(m);
#else
    pthread_mutex_lock(m);
#endif
}

void mutex_unlock(MUTEX_T* m) {
#ifdef _WIN32
    LeaveCriticalSection(m);
#else
    pthread_mutex_unlock(m);
#endif
}

THREAD_HANDLE create_input_thread() {
#ifdef _WIN32
    unsigned int tid;
    return (THREAD_HANDLE)_beginthreadex(NULL, 0, input_thread_func, NULL, 0, &tid);
#else
    pthread_t tid;
    pthread_create(&tid, NULL, input_thread_func, NULL);
    return tid;
#endif
}

void join_thread(THREAD_HANDLE t) {
#ifdef _WIN32
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
#else
    pthread_join(t, NULL);
#endif
}

#endif