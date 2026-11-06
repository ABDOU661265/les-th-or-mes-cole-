# Les Théorèmes — Espace scolaire

Application de bureau du **Groupe Scolaire Les Théorèmes** (Lotissement Est Villa N°54,
Génie Zéralda — Alger) : emploi du temps, devoirs, notes, absences, frais, annonces,
carnet de correspondance, cantine et transport — **en français, arabe et anglais**.

Interface mobile (thème **orange** du logo de l'école), serveur et base de données
**inclus dans le programme** : aucune installation, aucun compte à créer en ligne,
les données restent sur l'ordinateur de l'école.

```
┌──────────────────────────────────────────────┐
│  Les-Theoremes.exe   (Windows 64 bits)       │
│  ─ interface (HTML/CSS/JS)                   │
│  ─ serveur local 127.0.0.1:8765              │
│  ─ base SQLite  school.db                    │
└──────────────────────────────────────────────┘
```

## 1. Lancer l'application (Windows)

1. Copier **`Les-Theoremes.exe`** dans un dossier où l'école peut écrire
   (par exemple `C:\Les Theoremes\`). Éviter `C:\Program Files` : l'application
   basculerait alors vers le profil utilisateur pour sa base.
2. Double-cliquer sur **`Les-Theoremes.exe`**.
   - Une icône apparaît dans la zone de notification (à côté de l'heure) :
     double-clic = ouvrir l'application, clic droit = menu (**Ouvrir**,
     **Ouvrir le dossier des données**, **A propos**, **Quitter**).
   - La fenêtre de l'application s'ouvre (Edge ou Chrome en mode application).
     Si aucun des deux n'est présent, le navigateur par défaut est utilisé.
3. Se connecter avec le compte remis par la direction.

Windows 10/11 : au premier démarrage, Windows Defender peut demander une
confirmation (« Windows a protégé votre PC ») → **Informations complémentaires →
Exécuter quand même**. L'exécutable n'est pas signé numériquement (voir §5).

### Comptes créés au premier lancement

| Rôle | E-mail | Mot de passe | Droits |
|------|--------|--------------|--------|
| Direction (admin) | `admin@lestheoremes.dz` | `Admin@2026` | tout, y compris créer des modérateurs, supprimer des comptes, journal d'activité |
| Modérateur | `moderateur@lestheoremes.dz` | `Modo@2026` (à changer) | annonces, devoirs, absences, frais, création de comptes **parents** |
| Parent (démo) | `parent@lestheoremes.dz` | `Parent@2026` | ses enfants uniquement |

> **À faire avant utilisation réelle** : se connecter en admin → *Compte* →
> **Changer le mot de passe**, puis désactiver ou supprimer les comptes de démonstration.

### Comment se crée un compte

Il n'y a **pas** d'inscription libre (c'est voulu) :

1. la direction (ou un modérateur) ouvre **Comptes** → *Créer un compte*,
2. choisit le rôle (**Parent** ou **Modérateur**), saisit nom, e-mail et un mot de
   passe provisoire (8 caractères minimum, avec majuscule + minuscule + chiffre),
3. dans le même formulaire, **« Enfant rattaché »** est obligatoire (prénom, nom,
   classe) : le compte et l'élève sont créés ensemble, en une seule transaction.
   La tuile **Élèves** sert ensuite à ajouter un frère ou une sœur ;
4. elle remet l'e-mail et le mot de passe au parent ; celui-ci le change à la
   première connexion (*Compte* → *Changer le mot de passe*).

> Particularité demandée : **le parent ne peut pas changer son mot de passe**.
> S'il l'oublie, la direction lui en redonne un (*Comptes* → *Nouveau mot de passe*).

### Logo et thème

Le logo fourni (`Logo 2.png`, orange) est utilisé dans l'écran de connexion, la
barre supérieure et l'icône du fichier .exe. L'ancien bleu de la maquette a été
remplacé par l'orange de l'école (`#ef6c00`).

## 2. Accès depuis un autre appareil (téléphone, PC d'un parent)

Par défaut, l'application **écoute sur tout le réseau local de l'école** et affiche
son adresse dans la console (`--console`) :

```
reseau local (a donner aux parents) : http://192.168.1.24:8765/
```

C'est **cette adresse** que l'on donne aux parents — pas `127.0.0.1`, qui ne
fonctionne que sur l'ordinateur de l'école. Elle s'ouvre aussi dans le navigateur
d'un téléphone connecté au même Wi-Fi.

| Situation | Ce qu'il faut faire |
|---|---|
| même Wi-Fi que le PC de l'école | ouvrir `http://<adresse affichée>:8765/` |
| l'application est sur **son** PC | ouvrir le bloc **Serveur de l'école** sur l'écran de connexion, saisir l'adresse, **Tester** (elle est mémorisée) |
| aucune connexion | vérifier le Wi-Fi, puis refaire **Tester** |

Sécurité : le serveur répond au réseau local mais **chaque requête exige un jeton
de session** ; les mots de passe restent hachés et le blocage après 5 erreurs
s'applique. Pour revenir à un usage strictement local : lancer avec `--local`.

## 3. Les écrans

**Parents** : frais de scolarité, carnet de correspondance, emploi du temps,
devoirs, absences & retards, notes & résultats, menu cantine, transport scolaire,
annonces, galerie photos, contact, compte.

**Direction / modérateurs** : annonces (création/modification/suppression en 3
langues), comptes, élèves, tableau de bord, devoirs, emploi du temps, absences,
frais (marquer payé), photos, contact, journal d'activité.

Les 3 langues se changent à tout moment (bouton **FR / ع / EN** dans la barre du
haut). L'arabe est affiché de droite à gauche. Les contenus saisis par l'école
(titres d'annonces, devoirs, plats de la cantine…) ont trois champs : FR, AR, EN.

## 4. Sécurité

- mots de passe **jamais stockés en clair** : PBKDF2-HMAC-SHA256, 100 000 itérations,
  sel aléatoire de 16 octets par compte ;
- un compte parent sans enfant est refusé (`child_required`) ;
- mot de passe refusé s'il fait moins de 8 caractères ou s'il n'a pas majuscule +
  minuscule + chiffre ; changement obligatoire à la première connexion des comptes
  créés par l'administration ;
- jetons de session de 256 bits, expiration après 12 h d'inactivité, invalidation
  des autres sessions lors d'un changement de mot de passe ;
- 5 tentatives ratées → blocage progressif (30 s, 60 s… jusqu'à 15 min) ;
- requêtes SQL **toujours paramétrées** (pas d'injection SQL) ; échappement HTML
  de tout contenu affiché (pas d'injection de script) ;
- un parent ne voit **que** ses propres enfants (vérifié côté serveur, pas seulement
  dans l'interface) ; un modérateur ne peut ni créer/modifier un modérateur, ni
  supprimer un compte ;
- serveur lié à `127.0.0.1` : **aucun autre poste du réseau ne peut s'y connecter** ;
- en-têtes HTTP de durcissement + refus des chemins `../` ;
- **journal d'activité** : connexions réussies/échouées, créations de comptes,
  modifications, suppressions (visible par l'admin).

## 5. Compiler soi-même

```bash
# Linux / macOS (développement)
cd app && ./tools/build.sh

# Windows 64 bits (compilation croisée avec zig, aucun Windows requis)
cd app && ./tools/build.sh windows
#   -> app/release/Les-Theoremes.exe

# archive à distribuer
cd app && ./tools/build.sh installer
#   -> app/release/Les-Theoremes-Windows-1.0.0.zip
```

Le dossier `app/release/` contient le livrable : l'exécutable se reconstruit à partir des
sources (`app/server/src`, `app/web`) en une commande.

Le programme assemble : un serveur HTTP/SQLite en C (~5 000 lignes),
l'interface (HTML/CSS/JS sans dépendance, ~1 500 lignes) **embarquée dans
l'exécutable**, et SQLite + cJSON (domaine public / MIT) compilés statiquement.

## 6. Signature et avertissement SmartScreen

L'exécutable n'est pas signé. Windows SmartScreen peut afficher un avertissement
au premier lancement ; c'est normal pour un programme non signé. Pour le faire
disparaître lors d'un déploiement officiel, il faut un certificat de signature de
code (voir `PACKAGING.md`).

## 7. Version .apk (Android) — étape suivante

Voir `PACKAGING.md` (§APK) : la même interface et la même API sont réutilisées ;
seul l'emballage change.

## 8. Structure

```
app/
  server/src/      serveur C : main, http, api, db (SQLite), auth, util
  server/vendor/   sqlite3 (amalgamation) + cJSON
  web/             interface : index.html, styles.css, app.js, i18n.js, logo.png
  tools/           build.sh, gen_assets.py (embarque le web dans l'exe)
  packaging/       icône + version Windows (.rc)
app/release/       exécutables produits (.exe Windows + .zip)
```
