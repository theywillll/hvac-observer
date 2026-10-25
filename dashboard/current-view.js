'use strict';
// Copy-only display projection. elapsedUs includes the WHOLE status fetch round trip.
function liveCurrentView(status, elapsedUs, failure=null) {
 const out=JSON.parse(JSON.stringify(status)), r=out.latest;
 if(!r)return out;
 let limited=Boolean(failure || out.stale || out.error!=null || r.live_limited);
 const channels=out.current_live?.schema===1?out.current_live.channels:[];
 const keys=['compressor_a','blower_a','condenser_fan_a'];
 keys.forEach((key,c)=>{
  const ch=channels[c], value=r.values[key];
  let reason=failure || (out.error!=null?'acquisition_error':out.stale?'stale':null);
  if(!reason && value!=null && r.source==='hardware') {
   if(!ch || ch.field!==key || !ch.available || !Number.isFinite(ch.remaining_us))reason='no_receipt';
   else if(!Number.isFinite(elapsedUs) || elapsedUs<0 || elapsedUs>ch.remaining_us)reason='expired';
  }
  if(reason) {
   r.values[key]=null;limited=true;
   const q=r.values.current_quality?.channels?.[c];
   if(q?.available){q.available=false;q.faults|=({expired:128,clock:8192,no_receipt:64}[reason]??4096);}
   if(ch){ch.available=false;ch.remaining_us=null;ch.reason=reason;}
  } else if(ch?.available && Number.isFinite(ch.remaining_us))ch.remaining_us=Math.max(0,ch.remaining_us-elapsedUs);
 });
 if(limited) {
  r.mode='unknown';r.compressor_state=null;r.fan_state=null;r.quality='incomplete';r.steady=false;r.live_limited=true;
  r.alerts=r.alerts.filter(a=>a.id==='condensate').map(a=>({...a,last_observation:true}));
  r.new_alerts=[];r.health=r.alerts.length?'Critical':'Unknown';
  Object.keys(r.metrics).forEach(k=>{r.metrics[k]=k==='z_scores'?{}:k==='baseline_matched'?false:null;});
 }
 delete r.raw_values;
 return out;
}
if(typeof module!=='undefined')module.exports={liveCurrentView};
