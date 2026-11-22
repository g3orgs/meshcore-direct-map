#pragma once

namespace mdm {

inline constexpr char kMapPage[] = R"MAP(<!doctype html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MeshCore Direct Map</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<style>
html,body,#map{height:100%;margin:0}body{font:14px system-ui,sans-serif;color:#171b20}
#bar{position:absolute;z-index:1000;top:12px;left:12px;max-width:400px;background:#fffef8;
 padding:13px 15px;border-radius:9px;box-shadow:0 2px 15px #0004;line-height:1.45}
#bar strong{font-size:17px}#mode{display:inline-block;margin:6px 0;padding:3px 8px;border-radius:4px;
 background:#fff0c2;color:#593500;font-weight:700}#mode.real{background:#dcefdc;color:#145522}
#mode.archive{background:#dce9fa;color:#173d73}
#status{margin:7px 0}#hint,#session{font-size:12px;color:#4d5660}
#telemetry{font-weight:600;margin:5px 0}#session{word-break:break-word}
button{margin:8px 0 0;border:1px solid #80909e;background:#f6f8fb;border-radius:5px;padding:5px 8px;cursor:pointer}
.filepick{display:block;margin-top:10px;font-size:12px}input[type=file]{display:block;max-width:100%;margin-top:3px}
#scale{height:9px;margin:9px 0 3px;background:linear-gradient(90deg,#263aeb,#31adba,#decc3f,#ec5236);border-radius:3px}
#ends{display:flex;justify-content:space-between;font-size:12px}
@media(max-width:550px){#bar{max-width:calc(100vw - 55px);font-size:12px;padding:9px}#bar strong{font-size:14px}}
</style></head><body><div id="map"></div><div id="bar">
<strong>MeshCore Direct Map</strong><br><span id="mode">Verbinde …</span>
<div id="telemetry">GPS-Fix: unbekannt · Sitzung: 0 Einträge</div>
<div id="status">Warte auf Messpunkte …</div>
<div id="session">Aktuelle Sitzung: noch keine Messdatei</div>
<div id="hint">Nur 0-Hop-Gruppenpakete des eingestellten Kanal-Hashs. Ein leerer Pfad ist kein Beweis für die Identität des Senders. Eine feste Position markiert immer denselben Empfangsort.</div>
<div id="scale"></div><div id="ends"><span id="weak">schwächer</span><span id="strong">stärker</span></div>
<button id="zoom" type="button">Auf Punkte zoomen</button>
<button id="reset" type="button" hidden>Simulation zurücksetzen</button>
<button id="start" type="button" hidden>Simulation neu starten</button>
<label class="filepick">Gespeicherte Messdatei auf der Karte anzeigen
 <input id="csvfile" type="file" accept=".csv,text/csv"></label>
<button id="live" type="button" hidden>Zur Live-Karte</button>
</div>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<script>
if (!window.L) document.getElementById('status').textContent='Kartenbibliothek nicht erreichbar. Internetverbindung prüfen.';
else {
 const map=L.map('map',{preferCanvas:true}).setView([51.34,12.37],11);
 L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',
  {maxZoom:19,attribution:'© OpenStreetMap contributors'}).addTo(map);
 const marks=L.layerGroup().addTo(map);
 let lastKey='', lastSource='', bounds=[], archive=null, archiveRevision=0, lastData=null;
 const color=(v,level)=>{
   const lo=level?-105:-20, hi=level?-25:30;
   const t=Math.max(0,Math.min(1,(v-lo)/(hi-lo)));
   return `hsl(${240*(1-t)} 80% 48%)`;
 };
 const csvNumber=cell=>{
   const value=(cell??'').trim();
   if(!value) return null;
   const n=Number(value);
   return Number.isFinite(n)?n:null;
 };
 function parseCsv(text){
   const lines=text.replace(/^\uFEFF/,'').split(/\r?\n/);
   const columns=(lines.shift()||'').trim().split(',');
   const col=name=>columns.indexOf(name);
   const latColumn=col('receiver_lat')>=0?'receiver_lat':'gps_lat';
   const lonColumn=col('receiver_lon')>=0?'receiver_lon':'gps_lon';
   if([latColumn,lonColumn,'snr_db','level_dbfs','crc_present','crc_ok'].some(name=>col(name)<0))
     throw Error('Keine Messdatei dieses Plugins oder ungültiger CSV-Kopf.');
   const points=[]; let entries=0;
   for(const line of lines){
     if(!line.trim()) continue;
     const v=line.split(',');
     if(v.length<columns.length) continue;
     entries++;
     const lat=csvNumber(v[col(latColumn)]), lon=csvNumber(v[col(lonColumn)]);
     if(lat===null||lon===null||Math.abs(lat)>90||Math.abs(lon)>180) continue;
     const level=csvNumber(v[col('level_dbfs')]), snr=csvNumber(v[col('snr_db')]);
     if(level===null&&snr===null) continue;
     points.push({lat,lon,value:level===null?snr:level,metric:level===null?'SNR dB':'dBFS',
       crc_verified:v[col('crc_present')]==='1'&&v[col('crc_ok')]==='1',
       position_source:col('position_source')>=0?v[col('position_source')]:'legacy'});
   }
   return {entries,points};
 }
 function draw(data){
   const sim=!!data.simulation, source=archive?'archive':sim?'sim':'live';
   const pts=archive?archive.points:data.points;
   const label=document.getElementById('mode');
   label.textContent=source==='archive'?'ARCHIV – gespeicherte Messung':
     sim?'SIMULATION – erfundene Daten':'LIVE – echte Empfangsdaten';
   label.className=source==='archive'?'archive':source==='live'?'real':'';
   const position=data.position_mode==='onair'?'OnAir fest':
     data.position_mode==='manual'?'manuell fest':'GPS';
   document.getElementById('telemetry').textContent=
     `GPS-Fix: ${data.gps_fix?'ja':'nein'} · Position: ${position} ${data.position_ready?'bereit':'fehlt'} · aktuelle Sitzung: ${data.entries} Einträge, ${data.mapped} Kartenpunkte`;
   document.getElementById('session').textContent=
     `Aktuelle Sitzung: ${data.session||'noch keine Messdatei'}`;
   document.getElementById('reset').hidden=source!=='sim';
   document.getElementById('start').hidden=source!=='sim'||!data.paused;
   document.getElementById('live').hidden=source!=='archive';
   const level=pts.some(p=>p.metric==='dBFS');
   const shown=pts.filter(p=>p.metric===(level?'dBFS':'SNR dB'));
   const hidden=pts.length-shown.length;
   document.getElementById('status').textContent=source==='archive'
     ? `${archive.entries} Einträge aus ${archive.name} · ${shown.length} Kartenpunkte${hidden?` · ${hidden} mit anderer Metrik ausgeblendet`:''}`
     : shown.length
       ? `${shown.length} Kartenpunkte · ${level?'Empfangspegel (dBFS)':'SNR als vorläufige Metrik'}`
       : sim?(data.paused?'Simulation zurückgesetzt – keine Testpunkte.':'Simulation startet – erster Testpunkt erscheint gleich.')
            :'Noch keine 0-Hop-Pakete mit gültiger Empfängerposition.';
   document.getElementById('weak').textContent=level?'−105 dBFS':'−20 dB SNR';
   document.getElementById('strong').textContent=level?'−25 dBFS':'+30 dB SNR';
   const key=`${source}:${source==='archive'?archiveRevision:data.generation}:${shown.length}`;
   if(key===lastKey) return;
   const sourceChanged=source!==lastSource;
   lastKey=key; lastSource=source; marks.clearLayers(); bounds=[];
   for(const p of shown){
     const point=[p.lat,p.lon]; bounds.push(point);
     const location=p.position_source==='onair'?'feste OnAir-Position':
       p.position_source==='manual'?'feste manuelle Position':
       p.position_source==='gps'?'GPS-Position':'Empfängerposition (ältere Datei)';
     const note=source==='sim'?'Simulation':
       `${location}<br>${p.crc_verified?'CRC geprüft':'CRC-Status unbekannt'}`;
     L.circleMarker(point,{radius:11,color:color(p.value,level),fillColor:color(p.value,level),
       weight:2,fillOpacity:.65}).bindPopup(`${p.value.toFixed(1)} ${p.metric}<br>${note}`)
       .addTo(marks);
   }
   if(source==='archive'&&bounds.length)
     map.fitBounds(bounds,{padding:[45,45],maxZoom:15});
   else if(source==='sim'&&sourceChanged)
     map.fitBounds([[51.324,12.356],[51.350,12.395]],{padding:[35,35],maxZoom:13});
   else if(source==='live'&&sourceChanged&&bounds.length)
     map.fitBounds(bounds,{padding:[45,45],maxZoom:14});
 }
 async function update(){
   try {
     const response=await fetch('/api/points',{cache:'no-store'});
     if(!response.ok) throw Error('HTTP '+response.status);
     lastData=await response.json();
     draw(lastData);
   } catch(e){
     if(archive) draw(lastData||{simulation:false,gps_fix:false,entries:0,mapped:0,session:'',points:[],generation:0});
     else document.getElementById('status').textContent='Verbindung zum Plugin unterbrochen.';
   }
 }
 document.getElementById('zoom').onclick=()=>{if(bounds.length)map.fitBounds(bounds,{padding:[45,45],maxZoom:15});};
 document.getElementById('reset').onclick=async()=>{
   await fetch('/api/reset',{method:'POST',headers:{'X-MDM-Action':'reset'}});
   await update();
 };
 document.getElementById('start').onclick=async()=>{
   await fetch('/api/start',{method:'POST',headers:{'X-MDM-Action':'start'}});
   await update();
 };
 document.getElementById('csvfile').onchange=async event=>{
   const file=event.target.files[0];
   if(!file) return;
   try {
     if(file.size>20*1024*1024) throw Error('Datei größer als 20 MB.');
     const parsed=parseCsv(await file.text());
     archive={name:file.name,...parsed};
     archiveRevision++;
     draw(lastData||{simulation:false,gps_fix:false,entries:0,mapped:0,session:'',points:[],generation:0});
   } catch(e){document.getElementById('status').textContent=`CSV konnte nicht geladen werden: ${e.message}`;}
 };
 document.getElementById('live').onclick=()=>{
   archive=null;
   document.getElementById('csvfile').value='';
   update();
 };
 update(); setInterval(update,1500);
}
</script></body></html>)MAP";

} // namespace mdm
