// Web UI and first-boot setup page, served straight from flash.
#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>marquee</title><style>
:root{--bg:#0d0f12;--card:#161a1f;--line:#262c34;--text:#e6e9ee;--dim:#8b95a3;--accent:#ff9f43}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.5 ui-sans-serif,system-ui,sans-serif}
main{max-width:620px;margin:0 auto;padding:24px 16px 64px}
h1{font-size:18px;letter-spacing:.14em;text-transform:uppercase;margin:0 0 4px}
.sub{color:var(--dim);font-size:13px;margin-bottom:24px}
section{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin-bottom:14px}
h2{font-size:11px;letter-spacing:.14em;text-transform:uppercase;color:var(--dim);margin:0 0 12px}
label{display:block;font-size:12px;color:var(--dim);margin:10px 0 4px}
input,select,button{font:inherit;color:var(--text);background:#0f1317;border:1px solid var(--line);border-radius:6px;padding:8px 10px}
input[type=color]{padding:2px;height:36px;width:52px}
input[type=range]{width:100%;padding:0;background:none;border:none;accent-color:var(--accent)}
button{cursor:pointer}
button:hover{border-color:var(--accent)}
button.go{background:var(--accent);color:#141414;border-color:var(--accent);font-weight:600}
.row{display:flex;gap:8px;align-items:flex-end;flex-wrap:wrap}
.row>div{flex:1;min-width:76px}
.wide{width:100%}
.chips{display:flex;flex-wrap:wrap;gap:6px}
.chips button{padding:6px 10px;font-size:13px}
#grid{display:grid;grid-template-columns:repeat(30,1fr);gap:1px;background:var(--line);border:1px solid var(--line);border-radius:4px;overflow:hidden;touch-action:none}
#grid div{aspect-ratio:1;background:#000}
#status{color:var(--dim);font-size:12px;margin-top:10px}
</style></head><body><main>
<h1>Marquee</h1><div class="sub" id="sub">connecting</div>

<section><h2>Message</h2>
<input id="text" class="wide" placeholder="Type something" autocomplete="off">
<div class="row" style="margin-top:10px">
  <div style="flex:0 0 52px"><label>Color</label><input type="color" id="color" value="#ff9f43"></div>
  <div><label>Speed</label><input id="speed" type="number" value="45" min="5" max="300"></div>
  <div><label>Repeat</label><input id="repeat" type="number" value="1" min="1" max="99"></div>
  <div style="flex:0 0 auto"><label>&nbsp;</label><button class="go" onclick="say()">Send</button></div>
</div>
<div class="row" style="margin-top:10px">
  <label style="margin:0"><input type="checkbox" id="rainbow"> rainbow</label>
  <label style="margin:0"><input type="checkbox" id="scroll" checked> scroll</label>
</div>
</section>

<section><h2>Animations</h2><div class="chips" id="anims"></div>
<div class="chips" id="stored" style="margin-top:8px"></div></section>

<section><h2>Brightness</h2>
<input type="range" id="bright" min="0" max="255" oninput="send({brightness:+this.value})">
<div class="row" style="margin-top:10px">
  <button onclick="send({clear:true})">Clear queue</button>
  <button onclick="send({skip:true})">Skip</button>
  <button onclick="send({anim:'off',now:true})">Blank</button>
</div></section>

<section><h2>Draw</h2><div id="grid"></div>
<div class="row" style="margin-top:10px">
  <div style="flex:0 0 52px"><input type="color" id="pen" value="#ff2d55"></div>
  <button onclick="wipe()">Erase</button>
  <button onclick="send({clear:true})">Release</button>
</div></section>

<div id="status"></div>
</main><script>
const W=30,H=8,px=new Uint8Array(W*H*3);
let ws,live=false;
const $=id=>document.getElementById(id);

function connect(){
  ws=new WebSocket('ws://'+location.hostname+':81/');
  ws.binaryType='arraybuffer';
  ws.onopen=()=>{live=true;$('sub').textContent=location.hostname};
  ws.onclose=()=>{live=false;$('sub').textContent='reconnecting';setTimeout(connect,2000)};
}
function send(o){
  if(live) ws.send(JSON.stringify(o));
  else fetch('/api/cmd',{method:'POST',body:JSON.stringify(o)});
}
function say(){
  const t=$('text').value.trim(); if(!t) return;
  send({text:t,color:$('rainbow').checked?'rainbow':$('color').value,
        speed:+$('speed').value,repeat:+$('repeat').value,scroll:$('scroll').checked});
  $('text').value='';
}
$('text').addEventListener('keydown',e=>{if(e.key==='Enter')say()});

async function load(){
  const s=await (await fetch('/api/state')).json();
  $('bright').value=s.brightness;
  $('anims').innerHTML='';
  s.animations.forEach(n=>{
    const b=document.createElement('button');
    b.textContent=n; b.onclick=()=>send({anim:n,now:true});
    b.oncontextmenu=e=>{e.preventDefault();send({idle:n})};
    $('anims').appendChild(b);
  });
  $('stored').innerHTML='';
  (s.stored||[]).forEach(n=>{
    const b=document.createElement('button');
    b.textContent='> '+n; b.onclick=()=>send({play:n,now:true,repeat:3});
    $('stored').appendChild(b);
  });
  $('status').textContent=`${s.ip} - ${s.draw_ma} mA of ${s.power_ma} - heap ${s.free_heap} - up ${s.uptime_s}s`;
}

const cells=[];
for(let i=0;i<W*H;i++){
  const d=document.createElement('div');
  d.onpointerdown=e=>{e.preventDefault();paint(i)};
  d.onpointerenter=e=>{if(e.buttons)paint(i)};
  $('grid').appendChild(d); cells.push(d);
}
function paint(i){
  const c=$('pen').value;
  px[i*3]=parseInt(c.substr(1,2),16);
  px[i*3+1]=parseInt(c.substr(3,2),16);
  px[i*3+2]=parseInt(c.substr(5,2),16);
  cells[i].style.background=c;
  push();
}
function wipe(){px.fill(0);cells.forEach(c=>c.style.background='#000');push()}
function push(){if(live)ws.send(px)}

connect(); load(); setInterval(load,5000);
</script></body></html>)HTML";

const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>marquee setup</title><style>
body{margin:0;background:#0d0f12;color:#e6e9ee;font:15px/1.6 ui-sans-serif,system-ui,sans-serif}
form{max-width:380px;margin:12vh auto;padding:24px;background:#161a1f;border:1px solid #262c34;border-radius:10px}
h1{font-size:16px;letter-spacing:.14em;text-transform:uppercase;margin:0 0 18px}
label{display:block;font-size:12px;color:#8b95a3;margin:12px 0 4px}
input{width:100%;padding:9px 10px;background:#0f1317;color:inherit;border:1px solid #262c34;border-radius:6px;font:inherit}
button{margin-top:18px;width:100%;padding:10px;background:#ff9f43;color:#141414;border:0;border-radius:6px;font:inherit;font-weight:600;cursor:pointer}
</style></head><body>
<form method="POST" action="/api/wifi">
<h1>Marquee setup</h1>
<label>Wi-Fi network</label><input name="ssid" autocomplete="off" required>
<label>Password</label><input name="pass" type="password" autocomplete="off">
<label>Hostname</label><input name="host" value="marquee">
<button>Save and restart</button>
</form></body></html>)HTML";
