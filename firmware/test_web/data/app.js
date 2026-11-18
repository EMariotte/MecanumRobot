// Página de control — Robot Mecanum 4WD
// Paso 3c: envía comandos por WebSocket (movimientos y prueba de ruedas)
// y muestra el estado del robot.
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

const $ = (id) => document.getElementById(id);

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
//  Comandos
// ------------------------------------------------------------
function velocidadMax() {
  return Number($("vel-max").value);
}

const NOMBRE_RUEDA = ["FL", "FR", "RL", "RR"];

function mostrarComando() {
  $("cmd-actual").textContent = cmd.rueda >= 0
    ? `rueda ${NOMBRE_RUEDA[cmd.rueda]} adelante`
    : `vx ${cmd.vx} · vy ${cmd.vy} · w ${cmd.w}`;
}

function soltarTodo() {
  cmd = { vx: 0, vy: 0, w: 0, rueda: -1 };
  document.querySelectorAll(".dir.activo").forEach((b) => b.classList.remove("activo"));
  mostrarComando();
}

// Envío periódico: aunque no se toque nada se mandan ceros como latido.
// Con la pantalla bloqueada u oculta no se envía → el robot activa el failsafe.
setInterval(() => {
  if (document.hidden) return;
  if (cmd.rueda >= 0) {
    enviar(`M ${cmd.rueda} ${cmd.vy} ${velocidadMax()}`);     // prueba de una rueda
  } else {
    enviar(`C ${cmd.vx} ${cmd.vy} ${cmd.w} ${velocidadMax()}`);
  }
}, PERIODO_ENVIO_MS);

setInterval(() => enviar(`P ${Math.round(performance.now())}`), PERIODO_PING_MS);

// Si el robot deja de mandar estado, la conexión está muerta aunque el socket diga lo contrario
setInterval(() => {
  if (ws && ws.readyState === WebSocket.OPEN && performance.now() - ultimoEstado > ESTADO_TIMEOUT_MS && ultimoEstado > 0) {
    ws.close();
  }
}, 500);

// ------------------------------------------------------------
//  Botones de dirección: mover mientras se mantienen presionados
// ------------------------------------------------------------
document.querySelectorAll(".dir").forEach((boton) => {
  const presionar = (ev) => {
    ev.preventDefault();
    soltarTodo();
    boton.setPointerCapture(ev.pointerId);
    boton.classList.add("activo");
    if (boton.dataset.rueda !== undefined) {
      // Prueba de rueda: solo esa rueda, hacia adelante
      cmd = { vx: 0, vy: 100, w: 0, rueda: Number(boton.dataset.rueda) };
    } else {
      cmd = {
        vx: Number(boton.dataset.vx || 0),
        vy: Number(boton.dataset.vy || 0),
        w: Number(boton.dataset.w || 0),
        rueda: -1,
      };
    }
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
