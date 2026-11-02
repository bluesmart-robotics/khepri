# PCB de contrôle

Carte de contrôle principale du E-dolly. Projet KiCad 9 `e_dolly`, titre
« PCB e-dolly », révision v1 du 05/02/2026, 4 couches.

## Ce qu'il y a dessus

D'après la nomenclature :

- **ESP32-WROOM-32UE** (U6) — calculateur et radio, antenne externe.
- **SN65HVD230** (U3) — transceiver CAN, pour parler aux BSR Drive (voir
  `../../control_motor/`).
- **ICM-42688-P** (U4) — centrale inertielle 6 axes.
- **TCA9548A** (U5) — multiplexeur I²C 8 canaux.
- **CP2102N** (U7) — pont USB-UART pour la programmation.
- **MP2565** (U1) et **TPS54560** (U2) — deux abaisseurs de tension.
- Étage de puissance : MOSFET AO3400A, IPB110P06LMATMA1 et SQD50P06, avec
  protection SMCJ58A et diodes de roue libre SS34 / SS56.
- Connectique : XT30PW-M pour la puissance, nappes JST PH pour les
  périphériques, deux boutons SKRKAEE020.

## Fichiers

| Fichier | Rôle |
|---|---|
| `e_dolly.kicad_pro` / `.kicad_sch` / `.kicad_pcb` | projet KiCad |
| `bom.csv` | nomenclature avec références LCSC |
| `e_dolly.csv` | nomenclature détaillée |
| `layout.png`, `apercu-3d.png` | aperçus du routage et du rendu 3D |
| `production/` | BOM, désignateurs, positions, netlist IPC et `e_dolly.zip` (gerbers 4 couches + perçages, générés le 17/10/2026) |

## Bibliothèques

`Bibliotheque_kicad/` contient les symboles, empreintes et modèles 3D maison
(13 modèles STEP, 13 empreintes, 13 symboles).

Attention, les chemins des modèles 3D inscrits dans le `.kicad_pcb` sont des
chemins absolus pointant vers un autre poste
(`C:/Users/tayfu/.../Bibliotheque_kicad/edolly_3D`). Ils ne se résoudront donc
pas tels quels après clonage, même si le fichier est maintenant présent ici.
Sur les dix modèles ainsi référencés, cinq sont dans cette bibliothèque
(`ICM-42688-P`, `QFN10_MP2565_MNP`, `SMCJ58A`, `XT30PW-M`, `B4B-PH-K-S`) et
cinq restent introuvables : `BZT52C15GW`, `IPB110P06LMATMA1`, `MMBTA42LT1G`,
`SOT-323-3_2P2X1P35_ONS` et `XT60(2+4)-F`.

Pour que les cinq premiers s'affichent, il faut repointer leur chemin sur
`${KIPRJMOD}/Bibliotheque_kicad/edolly_3D/`. Cela n'affecte ni le schéma ni le
routage, seulement le rendu 3D.
