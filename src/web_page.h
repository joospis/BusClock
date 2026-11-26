// web_page.h - the setup page served at / (HTML, CSS and JS in one string).
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
.badge{display:inline-block;min-width:28px;padding:2px 6px;border-radius:4px;font-weight:700;text-align:center;font-size:14px}
</style></head><body>
<h1>BusClock</h1><p class="sub"><span class="badge" id="badge"></span> countdown sign</p>
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
<label for="routeSel">Route</label><select id="routeSel"></select>
<label for="stop">Your stop and direction</label><select id="stop" name="stop"></select>
<p class="sub" id="stopNote" style="margin:4px 0 0"></p>
<input type="hidden" id="route" name="route"><input type="hidden" id="rname" name="rname">
<input type="hidden" id="rcolor" name="rcolor">
<input type="hidden" id="stopname" name="stopname">
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
// Route and stop lists come straight from PVTA's bus tracker (it allows
// browser requests), so they only load while this phone is online.
const PVTA='https://bustracker.pvta.com/InfoPoint/rest/';
let routes=[];
// Like the panel: the number is cut out (dark) unless the badge is too dark to show it.
function showBadge(name,bg){const b=$('badge'),v=parseInt(bg,16)||0,max=Math.max(v>>16,(v>>8)&255,v&255);b.textContent=name;b.style.background='#'+bg;b.style.color=max<64?'#ddd':'#111';}
function setRoute(id,name,bg){$('route').value=id;$('rname').value=name;$('rcolor').value=bg;showBadge(name,bg);}
const offlineNote='Route and stop lists load from the PVTA bus tracker while this phone is online. Your saved choice is kept.';
async function getJson(path){const r=await fetch(PVTA+path);if(!r.ok)throw new Error('HTTP '+r.status);return r.json();}
// RouteDetails -> stops in route order, minus each direction's last stop (buses end there)
function routeStops(d){
  const names=new Map((d.Stops||[]).map(s=>[s.StopId,s.Name||'']));
  const rs=(d.RouteStops||[]).filter(r=>/^[A-Z]{1,4}$/.test(r.Direction||'')).sort((a,b)=>a.SortOrder-b.SortOrder);
  const toward={},last={};
  rs.forEach((r,i)=>{toward[r.Direction]=names.get(r.StopId);last[r.Direction]=i;});
  return rs.filter((r,i)=>i!==last[r.Direction]&&names.get(r.StopId)).map(r=>({key:r.StopId+':'+r.Direction,name:names.get(r.StopId),toward:toward[r.Direction]}));
}
async function loadStops(routeId,sel,selName){
  $('stop').innerHTML='';opt($('stop'),'','Loading stops…');
  try{
    const stops=routeStops(await getJson('RouteDetails/Get/'+routeId));
    $('stop').innerHTML='';opt($('stop'),'','Choose your stop…');
    for(const s of stops)opt($('stop'),s.key,s.name+' → '+s.toward);
    $('stop').value=stops.some(s=>s.key==sel)?sel:'';
    $('stopNote').textContent='';
  }catch(e){
    $('stop').innerHTML='';
    if(sel)opt($('stop'),sel,selName||sel);else opt($('stop'),'','Choose your stop…');
    $('stopNote').textContent=offlineNote;
  }
  stopChanged();
}
function stopChanged(){const o=$('stop').selectedOptions[0];$('stopname').value=o&&o.value?o.textContent.replace(' → ',' toward '):'';}
$('stop').onchange=stopChanged;
$('routeSel').onchange=()=>{
  const r=routes.find(x=>String(x.RouteId)==$('routeSel').value);
  if(!r)return;
  setRoute(r.RouteId,r.ShortName,r.Color);
  loadStops(r.RouteId,'','');
};
async function loadRoutes(c){
  const sel=$('routeSel');
  sel.innerHTML='';opt(sel,c.route,c.routeName+' (loading routes…)');
  try{
    routes=(await getJson('Routes/GetVisibleRoutes')).filter(r=>r.IsVisible!==false).sort((a,b)=>a.SortOrder-b.SortOrder);
    sel.innerHTML='';
    for(const r of routes)opt(sel,r.RouteId,r.ShortName+' · '+r.LongName);
    if(!routes.some(r=>r.RouteId==c.route))opt(sel,c.route,c.routeName);
    sel.value=c.route;
  }catch(e){
    sel.innerHTML='';opt(sel,c.route,c.routeName);
  }
  await loadStops(c.route,c.stop,c.stopName);
}
async function loadConfig(){
  const c=await (await fetch('/api/config')).json();
  setRoute(c.route,c.routeName,c.routeColor);
  $('ssid').value=c.ssid;
  for(const k of ['walk','bri','nbri','nstart','nend','driver'])$(k).value=c[k];
  for(const id of ['walk','bri','nbri'])$(id+'V').textContent=$(id).value;
  loadRoutes(c);
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
    h+='Stop: '+(s.stop?'Route '+esc(s.route)+', '+esc(s.stop):'<span class=warn>not chosen</span>')+'<br>';
    if(s.stop)h+='Next buses: '+(s.next.length?s.next.map(m=>m+' min').join(', '):'none in the next few days')+'<br>';
    if(s.stop){
      const age=s.liveAge<0?'':s.liveAge<120?s.liveAge+' s ago':Math.round(s.liveAge/60)+' min ago';
      h+='Bus tracker: '+(s.liveAge<0?'<span class=warn>no data yet</span>':s.liveAge<=s.maxAge?'<span class=ok>updated '+age+'</span>':'<span class=warn>last update '+age+'</span>');
      if(s.liveError)h+=' <span class=warn>('+esc(s.liveError)+')</span>';
    }
    $('status').innerHTML=h;
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
