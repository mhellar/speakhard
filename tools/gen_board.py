"""Generate the Keypad Synth Rev A KiCad project:
ESP32-S3 SuperMini + 1.5" 128x128 SH1107 I2C OLED + MAX98357A amp + 4x4 keypad module + pot module.
Single-sided, all copper on B.Cu, for CNC isolation milling (same rules as s3_handheld_revA).

The keypad and the pot have right-angle header pins. They hang OFF the board (keypad off the bottom edge, pot off the
right edge) and their pins lie flat on long SMD "edge pads" on B.Cu, so the modules sit right side up about one
header-height below the main board.

Run with any python 3:  python tools/gen_board.py   then  tools/fill_sync.py  with KiCad's python.

Layout (board-local mm, origin top-left, 96 x 66):
  SuperMini top-left, USB-C at the left edge     OLED top-centre (header on its left edge)
  amp bottom-left (speaker terminal at the bottom)    keypad edge pads bottom-centre    pot edge pads right edge
Planar plan: 3V3 runs over the top to the OLED, 5V down the left edge to the amp, the upper pin row's GPIOs leave
through the channel between the SuperMini rows (OLED, pot, keypad C3/C4), the lower row fans down to the keypad.
"""
import os, re, json, uuid, math

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.dirname(HERE)
KICAD = os.path.dirname(OUT)
REVB = os.path.join(KICAD, "ttgo_audio_revB")
NAME = "keypad_synth_revA"
FPLIB, SYMLIB = "ks_cnc", "keypad_synth"
OX, OY = 100.0, 100.0  # board origin in the KiCad sheet

U = lambda: str(uuid.uuid4())
os.makedirs(os.path.join(OUT, FPLIB + ".pretty"), exist_ok=True)


def sexp_block(text, start_pat):
    """Return the balanced (...) block starting at the first match of start_pat."""
    i = text.index(start_pat)
    depth = 0
    for j in range(i, len(text)):
        c = text[j]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    raise ValueError(start_pat)


# ---------------------------------------------------------------- footprints
# pad: (number, kind, shape, x, y, sx, sy, drill)   drill None = SMD pad
def tht_rowpads(n, pitch, first_rect=True, size=(3.2, 1.9), drill=1.0, x=0.0, y0=0.0, start=1):
    return [(str(start + i), "thru_hole", "rect" if (i == 0 and first_rect) else "oval",
             x, y0 + i * pitch, size[0], size[1], drill) for i in range(n)]


EDGE_PAD = (1.6, 4.5)   # long pads the bent-over header pins lie on; board edge is 2.8 mm beyond the pad centres


def edge_pads(n):
    return [(str(i + 1), "smd", "rect", round(i * 2.54, 2), 0, EDGE_PAD[0], EDGE_PAD[1], None) for i in range(n)]


