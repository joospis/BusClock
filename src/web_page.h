// web_page.h - setup page served at / (kept out of the .ino so PlatformIO's
// .ino preprocessor does not misread the embedded JavaScript).
#pragma once
#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>BusClock</title>
<style>
body{font-family:system-ui,-apple-system,sans-serif;background:#111;color:#eee;margin:0 auto;padding:16px;max-width:480px}
h1{font-size:22px;margin:0 0 2px}.sub{color:#999;font-size:13px;margin:0 0 16px}
fieldset{border:1px solid #333;border-radius:12px;margin:0 0 14px;padding:12px 14px}
legend{color:#fbb033;padding:0 6px;font-weight:600}
label{display:block;font-size:13px;color:#bbb;margin:12px 0 4px}
input,select,button{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #444;background:#1c1c1c;color:#eee;font-size:16px}
input[type=range]{padding:0;border:0;background:none}
button{background:#fbb033;color:#111;border:0;font-weight:600;cursor:pointer}
button.sec{background:#2a2a2a;color:#eee}
.row{display:flex;gap:8px;align-items:end}.row>*{flex:1}
.st{font-size:14px;line-height:1.7}.warn{color:#ff6b6b}.ok{color:#5ee27a}
#msg{margin:10px 0;font-size:14px;min-height:20px}
</style></head><body>
<h1>BusClock</h1><p class="sub">Route <span id="route"></span> countdown sign</p>
<fieldset><legend>Status</legend><div class="st" id="status">Loading…</div></fieldset>
<form id="f">
<fieldset><legend>Wi-Fi</legend>
<label for="ssidSel">Network</label>
<div class="row"><select id="ssidSel"><option value="">Scanning…</option></select>
<button type="button" class="sec" id="scan" style="flex:0 0 80px">Scan</button></div>
<input id="ssid" name="ssid" placeholder="Network name" style="margin-top:8px" autocomplete="off">
<label for="pass">Password</label>
<input id="pass" name="pass" type="password" placeholder="Leave blank to keep the saved one">
</fieldset>
<fieldset><legend>Bus stop</legend>
<label for="stop">Your stop and direction</label><select id="stop" name="stop"></select>
<p class="sub" id="stopNote" style="margin:4px 0 0"></p>
<label for="walk">Walk to the stop: <b id="walkV"></b> min</label>
<input type="range" id="walk" name="walk" min="0" max="20">
<p class="sub" style="margin:4px 0 0">The countdown turns amber when it's time to leave and red when you'd have to run.</p>
</fieldset>
<fieldset><legend>Display</legend>
<label for="bri">Brightness: <b id="briV"></b></label><input type="range" id="bri" name="bri" min="1" max="255">
<label for="nbri">Night brightness: <b id="nbriV"></b></label><input type="range" id="nbri" name="nbri" min="1" max="255">
<div class="row"><div><label for="nstart">Night starts</label><select id="nstart" name="nstart"></select></div>
<div><label for="nend">Night ends</label><select id="nend" name="nend"></select></div></div>
<label for="driver">Panel driver chip (try another if the panel shows noise)</label>
<select id="driver" name="driver"><option value="0">Standard shift register (most panels)</option>
<option value="1">FM6126A</option><option value="2">ICN2038S</option></select>
</fieldset>
<button type="submit">Save</button>
</form>
<div id="msg"></div>
<button class="sec" id="forget">Forget Wi-Fi and reopen setup</button>
<script>
const $=id=>document.getElementById(id);
const esc=t=>String(t).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
function opt(sel,v,t){const o=document.createElement('option');o.value=v;o.textContent=t;sel.appendChild(o);}
for(const s of ['nstart','nend'])for(let h=0;h<24;h++)opt($(s),h,((h%12)||12)+(h<12?' AM':' PM'));
for(const id of ['walk','bri','nbri'])$(id).oninput=()=>$(id+'V').textContent=$(id).value;
let stopsShown=false;
function fillStops(c,sel){
  if(c.stops.length)stopsShown=true;
  $('stop').innerHTML='';
  opt($('stop'),'','Choose your stop…');
  for(const s of c.stops)opt($('stop'),s.key,s.name+' → '+s.toward);
  if(sel&&!c.stops.some(s=>s.key==sel))opt($('stop'),sel,sel==c.stop?(c.stopLabel||sel):sel);
  $('stop').value=sel;
  $('stopNote').textContent=c.stops.length?'':'The stop list loads from the PVTA bus tracker once the clock is on your Wi-Fi.';
}
async function reloadStops(){
  const c=await (await fetch('/api/config')).json();
  fillStops(c,$('stop').value);   // keep whatever is selected now
}
async function loadConfig(){
  const c=await (await fetch('/api/config')).json();
  $('route').textContent=c.route;
  fillStops(c,c.stop);
  $('ssid').value=c.ssid;
  for(const k of ['walk','bri','nbri','nstart','nend','driver'])$(k).value=c[k];
  for(const id of ['walk','bri','nbri'])$(id+'V').textContent=$(id).value;
}
async function scan(){
  $('ssidSel').innerHTML='<option value="">Scanning…</option>';
  try{
    const n=await (await fetch('/api/scan')).json();
    $('ssidSel').innerHTML='';opt($('ssidSel'),'',n.length?'Choose a network…':'No networks found');
    for(const w of n)opt($('ssidSel'),w.ssid,w.ssid+'  ('+w.rssi+' dBm'+(w.open?', open':'')+')');
  }catch(e){$('ssidSel').innerHTML='<option value="">Scan failed</option>';}
}
$('ssidSel').onchange=()=>{if($('ssidSel').value){$('ssid').value=$('ssidSel').value;$('pass').focus();}};
$('scan').onclick=scan;
async function status(){
  try{
    const s=await (await fetch('/api/status')).json();
    let h='Wi-Fi: '+(s.connected?'<span class=ok>connected to '+esc(s.ssid)+'</span> ('+s.ip+')':'<span class=warn>not connected</span>')+'<br>';
    h+='Time: '+(s.time||'not set yet')+'<br>';
    h+='Stop: '+(s.stop?esc(s.stop):'<span class=warn>not chosen</span>')+'<br>';
    if(s.stop)h+='Next buses: '+(s.next.length?s.next.map(m=>m+' min').join(', '):'none in the next few days')+'<br>';
    if(s.stop){
      const age=s.liveAge<0?'':s.liveAge<120?s.liveAge+' s ago':Math.round(s.liveAge/60)+' min ago';
      h+='Bus tracker: '+(s.liveAge<0?'<span class=warn>no data yet</span>':s.liveAge<=s.maxAge?'<span class=ok>updated '+age+'</span>':'<span class=warn>last update '+age+'</span>');
      if(s.liveError)h+=' <span class=warn>('+esc(s.liveError)+')</span>';
    }
    $('status').innerHTML=h;
    if(s.stopsLoaded&&!stopsShown){stopsShown=true;reloadStops();}
  }catch(e){}
}
$('f').onsubmit=async e=>{
  e.preventDefault();
  $('msg').textContent='Saving…';
  try{const r=await fetch('/save',{method:'POST',body:new URLSearchParams(new FormData($('f')))});$('msg').textContent=await r.text();}
  catch(err){$('msg').textContent='Saved. The clock is restarting.';}
  $('pass').value='';setTimeout(status,1500);
};
$('forget').onclick=async()=>{
  if(!confirm('Forget the saved Wi-Fi network and restart in setup mode?'))return;
  try{await fetch('/forget',{method:'POST'});}catch(e){}
  $('msg').textContent='Restarting. Join the "BusClock" Wi-Fi network to set it up again.';
};
loadConfig().then(()=>{status();scan();});
setInterval(status,10000);
</script></body></html>)HTML";
