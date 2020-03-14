#include "memhub.h"
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#define SEM_NAME "/memhub"
#define SEM_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)
#define SEM_INIT 1

static sem_t *semaphore = NULL;
static bool busy = false;

int memhub_open(memsvc_handle_t *handle) {
    if (semaphore == NULL) {
        semaphore = sem_open(SEM_NAME, O_CREAT, SEM_PERMS, SEM_INIT);
        int semval = 0;
        sem_getvalue(semaphore, &semval);
        if (semval > 1) {
            printf("Invalid semaphore value = %d. Probably it was messed up by a dying process. Please clean up this semaphore (you can just delete /dev/shm/sem.memhub, I think)\n", semval);
            exit(1);
        }
        printf("\nMemhub initialized a semaphore. Current semaphore value = %d\n", semval);
    }
    if (semaphore == SEM_FAILED) {
        perror("sem_open(3) error");
        exit(1);
    }
   
    // handle all signals in attempt to undo an active semaphore if the process is killed in the middle of a transaction.. 
    signal(SIGABRT, die);
    signal(SIGFPE, die);
    signal(SIGILL, die);
    signal(SIGINT, die);
    signal(SIGSEGV, die);
    signal(SIGTERM, die);

    return memsvc_open(handle);
}

int memhub_close(memsvc_handle_t *handle) {
    sem_close(semaphore);
    return memsvc_close(handle);
}

int memhub_read(memsvc_handle_t handle, uint32_t addr, uint32_t words, uint32_t *data) {
    sem_wait(semaphore);
    busy = true;
    int ret = memsvc_read(handle, addr, words, data);
    sem_post(semaphore);
    busy = false;
    return ret;
}

int memhub_write(memsvc_handle_t handle, uint32_t addr, uint32_t words, const uint32_t *data) {
    sem_wait(semaphore);
    busy = true;
    int ret = memsvc_write(handle, addr, words, data);
    sem_post(semaphore);
    busy = false;
    return ret;
}

void die(int signo) {
    int semval = 0;
    sem_getvalue(semaphore, &semval);
    
    if (busy && (semval == 0)) {
        fprintf(stderr,"\n[!] Application is dying, trying to undo an active semaphore..\n");
        sem_post(semaphore);
    }
    fprintf(stderr,"\n[!] Application was killed or died with signal %d (semaphore value at the time of the kill = %d)...\n", signo, semval);
    exit(1);
}

