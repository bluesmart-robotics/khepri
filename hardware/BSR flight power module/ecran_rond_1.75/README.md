# Écran rond 1,75" KHEPRI — carte dérivée de la Waveshare ESP32-S3-Touch-AMOLED-1.75-G

Reproduction en KiCad 9 de la carte Waveshare **ESP32-S3-Touch-AMOLED-1.75(-G)** (écran AMOLED rond
466×466 sur ESP32-S3R8), adaptée à l'instrument de bord KHEPRI :

- **supprimé** : gestion de batterie (AXP2101, connecteur batterie, bouton PWR), micros et leur ADC
  (ES7210, 2 micros MEMS, circuit d'annulation d'écho), centrale inertielle (QMI8658) ;
- **ajouté / modifié** : entrée 5 V sur connecteur JST PH 4 points (+5 V, GND, TX, RX) partagé avec
  l'UART vers la carte façade, buck AP63203 3,3 V 2 A (même famille que l'AP63205 de la carte de
  contrôle), pile CR1220 (support CMS, côté écran) pour la RTC et le GPS externe ;
- **GPS déporté** : le module LC76G et son antenne sont retirés de la carte (place nécessaire au module
  ESP32) et remplacés par un connecteur **JST SH 6 points (J7)** pour un module GPS externe en I2C ;
- **MCU remplacé par un module certifié** : ESP32-S3-WROOM-1-N16R8 (même puce ESP32-S3R8, 16 Mo de
  flash, 8 Mo de PSRAM octale, antenne PCB intégrée, certifications radio CE/FCC/IC du module) à la
  place de la puce nue + flash + quartz + antenne céramique de la Waveshare ;
- **conservé à l'identique** : brochage ESP32 (mêmes GPIO), nappe écran 31 points (CO5300 QSPI + tactile
  CST9217 I2C), codec ES8311 + ampli NS4150B + haut-parleur, RTC PCF85063, extenseur TCA9554,
  microSD en SDMMC 1 bit, USB-C natif, boutons BOOT et RESET.

Le brochage ESP32 est **le même** que celui de la carte Waveshare (voir
[`../../../IHM/documentation/ecran_rond_1.75/SYNTHESE_carte_1.75.md`](../../../IHM/documentation/ecran_rond_1.75/SYNTHESE_carte_1.75.md)),
donc le code de [`IHM/code/ecran_rond_1.75`](../../../IHM/code/ecran_rond_1.75) tourne dessus avec
trois adaptations (voir § Firmware).

État : **v0.2 — schéma complet (ERC = 0), netlist vérifiée, PCB Ø46 mm 4 couches avec empreintes,
nets, netclasses et placement optimisé sur les deux faces (DRC sans erreur). Routage à faire.**

## Contenu

