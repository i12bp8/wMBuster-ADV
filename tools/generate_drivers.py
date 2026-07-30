#!/usr/bin/env python3
"""
wM-Buster ADV Driver Generator.

Parses the upstream wmbusmeters database (`generated_database.cc`) and embedded test
telegrams to generate C++ driver rule tables and regression vectors for the firmware.

Usage:
    ./generate_drivers.py [wmbusmeters_path]
"""

import argparse
import hashlib
import html
import json
import os
import re
import sys
from typing import Any, Dict, List, Optional, Tuple, Iterator

def L(vname, q, vr, mtype='Instantaneous', storage=None, comb=None, du=None,
      sign='Signed', scaling='Auto', kind='num'):
    return dict(vname=vname, quantity=q, vif_range=vr, mtype=mtype, storage=storage,
                combinable=comb, display_unit=du, signedness=sign, scaling=scaling, kind=kind)

LIBRARY_TEMPLATES = {
    'status-tpl-only': dict(kind='status_tpl_only'),
    'actuality_duration_s': L('actuality_duration', 'Time', 'ActualityDuration', du='s'),
    'actuality_duration_h': L('actuality_duration', 'Time', 'ActualityDuration'),
    'battery_v':            L('battery', 'Voltage', 'Voltage'),
    'fabrication_no':       L('fabrication_no', 'Text', 'FabricationNo', kind='str'),
    'enhanced_id':          L('enhanced_id', 'Text', 'EnhancedIdentification', kind='str'),
    'software_version':     L('software_version', 'Text', 'SoftwareVersion', kind='str'),
    'manufacturer':         L('manufacturer', 'Text', 'Manufacturer', kind='str'),
    'model_version':        L('model_version', 'Text', 'ModelVersion', kind='str'),
    'firmware_version':     L('firmware_version', 'Text', 'FirmwareVersion', kind='str'),
    'parameter_set':        L('parameter_set', 'Text', 'ParameterSet', kind='str'),
    'customer':             L('customer', 'Text', 'Customer', kind='str'),
    'location':             L('location', 'Text', 'Location', kind='str'),
    'operating_time_h':     L('operating_time', 'Time', 'OperatingTime'),
    'on_time_h':            L('on_time', 'Time', 'OnTime'),
    'on_time_at_error_h':   L('on_time_at_error', 'Time', 'OnTime', mtype='AtError'),
    'meter_date':           L('meter_date', 'Text', 'Date', kind='str'),
    'meter_date_at_error':  L('meter_date_at_error', 'Text', 'Date', mtype='AtError', kind='str'),
    'meter_datetime':       L('meter_datetime', 'Text', 'DateTime', kind='str'),
    'meter_datetime_at_error': L('meter_datetime_at_error', 'Text', 'DateTime', mtype='AtError', kind='str'),
    'total_m3':             L('total', 'Volume', 'Volume'),
    'target_m3':            L('target', 'Volume', 'Volume', storage=1),
    'total_kwh':            L('total', 'Energy', 'AnyEnergyVIF'),
    'target_kwh':           L('target', 'Energy', 'AnyEnergyVIF', storage=1),
    'target_date':          L('target', 'PointInTime', 'Date', storage=1),
    'total_forward_m3':     L('total_forward', 'Volume', 'Volume', comb='ForwardFlow'),
    'total_backward_m3':    L('total_backward', 'Volume', 'Volume', comb='BackwardFlow'),
    'flow_temperature_c':   L('flow_temperature', 'Temperature', 'FlowTemperature'),
    'external_temperature_c': L('external_temperature', 'Temperature', 'ExternalTemperature'),
    'return_temperature_c': L('return_temperature', 'Temperature', 'ReturnTemperature'),
    'flow_return_temperature_difference_c': L('flow_return_temperature_difference', 'Temperature', 'TemperatureDifference'),
    'volume_flow_m3h':      L('volume_flow', 'Flow', 'VolumeFlow'),
    'access_counter':       L('access', 'Dimensionless', 'AccessNumber', sign='Unsigned', scaling='None'),
    'consumption_hca':      L('consumption', 'HCA', 'HeatCostAllocation'),
    'target_hca':           L('target', 'HCA', 'HeatCostAllocation', storage=1),
}