FOOTPRINTS = {
    "ESP32-S3_SuperMini_CNC": dict(
        descr="ESP32-S3 SuperMini (22.5x18 mm) in 2x9 female sockets, rows 15.24 mm. Top view, USB-C end at -Y. "
              "Pins 1-9 left col top->bottom: TX RX 1 2 3 4 5 6 7; 10-18 right col: 5V GND 3V3 13 12 11 10 9 8. "
              "Bottom-copper oval pads for CNC milling.",
        pads=tht_rowpads(9, 2.54) + tht_rowpads(9, 2.54, first_rect=False, x=15.24, start=10),
        gfx=[("rect", "F.SilkS", -1.75, -1.1, 16.99, 21.42, 0.12),
             ("rect", "F.Fab", -1.38, -2.6, 16.62, 20.9, 0.1),          # module body
             ("rect", "F.Fab", 3.87, -3.6, 11.37, -0.4, 0.1),           # USB-C
             ("rect", "F.CrtYd", -2.4, -4.0, 17.64, 22.0, 0.05),
             ("dot", "F.SilkS", 0, -1.6)],
        ref_at=(7.62, -5.0)),
    "MAX98357A_Module_CNC": dict(
        descr="MAX98357A I2S amp breakout (~18x18 mm), 1x7 pins: LRC BCLK DIN GAIN SD GND VIN. Speaker screw terminal on the "
              "side opposite the pins (+X). Bottom-copper oval pads for CNC milling.",
        pads=tht_rowpads(7, 2.54),
        gfx=[("rect", "F.SilkS", -1.75, -1.1, 1.75, 16.34, 0.12),
             ("rect", "F.Fab", -1.5, -1.5, 16.5, 16.8, 0.1),
             ("rect", "F.Fab", 11.5, 4, 16.5, 11.5, 0.1),
             ("rect", "F.CrtYd", -2, -2, 17, 17.3, 0.05),
             ("dot", "F.SilkS", 0, -1.5)],
        ref_at=(0, -2.45)),
    "OLED_1.5in_128x128_I2C_CNC": dict(
        descr="1.5in 128x128 SH1107 I2C OLED module (GME128128-01-IIC), ~40.3 x 35.5 mm, 1x4 header on a short edge: "
              "VCC GND SCL SDA. Top view, display side up, body extends +X from the header. Dimensions measured from a photo "
              "- check with the 1:1 print. Bottom-copper oval pads for CNC milling.",
        pads=tht_rowpads(4, 2.54),
        gfx=[("rect", "F.SilkS", -1.75, -1.1, 1.75, 8.72, 0.12),
             ("rect", "F.Fab", -1.4, -13.94, 38.9, 21.56, 0.1),         # module PCB
             ("rect", "F.Fab", 4.0, -9.5, 34.0, 17.1, 0.1),             # glass
             ("rect", "F.CrtYd", -1.5, -14.04, 39.0, 21.66, 0.05),
             ("dot", "F.SilkS", 0, -1.5)],
        ref_at=(0, -2.45)),
    "EdgePads_Keypad4x4_CNC": dict(
        descr="Edge pads for a 4x4 tact keypad module (QYF-JP01 style, ~46 x 33 mm) with a RIGHT-ANGLE 1x8 header: "
              "R1 R2 R3 R4 C1 C2 C3 C4. The keypad hangs off the board edge (+Y), right side up, and its pins lie flat on "
              "these B.Cu pads. Keypad outline (F.Fab) estimated from a photo. Board edge 2.8 mm beyond the pad centres.",
        pads=edge_pads(8),
        gfx=[("rect", "F.Fab", -13.3, 5.8, 32.7, 38.8, 0.1),            # keypad PCB (off-board)
             ("rect", "F.Fab", -1.27, 2.8, 19.05, 5.3, 0.1),            # header plastic
             ("rect", "F.SilkS", -1.2, -2.6, 18.98, 2.2, 0.12),
             ("rect", "F.CrtYd", -1.3, -2.75, 19.08, 2.3, 0.05),
             ("dot", "F.SilkS", -1.6, -2.6)],
        ref_at=(8.89, -3.8)),
    "EdgePads_Pot_CNC": dict(
        descr="Edge pads for a pot module with a RIGHT-ANGLE 1x3 header (outer leg, wiper, outer leg). The module hangs off "
              "the board edge (+Y), knob up, and its pins lie flat on these B.Cu pads. Outline estimated from a photo. "
              "Board edge 2.8 mm beyond the pad centres.",
        pads=edge_pads(3),
        gfx=[("rect", "F.Fab", -5.5, 3.3, 10.58, 15.3, 0.1),            # pot module PCB (off-board)
             ("circle", "F.Fab", 2.54, 9.3, 8.5, 0.1),                  # knob
             ("rect", "F.SilkS", -1.2, -2.6, 6.28, 2.2, 0.12),
             ("rect", "F.CrtYd", -1.3, -2.75, 6.38, 2.3, 0.05),
             ("dot", "F.SilkS", -1.6, -2.6)],
        ref_at=(2.54, -3.8)),
}


def gfx_sexp(g, ind):
    t = g[0]
    if t == "rect":
        _, layer, x1, y1, x2, y2, w = g
        return (f"{ind}(fp_rect (start {x1} {y1}) (end {x2} {y2}) (stroke (width {w}) (type solid)) (fill no) "
                f"(layer \"{layer}\") (uuid \"{U()}\"))\n")
    if t == "line":
        _, layer, x1, y1, x2, y2, w = g
        return (f"{ind}(fp_line (start {x1} {y1}) (end {x2} {y2}) (stroke (width {w}) (type solid)) "
                f"(layer \"{layer}\") (uuid \"{U()}\"))\n")
    if t == "circle":
        _, layer, x, y, r, w = g
        return (f"{ind}(fp_circle (center {x} {y}) (end {x + r} {y}) (stroke (width {w}) (type solid)) (fill no) "
                f"(layer \"{layer}\") (uuid \"{U()}\"))\n")
    if t == "dot":
        _, layer, x, y = g
        return (f"{ind}(fp_circle (center {x} {y}) (end {x + 0.15} {y}) (stroke (width 0.1) (type solid)) (fill yes) "
                f"(layer \"{layer}\") (uuid \"{U()}\"))\n")
    raise ValueError(t)


def fp_attr(d):
    return "smd" if all(p[7] is None for p in d["pads"]) else "through_hole"


def pad_sexp(num, kind, shape, x, y, sx, sy, drill, ang, extra=""):
    if drill is None:
        return (f'(pad "{num}" smd {shape} (at {x} {y} {ang}) (size {sx} {sy}) (layers "B.Cu" "B.Mask"){extra} '
                f'(uuid "{U()}"))')
    return (f'(pad "{num}" {kind} {shape} (at {x} {y} {ang}) (size {sx} {sy}) (drill {drill}) '
            f'(layers "B.Cu" "B.Mask") (remove_unused_layers no){extra} (uuid "{U()}"))')


