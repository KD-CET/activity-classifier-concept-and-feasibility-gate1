wrist_classifier_pamap2.ipynb - trains the classifier and exports it for the watch
  1. colab.research.google.com > File > Upload notebook > this file.
  2. Folder icon on the left > upload the 9 CSVs from data/pamap2_wrist_csv.
  3. Runtime > Run all.
  Output: leave-one-subject-out results, before/after table, never-trained-activity table,
  then downloads activity_model.h (for the watch) and replay.bin (for the SD card).
  If you retrain, replace BOTH files in firmware; they carry a matching model id.

build_wrist.py - rebuilds data from the raw PAMAP2 .dat files
  python build_wrist.py path/to/subject*.dat     (writes compressed .csv.gz files)
