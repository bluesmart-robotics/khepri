# KHÉPRI — écrans pilote (ESP-IDF 5.5, LVGL 8.3)

Deux écrans côte à côte sur le tableau de bord, chacun avec un rôle fixe :

| Écran | Carte | Rôle | Dossier |
|---|---|---|---|
| **Rond 1,75"** | Waveshare ESP32-S3-Touch-AMOLED-1.75 (466×466) | commande et effet : jauge de régime, consigne, alertes | [`ecran_rond_1.75/`](ecran_rond_1.75/) |
| **Rectangulaire 1,64"** | Waveshare ESP32-S3-Touch-AMOLED-1.64 (280×456, portrait) | énergie : batterie, solaire, bilan | [`ecran_energie_1.64/`](ecran_energie_1.64/) |

Forme différente = nature différente : rond = moteur, rectangle = énergie. Pas de changement de
page en vol, mêmes couleurs sur les deux écrans, rien d'affiché quand tout va bien.

![Les deux écrans](docs/apercus/cote_a_cote.png)

Les deux écrans reçoivent la même télémétrie du maître (ESP-NOW) et se mettent à jour seuls
depuis ce dépôt GitHub quand ils sont connectés au Wi-Fi.

## Arborescence

```
ecran_rond_1.75/      projet ESP-IDF de l'écran rond (main.c, khepri_screen.c, gestes knob.c…)
ecran_energie_1.64/   projet ESP-IDF de l'écran énergie (main.c, energie_screen.c), cartes V1 / V1.1
components/           commun aux deux écrans et à la passerelle
  kh_common/          liaison ESP-NOW (khepri_link), Wi-Fi (kh_wifi), mises à jour OTA (kh_ota)
  kh_fonts/           polices Montserrat 14-64 px + thème de couleurs (kh_theme.h)
  khepri_proto/       protocole ESP-NOW, trames CAN, modèle, calcul d'autonomie, simulation (C pur)
  lvgl/, esp-now/, cmake_utilities/, esp_lcd_sh8601/, i2c_bsp/, qmi8658/
gateway_sim/          passerelle de simulation (rôle du maître) + khepri_relay à copier dans le maître
release/              binaires publiés et manifestes OTA (voir ci-dessous)
tools/                publier_maj.py, aperçus PC (tools/preview)
tests/                tests PC (protocole, modèle, autonomie, gestes)
docs/apercus/         images des deux écrans (rond/, energie/, cote_a_cote.png)
```

## Premier flash par USB (une seule fois par écran)

| Écran | Fichier à flasher à l'adresse 0x0 |
|---|---|
| Rond 1,75" | `release/ecran_rond/khepri_ecran_rond_1.75_0x0.bin` |
| 1,64" révision V1 (sérigraphie en haut) | `release/ecran_energie/khepri_ecran_energie_1.64_V1_0x0.bin` |
| 1,64" révision V1.1 (sérigraphie à droite) | `release/ecran_energie/khepri_ecran_energie_1.64_V1.1_0x0.bin` |
| Passerelle de simulation (tout ESP32-S3) | `release/passerelle/khepri_passerelle_simulation_0x0.bin` |

```
python -m esptool --chip esp32s3 -p COM11 -b 460800 write-flash 0x0 <fichier>
```

ou https://espressif.github.io/esptool-js/ (Chrome/Edge). Le flash USB écrit aussi la table de
partitions OTA ; ensuite tout passe par le Wi-Fi. Un écran rond déjà en 1.4.1 n'a pas besoin du
flash USB : il passe tout seul en 1.5.0 (l'ancien manifeste `release/version.json` reste tenu à
jour et pointe vers la nouvelle image).

## Mises à jour OTA

Connecté au Wi-Fi, chaque écran lit **son** manifeste toutes les 60 s :

| Écran | Manifeste | Champ de l'image |
|---|---|---|
| Rond | `release/ecran_rond/version.json` | `url` |
| 1,64" V1 / V1.1 | `release/ecran_energie/version.json` | `url_V1` / `url_V1.1` |

Si la version diffère de la sienne (`PROJECT_VER`), il télécharge l'image en HTTPS, l'écrit dans
l'autre partition et redémarre ; si elle ne tient pas 20 s, le chargeur revient à l'ancienne.
Jamais pendant que les moteurs tournent (ni pendant une alerte sur le rond). Le CDN de GitHub
garde les fichiers jusqu'à 5 min.

**Publier :**

```
# 1. augmenter set(PROJECT_VER "...") dans ecran_rond_1.75/CMakeLists.txt et/ou ecran_energie_1.64/CMakeLists.txt
python tools/publier_maj.py tout --compiler     # ou rond / energie / passerelle
git add -A release ecran_* && git commit -m "écrans : …" && git push
```

Le dépôt est public et les images ne sont pas signées : à verrouiller (branche protégée,
Secure Boot) avant un usage en vol. Wi-Fi et ESP-NOW partagent la radio : connecté à un point
d'accès sur un autre canal que le maître, un écran perd la liaison avion (indiqué à l'écran).
Le Wi-Fi est prévu pour le sol.

## Tester le double affichage

**Avec les deux écrans seulement** (pas de passerelle) — l'écran rond en simulation sert de maître :

1. Écran rond : page SYSTÈME, **SIMU** activée (c'est le réglage d'usine).
2. Écran 1,64" : glisser deux fois vers la droite → RÉGLAGES, **SIMULATION : NON**, puis **LIER**,
   les deux écrans à quelques centimètres l'un de l'autre.
3. Le 1,64" affiche SIMULATION en haut et suit le rond : en roulant le rond vers la droite (consigne
   de régime en hausse), la flèche de bilan du 1,64" passe vers le bas.

**Avec la passerelle** (un troisième ESP32-S3) : flasher la passerelle, désactiver SIMU sur les
deux écrans, puis LIER chacun. La passerelle sert les deux écrans et suit la consigne du rond.

Pour revenir au vrai maître : DÉLIER puis LIER à côté du maître.

## Liaison ESP-NOW (`components/khepri_proto/include/khepri_espnow.h`)

1. **Appairage** : LIER sur l'écran, à quelques centimètres du maître. L'écran est initiateur
   `espnow_ctrl` (attribut `KEY_7`) ; il retient en NVS le premier maître qui lui envoie de la
   télémétrie et ignore ensuite les autres. Le maître sert jusqu'à 4 écrans liés.
2. **Télémétrie maître → écrans** (10 Hz) : trames CAN telles quelles — SYNC 0x600, chaussettes
   0x601-0x605 et 0x621-0x625, BSR Drive `Get_Encoder_Estimates` (0x009) et
   `Get_Bus_Voltage_Current` (0x017) des axes 0 et 1, **énergie 0x680 (batterie : SOC, U, I, Wh) et
   0x681 (solaire : W par aile, crête installée)** calculées par le maître à partir de l'EMUS G1 et
   des MPPT Victron.
3. **Commandes écran → maître** (mot de 32 bits) : `1` acquittement d'alarme, `2` REPÈRE,
   `3` enregistrement, `4` battement de cœur 1 Hz, `5` consigne de régime en ‰ (rond seulement).

`gateway_sim/main/khepri_relay.c` est fait pour être copié dans le firmware du maître :
`khepri_relay_init()` après le Wi-Fi, puis `khepri_relay_push(id, data, dlc)` pour chaque trame.

## Tests et aperçus sur PC

```
make -C tests && ./tests/test_proto && ./tests/test_knob
make -C tools/preview && ./tools/preview/preview docs/apercus/rond && ./tools/preview/preview_energie docs/apercus/energie
```
