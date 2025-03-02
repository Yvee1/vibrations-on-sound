// Audio Spectrum Display
// Copyright 2013 Tony DiCola (tony@tonydicola.com)

// This code is part of the guide at http://learn.adafruit.com/fft-fun-with-fourier-transforms/

#define ARM_MATH_CM4
#include <arm_math.h>


////////////////////////////////////////////////////////////////////////////////
// CONIFIGURATION 
// These values can be changed to alter the behavior of the spectrum display.
////////////////////////////////////////////////////////////////////////////////

int SAMPLE_RATE_HZ = 10000;             // Sample rate of the audio in hertz.
float SPECTRUM_MIN_DB = 60.0;          // Audio intensity (in decibels) that maps to low LED brightness.
float SPECTRUM_MAX_DB = 70.0;          // Audio intensity (in decibels) that maps to high LED brightness.
int VIBRATION_ENABLED = 1;             // Control if vibration motors should vibrate or not.  1 is true, 0 is false.
                                       // Useful for turning the LED display on and off with commands from the serial port.
const int FFT_SIZE = 256;              // Size of the FFT.  Realistically can only be at most 256 
                                       // without running out of memory for buffers and other state.
const int AUDIO_INPUT_PIN = A0;        // Input ADC pin for audio data.
const int ANALOG_READ_RESOLUTION = 10; // Bits of resolution for the ADC.
const int ANALOG_READ_AVERAGING = 16;  // Number of samples to average with each ADC reading.
const int POWER_LED_PIN = 13;          // Output pin for power LED (pin 13 to use Teensy 3.0's onboard LED).
const int POT_PIN = 23;                // Input pin of potentiometer determining cutoff amplitude

const int VIBRATION_COUNT = 4;         // Number of vibration motors.  You should be able to increase this without
                                       // any other changes to the program.
const int START_PIN = 3;              // Vibration motors starting at this pin, and counting up
const int MAX_CHARS = 65;              // Max size of the input command buffer

const bool CUSTOM_RANGE = true;       // Whether we specify our own frequency windows
float customRange[VIBRATION_COUNT*2] = {750, 4000, 60, 130, 60, 130, 300, 750}; 
float amplificationFactor[VIBRATION_COUNT] = {3, 1, 1, 1}; // Manually amplify respecitve frequency ranges

////////////////////////////////////////////////////////////////////////////////
// INTERNAL STATE
// These shouldn't be modified unless you know what you're doing.
////////////////////////////////////////////////////////////////////////////////

IntervalTimer samplingTimer;
float samples[FFT_SIZE*2];
float magnitudes[FFT_SIZE];
int sampleCounter = 0;

char commandBuffer[MAX_CHARS];
float frequencyWindow[VIBRATION_COUNT+1];

////////////////////////////////////////////////////////////////////////////////
// MAIN SKETCH FUNCTIONS
////////////////////////////////////////////////////////////////////////////////

void setup() {
  // Set up serial port.
  Serial.begin(38400);
  
  // Set up ADC and audio input.
  pinMode(AUDIO_INPUT_PIN, INPUT);
  analogReadResolution(ANALOG_READ_RESOLUTION);
  analogReadAveraging(ANALOG_READ_AVERAGING);
  
  // Turn on the power indicator LED.
  pinMode(POWER_LED_PIN, OUTPUT);
  digitalWrite(POWER_LED_PIN, HIGH);

  // Pot meter
//  pinMode(POT_PIN, INPUT);

  // Make vibration motor output pins starting at 3.
  for (int i = 0; i < VIBRATION_COUNT; i++){
    pinMode(START_PIN+i, OUTPUT);
    setMotors(i, 0.);
  }
  
  // Clear the input command buffer
  memset(commandBuffer, 0, sizeof(commandBuffer));
  
  // Calculate frequency windows if needed
  if (!CUSTOM_RANGE){
    divideEvenly();
  }
  
  // Begin sampling audio
  samplingBegin();
}