VIF_RANGES = [
    'Volume','OnTime','OperatingTime','VolumeFlow','FlowTemperature','ReturnTemperature',
    'TemperatureDifference','ExternalTemperature','Pressure','HeatCostAllocation','Date','DateTime',
    'EnergyMJ','EnergyWh','PowerW','PowerJh','ActualityDuration','FabricationNo','EnhancedIdentification',
    'EnergyMWh','EnergyGJ','RelativeHumidity','AccessNumber','Medium','Manufacturer','ParameterSet',
    'ModelVersion','HardwareVersion','FirmwareVersion','SoftwareVersion','Location','Customer','ErrorFlags',
    'DigitalOutput','DigitalInput','DurationSinceReadout','DurationOfTariff','Dimensionless','Voltage',
    'Amperage','ResetCounter','CumulationCounter','SpecialSupplierInformation','RemainingBattery',
    'AnyVolumeVIF','AnyEnergyVIF','AnyPowerVIF',
]

QUANTITIES = [
    'Volume','Energy','Reactive_Energy','Apparent_Energy','Power','Flow','Temperature',
    'Voltage','Amperage','Pressure','Time','PointInTime','RH','HCA','Text','Counter',
    'Dimensionless','Mass','Frequency','Angle'
]

COMBINABLES = {
    'Average': 0x12, 'InverseCompactProfile': 0x13, 'RelativeDeviation': 0x14,
    'RecordErrorCodeMeterToController': 0x15, 'StandardConformantDataContent': 0x1D,
    'CompactProfileWithRegister': 0x1E, 'CompactProfile': 0x1F,
    'PerSecond': 0x20, 'PerMinute': 0x21, 'PerHour': 0x22, 'PerDay': 0x23,
    'PerWeek': 0x24, 'PerMonth': 0x25, 'PerYear': 0x26,
    'PerLitre': 0x2C, 'PerM3': 0x2D, 'PerKg': 0x2E, 'PerKelvin': 0x2F,
    'PerKWh': 0x30, 'PerGJ': 0x31, 'PerKW': 0x32, 'PerVolt': 0x34, 'PerAmpere': 0x35,
    'StartDateTimeOfAB': 0x39, 'UncorrectedMeterUnit': 0x3A,
    'ForwardFlow': 0x3B, 'BackwardFlow': 0x3C, 'ValueAtBaseCondC': 0x3E,
    'ObisDeclaration': 0x3F, 'LowerLimit': 0x40, 'UpperLimit': 0x48,
    'DurationExceedsLowerFirst': 0x50, 'DurationExceedsLowerLast': 0x54,
    'DurationExceedsUpperFirst': 0x58, 'DurationExceedsUpperLast': 0x5C,
    'LeakageValues': 0x69, 'OverflowValues': 0x6A,
    'FutureValue': 0x7E,
    'AtPhase1': 0x7C01, 'AtPhase2': 0x7C02, 'AtPhase3': 0x7C03, 'AtNeutral': 0x7C04,
    'BetweenPhaseL1AndL2': 0x7C05, 'BetweenPhaseL2AndL3': 0x7C06, 'BetweenPhaseL3AndL1': 0x7C07,
    'AtQuadrantQ1': 0x7C08, 'AtQuadrantQ2': 0x7C09, 'AtQuadrantQ3': 0x7C0A, 'AtQuadrantQ4': 0x7C0B,
    'Mfct00': 0x7F00, 'Mfct01': 0x7F01, 'Mfct02': 0x7F02, 'Mfct03': 0x7F03,
    'Mfct04': 0x7F04, 'Mfct05': 0x7F05, 'Mfct06': 0x7F06, 'Mfct21': 0x7F21,
}

ATTRS = {'HIDE': 1, 'STATUS': 2, 'INCLUDE_TPL_STATUS': 4, 'DEPRECATED': 8, 'INJECT_INTO_STATUS': 16}

# --- XMQ Parser ---

