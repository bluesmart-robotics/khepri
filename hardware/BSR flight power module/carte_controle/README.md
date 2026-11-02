# PCB de contrôle

Carte de contrôle de l'instrument KHEPRI, dérivée de la carte de contrôle du
E-dolly. Projet KiCad 9 `e_dolly`, 4 couches.

## Modifications pour KHEPRI (novembre 2026)

Cette copie a volontairement divergé de celle du dépôt E-dolly, qui garde son
E-stop et ses contacteurs. Ne pas reporter ces changements dans E-dolly.

- **Supprimé** : l'E-stop à MOSFET tête-bêche et sa précharge (Q5 à Q9, D5,
  D6, R28, R29, R32, R35 à R40), la commande des contacteurs (J1, J2, Q3, Q4,
  R9, R10, R11, R13), la boucle E-stop (J8, J9) et les entrées de puissance
  XT60 J10/J13 et XT30 J15. À 6 kW, la coupure se fait en amont (contacteur
  du BMS, EN_GATE de la BSR Drive, sectionneur cockpit).
- **Alimentation** : la carte est alimentée par la XT60 2+4 (J17) depuis la
  BSR Drive. L'ancien réseau `PWR_SWITCHED` est fusionné dans `+36V` (qui
  verra jusqu'à environ 54 V : vérifier la tenue des condensateurs d'entrée).
- **Ajouté** : feuille `capteurs_pression.kicad_sch` — MS5611 statique (0x76),
  MS5611 TE (0x77) et Honeywell HSCMRRN001PD2A3 pitot ±1 psi (0x28) sur le
  canal 2 du TCA9548A, chacun avec sa ferrite et son LDO LP5907 3,3 V, derrière
  un L78L05 depuis le +24 V. Conçue à partir des fiches techniques ; les cartes
  OpenVario, Larus et XCVario n'ont servi que de référence d'architecture.
- **À faire** : empreinte Honeywell HSC SMT à créer, layout du bloc capteurs
  (zone libre pour capots étanches imprimés en 3D), régénération des fichiers
  de `production/` qui correspondent encore à l'ancienne carte.

ERC KiCad 9.0.6 : 0 erreur, 9 avertissements (empreintes `L_7.1x6.6mm` et
XT60 2+4 hors bibliothèques standard, BAT54 sans bibliothèque).

## Ce qu'il y a dessus

D'après la nomenclature :

- **ESP32-WROOM-32UE** (U6) — calculateur et radio, antenne externe.
- **SN65HVD230** (U3) — transceiver CAN, pour parler aux BSR Drive (voir
  `../../control_motor/`).
- **ICM-42688-P** (U4) — centrale inertielle 6 axes.
- **TCA9548A** (U5) — multiplexeur I²C 8 canaux.
- **CP2102N** (U7) — pont USB-UART pour la programmation.
- **MP2565** (U1) et **TPS54560** (U2) — deux abaisseurs de tension.
- Protection d'entrée SMCJ58A, diodes SS34 / SS56, petits MOSFET AO3400A
  pour les commandes logiques.
- Capteurs de pression du variomètre (voir plus haut).
- Connectique : XT60 2+4 vers la BSR Drive (puissance + CAN), nappes JST PH
  pour les périphériques, deux boutons SKRKAEE020.

## Fichiers

| Fichier | Rôle |
|---|---|
| `e_dolly.kicad_pro` / `.kicad_sch` / `.kicad_pcb` | projet KiCad |
| `capteurs_pression.kicad_sch` | feuille du bloc capteurs de pression |
| `fp-lib-table`, `sym-lib-table` | bibliothèques du projet, en chemins relatifs `${KIPRJMOD}` |
| `bom.csv` | nomenclature avec références LCSC |
| `e_dolly.csv` | nomenclature détaillée |
| `layout.png`, `apercu-3d.png` | aperçus du routage et du rendu 3D |
| `production/` | BOM, désignateurs, positions, netlist IPC et `e_dolly.zip` de l'**ancienne** carte (17/10/2026), à régénérer |

## Bibliothèques

