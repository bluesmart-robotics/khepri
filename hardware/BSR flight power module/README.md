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
- **Carte de contrôle** — pilote le bus CAN entre la commande moteur et la
  façade.
- **Alimentation de secours** — une ou deux cellules 18650 LiFePO4, pour
  continuer à afficher les erreurs et à journaliser si la batterie du planeur
  défaille.
- **Ventilation pilotée** et surveillance des températures.

## Sous-modules

Un clonage simple laisse les sous-modules vides. Pour récupérer leur contenu :

```
git clone --recurse-submodules https://github.com/bluesmart-robotics/khepri
```

ou, si le dépôt est déjà cloné :

```
git submodule update --init --recursive
```
