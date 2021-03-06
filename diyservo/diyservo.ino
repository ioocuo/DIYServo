/*
   DIY Servo - Arduino driving VNH2SP30 Motor Driver Carrier MD01B

   This work is licensed under the Creative Commons Attribution-ShareAlike 3.0 Unported License. To view a copy of this license, visit http://creativecommons.org/licenses/by-sa/3.0/ or send a letter to Creative Commons, 444 Castro Street, Suite 900, Mountain View, California, 94041, USA.
 */

#include <PololuDriver.h>
#include <PID_v1.h>
#include <PID_AutoTune_v0.h>

#define ENCODER_A_PIN 2
#define ENCODER_B_PIN 3

// PID Variables
double currentPosition = 310;
double distance = 0; // distance in degrees from currentPosition to positionGoal
double distanceGoal = 0; // we always want the motor to be right on the money
double motorSpeed = 0;
double positionGoal = 120;
double kp=0.000001, ki=0, kd=40;
/* kd=0.005 kind of stable */
double upperSpeedLimit = 255;
double lowerSpeedLimit = 33;

// AutoTune Library Variables
double aTuneStep=80, aTuneNoise=1, aTuneStartValue=(upperSpeedLimit - lowerSpeedLimit) / 2 + lowerSpeedLimit;
unsigned int aTuneLookBack=20;
byte ATuneModeRemember=2;

PololuDriver myPololuDriver(11,10,12); // initialize instance of class

const int ledPin = 13; //LED connected to digital pin 13
const int clockPin = 7; //output to clock
const int CSnPin = 6; //output to chip select
const int inputPin = 2; //read AS5040

int debug = 1; //SET THIS TO 0 TO DISABLE PRINTING OF ERROR CODES
boolean tuning = false; // Set this to true to autotune PID values

String currentState = "stop";
String lastState = "stop";
long lastAngle = 0;

//Specify the links and initial tuning parameters
PID myPID(&distance, &motorSpeed, &distanceGoal,kp,ki,kd, DIRECT);
PID_ATune aTune(&currentPosition, &motorSpeed);

// the setup routine runs once when you press reset:
void setup() { 
  Serial.begin(19200);
  Serial.setTimeout(50);
  Serial.println("Started");

  pinMode(ENCODER_A_PIN, INPUT);
  pinMode(ENCODER_B_PIN, INPUT);

  pinMode(ledPin, OUTPUT); // visual signal of I/O to chip
  pinMode(clockPin, OUTPUT); // SCK
  pinMode(CSnPin, OUTPUT); // CSn -- has to toggle high and low to signal chip to start data transfer
  pinMode(inputPin, INPUT); // SDA

  myPololuDriver.Run(POLOLU_STOP);

  currentPosition = read_angle();

  //turn the PID on
  myPID.SetMode(AUTOMATIC);
  myPID.SetOutputLimits(lowerSpeedLimit,upperSpeedLimit);

  if(tuning)
  {
    tuning=false;
    changeAutoTune();
    tuning=true;
    Serial.println("tuning mode");
  }
}

long angle_diff(long angle1, long angle2) {
  return (abs(angle1 + 180 - angle2) % 360 - 180); 
}

boolean should_go_forward(long angle1, long angle2) {
  return ((angle1 - angle2 + 360)%360>180);
}