def tokenize(s: str) -> Iterator[str | Tuple[str, str]]:
    """Tokenize an XMQ string."""
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if c.isspace():
            i += 1
            continue
        if c in '{}=':
            yield c
            i += 1
            continue
        if c in "'\"":
            q = c
            i += 1
            start = i
            while i < n and s[i] != q:
                i += 1
            yield ('str', s[start:i])
            i += 1
            continue
        start = i
        while i < n and not s[i].isspace() and s[i] not in '{}=':
            i += 1
        yield ('str', s[start:i])


def parse_xmq(s: str) -> Dict[str, Any]:
    """Parse an XMQ string into a nested dictionary."""
    tokens = list(tokenize(s))
    pos = [0]

    def _add(obj: dict, key: str, val: Any) -> None:
        if key in obj:
            if not isinstance(obj[key], list):
                obj[key] = [obj[key]]
            obj[key].append(val)
        else:
            obj[key] = val

    def parse_block(end_tok: Optional[str] = None) -> Dict[str, Any]:
        obj = {}
        while pos[0] < len(tokens):
            tok = tokens[pos[0]]
            if tok == end_tok or tok == '}':
                pos[0] += 1
                return obj
            key = tok[1] if isinstance(tok, tuple) else tok
            pos[0] += 1
            if pos[0] < len(tokens) and tokens[pos[0]] == '=':
                pos[0] += 1
                val = tokens[pos[0]]
                pos[0] += 1
                v = val[1] if isinstance(val, tuple) else val
                _add(obj, key, html.unescape(v))
            elif pos[0] < len(tokens) and tokens[pos[0]] == '{':
                pos[0] += 1
                _add(obj, key, parse_block('}'))
            else:
                _add(obj, '_item', html.unescape(key))
        return obj

    return parse_block()


def as_list(v: Any) -> List[Any]:
    if v is None:
        return []
    return v if isinstance(v, list) else [v]


# --- Database Processing ---

def parse_int_range(s: Optional[str]) -> Tuple[int, int]:
    if s is None:
        return (-1, -1)
    parts = str(s).split(',')
    if len(parts) == 1:
        v = int(parts[0], 0)
        return (v, v)
    return (int(parts[0], 0), int(parts[1], 0))


def convert_match(m: Any) -> Dict[str, Any]:
    rule = {
        'difvifkey': None, 'vif_range': None, 'mtype': None,
        'storage': (-1, -1), 'tariff': (-1, -1), 'subunit': (-1, -1),
        'add_combinable': 0, 'combinable_synthetic': False, 'add_combinable_raw': None
    }
    if not isinstance(m, dict):
        return rule

    if 'difvifkey' in m: rule['difvifkey'] = str(m['difvifkey']).upper()
    if 'vif_range' in m: rule['vif_range'] = str(m['vif_range'])
    if 'measurement_type' in m: rule['mtype'] = str(m['measurement_type'])
    if 'storage_nr' in m: rule['storage'] = parse_int_range(m['storage_nr'])
    if 'tariff_nr' in m: rule['tariff'] = parse_int_range(m['tariff_nr'])
    if 'subunit_nr' in m: rule['subunit'] = parse_int_range(m['subunit_nr'])

    if 'add_combinable' in m:
        cb = str(m['add_combinable'])
        if cb == 'Synthetic':
            rule['combinable_synthetic'] = True
        elif cb in COMBINABLES:
            rule['add_combinable'] = COMBINABLES[cb]
        else:
            rule['add_combinable'] = int(cb, 0)
    
    if 'add_combinable_raw' in m:
        rule['add_combinable_raw'] = int(str(m['add_combinable_raw']), 0)
    
    return rule


def convert_lookup(lk: Any) -> Optional[Dict[str, Any]]:
    if not isinstance(lk, dict):
        return None
    maps = []
    for me in as_list(lk.get('map')):
        if not isinstance(me, dict):
            continue
        test = str(me.get('test', 'Set'))
        maps.append({
            'name': str(me.get('name', '')),
            'value': int(str(me.get('value', 0)), 0),
            'test': {'Set': 0, 'NotSet': 1, 'Equal': 2}.get(test, 0)
        })
    return {
        'name': str(lk.get('name', 'LOOKUP')),
        'map_type': {'BitToString': 0, 'IndexToString': 1}.get(str(lk.get('map_type', 'BitToString')), 0),
        'mask': int(str(lk.get('mask_bits', '0xffff')), 0),
        'default': str(lk.get('default_message', 'OK')),
        'maps': maps
    }


