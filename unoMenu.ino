#include <util/atomic.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// LED pins
#define LED_RED 6
#define LED_GREEN 5
#define LED_BLUE 3

// Button pins; red button = enter, green = exit
#define RED_BUTTON 2
#define GREEN_BUTTON 4

// Encoder pins; ENCODER1 = CLK, ENCODER2 = DT
#define ENCODER1 A2
#define ENCODER2 A3

#define REFRESH_INTERVAL 1000UL
#define DEBOUNCE_PERIOD 50UL
#define INTENSITY_STEP 10

LiquidCrystal_I2C lcd(0x27, 16, 2);

// Two DS18B20 temperature sensors on pin A1; sensor with id=0 is treated as the Interior Temp probe ("IN"), sensor with id=1 is the exterior one ("OUT")
OneWire oneWire(A1);
DallasTemperature sensors(&oneWire);

volatile int encoder1 = HIGH;
volatile int encoder2 = HIGH;
volatile unsigned long encoderTimestamp = 0UL;

ISR(PCINT1_vect) {
  encoder1 = digitalRead(ENCODER1);
  encoder2 = digitalRead(ENCODER2);
  encoderTimestamp = millis();
}

const String menu1[] = {"1. LED Options", "2. Display", "3. Temperature", "4. About"};
const String menu2[][4] = {
  {"1. Power [OFF]", "2. Red", "3. Green", "4. Blue"},
  {"1. Blight [ON]", "2. Selector [>]"},
  {"1. Sensor IN", "2. Sensor OUT", "3. Units [C]"},
  {"Copyright 2024","Kacper Tomczyk"}
};

const char selectors[] = {'>','-','*'};

int ledVals[] {0,0,0};
bool ledState = false;
bool backlightState = true;
bool isCelsius = true;

int currentLayer = 0;
int menu1Index = 0;
int menu2Index = 0;
char selectorId = 0;

void setup() {
  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_RED, LOW);

  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_GREEN, LOW);

  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, LOW);

  pinMode(ENCODER1, INPUT_PULLUP);
  pinMode(ENCODER2, INPUT_PULLUP);

  pinMode(RED_BUTTON, INPUT_PULLUP);
  pinMode(GREEN_BUTTON, INPUT_PULLUP);

  sensors.begin();

  lcd.init();
  lcd.backlight();
  menu();

  PCICR |= (1 << PCIE1);
  PCMSK1 |= (1 << PCINT10);

  Serial.begin(9600);
}

void menu() {
  static unsigned long lastTempReadout = millis();

  if (currentLayer == 0) {
    lcd.clear();
    int opt  = menu1Index%2;
    int start = menu1Index-opt;
    int end = min(start + 2, sizeof(menu1)/sizeof(menu1[0]));
    for (int i = start; i < end; i++) {
      lcd.setCursor(0,i-start);
      lcd.print((i == menu1Index ? selectors[selectorId] : ' ') + menu1[i]);
    }
    return;
  }
  
  if (currentLayer == 1) {
    lcd.clear();
    int opt = menu2Index%2;
    int start = menu2Index-opt;
    int end = min(start + 2, sizeof(menu2[menu1Index])/sizeof(menu2[menu1Index][0]));
    for (int i = start; i < end; i++) {
      lcd.setCursor(0,i-start);
      lcd.println((i == menu2Index ? selectors[selectorId] : ' ') + menu2[menu1Index][i]);
    }
    return;
  }

  // Displaying LED intensity
  
  if (menu1Index == 0) {
    lcd.setCursor(0,1);
    int intensity = (ledVals[menu2Index-1]*100)/255;
    for(int i=0; i < 3-String(intensity).length(); i++){
      lcd.write(' ');
    }
    lcd.print(intensity);
    return;
  }

  // Displaying temperature

  unsigned long currTime = millis();

  if(currTime - lastTempReadout > REFRESH_INTERVAL){
    lcd.setCursor(0,1);
    float readout = readTemp(menu2Index);
    String readoutStr = String(readout, 1);
    for(int i=0; i < 6-readoutStr.length(); i++) {
      lcd.write(' ');
    }
    lcd.print(readoutStr);
    lastTempReadout = currTime;
    return;
  }

  
}

bool scrollMenu(int encoder) {
  int* currMenuIndex;
  int menuLength;

  if (currentLayer == 0) {
    currMenuIndex = &menu1Index;
    menuLength = sizeof(menu1) / sizeof(menu1[0]);
  } else {
    currMenuIndex = &menu2Index;
    menuLength = sizeof(menu2[menu1Index]) / sizeof(menu2[menu1Index][0]);
  }

  if (encoder == 1) {
    *currMenuIndex = min(*currMenuIndex + 1, menuLength - 1);
    menu();
    return true;
  }

  if (encoder == -1) {
    *currMenuIndex = max(*currMenuIndex - 1, 0);
    menu();
    return true;
  }

  return false;
}

// INPUT

int readEncoder() {
  static unsigned long lastChangeTimestamp = millis();

	int en1;
	int en2;
	unsigned long timestamp;
	
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		en1 = encoder1;
		en2 = encoder2;
		timestamp = encoderTimestamp;
	}
	
	if(en1 == LOW && timestamp-lastChangeTimestamp > DEBOUNCE_PERIOD){
    lastChangeTimestamp = timestamp;

		if(en2 == HIGH) {
			return 1;
		}
		return -1;
	}
	
	return 0;
}