void loop() {
  // Calculate FFT if a full sample is available.
  if (samplingIsDone()) {
    // Run FFT on sample data.
    arm_cfft_radix4_instance_f32 fft_inst;
    arm_cfft_radix4_init_f32(&fft_inst, FFT_SIZE, 0, 1);
    arm_cfft_radix4_f32(&fft_inst, samples);
    // Calculate magnitude of complex numbers output by the FFT.
    arm_cmplx_mag_f32(samples, magnitudes, FFT_SIZE);
  
    if (VIBRATION_ENABLED == 1)
    {
      spectrumLoop();
    }
  
    // Restart audio sampling.
    samplingBegin();
  }

    
  // Parse any pending commands.
  parserLoop();
}


////////////////////////////////////////////////////////////////////////////////
// UTILITY FUNCTIONS
////////////////////////////////////////////////////////////////////////////////

// Compute the average magnitude of a target frequency window vs. all other frequencies.
void windowMean(float* magnitudes, int lowBin, int highBin, float* windowMean, float* otherMean) {
    *windowMean = 0;
    *otherMean = 0;
    // Notice the first magnitude bin is skipped because it represents the
    // average power of the signal.
    for (int i = 1; i < FFT_SIZE/2; ++i) {
      if (i >= lowBin && i <= highBin) {
        *windowMean += magnitudes[i];
      }
      else {
        *otherMean += magnitudes[i];
      }
    }
    *windowMean /= (highBin - lowBin) + 1;
    *otherMean /= (FFT_SIZE / 2 - (highBin - lowBin));
}

// Convert a frequency to the appropriate FFT bin it will fall within.
int frequencyToBin(float frequency) {
  float binFrequency = float(SAMPLE_RATE_HZ) / float(FFT_SIZE);
  return int(frequency / binFrequency);
}

////////////////////////////////////////////////////////////////////////////////
// SPECTRUM DISPLAY FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

void divideEvenly() {
  // Set the frequency window values by evenly dividing the possible frequency
  // spectrum across the number of vibration motors.
  float windowSize = (SAMPLE_RATE_HZ / 2.0) / float(VIBRATION_COUNT);
  for (int i = 0; i < VIBRATION_COUNT+1; ++i) {
    frequencyWindow[i] = i*windowSize;
  }
}

void spectrumLoop() {
  // Update each vibration motor based on the intensity of the audio 
  // in the associated frequency window.
  float intensity, otherMean;
  for (int i = 0; i < VIBRATION_COUNT; ++i) {
    if (!CUSTOM_RANGE){
      windowMean(magnitudes, 
                 frequencyToBin(frequencyWindow[i]),
                 frequencyToBin(frequencyWindow[i+1]),
                 &intensity,
                 &otherMean);
    } else {
      windowMean(magnitudes, 
                 frequencyToBin(customRange[2*i]),
                 frequencyToBin(customRange[2*i+1]),
                 &intensity,
                 &otherMean);
    }
    // Convert intensity to decibels.
    intensity *= amplificationFactor[i];
    intensity = 20.0*log10(intensity);
    // Scale the intensity and clamp between 0 and 1.0.
    intensity -= SPECTRUM_MIN_DB;
    intensity = intensity < 0.0 ? 0.0 : intensity;
    intensity /= (SPECTRUM_MAX_DB-SPECTRUM_MIN_DB);
    intensity = intensity > 1.0 ? 1.0 : intensity;
    setMotors(i, intensity);

    Serial.print(i);
    Serial.print(" | ");
    Serial.print("[");
    if (!CUSTOM_RANGE){
      Serial.print(frequencyWindow[i]);
      Serial.print(", ");
      Serial.print(frequencyWindow[i+1]);
    } else{
      Serial.print(customRange[2*i]);
      Serial.print(", ");
      Serial.print(customRange[2*i+1]);
    }
    Serial.print("]  : ");
//    if (i == 0){
    Serial.println(intensity);
//    }
//    Serial.println(magnitudes[100]);
  }
}

