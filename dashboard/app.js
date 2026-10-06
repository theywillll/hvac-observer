'use strict';
const $=id=>document.getElementById(id);
const fmt=(v,n=1)=>v==null?'—':Number(v).toFixed(n);
const tileFields=[['Supply air','supply_c','°C'],['Return air','return_c','°C'],['Temperature split','split_c','°C'],['Indoor humidity','indoor_rh','%RH'],['Outdoor air','outdoor_c','°C'],['Compressor','compressor_a','A'],['Blower','blower_a','A'],['Filter pressure','filter_pa','Pa'],['Vibration','vibration_g','g'],['Real power','power_w','W']];
let history=[];
function el(tag,text,cls){const e=document.createElement(tag);if(text!=null)e.textContent=text;if(cls)e.className=cls;return e;}
function render(s){
 const r=s.latest; $('connection').textContent=s.stale?'● Data unavailable / stale':'● Local connection';
 $('source').textContent=s.simulation?'SIMULATION · '+s.scenario.replaceAll('_',' ').toUpperCase():'HARDWARE · PASSIVE MONITOR';
 if(!r)return;
 $('health').textContent=s.stale?'Unknown / stale':r.health==='Normal'?(r.quality==='good'?'Healthy':'Insufficient evidence'):r.health;
 $('mode').textContent=r.mode.replaceAll('_',' '); $('quality').textContent=s.stale?'Stale':r.quality;
 $('baseline').textContent=r.metrics.baseline_matched?(s.simulation?'Synthetic reference':'Matched reference'):'Learning / no matching reference';
 $('summary').textContent=s.stale?'Last readings are retained for context. Current health cannot be assessed.':r.alerts.length?`${r.alerts.length} active diagnostic signal(s). Review evidence below.`:'No active rule alerts. Coverage depends on installed, valid sensors.';
 $('updated').textContent='Sample: '+new Date(r.timestamp).toLocaleString();
 $('tiles').replaceChildren(...tileFields.map(([label,key,unit])=>{const d=el('div',null,'tile');d.append(el('span',label));const v=el('strong',fmt(r.values[key],key==='vibration_g'?3:1)+' ');v.append(el('small',unit));d.append(v);return d;}));
 const m=r.metrics;
 const metrics=[['Compressor runtime',fmt(m.compressor_runtime_s/60)+' min'],['Duty cycle (observed)',fmt(m.duty_cycle==null?null:m.duty_cycle*100)+' %'],['Starts in last hour',m.cycles_last_hour],['Mean completed run',fmt(m.average_cycle_s==null?null:m.average_cycle_s/60)+' min'],['Energy (meter coverage)',fmt(m.energy_kwh,3)+' kWh'],['Call response',fmt(m.response_s,0)+' s'],['Humidity change',fmt(m.humidity_change_pct_per_hour)+' %RH/h'],['Anomaly score',fmt(m.anomaly_score,2)]];
 $('metrics').replaceChildren(...metrics.flatMap(([k,v])=>[el('dt',k),el('dd',v)]));
 $('alerts').replaceChildren(...r.alerts.map(a=>{const d=el('article',null,'alert '+(a.severity==='Critical'?'critical':''));d.append(el('div',a.severity.toUpperCase()+' · '+new Date(a.timestamp).toLocaleTimeString(),'badge'),el('h3',a.id.replaceAll('_',' ')+' · '+Math.round(a.confidence*100)+'% evidence score'),el('p',a.explanation),el('div',Object.entries(a.evidence).map(([k,v])=>k+': '+fmt(v)).join('  |  '),'evidence'),el('p','Possible causes: '+a.possible_causes.join('; ')),el('p','Next step: '+a.recommended_action),el('p',a.confidence_kind+' · '+a.threshold_source,'subtle'));return d;}));
 if(!r.alerts.length)$('alerts').append(el('div','No active alerts. An unmonitored or missing channel cannot establish normal operation.','empty'));
}
const charts={temperature:[['supply_c','Supply °C'],['return_c','Return °C']],humidity:[['indoor_rh','Indoor %RH'],['supply_rh','Supply %RH']],current:[['compressor_a','Compressor A'],['blower_a','Blower A']],pressure:[['filter_pa','Filter Pa']],vibration:[['vibration_g','Compressor g']],anomaly:[['anomaly_score','Score']],runtime:[['compressor_runtime_s','Runtime seconds']],duty:[['duty_cycle','Duty fraction']]};
function draw(){
 const series=charts[$('chart-select').value], W=760,H=245,p=38, colors=['#22766d','#d49b49'];
 const val=(r,k)=>r.values[k]??r.metrics[k]??null;
 const nums=history.flatMap(r=>series.map(([k])=>val(r,k))).filter(v=>v!==null);
 if(!nums.length){$('chart').replaceChildren(el('p','No valid history yet.'));return;}
 let lo=Math.min(...nums),hi=Math.max(...nums);if(hi===lo)hi=lo+1;
 const ns='http://www.w3.org/2000/svg',svg=document.createElementNS(ns,'svg');svg.setAttribute('viewBox',`0 0 ${W} ${H}`);
 const add=(tag,attrs,text)=>{const n=document.createElementNS(ns,tag);Object.entries(attrs).forEach(([k,v])=>n.setAttribute(k,v));if(text)n.textContent=text;svg.append(n);return n;};
 for(let i=0;i<4;i++){const y=p+i*(H-2*p)/3;add('line',{x1:p,y1:y,x2:W-p,y2:y,stroke:'#e0e6dc'});add('text',{x:0,y:y+4,fill:'#718477','font-size':11},fmt(hi-i*(hi-lo)/3));}
 series.forEach(([key,label],j)=>{let d='',pen=false;history.forEach((r,i)=>{const v=val(r,key);if(v===null||r.gap_detected){pen=false;return;}const x=p+i*(W-2*p)/Math.max(1,history.length-1),y=H-p-(v-lo)/(hi-lo)*(H-2*p);d+=`${pen?'L':'M'}${x.toFixed(1)},${y.toFixed(1)} `;pen=true;});add('path',{d,fill:'none',stroke:colors[j],'stroke-width':2.5});add('text',{x:p+j*190,y:H-5,fill:colors[j],'font-size':12},label);});
 $('chart').replaceChildren(svg);$('chart-note').textContent=`${history.length} samples · ${history.length?new Date(history[0].timestamp).toLocaleTimeString():''} — ${history.length?new Date(history.at(-1).timestamp).toLocaleTimeString():''} · Gaps are not interpolated.`;
}
async function tick(){try{const [s,h]=await Promise.all([fetch('/api/status').then(r=>r.json()),fetch('/api/history?limit=720').then(r=>r.json())]);history=h;render(s);draw();}catch(e){$('connection').textContent='● Connection lost';$('health').textContent='Unknown / offline';$('summary').textContent='The local service cannot be reached.';} $('clock').textContent=new Date().toLocaleTimeString();}
$('chart-select').addEventListener('change',draw);
fetch('/api/profiles').then(r=>r.json()).then(ps=>{ps.forEach((p,i)=>{const o=el('option',p.manufacturer+' · '+p.model+' ('+p.scope+')');o.value=i;$('profiles').append(o);});$('profiles').addEventListener('change',()=>{$('profile-detail').textContent=$('profiles').value===''?'Unknown equipment. Establish a reviewed baseline; no manufacturer limits applied.':JSON.stringify(ps[Number($('profiles').value)],null,2);});}).catch(()=>{});
tick();setInterval(tick,2000);
