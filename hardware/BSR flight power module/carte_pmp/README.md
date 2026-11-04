# Carte de charge PMP — version KHEPRI empilable

Chargeur USB-C EPR 240 W dérivé du design TI **PMP41115** (TPS26750 + BQ25756),
repris du dépôt `bluesmart-robotics/batterie` (`chargeur_pmp41115/`, Altium) et
converti en KiCad 9 pour être empilé au-dessus de la carte de contrôle et de la
BSR Drive. **Le dépôt `batterie` n'est pas modifié** (E-dolly garde sa version).

## Ce qui a été fait (04/11/2026)

1. **Import Altium → KiCad** (`PMP41115.PcbDoc`, importeur Altium de KiCad),
   puis ouverture et sauvegarde en KiCad 9.
2. **Bloc PMP tourné de −90°** et placé dans le contour de la pile : bloc
   d'origine 103 × 47 mm maintenant en x 134,5 → 181,3, y 31,0 → 134,0. Le
   connecteur USB-C J1 est en haut, à ≈ 7 mm du bord haut (bord à y 28,85).
   L'ancien contour Altium est conservé sur la couche `User.9` comme repère.
3. **Contour agrandi à 80 × 150 mm**, **même repère que la carte de contrôle**
   (x 104 → 184, y 28,85 → 178,85) : les deux fichiers KiCad se superposent
   sans décalage.
4. **XT60 2+4 J7**, même empreinte et **même position que J17 de la carte de
   contrôle** (138,125 ; 156,075), côté dessous (B.Cu). Liaison bidirectionnelle
   vers le pack : P = `BAT` (sortie/entrée batterie du BQ25756), N et broche 3 =
   GND, broches 1, 2, 4 (CAN, ESTOP) non reliées sur cette carte.
   Liaison en pistes de 1,5 mm (BAT sur B.Cu ≈ 23 mm, GND) : **à remplacer
   par des zones de cuivre** pour 5 A permanents.
5. **Connecteur de communication J8**, JST PH 6 voies (même empreinte que J24
   de la carte de contrôle, même position x 108,75 ; y 57), câble court entre
   les deux cartes :

   | J8 | Signal carte de contrôle (J24) | Net sur la PMP |
   |---|---|---|
   | 1 | GND | GND |
   | 2 | +3,3 V | `3V3_CTRL` — **non relié** (ne pas raccorder au LDO_3V3 de la PMP) |
   | 3 | SDA canal 0 | `I2Ct_SDA` (I²C cible du TPS26750, comme J6) |
   | 4 | SCL canal 0 | `I2Ct_SCL` |
   | 5 | INT_PWR | `~{I2Ct_IRQ}` (drain ouvert) |
   | 6 | CHG_EN | `CHG_EN` — **non relié** : choisir la broche (EN du BQ25756, ILIM_HIZ ou GPIO du TPS26750) |

6. **Trous M3** H11 à H15 aux positions des trous de la BSR Drive qui tombent
   hors du bloc PMP : (124,53 ; 66,30), (124,53 ; 104,80), (124,53 ; 147,30),
   (148,13 ; 147,30), (166,53 ; 157,30). Les trous BSR (148,13 ; 66,30) et
   (148,13 ; 104,80) tombent dans le bloc PMP et n'ont pas été percés (choix
   validé : garder le routage puissance d'origine).

## À reprendre par l'ingénieur

1. **Schéma** : il reste en Altium dans le dépôt `batterie`. J7, J8, H11–H15 et
   leurs nets n'existent que dans ce PCB. À reporter dans un schéma (ou importer
   les SchDoc dans KiCad) avant toute mise à jour schéma → PCB.
2. **Erreurs d'import Altium à vérifier contre le PcbDoc d'origine** (DRC KiCad :
   ≈ 1 060 erreurs, aucune ne vient des ajouts ci-dessus) :
   - pastilles « complex pad stack » non importées correctement : pastilles
     thermiques sans net de Q4, Q5, Q6, Q7 et U5 → 81 « courts-circuits »
     avec les vias VSYS, P_OUT, SW1, SW2 ;
   - 199 vias 0,3/0,2 mm d'Altium hors des règles par défaut de KiCad
     (diamètre, anneau) : ajuster les règles ou les vias ;
   - U8.5 (GND) et U1.28/29 (PP5V) non reliés après import.
3. Remplacer les pistes BAT / GND de J7 par des zones, ajouter des vias en
   parallèle.
4. Raccorder J8.2 et J8.6 (tableau ci-dessus).
5. Modèles 3D des composants : non repris de l'Altium, le STEP de cette carte
   ne montre que le circuit nu et les composants ajoutés.
