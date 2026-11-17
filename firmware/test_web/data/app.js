// Página de control — Robot Mecanum 4WD
// Paso 3a: solo consulta /estado para verificar la conexión con el robot.

const PERIODO_ESTADO_MS = 2000;   // cada cuánto se pregunta el estado
const TIMEOUT_MS = 1500;          // sin respuesta en este tiempo = desconectado

const $ = (id) => document.getElementById(id);

function marcarConexion(conectado) {
  const el = $("conexion");
  el.textContent = conectado ? "Conectado" : "Sin conexión";
  el.className = "indicador " + (conectado ? "conectado" : "desconectado");
}

function formatoTiempo(seg) {
  const m = Math.floor(seg / 60);
  const s = seg % 60;
  return m > 0 ? `${m} min ${s} s` : `${s} s`;
}

async function consultarEstado() {
  const control = new AbortController();
  const timer = setTimeout(() => control.abort(), TIMEOUT_MS);
  try {
    const resp = await fetch("/estado", { signal: control.signal, cache: "no-store" });
    const datos = await resp.json();
    $("uptime").textContent = formatoTiempo(datos.uptime);
    $("heap").textContent = (datos.heap / 1024).toFixed(1) + " kB";
    $("clientes").textContent = datos.clientes;
    marcarConexion(true);
  } catch (e) {
    marcarConexion(false);
  } finally {
    clearTimeout(timer);
  }
}

// Barra de velocidad: solo actualiza el texto (se usará en 3c)
$("vel-max").addEventListener("input", (e) => {
  $("vel-valor").textContent = e.target.value + "%";
});

consultarEstado();
setInterval(consultarEstado, PERIODO_ESTADO_MS);