// the loop routine runs over and over again forever:
void loop() {
  lastState = currentState;

  currentPosition = read_angle();
  distance = abs(angle_diff(currentPosition, positionGoal));

  if(tuning)
  {
    byte val = (aTune.Runtime());
    if (val!=0)
    {
      tuning = false;
    }
    if(!tuning)
    { //we're done, set the tuning parameters
      kp = aTune.GetKp();
      ki = aTune.GetKi();
      kd = aTune.GetKd();
      myPID.SetTunings(kp,ki,kd);
      AutoTuneHelper(false);
      Serial.print("kp: ");Serial.print(myPID.GetKp());Serial.print(" ");
      Serial.print("ki: ");Serial.print(myPID.GetKi());Serial.print(" ");
      Serial.print("kd: ");Serial.print(myPID.GetKd());Serial.println();
    }
  }
  else myPID.Compute();

  myPololuDriver.SetSpeed((byte) motorSpeed);

  // send data only when you receive data:
  if (Serial.available() > 0) {
    myPololuDriver.Run(POLOLU_STOP);
    currentState = "stop";
    lastState = "stop";
    // read the incoming byte:
    positionGoal = (double) Serial.parseInt();

    // say what you got:
    Serial.print("I received: ");
    Serial.println(positionGoal, DEC);
    Serial.print("myangle: ");
    Serial.println(currentPosition, DEC);

    //if((positionGoal==0 && !tuning) || (positionGoal!=0 && tuning))changeAutoTune();
  } else {
    if (abs(angle_diff(lastAngle,currentPosition)) > 1) {
      //Serial.print("angle_diff: ");
      //Serial.println(angle_diff(currentPosition, positionGoal), DEC);
    }

    if(tuning){
      //Serial.println("tuning mode");
    }
  }

  if (abs(angle_diff(currentPosition, positionGoal)) == 0 /* 0 degree hysterisis */) {
    if (lastState != "stop") {
/*
      Serial.println("stop");
      Serial.print("currentPosition: ");
      Serial.println(currentPosition, DEC);
*/
      myPololuDriver.SetSpeed((byte)motorSpeed);
      currentState = "stop";
      myPololuDriver.Run(POLOLU_STOP);
    }
  } else {
    if (!should_go_forward(currentPosition, positionGoal)) {
/*
      Serial.print("should go forward");
      Serial.print(",distance=");
      Serial.print(distance,DEC);
      Serial.print(",motorSpeed=");
      Serial.print(motorSpeed,DEC);
      Serial.print(",currentPosition=");
      Serial.print(currentPosition,DEC);
      Serial.print(",positionGoal=");
      Serial.println(positionGoal,DEC);
*/
      if (lastState != "forward") {
//        Serial.println("forward");
        myPololuDriver.SetSpeed((byte)motorSpeed);
        currentState = "forward";
        myPololuDriver.Run(POLOLU_FORWARD);
      }
    } else {
/*
      Serial.print("should reverse");
      Serial.print(",distance=");
      Serial.print(distance,DEC);
      Serial.print(",motorSpeed=");
      Serial.print(motorSpeed,DEC);
      Serial.print(",currentPosition=");
      Serial.print(currentPosition,DEC);
      Serial.print(",positionGoal=");
      Serial.println(positionGoal,DEC);
*/

      if (lastState != "reverse") {
//        Serial.println("reverse");
        myPololuDriver.SetSpeed((byte)motorSpeed);
        currentState = "reverse";
        myPololuDriver.Run(POLOLU_REVERSE);
      }
    }
  }
  lastAngle = currentPosition;
}

