# Services génériques

Test hôte minimal, sans Arduino ni ESP-IDF :

```sh
c++ -std=c++17 -Isrc tests/generic_services/test.cpp src/HestiaEquipmentSupervisor.cpp -o /tmp/hestia_generic_services && /tmp/hestia_generic_services
```

Il contrôle le tampon circulaire du journal et les transitions du superviseur,
y compris le passage à zéro d'une horloge `millis()` 32 bits.
