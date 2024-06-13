#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Process Control Block (PCB) structure
typedef struct pcb {
    int id;
    char name[20];
    int type; // 0: Real-Time Process (RTP), 1: Time-Sliced Process (TCP)
    int state; // 0: READY, 1: RUNNING, 2: BLOCKED, 3: DELAYED
    int registers[4]; // 4 registers each with 8 bits
    int semaphore_id;
    struct pcb* next;
} pcb_t;

// PCB Queue structure
typedef struct pcbq {
    pcb_t* front;
    pcb_t* rear;
    int size;
} pcbq_t;

// Semaphore structure
typedef struct semaphore {
    int state; // 0: available, 1: unavailable
    int value;
    pcbq_t pcbq;
} semaphore_t;

// Real-Time Queue (RTQ) and Time-Slice Queue (TSQ)
pcbq_t rtq, tsq, dq;

// Scheduler (SCH) function
void* scheduler(void* arg) {
    while (1) {
        // Check RTQ first
        if (rtq.front!= NULL) {
            pcb_t* pcb = rtq.front;
            rtq.front = pcb->next;
            if (rtq.front == NULL) rtq.rear = NULL;
            rtq.size--;
            pcb->state = 1; // RUNNING
            sem_post(&semaphores[pcb->semaphore_id]);
        } else {
            // Check TSQ
            if (tsq.front!= NULL) {
                pcb_t* pcb = tsq.front;
                tsq.front = pcb->next;
                if (tsq.front == NULL) tsq.rear = NULL;
                tsq.size--;
                pcb->state = 1; // RUNNING
                sem_post(&semaphores[pcb->semaphore_id]);
                // Add to Delta Queue (DQ) for time-slicing
                pcb->state = 3; // DELAYED
                enqueue(&dq, pcb);
            }
        }
        usleep(1000); // 1ms delay
    }
    return NULL;
}

// Enqueue function for PCB Queue
void enqueue(pcbq_t* pcbq, pcb_t* pcb) {
    if (pcbq->rear == NULL) {
        pcbq->front = pcbq->rear = pcb;
    } else {
        pcbq->rear->next = pcb;
        pcbq->rear = pcb;
    }
    pcbq->size++;
}

// Dequeue function for PCB Queue
pcb_t* dequeue(pcbq_t* pcbq) {
    if (pcbq->front == NULL) return NULL;
    pcb_t* pcb = pcbq->front;
    pcbq->front = pcb->next;
    if (pcbq->front == NULL) pcbq->rear = NULL;
    pcbq->size--;
    return pcb;
}

// Insert function for PCB Queue
void insert(pcbq_t* pcbq, pcb_t* pcb) {
    if (pcbq->front == NULL || pcb->type < pcbq->front->type) {
        pcb->next = pcbq->front;
        pcbq->front = pcb;
    } else {
        pcb_t* temp = pcbq->front;
        while (temp->next!= NULL && temp->next->type < pcb->type) {
            temp = temp->next;
        }
        pcb->next = temp->next;
        temp->next = pcb;
    }
    pcbq->size++;
}

// Take function for PCB Queue
pcb_t* take(pcbq_t* pcbq) {
    pcb_t* pcb = pcbq->front;
    pcbq->front = pcb->next;
    if (pcbq->front == NULL) pcbq->rear = NULL;
    pcbq->size--;
    return pcb;
}

// Make process function
pcb_t* make_proc(int id, char* name, int type) {
    pcb_t* pcb = (pcb_t*)malloc(sizeof(pcb_t));
    pcb->id = id;
    strcpy(pcb->name, name);
    pcb->type = type;
    pcb->state = 0; // READY
    pcb->semaphore_id = -1;
    return pcb;
}

// Make ready function
void make_ready(pcb_t* pcb) {
    pcb->state = 0; // READY
    if (pcb->type == 0) { // RTP
        insert(&rtq, pcb);
    } else { // TCP
        insert(&tsq, pcb);
    }
}

// Block function
void block(pcb_t* pcb) {
    pcb->state = 2; // BLOCKED
}

// Make semaphore function
semaphore_t* make_sem(int value) {
    semaphore_t* sem = (semaphore_t*)malloc(sizeof(semaphore_t));
    sem->state = 0; // available
    sem->value = value;
    sem->pcbq.front = sem->pcbq.rear = NULL;
    sem->pcbq.size = 0;
    return sem;
}

// Wait semaphore function
void wait_sem(semaphore_t* sem) {
    sem->state = 1; // unavailable
    sem->value--;
    pcb_t* pcb = (pcb_t*)pthread_self();
    pcb->state = 2; // BLOCKED
    enqueue(&sem->pcbq, pcb);
}

// Signal semaphore function
void signal_sem(semaphore_t* sem) {
    sem->state = 0; // available
    sem->value++;
    pcb_t* pcb = dequeue(&sem->pcbq);
    if (pcb!= NULL) {
        pcb->state = 0; // READY
        make_ready(pcb);
    }
}

// ALU function
void alu(int op, int reg1, int reg2, int* result, int* co, int* z) {
    int r1 = pcb->registers[reg1];
    int r2 = pcb->registers[reg2];
    switch (op) {
        case 0: // ADD
            *result = r1 + r2;
            *co = (*result > 255);
            *z = (*result == 0);
            break;
        case 1: // SUB
            *result = r1 - r2;
            *co = (*result < 0);
            *z = (*result == 0);
            break;
        case 2: // OR
            *result = r1 | r2;
            *co = 0;
            *z = (*result == 0);
            break;
    }
}

// Producer process
void* producer(void* arg) {
    pcb_t* pcb = (pcb_t*)arg;
    FILE* fp = fopen("rtk.c", "r");
    char c;
    while ((c = fgetc(fp))!= EOF) {
        wait_sem(&semaphores[0]);
        pcb->registers[0] = c;
        signal_sem(&semaphores[1]);
    }
    fclose(fp);
    return NULL;
}

// Consumer process
void* consumer(void* arg) {
    pcb_t* pcb = (pcb_t*)arg;
    FILE* fp = fopen("rtk2.c", "w");
    char c;
    while (1) {
        wait_sem(&semaphores[1]);
        c = pcb->registers[0];
        fputc(c, fp);
        signal_sem(&semaphores[0]);
    }
    fclose(fp);
    return NULL;
}

int main() {
    // Initialize RTK
    rtq.front = rtq.rear = NULL;
    rtq.size = 0;
    tsq.front = tsq.rear = NULL;
    tsq.size = 0;
    dq.front = dq.rear = NULL;
    dq.size = 0;

    // Create semaphores
    semaphore_t* semaphores[2];
    semaphores[0] = make_sem(1);
    semaphores[1] = make_sem(0);

    // Create processes
    pcb_t* producer_pcb = make_proc(1, "Producer", 1);
    pcb_t* consumer_pcb = make_proc(2, "Consumer", 1);

    // Create threads
    pthread_t producer_thread, consumer_thread;
    pthread_create(&producer_thread, NULL, producer, producer_pcb);
    pthread_create(&consumer_thread, NULL, consumer, consumer_pcb);

    // Start scheduler
    pthread_t scheduler_thread;
    pthread_create(&scheduler_thread, NULL, scheduler, NULL);

    // Wait for threads to finish
    pthread_join(producer_thread, NULL);
    pthread_join(consumer_thread, NULL);
    pthread_join(scheduler_thread, NULL);

    return 0;
}