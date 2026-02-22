# Distributed Proof-of-Work (PoW) Simulator

## Overview
A high-performance distributed system implemented in **C** that simulates a Blockchain mining environment. The system manages multiple concurrent mining processes that compete to solve Proof-of-Work puzzles, coordinated through advanced **Linux System Programming** techniques, including IPC (Inter-Process Communication) and multi-threading.

## Architecture & System Design
The system follows a decoupled architecture consisting of three main components:

* **Miners (Multi-threaded):** Independent processes that execute multiple threads (`pthread`) to solve the PoW. They communicate with each other via **signals (SIGUSR1, SIGUSR2)** to coordinate rounds and declare winners.
* **Checker (Comprobador):** Acts as the system validator. It receives proposed blocks via **POSIX Message Queues**, validates the solution, and manages the voting system among miners.
* **Monitor:** A child process of the Checker that provides real-time visualization of the blockchain state using **Shared Memory**.

## Technical Key Features & Implementation

### 1. Advanced Synchronization
* Implemented **POSIX Anonymous Semaphores** to manage access to shared resources and coordinate the Producer-Consumer pattern between the Checker and the Monitor.
* Used **Mutexes** and conditional logic to prevent race conditions during multi-threaded mining.
* Developed a "Gate" mechanism (`entry_gate`, `entry_mutex`) to handle the dynamic entry of new miners without disrupting active rounds.

### 2. Inter-Process Communication (IPC)
* **Shared Memory (`shm_open` & `mmap`):** Utilized for a global system state accessible by all miners and for a circular buffer between the Checker and Monitor.
* **POSIX Message Queues (`mq_send`/`mq_receive`):** Established a reliable, asynchronous channel for miners to submit blocks to the Checker.
* **Signal Handling:** Custom handlers for `SIGUSR1` (new block found), `SIGUSR2` (round end), `SIGINT` (graceful shutdown), and `SIGALRM` (round timeouts).

### 3. Resource Management & Robustness
* **Graceful Exit:** Guaranteed cleanup of all IPC resources (unlinking queues, detaching memory) even upon unexpected interruptions.
* **Concurrency Control:** Efficient management of up to `MAX_MINERS` and multiple threads per miner to maximize CPU utilization during the PoW search.

## Tech Stack
* **Language:** C (Standard C11)
* **Operating System:** Linux/Unix
* **Libraries:** `pthread.h`, `semaphore.h`, `mman.h`, `mqueue.h`, `signal.h`
* **Build System:** Makefile

## How to Run
1.  **Compile the system:**
    ```bash
    make
    ```
2.  **Launch the Checker (and Monitor):**
    ```bash
    ./checker
    ```
3.  **Launch one or more Miners (specifying threads and seconds of life):**
    ```bash
    ./miner <n_threads> <seconds>
    ```

---
*Developed as a core project for the Operating Systems course at Universidad Autónoma de Madrid, focusing on distributed computing and low-level resource synchronization.*
