# Préparation HestiaSDK 1.4.0 — analyse des dépendances

Analyse initiale du 6 septembre 2026, mise à jour le 14 septembre 2026.
Les sections d'analyse conservent les constats d'origine ; l'état et l'ordre
de travail à la fin du document reflètent désormais le code en cours.

## Bases examinées

- HestiaSDK : HEAD au tag `v1.3.0`. `library.properties` indiquait encore
  `1.1.7` ; correction à `1.3.0` dans cette étape préparatoire.
- Piscine : `../JiH/JiH_Piscin/JiH_Piscin`, HEAD `49a35ed`, dépendance
  explicite `HestiaSDK.git#v1.3.0`.
- Modèle suivant : `../JiH/HestiaIotVirgo/Virgo`. Son `platformio.ini`
  référence `HestiaSDK.git#1.2.4` et `src/main.h` annonce `sdk=1.2.4`.
  Ce répertoire n'est pas reconnu comme dépôt Git par la commande exécutée.

## Dépendances et frontière SDK/application

| Élément | Dépendances actuelles observées | Extraction pour 1.4.0 |
| --- | --- | --- |
| Horloge locale | Piscine `datetime_utils.*` : Arduino String, millis ; alimentation HA depuis main | Horloge autonome, validité et synchronisation explicites ; aucun accès à main.h ni nom de bridge imposé |
| Démarrage/reconnexion | Piscine main : `s_autonomousRuntimeReady`, restauration des automates dans `newSeqComm()` | Initialisation locale avant toute session réseau ; session HA distincte de la disponibilité locale |
| Resynchronisation HA | Core pour les contrôles ; main piscine et acquisitions pour les indicateurs | Préserver la répartition des responsabilités, sans restauration NVS ni commande matériel à la reconnexion |
| Journal | `event_journal.*` : horloge, main.h, sources et formatage piscine | Buffer borné, codes/sources applicatifs, horodatage qualifié ; rendu métier extérieur |
| Diagnostics RAM/NVS | Bloc main piscine : ESP, `nvs_get_stats`, Core::logBook | Capture de statistiques indépendante du réseau ; présentation/publication facultatives |
| Supervision | `EquipmentStateMonitor.*` : GPIO, `ReadADS1115`, journal, Core, noms Pompe/Hayward/Thermo | Comparaison commande/retour avec délai et validité ; acquisition injectée, événements sans noms métier |

Les dépendances communes du SDK sont Arduino ESP32, ArduinoJson 6,
MQTT 256dpi et Hestia-tempo. La piscine ajoute Hestia-compare 1.2.2,
ADS1X15, dhtESP32-rmt et OneWireNg selon l'environnement. Ces dépendances
d'acquisition ne doivent pas devenir obligatoires pour le SDK.

Restent dans la piscine : PoolControl, Pump/Hayward/Thermo/MeasurementAuto,
calibrations pH/ORP/chlore, analyses et résumés métier, fenêtres Eco/Boost,
priorités des demandes, seuils, brochages, polarités et pilotes de sondes.
Les intentions Hayward/Thermo/Measure et les exceptions basées sur le nom
de l'équipement ne font pas partie du superviseur générique.

## Constats déterminants

### Démarrage local

`HestiaCore::initCore()` restaure déjà les bridges depuis la NVS avant les
connexions. Cependant, la piscine ne positionne `s_autonomousRuntimeReady`
qu'à sa première séquence HA ; ses ticks métier restent donc inactifs lors
d'un démarrage sans réseau. Sa solution actuelle couvre la coupure après
connexion, pas le démarrage autonome.

Contrat cible : sorties sûres d'abord, configuration et valeurs locales,
initialisation des automates une seule fois, puis ticks locaux continus.
Les événements réseau déclenchent uniquement la synchronisation distante.
Le résultat de `initCore()` doit être vérifié dans les exemples et Virgo.

Configuration invalide ou provisioning forcé : `initCore()` appelle
`StartProvisioning()`, dont la boucle attend une sauvegarde. Pour permettre
un service local même sans configuration réseau, il faut séparer validation
locale et réseau et rendre le portail coopératif. Une configuration locale
invalide doit conduire au mode local sûr de l'application ; le SDK ne peut
pas inventer des paramètres d'actionnement. Une simple absence de Wi-Fi,
broker ou HA ne doit jamais bloquer le lancement local.

