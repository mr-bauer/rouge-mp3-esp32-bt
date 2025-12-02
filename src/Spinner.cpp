#include "Spinner.h"
#include "Display.h"
#include "State.h"
#include <math.h>

#define CENTER_X (SCREEN_WIDTH / 2)
#define CENTER_Y (SCREEN_HEIGHT / 2)
#define SPINNER_RADIUS 30
#define NUM_DOTS 8
#define DOT_RADIUS 5

TaskHandle_t spinnerHandle = NULL;
volatile bool animationRunning = true;

void fancySpinnerTask(void * parameter) {
  Serial.println("🔄 Spinner task started");
  
  float angle = 0;
  const float step = 2 * PI / NUM_DOTS;
  const int updateInterval = 50;
  
  // int frameCount = 0;

  while (animationRunning) {
    // CRITICAL: Take display mutex before drawing
    if (xSemaphoreTake(displayMutex, 100 / portTICK_PERIOD_MS)) {
      
      // Clear spinner area
      display.fillCircle(CENTER_X, CENTER_Y, SPINNER_RADIUS + DOT_RADIUS + 5, COLOR_BG);
      
      for (int i = 0; i < NUM_DOTS; i++) {
        float a = angle - i * step;
        int x = CENTER_X + SPINNER_RADIUS * cos(a);
        int y = CENTER_Y + SPINNER_RADIUS * sin(a);
        
        uint8_t brightness = 255 - (i * (255 / (NUM_DOTS + 1)));
        
        uint16_t color;
        if (brightness > 200) {
          color = COLOR_TEXT;
        } else if (brightness > 150) {
          color = 0xBDF7;
        } else if (brightness > 100) {
          color = 0x7BEF;
        } else if (brightness > 50) {
          color = 0x39E7;
        } else {
          color = 0x18C3;
        }
        
        display.fillCircle(x, y, DOT_RADIUS, color);
      }
      
      xSemaphoreGive(displayMutex);
      
      // Debug: Print every 20 frames
      // frameCount++;
      // if (frameCount % 20 == 0) {
      //   Serial.printf("🔄 Spinner frame %d\n", frameCount);
      // }
    } else {
      Serial.println("⚠️ Spinner couldn't get display mutex");
    }
    
    angle += 0.15;
    if (angle > 2 * PI) angle -= 2 * PI;
    
    vTaskDelay(updateInterval / portTICK_PERIOD_MS);
  }

  Serial.println("🔄 Spinner task ending");
  
  if (xSemaphoreTake(displayMutex, 100 / portTICK_PERIOD_MS)) {
    display.fillScreen(COLOR_BG);
    xSemaphoreGive(displayMutex);
  }
  
  spinnerHandle = NULL;
  vTaskDelete(NULL);
}

void startLoadingAnimation() {
  // Prevent starting multiple spinners
  if (spinnerHandle != NULL) {
    Serial.println("⚠️ Spinner already running!");
    return;
  }
  
  Serial.println("🔄 Starting loading animation...");
  
  animationRunning = true;
  display.fillScreen(COLOR_BG);
  
  // Title at top
  display.setTextColor(COLOR_ACCENT);
  display.setTextSize(3);
  drawCenteredText("ROUGE", 30);
  
  display.setTextColor(COLOR_TEXT);
  display.setTextSize(2);
  drawCenteredText("MP3 Player", 65);
  
  // Status at bottom
  display.setTextColor(COLOR_DISABLED);
  display.setTextSize(1);
  drawCenteredText("Loading...", SCREEN_HEIGHT - 30);

  BaseType_t result = xTaskCreatePinnedToCore(
    fancySpinnerTask,
    "SpinnerTask",
    4096,  // Increased stack size
    NULL,
    1,
    &spinnerHandle,
    0  // Core 0
  );
  
  if (result != pdPASS) {
    Serial.println("❌ Failed to create spinner task!");
    spinnerHandle = NULL;
  } else {
    Serial.println("✅ Spinner task created");
  }
}

void stopLoadingAnimation() {
  Serial.println("🛑 Stopping loading animation...");
  animationRunning = false;
  
  // Wait for task to clean up
  if (spinnerHandle != NULL) {
    vTaskDelay(200 / portTICK_PERIOD_MS);  // Increased wait time
  }
  
  Serial.println("✅ Loading animation stopped");
}