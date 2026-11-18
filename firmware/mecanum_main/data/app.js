// Página de control — Robot Mecanum 4WD — firmware mecanum_main
// Dos joysticks (mover + girar) enviados por WebSocket, prueba de ruedas
// como diagnóstico, estado del robot y batería (INA226).
//
// Protocolo (texto):
//   Página → robot   "C vx vy w vmax"   20 veces por segundo (también sirve de latido)
//                    "M i v vmax"       en lugar de "C" mientras se prueba una rueda (i: 0=FL 1=FR 2=RL 3=RR)
//                    "S"                STOP inmediato
//                    "P n"              ping para medir latencia
//   Robot → página   {"up","heap","cli","hz","fs","bat","lim",
//                     "v","i","p","mah","fw"}  estado y batería, 1 vez/s
//                    "P n"              respuesta al ping
//                    "TOMADO"           otro dispositivo tomó el control

const PERIODO_ENVIO_MS = 50;     // 20 comandos/s — el failsafe del robot es de 300 ms
const PERIODO_PING_MS = 1000;
const REINTENTO_MS = 1000;       // espera antes de reconectar
const ESTADO_TIMEOUT_MS = 2500;  // sin estado del robot en este tiempo = desconectado
const ZONA_MUERTA = 0.12;        // fracción del recorrido del joystick que se ignora (centro)

// Estado de la batería que decide el robot (campo "bat"); umbrales en config.h
const BAT = { SIN_SENSOR: 0, SIN_BATERIA: 1, NORMAL: 2, BAJA: 3, CRITICA: 4, AGOTADA: 5 };

// Batería 2S Li-ion (2 celdas en serie)
const CELDAS = 2;
// Carga aproximada de una celda Li-ion en reposo: [voltios, %]
const CURVA_CELDA = [
  [3.00, 0], [3.30, 5], [3.50, 10], [3.60, 20], [3.70, 35],
  [3.80, 50], [3.90, 65], [4.00, 78], [4.10, 90], [4.20, 100],
];

const $ = (id) => document.getElementById(id);
const NOMBRE_RUEDA = ["FL", "FR", "RL", "RR"];

let ws = null;
let controlTomado = false;       // otro dispositivo tiene el control: no reconectar solo
let ultimoEstado = 0;
let cmd = { vx: 0, vy: 0, w: 0, rueda: -1 };   // rueda ≥ 0: prueba de una sola rueda

// ------------------------------------------------------------
//  Conexión WebSocket
// ------------------------------------------------------------
function conectar() {
  if (controlTomado) return;
  ws = new WebSocket(`ws://${location.host}/ws`);

  ws.onopen = () => marcarConexion(true);

  ws.onclose = () => {
    ws = null;
    marcarConexion(false);
    if (!controlTomado) setTimeout(conectar, REINTENTO_MS);
  };

  ws.onerror = () => { if (ws) ws.close(); };

  ws.onmessage = (ev) => procesarMensaje(ev.data);
}

function enviar(texto) {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(texto);
}

function procesarMensaje(msg) {
  if (msg === "TOMADO") {
    controlTomado = true;
    soltarTodo();
    $("aviso-tomado").classList.remove("oculto");
    return;
  }
  if (msg.startsWith("P ")) {
    const enviado = Number(msg.slice(2));
    $("latencia").textContent = Math.round(performance.now() - enviado) + " ms";
    return;
  }
  if (msg.startsWith("{")) {
    const e = JSON.parse(msg);
    ultimoEstado = performance.now();
    $("uptime").textContent = formatoTiempo(e.up);
    $("heap").textContent = (e.heap / 1024).toFixed(1) + " kB";
    $("clientes").textContent = e.cli;
    $("hz").textContent = e.hz;
    $("failsafe").classList.toggle("oculto", e.fs !== 1);
    $("fw").textContent = e.fw;
    mostrarBateria(e);
  }
}

// ------------------------------------------------------------
//  Batería
// ------------------------------------------------------------
// Porcentaje aproximado interpolando la curva de una celda.
// Con los motores andando el voltaje baja y el % se ve menor: es normal.
function porcentajeCarga(voltaje) {
  const v = voltaje / CELDAS;
  if (v <= CURVA_CELDA[0][0]) return 0;
  for (let k = 1; k < CURVA_CELDA.length; k++) {
    const [v1, p1] = CURVA_CELDA[k];
    if (v <= v1) {
      const [v0, p0] = CURVA_CELDA[k - 1];
      return Math.round(p0 + (v - v0) / (v1 - v0) * (p1 - p0));
    }
  }
  return 100;
}