| Fichier | Rôle |
|---|---|
| `ecran_rond_1.75.kicad_pro` / `.kicad_sch` | Projet KiCad 9.0 ; feuille racine + 5 sous-feuilles |
| `alimentation.kicad_sch` | USB-C, JST 5 V, OU à diodes, buck 3,3 V, 3,3 V analogique, pile RTC |
| `mcu.kicad_sch` | Module ESP32-S3-WROOM-1-N16R8, découplage, USB, BOOT/RESET, points de test |
| `ecran.kicad_sch` | Nappe FH26-31S (CO5300 + CST9217), découplage, pull-ups I2C |
| `audio.kicad_sch` | ES8311, NS4150B, connecteur haut-parleur MX1.25 |
| `peripheriques.kicad_sch` | TCA9554, PCF85063 + 32,768 kHz, microSD, connecteur GPS externe J7, trous M2 |
| `ecran_rond_1.75.kicad_pcb` | PCB Ø46 mm, 4 couches (F.Cu / In1 GND / In2 GND / B.Cu), 1,0 mm, ENIG |
| `lib/` | Symboles propres (ES8311, NS4150B ; LC76G conservé mais inutilisé) et `ecran_rond.3dshapes/` : modèles STEP simplifiés aux cotes constructeur pour les boîtiers absents de la bibliothèque KiCad (USB-C HRO TYPE-C-31-M-12, inductance NR4018, QFN-20 3×3 de l'ES8311, DFN-10 2,6×2,6 du PCF85063), générés par `generateur/models3d.py` |
| `bom.csv` | Nomenclature (`;` comme séparateur) |
| `netlist.txt`, `placement.csv` | Netlist de référence (vérifiée identique à celle exportée par KiCad) et placement des empreintes |
| `doc/schema.pdf`, `doc/placement_v0.2.png` | Schéma imprimé, aperçu du placement (faces F et B) |
| `generateur/` | Scripts Python qui ont produit schéma et PCB (voir plus bas) |

Le schéma est **généré** : chaque composant est posé sur une grille et chaque broche porte une étiquette
globale (ou un symbole d'alimentation). C'est lisible mais pas « dessiné » ; on peut réarranger à la
main dans KiCad sans rien casser, les connexions sont dans les étiquettes.

## Architecture d'alimentation

```
USB-C (VBUS) ─B5819W─┐
                     ├─ +5V ── AP63203 (4,7 µH, 2×22 µF) ── +3V3 ── ESP32, écran, SD, GPS ext. (J7), TCA9554, ES8311 (num.), NS4150B
JST J2 (+5V_IN) B5819W                                       ├─ perle ferrite ── A3V3 (ES8311 analogique)
                                                             └─ BAT54C ─┬─ VCC_RTC ── PCF85063
                                                   CR1220 ── BAT54C ────┘
```

- Les deux sources 5 V sont découplées par diodes Schottky (B5819W, SOD-123, 1 A) : on peut laisser l'USB branché
  pour flasher pendant que l'instrument alimente la carte.
- Pas de PMU : plus de bouton PWR ni d'extinction logicielle ; la carte démarre dès que le 5 V est là.
- `AGND` (audio) est relié à `GND` par un net-tie (NT1) : un seul point de masse.
- L'écran AMOLED n'a pas de rétroéclairage : la luminosité se règle par commandes CO5300 (comme sur
  la Waveshare), toute l'alimentation du panneau est en 3,3 V sur la nappe.

## Connecteurs

| Rep. | Type | Brochage |
|---|---|---|
| J1 | USB-C 16P (HRO TYPE-C-31-M-12) | USB 2.0 natif ESP32-S3 (GPIO19/20), flash + logs ; CC 5,1 kΩ ; ESD USBLC6-2SC6 ; TVS SMF5.0A |
| J2 | JST PH 2,0 mm 4 points | 1 = +5 V entrée · 2 = GND · 3 = **TX** (GPIO17, sortie ESP32) · 4 = **RX** (GPIO18) — vers la carte façade |
| J3 | Hirose FH26-31S-0.3SHW | Nappe de l'écran, brochage relevé sur le schéma Waveshare (pins 2-3, 5-6, 8-9, 19, 24 NC ; 21-23 et 26 = 3,3 V) |
| J5 | Molex PicoBlade 1,25 mm 2P (= MX1.25) | Haut-parleur 8 Ω 2 W |
| J6 | microSD Hirose DM3AT | SDMMC 1 bit (CMD GPIO1, CLK GPIO2, D0 GPIO3, D3/CS GPIO41) |
| J7 | JST SH 1,0 mm 6 points (SM06B-SRSS-TB), face F, au bord | GPS externe : 1 = +3V3 · 2 = GND · 3 = SDA · 4 = SCL · 5 = GPS_RST (TCA9554 P7) · 6 = 1PPS (GPIO21) |
| BT1 | Support CMS CR1220 (Linx BAT-HLD-012-SMT), côté écran | Sauvegarde de la RTC PCF85063 (plusieurs années, ≈ 0,25 µA) |
| H1–H3 | Trous M2 | Positions provisoires, à caler sur la mécanique de l'instrument |

## Brochage ESP32-S3 (inchangé par rapport à Waveshare)

| GPIO | Signal | GPIO | Signal |
|---:|---|---:|---|
| 0 | BOOT (bouton, pull-up) | 19 / 20 | USB D− / D+ (22 Ω en série) |
| 1 / 2 / 3 | SD_CMD / SD_CLK / SD_D0 | 21 | 1PPS du GPS externe (J7 broche 6) |
| 4–7 | QSPI SIO0–SIO3 écran | 38 | QSPI_SCL écran |
| 8 | I2S DSDIN → ES8311 | 39 / 40 | LCD_RESET / TP_RESET |
| 9 | I2S SCLK | 41 | SDCS (microSD D3) |
| 10 | I2S ASDOUT ← ES8311 (libre côté codec, sans micro) | 42 | I2S MCLK |
| 11 | TP_INT tactile | 43 / 44 | UART0 TX / RX (console, points de test TP3/TP4) |
| 12 / 13 | LCD_CS / LCD_TE | 45 | I2S LRCK |
| 14 / 15 | I2C SCL / SDA (2,2 kΩ) | 46 | PA_CTRL (ampli, pull-down 10 kΩ) |
| 16 | libre (point de test TP1) | 35–37 | **réservés** PSRAM octale du module, non sortis |
| 17 / 18 | **UART façade TX / RX** (J2) | | |

Bus I2C (0x5A tactile, 0x18 ES8311, 0x20 TCA9554, 0x51 PCF85063, + le GPS externe sur J7 — 0x50/0x54 pour un LC76G comme sur la Waveshare).
TCA9554 : P3 = RTC_INT (pull-up 10 kΩ), P7 = GPS_RST ; P0–P2 et P4–P6 non utilisés.

## PCB

- Contour : cercle Ø46 mm comme la Waveshare (pas encore les encoches latérales pour les boutons ;
  à reprendre avec le dessin coté `IHM/documentation/ecran_rond_1.75/3D/`). Épaisseur 1,0 mm.
- Faces : l'écran est collé sur la face **B.Cu** ; n'y vont que des composants plats (≤ 2,5 mm) :
  nappe J3, microSD J6, pile BT1, codec U4, ampli U5 et leurs passifs. Le module ESP32-S3-WROOM-1
  (3,1 mm), l'USB-C, le JST façade, le GPS J7, le buck, la RTC, l'extenseur et les boutons sont
  sur la face **F.Cu**, à l'arrière de l'instrument.
- Module : antenne PCB tournée vers le bord gauche, débordant légèrement du cercle comme le
  recommande Espressif (antenne au ras ou hors de la carte). Zone `keepout_antenne_WROOM` sur les
  4 couches (ni piste, ni via, ni plan) = courtyard Espressif, 15 mm de chaque côté de l'antenne.
  Rien côté B sous les vias thermiques du pad central (GND) du module.
- microSD : fente vers le bord (bas de la carte), contacts vers le centre.
- Zones GND sur In1, In2, B.Cu et F.Cu (remplies).
- Placement (voir `doc/placement_v0.2.png`) : gros composants placés par recuit simulé
  (`generateur/placer.py` : pas de chevauchement, connecteurs au bord, fente microSD vers
  l'extérieur, antenne au bord, trous M2 écartés, longueur de fil minimisée), passifs près de leur
  circuit par recherche gloutonne. **DRC : 3 erreurs « starved thermal »** (pads GND de J1 et U7 qui
  n'ont qu'un rayon de frein thermique vers le plan F avant routage — disparaissent au routage),
  reste des avertissements de sérigraphie à nettoyer après routage.
- **Routage à faire** (freerouting ou à la main). Netclasses déjà définies dans le projet :
  `USB_90R` (paire USB_N/USB_P 0,18 / 0,18 mm — à recalculer avec le calculateur d'impédance du
  stackup retenu), `Alim` (0,5 mm, vias 0,8/0,4), `QSPI_ecran` (0,15 mm, QSPI_* et LCD_*),
  `Default` 0,2 mm, isolation 0,15 mm. Règles mini : 0,127 mm piste/isolation, via 0,45/0,2 mm,
  trou 0,2 mm (JLCPCB 4 couches). Contraintes : QSPI écran court et groupé (U1 → J3), rien dans la
  zone d'antenne, boucle du buck (U2, L1, C4/C5) compacte, plans GND In1/In2 continus sous l'USB.

## Firmware (`IHM/code/ecran_rond_1.75`)

Le code gère les deux cartes : `idf.py menuconfig` → « KHEPRI - ecran rond 1.75 » → *Carte* =
**Carte ecran rond KHEPRI (BSR)**. Ce choix :

1. remplace le bouton PWR (AXP2101 / TCA9554 P4) par le bouton **BOOT** (GPIO0) dans `kh_pwr_pressed()` ;
2. désactive les gestes à l'accéléromètre (pas de QMI8658) — la consigne viendra du bouton haptique ;
3. initialise l'**UART1 façade** sur GPIO17 (TX) / GPIO18 (RX), 115200 bauds (`kh_facade_uart_*`).
   Transport seulement : le protocole IHM (aujourd'hui en ESP-NOW, `khepri_link`) reste à porter
   dessus. Le GPS (externe, sur J7) reste en I2C ; son 1PPS arrive sur GPIO21.

## Regénérer les fichiers

`generateur/design.py` décrit toute la carte (composants, valeurs, empreintes, nets) ; `gen.py`
produit les feuilles de schéma, la bibliothèque projet, le `.kicad_pro`, la BOM et la netlist ;
`pcbgen.py` produit le `.kicad_pcb` (contour, nets, placement). Ils lisent les bibliothèques KiCad 9
officielles (`symbols/` et `footprints/`) dont le chemin est en tête de `symlib.py` / `fplib.py`.
`placer.py` optimise la position des gros composants (sortie `placement_big.json`, lue par
`pcbgen.py`). Après génération, les composants de la face B sont retournés dans KiCad (le script
les écrit tous côté F) : liste dans `placement.csv`, colonne `face`.
Attention à la convention de rotation au retournement : pour une empreinte de `placement.csv` en face B
avec la rotation *r*, la rotation finale dans KiCad (après retournement) doit valoir *r* ; avec
`batch_move_components` (MCP KiCad) cela revient à demander `rotation = 180 − r` avec `layer = B.Cu`.
`placer.py design_export.json SEED ITER [init.json REF1,REF2,…]` : la forme longue raffine un placement
existant en ne bougeant que les références listées ; `run_sa.sh` lance les recuits deux par deux.

Sources : schéma Waveshare (`IHM/documentation/ecran_rond_1.75/schema/`), `HARDWARE_REFERENCE.md` et
BSP ESP-IDF du dépôt officiel `waveshareteam/ESP32-S3-Touch-AMOLED-1.75`.
