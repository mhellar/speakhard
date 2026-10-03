"""Make FlatCAM drill G-code safe on controllers in GRBL laser mode ($32=1),
where the spindle only runs during G1 moves (not during G0 or G4 dwell).
- after M03, spin up with slow G1 moves in the air (~6 s) instead of relying on G4
- every G00 becomes G01 at F1500 so the spindle never stops between holes
- every plunge gets an explicit feed (F100)
Run: python fix_drill_spinup.py file1.nc file2.nc ...   (keeps a .orig copy)"""
import sys, re, shutil
for path in sys.argv[1:]:
    src = open(path).read()
    if "SPINUP-PATCHED" in src:
        print(path, "already patched"); continue
    shutil.copy(path, path + ".orig")
    out = []
    for line in src.splitlines():
        s = line.strip()
        if s.startswith("G4 P"):
            out += ["(SPINUP-PATCHED: spin up during slow G1 moves in the air)",
                    "G01 Z5.0000 F60", "G01 Z2.0000 F60", "G01 Z5.0000 F60", "G01 Z2.0000 F60"]
            continue
        if s.startswith("G00 "):
            out.append("G01 " + s[4:] + " F1500"); continue
        if re.match(r"G01 Z-\d", s) and " F" not in s:
            out.append(s + " F100"); continue
        out.append(line)
    open(path, "w").write("\n".join(out) + "\n")
    print(path, "patched")
