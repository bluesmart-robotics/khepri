# Carte de contrôle — BSR flight power module (KHEPRI)

Carte « cerveau » de l'instrument de tableau de bord KHEPRI (motorisation,
IHM, variomètre, alimentation de secours). Elle est empilée sur la
**BSR Drive 3.7**, la carte de puissance qui pilote les deux moteurs.

Projet KiCad 9 (`e_dolly.kicad_pro`, nom hérité de la carte E-dolly dont elle
dérive), 4 couches, **80 × 150 mm**.

> Cette copie a volontairement divergé de la carte de contrôle du dépôt
> E-dolly, qui garde son E-stop et ses contacteurs. Ne pas reporter les
> changements dans E-dolly.

---

## 1. Vue d'ensemble

```
                     tiges de puissance (bus pack 42-51 V, ex-XT60 J17)
                                   │
          ┌────────────────────────┴───────────────────────────┐
          │  TVS U8 ─ U2 TPS54560 ─► +24V ─┬─► ventilateur J25  │
          │                                ├─► chargeur secours │
          │                                └─► diode idéale ─┐  │
          │   cellules 2S J21+J22 ─► Q14 ─► diode idéale ────┤  │
          │                                       VSYS_OR ─ R70 ─► +VSYS
          │      +VSYS ─┬─► U1 MP2565 ─► +3.3V (ESP32, logique) │
          │             ├─► U20 L78L05 ─► +5V_SENS ─► 3 LDO capteurs
          │             └─► U30 AP63205 ─► +5V_IHM ─► façade (J20)
          │                                                     │
          │  ESP32 U6 ── CAN (U3) ── J17 (BSR Drive) / J4 (fuselage)
          │      ├── I²C ─ TCA9548A U5 ─ ch0 PMP / ch1 secours /  │
          │      │                       ch2 pression / ch3 env.   │
          │      ├── SPI ─ IMU U4                                  │
          │      ├── UART1/UART2 ─ façade (J20)                    │
          │      └── PWM/tachy ─ ventilateur (J25)                 │
          └─────────────────────────────────────────────────────┘
```

L'ESP32 de cette carte est la **référence unique** : il est le seul à envoyer
les consignes à la BSR Drive (CAN). La façade (smart knob, écrans) ne fait que
proposer des consignes, que l'ESP32 valide.

### Feuilles du schéma

| Feuille | Contenu |
|---|---|
| `e_dolly.kicad_sch` (racine) | ESP32, CAN, IMU, multiplexeur I²C, USB, alimentations 24 V et 3,3 V, mesure de tension bus |
| `capteurs_pression.kicad_sch` | variomètre et anémomètre : 2 × MS5611 + HSC, leurs alimentations |
| `liaison_ihm.kicad_sch` | nappe vers la carte façade, alimentation 5V_IHM |
| `alim_secours.kicad_sch` | batterie de secours 2S, chargeur, jauge, OU par diodes idéales |
| `mesures_env.kicad_sch` | température d'air, courant et tension consommés par l'instrument |
| `interface_pmp.kicad_sch` | liaison vers la carte de charge USB-C EPR (PMP41115) |
| `ventilation.kicad_sch` | commande du ventilateur de la BSR Drive |

---

## 2. Alimentations

| Rail | Produit par | Depuis | Alimente |
|---|---|---|---|
| **+36V** (nom historique) | — | bus du pack 42–51 V via J17 (futures tiges) | U2, mesure bus (R41–R43), TVS U8 SMCJ58A, C24 100 µF |
| **+24V** (≈ 25 V) | U2 TPS54560, L2 15,4 µH, D2 | +36V | diode idéale bus (U36/Q12), 5V_CHG (U34), ventilateur J25 |
| **VSYS_OR** | OU de U36/Q12 (bus) et U37/Q13 (secours) | +24V ou cellules 2S | shunt R70 |
| **+VSYS** | R70 20 mΩ | VSYS_OR | U1, U20, U30. Vaut ≈ 25 V sur bus, 6–8,4 V sur secours |
| **+3.3V** | U1 MP2565, L1 10 µH | +VSYS | ESP32, CAN, IMU, TCA9548A, CP2102N, I²C |
| **+5V_SENS** | U20 L78L05 (100 mA) | +VSYS | 3 LDO LP5907 des capteurs de pression |
| **+3V3_STAT / _TE / _PITOT** | U21 / U22 / U23 LP5907, ferrites L10–L12 | +5V_SENS | un capteur de pression chacun |
| **+5V_IHM** (2 A) | U30 AP63205, L13 6,8 µH | +VSYS | carte façade (J20), coupable par IO13 |
| **+5V_CHG** | U34 AP63205, L14 6,8 µH | +24V (bus seul) | chargeur BQ25887 |
| **BAT_P** (6–8,4 V) | cellules 2S J21 + J22 via F1 | — | chargeur, jauge, Q14 |

