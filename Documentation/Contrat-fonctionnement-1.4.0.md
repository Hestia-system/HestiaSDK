# Contrat de fonctionnement HestiaSDK 1.4.0

Statut : spécification cible pour l'implémentation et les essais. Ce document
ne signifie pas que le SDK actuel satisfait déjà toutes ces exigences.
Il précise le cycle de vie identifié dans `Preparation-1.4.0.md`.

## 1. Principe

L'application démarre et fonctionne localement sans attendre Wi-Fi, MQTT,
Home Assistant ou une heure civile connue. Le réseau fournit des commandes,
des références horaires et une présentation distante ; sa disponibilité ne
détermine pas la disponibilité locale.

Le SDK distingue trois informations indépendantes :

| Information | États conceptuels | Signification |
| --- | --- | --- |
| Application locale | Initialisation, fonctionnement, mode sûr | Configuration et ressources locales utilisables |
| Communication | Hors ligne, connexion, synchronisation HA, prête | Progression de la session distante |
| Horloge | Inconnue, synchronisée puis autonome | Disponibilité d'une référence civile pour les horaires |

Ces noms décrivent le contrat ; ils ne figent pas encore les noms des API.
Une panne réseau ne fait pas repasser l'application en initialisation.
Une heure inconnue ne bloque pas les fonctions qui n'en ont pas besoin.

## 2. Démarrage et redémarrage électrique

Ordre obligatoire :

1. L'application place ses sorties dans leurs états sûrs, avant toute
   attente, opération réseau ou portail de configuration.
2. Le SDK charge les paramètres, applique si nécessaire la politique de
   changement de firmware, crée les bridges et restaure les contrôles NVS.
3. L'application valide ses ressources et initialise ses fonctions locales
   une seule fois à partir des valeurs disponibles.
4. La boucle locale commence. Les tentatives réseau et le portail éventuel
   sont ensuite servis de manière coopérative avec cette boucle.

Restaurer une valeur de contrôle n'implique pas d'actionner un équipement.
L'application décide quelles demandes persistantes peuvent être reprises et
dans quelles conditions. Les indicateurs sont produits par l'application ;
les boutons ne sont jamais des actions à rejouer au démarrage.

Un redémarrage détruit les états RAM et les temporisations en cours. Une
reprise de cycle après redémarrage exige une politique persistante explicite
de l'application ; elle ne peut pas être déduite d'une ancienne mesure.

L'horloge est inconnue après démarrage électrique ou sortie de deep sleep.
Une ancienne date NVS ne permet pas de connaître la durée de l'arrêt.

## 3. Configuration absente ou invalide

La validation distingue les paramètres nécessaires au fonctionnement local
de ceux nécessaires à la communication.

- Configuration réseau absente/invalide : fonctionnement local disponible,
  portail accessible et tentatives limitées aux services configurés.
- Configuration locale absente/invalide : mode sûr défini par l'application,
  avec diagnostics et moyens de configuration disponibles.
- Configuration valide mais réseau inaccessible : fonctionnement local
  normal ; aucune réinitialisation ou remise aux valeurs par défaut.
- Erreur d'initialisation : ne pas utiliser les objets non initialisés ;
  conserver les sorties sûres et exposer la cause localement.

Le provisioning est un outil de maintenance, déclenché volontairement par le
bouton puis suivi d'un redémarrage. Le portail peut rester bloquant pendant
cette maintenance : l'application place alors ses sorties sûres avant
`initCore()`. Il ne fait pas partie du fonctionnement hors réseau normal et
ne doit pas être ouvert automatiquement parce que Wi-Fi, MQTT ou HA sont
indisponibles.

## 4. Première connexion et reconnexions

Une session de synchronisation HA est requise à la première disponibilité
de HA, après rétablissement du transport, ou après retour de HA lui-même
(y compris sans rupture de Wi-Fi/MQTT).

Pour chaque session :

1. Confirmer la disponibilité du transport et de HA avec des observations
   de cette session ; une ancienne valeur HA_online en RAM ne suffit pas.
2. Établir les abonnements et la découverte nécessaires.
3. Traiter la phase de réception initiale sans exécuter d'anciennes commandes.
4. Signaler à l'application qu'une synchronisation est à effectuer.
5. Republier les contrôles courants via le mécanisme SDK existant.
   L'application actualise et publie ses indicateurs selon leur validité.
