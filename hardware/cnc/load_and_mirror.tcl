# Keypad Synth Rev A: FlatCAM script -> ready-to-run G-code for the Genmitsu Cubiko.
# Run in FlatCAM: File > Scripting > Run Script... (Shift+S), pick this file.
# Same proven settings as s3_audio_revA: 0.1 mm 20deg V-bit, 1.0 mm drill, 2.0 mm drill, "default" preprocessor,
# spindle S10000 + 3 s pause before the first plunge, drill plunge 100 mm/min.

set d [file dirname [file normalize [info script]]]   ;# the folder this script is in
set out $d

open_gerber   "$d/keypad_synth_revA-B_Cu.gbl"       -outname copper
open_gerber   "$d/keypad_synth_revA-Edge_Cuts.gm1"  -outname outline
open_excellon "$d/keypad_synth_revA-PTH.drl"        -outname holes
open_excellon "$d/keypad_synth_revA-NPTH.drl"       -outname mounting

# Flip left<->right around the board centre (the copper is on the bottom).
# Note: the Tcl command swaps axis names vs. the GUI, so "-axis X" here = "Y" in the 2-Sided tool.
mirror copper outline holes mounting -box outline -axis X

# Move the board so the outline's lower-left corner is at 0,0
set b [lindex [bounds outline] 0]
set dx [expr {-[lindex $b 0]}]
set dy [expr {-[lindex $b 1]}]
offset copper outline holes mounting -x $dx -y $dy

# 1) Isolation: 0.1 mm V-bit, Z -0.05
isolate copper -dia 0.1 -passes 1 -combine 1 -outname iso_geo
cncjob iso_geo -dia 0.1 -z_cut -0.05 -z_move 2 -endz 15 -feedrate 120 -feedrate_z 60 -feedrate_rapid 1500 -spindlespeed 10000 -dwelltime 3 -pp default -outname iso_cnc
write_gcode iso_cnc "$out/1_isolation_0.1vbit.nc"

# 2) All 29 component holes: 1.0 mm drill (no 2.0 mm component holes on this board)
drillcncjob holes -drilled_dias "1.0" -drillz -1.7 -travelz 2 -endz 15 -feedrate_z 100 -feedrate_rapid 1500 -spindlespeed 10000 -dwelltime 3 -pp default -outname holes1_cnc
write_gcode holes1_cnc "$out/2_holes_1.0drill.nc"

# 3) 2.0 mm drill: the 4 mounting-hole pilots (open those to 3.2 by hand for M3)
drillcncjob mounting -drilled_dias all -drillz -1.7 -travelz 2 -endz 15 -feedrate_z 100 -feedrate_rapid 1500 -spindlespeed 10000 -dwelltime 3 -pp default -outname mounting_cnc
write_gcode mounting_cnc "$out/3_mounting_2.0drill.nc"

# 4) Outline: score it with the V-bit at Z -0.07, then cut/snap the board out by hand
follow outline -outname outline_geo
cncjob outline_geo -dia 0.1 -z_cut -0.07 -z_move 2 -endz 15 -feedrate 120 -feedrate_z 60 -feedrate_rapid 1500 -spindlespeed 10000 -dwelltime 3 -pp default -outname outline_cnc
write_gcode outline_cnc "$out/4_outline_score_0.1vbit.nc"

plot_all