def convert_field(f: dict, driver_name: str) -> Dict[str, Any]:
    name = str(f.get('name', '?')).strip("'\"")
    attrs = 0
    for a in str(f.get('attributes', '')).split(','):
        a = a.strip()
        if a in ATTRS:
            attrs |= ATTRS[a]

    force_scale = 0.0
    if 'force_scale' in f:
        try:
            force_scale = float(eval(str(f['force_scale']), {'__builtins__': {}}, {}))
        except Exception:
            pass

    field = {
        'name': name,
        'quantity': str(f.get('quantity', 'Text')),
        'display_unit': str(f.get('display_unit', '')),
        'attrs': attrs,
        'vif_scaling': str(f.get('vif_scaling', 'Auto')),
        'signedness': str(f.get('dif_signedness', 'Default')),
        'force_scale': force_scale,
        'match': convert_match(f.get('match')),
        'calculate': str(f['calculate']) if 'calculate' in f else None,
        'lookup': convert_lookup(f.get('lookup')),
        'match_entire_payload': str(f.get('match_entire_payload', 'false')) == 'true',
        'has_ixml': 'ixml' in f,
    }
    return field


def lib_field_to_rule(libname: str) -> Dict[str, Any]:
    lf = LIBRARY_TEMPLATES[libname]
    
    if lf.get('kind') == 'status_tpl_only':
        return {
            'name': 'status', 'quantity': 'Text', 'display_unit': '',
            'attrs': ATTRS['STATUS'] | ATTRS['INCLUDE_TPL_STATUS'],
            'vif_scaling': 'Auto', 'signedness': 'Default', 'force_scale': 0.0,
            'match': None, 'calculate': None, 'lookup': None,
            'match_entire_payload': False, 'has_ixml': False
        }
        
    storage = lf.get('storage')
    m = {
        'difvifkey': None, 'vif_range': lf.get('vif_range'), 'mtype': lf.get('mtype'),
        'storage': (storage, storage) if storage is not None else (-1, -1),
        'tariff': (-1, -1), 'subunit': (-1, -1),
        'add_combinable': COMBINABLES.get(lf.get('combinable', ''), 0),
        'combinable_synthetic': False, 'add_combinable_raw': None
    }
    
    return {
        'name': lf.get('vname', ''), 'quantity': lf.get('quantity', 'Text'),
        'display_unit': lf.get('display_unit', ''), 'attrs': 0,
        'vif_scaling': lf.get('scaling', 'Auto'), 'signedness': lf.get('signedness', 'Default'),
        'force_scale': 0.0, 'match': m, 'calculate': None, 'lookup': None,
        'match_entire_payload': False, 'has_ixml': False
    }


