#include "Spinner.h"
#include "Display.h"
#include "State.h"
#include <math.h>

#define CENTER_X (SCREEN_WIDTH / 2)
#define CENTER_Y (SCREEN_HEIGHT / 2)
#define SPINNER_RADIUS 20
#define NUM_DOTS 8
#define DOT_RADIUS 3

TaskHandle_t spinnerHandle = NULL;
volatile bool animationRunning = true;

void fancySpinnerTask(void * parameter) {
  float angle = 0;
  const float step = 2 * PI / NUM_DOTS;
  const int updateInterval = 20;

  while (animationRunning) {
    display.fillCircle(CENTER_X, CENTER_Y, SPINNER_RADIUS + DOT_RADIUS + 3, SSD1327_BLACK);
    
    for (int i = 0; i < NUM_DOTS; i++) {
      float a = angle - i * step;
      int x = CENTER_X + SPINNER_RADIUS * cos(a);
      int y = CENTER_Y + SPINNER_RADIUS * sin(a);
      uint8_t brightness = 255 - (i * (255 / (NUM_DOTS + 1)));
      display.fillCircle(x, y, DOT_RADIUS, brightness);
    }
    
    display.display();
    angle += 0.1;
    if (angle > 2 * PI) angle -= 2 * PI;
    
    vTaskDelay(updateInterval / portTICK_PERIOD_MS);
  }

  display.clearDisplay();
  display.display();
  
  // Clean up
  spinnerHandle = NULL;
  vTaskDelete(NULL);
}

void startLoadingAnimation() {
  // Prevent starting multiple spinners
  if (spinnerHandle != NULL) {
    Serial.println("⚠️ Spinner already running!");
    return;
  }
  
  animationRunning = true;
  display.clearDisplay();
  drawCenteredText("ROUGE MP3", 108, 2);
  display.display();

  BaseType_t result = xTaskCreatePinnedToCore(
    fancySpinnerTask,
    "SpinnerTask",
    2048,
    NULL,
    1,
    &spinnerHandle,
    0  // Core 0
  );
  
  if (result != pdPASS) {
    Serial.println("❌ Failed to create spinner task!");
    spinnerHandle = NULL;
  }
}

void stopLoadingAnimation() {
  animationRunning = false;
  
  // Wait for task to clean up
  if (spinnerHandle != NULL) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}