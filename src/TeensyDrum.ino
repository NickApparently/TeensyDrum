/*
 * DONE: Auto calculating numOfSlots for EEPROM
 * DONE: Quadratic for LCD brightness
 * DONE: Settings.Data1 and Data2 could be removed from EEPROM if midi cmd is configured on start-up.
 * DONE: settings struct, change order to: command, channel, note value, note velocity, ctrl num, ctrl val, prog num, load slot, threshold, tScan, tMask
 * DONE: numOfSettings by sizeof(struct)
 * DONE: Check channel setting turncation
 * DONE: Names for save slots
 * DONE: Add Control Change Names
 * DONE: Footswitches debounce (masktime)
 * DONE: Switch which settings to edit when hitting different pad if menu is active
 * 
 * TODO: One byte for settings like: Display hits
 * TODO: Different modes of footswitches (polarity/momentary/hold)
 * TODO: Finetune range from soft/hard hits
 * TODO: Receive MIDI messages to change save slots
 * TODO: Receive MIDI messages to change settings
 */

#include <Bounce.h>
#include <EEPROM.h>
#include <LiquidCrystal.h>
LiquidCrystal lcd(4, 5, 6, 7, 8, 9); //RS, Enable, D4, D5, D6, D7

#define Monitor       Serial
#define Midi          Serial1
//Define the number of items, consisting of Pads, External Triggers and Switches
#define numOfPads     6 //The number of pads with Piezo and FSR sensors
#define numOfExtTrig  2 //The number of external triggers with Piezo only
#define numOfSwitch   2 //The number of switches

//Every 'item' (Pad, external trigger, switch) has the following settings. These are saved on the EEPROM.
typedef struct{
  byte command, channel, noteValue, noteVelocity, ctrlNumber, ctrlValue, progNumber, loadSlot, threshold, tScan, tMask;
} setting;

//Midi data for every item, to be sent
typedef struct{
  byte command, data1, data2, channel;
} midiData;

//EEPROM settings
const int  sizeOfEEPROM     = 1080; //Size of Teensy4.0 EEPROM in number of bytes, check with EEPROM.length()
const byte numOfMenuItems   = numOfPads + numOfExtTrig + numOfSwitch;  //Number of menu items. Brightness, Save, load and exit are always there.
const byte numOfSettings    = sizeof(setting); //Number of settings per menu item
const byte bytesPerSlotName = 8; //Number of characters for each name
//Number of save slots for data in EEPROM. Teensy4.0 has 1080 bytes. Bytes required = ((numOfMenuItems * numOfSettings) * numOfSlots) + (bytesPerSlotName * numOfSlots) + 2 = ((6+2+2) * 11 * 9) + (8 * 9) + 2 = 1064 bytes.
const byte numOfSlots       = (sizeOfEEPROM - 2 - ((sizeOfEEPROM - 2) / (numOfMenuItems * numOfSettings) * bytesPerSlotName)) / (numOfMenuItems * numOfSettings);
const int  brightnessAddress      = EEPROM.length() - 2; //1078
const int  lastLoadedSlotAddress  = EEPROM.length() - 1; //1079

//Pin declaration
const byte brightnessPin  = 10;
const byte encNextPin     = 32;
const byte encPrevPin     = 31; //Normally pin 33
const byte encSelectPin   = 13;
const byte footswitchPin[numOfSwitch] = {2, 3};

Bounce FB_encNextPin = Bounce(encNextPin, 2); // 2 ms debounce
Bounce FB_encPrevPin = Bounce(encPrevPin, 2); // 2 ms debounce
Bounce FB_encSelectPin = Bounce(encSelectPin, 10); // 10 ms debounce

bool encNext;
bool encPrev;
bool encSelect;
bool encLongPress; //Only true for one cycle
bool encPressedLong; // true as long as the button is pressed and long press time has passed
bool firstScan = true;
unsigned long pressTime;

//Data for PADs and switches
//Data for compressor
float PADthreshold[numOfPads + numOfExtTrig] = {1023,1023,1023,1023,1023,1023,200,200};
int   PADratio[numOfPads + numOfExtTrig]     = {1,1,1,1,1,1,1,1}; // Default = 1, compressor off
float PADgain[numOfPads + numOfExtTrig]; //Calculated by calcCompressor()

//Data for trigger evaluation
unsigned long PADhitTime[numOfPads + numOfExtTrig];
int   PADvalue[numOfPads + numOfExtTrig];
int   PADmax[numOfPads + numOfExtTrig];
int   FSRvalue[numOfPads];
int   FSRlast[numOfPads];
int   FSRdiff[numOfPads];
bool  PADhit[numOfPads + numOfExtTrig];
bool  PADscan[numOfPads + numOfExtTrig];
bool  PADdisplay[numOfPads + numOfExtTrig];
long  displayHitTime = 200 * 1000; //200 ms
bool  switchPressed[numOfSwitch];
bool  switchState[numOfSwitch];
unsigned long switchChangeTime[numOfSwitch];

int numOfScans[numOfPads + numOfExtTrig]; //Diag
bool plot[numOfPads + numOfExtTrig];//Diag
int tempPin = 2; //Diag

//Data for menu
int   menuItem[5]; //An int for the selected menu item for every menu depth. menuItem[0] is not used.
byte  menuDepth = 0;
bool  menuActive = false;
byte  lastOperatedItem; //Keep track of the last item that has been operated. Pad hit / switch switched.
bool  changeMenuItem; //If an item has been operated, the menu has to be updated.
const String menuText1[numOfMenuItems + 4] = {"Pad 1", "Pad 2", "Pad 3", "Pad 4", "Pad 5", "Pad 6", "Trigger 1", "Trigger 2", "Footswitch 1", "Footswitch 2", "Brightness", "Save Settings", "Load Settings", "Exit Menu"};
const String menuText1Short[numOfMenuItems]= {"PD1", "PD2", "PD3", "PD4", "PD5", "PD6", "TR1", "TR2", "FS1", "FS2"};
const String menuText2[6]  = {"Command", "Channel", "Threshold", "Scan Time", "Mask Time", "Back"};
const String menuText2b[5] = {"Command", "Channel", "Polarity", "Mask Time", "Back"};
const String menuText3[5]  = {"Note On", "Control Change", "Program Change", "Change Load Slt", "Back"};
const String menuText4[11] = {"Note Value", "Note Velocity", "Back", "", "", "Control Number", "Control Value", "Back", "", "", "Program Number"};
const String noteName[12]  = {"C", "C#", "D",  "D#", "E",  "F",  "F#", "G",  "G#", "A",  "A#", "B"};
String controlName[128];

String slotName[numOfSlots + 1] = {"","Slot 1  ", "Empty  ", "Empty   ", "Empty   ", "Empty   ", "Empty   ", "Empty   ", "Empty   ", "Empty   "}; 
char  character;
byte  charIndex;

//14 rows of 12 settings
setting settings[numOfMenuItems + 4] = {   
                   //Cmd , Channel, Note val, Note velo, Ctrl num, Ctrl val, Prog num, loadSlot, Threshold, tScan, tMask
  /*Pad 1*/         {0x90, 0x00,    42,       0,         1,        100,      1,        1,        5,         10,    20   },
  /*Pad 2*/         {0x90, 0x00,    47,       0,         1,        100,      1,        1,        5,         10,    20   },
  /*Pad 3*/         {0x90, 0x00,    49,       0,         2,        100,      1,        1,        5,         10,    20   },
  /*Pad 4*/         {0x90, 0x00,    36,       0,         2,        100,      1,        1,        5,         10,    20   },
  /*Pad 5*/         {0x90, 0x00,    38,       0,         2,        100,      1,        1,        5,         10,    20   },
  /*Pad 6*/         {0x90, 0x00,    43,       0,         2,        100,      1,        1,        5,         10,    20   },
  /*Ext trig 1*/    {0x90, 0x00,    39,       0,         2,        100,      1,        1,        25,        10,    25   },
  /*Ext trig 2*/    {0x90, 0x00,    40,       0,         2,        100,      1,        1,        25,        10,    25   },
                   //Cmd , Channel, Note val, Note velo, Ctrl num, Ctrl val, Prog num, loadSlot, Mode,   Not Used, tMask
  /*Footswitch 1*/  {0xB0, 0x00,    60,       100,       64,       100,      1,        1,        0,         0,     0    },
  /*Footswitch 2*/  {0xB0, 0x00,    60,       100,       65,       100,      1,        1,        0,         0,     0    }};

midiData MidiData[numOfMenuItems];

byte  brightness, //Brightness will be saved on the second to last EEPROM slot (1078)
      lastLoadedSlot, //the last loaded slot will be saved on the last EEPROM slot (1079)
      saveSlot = 1, 
      loadSlot = 1;

