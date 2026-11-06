# Packaging & déploiement

## Windows (.exe) — fait

`app/tools/build.sh windows` produit **`app/release/Les-Theoremes.exe`** : un seul fichier
(~1,8 Mo), 64 bits, sans installation, sans runtime à télécharger.

Contenu de l'exécutable :

| élément | rôle |
|---|---|
| serveur HTTP + API JSON (C) | `app/server/src/*.c` |
| SQLite 3.38 (amalgamation) | compilé statiquement dans l'exe |
| interface HTML/CSS/JS + logo | embarquée en ressources binaires (`tools/gen_assets.py`) |
| icône + version | `app/packaging/app.rc` (compilé avec `zig rc`) |

Le serveur n'écoute que sur `127.0.0.1` (option `--bind-all` pour un usage réseau
en développement). La fenêtre de l'application est ouverte via Edge/Chrome en mode
`--app=`, avec repli sur le navigateur par défaut.

### Signature (recommandé pour une distribution officielle)

```powershell
# certificat de signature de code (OV/EV) acheté chez un autorité de certification
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
          /f certificat.pfx /p MOTDEPASSE Les-Theoremes.exe
```

Sans signature, SmartScreen affiche un avertissement au premier lancement
(« Informations complémentaires → Exécuter quand même »).

### Créer un programme d'installation (facultatif)

Inno Setup (gratuit, Windows) :

```ini
[Setup]
AppName=Les Théorèmes
AppVersion=1.0.0
DefaultDirName={localappdata}\LesTheoremes
DefaultGroupName=Les Théorèmes
OutputBaseFilename=Install-Les-Theoremes
SetupIconFile=app\packaging\app.ico

[Files]
Source: "app\dist\Les-Theoremes.exe"; DestDir: "{app}"

[Icons]
Name: "{group}\Les Théorèmes"; Filename: "{app}\Les-Theoremes.exe"
Name: "{userdesktop}\Les Théorèmes"; Filename: "{app}\Les-Theoremes.exe"

[Run]
Filename: "{app}\Les-Theoremes.exe"; Flags: nowait postinstall
```

> Installer dans `{localappdata}` (et non `Program Files`) pour que la base
> `school.db` soit accessible en écriture.

## Android (.apk) — étape suivante

L'API et l'interface sont déjà séparées : le `.apk` n'est qu'un nouvel emballage.

Deux chemins possibles :

1. **Application native légère (recommandé)**
   - l'interface `app/web` est copiée dans `android/app/src/main/assets/` et chargée
     dans une `WebView` ;
   - `index.html` appelle `/api/...` → remplacer `fetch("/api" + path)` par
     `fetch(BASE + path)` avec `BASE = "http://<ip-du-serveur-de-l-ecole>:8765"` ;
   - le serveur C (fichiers `server/`) se recompile pour Linux/ARM
     (`cc -O2 *.c vendor/sqlite3.c -o theorèmes`) ; il tourne sur le PC de l'école ;
   - `AndroidManifest.xml` : `INTERNET` + `usesCleartextTraffic=true` (HTTP local) ;
   - signature par `apksigner`, publication sur le Play Store ou installation directe.

2. **Coque Capacitor / Cordova** : `npx cap add android` en pointant sur `app/web`,
   puis `./gradlew assembleRelease` → `app-release.apk`. Plus rapide à mettre en
   place, même adaptation d'URL que ci-dessus.

Points d'attention pour la version mobile :

- autoriser le trafic HTTP en clair uniquement vers l'adresse du serveur de l'école ;
- prévoir la reconnexion si le Wi-Fi de l'école change d'adresse IP
  (ajouter un champ « adresse du serveur » dans l'écran de connexion) ;
- conserver le jeton de session dans `localStorage` (déjà le cas) et re-demander
  le mot de passe après 12 h d'inactivité (déjà le cas, côté serveur).

## Distribution des mises à jour

Le serveur et l'interface sont dans un seul fichier : remplacer
`Les-Theoremes.exe` suffit. Les données restent dans `school.db` à côté de
l'exécutable et ne sont pas touchées.
