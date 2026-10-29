# Documentation — Waveshare ESP32-S3-Touch-AMOLED-1.75

Ressources officielles de la carte de l'écran pilote, téléchargées le 29/10/2026 depuis la page
« Resources » de Waveshare (https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75/Resources-And-Documents).

| Dossier / fichier | Contenu |
|---|---|
| `SYNTHESE_carte_1.75.md` | Synthèse en français : versions (-G avec GPS LC76G), brochage complet, bus I2C, extenseur, audio, points d'attention |
| `schema/ESP32-S3-Touch-AMOLED-1.75.pdf` | Schéma électrique de la carte |
| `datasheets/` | ESP32-S3 (datasheet et manuel de référence), QMI8658C (IMU), PCF85063A (RTC), AXP2101 (alimentation), ES8311 (guide du codec audio) |
| `3D/` | Plans et modèles 3D (DWG, PDF, STEP) de la carte nue et de la version -B en boîtier |
| `liens_utiles.md` | ESP-IDF, Arduino-ESP32, MicroPython, tutoriels Waveshare, dépôt d'exemples officiel |

Outils logiciels téléchargés le même jour, non recopiés ici (exécutables tiers) :

- Flash Download Tool (Espressif) : https://dl.espressif.com/public/flash_download_tool.zip
- uSTAR3, outil de test GPS : https://files.waveshare.com/wiki/A7670E-Cat-1-GNSS-HAT/uSTAR3.zip
- Image2Lcd 2.9 : https://files.waveshare.com/wiki/common/Image2Lcd2.9.zip
- Zimo221 (polices) : https://files.waveshare.com/wiki/common/Zimo221.7z

Waveshare ne publie pas de datasheet pour le CO5300 (écran), le CST9217 (tactile), l'ES7210
(micros), le TCA9554 (extenseur) ni le LC76G (GPS). Le brochage tiré du schéma est dans la synthèse.
