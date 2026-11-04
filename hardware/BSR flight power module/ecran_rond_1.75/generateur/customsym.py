"""Symboles absents des bibliothèques KiCad : ES8311, NS4150B, LC76G.
Brochages relevés sur le schéma Waveshare ESP32-S3-Touch-AMOLED-1.75 (page 1)."""
import sexpdata

def mk(name, footprint, desc, left, right, w=12.7, h=None, datasheet=''):
    """left/right : listes de (numéro, nom, type) de haut en bas. Pas de 2,54 mm."""
    n = max(len(left), len(right))
    if h is None:
        h = (n + 1) * 2.54 / 2
    pins = []
    def side(lst, x, ang):
        y = (len(lst) - 1) * 2.54 / 2
        for num, nm, tp in lst:
            pins.append(f'(pin {tp} line (at {x:.2f} {y:.2f} {ang}) (length 5.08) '
                        f'(name "{nm}" (effects (font (size 1.27 1.27)))) (number "{num}" (effects (font (size 1.27 1.27)))))')
            y -= 2.54
    side(left, -w - 5.08, 0)
    side(right, w + 5.08, 180)
    txt = f'''(symbol "{name}" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)
  (property "Reference" "U" (at {-w:.2f} {h + 1.27:.2f} 0) (effects (font (size 1.27 1.27)) (justify left)))
  (property "Value" "{name}" (at {w:.2f} {h + 1.27:.2f} 0) (effects (font (size 1.27 1.27)) (justify right)))
  (property "Footprint" "{footprint}" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
  (property "Datasheet" "{datasheet}" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
  (property "Description" "{desc}" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
  (symbol "{name}_0_1" (rectangle (start {-w:.2f} {h:.2f}) (end {w:.2f} {-h:.2f}) (stroke (width 0.254) (type default)) (fill (type background))))
  (symbol "{name}_1_1" {' '.join(pins)}))'''
    return sexpdata.loads(txt)

LIB = {}

LIB['ES8311'] = mk('ES8311', 'Package_DFN_QFN:QFN-20-1EP_3x3mm_P0.4mm_EP1.65x1.65mm',
    'Codec audio mono 24 bits I2S, DAC + ADC, QFN-20 3x3 (Everest)',
    left=[('1', 'CCLK', 'input'), ('19', 'CDATA', 'bidirectional'), ('20', 'CE', 'input'),
          ('2', 'MCLK', 'input'), ('6', 'SCLK/DMIC_SCL', 'input'), ('8', 'LRCK', 'input'),
          ('9', 'DSDIN', 'input'), ('7', 'ASDOUT', 'output'),
          ('17', 'MIC1N', 'input'), ('18', 'MIC1P/DMIC_SDA', 'input')],
    right=[('3', 'PVDD', 'power_in'), ('4', 'DVDD', 'power_in'), ('11', 'AVDD', 'power_in'),
           ('12', 'OUTP', 'output'), ('13', 'OUTN', 'output'),
           ('14', 'DACVREF', 'passive'), ('15', 'ADCVREF', 'passive'), ('16', 'VMID', 'passive'),
           ('5', 'DGND', 'power_in'), ('10', 'AGND', 'power_in'), ('21', 'EP', 'passive')],
    w=15.24, h=15.24)

LIB['NS4150B'] = mk('NS4150B', 'Package_SO:SOIC-8_3.9x4.9mm_P1.27mm',
    'Amplificateur audio classe D mono 3 W, entree differentielle, SOP-8 (Nsiway)',
    left=[('1', 'CTRL', 'input'), ('2', 'BYPASS', 'passive'), ('3', 'IN+', 'input'), ('4', 'IN-', 'input')],
    right=[('6', 'VDD', 'power_in'), ('8', 'OUT+', 'output'), ('5', 'OUT-', 'output'), ('7', 'GND', 'power_in')],
    w=10.16, h=7.62)

LIB['LC76G'] = mk('LC76G', 'ecran_rond:LC76G',
    'Module GNSS Quectel LC76G (GPS/GLONASS/Galileo/BDS/QZSS), LCC 18 broches 10.1x9.7 mm, UART + I2C',
    left=[('8', 'VCC', 'power_in'), ('6', 'V_BCKP', 'power_in'), ('14', 'VCC_RF', 'power_out'),
          ('2', 'TXD', 'output'), ('3', 'RXD', 'input'), ('16', 'SDA/RES', 'bidirectional'), ('17', 'SCL/RES', 'input'),
          ('4', '1PPS', 'output'), ('5', 'STANDBY', 'input'), ('18', 'FORCE_ON', 'input'), ('9', 'RESET_N', 'input')],
    right=[('11', 'RF_IN', 'input'), ('13', 'ANT_ON', 'output'), ('7', 'NC', 'no_connect'), ('15', 'NC', 'no_connect'),
           ('1', 'GND', 'power_in'), ('10', 'GND', 'power_in'), ('12', 'GND', 'power_in')],
    w=15.24, h=15.24,
    datasheet='https://www.quectel.com/product/gnss-lc76g')

CUSTOM = {'ecran_rond': LIB}

def write_lib(path):
    body = '\n'.join(sexpdata.dumps(s, true_as='yes', false_as='no') for s in LIB.values())
    open(path, 'w', encoding='utf-8').write(
        f'(kicad_symbol_lib (version 20241209) (generator "ecran_rond_gen") (generator_version "9.0")\n{body}\n)\n')
