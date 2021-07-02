
/*
 * 
 * 
 * @autor:        Lars Hansmann
 * @datum:        20.06.2021
 * @matrikel-Nr:  15414064
 *
 *Compelete implementation for an multimeter, based on the Arduino Uno.
 *Measured units are voltage, current and power. They get displayed on an 16x2 LCD.
 */



#include <LiquidCrystal.h>


#define ADC_RESOLUTION 1024                  // 10 bit ADC resolution
#define AREF 1.077                           // AREF (internal reference voltage) 1077mV 
#define FACTOR_VOLTAGEDIVIDER 102.1          // calibrated factor for the voltage divider
#define SHUNT_RES 0.51                       // resitance of shunt resistor

//as the reference voltage coming from the voltage divider lies at 0.504V and the Arduino internal reference voltage lies at 1.1V,
//the lower limit is little bit smaller than the upper limit. If the measures current/voltage goes over these values the overflow LED turns on
#define CURRENT_LOWER_LIMIT -0.90               
#define CURRENT_UPPER_LIMIT 1.05
#define VOLTAGE_LOWER_LIMIT -45.0
#define VOLTAGE_UPPER_LIMIT 55.0



//strings to display on the LCD 16x2 display
const String voltage_str = "Spannung:";      
const String current_str = "Strom:";         
const String power_str = "Leistung:";        

const int pushButton_R = 8;                  //right push button to pin 8
const int pushButton_L = 9;                  //left push button to pin 9
const int led = 10;                          //led to pin 10


long sum_voltage = 0;                        //adc 1 for reading measured voltage
long sum_halfRef = 0;                        //adc 3 for reading external reference voltage, should read ~0.5V
int dump = 0;                                //to dump the first reading for better accuracy
int numOfSamples = 256;                      //number of samples for oversampling
float voltage = 0;                           //voltage after all calculations

long sum_current = 0;                        //for summing up the voltage drop over the shunt resistor
float current = 0;                           //current after all calculations

float power = 0;                             //current * voltage


int buttonState_new_R = 0;
int buttonState_old_R = 1;
int buttonState_new_L = 0;
int buttonState_old_L = 1;
int measuringMode = 0;                       //0 = voltage, 1 = current, 2 = power



float difference_vol = 0;                   //used to up the precision of the measured voltage
float difference_cur = 0;                   //used to up the precision of the measured current

//used to store measured reference voltage on all three analog pins on startup
float analog_voltage = 0;                  
float analog_current = 0;
float analog_aref = 0;


//init LCD interface pins with the connected arduino pins
const int rs = 2, en = 3, d4 = 4, d5 = 5, d6 = 6, d7 = 7;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

void setup() {

  analogReference(INTERNAL); //set 1.1v internal reference
  delay(500);                //delay to stabilize AREF voltage
  
  // initialize serial communication at 9600 bits per second
  Serial.begin(9600); 

  //init switches and led
  pinMode(pushButton_R, INPUT_PULLUP); 
  pinMode(pushButton_L, INPUT_PULLUP);
  pinMode(led, OUTPUT);

  measureAnalogDifference(); //call this function on startup to measure the difference in the reference voltage on all three analog pins for later calculations
 
  lcd.begin(16, 2);         // set up the LCD's number of columns and rows:
  lcd.setCursor(0, 1);      // set the cursor to column 0, line 1
  lcd.print(voltage_str);   //on startup display voltage string 
}



/*
 * Holds the switch-statement to switch between measuring modes
 */
void loop() {

  switchMeasuringMode();
  
   switch (measuringMode) {
      case 0: //measure voltage
        lcd.setCursor(15, 0);      //set the cursor to position 15 in line 0 to set the navigation arrows
        lcd.print(">");            //print navigation arrow
        lcd.setCursor(0, 1);       //set cursor to display voltage string    
        lcd.print(voltage_str);
        measureVoltage();          //measure voltage
        displayVoltage();          //display voltage
        break;
      case 1: //measure current
        lcd.setCursor(15, 0);        
        lcd.print(">");
        lcd.setCursor(0, 0);     
        lcd.print("<");
        lcd.setCursor(0, 1);      
        lcd.print(current_str);
        measureCurrent();
        displayCurrent();
        break;
      case 2: //measure power
        lcd.setCursor(0, 0);      
        lcd.print("<");
        lcd.setCursor(0, 1);     
        lcd.print(power_str); 
        measurePower(); 
        displayPower();
        break;
      default:
        Serial.print("void loop: default");
        break;
      }
}


