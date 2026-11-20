THIGH UNIT - ESP32-WROOM + MPU-6050 in the printed box, strapped to the front of the thigh
==========================================================================================
What it does
  Reads the MPU-6050 at 40 Hz, estimates thigh tilt with a complementary filter (gyro for
  smooth short-term motion, accelerometer to stop drift), and every 1.5 s sends 14 features
  to the watch over ESP-NOW: the same 10 motion features as the wrist + tilt mean and spread
  from the filter + tilt mean and spread from the accelerometer alone.
  SENSOR FUSION on the watch: its wrist model and a thigh model each vote, the votes are
  averaged (decision-level fusion; best of the methods compared on public wrist+thigh data,
  evidence/C9_fusion/). Without thigh packets the watch uses the wrist alone (REQ-AC-10).

  thigh unit (ESP-NOW, ~1 packet / 1.5 s) --> watch --(Wi-Fi, MQTT)--> Pi
  The thigh unit never joins the Wi-Fi network: it finds the watch by itself and uses the
  watch's channel, so MQTT on the watch keeps working.

Files (keep together in this folder)
  ThighSensor.ino        the program
  thigh_fusion.h         complementary filter + tilt features (tested on a PC: tests/)
  thigh_packet.h         radio messages; SAME file as in ActivityClassifier
  activity_features.h    the 10 motion features; SAME file as in ActivityClassifier
  tests/thigh_fusion_test.cpp   filter check on a simulated thigh (g++, no hardware)


1. WIRING
---------
  GY-521 (MPU-6050)        ESP32 dev board
    VCC  ----------------  3V3
    GND  ----------------  GND
    SCL  ----------------  GPIO22
    SDA  ----------------  GPIO21
    AD0  ----------------  GND   (address 0x68; or leave unconnected, the GY-521 pulls it low)

  Battery (in the lid) -> TP4056 B+ / B-
  TP4056 OUT+ -> slide switch -> ESP32 5V (VIN) pin
  TP4056 OUT- -> ESP32 GND

  !! SAFETY: with the battery on the 5V/VIN pin, turn the SWITCH OFF before plugging the
     ESP32's USB cable in (to upload or read the Serial Monitor). With both connected, the USB
     5 V can feed back into the battery, bypassing the charger. Charge through the TP4056's own
     USB-C port.
  !! Never connect the battery to the 3V3 pin (4.2 V when full is too much for the ESP32).

  Known limit of this wiring: the dev board's regulator drops ~1 V, so from a 3.7 V LiPo the
  ESP32 gets ~2.7-3.2 V. It usually runs, but may reset when the battery is low. The firmware
  helps (ESP-NOW only, low radio power). The proper fix, already chosen on Nov 6, is an
  AP2112K-3.3 regulator to the 3V3 pin with a 470 uF capacitor (Project II PCB). For tests
  where resets would spoil the data, power the unit from a USB power bank instead.


2. UPLOAD
---------
  Arduino IDE: Tools > Board > "ESP32 Dev Module" (esp32 board package 2.x or 3.x).
  Switch OFF, plug in USB, pick the port, Upload. Serial Monitor at 115200:
    [boot] MPU-6050 ok
    [radio] thigh unit MAC ..., searching for the watch
    [radio] linked to watch ... on channel 6          <- once the watch is on
    1,linked,6,0,0,1.0123,0.0051,...                  <- one line per 1.5 s window
  Columns: window, link, channel, i2c errors in window, i2c errors total, then the 14 features.

  The watch needs its new firmware too (ActivityClassifier with activity_board.h and
  thigh_packet.h, activity_model_thigh.h). Its header pill shows WRIST+THIGH while the fusion
  is used, CAL THIGH if packets arrive but the standing pose was never recorded.


3. WEARING AND CALIBRATION
--------------------------
  Strap the box flat on the front of the thigh, about halfway between hip and knee, so it
  cannot slide or rotate.
  Stand up straight and still, then HOLD THE BOOT BUTTON FOR 2 SECONDS: the LED flashes three
  times and "[cal] standing pose saved" appears. Standing then reads ~0 deg tilt, sitting
  ~80-90 deg. The pose is saved through power-off; repeat it every time you re-strap the box.
  (Until it is done once, tilt is measured from an arbitrary pose and the packets say
  "not calibrated".)

  LED: slow blink = searching for the watch; short blink every 1.5 s = linked and sending;
       fast blink = sensor error (check the 4 jumpers). Some ELEGOO boards have no LED on
       GPIO2: then use the Serial Monitor lines instead.


4. HOW IT FITS THE CLAIMS
-------------------------
  C7  board packets reach the watch: watch Serial Monitor, "thigh" column = yes, and the
      thigh_seq numbers have no gaps (each gap = a lost packet).
  C10 board unplugged / off: within ~3.5 s the watch's pill goes back to WRIST and its reports
      are tagged "wrist" (dashboard: System health > sensors). Turn it back on: WRIST+THIGH.
  C16 sensor reads without I2C errors while walking/running: i2c_err columns (thigh Serial
      Monitor, or thigh_i2c_err in the watch's SD recording) stay 0 over a session.
  C9  wrist vs thigh vs fused: on public data, evidence/C9_fusion/. With this hardware: record
      sessions (the watch writes /fusion/S0001.csv ... on its SD card), then run
      training/train_fusion.py. It also writes activity_model_thigh.h trained on your data;
      put it in the ActivityClassifier folder to replace the public-data thigh model.
  Procedures: docs/test_procedures.md (thigh unit section).


5. SETTINGS (top of ThighSensor.ino)
------------------------------------
  TF_ALPHA 0.97 (thigh_fusion.h)   filter: higher = trust the gyro longer
  I2C_HZ 100000                    100 kHz for loose jumper wires
  TX_POWER WIFI_POWER_8_5dBm       lower radio power for the battery; raise it if packets drop
  PIN_LED 2, PIN_BTN 0             LED and BOOT button
