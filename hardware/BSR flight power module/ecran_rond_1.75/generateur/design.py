"""Description netlist de l'écran rond 1,75" KHEPRI (dérivé Waveshare ESP32-S3-Touch-AMOLED-1.75-G
sans AXP2101/batterie, sans ES7210/micros, sans QMI8658)."""
from kicadsch import Comp

R0402 = 'Resistor_SMD:R_0402_1005Metric'
C0402 = 'Capacitor_SMD:C_0402_1005Metric'
C0603 = 'Capacitor_SMD:C_0603_1608Metric'
C0805 = 'Capacitor_SMD:C_0805_2012Metric'
L0603 = 'Inductor_SMD:L_0603_1608Metric'
L0402 = 'Inductor_SMD:L_0402_1005Metric'
TP = 'TestPoint:TestPoint_Pad_D1.0mm'

def R(ref, val, a, b, fp=R0402, dnp=False, desc=''):
    return Comp(ref, 'Device:R', val, fp, {'1': a, '2': b}, desc, dnp)
def C(ref, val, a, b, fp=C0402, desc=''):
    return Comp(ref, 'Device:C', val, fp, {'1': a, '2': b}, desc)
def L(ref, val, a, b, fp=L0603, desc=''):
    return Comp(ref, 'Device:L', val, fp, {'1': a, '2': b}, desc)
def TPt(ref, net, desc=''):
    return Comp(ref, 'Connector:TestPoint', net, TP, {'1': net}, desc or f'Point de test {net}')

