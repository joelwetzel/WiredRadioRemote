#include <Arduino.h>
#include <Encoder.h>
#include "X9C.h"
#include "pt.h"

// Pins for Rotatary Encoder
#define             clkPin                         D2
#define             dtPin                          D1
#define             swPin                          D3

// Pins for digital Pot
#define             CS                             D4
#define             UD                             D5
#define             INC                            D6

// 20200714 -- you must now place the defines for anything that causes the Pioneer VolumeScreen to come up first and contiguous so that SCREENRANGE is < all of them
#define VOLUMEUP      1
#define VOLUMEDOWN    2
#define MUTE          3         // mute can be implemented as the button MUTE or PLAY/PAUSE which would allow Navigation to continue, I usually opt for that method
#define SCREENRANGE   4

#define TRACKFF       4
#define TRACKPV       5
#define TRIPLECLICK   6       // unsure what this does, mostly testing right now

// in the case of the X9C pot, its a percentage and the 104 is 100K with 100 steps so each step is 1000 Ohm or 1K so you just set the percentage directly
//

#define REST_VOLUMEUP                              16
#define REST_VOLUMEDOWN                            24
#define REST_TRACKFF                               7
#define REST_MUTE                                  3.5
#define REST_TRACKPV                               10
#define REST_TRIPLECLICK                           0

// time vars
static int          MinSliceDelay                 = 1;
static int          DeBounceDelay                 = 10;          // this is hardware/software loop centric, for my setup and loop() 1 seems to be working well


// Threading times                                              //  libraries scheduler seem to need different CPUs, and protothread seemed to involved, so just did a simple wait schedule with anti-stravation
static int          WaitForUnitToComplete         = 41;         // so far it looks like the Pioneer might need 40msec to respond to the event
static int          WaitForDisplayTime            = 850;        // was 650        // this should be the minimum time to display the screen
static int          WaitTimeForBetweenScreens     = 4100;       // this should be the minimum time for the volume screen to remove after no other commands have been sent

// ProtoThreads
static struct pt pt1, pt2;                                      // 2 threads, the encodes pt1 thread, and the writing of commands (the POT setter) pt2

// ProtoThread Queue
#define               QUEUEMAXSIZE  200
int                   QueueCommands[QUEUEMAXSIZE];
int                   QueueIndex=0;
int                   QueueLastProc=0;
static int            LoopOfThread                  = 0;
static int            CurLimit                      = 0;

Encoder myEnc(dtPin, clkPin);

X9C pot;  //  100 KΩ

int counter = 0;
int lastVolumeCount = 0;

int volume = 0;

long buttonPressMillis = 0;

void IncreaseQueueIndex()
{
    QueueIndex++;
    if (QueueIndex > QUEUEMAXSIZE)
      QueueIndex=0;
}

void ClearQueue()
{
  //init the queue, (not really needed but just in case)
  static int clearloop=0;
  CurLimit=0;
  LoopOfThread=0;
  QueueIndex=0;
  QueueLastProc=0;
  for (clearloop=0;clearloop<=QUEUEMAXSIZE;clearloop++)
     QueueCommands[clearloop]=0;

}

//  commands
void PulseVolumeUp()
{
  QueueCommands[QueueIndex]=VOLUMEUP;
  IncreaseQueueIndex();
  Serial.println("PULSE-UP");
}
void PulseVolumeDown()
{
  QueueCommands[QueueIndex]=VOLUMEDOWN;
  IncreaseQueueIndex();
  Serial.println("PULSE-DOWN");
}
void PulseTrackForward(void)
{
  QueueCommands[QueueIndex]=TRACKFF;
  IncreaseQueueIndex();
  Serial.println("PULSE-FF");
}
void PulseTrackBack(void)
{
  QueueCommands[QueueIndex]=TRACKPV;
  IncreaseQueueIndex();
  Serial.println("PULSE-PV");
}
void PulseMute(void)
{
  QueueCommands[QueueIndex]=MUTE;
  IncreaseQueueIndex();
  Serial.println("PULSE-MUTE");
}
void PulseTripleClick(void)
{
  QueueCommands[QueueIndex]=TRIPLECLICK;
  IncreaseQueueIndex();
  Serial.println("PULSE-TRIPLE");
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
  static unsigned long timestamp            = 0;

  PT_BEGIN(pt);

  while(1)
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

    timestamp = millis(); PT_WAIT_UNTIL(pt, millis() - timestamp > MinSliceDelay);                        // allow other thread some time
  }

  PT_END(pt);
}


