#!/bin/bash

# Configuración
DB_NAME="javi"
INTERVALO_SEGUNDOS=2
BASE_TEMP=20
RANGO=10  # Variación total de 10 grados

echo "Iniciando simulador de telemetría en tabla: sensor_state..."
echo "Presiona [CTRL+C] para detener."

while true; do
    # Generamos los updates para 3 sensores distintos
    for ID in 1 2 3; do
        # SQL: Calcula una temp aleatoria entre 15.0 y 25.0 y actualiza el timestamp
        QUERY="UPDATE sensor_state 
               SET current_temp = ($BASE_TEMP + (random() * $RANGO - ($RANGO / 2)))::numeric(5,2),
                   last_update = CURRENT_TIMESTAMP 
               WHERE sensor_id = $ID;"

        psql -d "$DB_NAME" -c "$QUERY" > /dev/null 2>&1
    done

    echo "Sent: Update telemetry for sensors at $(date +%T)"
    
    # Pausa para no saturar la base de datos
    sleep "$INTERVALO_SEGUNDOS"
done