# ------------------------------------------------------------------ ALIMENTATION
def alimentation(sh):
    sh.text('ALIMENTATION — entrée 5 V (JST PH 4P depuis la carte façade, ou USB-C), buck 3,3 V, 3,3 V analogique, sauvegarde RTC', 20, 20)
    sh.add(Comp('J1', 'Connector:USB_C_Receptacle_USB2.0_16P', 'USB-C (flash / logs)',
                'Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12',
                {'A1': 'GND', 'A12': 'GND', 'B1': 'GND', 'B12': 'GND', 'S1': 'GND',
                 'A4': 'VBUS', 'A9': 'VBUS', 'B4': 'VBUS', 'B9': 'VBUS',
                 'A5': 'CC1', 'B5': 'CC2', 'A7': 'USB_N', 'B7': 'USB_N', 'A6': 'USB_P', 'B6': 'USB_P',
                 'A8': None, 'B8': None}, 'Prise USB-C 16P, USB 2.0'))
    sh.add(R('R1', '5.1k', 'CC1', 'GND', desc='Rd USB-C'))
    sh.add(R('R2', '5.1k', 'CC2', 'GND', desc='Rd USB-C'))
    sh.add(Comp('D1', 'Power_Protection:USBLC6-2SC6', 'USBLC6-2SC6', 'Package_TO_SOT_SMD:SOT-23-6',
                {'1': 'USB_N', '6': 'USB_N', '3': 'USB_P', '4': 'USB_P', '5': 'VBUS', '2': 'GND'}, 'Protection ESD USB'))
    sh.add(Comp('D2', 'Device:D_TVS', 'SMF5.0A', 'Diode_SMD:D_SOD-123F', {'1': 'VBUS', '2': 'GND'}, 'TVS 5 V sur VBUS'))
    sh.add(Comp('J2', 'Connector_Generic:Conn_01x04', 'IHM : 5V GND TX RX', 'Connector_JST:JST_PH_B4B-PH-K_1x04_P2.00mm_Vertical',
                {'1': '+5V_IN', '2': 'GND', '3': 'IHM_TX', '4': 'IHM_RX'},
                'JST PH 2,0 mm vers la carte façade : 1 = +5 V, 2 = GND, 3 = TX (GPIO17, sortie ESP32), 4 = RX (GPIO18)'))
    sh.add(Comp('D3', 'Device:D_Schottky', 'B5819W', 'Diode_SMD:D_SOD-123', {'2': 'VBUS', '1': '+5V'}, 'OU diode : USB (Schottky 1 A 40 V)'))
    sh.add(Comp('D4', 'Device:D_Schottky', 'B5819W', 'Diode_SMD:D_SOD-123', {'2': '+5V_IN', '1': '+5V'}, 'OU diode : connecteur (Schottky 1 A 40 V)'))
    sh.add(C('C1', '10uF', '+5V', 'GND', C0805))
    sh.add(C('C2', '100nF', '+5V', 'GND'))
    sh.add(Comp('U2', 'Regulator_Switching:AP63203WU', 'AP63203WU', 'Package_TO_SOT_SMD:TSOT-23-6',
                {'3': '+5V', '2': 'BUCK_EN', '4': 'GND', '5': 'SW_3V3', '6': 'BST', '1': '+3V3'},
                'Buck 5 V -> 3,3 V 2 A, 1,1 MHz, sortie fixe (FB relié à la sortie)'))
    sh.add(R('R3', '100k', '+5V', 'BUCK_EN', desc='Enable buck'))
    sh.add(C('C3', '100nF', 'BST', 'SW_3V3', desc='Bootstrap'))
    sh.add(L('L1', '4.7uH', 'SW_3V3', '+3V3', 'Inductor_SMD:L_Taiyo-Yuden_NR-40xx', 'Self buck 4,7 µH ≥ 2,5 A sat.'))
    sh.add(C('C4', '22uF', '+3V3', 'GND', C0805))
    sh.add(C('C5', '22uF', '+3V3', 'GND', C0805))
    sh.add(C('C6', '100nF', '+3V3', 'GND'))
    sh.add(Comp('FB1', 'Device:FerriteBead', '600R@100MHz', L0603, {'1': '+3V3', '2': 'A3V3'}, 'Filtrage 3,3 V analogique codec'))
    sh.add(C('C7', '10uF', 'A3V3', 'AGND', C0805))
    sh.add(C('C8', '100nF', 'A3V3', 'AGND'))
    sh.add(Comp('NT1', 'Device:NetTie_2', 'AGND=GND', 'NetTie:NetTie-2_SMD_Pad0.5mm', {'1': 'GND', '2': 'AGND'}, 'Point de masse unique audio'))
    # sauvegarde RTC / GPS
    sh.add(Comp('BT1', 'Device:Battery_Cell', 'CR1220', 'Battery:BatteryHolder_LINX_BAT-HLD-012-SMT',
                {'1': 'VBAT_RTC', '2': 'GND'}, 'Pile de sauvegarde RTC + démarrage à chaud GPS ; support CMS Linx BAT-HLD-012-SMT, côté écran'))
    sh.add(Comp('D5', 'Diode:BAT54C', 'BAT54C', 'Package_TO_SOT_SMD:SOT-23', {'1': '+3V3', '2': 'VBAT_RTC', '3': 'VCC_RTC'},
                'OU diode 3,3 V / pile -> VCC_RTC'))
    sh.add(C('C9', '100nF', 'VCC_RTC', 'GND'))
    for i, n in enumerate(['VBUS', '+5V_IN', '+5V', '+3V3', 'A3V3', 'VCC_RTC', 'VBAT_RTC', 'GND', 'AGND']):
        sh.add(Comp(f'#FLG{i+1:02d}', 'power:PWR_FLAG', 'PWR_FLAG', '', {'1': n}))

