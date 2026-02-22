#include "monitor.h"

/**
 * @brief Función que inicializa el segmento de memoria compartida y la cola de mensajes.
 * 
 * Crea el segmento de memoria compartida y la cola de mensajes para la comunicación
 * entre los procesos mineros y el monitor.
 * 
 * @param fd_shm Descriptor del segmento de memoria compartida. 
 * @param segmento Puntero al segmento de memoria compartida.
 * @param mq Cola de mensajes para la comunicación con el comprobador.
 */
int setup_comprobador(SharedMem **segmento, mqd_t *mq){
    struct mq_attr attr;

    printf("[%d] Checking blocks...\n", getpid());
    fflush(stdout);

    (*segmento)->in = 0;
    (*segmento)->out = 0;

    /* Si apuntan al mismo es que está vacio, se incremente in al insertar */
    /* Se aumenta out al sacar, */
    /* Si in + 1 apunta a out, está lleno y no se puede añadir */

    /* Iniciar semaforos aqui? */
    /* Inicializamos el semáforo anónimo. El valor inicial es 1 para mutex, 0 para el resto. */
    if (sem_init(&(*segmento)->semaforos.mutex, 1, 1) != 0) {
        perror("sem_init\n");
        fflush(stdout);
        return 1;
    }
    if (sem_init(&(*segmento)->semaforos.sem_fill, 1, 0) != 0) {
        perror("sem_init\n");
        fflush(stdout);
        return 1;
    }
    if (sem_init(&(*segmento)->semaforos.sem_empty, 1, MAX_BLOQUES) != 0) {
        perror("sem_init\n");
        fflush(stdout);
        return 1;
    }

    attr.mq_flags   = 0;               // bloqueo
    attr.mq_maxmsg  = 10;              // max. mensajes en cola
    attr.mq_msgsize = sizeof(Bloque);  // ¡tamaño exacto!
    attr.mq_curmsgs = 0;               // (lectura solo)
    /* Abrir la cola de mensajes para lectura */
    *mq = mq_open(QUEUE_NAME, O_CREAT | O_RDONLY, 0666, &attr);
    if (*mq == (mqd_t)-1) {
        perror("Error al crear/abrir la cola");
        return 1;
    }
    return 0;
}

void comprobador(SharedMem *segmento, mqd_t *mq){
    Bloque recibido;
    int objetivo, solucion, in;
    bool correcto;

    /* Recibir bloque de la cola de mensajes */
    /* Recibir mensajes de la cola */
    do {
        while(mq_receive(*mq, (char*)&recibido, sizeof(Bloque), NULL) == -1);
        objetivo = recibido.objetivo;
        solucion = recibido.solucion;
        if (pow_hash(solucion) == objetivo){
            correcto = true;
        }
        else {
            correcto = false;
        }
        /* Mensaje recibido */
        safe_sem_wait(&segmento->semaforos.sem_empty, "sem_empty");
        safe_sem_wait(&segmento->semaforos.mutex, "mutex");
        in = segmento->in;
        
        /* Escritura en el buffer */
        segmento->bloques[in].id = recibido.id;
        segmento->bloques[in].objetivo = objetivo;
        segmento->bloques[in].solucion = solucion;
        segmento->bloques[in].ganador = recibido.ganador;
        for (int i = 0; i < MAX_MINERS; i++) {
            segmento->bloques[in].monedas_mineros[i].pid = recibido.monedas_mineros[i].pid;
            segmento->bloques[in].monedas_mineros[i].monedas = recibido.monedas_mineros[i].monedas;
        }
        segmento->bloques[in].total_votos = recibido.total_votos;
        segmento->bloques[in].votos_positivos = recibido.votos_positivos;   
        segmento->bloques[in].correcto = correcto;
        segmento->in = (in + 1) % MAX_BLOQUES;

        safe_sem_post(&segmento->semaforos.mutex, "mutex");
        safe_sem_post(&segmento->semaforos.sem_fill, "sem_fill");
    } while (solucion != COD_SALIDA);

    printf("[%d] Finishing\n", getpid());
    fflush(stdout);

    mq_unlink(QUEUE_NAME);
    return;
}