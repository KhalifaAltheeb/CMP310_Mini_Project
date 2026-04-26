#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

#define ITEMS_PER_PRODUCER 20
#define NORMAL_PRIORITY 0
#define URGENT_PRIORITY 1
#define POISON_VALUE -1

typedef struct {
    int value;
    int producer_id;
    int item_number;
    int priority;      // 0 = normal, 1 = urgent
    int is_poison;     // 0 = real item, 1 = poison pill
    struct timespec enqueue_time;
} Item;

typedef struct {
    Item *urgent_queue;
    Item *normal_queue;

    int capacity;
    int urgent_head, urgent_tail, urgent_count;
    int normal_head, normal_tail, normal_count;
    int total_count;

    sem_t empty_slots;        // free buffer slots
    sem_t filled_slots;       // filled buffer slots
    pthread_mutex_t mutex;    // protects buffer access
} BoundedBuffer;

int num_producers;
int num_consumers;
int buffer_size;

BoundedBuffer shared_buffer;

pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
int real_items_produced = 0;
int real_items_consumed = 0;
int urgent_items_consumed = 0;
int normal_items_consumed = 0;
double total_latency_seconds = 0.0;

struct timespec program_start_time;
struct timespec program_end_time;

double time_difference_seconds(struct timespec start, struct timespec end) {
    double seconds = (double)(end.tv_sec - start.tv_sec);
    double nanoseconds = (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
    return seconds + nanoseconds;
}

int parse_positive_int(const char *text, const char *name, int *result) {
    char *endptr;
    long value;

    errno = 0;
    value = strtol(text, &endptr, 10);

    if (errno != 0 || *endptr != '\0' || value <= 0 || value > INT_MAX) {
        printf("Error: %s must be a positive integer.\n", name);
        return 0;
    }

    *result = (int)value;
    return 1;
}

void initialize_buffer(BoundedBuffer *buffer, int capacity) {
    buffer->capacity = capacity;

    buffer->urgent_head = 0;
    buffer->urgent_tail = 0;
    buffer->urgent_count = 0;

    buffer->normal_head = 0;
    buffer->normal_tail = 0;
    buffer->normal_count = 0;

    buffer->total_count = 0;

    buffer->urgent_queue = malloc(sizeof(Item) * capacity);
    buffer->normal_queue = malloc(sizeof(Item) * capacity);

    if (buffer->urgent_queue == NULL || buffer->normal_queue == NULL) {
        printf("Error: failed to allocate buffer memory.\n");
        exit(1);
    }

    if (sem_init(&buffer->empty_slots, 0, capacity) != 0) {
        perror("sem_init empty_slots failed");
        exit(1);
    }

    if (sem_init(&buffer->filled_slots, 0, 0) != 0) {
        perror("sem_init filled_slots failed");
        exit(1);
    }

    if (pthread_mutex_init(&buffer->mutex, NULL) != 0) {
        printf("Error: mutex initialization failed.\n");
        exit(1);
    }
}

void destroy_buffer(BoundedBuffer *buffer) {
    sem_destroy(&buffer->empty_slots);
    sem_destroy(&buffer->filled_slots);
    pthread_mutex_destroy(&buffer->mutex);
    free(buffer->urgent_queue);
    free(buffer->normal_queue);
}

void enqueue_item(BoundedBuffer *buffer, Item item) {
    int empty_value;

    sem_getvalue(&buffer->empty_slots, &empty_value);
    if (empty_value == 0) {
        printf("[Info] Buffer full. Producer/main is blocking...\n");
    }

    sem_wait(&buffer->empty_slots);

    pthread_mutex_lock(&buffer->mutex);

    clock_gettime(CLOCK_MONOTONIC, &item.enqueue_time);

    if (item.priority == URGENT_PRIORITY && item.is_poison == 0) {
        buffer->urgent_queue[buffer->urgent_tail] = item;
        buffer->urgent_tail = (buffer->urgent_tail + 1) % buffer->capacity;
        buffer->urgent_count++;
    } else {
        buffer->normal_queue[buffer->normal_tail] = item;
        buffer->normal_tail = (buffer->normal_tail + 1) % buffer->capacity;
        buffer->normal_count++;
    }

    buffer->total_count++;

    pthread_mutex_unlock(&buffer->mutex);

    sem_post(&buffer->filled_slots);
}

Item dequeue_item(BoundedBuffer *buffer) {
    Item item;
    int filled_value;

    sem_getvalue(&buffer->filled_slots, &filled_value);
    if (filled_value == 0) {
        printf("[Info] Buffer empty. Consumer is blocking...\n");
    }

    sem_wait(&buffer->filled_slots);

    pthread_mutex_lock(&buffer->mutex);

    // Bonus: urgent items are consumed first, FIFO inside each priority.
    if (buffer->urgent_count > 0) {
        item = buffer->urgent_queue[buffer->urgent_head];
        buffer->urgent_head = (buffer->urgent_head + 1) % buffer->capacity;
        buffer->urgent_count--;
    } else {
        item = buffer->normal_queue[buffer->normal_head];
        buffer->normal_head = (buffer->normal_head + 1) % buffer->capacity;
        buffer->normal_count--;
    }

    buffer->total_count--;

    pthread_mutex_unlock(&buffer->mutex);

    sem_post(&buffer->empty_slots);

    return item;
}

void *producer_thread(void *arg) {
    int producer_id = *(int *)arg;
    unsigned int seed = (unsigned int)time(NULL) ^ (producer_id * 7919);

    for (int i = 1; i <= ITEMS_PER_PRODUCER; i++) {
        Item item;

        item.value = (int)(rand_r(&seed) % 1000);
        item.producer_id = producer_id;
        item.item_number = i;
        item.is_poison = 0;

        // At least 25% urgent items for bonus.
        if (i % 4 == 0) {
            item.priority = URGENT_PRIORITY;
        } else {
            item.priority = NORMAL_PRIORITY;
        }

        enqueue_item(&shared_buffer, item);

        pthread_mutex_lock(&stats_mutex);
        real_items_produced++;
        pthread_mutex_unlock(&stats_mutex);

        printf("[Producer-%d] Produced %s item: %d\n",
               producer_id,
               item.priority == URGENT_PRIORITY ? "URGENT" : "NORMAL",
               item.value);
    }

    printf("[Producer-%d] Finished producing %d items.\n",
           producer_id, ITEMS_PER_PRODUCER);

    return NULL;
}

void *consumer_thread(void *arg) {
    int consumer_id = *(int *)arg;

    while (1) {
        Item item = dequeue_item(&shared_buffer);

        if (item.is_poison == 1) {
            printf("[Consumer-%d] Received POISON_PILL. Exiting.\n", consumer_id);
            break;
        }

        struct timespec dequeue_time;
        clock_gettime(CLOCK_MONOTONIC, &dequeue_time);

        double latency = time_difference_seconds(item.enqueue_time, dequeue_time);

        pthread_mutex_lock(&stats_mutex);

        real_items_consumed++;
        total_latency_seconds += latency;

        if (item.priority == URGENT_PRIORITY) {
            urgent_items_consumed++;
        } else {
            normal_items_consumed++;
        }

        pthread_mutex_unlock(&stats_mutex);

        printf("[Consumer-%d] Consumed %s item: %d from Producer-%d\n",
               consumer_id,
               item.priority == URGENT_PRIORITY ? "URGENT" : "NORMAL",
               item.value,
               item.producer_id);
    }

    printf("[Consumer-%d] Finished consuming.\n", consumer_id);

    return NULL;
}

void print_final_statistics(void) {
    double total_time = time_difference_seconds(program_start_time, program_end_time);

    double average_latency = 0.0;
    double throughput = 0.0;

    if (real_items_consumed > 0) {
        average_latency = total_latency_seconds / real_items_consumed;
    }

    if (total_time > 0) {
        throughput = real_items_consumed / total_time;
    }

    printf("\n========== Final Statistics ==========\n");
    printf("Total real items produced: %d\n", real_items_produced);
    printf("Total real items consumed: %d\n", real_items_consumed);
    printf("Urgent items consumed: %d\n", urgent_items_consumed);
    printf("Normal items consumed: %d\n", normal_items_consumed);
    printf("Average latency: %.6f seconds\n", average_latency);
    printf("Throughput: %.2f items/second\n", throughput);
    printf("Total execution time: %.6f seconds\n", total_time);
    printf("All consumers exited successfully.\n");
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <producers> <consumers> <buffer_size>\n", argv[0]);
        return 1;
    }

    if (!parse_positive_int(argv[1], "number of producers", &num_producers) ||
        !parse_positive_int(argv[2], "number of consumers", &num_consumers) ||
        !parse_positive_int(argv[3], "buffer size", &buffer_size)) {
        printf("Usage: %s <producers> <consumers> <buffer_size>\n", argv[0]);
        return 1;
    }

    printf("Starting Producer-Consumer Program\n");
    printf("Producers: %d\n", num_producers);
    printf("Consumers: %d\n", num_consumers);
    printf("Buffer size: %d\n", buffer_size);
    printf("Items per producer: %d\n", ITEMS_PER_PRODUCER);
    printf("Total real items expected: %d\n\n",
           num_producers * ITEMS_PER_PRODUCER);

    initialize_buffer(&shared_buffer, buffer_size);

    pthread_t *producer_threads = malloc(sizeof(pthread_t) * num_producers);
    pthread_t *consumer_threads = malloc(sizeof(pthread_t) * num_consumers);

    int *producer_ids = malloc(sizeof(int) * num_producers);
    int *consumer_ids = malloc(sizeof(int) * num_consumers);

    if (producer_threads == NULL || consumer_threads == NULL ||
        producer_ids == NULL || consumer_ids == NULL) {
        printf("Error: failed to allocate thread arrays.\n");
        destroy_buffer(&shared_buffer);
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &program_start_time);

    for (int i = 0; i < num_consumers; i++) {
        consumer_ids[i] = i + 1;

        if (pthread_create(&consumer_threads[i], NULL,
                           consumer_thread, &consumer_ids[i]) != 0) {
            printf("Error: failed to create consumer thread %d.\n", i + 1);
            return 1;
        }
    }

    for (int i = 0; i < num_producers; i++) {
        producer_ids[i] = i + 1;

        if (pthread_create(&producer_threads[i], NULL,
                           producer_thread, &producer_ids[i]) != 0) {
            printf("Error: failed to create producer thread %d.\n", i + 1);
            return 1;
        }
    }

    for (int i = 0; i < num_producers; i++) {
        if (pthread_join(producer_threads[i], NULL) != 0) {
            printf("Error: failed to join producer thread %d.\n", i + 1);
            return 1;
        }
    }

    printf("\nAll producers finished. Inserting one POISON_PILL per consumer...\n\n");

    for (int i = 0; i < num_consumers; i++) {
        Item poison;

        poison.value = POISON_VALUE;
        poison.producer_id = -1;
        poison.item_number = -1;
        poison.priority = NORMAL_PRIORITY;
        poison.is_poison = 1;

        enqueue_item(&shared_buffer, poison);
    }

    for (int i = 0; i < num_consumers; i++) {
        if (pthread_join(consumer_threads[i], NULL) != 0) {
            printf("Error: failed to join consumer thread %d.\n", i + 1);
            return 1;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &program_end_time);

    print_final_statistics();

    destroy_buffer(&shared_buffer);
    pthread_mutex_destroy(&stats_mutex);

    free(producer_threads);
    free(consumer_threads);
    free(producer_ids);
    free(consumer_ids);

    return 0;
}
