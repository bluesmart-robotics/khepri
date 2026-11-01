# Synthèse — Waveshare ESP32-S3-Touch-AMOLED-1.64 (écran énergie)

Carte déjà utilisée par `bluesmart-robotics/bsr_screen` (télécommande et module batterie BSR) :
le brochage ci-dessous vient de son `board_pins.h`, vérifié sur les deux révisions.

| Élément | Détail |
|---|---|
| MCU | ESP32-S3R8, 240 MHz, 8 Mo de PSRAM, 16 Mo de flash |
| Écran | AMOLED 1,64" 280×456, contrôleur CO5300 (piloté avec `esp_lcd_sh8601`), QSPI 40 MHz, décalage de 20 colonnes |
| Tactile | FT3168, I2C 0x38, lu en scrutation (registre 0x02 = nombre de points, 0x03-0x06 = X, Y) |
| IMU | QMI8658, I2C 0x6B (non utilisée par l'écran énergie) |
| Batterie | connecteur MX1.25 3,7 V, charge intégrée, mesure sur GPIO4 (ADC1_CH3, pont 200 k / 100 k) |
| Boutons | RST, BOOT |

## Brochage

| Signal | V1 | V1.1 |
|---|---|---|
| LCD_CS | **GPIO9** | **GPIO46** |
| LCD_PCLK / D0 / D1 / D2 / D3 | 10 / 11 / 12 / 13 / 14 | idem |
| LCD_RST (et reset du tactile) | 21 | 21 |
| I2C SDA / SCL (tactile + IMU) | 47 / 48 | 47 / 48 |
| IMU_INT1 | 46 | 9 |
| IMU_INT2 | — | 17 (ne jamais activer : partagé avec le bouton C de la télécommande BSR) |
| TP_INT | — | 18 (non utilisé) |
| LCD_TE | — | 45 (non utilisé) |

**Repérer la révision** : V1 = sérigraphie de version en haut du PCB ; V1.1 = sérigraphie à droite,
près des connecteurs. Les firmwares ne sont pas interchangeables (écran noir avec la mauvaise image).

## Liens

- Wiki Waveshare : https://waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.64
- Page produit : https://www.waveshare.com/esp32-s3-touch-amoled-1.64.htm
- Code de référence sur cette carte : dépôt `bluesmart-robotics/bsr_screen` (dossiers V1 et V1.1)
