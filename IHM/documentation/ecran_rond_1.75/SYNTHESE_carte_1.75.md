# Waveshare ESP32-S3-Touch-AMOLED-1.75 — synthèse technique

Compilé le 28/10/2026 à partir du site Waveshare (page produit + docs.waveshare.com) et du dépôt GitHub officiel `waveshareteam/ESP32-S3-Touch-AMOLED-1.75` (commit e4344e7, 21/08/2026). En cas de doute, le schéma (`schema/`) fait foi. Les chemins `05_Firmware_usine/` cités plus bas renvoient au dossier `firmware/` du dépôt Waveshare.

## 1. Versions

| SKU | Référence | Différence |
|---|---|---|
| 31261 | ESP32-S3-Touch-AMOLED-1.75 | Version standard |
| 31262 | ESP32-S3-Touch-AMOLED-1.75-B | Standard + boîtier de protection |
| 31264 | **ESP32-S3-Touch-AMOLED-1.75-G** | **Module GNSS LC76G + antenne céramique** (connecteur IPEX1 GPS) |

Prix affiché : 33,99 USD (version standard).

## 2. Caractéristiques

| Fonction | Composant | Détails |
|---|---|---|
| MCU | ESP32-S3R8 | 2× Xtensa LX7 240 MHz, 512 Ko SRAM, 384 Ko ROM, **8 Mo PSRAM**, **16 Mo flash** externe |
| Radio | ESP32-S3 | Wi-Fi 2,4 GHz b/g/n, BLE 5, antenne céramique + IPEX1 (après déplacement d'une résistance) |
| Écran | **CO5300** (QSPI) | AMOLED rond 1,75", **466×466**, 16,7 M couleurs, 700 cd/m², contraste 100000:1 |
| Tactile | **CST9217** (I2C) | Capacitif, reset et interruption dédiés |
| PMU | **AXP2101** | Chemin d'alimentation, charge, jauge batterie Li-ion 3,7 V (connecteur MX1.25) |
| IMU | **QMI8658** | 6 axes (accéléro + gyro) |
| RTC | **PCF85063** | Sauvegarde par le circuit d'alim |
| Sortie audio | **ES8311** + ampli **NS4150B** | Connecteur haut-parleur MX1.25 2P |
| Entrée audio | **ES7210** | ADC 4 voies, **2 micros**, voie de référence pour annulation d'écho |
| Extenseur E/S | **TCA9554** | Interruptions, reset GPS, état bouton PWR |
| Stockage | Lecteur TF/microSD | SDMMC (1 bit dans le BSP) |
| GNSS (-G) | **LC76G** | I2C (0x50/0x54), UART optionnel sur GPIO17/18, reset via TCA9554 P7 |
| USB | Type-C | USB natif ESP32-S3 (flash + logs série/JTAG) |
| Boutons | PWR, BOOT | PWR géré par l'AXP2101, BOOT = GPIO0 |
| Extension | Connecteur 2,54 mm 8 broches | VBUS, GND, 3V3, UART0, 3 GPIO |

## 3. Brochage ESP32-S3

| GPIO | Signal | Rôle |
|---:|---|---|
| 0 | BOOT | Bouton BOOT / strap de démarrage |
| 1 | SD_CMD | Carte TF |
| 2 | SD_CLK | Carte TF |
| 3 | SD_D0 | Carte TF |
| 4–7 | LCD_DATA0–3 | QSPI CO5300 |
| 8 | I2S_DOUT | Vers ES8311 (lecture) |
| 9 | I2S_BCLK | Horloge bit ES8311 + ES7210 |
| 10 | I2S_DIN | Depuis ES7210 (micros, TDM) |
| 11 | TP_INT | Interruption tactile |
| 12 | LCD_CS | CS écran |
| 13 | LCD_TE | Tearing effect écran |
| 14 | I2C_SCL | Bus I2C commun |
| 15 | I2C_SDA | Bus I2C commun |
| 16 | GPIO16 | Connecteur, broche 8 (libre) |
| 17 | GPIO17 | Connecteur, broche 6 — **TX vers LC76G sur -G** |
| 18 | GPIO18 | Connecteur, broche 7 — **RX depuis LC76G sur -G** |
| 19 / 20 | USB D- / D+ | USB natif |
| 21 | QMI_INT2 | Interruption IMU 2 |
| 38 | LCD_PCLK | Horloge QSPI |
| 39 | LCD_RST | Reset écran |
| 40 | TP_RST | Reset tactile |
| 41 | SD_D3/CS | Câblé, inutilisé en SDMMC 1 bit |
| 42 | I2S_MCLK | Horloge maître audio |
| 43 / 44 | U0TXD / U0RXD | UART0, broches 5 / 4 du connecteur |
| 45 | I2S_LRCK | Horloge mot audio |
| 46 | PA_CTRL | Commande ampli NS4150B |

> ⚠️ Incohérence trouvée : `Mylibrary/pin_config.h` (exemples Arduino) déclare `I2S_MCK_IO 16`, mais la référence matérielle officielle et le BSP ESP-IDF utilisent **GPIO42** pour MCLK. Le même fichier redéfinit d'ailleurs `MCLKPIN 42` juste en dessous. Utiliser 42.

## 4. Bus I2C (SCL = GPIO14, SDA = GPIO15)

| Adresse 7 bits | Composant |
|---:|---|
| 0x18 | ES8311 (0x30 en notation 8 bits) |
| 0x20 | TCA9554 |
| 0x34 | AXP2101 |
| 0x40 | ES7210 (0x80 en notation 8 bits) |
| 0x51 | PCF85063 |
| 0x5A | CST9217 |
| 0x6B | QMI8658 |
| 0x50 / 0x54 | LC76G (-G) : écriture / lecture |

Le BSP tourne à 400 kHz ; l'exemple LC76G à 100 kHz. Sur la -G, choisir une vitesse supportée par tous.

## 5. Extenseur TCA9554

| Broche | Signal | Fonction |
|---|---|---|
| P0–P2 | EXIO0–2 | Libres (points de test) |
| P3 | RTC_INT | Interruption PCF85063 |
| P4 | SYS_OUT | État bouton PWR (haut = appuyé) |
| P5 | AXP_IRQ | Interruption AXP2101 |
| P6 | QMI_INT1 | Interruption IMU 1 |
| P7 | GPS_RST | Reset LC76G (-G) |

## 6. Audio

- ES8311 en lecture I2S standard + ES7210 en capture TDM 4 slots, horloges partagées (MCLK 42, BCLK 9, WS 45).
- Profil voix du BSP : 24 kHz, 16 bits, MCLK ×256.
- Entrées ES7210 : MIC1 et MIC2 = micros ; MIC3 = référence de lecture (annulation d'écho) ; MIC4 non connecté. Ordre des slots : MIC1, MIC3, MIC2, MIC4.

## 7. Points d'attention

- **Pas de rétroéclairage PWM** : la luminosité se règle par commandes CO5300.
- Resets écran (39) et tactile (40) séparés.
- GPIO1–3 réservés quand la carte SD est montée.
- Logique 3,3 V, non tolérante 5 V.
- Ne pas alimenter VBUS/3V3 en externe sans tenir compte du chemin d'alim de l'AXP2101.

## 8. Logiciel

| Environnement | Versions |
|---|---|
| Arduino | Wiki : core esp32 **3.1.0** ; dépôt : validé en **3.3.10**. LVGL **8.4.0**, SensorLib 0.3.1, XPowersLib 0.2.6, ESP32_IO_Expander 0.0.3 (toutes fournies dans le dépôt) |
| ESP-IDF | Wiki : ≥ 5.3.1 ; dépôt : validé en **5.5.5** et **6.0.2** |
| Brookesia (firmware usine) | ESP-IDF 5.5.4 |

Exemples Arduino du dépôt : 01_HelloWorld, 02_GFX_AsciiTable, 03_LVGL_PCF85063_simpleTime, 04_LVGL_QMI8658_ui, 05_LVGL_AXP2101_ADC_Data, 06_LVGL_Widgets, 07_LVGL_SD_Test, 08_ES8311, **09_LC76G_I2C (GPS)**, 10_Touch_CST9217.
Exemples ESP-IDF : 01_AXP2101, 02_lvgl_demo_v9, 03_esp-brookesia, 04_Immersive_block, 05_Spec_Analyzer.

Le wiki décrit une ancienne série d'exemples ESP-IDF (02_PCF85063, 03_QMI8658, 04_SD_MMC, 05_LVGL_WITH_RAM ~200–300 fps, 06_I2SCodec) qui n'est plus dans le dépôt actuel.

Pour le GPS (09_LC76G_I2C) : activer **USB CDC On Boot** et brancher l'antenne. Les trames NMEA sortent sur le port série.

## 9. Flash et récupération

- Firmware usine complet : `05_Firmware_usine/ESP32-S3-Touch-AMOLED-1.75-FactoryOnly-260805.bin`, à l'adresse **0x0** (outil web esptool-js ou Flash Download Tool, ESP32-S3, USB).
- Médias de la carte SD pour le firmware usine : `SD-Card-Media-260805.zip`.
- Mode téléchargement : maintenir BOOT et redémarrer.

## 10. FAQ officielle

- **Changer l'orientation** : rotation logicielle LVGL.
- **`driver/i2c.h: No such file`** : dans menuconfig, désactiver « Enable backward compatibility for the I2C driver ».

## 11. IA embarquée

Compatible avec le firmware **XiaoZhi AI v2.4.0** : https://github.com/78/xiaozhi-esp32/releases ou https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32

## 12. Liens

Documentation
- Page produit : https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm
- Documentation : https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75
- Ressources : https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75/Resources-And-Documents
- Arduino : https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75/Arduino
- ESP-IDF : https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75/ESP-IDF
- Flash : https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75/Firmware-Flashing
- FAQ : https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75/FAQ
- GitHub : https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75

Fichiers en ligne non inclus dans ce dossier (serveur Waveshare inaccessible au téléchargement direct)
- Plans 3D : https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/ESP32-S3-Touch-AMOLED-1.75-3D.zip
- Plans 3D version -B : https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/ESP32-S3-Touch-AMOLED-1.75-B-3D.zip
- Datasheet ES8311 : https://files.waveshare.com/wiki/common/ES8311.DS.pdf
- Guide ES8311 : https://files.waveshare.com/wiki/common/ES8311.user.Guide.pdf
- Datasheet ESP32-S3 : https://documentation.espressif.com/esp32-s3_datasheet_en.pdf
- Manuel de référence ESP32-S3 : https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf
- Outil GPS uSTAR3 : https://files.waveshare.com/wiki/A7670E-Cat-1-GNSS-HAT/uSTAR3.zip
- Flash Download Tool : https://dl.espressif.com/public/flash_download_tool.zip

Waveshare ne publie pas de datasheet pour CO5300, CST9217, ES7210, TCA9554 ni LC76G.
