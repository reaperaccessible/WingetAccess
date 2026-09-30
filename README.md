# WingetAccess

**Gérez vos logiciels Winget au clavier, avec votre lecteur d'écran.**
*Manage your Winget software from the keyboard, with your screen reader.*

Par [ReaperAccessible](https://reaperaccessible.fr) — logiciels accessibles pour
personnes non-voyantes.

---

## Français

WingetAccess est une application **portable** (un seul `WingetAccess.exe`, rien
à installer) qui pilote le gestionnaire de paquets Winget de Windows sans
jamais avoir à copier un identifiant ni à taper une commande. L'interface
utilise des contrôles Windows natifs : NVDA, JAWS et le Narrateur lisent tout
naturellement.

### Ce qu'il fait

- **Installés** (Ctrl+1) — les paquets de votre machine gérés par une source
  Winget, avec nom, identifiant, version et mise à jour disponible.
- **Mises à jour** (Ctrl+2) — uniquement ce qui a une version plus récente ;
  Entrée met à jour la sélection, Ctrl+Maj+U met tout à jour d'un coup.
- **Recherche** (Ctrl+3) — tapez un mot, Entrée, et installez le résultat
  choisi avec Entrée. Plus jamais de `winget install` à écrire.
- **Description** — dans chaque onglet, Tab depuis la liste lit la description
  du paquet sélectionné (auteur, page d'accueil, licence, version), chargée en
  arrière-plan par `winget show`.
- **Désinstallation** — Suppr sur une ligne, avec confirmation ; la version
  exacte est ciblée quand un même identifiant couvre plusieurs installations.
- **Journal** — toute la sortie de winget reste lisible en bas de la fenêtre,
  et les événements sont annoncés au lecteur d'écran (« Mise à jour de
  Firefox… », « Terminé », « 10 mises à jour »).
- **Autonome** — au démarrage, WingetAccess vérifie winget et le prépare tout
  seul : sur une machine fraîchement installée, il enregistre, répare ou
  installe le Programme d'installation d'application ; il accepte les
  conditions des sources ; et quand une nouvelle version de winget existe, il
  l'installe en premier, avant tout le reste.
- **Mises à jour automatiques** — au démarrage, WingetAccess regarde s'il
  existe une nouvelle version sur GitHub. Si oui, il la télécharge, vérifie son
  empreinte, prend sa place et redémarre tout seul, sans jamais interrompre
  une installation en cours. Menu Aide : « Rechercher une mise à jour de
  WingetAccess » pour vérifier à la demande.

### Raccourcis

- Ctrl+1 / Ctrl+2 / Ctrl+3 : changer d'onglet (le nom est annoncé)
- Entrée : action contextuelle (mettre à jour, ou installer depuis la recherche)
- Ctrl+U : mettre à jour la sélection ; Ctrl+Maj+U : tout mettre à jour
- Ctrl+I : installer la sélection
- Suppr : désinstaller la sélection (avec confirmation)
- Ctrl+Maj+C : copier l'identifiant du paquet
- F5 : actualiser l'onglet courant ; Ctrl+H : rappel des raccourcis

### Prérequis

- Windows 10 ou 11. winget (Programme d'installation d'application) est
  préparé automatiquement s'il manque ; une connexion Internet peut alors être
  nécessaire.
- Aucune installation : posez `WingetAccess.exe` où vous voulez et lancez-le.
  Les demandes d'élévation (UAC) viennent de winget lui-même, au moment voulu.

### Compiler

wxWidgets 3.3 statique via vcpkg (triplet `x64-windows-static`), CMake, MSVC :

```
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release
```

L'exécutable est produit dans `build/Release/WingetAccess.exe`.

---

## English

WingetAccess is a **portable** application (a single `WingetAccess.exe`,
nothing to install) that drives the Windows Winget package manager without ever
copying an ID or typing a command. The UI uses native Windows controls: NVDA,
JAWS and Narrator read everything out of the box.

### Features

- **Installed** (Ctrl+1) — the packages on your machine managed by a Winget
  source, with name, ID, version and available update.
- **Updates** (Ctrl+2) — only what has a newer version; Enter upgrades the
  selection, Ctrl+Shift+U upgrades everything at once.
- **Search** (Ctrl+3) — type a word, press Enter, and install the chosen
  result with Enter. No more typing `winget install`.
- **Description** — in every tab, Tab from the list reads the selected
  package's description (author, homepage, license, version), fetched in the
  background with `winget show`.
- **Uninstall** — Delete on a row, with confirmation; the exact version is
  targeted when one ID covers several installations.
- **Log** — winget's full output stays readable at the bottom of the window,
  and events are announced to the screen reader.
- **Self-sufficient** — at startup, WingetAccess checks winget and prepares
  it on its own: on a freshly installed machine it registers, repairs or
  installs App Installer; it accepts the source agreements; and when a newer
  winget exists, it installs it first, before anything else.
- **Automatic updates** — at startup, WingetAccess checks GitHub for a newer
  version; if there is one, it downloads it, verifies its hash, takes its place
  and restarts on its own, never cutting an install short. Help menu: "Check
  for a WingetAccess update" to check on demand.

### Requirements

- Windows 10 or 11. winget (App Installer) is prepared automatically when
  missing; an Internet connection may then be needed.
- No installation: put `WingetAccess.exe` anywhere and run it. Elevation
  prompts (UAC) come from winget itself, when needed.

### Building

Static wxWidgets 3.3 via vcpkg (`x64-windows-static` triplet), CMake, MSVC:

```
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release
```

The executable lands in `build/Release/WingetAccess.exe`.

## Licence / License

GPL-3.0 — voir / see [LICENSE](LICENSE).
