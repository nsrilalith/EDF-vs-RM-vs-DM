#include <Arduino_FreeRTOS.h>

#include "scheduler.h"
#include "task.h"

//#include "PerformanceMetrics.h"

unsigned long startTime;
const unsigned long timeoutPeriod = 60000; // 2 minutes in milliseconds


TaskHandle_t xHandle1 = NULL;
TaskHandle_t xHandle2 = NULL;
TaskHandle_t xHandle3 = NULL;
TaskHandle_t xHandle4 = NULL;


// Function prototypes
void printSchedulerOverhead(void);
// the loop function runs over and over again forever
void loop() {
      // Check if the current time minus the start time is greater than the timeout period
    if (millis() - startTime > timeoutPeriod) {
        // Print the performance metrics just before ending

        // End the scheduler
        vTaskEndScheduler();

        printMetrics();
        // Halt the system
        while (true) {
            // Optional: Add a small delay to ensure the output buffer is emptied
            delay(1000);
        }

    }

    //Optional: Include a small delay here if the loop runs too fast
    delay(10);
}

static void testFunc1( void *pvParameters )
{ 
  (void) pvParameters;
    TickType_t Task1_start;

  for(int i=0; i<5; i++){
    TickType_t Task1_start = xTaskGetTickCount() *16 ;
    Serial.println("task 1 running");
    // Serial.println(Task1_start); // Convert milliseconds 

  }
}

static void testFunc2( void *pvParameters )
{ 
  (void) pvParameters;
    TickType_t Task2_start;

  for(int i=0; i<11; i++){
    TickType_t Task2_start = xTaskGetTickCount() *16 ;
    Serial.println("task 2 running ");
    // Serial.println(Task2_start); // Convert milliseconds 
  }
}

static void testFunc3( void *pvParameters )
{ 
  (void) pvParameters;
  TickType_t Task3_start;
  for(int i=0; i<11; i++){
  TickType_t Task3_start = xTaskGetTickCount() *16;
  Serial.println("task 3 running ");
    // Serial.println(Task3_start); // Convert milliseconds 
  }
}

static void testFunc4( void *pvParameters )
{ 
  (void) pvParameters;
  TickType_t Task4_start; 
  for(int i=0; i<5; i++){
    TickType_t Task4_start = xTaskGetTickCount() *16;
    Serial.println("task 4 running ");
    // Serial.println(Task4_start); // Convert milliseconds 
  }
  

}


void setup()
{
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB, on LEONARDO, MICRO, YUN, and other 32u4 based boards.
  }
	char c1 = 'a';
	char c2 = 'b';
  char c3 = 'c';
  char c4 = 'd';			
  startTime = millis();
  vSchedulerInit();

  // vSchedulerSetPolicy(schedSCHEDULING_POLICY_EDF); // Set the scheduling policy to EDF explicitly
  
  initializePerformanceMetrics();
  Serial.println("System Initialized.");

  vSchedulerPeriodicTaskCreate(testFunc1, "t1", configMINIMAL_STACK_SIZE, &c1, 4, &xHandle1, pdMS_TO_TICKS(0), pdMS_TO_TICKS(800), pdMS_TO_TICKS(100), pdMS_TO_TICKS(800));
  vSchedulerPeriodicTaskCreate(testFunc2, "t2", configMINIMAL_STACK_SIZE, &c2, 3, &xHandle2, pdMS_TO_TICKS(0), pdMS_TO_TICKS(1000), pdMS_TO_TICKS(200), pdMS_TO_TICKS(1000));
  vSchedulerPeriodicTaskCreate(testFunc3, "t3", configMINIMAL_STACK_SIZE, &c3, 1, &xHandle3, pdMS_TO_TICKS(0), pdMS_TO_TICKS(1500), pdMS_TO_TICKS(200), pdMS_TO_TICKS(1500));
  vSchedulerPeriodicTaskCreate(testFunc4, "t4", configMINIMAL_STACK_SIZE, &c4, 2, &xHandle4, pdMS_TO_TICKS(0), pdMS_TO_TICKS(2000), pdMS_TO_TICKS(100), pdMS_TO_TICKS(2000));
  
  

  vSchedulerStart();

}