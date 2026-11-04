# KHÉPRI — Blue Smart Robotics

- [`IHM/`](IHM/) : écrans pilote — rond 1,75" (régime moteur) et rectangulaire 1,64" (énergie),
  sur cartes Waveshare ESP32-S3-Touch-AMOLED — documentation et code, mises à jour OTA depuis ce dépôt.
- [`hardware/BSR flight power module/`](hardware/BSR%20flight%20power%20module/) : instrument de vol
  de 80 mm — commande moteur BSR Drive v3.7 (sous-module), carte de contrôle CAN et interface pilote.
  Contient un sous-module : cloner avec `--recurse-submodules`.
  - [`ecran_rond_1.75/`](hardware/BSR%20flight%20power%20module/ecran_rond_1.75/) : carte écran rond
    1,75" propre à KHEPRI (KiCad 9), dérivée de la Waveshare sans PMU/batterie, micros ni IMU —
    schéma complet, PCB Ø46 mm à router.

L'ancien code de démonstration de l'écran AMOLED 1,64" (bsr_screen) reste consultable dans
l'historique Git (commit 8a9e916).