/*
 * Called once on startup to calculate the difference in the ~0.5V reference voltage on the three used analog pins.
 * The difference for current and voltage are later used in the calculations to up the precision.
 */
void measureAnalogDifference(){

  //reset sums
  analog_voltage = 0;
  analog_current = 0;
  analog_aref = 0;

  //add up 256 samples and calculate the average
  for(int a = 0; a < numOfSamples; a++){
    analog_voltage = analog_voltage + analogRead(A1);
    delay(1);                                         //1ms between every reading to space out the readings for a better result
  }
  analog_voltage = analog_voltage/numOfSamples;       //calculate the average


  for(int b = 0; b < numOfSamples; b++){
    analog_current = analog_current + analogRead(A2);
    delay(1);
  }
  analog_current = analog_current/numOfSamples;


  for(int c = 0; c < numOfSamples; c++){
    analog_aref = analog_aref + analogRead(A3);
    delay(1);
  }
  analog_aref = analog_aref/numOfSamples;


  difference_cur = (analog_aref-analog_current);    
  difference_vol = (analog_aref-analog_voltage);
 
}  


/*
 * Measure power by calling the measureVoltage and measureCurrent functions and multiplying 
 * the outcome P = U * I
 */
void measurePower(){
  
    measureVoltage();
    measureCurrent();
    power = voltage * current;    //voltage and current are global variables
 
}


/*
 * Display the watt (power) in W
 */
void displayPower(){


   char powerDisplay_str[6];              //array to hold the power value and the unit
   dtostrf(power,6,3,powerDisplay_str);   //copy the float value into the array with 3 decial digits
   powerDisplay_str[6] = 'W';             //add unit
   lcd.setCursor(9,1);
   lcd.print(powerDisplay_str);           //display the value 
   delay(500);                            //delay for 500ms so that the display doesn't constantly update

}



/*
 * Measures current by calculating the difference in voltage over a shunt resistor.
 */
void measureCurrent(){

  //reset sums
  sum_current = 0;
  sum_halfRef = 0;

  dump = analogRead(A2); //dump the first reading for better accuarcy
  delay(1);

  //sum up 256 reading and calculate the average
  for(int i = 0; i < numOfSamples; i++){
    sum_current += analogRead(A2);
  }
  //adding difference_cur because the readings of A2 and A3 are not excactly the same even though they should be,
  //difference_cur is the measured difference
  sum_current = (sum_current/numOfSamples) + difference_cur; 

  dump = analogRead(A3);
  delay(1);

  //sum up 256 reading and calculate the average for the external reference voltage
   for(int i = 0; i < numOfSamples; i++){
    sum_halfRef += analogRead(A3);
  }
  sum_halfRef = sum_halfRef/numOfSamples;
  sum_current = (float)(sum_current-sum_halfRef); //difference is equal to the voltage drop over the shunt in bits

  //clipping the difference to 0 if in intervall (-1,1) to cope with noises 
  if(sum_current >= -1 && sum_current <= 1){
      sum_current = 0;
    }

   //if a negative current is measured, we need to add the analog difference again
  if(sum_current < 0){
    sum_current = sum_current + difference_cur;
  } 

  current = (((float)sum_current * AREF) / ADC_RESOLUTION); //calculate the voltage
  current = (float)current/SHUNT_RES;                       //divide by resistance of shunt resistor to get the current


  //Turn the overflow LED on in case measured current is to high
  if(current <= CURRENT_LOWER_LIMIT || current >= CURRENT_UPPER_LIMIT){
    digitalWrite(led, HIGH); 
  } else {
    digitalWrite(led, LOW); 
  }

   delay(100);
}



/*
 * Display the current in A
 */