# ------------------------------------------------------------------ MCU
def mcu(sh):
    sh.text('MCU — module ESP32-S3-WROOM-1-N16R8 (certifié, antenne intégrée, 16 Mo flash, 8 Mo PSRAM octale), USB, boutons BOOT / RESET', 20, 20)
    sh.add(Comp('U1', 'RF_Module:ESP32-S3-WROOM-1', 'ESP32-S3-WROOM-1-N16R8', 'RF_Module:ESP32-S3-WROOM-1', {
        '1': 'GND', '2': '+3V3', '3': 'CHIP_PU',
        '4': 'QSPI_SIO0', '5': 'QSPI_SI1', '6': 'QSPI_SI2', '7': 'QSPI_SI3',
        '8': 'I2C_SDA', '9': 'GPIO16', '10': 'IHM_TX', '11': 'IHM_RX', '12': 'I2S_DSDIN',
        '13': 'USB_N_MCU', '14': 'USB_P_MCU', '15': 'SD_D0', '16': 'PA_CTRL',
        '17': 'I2S_SCLK', '18': 'I2S_ASDOUT', '19': 'TP_INT', '20': 'LCD_CS', '21': 'LCD_TE', '22': 'I2C_SCL',
        '23': 'GPS_1PPS', '24': None, '25': None, '26': 'I2S_LRCK', '27': 'GPIO0',
        '28': None, '29': None, '30': None,
        '31': 'QSPI_SCL', '32': 'LCD_RESET', '33': 'TP_RESET', '34': 'SDCS', '35': 'I2S_MCLK',
        '36': 'U0RXD', '37': 'U0TXD', '38': 'SD_CLK', '39': 'SD_CMD', '40': 'GND', '41': 'GND'},
        'Module ESP32-S3-WROOM-1-N16R8 : ESP32-S3R8 + flash 16 Mo, antenne PCB, certifié CE/FCC. '
        'IO35-37 utilisés par la PSRAM octale (laisser en l air), IO47/48 libres. Antenne au bord de carte, '
        'zone sans cuivre dessous sur toutes les couches.'))
    sh.add(C('C13', '100nF', '+3V3', 'GND', desc='Découplage module, au plus près de la broche 2'))
    sh.add(C('C18', '1uF', '+3V3', 'GND', desc='Découplage module'))
    sh.add(C('C19', '22uF', '+3V3', 'GND', C0805, desc='Réservoir module (pics d émission Wi-Fi)'))
    sh.add(R('R5', '10k', '+3V3', 'CHIP_PU', desc='Pull-up EN'))
    sh.add(C('C20', '1uF', 'CHIP_PU', 'GND', desc='Délai reset (RC recommandé par Espressif)'))
    sh.add(Comp('SW1', 'Switch:SW_Push', 'RESET', 'Button_Switch_SMD:SW_SPST_PTS810', {'1': 'CHIP_PU', '2': 'GND'}, 'Bouton reset'))
    sh.add(R('R6', '10k', '+3V3', 'GPIO0', desc='Pull-up BOOT'))
    sh.add(C('C21', '100nF', 'GPIO0', 'GND'))
    sh.add(Comp('SW2', 'Switch:SW_Push', 'BOOT', 'Button_Switch_SMD:SW_SPST_PTS810', {'1': 'GPIO0', '2': 'GND'}, 'Bouton BOOT (GPIO0)'))
    sh.add(R('R7', '22R', 'USB_N', 'USB_N_MCU'))
    sh.add(R('R8', '22R', 'USB_P', 'USB_P_MCU'))
    sh.add(TPt('TP1', 'GPIO16'))
    sh.add(TPt('TP3', 'U0TXD', 'Console UART0 TX'))
    sh.add(TPt('TP4', 'U0RXD', 'Console UART0 RX'))

# ------------------------------------------------------------------ ÉCRAN
def ecran(sh):
    sh.text('ÉCRAN — AMOLED rond 1,75" 466x466, CO5300 en QSPI + tactile CST9217 en I2C, nappe 31 broches 0,3 mm (Hirose FH26)', 20, 20)
    nets = {1: 'GND', 2: None, 3: None, 4: 'GND', 5: None, 6: None, 7: 'GND', 8: None, 9: None, 10: 'GND',
            11: 'LCD_TE', 12: 'QSPI_SI2', 13: 'QSPI_SI3', 14: 'QSPI_SCL', 15: 'QSPI_SI1', 16: 'QSPI_SIO0',
            17: 'LCD_CS', 18: 'LCD_RESET', 19: None, 20: 'GND', 21: '+3V3', 22: '+3V3', 23: '+3V3', 24: None,
            25: 'GND', 26: '+3V3', 27: 'I2C_SCL', 28: 'I2C_SDA', 29: 'TP_RESET', 30: 'TP_INT', 31: 'GND'}
    sh.add(Comp('J3', 'Connector_Generic:Conn_01x31', 'AMOLED 1.75" CO5300 + CST9217',
                'Connector_FFC-FPC:Hirose_FH26-31S-0.3SHW_2Rows-31Pins-1MP_P0.60mm_Horizontal',
                {str(k): v for k, v in nets.items()},
                'Connecteur nappe écran, brochage relevé sur le schéma Waveshare (pins 2-3, 5-6, 8-9, 19, 24 non connectées)'))
    sh.add(C('C25', '10uF', '+3V3', 'GND', C0805, desc='Alim écran'))
    sh.add(C('C26', '1uF', '+3V3', 'GND'))
    sh.add(C('C27', '100nF', '+3V3', 'GND'))
    sh.add(R('R11', '2.2k', '+3V3', 'I2C_SCL', desc='Pull-up I2C'))
    sh.add(R('R12', '2.2k', '+3V3', 'I2C_SDA', desc='Pull-up I2C'))

