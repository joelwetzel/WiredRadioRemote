#include <Arduino.h>
#include <Encoder.h>
#include "pt.h"

// Pins for rotatary encoder
#define clkPin D2
#define dtPin D1
#define swPin D3

// Multiplexer input pins
#define m1 D4
#define m2 D5
#define m3 D6

// Commands.  Map of commands to multiplexer channels.  (With comment for the resistors that should be on that channel.)
#define DEFAULT 0             // 100 kOhm
#define ATT 1                 // 3.5 kOhm
#define DISPLAY_SONG_TAG 2    // 5.75 kOhm
#define NEXT 3                // 8 kOhm
#define PREV 4                // 11.25 kOhm
#define VOLUME_UP 5           // 16 kOhm
#define VOLUME_DOWN 6         // 24 kOhm
#define BAND 7                // 62.75 kOhm

// time vars
static int MinSliceDelay = 1;

// Threading times                                              //  libraries scheduler seem to need different CPUs, and protothread seemed to involved, so just did a simple wait schedule with anti-stravation
static int WaitForUnitToComplete = 41;       // so far it looks like the Pioneer might need 40msec to respond to the event
//static int WaitForDisplayTime = 850;         // was 650        // this should be the minimum time to display the screen
//static int WaitTimeForBetweenScreens = 4100; // this should be the minimum time for the volume screen to remove after no other commands have been sent

// ProtoThreads
static struct pt pt1, pt2; // 2 threads, the encodes pt1 thread, and the writing of commands (the multiplexer setter) pt2

Encoder myEnc(dtPin, clkPin);

long buttonPressMillis = 0;

int controlPins[] = {m1, m2, m3};

int nextCommand = -1;

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

void setMultiplexer(int command)
{
  if (command < 0 || command > 7)
  {
    return;
  }

  // Set multiplexer to the channel with the resistance for the command we want.
  for (int i = 0; i < 3; i++)
  {
    digitalWrite(controlPins[i], Channels[command][i]);
  }
}

//  commands
void PulseVolumeUp()
{
  nextCommand = VOLUME_UP;
  Serial.println("PULSE-UP");
}
void PulseVolumeDown()
{
  nextCommand = VOLUME_DOWN;
  Serial.println("PULSE-DOWN");
}
void PulseTrackForward(void)
{
  nextCommand = NEXT;
  Serial.println("PULSE-FF");
}
void PulseTrackBack(void)
{
  nextCommand = PREV;
  Serial.println("PULSE-PV");
}
void PulseMute(void)
{
  nextCommand = ATT;
  Serial.println("PULSE-MUTE");
}

ICACHE_RAM_ATTR void buttonPressed()
{
  long now = millis();

  if (now - buttonPressMillis < 100)
  {
    // Debounce
    return;
  }

  PulseMute();

  buttonPressMillis = millis();
}

static int protothread1(struct pt *pt)
{
  static unsigned long timestamp = 0;
  int counter = 0;
  int lastVolumeCount = 0;

  PT_BEGIN(pt);

  while (1)
  {
    counter = myEnc.read();

    if (counter - lastVolumeCount > 1)
    {
      PulseVolumeUp();
      lastVolumeCount = counter;
    }
    else if (counter - lastVolumeCount < -1)
    {
      PulseVolumeDown();
      lastVolumeCount = counter;
    }

    timestamp = millis();
    PT_WAIT_UNTIL(pt, millis() - timestamp > MinSliceDelay); // allow other thread some time
  }

  PT_END(pt);
}

static int protothread2(struct pt *pt)
{
  static unsigned long timestamp = 0;

  PT_BEGIN(pt);

  while (1)
  {
    if (nextCommand < 0 || nextCommand > 7)
    {
      nextCommand = DEFAULT;
    }

    // Send the command
    setMultiplexer(nextCommand);

    // Wait long enough for the stereo to process the command
    timestamp = millis();
    PT_WAIT_UNTIL(pt, millis() - timestamp > WaitForUnitToComplete);

    // Set multiplexer back to the default channel 0 for the next go-round
    nextCommand = DEFAULT;
  }
  PT_END(pt);
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
  protothread1(&pt1);
  protothread2(&pt2);
}
