# Data

`pamap2_wrist_csv/subject101.csv.gz` ... `subject109.csv.gz`: the wrist (hand IMU) data of the
nine PAMAP2 wearers, used to train and test the classifier (claim C2).

* Source: PAMAP2 Physical Activity Monitoring dataset, A. Reiss and D. Stricker, UCI Machine
  Learning Repository (https://archive.ics.uci.edu/dataset/231/pamap2+physical+activity+monitoring),
  licence CC BY 4.0.
* Kept: hand IMU only (dominant wrist; subject 108 left, the others right): the ±16 g
  accelerometer (m/s²) and the gyroscope (rad/s), at the original 100 Hz. The notebook converts
  to g and deg/s and resamples to the watch's 40 Hz.
* Labels: `sedentary` = lying, sitting, standing, watching TV, computer work, car driving;
  `walking`; `running`; `test_only` = stairs, vacuuming, ironing, folding laundry, house cleaning
  (never trained on, used to see what the model does with unseen activities).

To rebuild these files from the raw dataset: put the raw `subject10x.dat` files in
`data/raw/` and run `python training/build_wrist.py`.
