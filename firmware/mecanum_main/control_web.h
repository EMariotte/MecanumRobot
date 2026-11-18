#pragma once
#include <Arduino.h>
#include "motores.h"

// ============================================================
//  CONTROL WEB — Wi-Fi en modo AP, página desde LittleFS y
//  comandos por WebSocket (/ws).
//
//  El callback del WebSocket corre fuera de loop(): solo guarda
//  datos y levanta banderas. loop() las consulta con estas
//  funciones (cada "Tomar" devuelve true una vez por evento).
// ============================================================

void webIniciar();                       // LittleFS + AP + servidor + WebSocket
void webAtender();                       // tareas que van en loop() (cerrar clientes viejos)

bool webTomarComando(Comando &c);        // true si llegó un comando nuevo
bool webTomarStop();                     // true si se pidió STOP
bool webTomarControlPerdido();           // true si se desconectó el dispositivo con el control
unsigned long webTiempoUltimoComando();  // millis() del último comando (latido del failsafe)
uint32_t webTomarContadorComandos();     // comandos recibidos desde la llamada anterior

int  webDispositivos();                  // equipos conectados a la red del robot
void webEnviarEstado(const char *json);  // a todos los clientes
