'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const {liveCurrentView}=require('../dashboard/current-view.js');
function sample(){return {stale:false,error:null,simulation:false,scenario:'test',current_live:{schema:1,channels:['compressor_a','blower_a','condenser_fan_a'].map((field,c)=>({field,available:true,remaining_us:4000-c*1000,reason:null}))},latest:{timestamp:'2026-10-26T01:00:00Z',source:'hardware',mode:'cooling',health:'Normal',quality:'good',steady:true,values:{compressor_a:2,blower_a:1,condenser_fan_a:0.3,current_quality:{channels:[0,1,2].map(channel=>({channel,available:true,faults:0}))}},alerts:[],new_alerts:[],metrics:{compressor_runtime_s:60,anomaly_score:0.1,baseline_matched:true,z_scores:{compressor_a:1}}}};}
const original=sample();
assert.equal(liveCurrentView(original,2000).latest.values.condenser_fan_a,0.3);
let out=liveCurrentView(original,2001);
assert.equal(out.latest.values.condenser_fan_a,null);
assert.equal(out.latest.values.compressor_a,2);
assert.equal(out.latest.health,'Unknown');
assert.equal(out.latest.metrics.compressor_runtime_s,null);
assert.equal(original.latest.values.condenser_fan_a,0.3);
assert.equal(liveCurrentView(original,4001).latest.values.compressor_a,null);
for(const elapsed of [-1,NaN,Infinity])assert.equal(liveCurrentView(original,elapsed).latest.values.compressor_a,null);
for(const reason of ['connection_lost','acquisition_error'])assert.equal(liveCurrentView(original,0,reason).latest.values.compressor_a,null);
const alerting=sample();alerting.latest.alerts=[{id:'high_current'},{id:'condensate'}];alerting.error='Disconnected';
out=liveCurrentView(alerting,0);assert.deepEqual(out.latest.alerts,[{id:'condensate',last_observation:true}]);assert.equal(out.latest.health,'Critical');
const missing=sample();delete missing.current_live;assert.equal(liveCurrentView(missing,0).latest.values.compressor_a,null);
const sim=sample();sim.latest.source='simulator';sim.simulation=true;sim.current_live.channels.forEach(c=>c.remaining_us=null);
assert.equal(liveCurrentView(sim,1e9).latest.values.compressor_a,2);

// Execute the actual app with a small DOM and deterministic clocks/timers.
class Element {
 constructor(){this.textContent='';this.children=[];this.value='temperature';}
 append(...nodes){this.children.push(...nodes);}
 replaceChildren(...nodes){this.children=nodes;}
 addEventListener(){}
 setAttribute(){}
}
const elements=new Map();
const element=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);};
let clock=0,fail=false,fetchLatency=0,nextTimer=1,timers=new Map(),events={};
const status=sample();status.current_live.channels.forEach(c=>c.remaining_us=100000);
let serverStatus=status;
const context=vm.createContext({console,performance:{now:()=>clock},document:{getElementById:element,createElement:()=>new Element(),createElementNS:()=>new Element(),addEventListener:(n,f)=>events[n]=f},window:{addEventListener:(n,f)=>events[n]=f},fetch:async path=>{if(fail)throw Error('Offline');if(path==='/api/status')clock+=fetchLatency;return {ok:true,json:async()=>path==='/api/status'?serverStatus:[]};},setInterval:()=>0,setTimeout:f=>{const id=nextTimer++;timers.set(id,f);return id;},clearTimeout:id=>timers.delete(id)});
vm.runInContext(fs.readFileSync('dashboard/current-view.js','utf8'),context);
vm.runInContext(fs.readFileSync('dashboard/app.js','utf8'),context);
const flush=()=>new Promise(resolve=>setImmediate(resolve));
function tileText(index){return element('tiles').children[index].children[1].textContent;}
(async()=>{
 await flush();assert.match(tileText(5),/^2\.0/);
 clock=101;const [id,expire]=timers.entries().next().value;timers.delete(id);expire();assert.match(tileText(5),/^—/);assert.equal(element('health').textContent,'Insufficient live evidence');
 clock=0;await vm.runInContext('tick()',context);assert.match(tileText(5),/^2\.0/);
 fail=true;await vm.runInContext('tick()',context);assert.match(tileText(5),/^—/);assert.match(element('connection').textContent,/lost/);
 events.focus();assert.match(tileText(5),/^—/);assert.match(element('connection').textContent,/lost/); // No offline revival.
 assert.equal(timers.size,0);
 fail=false;clock=0;fetchLatency=101;await vm.runInContext('tick()',context);assert.match(tileText(5),/^—/); // Charge response transit before first display.
 fetchLatency=0;
 fail=false;clock=0;await vm.runInContext('tick()',context);clock=101;events.visibilitychange();assert.match(tileText(5),/^—/);
 assert.equal(status.latest.values.compressor_a,2);
 serverStatus={...status,latest:null,current_live:{schema:1,channels:[]}};clock=0;await vm.runInContext('tick()',context);
 assert.equal(element('tiles').children.length,0);assert.equal(element('health').textContent,'Unknown');
 console.log('Current display projections, expiry timer, fetch failure and visibility recheck passed (synthetic DOM/clocks).');
})().catch(error=>{console.error(error);process.exitCode=1;});