6. Terminer explicitement la synchronisation avant d'annoncer HA prêt.

Le signal de session reste disponible jusqu'à consommation par l'application.
Une session interrompue ne peut pas être déclarée terminée par une confirmation
devenue obsolète. La prochaine tentative recommence la synchronisation, sans
recommencer l'initialisation locale.

Les appels existants `newSeqComm()`, `HAInit()` et `setHAInitDone()` doivent
rester utilisables. Leur implémentation doit respecter ces garanties.

Une reconnexion ne doit jamais :

- rappeler l'initialisation matérielle ou restaurer les bridges depuis la NVS ;
- annuler ou relancer une temporisation, un mode ou une demande en RAM ;
- forcer une sortie à sa valeur de démarrage ;
- déclencher automatiquement un bouton ou republier tous les indicateurs
  indépendamment de leur signification.

Les changements de commande reçus après synchronisation restent des actions
applicatives normales, soumises aux mêmes validations que les autres demandes.

## 5. Perte de communication et fonctionnement hors réseau

La perte de Wi-Fi, de MQTT ou de HA rend la communication indisponible, sans
changer la disponibilité locale. Les états, demandes et temporisations RAM
continuent selon la logique de l'application.

Les écritures locales des bridges continuent de mettre à jour leurs valeurs.
Les HA_CONTROL conservent leur politique de persistance existante. L'absence
de réseau ne provoque ni restauration NVS ni abandon d'une mesure locale.

Le SDK ne promet pas la livraison des publications faites hors ligne. À la
reconnexion, on transmet les états courants utiles, sans rejouer tout
l'historique comme des commandes. Le journal local sert à conserver les
événements dans la limite de sa capacité.

Une application peut explicitement exiger une commande distante récente
pour un équipement donné. Cette règle et sa réaction à expiration restent
applicatives ; le SDK n'impose pas un arrêt général sur perte de HA.

## 6. Heure civile et temporisations

L'application transmet au service d'horloge une référence fraîche et valide.
L'origine peut être HA ou un autre adaptateur. Le SDK ne dépend d'aucun nom
de bridge et ne déduit pas la fraîcheur du seul fait qu'un message est reçu.
Un message MQTT retained peut contenir une ancienne date.

Après synchronisation, l'horloge avance hors réseau avec le timer 64 bits.
Sa validité signifie qu'une référence a été acceptée ; elle ne garantit pas
une précision illimitée ni une connaissance des changements d'heure futurs.

- Heure inconnue : seules les décisions fondées sur un horaire sont en attente.
- Nouvelle heure valide : mise à jour de la référence civile.
- Correction en avant ou en arrière : pas de modification des délais monotones.
- Événement planifié sauté ou heure répétée : politique de rattrapage et de
  non-répétition décidée par l'application, pas par le service d'horloge.

Le format civil et l'absence de conversion UTC/DST automatique sont ceux
documentés dans `datetime_utils.h`.

## 7. Anciennes commandes et autorité des états

Au démarrage, les valeurs NVS/default constituent la base locale ; pendant
un fonctionnement continu, l'état RAM courant fait référence.

Une ancienne commande retenue ne doit pas écraser cet état lors d'une
reconnexion. Les boutons de commande ne doivent pas être publiés en retained
par leurs émetteurs. Pour les contrôles, la reprise doit distinguer les valeurs
de synchronisation des nouvelles commandes acceptables.

La fenêtre de flush actuelle est conservée. L'étude d'un filtrage des messages
retained tardifs est différée après 1.4.0 : aucun besoin concret n'a été relevé
dans JiH_Piscin et le client MQTT actuel ne fournit pas cette information au
callback du SDK.

## 8. Changement de firmware et maintenance

Un redémarrage avec le même identifiant firmware ne réapplique pas la politique
de mise à jour. Un identifiant différent, ou absent lors de la première
installation, déclenche l'évaluation de `init_on_update` une seule fois.

Pour préserver la sémantique actuelle :

| Politique | Effet sur les paramètres de provisioning |
| --- | --- |
| `init_on_update=false` | Conservation des valeurs NVS |
| `init_on_update=true` | Remplacement par les valeurs par défaut du schéma et suppression du drapeau de provisioning forcé |