//====================================================================================================
//      SETUP
//====================================================================================================
void setup() {
  lcd.begin(16, 2);
  Midi.begin(31250);
  Monitor.begin(9600);

  pinMode(footswitchPin[0], INPUT_PULLUP);
  pinMode(footswitchPin[1], INPUT_PULLUP);
  pinMode(encNextPin, INPUT_PULLUP);
  pinMode(encPrevPin, INPUT_PULLUP);
  pinMode(encSelectPin, INPUT_PULLUP);

  //Read slotnames from EEPROM
  for(int slot = 1; slot < numOfSlots + 1; slot++){
    slotName[slot] = ""; //Empty old slot name
    for(int i = 0; i < 8; i++){ //Read each character from EEPROM and add to slot name
      slotName[slot] = slotName[slot] + (char)EEPROM.read((numOfSettings * numOfMenuItems * numOfSlots) + ((slot - 1) * 8) + i); //Slot names are stored after all the settings
    }
  }

  //Boot sequence
  changeBrightness(100); //Starts at 100% brightness
  lcd.clear();
  lcd.setCursor(3, 0);
  lcd.print("TEENSYDRUM");
  lcd.setCursor(5, 1);
  lcd.print("v 3.03");
  delay(1500); //startup time
  lcd.setCursor(0, 1);
  lcd.print("Loaded: ");
  lcd.print(slotName[EEPROM.read(lastLoadedSlotAddress)]);
  delay(1500);
  lcd.clear();
  load(EEPROM.read(lastLoadedSlotAddress));//Load last stored settings through the menu from EEPROM. Loaded through playing is disgarded.

  brightness = EEPROM.read(brightnessAddress);//Load brightness settings from EEPROM
  changeBrightness(brightness);//Change Brightness to last saved user settings.
  calcCompressor();
  configADC();
  controlNames();

  //Get Switch polarity. If the switch contact is open during startup, the switch is Normally Open, if the switch contact is closed during startup, the switch is Normally Closed.
  for (int pin = 0; pin < numOfSwitch; pin++){
    settings[pin + numOfPads + numOfExtTrig].threshold = !digitalRead(footswitchPin[pin]); //Polarity inverse if mode = 1 (named threshold, sadly)
  }
}

//====================================================================================================
//      MAIN
//====================================================================================================
void loop() {

  checkEnc();

  if (encSelect && !menuActive){
    menuActive = true;
  }

  //Update menu if the menu is Active and one of the following:
  //Encoder turns left or right
  //Encoder pressed or longpress
  //An item is operated (Pad hit or switch switched) and its a different item than the displayed item
  if (menuActive && (encSelect || encPrev || encNext || encLongPress || (changeMenuItem && menuItem[1] != lastOperatedItem))) {
    if (changeMenuItem){
      menuItem[1] = lastOperatedItem;
    }
    changeMenuItem = false;
    menu();  // if menu activate, run menu code
    updateScreen();
  }

  //Get PAD values
  for (int pin = 0; pin < numOfPads; pin++){
    PADvalue[pin] = analogRead(pin);
    FSRvalue[pin] = analogRead(pin+8);
    FSRdiff[pin] = (FSRlast[pin] - FSRvalue[pin] < 0) ? 0 : FSRlast[pin] - FSRvalue[pin];
    FSRlast[pin] = FSRvalue[pin];
  }

  //Get External Trigger values
  for (int pin = 0; pin < numOfExtTrig; pin++){
    PADvalue[pin + numOfPads] = analogRead(pin + numOfPads);
  }

  //Get Switch statusses
  for (int pin = 0; pin < numOfSwitch; pin++){
    switchState[pin] = (settings[pin + numOfPads + numOfExtTrig].threshold > 0) ? digitalRead(footswitchPin[pin]) : !digitalRead(footswitchPin[pin]); //Inverse if mode = 1 (named threshold, sadly)
  }
  
  //PAD and External Trigger evaluation
  for (int pin = 0; pin < numOfPads + numOfExtTrig; pin++){
    if ((pin < numOfPads && PADhit[pin] == false && (FSRdiff[pin] > (settings[pin].threshold * 4)))  //If the FSR value changes more than the threshold and no hit is active then there is a new hit!
    || (pin >= numOfPads && PADhit[pin] == false && (PADvalue[pin] > (settings[pin].threshold * 4)))){//OR if the Piezo value on external triggers is higher than the threshold and no hit is active then there is a new hit!
      PADhitTime[pin] = micros(); //Record time of hit for scan time and mask time
      PADhit[pin] = true; //Hit is active
      PADscan[pin] = true; //Scan Time is active
      PADdisplay[pin] = true; //Display hit on LCD is active
      //diag
      plot[pin] = true;
      //diag
  //      if (!menuActive){ //Only display the hit if the menu is not active
  //        if (pin < numOfPads){
  //          lcd.setCursor(pin % 3, pin / 3); //Display hit
  //          lcd.write(0b11111111);
  //        } else {
  //          lcd.setCursor(pin % 2 + 4, pin / 2 - 3); //Display hit
  //          //lcd.write(0b11111111);
  //          lcd.print(pin - 5);
  //        }
  //      }
    }

    //Stop displaying hit after displaytime has passed
    if ((PADdisplay[pin] == true && PADhitTime[pin] + displayHitTime < micros()) && !menuActive){ 
      PADdisplay[pin] = false; //Display hit on LCD is deactivated
  //      if (pin < numOfPads){
  //        lcd.setCursor(pin % 3, pin / 3); //Display empty char
  //        lcd.write(0b00010000);
  //      } else {
  //        lcd.setCursor(pin % 2 + 4, pin / 2 - 3); //Display empty char
  //        lcd.write(0b00010000);
  //      }
    }

    //If Scan Time is active and PadSensitivity is on, record highest peak (note velocity = 0)
    if (PADscan[pin] == true && MidiData[pin].data2 == 0){ 
        PADmax[pin] = max(PADmax[pin],PADvalue[pin]); //Get peak value
        numOfScans[pin] ++; //Diag

      if(PADscan[pin] == true && PADhitTime[pin] + (settings[pin].tScan * 100) < micros()){ //Send max peakhight as midi velocity after scantime is over
        PADscan[pin] = false; //Deactivate Scan      //Calculate value with compressor settings which also scales from 0-127
        midiSend(MidiData[pin].command, MidiData[pin].data1, ((PADmax[pin] - PADthreshold[pin] < 0) ? PADmax[pin] : (PADthreshold[pin] + ((PADmax[pin] - PADthreshold[pin]) / PADratio[pin]))) * PADgain[pin], MidiData[pin].channel);
        
        lastOperatedItem = pin; //Set last operated item to pad number for menu purposes
        changeMenuItem = true; //Set changeMenuItem flag to update menu
        
        if(MidiData[pin].command == 0x90){ //If command is Note On, then also send Note Off
          midiSend(0x80, MidiData[pin].data1, 0, MidiData[pin].channel);
        }

        Serial.println(PADmax[pin]); //Diag
        Serial.println(numOfScans[pin]); //Diag
        numOfScans[pin] = 0; //Diag
      } 
    } else if(PADscan[pin] == true && MidiData[pin].data2 > 0){ //If PadSensitivity is off, send midi with a set velocity
      PADscan[pin] = false; //Deactivate Scan, as there is no scan time
      midiSend(MidiData[pin].command, MidiData[pin].data1, MidiData[pin].data2, MidiData[pin].channel);
      
      lastOperatedItem = pin; //Set last operated item to pad number for menu purposes
      changeMenuItem = true; //Set changeMenuItem flag to update menu
      
      if(MidiData[pin].command == 0x90){ //If command is Note On, then also send Note Off
        midiSend(0x80, MidiData[pin].data1, 0, MidiData[pin].channel);
      }
    }

    //Disable new hits as long as masktime is active. 
    if (PADhit[pin] == true && PADhitTime[pin] + (settings[pin].tMask * 1000) < micros()){
      PADmax[pin] = 0; //Reset highest peak
      PADhit[pin] = false; //Deactivate hit
    }

    //Diag
    if (plot[pin] == true && PADhitTime[pin] + (settings[pin].tMask * 1000) + 1000 < micros()){
      plot[pin] = false;
    }
  }

  //Footswitches
  for (int pin = 0; pin < numOfSwitch; pin++){
    if (switchState[pin] == true && switchPressed[pin] == false && switchChangeTime[pin] + settings[pin + 8].tMask * 1000 < micros()){ //Rising edge of footswitch
      switchChangeTime[pin] = micros();
      switchPressed[pin] = true;
      lastOperatedItem = pin + 8; //Set last operated item to pad number for menu purposes
      changeMenuItem = true; //Set changeMenuItem flag to update menu
      midiSend(MidiData[pin + 8].command, MidiData[pin + 8].data1, MidiData[pin + 8].data2, MidiData[pin + 8].channel);
  //      if (!menuActive){ //Only display footswitch status if the menu is not active
  //        lcd.setCursor(pin + 4, 1); //Display status
  //        lcd.write(0b11111111);
  //      }
    }

    if (switchState[pin] == false && switchPressed[pin] == true && switchChangeTime[pin] + settings[pin + 8].tMask * 1000 < micros()){ //Falling edge of footswitch
      switchChangeTime[pin] = micros();
      switchPressed[pin] = false;
      if(MidiData[pin + 8].command == 0x90){ //If command is Note On, then also send Note Off.
        midiSend(0x80, MidiData[pin + 8].data1, 0, MidiData[pin + 8].channel);
      }
      
  //      if (!menuActive){ //Only stop displaying footswitch status if the menu is not active
  //        lcd.setCursor(pin + 4, 1); //Display empty char
  //        lcd.write(0b00010000);
  //      }
    }
  }

  //  if(plot[tempPin]){
  //    Serial.printf("PAD%u:%u,",tempPin,PADvalue[tempPin]);
  //    Serial.printf("Diff%u:%u,",tempPin,FSRdiff[tempPin]);
  //    Serial.printf("Thres:%u,",settings[tempPin].threshold * 4);
  //    Serial.printf("Scan:%u,",PADscan[tempPin] * 200);
  //    Serial.printf("Peak:%u,",PADmax[tempPin]);
  //
  //    Serial.printf("Min:%u,",0);
  //    Serial.printf("Max:%u,\n",1000);
  //  }

}

