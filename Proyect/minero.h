#ifndef MINERO_H
#define MINERO_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include "pow.h"

#define QUEUE_NAME "/cola_mensajes_con_monitor"
#define SHM_NAME "/red_de_mineros"
#define MAX_MSG_SIZE 100
#define MAX_MSG_COUNT 7  
#define MAX_MINERS 50
#define COD_SALIDA 10000000

#define MAX_THREADS 100 /**< Número máximo de hilos permitidos en la minería */

/**
 * @brief Indica si se ha recibido la señal `SIGINT` (Ctrl+C).
 */
extern volatile sig_atomic_t got_signal_SIGINT;

/**
 * @brief Indica si se ha recibido la señal `SIGALRM` (Finalización por tiempo límite).
 */
extern volatile sig_atomic_t got_signal_SIGALARM; 

/**
 * @brief Indica si se ha recibido la señal `SIGUSR1` (Inicio de votación).
 */
extern volatile sig_atomic_t got_signal_SIGUSR1;

/**
 * @brief Indica si se ha recibido la señal `SIGUSR2` (Indicación para emitir un voto).
 */
extern volatile sig_atomic_t got_signal_SIGUSR2;

/**
 * @brief Manejador de la señal `SIGINT`.
 *
 * Cuando se recibe SIGINT (Ctrl+C), esta función actualiza `got_signal_SIGINT`
 * para indicar que el programa debe finalizar.
 */
void handle_sigint();

/**
 * @brief Manejador de la señal `SIGINT`.
 *
 * Cuando se recibe SIGINT (Ctrl+C), esta función actualiza `got_signal_SIGINT`
 * para indicar que el programa debe finalizar.
 */
void handler_sigusr1();

/**
 * @brief Manejador de la señal `SIGUSR2`.
 *
 * Se usa para indicar a los votantes que deben emitir su voto.
 */
void handler_sigusr2();

/**
 * @brief Manejador de la señal `SIGALRM`.
 *
 * Se usa para indicar que ha finalizado el tiempo de ejecución del programa.
 */
void handler_sigalrm();

/**
 * @struct ThreadData
 * @brief Estructura que almacena los datos necesarios para cada hilo minero.
 * 
 * Esta estructura contiene la información de los rangos de búsqueda, el objetivo a encontrar 
 * y punteros compartidos para comunicar el resultado entre los hilos.
 */
typedef struct
{
    long int start;    /**< Inicio del rango de búsqueda */
    long int end;      /**< Fin del rango de búsqueda */
    long int target;   /**< Valor objetivo a encontrar */
    long int *solution; /**< Puntero para almacenar la solución encontrada */
    int *found;        /**< Indicador de si se encontró la solución */
} ThreadData;

/**
 * @brief Estructura que contiene los semáforos anónimos utilizados en el buffer compartido.
 */
typedef struct {
  sem_t mutex; /**< Semáforo de exclusión mutua */
  sem_t mutex_ronda; /**< Semáforo de exclusión mutua para la ronda */
  sem_t ganador; /**< Semáforo que controla el registro de nuevos mineros */
} Semaforo;

typedef struct {
    pid_t pid; /**< PID del minero */
    int voto; /**< Voto del minero: 1 para aprobar, 0 para rechazar */ 
} Voto;

typedef struct {
    pid_t pid; /**< PID del minero */
    int monedas; /**< Cantidad de monedas del minero */
} Monedas;

/**
 * @brief Representa un bloque de la cadena con objetivo, solución y validez.
 */
typedef struct {
    int id;
    int objetivo;  /**< Valor objetivo del bloque (resultado deseado del POW) */
    int solucion;  /**< Solución propuesta para el POW */
    pid_t ganador;
    Monedas monedas_mineros[MAX_MINERS];
    int total_votos;
    int votos_positivos;
    bool correcto; /**< Bandera que indica si la solución es válida */
} Bloque;

/**
 * @brief Representa el segmento de memoria compartida del sistema.
 */
typedef struct {
    pid_t pid[MAX_MINERS];
    Voto votos_mineros[MAX_MINERS];
    Monedas monedas_mineros[MAX_MINERS];
    Bloque bloque_anterior;
    Bloque bloque_actual; 
    Semaforo semaforos; /**< Estructura con semáforos de control */
    sem_t entry_mutex;    // protege can_enter y waiters_count
    sem_t entry_gate;     // puerta de entrada cerrada cuando ronda ≠ abierta
    int   waiters_count;  
    bool  can_enter;  
} SharedMemMiner;

#endif