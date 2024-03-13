#include <Arduino.h>
#include <Encoder.h>

// Threading times                                              //  libraries scheduler seem to need different CPUs, and protothread seemed to involved, so just did a simple wait schedule with anti-stravation
static int WaitForUnitToComplete = 4001;       // so far it looks like the Pioneer might need 40msec to respond to the event
//static int WaitForDisplayTime = 850;         // was 650        // this should be the minimum time to display the screen
//static int WaitTimeForBetweenScreens = 4100; // this should be the minimum time for the volume screen to remove after no other commands have been sent
unsigned long nextCommandTime = 0;

long lastActionMillis = 0;
bool needsReset = false;
long debounceMillis = WaitForUnitToComplete + 1;

// Pins for rotatary encoder
#define clkPin D5
#define dtPin D6
#define swPin D7

Encoder myEnc(dtPin, clkPin);

// Multiplexer input pins
#define m1 D2
#define m2 D3
#define m3 D4

int controlPins[] = {m1, m2, m3};

// Commands.  Map of commands to multiplexer channels.  (With comment for the resistors that should be on that channel.)
#define DEFAULT_PIN 0             // 100 kOhm
#define ATT 1                 // 3.5 kOhm
#define DISPLAY_SONG_TAG 2    // 5.75 kOhm
#define NEXT 3                // 8 kOhm
#define PREV 4                // 11.25 kOhm
#define VOLUME_UP 5           // 16 kOhm
#define VOLUME_DOWN 6         // 24 kOhm
#define BAND 7                // 62.75 kOhm

int nextCommand = -1;
int lastCommand = -100;

int Channels[8][3] = {          // TODO - add a 4th column, with the delay for each command type
    {0, 0, 0}, // channel 0
    {1, 0, 0}, // channel 1
    {0, 1, 0}, // channel 2
    {1, 1, 0}, // channel 3
    {0, 0, 1}, // channel 4
    {1, 0, 1}, // channel 5
    {0, 1, 1}, // channel 6
    {1, 1, 1}, // channel 7
};


//  commands
void PulseVolumeUp()
{
  if (millis() - lastActionMillis < debounceMillis || needsReset)
  {
    // Debounce
    return;
  }

  nextCommand = VOLUME_UP;
  lastActionMillis = millis();
  Serial.println("PULSE-UP");
}

void PulseVolumeDown()
{
  if (millis() - lastActionMillis < debounceMillis || needsReset)
  {
    // Debounce
    return;
  }

  nextCommand = VOLUME_DOWN;
  lastActionMillis = millis();
  Serial.println("PULSE-DOWN");
}

void PulseMute(void)
{
  if (millis() - lastActionMillis < debounceMillis || needsReset)
  {
    // Debounce
    return;
  }

  nextCommand = ATT;
  lastActionMillis = millis();
  Serial.println("PULSE-MUTE");
}


ICACHE_RAM_ATTR void buttonPressed()
{
  PulseMute();
}


int encoderValue = 0;
int lastEncoderValue = 0;

void evalEncoder()
{
  encoderValue = myEnc.read();

  if (encoderValue == lastEncoderValue)
  {
    return;
  }

  if (encoderValue - lastEncoderValue > 0)
  {
    PulseVolumeUp();
    lastEncoderValue = encoderValue;
  }
  else if (encoderValue - lastEncoderValue < 0)
  {
    PulseVolumeDown();
    lastEncoderValue = encoderValue;
  }
}

unsigned long timestamp2 = 0;

void evalMultiplexer()
{
  if (millis() < nextCommandTime)
  {
    return;
  }

  if (nextCommand == lastCommand)
  {
    return;
  }

  if (nextCommand < DEFAULT_PIN)
  {
    nextCommand = DEFAULT_PIN;
  }

  // Send the command

  Serial.println("Sending command: " + String(nextCommand));

  // Set multiplexer to the channel with the resistance for the command we want.
  for (int i = 0; i < 3; i++)
  {
    digitalWrite(controlPins[i], Channels[nextCommand][i]);
  }

  if (nextCommand == DEFAULT_PIN)
  {
    needsReset = false;
  }
  else
  {
    needsReset = true;
  }

  // Set multiplexer back to the default channel 0 for the next go-round
  lastCommand = nextCommand;
  nextCommand = DEFAULT_PIN;
  nextCommandTime = millis() + WaitForUnitToComplete;
}

void setup()
{
  // Setup pins for pushbutton on Encoder
  pinMode(swPin, INPUT_PULLUP);
  attachInterrupt(swPin, buttonPressed, RISING);

  // Setup pins for multiplexer
  pinMode(m1, OUTPUT);
  pinMode(m2, OUTPUT);
  pinMode(m3, OUTPUT);

  Serial.begin(9600);
  Serial.println();
}

void loop()
{
  evalEncoder();
  evalMultiplexer();

  delay(1);
}
