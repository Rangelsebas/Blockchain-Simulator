/**
 * @file monitor.h
 * @brief Definiciones de estructuras, constantes y funciones para el sistema de monitoreo.
 * 
 * Este archivo contiene la definición de las estructuras necesarias para la comunicación entre
 * los procesos Comprobador y Monitor a través de memoria compartida, incluyendo el esquema 
 * de productor-consumidor implementado con semáforos anónimos. Además, se definen funciones 
 * auxiliares seguras para el manejo de semáforos.
 * 
 * Es una parte fundamental del sistema de verificación de bloques basado en la técnica de 
 * prueba de trabajo (POW), ya que centraliza el acceso sincronizado a los bloques verificados.
 * 
 * @author Angel Dal
 * @date 11-05-2025
 */

#ifndef MONITOR_H
#define MONITOR_H

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <semaphore.h>
#include <stdbool.h>
#include <sys/wait.h>

#include "pow.h"
#include "minero.h"

#define SHM_NAME_MONITOR "/monitor" /**< Nombre del segmento de memoria compartida */
#define MAX_BLOQUES 6

/**
 * @brief Estructura que contiene los semáforos anónimos utilizados en el buffer compartido.
 */
typedef struct {
  sem_t mutex; /**< Semáforo de exclusión mutua */
  sem_t sem_empty; /**< Semáforo que controla los espacios vacíos */
  sem_t sem_fill; /**< Semáforo que controla los bloques disponibles */
} Semaforo_monitor;

/**
 * @brief Representa el segmento de memoria compartida con el buffer circular y semáforos.
 */
typedef struct {
  Bloque bloques[MAX_BLOQUES]; /**< Array circular de bloques verificados */
  Semaforo_monitor semaforos; /**< Estructura con semáforos de control */
  int out; /**< Índice de lectura del buffer */
  int in; /**< Índice de escritura del buffer */
} SharedMem;

/**
 * @brief Función auxiliar segura para realizar sem_wait con control de errores.
 * 
 * Esta función intenta realizar un sem_wait en el semáforo proporcionado. Si se interrumpe
 * por una señal, vuelve a intentar la operación. En caso de error, imprime un mensaje y 
 * devuelve false.
 * 
 * @param sem Puntero al semáforo a esperar.
 * @param msg Mensaje de error en caso de fallo.
 * 
 * @return true si la operación fue exitosa, false en caso contrario.
 */
bool safe_sem_wait(sem_t *sem, const char *msg);

/**
 * @brief Función auxiliar segura para realizar sem_post con control de errores.
 * 
 * Esta función intenta realizar un sem_post en el semáforo proporcionado. Si se interrumpe
 * por una señal, vuelve a intentar la operación. En caso de error, imprime un mensaje y 
 * devuelve false.
 * 
 * @param sem Puntero al semáforo a liberar.
 * @param msg Mensaje de error en caso de fallo.
 * 
 * @return true si la operación fue exitosa, false en caso contrario.
 */
bool safe_sem_post(sem_t *sem, const char *msg);

/**
 * @brief Función principal del proceso Comprobador.
 * 
 * Esta función implementa la lógica del proceso Comprobador, que inicializa el segmento 
 * de memoria compartida, los semáforos anónimos, recibe bloques desde una cola de mensajes,
 * los valida usando la función POW y los introduce en el buffer compartido. El proceso se 
 * ejecuta hasta recibir un bloque especial con el objetivo 10000000, tras lo cual limpia 
 * los recursos utilizados.
 * 
 * @param fd_shm Descriptor del segmento de memoria compartida previamente abierto.
 * @param segmento Puntero al segmento de memoria compartida.
 * @param mq Cola de mensajes para recibir bloques.
 * 
 * @return 0 si finaliza correctamente, 1 en caso de error.
 */
void comprobador(SharedMem *segmento, mqd_t *mq);

int setup_comprobador(SharedMem **segmento, mqd_t *mq);

#endif