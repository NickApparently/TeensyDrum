# TeensyDrum
TeensyDrum is a 6 pad Midi Drum Pad powered by a Teensy 4.0. Expanded by 2 external inputs and 2 footswitch inputs it's a gig ready device. The book size device mounts to standard drum hardware. Hits on the pads are registered by a FSR sensor where the velocity is determined by piezo elements. The 1602 LCD in combination with the encoder makes it possible to navigate through the menu and change settings. A total of 9 saveslots are available to save the different settings. Names of the slots are changable.

<img src="https://raw.githubusercontent.com/NickApparently/TeensyDrum/main/images/20220511_072148.jpg" height="300"> <img src="https://raw.githubusercontent.com/NickApparently/TeensyDrum/main/images/20221029_130430.jpg" height="300">


# PCB
The PCB contains a Teensy 4.0 on headers, using the extra pins 24-33 on the bottom. On the right-hand side a DIN-MIDI connector is added for MIDI output only. Just below that is a toothed encoder to navigate through the menu. The FSR's and Piezo's connect to the double headers on the bottom side. Left of the Teensy are the 1/4" jack sockets for external triggers and footswitches. On the far left is the JST connector for the 1602 display. The transistor is used for the brightness of the display.

<img src="https://raw.githubusercontent.com/NickApparently/TeensyDrum/main/images/20220416_152520.jpg" width="400"> <img src="https://raw.githubusercontent.com/NickApparently/TeensyDrum/main/images/20220419_194948.jpg" width="400">

# FSR and Piezo analysis
To determine a hit, the following happens:
If the difference of the FSR value is high, the scan period is activated. Within the scan period the max value of the piezo is saved. As soon as the scan period ends, a midi signal is sent. Piezo peaks outside of the scan period are ignored. New hits are ignored as long as the mask time is active. As you can see, a hit will make other piezo's vibrate as well, but since the FSR's from other pads aren't detecting a hit, these piezo's are ignored.

<img src="https://raw.githubusercontent.com/NickApparently/TeensyDrum/main/images/2022-06-18 Text 1.png" width="400"> <img src="https://raw.githubusercontent.com/NickApparently/TeensyDrum/main/images/2022-06-18 Text 2.png" width="400">

## FSR sensor
The FSR sensor is a home-made sensor. It consists of a thin PCB with open copper traces and a layer of velostat. When pressure is applied to the velostat, the resistance will drop. Together with a resistor, this is made into a voltage devider. The voltage is measured by the analog input of the Teensy.

<img src="https://content.instructables.com/FDX/41BM/KKV5OCEU/FDX41BMKKV5OCEU.png" width="150"> 
<sub>Source: https://www.instructables.com/Pressure-Sensor-to-Measure-Respiration/</sub>

The traces on the PCB are 2 combs with alternating fingers. The layer velostat is taped to the PCB using dubble sided tape. This leaves a small gap between the velostat and the PCB, resulting in a disconnected "R2". The Teensy measures 3.3v. If a pad is hit, the velostat will connect the 2 combs resulting in a quick voltage drop.

<img src="https://raw.githubusercontent.com/NickApparently/TeensyDrum/main/images/20220430_150813.jpg" height="300"> <img src="https://raw.githubusercontent.com/NickApparently/TeensyDrum/main/images/20220505_092533.jpg" height="300">

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