Points à comprendre :
- **Sans bus** (planeur éteint, BMS ouvert), +24V tombe. Si l'interrupteur
  J23 est fermé, la batterie de secours alimente +VSYS via U37/Q13 :
  l'instrument démarre, affiche, logue et peut envoyer l'ordre CAN d'armement
  au BMS. Quand le bus arrive, +24V (25 V) dépasse la batterie (≤ 8,4 V) et
  la diode idéale côté secours se bloque toute seule.
- **Tout ce qui est en aval de +VSYS doit accepter 6 V** : U1 (VIN ≥ 4,5 V),
  U30 (VIN ≥ 3,8 V, sortie 5 V un peu juste à 6 V), U20 L78L05 (sortie ≈ 4,3 V
  à 6 V d'entrée, suffisant pour les LDO 3,3 V).
- Le chargeur n'est alimenté que par le bus (U34 sur +24V) : il ne peut pas
  « se charger lui-même » en boucle sur la batterie.

Valeurs à recontrôler : U2 sortie = 0,8 V × (1 + R21/R22) = 0,8 × (1 + 100k/3,3k)
≈ 25 V ; U1 sortie = 0,8 V × (1 + R18/R14) = 0,8 × (1 + 127k/40,2k) ≈ 3,33 V.

---

## 3. Calculateur et communications

### ESP32-WROOM-32UE (U6) — affectation de toutes les broches

| GPIO | Broche | Fonction | Réseau | Vers |
|---|---|---|---|---|
| EN | 3 | reset | /EN | SW1, R25/C17 |
| IO0 | 25 | boot | /IO0 | SW2, R27 |
| IO1 / IO3 | 35 / 34 | UART0 programmation et console | ESP_TX / ESP_RX | CP2102N U7, LED D4/D3 |
| IO16 / IO17 | 27 / 28 | CAN RX / TX | /RXD, /TXD | SN65HVD230 U3 |
| IO5 / IO4 | 29 / 26 | I²C principal SDA / SCL (pull-up R23/R20) | /SDA, /SCL | TCA9548A U5 |
| IO22 / IO23 / IO25 | 36 / 37 / 10 | SPI SCLK / MOSI / MISO | /SCLK /MOSI /MISO | IMU U4 |
| IO26 | 11 | SPI CS IMU (pull-up R17) | /IO26 | U4 |
| IO27 | 12 | interruption IMU | /IO27 | U4 INT1 |
| IO33 | 9 | ADC tension bus | /PDT_pwr_switched | pont R41+R42 / R43, R44, C25, clamp D7 |
| IO18 / IO34 | 30 / 6 | UART1 TX / RX ↔ STM32 haptique | IHM_TX1 / IHM_RX1 | J20 |
| IO19 / IO35 | 31 / 7 | UART2 TX / RX ↔ ESP32-S3 façade | IHM_TX2 / IHM_RX2 | J20 |
| IO21 | 33 | NRST du STM32 (drain ouvert) | IHM_NRST | J20 |
| IO14 | 13 | BOOT0 du STM32 | IHM_BOOT0 | J20 |
| IO13 | 16 | coupure 5V_IHM (haut = coupé) | IHM_PWR_OFF | Q10 |
| IO36 (SENSOR_VP) | 4 | interruption commune (entrée seule) | INT_PWR | PMP, BQ25887, MAX17205, TMP117, INA226 |
| IO12 | 14 | autorisation de charge PMP (pull-down R84, **doit rester bas au boot**) | PMP_CHG_EN | J24 |
| IO15 | 23 | PWM ventilateur | FAN_PWM | Q15 |
| IO39 (SENSOR_VN) | 5 | tachymètre ventilateur (entrée seule) | FAN_TACH | J25 |

**Plus aucune broche libre.** Les ajouts futurs (arrêts d'urgence, coupure du
faisceau chaussettes) passeront par un expandeur I²C.

### CAN — U3 SN65HVD230

- Relié à la BSR Drive par J17 (broches 1 = CANL, 2 = CANH, 3 = GND) et au
  fuselage par J4 (JST PH 3 : 1 = CANH, 2 = CANL, 3 = GND, sérigraphie
  « GND / CAN H / CAN L »).
- R19 120 Ω : terminaison **fixe** (à rendre commutable, voir § 9).
- R15 10 k sur Rs : mode pente contrôlée.

### I²C — multiplexeur TCA9548A (U5, 0x70)

| Canal | Réseau | Composants (adresse) | Pull-up |
|---|---|---|---|
| 0 | I2C_PMP | carte de charge PMP41115 via J24 | R82/R83 4,7 k |
| 1 | I2C_PWR | BQ25887 (0x6B), MAX17205 (0x36 et 0x0B) | R99/R100 4,7 k |
| 2 | I2C_PRES | MS5611 statique (0x76), MS5611 TE (0x77), HSC (0x28) | R50/R51 4,7 k (sur +3V3_STAT) |
| 3 | I2C_ENV | TMP117 (0x48), INA226 (0x40) | R80/R81 4,7 k |
| 4–7 | — | libres | — |

### Autres

- **IMU U4 ICM-42688-P** (accéléromètre et gyroscope 6 axes, SPI) : fusion
  avec les capteurs de pression pour le vario.
- **USB U7 CP2102N** : programmation de l'ESP32 par J14 (JST PH 6 vers une
  prise USB-C déportée), LED TX/RX D3/D4, reset automatique.