`tryWiFiConnectNonBlocking()` contient néanmoins `WiFi.scanNetworks()`
synchrone et `delay(50)`. La connexion MQTT utilise également un appel de
connexion dont le temps d'attente doit être mesuré/borné. Le nom du garde
ne garantit donc pas la latence des ticks locaux. Traiter ces chemins avec
la séparation du démarrage, avant de promettre une supervision continue.

### Horloge

L'horloge piscine reçoit une heure civile locale sans offset, puis l'avance
avec millis. Ce n'est pas un timestamp UTC. Sans synchronisation de ce
démarrage, elle doit rester invalide : restaurer une ancienne date NVS ne
permet pas de connaître la durée de coupure électrique.

Le runtime doit donc fonctionner avec une heure inconnue ; seules les
fonctions nécessitant une heure civile attendent une source valide.
Temporisations et supervision utilisent une durée monotone. Le SDK reçoit
une synchronisation explicite depuis HA, RTC ou autre adaptateur ; il ne
doit pas lire un bridge arbitraire comme preuve de fraîcheur.

À corriger lors de l'extraction : parsing strict (chiffres et longueur),
qualification heure civile/UTC, gestion des corrections d'heure et durée
hors ligne supérieure à un tour de millis (~49,7 jours). La soustraction
actuelle depuis un point de synchronisation fixe perd un tour complet.
Un timestamp retained reçu à la reconnexion peut lui-même être ancien :
la réception seule ne garantit pas la fraîcheur de l'heure.

### Bridges et sessions HA

`HAIoTBridge::write()` conserve déjà `_value` même hors réseau.
`publishValueToHA()` ne publie actuellement que les `HA_CONTROL`. Ce constat
porte sur cette méthode, pas sur la resynchronisation complète : la piscine
publie aussi des indicateurs via `write()` et les acquisitions `ACQ_TXHA`.
Il ne justifie pas une republication automatique de tous les indicateurs
par le SDK. Conserver leur actualisation sous responsabilité applicative,
selon la validité de l'état ou de la mesure. Ne pas rejouer les boutons.
Ne pas appeler `initAll()` ou
`InitValueNVS()` lors d'une reconnexion : cela écraserait l'état RAM.

Conserver la compatibilité des appels `newSeqComm()`, `HAInit()` et
`setHAInitDone()`, tout en rendant explicites disponibilité locale et
session de communication. Le signal de nouvelle session doit rester en
attente jusqu'à sa consommation : actuellement CoreComm peut faire passer
HA_NEWSEQCOM à HA_INIT_WAIT sans que l'application consomme le signal.

Auditer également la réinitialisation du flush après coupure et la valeur
HA_online conservée en RAM : elle ne prouve pas que HA est en ligne dans
la nouvelle session. `commOK()` repose actuellement sur l'ordre des états,
pas directement sur la connexion transport réelle. La publication et les
logs distants doivent vérifier cette disponibilité.

Les messages de commande sont ignorés pendant le flush, sauf HA_ENTITIES.
Une fenêtre temporelle ne garantit pas l'élimination de tous les retained
tardifs : vérifier le comportement MQTT avant d'en faire une garantie.

### Journal et diagnostics

Le journal piscine alloue 256 événements avec 256 octets de texte chacun,
soit environ 69 Kio pour le tableau seul (276 octets par événement sur ABI
ESP32 attendu). Préférer capacité et taille de texte configurables, sans
coût global imposé aux applications qui n'utilisent pas le module.

Séparer séquence, durée monotone et heure civile optionnelle ; le timestamp
actuel mélange secondes depuis le boot et heure civile sans indicateur.
Définir écrasement, retard des lecteurs et débordement des séquences.
Documenter un usage dans la boucle unique ou ajouter la synchronisation
nécessaire : les écritures actuelles ne sont pas atomiques entre tâches.
Conserver les rendus Pump et les analyzers dans l'application.

Les diagnostics doivent retourner un instantané avec le code d'erreur NVS,
sans écrire ni effacer la NVS. Ils doivent être consultables avant toute
connexion et pouvoir être publiés ensuite. Le journal ne doit pas publier
via Core et créer une boucle journal → publication → journal.

### Supervision d'équipement

Extraire le calcul générique commande/retour réel et la temporisation de
défaut. L'application fournit la mesure ou un adaptateur d'acquisition et
garde la décision d'actionner les relais. Éviter la dépendance aux deux ADS,
aux canaux globaux 0..7 et aux intentions piscine.

