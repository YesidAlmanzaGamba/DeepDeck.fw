/**
 * One layer as a file to share (R6.8): the layer, its key colours, and the definitions of the
 * macros, sequences and tap-dances its keys and knobs use, so it works on another DeepDeck.
 * Importing creates those definitions in free slots and rewrites the key codes to match.
 */
import { MACRO_BASE, SEQUENCE_BASE, TAPDANCE_BASE } from '../keyboard/key-catalog';

export const LAYER_FORMAT = 'deepdeck-layer';
const LAYER_VERSION = 1;
const KNOB_ACTIONS = ['cw', 'ccw', 'single_press', 'long_press', 'double_press'];

type Call = (method: string, params?: any) => Promise<any>;

const isMacro = (c: number) => c >= MACRO_BASE && c <= 600;
const isSequence = (c: number) => c >= SEQUENCE_BASE && c < SEQUENCE_BASE + 32;
const isTapdance = (c: number) => c >= TAPDANCE_BASE && c < TAPDANCE_BASE + 16;

/** Every action code a layer uses (keys, then knobs) */
function layerCodes(layer: any): number[] {
  const codes: number[] = [];
  for (const row of ['row0', 'row1', 'row2', 'row3']) {
    (layer[row] || []).forEach((k: any) => codes.push(k.key_code));
  }
  for (const side of ['left_encoder_map', 'right_encoder_map']) {
    KNOB_ACTIONS.forEach((a) => codes.push(layer[side]?.[a] || 0));
  }
  return codes;
}

function mapLayerCodes(layer: any, map: (c: number) => number) {
  for (const row of ['row0', 'row1', 'row2', 'row3']) {
    (layer[row] || []).forEach((k: any) => (k.key_code = map(k.key_code)));
  }
  for (const side of ['left_encoder_map', 'right_encoder_map']) {
    KNOB_ACTIONS.forEach((a) => (layer[side][a] = map(layer[side][a] || 0)));
  }
}

export async function exportLayer(call: Call, uuid: string): Promise<any> {
  const layer = await call('layers.get', { uuid });
  delete layer.uuid;
  const file: any = { format: LAYER_FORMAT, version: LAYER_VERSION, created: new Date().toISOString(), layer, macros: {}, sequences: {}, tapdance: {} };
  try {
    const c = await call('led.keys.get', { uuid });
    file.colors = { keys: c.keys, layer: c.layer, custom: c.custom };
  } catch {
    /* older firmware */
  }
  const macros = (await call('macros.list')).macros;
  const sequences = await call('keys.sequences.list').then((r) => r.sequences, () => []);
  const tapdance = await call('keys.tapdance.list').then((r) => r.tapdance, () => []);

  const include = (code: number) => {
    if (isMacro(code) && !file.macros[code]) {
      const m = macros.find((x: any) => x.keycode === code);
      if (m) file.macros[code] = { name: m.name, key: m.key };
    } else if (isSequence(code) && !file.sequences[code]) {
      const s = sequences[code - SEQUENCE_BASE];
      if (s) file.sequences[code] = { name: s.name, steps: s.steps };
    } else if (isTapdance(code) && !file.tapdance[code]) {
      const t = tapdance[code - TAPDANCE_BASE];
      if (t) {
        file.tapdance[code] = { tap: t.tap, double: t.double, hold: t.hold };
        [t.tap, t.double, t.hold].forEach(include); // its macros and sequences too
      }
    }
  };
  layerCodes(layer).forEach(include);
  return file;
}

export function describeFile(file: any): string {
  const n = (o: any) => Object.keys(o || {}).length;
  return `"${file.layer?.name}": ${n(file.macros)} macro(s), ${n(file.sequences)} sequence(s), ${n(file.tapdance)} tap-dance(s)`;
}

export async function importLayer(call: Call, file: any, existingNames: string[]): Promise<{ uuid: string; name: string }> {
  if (file?.format !== LAYER_FORMAT || !file.layer?.row0) {
    throw new Error('this file is not a DeepDeck layer');
  }
  const remap = new Map<number, number>();

  // macros: new slots (macros.create picks the keycode)
  for (const [code, m] of Object.entries<any>(file.macros || {})) {
    const r = await call('macros.create', { name: m.name, key: m.key });
    remap.set(+code, r.keycode);
  }
  // sequences: free slots on this DeepDeck
  const sequences = await call('keys.sequences.list').then((r) => r.sequences, () => null);
  for (const [code, s] of Object.entries<any>(file.sequences || {})) {
    const free = sequences?.find((x: any) => !x.name && !x.steps.length && ![...remap.values()].includes(SEQUENCE_BASE + x.id));
    if (!free) {
      throw new Error('no free sequence slot (32 in use)');
    }
    await call('keys.sequences.set', { id: free.id, name: s.name, steps: s.steps });
    free.name = s.name; // taken
    remap.set(+code, SEQUENCE_BASE + free.id);
  }
  const map = (c: number) => remap.get(c) ?? c;
  // tap-dances last: their actions may be macros or sequences remapped above
  const tapdance = await call('keys.tapdance.list').then((r) => r.tapdance, () => null);
  for (const [code, t] of Object.entries<any>(file.tapdance || {})) {
    const free = tapdance?.find((x: any) => !x.tap && !x.double && !x.hold);
    if (!free) {
      throw new Error('no free tap-dance slot (16 in use)');
    }
    await call('keys.tapdance.set', { id: free.id, tap: map(t.tap), double: map(t.double), hold: map(t.hold) });
    free.tap = 1; // taken
    remap.set(+code, TAPDANCE_BASE + free.id);
  }

  const layer = JSON.parse(JSON.stringify(file.layer));
  mapLayerCodes(layer, map);
  let name = String(layer.name || 'Imported').slice(0, 14);
  for (let i = 2; existingNames.includes(name); i++) {
    name = `${String(layer.name).slice(0, 11)} ${i}`;
  }
  layer.name = name;
  const created = await call('layers.create', layer);
  if (file.colors?.custom) {
    await call('led.keys.set', { uuid: created.uuid, keys: file.colors.keys, layer: file.colors.layer }).catch(() => {});
  }
  return { uuid: created.uuid, name };
}