static int protothread2(struct pt *pt)
{
  static unsigned long  timestamp                      = 0;
  static uint16_t       Command                        = 0;
  static unsigned long  PreviousCommandTimeStamp       = 0;
  static bool           WaitForDisplay                 = false;
  static unsigned long  TimeWhenInQueue                = 0;
  static bool           InsideAQueueProcess            = false;
  static int            TempCommand                    = 0;

  PT_BEGIN(pt);

  while(1)
  {
    Command=0;
    CurLimit=QueueIndex;
    if (CurLimit<QueueLastProc)
    {
      CurLimit=CurLimit+QUEUEMAXSIZE;
    }

    if (QueueLastProc != CurLimit)
    {
      Serial.println((String)"QueueIndex="+QueueIndex+" QueueLastProc="+QueueLastProc+ " CurLimit="+CurLimit+" LoopOfThread="+LoopOfThread);
      for (LoopOfThread = QueueLastProc; LoopOfThread < CurLimit; LoopOfThread++)
      {
          Command=0;
          switch(QueueCommands[LoopOfThread%QUEUEMAXSIZE])
          {
            case VOLUMEUP:
              Command = REST_VOLUMEUP;
              Serial.print("UP ");
              break;
            case VOLUMEDOWN:
              Command = REST_VOLUMEDOWN;
              Serial.print("DOWN ");
              break;
            case TRACKFF:
              Command = REST_TRACKFF;
              Serial.print("FF ");
              break;
            case TRACKPV:
              Command = REST_TRACKPV;
              Serial.print("PV ");
              break;
            case MUTE:
              Command = REST_MUTE;
              Serial.print("MUTE ");
              break;
            case 0:
              Serial.println("I hit 0");
              Command = 0;
              break;
            default:
              Serial.println((String)"Dont think I should hit these QueueIndex="+QueueIndex+" QueueLastProc="+QueueLastProc+ " CurLimit="+CurLimit);
              Command=0;
              timestamp = millis(); PT_WAIT_UNTIL(pt, millis() - timestamp > DeBounceDelay);
              break;
          }
          TempCommand = QueueCommands[LoopOfThread%QUEUEMAXSIZE];
          QueueCommands[LoopOfThread%QUEUEMAXSIZE]=0;

          if (Command != 0)
          {
              timestamp = millis();
              if (timestamp-PreviousCommandTimeStamp > WaitTimeForBetweenScreens && !InsideAQueueProcess && TempCommand < SCREENRANGE)    // SCREENRANGE must be +1 then ALL display impacting cases
              {
                PreviousCommandTimeStamp = timestamp;
                WaitForDisplay=true;
              }

              InsideAQueueProcess=true;
              TimeWhenInQueue = millis();


              pot.setPot(Command,false);
              timestamp = millis(); PT_WAIT_UNTIL(pt, millis() - timestamp > WaitForUnitToComplete);                       // allow stereo time to handle the input
              Serial.println("CommandDone");

              pot.setPotMax(true);
              timestamp = millis(); PT_WAIT_UNTIL(pt, millis() - timestamp > WaitForUnitToComplete);                       // allow stereo time to handle the input

              if (WaitForDisplay)
              {
                  Serial.println("Waiting for Screen");
                  WaitForDisplay = false;
                  timestamp = millis(); PT_WAIT_UNTIL(pt, millis() - timestamp > WaitForDisplayTime);                // allow stereo time to handle the input
                  Serial.println("Screen should be up, do any other commands");
              }
          }
          Command=0;
      } // when the for-next loop is done (all the commands on the queue)
      QueueLastProc=CurLimit%QUEUEMAXSIZE;
    }
    else // if you are here there was no messages in the queue
    {
      timestamp = millis();
      if (timestamp-TimeWhenInQueue > WaitTimeForBetweenScreens)
      {
          if (InsideAQueueProcess)
            Serial.println("The screen is no longer on ");
          InsideAQueueProcess=false;
      }
    }
    timestamp = millis(); PT_WAIT_UNTIL(pt, millis() - timestamp > MinSliceDelay);                                // allow other thread some time
  }
  PT_END(pt);
}


void setup()
{
  // Setup pushbutton on Encoder
  pinMode(swPin, INPUT_PULLUP);
  attachInterrupt(swPin, buttonPressed, RISING);

  // setup POT
  pot.begin(CS, INC, UD);
  delay(1);
  pot.setPotMax(true);
  delay(WaitForUnitToComplete);

  // clear the queue, I believe this can be removed since the units memory starts zero'ed, (I remember reading that somewhere)
  ClearQueue();

  Serial.begin(9600);
  Serial.println();
}

void loop()
{
  protothread1(&pt1);
  protothread2(&pt2);

  //noInterrupts();

  // counter = myEnc.read();

  // if (counter - lastVolumeCount > 1)
  // {
  //   volume++;
  //   if (volume > 20)
  //   {
  //     volume = 20;
  //   }
  //   Serial.println(volume);
  //   lastVolumeCount = counter;
  // }
  // else if (counter - lastVolumeCount < -1)
  // {
  //   volume--;
  //   if (volume < 0)
  //   {
  //     volume = 0;
  //   }
  //   Serial.println(volume);
  //   lastVolumeCount = counter;
  // }

  // if (c > 0)
  // {
  //   pot.decr();
  // }
  // else
  // {
  //   pot.incr();
  // }

  // delay(1000);

  // c++;

  // if (c > 30)
  // {
  //   c = -30;
  // }

  //interrupts();
}