Prévoir mesure invalide/périmée distincte de « arrêté », seuil et délai
validés, changement de commande pendant la temporisation, débordement du
calcul secondes × 1000 et de millis. Les événements doivent être exposés
sans imposer log HA ou arrêt matériel. Les politiques d'arrêt restent
applicatives.

## État de l'implémentation

| Élément | État au 14 septembre | Remarque |
| --- | --- | --- |
| Version de bibliothèque | Fait pour l'état 1.3.0 | `library.properties` est passé de 1.1.7 à 1.3.0. Le passage à 1.4.0 attend la fin des travaux. |
| Modèle Virgo | Fait, à valider avec le SDK final | Nettoyé de la logique piscine ; identité Virgo, Discovery mapping, documentation des zones de sécurité. |
| Horloge civile | Fait | Service autonome sans réseau, timer 64 bits, API indépendante de HA et tests hôte. Virgo reste l'adaptateur de `HA_DateTime`. |
| Logs de bridges | Fait | Silencieux par défaut ; activation explicite par bridge. |
| Validation locale/réseau | Hors périmètre générique | La sécurité et les paramètres métier restent dans chaque IoT. L'absence de réseau ne doit pas ouvrir automatiquement le provisioning. |
| Portail de provisioning | Conservé en mode maintenance | Déclenché volontairement par bouton puis bloquant jusqu'à sauvegarde/redémarrage ; aucun portail coopératif n'est prévu pour 1.4.0. |
| Sessions HA | Implémenté, à compiler et tester | Époques de session, preuves HA/heartbeat remises à zéro, annulation lors d'une coupure et signal `newSeqComm()` conservé jusqu'à consommation. |
| Commandes retained tardives | Différé après 1.4.0 | Piste de robustesse générale, sans besoin identifié dans JiH_Piscin. Le flush existant est conservé. |
| Latence réseau | Différé après 1.4.0 | Optimisation générale, sans besoin identifié dans JiH_Piscin. Aucun changement prévu dans cette version. |
| Diagnostics, journal, supervision | Implémenté, à valider | Services sans réseau ni dépendance métier : instantané RAM/NVS, journal applicatif borné et supervision commande/retour. |
| Essais matériel | DevKit validé partiellement | Le 15 septembre : coupure Wi-Fi, horloge et bouton locaux actifs, puis session HA rétablie. Reste à valider la commande `Led` depuis HA et les autres cibles. |

## Ordre de travail révisé

1. **Valider les sessions HA.** Compiler les changements, puis provoquer une
   coupure Wi-Fi/MQTT pendant le flush et HAInit, un retour de HA sans coupure
   MQTT, et vérifier qu'une synchronisation est consommée une fois par session.
2. **Valider les services génériques.** Vérifier les diagnostics RAM/NVS, le
   débordement du journal et les états du superviseur. Les pilotes ADS, GPIO,
   noms, seuils et politiques restent dans l'application piscine.
3. **Valider l'intégration.** Exécuter la matrice du contrat sur simulateur,
   compilation DevKit/C3/C6 ou S3, puis matériel. Migrer ensuite le modèle
   externe `../JiH/HestiaIotVirgo/Virgo` vers le tag final et corriger sa
   dépendance SDK.
4. **Publier 1.4.0.** Mettre les métadonnées à 1.4.0, rédiger les notes de
   version, créer le tag, puis mettre Virgo à jour vers ce tag. Rien de cela
   n'est fait par cette analyse.

Scénarios d'acceptation : démarrage sans AP, AP présent sans broker, broker
sans HA, configuration réseau absente, coupure/reconnexion en pleine
temporisation, redémarrage HA sans rupture MQTT, retained tardif, maintien
des indicateurs modifiés hors ligne, absence de double restauration,
heure inconnue puis valide, correction d'heure et longue coupure,
saturation du journal, erreur NVS et retour de mesure invalide.

Vérifier sur matériel que les sorties sûres précèdent toute attente, que
la cadence locale reste acceptable pendant les échecs réseau et qu'aucune
reconnexion ne redémarre les automates. La cadence admissible doit être
fixée par l'application d'équipement.

## Prochaine étape

La prochaine étape est la gestion des sessions HA : le signal de nouvelle
session doit être consommé par l'application avant de passer à l'attente de
fin d'initialisation. Le portail reste un outil manuel de maintenance ; il ne
fait pas partie de la boucle locale normale. Chaque IoT documente ses sorties
sûres dans Virgo et les place avant `initCore()`.