function avisoBateria(texto, clase) {
  const el = $("bat-aviso");
  el.textContent = texto;
  el.className = "indicador " + clase + (texto ? "" : " oculto");
}

function mostrarBateria(e) {
  // Velocidad máxima recortada por el robot (batería crítica o agotada)
  $("vel-limite").textContent = e.lim < 100 ? ` · limitada a ${e.lim}%` : "";

  if (e.bat === BAT.SIN_SENSOR) {
    $("bateria").textContent = "sin sensor";
    ["corriente", "potencia", "mah"].forEach((id) => ($(id).textContent = "--"));
    avisoBateria("", "");
    return;
  }
  $("corriente").textContent = e.i.toFixed(2) + " A";
  $("potencia").textContent = e.p.toFixed(1) + " W";
  $("mah").textContent = Math.round(e.mah) + " mAh";

  if (e.bat === BAT.SIN_BATERIA) {
    $("bateria").textContent = "sin batería";
    avisoBateria("", "");
    return;
  }
  $("bateria").textContent = `${e.v.toFixed(2)} V · ${porcentajeCarga(e.v)}%`;
  switch (e.bat) {
    case BAT.BAJA:    avisoBateria("Batería baja", "aviso"); break;
    case BAT.CRITICA: avisoBateria(`Batería crítica · velocidad ${e.lim}%`, "critico"); break;
    case BAT.AGOTADA: avisoBateria("Batería agotada · motores bloqueados", "critico"); break;
    default:          avisoBateria("", "");
  }
}

function marcarConexion(conectado) {
  const el = $("conexion");
  el.textContent = conectado ? "Conectado" : "Sin conexión";
  el.className = "indicador " + (conectado ? "conectado" : "desconectado");
  if (!conectado) {
    $("latencia").textContent = "--";
    $("hz").textContent = "--";
  }
}

function formatoTiempo(seg) {
  const m = Math.floor(seg / 60);
  const s = seg % 60;
  return m > 0 ? `${m} min ${s} s` : `${s} s`;
}

// ------------------------------------------------------------
//  Joystick táctil reutilizable
//  base: elemento donde se toca · horizontal: solo eje X
//  alCambiar(x, y): valores -100..100 (y positivo = arriba)
// ------------------------------------------------------------
function crearJoystick(base, horizontal, alCambiar) {
  const mando = base.querySelector(".mando");
  let puntero = null;

  function mover(ev) {
    const r = base.getBoundingClientRect();
    // Recorrido máximo del mando desde el centro
    const radioX = (r.width - mando.offsetWidth) / 2;
    const radioY = horizontal ? 0 : (r.height - mando.offsetHeight) / 2;
    let dx = ev.clientX - (r.left + r.width / 2);
    let dy = horizontal ? 0 : ev.clientY - (r.top + r.height / 2);

    // Limitar al círculo (o a la barra si es horizontal)
    let x = dx / radioX;
    let y = horizontal ? 0 : -dy / radioY;
    const largo = Math.hypot(x, y);
    if (largo > 1) { x /= largo; y /= largo; }
    mando.style.transform = `translate(${x * radioX}px, ${-y * radioY}px)`;

    // Zona muerta en el centro y reescalado del resto a 0..1
    const m = Math.min(Math.hypot(x, y), 1);
    if (m < ZONA_MUERTA) {
      x = 0; y = 0;
    } else {
      const k = (m - ZONA_MUERTA) / (1 - ZONA_MUERTA) / m;
      x *= k; y *= k;
    }
    alCambiar(Math.round(x * 100), Math.round(y * 100));
  }

  function soltar() {
    if (puntero === null) return;
    puntero = null;
    mando.style.transform = "";
    base.classList.remove("activo");
    alCambiar(0, 0);
  }

  base.addEventListener("pointerdown", (ev) => {
    ev.preventDefault();
    if (puntero !== null) return;            // ya hay un dedo en este joystick
    puntero = ev.pointerId;
    base.setPointerCapture(puntero);
    base.classList.add("activo");
    mover(ev);
  });
  base.addEventListener("pointermove", (ev) => { if (ev.pointerId === puntero) mover(ev); });
  base.addEventListener("pointerup", (ev) => { if (ev.pointerId === puntero) soltar(); });
  base.addEventListener("pointercancel", (ev) => { if (ev.pointerId === puntero) soltar(); });
  base.addEventListener("lostpointercapture", (ev) => { if (ev.pointerId === puntero) soltar(); });

  return {
    soltar() {
      if (puntero !== null && base.hasPointerCapture(puntero)) base.releasePointerCapture(puntero);
      soltar();
    },
  };
}