//====================================================================================================
//Code to save settings to EEPROM
//====================================================================================================
void save(byte slot) {
  ///*  //print old saved settings
  Monitor.printf("\nPreviously Saved settings on slot %u. [Address:value]", slot); //Diag
  for (int item = 0; item < numOfMenuItems * numOfSettings; item += numOfSettings){ //Diag
    if (item % numOfSettings == 0){
      Monitor.println();
    }
    for(int i = 0; i < numOfSettings; i++){
  //      Monitor.printf("%u:",item + ((slot - 1) * (numOfMenuItems * numOfSettings)) + i); //Address
      Monitor.printf("%u\t",EEPROM.read(item + ((slot - 1) * (numOfMenuItems * numOfSettings)) + i));
    }
  }

  //print current settings
  Monitor.println("\n\nCurrent settings"); //Diag
  for (int item = 0; item < numOfMenuItems; item++){ //Diag
    Monitor.print(settings[item].command, HEX); Monitor.print("\t");
    Monitor.print(settings[item].channel); Monitor.print("\t");
    Monitor.print(settings[item].noteValue); Monitor.print("\t");
    Monitor.print(settings[item].noteVelocity); Monitor.print("\t");
    Monitor.print(settings[item].ctrlNumber); Monitor.print("\t");
    Monitor.print(settings[item].ctrlValue); Monitor.print("\t");
    Monitor.print(settings[item].progNumber); Monitor.print("\t");
    Monitor.print(settings[item].loadSlot); Monitor.print("\t");
    Monitor.print(settings[item].threshold); Monitor.print("\t");
    Monitor.print(settings[item].tScan); Monitor.print("\t");
    Monitor.print(settings[item].tMask); Monitor.print("\t");
    Monitor.println();
  }
  //*/
  Monitor.printf("\nSaved on slot %u", slot); //Diag
  Monitor.print("\tAddress: ");
  Monitor.print(0 + ((slot - 1) * numOfMenuItems * numOfSettings));
  Monitor.print(" to ");
  Monitor.println((numOfMenuItems * numOfSettings) + ((slot - 1) * numOfMenuItems * numOfSettings) - 1 );
  
  //11 settings for each of the 10 items (Pad/External Trigger/Switch)
  //The EEPROM address is calculated by multiplying the item nummber by the nummer of settings per item. So the addresses of each item are offset by 11 settings.
  //Then add the offset of the total amount of settings per slot. 
  //Then add 1 for each setting the requested item.
  //Example: Slot 2, Item 0 (Pad 1)
  //(0 * 11) + ((2-1) * 10 * 11) + 0 = Address 110
  //(0 * 11) + ((2-1) * 10 * 11) + 1 = Address 111.. etc
  //Example: Slot 4, Item 7 (External Trigger 2)
  //(7 * 11) + ((4-1) * 10 * 11) + 0 = Address 407
  //(7 * 11) + ((4-1) * 10 * 11) + 1 = Address 408.. etc
  for (int item = 0; item < numOfMenuItems; item++){
    EEPROM.update((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 0, settings[item].command); //Actual code to save settings to save slot on EEPROM
    EEPROM.update((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 1, settings[item].channel); //Actual code to save settings to save slot on EEPROM
    EEPROM.update((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 2, settings[item].noteValue); //Actual code to save settings to save slot on EEPROM
    EEPROM.update((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 3, settings[item].noteVelocity); //Actual code to save settings to save slot on EEPROM
    EEPROM.update((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 4, settings[item].ctrlNumber); //Actual code to save settings to save slot on EEPROM
    EEPROM.update((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 5, settings[item].ctrlValue); //Actual code to save settings to save slot on EEPROM
    EEPROM.update((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 6, settings[item].progNumber); //Actual code to save settings to save slot on EEPROM
    EEPROM.update((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 7, settings[item].loadSlot); //Actual code to save settings to save slot on EEPROM
    EEPROM.update((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 8, settings[item].threshold); //Actual code to save settings to save slot on EEPROM
    EEPROM.update((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 9, settings[item].tScan); //Actual code to save settings to save slot on EEPROM
    EEPROM.update((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 10, settings[item].tMask); //Actual code to save settings to save slot on EEPROM
    Monitor.print(EEPROM.read((item * numOfSettings) + ((slot - 1) * numOfMenuItems * numOfSettings) + 0), HEX); Monitor.print("\t"); //Diag
    Monitor.print(EEPROM.read((item * numOfSettings) + ((slot - 1) * numOfMenuItems * numOfSettings) + 1)); Monitor.print("\t"); //Diag
    Monitor.print(EEPROM.read((item * numOfSettings) + ((slot - 1) * numOfMenuItems * numOfSettings) + 2)); Monitor.print("\t"); //Diag
    Monitor.print(EEPROM.read((item * numOfSettings) + ((slot - 1) * numOfMenuItems * numOfSettings) + 3)); Monitor.print("\t"); //Diag
    Monitor.print(EEPROM.read((item * numOfSettings) + ((slot - 1) * numOfMenuItems * numOfSettings) + 4)); Monitor.print("\t"); //Diag
    Monitor.print(EEPROM.read((item * numOfSettings) + ((slot - 1) * numOfMenuItems * numOfSettings) + 5)); Monitor.print("\t"); //Diag
    Monitor.print(EEPROM.read((item * numOfSettings) + ((slot - 1) * numOfMenuItems * numOfSettings) + 6)); Monitor.print("\t"); //Diag
    Monitor.print(EEPROM.read((item * numOfSettings) + ((slot - 1) * numOfMenuItems * numOfSettings) + 7)); Monitor.print("\t"); //Diag
    Monitor.print(EEPROM.read((item * numOfSettings) + ((slot - 1) * numOfMenuItems * numOfSettings) + 8)); Monitor.print("\t"); //Diag
    Monitor.print(EEPROM.read((item * numOfSettings) + ((slot - 1) * numOfMenuItems * numOfSettings) + 9)); Monitor.print("\t"); //Diag
    Monitor.print(EEPROM.read((item * numOfSettings) + ((slot - 1) * numOfMenuItems * numOfSettings) + 10)); Monitor.print("\t\n"); //Diag
  }
  Monitor.println(); //Diag

  //If you wish to start next time with the last saved slot, save this slot.
  //EEPROM.update(lastLoadedSlotAddress, slot); //Load slot
}

//====================================================================================================
//Code to load settings from EEPROM
//====================================================================================================
void load(int slot) {
    //print current settings
  //  Monitor.println(); //Diag
  //  Monitor.println("Current settings"); //Diag
  //  for (int item = 0; item < numOfMenuItems; item++){ //Diag
  //    Monitor.print(settings[item].command, HEX); Monitor.print("\t");
  //    Monitor.print(settings[item].channel); Monitor.print("\t");
  //    Monitor.print(settings[item].noteValue); Monitor.print("\t");
  //    Monitor.print(settings[item].noteVelocity); Monitor.print("\t");
  //    Monitor.print(settings[item].ctrlNumber); Monitor.print("\t");
  //    Monitor.print(settings[item].ctrlValue); Monitor.print("\t");
  //    Monitor.print(settings[item].progNumber); Monitor.print("\t");
  //    Monitor.print(settings[item].loadSlot); Monitor.print("\t");
  //    Monitor.print(settings[item].threshold); Monitor.print("\t");
  //    Monitor.print(settings[item].tScan); Monitor.print("\t");
  //    Monitor.print(settings[item].tMask); Monitor.print("\t");
  //    Monitor.println();
  //  }

    //print current saved settings
  //  Monitor.printf("\nSetting on slot %u [Address:value]", slot); //Diag
  //  for (int item = 0; item < numOfMenuItems * numOfSettings; item += numOfSettings){ //Diag
  //    if (item % numOfSettings == 0){
  //      Monitor.println();
  //    }
  //    for(int i = 0; i < numOfSettings; i++){
  ////      Monitor.printf("%u:",item + ((slot - 1) * (numOfMenuItems * numOfSettings)) + i); //Address
  //      Monitor.printf("%u",EEPROM.read(item + ((slot - 1) * (numOfMenuItems * numOfSettings)) + i));
  ////      Monitor.printf("-%u-%u",item, i);
  //      Monitor.print("\t");
  //    }
  //  }
  //  Monitor.println(); //Diag
  //  Monitor.println(); //Diag

  //  Monitor.printf("Loaded from slot %u\n", slot); //Diag
  for (int item = 0; item < numOfMenuItems; item++){
    settings[item].command      = EEPROM.read((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 0); //Actual code to load settings from EEPROM
    settings[item].channel      = EEPROM.read((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 1); //Actual code to load settings from EEPROM
    settings[item].noteValue    = EEPROM.read((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 2); //Actual code to load settings from EEPROM
    settings[item].noteVelocity = EEPROM.read((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 3); //Actual code to load settings from EEPROM
    settings[item].ctrlNumber   = EEPROM.read((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 4); //Actual code to load settings from EEPROM
    settings[item].ctrlValue    = EEPROM.read((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 5); //Actual code to load settings from EEPROM
    settings[item].progNumber   = EEPROM.read((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 6); //Actual code to load settings from EEPROM
    settings[item].loadSlot     = EEPROM.read((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 7); //Actual code to load settings from EEPROM
    settings[item].threshold    = EEPROM.read((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 8); //Actual code to load settings from EEPROM
    settings[item].tScan        = EEPROM.read((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 9); //Actual code to load settings from EEPROM
    settings[item].tMask        = EEPROM.read((item * numOfSettings) + ((slot - 1) * (numOfMenuItems * numOfSettings)) + 10); //Actual code to load settings from EEPROM
  //    Monitor.print(settings[item].command, HEX); Monitor.print("\t");
  //    Monitor.print(settings[item].channel); Monitor.print("\t");
  //    Monitor.print(settings[item].noteValue); Monitor.print("\t");
  //    Monitor.print(settings[item].noteVelocity); Monitor.print("\t");
  //    Monitor.print(settings[item].ctrlNumber); Monitor.print("\t");
  //    Monitor.print(settings[item].ctrlValue); Monitor.print("\t");
  //    Monitor.print(settings[item].progNumber); Monitor.print("\t");
  //    Monitor.print(settings[item].loadSlot); Monitor.print("\t");
  //    Monitor.print(settings[item].threshold); Monitor.print("\t");
  //    Monitor.print(settings[item].tScan); Monitor.print("\t");
  //    Monitor.print(settings[item].tMask); Monitor.print("\t\n");
  }
  //  Monitor.println(); //Diag

  //Update all midi data from loaded settings
  for(int i = 0; i < numOfMenuItems; i++){
    updateMidiData(i);
  }

    //Save which slot was loaded last for recallability
    //Only activate if you wish to restart with last loaded settings, even when changing with pads or switches
    //When loading from the menu, the lastloadedslot is saved to the EEPROM directly.
  //  EEPROM.update(lastLoadedSlotAddress, slot); 
  lastLoadedSlot = slot;

  //Display loaded slot if slot is changed
  if (!menuActive){
    lcd.setCursor(15,0);
    lcd.print(lastLoadedSlot);
    lcd.setCursor(8, 1);
    lcd.print(slotName[lastLoadedSlot]);
  }
}

//====================================================================================================
//Code to change brightness, only when changed
//====================================================================================================
void changeBrightness(byte value) {
  //Voltage to brightness is almoste quadratic. For more accuracy, check "CIE Lookup Table"
  analogWrite(brightnessPin, map(pow(value/10,2), 0, 100, 0, 255)); //Set PWM output for LCD brightness. Map 0-100% to 0-255.
}

//====================================================================================================
//Code to update midi data, only when changed
//====================================================================================================
void updateMidiData(int item){
  switch (settings[item].command)
    {
      case 0x90: //If Cmd = Note On, get Note Number and Note value for Data1 and Data2
        MidiData[item].data1 = settings[item].noteValue; //Set Data1 to Note Number
        MidiData[item].data2 = settings[item].noteVelocity; //Set Data2 to Note Value
        break;
      case 0xB0: //If Cmd = Control Change, get Control Number and Control value for Data1 and Data2
        MidiData[item].data1 = settings[item].ctrlNumber; //Set Data1 to Control Number
        MidiData[item].data2 = settings[item].ctrlValue; //Set Data2 to Control Value
        break;
      case 0xC0: //If Cmd = Program Change, get Program Number for Data1
        MidiData[item].data1 = settings[item].progNumber; //Set Data1 to Program Number
        MidiData[item].data2 = 0xFE; //Set Data2 to 254 just so Pad Sensitivity is off
        break;
      case 0xF0: //If Cmd = load different slot
        MidiData[item].data1 = settings[item].loadSlot;
        MidiData[item].data2 = 0xFE; //Set Data2 to 254 just so Pad Sensitivity is off
    }
    
  MidiData[item].command = settings[item].command;
  MidiData[item].channel = settings[item].channel;
}

//====================================================================================================
//Code to send a midi message
//====================================================================================================
void midiSend(int cmd, int data1, int data2, int channel) {
  switch (cmd){ //usbMIDI requires to disect the midi cmd. serialMIDI is basicly the same every time.
    case 0x90: //Note On
      usbMIDI.sendNoteOn(data1, data2, channel + 1); // Note, velocity, channel. Channel value is 0-15, actual channel is 1-16
      Midi.write(cmd + channel); //Add the channel to the midi command
      Midi.write(data1); //Send data 1
      Midi.write(data2); //Send data 2
      break;
    case 0x80: //Note Off
      usbMIDI.sendNoteOff(data1, data2, channel + 1); // Note, velocity, channel
      Midi.write(cmd + channel); //Add the channel to the midi command
      Midi.write(data1); //Send data 1
      Midi.write(data2); //Send data 2
      break;
    case 0xB0: //Control Change
      usbMIDI.sendControlChange(data1, data2, channel + 1); // Control, Value, channel
      Midi.write(cmd + channel); //Add the channel to the midi command
      Midi.write(data1); //Send data 1
      Midi.write(data2); //Send data 2
      break;
    case 0xC0: //Program Change
      usbMIDI.sendProgramChange(data1, channel + 1); // Program, channel
      Midi.write(cmd + channel); //Add the channel to the midi command
      Midi.write(data1); //Send data 1
      break;
    case 0xF0: //Load different setting
      if ((data1 == numOfSlots + 1) && lastLoadedSlot < numOfSlots){ //Load next slot as long as last slot is not reached
        load(lastLoadedSlot + 1); //Load Next slot
      } else if((data1 == numOfSlots + 1) && lastLoadedSlot == numOfSlots){ //If next slot should be loaded, but current slot is last slot, turnkate to slot 1
        load(1); 
      } else if(data1 == numOfSlots + 2 && lastLoadedSlot > 1){ //Load previous slot as long as current slot is not first slot
        load(lastLoadedSlot - 1); //Load previous slot
      } else if(data1 == numOfSlots + 2 && lastLoadedSlot == 1){ //If previous slot should be loaded, but current slot is first slot, turnkate to last slot
        load(numOfSlots); //Load last slot
      } else {
        load(data1); //Load slot according to data
      }
      break;
  }
  //  Serial.printf("Cmd %0X - Data1 %3u - Data2 %3u\n",cmd, data1, data2);
}

//====================================================================================================
//Navigation throughout the menu
//====================================================================================================
void menu() {
  switch (menuDepth) //Every menudepth gives different menu options
  {
    case 1: //menuDepth 1 = "Pad 1", "Pad 2", "Pad 3", "Pad 4", "Pad 5", "Pad 6", "External Trig 1", "External Trig 2", "Footswitch 1", "Footswitch 2", "Brightness", "Save Settings", "Load Settings", "Exit Menu"
      switch (menuItem[1]) //menuItem keeps track of every selected menu item at every menu depth. If menuItem[1] = 4, that means that Pad 5 is selected.
      {
        case numOfMenuItems + 3: //Exit menu if menuItem[1] = 13
          if (encPrev) {menuItem[menuDepth]--;} //Move one up at menuItem[1]
          if (encNext) {menuItem[menuDepth]++;} //Move one down at menuItem[1]
          if (encSelect) {menuItem[menuDepth] = 0; menuDepth--; menuActive = false;} //Exit menu, set variables accordingly
          break;
        default: //Any other value, move deeper into the menu
          if (encPrev) {menuItem[menuDepth]--;}
          if (encNext) {menuItem[menuDepth]++;}
          if (encSelect) {menuDepth++;} //Move one deeper into the menu
          break;
      }
      break;

    case 2: //menuDepth 2 = "Message", "Channel", "Threshold", "Back"
      switch (menuItem[1]) //unless Footswitch 1 or 2, menuDepth 2 = "Message", "Channel", "Back"
      {
        case numOfPads + numOfExtTrig:     //Footswitch 1
        case numOfPads + numOfExtTrig + 1: //Footswitch 2
          switch (menuItem[2])
          {
            case 4: //"Back"
              if (encPrev) {menuItem[menuDepth]--;}
              if (encNext) {menuItem[menuDepth]++;}
              if (encSelect) {menuDepth--; menuItem[2] = 0;} //Reset menu to top of the list
              break;
            default: //Any other value, move deeper into the menu
              if (encPrev) {menuItem[menuDepth]--;}
              if (encNext) {menuItem[menuDepth]++;}
              if (encSelect) {menuDepth++;}
              break;
          }
          break;
        case numOfMenuItems + 0: //"Brightness"
          if (encPrev && brightness == 0) {brightness = 110;} //If brightness gonna go below 0, reset to 101. Next line will substract one to 100
          if (encPrev) {brightness-=10; changeBrightness(brightness);} //Change brightness directly when changing settings
          if (encNext && brightness >= 100) {brightness = 246;} //If brightness gonna go over 100, reset to max. Next line will turncate to 0
          if (encNext) {brightness+=10; changeBrightness(brightness);} //Change brightness directly when changing settings
          if (encSelect) {menuDepth--; EEPROM.update(brightnessAddress, brightness);} //Once brightness is selected, save to EEPROM
          break;
        case numOfMenuItems + 1: //"Save settings"
          if (encPrev) {saveSlot--;}
          if (encNext) {saveSlot++;}
          if (encSelect) {menuDepth--; if (saveSlot == numOfSlots + 1) {saveSlot = 1;} else {save(saveSlot);}} //If "Back" is selected, go back. Else, save settings to selected slot and go back
          if (encLongPress) {menuDepth++; menuItem[2] = 5; character = slotName[saveSlot].charAt(charIndex);}
          break;
        case numOfMenuItems + 2: //"Load settings"
          if (encPrev) {loadSlot--;}
          if (encNext) {loadSlot++;}
          if (encSelect) {menuDepth--; if (loadSlot == numOfSlots + 1) {loadSlot = 1;} else {load(loadSlot); EEPROM.update(lastLoadedSlotAddress, loadSlot);}} //If "Back" is selected, go back. Else, load settings to selected slot and go back
          break;
        default:
          switch (menuItem[2])
          {
            case 5: //When "Back" is selected, move 1 menudepth back. Back is always the last of the Text.
              if (encPrev) {menuItem[menuDepth]--;}
              if (encNext) {menuItem[menuDepth]++;}
              if (encSelect) {menuDepth--; menuItem[2] = 0;} //Reset menu to top of the list
              break;
            default:
              if (encPrev) {menuItem[menuDepth]--;}
              if (encNext) {menuItem[menuDepth]++;}
              if (encSelect) {menuDepth++;}
              break;
          }
          break;
      }
      break;

    case 3: //Menu depth 3 lets you edit Channel or Threshold
      switch (menuItem[2]) //menu item 2 = "Message", "Channel", "Threshold", "Scan Time", "Mask Time", "Back"
      {
        case 0: //Message - At message there is a new menu available
          switch (menuItem[3])
          {
            case 0: //"Note On"
              if (encPrev) {menuItem[menuDepth]--;}
              if (encNext) {menuItem[menuDepth]++;}
              if (encSelect) {menuDepth++; menuItem[4] = 0;} //Move deeper to a specific part of the list
              if (encSelect) {settings[menuItem[1]].command = 0x90; updateMidiData(menuItem[1]);} //Set Midi Cmd to 9x
              break;
            case 1: //"Control change"
              if (encPrev) {menuItem[menuDepth]--;}
              if (encNext) {menuItem[menuDepth]++;}
              if (encSelect) {menuDepth++; menuItem[4] = 5;} //Move deeper to a specific part of the list
              if (encSelect) {settings[menuItem[1]].command = 0xB0; updateMidiData(menuItem[1]);} //Set Midi Cmd to Bx
              break;
            case 2: //"Program Change"
              if (encPrev) {menuItem[menuDepth]--;}
              if (encNext) {menuItem[menuDepth]++;}
              if (encSelect) {menuDepth++; menuItem[4] = 10;} //Move deeper to a specific part of the list
              if (encSelect) {settings[menuItem[1]].command = 0xC0; updateMidiData(menuItem[1]);} //Set Midi Cmd to Cx
              break;
            case 3: //"Load Setting"
              if (encPrev) {menuItem[menuDepth]--;}
              if (encNext) {menuItem[menuDepth]++;}
              if (encSelect) {menuDepth++;} //Move deeper
              if (encSelect) {settings[menuItem[1]].command = 0xF0; updateMidiData(menuItem[1]);} //Set Midi Cmd to FF
              break;
            case 4: //"Back"
              if (encPrev) {menuItem[menuDepth]--;}
              if (encNext) {menuItem[menuDepth]++;}
              if (encSelect) {menuDepth--; menuItem[3] = 0; menuItem[4] = 0;} //Reset menu to top of the list
              break;
          }
          break;
        case 1: //Channel
          if (encPrev && settings[menuItem[1]].channel == 0) {settings[menuItem[1]].channel = 16;} //If channel gonna go below 0, reset to 16. Next line will substract one to 15
          if (encPrev) {settings[menuItem[1]].channel--; updateMidiData(menuItem[1]);}
          if (encNext && settings[menuItem[1]].channel == 15) {settings[menuItem[1]].channel = 255;} //If channel gonna go below 0, reset to 16. Next line will substract one to 15
          if (encNext) {settings[menuItem[1]].channel++; updateMidiData(menuItem[1]);}
          if (encSelect) {menuDepth--;}
          break;
        case 2: //Threshold
          switch (menuItem[1])
          {
            case numOfPads + numOfExtTrig:     //Footswitch 1
            case numOfPads + numOfExtTrig + 1: //Footswitch 2
              if (encPrev || encNext) {settings[menuItem[1]].threshold = !settings[menuItem[1]].threshold;}
              if (encSelect) {menuDepth--;}
              break;
            default:
              if (encPrev) {settings[menuItem[1]].threshold--;}
              if (encNext) {settings[menuItem[1]].threshold++;}
              if (encSelect) {menuDepth--;}
              break;
          }
          break;
        case 3: //Scan Time
          switch (menuItem[1])
          {
            case numOfPads + numOfExtTrig:     //Footswitch 1
            case numOfPads + numOfExtTrig + 1: //Footswitch 2
              if (encPrev) {settings[menuItem[1]].tMask--;}
              if (encNext) {settings[menuItem[1]].tMask++;}
              if (encSelect) {menuDepth--;}
              break;
            default:
              if (encPrev && settings[menuItem[1]].tScan == 1) {settings[menuItem[1]].tScan = 101;} //If scan time gonna go below 0, reset to 10.1. Next line will substract one to 10.0
              if (encPrev) {settings[menuItem[1]].tScan--;}
              if (encNext) {settings[menuItem[1]].tScan++;}
              if (encSelect) {menuDepth--;}
              break;
          }
          break;
        case 4: //Mask Time
          if (encPrev && ((settings[menuItem[1]].tMask == 1) || ((settings[menuItem[1]].tMask - 1) * 10 < settings[menuItem[1]].tScan))) {settings[menuItem[1]].tMask = 101;} // If Mask Time is shorter than Scan Time, or shorter than 1ms, Mask Time will turn to max value
          if (encPrev) {settings[menuItem[1]].tMask--;}
          if (encNext) {settings[menuItem[1]].tMask++;}
          if (encSelect) {menuDepth--;}
          break;
        case 5: //Change slot name
          if (encPrev) {character--;} if(character > 126) {character = 32;} //chance character
          if (encNext) {character++;} if(character < 32) {character = 126;} //change character
          slotName[saveSlot].setCharAt(charIndex,character); //change the character of the string
          if (encSelect) { //if you press select, move next character or close out name change
            if(charIndex < 7) { //if character is not the last character, move to next character
              charIndex++; character = slotName[saveSlot].charAt(charIndex);
            } else {
              menuDepth--; menuItem[2] = 0; charIndex = 0; lcd.noCursor(); //else, reset name changer, save name on EEPROM
              byte charsOfName[8]; slotName[saveSlot].getBytes(charsOfName, 9);
              for(int i = 0; i < 8; i++){
                EEPROM.update((numOfSettings * numOfMenuItems * numOfSlots) + ((saveSlot - 1) * 8) + i, charsOfName[i]);
              }
            }
          }
          break;
      }
      break;

    case 4: //Menu depth 4 lets you edit Program Number
      switch (menuItem[3]) //menu item 3 = "Note On", "Control Change", "Program Change", "Back"
      {
        case 0: //Note On - At Note On there is a new menu available
        case 1: //Control Change - At Control Change there is a new menu available
          switch (menuItem[4])
          {
            case 2: //"Back"
            case 7: //"Back"
              if (encPrev) {menuItem[menuDepth]--;}
              if (encNext) {menuItem[menuDepth]++;}
              if (encSelect) {menuDepth--; menuItem[4] = 0;} //Reset menu to top of the list
              break;
            default:
              if (encPrev) {menuItem[menuDepth]--;}
              if (encNext) {menuItem[menuDepth]++;}
              if (encSelect) {menuDepth++;}
              break;
          }
          break;
        case 2: //Program Change
          if (encPrev && settings[menuItem[1]].progNumber == 0) {settings[menuItem[1]].progNumber = 128;} //If Program Change gonna go below 0, reset to 128. Next line will substract one to 127
          if (encPrev) {settings[menuItem[1]].progNumber--; settings[menuItem[1]].command = 0xC0; updateMidiData(menuItem[1]);} //Set Midi Cmd to Cx, update midi command settings
          if (encNext && settings[menuItem[1]].progNumber == 127) {settings[menuItem[1]].progNumber = 255;} //If Program Change gonna go over 127, reset to 255. Next line will add one to 0 
          if (encNext) {settings[menuItem[1]].progNumber++; settings[menuItem[1]].command = 0xC0; updateMidiData(menuItem[1]);} //Set Midi Cmd to Cx, update midi command settings
          if (encSelect) {menuDepth--;}
          break;
        case 3: //Change Load Slt
          if (encPrev && settings[menuItem[1]].loadSlot == 1) {settings[menuItem[1]].loadSlot = numOfSlots + 4;}
          if (encPrev) {settings[menuItem[1]].loadSlot--; settings[menuItem[1]].command = 0xF0; updateMidiData(menuItem[1]);}
          if (encNext && settings[menuItem[1]].loadSlot >= numOfSlots + 3) {settings[menuItem[1]].loadSlot = 0;}
          if (encNext) {settings[menuItem[1]].loadSlot++; settings[menuItem[1]].command = 0xF0; updateMidiData(menuItem[1]);}
          if (encSelect) {menuDepth--;}
          break;
      }
      break;

    case 5:
      switch (menuItem[4])
      {
        case 0: //Note Value
          if (encPrev && settings[menuItem[1]].noteValue == 0) {settings[menuItem[1]].noteValue = 128;} //If Note Value gonna go below 0, reset to 128. Next line will substract one to 127
          if (encPrev) {settings[menuItem[1]].noteValue--; settings[menuItem[1]].command = 0x90; updateMidiData(menuItem[1]);} //Set Midi Cmd to 9x, update midi command settings
          if (encNext && settings[menuItem[1]].noteValue == 127) {settings[menuItem[1]].noteValue = 255;} //If Note Value gonna go over 127, reset to 255. Next line will add one to 0 
          if (encNext) {settings[menuItem[1]].noteValue++; settings[menuItem[1]].command = 0x90; updateMidiData(menuItem[1]);} //Set Midi Cmd to 9x, update midi command settings
          if (encSelect) {menuDepth--;}
          break;
        case 1: //Note Velocity
          if (encPrev && settings[menuItem[1]].noteVelocity == 0) {settings[menuItem[1]].noteVelocity = 128;} //If Note Velocity gonna go below 0, reset to 128. Next line will substract one to 127
          if (encPrev) {settings[menuItem[1]].noteVelocity--; settings[menuItem[1]].command = 0x90; updateMidiData(menuItem[1]);} //Set Midi Cmd to 9x, update midi command settings       
          if (encNext && settings[menuItem[1]].noteVelocity == 127) {settings[menuItem[1]].noteVelocity = 255;} //If Note Velocity gonna go over 127, reset to 255. Next line will add one to 0 
          if (encNext) {settings[menuItem[1]].noteVelocity++; settings[menuItem[1]].command = 0x90; updateMidiData(menuItem[1]);} //Set Midi Cmd to 9x, update midi command settings
          if (encSelect) {menuDepth--;}
          break;
        case 5: //Control Number
          if (encPrev && settings[menuItem[1]].ctrlNumber == 0) {settings[menuItem[1]].ctrlNumber = 128;} //If Control Nummer gonna go below 0, reset to 128. Next line will substract one to 127
          if (encPrev) {settings[menuItem[1]].ctrlNumber--; settings[menuItem[1]].command = 0xB0; updateMidiData(menuItem[1]);} //Set Midi Cmd to Bx, update midi command settings
          if (encNext && settings[menuItem[1]].ctrlNumber == 127) {settings[menuItem[1]].ctrlNumber = 255;} //If Control Nummer gonna go over 127, reset to 255. Next line will add one to 0 
          if (encNext) {settings[menuItem[1]].ctrlNumber++; settings[menuItem[1]].command = 0xB0; updateMidiData(menuItem[1]);} //Set Midi Cmd to Bx, update midi command settings
          if (encSelect) {menuDepth--;}
          break;
        case 6: //Control Value
          if (encPrev && settings[menuItem[1]].ctrlValue == 0) {settings[menuItem[1]].ctrlValue = 128;} //If Control Value gonna go below 0, reset to 128. Next line will substract one to 127
          if (encPrev) {settings[menuItem[1]].ctrlValue--; settings[menuItem[1]].command = 0xB0; updateMidiData(menuItem[1]);} //Set Midi Cmd to Bx, update midi command settings
          if (encNext && settings[menuItem[1]].ctrlValue == 127) {settings[menuItem[1]].ctrlValue = 255;} //If Control Value gonna go over 127, reset to 255. Next line will add one to 0 
          if (encNext) {settings[menuItem[1]].ctrlValue++; settings[menuItem[1]].command = 0xB0; updateMidiData(menuItem[1]);} //Set Midi Cmd to Bx, update midi command settings
          if (encSelect) {menuDepth--;}
          break;
        default: //Back
          if (encSelect) {menuDepth--;}
          break;
      }
      break;

    default: //Not sure if this is neccesary. menuDepth needs to become 1 once the menu becomes activated.
      if (encPrev) {menuItem[menuDepth]--;}
      if (encNext) {menuItem[menuDepth]++;}
      if (encSelect) {menuDepth++;}
      break;
  }

  //====================================================================================================
  //Limitation of the different menu items. Jump to top when end is reached.
  if      (menuItem[1] < 0)   {menuItem[1] = numOfMenuItems + 3;}
  else if (menuItem[1] >= numOfMenuItems + 4) {menuItem[1] = 0;}

  if (menuItem[1] == numOfPads + numOfExtTrig || menuItem[1] == numOfPads + numOfExtTrig + 1)
  { //If one of the Footswitches are selected, less settings are available
    if      (menuItem[2] < 0)  {menuItem[2] = 4;}
    else if (menuItem[2] >= 5) {menuItem[2] = 0;}
  } else { //Else, show full settings available
    if      (menuItem[2] < 0)  {menuItem[2] = 5;}
    else if (menuItem[2] >= 6) {menuItem[2] = 0;}
  }

  if      (menuItem[3] < 0)  {menuItem[3] = 4;}
  else if (menuItem[3] >= 5) {menuItem[3] = 0;}

  if      (menuItem[4] < 0)  {menuItem[4] = 2;}
  else if (menuItem[4] == 3) {menuItem[4] = 0;}

  if      (menuItem[4] == 4) {menuItem[4] = 7;}
  else if (menuItem[4] == 8) {menuItem[4] = 5;}

  //Limitation of the different settings. 127 jumps to 0.
  if      (settings[menuItem[1]].tScan > 100)         {settings[menuItem[1]].tScan = 1;} //Scan Time longer than 10.0ms is not allowed
  if      (settings[menuItem[1]].tScan > (settings[menuItem[1]].tMask * 10)) {settings[menuItem[1]].tMask = (settings[menuItem[1]].tScan / 10) + 1;} //If Scan time is longer than mask time, mask time will shift accordingly. Else no message will be sent.
  if      (settings[menuItem[1]].tMask > 100 && menuItem[1] < 8) {settings[menuItem[1]].tMask = ((settings[menuItem[1]].tScan - 1) / 10) + 1;  } //Pad Mask Time longer than 100ms is not allowed, shorter than scan time also not allowed
  
  if      (saveSlot < 1)                {saveSlot = numOfSlots + 1;} //Save slot
  else if (saveSlot > numOfSlots + 1)   {saveSlot = 1;}
  if      (loadSlot < 1)                {loadSlot = numOfSlots + 1;} //Load slot
  else if (loadSlot > numOfSlots + 1)   {loadSlot = 1;}

  Serial.printf("Depth: %u\t\t[1]:%2u\t[2]: %u\t[3]: %u\t[4] %u\n",menuDepth, menuItem[1], menuItem[2], menuItem[3], menuItem[4]);
}

//====================================================================================================
//Code to write the screen accordingly
//====================================================================================================
void updateScreen(){
  lcd.clear();
  if(!menuActive){
    lcd.setCursor(15,0);
    lcd.print(lastLoadedSlot);
    lcd.setCursor(8, 1);
    lcd.print(slotName[lastLoadedSlot]);
  }
 
  switch (menuDepth)
  {
    case 1: //menuDepth 1 = "Pad 1", "Pad 2", "Pad 3", "Pad 4", "Pad 5", "Pad 6", "External Trig 1", "External Trig 2", "Footswitch 1", "Footswitch 2", "Brightness", "Save Settings", "Load Settings", "Exit Menu"
      switch (menuItem[1])
      {
        case numOfMenuItems + 3: //Once at the bottom of the menu, display differently
          lcd.setCursor(1, 0);                 lcd.print(menuText1[menuItem[1] - 1]);
          lcd.setCursor(0, 1); lcd.print(">"); lcd.print(menuText1[menuItem[1]]);
          break;
        default:  //Default way of displaying the different menu items and the next one available
          lcd.setCursor(0, 0); lcd.print(">"); lcd.print(menuText1[menuItem[1]]);
          lcd.setCursor(1, 1);                 lcd.print(menuText1[menuItem[1] + 1]);
          break;
      }
      break;

    case 2: //menuDepth 2 = "Message", "Channel", "Threshold", "Back"
      switch (menuItem[1]) //unless Footswitch 1 or 2 selected. menuDepth 2 = "Message", "Channel", "Back"
      {
        case numOfPads + numOfExtTrig: //Footswitch 1
        case numOfPads + numOfExtTrig + 1: //Footswitch 2
          switch (menuItem[2]) //Display different menu when Footswitch is selected
          {
            case 4:
              lcd.setCursor(1, 0);                 lcd.print(menuText2b[menuItem[2] - 1]);
              lcd.setCursor(0, 1); lcd.print(">"); lcd.print(menuText2b[menuItem[2]]);
              break;
            default:
              lcd.setCursor(0, 0); lcd.print(">"); lcd.print(menuText2b[menuItem[2]]);
              lcd.setCursor(1, 1);                 lcd.print(menuText2b[menuItem[2] + 1]);
              break;
          }
          break;
        case numOfMenuItems + 0: //"Brightness"
          lcd.setCursor(1, 0); lcd.print(menuText1[menuItem[1]]); //Display the to be adjusted setting name
          lcd.setCursor(1, 1); lcd.print(brightness); lcd.print("%"); //Display the current setting value
          break;
        case numOfMenuItems + 1: //"Save settings"
          lcd.setCursor(1, 0); lcd.print(menuText1[menuItem[1]]); //Display the to be adjusted setting name
          lcd.setCursor(1, 1); if (saveSlot == numOfSlots + 1) {
            lcd.print("Back");
          } else {
            lcd.print(saveSlot); lcd.print(": ");
            lcd.print(slotName[saveSlot]); //Display the name of the slotnumber
          }
          break;
        case numOfMenuItems + 2: //"Load settings"
          lcd.setCursor(1, 0); lcd.print(menuText1[menuItem[1]]); //Display the to be adjusted setting name
          lcd.setCursor(1, 1); if (loadSlot == numOfSlots + 1) {
            lcd.print("Back");
          } else {
            lcd.print(loadSlot); lcd.print(": "); //Display the slotnumber
            lcd.print(slotName[loadSlot]); //Display the slot name
          }
          break;
        default:
          switch (menuItem[2])
          {
            case 5:
              lcd.setCursor(1, 0);                 lcd.print(menuText2[menuItem[2] - 1]);
              lcd.setCursor(0, 1); lcd.print(">"); lcd.print(menuText2[menuItem[2]]);
              break;
            default:
              lcd.setCursor(0, 0); lcd.print(">"); lcd.print(menuText2[menuItem[2]]);
              lcd.setCursor(1, 1);                 lcd.print(menuText2[menuItem[2] + 1]);
              break;
          }
          break;
      }
      break;

    case 3: //menuDepth 3 = "Note On", "Control Change", "Program Change", "Load Setting", "Back"
      //menuDepth 3 = set channel / set threshold
      switch (menuItem[2])
      {
        case 1: //Channel
          lcd.setCursor(1, 0); lcd.print(menuText2[1]); //Display the to be adjusted setting name
          lcd.setCursor(1, 1); lcd.print(settings[menuItem[1]].channel + 1); //Display the current setting value
          break;
        case 2: //Threshold
          switch (menuItem[1])
            {
              case numOfPads + numOfExtTrig: //Footswitch 1
              case numOfPads + numOfExtTrig + 1: //Footswitch 2
                lcd.setCursor(1, 0); lcd.print(menuText2b[2]); //Display the to be adjusted setting name
                lcd.setCursor(1, 1); //lcd.print(settings[menuItem[1]].threshold); //Display the current setting value
                if(settings[menuItem[1]].threshold) {lcd.print("Inverted");} else {lcd.print("Normal");}
                break;
              default:
                lcd.setCursor(1, 0); lcd.print(menuText2[2]); //Display the to be adjusted setting name
                lcd.setCursor(1, 1); lcd.print(settings[menuItem[1]].threshold); //Display the current setting value
                break;
            }
          break;
        case 3: //Scan Time
          switch (menuItem[1])
          {
            case numOfPads + numOfExtTrig: //Footswitch 1
            case numOfPads + numOfExtTrig + 1: //Footswitch 2
              lcd.setCursor(1, 0); lcd.print(menuText2b[3]); //Display the to be adjusted setting name
              lcd.setCursor(1, 1); lcd.print(settings[menuItem[1]].tMask); lcd.print(" ms"); //Display the current setting value
              break;
            default:
              lcd.setCursor(1, 0); lcd.print(menuText2[3]); //Display the to be adjusted setting name
              lcd.setCursor(1, 1); lcd.print(settings[menuItem[1]].tScan / 10); lcd.print("."); lcd.print(settings[menuItem[1]].tScan % 10); lcd.print(" ms"); //Display the current setting value
              break;
          }
          break;
        case 4: //Mask Time
          lcd.setCursor(1, 0); lcd.print(menuText2[4]); //Display the to be adjusted setting name
          lcd.setCursor(1, 1); lcd.print(settings[menuItem[1]].tMask); lcd.print(" "); lcd.print("ms"); //Display the current setting value
          break;
        case 5: //Chance save slot name
          lcd.setCursor(1, 0); lcd.print("Change name");
          lcd.setCursor(1, 1); lcd.print(saveSlot); lcd.print(": "); lcd.print(slotName[saveSlot]); 
          lcd.setCursor(charIndex + 4, 1); lcd.cursor();
          break;
        default:
          switch (menuItem[3])
          {
            case 4:
              lcd.setCursor(1, 0);                 lcd.print(menuText3[menuItem[3] - 1]);
              lcd.setCursor(0, 1); lcd.print(">"); lcd.print(menuText3[menuItem[3]]);
              break;
            default:
              lcd.setCursor(0, 0); lcd.print(">"); lcd.print(menuText3[menuItem[3]]);
              lcd.setCursor(1, 1);                 lcd.print(menuText3[menuItem[3] + 1]);
              break;
          }
          break;
      }
      break;

    case 4: //menuDepth 4 = "Note Value", "Note Velocity", "Controller Number", "Controller Value", " Program Number"
      //menuDepth 4 = set Program Number
      switch (menuItem[3])
      {
        case 2: //Program Change
          lcd.setCursor(1, 0); lcd.print(menuText4[10]); //Display the to be adjusted setting name
          lcd.setCursor(1, 1); lcd.print(settings[menuItem[1]].progNumber); //Display the current setting value
          break;
        case 3: //Change Load Slt
          lcd.setCursor(1, 0); lcd.print(menuText3[3]); //Display the to be adjusted setting name
          lcd.setCursor(1, 1); 
          switch (settings[menuItem[1]].loadSlot)
          {
            case numOfSlots + 1:
              lcd.print("Next Slot");
              break;
            case numOfSlots + 2:
              lcd.print("Previous Slot");
              break;
            case numOfSlots + 3:
              lcd.print("Back");
              break;
            default:
              lcd.print(settings[menuItem[1]].loadSlot); //Display the slotnumber
              lcd.print(": "); lcd.print(slotName[settings[menuItem[1]].loadSlot]);
              break;
          }
          break;
        default:
          switch (menuItem[4])
          {
            case 2:
            case 7:
              lcd.setCursor(1, 0);                 lcd.print(menuText4[menuItem[4] - 1]);
              lcd.setCursor(0, 1); lcd.print(">"); lcd.print(menuText4[menuItem[4]]);
              break;
            default:
              lcd.setCursor(0, 0); lcd.print(">"); lcd.print(menuText4[menuItem[4]]);
              lcd.setCursor(1, 1);                 lcd.print(menuText4[menuItem[4] + 1]);
              break;
          }
          break;
      }
      break;

    case 5: //menuDepth 4 = set Note Value / Note Velocity set Controller Number/Controller Value
      switch (menuItem[4])
      {
        case 0: //Note Value
          lcd.setCursor(1, 0); lcd.print(menuText4[0]); //Display the to be adjusted setting name
          lcd.setCursor(1, 1); lcd.print(settings[menuItem[1]].noteValue); //Display the current setting value
          lcd.setCursor(5, 1); lcd.print(noteName[settings[menuItem[1]].noteValue % 12]); //Also display note name
          lcd.setCursor(7, 1); lcd.print((settings[menuItem[1]].noteValue / 12) - 2); //And octave number
          break;
        case 1: //Note Velocity
          lcd.setCursor(1, 0); lcd.print(menuText4[1]); //Display the to be adjusted setting name
          lcd.setCursor(0, 1); lcd.print(settings[menuItem[1]].noteVelocity); //Display the current setting value
          if (menuItem[1] != (numOfPads + numOfExtTrig) && menuItem[1] != (numOfPads + numOfExtTrig + 1)) {lcd.setCursor(4, 1); lcd.print("0=Padsens On");}
          break;
        case 5: //Control Number
          lcd.setCursor(1, 0); lcd.print(menuText4[5]); //Display the to be adjusted setting name
          lcd.setCursor(1, 1); lcd.print(settings[menuItem[1]].ctrlNumber); //Display the current setting value
          lcd.setCursor(5, 1); lcd.print(controlName[settings[menuItem[1]].ctrlNumber]);
          break;
        case 6: //Control Value
          lcd.setCursor(1, 0); lcd.print(menuText4[6]); //Display the to be adjusted setting name
          lcd.setCursor(0, 1); lcd.print(settings[menuItem[1]].ctrlValue); //Display the current setting value
          if (menuItem[1] != (numOfPads + numOfExtTrig) && menuItem[1] != (numOfPads + numOfExtTrig + 1)) {lcd.setCursor(4, 1); lcd.print("0=Padsens On");} //If one of the footswitches are selected then don't show text
          break;
      }
      break;
  }

  //while navigating through the menu, always display which item the settings belong to, as long as it is an item and not default menu
  if(menuDepth > 1 && menuItem[1] < numOfPads + numOfExtTrig + numOfSwitch){
    lcd.setCursor(13, 0);
    lcd.print(menuText1Short[menuItem[1]]);
  }
}

//====================================================================================================
//Code to calculate compressorsettings
//====================================================================================================
void calcCompressor() {
  int maxInput = 1023; //The max value from the analog input. The Zener diode limits the max input on loud hits. Lower to send max velocity on lower values.
  byte maxOutput = 127; //The max value able to send with midi.
  
  for(int i = 0; i < numOfPads + numOfExtTrig; i++){
    PADgain[i] = maxOutput / (PADthreshold[i] + (maxInput - PADthreshold[i]) / PADratio[i]); //Calculate auto-gain settings for compressor. Only active once hit-value is above the compressor threshold.
  //    Serial.print("Pad "); //Diag
  //    Serial.print(i); //Diag
  //    Serial.print(" "); //Diag
  //    Serial.println(PADgain[i]); //Diag
  }
}

//====================================================================================================
//Code to check if a button is pressed (edge detection)
//====================================================================================================
void checkEnc() {
  FB_encNextPin.update(); //Update function block
  FB_encPrevPin.update();
  FB_encSelectPin.update();

  encNext = FB_encNextPin.fallingEdge() && FB_encPrevPin.read(); //If the encoder has one pin on falling edge and the other high then direction 1
  encPrev = FB_encPrevPin.fallingEdge() && FB_encNextPin.read(); //If the encoder has the other pin falling and the one pin high then direction 2

  if(FB_encSelectPin.fallingEdge()){
    pressTime = millis(); //Record time when switch is pressed
  }

  encLongPress = false; //Make long press low one cycle after it has been made high
  
  //If there is no long press yet, the switch is still pressed and the elapsed time is over 2000ms, long press!
  if(!encPressedLong && !FB_encSelectPin.read() && pressTime + 2000 < millis()){
    encPressedLong = true;
    encLongPress = true;
  }

  //If there is a rising edge on the switch, the switch is released. rising because of pullup resistor. Ignore if there its pressed for some time, or when is the first cycle
  encSelect = FB_encSelectPin.risingEdge() && (pressTime + 500 > millis()) && !firstScan;

  if(FB_encSelectPin.risingEdge()){
    encPressedLong = false; //If the switch is released, also release longpress
  }

  firstScan = false;
}

//====================================================================================================
//Code to fill names for Change Control midi messages
//====================================================================================================
void controlNames(){
  controlName[0] = "Bank Select";
  controlName[1] = "Mod Wheel";
  controlName[2] = "Breath ctrl";

  controlName[4] = "Foot Pedal";
  controlName[5] = "Portamen Time";
  controlName[6] = "Data Entry";
  controlName[7] = "Volume";
  controlName[8] = "Balance";

  controlName[10] = "Pan";
  controlName[11] = "Expression";
  controlName[12] = "Efx ctrl 1";
  controlName[13] = "Efx ctrl 2";
  
  controlName[64] = "Damper pdl";
  controlName[65] = "Portamento";
  controlName[66] = "Sostenuto";
  controlName[67] = "Soft Pedal";
  controlName[68] = "Legato FS";
  controlName[69] = "Hold 2";
  controlName[70] = "Sound ctrl1";
  controlName[71] = "Sound ctrl2";
  controlName[72] = "Sound ctrl3";
  controlName[73] = "Sound ctrl4";
  controlName[74] = "Sound ctrl5";
  controlName[75] = "Sound ctrl6";
  controlName[76] = "Sound ctrl7";
  controlName[77] = "Sound ctrl8";
  controlName[78] = "Sound ctrl9";
  controlName[79] = "Sound ctrlx";
  
  controlName[84] = "Portam ctrl";
  
  controlName[91] = "Efx 1 depth";
  controlName[92] = "Efx 2 depth";
  controlName[93] = "Efx 3 depth";
  controlName[94] = "Efx 4 depth";
  controlName[95] = "Efx 5 depth";
  controlName[96] = "Data increm";
  controlName[97] = "Data decrem";
  
  controlName[120] = "Sound off";
  controlName[121] = "Reset all";
  controlName[122] = "Local ctrl";
  controlName[123] = "Notes off";
  controlName[124] = "Omni off";
  controlName[125] = "Omni on";
  controlName[126] = "Mono on";
  controlName[127] = "Poly on";
}

//====================================================================================================
//Code to configure ADC's of the Teensy 4.0 for faster analog read
//====================================================================================================
void configADC() {
  /* The Default configuration samples at about 60kHz, this configuration samples at 200kHz.
  * Faster is possible, at the cost of accuracy and stability.
  * 
  * See IMXRT1060RM_rev2 manual for more information. Chapter 66.8.6 and 66.8.7.
  * ADC_CFG excists of:
  * ADC_CFG_OVWREN    //Bit    16: Data Overwrite Enable. Default disabled.
  * ADC_CFG_AVGS(0)   //Bit 15-14: Hardware Averaging. 0 = 4 samples, 1 = 8 samples, 2 = 16 samples, 3 = 32 samples. Default 0. Enable Hardware Averaging with ADC_GC_AVGE.
  * ADC_CFG_ADTRG     //Bit    13: Conversion Trigger. 0 = Software, 1 = Hardware. Default 0.
  * ADC_CFG_REFSEL(0) //Bit 12-11: Voltage Reference Selection. Default 0.
  * ADC_CFG_ADHSC     //Bit    10: High Speed Conversion. 0 = Normal, 1 = High Speed. Default 1.
  * ADC_CFG_ADSTS(2)  //Bit   9-8: Sample Time Select. 0 = 3 or 13 clocks, 1 = 5 or 17 clocks, 2 = 7 or 21 clocks, 3 = 9 or 25 clocks. Selected by ADLSMP for low or high. Default 2.
  * ADC_CFG_ADLPC     //Bit     7: Low Power Configuration. 0 = hard block not in low power, 1 = hard block in low power. Default 0.
  * ADC_CFG_ADIV(1)   //Bit   6-5: Clock Devide Select. 0 = Input clock, 1 = /2, 2 = /4, 3 = /8. 0 fast, 3 slow. Default 1.
  * ADC_CFG_ADLSMP    //Bit     4: Long Sample Time. 0 = Short, 1 = Long. See ADSTS. Default 1.
  * ADC_CFG_MODE(1)   //Bit   3-2: Conversion Mode. 0 = 8bit, 1 = 10bit, 2 = 12bit, 3 = reserved. Default 1.
  * ADC_CFG_ADICLK(3) //Bit   1-0: Input Clock Select. 0 = IPG, 1 = IPG/2, 2 = reserved, 3 = Async clock (ADACK). Default 3.
  */

  int ADC_CFG = ADC_CFG_AVGS(0)     //Hardware Averaging. Default 0
              | ADC_CFG_ADHSC       //High Speed Conversion. Default 1
              | ADC_CFG_ADSTS(0)    //Sample Time Select. Default 2
              | ADC_CFG_ADIV(0)     //Clock Devide. Default 1
  //            | ADC_CFG_ADLSMP      //Long Sample Time. Default 1
              | ADC_CFG_MODE(1)     //Conversion mode. Default 1
              | ADC_CFG_ADICLK(3);  //Input Clock. Default 3

  ADC1_CFG = ADC_CFG; //(0x00000407)
  ADC2_CFG = ADC_CFG; //(0x00000407)
  ADC1_GC  = ADC_GC_AVGE; //(0x00000020)
  ADC2_GC  = ADC_GC_AVGE; //(0x00000020)
}