Ce mécanisme n'efface pas globalement la NVS et ne réinitialise pas implicitement
les contrôles des bridges. Une migration de schéma ou un effacement utilisateur
sont des opérations distinctes. L'identifiant de firmware traité ne doit être
enregistré qu'après réussite des écritures nécessaires ; les erreurs doivent
rester observables et permettre une reprise.

L'entrée en OTA est un mode de maintenance explicite. L'application définit
la mise en sécurité et l'éventuelle suspension de son activité avant le
transfert. La garantie de fonctionnement continu lors d'une panne réseau ne
s'applique pas à un arrêt volontaire pour flash/redémarrage.

## 9. Répartition des responsabilités

| SDK | Application Virgo ou équipement |
| --- | --- |
| Configuration, persistance et statut d'initialisation | Sorties sûres, validation et initialisation métier |
| Gestion des transports et sessions HA | Réaction aux nouvelles commandes |
| Publication des contrôles courants | Actualisation et publication des indicateurs |
| Horloge civile et état de validité | Source horaire et décisions planifiées |
| Journal borné et diagnostics RAM/NVS | Codes et interprétation métier des événements |
| Supervision générique commande/retour | Acquisition, seuils, priorités et décision d'arrêt |

Le journal et les diagnostics doivent pouvoir fonctionner sans réseau. Le
journal RAM n'est pas un historique garanti après coupure électrique ; les
pertes par saturation doivent être identifiables. Les diagnostics ne doivent
pas écrire ni effacer la NVS pour la mesurer.

## 10. Critères d'acceptation 1.4.0

| Essai | Résultat observable attendu |
| --- | --- |
| Boot sans AP, puis sans broker, puis sans HA | Initialisation locale exécutée une fois et boucle locale active |
| Boot sans configuration réseau | Configuration possible et fonctionnement local ou mode sûr selon les seuls besoins locaux |
| Boot avec configuration locale invalide | Sorties sûres, cause observable, aucune utilisation d'objet invalide |
| Coupure pendant une temporisation | Échéance monotone inchangée ; pas de redémarrage d'automate |
| Modification locale hors réseau puis reconnexion | État courant conservé ; contrôles et indicateurs transmis selon leurs responsabilités |
| Retour HA sans rupture MQTT | Une nouvelle synchronisation consommable, sans initialisation locale |
| Coupure pendant le flush ou la synchronisation | Session abandonnée proprement ; aucune confirmation obsolète acceptée |
| Boot sans heure puis synchronisation | Fonctions sans horaire actives ; heure valide ensuite |
| Correction horaire dans les deux sens | Délais monotones inchangés ; politique applicative des horaires respectée |
| Coupure réseau supérieure à 49,7 jours simulée | Avancement civil conservé sans tick obligatoire |
| Redémarrage ordinaire et changement firmware, avec les deux politiques | Nombre de restaurations et paramètres résultants conformes aux sections 2 et 8 |
| Erreur NVS ou journal saturé | Erreur/perte identifiable ; aucun effacement implicite |

Mesurer la durée maximale entre deux ticks locaux durant scans, connexions,
découverte, synchronisation et provisioning. L'application doit déclarer sa
cadence maximale admissible ; les mesures doivent la respecter. Le qualificatif
« non bloquant » et la présence d'un watchdog ne remplacent pas ces mesures.

## 11. État actuel et ordre de réalisation

Déjà réalisé : modèle Virgo débarrassé de la logique piscine et du drapeau
attendant HA, horloge indépendante et tests hôte, compilation DevKit, et
documentation des emplacements de sécurité applicative dans Virgo.

En cours : les sessions HA disposent désormais d'une époque interne. Une
nouvelle connexion MQTT, la perte de `HA_online` ou l'expiration du heartbeat
effacent les preuves de la session précédente. `newSeqComm()` conserve le
signal jusqu'à consommation et `setHAInitDone()` vérifie l'époque active. Ces
changements doivent encore être compilés et testés sur matériel.

À réaliser avant conformité complète :

1. Extraire les diagnostics, le journal et le superviseur génériques.
2. Exécuter les essais d'intégration et sur matériel selon la matrice ci-dessus.

Le filtrage des commandes retained tardives et l'optimisation des latences de
scan/connexion réseau sont reportés après 1.4.0. Ce sont des pistes générales
de robustesse, pas des améliorations issues de JiH_Piscin.

Ce contrat n'ajoute aucune fonction spécifique à la piscine au SDK et ne
modifie pas la politique de republication des indicateurs par l'application.
