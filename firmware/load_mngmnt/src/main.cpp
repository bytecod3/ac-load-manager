#include <Arduino.h>

// put function declarations here:
int myFunction();

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  myFunction();
}

// put function definitions here:
int myFunction() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(1000);
  digitalWrite(LED_BUILTIN, LOW);
  delay(400);
}