- **SW1** = EN (reset), **SW2** = IO0 (boot).

---

## 4. Variomètre et anémomètre (`capteurs_pression.kicad_sch`, haut de carte)

| Capteur | Réf. | Mesure | Repère sérigraphie |
|---|---|---|---|
| MS5611 | U24 | pression statique (altitude, vario brut) | `P-statique` |
| MS5611 | U25 | pression de la sonde d'énergie totale (vario compensé) | `P-TE` |
| HSCMRRN001PD2A3 | U26 | Δp pitot ±1 psi (vitesse air) | `Δp pitot`, ports `P-tot` / `P-stat` |

- Chaque capteur a sa propre chaîne d'alimentation : ferrite, LDO LP5907
  3,3 V faible bruit et découplage, pour que le bruit numérique ne pollue pas
  la mesure.
- Les deux MS5611 sont chacun dans une **zone de capot étanche** de
  11 × 12 mm (`CAPOT_STATIQUE`, `CAPOT_TE`) : contour en sérigraphie et zone
  d'interdiction de vias, car un via sous le capot ferait fuir l'air. Les capots
  sont imprimés en 3D et reliés aux prises par tube.
- Le HSC a ses propres embouts cannelés ; ses ports sortent vers le bord haut.

---

## 5. Liaison façade (`liaison_ihm.kicad_sch`, bord droit)

Une seule nappe **J20 JST-GH 12 voies** vers la carte façade (smart knob
STM32 + écran 1,64" ESP32-S3 fusionnés ; l'écran 1,75" est raccordé à la
façade). Tout est en 3,3 V.

| J20 | Signal | ESP32 |
|---|---|---|
| 1, 2, 3 | +5V_IHM (2 A au total) | — |
| 4, 11, 12 | GND | — |
| 5 | TX1 → RX du STM32 | IO18 |
| 6 | RX1 ← TX du STM32 | IO34 |
| 7 | TX2 → RX de l'ESP32-S3 | IO19 |
| 8 | RX2 ← TX de l'ESP32-S3 | IO35 |
| 9 | NRST du STM32 | IO21 |
| 10 | BOOT0 du STM32 | IO14 |

