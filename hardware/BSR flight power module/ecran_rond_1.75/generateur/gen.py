import os, sys, json, csv, collections
import kicadsch
from kicadsch import Sheet, write_root, U, POWER_NETS
import customsym, design

PROJECT = 'ecran_rond_1.75'
OUT = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/work/out/ecran_rond_1.75'
os.makedirs(OUT, exist_ok=True)

POWER_NETS.update({'GND': ('power', 'GND'), '+3V3': ('power', '+3V3'), '+5V': ('power', '+5V'),
                   'AGND': ('power', 'GNDA'), 'A3V3': ('power', '+3.3VA'), 'VBUS': ('power', 'VBUS'),
                   '+5V_IN': ('power', 'VDD'), 'VCC_RTC': ('power', 'VCC')})

root_uuid = U()
sheets = []
for name, fn, build in design.SHEETS:
    sh = Sheet(name, fn, PROJECT, root_uuid)
    build(sh)
    sheets.append(sh)

# ---- contrôle de la netlist : chaque net doit avoir au moins 2 connexions
nets = collections.defaultdict(list)
refs = set()
for sh in sheets:
    for c in sh.comps:
        if c.ref in refs: raise SystemExit(f'référence en double : {c.ref}')
        refs.add(c.ref)
        for pin, net in c.nets.items():
            if net: nets[net].append(f'{c.ref}.{pin}')
bad = {n: p for n, p in nets.items() if len(p) < 2}
if bad:
    print('NETS A UNE SEULE CONNEXION :', bad)

# ---- placement + écriture
for sh in sheets:
    sh.autoplace(customsym.CUSTOM)
    for c in sh.comps:
        sh.emit_comp(c, customsym.CUSTOM)
    sh.write(os.path.join(OUT, sh.filename), customsym.CUSTOM, f'Écran rond 1,75" KHEPRI — {sh.name}')
write_root(os.path.join(OUT, PROJECT + '.kicad_sch'), PROJECT, root_uuid, sheets, customsym.CUSTOM, 'Écran rond 1,75" KHEPRI')

# ---- bibliothèque projet
os.makedirs(os.path.join(OUT, 'lib'), exist_ok=True)
customsym.write_lib(os.path.join(OUT, 'lib', 'ecran_rond.kicad_sym'))
open(os.path.join(OUT, 'sym-lib-table'), 'w').write(
    '(sym_lib_table (version 7)\n  (lib (name "ecran_rond")(type "KiCad")(uri "${KIPRJMOD}/lib/ecran_rond.kicad_sym")(options "")(descr "Symboles propres à l écran rond"))\n)\n')
open(os.path.join(OUT, 'fp-lib-table'), 'w').write('(fp_lib_table (version 7)\n  (lib (name "ecran_rond")(type "KiCad")(uri "${KIPRJMOD}/lib/ecran_rond.pretty")(options "")(descr "Empreintes propres à l écran rond"))\n)\n')