void displayCurrent(){


   char currentDisplay_str[6];                //array to hold the power value and the unit
   dtostrf(current,6,3,currentDisplay_str);   //copy float value into array with 3 decimal digits
   currentDisplay_str[6] = 'A';               //add unit
   lcd.setCursor(9,1);
   lcd.print(currentDisplay_str);             //display value
   delay(500);                                //delay for 500ms so that the display doens't constantly update

}




/*
 * Measures voltage by adding up a certain amount of samples coming from analog input A3 and A1 (voltage divider),
 * substracting the A3 samples from the A1 samples and converting them to voltage.
 * 
 */
void measureVoltage(){
  
  //reset sums
  sum_voltage = 0;
  sum_halfRef = 0;

  
  dump = analogRead(A3); //dump first reading for better accuarcy
  delay(1);

  //Add up samples for AREF
  for(int i = 0; i < numOfSamples; i++){
    sum_halfRef += analogRead(A3);
  }
  sum_halfRef = ((long)sum_halfRef/numOfSamples); //divide sum by 'numOfSamples' to get the average value

  dump = analogRead(A1); 
  delay(1);
  
  //Add up samples for ADC 1
  for(int i = 0; i < numOfSamples; i++){
    sum_voltage += analogRead(A1);
  }
  
  //adding difference_vol because the readings of A1 and A3 without load are not excactly the same even though they should be,
  //difference_vol is the measured difference
  sum_voltage = (long)sum_voltage/numOfSamples + difference_vol;
  sum_voltage = sum_voltage - sum_halfRef; //subtract external reference voltage from ADC 1 to allow reading negative and positive voltages

  //Clip sum to 0 if in interval (-1,1) to cope with noises
  if(sum_voltage <= 1 && sum_voltage >= -1){
    sum_voltage = 0;
  }

  //if a negative voltage is measured, we need to add the analog difference again
  if(sum_voltage < 0){
    sum_voltage = sum_voltage + difference_vol;
  } 


  //calculate voltage
  voltage = ((((float)sum_voltage * AREF) / ADC_RESOLUTION)* FACTOR_VOLTAGEDIVIDER);  

  //Turn the overflow LED on in case measured voltage is to high
  if(voltage <= VOLTAGE_LOWER_LIMIT || voltage >= VOLTAGE_UPPER_LIMIT){
    digitalWrite(led, HIGH); 
  } else {
    digitalWrite(led, LOW); 
  }

   delay(100); 

}



/*
 * Display the voltage using the unit V
 */
void displayVoltage(){

   char voltageDisplay_str[5];
   dtostrf(voltage,5,2,voltageDisplay_str);
   voltageDisplay_str[5] = 'V';
   lcd.setCursor(10,1);
   lcd.print(voltageDisplay_str);
   delay(500);
}



/*
 * Used to switch between measuring voltage, current and power
 * 
 */
void switchMeasuringMode(){

  //read the digital in values for button right (R) and left (L)
  buttonState_new_R = digitalRead(pushButton_R);
  buttonState_new_L = digitalRead(pushButton_L);


  //as we are using 2 pullup buttons, only switch mode if there is a 1 to 0 transition
  if(buttonState_old_R == 1 && buttonState_new_R == 0){
      if(!(measuringMode >= 2)){ //don't do anything if we are already at power (state 2)
        clearLCD();              //clear the lcd display before displaying new string
        measuringMode++;         //increment measuringMode
        
      }
  } else if(buttonState_old_L == 1 && buttonState_new_L == 0){
      if(!(measuringMode <= 0)){ //don't do anything if we are already at voltage (state 0) 
         clearLCD();
        measuringMode--;
       
      }
  }
  buttonState_old_R = buttonState_new_R;
  buttonState_old_L = buttonState_new_L;

}



/*
 * Clears everything on the lcd display
 */
void clearLCD(){
       lcd.setCursor(0, 0);      // set the cursor to column 0, line 0 to clear line 0
      for (int i = 0; i < 16; i++){
           lcd.write(' ');
         }
      lcd.setCursor(0, 1);      // set the cursor to column 0, line 1 to clear line 1
      for (int i = 0; i < 16; i++){
           lcd.write(' ');
         }
        
}
