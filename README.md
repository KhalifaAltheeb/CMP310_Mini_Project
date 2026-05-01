# CMP310 Mini Project - Multithreaded Producer-Consumer Application

## Project Description

This project implements a multithreaded Producer-Consumer application in C using POSIX pthreads, semaphores, and mutexes.

Producer threads generate random integer items and insert them into a shared bounded circular buffer. Consumer threads remove items from the buffer and process them by printing the consumed item information.

The program uses synchronization to prevent race conditions, buffer overflow, and buffer underflow.

## Group Members

- Student 1: Khalifa Altheeb / B00095261
- Student 2: Karim Rashad / b00079266 


## Features

- Implemented in C
- Uses POSIX pthreads
- Uses semaphores for synchronization
- Uses a mutex for mutual exclusion
- Configurable number of producer threads
- Configurable number of consumer threads
- Configurable buffer size
- Circular bounded buffer
- Producers block when the buffer is full
- Consumers block when the buffer is empty
- Graceful termination using poison pills
- Input validation and error handling
- Priority handling bonus
- Throughput and latency metrics bonus

## Compilation

Compile the program using:

```bash
gcc -o producer_consumer producer_consumer.c -pthread
```

## Usage

Run the program using:

```bash
./producer_consumer <number_of_producers> <number_of_consumers> <buffer_size>
```

Example:

```bash
./producer_consumer 3 2 10
```

This means:

- 3 producer threads
- 2 consumer threads
- Buffer size = 10
- Each producer produces 20 items
- Total real items = 3 × 20 = 60

## Example Output

```text
Starting Producer-Consumer Program
Producers: 3
Consumers: 2
Buffer size: 10
Items per producer: 20
Total real items expected: 60

[Producer-1] Produced NORMAL item: 187
[Producer-1] Produced URGENT item: 95
[Consumer-1] Consumed URGENT item: 95 from Producer-1
[Consumer-2] Consumed NORMAL item: 187 from Producer-1

All producers finished. Inserting one POISON_PILL per consumer...

[Consumer-1] Received POISON_PILL. Exiting.
[Consumer-1] Finished consuming.
[Consumer-2] Received POISON_PILL. Exiting.
[Consumer-2] Finished consuming.

========== Final Statistics ==========
Total real items produced: 60
Total real items consumed: 60
Urgent items consumed: 15
Normal items consumed: 45
Average latency: 0.000416 seconds
Throughput: 11933.92 items/second
Total execution time: 0.005028 seconds
All consumers exited successfully.
```

## Test Cases

### Test 1: Required Example

```bash
./producer_consumer 3 2 10
```

Expected behavior:

- 3 producers run
- 2 consumers run
- 60 real items are produced
- 60 real items are consumed
- 15 urgent items are consumed
- 45 normal items are consumed
- Consumers exit after receiving poison pills

### Test 2: Small Buffer / Blocking Test

```bash
./producer_consumer 6 2 2
```

Expected behavior:

- Small buffer size causes producers to block when the buffer becomes full
- Output shows buffer full blocking messages
- 120 real items are produced
- 120 real items are consumed

### Test 3: Bonus Metrics - Small Buffer

```bash
./producer_consumer 8 8 2
```

Observed result:

```text
Total real items produced: 160
Total real items consumed: 160
Urgent items consumed: 40
Normal items consumed: 120
Average latency: 0.000121 seconds
Throughput: 7376.19 items/second
Total execution time: 0.021691 seconds
```

### Test 4: Bonus Metrics - Large Buffer

```bash
./producer_consumer 8 8 32
```

Observed result:

```text
Total real items produced: 160
Total real items consumed: 160
Urgent items consumed: 40
Normal items consumed: 120
Average latency: 0.000303 seconds
Throughput: 14225.87 items/second
Total execution time: 0.011247 seconds
```

## Synchronization Design

The program uses two semaphores and one mutex.

### Semaphores

`empty_slots` counts the number of empty spaces in the buffer.

`filled_slots` counts the number of filled spaces in the buffer.

### Mutex

`mutex` protects the shared buffer during insert and remove operations.

This ensures that only one thread can modify the buffer at a time.

## Producer Logic

Each producer performs the following steps:

1. Create a random item.
2. Assign it a priority.
3. Wait on `empty_slots`.
4. Lock the mutex.
5. Insert the item into the circular buffer.
6. Unlock the mutex.
7. Post `filled_slots`.

If the buffer is full, the producer blocks using the semaphore.

## Consumer Logic

Each consumer performs the following steps:

1. Wait on `filled_slots`.
2. Lock the mutex.
3. Remove an urgent item first if available.
4. If no urgent item exists, remove a normal item.
5. Unlock the mutex.
6. Post `empty_slots`.
7. Process the consumed item.

If the buffer is empty, the consumer blocks using the semaphore.

## Circular Buffer

The buffer uses circular queue logic. The head and tail indexes wrap around using modulo operation:

```c
tail = (tail + 1) % capacity;
head = (head + 1) % capacity;
```

This allows efficient use of fixed-size memory.

## Graceful Termination

Each producer generates exactly 20 real items.

After all producers finish, the main thread inserts one poison pill for each consumer.

When a consumer receives a poison pill, it exits its loop safely.

Poison pills are not counted as real items.

## Priority Handling Bonus

Each item has a priority field:

- `0` = normal
- `1` = urgent

At least 25% of the produced items are urgent.

Consumers always remove urgent items first when urgent items are available.

FIFO order is preserved inside each priority queue by using separate circular queues:

- Urgent queue
- Normal queue

## Throughput and Latency Bonus

The program records:

- Enqueue timestamp when an item is inserted
- Dequeue timestamp when an item is removed
- Latency = dequeue time − enqueue time
- Throughput = total consumed items / total execution time

### Bonus Results Table

| Run | Producers | Consumers | Buffer Size | Average Latency | Throughput |
|---|---:|---:|---:|---:|---:|
| Small Buffer | 8 | 8 | 2 | 0.000121 sec | 7376.19 items/sec |
| Large Buffer | 8 | 8 | 32 | 0.000303 sec | 14225.87 items/sec |

The larger buffer achieved higher throughput because producers and consumers blocked less often. The small buffer caused more synchronization blocking because only 2 items could fit in the buffer at once.

## Error Handling

The program validates:

- Correct number of command-line arguments
- Number of producers must be greater than 0
- Number of consumers must be greater than 0
- Buffer size must be greater than 0
- Thread creation errors
- Thread joining errors
- Semaphore initialization errors
- Mutex initialization errors
- Memory allocation errors

## Files Included

```text
producer_consumer.c
README.md
report.pdf
demo_video.mp4
```

## Notes

This program avoids busy waiting. Threads block using semaphores when the buffer is full or empty.

The mutex is only used when accessing the shared buffer, which helps avoid race conditions and deadlocks.