bool isButtonPressed(int pin, int* debounced_button_state, int* previous_reading, unsigned long* last_change_time) {
 bool isPressed = false;
  int current_reading = digitalRead(pin);

  if (*previous_reading != current_reading) {
    *last_change_time = millis();
  }

  if (millis() - *last_change_time > DEBOUNCE_PERIOD) {
    if (current_reading != *debounced_button_state) {
      if (*debounced_button_state == HIGH && current_reading == LOW) {
        isPressed = true;
      }
      *debounced_button_state = current_reading;
    }
  }

  *previous_reading = current_reading;

  return isPressed;
}

bool isEnterButtonPressed() {
	static int debounced_button_state = HIGH;
	static int previous_reading = HIGH;
	static unsigned long last_change_time = 0UL;
	
	return isButtonPressed(RED_BUTTON, &debounced_button_state, &previous_reading, &last_change_time);
}

bool isExitButtonPressed() {
	static int debounced_button_state = HIGH;
	static int previous_reading = HIGH;
	static unsigned long last_change_time = 0UL;
	
	return isButtonPressed(GREEN_BUTTON, &debounced_button_state, &previous_reading, &last_change_time);
}

// LOGIKA

float readTemp(int index){
  sensors.requestTemperatures();

  if(isCelsius){
    return sensors.getTempCByIndex(index);
  }

  return sensors.getTempFByIndex(index);
}

void toggleUnits() {
  isCelsius = !isCelsius;

  if(isCelsius){
    menu2[2][2] = "3. Units [C]";
  } else {
    menu2[2][2] = "3. Units [F]";
  }

  menu();
}

void toggleSelector() {
  selectorId = (selectorId+1)%(sizeof(selectors)/sizeof(selectors[0]));
  switch(selectorId){
    case 0:
      menu2[1][1] = "2. Selector [>]";
    break;
    case 1:
      menu2[1][1] = "2. Selector [-]";
    break;
    case 2:
      menu2[1][1] = "2. Selector [*]";
    break;
  }
  menu();
}

void toggleBacklight() {
  backlightState = !backlightState;

  if(backlightState){
    menu2[1][0] = "1. Blight [ON]";
    lcd.backlight();
  } else {
    menu2[1][0] = "1. Blight [OFF]";
    lcd.noBacklight();
  }

  menu();
}

int getDiodePin(int menu2Option) {
  switch(menu2Option) {
    case 0:
      return 6;
      break;
    case 1:
      return 5;
      break;
    case 2:
      return 3;
      break;
  }
}

void toggleLED() {
  ledState = !ledState;

  if(ledState){
    for(int i=0; i<3; i++){
      menu2[0][0] = "1. Power [ON]";
      analogWrite(getDiodePin(i), ledVals[i]);
    }
  } else {
    for(int i=0; i<3; i++){
      menu2[0][0] = "1. Power [OFF]";
      analogWrite(getDiodePin(i), 0);
    }
  }

  menu();
}

void changeIntensity(int val) {
  int diode = menu2Index-1;
  ledVals[diode] = max(min(255, ledVals[diode]+val), 0);
  if(ledState){
    analogWrite(getDiodePin(diode), ledVals[diode]);
  }
  menu();
}

void increaseIntensity() {
  changeIntensity(INTENSITY_STEP);
}

void decreaseIntensity() {
  changeIntensity(-INTENSITY_STEP);
}

void loop() {
  int encoder = readEncoder();
  bool enterButtonState = isEnterButtonPressed();
  bool exitButtonState = isExitButtonPressed();

	switch(currentLayer) {
    // Layer 1
		case 0:

			if(enterButtonState) {
				currentLayer++;
				menu();
        return;
			}

      if(scrollMenu(encoder)){
        return;
      }
			
		break;
		
    // Layer 2
		case 1:

      if(exitButtonState) {
        menu2Index = 0;
        currentLayer--;
        menu();
        return;
      } 
			  
      switch(menu1Index) {
        // LED Options
        case 0:

          if(enterButtonState){
            if(menu2Index == 0){
              toggleLED();
              return;
            }
              
            currentLayer++;

            // Initialize display for Intensity readout
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("Intensity:");
            lcd.setCursor(3,1);
            lcd.write('%');
            menu();
            return;
          }
            
          if(scrollMenu(encoder)){
            return;
          }
          
        break;

        // Display
        case 1:

          if(enterButtonState){
            if(menu2Index == 0){
              toggleBacklight();
              return;
            }

            toggleSelector();
            return;
          }

          if(scrollMenu(encoder)){
            return;
          }

        break;

        // Temperature
        case 2:
          if(enterButtonState){
            if(menu2Index == 2){
              toggleUnits();
              return;
            }
            
            
            currentLayer++;

            // Initialize display for temperature readout
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("Temperature:");
            lcd.setCursor(6,1);
            char unit = isCelsius ? 'C' : 'F';
            lcd.print(unit);
            return;
          }

          if(scrollMenu(encoder)){
            return;
          }

        break;
			}
      
		break;
    
    // Layer 3
    case 2:
      if(exitButtonState) {
        currentLayer--;
        menu();
        return;
      }

      if(menu1Index == 2){
        menu();  // Refresh the menu so that the temperature updates
        return;
      }

      if(encoder == 1){
        increaseIntensity();
        return;
      }

      if(encoder == -1){
        decreaseIntensity();
        return;
      }
      
    break;
	}
}