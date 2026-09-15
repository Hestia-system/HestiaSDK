# Créer un IoT à partir de Virgo : sécurité et fonctionnement local

`src/main.cpp` est un modèle d'application. Le SDK gère la configuration,
les bridges et la communication ; l'application reste responsable de la
sécurité physique et de la logique métier.

## Checklist avant de programmer l'équipement

1. Identifier l'état sûr de chaque sortie : relais, moteur, vanne, LED,
   alimentation de capteur, etc.
2. Placer les niveaux électriques sûrs avant de déclarer les GPIO en sortie,
   puis le faire dans la section **[1] ÉTAT SÛR MATÉRIEL** de `setup()`.
3. Distinguer les paramètres métier critiques des paramètres réseau.
   Les paramètres Wi-Fi, MQTT et HA ne doivent pas empêcher une fonction
   locale de fonctionner.
4. Dans **[2] INITIALISATION APPLICATIVE LOCALE**, initialiser une seule fois
   les pilotes, capteurs et automatismes à partir de la configuration locale.
5. Si un paramètre métier critique est invalide, conserver l'état sûr,
   afficher ou enregistrer un diagnostic et ne pas lancer l'automatisme.
   Le provisioning est une opération de maintenance demandée par bouton.
6. Dans **[3] BOUCLE APPLICATIVE LOCALE**, faire tourner la logique métier à
   chaque passage, y compris hors Wi-Fi, MQTT ou Home Assistant.
7. Employer `LocalClock::isValid()` seulement autour des décisions qui ont
   besoin d'une heure civile. Les délais de sécurité utilisent un temps
   monotone, pas l'horloge locale.
8. Lors d'une reconnexion HA, publier les états utiles sans réinitialiser les
   équipements, les demandes ou les temporisations RAM.

## Exemple de structure

```cpp
void setup() {
    mettreSortiesDansEtatSur();
    HestiaCore::initCore(...);
    initialiserPilotesEtAutomatismes();
}

void loop() {
    HestiaCore::CoreComm();

    executerLogiqueLocale();
}
```

L'exemple ne donne volontairement aucun état sûr concret : `OFF` peut être
sûr pour un équipement et dangereux pour un autre. Cette décision appartient
au firmware de l'IoT et doit être confirmée avec son schéma électrique.