- **Côté façade, croiser TX et RX.** Prendre l'USART1 du STM32 (PA9/PA10)
  pour que le reflash par BOOT0 fonctionne.
- Protections : 100 Ω en série sur chaque ligne (R54–R59), pull-up 10 k sur
  les deux RX (R60/R61, IO34/35 n'en ont pas), pull-down 10 k sur BOOT0
  (R62), diodes ESD TPD4E05U06 (U31, U32).
- Alimentation : U30 AP63205. Son EN est tiré à +VSYS (R52) ; Q10 le met à la
  masse quand IO13 est haut. La façade est donc alimentée par défaut, et
  l'ESP32 peut la redémarrer à froid.

---

## 6. Alimentation de secours (`alim_secours.kicad_sch`, bande gauche)

- **Deux cellules Li-ion 18650 en série (2S)**. J21 à gauche = cellule basse,
  J22 à droite = cellule haute, JST PH 2.0 2 voies, **1 = −, 2 = +** sur les
  deux. Le point milieu BAT_MID relie le + de J21 au − de J22.
  **Cellules protégées (PCM intégré) obligatoires.**
  Pourquoi la série : à 6–8,4 V, la batterie se met en OU directement sur
  +VSYS, sans convertisseur élévateur.
- **F1** fusible réarmable 1812 sur le + de la pile, **R71** 10 mΩ côté −
  (shunt de la jauge).
- **J23 interrupteur de tableau de bord** (PH 2.0 2P) : contact fermé entre
  les broches 1 et 2 → Q14 (PMOS AO3401A) conduit et branche la batterie.
  Ouvert, la batterie est isolée : elle ne se vide pas au parking. Une diode
  idéale seule ne suffirait pas, la diode interne du MOSFET conduirait.
- **OU par diodes idéales** : U36 LM74700 + Q12 SI2318 côté bus (+24V),
  U37 + Q13 côté secours (BAT_SW). Sortie commune VSYS_OR → R70 → +VSYS.
- **Chargeur U35 BQ25887** (élévateur 2S, équilibrage des cellules, I²C 0x6B) :
  - entrée +5V_CHG (U34, depuis le bus seulement) ;
  - courant d'entrée limité à 1 A max par R88 1,27 kΩ, réglable plus bas en I²C ;
  - équilibrage par R91 100 Ω (≈ 40 mA), R92 300 Ω de protection sur MID ;
  - thermistance TH1 (pont R89/R90) ;
  - CD et PSEL à la masse (charge autorisée, mode adaptateur) ;
  - ~INT sur INT_PWR ; ~PG et ~STAT non utilisés.
- **Jauge U38 MAX17205** (ModelGauge m5) : état de charge, tension de chaque
  cellule, courant via R71 ; I²C 0x36 / 0x0B ; alerte sur INT_PWR ;
  thermistance TH2 sur AIN1.

---

## 7. Mesures d'environnement et carte de charge

**`mesures_env.kicad_sch`** (en haut à gauche, à côté des capteurs de
pression) :
- **U40 TMP117** (0x48, ±0,1 °C) : température de l'air, pour la vitesse
  vraie et la densité. À garder loin des sources de chaleur.
- **U41 INA226** (0x40) : courant et tension de +VSYS, c'est-à-dire la
  consommation totale de l'instrument, sur bus comme sur secours. Shunt R70
  20 mΩ, filtre R96/R97/C62.

**`interface_pmp.kicad_sch`** — J24 JST PH 2.0 6 voies, sérigraphie
« to USB-C EPR », vers la carte de charge PMP41115 empilée :

| J24 | Signal |
|---|---|
| 1 | GND |
| 2 | +3,3 V (référence logique et pull-up) |
| 3 / 4 | SDA / SCL (canal 0) |
| 5 | INT (ligne commune INT_PWR, drain ouvert, pull-up R85) |
| 6 | CHG_EN (IO12, pull-down R84) |

La puissance de charge (240 W) ne passe pas par ce connecteur mais par les
tiges de puissance.

---

## 8. Ventilation de la BSR Drive (`ventilation.kicad_sch`, bas gauche)

- **J25 JST XH 4 voies**, ventilateur **8038 24 V** sur le +24V interne.
  Brochage supposé : 1 rouge +24V, 2 noir GND, 3 jaune tachymètre, 4 bleu
  PWM. **À vérifier sur le ventilateur reçu.**
- PWM 25 kHz en drain ouvert : Q15 2N7002 commandé par IO15 (R101/R102).
  Au démarrage, IO15 est haut → PWM bas → ventilateur lent.
- Tachymètre sur IO39 : pull-up R103 10 k, filtre R104 1 k / C67 1 nF.
  Sert à détecter une panne du ventilateur et brider les moteurs.

Dimensionnement (détail des hypothèses dans l'historique ci-dessous) :
≈ 100 W de pertes dans la BSR Drive à 6 kW, dimensionné à 125 W. Avec un
dissipateur 80 × 39 mm coupé à 150 mm et ≈ 22 CFM réels, le dissipateur monte
≈ 22 K au-dessus de l'air entrant (≈ 24 K à 3000 m). Conditions : carénage qui
force tout l'air entre les ailettes, et puissance de freinage dissipée limitée
en logiciel.

---

## 9. Ce qui reste à faire

1. **Tiges de puissance** à la place du XT60 J17 (diamètre et entraxe à
   fournir), et nouveau connecteur pour les 4 signaux CAN vers la BSR Drive.
2. **Plans de cuivre du bas** : les grandes zones +36V datent de l'E-stop,
   quand tout le courant moteur traversait la carte. La carte ne prend plus
   que ≈ 8 A sur le bus : réduire ces zones autour des tiges, de U2, du futur
   convertisseur et de U8.
3. **Faisceau chaussettes** : convertisseur dédié **24 V / 10 A** coupable par
   l'ESP32 (via expandeur I²C) et par l'arrêt d'urgence matériel ; connecteur
   Deutsch DTM 6 voies (CANH, CANL, 2 × V+, 2 × V−) à la place de J4 ;
   terminaison CAN 120 Ω commutable. Dans chaque chaussette : 24 → 12 V pour
   le servo RDS51150 (8 A au blocage) et 3,3 V pour l'ESP32.
4. **Arrêts d'urgence** : bouton du tableau → EN_GATE de la BSR Drive (via les
   « +4 » du XT60 aujourd'hui), lecture d'état ; expandeur I²C PCA9555.
5. **Finitions** : trous de fixation (aucun dans le fichier), routage complet,
   DRC, fichiers de production (`production/` contient encore ceux de
   l'ancienne carte du 17/10/2026, sauf la netlist).

---

## 10. Comment vérifier ce travail

**Ce qui a été vérifié automatiquement**
- ERC KiCad 9.0.6 : **0 erreur**, 9 avertissements connus (voir plus bas).
- Chaque pastille du `.kicad_pcb` porte le net de la netlist exportée par
  kicad-cli (`production/khepri_v9.net`), contrôle par script.
- Pas de chevauchement de courtyard entre les composants ajoutés.

**Ce qui n'a pas été fait** : routage des nouveaux blocs, remplissage des
zones, DRC. Les anciennes pistes sont restées, certaines traversent les
nouvelles zones.

**À vérifier par un humain, dans cet ordre**
1. Ouvrir le projet, remplir les zones (B), lancer le DRC. Erreurs attendues :
   - cuivre +VSYS (ex-plan +24V) encore relié aux pastilles +24V autour de U2
     → la liaison U2 → U36/Q12 est à router ;
   - pistes et vias dans les capots ;
   - composants non routés.
2. Empreintes déduites, à comparer à la datasheet :
   - `Honeywell_HSC_SMT_RR_PROVISOIRE` (U26) : entraxe des rangées 11,24 mm,
     pastilles 1,84 × 1,14 mm, port 1 = P-tot supposé ;
   - MS5611 sur `LGA-8_3x5mm_P1.25mm` ;
   - BQ25887 sur `Texas_RGE0024C` (taille de la pastille exposée à confirmer) ;
   - MAX17205 : AIN2 et CELLX mis à la masse (à confirmer avec la config
     nPackCfg).
3. Brochages des connecteurs externes : J21/J22 (1 = −), J25 (ventilateur),
   J20 (croisement TX/RX côté façade), J24.
4. Valeurs : ILIM du BQ25887 (R88), CBSET (R91), pont de mesure bus
   (R41 + R42 = 102 k / R43 5,62 k → 54 V donnent 2,8 V sur IO33).
5. Tenue en tension : le +36V verra jusqu'à ≈ 54 V. Vérifier C3, C4, C22,
   C23, C24 et U2 (TPS54560 : 60 V max).

**Avertissements ERC connus** (sans effet sur le circuit) : empreinte
`L_7.1x6.6mm` (L1, L2) absente de KiCad 9, bibliothèque BAT54 de D7
absente, symbole et empreinte XT60 2+4 de J17 hors bibliothèques standard,
deux conflits de type de broche hérités de l'E-dolly.

---

## 11. Fichiers et bibliothèques

| Fichier | Rôle |
|---|---|
| `e_dolly.kicad_pro` / `.kicad_sch` / `.kicad_pcb` | projet KiCad (racine) |
| `*.kicad_sch` | feuilles listées au § 1 |
| `fp-lib-table`, `sym-lib-table` | bibliothèques du projet, chemins `${KIPRJMOD}` |
| `Bibliotheque_kicad/` | symboles, empreintes et modèles 3D maison ; `khepri_Symboles/khepri.kicad_sym` regroupe les symboles créés pour KHEPRI |
| `production/khepri_v9.net` | netlist de référence (kicad-cli) |
| `production/` (reste) | fichiers de l'**ancienne** carte, à régénérer |
| `bom.csv`, `e_dolly.csv`, `layout.png`, `apercu-3d.png` | anciens, de la carte E-dolly |

Les chemins des modèles 3D hérités pointent vers un autre poste
(`C:/Users/tayfu/...`). Les repointer vers
`${KIPRJMOD}/Bibliotheque_kicad/edolly_3D/` pour le rendu 3D.

**Ne pas utiliser** la mise à jour schéma → PCB du serveur KiCad MCP
(`sync_schematic_to_board`) : elle coupait des nets en deux. La touche F8 de
KiCad reste sûre.

---

## 12. Historique des modifications (novembre 2026)

- **Retiré de la carte E-dolly** : E-stop à MOSFET tête-bêche et précharge
  (Q5–Q9, D5, D6, R28, R29, R32, R35–R40), commande des contacteurs (J1, J2,
  Q3, Q4, R9–R11, R13), boucle E-stop (J8, J9), entrées XT60 J10/J13 et XT30
  J15, connecteurs périphériques E-dolly (J3, J5, J6, J7, J16, Q1, Q2, R1, R2,
  R4, R6), sérigraphies associées. `PWR_SWITCHED` fusionné dans `+36V`. À
  6 kW, la coupure se fait en amont : contacteur du BMS, EN_GATE de la BSR
  Drive, sectionneur cockpit.
- **Ajouté** : capteurs de pression, liaison façade, passage en 80 mm,
  alimentation de secours, mesures d'environnement, interface carte de charge,
  ventilation.
- **Bilan thermique détaillé** (base du § 8) : courant de phase ≈ 61 A eff.
  par moteur à 3 kW sous 42 V ; par moteur ≈ 34 W de conduction
  (R_DS(on) chaud supposé 3 mΩ ; 23 W à 2 mΩ, 45 W à 4 mΩ), ≈ 7 W de
  commutation (24 kHz, 80 ns), ≈ 4 W dans les shunts. Dissipateur supposé à
  20 ailettes de 1,2 × 33 mm (à remesurer), écoulement laminaire,
  h ≈ 40 W/m²K, R_th ≈ 0,17–0,19 K/W. Perte de charge ≈ 70–85 Pa à 22 CFM.
