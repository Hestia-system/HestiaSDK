# Test hôte de l'horloge

Depuis la racine du dépôt, avec un compilateur C++17 :

```sh
c++ -std=c++17 -Wall -Wextra -Werror -Itests/local_clock/stubs -Isrc src/datetime_utils.cpp tests/local_clock/test.cpp -o /tmp/hestia-local-clock-test
/tmp/hestia-local-clock-test
```

Le test compile le service réel avec une petite surface Arduino String et un
timer ESP32 simulé. Il couvre boot sans heure, validation, années bissextiles,
corrections, doublons HA, longue durée hors ligne, fuseaux et limites de plage.
La compilation PlatformIO vérifie séparément les véritables API ESP32/Arduino.