def parse_drivers(wmbusmeters_path: str) -> List[Dict[str, Any]]:
    db_path = os.path.join(wmbusmeters_path, 'src/generated_database.cc')
    with open(db_path, 'r', encoding='utf-8') as f:
        src = f.read()

    entries = re.findall(r'\{\s*"([^"]+)",\s*"([^"]*)",\s*"(driver\{.*?\})",\s*(?:false|true)\s*\},', src, re.S)
    drivers = []

    for name, aliases, drv in entries:
        drv_c = drv.replace('\\"', '"').replace('\\\\', '\\')
        try:
            doc = parse_xmq(drv_c)
        except Exception as e:
            print(f"WARN: xmq parse failed for {name}: {e}")
            continue

        d = doc.get('driver', doc)
        
        detect = []
        for det in as_list(d.get('detect')):
            if not isinstance(det, dict): continue
            for mvt in as_list(det.get('mvt')):
                parts = str(mvt).split(',')
                if len(parts) == 3:
                    m, v, t = parts
                    ver = 0xFF if v == '*' else int(v, 16)
                    typ = 0xFF if t == '*' else int(t, 16)
                    detect.append((m, ver, typ))

        fields = []
        has_payload_decoder = False
        flds = d.get('fields')
        if isinstance(flds, dict):
            for f in as_list(flds.get('field')):
                if not isinstance(f, dict): continue
                cf = convert_field(f, name)
                if cf['has_ixml'] or cf['match_entire_payload']:
                    has_payload_decoder = True
                fields.append(cf)

        libnames = []
        lib = d.get('library')
        if isinstance(lib, dict):
            for u in as_list(lib.get('use')):
                for part in str(u).split(','):
                    part = part.strip().split('|')[0].strip()
                    if part: libnames.append(part)

        lib_fields = []
        for ln in libnames:
            if ln in LIBRARY_TEMPLATES:
                lib_fields.append(lib_field_to_rule(ln))
            else:
                print(f"WARN: {name}: unknown library field {ln}")

        tpl_lookup = None
        if isinstance(d.get('mfct_tpl_status_bits'), dict):
            tpl_lookup = convert_lookup(d['mfct_tpl_status_bits'])
            
        if tpl_lookup is None:
            for cf in fields:
                if (cf['attrs'] & ATTRS['INCLUDE_TPL_STATUS']) and cf['match'] is None and cf['lookup']:
                    tpl_lookup = cf['lookup']
                    break

        drivers.append({
            'name': name, 'aliases': aliases,
            'meter_type': str(d.get('meter_type', 'UnknownMeter')),
            'default_fields': str(d.get('default_fields', 'name,id,timestamp')),
            'detect': detect,
            'fields': lib_fields + fields,
            'payload_decoder': has_payload_decoder,
            'tpl_lookup': tpl_lookup,
            'compact': bool(d.get('compact_frame_formats')),
        })
    return drivers


def parse_tests(wmbusmeters_path: str) -> List[Dict[str, Any]]:
    tests = []
    srcdir = os.path.join(wmbusmeters_path, 'drivers/src')
    
    if not os.path.isdir(srcdir):
        return tests
        
    for fn in sorted(os.listdir(srcdir)):
        if not fn.endswith('.xmq'): continue
        with open(os.path.join(srcdir, fn), 'r', encoding='utf-8') as f:
            text = f.read()

        ti = text.find('tests')
        if ti < 0: continue
        bi = text.find('{', ti)
        if bi < 0: continue
        
        depth, i = 0, bi
        while i < len(text):
            if text[i] == '{': depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0: break
            i += 1
            
        block = text[bi:i + 1]
        for tm in re.finditer(r'test\s*\{', block):
            start = tm.end() - 1
            depth, j = 0, start
            while j < len(block):
                if block[j] == '{': depth += 1
                elif block[j] == '}':
                    depth -= 1
                    if depth == 0: break
                j += 1
            tb = block[start:j + 1]

            def grab(key: str) -> Optional[str]:
                m = re.search(key + r"\s*=\s*'((?:[^'\\]|\\.)*)'", tb, re.S)
                if m: return m.group(1)
                m = re.search(key + r"\s*=\s*([^\s]+)", tb)
                return m.group(1) if m else None

            args, telegram, js = grab('args'), grab('telegram'), grab('json')
            if not telegram: continue
            
            telegram = telegram.replace('_', '').replace(' ', '')
            key, name = 'NOKEY', ''
            if args:
                parts = args.split()
                if len(parts) >= 4:
                    name, key = parts[0], parts[3]
                    
            expected = None
            if js:
                try: expected = json.loads(js)
                except Exception: pass
                
            tests.append({
                'driver': fn[:-4], 'name': name, 'key': key,
                'telegram': telegram, 'expected': expected
            })
    return tests


# --- Code Generation ---

def cstr(s: Optional[str]) -> str:
    if s is None:
        return 'nullptr'
    s = str(s).replace('\\', '\\\\').replace('"', '\\"').replace('\n', ' ')
    return f'"{s}"'