def write_lib_footprint(name, d):
    s = f'(footprint "{name}"\n\t(version 20241229)\n\t(generator "claude_gen")\n\t(layer "F.Cu")\n'
    s += f'\t(descr "{d["descr"]}")\n'
    rx, ry = d["ref_at"]
    s += (f'\t(property "Reference" "REF**" (at {rx} {ry} 0) (layer "F.SilkS") (uuid "{U()}") '
          f'(effects (font (size 1 1) (thickness 0.15))))\n')
    s += (f'\t(property "Value" "{name}" (at {rx} {-ry + 2} 0) (layer "F.Fab") (hide yes) (uuid "{U()}") '
          f'(effects (font (size 1 1) (thickness 0.15))))\n')
    s += f'\t(attr {fp_attr(d)})\n'
    for g in d["gfx"]:
        s += gfx_sexp(g, "\t")
    for p in d["pads"]:
        s += "\t" + pad_sexp(*p, 0) + "\n"
    s += '\t(embedded_fonts no)\n)\n'
    open(os.path.join(OUT, FPLIB + ".pretty", name + ".kicad_mod"), "w", encoding="utf8").write(s)


for n, d in FOOTPRINTS.items():
    write_lib_footprint(n, d)

# ---------------------------------------------------------------- symbols
SUPERMINI_PINS = (
    [(str(i + 1), nm, -12.7, 10.16 - i * 2.54, 0) for i, nm in
     enumerate(["TX", "RX", "IO1", "IO2", "IO3", "IO4", "IO5", "IO6", "IO7"])] +
    [(str(i + 10), nm, 12.7, 10.16 - i * 2.54, 180) for i, nm in
     enumerate(["5V", "GND", "3V3", "IO13", "IO12", "IO11", "IO10", "IO9", "IO8"])])


def symbol(name, value, fp, descr, pins, half_w, half_h):
    """pins: (number, name, x, y, angle)."""
    body = "".join(
        f'\t\t\t(pin passive line (at {x} {y} {a}) (length 2.54) (name "{nm}" (effects (font (size 1.27 1.27)))) '
        f'(number "{num}" (effects (font (size 1.27 1.27)))))\n' for num, nm, x, y, a in pins)
    base = name.split(":")[-1]
    return (f'\t\t(symbol "{name}"\n\t\t\t(pin_names (offset 1.016))\n\t\t\t(exclude_from_sim no) (in_bom yes) (on_board yes)\n'
            f'\t\t\t(property "Reference" "U" (at 0 {half_h + 1.27} 0) (effects (font (size 1.27 1.27))))\n'
            f'\t\t\t(property "Value" "{value}" (at 0 {-half_h - 1.27} 0) (effects (font (size 1.27 1.27))))\n'
            f'\t\t\t(property "Footprint" "{FPLIB}:{fp}" (at 0 0 0) (hide yes) (effects (font (size 1.27 1.27))))\n'
            f'\t\t\t(property "Datasheet" "" (at 0 0 0) (hide yes) (effects (font (size 1.27 1.27))))\n'
            f'\t\t\t(property "Description" "{descr}" (at 0 0 0) (hide yes) (effects (font (size 1.27 1.27))))\n'
            f'\t\t\t(symbol "{base}_0_1" (rectangle (start {-half_w} {half_h}) (end {half_w} {-half_h}) '
            f'(stroke (width 0.254) (type default)) (fill (type background))))\n'
            f'\t\t\t(symbol "{base}_1_1"\n{body}\t\t\t)\n\t\t\t(embedded_fonts no)\n\t\t)\n')