`Bibliotheque_kicad/` contient les symboles, empreintes et modèles 3D maison
(13 modèles STEP, 13 empreintes, 13 symboles), plus `khepri_Symboles/` avec
les symboles du bloc capteurs (MS5611, HSC, LP5907, L78L05). Toutes ces
bibliothèques sont déclarées dans les tables du projet.

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

## État du placement (02/11/2026)

- Connecteurs E-dolly retirés (J3, J5, J6, J7, J16, Q1, Q2, R1, R2, R4, R6).
  **J4 est conservé** : c'est le connecteur CAN externe (CANH/CANL/GND).
- Bloc capteurs placé en haut de carte (zone libérée par l'ancien E-stop) :
  deux zones de capot étanche de 11 × 12 mm (`CAPOT_STATIQUE`, `CAPOT_TE`),
  matérialisées en sérigraphie et en zones d'interdiction de vias ; seul le
  MS5611 se trouve dans chaque zone. Le HSC (pitot) est à droite, ports vers
  le bord haut. Les LDO, ferrites et condensateurs sont sur une rangée sous
  les capots.
- Le plan +36V en B.Cu, qui remontait jusqu'au haut de carte pour l'E-stop,
  est rogné à y ≥ 118 mm. **Zones à remplir (touche B) avant DRC.**
- Empreinte `Honeywell_HSC_SMT_RR_PROVISOIRE` : **à vérifier** contre le plan
  de pastilles SMT de la datasheet Honeywell avant fabrication.
- Routage du bloc capteurs non fait ; d'anciennes pistes traversent encore les
  capots et seront à reprendre.
- `production/khepri_v5.net` : netlist exportée par kicad-cli, de référence.

### Points ouverts (à reprendre plus tard)

- **Trous de fixation** : le fichier ne contient aucun perçage. Les trous qui
  servent à visser la carte sur la BSR Drive sont à ajouter, ou à reprendre
  de leur définition actuelle. J17 et le contour 50 × 150 mm n'ont pas bougé.
- **Empreinte HSC** (`Honeywell_HSC_SMT_RR_PROVISOIRE`) : pas 2,54 mm, entraxe
  des rangées 11,24 mm, pastilles 1,84 × 1,14 mm, ports vers −Y. Ces cotes
  sont déduites, faute d'avoir pu lire le plan complet de la datasheet
  Honeywell (HSC_DS.pdf). À vérifier avant fabrication.
- **Empreinte MS5611** : `Package_LGA:LGA-8_3x5mm_P1.25mm` de KiCad. Elle est
  cohérente avec le boîtier, mais à comparer avec le plan de pastilles TE.
- **Anciennes pistes** : quelques-unes traversent encore les capots, avec deux
  vias dans le capot TE. Le DRC (zone d'interdiction de vias) les signalera ;
  elles sont à refaire au routage.
- **Zones de cuivre** : à remplir (touche B) avant DRC, en particulier le
  +36V B.Cu rogné.
- **Mise à jour schéma → PCB par le serveur KiCad MCP (`sync_schematic_to_board`)** :
  à ne pas utiliser. Elle coupait des nets en deux (/CANH, /EN, /IO0,
  /PDT_pwr_switched séparés de leurs pistes). La carte a été mise à jour à
  partir de la netlist kicad-cli (`production/khepri_v5.net`). Dans KiCad, la
  commande F8 classique reste sûre.
- **Avertissements ERC restants** : empreinte `L_7.1x6.6mm` (L1, L2) absente
  de KiCad 9, bibliothèque BAT54 de D7 absente, symbole et empreinte XT60 2+4
  de J17 hors bibliothèques.
- **Repérage des prises de pression** (sérigraphie) : `P-statique` (U24),
  `P-TE` (U25), `P-tot` / `P-stat` sur les deux ports du HSC (U26, Δp pitot).
  L'attribution port 1 = P-tot (haute pression) / port 2 = P-stat est supposée
  et reste à confirmer sur le plan Honeywell du boîtier RR.
- **Suite prévue** : DRC, routage Freerouting, régénération de `production/`.