void setMotors(int i, float intensity){
  // Do PWM based on intensity
//  analogWrite(i+START_PIN, intensity * 255);
 

  // Cutoff value
//  float cutoff = map(analogRead(POT_PIN), 0, 1024, 0.2, 0.8);
  float cutoff = 0.5;
  boolean on = intensity > cutoff;
  digitalWrite(i+START_PIN, on);
}


////////////////////////////////////////////////////////////////////////////////
// SAMPLING FUNCTIONS
////////////////////////////////////////////////////////////////////////////////

void samplingCallback() {
  // Read from the ADC and store the sample data
  samples[sampleCounter] = (float32_t)analogRead(AUDIO_INPUT_PIN);
//  Serial.println(samples[sampleCounter]);
  // Complex FFT functions require a coefficient for the imaginary part of the input.
  // Since we only have real data, set this coefficient to zero.
  samples[sampleCounter+1] = 0.0;
  // Update sample buffer position and stop after the buffer is filled
  sampleCounter += 2;
  if (sampleCounter >= FFT_SIZE*2) {
    samplingTimer.end();
  }
}

void samplingBegin() {
  // Reset sample buffer position and start callback at necessary rate.
  sampleCounter = 0;
  samplingTimer.begin(samplingCallback, 1000000/SAMPLE_RATE_HZ);
}

boolean samplingIsDone() {
  return sampleCounter >= FFT_SIZE*2;
}


////////////////////////////////////////////////////////////////////////////////
// COMMAND PARSING FUNCTIONS
// These functions allow parsing simple commands input on the serial port.
// Commands allow reading and writing variables that control the device.
//
// All commands must end with a semicolon character.
// 
// Example commands are:
// GET SAMPLE_RATE_HZ;
// - Get the sample rate of the device.
// SET SAMPLE_RATE_HZ 400;
// - Set the sample rate of the device to 400 hertz.
// 
////////////////////////////////////////////////////////////////////////////////

void parserLoop() {
  // Process any incoming characters from the serial port
  while (Serial.available() > 0) {
    char c = Serial.read();
    // Add any characters that aren't the end of a command (semicolon) to the input buffer.
    if (c != ';') {
      c = toupper(c);
      strncat(commandBuffer, &c, 1);
    }
    else
    {
      // Parse the command because an end of command token was encountered.
      parseCommand(commandBuffer);
      // Clear the input buffer
      memset(commandBuffer, 0, sizeof(commandBuffer));
    }
  }
}

// Macro used in parseCommand function to simplify parsing get and set commands for a variable
#define GET_AND_SET(variableName) \
  else if (strcmp(command, "GET " #variableName) == 0) { \
    Serial.println(variableName); \
  } \
  else if (strstr(command, "SET " #variableName " ") != NULL) { \
    variableName = (typeof(variableName)) atof(command+(sizeof("SET " #variableName " ")-1)); \
  }

void parseCommand(char* command) {
  if (strcmp(command, "GET MAGNITUDES") == 0) {
    for (int i = 0; i < FFT_SIZE; ++i) {
      Serial.println(magnitudes[i]);
    }
  }
  else if (strcmp(command, "GET SAMPLES") == 0) {
    for (int i = 0; i < FFT_SIZE*2; i+=2) {
      Serial.println(samples[i]);
    }
  }
  else if (strcmp(command, "GET FFT_SIZE") == 0) {
    Serial.println(FFT_SIZE);
  }
  GET_AND_SET(SAMPLE_RATE_HZ)
  GET_AND_SET(VIBRATION_ENABLED)
  GET_AND_SET(SPECTRUM_MIN_DB)
  GET_AND_SET(SPECTRUM_MAX_DB)
  
  // Update spectrum display values if sample rate was changed.
  if (strstr(command, "SET SAMPLE_RATE_HZ ") != NULL && !CUSTOM_RANGE) {
    divideEvenly();
  }
  
  // Turn off the LEDs if the state changed.
  if (VIBRATION_ENABLED == 0) {
    for (int i = 0; i < VIBRATION_COUNT; ++i) {
      setMotors(i, 0.);
    }
  }
}