# ------------------------------------------------------------------ AUDIO
def audio(sh):
    sh.text('AUDIO — codec ES8311 (I2S, I2C 0x18) + ampli classe D NS4150B, haut-parleur 8 Ω 2 W sur MX1.25', 20, 20)
    sh.add(Comp('U4', 'ecran_rond:ES8311', 'ES8311', 'Package_DFN_QFN:QFN-20-1EP_3x3mm_P0.4mm_EP1.65x1.65mm', {
        '1': 'I2C_SCL', '19': 'I2C_SDA', '20': 'CODEC_CE', '2': 'I2S_MCLK', '6': 'I2S_SCLK', '8': 'I2S_LRCK',
        '9': 'I2S_DSDIN', '7': 'I2S_ASDOUT', '17': None, '18': None,
        '3': '+3V3', '4': '+3V3', '11': 'A3V3', '12': 'CODEC_OUTP', '13': 'CODEC_OUTN',
        '14': 'DACVREF', '15': 'ADCVREF', '16': 'VMID', '5': 'GND', '10': 'AGND', '21': 'AGND'},
        'Codec audio ; CE à la masse -> adresse I2C 0x18'))
    sh.add(R('R13', '10k', 'CODEC_CE', 'AGND', desc='CE bas : adresse 0x18'))
    sh.add(C('C28', '100nF', '+3V3', 'GND', desc='PVDD/DVDD'))
    sh.add(C('C29', '1uF', 'A3V3', 'AGND', desc='AVDD'))
    sh.add(C('C30', '22pF', 'A3V3', 'AGND'))
    sh.add(C('C31', '1uF', 'DACVREF', 'AGND'))
    sh.add(C('C32', '1uF', 'ADCVREF', 'AGND'))
    sh.add(C('C33', '1uF', 'VMID', 'AGND'))
    sh.add(C('C34', '22pF', 'I2S_MCLK', 'AGND', desc='CEM'))
    sh.add(C('C35', '22pF', 'I2S_SCLK', 'AGND', desc='CEM'))
    sh.add(C('C36', '22pF', 'I2S_DSDIN', 'AGND', desc='CEM'))
    sh.add(C('C37', '1uF', 'CODEC_OUTP', 'PA_INP_AC', C0603, desc='Couplage'))
    sh.add(C('C38', '1uF', 'CODEC_OUTN', 'PA_INN_AC', C0603, desc='Couplage'))
    sh.add(R('R14', '150k', 'PA_INP_AC', 'PA_INP', desc='Gain ampli'))
    sh.add(R('R15', '150k', 'PA_INN_AC', 'PA_INN', desc='Gain ampli'))
    sh.add(Comp('U5', 'ecran_rond:NS4150B', 'NS4150B', 'Package_SO:SOIC-8_3.9x4.9mm_P1.27mm',
                {'1': 'PA_CTRL_IN', '2': 'PA_BYPASS', '3': 'PA_INP', '4': 'PA_INN', '6': '+3V3', '8': 'SPK_P', '5': 'SPK_N', '7': 'AGND'},
                'Ampli classe D 3 W mono'))
    sh.add(R('R16', '0R', 'PA_CTRL', 'PA_CTRL_IN', desc='Commande ampli (GPIO46)'))
    sh.add(R('R17', '10k', 'PA_CTRL_IN', 'AGND', desc='Pull-down : ampli coupé par défaut'))
    sh.add(C('C39', '1uF', 'PA_BYPASS', 'AGND'))
    sh.add(C('C40', '10uF', '+3V3', 'AGND', C0805, desc='Alim ampli'))
    sh.add(C('C41', '100nF', '+3V3', 'AGND'))
    sh.add(C('C42', '1uF', '+3V3', 'AGND'))
    sh.add(Comp('J5', 'Connector_Generic:Conn_01x02', 'HP 8R 2W', 'Connector_Molex:Molex_PicoBlade_53398-0271_1x02-1MP_P1.25mm_Vertical',
                {'1': 'SPK_P', '2': 'SPK_N'}, 'Haut-parleur, MX1.25 2P'))

