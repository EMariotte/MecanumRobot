// Página de control — Robot Mecanum 4WD
// Paso 3d: dos joysticks (mover + girar) enviados por WebSocket,
// prueba de ruedas como diagnóstico y estado del robot.
//
// Protocolo (texto):
//   Página → robot   "C vx vy w vmax"   20 veces por segundo (también sirve de latido)
//                    "M i v vmax"       en lugar de "C" mientras se prueba una rueda (i: 0=FL 1=FR 2=RL 3=RR)
//                    "S"                STOP inmediato
//                    "P n"              ping para medir latencia
//   Robot → página   {"up","heap","cli","hz","fs"}  estado, 1 vez/s
//                    "P n"              respuesta al ping
//                    "TOMADO"           otro dispositivo tomó el control

const PERIODO_ENVIO_MS = 50;     // 20 comandos/s — el failsafe del robot es de 300 ms
const PERIODO_PING_MS = 1000;
const REINTENTO_MS = 1000;       // espera antes de reconectar
const ESTADO_TIMEOUT_MS = 2500;  // sin estado del robot en este tiempo = desconectado
const ZONA_MUERTA = 0.12;        // fracción del recorrido del joystick que se ignora (centro)

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
