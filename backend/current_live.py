"""Process-local receipt leases and copy-only live projections; history is immutable."""
from copy import deepcopy
from dataclasses import dataclass
import json
from .current_quality import CURRENT_FIELDS


def _binding(frame):
    return json.dumps({k:frame.get(k) for k in
        ('seq','ts','source','current_quality',*CURRENT_FIELDS)},sort_keys=True,allow_nan=False)


@dataclass(frozen=True)
class CurrentReceipt:
    binding: str
    received_ns: int
    deadlines: tuple


class ReceivedFrame(dict):
    """Trusted in-process context. Attributes never become JSON/wire fields."""
    def __init__(self, frame, received_ns, deadlines):
        super().__init__(frame)
        self.current_receipt=CurrentReceipt(_binding(frame),received_ns,tuple(deadlines))


def receipt_deadlines(frame, now_ns):
    receipt=getattr(frame,'current_receipt',None)
    numeric=frame.get('source')=='hardware' and any(frame.get(k) is not None for k in CURRENT_FIELDS)
    if receipt is None:
        if numeric:raise ValueError('Hardware current requires a trusted in-process receipt lease')
        return (None,None,None)
    if (not isinstance(receipt,CurrentReceipt) or receipt.binding!=_binding(frame)
            or now_ns<receipt.received_ns):
        raise ValueError('Current receipt was altered or host clock regressed')
    return receipt.deadlines


def mask_channel(values, channel, fault):
    values[CURRENT_FIELDS[channel]]=None
    q=values.get('current_quality')
    if q is not None and q['channels'][channel]['available']:
        q['channels'][channel]['available']=False
        q['channels'][channel]['faults']|=fault


def ingest_projection(frame, deadlines, now_ns):
    projected=deepcopy(dict(frame))
    if frame.get('source')=='hardware':
        for c,key in enumerate(CURRENT_FIELDS):
            if frame.get(key) is not None and (deadlines[c] is None or now_ns>deadlines[c]):
                mask_channel(projected,c,128) # Consumer age expiry, not a new source timestamp.
    return projected


def limit_diagnostics(result):
    """Do not recompute state/rules during reads. Keep independent warning context."""
    result.update(mode='unknown',compressor_state=None,fan_state=None,quality='incomplete',
                  steady=False,live_limited=True)
    # Most prototype rules depend directly/indirectly on current state. Preserve
    # the independent float warning as last-observation context, never as fresh proof.
    result['alerts']=[dict(a,last_observation=True) for a in result['alerts'] if a['id']=='condensate']
    result['new_alerts']=[]
    result['health']='Critical' if result['alerts'] else 'Unknown'
    result['metrics']={k:({} if k=='z_scores' else False if k=='baseline_matched' else None)
                       for k in result['metrics']}


def live_projection(result, deadlines, now_ns, blocked=None):
    if result is None:return None,dict(schema=1,channels=[])
    out=deepcopy(result)
    out.pop('raw_values',None) # Original inputs remain available through history only.
    hardware=result['source']=='hardware'
    limited=blocked is not None
    channels=[]
    for c,key in enumerate(CURRENT_FIELDS):
        value=result['values'].get(key)
        deadline=deadlines[c] if hardware else None
        reason=blocked
        if reason is None and hardware and deadline is not None and now_ns>deadline:reason='expired'
        if reason is None and value is not None and hardware and deadline is None:reason='no_receipt'
        if reason is None and value is None:reason='unavailable'
        available=reason is None
        remaining=max(0,(deadline-now_ns)//1000) if available and deadline is not None else None
        if reason not in (None,'unavailable'):
            mask_channel(out['values'],c,dict(expired=128,clock=8192,no_receipt=64).get(reason,4096))
            limited=True
        channels.append(dict(field=key,available=available,remaining_us=remaining,reason=reason))
    if limited:limit_diagnostics(out)
    return out,dict(schema=1,channels=channels)
