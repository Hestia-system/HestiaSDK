#pragma once

#include <Arduino.h>

namespace DateTimeUtils {

// Format strict YYYY-MM-DD HH:MM:SS, années 1970..9999.
// Les « epoch » sont des secondes civiles depuis 1970-01-01, pas de l'UTC.
// Aucun fuseau système ni changement d'heure automatique.
// Entrée invalide : -1, chaîne vide ou "--:--[:--]" selon le résultat.
int parseHourFromDateTime(const String& dateTime);
int parseMinuteOfDay(const String& dateTime);
String parseDayKey(const String& dateTime);
int64_t parseEpochSecond(const String& dateTime);
int64_t parseEpochMinute(const String& dateTime);
// Début inclus, fin exclue ; bornes égales = journée entière.
bool isHourInDayWindow(int hour, int hourDay, int hourNight);

String formatClock(int minuteOfDay);
String formatClockMinute(int64_t epochSecond);
String formatClockSecond(int64_t epochSecond);

}

// Horloge civile du SDK, sans dépendance réseau ou NVS.
// Invalide au boot ; avance après synchronisation avec le timer ESP32 64 bits.
// Aucun tick nécessaire. Le redémarrage/deep sleep exige une nouvelle synchro.
// Utiliser depuis une seule tâche (ou protéger les appels côté application).
namespace LocalClock {

// Applique une nouvelle référence civile valide, même identique à la précédente.
// Une correction en arrière est permise ; utiliser un timer monotone pour les délais.
// Une entrée invalide retourne false et conserve la référence courante.
bool sync(const String& localDateTime);
void invalidate();

// Compatibilité : ignore une valeur HA identique à la dernière acceptée,
// afin de pouvoir appeler cette fonction en boucle sans figer l'horloge.
// L'appelant reste responsable de la fraîcheur de la source (notamment retained).
bool syncFromHA(const String& haDateTime);
bool isValid();

int64_t nowEpochSecond();
int64_t nowEpochMinute();
int minuteOfDay();
String nowDateTime();
String dayKey();

String formatClockMinute(int64_t localEpochSecond);
String formatClockSecond(int64_t localEpochSecond);
int dayOfMonth(int64_t localEpochSecond);

}