long read_angle()
{
  int inputstream = 0; //one bit read from pin
  long packeddata = 0; //two bytes concatenated from inputstream
  long angle = 0; //holds processed angle value
  long anglemask = 65472; //0x1111111111000000: mask to obtain first 10 digits with position info
  long statusmask = 63; //0x000000000111111; mask to obtain last 6 digits containing status info
  long statusbits; //holds status/error information
  int DECn; //bit holding decreasing magnet field error data
  int INCn; //bit holding increasing magnet field error data
  int OCF; //bit holding startup-valid bit
  int COF; //bit holding cordic DSP processing error data
  int LIN; //bit holding magnet field displacement error data
  int shortdelay = 40; // this is the microseconds of delay in the data clock
  int longdelay = 1; // this is the milliseconds between readings

  // CSn needs to cycle from high to low to initiate transfer. Then clock cycles. As it goes high
  // again, data will appear on sda
  digitalWrite(CSnPin, HIGH); // CSn high
  digitalWrite(clockPin, HIGH); // CLK high
  //delay(longdelay);// time between readings
  delayMicroseconds(shortdelay); // delay for chip initialization
  digitalWrite(ledPin, HIGH); // signal start of transfer with LED
  digitalWrite(CSnPin, LOW); // CSn low: start of transfer
  delayMicroseconds(shortdelay); // delay for chip initialization
  digitalWrite(clockPin, LOW); // CLK goes low: start clocking
  delayMicroseconds(shortdelay); // hold low
  for (int x=0; x <16; x++) // clock signal, 16 transitions, output to clock pin
  {
    digitalWrite(clockPin, HIGH); //clock goes high
    delayMicroseconds(shortdelay); // 
    inputstream =digitalRead(inputPin); // read one bit of data from pin
    //Serial.print(inputstream, DEC);
    packeddata = ((packeddata << 1) + inputstream);// left-shift summing variable, add pin value
    digitalWrite(clockPin, LOW);
    delayMicroseconds(shortdelay); // end of one clock cycle
  }
  // end of entire clock cycle
  //Serial.println(" ");
  digitalWrite(ledPin, LOW); // signal end of transmission
  // lots of diagnostics for verifying bitwise operations
  //Serial.print("packed:");
  //Serial.println(packeddata,DEC);
  //Serial.print("pack bin: ");
  // Serial.println(packeddata,BIN);
  angle = packeddata & anglemask; // mask rightmost 6 digits of packeddata to zero, into angle.
  //Serial.print("mask: ");
  //Serial.println(anglemask, BIN);
  //Serial.print("bin angle:");
  //Serial.println(angle, BIN);
  //Serial.print("angle: ");
  //Serial.println(angle, DEC);
  angle = (angle >> 6); // shift 16-digit angle right 6 digits to form 10-digit value
  //Serial.print("angleshft:");
  //Serial.println(angle, BIN);
  //Serial.print("angledec: ");
  //Serial.println(angle, DEC);
  angle = angle * 0.3515; // angle * (360/1024) == actual degrees
  //Serial.print("angle: "); // and, finally, print it.
  //Serial.println(angle, DEC);
  //Serial.println("--------------------");
  //Serial.print("raw: "); // this was the prefix for the bit-by-bit diag output inside the loop.
  if (debug)
  {
    statusbits = packeddata & statusmask;
    DECn = statusbits & 2; // goes high if magnet moved away from IC
    INCn = statusbits & 4; // goes high if magnet moved towards IC
    LIN = statusbits & 8; // goes high for linearity alarm
    COF = statusbits & 16; // goes high for cordic overflow: data invalid
    OCF = statusbits & 32; // this is 1 when the chip startup is finished.
    if (DECn && INCn) { Serial.println("magnet moved out of range"); }
    else
    {
      if (DECn) { Serial.println("magnet moved away from chip"); }
      if (INCn) { Serial.println("magnet moved towards chip"); }
    }
    if (LIN) { Serial.println("linearity alarm: magnet misaligned? Data questionable."); }
    if (COF) { Serial.println("cordic overflow: magnet misaligned? Data invalid."); }
  }

  return (double) angle;

  packeddata = 0; // reset both variables to zero so they don't just accumulate
  angle = 0;
}

void changeAutoTune()
{
  if(!tuning)
  {
    //Set the output to the desired starting frequency.
    motorSpeed=aTuneStartValue;
    aTune.SetNoiseBand(aTuneNoise);
    aTune.SetOutputStep(aTuneStep);
    aTune.SetLookbackSec((int)aTuneLookBack);
    AutoTuneHelper(true);
    tuning = true;
  }
  else
  { //cancel autotune
    aTune.Cancel();
    tuning = false;
    AutoTuneHelper(false);
  }
}

void AutoTuneHelper(boolean start)
{
  if(start)
    ATuneModeRemember = myPID.GetMode();
  else
    myPID.SetMode(ATuneModeRemember);
}
