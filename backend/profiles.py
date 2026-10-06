import json
from pathlib import Path
PATH = Path(__file__).resolve().parents[1]/'data/equipment_profiles.json'

def profiles(): return json.loads(PATH.read_text(encoding='utf-8'))

def match(manufacturer, model):
    # Do not prefix-match an exact SKU to a marketing family.
    return next((p for p in profiles() if p['manufacturer'].casefold() == manufacturer.casefold() and p['model'].casefold() == model.casefold()), None)
