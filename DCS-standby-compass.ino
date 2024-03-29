// this code came 99.9% from https://gist.github.com/jboecker/1084b3768c735b164c34d6087d537c18
// I modifed it based on my specific hardware - see BOTTOM of this code for the place to change
// pins, max step count, etc.

// Uses the AccelStepper and DCSBios libraries
// hardware I used: x27-168, EasyDriver board with 12v for motor, Arduino nano, 3D printed parts
// I copied this design/process: https://www.youtube.com/watch?v=ZN9glqgp9TY&ab_channel=TheWarthogProject

#define DCSBIOS_IRQ_SERIAL

#include <AccelStepper.h>
#include "DcsBios.h"

struct StepperConfig {
  unsigned int maxSteps;
  unsigned int acceleration;
  unsigned int maxSpeed;
};


class Vid60Stepper : public DcsBios::Int16Buffer {
  private:
    AccelStepper& stepper;
    StepperConfig& stepperConfig;
    inline bool zeroDetected() { return digitalRead(irDetectorPin) == 1; }
    unsigned int (*map_function)(unsigned int);
    unsigned char initState;
    long currentStepperPosition;
    long lastAccelStepperPosition;
    unsigned char irDetectorPin;
    long zeroOffset;
    bool movingForward;
    bool lastZeroDetectState;

    long normalizeStepperPosition(long pos) {
      if (pos < 0) return pos + stepperConfig.maxSteps;
      if (pos >= stepperConfig.maxSteps) return pos - stepperConfig.maxSteps;
      return pos;
    }

    void updateCurrentStepperPosition() {
      // adjust currentStepperPosition to include the distance our stepper motor
      // was moved since we last updated it
      long movementSinceLastUpdate = stepper.currentPosition() - lastAccelStepperPosition;
      currentStepperPosition = normalizeStepperPosition(currentStepperPosition + movementSinceLastUpdate);
      lastAccelStepperPosition = stepper.currentPosition();
    }
  public:
    Vid60Stepper(unsigned int address, AccelStepper& stepper, StepperConfig& stepperConfig, unsigned char irDetectorPin, long zeroOffset, unsigned int (*map_function)(unsigned int))
    : Int16Buffer(address), stepper(stepper), stepperConfig(stepperConfig), irDetectorPin(irDetectorPin), zeroOffset(zeroOffset), map_function(map_function), initState(0), currentStepperPosition(0), lastAccelStepperPosition(0) {
    }

    virtual void loop() {
      if (initState == 0) { // not initialized yet
        pinMode(irDetectorPin, INPUT);
        stepper.setMaxSpeed(stepperConfig.maxSpeed);
        stepper.setSpeed(600);
        
        initState = 1;
      }
      if (initState == 1) {
        // move off zero if already there so we always get movement on reset
        // (to verify that the stepper is working)
        if (zeroDetected()) {
          stepper.runSpeed();
        } else {
            initState = 2;
        }
      }
      if (initState == 2) { // zeroing
        if (!zeroDetected()) {
          stepper.runSpeed();
        } else {
            stepper.setAcceleration(stepperConfig.acceleration);
            stepper.runToNewPosition(stepper.currentPosition() + zeroOffset);
            // tell the AccelStepper library that we are at position zero
            stepper.setCurrentPosition(0);
            lastAccelStepperPosition = 0;
            // set stepper acceleration in steps per second per second
            // (default is zero)
            stepper.setAcceleration(stepperConfig.acceleration);
            
            lastZeroDetectState = true;
            initState = 3;
        }
      }
      if (initState == 3) { // running normally
        
        // recalibrate when passing through zero position
        bool currentZeroDetectState = zeroDetected();
        if (!lastZeroDetectState && currentZeroDetectState && movingForward) {
          // we have moved from left to right into the 'zero detect window'
          // and are now at position 0
          lastAccelStepperPosition = stepper.currentPosition();
          currentStepperPosition = normalizeStepperPosition(zeroOffset);
        } else if (lastZeroDetectState && !currentZeroDetectState && !movingForward) {
          // we have moved from right to left out of the 'zero detect window'
          // and are now at position (maxSteps-1)
          lastAccelStepperPosition = stepper.currentPosition();
          currentStepperPosition = normalizeStepperPosition(stepperConfig.maxSteps + zeroOffset);
        }
        lastZeroDetectState = currentZeroDetectState;
        
        
        if (hasUpdatedData()) {
            // convert data from DCS to a target position expressed as a number of steps
            long targetPosition = (long)map_function(getData());

            updateCurrentStepperPosition();
            
            long delta = targetPosition - currentStepperPosition;
            
            // if we would move more than 180 degree counterclockwise, move clockwise instead
            if (delta < -((long)(stepperConfig.maxSteps/2))) delta += stepperConfig.maxSteps;
            // if we would move more than 180 degree clockwise, move counterclockwise instead
            if (delta > (stepperConfig.maxSteps/2)) delta -= (long)stepperConfig.maxSteps;

            movingForward = (delta >= 0);
            
            // tell AccelStepper to move relative to the current position
            stepper.move(delta);
            
        }
        stepper.run();
      }
    }
};

//==================================================================================
/* modify below this line */

/* define stepper parameters
   multiple Vid60Stepper instances can share the same StepperConfig object */
struct StepperConfig stepperConfig = {
  5760,  // maxSteps - 720*8 seems to test well in the sim - when I had 720 the motor would turn too slow, 
         // b/c it is in 1/8 step mode by default - to get full step mode you have to set special pins 
         // on the driver board, MS1 or MS2 - I had to ground those - but I ended up 
         // staying in 1/8 mode and going with *8 max step count - 2 less wires to solder
  8000, // maxSpeed - not sure what this should really be, seems to work
  80000 // acceleration - not sure what this should really be, seems to work
  };

#define MOTOR_STEP_PIN 2
#define MOTOR_DIR_PIN 3 // if direction ends up backwards you can swap the wires that drive one 
                        // coil of the motor, or use stepper.setPinsInverted( true ); in the setup method below
#define IR_DETECT_PIN 9

#define READY_LED_PIN 7
#define LATCHED_LED_PIN 6
#define DISCONNECT_LED_PIN 8

// define AccelStepper instance
AccelStepper stepper(AccelStepper::DRIVER, MOTOR_STEP_PIN, MOTOR_DIR_PIN);

// define Vid60Stepper class that uses the AccelStepper instance defined in the line above
Vid60Stepper vid60(          0x104c,          // address of stepper data - HSI HEADING, pulled from DCS-BIOS docs
                             stepper,         // name of AccelStepper instance
                             stepperConfig,   // StepperConfig struct instance
                             IR_DETECT_PIN,              // IR Detector Pin (must be HIGH in zero position)
                             0,               // zero offset
                             [](unsigned int newValue) -> unsigned int {
  /* this function needs to map newValue to the correct number of steps */
  return map(newValue, 0, 65535, 0, stepperConfig.maxSteps-1);
});

DcsBios::LED airRefuelReady(0x1012, 0x8000, READY_LED_PIN);
DcsBios::LED airRefuelLatched(0x1026, 0x0100, LATCHED_LED_PIN);
DcsBios::LED airRefuelDisconnect(0x1026, 0x0200, DISCONNECT_LED_PIN);

void setup() {
  DcsBios::setup();
  stepper.setPinsInverted( true ); // i happened to wire the motor backwards
  pinMode(13, OUTPUT); // sets LED on the nano to be used?  Not sure this is used at all
}

void loop() {
  PORTB |= (1<<5);
  PORTB &= ~(1<<5);

  DcsBios::loop();
}
