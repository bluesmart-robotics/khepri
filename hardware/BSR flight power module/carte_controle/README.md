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
                XT60 2+4 J17 vers la BSR Drive (bus pack 42-51 V)
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
          │  +36V ─ U43 LM5146 ─► +24V_EXT 10 A ─► J26 faisceau     │
          │  ESP32 U6 ── CAN (U3) ── J28 (BSR Drive) / J26 (faisceau)
          │      ├── I²C ─ TCA9548A U5 ─ ch0 PMP / ch1 secours /  │
          │      │            ch2 pression / ch3 env. / ch4 PCA9555│
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
| `faisceau_au.kicad_sch` | XT60 J17, trous de fixation, liaison BSR Drive, faisceau chaussettes 24 V, arrêt d'urgence, expandeur I²C |

---

## 2. Alimentations

| Rail | Produit par | Depuis | Alimente |
|---|---|---|---|
| **+36V** (nom historique) | — | bus du pack 42–51 V via la XT60 J17 | U2, U43, mesure bus (R41–R43), TVS U8 SMCJ58A, C24 100 µF |
| **+24V_EXT** (≈ 24,3 V, 10 A) | U43 LM5146 + Q18/Q19 NTMFS6H800NL, L16 10 µH XAL1510 | +36V | faisceau des chaussettes (J26), coupable |
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