const joyMover = crearJoystick($("joy-mover"), false, (x, y) => {
  cmd.vx = x;
  cmd.vy = y;
  mostrarComando();
});

const joyGirar = crearJoystick($("joy-girar"), true, (x) => {
  cmd.w = x;                     // derecha = girar en sentido horario
  mostrarComando();
});

// ------------------------------------------------------------
//  Comandos
// ------------------------------------------------------------
function velocidadMax() {
  return Number($("vel-max").value);
}

function mostrarComando() {
  $("cmd-actual").textContent = cmd.rueda >= 0
    ? `rueda ${NOMBRE_RUEDA[cmd.rueda]} adelante`
    : `vx ${cmd.vx} · vy ${cmd.vy} · w ${cmd.w}`;
}

// Suelta joysticks y botones: el robot recibe ceros desde el próximo envío
function soltarTodo() {
  joyMover.soltar();
  joyGirar.soltar();
  cmd = { vx: 0, vy: 0, w: 0, rueda: -1 };
  document.querySelectorAll(".rueda.activo").forEach((b) => b.classList.remove("activo"));
  mostrarComando();
}

// Envío periódico: aunque no se toque nada se mandan ceros como latido.
// Con la pantalla bloqueada u oculta no se envía → el robot activa el failsafe.
setInterval(() => {
  if (document.hidden) return;
  if (cmd.rueda >= 0) {
    enviar(`M ${cmd.rueda} 100 ${velocidadMax()}`);     // prueba de una rueda
  } else {
    enviar(`C ${cmd.vx} ${cmd.vy} ${cmd.w} ${velocidadMax()}`);
  }
}, PERIODO_ENVIO_MS);

setInterval(() => enviar(`P ${Math.round(performance.now())}`), PERIODO_PING_MS);

// Si el robot deja de mandar estado, la conexión está muerta aunque el socket diga lo contrario
setInterval(() => {
  if (ws && ws.readyState === WebSocket.OPEN && ultimoEstado > 0 &&
      performance.now() - ultimoEstado > ESTADO_TIMEOUT_MS) {
    ws.close();
  }
}, 500);

// ------------------------------------------------------------
//  Prueba de ruedas: mover mientras se mantiene presionado
// ------------------------------------------------------------
document.querySelectorAll(".rueda").forEach((boton) => {
  const presionar = (ev) => {
    ev.preventDefault();
    soltarTodo();
    boton.setPointerCapture(ev.pointerId);
    boton.classList.add("activo");
    cmd.rueda = Number(boton.dataset.rueda);
    mostrarComando();
  };
  const soltar = () => {
    if (boton.classList.contains("activo")) soltarTodo();
  };
  boton.addEventListener("pointerdown", presionar);
  boton.addEventListener("pointerup", soltar);
  boton.addEventListener("pointercancel", soltar);
  boton.addEventListener("lostpointercapture", soltar);
});

// ------------------------------------------------------------
//  Otros controles
// ------------------------------------------------------------
$("stop").addEventListener("pointerdown", (ev) => {
  ev.preventDefault();
  soltarTodo();
  enviar("S");
});

$("vel-max").addEventListener("input", (e) => {
  $("vel-valor").textContent = e.target.value + "%";
});

$("recuperar").addEventListener("click", () => {
  controlTomado = false;
  $("aviso-tomado").classList.add("oculto");
  conectar();
});

// Al ocultar la página (cambiar de app, bloquear pantalla) se sueltan los controles
document.addEventListener("visibilitychange", () => {
  if (document.hidden) soltarTodo();
});

conectar();
