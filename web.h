#ifndef WEB_H
#define WEB_H

#include <Arduino.h>
#include <WebServer.h>

// Este archivo se incluye DESPUES de apwifieeprommode.h (usa su objeto "server").

#define WEB_MUESTRAS 200   // 2 s de senal a 100 Hz
#define WEB_BINES 40       // bins 1..40 = 0.39 .. 15.6 Hz

// Datos que el sensor (nucleo 1) publica y la web (nucleo 0) lee
struct DatosWeb {
    uint8_t estado;                    // 0 inicio, 1 reposo, 2 movimiento, 3 alerta, 4 error
    uint32_t ventana;                  // contador de ventanas evaluadas
    float frec, rms, ratio, movLento, movProm;
    uint8_t compat;
    float uRms, uRatio, uMov;          // umbrales (para dibujarlos)
    uint8_t uN;
    int16_t espectro[WEB_BINES];       // mg
    int16_t m[3][WEB_MUESTRAS];        // mg, ejes X, Y, Z
    int cabeza;
};

DatosWeb dw;                           // variable global (inicia en cero)
portMUX_TYPE webMux = portMUX_INITIALIZER_UNLOCKED;

// Guarda una muestra (en g) en el buffer circular de la senal en vivo
void webPushMuestra(float x, float y, float z) {
    int c = dw.cabeza;
    dw.m[0][c] = (int16_t)(x * 1000.0f);
    dw.m[1][c] = (int16_t)(y * 1000.0f);
    dw.m[2][c] = (int16_t)(z * 1000.0f);
    dw.cabeza = (c + 1) % WEB_MUESTRAS;
}

static const char PAGINA[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Monitor de temblor</title>
<style>
:root{color-scheme:light dark;--ok:#1d9e75;--mov:#ef9f27;--alerta:#e24b4a;--gris:#888}
*{box-sizing:border-box}
body{font-family:system-ui,sans-serif;margin:0 auto;padding:12px;max-width:760px}
h1{font-size:18px;margin:0 0 10px}
.estado{display:flex;align-items:center;gap:12px;padding:10px 12px;border-radius:10px;border:1px solid rgba(128,128,128,.35);margin-bottom:10px}
.punto{width:18px;height:18px;border-radius:50%;background:var(--gris);flex:none}
.punto.alerta{animation:parp .4s infinite alternate}
@keyframes parp{to{opacity:.2}}
.sub{font-size:12px;opacity:.7}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(110px,1fr));gap:8px;margin-bottom:10px}
.m{border:1px solid rgba(128,128,128,.35);border-radius:10px;padding:8px 10px}
.m span{font-size:12px;opacity:.7}
.m b{display:block;font-size:20px;font-weight:600}
.card{border:1px solid rgba(128,128,128,.35);border-radius:10px;padding:8px 10px;margin-bottom:10px}
.card h2{font-size:13px;font-weight:600;margin:0 0 6px;opacity:.85}
canvas{width:100%;height:150px;display:block}
.ley{font-size:12px;opacity:.8;margin-top:4px}
.ley i{display:inline-block;width:10px;height:10px;border-radius:2px;margin:0 4px 0 10px;vertical-align:middle}
.ley i:first-child{margin-left:0}
a{color:inherit;font-size:13px}
</style></head><body>
<h1>Monitor de temblor</h1>
<div class="estado"><div id="punto" class="punto"></div>
<div><b id="txt">Conectando...</b><div class="sub" id="sub"></div></div></div>
<div class="grid">
<div class="m"><span>Frecuencia</span><b id="vf">-</b></div>
<div class="m"><span>RMS en banda</span><b id="vr">-</b></div>
<div class="m"><span>Razón de energía</span><b id="vq">-</b></div>
<div class="m"><span>Mov. lento (máx.)</span><b id="vm">-</b></div>
<div class="m"><span>Ventanas compat.</span><b id="vc">-</b></div>
</div>
<div class="card"><h2>Señal en vivo (últimos 2 s, sin gravedad, en g)</h2><canvas id="c1"></canvas>
<div class="ley"><i style="background:#e24b4a"></i>X<i style="background:#1d9e75"></i>Y<i style="background:#378add"></i>Z</div></div>
<div class="card"><h2>Espectro de la última ventana (g, 3 ejes combinados)</h2><canvas id="c2"></canvas>
<div class="ley"><i style="background:rgba(29,158,117,.4)"></i>banda de temblor 3.5–7.5 Hz</div></div>
<div class="card"><h2>RMS en banda por ventana (g)</h2><canvas id="c3"></canvas>
<div class="ley">Línea roja punteada: umbral de detección</div></div>
<div class="card"><h2>Razón de energía en banda</h2><canvas id="c4"></canvas>
<div class="ley">Línea roja punteada: umbral de detección</div></div>
<p><a href="/config">Configurar WiFi</a></p>
<script>
const $=id=>document.getElementById(id);
const ST=[["Iniciando sensor","gris"],["Reposo","ok"],["Movimiento","mov"],["Posible temblor","alerta"],["Error del sensor","alerta"]];
const COL=['#e24b4a','#1d9e75','#378add'];
let ultV=-1,hR=[],hQ=[];
const tx=()=>getComputedStyle(document.body).color;
function prep(id){const c=$(id),r=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;
 if(c.width!==Math.round(w*r)){c.width=Math.round(w*r);c.height=Math.round(h*r)}
 const x=c.getContext('2d');x.setTransform(r,0,0,r,0,0);x.clearRect(0,0,w,h);x.font='11px system-ui';return[x,w,h]}
