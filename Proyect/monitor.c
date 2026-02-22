#include "monitor.h"

bool safe_sem_wait(sem_t *sem, const char *msg) {
    while (sem_wait(sem) == -1) {
        if (errno != EINTR) {
            perror(msg);
            return false;
        }
    }
    return true;
}

bool safe_sem_post(sem_t *sem, const char *msg) {
    while (sem_post(sem) == -1) {
        if (errno != EINTR) {
            perror(msg);
            return false;
        }
    }
    return true;
}

int setup_monitor(int fd_shm, SharedMem **segmento){
    fd_shm = shm_open(SHM_NAME_MONITOR, O_RDWR, S_IRUSR | S_IWUSR);
    if (fd_shm == -1) {
        perror("shm_open");
        return false;
    }
    *segmento = mmap(NULL, sizeof(SharedMem), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0);
    close(fd_shm);
    if (*segmento == MAP_FAILED) {
        perror("mmap\n");
        fflush(stdout);
        return false;
    }
    return 0;
}

int monitor(SharedMem *segmento) {
    int objetivo, solucion, out;
    bool correcto;
    int id, ganador, votos_positivos, total_votos;
    Monedas monedas_mineros[MAX_MINERS];

    printf("[%d] Printing blocks...\n", getpid());
    fflush(stdout);

    do {
        safe_sem_wait(&segmento->semaforos.sem_fill, "sem_fill");
        safe_sem_wait(&segmento->semaforos.mutex, "mutex");
        out = segmento->out;
        id = segmento->bloques[out].id;
        objetivo = segmento->bloques[out].objetivo;
        solucion = segmento->bloques[out].solucion;
        ganador = segmento->bloques[out].ganador;
        for (int i = 0; i < MAX_MINERS; i++) {
            monedas_mineros[i].pid = segmento->bloques[out].monedas_mineros[i].pid;
            monedas_mineros[i].monedas = segmento->bloques[out].monedas_mineros[i].monedas;
        }
        total_votos = segmento->bloques[out].total_votos;
        votos_positivos = segmento->bloques[out].votos_positivos;
        correcto = segmento->bloques[out].correcto;
        segmento->out = (out + 1) % MAX_BLOQUES;
        safe_sem_post(&segmento->semaforos.mutex, "mutex");
        safe_sem_post(&segmento->semaforos.sem_empty, "sem_empty");

        if (solucion == COD_SALIDA) {
            break;
        }

        fprintf(stdout, "Id:         %5d\n", id);
        fprintf(stdout, "Winner:     %5d\n", ganador);
        fprintf(stdout, "Target:     %5d\n", objetivo);
        fprintf(stdout, "Solution:   %5d ", solucion);
        if (correcto) {
            fprintf(stdout, "(validated)\n");
        } else {
            fprintf(stdout, "(incorrect)\n");
        }
        fprintf(stdout, "Votes:      %d/%d\n", total_votos, votos_positivos);
        fprintf(stdout, "Wallets:    ");
        for (int i = 0; i < MAX_MINERS; i++) {
            if (monedas_mineros[i].pid != -1 && monedas_mineros[i].pid != 0) {
                fprintf(stdout, "%d:%d ", monedas_mineros[i].pid, monedas_mineros[i].monedas);
            }
        }
        fprintf(stdout, "\n\n");
        fflush(stdout);
    } while(solucion != COD_SALIDA);

    printf("[%d] Finishing\n", getpid());
    fflush(stdout);

    return 0;
}

int main() {
    int fd_shm = 0;
    pid_t pid;
    mqd_t mq;
    SharedMem *segmento = NULL;

        if ((fd_shm = shm_open(SHM_NAME_MONITOR, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR)) == -1) {
        if (errno == EEXIST){
            fprintf(stderr, "Error: El segmento de memoria compartida ya existe.\n");
            shm_unlink(SHM_NAME_MONITOR);
            exit(EXIT_FAILURE);
        }
    }

    /* Resize of the memory segment. */
    if (ftruncate(fd_shm, sizeof(SharedMem)) == -1) {
        perror("ftruncate\n");
        fflush(stdout);
        close(fd_shm);
        return 1;
    }
    /* Mapping of the memory segment. */
    segmento = mmap(NULL, sizeof(SharedMem), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0);
    close(fd_shm);
    if (segmento == MAP_FAILED) {
        perror("mmap\n");
        fflush(stdout);
        return 1;
    }

    pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        /* Soy el monitor */
        usleep(100 * 1000);
        // if (setup_monitor(fd_shm, &segmento) == 1) {
        //     fprintf(stderr, "Error setting up comprobador\n");
        //     exit(EXIT_FAILURE);
        // }
        monitor(segmento);
    } else {
        /* Soy el comprobador */
        if (setup_comprobador(&segmento, &mq) == 1) {
            fprintf(stderr, "Error setting up comprobador\n");
            exit(EXIT_FAILURE);
        }
        comprobador(segmento, &mq);

        wait(NULL);
    }  

    fprintf(stdout, "Finishing monitor\n");
    fflush(stdout);

    mq_close(mq);
    munmap(segmento, sizeof(SharedMem));
    mq_unlink(QUEUE_NAME);
    shm_unlink(SHM_NAME_MONITOR);
    exit(EXIT_SUCCESS);
}
