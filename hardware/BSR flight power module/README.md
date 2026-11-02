# BSR Flight Power Module

Instrument de vol de KHEPRI : un module de 80 mm de diamètre qui réunit la
commande moteur, le bus CAN et l'interface pilote dans un seul boîtier à
dissipateur.

## Architecture

- **Façade** — smart knob et écran rond Waveshare ESP32-S3-Touch-AMOLED 1,75".
  Le code des écrans pilote vit dans [`../../IHM/`](../../IHM/) à la racine de
  ce dépôt, et se met à jour en OTA depuis là.
- **`bsr-drive/`** — contrôleur moteur BSR Drive v3.7, en sous-module vers le
  dépôt [bluesmart-robotics/Bsr-drive](https://github.com/bluesmart-robotics/Bsr-drive).
  Y vivent le firmware (fork ODrive), les GUI et la conception de la carte
  (`hardware/bsr-drive-v3.7/`).
- **`carte_controle/`** — carte qui pilote le bus CAN entre la commande moteur
  et la façade : ESP32-WROOM-32UE, transceiver CAN SN65HVD230, centrale
  inertielle ICM-42688-P, multiplexeur I²C TCA9548A, deux abaisseurs MP2565 et
  TPS54560. Projet KiCad 9 avec sa nomenclature, ses gerbers et sa
  bibliothèque.
- **Alimentation de secours** — une ou deux cellules 18650 LiFePO4, pour
  continuer à afficher les erreurs et à journaliser si la batterie du planeur
  défaille.
- **Ventilation pilotée** et surveillance des températures.

## Deux modes de rattachement, à connaître

`bsr-drive/` est un **sous-module** : il pointe vers un commit du dépôt
Bsr-drive, qui reste la seule source de vérité. Une mise à jour là-bas se
récupère ici par un `git submodule update --remote`.

`carte_controle/` est une **copie**, prise du dépôt E-dolly (`hardware/pcb_controle/`)
au commit `20ab2e3`. E-dolly étant privé et ce dépôt public, un sous-module
n'était pas possible. Conséquence à garder en tête : les deux copies peuvent
diverger. Toute modification de cette carte doit être reportée à la main dans
l'autre dépôt, ou bien la carte doit être sortie dans son propre dépôt et
sous-modulée depuis les deux projets.

## Sous-modules

Un clonage simple laisse les sous-modules vides. Pour récupérer leur contenu :

```
git clone --recurse-submodules https://github.com/bluesmart-robotics/khepri
```

ou, si le dépôt est déjà cloné :

```
git submodule update --init --recursive
```