# ---- fichier projet
pro = {
    "board": {"3dviewports": [], "design_settings": {"defaults": {}, "diff_pair_dimensions": [], "drc_exclusions": [],
              "rules": {"min_clearance": 0.127, "min_track_width": 0.127, "min_via_diameter": 0.45, "min_via_drill": 0.2, "min_hole_clearance": 0.25,
                        "min_copper_edge_clearance": 0.3, "min_hole_to_hole": 0.25, "min_microvia_diameter": 0.2, "min_microvia_drill": 0.1,
                        "min_resolved_spokes": 2, "min_silk_clearance": 0.0, "min_text_height": 0.8, "min_text_thickness": 0.08, "min_via_annular_width": 0.1,
                        "solder_mask_clearance": 0.0, "solder_mask_min_width": 0.0, "solder_mask_to_copper_clearance": 0.0, "use_height_for_length_calcs": True},
              "track_widths": [0.0, 0.15, 0.18, 0.2, 0.3, 0.5, 0.8], "via_dimensions": [{"diameter": 0.0, "drill": 0.0}, {"diameter": 0.5, "drill": 0.25}, {"diameter": 0.6, "drill": 0.3}, {"diameter": 0.8, "drill": 0.4}],
              "diff_pair_dimensions": [{"gap": 0.0, "via_gap": 0.0, "width": 0.0}, {"gap": 0.18, "via_gap": 0.25, "width": 0.18}]},
              "layer_presets": [], "viewports": []},
    "boards": [], "cvpcb": {"equivalence_files": []},
    "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": ["ecran_rond"]},
    "meta": {"filename": PROJECT + ".kicad_pro", "version": 3},
    "net_settings": {"classes": [
        {"bus_width": 12, "clearance": 0.15, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2,
         "line_style": 0, "microvia_diameter": 0.3, "microvia_drill": 0.1, "name": "Default", "pcb_color": "rgba(0, 0, 0, 0.000)",
         "priority": 2147483647, "schematic_color": "rgba(0, 0, 0, 0.000)", "track_width": 0.2, "via_diameter": 0.6, "via_drill": 0.3, "wire_width": 6},
        {"bus_width": 12, "clearance": 0.2, "diff_pair_gap": 0.18, "diff_pair_via_gap": 0.25, "diff_pair_width": 0.18,
         "line_style": 0, "microvia_diameter": 0.3, "microvia_drill": 0.1, "name": "USB_90R", "pcb_color": "rgba(0, 128, 255, 0.800)",
         "priority": 0, "schematic_color": "rgba(0, 128, 255, 0.800)", "track_width": 0.18, "via_diameter": 0.5, "via_drill": 0.25, "wire_width": 6},
        {"bus_width": 12, "clearance": 0.2, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2,
         "line_style": 0, "microvia_diameter": 0.3, "microvia_drill": 0.1, "name": "RF_50R", "pcb_color": "rgba(255, 64, 0, 0.800)",
         "priority": 1, "schematic_color": "rgba(255, 64, 0, 0.800)", "track_width": 0.18, "via_diameter": 0.5, "via_drill": 0.25, "wire_width": 6},
        {"bus_width": 12, "clearance": 0.2, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2,
         "line_style": 0, "microvia_diameter": 0.3, "microvia_drill": 0.1, "name": "Alim", "pcb_color": "rgba(255, 0, 128, 0.800)",
         "priority": 2, "schematic_color": "rgba(255, 0, 128, 0.800)", "track_width": 0.5, "via_diameter": 0.8, "via_drill": 0.4, "wire_width": 6},
        {"bus_width": 12, "clearance": 0.15, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2,
         "line_style": 0, "microvia_diameter": 0.3, "microvia_drill": 0.1, "name": "QSPI_ecran", "pcb_color": "rgba(0, 200, 0, 0.800)",
         "priority": 3, "schematic_color": "rgba(0, 200, 0, 0.800)", "track_width": 0.15, "via_diameter": 0.5, "via_drill": 0.25, "wire_width": 6}],
        "meta": {"version": 4}, "net_colors": None, "netclass_assignments": None,
        "netclass_patterns": [
            {"netclass": "USB_90R", "pattern": "USB_N"}, {"netclass": "USB_90R", "pattern": "USB_P"},
            {"netclass": "USB_90R", "pattern": "USB_N_MCU"}, {"netclass": "USB_90R", "pattern": "USB_P_MCU"},
            {"netclass": "RF_50R", "pattern": "LNA_IN"}, {"netclass": "RF_50R", "pattern": "ANT*"},
            {"netclass": "RF_50R", "pattern": "GPS_RF*"}, {"netclass": "RF_50R", "pattern": "GPS_ANT"},
            {"netclass": "Alim", "pattern": "+5V*"}, {"netclass": "Alim", "pattern": "+3V3"}, {"netclass": "Alim", "pattern": "VBUS"},
            {"netclass": "Alim", "pattern": "SW_3V3"}, {"netclass": "Alim", "pattern": "A3V3"}, {"netclass": "Alim", "pattern": "SPK_*"},
            {"netclass": "QSPI_ecran", "pattern": "QSPI_*"}, {"netclass": "QSPI_ecran", "pattern": "LCD_*"}]},
    "pcbnew": {"last_paths": {"gencad": "", "idf": "", "netlist": "", "plot": "", "pos_files": "", "specctra_dsn": "", "step": "", "svg": "", "vrml": ""}, "page_layout_descr_file": ""},
    "schematic": {"annotate_start_num": 0, "bom_export_filename": "", "drawing": {"default_line_thickness": 6.0, "default_text_size": 50.0, "label_size_ratio": 0.375,
                  "pin_symbol_size": 25.0, "text_offset_ratio": 0.15}, "legacy_lib_dir": "", "legacy_lib_list": [], "meta": {"version": 1},
                  "net_format_name": "", "page_layout_descr_file": "", "plot_directory": "", "spice_current_sheet_as_root": False,
                  "spice_external_command": "spice \"%I\"", "spice_model_current_sheet_as_root": True, "spice_save_all_currents": False,
                  "spice_save_all_dissipations": False, "spice_save_all_voltages": False, "subpart_first_id": 65, "subpart_id_separator": 0},
    "sheets": [[root_uuid, "Racine"]] + [[sh.uuid, sh.name] for sh in sheets],
    "text_variables": {}
}
json.dump(pro, open(os.path.join(OUT, PROJECT + '.kicad_pro'), 'w'), indent=2, ensure_ascii=False)

# ---- BOM
with open(os.path.join(OUT, 'bom.csv'), 'w', newline='', encoding='utf-8') as f:
    w = csv.writer(f, delimiter=';')
    w.writerow(['Référence', 'Valeur', 'Empreinte', 'Feuille', 'Description', 'DNP'])
    for sh in sheets:
        for c in sh.comps:
            if c.ref.startswith('#'): continue
            w.writerow([c.ref, c.value, c.footprint, sh.name, c.desc, 'oui' if c.dnp else ''])

# ---- netlist texte (pour le README / vérification)
with open(os.path.join(OUT, 'netlist.txt'), 'w', encoding='utf-8') as f:
    for n in sorted(nets): f.write(f'{n}: ' + ' '.join(sorted(nets[n])) + '\n')
print('OK', OUT, 'composants:', len(refs), 'nets:', len(nets))

# ---- export pour le générateur de PCB
json.dump({'root_uuid': root_uuid, 'project': PROJECT,
           'sheets': [{'name': sh.name, 'file': sh.filename, 'uuid': sh.uuid,
                       'comps': [{'ref': c.ref, 'lib_id': c.lib_id, 'value': c.value, 'footprint': c.footprint,
                                  'nets': c.nets, 'uuid': c.uuid, 'desc': c.desc, 'dnp': c.dnp} for c in sh.comps]} for sh in sheets],
           'nets': {n: p for n, p in nets.items()}},
          open(os.path.join(OUT, 'design_export.json'), 'w'), indent=1, ensure_ascii=False)
