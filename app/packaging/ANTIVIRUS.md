# Faux positifs antivirus (Windows Defender, AVG, Avast…)

## En résumé

Notre `.exe` ne contient **aucun code viral**. Il est simplement **jeune et inconnu** :
Windows Defender et AVG notent chaque programme selon sa réputation. Un exécutable
récent, téléchargé depuis GitHub, jamais vu par leurs utilisateurs — donc réputé 0 —
est signalé « Trojan / Wacatac / Filerepmalware » **par précaution**, pas par preuve.

Vérifications faites sur le binaire livré :

| Comportement typiquement viral | Présent ? |
|---|---|
| injection dans un autre processus (`VirtualAllocEx`, `CreateRemoteThread`) | ❌ non |
| démarrage automatique / base de registre (`Run`, `CreateService`) | ❌ non |
| capture clavier ou écran (`SetWindowsHookEx`, `GetAsyncKeyState`) | ❌ non |
| téléchargement d'un fichier à distance (`URLDownloadToFile`) | ❌ non |
| obfuscation / compression type UPX | ❌ non |
| exécution d'une commande système (`cmd.exe`, `powershell`) | ❌ non |
| section mémoire à la fois inscriptible **et** exécutable (RWX) | ❌ non |
| ASLR + DEP + haute entropie | ✅ activés |
| manifeste « asInvoker » (aucun droit administrateur) | ✅ présent |
| signature numérique | ❌ **absente → cause principale** |

## 1. Le vrai correctif : signer l'exécutable (ou le soumettre à Microsoft)

### A. Immédiat et gratuit : déclarer le fichier comme sûr chez Microsoft

1. Aller sur <https://www.microsoft.com/en-us/wdsi/filesubmission>
2. Choisir **« Developer »**, connexion avec un compte Microsoft.
3. Envoyer `Les-Theoremes.exe` en indiquant :
   - nom du produit : **Les Théorèmes — Espace scolaire**
   - éditeur : **Groupe Scolaire Les Théorèmes, Zéralda (Alger)**
   - description : « application scolaire hors-ligne, serveur local, aucune
     connexion sortante, distribuée aux parents par l'école »
   - joindre le lien de téléchargement
4. Compter 24 à 72 h. Une fois classé *clean*, Defender cesse de bloquer, et la
   réputation du fichier remonte pour tous les utilisateurs.

Faire de même chez les autres éditeurs :

| Éditeur | Adresse de soumission |
|---|---|
| AVG / Avast | <https://www.avg.com/false-positive-file-form> |
| Avast | <https://www.avast.com/false-positive-file-form.php> |
| Kaspersky | <https://opentip.kaspersky.com/> |
| ESET | <https://www.eset.com/int/support/sample-submission/> |
| Bitdefender | <https://www.bitdefender.com/consumer/support/answer/29358/> |

> ⚠️ À refaire **à chaque nouvelle version** : le fichier change, donc son empreinte
> (SHA-256) change. Notez l'empreinte avec
> `certutil -hashfile Les-Theoremes.exe SHA256` et joignez-la à la demande.

### B. Définitif : un certificat de signature de code

Un exécutable signé par un certificat reconnu n'est plus jamais « inconnu ».

| Type | Prix indicatif | Réputation | Remarque |
|---|---|---|---|
| OV (organisation validée) | 200–400 €/an | se construit en quelques semaines | suffisant pour une école |
| EV (validation étendue) | 400–700 €/an | **immédiate** (SmartScreen ne bloque plus) | livré sur clé USB ou HSM ; recommandé si beaucoup de parents installent |

Signature (après achat du certificat) :

```powershell
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
          /f certificat.pfx /p MOTDEPASSE Les-Theoremes.exe
signtool verify /pa /v Les-Theoremes.exe
```

Avec un certificat EV, l'avertissement SmartScreen et la plupart des alertes
antivirus disparaissent dès le premier téléchargement.

## 2. Ce qui a déjà été fait dans le build

- **manifeste Windows** (`app/packaging/app.manifest`) : application normale,
  `asInvoker`, déclarée compatible Windows 7 → 11, mise à l'échelle DPI.
  Un exe sans manifeste est beaucoup plus souvent signalé.
- **ASLR + DEP + haute entropie** activés à l'édition de liens.
- **aucune section RWX**, aucune API d'injection ou de persistance.
- **journal de démarrage** (`journal.txt` dans le dossier de données) : un
  programme qui laisse une trace lisible est moins suspect.
- **réponses HTTP complètes** (en-tête `Date`, `Server`) : le serveur local ne
  ressemble plus à un programme qui « parle » bizarrement.
- **aucune connexion sortante** vers Internet : tout reste sur le réseau local,
  ce qui est l'argument principal à donner à un antivirus.

## 3. Ce qu'il faut dire à l'école et aux parents

> Le programme est nouveau et n'a pas encore de réputation chez Microsoft.
> Il ne se connecte pas à Internet, il ne s'installe pas dans le système
> (aucune modification du registre ni démarrage automatique) : il s'agit d'un
> serveur local qui affiche l'espace scolaire dans une fenêtre.
> Nous avons déclaré le fichier comme sûr auprès de Microsoft et d'AVG.

## 4. En attendant, comment installer sans se battre

1. **Windows Defender** : quand l'alerte apparaît → *Plus d'informations* →
   **Exécuter quand même**. (Le message n'apparaît pas toujours ; si le fichier
   est supprimé, restaurer depuis *Protection contre les virus → Historique*.)
2. **Ajouter une exclusion** (à faire par la direction seulement) :
   *Sécurité Windows → Virus et menace → Gérer les paramètres → Exclusions →
   Ajouter une exclusion → Dossier* → le dossier de l'application
   (ex. `C:\LesTheoremes`).
3. **AVG / Avast** : *Menu → Paramètres → Général → Exceptions → Ajouter une
   exception* → choisir le `.exe`. (Ils proposent aussi « Restaurer et ajouter
   à la liste d'exceptions » dans la quarantaine.)
4. **Éviter le navigateur** pour la copie : un fichier arrivé par WhatsApp/Telegram
   ou marqué « provenant d'Internet » subit un contrôle supplémentaire. Copier
   le `.exe` par **clé USB** depuis le PC de l'école, ou installer l'application
   sur **un seul** PC (celui de l'école) et donner aux parents uniquement
   l'adresse réseau (`http://192.168.x.x:8765/`) — dans ce cas **aucun
   téléchargement d'exécutable** n'est nécessaire pour eux.

## 5. Distribution : la meilleure façon d'éviter le problème

| Public | Ce qu'il reçoit | Antivirus ? |
|---|---|---|
| Direction / modération (1 ou 2 PC) | le `.exe` | oui, à traiter une fois (signature ou exclusion) |
| Parents | **un simple lien** `http://192.168.x.x:8765/` | ❌ aucun téléchargement |

C'est le mode recommandé : un seul poste exécute le programme, les familles
utilisent leur navigateur, il n'y a donc plus de faux positif à gérer au
quotidien.

## 6. Vérifier l'intégrité du fichier distribué

Après chaque compilation :

```powershell
certutil -hashfile Les-Theoremes.exe SHA256
```

Communiquez cette empreinte sur l'affiche ou le papier remis aux parents :
si le fichier est modifié par un tiers, l'empreinte change.
