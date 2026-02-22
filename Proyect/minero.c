#include "minero.h"

/**
 * @brief Función auxiliar segura para realizar sem_wait con control de errores.
 * 
 * @param sem Puntero al semáforo a esperar.
 * @param msg Mensaje de error en caso de fallo.
 */
bool safe_sem_wait(sem_t *sem, const char *msg) {
    while (sem_wait(sem) == -1) {
        if (errno != EINTR) {
            perror(msg);
            return false;
        }
        if (got_signal_SIGINT || got_signal_SIGALARM) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Función auxiliar segura para realizar sem_post con control de errores.
 * 
 * @param sem Puntero al semáforo a liberar.
 * @param msg Mensaje de error en caso de fallo.
 */
bool safe_sem_post(sem_t *sem, const char *msg) {
    while (sem_post(sem) == -1) {
        if (errno != EINTR) {
            perror(msg);
            return false;
        }
        if (got_signal_SIGINT || got_signal_SIGALARM) {
            return false;
        }
    }
    return true;
}

/* Variables globales para la gestión de señales */
volatile sig_atomic_t got_signal_SIGINT = 0;
volatile sig_atomic_t got_signal_SIGALARM = 0; 
volatile sig_atomic_t got_signal_SIGUSR1 = 0;
volatile sig_atomic_t got_signal_SIGUSR2 = 0;

void handle_sigint() {
    got_signal_SIGINT = 1;
}

void handler_sigusr1() {
    got_signal_SIGUSR1 = 1;
}

void handler_sigusr2() {
    got_signal_SIGUSR2 = 1;
}

void handler_sigalrm() {
    got_signal_SIGALARM = 1;
}

/**
 * @brief Función que gestiona la salida del minero.
 * 
 * Elimina el registro del minero en el sistema y envía un mensaje de salida al monitor.
 * 
 * @param segmento Segmento de memoria compartida del sistema.
 * @param mq Cola de mensajes para la comunicación con el comprobador.
 */
void salir(SharedMemMiner **segmento, mqd_t *mq){
    int contador = 0;
    Bloque envio = {0};  // inicializa todo a cero

    /* Debo borrar todos mis datos del segmento del sistema */
    /* Salir de la lista de pids, votos_mineros y monedas_mineros */
    safe_sem_wait(&(*segmento)->semaforos.mutex, "salir");
    for (int i = 0; i < MAX_MINERS; i++){
        if ((*segmento)->pid[i] == getpid()){
            (*segmento)->pid[i] = -1;
            (*segmento)->votos_mineros[i].pid = -1;
            (*segmento)->votos_mineros[i].voto = -1;
            (*segmento)->monedas_mineros[i].monedas = -1;
            (*segmento)->monedas_mineros[i].pid = -1;
            break;
        }
    }
    /* Comprobar si soy el último minero */
    for (int i = 0; i < MAX_MINERS; i++) {
        if ((*segmento)->pid[i] != -1){
            contador++;
        }
    }
    safe_sem_post(&(*segmento)->semaforos.mutex, "salir");
    if (contador == 0){
        /* Soy el último minero, enviar codigo de salida al monitor */
        /* Rellenar el bloque con datos a enviar */
        envio.solucion = COD_SALIDA;
        /* Enviar el bloque completo */
        if (mq_send(*mq, (const char*)&envio, sizeof(Bloque), 0) == -1) {
            perror("Error en mq_send");
            mq_close(*mq);
            exit(EXIT_FAILURE);
        }    
        mq_unlink(QUEUE_NAME);
        shm_unlink(SHM_NAME);
    }

}

/**
 * @brief Función que configura las señales para el proceso minero.
 * 
 * Registra los manejadores de señales para SIGINT, SIGALRM, SIGUSR1 y SIGUSR2.
 * 
 * @return 0 si la configuración es exitosa, -1 en caso contrario.
 */
int setup_signals(){
    sigset_t mask, oset;
    struct sigaction sa_usr1, sa_usr2, sa_int, sa_alrm;


    sigfillset(&oset);
    if (sigprocmask(SIG_BLOCK, &oset, &mask) < 0) {
        perror("sigprocmask");
        return(EXIT_FAILURE);
    }

    /* Configurar el handler para SIGUSR1 */
    sa_usr1.sa_handler = handler_sigusr1;
    sigemptyset(&sa_usr1.sa_mask);
    sa_usr1.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &sa_usr1, NULL) < 0) {
        perror("sigaction");
        return(EXIT_FAILURE);
    }

    /* Configurar el handler para SIGUSR2 */
    sa_usr2.sa_handler = handler_sigusr2;
    sigemptyset(&sa_usr2.sa_mask);
    sa_usr2.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR2, &sa_usr2, NULL) < 0) {
        perror("sigaction");
        return(EXIT_FAILURE);
    }

    /* Configurar el handler para SIGINT */
    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask); 
    sa_int.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa_int, NULL) < 0) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    /* Configurar el handler para SIGALRM */
    sa_alrm.sa_handler = handler_sigalrm;
    sigemptyset(&sa_alrm.sa_mask);
    sa_alrm.sa_flags = SA_RESTART;
    if (sigaction(SIGALRM, &sa_alrm, NULL) < 0) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    /* Desbloquear solo SIGUSR1, SIGUSR2, SIGALRM y SIGINT */
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGALRM);

    if (sigprocmask(SIG_UNBLOCK, &mask, &oset) < 0) {
        perror("sigprocmask");
        return(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Función que envía una señal a todos los mineros registrados en el sistema.
 * 
 * @param sig Señal a enviar.
 * @param segmento Segmento de memoria compartida del sistema.
 * @param no_enviar PID del minero que no debe recibir la señal.
 */
void enviar_señal(int sig, SharedMemMiner *segmento, pid_t no_enviar){
    /* Enviar señal a todos los mineros */
    for (int i = 0; i < MAX_MINERS; i++){
        if (segmento->pid[i] != -1 && segmento->pid[i] != no_enviar){
            if (kill(segmento->pid[i], sig) == -1) {
                switch (errno) {
                    case ESRCH:
                        fprintf(stderr, "Error: no existe el proceso %d\n", segmento->pid[i]);
                        break;
                    case EPERM:
                        fprintf(stderr, "Error: sin permisos para señal %d al proceso %d\n", sig, segmento->pid[i]);
                        break;
                    default:
                        fprintf(stderr, "Error al enviar señal %d a %d: %s\n",
                                sig, segmento->pid[i], strerror(errno));
                }
            }
        }
    }
}

//-> Sí, soy el primer minero
/**
 * @brief Función que gestiona el registro de un nuevo minero en el sistema.
 * 
 * Si el minero es el primero en unirse, inicializa el segmento de memoria compartida y 
 * la cola de mensajes. Si no, se une al sistema existente.
 * 
 * @param fd_shm Descriptor del segmento de memoria compartida.
 * @param segmento Puntero al segmento de memoria compartida.
 * @param mq Cola de mensajes para la comunicación con el comprobador.
 */
bool primer_minero(int fd_shm, SharedMemMiner **segmento, mqd_t *mq){
    /* Comprobar que el monitor esté activo */
    *mq = mq_open(QUEUE_NAME, O_RDWR);
    if (*mq == (mqd_t)-1) {
        printf("Error al abrir la cola de mensajes\n");
        perror("Error al abrir la cola");
        return false;
    }
    
    // 1) Crear y abrir (o abrir si ya existe) con lectura y escritura
    fd_shm = shm_open(SHM_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd_shm == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }

    /* Iniciar el sistema */
    /* Dar tamaño al segmento de MEM compartida del sistema */
    if (ftruncate(fd_shm, sizeof(SharedMemMiner)) == -1) {
        perror("ftruncate\n");
        fflush(stdout);
        close(fd_shm);
        return false;
    }
    /* Enlazarlo a su espacio de memoria */
    (*segmento) = mmap(NULL, sizeof(SharedMemMiner), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0);
    close(fd_shm);
    if ((*segmento) == MAP_FAILED) {
        perror("mmap\n");
        fflush(stdout);
        return 1;
    }
    /* Inicializar todo a un valor por defecto */
    /* Cuantos semaforos? de momento solo uno, acceso a memoria compartida */
    /* Inicializamos el semáforo anónimo. El valor inicial es 1 para mutex */
    if (sem_init(&(*segmento)->semaforos.mutex, 1, 1) != 0) {
        perror("sem_init\n");
        fflush(stdout);
        return false;
    }

    if (sem_init(&(*segmento)->semaforos.mutex_ronda, 1, 1) != 0) {
        perror("sem_init\n");
        fflush(stdout);
        return false;
    }

    if (sem_init(&(*segmento)->semaforos.ganador, 1, 1) != 0) {
        perror("sem_init\n");
        fflush(stdout);
        return false;
    }

    (*segmento)->waiters_count = 0;
    (*segmento)->can_enter    = true;
    if (sem_init(&(*segmento)->entry_mutex, 1, 1) != 0) {
        perror("sem_init\n");
        fflush(stdout);
        return false;
    }

    if (sem_init(&(*segmento)->entry_gate, 1, 0) != 0) {
        perror("sem_init\n");
        fflush(stdout);
        return false;
    }

    safe_sem_wait(&(*segmento)->semaforos.mutex, "mutex");
    for (int i = 0; i < MAX_MINERS; i++) {
        (*segmento)->pid[i] = -1;
        (*segmento)->votos_mineros[i].pid = -1;
        (*segmento)->votos_mineros[i].voto = -1;
        (*segmento)->monedas_mineros[i].pid = -1;
        (*segmento)->monedas_mineros[i].monedas = -1;
    }
    (*segmento)->bloque_anterior.id = -1;
    (*segmento)->bloque_anterior.objetivo = 0;
    (*segmento)->bloque_anterior.solucion = 0;
    (*segmento)->bloque_anterior.ganador = -1;
    (*segmento)->bloque_anterior.total_votos = -1;
    (*segmento)->bloque_anterior.votos_positivos = -1;
    (*segmento)->bloque_actual.id = 1;
    (*segmento)->bloque_actual.objetivo = 0;
    (*segmento)->bloque_actual.solucion = 0;
    (*segmento)->bloque_actual.ganador = -1;
    (*segmento)->bloque_actual.total_votos = -1;
    (*segmento)->bloque_actual.votos_positivos = -1;
    safe_sem_post(&(*segmento)->semaforos.mutex, "mutex");

    /* Esperamos 50ms por si se han unido ya mineros para empezar */
    usleep(5 * 1000);

    /* Mandamos SIGUSR1 a los que estén registrados y empezamos la ronda */
    safe_sem_wait(&(*segmento)->semaforos.mutex_ronda, "mutex_ronda");
    enviar_señal(SIGUSR1, (*segmento), getpid());
    got_signal_SIGUSR1 = 1;

    return true;
}

/**
 * @brief Función que gestiona la entrada de un nuevo minero al sistema.
 * 
 * Si el minero es el primero en unirse, inicializa el segmento de memoria compartida y 
 * la cola de mensajes. Si no, se une al sistema existente.
 * 
 * @param fd_shm Descriptor del segmento de memoria compartida.
 * @param segmento Puntero al segmento de memoria compartida.
 * @param mq Cola de mensajes para la comunicación con el comprobador.
 */
bool otro_minero(int fd_shm, SharedMemMiner **segmento, mqd_t *mq){
    usleep(1 * 1000);
    /* Enlazarlo a su espacio de memoria */
    *mq = mq_open(QUEUE_NAME, O_RDWR);
    if (*mq == (mqd_t)-1){
        printf("Error al abrir la cola de mensajes\n");
        perror("Error al abrir la cola");
        return false;
    }

    fd_shm = shm_open(SHM_NAME, O_RDWR, S_IRUSR | S_IWUSR);
    if (fd_shm == -1) {
        perror("shm_open");
        return false;
    }
    *segmento = mmap(NULL, sizeof(SharedMemMiner), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0);
    close(fd_shm);
    if (*segmento == MAP_FAILED) {
        perror("mmap\n");
        fflush(stdout);
        return false;
    }
    /* Esperar a que el primer minero inicie el sistema */
    /* Comprobar si se ha inicializado */
    // safe_sem_wait(&segmento->semaforos.mutex, "mutex"); Solo leo, no hace falta
    while ((*segmento)->bloque_actual.id <= 0) {
        usleep(1 * 1000);
    }
    //safe_sem_post(&segmento->semaforos.mutex, "mutex");
    return true;
}

/* Soy GANADOR? */
//-> Sí
/**
 * @brief Función que verifica si el minero es el ganador.
 * 
 * Si el minero encuentra una solución válida, actualiza su estado y envía la señal 
 * correspondiente a los demás mineros.
 * 
 * @param solucion Solución encontrada por el minero.
 * @param wallet Puntero al wallet del minero.
 * @param mq Cola de mensajes para la comunicación con el comprobador.
 * @param segmento Segmento de memoria compartida del sistema.
 */
bool ganador(int solucion, int *wallet, mqd_t mq, SharedMemMiner **segmento){
    int mineros = 0;
    int espera_maxima = 0;
    Bloque envio = {0};  // inicializa todo a cero
    bool terminado = false;

    /* Introduce la solución al bloque actual y vota (obviamente a favor) */
    safe_sem_wait(&(*segmento)->semaforos.mutex, "mutex");
    /* Establecer todos los votos a 0 */
    for (int i = 0; i < MAX_MINERS; i++) {
        (*segmento)->votos_mineros[i].voto = 0;
    }
    (*segmento)->bloque_actual.solucion = solucion;
    (*segmento)->bloque_actual.ganador = getpid();
    for (int i = 0; i < MAX_MINERS && !terminado; i++) {
            if ((*segmento)->votos_mineros[i].pid == getpid()){
                (*segmento)->votos_mineros[i].voto = 1;
                terminado = true;
            }
        }
    safe_sem_post(&(*segmento)->semaforos.mutex, "mutex");
    /* Enviar la señal SIGUSR2 a todos los mineros */
    enviar_señal(SIGUSR2, (*segmento), getpid());
    /* Contar mineros registrados */
    for (int i = 0; i < MAX_MINERS; i++) {
        if ((*segmento)->pid[i] != -1) {
            mineros++;
        }
    }
    usleep(1 * 1000);
    safe_sem_post(&(*segmento)->semaforos.ganador, "ganador");
    /* Esperar a que todos los mineros voten */
    while((*segmento)->bloque_actual.total_votos < mineros && espera_maxima < 500){
        usleep(1 * 1000);
        espera_maxima++;
    }

    /* Contar votos */
    safe_sem_wait(&(*segmento)->semaforos.mutex, "mutex");
    (*segmento)->bloque_actual.votos_positivos = 0;
    (*segmento)->bloque_actual.total_votos = 0;
    for (int i = 0; i < MAX_MINERS; i++) {
        if ((*segmento)->votos_mineros[i].voto == 1) {
            (*segmento)->bloque_actual.votos_positivos++;
        }
        if ((*segmento)->votos_mineros[i].pid != -1 && (*segmento)->votos_mineros[i].voto != -1) {
            (*segmento)->bloque_actual.total_votos++;
        }
    }
    /* Si es aprobado se añade una moneda */
    if ((*segmento)->bloque_actual.votos_positivos > mineros / 2){
        /* Añadir monedas al wallet */
        for (int i = 0; i < MAX_MINERS; i++) {
            if ((*segmento)->monedas_mineros[i].pid == getpid()) {
                (*segmento)->monedas_mineros[i].monedas++;
                (*wallet)++;
            }
        }
        (*segmento)->bloque_actual.correcto = true;
    }
    else {
        (*segmento)->bloque_actual.correcto = false;
    }
    for (int i = 0; i < MAX_MINERS; i++) {
        (*segmento)->bloque_actual.monedas_mineros[i].pid = (*segmento)->monedas_mineros[i].pid; 
        (*segmento)->bloque_actual.monedas_mineros[i].monedas = (*segmento)->monedas_mineros[i].monedas;
    }
    /* Envia el bloque por la cola de mensajes al comprobador */
    /* Rellenar el bloque con datos a enviar */
    envio.id            = (*segmento)->bloque_actual.id;
    envio.objetivo      = (*segmento)->bloque_actual.objetivo;
    envio.solucion      = (*segmento)->bloque_actual.solucion;
    envio.ganador       = (*segmento)->bloque_actual.ganador;
    for (int i = 0; i < MAX_MINERS; i++) {
        if ((*segmento)->votos_mineros[i].voto != -1) {
            envio.monedas_mineros[i].pid     = (*segmento)->monedas_mineros[i].pid;
            envio.monedas_mineros[i].monedas = (*segmento)->monedas_mineros[i].monedas;
        }
    }
    envio.total_votos     = (*segmento)->bloque_actual.total_votos;
    envio.votos_positivos = (*segmento)->bloque_actual.votos_positivos;
    /* Enviar el bloque completo */
    if (mq_send(mq, (const char*)&envio, sizeof(Bloque), 0) == -1) {
        perror("Error en mq_send");
        mq_close(mq);
        return false;
    }    
    /* Prepara la siguiente ronda */
    /* Desecha el último bloque, el bloque actual pasa a ser el último y crea uno nuevo */
    /* Establece como objetivo la solucion anterior */
    (*segmento)->bloque_anterior = (*segmento)->bloque_actual;
    (*segmento)->bloque_actual.id++;
    (*segmento)->bloque_actual.objetivo = (*segmento)->bloque_anterior.solucion;
    (*segmento)->bloque_actual.solucion = pow_hash((*segmento)->bloque_actual.solucion);
    (*segmento)->bloque_actual.ganador = getpid();
    (*segmento)->bloque_actual.correcto = false;
    (*segmento)->bloque_actual.total_votos = 0;
    (*segmento)->bloque_actual.votos_positivos = 0;
    for (int i = 0; i < MAX_MINERS; i++) {
        if ((*segmento)->votos_mineros[i].pid != -1) {
            (*segmento)->votos_mineros[i].voto = 0;
        }
    }
    safe_sem_post(&(*segmento)->semaforos.mutex, "mutex");
    usleep(1 * 1000); // Esperar 25ms para que los mineros se preparen
    /* Enviar la señal SIGUSR1 a los mineros */
    enviar_señal(SIGUSR1, (*segmento), getpid());
    got_signal_SIGUSR1 = 1;

    return true;
}

//-> No
/**
 * @brief Función que gestiona la salida de un minero del sistema.
 * 
 * Si el minero no es el ganador, se registra su voto y se espera a la señal SIGUSR1 
 * para comenzar una nueva ronda.
 * 
 * @param segmento Segmento de memoria compartida del sistema.
 * @return true si el minero sigue en el sistema, false si ha terminado.
 */
bool perdedor(SharedMemMiner **segmento){
    sigset_t emptymask;
    bool terminado = false;
    /* Esperar a que el ganador envíe la señal SIGUSR2 */
    sigfillset(&emptymask);
    sigdelset(&emptymask, SIGUSR2);
    sigdelset(&emptymask, SIGINT);
    sigdelset(&emptymask, SIGALRM);

    /* Esperar SIGUSR2 para comenzar */
    /* Si se recibe SIGUSR2 se terminan todos los hilos y se pasa a la votación */
    while(!got_signal_SIGUSR2 && !got_signal_SIGINT && !got_signal_SIGALARM){
        sigsuspend(&emptymask);
    }
    got_signal_SIGUSR2 = 0;

    if (got_signal_SIGINT || got_signal_SIGALARM) {
        return false;
    }

    usleep(1 * 1000); // Esperar 1ms para que los mineros se preparen
    /* Comprueba el bloque actual y vota */

    safe_sem_wait(&(*segmento)->semaforos.mutex, "mutex");

    if ((*segmento)->bloque_actual.objetivo == pow_hash((*segmento)->bloque_actual.solucion)){
        for (int i = 0; i < MAX_MINERS && !terminado; i++) {
            if ((*segmento)->votos_mineros[i].pid == getpid()){
                (*segmento)->votos_mineros[i].voto = 1;
                terminado = true;
            }
        }
        
    }
    else {
        // Perdedor vota NO (0) en su propia casilla
        for (int i = 0; i < MAX_MINERS && !terminado; i++) {
            if ((*segmento)->votos_mineros[i].pid == getpid()) {
                (*segmento)->votos_mineros[i].voto = 0;
                terminado = true;
            }
        }
    }
    safe_sem_post(&(*segmento)->semaforos.mutex, "mutex");
    /* Entra en espera no activa hasta recibir SIGUSR1 para una nueva ronda */
    return true;
}

/**
 * @brief Función que ejecuta un hilo minero.
 * 
 * Cada hilo busca una solución dentro de su rango asignado. Si encuentra una coincidencia 
 * con el valor objetivo, actualiza la variable compartida de solución y termina la ejecución.
 * 
 * @param data Puntero a una estructura ThreadData con los datos del hilo.
 * @return NULL siempre.
 */
void *miner_thread(void *data) {
    int i;
    long int end, target;
    ThreadData *thread_data;

    thread_data = (ThreadData *)data;

    end = thread_data->end;
    target = thread_data->target;
    for (i = thread_data->start; i < end; i++) {
        if (pow_hash(i) == target) {
            *(thread_data->found) = 1;
            *(thread_data->solution) = i;

            return NULL;
        }
        if (*(thread_data->found) == 1)
            return NULL;

        if (got_signal_SIGUSR2) {
            *(thread_data->found) = -1;
            *(thread_data->solution) = -1;
            return NULL;
        }

        if (got_signal_SIGALARM || got_signal_SIGINT) {
            *(thread_data->found) = -1;
            *(thread_data->solution) = -1;
            return NULL;
        }   
    }

    return NULL;
}

/**
 * @brief Función principal del proceso minero.
 * 
 * Inicializa la minería dividiendo el trabajo entre múltiples hilos, gestiona la comunicación 
 * con el monitor y verifica los resultados obtenidos.
 * 
 * @param N_THREADS Número de hilos a crear para la minería.
 * @param mq Cola de mensajes para la comunicación con el comprobador.
 * @param segmento Segmento de memoria compartida para la comunicación con el monitor.
 * @param wallet Puntero al wallet del minero.
 */
int minero(int N_THREADS, mqd_t mq, SharedMemMiner **segmento, int *wallet) {
    long int target, range, solution;
    sigset_t emptymask;
    pthread_t *threads;
    ThreadData *thread_data;
    int found, j;
    bool registrado = false;

    /* Verifico que no esté en la tabla */
    for (int i = 0; i < MAX_MINERS; i++) {
        if ((*segmento)->pid[i] == getpid()) {
            registrado = true;
        }
    }

    if (!registrado) {
        safe_sem_wait(&(*segmento)->entry_mutex, "entry_mutex");
        if ((*segmento)->can_enter) {
            // se registra inmediatamente
            for (int i = 0; i < MAX_MINERS; i++) {
                if ((*segmento)->pid[i] == -1) {
                    (*segmento)->pid[i]                = getpid();
                    (*segmento)->votos_mineros[i].pid  = getpid();
                    (*segmento)->votos_mineros[i].voto = -1;
                    (*segmento)->monedas_mineros[i].pid= getpid();
                    (*segmento)->monedas_mineros[i].monedas = *wallet;
                    break;
                }
            }
            safe_sem_post(&(*segmento)->entry_mutex, "entry_mutex");
        } else {
            // ronda ya empezó, cuenta y espera
            (*segmento)->waiters_count++;
            safe_sem_post(&(*segmento)->entry_mutex, "entry_mutex");
            safe_sem_wait(&(*segmento)->entry_gate, "entry_gate");
            // al despertar, se registra
            for (int i = 0; i < MAX_MINERS; i++) {
                if ((*segmento)->pid[i] == -1) {
                    (*segmento)->pid[i]                = getpid();
                    (*segmento)->votos_mineros[i].pid  = getpid();
                    (*segmento)->votos_mineros[i].voto = -1;
                    (*segmento)->monedas_mineros[i].pid= getpid();
                    (*segmento)->monedas_mineros[i].monedas = *wallet;
                    break;
                }
            }
        }
    }

    /* PARTE COMUN 1) */
    /* Esperar a que el primer minero envíe la señal SIGUSR1 */
    /* Enmascarar señales que no sean las esperadas */
    sigfillset(&emptymask);
    sigdelset(&emptymask, SIGUSR1);
    sigdelset(&emptymask, SIGINT);
    sigdelset(&emptymask, SIGALRM);

    /* Esperar SIGUSR1 para comenzar */
    while(!got_signal_SIGUSR1 && !got_signal_SIGINT && !got_signal_SIGALARM){
        sigsuspend(&emptymask);
    }
    got_signal_SIGUSR1 = 0;

    usleep(10 * 1000); // Esperar 10ms para que los mineros se preparen

    // justo antes de que empiece la fase de minado:
    safe_sem_wait(&(*segmento)->entry_mutex, "entry_mutex");
    (*segmento)->can_enter = false;
    safe_sem_post(&(*segmento)->entry_mutex, "entry_mutex");


    if (got_signal_SIGINT || got_signal_SIGALARM) {
        return 0;
    }

    if (N_THREADS < 1) {
        return 1;
    }

    if (N_THREADS > MAX_THREADS) {
        fprintf(stderr, "Error: Number of threads exceeded\n");
        fflush(stdout);
        return 1;
    }

    threads = (pthread_t *)malloc(sizeof(pthread_t) * N_THREADS);
    if (!threads) {
        perror("malloc() threads failure\n");
        fflush(stdout);
        return 1;
    }
    thread_data = (ThreadData *)malloc(sizeof(ThreadData) * N_THREADS);
    if (!thread_data) {
        perror("malloc() thread_data failure\n");
        fflush(stdout);
        free(threads);
        return 1;
    }

    /* ---PROCESO MINERO--- */
    printf("[%d] Generating blocks...\n", getpid());
    fflush(stdout);


    found = 0;
    solution = -1;
    range = POW_LIMIT / N_THREADS;
    target = (*segmento)->bloque_actual.objetivo;

    for (j = 0; j < N_THREADS; j++) {
        thread_data[j].start = j * range;
        thread_data[j].end = (j == N_THREADS - 1) ? POW_LIMIT : (j + 1) * range;
        thread_data[j].target = target;
        thread_data[j].solution = &solution;
        thread_data[j].found = &found;

        pthread_create(&threads[j], NULL, miner_thread, &thread_data[j]);
    }

    for (j = 0; j < N_THREADS; j++) {
        pthread_join(threads[j], NULL);
    }

    if (got_signal_SIGINT || got_signal_SIGALARM) {
        free(thread_data);
        free(threads);
        return 0;
    }

    /* Si se obtiene la solución trata de convertirse en GANADOR usando los semaforos */
    if (found) {
        safe_sem_wait(&(*segmento)->semaforos.ganador, "ganador");
        if (got_signal_SIGUSR2) {
            /* He perdido, no soy el ganador*/
            safe_sem_post(&(*segmento)->semaforos.ganador, "ganador");
            if (!perdedor(segmento)) {
                free(thread_data);
                free(threads);
                return 1;
            }       
        }
        /* Soy el ganador */
        else {
            if (!ganador(solution, wallet, mq, segmento)) {
                free(thread_data);
                free(threads);
                safe_sem_post(&(*segmento)->semaforos.ganador, "ganador");
                return 1;
            }
        }
        safe_sem_post(&(*segmento)->semaforos.ganador, "ganador");
    }

    if (got_signal_SIGINT || got_signal_SIGALARM) {
        free(thread_data);
        free(threads);
        return 0;
    }

    if (got_signal_SIGUSR2) {
        /* He perdido, no soy el ganador*/
        if (!perdedor(segmento)) {
            free(thread_data);
            free(threads);
            return 1;
        }       
    }

    safe_sem_wait(&(*segmento)->entry_mutex, "entry_mutex");
    (*segmento)->can_enter = true;
    for (int i = 0; i < (*segmento)->waiters_count; i++) {
        safe_sem_post(&(*segmento)->entry_gate, "entry_gate");
    }
    (*segmento)->waiters_count = 0;
    safe_sem_post(&(*segmento)->entry_mutex, "entry_mutex");
    
    free(thread_data);
    free(threads);
    return 0;
}

int main(int argc, char const *argv[]) {
    SharedMemMiner *segmento = NULL;
    mqd_t mq;
    int n_hilos, n_seconds;
    int fd_shm;
    int wallet = 0;

    if (argc != 3)
    {
        printf("\nError en parametros\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    n_seconds = atoi(argv[1]);
    if (n_seconds <= 0)
    {
        printf("\nLa cantidad de segundos debe ser superior a 0.\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    n_hilos = atoi(argv[2]);
    if (n_hilos <= 0 && n_hilos > MAX_THREADS)
    {
        printf("\nEl número de hilos debe ser superior a 0 y no mayor a %d\n", MAX_THREADS);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    /* Configurar señales */
    /* Establecer alarma */
    /* Configurar handlers y mascaras*/
    if (setup_signals() == EXIT_FAILURE) {
        mq_close(mq);
        mq_unlink(QUEUE_NAME);
        shm_unlink(SHM_NAME);
        exit(EXIT_FAILURE);
    }

    /* Soy el primer minero? */
    /* ¿Cómo? Viendo si ya hay memoria compartida */
    /* Crear el fichero y comprobar que si existe */
    if ((fd_shm = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR)) == -1) {
        if (errno == EEXIST){
            /* Ya existía el fichero*/
            /* Me he unido al sistema */
            if(!otro_minero(fd_shm, &segmento, &mq)){
                mq_close(mq);
                mq_unlink(QUEUE_NAME);
                shm_unlink(SHM_NAME);
                exit(EXIT_FAILURE);
            }
        }
        else {
            perror("shm_open\n");
            fflush(stdout);
            mq_close(mq);
            mq_unlink(QUEUE_NAME);
            shm_unlink(SHM_NAME);
            exit(EXIT_FAILURE);
        }
    } else {
        /* No existia */
        //-> Sí, soy el primer minero
        if(!primer_minero(fd_shm, &segmento, &mq)){
            mq_close(mq);
            mq_unlink(QUEUE_NAME);
            shm_unlink(SHM_NAME);
            exit(EXIT_FAILURE);
        }
    }


    alarm(n_seconds); // Establece la alarma
    /* Entrar en el sistema */

    while(got_signal_SIGALARM == 0 && got_signal_SIGINT == 0){
        if(minero(n_hilos, mq, &segmento, &wallet) != 0){
            mq_close(mq);
            mq_unlink(QUEUE_NAME);
            shm_unlink(SHM_NAME);
            exit(EXIT_FAILURE);
        }
    }


    /* Comun a todos 1) */
    /* Registrar su PID en el listado de MEM comp */
    /* Si está lleno debe salir */

    /* Comun a todos 2) */
    /* Proceso de minado */
    /* Minar */

        //-> No
            /* Esperar a que el ganador envíe la señal SIGUSR2 */
            /* Si se recibe SIGUSR2 se terminan todos los hilos y se pasa a la votación */
            /* Comprueba el bloque actual y vota */
            /* Entra en espera no activa hasta recibir SIGUSR1 para una nueva ronda */

    /* IMPORTANTE, USAR SEMAFOROS PARA LAS RONDAS Y PERMITIR ENTRAR NUEVOS MINEROS O NO */

    /* Comun a todos 3) */
    /* Si se recibe SIGINT o SIGALRM se debe salir del sistema correctamente */


    /* Cola de mensajes PARA TODOS, la usará el ganador */
    salir(&segmento, &mq);

    munmap(segmento, sizeof(SharedMemMiner));
    mq_close(mq);
    exit(EXIT_SUCCESS);
}


