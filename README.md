# TeensyDrum
TeensyDrum is a 6 pad Midi Drum Pad powered by a Teensy 4.0. Expanded by 2 external inputs and 2 footswitch inputs its a gig ready device. Hits on the pads are registered by a FSR sensor where the velocity is determined by piezo elements. The 1602 LCD in combination with the encoder makes it possible to navigate through the menu and change settings. A total of 9 saveslots are available to save the different settings. Names of the slots are changable.

![](https://raw.githubusercontent.com/NickApparently/TeensyDrum/main/20220511_072148.jpg)

# FSR and Piezo analysis
> Add explanation on FSR and Piezo

# Menu
1. Pad 1
   - Command
     - Note On
       - Note Value
       - Note Velocity
       - Back
     - Control Change
       - Control Number
       - Control Value
       - Back
     - Program Change
     - Change Load Slot
     - Back
   - Channel
   - Threshold
   - Scan Time
   - Mask Time
   - Back
2. Pad 2
   - See Pad 1
3. Pad 3
4. Pad 4
5. Pad 5
6. Pad 6
7. Trigger 1
8. Trigger 2
9. Footswitch 1
   - Command (Same as Pads)
   - Channel
   - Polarity
   - Mask Time
   - Back
10. Footswitch 2
11. Brightness
12. Save Settings
13. Load Settings
14. Exit Menu
