#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <unistd.h>

#define MAX_PROCESSES 10
#define REGISTER_COUNT 4

typedef enum { READY, RUNNING, BLOCKED, DELAYED } state_t;
typedef enum { RTP, TCP } type_t;

typedef struct {
    int id;
    char name[20];
    type_t type;
    state_t state;
    uint8_t registers[REGISTER_COUNT];
    int semaphore_id;
    pthread_t thread;
} pcb_t;

typedef struct {
    pcb_t* processes[MAX_PROCESSES];
    int front, rear, size;
    state_t state;
} pcb_queue_t;

pcb_queue_t RTQ = { .front = 0, .rear = 0, .size = 0, .state = READY };
pcb_queue_t TSQ = { .front = 0, .rear = 0, .size = 0, .state = READY };

void enqueue(pcb_queue_t* queue, pcb_t* pcb) {
    if (queue->size == MAX_PROCESSES) return;
    queue->processes[queue->rear] = pcb;
    queue->rear = (queue->rear + 1) % MAX_PROCESSES;
    queue->size++;
}

pcb_t* dequeue(pcb_queue_t* queue) {
    if (queue->size == 0) return NULL;
    pcb_t* pcb = queue->processes[queue->front];
    queue->front = (queue->front + 1) % MAX_PROCESSES;
    queue->size--;
    return pcb;
}
typedef struct {
    sem_t semaphore;
    int value;
    pcb_queue_t waiting_queue;
} semaphore_t;

semaphore_t semaphores[MAX_PROCESSES];
int semaphore_count = 0;

int make_sem(int initial_value) {
    if (semaphore_count >= MAX_PROCESSES) return -1;
    semaphores[semaphore_count].value = initial_value;
    sem_init(&semaphores[semaphore_count].semaphore, 0, initial_value);
    semaphore_count++;
    return semaphore_count - 1;
}

void wait_sem(int sem_id) {
    sem_wait(&semaphores[sem_id].semaphore);
}

void signal_sem(int sem_id) {
    sem_post(&semaphores[sem_id].semaphore);
}
pcb_t* create_process(int id, const char* name, type_t type, void* (*func)(void*)) {
    pcb_t* pcb = (pcb_t*)malloc(sizeof(pcb_t));
    pcb->id = id;
    strcpy(pcb->name, name);
    pcb->type = type;
    pcb->state = READY;
    memset(pcb->registers, 0, sizeof(pcb->registers));
    pcb->semaphore_id = make_sem(0);
    pthread_create(&pcb->thread, NULL, func, (void*)pcb);
    return pcb;
}

void make_ready(pcb_t* pcb) {
    pcb->state = READY;
    if (pcb->type == RTP) {
        enqueue(&RTQ, pcb);
    } else {
        enqueue(&TSQ, pcb);
    }
}

void block_process(pcb_t* pcb) {
    pcb->state = BLOCKED;
}
void* scheduler(void* arg) {
    while (1) {
        pcb_t* next_process = dequeue(&RTQ);
        if (!next_process) {
            next_process = dequeue(&TSQ);
        }

        if (next_process) {
            next_process->state = RUNNING;
            signal_sem(next_process->semaphore_id);
            pthread_join(next_process->thread, NULL);
        }
    }
}
char shared;
sem_t s1, s2;

void* producer(void* arg) {
    pcb_t* pcb = (pcb_t*)arg;
    FILE* file = fopen("rtk.c", "r");
    if (!file) {
        perror("Failed to open rtk.c");
        pthread_exit(NULL);
    }

    while (!feof(file)) {
        wait_sem(pcb->semaphore_id);
        shared = fgetc(file);
        signal_sem(semaphores[s2].semaphore);
        if (feof(file)) break;
    }
    fclose(file);
    pthread_exit(NULL);
}

void* consumer(void* arg) {
    pcb_t* pcb = (pcb_t*)arg;
    FILE* file = fopen("rtk2.c", "w");
    if (!file) {
        perror("Failed to open rtk2.c");
        pthread_exit(NULL);
    }

    while (1) {
        wait_sem(pcb->semaphore_id);
        if (shared == 0) break;
        fputc(shared, file);
        signal_sem(semaphores[s1].semaphore);
    }
    fclose(file);
    pthread_exit(NULL);
}
int main() {
    sem_init(&s1, 0, 1);
    sem_init(&s2, 0, 0);

    pcb_t* producer_pcb = create_process(1, "Producer", RTP, producer);
    pcb_t* consumer_pcb = create_process(2, "Consumer", RTP, consumer);

    make_ready(producer_pcb);
    make_ready(consumer_pcb);

    pthread_t scheduler_thread;
    pthread_create(&scheduler_thread, NULL, scheduler, NULL);

    pthread_join(scheduler_thread, NULL);

    sem_destroy(&s1);
    sem_destroy(&s2);

    return 0;
}