def generate_cpp(drivers: List[Dict[str, Any]], tests: List[Dict[str, Any]], out_dir: str):
    meter_types = list(dict.fromkeys(d['meter_type'] for d in drivers))
    
    lines = [
        '// GENERATED CODE - DO NOT EDIT.',
        '#include "wmbus_decode/driver_table.h"',
        '',
        'namespace wmb {',
        '',
    ]
    
    lookup_pool = {}
    
    def lookup_key(lk: dict) -> str:
        return json.dumps(lk, sort_keys=True)

    body = []
    driver_entries = []
    
    for d in drivers:
        sym = re.sub(r'[^a-zA-Z0-9_]', '_', d['name'])
        
        det_sym = 'nullptr'
        if d['detect']:
            det_sym = f'DETECT_{sym}'
            body.append(f'static const DriverDetect {det_sym}[] PROGMEM = {{')
            for (m, v, t) in d['detect']:
                body.append(f'    {{ {{ \'{m[0]}\', \'{m[1]}\', \'{m[2]}\', 0 }}, 0x{v:02X}, 0x{t:02X} }},')
            body.append('};')

        fld_sym = 'nullptr'
        if d['fields']:
            fld_sym = f'FIELDS_{sym}'
            body.append(f'static const FieldRule {fld_sym}[] PROGMEM = {{')
            for f in d['fields']:
                m = f['match'] or {}
                dk = cstr(m.get('difvifkey')) if m else 'nullptr'
                vr = m.get('vif_range') if m else None
                vr_s = f'VifRange::{vr}' if vr in VIF_RANGES else 'VifRange::None'
                
                mt = m.get('mtype') if m else None
                mt_s = {'Instantaneous': '(int8_t)MeasureType::Instantaneous',
                        'Maximum': '(int8_t)MeasureType::Maximum',
                        'Minimum': '(int8_t)MeasureType::Minimum',
                        'AtError': '(int8_t)MeasureType::AtError'}.get(mt, '-1')
                        
                st = m.get('storage', (-1, -1)) if m else (-1, -1)
                tr = m.get('tariff', (-1, -1)) if m else (-1, -1)
                su = m.get('subunit', (-1, -1)) if m else (-1, -1)
                ac = m.get('add_combinable', 0) if m else 0
                
                if m and m.get('add_combinable_raw') is not None:
                    ac = m['add_combinable_raw'] & 0xFFFF
                    
                syn = 'true' if (m and m.get('combinable_synthetic')) else 'false'
                q = f['quantity'] if f['quantity'] in QUANTITIES else 'Text'
                du = f['display_unit']
                
                if du == 'DateLT': du = 'date'
                elif du == 'DateTimeLT': du = 'datetime'
                
                vs = 1 if f['vif_scaling'] == 'None' else 0
                sg = {'Default': 0, 'Signed': 1, 'Unsigned': 2}.get(f['signedness'], 0)
                
                lk_s = 'nullptr'
                if f['lookup']:
                    key = lookup_key(f['lookup'])
                    if key not in lookup_pool:
                        lookup_pool[key] = f'LOOKUP_{len(lookup_pool)}'
                    lk_s = f'&{lookup_pool[key]}'
                    
                fs = '{:.10g}'.format(f['force_scale'])
                comb_raw_eq = m.get('add_combinable_raw') if m else None
                cre_s = str(comb_raw_eq) if comb_raw_eq is not None else '-1'
                
                body.append(
                    f'    {{ {cstr(f["name"])}, Quantity::{q}, {cstr(du) if du else "nullptr"}, 0x{f["attrs"]:04X}, {vs}, {sg}, {fs},\n'
                    f'      {dk}, {vr_s}, {mt_s}, {st[0]}, {st[1]}, {tr[0]}, {tr[1]}, {su[0]}, {su[1]}, {ac}, {cre_s}, {syn},\n'
                    f'      {cstr(f["calculate"])}, {lk_s} }},'
                )
            body.append('};')

        tpl_s = 'nullptr'
        if d['tpl_lookup']:
            key = lookup_key(d['tpl_lookup'])
            if key not in lookup_pool:
                lookup_pool[key] = f'LOOKUP_{len(lookup_pool)}'
            tpl_s = f'&{lookup_pool[key]}'
            
        driver_entries.append((d, det_sym, len(d['detect']), fld_sym, len(d['fields']), tpl_s))

    lk_lines = []
    for key, sym in lookup_pool.items():
        lk = json.loads(key)
        map_sym = 'nullptr'
        if lk['maps']:
            map_sym = f'{sym}_MAP'
            lk_lines.append(f'static const LookupMapEntry {map_sym}[] PROGMEM = {{')
            for me in lk['maps']:
                lk_lines.append(f'    {{ {cstr(me["name"])}, 0x{me["value"]:X}ULL, {me["test"]} }},')
            lk_lines.append('};')
        lk_lines.append(f'static const LookupDef {sym} PROGMEM = {{ {cstr(lk["name"])}, {lk["map_type"]}, 0x{lk["mask"]:X}ULL, {cstr(lk["default"])}, {map_sym}, {len(lk["maps"])} }};')
    lk_lines.append('')

    lines.extend([
        'static const char* const METER_TYPE_NAMES[] PROGMEM = {',
        *(f'    {cstr(mt)},' for mt in meter_types),
        '};',
        '',
        'const DriverDef DRIVERS[] PROGMEM = {'
    ])
    
    for (d, det_sym, ndet, fld_sym, nfld, tpl_s) in driver_entries:
        mt_idx = meter_types.index(d['meter_type'])
        pd = 1 if d['payload_decoder'] else 0
        lines.append(f'    {{ {cstr(d["name"])}, {mt_idx}, {cstr(d["default_fields"])}, {det_sym}, {ndet}, {fld_sym}, {nfld}, {pd}, {tpl_s} }},')
    
    lines.extend([
        '};',
        '',
        f'const size_t DRIVERS_LEN = {len(driver_entries)};',
        f'const size_t METER_TYPES_LEN = {len(meter_types)};',
        '',
        'const char* meter_type_name(uint8_t idx) {',
        '    if (idx >= METER_TYPES_LEN) return "UnknownMeter";',
        '    return METER_TYPE_NAMES[idx];',
        '}',
        '',
        '} // namespace wmb'
    ])

    ns_idx = next(i for i, l in enumerate(lines) if l.startswith('namespace'))
    final = lines[:ns_idx + 2] + lk_lines + [''] + body + lines[ns_idx + 2:]
    
    cpp_out = os.path.join(out_dir, 'lib/wmbus_decode/src/drivers_generated.cpp')
    os.makedirs(os.path.dirname(cpp_out), exist_ok=True)
    with open(cpp_out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(final) + '\n')
    print(f"Generated {cpp_out} ({len(final)} lines)")

    # Tests
    tv = [
        '// GENERATED CODE - DO NOT EDIT.',
        '#pragma once',
        '',
        'struct DriverTestVector {',
        '    const char* driver;',
        '    const char* name;',
        '    const char* key;',
        '    const char* telegram;',
        '    const char* expected;',
        '};',
        '',
        'static const DriverTestVector DRIVER_TEST_VECTORS[] = {'
    ]
    
    n_vec = 0
    for t in tests:
        if not t['expected']: continue
        exp = json.dumps(t['expected'], separators=(',', ':'))
        tv.append(f'    {{ {cstr(t["driver"])}, {cstr(t["name"])}, {cstr(t["key"])}, {cstr(t["telegram"])}, {cstr(exp)} }},')
        n_vec += 1
        
    tv.extend([
        '};',
        f'static const size_t DRIVER_TEST_VECTORS_LEN = {n_vec};'
    ])
    
    h_out = os.path.join(out_dir, 'test/test_vectors/driver_tests.h')
    os.makedirs(os.path.dirname(h_out), exist_ok=True)
    with open(h_out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(tv) + '\n')
    print(f"Generated {h_out} ({n_vec} vectors)")


def main():
    parser = argparse.ArgumentParser(description="wM-Buster ADV Driver Generator")
    parser.add_argument("wmbusmeters_path", nargs="?", default=".upstream/wmbusmeters", help="Path to upstream wmbusmeters repository")
    args = parser.parse_args()

    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    if not os.path.exists(args.wmbusmeters_path):
        print(f"Error: Upstream path {args.wmbusmeters_path} not found.")
        sys.exit(1)

    print(f"Parsing drivers from {args.wmbusmeters_path}...")
    drivers = parse_drivers(args.wmbusmeters_path)
    tests = parse_tests(args.wmbusmeters_path)
    
    print(f"Generating C++ code for {len(drivers)} drivers and {len(tests)} tests...")
    generate_cpp(drivers, tests, project_root)
    print("Done.")


if __name__ == '__main__':
    main()