# ------------------------------------------------------------------ PÉRIPHÉRIQUES
def peripheriques(sh):
    sh.text('PÉRIPHÉRIQUES — extenseur TCA9554 (0x20), RTC PCF85063 (0x51), microSD (SDMMC 1 bit), connecteur GPS externe (I2C, 1PPS)', 20, 20)
    sh.add(Comp('U6', 'Interface_Expansion:TCA9554PW', 'TCA9554PWR', 'Package_SO:TSSOP-16_4.4x5mm_P0.65mm',
                {'14': 'I2C_SCL', '15': 'I2C_SDA', '13': None, '1': 'GND', '2': 'GND', '3': 'GND', '16': '+3V3', '8': 'GND',
                 '4': None, '5': None, '6': None, '7': 'RTC_INT', '9': None, '10': None, '11': None, '12': 'GPS_RST'},
                'Extenseur E/S : P3 = RTC_INT, P7 = reset GPS ; P0-P2, P4-P6 non utilisés'))
    sh.add(C('C43', '100nF', '+3V3', 'GND'))
    sh.add(Comp('U7', 'Timer_RTC:PCF85063ATL', 'PCF85063ATL', 'Package_DFN_QFN:DFN-10-1EP_2.6x2.6mm_P0.5mm_EP1.3x2.2mm',
                {'7': 'I2C_SCL', '6': 'I2C_SDA', '3': 'GND', '1': 'RTC_OSCI', '2': 'RTC_OSCO', '10': 'VCC_RTC', '5': 'GND',
                 '8': None, '4': 'RTC_INT', '9': None}, 'RTC I2C, alimentée par VCC_RTC (3,3 V ou pile)'))
    sh.add(Comp('Y2', 'Device:Crystal', '32.768kHz', 'Crystal:Crystal_SMD_3215-2Pin_3.2x1.5mm', {'1': 'RTC_OSCI', '2': 'RTC_OSCO'},
                'Quartz horloger 12,5 pF'))
    sh.add(C('C44', '22pF', 'RTC_OSCI', 'GND'))
    sh.add(C('C45', '22pF', 'RTC_OSCO', 'GND'))
    sh.add(C('C46', '100nF', 'VCC_RTC', 'GND'))
    sh.add(R('R18', '10k', '+3V3', 'RTC_INT', desc='Pull-up INT RTC (drain ouvert)'))
    sh.add(Comp('J6', 'Connector:Micro_SD_Card_Det_Hirose_DM3AT', 'microSD', 'Connector_Card:microSD_HC_Hirose_DM3AT-SF-PEJM5',
                {'1': None, '2': 'SDCS', '3': 'SD_CMD', '4': '+3V3', '5': 'SD_CLK', '6': 'GND', '7': 'SD_D0', '8': None,
                 '10': None, '9': None, '11': 'GND'}, 'Lecteur microSD, SDMMC 1 bit (CMD GPIO1, CLK GPIO2, D0 GPIO3, D3 GPIO41)'))
    sh.add(R('R19', '10k', '+3V3', 'SD_CMD'))
    sh.add(R('R20', '10k', '+3V3', 'SD_D0'))
    sh.add(R('R21', '10k', '+3V3', 'SDCS'))
    sh.add(C('C47', '100nF', '+3V3', 'GND'))
    sh.add(C('C48', '2.2uF', '+3V3', 'GND', C0603))
    sh.add(Comp('J7', 'Connector_Generic:Conn_01x06', 'GPS externe', 'Connector_JST:JST_SH_SM06B-SRSS-TB_1x06-1MP_P1.00mm_Horizontal',
                {'1': '+3V3', '2': 'GND', '3': 'I2C_SDA', '4': 'I2C_SCL', '5': 'GPS_RST', '6': 'GPS_1PPS'},
                'JST-SH 1,0 mm 6P vers module GPS externe (ex. LC76G + antenne) : 1 = 3V3, 2 = GND, 3 = SDA, 4 = SCL, '
                '5 = RESET (TCA9554 P7), 6 = 1PPS (GPIO21) ; même bus I2C que l ancien LC76G intégré'))
    sh.add(C('C49', '100nF', '+3V3', 'GND', desc='Découplage alim GPS externe'))
    for i in (1, 2, 3):
        sh.add(Comp(f'H{i}', 'Mechanical:MountingHole', 'M2', 'MountingHole:MountingHole_2.2mm_M2', {}, 'Trou de fixation M2'))

SHEETS = [('Alimentation', 'alimentation.kicad_sch', alimentation),
          ('MCU', 'mcu.kicad_sch', mcu),
          ('Ecran', 'ecran.kicad_sch', ecran),
          ('Audio', 'audio.kicad_sch', audio),
          ('Peripheriques', 'peripheriques.kicad_sch', peripheriques)]