def left_pins(names):
    """Single-row module: all pins on the left, top to bottom, on the 2.54 grid."""
    top = 2.54 * (len(names) // 2)
    return [(str(i + 1), nm, -10.16, round(top - i * 2.54, 2), 0) for i, nm in enumerate(names)]


SYMS = {
    "ESP32-S3_SuperMini": lambda n: symbol(n, "ESP32-S3_SuperMini", "ESP32-S3_SuperMini_CNC",
                                           "ESP32-S3 SuperMini dev board in 2x9 sockets (top view, USB-C up)",
                                           SUPERMINI_PINS, 10.16, 12.7),
    "OLED_128x128_I2C": lambda n: symbol(n, "OLED 1.5in SH1107", "OLED_1.5in_128x128_I2C_CNC",
                                         "1.5in 128x128 SH1107 I2C OLED module, I2C address 0x3C",
                                         left_pins(["VCC", "GND", "SCL", "SDA"]), 7.62, 7.62),
    "Keypad_4x4_Module": lambda n: symbol(n, "Keypad 4x4", "EdgePads_Keypad4x4_CNC",
                                          "4x4 tact switch matrix module (passive, no diodes), right-angle 1x8 header",
                                          left_pins(["R1", "R2", "R3", "R4", "C1", "C2", "C3", "C4"]), 7.62, 12.7),
    "Pot_Module": lambda n: symbol(n, "Pot 10k", "EdgePads_Pot_CNC",
                                   "Potentiometer module, right-angle 1x3 header: outer leg, wiper, outer leg",
                                   left_pins(["A", "W", "B"]), 7.62, 5.08),
}

revb_sch = open(os.path.join(REVB, "ttgo_audio_revB.kicad_sch"), encoding="utf8").read()
revb_sym = open(os.path.join(REVB, "ttgo_revB.kicad_sym"), encoding="utf8").read()
amp_lib = sexp_block(revb_sym, '(symbol "MAX98357A_Module"').replace(
    'ttgo_cnc:MAX98357A_Module_CNC', f'{FPLIB}:MAX98357A_Module_CNC')

open(os.path.join(OUT, SYMLIB + ".kicad_sym"), "w", encoding="utf8").write(
    '(kicad_symbol_lib\n\t(version 20241209)\n\t(generator "claude_gen")\n\t(generator_version "10.0")\n'
    + "".join(f(n) for n, f in SYMS.items()) + "\t" + amp_lib + "\n)\n")

embedded = {
    f"{SYMLIB}:MAX98357A_Module": sexp_block(revb_sch, '(symbol "ttgo_revB:MAX98357A_Module"').replace(
        '"ttgo_revB:MAX98357A_Module"', f'"{SYMLIB}:MAX98357A_Module"').replace(
        'ttgo_cnc:MAX98357A_Module_CNC', f'{FPLIB}:MAX98357A_Module_CNC'),
}
for n, f in SYMS.items():
    embedded[f"{SYMLIB}:{n}"] = f(f"{SYMLIB}:{n}")


def sym_pins(block):
    out = []
    for m in re.finditer(r'\(pin \w+ \w+\s*\(at ([-\d\.]+) ([-\d\.]+) (\d+)\)[\s\S]*?\(name "([^"]*)"[\s\S]*?\(number "([^"]*)"', block):
        out.append((m.group(5), m.group(4), float(m.group(1)), float(m.group(2)), int(m.group(3))))
    return out


# ---------------------------------------------------------------- netlist
# ref: (lib_id, value, footprint, sch_xy, {pin_number: net or None(no-connect)}, ref/value label offset)
NC = None
PARTS = {
    "U1": (f"{SYMLIB}:ESP32-S3_SuperMini", "ESP32-S3_SuperMini", "ESP32-S3_SuperMini_CNC", (101.6, 88.9),
           {"1": "I2S_DIN", "2": "I2S_BCLK", "3": "I2S_LRC", "4": "KEY_R1", "5": "KEY_R2", "6": "KEY_R3",
            "7": "KEY_R4", "8": "KEY_C1", "9": "KEY_C2",
            "10": "+5V", "11": "GND", "12": "+3V3", "13": "KEY_C3", "14": "KEY_C4", "15": "POT_HI",
            "16": "POT_WIPER", "17": "I2C_SDA", "18": "I2C_SCL"}, 13.97),
    "U2": (f"{SYMLIB}:MAX98357A_Module", "MAX98357A", "MAX98357A_Module_CNC", (190.5, 88.9),
           {"1": "I2S_LRC", "2": "I2S_BCLK", "3": "I2S_DIN", "4": NC, "5": NC, "6": "GND", "7": "+5V"}, 12.7),
    "U3": (f"{SYMLIB}:OLED_128x128_I2C", "OLED 1.5in SH1107", "OLED_1.5in_128x128_I2C_CNC", (190.5, 50.8),
           {"1": "+3V3", "2": "GND", "3": "I2C_SCL", "4": "I2C_SDA"}, 8.89),
    "U4": (f"{SYMLIB}:Keypad_4x4_Module", "Keypad 4x4", "EdgePads_Keypad4x4_CNC", (45.72, 88.9),
           {"1": "KEY_R1", "2": "KEY_R2", "3": "KEY_R3", "4": "KEY_R4",
            "5": "KEY_C1", "6": "KEY_C2", "7": "KEY_C3", "8": "KEY_C4"}, 13.97),
    "U5": (f"{SYMLIB}:Pot_Module", "Pot 10k", "EdgePads_Pot_CNC", (190.5, 127.0),
           {"1": "POT_HI", "2": "POT_WIPER", "3": "GND"}, 6.35),
}

ROOT = U()
SYM_UUID = {r: U() for r in PARTS}


def pcb_net(ref, pin_num, pin_name):
    net = PARTS[ref][4][pin_num]
    return f"/{net}" if net else f"unconnected-({ref}-{pin_name}-Pad{pin_num})"


# ---------------------------------------------------------------- schematic
def label_dir(angle):
    # pin angle points from connection point toward the body; outward is the opposite way
    return {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}[angle]


sch = [f'(kicad_sch\n\t(version 20250610)\n\t(generator "claude_gen")\n\t(generator_version "10.0")\n'
       f'\t(uuid "{ROOT}")\n\t(paper "A4")\n'
       f'\t(title_block\n\t\t(title "Keypad Synth Rev A")\n\t\t(rev "A")\n'
       f'\t\t(comment 1 "ESP32-S3 SuperMini + 1.5in SH1107 OLED + 4x4 keypad + pot + MAX98357A I2S amp")\n'
       f'\t\t(comment 2 "Single-sided CNC board, all copper on B.Cu; keypad and pot on edge pads")\n\t)\n\t(lib_symbols\n']
for k, blk in embedded.items():
    sch.append("\t\t" + blk.strip() + "\n")
sch.append("\t)\n")

items = []
for ref, (lib, val, fp, (X, Y), nets, lab_off) in PARTS.items():
    for num, name, px, py, ang in sym_pins(embedded[lib]):
        sx, sy = round(X + px, 4), round(Y - py, 4)
        net = nets.get(num, "MISSING")
        if net == "MISSING":
            raise SystemExit(f"{ref} pin {num} not assigned")
        if net is None:
            items.append(f'\t(no_connect (at {sx} {sy}) (uuid "{U()}"))\n')
            continue
        dx, dy = label_dir(ang)
        ex, ey = round(sx + dx * 2.54, 4), round(sy + dy * 2.54, 4)
        items.append(f'\t(wire (pts (xy {sx} {sy}) (xy {ex} {ey})) (stroke (width 0) (type default)) (uuid "{U()}"))\n')
        la, just = {(-1, 0): (180, "right bottom"), (1, 0): (0, "left bottom"),
                    (0, 1): (270, "right bottom"), (0, -1): (90, "left bottom")}[(dx, dy)]
        items.append(f'\t(label "{net}" (at {ex} {ey} {la}) (effects (font (size 1.27 1.27)) (justify {just})) (uuid "{U()}"))\n')
    items.append(
        f'\t(symbol\n\t\t(lib_id "{lib}")\n\t\t(at {X} {Y} 0)\n\t\t(unit 1)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(dnp no)\n'
        f'\t\t(uuid "{SYM_UUID[ref]}")\n'
        f'\t\t(property "Reference" "{ref}" (at {X} {Y - lab_off} 0) (effects (font (size 1.27 1.27))))\n'
        f'\t\t(property "Value" "{val}" (at {X} {Y + lab_off} 0) (effects (font (size 1.27 1.27))))\n'
        f'\t\t(property "Footprint" "{FPLIB}:{fp}" (at {X} {Y} 0) (hide yes) (effects (font (size 1.27 1.27))))\n'
        f'\t\t(property "Datasheet" "" (at {X} {Y} 0) (hide yes) (effects (font (size 1.27 1.27))))\n'
        f'\t\t(property "Description" "" (at {X} {Y} 0) (hide yes) (effects (font (size 1.27 1.27))))\n'
        f'\t\t(instances (project "{NAME}" (path "/{ROOT}" (reference "{ref}") (unit 1))))\n\t)\n')

NOTE = ("FIRMWARE PIN MAP (ESP32-S3 SuperMini)\\n"
        "OLED I2C (SH1107 128x128, addr 0x3C): SDA = GPIO9, SCL = GPIO8, powered from 3V3\\n"
        "Keypad rows R1-R4 = GPIO 2 3 4 5, columns C1-C4 = GPIO 6 7 13 12 (passive matrix, no diodes)\\n"
        "Pot: wiper = GPIO10 (ADC1_CH9); high leg = GPIO11 driven HIGH; low leg = GND\\n"
        "Amp I2S: LRC = GPIO1, BCLK = RX (GPIO44), DIN = TX (GPIO43), VIN = 5V. Needs USB CDC On Boot = Enabled\\n"
        "All 15 header GPIOs used. Onboard RGB LED: GPIO48")
items.append(f'\t(text "{NOTE}" (exclude_from_sim no) (at 30.48 152.4 0) (effects (font (size 1.524 1.524)) (justify left bottom)) (uuid "{U()}"))\n')
sch += items
sch.append('\t(sheet_instances (path "/" (page "1")))\n\t(embedded_fonts no)\n)\n')
open(os.path.join(OUT, NAME + ".kicad_sch"), "w", encoding="utf8").write("".join(sch))

# ---------------------------------------------------------------- PCB placement


def rot(px, py, deg):
    # KiCad: positive angle = counter-clockwise on screen (y down)
    a = math.radians(deg)
    return (px * math.cos(a) + py * math.sin(a), -px * math.sin(a) + py * math.cos(a))


W, H, R = 96, 66, 4
P = 2.54
# SuperMini rotated 90 so USB-C faces the left edge. Pin 1 (TX) at (SX, YL); the lower row is TX RX 1..7 (left -> right),
# the upper row (YU) is 5V GND 3V3 13 12 11 10 9 8 (left -> right).
SX, YL = 7.0, 25.24
YU = round(YL - 15.24, 2)
col = lambda k: round(SX + k * P, 2)           # k-th pad from the left in either row
OLED_X, OLED_Y = 39.5, 18.0                    # OLED pin 1 (VCC); rotation 0 -> pins go down, body to the right
AMP_X, AMP_Y = 18.74, 41.5                     # amp pin 1 (LRC); rotation 270 -> row goes left to VIN, body below
EDGE_IN = 2.8                                  # edge-pad centre to board edge
KP_X, KP_Y = 38.1, H - EDGE_IN                 # keypad pad 1 (R1); pads go right, keypad hangs below the board
POT_X, POT_Y = W - EDGE_IN, 35.54              # pot pad 1 (high leg); rotation 90 -> pads go up, pot hangs to the right

PLACE = {
    "U1": (SX, YL, 90),
    "U2": (AMP_X, AMP_Y, 270),
    "U3": (OLED_X, OLED_Y, 0),
    "U4": (KP_X, KP_Y, 0),
    "U5": (POT_X, POT_Y, 90),
}

pad_xy = {}  # (ref, padnum) -> list of absolute (x,y)


def board_fp(ref):
    lib, val, fpname, _, nets, _ = PARTS[ref]
    d = FOOTPRINTS[fpname]
    x, y, r = PLACE[ref]
    pins = {num: name for num, name, *_ in sym_pins(embedded[lib])}
    s = f'\t(footprint "{FPLIB}:{fpname}"\n\t\t(layer "F.Cu")\n\t\t(uuid "{U()}")\n\t\t(at {OX + x} {OY + y} {r})\n'
    s += f'\t\t(descr "{d["descr"]}")\n'
    rx, ry = d["ref_at"]
    s += (f'\t\t(property "Reference" "{ref}" (at {rx} {ry} {r}) (layer "F.SilkS") (uuid "{U()}") '
          f'(effects (font (size 1 1) (thickness 0.15))))\n')
    s += (f'\t\t(property "Value" "{val}" (at {rx} {-ry + 2} {r}) (layer "F.Fab") (hide yes) (uuid "{U()}") '
          f'(effects (font (size 1 1) (thickness 0.15))))\n')
    s += (f'\t\t(property "Footprint" "{FPLIB}:{fpname}" (at 0 0 {r}) (layer "F.Fab") (hide yes) (uuid "{U()}") '
          f'(effects (font (size 1.27 1.27))))\n')
    s += f'\t\t(path "/{SYM_UUID[ref]}")\n\t\t(sheetname "/")\n\t\t(sheetfile "{NAME}.kicad_sch")\n'
    s += f'\t\t(attr {fp_attr(d)})\n'
    for g in d["gfx"]:
        s += gfx_sexp(g, "\t\t")
    for (num, kind, shape, px, py, sx, sy, drill) in d["pads"]:
        net = pcb_net(ref, num, pins[num])
        dx, dy = rot(px, py, r)
        pad_xy.setdefault((ref, num), []).append((round(x + dx, 4), round(y + dy, 4)))
        extra = f' (net "{net}") (pinfunction "{pins[num]}") (pintype "passive")'
        s += "\t\t" + pad_sexp(num, kind, shape, px, py, sx, sy, drill, r % 360, extra) + "\n"
    s += '\t\t(embedded_fonts no)\n\t)\n'
    return s


def hole(ref, x, y):
    return (f'\t(footprint "MountingHole:MountingHole_3.2mm_M3"\n\t\t(layer "F.Cu")\n\t\t(uuid "{U()}")\n'
            f'\t\t(at {OX + x} {OY + y})\n'
            f'\t\t(property "Reference" "{ref}" (at 0 -4.15 0) (layer "F.SilkS") (hide yes) (uuid "{U()}") (effects (font (size 1 1) (thickness 0.15))))\n'
            f'\t\t(property "Value" "MountingHole_3.2mm_M3" (at 0 4.15 0) (layer "F.Fab") (hide yes) (uuid "{U()}") (effects (font (size 1 1) (thickness 0.15))))\n'
            f'\t\t(attr exclude_from_pos_files exclude_from_bom board_only)\n'
            f'\t\t(fp_circle (center 0 0) (end 3.2 0) (stroke (width 0.15) (type solid)) (fill no) (layer "Cmts.User") (uuid "{U()}"))\n'
            f'\t\t(fp_circle (center 0 0) (end 3.45 0) (stroke (width 0.05) (type solid)) (fill no) (layer "F.CrtYd") (uuid "{U()}"))\n'
            f'\t\t(pad "" np_thru_hole circle (at 0 0) (size 3.2 3.2) (drill 3.2) (layers "*.Cu" "*.Mask") (uuid "{U()}"))\n'
            f'\t\t(embedded_fonts no)\n\t)\n')


# ---------------------------------------------------------------- routing (board-local mm)
SIG, PWR = 0.75, 1.0
# upper row pads: 0 5V, 1 GND, 2 3V3, 3 IO13, 4 IO12, 5 IO11, 6 IO10, 7 IO9, 8 IO8
# lower row pads: 0 TX, 1 RX, 2 IO1, 3 IO2 ... 8 IO7
# channel lanes (between the rows, leaving to the right): IO9 top ... IO13 bottom
LANE = {7: 12.6, 6: 14.1, 5: 15.6, 4: 17.1, 3: 18.6}
# where each upper-row signal turns down after leaving the channel (outermost = furthest right)
TURN = {8: 36.7, 7: 35.2, 6: 33.7, 5: 32.2, 4: 30.7, 3: 29.2}
oled = lambda k: round(OLED_Y + k * P, 2)      # 0 VCC, 1 GND, 2 SCL, 3 SDA
amp = lambda k: round(AMP_X - k * P, 2)        # 0 LRC, 1 BCLK, 2 DIN, 5 GND, 6 VIN
kp = lambda k: round(KP_X + k * P, 2)          # 0 R1 ... 7 C4
pot = lambda k: round(POT_Y - k * P, 2)        # 0 high leg, 1 wiper, 2 GND
FAN_Y = 33.0                                   # keypad fan: verticals drop to here, then 45 deg to the pads


def fan(x0, y_start, x1):
    """Vertical from (x0, y_start) to FAN_Y, 45 deg down-right to x1, then down to the keypad pad."""
    return [(x0, y_start), (x0, FAN_Y), (x1, round(FAN_Y + x1 - x0, 2)), (x1, KP_Y)]


ROUTES = [
    # power: 3V3 over the top to the OLED, 5V down the left edge to the amp
    ("+3V3", PWR, [(col(2), YU), (col(2), 3.5), (OLED_X, 3.5), (OLED_X, oled(0))]),
    ("+5V", PWR, [(col(0), YU), (col(0), 17.0), (3.5, 20.5), (3.5, AMP_Y)]),
    # OLED I2C (IO8 goes straight out the end of the row, IO9 through the top channel lane)
    ("I2C_SCL", SIG, [(col(8), YU), (TURN[8], YU), (TURN[8], oled(2)), (OLED_X, oled(2))]),
    ("I2C_SDA", SIG, [(col(7), YU), (col(7), LANE[7]), (TURN[7], LANE[7]), (TURN[7], oled(3)), (OLED_X, oled(3))]),
    # pot: under the OLED header, then across under the OLED to the right edge
    ("POT_WIPER", SIG, [(col(6), YU), (col(6), LANE[6]), (TURN[6], LANE[6]), (TURN[6], 27.5), (79.5, 27.5),
                        (79.5 + pot(1) - 27.5, pot(1)), (POT_X, pot(1))]),
    ("POT_HI", SIG, [(col(5), YU), (col(5), LANE[5]), (TURN[5], LANE[5]), (TURN[5], 29.0), (78.0, 29.0),
                     (78.0 + pot(0) - 29.0, pot(0)), (POT_X, pot(0))]),
    # keypad C4 / C3 from the upper row (C4 starts its diagonal 0.5 mm early to keep clear of C3)
    ("KEY_C4", SIG, [(col(4), YU), (col(4), LANE[4]), (TURN[4], LANE[4]), (TURN[4], FAN_Y - 0.5),
                     (kp(7), round(FAN_Y - 0.5 + kp(7) - TURN[4], 2)), (kp(7), KP_Y)]),
    ("KEY_C3", SIG, [(col(3), YU), (col(3), LANE[3])] + fan(TURN[3], LANE[3], kp(6))),
    # keypad R1-R4, C1, C2 straight down from the lower row
    ("KEY_R1", SIG, fan(col(3), YL, kp(0))),
    ("KEY_R2", SIG, fan(col(4), YL, kp(1))),
    ("KEY_R3", SIG, fan(col(5), YL, kp(2))),
    ("KEY_R4", SIG, fan(col(6), YL, kp(3))),
    ("KEY_C1", SIG, fan(col(7), YL, kp(4))),
    ("KEY_C2", SIG, fan(col(8), YL, kp(5))),
    # amp I2S from the left end of the lower row, jogging right to the amp header
    ("I2S_DIN", SIG, [(col(0), YL), (col(0), 34.0), (amp(2), round(34.0 + amp(2) - col(0), 2)), (amp(2), AMP_Y)]),
    ("I2S_BCLK", SIG, [(col(1), YL), (col(1), 34.0), (amp(1), round(34.0 + amp(1) - col(1), 2)), (amp(1), AMP_Y)]),
    ("I2S_LRC", SIG, [(col(2), YL), (col(2), 34.0), (amp(0), round(34.0 + amp(0) - col(2), 2)), (amp(0), AMP_Y)]),
]

revb_pcb = open(os.path.join(REVB, "ttgo_audio_revB.kicad_pcb"), encoding="utf8").read()
header = revb_pcb[:revb_pcb.index("\n\t(footprint")]
pcb = [header + "\n"]
for ref in PARTS:
    pcb.append(board_fp(ref))
for i, (hx, hy) in enumerate([(4, 4), (92, 4), (92, 62), (4, 62)], 1):
    pcb.append(hole(f"H{i}", hx, hy))

# sanity: route endpoints that sit on pads must sit on a pad of the same net, and every netted pad gets a route
pad_net = {}
for (ref, num), pts in pad_xy.items():
    lib = PARTS[ref][0]
    pname = {n: nm for n, nm, *_ in sym_pins(embedded[lib])}[num]
    for p in pts:
        pad_net[p] = pcb_net(ref, num, pname)
hit = set()
for net, w, pts in ROUTES:
    for p in (pts[0], pts[-1]):
        key = (round(p[0], 4), round(p[1], 4))
        if key not in pad_net:
            raise SystemExit(f"route {net} end {p} is not on a pad")
        if pad_net[key] != f"/{net}":
            raise SystemExit(f"route {net} ends on pad of {pad_net[key]} at {p}")
        hit.add(key)
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        if (x1, y1) == (x2, y2):
            continue
        if not (x1 == x2 or y1 == y2 or abs(abs(x2 - x1) - abs(y2 - y1)) < 1e-6):
            raise SystemExit(f"route {net} segment {(x1, y1)}->{(x2, y2)} is not 0/45/90 deg")
        pcb.append(f'\t(segment (start {round(OX + x1, 4)} {round(OY + y1, 4)}) (end {round(OX + x2, 4)} {round(OY + y2, 4)}) '
                   f'(width {w}) (layer "B.Cu") (net "/{net}") (uuid "{U()}"))\n')
# the OLED GND pad is boxed in on the left by the SCL run, so it gets a solid stub out into the open pour
for a, b in [((OLED_X, oled(1)), (OLED_X + 4.5, oled(1)))]:
    if pad_net[a] != "/GND":
        raise SystemExit(f"GND stub starts on {pad_net[a]}")
    pcb.append(f'\t(segment (start {OX + a[0]} {OY + a[1]}) (end {OX + b[0]} {OY + b[1]}) '
               f'(width {SIG}) (layer "B.Cu") (net "/GND") (uuid "{U()}"))\n')
for p, n in pad_net.items():
    if n.startswith("/") and n != "/GND" and p not in hit:
        print("WARNING: pad without a route:", n, p)

# board outline: 96 x 66, r=4 corners
def L(x1, y1, x2, y2):
    return (f'\t(gr_line (start {OX + x1} {OY + y1}) (end {OX + x2} {OY + y2}) (stroke (width 0.05) (type default)) '
            f'(layer "Edge.Cuts") (uuid "{U()}"))\n')
def A(sx, sy, mx, my, ex, ey):
    return (f'\t(gr_arc (start {OX + sx} {OY + sy}) (mid {OX + mx} {OY + my}) (end {OX + ex} {OY + ey}) '
            f'(stroke (width 0.05) (type default)) (layer "Edge.Cuts") (uuid "{U()}"))\n')
k = R - R / math.sqrt(2)
pcb += [L(R, 0, W - R, 0), L(W, R, W, H - R), L(W - R, H, R, H), L(0, H - R, 0, R),
        A(0, R, k, k, R, 0), A(W - R, 0, W - k, k, W, R), A(W, H - R, W - k, H - k, W - R, H), A(R, H, k, H - k, 0, H - R)]

pcb.append(f'\t(zone (net "/GND") (layer "B.Cu") (uuid "{U()}") (name "GND_pour") (hatch edge 0.508)\n'
           f'\t\t(connect_pads (clearance 0.6)) (min_thickness 0.4)\n'
           f'\t\t(fill yes (thermal_gap 0.5) (thermal_bridge_width 0.5) (island_removal_mode 0))\n'
           f'\t\t(polygon (pts (xy {OX} {OY}) (xy {OX + W} {OY}) (xy {OX + W} {OY + H}) (xy {OX} {OY + H}))))\n')
pcb.append('\t(embedded_fonts no)\n)\n')
open(os.path.join(OUT, NAME + ".kicad_pcb"), "w", encoding="utf8").write("".join(pcb))

# ---------------------------------------------------------------- project + lib tables
pro = json.load(open(os.path.join(REVB, "ttgo_audio_revB.kicad_pro"), encoding="utf8"))
pro["meta"]["filename"] = NAME + ".kicad_pro"
pro["sheets"] = [[ROOT, "Root"]]
for c in pro["net_settings"]["classes"]:
    if c["name"] == "Default":
        c["clearance"] = 0.4
        c["track_width"] = 0.75
pro.get("pcbnew", {}).pop("last_paths", None)
json.dump(pro, open(os.path.join(OUT, NAME + ".kicad_pro"), "w", encoding="utf8"), indent=2)
open(os.path.join(OUT, "fp-lib-table"), "w").write(
    f'(fp_lib_table\n  (version 7)\n  (lib (name "{FPLIB}") (type "KiCad") (uri "${{KIPRJMOD}}/{FPLIB}.pretty") (options "") (descr "")))\n')
open(os.path.join(OUT, "sym-lib-table"), "w").write(
    f'(sym_lib_table\n  (version 7)\n  (lib (name "{SYMLIB}") (type "KiCad") (uri "${{KIPRJMOD}}/{SYMLIB}.kicad_sym") (options "") (descr "")))\n')

for ref, pts in sorted(pad_xy.items()):
    print(ref, pts)
print("written to", OUT)