**Plus aucune broche libre.** Les commandes ajoutées ensuite (faisceau,
arrêt d'urgence) passent par l'expandeur PCA9555 (§ 9).

### CAN — U3 SN65HVD230

- Relié à la BSR Drive par J28 et au faisceau des chaussettes par J26 (§ 9).
  L'ancienne XT60 J17 et l'ancien connecteur J4 sont supprimés.
- Terminaison 120 Ω (R112) **commutable** par le cavalier JP1 « TERM CAN ».
  L'ancienne terminaison fixe R19 est supprimée.
- R15 10 k sur Rs : mode pente contrôlée.

### I²C — multiplexeur TCA9548A (U5, 0x70)

| Canal | Réseau | Composants (adresse) | Pull-up |
|---|---|---|---|
| 0 | I2C_PMP | carte de charge PMP41115 via J24 | R82/R83 4,7 k |
| 1 | I2C_PWR | BQ25887 (0x6B), MAX17205 (0x36 et 0x0B) | R99/R100 4,7 k |
| 2 | I2C_PRES | MS5611 statique (0x76), MS5611 TE (0x77), HSC (0x28) | R50/R51 4,7 k (sur +3V3_STAT) |
| 3 | I2C_ENV | TMP117 (0x48), INA226 (0x40) | R80/R81 4,7 k |
| 4 | I2C_X | PCA9555 (0x20) | R107/R108 4,7 k |
| 5–7 | — | libres | — |

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

La puissance de charge (240 W) ne passe pas par ce connecteur : elle va directement
sur le bus du pack.

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
≈ 100 W de pertes dans la BSR Drive à 6 kW, dimensionné à 125 W. **Mise à jour** :
la nomenclature de la BSR Drive donne 28 × NTMFS6H800NL (80 V), soit
probablement **2 MOSFET en parallèle par interrupteur**. La conduction par moteur
tomberait alors vers ≈ 15–20 W, et le total vers ≈ 60–70 W, d'où une marge
confortable. À confirmer avec le R_DS(on) de la fiche technique. Avec un
dissipateur 80 × 39 mm coupé à 150 mm et ≈ 22 CFM réels, le dissipateur monte
≈ 22 K au-dessus de l'air entrant (≈ 24 K à 3000 m). Conditions : carénage qui
force tout l'air entre les ailettes, et puissance de freinage dissipée limitée
en logiciel.

---

## 9. XT60, fixation, faisceau et arrêt d'urgence (`faisceau_au.kicad_sch`)

**Alignement sur la BSR Drive.** Les positions viennent du fichier Altium de
la BSR Drive (`bsr-drive/hardware/bsr-drive-v3.7/PCB.PcbDoc`, lu par script).
- **Correspondance de repère :** x_carte = y_BSR + 91,5 ; y_carte = 186,3 − x_BSR.
- **Calage :** la largeur de 50 mm de la BSR Drive retombe exactement sur
  l'ancienne largeur de cette carte (x 119 → 169), et sa XT60 J9 sur l'ancienne
  J17.
- **Débord :** la BSR Drive dépasse de ≈ 9,5 mm au-dessus du bord haut de cette
  carte, et cette carte dépasse de ≈ 19 mm sous la BSR Drive.

**Liaison à la BSR Drive : XT60 2+4 J17, inchangée** (même empreinte que la
BSR Drive, sous la carte). P = +BUS, N = GND, 1 = CANL, 2 = CANH, 3 = GND,
4 = ESTOP_OK via R121 (0 Ω, à ne monter que si la broche 4 est câblée côté BSR
Drive pour couper EN_GATE).

**Faisceau des chaussettes : U43 LM5146 + Q18/Q19, +24V_EXT 10 A**
- Valeurs de base :
  - FB R115/R116 = 130 k / 4,42 k → 24,3 V ;
  - RT 40,2 k → 250 kHz ;
  - L16 10 µH XAL1510 ;
  - 3 × 2,2 µF 100 V en entrée, 4 × 22 µF 50 V en sortie.
- **R117 (ILIM) et la compensation R114/C70/C71 sont à calculer** avec le
  « LM5146 Quickstart Calculator » de TI.
- **Validation :** EN = ESTOP_OK **ET** EXT_ON (U44 74LVC1G08, R118/R119).
  Sécurité positive : sans 3,3 V, ou bouton ouvert, ou EXT_ON bas par défaut
  (R109), le faisceau est coupé.
- **J26 Micro-Fit 3.0 2 × 3 vertical** (vers le DTM 6 voies) : 1–2 +24V_EXT,
  3–4 GND, 5 CANH, 6 CANL.
- Dans chaque chaussette : 24 → 12 V pour le servo RDS51150, 3,3 V pour l'ESP32.

**Arrêt d'urgence**
- **J27** (JST PH 2 vertical) : bouton du tableau à **contact normalement
  fermé**, qui relie le +3,3 V (via R105 1 k) à ESTOP_OK.
- **Ouvert ou câble arraché** → ESTOP_OK tiré bas par R106. La BSR Drive (J28.4)
  et le faisceau sont alors coupés, même si l'ESP32 est planté.
- **Arrêt logiciel :** Q16 (SOFT_STOP) force ESTOP_OK bas.

**Expandeur U42 PCA9555** (0x20, canal 4) :

| Broche | Fonction |
|---|---|
| IO0_0 | EXT_ON (sortie, pull-down) |
| IO0_1 | SOFT_STOP (sortie, pull-down) |
| IO0_2 | lecture ESTOP_OK |
| IO0_3 | lecture EXT_PGOOD |

- Son ~INT est sur la ligne commune INT_PWR.
- IO0_4–7 et IO1_x sont libres : les configurer en sorties dans le logiciel.

**Connecteurs** : tous verticaux et repérés en sérigraphie :

| Réf. | Sérigraphie | Va vers |
|---|---|---|
| J14 | USB PROG | prise USB-C de programmation |
| J17 (dessous) | XT60 -> BSR DRIVE | BSR Drive (bus + CAN) |
| J20 | VERS FACADE | carte façade (smart knob + écran 1,64) |
| J21 / J22 | CELL GAUCHE / CELL DROITE 1- 2+ | cellules de secours |
| J23 | INTER SECOURS | interrupteur de secours du tableau |
| J24 | to USB-C EPR (PMP) | carte de charge |
| J25 | FAN 24V PWM | ventilateur 8038 |
| J26 | FAISCEAU CHAUSSETTES 24V+CAN | DTM 6 voies |
| J27 | ARRET URGENCE (NF) | bouton du tableau |
| JP1 | TERM CAN | cavalier de terminaison |

## 10. Plans de cuivre

| Couche | Contenu |
|---|---|
| F.Cu | GND sur toute la carte (priorité 0) ; +36V autour de la broche P de J17 ; anciennes petites zones locales des alimentations conservées. La zone de sortie de U2 est renommée +24V. |
| In1.Cu | GND sur toute la carte |
| In2.Cu | +3.3V (haut, y < 107,5) et +VSYS (bas) sur toute la largeur de 80 mm |
| B.Cu | GND sur toute la carte ; +36V en bande y 147 → 165 de J17 jusqu'au convertisseur U43, prolongée jusqu'à U2 |

- Les anciennes grandes zones +36V, de l'époque de l'E-stop, sont supprimées.
- Les 52 vias +36V qui les cousaient sont supprimés aussi.
- **Toutes les zones sont non remplies dans le fichier : faire B (remplir)
  avant le DRC.**

## 11. Ce qui reste à faire

1. **Finir le routage** : voir la section 11 bis, étape par étape.
2. **Calculer R117 (ILIM) et la compensation de U43**, vérifier la dissipation
   de Q18/Q19 à 10 A.
3. **Confirmer** :
   - câblage de la broche 4 de la XT60 côté BSR Drive (ESTOP_OK → EN_GATE),
     sinon laisser R121 non monté.
4. **Fichiers de production** (`production/` contient encore ceux de
   l'ancienne carte, sauf la netlist) : pas générés tant que les 16
   liaisons ne sont pas faites.

## 11 bis. Reprise par l'ingénieur : quoi toucher, dans quel ordre

État au 04/11/2026 : carte routée à environ 99 %, DRC sans erreur d'isolation,
de bord, de largeur de piste ni de court-circuit. Elle **n'est pas encore
fabricable** : 16 liaisons manquent. Tout se fait dans KiCad 9 (éditeur de PCB).
Coordonnées en mm, origine KiCad (carte de x 104 à 184, y 28,85 à 178,85).

Avant de commencer : ouvrir `e_dolly.kicad_pcb`, remplir les zones (touche B),
lancer Inspecter > DRC en cochant « Tester les connexions non routées ».
KiCad doit afficher les mêmes 16 liaisons que ci-dessous.

### Étape 1 : router les 16 liaisons manquantes (bloquant)

Toutes sont au pied de boîtiers à pas fin, coincées par des pistes de
l'autoroutage. Utiliser le routeur interactif (X) en mode « pousser-écarter »,
ou déplacer/supprimer les pistes gênantes puis re-router.

| Net | Broche à relier | Position | Remarque |
|---|---|---|---|
| I2C_PWR_SDA | U38.12 et U35.4 | (108,4 ; 90,0) et (105,4 ; 99,0) | bus I2C jauge/chargeur → U5 |
| I2C_PWR_SCL | U38.11 | (108,4 ; 90,4) | idem |
| GAUGE_REG2 | U38.3 → C58 | (105,4 ; 90,0) | découplage REG2 du MAX17205 |
| GAUGE_AIN | U38.9 → R95/TH2 | (108,4 ; 91,2) | thermistance |
| CHG_MID | U35.9 → R92 | (107,1 ; 100,7) | BQ25887, mesure point milieu |
| I2C_ENV_SDA | U40.6 | (106,6 ; 30,1) | capteur d'environnement |
| /Faisceau AU tiges/EXT_FB | U43.5 | (178,9 ; 140,4) | contre-réaction LM5146 |
| +36V | U43.20 | (181,3 ; 138,5) | alim VIN du LM5146 : piste ≥ 0,5 mm ou via vers la bande +36V B.Cu |
| GND | C50.2 / C51.2 | (111,2 ; 96,8) | via vers In1 (plan GND) |
| GND | C60.2, U41.1, U41.2 | (108,2 ; 30,1) | idem |
| GND | U40.2, U40.4, U40.7 | (104,8 ; 30,7) | idem |
| GND | U41.7 | (114,7 ; 31,7) | idem |
| GND | U5.9 | (144,6 ; 96,1) | relier à la pastille centrale de U5 |
| GND | C17.1 | (140,3 ; 83,4) | via vers In1 |
| GND | U35 broches 3, 19, 20, 24, 25 | (106,1 ; 96,8) | relier à la pastille centrale de U35 |

Zone la plus dense : U35/U38 en bas à gauche (x 104–112, y 88–101), 8 des 16
liaisons. Il peut être plus simple d'y supprimer les pistes de l'autoroutage
et de tout refaire à la main.

### Étape 2 : nettoyer (non bloquant, mais à faire)

1. **13 bouts de piste en l'air** (avertissement DRC « piste non connectée »),
   restes du second autoroutage, à supprimer :
   - (112,2 ; 36,1) : +3.3V sur F.Cu ;
   - (104,6 ; 100,8) : BAT_P sur In1.Cu ;
   - (104,9 ; 112,4) : CHG_TS sur F.Cu ;
   - (105,5 ; 94,2) : GAUGE_VB sur F.Cu ;
   - (104,9 ; 133,6) : OR_VCAP_U37 sur F.Cu ;
   - (109,2 ; 29,8) : I2C_ENV_SDA sur F.Cu ;
   - (104,6 ; 98,7) et (104,7 ; 99,2) : I2C_PWR_SCL sur F.Cu ;
   - (104,6 ; 96,2) : I2C_PWR_SCL sur In1.Cu ;
   - (110,8 ; 29,7) : INT_PWR sur F.Cu ;
   - (110,6 ; 29,5) : INT_PWR sur In1.Cu et In2.Cu ;
   - (151,9 ; 169,3) : Net-(U2-EN) sur F.Cu ;
   Freerouting a aussi posé quelques pistes de signal sur In1/In2 (plans) :
   les déplacer sur F.Cu/B.Cu si possible, pour ne pas découper les plans.
2. **45 erreurs « frein thermique incomplet »** : pastilles reliées à une zone
   par un seul rayon (40 sur la zone GND F.Cu, 5 sur B.Cu). Ajouter une courte
   piste ou un via à côté de la pastille, ou réduire le minimum de rayons dans
   les propriétés de la zone.
3. **18 vias posés dans une pastille** (ajoutés par script pour relier les
   plans). Soit commander des vias bouchés (via-in-pad rempli), soit les
   décaler à côté de la pastille avec une courte piste :
   R12.2 (145,8 ; 115,6), C53.2 (115,2 ; 102,6), C46.2 (110,1 ; 117,0), C52.2 (112,8 ; 102,6), U31.8 (175,3 ; 84,6 — pastille de 0,3 mm, à décaler en priorité), C60.2 (108,2 ; 30,1), C1.1 (135,8 ; 112,2), C8.2 (148,3 ; 101,1), C10.1 (149,2 ; 111,7), C76.2 (167,2 ; 146,1), C25.1 (147,0 ; 88,7), J20.4 (182,7 ; 91,1), C66.2 (105,6 ; 138,2), C18.1 (122,3 ; 117,5), U36.2 (105,3 ; 130,2), L1.1 (153,8 ; 105,6), C10.2 (149,2 ; 109,8), U2.2 (150,8 ; 170,7).
4. **Avertissements de sérigraphie** (≈ 250) : textes qui se chevauchent ou
   débordent sur le cuivre ; à ranger avant fabrication.

### Étape 3 : corriger le schéma (sinon F8 défait le PCB)

- **U43 (LM5146), pastille centrale 21** : sans net dans le schéma. Elle a été
  mise à GND directement dans le PCB. Ajouter la broche EP/PGND au symbole et
  la relier à GND, sinon la mise à jour schéma → PCB (F8) la remet sans net
  et les pistes GND qui la traversent deviennent des courts-circuits.

### Étape 4 : règles et choix à valider

- **Vias** : les vias ajoutés font 0,45 mm / perçage 0,2 mm. Les règles du
  projet ont été abaissées à ce minimum (Carte > Configuration > Contraintes).
  Accepté par JLCPCB en 4 couches ; vérifier avec le fabricant choisi.
- **Isolation au bord** : 0,5 mm (règle du projet), respectée partout.
- **Netclasses** : +36V à 3 mm et +24V à 1,5 mm dans la DSN ; plusieurs
  liaisons de puissance passent en 0,5 mm ou par les zones. Vérifier les
  courants (U43 → Q18/Q19 à 10 A) et élargir si besoin.

### Étape 5 : fichiers de fabrication (quand le DRC est propre)

Fichiers > Fabrication : Gerbers (F.Cu, In1.Cu, In2.Cu, B.Cu, masques,
sérigraphies, Edge.Cuts), perçage Excellon (PTH/NPTH séparés), fichier de
placement, BOM. Les remplacer dans `production/` (qui contient encore ceux
de l'ancienne carte E-dolly).

### Rendu 3D

Modèles absents : J26 (Micro-Fit 2×3, aucun modèle KiCad) et J17 (XT60 2+4,
STEP resté sur l'ancien poste `C:/Users/tayfu/...`). Sans effet sur la
fabrication.

## 12. Comment vérifier ce travail

**Ce qui a été vérifié automatiquement**
- ERC KiCad 9.0.6 : **0 erreur**, 9 avertissements connus (voir plus bas).
- Chaque pastille du `.kicad_pcb` porte le net de la netlist exportée par
  kicad-cli (`production/khepri_v11.net`), contrôle par script.
- Pas de chevauchement de courtyard entre les composants ajoutés.

**Ce qui n'a pas été fait** : 16 liaisons non routées, nettoyage et
fichiers de fabrication (voir § 11 bis).

**À vérifier par un humain, dans cet ordre**
1. Ouvrir le projet, remplir les zones (B), lancer le DRC. Erreurs attendues :
   - anciennes pistes +VSYS/+24V autour de U2
     → la liaison U2 → U36/Q12 est à router ;
   - pistes et vias dans les capots ;
   - composants non routés.
2. Empreintes déduites, à comparer à la datasheet :
   - `Honeywell_HSC_SMT_RR_PROVISOIRE` (U26) : entraxe des rangées 11,24 mm,
     pastilles 1,84 × 1,14 mm, port 1 = P-tot supposé ;
   - MS5611 sur `LGA-8_3x5mm_P1.25mm` ;
   - BQ25887 sur `Texas_RGE0024C` (taille de la pastille exposée à confirmer) ;
   - MAX17205 : AIN2 et CELLX mis à la masse (à confirmer avec la config
     nPackCfg) ;
   - LM5146 sur `Texas_RGY_R-PVQFN-N20` et NTMFS6H800NL sur `ONSemi_SO-8FL_488AA`.
3. Brochages des connecteurs externes : J21/J22 (1 = −), J25 (ventilateur),
   J20 (croisement TX/RX côté façade), J24.
4. Valeurs : ILIM du BQ25887 (R88), CBSET (R91), pont de mesure bus
   (R41 + R42 = 102 k / R43 5,62 k → 54 V donnent 2,8 V sur IO33).
5. Tenue en tension : le +36V verra jusqu'à ≈ 54 V. Vérifier C3, C4, C22,
   C23, C24 et U2 (TPS54560 : 60 V max).

**Avertissements ERC connus** (sans effet sur le circuit) : empreinte
`L_7.1x6.6mm` (L1, L2) absente de KiCad 9, bibliothèque BAT54 de D7
absente, deux conflits de type de broche hérités de l'E-dolly. ERC : 0 erreur,
7 avertissements.

---

## 13. Fichiers et bibliothèques

| Fichier | Rôle |
|---|---|
| `e_dolly.kicad_pro` / `.kicad_sch` / `.kicad_pcb` | projet KiCad (racine) |
| `*.kicad_sch` | feuilles listées au § 1 |
| `fp-lib-table`, `sym-lib-table` | bibliothèques du projet, chemins `${KIPRJMOD}` |
| `Bibliotheque_kicad/` | symboles, empreintes et modèles 3D maison ; `khepri_Symboles/khepri.kicad_sym` regroupe les symboles créés pour KHEPRI |
| `production/khepri_v11.net` | netlist de référence (kicad-cli) |
| `production/` (reste) | fichiers de l'**ancienne** carte, à régénérer |
| `bom.csv`, `e_dolly.csv`, `layout.png`, `apercu-3d.png` | anciens, de la carte E-dolly |

Modèles 3D (04/11/2026) : U1, U4, U8 et D7 pointent maintenant vers
`${KIPRJMOD}/Bibliotheque_kicad/edolly_3D/` (modèles constructeur pour U1,
U4, U8 ; modèle générique SOT-323 pour D7). Sur le poste d'Antoine, les
modèles absents de la bibliothèque KiCad 9 (JST PH SMD, SO-8FL, TDFN-14,
fusible 1812, ESP32-WROOM-32UE, R-PDSO-G8) ont été remplacés par des
équivalents de même boîtier, copiés sous le nom attendu dans le dossier 3D de
KiCad. Restent sans modèle : J26 (Micro-Fit 2×3, aucun modèle KiCad) et J17
(XT60 2+4, STEP resté sur l'ancien poste `C:/Users/tayfu/...`).

**Ne pas utiliser** la mise à jour schéma → PCB du serveur KiCad MCP
(`sync_schematic_to_board`) : elle coupait des nets en deux. La touche F8 de
KiCad reste sûre.

---

## 14. Historique des modifications (novembre 2026)

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
