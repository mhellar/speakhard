"""Run with KiCad's python after gen_board.py: fills the GND pour and re-saves the library footprints from the board
(so the .pretty files are in KiCad's own format and match the board exactly)."""
import os, pcbnew
OUT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
f = os.path.join(OUT, "keypad_synth_revA.kicad_pcb")
lib = os.path.join(OUT, "ks_cnc.pretty")
b = pcbnew.LoadBoard(f)
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
b.Save(f)
io = pcbnew.PCB_IO_KICAD_SEXPR()
done = set()
for fp in b.GetFootprints():
    lid = fp.GetFPID()
    if str(lid.GetLibNickname()) != "ks_cnc" or str(lid.GetLibItemName()) in done:
        continue
    c = pcbnew.FOOTPRINT(fp)
    c.SetParent(None)
    c.SetOrientationDegrees(0); c.SetPosition(pcbnew.VECTOR2I(0, 0))
    c.SetReference("REF**"); c.SetValue(str(lid.GetLibItemName()))
    c.SetPath(pcbnew.KIID_PATH())
    for p in c.Pads():
        p.SetNetCode(0)
    io.FootprintSave(lib, c)
    done.add(str(lid.GetLibItemName()))
print("filled; synced", sorted(done))