function marco(x,L,T,W,H,y0,y1,fmt){x.strokeStyle='rgba(128,128,128,.3)';x.lineWidth=1;x.fillStyle=tx();x.textAlign='right';
 for(let i=0;i<=2;i++){const py=T+H-H*i/2;x.beginPath();x.moveTo(L,py);x.lineTo(L+W,py);x.stroke();x.fillText(fmt(y0+(y1-y0)*i/2),L-4,py+4)}}
function dibSenal(d){const[x,w,h]=prep('c1'),L=44,T=6,W=w-L-6,H=h-T-6;
 const s=[d.x,d.y,d.z].map(a=>{const m=a.reduce((p,q)=>p+q,0)/a.length;return a.map(v=>(v-m)/1000)});
 let mx=0.02;s.forEach(a=>a.forEach(v=>{mx=Math.max(mx,Math.abs(v))}));mx*=1.1;
 marco(x,L,T,W,H,-mx,mx,v=>v.toFixed(3));
 s.forEach((a,k)=>{x.strokeStyle=COL[k];x.lineWidth=1.3;x.beginPath();
  a.forEach((v,i)=>{const px=L+W*i/(a.length-1),py=T+H/2-H/2*v/mx;i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()})}
function dibEsp(d){const[x,w,h]=prep('c2'),L=44,T=6,W=w-L-6,H=h-T-18;
 const sp=d.sp.map(v=>v/1000),n=sp.length,df=0.390625,fm=(n+0.5)*df;
 let mx=0.02;sp.forEach(v=>{mx=Math.max(mx,v)});mx*=1.15;
 const bx=f=>L+W*f/fm;
 x.fillStyle='rgba(29,158,117,.16)';x.fillRect(bx(3.5),T,bx(7.5)-bx(3.5),H);
 marco(x,L,T,W,H,0,mx,v=>v.toFixed(3));
 x.fillStyle='#378add';const bw=Math.max(1,W/n-1);
 sp.forEach((v,i)=>{const bh=H*v/mx;x.fillRect(bx((i+1)*df)-bw/2,T+H-bh,bw,bh)});
 x.fillStyle=tx();x.textAlign='center';[0,5,10,15].forEach(f=>x.fillText(f+' Hz',bx(f),h-4))}
function dibHist(id,a,umb,piso,col,dec){const[x,w,h]=prep(id),L=44,T=6,W=w-L-6,H=h-T-6,N=60;
 let mx=Math.max(piso,umb*1.5);a.forEach(v=>{mx=Math.max(mx,v)});mx*=1.1;
 marco(x,L,T,W,H,0,mx,v=>v.toFixed(dec));
 const pu=T+H-H*umb/mx;x.setLineDash([5,4]);x.strokeStyle='#e24b4a';x.beginPath();x.moveTo(L,pu);x.lineTo(L+W,pu);x.stroke();x.setLineDash([]);
 x.strokeStyle=col;x.lineWidth=1.6;x.beginPath();
 a.forEach((v,i)=>{const px=L+W-W*(a.length-1-i)/(N-1),py=T+H-H*v/mx;i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()}
async function tick(){
 try{
  const r=await fetch('/datos',{cache:'no-store'}),d=await r.json(),s=ST[d.e]||ST[0];
  $('punto').className='punto'+(d.e==3?' alerta':'');
  $('punto').style.background='var(--'+s[1]+')';
  $('txt').textContent=s[0];
  $('sub').textContent=d.e==3?'Patrón compatible con temblor de 3.5–7.5 Hz':'Ventanas compatibles: '+d.c+' de '+d.u.n;
  $('vf').textContent=d.f.toFixed(2)+' Hz';
  $('vr').textContent=d.rms.toFixed(4)+' g';
  $('vq').textContent=d.ra.toFixed(2);
  $('vm').textContent=d.ml.toFixed(3)+' g';
  $('vc').textContent=d.c+' / '+d.u.n;
  if(d.v>0&&d.v!==ultV){ultV=d.v;hR.push(d.rms);hQ.push(d.ra);if(hR.length>60){hR.shift();hQ.shift()}}
  dibSenal(d);dibEsp(d);dibHist('c3',hR,d.u.rms,0.05,'#378add',3);dibHist('c4',hQ,d.u.ra,1,'#1d9e75',2);
 }catch(e){$('txt').textContent='Sin conexión con el dispositivo';$('punto').className='punto';$('punto').style.background='var(--gris)'}
 setTimeout(tick,300)}
tick();
</script></body></html>)rawliteral";

void handlePagina() {
    server.send_P(200, "text/html; charset=utf-8", PAGINA);
}

void handleDatos() {

    DatosWeb c;
    portENTER_CRITICAL(&webMux);
    c = dw;
    portEXIT_CRITICAL(&webMux);

    String j;
    j.reserve(4200);

    j += "{\"e\":" + String((int)c.estado);
    j += ",\"v\":" + String((unsigned long)c.ventana);
    j += ",\"f\":" + String(c.frec, 2);
    j += ",\"rms\":" + String(c.rms, 4);
    j += ",\"ra\":" + String(c.ratio, 3);
    j += ",\"ml\":" + String(c.movLento, 3);
    j += ",\"mp\":" + String(c.movProm, 4);
    j += ",\"c\":" + String((int)c.compat);
    j += ",\"u\":{\"rms\":" + String(c.uRms, 4);
    j += ",\"ra\":" + String(c.uRatio, 2);
    j += ",\"ml\":" + String(c.uMov, 3);
    j += ",\"n\":" + String((int)c.uN) + "}";

    j += ",\"sp\":[";
    for (int i = 0; i < WEB_BINES; i++) {
        if (i) j += ',';
        j += String(c.espectro[i]);
    }
    j += "]";

    const char *claves[3] = {"x", "y", "z"};
    for (int k = 0; k < 3; k++) {
        j += ",\"";
        j += claves[k];
        j += "\":[";
        for (int i = 0; i < WEB_MUESTRAS; i++) {
            if (i) j += ',';
            j += String(c.m[k][(c.cabeza + i) % WEB_MUESTRAS]);   // orden cronologico
        }
        j += "]";
    }
    j += "}";

    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", j);
}

// Se pasa a initAP() para registrar las rutas antes de server.begin()
void registrarRutasWeb() {
    server.on("/", handlePagina);
    server.on("/datos", handleDatos);
}

#endif
