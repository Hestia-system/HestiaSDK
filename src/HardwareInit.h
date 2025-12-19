#pragma once
#include <Arduino.h>
#include "esp_task_wdt.h"

/*****************************************************************************************
 *  File     : HardwareInit.h
 *  Project  : Hestia SDK / Virgo IoT
 *
 *  Summary
 *  -------
 *  Low-level hardware initialization utilities, providing:
 *    • Serial interface startup
 *    • Diagnostic firmware banner
 *    • ESP-IDF task watchdog configuration
 *
 *  Design Principles
 *  -----------------
 *    • Keep hardware setup minimal — no application logic here
 *    • Ensure deterministic watchdog configuration
 *    • Avoid blocking delays except where stabilization is required (USB CDC)
 *
 *****************************************************************************************/

namespace HardwareInit {

  /**
   * @brief Perform minimal hardware startup.
   *
   * Actions:
   *   • Initialize the Serial interface
   *   • Allow USB CDC stabilization
   *   • Print a firmware banner including build timestamp
   */
  void InitHardwareMinimal();

  /**
   * @brief Configure the ESP-IDF task watchdog.
   *
   * @param timeoutMs  Timeout duration in milliseconds.
   *
   * Behavior:
   *   • Remove any existing watchdog instance
   *   • Initialize a new watchdog (struct or legacy API depending on target)
   *   • Register the current FreeRTOS task
   *   • Perform an initial reset
   */
  void InitHardwareWatchdog(int timeoutMs);

  /**
   * @brief Safely reset the watchdog timer (“kick”).
   *
   * Behavior:
   *   • If the watchdog was successfully initialized, perform a reset.
   *   • Otherwise, do nothing.
   */
  void watchdogKick();

  /**
   * @brief Query whether the watchdog subsystem is active.
   *
   * @return true if the watchdog was initialized and is ready to be used.
   *
   * @note Implemented in HardwareInit.cpp if required.
   */
  bool wdtReady();

} // namespace HardwareInit
// ============================================================================